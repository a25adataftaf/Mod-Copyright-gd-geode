"""Versioned (/api/v1) HTTP endpoints for Music Rights Checker."""

from __future__ import annotations

from typing import Any, Optional

from fastapi import APIRouter, Depends, HTTPException, Query, Request, Response, status
from sqlalchemy import text

from backend.app.schemas.rights import (
    AdminSongOverrideRequest,
    AuditLogOut,
    JobStatusOut,
    OverallRightsStatus,
    PermissionLevel,
    PlatformName,
    ReportStatus,
    ReviewReportCreateRequest,
    ReviewReportOut,
    SongResolveRequest,
    SongResolveResponse,
    SongRightsRecordOut,
    SongSearchResponse,
    SourceType,
)
from backend.app.security.auth import verify_admin_credentials
from backend.app.security.rate_limit import SlidingWindowRateLimiter
from backend.app.services.rights_service import RightsService

router = APIRouter(prefix="/api/v1", tags=["v1"])


def _client_key(request: Request) -> str:
    # Prefer direct socket client host to prevent X-Forwarded-For spoofing unless behind trusted proxy
    if request.client and request.client.host:
        return request.client.host
    return "unknown"


def _enforce_general_rate_limit(request: Request, response: Response) -> None:
    limiter: SlidingWindowRateLimiter = request.app.state.general_limiter
    decision = limiter.check(_client_key(request))
    response.headers["X-RateLimit-Remaining"] = str(decision.remaining)
    if not decision.allowed:
        raise HTTPException(
            status_code=status.HTTP_429_TOO_MANY_REQUESTS,
            detail="Rate limit exceeded. Please wait before issuing additional requests.",
            headers={"Retry-After": str(decision.retry_after_seconds)},
        )


def _enforce_report_rate_limit(request: Request, response: Response) -> None:
    limiter: SlidingWindowRateLimiter = request.app.state.report_limiter
    decision = limiter.check(_client_key(request))
    response.headers["X-RateLimit-Remaining"] = str(decision.remaining)
    if not decision.allowed:
        raise HTTPException(
            status_code=status.HTTP_429_TOO_MANY_REQUESTS,
            detail="Too many review reports submitted recently. Please wait before retrying.",
            headers={"Retry-After": str(decision.retry_after_seconds)},
        )


def _get_service(request: Request) -> RightsService:
    return request.app.state.rights_service


@router.get("/health")
async def health_check(request: Request) -> Response:
    """Service liveness and database connectivity check."""
    settings = request.app.state.settings
    session_factory = request.app.state.session_factory
    db_ok = False
    try:
        async with session_factory() as session:
            await session.execute(text("SELECT 1"))
            db_ok = True
    except Exception:  # noqa: BLE001
        db_ok = False

    payload = {
        "status": "ok" if db_ok else "degraded",
        "version": settings.app_version,
        "api_version": "v1",
        "database": "connected" if db_ok else "unavailable",
        "ai_provider_mode": settings.ai_provider if settings.ai_api_key else "deterministic_only",
    }
    import json

    return Response(
        content=json.dumps(payload),
        status_code=status.HTTP_200_OK if db_ok else status.HTTP_503_SERVICE_UNAVAILABLE,
        media_type="application/json",
    )


@router.post(
    "/songs/resolve",
    response_model=SongResolveResponse,
    dependencies=[Depends(_enforce_general_rate_limit)],
)
async def resolve_song_rights(
    payload: SongResolveRequest,
    response: Response,
    service: RightsService = Depends(_get_service),
) -> SongResolveResponse:
    """
    Identify a song and return its cached or freshly researched music rights record.
    Returns 200 OK when ready, or 202 Accepted when async_mode=true enqueues a background job.
    """
    res_status, cached, job_id, record = await service.resolve_song(payload)
    if res_status == "processing":
        response.status_code = status.HTTP_202_ACCEPTED
    return SongResolveResponse(
        resolution_status=res_status,
        cached=cached,
        job_id=job_id,
        record=record,
    )


@router.get(
    "/songs",
    response_model=SongSearchResponse,
    dependencies=[Depends(_enforce_general_rate_limit)],
)
async def search_songs(
    q: Optional[str] = Query(default=None, max_length=120),
    source_type: Optional[SourceType] = Query(default=None),
    overall_status: Optional[OverallRightsStatus] = Query(default=None),
    commercial_use_status: Optional[PermissionLevel] = Query(default=None),
    platform: Optional[PlatformName] = Query(default=None),
    monetization_permission: Optional[PermissionLevel] = Query(default=None),
    limit: int = Query(default=25, ge=1, le=100),
    offset: int = Query(default=0, ge=0),
    service: RightsService = Depends(_get_service),
) -> SongSearchResponse:
    """Search and filter shared song rights records."""
    return await service.search_songs(
        query=q,
        source_type=source_type,
        overall_status=overall_status,
        commercial_use_status=commercial_use_status,
        platform=platform,
        monetization_permission=monetization_permission,
        limit=limit,
        offset=offset,
    )


