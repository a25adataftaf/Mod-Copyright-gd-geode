"""Conservative per-platform music rights evaluator and deterministic legal guardrails."""

from __future__ import annotations

from dataclasses import dataclass, field
from typing import Optional

from backend.app.research.ai_providers import AISuggestedAnalysis
from backend.app.research.collectors import POLICY_URLS, CollectedEvidenceItem, EvidenceBundle
from backend.app.research.identity import detect_derivative_characteristics
from backend.app.schemas.rights import (
    ALL_PLATFORMS,
    ClaimRiskLevel,
    CopyrightOwnershipStatus,
    OverallRightsStatus,
    PermissionLevel,
    PlatformName,
    PlatformPermissionOut,
    SongResolveRequest,
    SourceType,
    VerificationMethod,
)
from backend.app.security.sanitizer import sanitize_untrusted_text


@dataclass
class EvaluatedSongRights:
    original_artist: Optional[str]
    rights_holders: list[str]
    original_source_url: Optional[str]
    license_name: str
    license_url: Optional[str]
    copyright_ownership_status: CopyrightOwnershipStatus
    overall_status: OverallRightsStatus
    commercial_use_status: PermissionLevel
    attribution_required: bool
    attribution_template: Optional[str]
    platforms: list[PlatformPermissionOut]
    evidence: list[CollectedEvidenceItem]
    uncertainties: list[str] = field(default_factory=list)
    conflicting_information: list[str] = field(default_factory=list)
    security_flags: list[str] = field(default_factory=list)
    verification_method: VerificationMethod = VerificationMethod.CONSERVATIVE_FALLBACK
    ai_diagnostic_confidence: Optional[float] = None


def _build_ncs_platforms() -> list[PlatformPermissionOut]:
    """
    Evaluate NCS tracks distinctly per platform based on NCS Usage Policy + each platform's own terms.
    """
    return [
        PlatformPermissionOut(
            platform=PlatformName.YOUTUBE,
            video_upload_permission=PermissionLevel.CONDITIONAL,
            livestream_permission=PermissionLevel.CONDITIONAL,
            commercial_use_permission=PermissionLevel.CONDITIONAL,
            monetization_permission=PermissionLevel.ALLOWED,
            attribution_required=True,
            automated_claim_risk=ClaimRiskLevel.LOW,
            conditions=[
                "Independent creators must paste the full NCS track credit and link in the video or stream description.",
                "Permitted for independent creator UGC ad monetization; corporate or brand product commercials require a paid NCS commercial licence.",
            ],
            restrictions=[
                "Do not register videos containing NCS audio in YouTube Content ID or claim ownership of the audio."
            ],
            policy_reference_url=POLICY_URLS["ncs_policy"],
        ),
        PlatformPermissionOut(
            platform=PlatformName.TWITCH,
            video_upload_permission=PermissionLevel.CONDITIONAL,
            livestream_permission=PermissionLevel.CONDITIONAL,
            commercial_use_permission=PermissionLevel.CONDITIONAL,
            monetization_permission=PermissionLevel.ALLOWED,
            attribution_required=True,
            automated_claim_risk=ClaimRiskLevel.LOW,
            conditions=[
                "Independent streamers must include NCS attribution (e.g., 'Music provided by http://spoti.fi/NCS' or track credit) in stream/panel descriptions.",
                "Corporate/organization broadcasts promoting external brands require a commercial licence from NCS.",
            ],
            restrictions=[
                "Do not operate an unaccompanied 24/7 radio-style stream without live creator commentary/gameplay."
            ],
            policy_reference_url=POLICY_URLS["twitch"],
        ),
        PlatformPermissionOut(
            platform=PlatformName.TIKTOK,
            video_upload_permission=PermissionLevel.CONDITIONAL,
            livestream_permission=PermissionLevel.CONDITIONAL,
            commercial_use_permission=PermissionLevel.CONDITIONAL,
            monetization_permission=PermissionLevel.CONDITIONAL,
            attribution_required=True,
            automated_claim_risk=ClaimRiskLevel.MODERATE,
            conditions=[
                "NCS Terms permit Independent Creator UGC with credit, but TikTok Business Accounts and commercial/promotional posts must comply with TikTok's Commercial Music Library (CML) rules and Music Usage Confirmation (effective July 25, 2025).",
                "Corporate brand campaigns on TikTok require a separate NCS commercial licence unless sourced directly from TikTok CML within Usable Placements.",
            ],
            restrictions=[
                "Audio selected inside TikTok's CML cannot be cross-posted commercially to non-TikTok platforms without independent rights."
            ],
            policy_reference_url=POLICY_URLS["tiktok"],
        ),
        PlatformPermissionOut(
            platform=PlatformName.INSTAGRAM,
            video_upload_permission=PermissionLevel.CONDITIONAL,
            livestream_permission=PermissionLevel.CONDITIONAL,
            commercial_use_permission=PermissionLevel.CONDITIONAL,
            monetization_permission=PermissionLevel.CONDITIONAL,
            attribution_required=True,
            automated_claim_risk=ClaimRiskLevel.MODERATE,
            conditions=[
                "Include NCS attribution in the Reel or post caption for Independent Creator UGC.",
                "Visual gameplay/commentary must remain primary; Meta Music Guidelines prohibit creating a pure music-listening experience.",
            ],
            restrictions=[
                "Branded content, paid partnerships, or boosted ads require explicit commercial licensing or Meta Sound Collection audio."
            ],
            policy_reference_url=POLICY_URLS["instagram"],
        ),
        PlatformPermissionOut(
            platform=PlatformName.FACEBOOK,
            video_upload_permission=PermissionLevel.CONDITIONAL,
            livestream_permission=PermissionLevel.CONDITIONAL,
            commercial_use_permission=PermissionLevel.CONDITIONAL,
            monetization_permission=PermissionLevel.CONDITIONAL,
            attribution_required=True,
            automated_claim_risk=ClaimRiskLevel.MODERATE,
            conditions=[
                "Include NCS attribution in the video/stream description for Independent Creator UGC.",
                "On Facebook Live, keep recorded music secondary to active gameplay/commentary to avoid automated live audio muting.",
            ],
            restrictions=[
                "Paid ads and corporate business promotions require separate commercial sync licensing."
            ],
            policy_reference_url=POLICY_URLS["facebook"],
        ),
    ]


