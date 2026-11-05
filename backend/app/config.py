"""Runtime configuration loaded from environment variables with safe defaults."""

from __future__ import annotations

import os
from dataclasses import dataclass

from backend.app import __version__


def _env_bool(name: str, default: bool) -> bool:
    raw = os.getenv(name)
    if raw is None:
        return default
    return raw.strip().lower() in {"1", "true", "yes", "on"}


def _env_int(name: str, default: int, minimum: int = 1) -> int:
    raw = os.getenv(name)
    if raw is None:
        return default
    try:
        parsed = int(raw.strip())
    except ValueError:
        return default
    return max(minimum, parsed)


def _env_float(name: str, default: float, minimum: float = 0.5) -> float:
    raw = os.getenv(name)
    if raw is None:
        return default
    try:
        parsed = float(raw.strip())
    except ValueError:
        return default
    return max(minimum, parsed)


@dataclass(frozen=True)
class Settings:
    """Immutable application settings."""

    app_name: str = "Music Rights Checker API"
    app_version: str = __version__
    api_prefix: str = "/api/v1"
    environment: str = "development"
    database_url: str = "sqlite+aiosqlite:///./data/mrc.db"
    sync_database_url: str = "sqlite:///./data/mrc.db"

    # Security and rate limiting
    admin_api_key: str = ""
    rate_limit_requests_per_minute: int = 60
    report_rate_limit_per_minute: int = 10
    max_concurrent_research_jobs: int = 4

    # Cache and revalidation windows
    verified_cache_ttl_seconds: int = 604800  # 7 days
    unverified_cache_ttl_seconds: int = 86400  # 24 hours
    force_refresh_cooldown_seconds: int = 300  # 5 minutes

    # Evidence fetching safety boundaries
    allow_live_http_evidence: bool = True
    http_fetch_timeout_seconds: float = 8.0
    max_evidence_bytes: int = 262_144  # 256 KiB cap on untrusted remote responses

    # AI provider settings (credentials NEVER leave backend)
    ai_provider: str = "none"  # none | openai | anthropic | gemini
    ai_api_key: str = ""
    ai_model: str = ""
    ai_base_url: str = ""
    ai_timeout_seconds: float = 15.0
    daily_ai_call_budget: int = 250

    @classmethod
    def from_env(cls) -> "Settings":
        db_url = os.getenv("MRC_DATABASE_URL", "sqlite+aiosqlite:///./data/mrc.db").strip()
        sync_db_url = os.getenv("MRC_SYNC_DATABASE_URL", "").strip()
        if not sync_db_url:
            sync_db_url = db_url.replace("sqlite+aiosqlite://", "sqlite://").replace(
                "postgresql+asyncpg://", "postgresql://"
            )
        return cls(
            environment=os.getenv("MRC_ENV", "development").strip() or "development",
            database_url=db_url,
            sync_database_url=sync_db_url,
            admin_api_key=os.getenv("MRC_ADMIN_API_KEY", "").strip(),
            rate_limit_requests_per_minute=_env_int("MRC_RATE_LIMIT_REQUESTS_PER_MINUTE", 60),
            report_rate_limit_per_minute=_env_int("MRC_REPORT_RATE_LIMIT_PER_MINUTE", 10),
            max_concurrent_research_jobs=_env_int("MRC_MAX_CONCURRENT_RESEARCH_JOBS", 4),
            verified_cache_ttl_seconds=_env_int("MRC_VERIFIED_CACHE_TTL_SECONDS", 604800, minimum=60),
            unverified_cache_ttl_seconds=_env_int("MRC_UNVERIFIED_CACHE_TTL_SECONDS", 86400, minimum=60),
            force_refresh_cooldown_seconds=_env_int("MRC_FORCE_REFRESH_COOLDOWN_SECONDS", 300, minimum=0),
            allow_live_http_evidence=_env_bool("MRC_ALLOW_LIVE_HTTP_EVIDENCE", True),
            http_fetch_timeout_seconds=_env_float("MRC_HTTP_FETCH_TIMEOUT_SECONDS", 8.0),
            max_evidence_bytes=_env_int("MRC_MAX_EVIDENCE_BYTES", 262_144, minimum=4096),
            ai_provider=os.getenv("MRC_AI_PROVIDER", "none").strip().lower() or "none",
            ai_api_key=os.getenv("MRC_AI_API_KEY", "").strip(),
            ai_model=os.getenv("MRC_AI_MODEL", "").strip(),
            ai_base_url=os.getenv("MRC_AI_BASE_URL", "").strip(),
            ai_timeout_seconds=_env_float("MRC_AI_TIMEOUT_SECONDS", 15.0),
            daily_ai_call_budget=_env_int("MRC_DAILY_AI_CALL_BUDGET", 250, minimum=0),
        )
