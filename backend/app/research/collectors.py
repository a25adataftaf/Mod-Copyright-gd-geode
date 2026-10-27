"""Evidence collection from official music pages, licence registries, and platform policies."""

from __future__ import annotations

import re
from dataclasses import dataclass, field
from datetime import datetime, timezone
from typing import Optional

import httpx

from backend.app.config import Settings
from backend.app.schemas.rights import SongResolveRequest, SourceType
from backend.app.security.sanitizer import (
    detect_prompt_injection,
    extract_visible_html,
    sanitize_untrusted_text,
    validate_public_http_url,
)

# Official platform policy URLs referenced across evaluations
POLICY_URLS: dict[str, str] = {
    "youtube": "https://support.google.com/youtube/answer/2797370",
    "youtube_creator_music": "https://support.google.com/youtube/answer/11611019",
    "twitch": "https://www.twitch.tv/p/en/legal/community-guidelines/music/",
    "twitch_dmca": "https://www.twitch.tv/p/en/legal/dmca-guidelines/",
    "tiktok": "https://www.tiktok.com/legal/page/global/commercial-music-library-user-terms/en",
    "instagram": "https://help.instagram.com/629037417957828",
    "facebook": "https://www.facebook.com/legal/music_guidelines",
    "ncs_policy": "https://ncs.io/usage-policy",
    "ncs_terms": "https://ncs.io/usage-policy/terms",
    "gd_music_faq": "https://www.robtopgames.com/faq/en/answers/music/",
}

_CC_LICENSE_RE = re.compile(
    r"https?://creativecommons\.org/(licenses|publicdomain)/([a-z0-9\-]+)/([0-9.]+)",
    re.IGNORECASE,
)
_NCS_CREDIT_RE = re.compile(
    r"(Song:\s*[^\n]+(?:\n|\s)+Music provided by NoCopyrightSounds(?:\n|\s)+Free Download/Stream:\s*\S+(?:(?:\n|\s)+Watch:\s*\S+)?)",
    re.IGNORECASE,
)


@dataclass
class CollectedEvidenceItem:
    source_kind: str
    source_title: str
    source_url: Optional[str]
    fetch_status: str
    excerpt: str
    retrieved_at: datetime = field(default_factory=lambda: datetime.now(timezone.utc))


@dataclass
class EvidenceBundle:
    canonical_source_url: Optional[str] = None
    detected_license_code: Optional[str] = None  # e.g. "NCS_UGC", "CC-BY-4.0", "CC-BY-NC-SA-3.0", "ALL_RIGHTS_RESERVED"
    detected_license_name: Optional[str] = None
    detected_license_url: Optional[str] = None
    extracted_attribution: Optional[str] = None
    detected_rights_holders: list[str] = field(default_factory=list)
    items: list[CollectedEvidenceItem] = field(default_factory=list)
    security_flags: list[str] = field(default_factory=list)
    conflicting_signals: list[str] = field(default_factory=list)


def _classify_cc_url(domain_section: str, code: str, version: str) -> tuple[str, str, str]:
    code_lower = code.lower()
    full_url = f"https://creativecommons.org/{domain_section.lower()}/{code_lower}/{version}/"
    if domain_section.lower() == "publicdomain" and code_lower == "zero":
        return "CC0", f"Creative Commons CC0 {version} Universal", full_url
    code_upper = code_lower.upper()
    return f"CC-{code_upper}-{version}", f"Creative Commons {code_upper} {version}", full_url