def _build_cc_commercial_platforms(
    license_label: str, share_alike: bool, no_derivatives: bool
) -> list[PlatformPermissionOut]:
    """
    Build per-platform evaluations for CC0 / CC BY / CC BY-SA (and handle CC BY-ND video-sync restriction!).
    Note: Under Creative Commons BY-ND, syncing audio to timed video is widely treated as creating an
    adaptation/derivative work, so CC BY-ND CANNOT be classified as permitted for video synchronization!
    """
    if no_derivatives:
        return [
            PlatformPermissionOut(
                platform=p,
                video_upload_permission=PermissionLevel.NOT_ALLOWED,
                livestream_permission=PermissionLevel.NOT_ALLOWED,
                commercial_use_permission=PermissionLevel.NOT_ALLOWED,
                monetization_permission=PermissionLevel.NOT_ALLOWED,
                attribution_required=True,
                automated_claim_risk=ClaimRiskLevel.MODERATE,
                conditions=[],
                restrictions=[
                    f"{license_label} prohibits adaptations/derivatives (NoDerivatives); synchronizing music to gameplay video requires separate permission from the rights holder."
                ],
                policy_reference_url=POLICY_URLS.get(p.value),
            )
            for p in ALL_PLATFORMS
        ]

    sa_cond = (
        ["ShareAlike (SA): any derivative adaptation must be distributed under the same or a compatible licence, which may conflict with standard platform terms."]
        if share_alike
        else []
    )

    out: list[PlatformPermissionOut] = []
    for p in ALL_PLATFORMS:
        platform_extra: list[str] = []
        monetization = PermissionLevel.ALLOWED if not share_alike else PermissionLevel.CONDITIONAL
        if p == PlatformName.TIKTOK:
            monetization = PermissionLevel.CONDITIONAL
            platform_extra.append(
                "Commercial/Business accounts uploading external licensed audio must complete TikTok's Music Usage Confirmation and retain proof of the Creative Commons licence."
            )
        elif p in {PlatformName.INSTAGRAM, PlatformName.FACEBOOK}:
            monetization = PermissionLevel.CONDITIONAL
            platform_extra.append(
                "Meta Music Guidelines prohibit static/audio-primary 'music listening experience' videos and streams; retain licence URL if appealing automated Rights Manager flags."
            )
        elif p == PlatformName.TWITCH:
            platform_extra.append(
                "Retain the official track URL and CC licence link in case a third-party distributor triggers an automated Audible Magic VOD mute."
            )
        elif p == PlatformName.YOUTUBE:
            platform_extra.append(
                "Provide full TASL attribution (Title, Author, Source URL, Licence URL) in the video description; keep licence evidence if disputing false third-party Content ID claims."
            )

        out.append(
            PlatformPermissionOut(
                platform=p,
                video_upload_permission=PermissionLevel.CONDITIONAL,
                livestream_permission=PermissionLevel.CONDITIONAL,
                commercial_use_permission=PermissionLevel.ALLOWED if not share_alike else PermissionLevel.CONDITIONAL,
                monetization_permission=monetization,
                attribution_required=(license_label != "Creative Commons CC0 1.0 Universal"),
                automated_claim_risk=ClaimRiskLevel.MODERATE,
                conditions=[
                    f"Must comply with {license_label} attribution and notice requirements.",
                    *sa_cond,
                    *platform_extra,
                ],
                restrictions=[
                    "Do not imply endorsement by the licensor or register the audio in automated Content ID systems."
                ],
                policy_reference_url=POLICY_URLS.get(p.value),
            )
        )
    return out