@router.get(
    "/songs/{song_id}",
    response_model=SongRightsRecordOut,
    dependencies=[Depends(_enforce_general_rate_limit)],
)
async def get_song_details(
    song_id: str,
    service: RightsService = Depends(_get_service),
) -> SongRightsRecordOut:
    record = await service.get_song_by_id(song_id)
    if record is None:
        raise HTTPException(status_code=status.HTTP_404_NOT_FOUND, detail="Song record not found.")
    return record


@router.get(
    "/songs/{song_id}/audit",
    response_model=list[AuditLogOut],
    dependencies=[Depends(_enforce_general_rate_limit)],
)
async def get_song_audit_trail(
    song_id: str,
    service: RightsService = Depends(_get_service),
) -> list[AuditLogOut]:
    record = await service.get_song_by_id(song_id)
    if record is None:
        raise HTTPException(status_code=status.HTTP_404_NOT_FOUND, detail="Song record not found.")
    return await service.get_audit_history(song_id)


@router.get(
    "/jobs/{job_id}",
    response_model=JobStatusOut,
    dependencies=[Depends(_enforce_general_rate_limit)],
)
async def get_research_job(
    job_id: str,
    service: RightsService = Depends(_get_service),
) -> JobStatusOut:
    job = await service.get_job_status(job_id)
    if job is None:
        raise HTTPException(status_code=status.HTTP_404_NOT_FOUND, detail="Research job not found.")
    return job


@router.post(
    "/reviews/reports",
    response_model=ReviewReportOut,
    status_code=status.HTTP_201_CREATED,
    dependencies=[Depends(_enforce_report_rate_limit)],
)
async def create_review_report(
    payload: ReviewReportCreateRequest,
    service: RightsService = Depends(_get_service),
) -> ReviewReportOut:
    try:
        created = await service.submit_review_report(payload)
    except ValueError as exc:
        raise HTTPException(status_code=status.HTTP_422_UNPROCESSABLE_ENTITY, detail=str(exc)) from exc
    if created is None:
        raise HTTPException(status_code=status.HTTP_404_NOT_FOUND, detail="Referenced song_id does not exist.")
    return created


@router.get("/admin/reviews", response_model=list[ReviewReportOut])
async def admin_list_reviews(
    status_filter: Optional[ReportStatus] = Query(default=None, alias="status"),
    limit: int = Query(default=50, ge=1, le=200),
    _actor: str = Depends(verify_admin_credentials),
    service: RightsService = Depends(_get_service),
) -> list[ReviewReportOut]:
    return await service.list_review_reports(status_filter=status_filter, limit=limit)


@router.patch("/admin/songs/{song_id}/override", response_model=SongRightsRecordOut)
async def admin_override_song(
    song_id: str,
    payload: AdminSongOverrideRequest,
    actor: str = Depends(verify_admin_credentials),
    service: RightsService = Depends(_get_service),
) -> SongRightsRecordOut:
    try:
        updated = await service.apply_admin_override(song_id, payload, actor=actor)
    except ValueError as exc:
        raise HTTPException(status_code=status.HTTP_422_UNPROCESSABLE_ENTITY, detail=str(exc)) from exc
    if updated is None:
        raise HTTPException(status_code=status.HTTP_404_NOT_FOUND, detail="Song record not found.")
    return updated


@router.post("/admin/songs/{song_id}/revalidate", response_model=SongRightsRecordOut)
async def admin_revalidate_song(
    song_id: str,
    actor: str = Depends(verify_admin_credentials),
    service: RightsService = Depends(_get_service),
) -> SongRightsRecordOut:
    updated = await service.revalidate_song_by_id(song_id, actor=f"{actor}:revalidate")
    if updated is None:
        raise HTTPException(status_code=status.HTTP_404_NOT_FOUND, detail="Song record not found.")
    return updated


@router.get("/admin/metrics")
async def admin_metrics(
    request: Request,
    _actor: str = Depends(verify_admin_credentials),
    service: RightsService = Depends(_get_service),
) -> dict[str, Any]:
    general_limiter: SlidingWindowRateLimiter = request.app.state.general_limiter
    report_limiter: SlidingWindowRateLimiter = request.app.state.report_limiter
    total_rl = general_limiter.rejected_count + report_limiter.rejected_count
    return await service.get_operational_metrics(rate_limit_rejections=total_rl)
