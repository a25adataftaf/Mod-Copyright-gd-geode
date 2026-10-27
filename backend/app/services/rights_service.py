"""Core persistence, caching, single-flight deduplication, async job, and review workflows."""

from __future__ import annotations

import asyncio
from datetime import datetime, timedelta, timezone
from typing import Any, Optional

from sqlalchemy import delete, func, or_, select
from sqlalchemy.exc import IntegrityError
from sqlalchemy.ext.asyncio import AsyncSession, async_sessionmaker
from sqlalchemy.orm import selectinload

from backend.app.config import Settings
from backend.app.db.models import (
    AuditLogRecord,
    EvidenceRecord,
    PlatformPermissionRecord,
    ResearchJobRecord,
    ReviewReportRecord,
    SongRecord,
    utc_now,
)
from backend.app.research.ai_providers import AIResearchAdapter, DailyBudgetTracker
from backend.app.research.collectors import collect_evidence
from backend.app.research.evaluator import EvaluatedSongRights, evaluate_song_rights
from backend.app.research.identity import compute_canonical_key
from backend.app.schemas.rights import (
    AdminSongOverrideRequest,
    AuditLogOut,
    ClaimRiskLevel,
    CopyrightOwnershipStatus,
    EvidenceItemOut,
    JobStatus,
    JobStatusOut,
    OverallRightsStatus,
    PermissionLevel,
    PlatformName,
    PlatformPermissionOut,
    ReportStatus,
    ReportType,
    ReviewReportCreateRequest,
    ReviewReportOut,
    SongResolveRequest,
    SongRightsRecordOut,
    SongSearchResponse,
    SourceType,
    VerificationMethod,
)
from backend.app.security.sanitizer import sanitize_untrusted_text, validate_public_http_url


def _ensure_utc(dt: Optional[datetime]) -> Optional[datetime]:
    if dt is None:
        return None
    if dt.tzinfo is None:
        return dt.replace(tzinfo=timezone.utc)
    return dt.astimezone(timezone.utc)


def serialize_song_record(song: SongRecord, now: Optional[datetime] = None) -> SongRightsRecordOut:
    ref_now = now or utc_now()
    expires = _ensure_utc(song.expires_at)
    verified_at = _ensure_utc(song.last_verified_at)
    is_stale = bool(expires is None or expires <= ref_now)

    # Sort platforms in canonical order
    order_map = {p.value: idx for idx, p in enumerate(PlatformName)}
    sorted_platforms = sorted(
        song.platform_permissions, key=lambda row: order_map.get(row.platform, 99)
    )

    return SongRightsRecordOut(
        id=song.id,
        canonical_key=song.canonical_key,
        source_type=SourceType(song.source_type),
        gd_song_id=song.gd_song_id,
        jukebox_unique_id=song.jukebox_unique_id,
        jukebox_index_id=song.jukebox_index_id,
        youtube_id=song.youtube_id,
        title=song.title,
        artist=song.artist,
        original_artist=song.original_artist,
        rights_holders=list(song.rights_holders_json or []),
        original_source_url=song.original_source_url,
        license_name=song.license_name,
        license_url=song.license_url,
        copyright_ownership_status=CopyrightOwnershipStatus(song.copyright_ownership_status),
        overall_status=OverallRightsStatus(song.overall_status),
        commercial_use_status=PermissionLevel(song.commercial_use_status),
        attribution_required=song.attribution_required,
        attribution_template=song.attribution_template,
        platforms=[
            PlatformPermissionOut(
                platform=PlatformName(p.platform),
                video_upload_permission=PermissionLevel(p.video_upload_permission),
                livestream_permission=PermissionLevel(p.livestream_permission),
                commercial_use_permission=PermissionLevel(p.commercial_use_permission),
                monetization_permission=PermissionLevel(p.monetization_permission),
                attribution_required=p.attribution_required,
                automated_claim_risk=ClaimRiskLevel(p.automated_claim_risk),
                conditions=list(p.conditions_json or []),
                restrictions=list(p.restrictions_json or []),
                policy_reference_url=p.policy_reference_url,
            )
            for p in sorted_platforms
        ],
        evidence=[
            EvidenceItemOut(
                source_kind=e.source_kind,
                source_title=e.source_title,
                source_url=e.source_url,
                fetch_status=e.fetch_status,
                excerpt=e.excerpt,
                retrieved_at=_ensure_utc(e.retrieved_at) or ref_now,
            )
            for e in song.evidence_records
        ],
        uncertainties=list(song.uncertainties_json or []),
        conflicting_information=list(song.conflicting_info_json or []),
        security_flags=list(song.security_flags_json or []),
        verification_method=VerificationMethod(song.verification_method),
        reviewer_notes=song.reviewer_notes,
        last_verified_at=verified_at,
        expires_at=expires,
        is_stale=is_stale,
    )