def _build_cc_non_commercial_platforms(
    license_label: str, no_derivatives: bool
) -> list[PlatformPermissionOut]:
    out: list[PlatformPermissionOut] = []
    for p in ALL_PLATFORMS:
        upload_perm = (
            PermissionLevel.NOT_ALLOWED if no_derivatives else PermissionLevel.CONDITIONAL
        )
        restrictions = [
            f"{license_label} strictly prohibits commercial use, ad-revenue monetization, sponsorships, and promotional business posts without a separate licence from the artist."
        ]
        if no_derivatives:
            restrictions.append(
                f"{license_label} also prohibits adaptations/derivatives (NoDerivatives), restricting video synchronization without explicit artist permission."
            )
        out.append(
            PlatformPermissionOut(
                platform=p,
                video_upload_permission=upload_perm,
                livestream_permission=upload_perm,
                commercial_use_permission=PermissionLevel.NOT_ALLOWED,
                monetization_permission=PermissionLevel.NOT_ALLOWED,
                attribution_required=True,
                automated_claim_risk=ClaimRiskLevel.MODERATE,
                conditions=(
                    []
                    if no_derivatives
                    else [
                        f"Non-commercial personal uploads only, with full attribution under {license_label}."
                    ]
                ),
                restrictions=restrictions,
                policy_reference_url=POLICY_URLS.get(p.value),
            )
        )
    return out