async def _safe_fetch_html(url: str, settings: Settings) -> tuple[str, Optional[str], list[str]]:
    """
    Safely fetch a public HTTP(S) document with SSRF validation, timeout, and byte limits.
    Returns (fetch_status, raw_html_or_none, security_flags).
    """
    if not settings.allow_live_http_evidence:
        return "live_fetch_disabled", None, []

    safe_url = validate_public_http_url(url, resolve_dns=True)
    if not safe_url:
        return "blocked_unsafe_url", None, ["ssrf_guard:blocked_non_public_url"]

    headers = {
        "User-Agent": "MusicRightsChecker/0.1 (+https://geode-sdk.org; rights-verification)",
        "Accept": "text/html,application/xhtml+xml;q=0.9,*/*;q=0.5",
    }
    try:
        async with httpx.AsyncClient(
            timeout=httpx.Timeout(settings.http_fetch_timeout_seconds),
            follow_redirects=False,
        ) as client:
            current_url = safe_url
            for _ in range(3):
                resp = await client.get(current_url, headers=headers)
                if resp.status_code in {301, 302, 303, 307, 308}:
                    loc = resp.headers.get("location", "")
                    if loc.startswith("/"):
                        parsed_base = httpx.URL(current_url)
                        loc = f"{parsed_base.scheme}://{parsed_base.host}{loc}"
                    next_url = validate_public_http_url(loc, resolve_dns=True)
                    if not next_url:
                        return "blocked_unsafe_redirect", None, ["ssrf_guard:blocked_redirect"]
                    current_url = next_url
                    continue
                break

            if resp.status_code == 403:
                return "blocked_403", None, []
            if resp.status_code == 404:
                return "not_found", None, []
            if resp.status_code >= 400:
                return f"http_{resp.status_code}", None, []

            raw_bytes = resp.content[: settings.max_evidence_bytes]
            encoding = resp.encoding or "utf-8"
            decoded = raw_bytes.decode(encoding, errors="replace")
            return "verified", decoded, []
    except httpx.TimeoutException:
        return "timeout", None, []
    except httpx.HTTPError:
        return "network_error", None, []


def _inspect_html_for_licenses(
    raw_html: str, source_url: Optional[str], source_title: str, bundle: EvidenceBundle
) -> None:
    visible_text, outbound_links, flags = extract_visible_html(raw_html)
    for flag in flags:
        if flag not in bundle.security_flags:
            bundle.security_flags.append(flag)

    # Check NCS credit block
    ncs_match = _NCS_CREDIT_RE.search(visible_text)
    if ncs_match:
        bundle.extracted_attribution = sanitize_untrusted_text(ncs_match.group(1), max_length=600)
        bundle.detected_license_code = "NCS_UGC"
        bundle.detected_license_name = "NCS Independent Creator Usage Policy"
        bundle.detected_license_url = POLICY_URLS["ncs_policy"]
        if "NoCopyrightSounds (NCS)" not in bundle.detected_rights_holders:
            bundle.detected_rights_holders.append("NoCopyrightSounds (NCS)")

    # Check Creative Commons links in both outbound anchors and visible text
    found_cc_codes: list[tuple[str, str, str]] = []
    for candidate in [*outbound_links, visible_text]:
        for match in _CC_LICENSE_RE.finditer(candidate):
            code_id, label, cc_url = _classify_cc_url(match.group(1), match.group(2), match.group(3))
            if (code_id, label, cc_url) not in found_cc_codes:
                found_cc_codes.append((code_id, label, cc_url))

    # Check explicit "All Rights Reserved" statement in visible text
    has_arr = bool(re.search(r"\ball\s+rights\s+reserved\b", visible_text, re.IGNORECASE))

    if len(found_cc_codes) > 1:
        distinct = {c[0] for c in found_cc_codes}
        if len(distinct) > 1:
            bundle.conflicting_signals.append(
                f"Page references multiple Creative Commons licences: {', '.join(sorted(distinct))}."
            )

    if found_cc_codes and has_arr:
        bundle.conflicting_signals.append(
            f"Page contains both a Creative Commons licence ({found_cc_codes[0][1]}) and an 'All Rights Reserved' notice."
        )

    if found_cc_codes and not bundle.detected_license_code:
        bundle.detected_license_code, bundle.detected_license_name, bundle.detected_license_url = (
            found_cc_codes[0]
        )
    elif has_arr and not bundle.detected_license_code:
        bundle.detected_license_code = "ALL_RIGHTS_RESERVED"
        bundle.detected_license_name = "All Rights Reserved"

    excerpt = visible_text[:700] if visible_text else "Page retrieved with no extractable text."
    bundle.items.append(
        CollectedEvidenceItem(
            source_kind="song_page",
            source_title=source_title,
            source_url=source_url,
            fetch_status="verified",
            excerpt=excerpt,
        )
    )


