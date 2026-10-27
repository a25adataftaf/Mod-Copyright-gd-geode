"""Initial database schema for Music Rights Checker.

Revision ID: 0001_initial_schema
Revises: None
Create Date: 2026-10-27
"""

from __future__ import annotations

from typing import Sequence, Union

import sqlalchemy as sa
from alembic import op

revision: str = "0001_initial_schema"
down_revision: Union[str, None] = None
branch_labels: Union[str, Sequence[str], None] = None
depends_on: Union[str, Sequence[str], None] = None


def upgrade() -> None:
    op.create_table(
        "songs",
        sa.Column("id", sa.String(length=36), nullable=False),
        sa.Column("canonical_key", sa.String(length=255), nullable=False),
        sa.Column("source_type", sa.String(length=48), nullable=False),
        sa.Column("gd_song_id", sa.Integer(), nullable=True),
        sa.Column("jukebox_unique_id", sa.String(length=128), nullable=True),
        sa.Column("jukebox_index_id", sa.String(length=256), nullable=True),
        sa.Column("youtube_id", sa.String(length=32), nullable=True),
        sa.Column("audio_sha256", sa.String(length=64), nullable=True),
        sa.Column("title", sa.String(length=300), nullable=False),
        sa.Column("artist", sa.String(length=300), nullable=False),
        sa.Column("original_artist", sa.String(length=300), nullable=True),
        sa.Column("rights_holders_json", sa.JSON(), nullable=False),
        sa.Column("original_source_url", sa.String(length=1024), nullable=True),
        sa.Column("license_name", sa.String(length=256), nullable=False),
        sa.Column("license_url", sa.String(length=1024), nullable=True),
        sa.Column("copyright_ownership_status", sa.String(length=48), nullable=False),
        sa.Column("overall_status", sa.String(length=48), nullable=False),
        sa.Column("commercial_use_status", sa.String(length=48), nullable=False),
        sa.Column("attribution_required", sa.Boolean(), nullable=False),
        sa.Column("attribution_template", sa.Text(), nullable=True),
        sa.Column("uncertainties_json", sa.JSON(), nullable=False),
        sa.Column("conflicting_info_json", sa.JSON(), nullable=False),
        sa.Column("security_flags_json", sa.JSON(), nullable=False),
        sa.Column("verification_method", sa.String(length=48), nullable=False),
        sa.Column("reviewer_notes", sa.Text(), nullable=True),
        sa.Column("ai_diagnostic_confidence", sa.Float(), nullable=True),
        sa.Column("last_verified_at", sa.DateTime(timezone=True), nullable=True),
        sa.Column("expires_at", sa.DateTime(timezone=True), nullable=True),
        sa.Column("created_at", sa.DateTime(timezone=True), nullable=False),
        sa.Column("updated_at", sa.DateTime(timezone=True), nullable=False),
        sa.PrimaryKeyConstraint("id"),
        sa.UniqueConstraint("canonical_key", name="uq_songs_canonical_key"),
    )
    op.create_index("ix_songs_canonical_key", "songs", ["canonical_key"], unique=True)
    op.create_index("ix_songs_source_type", "songs", ["source_type"], unique=False)
    op.create_index("ix_songs_gd_song_id", "songs", ["gd_song_id"], unique=False)
    op.create_index("ix_songs_youtube_id", "songs", ["youtube_id"], unique=False)
    op.create_index("ix_songs_audio_sha256", "songs", ["audio_sha256"], unique=False)
    op.create_index("ix_songs_title", "songs", ["title"], unique=False)
    op.create_index("ix_songs_artist", "songs", ["artist"], unique=False)
    op.create_index("ix_songs_overall_status", "songs", ["overall_status"], unique=False)
    op.create_index("ix_songs_commercial_use_status", "songs", ["commercial_use_status"], unique=False)
    op.create_index("ix_songs_expires_at", "songs", ["expires_at"], unique=False)

    op.create_table(
        "platform_permissions",
        sa.Column("id", sa.Integer(), autoincrement=True, nullable=False),
        sa.Column("song_id", sa.String(length=36), nullable=False),
        sa.Column("platform", sa.String(length=32), nullable=False),
        sa.Column("video_upload_permission", sa.String(length=48), nullable=False),
        sa.Column("livestream_permission", sa.String(length=48), nullable=False),
        sa.Column("commercial_use_permission", sa.String(length=48), nullable=False),
        sa.Column("monetization_permission", sa.String(length=48), nullable=False),
        sa.Column("attribution_required", sa.Boolean(), nullable=False),
        sa.Column("automated_claim_risk", sa.String(length=32), nullable=False),
        sa.Column("conditions_json", sa.JSON(), nullable=False),
        sa.Column("restrictions_json", sa.JSON(), nullable=False),
        sa.Column("policy_reference_url", sa.String(length=1024), nullable=True),
        sa.ForeignKeyConstraint(["song_id"], ["songs.id"], ondelete="CASCADE"),
        sa.PrimaryKeyConstraint("id"),
        sa.UniqueConstraint("song_id", "platform", name="uq_platform_permissions_song_platform"),
    )
    op.create_index("ix_platform_permissions_song_id", "platform_permissions", ["song_id"], unique=False)
    op.create_index(
        "ix_platform_permissions_lookup",
        "platform_permissions",
        ["platform", "monetization_permission"],
        unique=False,
    )

    op.create_table(
        "evidence_records",
        sa.Column("id", sa.Integer(), autoincrement=True, nullable=False),
        sa.Column("song_id", sa.String(length=36), nullable=False),
        sa.Column("source_kind", sa.String(length=48), nullable=False),
        sa.Column("source_title", sa.String(length=300), nullable=False),
        sa.Column("source_url", sa.String(length=1024), nullable=True),
        sa.Column("fetch_status", sa.String(length=48), nullable=False),
        sa.Column("excerpt", sa.Text(), nullable=False),
        sa.Column("retrieved_at", sa.DateTime(timezone=True), nullable=False),
        sa.ForeignKeyConstraint(["song_id"], ["songs.id"], ondelete="CASCADE"),
        sa.PrimaryKeyConstraint("id"),
    )
    op.create_index("ix_evidence_records_song_id", "evidence_records", ["song_id"], unique=False)

    op.create_table(
        "research_jobs",
        sa.Column("id", sa.String(length=36), nullable=False),
        sa.Column("canonical_key", sa.String(length=255), nullable=False),
        sa.Column("song_id", sa.String(length=36), nullable=True),
        sa.Column("status", sa.String(length=32), nullable=False),
        sa.Column("request_payload_json", sa.JSON(), nullable=False),
        sa.Column("error_message", sa.String(length=512), nullable=True),
        sa.Column("created_at", sa.DateTime(timezone=True), nullable=False),
        sa.Column("started_at", sa.DateTime(timezone=True), nullable=True),
        sa.Column("completed_at", sa.DateTime(timezone=True), nullable=True),
        sa.ForeignKeyConstraint(["song_id"], ["songs.id"], ondelete="SET NULL"),
        sa.PrimaryKeyConstraint("id"),
    )
    op.create_index("ix_research_jobs_canonical_key", "research_jobs", ["canonical_key"], unique=False)
    op.create_index("ix_research_jobs_status", "research_jobs", ["status"], unique=False)
    op.create_index(
        "ix_research_jobs_canonical_status",
        "research_jobs",
        ["canonical_key", "status"],
        unique=False,
    )

    op.create_table(
        "review_reports",
        sa.Column("id", sa.String(length=36), nullable=False),
        sa.Column("song_id", sa.String(length=36), nullable=False),
        sa.Column("report_type", sa.String(length=48), nullable=False),
        sa.Column("platform", sa.String(length=32), nullable=True),
        sa.Column("description", sa.Text(), nullable=False),
        sa.Column("evidence_url", sa.String(length=1024), nullable=True),
        sa.Column("status", sa.String(length=32), nullable=False),
        sa.Column("resolution_notes", sa.Text(), nullable=True),
        sa.Column("created_at", sa.DateTime(timezone=True), nullable=False),
        sa.Column("resolved_at", sa.DateTime(timezone=True), nullable=True),
        sa.ForeignKeyConstraint(["song_id"], ["songs.id"], ondelete="CASCADE"),
        sa.PrimaryKeyConstraint("id"),
    )
    op.create_index("ix_review_reports_song_id", "review_reports", ["song_id"], unique=False)
    op.create_index("ix_review_reports_status", "review_reports", ["status"], unique=False)

    op.create_table(
        "audit_logs",
        sa.Column("id", sa.Integer(), autoincrement=True, nullable=False),
        sa.Column("song_id", sa.String(length=36), nullable=True),
        sa.Column("actor", sa.String(length=128), nullable=False),
        sa.Column("action", sa.String(length=128), nullable=False),
        sa.Column("reason", sa.Text(), nullable=True),
        sa.Column("before_snapshot_json", sa.JSON(), nullable=True),
        sa.Column("after_snapshot_json", sa.JSON(), nullable=True),
        sa.Column("created_at", sa.DateTime(timezone=True), nullable=False),
        sa.PrimaryKeyConstraint("id"),
    )
    op.create_index("ix_audit_logs_song_id", "audit_logs", ["song_id"], unique=False)
    op.create_index("ix_audit_logs_created_at", "audit_logs", ["created_at"], unique=False)