def _snapshot_for_audit(song: Optional[SongRecord]) -> Optional[dict[str, Any]]:
    if song is None:
        return None
    return {
        "canonical_key": song.canonical_key,
        "license_name": song.license_name,
        "overall_status": song.overall_status,
        "commercial_use_status": song.commercial_use_status,
        "verification_method": song.verification_method,
        "last_verified_at": song.last_verified_at.isoformat() if song.last_verified_at else None,
    }


class RightsService:
    """Coordinates song lookup, single-flight research, async jobs, caching, and reviews."""

    def __init__(
        self,
        session_factory: async_sessionmaker[AsyncSession],
        settings: Settings,
        ai_adapter: AIResearchAdapter,
        budget_tracker: DailyBudgetTracker,
    ) -> None:
        self._session_factory = session_factory
        self._settings = settings
        self._ai_adapter = ai_adapter
        self._budget_tracker = budget_tracker
        self._key_locks: dict[str, asyncio.Lock] = {}
        self._job_semaphore = asyncio.Semaphore(settings.max_concurrent_research_jobs)
        self._background_tasks: set[asyncio.Task[None]] = set()

        # Operational telemetry counters
        self.cache_hits = 0
        self.cache_misses = 0
        self.deduplicated_investigations = 0
        self.failed_jobs = 0

    def _lock_for_key(self, canonical_key: str) -> asyncio.Lock:
        # Prune idle locks when dictionary grows large
        if len(self._key_locks) > 4096:
            idle = [k for k, lk in self._key_locks.items() if not lk.locked()]
            for k in idle[:2048]:
                self._key_locks.pop(k, None)
        lock = self._key_locks.get(canonical_key)
        if lock is None:
            lock = asyncio.Lock()
            self._key_locks[canonical_key] = lock
        return lock

    async def _load_song_by_canonical_key(
        self, session: AsyncSession, canonical_key: str
    ) -> Optional[SongRecord]:
        stmt = (
            select(SongRecord)
            .where(SongRecord.canonical_key == canonical_key)
            .options(
                selectinload(SongRecord.platform_permissions),
                selectinload(SongRecord.evidence_records),
            )
        )
        res = await session.execute(stmt)
        return res.scalar_one_or_none()

    async def get_song_by_id(self, song_id: str) -> Optional[SongRightsRecordOut]:
        async with self._session_factory() as session:
            stmt = (
                select(SongRecord)
                .where(SongRecord.id == song_id)
                .options(
                    selectinload(SongRecord.platform_permissions),
                    selectinload(SongRecord.evidence_records),
                )
            )
            song = (await session.execute(stmt)).scalar_one_or_none()
            if song is None:
                return None
            return serialize_song_record(song)

    def _can_reuse_cached(self, song: SongRecord, force_refresh: bool, now: datetime) -> bool:
        # Never allow unauthenticated client `force_refresh` to clobber a human reviewer's override
        if song.verification_method == VerificationMethod.MANUAL_REVIEW.value:
            expires = _ensure_utc(song.expires_at)
            if expires and expires > now:
                return True

        expires = _ensure_utc(song.expires_at)
        if not force_refresh:
            return bool(expires and expires > now)

        # Enforce cooldown on client-requested force_refresh to prevent outbound amplification abuse
        verified_at = _ensure_utc(song.last_verified_at)
        if verified_at:
            age_seconds = (now - verified_at).total_seconds()
            if age_seconds < self._settings.force_refresh_cooldown_seconds:
                return True
        return False

    def _compute_expiry(self, evaluated: EvaluatedSongRights, now: datetime) -> datetime:
        if evaluated.overall_status in {
            OverallRightsStatus.PERMITTED,
            OverallRightsStatus.PERMITTED_WITH_CONDITIONS,
            OverallRightsStatus.CONDITIONAL_NON_COMMERCIAL,
        }:
            return now + timedelta(seconds=self._settings.verified_cache_ttl_seconds)
        return now + timedelta(seconds=self._settings.unverified_cache_ttl_seconds)

    async def _persist_evaluation(
        self,
        session: AsyncSession,
        canonical_key: str,
        req: SongResolveRequest,
        evaluated: EvaluatedSongRights,
        actor: str = "system:evaluator",
    ) -> SongRecord:
        now = utc_now()
        existing = await self._load_song_by_canonical_key(session, canonical_key)
        before_snap = _snapshot_for_audit(existing)

        safe_source_url = validate_public_http_url(evaluated.original_source_url, resolve_dns=False)
        safe_license_url = validate_public_http_url(evaluated.license_url, resolve_dns=False)

        if existing is None:
            song = SongRecord(
                canonical_key=canonical_key,
                source_type=req.source_type.value,
                gd_song_id=req.gd_song_id,
                jukebox_unique_id=sanitize_untrusted_text(req.jukebox_unique_id or "", 128) or None,
                jukebox_index_id=sanitize_untrusted_text(req.jukebox_index_id or "", 256) or None,
                youtube_id=req.youtube_id,
                audio_sha256=req.audio_sha256,
                title=sanitize_untrusted_text(req.title, 300),
                artist=sanitize_untrusted_text(req.artist, 300),
            )
            session.add(song)
            await session.flush()
        else:
            song = existing
            if req.gd_song_id is not None and song.gd_song_id is None:
                song.gd_song_id = req.gd_song_id
            if req.youtube_id and not song.youtube_id:
                song.youtube_id = req.youtube_id
            if req.audio_sha256 and not song.audio_sha256:
                song.audio_sha256 = req.audio_sha256

        song.original_artist = evaluated.original_artist
        song.rights_holders_json = evaluated.rights_holders
        song.original_source_url = safe_source_url
        song.license_name = evaluated.license_name
        song.license_url = safe_license_url
        song.copyright_ownership_status = evaluated.copyright_ownership_status.value
        song.overall_status = evaluated.overall_status.value
        song.commercial_use_status = evaluated.commercial_use_status.value
        song.attribution_required = evaluated.attribution_required
        song.attribution_template = evaluated.attribution_template
        song.uncertainties_json = evaluated.uncertainties
        song.conflicting_info_json = evaluated.conflicting_information
        song.security_flags_json = evaluated.security_flags
        song.verification_method = evaluated.verification_method.value
        song.ai_diagnostic_confidence = evaluated.ai_diagnostic_confidence
        song.last_verified_at = now
        song.expires_at = self._compute_expiry(evaluated, now)
        song.updated_at = now

        # Replace per-platform permission rows and evidence rows via explicit async SQL
        # to avoid synchronous ORM collection lazy-loading (MissingGreenlet) in SQLAlchemy 2.1+.
        if existing is not None:
            await session.execute(
                delete(PlatformPermissionRecord).where(PlatformPermissionRecord.song_id == song.id)
            )
            await session.execute(
                delete(EvidenceRecord).where(EvidenceRecord.song_id == song.id)
            )

        for p in evaluated.platforms:
            session.add(
                PlatformPermissionRecord(
                    song_id=song.id,
                    platform=p.platform.value,
                    video_upload_permission=p.video_upload_permission.value,
                    livestream_permission=p.livestream_permission.value,
                    commercial_use_permission=p.commercial_use_permission.value,
                    monetization_permission=p.monetization_permission.value,
                    attribution_required=p.attribution_required,
                    automated_claim_risk=p.automated_claim_risk.value,
                    conditions_json=p.conditions,
                    restrictions_json=p.restrictions,
                    policy_reference_url=validate_public_http_url(p.policy_reference_url, resolve_dns=False),
                )
            )

        for ev in evaluated.evidence:
            session.add(
                EvidenceRecord(
                    song_id=song.id,
                    source_kind=ev.source_kind,
                    source_title=sanitize_untrusted_text(ev.source_title, 300),
                    source_url=validate_public_http_url(ev.source_url, resolve_dns=False),
                    fetch_status=ev.fetch_status,
                    excerpt=sanitize_untrusted_text(ev.excerpt, 1500),
                    retrieved_at=ev.retrieved_at,
                )
            )

        session.add(
            AuditLogRecord(
                song_id=song.id,
                actor=actor,
                action="song_evaluated",
                reason=f"Evaluated via {evaluated.verification_method.value}",
                before_snapshot_json=before_snap,
                after_snapshot_json=_snapshot_for_audit(song),
            )
        )
        await session.commit()
        session.expire_all()
        return await self._load_song_by_canonical_key(session, canonical_key) or song

    async def _run_pipeline_and_save(
        self, canonical_key: str, req: SongResolveRequest, actor: str = "system:evaluator"
    ) -> SongRightsRecordOut:
        bundle = await collect_evidence(req, self._settings)
        ai_analysis = await self._ai_adapter.analyze(req, bundle)
        evaluated = evaluate_song_rights(req, bundle, ai_analysis)

        async with self._session_factory() as session:
            try:
                saved = await self._persist_evaluation(session, canonical_key, req, evaluated, actor=actor)
            except IntegrityError:
                await session.rollback()
                # Concurrent insert race safeguard: reload and update existing record
                saved = await self._persist_evaluation(session, canonical_key, req, evaluated, actor=actor)
            return serialize_song_record(saved)

    async def resolve_song(
        self, req: SongResolveRequest
    ) -> tuple[str, bool, Optional[str], Optional[SongRightsRecordOut]]:
        """
        Resolve song rights synchronously or enqueue an asynchronous deduplicated job.
        Returns (resolution_status, cached, job_id, record).
        """
        canonical_key = compute_canonical_key(req)
        now = utc_now()

        # Fast path check before acquiring per-key lock
        async with self._session_factory() as session:
            existing = await self._load_song_by_canonical_key(session, canonical_key)
            if existing is not None and self._can_reuse_cached(existing, req.force_refresh, now):
                self.cache_hits += 1
                return ("ready", True, None, serialize_song_record(existing, now=now))
            stale_snapshot = serialize_song_record(existing, now=now) if existing is not None else None

        if req.async_mode:
            job_id = await self._enqueue_or_join_async_job(canonical_key, req)
            return ("processing", False, job_id, stale_snapshot)

        # Single-flight synchronous investigation per canonical_key
        key_lock = self._lock_for_key(canonical_key)
        if key_lock.locked():
            self.deduplicated_investigations += 1
        async with key_lock:
            async with self._session_factory() as session:
                rechecked = await self._load_song_by_canonical_key(session, canonical_key)
                if rechecked is not None and self._can_reuse_cached(rechecked, req.force_refresh, utc_now()):
                    self.cache_hits += 1
                    return ("ready", True, None, serialize_song_record(rechecked))

            self.cache_misses += 1
            record_out = await self._run_pipeline_and_save(canonical_key, req)
            return ("ready", False, None, record_out)

    async def _enqueue_or_join_async_job(self, canonical_key: str, req: SongResolveRequest) -> str:
        key_lock = self._lock_for_key(f"job:{canonical_key}")
        async with key_lock:
            async with self._session_factory() as session:
                stmt = (
                    select(ResearchJobRecord)
                    .where(
                        ResearchJobRecord.canonical_key == canonical_key,
                        ResearchJobRecord.status.in_([JobStatus.QUEUED.value, JobStatus.RUNNING.value]),
                    )
                    .order_by(ResearchJobRecord.created_at.desc())
                    .limit(1)
                )
                active_job = (await session.execute(stmt)).scalar_one_or_none()
                if active_job is not None:
                    self.deduplicated_investigations += 1
                    return active_job.id

                self.cache_misses += 1
                job = ResearchJobRecord(
                    canonical_key=canonical_key,
                    status=JobStatus.QUEUED.value,
                    request_payload_json=req.model_dump(mode="json"),
                )
                session.add(job)
                await session.commit()
                job_id = job.id

        task = asyncio.create_task(self._execute_background_job(job_id, canonical_key, req))
        self._background_tasks.add(task)
        task.add_done_callback(self._background_tasks.discard)
        return job_id

    async def _execute_background_job(
        self, job_id: str, canonical_key: str, req: SongResolveRequest
    ) -> None:
        async with self._job_semaphore:
            async with self._session_factory() as session:
                job = await session.get(ResearchJobRecord, job_id)
                if job is None:
                    return
                job.status = JobStatus.RUNNING.value
                job.started_at = utc_now()
                await session.commit()

            try:
                async with self._lock_for_key(canonical_key):
                    record_out = await self._run_pipeline_and_save(canonical_key, req)
                async with self._session_factory() as session:
                    job = await session.get(ResearchJobRecord, job_id)
                    if job is not None:
                        job.status = JobStatus.COMPLETED.value
                        job.song_id = record_out.id
                        job.completed_at = utc_now()
                        await session.commit()
            except Exception as exc:  # noqa: BLE001
                self.failed_jobs += 1
                async with self._session_factory() as session:
                    job = await session.get(ResearchJobRecord, job_id)
                    if job is not None:
                        job.status = JobStatus.FAILED.value
                        job.error_message = f"Research job failed: {type(exc).__name__}"
                        job.completed_at = utc_now()
                        await session.commit()

    async def get_job_status(self, job_id: str) -> Optional[JobStatusOut]:
        async with self._session_factory() as session:
            job = await session.get(ResearchJobRecord, job_id)
            if job is None:
                return None
            return JobStatusOut(
                job_id=job.id,
                canonical_key=job.canonical_key,
                status=JobStatus(job.status),
                song_id=job.song_id,
                error_message=job.error_message,
                created_at=_ensure_utc(job.created_at) or utc_now(),
                started_at=_ensure_utc(job.started_at),
                completed_at=_ensure_utc(job.completed_at),
            )

    async def search_songs(
        self,
        *,
        query: Optional[str] = None,
        source_type: Optional[SourceType] = None,
        overall_status: Optional[OverallRightsStatus] = None,
        commercial_use_status: Optional[PermissionLevel] = None,
        platform: Optional[PlatformName] = None,
        monetization_permission: Optional[PermissionLevel] = None,
        limit: int = 25,
        offset: int = 0,
    ) -> SongSearchResponse:
        safe_limit = max(1, min(100, limit))
        safe_offset = max(0, offset)

        async with self._session_factory() as session:
            filters = []
            if query:
                cleaned_q = sanitize_untrusted_text(query, 120)
                if cleaned_q:
                    like_expr = f"%{cleaned_q}%"
                    filters.append(
                        or_(
                            SongRecord.title.ilike(like_expr),
                            SongRecord.artist.ilike(like_expr),
                            SongRecord.original_artist.ilike(like_expr),
                            SongRecord.license_name.ilike(like_expr),
                        )
                    )
            if source_type is not None:
                filters.append(SongRecord.source_type == source_type.value)
            if overall_status is not None:
                filters.append(SongRecord.overall_status == overall_status.value)
            if commercial_use_status is not None:
                filters.append(SongRecord.commercial_use_status == commercial_use_status.value)
            if platform is not None:
                subq = select(PlatformPermissionRecord.song_id).where(
                    PlatformPermissionRecord.platform == platform.value
                )
                if monetization_permission is not None:
                    subq = subq.where(
                        PlatformPermissionRecord.monetization_permission == monetization_permission.value
                    )
                filters.append(SongRecord.id.in_(subq))

            count_stmt = select(func.count(SongRecord.id))
            if filters:
                count_stmt = count_stmt.where(*filters)
            total = int((await session.execute(count_stmt)).scalar_one() or 0)

            data_stmt = (
                select(SongRecord)
                .options(
                    selectinload(SongRecord.platform_permissions),
                    selectinload(SongRecord.evidence_records),
                )
                .order_by(SongRecord.updated_at.desc())
                .limit(safe_limit)
                .offset(safe_offset)
            )
            if filters:
                data_stmt = data_stmt.where(*filters)

            rows = (await session.execute(data_stmt)).scalars().all()
            now = utc_now()
            return SongSearchResponse(
                total=total,
                limit=safe_limit,
                offset=safe_offset,
                items=[serialize_song_record(row, now=now) for row in rows],
            )

    async def submit_review_report(self, payload: ReviewReportCreateRequest) -> Optional[ReviewReportOut]:
        safe_evidence_url = None
        if payload.evidence_url:
            safe_evidence_url = validate_public_http_url(payload.evidence_url, resolve_dns=False)
            if safe_evidence_url is None:
                raise ValueError("evidence_url must be a valid public HTTP or HTTPS URL.")

        async with self._session_factory() as session:
            song = await session.get(SongRecord, payload.song_id)
            if song is None:
                return None

            report = ReviewReportRecord(
                song_id=song.id,
                report_type=payload.report_type.value,
                platform=payload.platform.value if payload.platform else None,
                description=sanitize_untrusted_text(payload.description, 2000),
                evidence_url=safe_evidence_url,
                status=ReportStatus.OPEN.value,
            )
            session.add(report)
            session.add(
                AuditLogRecord(
                    song_id=song.id,
                    actor="client:report",
                    action="review_report_submitted",
                    reason=f"{payload.report_type.value}",
                    before_snapshot_json=None,
                    after_snapshot_json={"report_type": payload.report_type.value},
                )
            )
            await session.commit()
            await session.refresh(report)
            return ReviewReportOut(
                id=report.id,
                song_id=report.song_id,
                report_type=ReportType(report.report_type),
                platform=PlatformName(report.platform) if report.platform else None,
                description=report.description,
                evidence_url=report.evidence_url,
                status=ReportStatus(report.status),
                resolution_notes=report.resolution_notes,
                created_at=_ensure_utc(report.created_at) or utc_now(),
                resolved_at=_ensure_utc(report.resolved_at),
            )

    async def list_review_reports(
        self, status_filter: Optional[ReportStatus] = None, limit: int = 50
    ) -> list[ReviewReportOut]:
        async with self._session_factory() as session:
            stmt = select(ReviewReportRecord).order_by(ReviewReportRecord.created_at.desc()).limit(
                max(1, min(200, limit))
            )
            if status_filter is not None:
                stmt = stmt.where(ReviewReportRecord.status == status_filter.value)
            rows = (await session.execute(stmt)).scalars().all()
            return [
                ReviewReportOut(
                    id=r.id,
                    song_id=r.song_id,
                    report_type=ReportType(r.report_type),
                    platform=PlatformName(r.platform) if r.platform else None,
                    description=r.description,
                    evidence_url=r.evidence_url,
                    status=ReportStatus(r.status),
                    resolution_notes=r.resolution_notes,
                    created_at=_ensure_utc(r.created_at) or utc_now(),
                    resolved_at=_ensure_utc(r.resolved_at),
                )
                for r in rows
            ]

    async def apply_admin_override(
        self, song_id: str, payload: AdminSongOverrideRequest, actor: str
    ) -> Optional[SongRightsRecordOut]:
        async with self._session_factory() as session:
            stmt = (
                select(SongRecord)
                .where(SongRecord.id == song_id)
                .options(
                    selectinload(SongRecord.platform_permissions),
                    selectinload(SongRecord.evidence_records),
                )
            )
            song = (await session.execute(stmt)).scalar_one_or_none()
            if song is None:
                return None

            before_snap = _snapshot_for_audit(song)
            now = utc_now()

            if payload.original_source_url is not None:
                validated = validate_public_http_url(payload.original_source_url, resolve_dns=False)
                if payload.original_source_url and validated is None:
                    raise ValueError("original_source_url must be a valid public HTTP(S) URL.")
                song.original_source_url = validated

            if payload.license_url is not None:
                validated_lic = validate_public_http_url(payload.license_url, resolve_dns=False)
                if payload.license_url and validated_lic is None:
                    raise ValueError("license_url must be a valid public HTTP(S) URL.")
                song.license_url = validated_lic

            if payload.original_artist is not None:
                song.original_artist = sanitize_untrusted_text(payload.original_artist, 300) or None
            if payload.rights_holders is not None:
                song.rights_holders_json = [
                    sanitize_untrusted_text(h, 200) for h in payload.rights_holders if h.strip()
                ]

            song.license_name = sanitize_untrusted_text(payload.license_name, 256)
            song.copyright_ownership_status = payload.copyright_ownership_status.value
            song.overall_status = payload.overall_status.value
            song.commercial_use_status = payload.commercial_use_status.value
            song.attribution_required = payload.attribution_required
            song.attribution_template = (
                sanitize_untrusted_text(payload.attribution_template, 2000)
                if payload.attribution_template
                else None
            )
            song.uncertainties_json = [
                sanitize_untrusted_text(u, 400) for u in payload.uncertainties if u.strip()
            ]
            song.conflicting_info_json = [
                sanitize_untrusted_text(c, 400) for c in payload.conflicting_information if c.strip()
            ]
            song.verification_method = VerificationMethod.MANUAL_REVIEW.value
            song.reviewer_notes = sanitize_untrusted_text(payload.reviewer_notes, 2000)
            song.last_verified_at = now
            song.expires_at = now + timedelta(seconds=self._settings.verified_cache_ttl_seconds)
            song.updated_at = now

            if payload.platforms is not None:
                await session.execute(
                    delete(PlatformPermissionRecord).where(
                        PlatformPermissionRecord.song_id == song.id
                    )
                )
                for p in payload.platforms:
                    session.add(
                        PlatformPermissionRecord(
                            song_id=song.id,
                            platform=p.platform.value,
                            video_upload_permission=p.video_upload_permission.value,
                            livestream_permission=p.livestream_permission.value,
                            commercial_use_permission=p.commercial_use_permission.value,
                            monetization_permission=p.monetization_permission.value,
                            attribution_required=p.attribution_required,
                            automated_claim_risk=p.automated_claim_risk.value,
                            conditions_json=[sanitize_untrusted_text(c, 300) for c in p.conditions],
                            restrictions_json=[sanitize_untrusted_text(r, 300) for r in p.restrictions],
                            policy_reference_url=validate_public_http_url(
                                p.policy_reference_url, resolve_dns=False
                            ),
                        )
                    )

            session.add(
                EvidenceRecord(
                    song_id=song.id,
                    source_kind="manual_reviewer",
                    source_title="Authorized Reviewer Verification",
                    source_url=song.license_url or song.original_source_url,
                    fetch_status="manual",
                    excerpt=song.reviewer_notes or "Manual rights review applied.",
                    retrieved_at=now,
                )
            )

            if payload.resolve_report_id:
                rep = await session.get(ReviewReportRecord, payload.resolve_report_id)
                if rep is not None and rep.song_id == song.id:
                    rep.status = ReportStatus.ACCEPTED.value
                    rep.resolution_notes = song.reviewer_notes
                    rep.resolved_at = now

            session.add(
                AuditLogRecord(
                    song_id=song.id,
                    actor=actor,
                    action="manual_override",
                    reason=song.reviewer_notes,
                    before_snapshot_json=before_snap,
                    after_snapshot_json=_snapshot_for_audit(song),
                )
            )
            canonical_key_saved = song.canonical_key
            await session.commit()
            session.expire_all()
            refreshed = await self._load_song_by_canonical_key(session, canonical_key_saved)
            return serialize_song_record(refreshed or song)

    async def revalidate_song_by_id(self, song_id: str, actor: str) -> Optional[SongRightsRecordOut]:
        async with self._session_factory() as session:
            song = await session.get(SongRecord, song_id)
            if song is None:
                return None
            req = SongResolveRequest(
                source_type=SourceType(song.source_type),
                gd_song_id=song.gd_song_id,
                jukebox_unique_id=song.jukebox_unique_id,
                jukebox_index_id=song.jukebox_index_id,
                title=song.title,
                artist=song.artist,
                source_url=song.original_source_url,
                youtube_id=song.youtube_id,
                audio_sha256=song.audio_sha256,
                force_refresh=True,
            )
            canonical_key = song.canonical_key

        async with self._lock_for_key(canonical_key):
            return await self._run_pipeline_and_save(canonical_key, req, actor=actor)

    async def get_audit_history(self, song_id: str, limit: int = 25) -> list[AuditLogOut]:
        async with self._session_factory() as session:
            stmt = (
                select(AuditLogRecord)
                .where(AuditLogRecord.song_id == song_id)
                .order_by(AuditLogRecord.id.desc())
                .limit(max(1, min(100, limit)))
            )
            rows = (await session.execute(stmt)).scalars().all()
            return [
                AuditLogOut(
                    id=r.id,
                    song_id=r.song_id,
                    actor=r.actor,
                    action=r.action,
                    reason=r.reason,
                    created_at=_ensure_utc(r.created_at) or utc_now(),
                )
                for r in rows
            ]

    async def get_operational_metrics(self, rate_limit_rejections: int) -> dict[str, Any]:
        async with self._session_factory() as session:
            total_songs = int(
                (await session.execute(select(func.count(SongRecord.id)))).scalar_one() or 0
            )
            open_reports = int(
                (
                    await session.execute(
                        select(func.count(ReviewReportRecord.id)).where(
                            ReviewReportRecord.status == ReportStatus.OPEN.value
                        )
                    )
                ).scalar_one()
                or 0
            )
            active_jobs = int(
                (
                    await session.execute(
                        select(func.count(ResearchJobRecord.id)).where(
                            ResearchJobRecord.status.in_(
                                [JobStatus.QUEUED.value, JobStatus.RUNNING.value]
                            )
                        )
                    )
                ).scalar_one()
                or 0
            )

        return {
            "total_songs": total_songs,
            "open_review_reports": open_reports,
            "active_research_jobs": active_jobs,
            "cache_hits": self.cache_hits,
            "cache_misses": self.cache_misses,
            "deduplicated_investigations": self.deduplicated_investigations,
            "failed_jobs": self.failed_jobs,
            "rate_limit_rejections": rate_limit_rejections,
            "ai_budget": self._budget_tracker.snapshot(),
        }

    async def wait_for_background_tasks(self, timeout: float = 5.0) -> None:
        if not self._background_tasks:
            return
        await asyncio.wait(self._background_tasks, timeout=timeout)