async def collect_evidence(req: SongResolveRequest, settings: Settings) -> EvidenceBundle:
    """Gather verifiable evidence from official sources and platform policies."""
    bundle = EvidenceBundle()

    # Check untrusted request metadata itself for injection attempts
    for raw_field in (req.title, req.artist, req.source_url or "", req.jukebox_index_id or ""):
        for flag in detect_prompt_injection(raw_field):
            if flag not in bundle.security_flags:
                bundle.security_flags.append(flag)

    safe_source_url = validate_public_http_url(req.source_url, resolve_dns=False)

    # 1. NCS Library or verified ncs.io track URL
    is_ncs_domain = bool(
        safe_source_url
        and (
            safe_source_url.startswith("https://ncs.io/")
            or safe_source_url.startswith("http://ncs.io/")
        )
    )
    if req.source_type == SourceType.NCS_LIBRARY or is_ncs_domain:
        bundle.canonical_source_url = safe_source_url or "https://ncs.io/"
        bundle.detected_license_code = "NCS_UGC"
        bundle.detected_license_name = "NCS Independent Creator Usage Policy"
        bundle.detected_license_url = POLICY_URLS["ncs_policy"]
        bundle.detected_rights_holders = [req.artist, "NoCopyrightSounds (NCS)"]

        if req.html_evidence_override:
            _inspect_html_for_licenses(
                req.html_evidence_override, bundle.canonical_source_url, "NCS Track Page Snapshot", bundle
            )
        elif is_ncs_domain and safe_source_url:
            status_code, html_doc, flags = await _safe_fetch_html(safe_source_url, settings)
            bundle.security_flags.extend(f for f in flags if f not in bundle.security_flags)
            if status_code == "verified" and html_doc:
                _inspect_html_for_licenses(html_doc, safe_source_url, "NCS Official Track Page", bundle)
            else:
                bundle.items.append(
                    CollectedEvidenceItem(
                        source_kind="song_page",
                        source_title="NCS Track URL Fetch",
                        source_url=safe_source_url,
                        fetch_status=status_code,
                        excerpt=f"NCS track page fetch returned status '{status_code}'; falling back to official NCS library policy.",
                    )
                )

        if not bundle.extracted_attribution:
            track_link = safe_source_url or "https://ncs.io/"
            bundle.extracted_attribution = (
                f"Song: {sanitize_untrusted_text(req.artist, 120)} - {sanitize_untrusted_text(req.title, 120)} [NCS Release]\n"
                f"Music provided by NoCopyrightSounds\n"
                f"Free Download/Stream: {track_link}"
            )

        bundle.items.append(
            CollectedEvidenceItem(
                source_kind="official_license",
                source_title="NoCopyrightSounds Official Usage Policy & Terms",
                source_url=POLICY_URLS["ncs_policy"],
                fetch_status="policy_reference",
                excerpt=(
                    "NCS grants Independent Creators non-exclusive permission to synchronize NCS Music "
                    "with user-generated content on YouTube and Twitch (including monetization) when "
                    "mandatory artist and NCS attribution is included in the description. Corporate or "
                    "brand commercial campaigns promoting external products/services require a separate "
                    "commercial licence."
                ),
            )
        )
        return bundle

    # 2. Newgrounds Audio Portal
    if req.source_type == SourceType.NEWGROUNDS:
        ng_url = (
            f"https://www.newgrounds.com/audio/listen/{req.gd_song_id}"
            if req.gd_song_id and req.gd_song_id > 0
            else safe_source_url
        )
        bundle.canonical_source_url = ng_url
        bundle.detected_rights_holders = [req.artist] if req.artist != "Unknown Artist" else []

        if req.html_evidence_override:
            _inspect_html_for_licenses(
                req.html_evidence_override, ng_url, "Newgrounds Audio Portal Snapshot", bundle
            )
        elif ng_url:
            status_code, html_doc, flags = await _safe_fetch_html(ng_url, settings)
            bundle.security_flags.extend(f for f in flags if f not in bundle.security_flags)
            if status_code == "verified" and html_doc:
                _inspect_html_for_licenses(html_doc, ng_url, "Newgrounds Audio Portal Page", bundle)
            else:
                bundle.items.append(
                    CollectedEvidenceItem(
                        source_kind="song_page",
                        source_title="Newgrounds Audio Portal Lookup",
                        source_url=ng_url,
                        fetch_status=status_code,
                        excerpt=(
                            f"Direct retrieval from Newgrounds returned '{status_code}' (e.g., automated "
                            "bot protection or unavailable page). Individual Newgrounds tracks choose their "
                            "own Creative Commons or All Rights Reserved terms; Geometry Dash whitelist "
                            "approval alone does not verify commercial or monetization rights."
                        ),
                    )
                )

        bundle.items.append(
            CollectedEvidenceItem(
                source_kind="platform_policy",
                source_title="Geometry Dash Official Music & Newgrounds Whitelist FAQ",
                source_url=POLICY_URLS["gd_music_faq"],
                fetch_status="policy_reference",
                excerpt=(
                    "Geometry Dash requires Newgrounds artists to be scouted and whitelisted for in-game "
                    "custom level use, but each artist independently sets external video, commercial, and "
                    "Content ID policies on their Newgrounds audio entry."
                ),
            )
        )
        return bundle

    # 3. Geometry Dash Music Library & Official RobTop Level Songs
    if req.source_type in {SourceType.GD_MUSIC_LIBRARY, SourceType.ROBTOP_OFFICIAL}:
        bundle.canonical_source_url = safe_source_url or POLICY_URLS["gd_music_faq"]
        bundle.detected_rights_holders = [req.artist] if req.artist != "Unknown Artist" else ["RobTop Games / Featured Artist"]

        if req.html_evidence_override:
            _inspect_html_for_licenses(
                req.html_evidence_override,
                bundle.canonical_source_url,
                "Artist / Library Licence Snapshot",
                bundle,
            )

        bundle.items.append(
            CollectedEvidenceItem(
                source_kind="platform_policy",
                source_title="Geometry Dash Official Music Library Policy",
                source_url=POLICY_URLS["gd_music_faq"],
                fetch_status="policy_reference",
                excerpt=(
                    "Songs in Geometry Dash's official levels and Music Library are curated by RobTop "
                    "for in-game level creation and gameplay. In-game licensing does not automatically "
                    "grant universal commercial synchronization, TikTok Business, Meta ad, or Content ID "
                    "exemption rights outside Geometry Dash without verifying the artist's external usage policy."
                ),
            )
        )
        return bundle

    # 4. Jukebox NONGs (YouTube, Hosted / Song File Hub, Local files) & Custom Libraries
    if req.youtube_id:
        bundle.canonical_source_url = f"https://www.youtube.com/watch?v={req.youtube_id}"
    else:
        bundle.canonical_source_url = safe_source_url

    if req.artist and req.artist != "Unknown Artist":
        bundle.detected_rights_holders = [req.artist]

    if req.html_evidence_override:
        _inspect_html_for_licenses(
            req.html_evidence_override,
            bundle.canonical_source_url,
            "Submitted Song Source Snapshot",
            bundle,
        )
    elif safe_source_url and req.source_type == SourceType.CUSTOM_LIBRARY:
        status_code, html_doc, flags = await _safe_fetch_html(safe_source_url, settings)
        bundle.security_flags.extend(f for f in flags if f not in bundle.security_flags)
        if status_code == "verified" and html_doc:
            _inspect_html_for_licenses(html_doc, safe_source_url, "Custom Library Source Page", bundle)

    is_sfh = bool(
        (req.jukebox_index_id and ("sfh" in req.jukebox_index_id.lower() or "song file hub" in req.jukebox_index_id.lower()))
        or (safe_source_url and "songfilehub.com" in safe_source_url.lower())
    )
    if is_sfh:
        bundle.items.append(
            CollectedEvidenceItem(
                source_kind="nong_index",
                source_title="Jukebox / Song File Hub NONG Index Entry",
                source_url=safe_source_url,
                fetch_status="unverified_metadata",
                excerpt=(
                    "Track was imported via a NONG (Not On Newgrounds) index (Song File Hub). NONG indexes "
                    "catalog community replacement audio—frequently commercial label releases, soundtrack "
                    "recordings, or mashups—and do not grant any copyright or monetization licence."
                ),
            )
        )
    elif req.source_type in {SourceType.JUKEBOX_LOCAL, SourceType.JUKEBOX_YOUTUBE, SourceType.JUKEBOX_HOSTED}:
        bundle.items.append(
            CollectedEvidenceItem(
                source_kind="nong_index",
                source_title=f"Jukebox {req.source_type.value} Audio Replacement",
                source_url=bundle.canonical_source_url,
                fetch_status="unverified_metadata",
                excerpt=(
                    "Audio is a local or external NONG replacement managed through Jukebox. No verified "
                    "official licence grant was attached to the file metadata."
                ),
            )
        )

    return bundle