def downgrade() -> None:
    op.drop_index("ix_audit_logs_created_at", table_name="audit_logs")
    op.drop_index("ix_audit_logs_song_id", table_name="audit_logs")
    op.drop_table("audit_logs")

    op.drop_index("ix_review_reports_status", table_name="review_reports")
    op.drop_index("ix_review_reports_song_id", table_name="review_reports")
    op.drop_table("review_reports")

    op.drop_index("ix_research_jobs_canonical_status", table_name="research_jobs")
    op.drop_index("ix_research_jobs_status", table_name="research_jobs")
    op.drop_index("ix_research_jobs_canonical_key", table_name="research_jobs")
    op.drop_table("research_jobs")

    op.drop_index("ix_evidence_records_song_id", table_name="evidence_records")
    op.drop_table("evidence_records")

    op.drop_index("ix_platform_permissions_lookup", table_name="platform_permissions")
    op.drop_index("ix_platform_permissions_song_id", table_name="platform_permissions")
    op.drop_table("platform_permissions")

    op.drop_index("ix_songs_expires_at", table_name="songs")
    op.drop_index("ix_songs_commercial_use_status", table_name="songs")
    op.drop_index("ix_songs_overall_status", table_name="songs")
    op.drop_index("ix_songs_artist", table_name="songs")
    op.drop_index("ix_songs_title", table_name="songs")
    op.drop_index("ix_songs_audio_sha256", table_name="songs")
    op.drop_index("ix_songs_youtube_id", table_name="songs")
    op.drop_index("ix_songs_gd_song_id", table_name="songs")
    op.drop_index("ix_songs_source_type", table_name="songs")
    op.drop_index("ix_songs_canonical_key", table_name="songs")
    op.drop_table("songs")
