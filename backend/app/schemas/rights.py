"""Domain enumerations and Pydantic v2 API contracts for Music Rights Checker."""

from __future__ import annotations

from datetime import datetime
from enum import StrEnum
from typing import Optional

from pydantic import BaseModel, ConfigDict, Field, field_validator


CLAIM_DISCLAIMER = (
    "Informational guidance only (not legal advice). Even when music use is permitted by a "
    "licence, automated detection systems (such as YouTube Content ID, Twitch Audible Magic VOD "
    "scanning, TikTok audio filters, or Meta Rights Manager) may still issue claims or mutes, "
    "and lack of an automated claim never proves legal authorization."
)


class SourceType(StrEnum):
    ROBTOP_OFFICIAL = "robtop_official"
    GD_MUSIC_LIBRARY = "gd_music_library"
    NCS_LIBRARY = "ncs_library"
    NEWGROUNDS = "newgrounds"
    JUKEBOX_LOCAL = "jukebox_local"
    JUKEBOX_YOUTUBE = "jukebox_youtube"
    JUKEBOX_HOSTED = "jukebox_hosted"
    CUSTOM_LIBRARY = "custom_library"


class PlatformName(StrEnum):
    YOUTUBE = "youtube"
    TIKTOK = "tiktok"
    TWITCH = "twitch"
    INSTAGRAM = "instagram"
    FACEBOOK = "facebook"


ALL_PLATFORMS: tuple[PlatformName, ...] = (
    PlatformName.YOUTUBE,
    PlatformName.TIKTOK,
    PlatformName.TWITCH,
    PlatformName.INSTAGRAM,
    PlatformName.FACEBOOK,
)


class OverallRightsStatus(StrEnum):
    PERMITTED = "permitted"
    PERMITTED_WITH_CONDITIONS = "permitted_with_conditions"
    CONDITIONAL_NON_COMMERCIAL = "conditional_non_commercial"
    RESTRICTED = "restricted"
    REVIEW_REQUIRED = "review_required"
    UNKNOWN = "unknown"


class PermissionLevel(StrEnum):
    ALLOWED = "allowed"
    CONDITIONAL = "conditional"
    CONDITIONAL_REVENUE_SHARE = "conditional_revenue_share"
    NOT_ALLOWED = "not_allowed"
    UNKNOWN = "unknown"


class CopyrightOwnershipStatus(StrEnum):
    COPYRIGHTED = "copyrighted"
    CREATIVE_COMMONS = "creative_commons"
    PUBLIC_DOMAIN_VERIFIED = "public_domain_verified"
    UNKNOWN = "unknown"


class ClaimRiskLevel(StrEnum):
    LOW = "low"
    MODERATE = "moderate"
    HIGH = "high"
    UNKNOWN = "unknown"


class VerificationMethod(StrEnum):
    OFFICIAL_SOURCE = "official_source"
    AI_ASSISTED_EVIDENCE = "ai_assisted_evidence"
    MANUAL_REVIEW = "manual_review"
    CONSERVATIVE_FALLBACK = "conservative_fallback"


class JobStatus(StrEnum):
    QUEUED = "queued"
    RUNNING = "running"
    COMPLETED = "completed"
    FAILED = "failed"


class ReportType(StrEnum):
    COPYRIGHT_CLAIM_RECEIVED = "copyright_claim_received"
    INCORRECT_LICENSE = "incorrect_license"
    WRONG_SONG_MATCH = "wrong_song_match"
    SOURCE_UPDATED = "source_updated"
    OTHER = "other"


class ReportStatus(StrEnum):
    OPEN = "open"
    ACCEPTED = "accepted"
    REJECTED = "rejected"


class SongResolveRequest(BaseModel):
    """Input payload sent by the Geode mod or API consumer to identify and check a song."""

    model_config = ConfigDict(extra="forbid")

    source_type: SourceType
    gd_song_id: Optional[int] = Field(default=None, ge=-1_000_000, le=2_147_483_647)
    jukebox_unique_id: Optional[str] = Field(default=None, max_length=128)
    jukebox_index_id: Optional[str] = Field(default=None, max_length=256)
    title: str = Field(min_length=1, max_length=300)
    artist: str = Field(default="Unknown Artist", max_length=300)
    source_url: Optional[str] = Field(default=None, max_length=1024)
    youtube_id: Optional[str] = Field(default=None, max_length=32)
    audio_sha256: Optional[str] = Field(default=None, min_length=64, max_length=64)
    html_evidence_override: Optional[str] = Field(
        default=None,
        max_length=131_072,
        description="Optional client/reviewer-supplied official page snapshot treated as untrusted text.",
    )
    async_mode: bool = Field(
        default=False,
        description="If true and record is uncached/stale, enqueue background research and return 202.",
    )
    force_refresh: bool = Field(default=False)

    @field_validator("title", "artist")
    @classmethod
    def _strip_non_empty(cls, val: str) -> str:
        cleaned = " ".join(val.split()).strip()
        if not cleaned:
            raise ValueError("Field cannot be blank or whitespace only.")
        return cleaned

    @field_validator("audio_sha256")
    @classmethod
    def _validate_sha256(cls, val: Optional[str]) -> Optional[str]:
        if val is None:
            return None
        lower = val.strip().lower()
        if len(lower) != 64 or any(ch not in "0123456789abcdef" for ch in lower):
            raise ValueError("audio_sha256 must be a 64-character lowercase hexadecimal SHA-256 digest.")
        return lower

    @field_validator("youtube_id")
    @classmethod
    def _validate_youtube_id(cls, val: Optional[str]) -> Optional[str]:
        if val is None:
            return None
        cleaned = val.strip()
        if not cleaned:
            return None
        allowed = set("abcdefghijklmnopqrstuvwxyzABCDEFGHIJKLMNOPQRSTUVWXYZ0123456789_-")
        if len(cleaned) > 20 or any(ch not in allowed for ch in cleaned):
            raise ValueError("Invalid YouTube video identifier.")
        return cleaned


