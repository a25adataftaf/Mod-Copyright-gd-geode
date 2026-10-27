"""Canonical song identification, normalization, and mashup/derivative detection."""

from __future__ import annotations

import re
import unicodedata

from backend.app.schemas.rights import SongResolveRequest, SourceType
from backend.app.security.sanitizer import validate_public_http_url

_NON_ALNUM_RE = re.compile(r"[^a-z0-9]+")
_MASHUP_RE = re.compile(
    r"\b(mashup|megamix|bootleg|nightcore|daycore|sped\s*up|slowed|reverb|\bvs\.?\b)\b",
    re.IGNORECASE,
)
_REMIX_COVER_RE = re.compile(r"\b(remix|vip\s+mix|cover|flip|edit)\b", re.IGNORECASE)


def normalize_token(text: str) -> str:
    nfkc = unicodedata.normalize("NFKC", text or "").lower().strip()
    slug = _NON_ALNUM_RE.sub("-", nfkc).strip("-")
    return slug[:96] or "unknown"


def compute_canonical_key(req: SongResolveRequest) -> str:
    """
    Build a deterministic deduplication key so identical songs investigated by multiple
    Geometry Dash / Jukebox clients share a single database record and research job.
    """
    norm_artist = normalize_token(req.artist)
    norm_title = normalize_token(req.title)

    if req.source_type == SourceType.ROBTOP_OFFICIAL and req.gd_song_id is not None:
        return f"robtop:{req.gd_song_id}"

    if req.source_type == SourceType.GD_MUSIC_LIBRARY and req.gd_song_id is not None:
        return f"gdml:{req.gd_song_id}"

    if req.source_type == SourceType.NCS_LIBRARY:
        if req.gd_song_id is not None:
            return f"ncs:gd:{req.gd_song_id}"
        return f"ncs:meta:{norm_artist}:{norm_title}"

    if req.source_type == SourceType.NEWGROUNDS and req.gd_song_id is not None and req.gd_song_id > 0:
        return f"ng:{req.gd_song_id}"

    if req.source_type == SourceType.JUKEBOX_YOUTUBE and req.youtube_id:
        return f"jb_yt:{req.youtube_id}"

    if req.source_type == SourceType.JUKEBOX_HOSTED and req.source_url:
        safe_url = validate_public_http_url(req.source_url, resolve_dns=False)
        if safe_url:
            return f"jb_url:{safe_url[:190]}"

    if req.source_type == SourceType.JUKEBOX_LOCAL and req.audio_sha256:
        return f"jb_local_sha256:{req.audio_sha256}"

    return f"{req.source_type.value}:meta:{norm_artist}:{norm_title}"


def detect_derivative_characteristics(title: str, artist: str) -> list[str]:
    """Identify mashups, remixes, covers, or speed-altered edits that complicate rights clearance."""
    combined = f"{title} {artist}"
    notes: list[str] = []
    if _MASHUP_RE.search(combined):
        notes.append(
            "Track metadata indicates a mashup, speed-edit, or bootleg; every underlying master "
            "recording and musical composition must be independently cleared."
        )
    if _REMIX_COVER_RE.search(combined):
        notes.append(
            "Track metadata indicates a remix, edit, or cover; rights to the remix/cover do not "
            "automatically include rights to the original composition or master."
        )
    return notes
