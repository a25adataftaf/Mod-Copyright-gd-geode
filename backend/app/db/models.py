"""SQLAlchemy 2.0+ relational models for Music Rights Checker."""

from __future__ import annotations

import uuid
from datetime import datetime, timezone
from typing import Any, Optional

from sqlalchemy import (
    JSON,
    Boolean,
    DateTime,
    Float,
    ForeignKey,
    Index,
    Integer,
    String,
    Text,
    UniqueConstraint,
)
from sqlalchemy.orm import DeclarativeBase, Mapped, mapped_column, relationship


def utc_now() -> datetime:
    return datetime.now(timezone.utc)


def new_uuid() -> str:
    return str(uuid.uuid4())


class Base(DeclarativeBase):
    pass


class SongRecord(Base):
    """Canonical song rights record shared across clients."""

    __tablename__ = "songs"

    id: Mapped[str] = mapped_column(String(36), primary_key=True, default=new_uuid)
    canonical_key: Mapped[str] = mapped_column(String(255), unique=True, index=True, nullable=False)
    source_type: Mapped[str] = mapped_column(String(48), index=True, nullable=False)
    gd_song_id: Mapped[Optional[int]] = mapped_column(Integer, index=True, nullable=True)
    jukebox_unique_id: Mapped[Optional[str]] = mapped_column(String(128), nullable=True)
    jukebox_index_id: Mapped[Optional[str]] = mapped_column(String(256), nullable=True)
    youtube_id: Mapped[Optional[str]] = mapped_column(String(32), index=True, nullable=True)
    audio_sha256: Mapped[Optional[str]] = mapped_column(String(64), index=True, nullable=True)

    title: Mapped[str] = mapped_column(String(300), index=True, nullable=False)
    artist: Mapped[str] = mapped_column(String(300), index=True, nullable=False)
    original_artist: Mapped[Optional[str]] = mapped_column(String(300), nullable=True)
    rights_holders_json: Mapped[list[str]] = mapped_column(JSON, default=list, nullable=False)
    original_source_url: Mapped[Optional[str]] = mapped_column(String(1024), nullable=True)

    license_name: Mapped[str] = mapped_column(String(256), nullable=False, default="Unknown")
    license_url: Mapped[Optional[str]] = mapped_column(String(1024), nullable=True)
    copyright_ownership_status: Mapped[str] = mapped_column(
        String(48), nullable=False, default="unknown"
    )
    overall_status: Mapped[str] = mapped_column(
        String(48), index=True, nullable=False, default="unknown"
    )
    commercial_use_status: Mapped[str] = mapped_column(
        String(48), index=True, nullable=False, default="unknown"
    )

    attribution_required: Mapped[bool] = mapped_column(Boolean, nullable=False, default=False)
    attribution_template: Mapped[Optional[str]] = mapped_column(Text, nullable=True)

    uncertainties_json: Mapped[list[str]] = mapped_column(JSON, default=list, nullable=False)
    conflicting_info_json: Mapped[list[str]] = mapped_column(JSON, default=list, nullable=False)
    security_flags_json: Mapped[list[str]] = mapped_column(JSON, default=list, nullable=False)

    verification_method: Mapped[str] = mapped_column(
        String(48), nullable=False, default="conservative_fallback"
    )
    reviewer_notes: Mapped[Optional[str]] = mapped_column(Text, nullable=True)
    ai_diagnostic_confidence: Mapped[Optional[float]] = mapped_column(Float, nullable=True)

    last_verified_at: Mapped[Optional[datetime]] = mapped_column(DateTime(timezone=True), nullable=True)
    expires_at: Mapped[Optional[datetime]] = mapped_column(
        DateTime(timezone=True), index=True, nullable=True
    )
    created_at: Mapped[datetime] = mapped_column(
        DateTime(timezone=True), default=utc_now, nullable=False
    )
    updated_at: Mapped[datetime] = mapped_column(
        DateTime(timezone=True), default=utc_now, onupdate=utc_now, nullable=False
    )

    platform_permissions: Mapped[list["PlatformPermissionRecord"]] = relationship(
        back_populates="song",
        cascade="all, delete-orphan",
        lazy="selectin",
    )
    evidence_records: Mapped[list["EvidenceRecord"]] = relationship(
        back_populates="song",
        cascade="all, delete-orphan",
        lazy="selectin",
    )
    review_reports: Mapped[list["ReviewReportRecord"]] = relationship(
        back_populates="song",
        cascade="all, delete-orphan",
        lazy="selectin",
    )


