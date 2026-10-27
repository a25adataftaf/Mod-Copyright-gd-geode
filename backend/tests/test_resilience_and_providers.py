"""Tests for AI provider adapters, HTTP error/timeout resilience, DB failure safety, and revalidation."""

from __future__ import annotations

from pathlib import Path
from unittest.mock import AsyncMock, patch

import httpx
import pytest
from httpx import ASGITransport, AsyncClient

from backend.app.config import Settings
from backend.app.main import create_app
from backend.app.research.ai_providers import (
    ConfigurableHttpAIAdapter,
    DailyBudgetTracker,
    _parse_ai_json,
)
from backend.app.research.collectors import CollectedEvidenceItem, EvidenceBundle
from backend.app.research.identity import compute_canonical_key
from backend.app.schemas.rights import SongResolveRequest, SourceType


def test_parse_ai_json_handles_code_fences_and_clamps_confidence() -> None:
    fenced = """```json
    {
      "original_artist": "Waterflame",
      "rights_holders": ["Waterflame"],
      "detected_license_code": "CC-BY-NC-SA-3.0",
      "detected_license_name": "Creative Commons BY-NC-SA 3.0",
      "uncertainties": ["Verify external Content ID registration."],
      "conflicting_information": [],
      "diagnostic_confidence": 1.7
    }
    ```"""
    parsed = _parse_ai_json(fenced, "openai")
    assert parsed.original_artist == "Waterflame"
    assert parsed.diagnostic_confidence == 1.0
    assert parsed.provider_used == "openai"


@pytest.mark.asyncio
async def test_ai_adapter_timeout_invalid_json_and_security_quarantine() -> None:
    req = SongResolveRequest(
        source_type=SourceType.NEWGROUNDS,
        gd_song_id=12345,
        title="Test Track",
        artist="Test Artist",
    )

    # 1. Security flag skips outbound AI call completely
    flagged_bundle = EvidenceBundle(
        security_flags=["untrusted_input:instruction_override"],
        items=[],
    )
    settings_openai = Settings(ai_provider="openai", ai_api_key="sk-private-do-not-leak", daily_ai_call_budget=10)
    budget = DailyBudgetTracker(10)
    adapter = ConfigurableHttpAIAdapter(settings_openai, budget)
    res_sec = await adapter.analyze(req, flagged_bundle)
    assert res_sec is not None
    assert res_sec.provider_used == "skipped_security_guard"
    assert budget.snapshot()["calls_today"] == 0

    # 2. Simulated provider timeout degrades gracefully and never leaks API key
    clean_bundle = EvidenceBundle(
        items=[
            CollectedEvidenceItem(
                source_kind="song_page",
                source_title="Page",
                source_url="https://example.com",
                fetch_status="verified",
                excerpt="Sample excerpt",
            )
        ]
    )
    with patch("httpx.AsyncClient.post", new_callable=AsyncMock) as mock_post:
        mock_post.side_effect = httpx.ReadTimeout("Timed out")
        res_timeout = await adapter.analyze(req, clean_bundle)
        assert res_timeout is not None
        assert res_timeout.provider_used == "openai_fallback"
        assert all("sk-private" not in u for u in res_timeout.uncertainties)

    # 3. Simulated malformed non-JSON response from Gemini provider degrades safely
    settings_gemini = Settings(ai_provider="gemini", ai_api_key="gem-secret-key", daily_ai_call_budget=10)
    gem_adapter = ConfigurableHttpAIAdapter(settings_gemini, DailyBudgetTracker(10))
    fake_resp = httpx.Response(
        200,
        json={"candidates": [{"content": {"parts": [{"text": "NOT VALID JSON {{{"}]}}]},
        request=httpx.Request("POST", "https://generativelanguage.googleapis.com/v1beta/models/test"),
    )
    with patch("httpx.AsyncClient.post", new_callable=AsyncMock, return_value=fake_resp):
        res_bad_json = await gem_adapter.analyze(req, clean_bundle)
        assert res_bad_json is not None
        assert res_bad_json.provider_used == "gemini_fallback"


@pytest.mark.asyncio
async def test_admin_disabled_when_unconfigured_and_db_degraded_health(tmp_path: Path) -> None:
    db_file = tmp_path / "degrade.db"
    settings = Settings(
        environment="test",
        database_url=f"sqlite+aiosqlite:///{db_file}",
        sync_database_url=f"sqlite:///{db_file}",
        admin_api_key="",  # Unconfigured admin key must fail closed with 503
        allow_live_http_evidence=False,
    )
    app = create_app(settings=settings)
    async with app.router.lifespan_context(app):
        async with AsyncClient(transport=ASGITransport(app=app), base_url="http://test") as client:
            # Admin endpoints fail closed with 503 when MRC_ADMIN_API_KEY is unset
            adm = await client.get(
                "/api/v1/admin/metrics", headers={"Authorization": "Bearer anything"}
            )
            assert adm.status_code == 503

            # Simulate database failure on session execution -> /health reports 503 degraded safely
            class BrokenSession:
                async def __aenter__(self) -> "BrokenSession":
                    raise RuntimeError("Simulated DB connection refusal: secret_password_123")

                async def __aexit__(self, *args: object) -> None:
                    return None

            orig_factory = app.state.session_factory
            app.state.session_factory = lambda: BrokenSession()
            try:
                h_resp = await client.get("/api/v1/health")
                assert h_resp.status_code == 503
                assert h_resp.json()["status"] == "degraded"
                assert "secret_password_123" not in h_resp.text
            finally:
                app.state.session_factory = orig_factory


@pytest.mark.asyncio
async def test_admin_revalidation_and_canonical_deduplication(tmp_path: Path) -> None:
    # Verify metadata normalization produces identical canonical keys
    k1 = compute_canonical_key(
        SongResolveRequest(
            source_type=SourceType.JUKEBOX_LOCAL,
            title="  Clubstep   VIP! ",
            artist="DJ-Nate",
        )
    )
    k2 = compute_canonical_key(
        SongResolveRequest(
            source_type=SourceType.JUKEBOX_LOCAL,
            title="clubstep vip",
            artist="  dj nate ",
        )
    )
    assert k1 == k2

    db_file = tmp_path / "reval.db"
    settings = Settings(
        environment="test",
        database_url=f"sqlite+aiosqlite:///{db_file}",
        sync_database_url=f"sqlite:///{db_file}",
        admin_api_key="admin-reval-key-999999999999",
        allow_live_http_evidence=False,
    )
    app = create_app(settings=settings)
    async with app.router.lifespan_context(app):
        async with AsyncClient(transport=ASGITransport(app=app), base_url="http://test") as client:
            created = await client.post(
                "/api/v1/songs/resolve",
                json={
                    "source_type": "ncs_library",
                    "gd_song_id": 10007777,
                    "title": "On & On",
                    "artist": "Cartoon",
                },
            )
            song_id = created.json()["record"]["id"]
            reval = await client.post(
                f"/api/v1/admin/songs/{song_id}/revalidate",
                headers={"X-MRC-Admin-Key": "admin-reval-key-999999999999"},
            )
            assert reval.status_code == 200
            assert reval.json()["id"] == song_id