def _build_unverified_or_restricted_platforms(
    *,
    is_likely_unlicensed_nong: bool,
    is_in_game_curated: bool,
) -> list[PlatformPermissionOut]:
    out: list[PlatformPermissionOut] = []
    for p in ALL_PLATFORMS:
        if is_likely_unlicensed_nong:
            out.append(
                PlatformPermissionOut(
                    platform=p,
                    video_upload_permission=PermissionLevel.NOT_ALLOWED,
                    livestream_permission=PermissionLevel.NOT_ALLOWED,
                    commercial_use_permission=PermissionLevel.NOT_ALLOWED,
                    monetization_permission=PermissionLevel.NOT_ALLOWED,
                    attribution_required=False,
                    automated_claim_risk=ClaimRiskLevel.HIGH,
                    conditions=[],
                    restrictions=[
                        "NONG replacement audio has no verified licence grant. Mainstream recordings, soundtracks, and mashups commonly trigger Content ID blocks/claims (including global blocks on YouTube Shorts > 60s), Twitch VOD mutes or DMCA strikes, TikTok commercial audio removals, and Meta Reels/Live blocks."
                    ],
                    policy_reference_url=POLICY_URLS.get(p.value),
                )
            )
        elif is_in_game_curated:
            out.append(
                PlatformPermissionOut(
                    platform=p,
                    video_upload_permission=PermissionLevel.UNKNOWN,
                    livestream_permission=PermissionLevel.UNKNOWN,
                    commercial_use_permission=PermissionLevel.UNKNOWN,
                    monetization_permission=PermissionLevel.UNKNOWN,
                    attribution_required=True,
                    automated_claim_risk=ClaimRiskLevel.MODERATE,
                    conditions=[
                        "Verify the featured artist's external video/streaming policy before monetizing; credit the artist and track title."
                    ],
                    restrictions=[
                        "Inclusion in Geometry Dash's official levels or Music Library grants in-game use, not an automatic commercial licence across external platforms."
                    ],
                    policy_reference_url=POLICY_URLS.get(p.value),
                )
            )
        else:
            out.append(
                PlatformPermissionOut(
                    platform=p,
                    video_upload_permission=PermissionLevel.UNKNOWN,
                    livestream_permission=PermissionLevel.UNKNOWN,
                    commercial_use_permission=PermissionLevel.UNKNOWN,
                    monetization_permission=PermissionLevel.UNKNOWN,
                    attribution_required=False,
                    automated_claim_risk=ClaimRiskLevel.UNKNOWN,
                    conditions=[],
                    restrictions=[
                        "No verifiable licence evidence was retrieved for this track. Do not monetize or use commercially unless rights-holder authorization is confirmed."
                    ],
                    policy_reference_url=POLICY_URLS.get(p.value),
                )
            )
    return out