class PlatformPermissionRecord(Base):
    """Per-platform usage and monetization evaluation for a song."""

    __tablename__ = "platform_permissions"
    __table_args__ = (
        UniqueConstraint("song_id", "platform", name="uq_platform_permissions_song_platform"),
        Index("ix_platform_permissions_lookup", "platform", "monetization_permission"),
    )

    id: Mapped[int] = mapped_column(Integer, primary_key=True, autoincrement=True)
    song_id: Mapped[str] = mapped_column(
        String(36), ForeignKey("songs.id", ondelete="CASCADE"), index=True, nullable=False
    )
    platform: Mapped[str] = mapped_column(String(32), nullable=False)
    video_upload_permission: Mapped[str] = mapped_column(String(48), nullable=False, default="unknown")
    livestream_permission: Mapped[str] = mapped_column(String(48), nullable=False, default="unknown")
    commercial_use_permission: Mapped[str] = mapped_column(String(48), nullable=False, default="unknown")
    monetization_permission: Mapped[str] = mapped_column(String(48), nullable=False, default="unknown")
    attribution_required: Mapped[bool] = mapped_column(Boolean, nullable=False, default=False)
    automated_claim_risk: Mapped[str] = mapped_column(String(32), nullable=False, default="unknown")
    conditions_json: Mapped[list[str]] = mapped_column(JSON, default=list, nullable=False)
    restrictions_json: Mapped[list[str]] = mapped_column(JSON, default=list, nullable=False)
    policy_reference_url: Mapped[Optional[str]] = mapped_column(String(1024), nullable=True)

    song: Mapped["SongRecord"] = relationship(back_populates="platform_permissions")


class EvidenceRecord(Base):
    """Verifiable source evidence supporting a song's rights classification."""

    __tablename__ = "evidence_records"

    id: Mapped[int] = mapped_column(Integer, primary_key=True, autoincrement=True)
    song_id: Mapped[str] = mapped_column(
        String(36), ForeignKey("songs.id", ondelete="CASCADE"), index=True, nullable=False
    )
    source_kind: Mapped[str] = mapped_column(String(48), nullable=False)
    source_title: Mapped[str] = mapped_column(String(300), nullable=False)
    source_url: Mapped[Optional[str]] = mapped_column(String(1024), nullable=True)
    fetch_status: Mapped[str] = mapped_column(String(48), nullable=False)
    excerpt: Mapped[str] = mapped_column(Text, nullable=False)
    retrieved_at: Mapped[datetime] = mapped_column(
        DateTime(timezone=True), default=utc_now, nullable=False
    )

    song: Mapped["SongRecord"] = relationship(back_populates="evidence_records")


class ResearchJobRecord(Base):
    """Asynchronous research task record used for single-flight deduplication and status polling."""

    __tablename__ = "research_jobs"
    __table_args__ = (
        Index("ix_research_jobs_canonical_status", "canonical_key", "status"),
    )

    id: Mapped[str] = mapped_column(String(36), primary_key=True, default=new_uuid)
    canonical_key: Mapped[str] = mapped_column(String(255), index=True, nullable=False)
    song_id: Mapped[Optional[str]] = mapped_column(
        String(36), ForeignKey("songs.id", ondelete="SET NULL"), nullable=True
    )
    status: Mapped[str] = mapped_column(String(32), index=True, nullable=False, default="queued")
    request_payload_json: Mapped[dict[str, Any]] = mapped_column(JSON, default=dict, nullable=False)
    error_message: Mapped[Optional[str]] = mapped_column(String(512), nullable=True)
    created_at: Mapped[datetime] = mapped_column(
        DateTime(timezone=True), default=utc_now, nullable=False
    )
    started_at: Mapped[Optional[datetime]] = mapped_column(DateTime(timezone=True), nullable=True)
    completed_at: Mapped[Optional[datetime]] = mapped_column(DateTime(timezone=True), nullable=True)


class ReviewReportRecord(Base):
    """User or creator report regarding claims, outdated licences, or metadata mismatches."""

    __tablename__ = "review_reports"

    id: Mapped[str] = mapped_column(String(36), primary_key=True, default=new_uuid)
    song_id: Mapped[str] = mapped_column(
        String(36), ForeignKey("songs.id", ondelete="CASCADE"), index=True, nullable=False
    )
    report_type: Mapped[str] = mapped_column(String(48), nullable=False)
    platform: Mapped[Optional[str]] = mapped_column(String(32), nullable=True)
    description: Mapped[str] = mapped_column(Text, nullable=False)
    evidence_url: Mapped[Optional[str]] = mapped_column(String(1024), nullable=True)
    status: Mapped[str] = mapped_column(String(32), index=True, nullable=False, default="open")
    resolution_notes: Mapped[Optional[str]] = mapped_column(Text, nullable=True)
    created_at: Mapped[datetime] = mapped_column(
        DateTime(timezone=True), default=utc_now, nullable=False
    )
    resolved_at: Mapped[Optional[datetime]] = mapped_column(DateTime(timezone=True), nullable=True)

    song: Mapped["SongRecord"] = relationship(back_populates="review_reports")


class AuditLogRecord(Base):
    """Immutable audit log recording automated evaluations, revalidations, and manual overrides."""

    __tablename__ = "audit_logs"

    id: Mapped[int] = mapped_column(Integer, primary_key=True, autoincrement=True)
    song_id: Mapped[Optional[str]] = mapped_column(String(36), index=True, nullable=True)
    actor: Mapped[str] = mapped_column(String(128), nullable=False)
    action: Mapped[str] = mapped_column(String(128), nullable=False)
    reason: Mapped[Optional[str]] = mapped_column(Text, nullable=True)
    before_snapshot_json: Mapped[Optional[dict[str, Any]]] = mapped_column(JSON, nullable=True)
    after_snapshot_json: Mapped[Optional[dict[str, Any]]] = mapped_column(JSON, nullable=True)
    created_at: Mapped[datetime] = mapped_column(
        DateTime(timezone=True), default=utc_now, index=True, nullable=False
    )