class PlatformPermissionOut(BaseModel):
    platform: PlatformName
    video_upload_permission: PermissionLevel
    livestream_permission: PermissionLevel
    commercial_use_permission: PermissionLevel
    monetization_permission: PermissionLevel
    attribution_required: bool
    automated_claim_risk: ClaimRiskLevel
    conditions: list[str] = Field(default_factory=list)
    restrictions: list[str] = Field(default_factory=list)
    policy_reference_url: Optional[str] = None


class EvidenceItemOut(BaseModel):
    source_kind: str
    source_title: str
    source_url: Optional[str] = None
    fetch_status: str
    excerpt: str
    retrieved_at: datetime


class SongRightsRecordOut(BaseModel):
    id: str
    canonical_key: str
    source_type: SourceType
    gd_song_id: Optional[int] = None
    jukebox_unique_id: Optional[str] = None
    jukebox_index_id: Optional[str] = None
    youtube_id: Optional[str] = None
    title: str
    artist: str
    original_artist: Optional[str] = None
    rights_holders: list[str] = Field(default_factory=list)
    original_source_url: Optional[str] = None
    license_name: str
    license_url: Optional[str] = None
    copyright_ownership_status: CopyrightOwnershipStatus
    overall_status: OverallRightsStatus
    commercial_use_status: PermissionLevel
    attribution_required: bool
    attribution_template: Optional[str] = None
    platforms: list[PlatformPermissionOut] = Field(default_factory=list)
    evidence: list[EvidenceItemOut] = Field(default_factory=list)
    uncertainties: list[str] = Field(default_factory=list)
    conflicting_information: list[str] = Field(default_factory=list)
    security_flags: list[str] = Field(default_factory=list)
    verification_method: VerificationMethod
    reviewer_notes: Optional[str] = None
    last_verified_at: Optional[datetime] = None
    expires_at: Optional[datetime] = None
    is_stale: bool = False
    disclaimer: str = CLAIM_DISCLAIMER


class SongResolveResponse(BaseModel):
    resolution_status: str  # "ready" | "processing"
    cached: bool
    job_id: Optional[str] = None
    record: Optional[SongRightsRecordOut] = None


class SongSearchResponse(BaseModel):
    total: int
    limit: int
    offset: int
    items: list[SongRightsRecordOut]


class JobStatusOut(BaseModel):
    job_id: str
    canonical_key: str
    status: JobStatus
    song_id: Optional[str] = None
    error_message: Optional[str] = None
    created_at: datetime
    started_at: Optional[datetime] = None
    completed_at: Optional[datetime] = None


class ReviewReportCreateRequest(BaseModel):
    model_config = ConfigDict(extra="forbid")

    song_id: str = Field(min_length=8, max_length=64)
    report_type: ReportType
    platform: Optional[PlatformName] = None
    description: str = Field(min_length=10, max_length=2000)
    evidence_url: Optional[str] = Field(default=None, max_length=1024)


class ReviewReportOut(BaseModel):
    id: str
    song_id: str
    report_type: ReportType
    platform: Optional[PlatformName] = None
    description: str
    evidence_url: Optional[str] = None
    status: ReportStatus
    resolution_notes: Optional[str] = None
    created_at: datetime
    resolved_at: Optional[datetime] = None


class PlatformOverrideInput(BaseModel):
    model_config = ConfigDict(extra="forbid")

    platform: PlatformName
    video_upload_permission: PermissionLevel
    livestream_permission: PermissionLevel
    commercial_use_permission: PermissionLevel
    monetization_permission: PermissionLevel
    attribution_required: bool
    automated_claim_risk: ClaimRiskLevel
    conditions: list[str] = Field(default_factory=list)
    restrictions: list[str] = Field(default_factory=list)
    policy_reference_url: Optional[str] = Field(default=None, max_length=1024)


class AdminSongOverrideRequest(BaseModel):
    model_config = ConfigDict(extra="forbid")

    original_artist: Optional[str] = Field(default=None, max_length=300)
    rights_holders: Optional[list[str]] = None
    original_source_url: Optional[str] = Field(default=None, max_length=1024)
    license_name: str = Field(min_length=2, max_length=256)
    license_url: Optional[str] = Field(default=None, max_length=1024)
    copyright_ownership_status: CopyrightOwnershipStatus
    overall_status: OverallRightsStatus
    commercial_use_status: PermissionLevel
    attribution_required: bool
    attribution_template: Optional[str] = Field(default=None, max_length=2000)
    uncertainties: list[str] = Field(default_factory=list)
    conflicting_information: list[str] = Field(default_factory=list)
    platforms: Optional[list[PlatformOverrideInput]] = None
    reviewer_notes: str = Field(min_length=5, max_length=2000)
    resolve_report_id: Optional[str] = Field(default=None, max_length=64)


class AuditLogOut(BaseModel):
    id: int
    song_id: Optional[str] = None
    actor: str
    action: str
    reason: Optional[str] = None
    created_at: datetime