def evaluate_song_rights(
    req: SongResolveRequest,
    bundle: EvidenceBundle,
    ai_suggestion: Optional[AISuggestedAnalysis] = None,
) -> EvaluatedSongRights:
    """
    Combine collected evidence and optional AI diagnostic analysis under strict
    deterministic legal guardrails.
    """
    derivative_notes = detect_derivative_characteristics(req.title, req.artist)
    is_mashup = any("mashup" in note.lower() for note in derivative_notes)

    uncertainties: list[str] = list(derivative_notes)
    conflicts: list[str] = list(bundle.conflicting_signals)
    security_flags: list[str] = list(bundle.security_flags)

    original_artist = req.artist if req.artist != "Unknown Artist" else None
    rights_holders = list(bundle.detected_rights_holders)

    # Merge informational notes from AI ONLY if no security flags triggered
    verification_method = VerificationMethod.CONSERVATIVE_FALLBACK
    ai_conf: Optional[float] = None
    if ai_suggestion and not security_flags:
        ai_conf = ai_suggestion.diagnostic_confidence
        if ai_suggestion.original_artist and not original_artist:
            original_artist = ai_suggestion.original_artist
        for holder in ai_suggestion.rights_holders:
            if holder not in rights_holders:
                rights_holders.append(holder)
        for u in ai_suggestion.uncertainties:
            if u not in uncertainties:
                uncertainties.append(u)
        for c in ai_suggestion.conflicting_information:
            if c not in conflicts:
                conflicts.append(c)

    if security_flags:
        uncertainties.append(
            "Untrusted external metadata or page content contained suspicious instruction-override or unsafe URL patterns and was quarantined."
        )

    # HARD GUARDRAIL 1: Only deterministic verified official evidence in `bundle` can establish a permissive licence!
    # An AI suggestion WITHOUT verified official evidence in `bundle` can NEVER upgrade an unverified song to permitted.
    has_verified_page_or_official_policy = any(
        item.fetch_status == "verified"
        or (item.source_kind == "official_license" and req.source_type == SourceType.NCS_LIBRARY)
        for item in bundle.items
    )

    effective_license_code = bundle.detected_license_code if has_verified_page_or_official_policy else None
    effective_license_name = bundle.detected_license_name if has_verified_page_or_official_policy else None
    effective_license_url = bundle.detected_license_url if has_verified_page_or_official_policy else None

    # HARD GUARDRAIL 2: Conflicting licences or mashups force REVIEW_REQUIRED and block auto-clearance!
    if conflicts or is_mashup:
        if is_mashup and effective_license_code == "NCS_UGC":
            conflicts.append(
                "Track is marked as a mashup/bootleg; NCS's creator policy covers only unaltered official NCS releases, not third-party mashups."
            )
        platforms = _build_unverified_or_restricted_platforms(
            is_likely_unlicensed_nong=is_mashup,
            is_in_game_curated=False,
        )
        return EvaluatedSongRights(
            original_artist=original_artist,
            rights_holders=rights_holders,
            original_source_url=bundle.canonical_source_url,
            license_name=effective_license_name or "Conflicting or Multi-Work Unverified Rights",
            license_url=effective_license_url,
            copyright_ownership_status=CopyrightOwnershipStatus.UNKNOWN,
            overall_status=OverallRightsStatus.REVIEW_REQUIRED,
            commercial_use_status=PermissionLevel.UNKNOWN if not is_mashup else PermissionLevel.NOT_ALLOWED,
            attribution_required=bool(effective_license_code),
            attribution_template=bundle.extracted_attribution,
            platforms=platforms,
            evidence=bundle.items,
            uncertainties=uncertainties,
            conflicting_information=conflicts,
            security_flags=security_flags,
            verification_method=VerificationMethod.AI_ASSISTED_EVIDENCE
            if (ai_suggestion and ai_suggestion.provider_used in {"openai", "anthropic", "gemini"})
            else VerificationMethod.OFFICIAL_SOURCE,
            ai_diagnostic_confidence=ai_conf,
        )

    # Case A: Verified NCS release
    if effective_license_code == "NCS_UGC" and not security_flags:
        uncertainties.append(
            "NCS permission applies to Independent Creators' UGC with proper credit; corporate brands or paid off-platform commercials require a separate commercial licence."
        )
        return EvaluatedSongRights(
            original_artist=original_artist,
            rights_holders=rights_holders or ["NoCopyrightSounds (NCS)"],
            original_source_url=bundle.canonical_source_url,
            license_name=effective_license_name or "NCS Independent Creator Usage Policy",
            license_url=effective_license_url or POLICY_URLS["ncs_policy"],
            copyright_ownership_status=CopyrightOwnershipStatus.COPYRIGHTED,
            overall_status=OverallRightsStatus.PERMITTED_WITH_CONDITIONS,
            commercial_use_status=PermissionLevel.CONDITIONAL,
            attribution_required=True,
            attribution_template=bundle.extracted_attribution,
            platforms=_build_ncs_platforms(),
            evidence=bundle.items,
            uncertainties=uncertainties,
            conflicting_information=conflicts,
            security_flags=security_flags,
            verification_method=VerificationMethod.OFFICIAL_SOURCE,
            ai_diagnostic_confidence=ai_conf,
        )

    # Case B: Verified Creative Commons licence extracted from official track page
    if effective_license_code and effective_license_code.startswith("CC") and not security_flags:
        is_cc0 = effective_license_code == "CC0"
        is_nc = "-NC" in effective_license_code
        is_nd = "-ND" in effective_license_code
        is_sa = "-SA" in effective_license_code
        lic_label = effective_license_name or effective_license_code

        attr_text = (
            None
            if is_cc0
            else (
                f"Music: \"{sanitize_untrusted_text(req.title, 120)}\" by {sanitize_untrusted_text(req.artist, 120)}\n"
                f"Source: {bundle.canonical_source_url or 'Original artist page'}\n"
                f"Licence: {lic_label} ({effective_license_url or 'https://creativecommons.org/'})"
            )
        )

        if is_nc or is_nd:
            overall = (
                OverallRightsStatus.RESTRICTED
                if is_nd
                else OverallRightsStatus.CONDITIONAL_NON_COMMERCIAL
            )
            platforms = (
                _build_cc_non_commercial_platforms(lic_label, no_derivatives=is_nd)
                if is_nc
                else _build_cc_commercial_platforms(lic_label, share_alike=is_sa, no_derivatives=True)
            )
            uncertainties.append(
                f"Official source specifies {lic_label}; commercial monetization requires separate written permission from the rights holder."
            )
            return EvaluatedSongRights(
                original_artist=original_artist,
                rights_holders=rights_holders,
                original_source_url=bundle.canonical_source_url,
                license_name=lic_label,
                license_url=effective_license_url,
                copyright_ownership_status=CopyrightOwnershipStatus.CREATIVE_COMMONS,
                overall_status=overall,
                commercial_use_status=PermissionLevel.NOT_ALLOWED,
                attribution_required=True,
                attribution_template=attr_text,
                platforms=platforms,
                evidence=bundle.items,
                uncertainties=uncertainties,
                conflicting_information=conflicts,
                security_flags=security_flags,
                verification_method=VerificationMethod.OFFICIAL_SOURCE,
                ai_diagnostic_confidence=ai_conf,
            )

        # CC0, CC BY, or CC BY-SA
        uncertainties.append(
            "Creative Commons licences rely on the uploader actually owning all underlying rights; third-party distributors may still trigger automated Content ID or VOD claims."
        )
        return EvaluatedSongRights(
            original_artist=original_artist,
            rights_holders=rights_holders,
            original_source_url=bundle.canonical_source_url,
            license_name=lic_label,
            license_url=effective_license_url,
            copyright_ownership_status=(
                CopyrightOwnershipStatus.PUBLIC_DOMAIN_VERIFIED
                if is_cc0
                else CopyrightOwnershipStatus.CREATIVE_COMMONS
            ),
            overall_status=(
                OverallRightsStatus.PERMITTED
                if is_cc0
                else OverallRightsStatus.PERMITTED_WITH_CONDITIONS
            ),
            commercial_use_status=PermissionLevel.ALLOWED if not is_sa else PermissionLevel.CONDITIONAL,
            attribution_required=not is_cc0,
            attribution_template=attr_text,
            platforms=_build_cc_commercial_platforms(lic_label, share_alike=is_sa, no_derivatives=False),
            evidence=bundle.items,
            uncertainties=uncertainties,
            conflicting_information=conflicts,
            security_flags=security_flags,
            verification_method=VerificationMethod.OFFICIAL_SOURCE,
            ai_diagnostic_confidence=ai_conf,
        )

    # Case C: Explicit All Rights Reserved found on official page
    if effective_license_code == "ALL_RIGHTS_RESERVED":
        uncertainties.append(
            "Source page explicitly reserves all rights; no blanket creator or commercial licence was found."
        )
        return EvaluatedSongRights(
            original_artist=original_artist,
            rights_holders=rights_holders,
            original_source_url=bundle.canonical_source_url,
            license_name="All Rights Reserved",
            license_url=None,
            copyright_ownership_status=CopyrightOwnershipStatus.COPYRIGHTED,
            overall_status=OverallRightsStatus.RESTRICTED,
            commercial_use_status=PermissionLevel.NOT_ALLOWED,
            attribution_required=False,
            attribution_template=None,
            platforms=_build_unverified_or_restricted_platforms(
                is_likely_unlicensed_nong=True, is_in_game_curated=False
            ),
            evidence=bundle.items,
            uncertainties=uncertainties,
            conflicting_information=conflicts,
            security_flags=security_flags,
            verification_method=VerificationMethod.OFFICIAL_SOURCE,
            ai_diagnostic_confidence=ai_conf,
        )

    # Case D: Jukebox hosted/YouTube NONG (Song File Hub etc.) without verified open licence
    if req.source_type in {SourceType.JUKEBOX_YOUTUBE, SourceType.JUKEBOX_HOSTED}:
        uncertainties.append(
            "NONG track imported via Jukebox has no verified creator licence. Most NONG replacements are copyrighted commercial releases subject to Content ID claims, Shorts blocks, and Twitch VOD mutes/DMCA notices."
        )
        return EvaluatedSongRights(
            original_artist=original_artist,
            rights_holders=rights_holders,
            original_source_url=bundle.canonical_source_url,
            license_name="Unverified NONG Replacement (Assume All Rights Reserved)",
            license_url=None,
            copyright_ownership_status=CopyrightOwnershipStatus.COPYRIGHTED,
            overall_status=OverallRightsStatus.RESTRICTED,
            commercial_use_status=PermissionLevel.NOT_ALLOWED,
            attribution_required=False,
            attribution_template=None,
            platforms=_build_unverified_or_restricted_platforms(
                is_likely_unlicensed_nong=True, is_in_game_curated=False
            ),
            evidence=bundle.items,
            uncertainties=uncertainties,
            conflicting_information=conflicts,
            security_flags=security_flags,
            verification_method=(
                VerificationMethod.AI_ASSISTED_EVIDENCE
                if (ai_suggestion and ai_suggestion.provider_used in {"openai", "anthropic", "gemini"})
                else VerificationMethod.CONSERVATIVE_FALLBACK
            ),
            ai_diagnostic_confidence=ai_conf,
        )

    # Case E: Geometry Dash Official / Music Library track without verified external licence page
    if req.source_type in {SourceType.GD_MUSIC_LIBRARY, SourceType.ROBTOP_OFFICIAL}:
        uncertainties.append(
            "Track comes from Geometry Dash's official levels or curated Music Library, but the artist's external commercial/monetization licence has not been verified for each target platform."
        )
        attr_fallback = (
            f"Music: \"{sanitize_untrusted_text(req.title, 120)}\" by {sanitize_untrusted_text(req.artist, 120)} "
            "(Geometry Dash Music Library)"
        )
        return EvaluatedSongRights(
            original_artist=original_artist,
            rights_holders=rights_holders,
            original_source_url=bundle.canonical_source_url,
            license_name="Geometry Dash In-Game Licence (External Rights Unverified)",
            license_url=POLICY_URLS["gd_music_faq"],
            copyright_ownership_status=CopyrightOwnershipStatus.COPYRIGHTED,
            overall_status=OverallRightsStatus.REVIEW_REQUIRED,
            commercial_use_status=PermissionLevel.UNKNOWN,
            attribution_required=True,
            attribution_template=attr_fallback,
            platforms=_build_unverified_or_restricted_platforms(
                is_likely_unlicensed_nong=False, is_in_game_curated=True
            ),
            evidence=bundle.items,
            uncertainties=uncertainties,
            conflicting_information=conflicts,
            security_flags=security_flags,
            verification_method=VerificationMethod.CONSERVATIVE_FALLBACK,
            ai_diagnostic_confidence=ai_conf,
        )

    # Case F: Unverified Newgrounds (e.g., 403/timeout), Local NONG audio file, or Custom Library
    if req.source_type == SourceType.NEWGROUNDS:
        uncertainties.append(
            "Newgrounds licence terms could not be verified directly (page unavailable or blocked to automated fetch). Check the Newgrounds song page directly before monetizing."
        )
    elif req.source_type == SourceType.JUKEBOX_LOCAL:
        uncertainties.append(
            "Locally imported audio file has no verifiable origin or licence metadata; local audio files are never uploaded automatically for privacy."
        )
    else:
        uncertainties.append("Insufficient verifiable official evidence to establish usage or monetization rights.")

    return EvaluatedSongRights(
        original_artist=original_artist,
        rights_holders=rights_holders,
        original_source_url=bundle.canonical_source_url,
        license_name="Unknown (Unverified)",
        license_url=None,
        copyright_ownership_status=CopyrightOwnershipStatus.UNKNOWN,
        overall_status=OverallRightsStatus.UNKNOWN,
        commercial_use_status=PermissionLevel.UNKNOWN,
        attribution_required=False,
        attribution_template=None,
        platforms=_build_unverified_or_restricted_platforms(
            is_likely_unlicensed_nong=False, is_in_game_curated=False
        ),
        evidence=bundle.items,
        uncertainties=uncertainties,
        conflicting_information=conflicts,
        security_flags=security_flags,
        verification_method=VerificationMethod.CONSERVATIVE_FALLBACK,
        ai_diagnostic_confidence=ai_conf,
    )
