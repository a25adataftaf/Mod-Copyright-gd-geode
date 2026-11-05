#include <mrc/DirectEvaluator.hpp>

#include <algorithm>
#include <array>
#include <cctype>
#include <sstream>

namespace mrc {
namespace {

constexpr std::string_view STANDARD_DISCLAIMER =
    "Informational guidance only, not legal advice. Copyright ownership, creator licences, "
    "platform terms, and automated Content ID / Audible Magic / Rights Manager enforcement are "
    "separate mechanisms; automated claims can still occur and the absence of a claim never "
    "proves legal permission.";

constexpr std::array<Platform, 5> ALL_PLATFORMS = {
    Platform::YouTube,
    Platform::TikTok,
    Platform::Twitch,
    Platform::Instagram,
    Platform::Facebook,
};

std::string toLowerAscii(std::string_view s) {
    std::string out(s.size(), '\0');
    for (std::size_t i = 0; i < s.size(); ++i) {
        out[i] = static_cast<char>(std::tolower(static_cast<unsigned char>(s[i])));
    }
    return out;
}

bool isWordChar(char c) noexcept {
    return std::isalnum(static_cast<unsigned char>(c)) != 0;
}

bool containsWholePhrase(std::string_view lowerHaystack, std::string_view lowerPhrase) noexcept {
    if (lowerPhrase.empty() || lowerHaystack.size() < lowerPhrase.size()) {
        return false;
    }
    std::size_t pos = 0;
    while ((pos = lowerHaystack.find(lowerPhrase, pos)) != std::string_view::npos) {
        const bool leftOk = (pos == 0) || !isWordChar(lowerHaystack[pos - 1]);
        const std::size_t endPos = pos + lowerPhrase.size();
        const bool rightOk = (endPos >= lowerHaystack.size()) || !isWordChar(lowerHaystack[endPos]);
        if (leftOk && rightOk) {
            return true;
        }
        ++pos;
    }
    return false;
}

std::string policyUrlForPlatform(Platform p) {
    switch (p) {
        case Platform::YouTube:
            return "https://support.google.com/youtube/answer/2797370";
        case Platform::TikTok:
            return "https://www.tiktok.com/legal/page/global/music-usage-confirmation/en";
        case Platform::Twitch:
            return "https://www.twitch.tv/p/en/legal/dmca-guidelines/";
        case Platform::Instagram:
            return "https://help.instagram.com/402084904469945";
        case Platform::Facebook:
            return "https://www.facebook.com/legal/music_guidelines";
    }
    return "";
}

std::vector<PlatformRights> makeUniformPlatforms(
    PermissionLevel upload,
    PermissionLevel livestream,
    PermissionLevel commercial,
    PermissionLevel monetization,
    bool attributionReq,
    ClaimRisk risk,
    const std::vector<std::string>& baseConditions,
    const std::vector<std::string>& baseRestrictions
) {
    std::vector<PlatformRights> out;
    out.reserve(ALL_PLATFORMS.size());
    for (Platform p : ALL_PLATFORMS) {
        PlatformRights pr;
        pr.platform = p;
        pr.videoUpload = upload;
        pr.livestream = livestream;
        pr.commercialUse = commercial;
        pr.monetization = monetization;
        pr.attributionRequired = attributionReq;
        pr.automatedClaimRisk = risk;
        pr.conditions = baseConditions;
        pr.restrictions = baseRestrictions;
        pr.policyReferenceUrl = policyUrlForPlatform(p);

        if (p == Platform::YouTube && risk == ClaimRisk::High) {
            pr.restrictions.emplace_back(
                "YouTube Content ID may route ad revenue to rightsholders or block Shorts exceeding 60 seconds."
            );
        } else if (p == Platform::Twitch && risk != ClaimRisk::Low) {
            pr.restrictions.emplace_back(
                "Twitch Audible Magic may mute VOD/Clip segments; live DMCA notices remain possible if unlicensed."
            );
        } else if (p == Platform::TikTok && monetization != PermissionLevel::Allowed) {
            pr.restrictions.emplace_back(
                "TikTok Business accounts and videos over 60 seconds face strict audio muting unless cleared in CML."
            );
        } else if ((p == Platform::Instagram || p == Platform::Facebook) && risk != ClaimRisk::Low) {
            pr.restrictions.emplace_back(
                "Meta Rights Manager may mute Reels, Stories, or Live broadcasts containing third-party recordings."
            );
        }
        out.push_back(std::move(pr));
    }
    return out;
}

struct DetectedCcLicense {
    std::string code;  // "cc0", "cc-by", "cc-by-sa", "cc-by-nc", "cc-by-nc-sa", "cc-by-nd", "cc-by-nc-nd"
    std::string displayName;
    std::string url;
};

std::optional<DetectedCcLicense> detectCreativeCommonsInHtml(std::string_view html) {
    const std::string low = toLowerAscii(html);

    auto extractHrefStartingAt = [&](std::size_t idx) -> std::string {
        std::size_t end = idx;
        while (end < html.size()) {
            char c = html[end];
            if (c == '"' || c == '\'' || c == '<' || c == '>' || std::isspace(static_cast<unsigned char>(c))) {
                break;
            }
            ++end;
        }
        std::string url(html.substr(idx, end - idx));
        if (url.starts_with("http://")) {
            url = "https://" + url.substr(7);
        } else if (url.starts_with("//")) {
            url = "https:" + url;
        }
        return url;
    };

    if (auto p = low.find("creativecommons.org/publicdomain/zero/"); p != std::string::npos) {
        std::size_t start = (p >= 8 && low.substr(p - 8, 8) == "https://") ? p - 8 : p;
        std::string u = extractHrefStartingAt(start);
        if (!u.starts_with("https://")) u = "https://" + u;
        return DetectedCcLicense{"cc0", "CC0 1.0 Universal (Public Domain Dedication)", std::move(u)};
    }

    struct Pattern {
        std::string_view needle;
        const char* code;
        const char* label;
    };
    static constexpr std::array<Pattern, 6> PATTERNS = {{
        {"creativecommons.org/licenses/by-nc-nd/", "cc-by-nc-nd",
         "Creative Commons Attribution-NonCommercial-NoDerivatives (CC BY-NC-ND)"},
        {"creativecommons.org/licenses/by-nc-sa/", "cc-by-nc-sa",
         "Creative Commons Attribution-NonCommercial-ShareAlike (CC BY-NC-SA)"},
        {"creativecommons.org/licenses/by-nc/", "cc-by-nc",
         "Creative Commons Attribution-NonCommercial (CC BY-NC)"},
        {"creativecommons.org/licenses/by-nd/", "cc-by-nd",
         "Creative Commons Attribution-NoDerivatives (CC BY-ND)"},
        {"creativecommons.org/licenses/by-sa/", "cc-by-sa",
         "Creative Commons Attribution-ShareAlike (CC BY-SA)"},
        {"creativecommons.org/licenses/by/", "cc-by",
         "Creative Commons Attribution (CC BY)"},
    }};

    for (const auto& pat : PATTERNS) {
        if (auto p = low.find(pat.needle); p != std::string::npos) {
            std::size_t start = p;
            if (p >= 8 && low.substr(p - 8, 8) == "https://") {
                start = p - 8;
            } else if (p >= 7 && low.substr(p - 7, 7) == "http://") {
                start = p - 7;
            }
            std::string u = extractHrefStartingAt(start);
            if (!u.starts_with("https://")) {
                u = "https://" + u;
            }
            return DetectedCcLicense{pat.code, pat.label, std::move(u)};
        }
    }
    return std::nullopt;
}

}  // namespace

bool DirectEvaluator::hasRemixOrMashupSignals(
    std::string_view title,
    std::string_view artist,
    std::string_view extraText
) noexcept {
    std::string combined = toLowerAscii(title);
    combined.push_back(' ');
    combined += toLowerAscii(artist);
    if (!extraText.empty()) {
        combined.push_back(' ');
        combined += toLowerAscii(extraText.substr(0, 300));
    }

    static constexpr std::array<std::string_view, 11> KEYWORDS = {
        "remix",
        "mashup",
        "bootleg",
        "nightcore",
        "daycore",
        "sped up",
        "slowed",
        "cover",
        "flip",
        "megamix",
        "edit",
    };

    for (std::string_view kw : KEYWORDS) {
        if (containsWholePhrase(combined, kw)) {
            return true;
        }
    }
    return false;
}

std::string DirectEvaluator::sanitizeHtmlExcerpt(std::string_view rawHtml, std::size_t maxChars) {
    // Strip <script>...</script> and <style>...</style> blocks first
    std::string cleaned;
    cleaned.reserve(std::min<std::size_t>(rawHtml.size(), 8192));

    const std::string lower = toLowerAscii(rawHtml);
    std::size_t i = 0;
    while (i < rawHtml.size()) {
        if (lower.compare(i, 7, "<script") == 0) {
            auto closePos = lower.find("</script>", i + 7);
            if (closePos == std::string::npos) break;
            i = closePos + 9;
            cleaned.push_back(' ');
            continue;
        }
        if (lower.compare(i, 6, "<style") == 0) {
            auto closePos = lower.find("</style>", i + 6);
            if (closePos == std::string::npos) break;
            i = closePos + 8;
            cleaned.push_back(' ');
            continue;
        }
        if (rawHtml[i] == '<') {
            if (lower.compare(i, 4, "<br>") == 0 || lower.compare(i, 5, "<br/>") == 0 ||
                lower.compare(i, 6, "<br />") == 0) {
                cleaned.push_back('\n');
            } else {
                cleaned.push_back(' ');
            }
            auto closeAngle = rawHtml.find('>', i + 1);
            if (closeAngle == std::string_view::npos) break;
            i = closeAngle + 1;
            continue;
        }
        if (rawHtml[i] == '&') {
            if (lower.compare(i, 5, "&amp;") == 0) {
                cleaned.push_back('&');
                i += 5;
                continue;
            }
            if (lower.compare(i, 6, "&quot;") == 0) {
                cleaned.push_back('"');
                i += 6;
                continue;
            }
            if (lower.compare(i, 6, "&#039;") == 0 || lower.compare(i, 5, "&#39;") == 0 ||
                lower.compare(i, 6, "&apos;") == 0) {
                cleaned.push_back('\'');
                i += 6 - (lower.compare(i, 5, "&#39;") == 0 ? 1 : 0);
                continue;
            }
            if (lower.compare(i, 4, "&lt;") == 0) {
                cleaned.push_back('<');
                i += 4;
                continue;
            }
            if (lower.compare(i, 4, "&gt;") == 0) {
                cleaned.push_back('>');
                i += 4;
                continue;
            }
            if (lower.compare(i, 6, "&nbsp;") == 0) {
                cleaned.push_back(' ');
                i += 6;
                continue;
            }
        }
        unsigned char uc = static_cast<unsigned char>(rawHtml[i]);
        if (uc == '\n' || uc >= 0x20U) {
            cleaned.push_back(rawHtml[i]);
        }
        ++i;
    }

    // Collapse repeated spaces while preserving single newlines
    std::string compact;
    compact.reserve(std::min(cleaned.size(), maxChars));
    bool prevSpace = true;
    for (char c : cleaned) {
        if (c == '\n') {
            if (!compact.empty() && compact.back() != '\n') {
                compact.push_back('\n');
            }
            prevSpace = true;
        } else if (std::isspace(static_cast<unsigned char>(c))) {
            if (!prevSpace) {
                compact.push_back(' ');
                prevSpace = true;
            }
        } else {
            compact.push_back(c);
            prevSpace = false;
        }
        if (compact.size() >= maxChars) {
            break;
        }
    }
    while (!compact.empty() && (compact.back() == ' ' || compact.back() == '\n')) {
        compact.pop_back();
    }
    return compact;
}

std::string DirectEvaluator::extractNcsAttributionFromHtml(std::string_view ncsHtml) {
    const std::string lower = toLowerAscii(ncsHtml);
    auto idPos = lower.find("panel-copy");
    if (idPos == std::string::npos) {
        return "";
    }
    auto openEnd = ncsHtml.find('>', idPos);
    if (openEnd == std::string_view::npos) {
        return "";
    }
    auto closeP = lower.find("</p>", openEnd + 1);
    if (closeP == std::string::npos || closeP <= openEnd + 1) {
        return "";
    }
    return sanitizeHtmlExcerpt(ncsHtml.substr(openEnd + 1, closeP - (openEnd + 1)), 500);
}

SongRightsRecord DirectEvaluator::evaluate(
    const SongQuery& query,
    const std::optional<DirectFetchedPage>& fetchedPage,
    std::string_view verifiedAtIsoUtc
) {
    SongRightsRecord rec;
    rec.canonicalKey = computeClientCacheKey(query);
    rec.id = "direct:" + rec.canonicalKey;
    rec.sourceType = query.sourceType;
    rec.gdSongId = query.gdSongId;
    rec.title = query.title.empty() ? "Unknown Title" : query.title;
    rec.artist = query.artist.empty() ? "Unknown Artist" : query.artist;
    rec.originalArtist = rec.artist;
    rec.originalSourceUrl = query.sourceUrl;
    rec.verificationMethod = "client_direct_inspector";
    rec.lastVerifiedAtIso = std::string(verifiedAtIsoUtc);
    rec.isStale = false;
    rec.disclaimer = std::string(STANDARD_DISCLAIMER);

    const bool remixDetected = hasRemixOrMashupSignals(rec.title, rec.artist);
    if (remixDetected) {
        rec.uncertainties.emplace_back(
            "Title or artist indicates a remix, mashup, cover, or edit; underlying master and publishing rights of sampled works require separate clearance."
        );
    }

    // -------------------------------------------------------------------------
    // 1. NoCopyrightSounds (NCS Library)
    // -------------------------------------------------------------------------
    if (query.sourceType == SourceType::NcsLibrary) {
        rec.rightsHolders = {rec.artist, "NoCopyrightSounds (NCS)"};
        rec.copyrightOwnershipStatus = "Participating Artist / NoCopyrightSounds (NCS)";
        rec.licenseName = "NCS Creator Usage Policy (Independent Creators Only)";
        rec.licenseUrl = "https://ncs.io/usage-policy";
        if (rec.originalSourceUrl.empty()) {
            rec.originalSourceUrl = "https://ncs.io/music";
        }
        rec.overallStatus = remixDetected ? OverallStatus::ReviewRequired
                                          : OverallStatus::PermittedWithConditions;
        rec.commercialUseStatus = PermissionLevel::Conditional;
        rec.attributionRequired = true;

        std::string extractedCopy;
        if (fetchedPage.has_value() && fetchedPage->httpStatus == 200) {
            extractedCopy = extractNcsAttributionFromHtml(fetchedPage->rawBody);
            rec.originalSourceUrl = fetchedPage->url;
            rec.evidence.push_back(EvidenceItem{
                .sourceKind = "ncs_release_page",
                .sourceTitle = "NCS Track Page",
                .sourceUrl = fetchedPage->url,
                .fetchStatus = "verified_http_200",
                .excerpt = extractedCopy.empty() ? "Verified active release on ncs.io." : extractedCopy,
                .retrievedAtIso = rec.lastVerifiedAtIso,
            });
        }

        rec.evidence.push_back(EvidenceItem{
            .sourceKind = "ncs_usage_policy",
            .sourceTitle = "NCS Official Creator Usage Policy",
            .sourceUrl = "https://ncs.io/usage-policy",
            .fetchStatus = "policy_rule",
            .excerpt = "Independent YouTube and Twitch creators may monetize videos/streams when full NCS track attribution and links are included in the description or stream panel. Commercial brands require a separate licence.",
            .retrievedAtIso = rec.lastVerifiedAtIso,
        });

        if (!extractedCopy.empty()) {
            rec.attributionTemplate = std::move(extractedCopy);
        } else {
            std::ostringstream credit;
            credit << "Song: " << rec.artist << " - " << rec.title << " [NCS Release]\n"
                   << "Music provided by NoCopyrightSounds\n"
                   << "Usage Policy: https://ncs.io/usage-policy";
            rec.attributionTemplate = credit.str();
        }

        for (Platform p : ALL_PLATFORMS) {
            PlatformRights pr;
            pr.platform = p;
            pr.attributionRequired = true;
            pr.policyReferenceUrl = "https://ncs.io/usage-policy";
            pr.restrictions.emplace_back(
                "Brands, companies, paid advertisements, and standalone commercial products require a paid commercial licence from NCS."
            );

            if (p == Platform::YouTube || p == Platform::Twitch) {
                pr.videoUpload = PermissionLevel::Allowed;
                pr.livestream = PermissionLevel::Allowed;
                pr.commercialUse = PermissionLevel::Conditional;
                pr.monetization = remixDetected ? PermissionLevel::Unknown : PermissionLevel::Conditional;
                pr.automatedClaimRisk = remixDetected ? ClaimRisk::Moderate : ClaimRisk::Low;
                pr.conditions.emplace_back(
                    p == Platform::YouTube
                        ? "Paste full NCS attribution block in the YouTube video description."
                        : "Include NCS credit in your Twitch stream panel or broadcast info."
                );
            } else {
                pr.videoUpload = PermissionLevel::Conditional;
                pr.livestream = PermissionLevel::Conditional;
                pr.commercialUse = PermissionLevel::Conditional;
                pr.monetization = PermissionLevel::Conditional;
                pr.automatedClaimRisk = ClaimRisk::Moderate;
                pr.conditions.emplace_back(
                    "Attach the official NCS sound from the platform's native audio library where available and credit NCS in the caption."
                );
            }
            rec.platforms.push_back(std::move(pr));
        }
        return rec;
    }

    // -------------------------------------------------------------------------
    // 2. Newgrounds Audio Portal
    // -------------------------------------------------------------------------
    if (query.sourceType == SourceType::Newgrounds) {
        if (rec.originalSourceUrl.empty() && query.gdSongId.has_value() && *query.gdSongId > 0) {
            rec.originalSourceUrl =
                "https://www.newgrounds.com/audio/listen/" + std::to_string(*query.gdSongId);
        }
        rec.rightsHolders = {rec.artist};

        if (fetchedPage.has_value() && fetchedPage->httpStatus == 200 && !fetchedPage->rawBody.empty()) {
            auto ccOpt = detectCreativeCommonsInHtml(fetchedPage->rawBody);
            const std::string lowerHtml = toLowerAscii(fetchedPage->rawBody);

            if (ccOpt.has_value()) {
                rec.licenseName = ccOpt->displayName;
                rec.licenseUrl = ccOpt->url;
                rec.copyrightOwnershipStatus = "Creator-uploaded Newgrounds work (" + rec.artist + ")";
                rec.evidence.push_back(EvidenceItem{
                    .sourceKind = "newgrounds_audio_page",
                    .sourceTitle = "Newgrounds Audio Portal Licence Block",
                    .sourceUrl = fetchedPage->url,
                    .fetchStatus = "verified_http_200",
                    .excerpt = "Detected explicit Creative Commons licence link: " + ccOpt->url,
                    .retrievedAtIso = rec.lastVerifiedAtIso,
                });

                if (ccOpt->code == "cc0") {
                    rec.overallStatus = remixDetected ? OverallStatus::ReviewRequired : OverallStatus::Permitted;
                    rec.commercialUseStatus = remixDetected ? PermissionLevel::Unknown : PermissionLevel::Allowed;
                    rec.attributionRequired = false;
                    rec.attributionTemplate =
                        "Music: \"" + rec.title + "\" by " + rec.artist + "\nSource: " + rec.originalSourceUrl +
                        "\nLicence: CC0 1.0 (" + rec.licenseUrl + ")";
                    rec.platforms = makeUniformPlatforms(
                        PermissionLevel::Allowed,
                        PermissionLevel::Allowed,
                        rec.commercialUseStatus,
                        rec.commercialUseStatus,
                        false,
                        remixDetected ? ClaimRisk::Moderate : ClaimRisk::Low,
                        {"Verify no uncredited third-party samples are included before commercial use."},
                        {}
                    );
                    return rec;
                }

                if (ccOpt->code == "cc-by") {
                    rec.overallStatus =
                        remixDetected ? OverallStatus::ReviewRequired : OverallStatus::PermittedWithConditions;
                    rec.commercialUseStatus =
                        remixDetected ? PermissionLevel::Unknown : PermissionLevel::Conditional;
                    rec.attributionRequired = true;
                    rec.attributionTemplate =
                        "Music: \"" + rec.title + "\" by " + rec.artist + "\nSource: " + rec.originalSourceUrl +
                        "\nLicensed under " + rec.licenseName + " (" + rec.licenseUrl + ")\nUsed in gameplay video (synchronized with gameplay audio/visuals).";
                    rec.platforms = makeUniformPlatforms(
                        PermissionLevel::Conditional,
                        PermissionLevel::Conditional,
                        rec.commercialUseStatus,
                        rec.commercialUseStatus,
                        true,
                        ClaimRisk::Moderate,
                        {
                            "Provide full TASL attribution (Title, Author, Source link, Licence URL) and note if audio was edited.",
                            "Confirm the uploader is the original rights holder; third-party Content ID claims can still occur."
                        },
                        {"Do not imply endorsement by the artist."}
                    );
                    return rec;
                }

                if (ccOpt->code == "cc-by-sa") {
                    rec.overallStatus = OverallStatus::ReviewRequired;
                    rec.commercialUseStatus = PermissionLevel::Conditional;
                    rec.attributionRequired = true;
                    rec.attributionTemplate =
                        "Music: \"" + rec.title + "\" by " + rec.artist + "\nSource: " + rec.originalSourceUrl +
                        "\nLicensed under " + rec.licenseName + " (" + rec.licenseUrl + ")";
                    rec.uncertainties.emplace_back(
                        "CC BY-SA requires adaptations (including synced gameplay videos) to be distributed under CC BY-SA or a compatible licence, which conflicts with standard proprietary platform terms on TikTok/Twitch/Meta unless separate artist permission is obtained."
                    );
                    rec.platforms = makeUniformPlatforms(
                        PermissionLevel::Conditional,
                        PermissionLevel::Conditional,
                        PermissionLevel::Conditional,
                        PermissionLevel::Conditional,
                        true,
                        ClaimRisk::Moderate,
                        {"Requires full TASL credit AND releasing the video under a ShareAlike-compatible licence (e.g. YouTube CC-BY where compatible) or obtaining direct permission."},
                        {"Standard 'All Rights Reserved' uploads violate ShareAlike terms."}
                    );
                    return rec;
                }

                if (ccOpt->code == "cc-by-nc" || ccOpt->code == "cc-by-nc-sa") {
                    rec.overallStatus = OverallStatus::ConditionalNonCommercial;
                    rec.commercialUseStatus = PermissionLevel::NotAllowed;
                    rec.attributionRequired = true;
                    rec.attributionTemplate =
                        "Music: \"" + rec.title + "\" by " + rec.artist + "\nSource: " + rec.originalSourceUrl +
                        "\nLicensed under " + rec.licenseName + " (" + rec.licenseUrl + ") [Non-Commercial Use Only]";
                    rec.platforms = makeUniformPlatforms(
                        PermissionLevel::Conditional,
                        PermissionLevel::Conditional,
                        PermissionLevel::NotAllowed,
                        PermissionLevel::NotAllowed,
                        true,
                        ClaimRisk::Moderate,
                        {"Permitted strictly for non-commercial uploads with full TASL attribution."},
                        {"Ad revenue monetization, channel memberships, sponsorships, and commercial promotions are NOT permitted without a separate licence from the artist."}
                    );
                    return rec;
                }

                if (ccOpt->code == "cc-by-nd" || ccOpt->code == "cc-by-nc-nd") {
                    rec.overallStatus = OverallStatus::Restricted;
                    rec.commercialUseStatus = PermissionLevel::NotAllowed;
                    rec.attributionRequired = true;
                    rec.uncertainties.emplace_back(
                        "NoDerivatives (ND) Creative Commons licences do not permit synchronizing music into a video (considered an adaptation under CC legal text) without separate permission from the artist."
                    );
                    rec.platforms = makeUniformPlatforms(
                        PermissionLevel::NotAllowed,
                        PermissionLevel::NotAllowed,
                        PermissionLevel::NotAllowed,
                        PermissionLevel::NotAllowed,
                        true,
                        ClaimRisk::High,
                        {},
                        {"Synchronizing ND-licensed audio into gameplay videos requires separate written permission from the artist."}
                    );
                    return rec;
                }
            }

            if (lowerHtml.find("all rights reserved") != std::string::npos) {
                rec.licenseName = "All Rights Reserved (Newgrounds)";
                rec.copyrightOwnershipStatus = "All Rights Reserved by " + rec.artist;
                rec.overallStatus = OverallStatus::Restricted;
                rec.commercialUseStatus = PermissionLevel::NotAllowed;
                rec.evidence.push_back(EvidenceItem{
                    .sourceKind = "newgrounds_audio_page",
                    .sourceTitle = "Newgrounds Licensing Block",
                    .sourceUrl = fetchedPage->url,
                    .fetchStatus = "verified_http_200",
                    .excerpt = "Page indicates All Rights Reserved with no Creative Commons grant.",
                    .retrievedAtIso = rec.lastVerifiedAtIso,
                });
                rec.platforms = makeUniformPlatforms(
                    PermissionLevel::NotAllowed,
                    PermissionLevel::NotAllowed,
                    PermissionLevel::NotAllowed,
                    PermissionLevel::NotAllowed,
                    false,
                    ClaimRisk::High,
                    {},
                    {"No public synchronization or monetization licence granted on Newgrounds; obtain direct artist permission."}
                );
                return rec;
            }
        }

        // Newgrounds page blocked (e.g. Cloudflare 403/timeout) or licence block ambiguous
        rec.licenseName = "Unverified Newgrounds Licence (Check Source Page)";
        rec.overallStatus = OverallStatus::Unknown;
        rec.commercialUseStatus = PermissionLevel::Unknown;
        if (fetchedPage.has_value() && fetchedPage->httpStatus != 200) {
            rec.evidence.push_back(EvidenceItem{
                .sourceKind = "newgrounds_audio_page",
                .sourceTitle = "Newgrounds Audio Portal",
                .sourceUrl = fetchedPage->url,
                .fetchStatus = "http_" + std::to_string(fetchedPage->httpStatus),
                .excerpt = "Automated check received HTTP " + std::to_string(fetchedPage->httpStatus) +
                           " (anti-bot challenge or network block). Click 'Source Page' to inspect the licensing box in your browser.",
                .retrievedAtIso = rec.lastVerifiedAtIso,
            });
        }
        rec.uncertainties.emplace_back(
            "Could not automatically verify the Creative Commons box on Newgrounds. Open 'Source Page' to check whether the artist selected CC-BY, NonCommercial (NC), NoDerivatives (ND), or All Rights Reserved."
        );
        rec.platforms = makeUniformPlatforms(
            PermissionLevel::Unknown,
            PermissionLevel::Unknown,
            PermissionLevel::Unknown,
            PermissionLevel::Unknown,
            true,
            ClaimRisk::Moderate,
            {"Click 'Source Page' below to verify the exact Newgrounds licensing terms before monetizing."},
            {"Do not enable ad monetization until licence terms are confirmed."}
        );
        return rec;
    }

    // -------------------------------------------------------------------------
    // 3. Official RobTop Songs & Geometry Dash Music Library
    // -------------------------------------------------------------------------
    if (query.sourceType == SourceType::RobTopOfficial || query.sourceType == SourceType::GdMusicLibrary) {
        const bool isKevinMacLeod = toLowerAscii(rec.artist).find("kevin macleod") != std::string::npos;
        rec.rightsHolders = {rec.artist};
        rec.originalSourceUrl = isKevinMacLeod
                                    ? "https://incompetech.com/music/royalty-free/licenses/"
                                    : "https://www.robtopgames.com/faq/en/answers/music/";
        rec.licenseUrl = rec.originalSourceUrl;

        if (isKevinMacLeod) {
            rec.licenseName = "Kevin MacLeod Standard Licence (Typically CC BY 4.0 with Exact Credit)";
            rec.copyrightOwnershipStatus = "Kevin MacLeod (Incompetech)";
            rec.overallStatus = OverallStatus::PermittedWithConditions;
            rec.commercialUseStatus = PermissionLevel::Conditional;
            rec.attributionRequired = true;
            rec.attributionTemplate =
                "\"" + rec.title + "\" Kevin MacLeod (incompetech.com)\n"
                "Licensed under Creative Commons: By Attribution 4.0 License\n"
                "https://creativecommons.org/licenses/by/4.0/";
            rec.evidence.push_back(EvidenceItem{
                .sourceKind = "artist_license_policy",
                .sourceTitle = "Incompetech Royalty-Free Music Licences",
                .sourceUrl = "https://incompetech.com/music/royalty-free/licenses/",
                .fetchStatus = "policy_rule",
                .excerpt = "Kevin MacLeod's Incompetech tracks permit video monetization when the exact 3-line attribution block is included in the video description.",
                .retrievedAtIso = rec.lastVerifiedAtIso,
            });
            rec.platforms = makeUniformPlatforms(
                PermissionLevel::Conditional,
                PermissionLevel::Conditional,
                PermissionLevel::Conditional,
                PermissionLevel::Conditional,
                true,
                ClaimRisk::Low,
                {"Include the exact 3-line Kevin MacLeod (incompetech.com) CC BY 4.0 credit in the description."},
                {"Omitting attribution violates the licence and requires purchasing a no-attribution licence."}
            );
            return rec;
        }

        rec.copyrightOwnershipStatus =
            query.sourceType == SourceType::RobTopOfficial
                ? "Licensed to RobTop Games for in-game use; external rights held by " + rec.artist
                : "Geometry Dash Music Library partner artist (" + rec.artist + ")";
        rec.licenseName = "In-Game GD Licence (External Video Monetization Varies by Artist)";
        rec.overallStatus = OverallStatus::ReviewRequired;
        rec.commercialUseStatus = PermissionLevel::Conditional;
        rec.attributionRequired = true;
        rec.attributionTemplate =
            "Music: \"" + rec.title + "\" by " + rec.artist +
            "\nFeatured in Geometry Dash (check artist's official usage policy for monetization terms).";

        rec.evidence.push_back(EvidenceItem{
            .sourceKind = "gd_music_faq",
            .sourceTitle = "RobTop Games Official Music FAQ",
            .sourceUrl = "https://www.robtopgames.com/faq/en/answers/music/",
            .fetchStatus = "policy_rule",
            .excerpt = "In-game music rights in Geometry Dash do not automatically grant external video monetization across all platforms; individual artists and distributors set YouTube Content ID, Twitch VOD, and TikTok rules.",
            .retrievedAtIso = rec.lastVerifiedAtIso,
        });
        rec.uncertainties.emplace_back(
            "Verify " + rec.artist + "'s individual creator usage policy: many GD artists allow gameplay uploads with credit, while distributors may claim YouTube ad revenue via Content ID or mute Twitch VODs."
        );

        rec.platforms = makeUniformPlatforms(
            PermissionLevel::Conditional,
            PermissionLevel::Conditional,
            PermissionLevel::Unknown,
            PermissionLevel::ConditionalRevenueShare,
            true,
            ClaimRisk::Moderate,
            {
                "Credit the artist (" + rec.artist + ") and track title in the description.",
                "Check the artist's official website/YouTube FAQ before enabling commercial monetization."
            },
            {}
        );
        return rec;
    }

    // -------------------------------------------------------------------------
    // 4. Jukebox Hosted (Song File Hub) & YouTube NONG Replacements
    // -------------------------------------------------------------------------
    if (query.sourceType == SourceType::JukeboxHosted || query.sourceType == SourceType::JukeboxYoutube) {
        rec.rightsHolders = {rec.artist, "Original Record Label / Music Publisher (Unverified NONG)"};
        rec.copyrightOwnershipStatus = "Third-Party Copyrighted Recording (NONG Replacement)";
        rec.licenseName = "No Creator Sync/Monetization Licence Verified (NONG)";
        rec.overallStatus = OverallStatus::Restricted;
        rec.commercialUseStatus = PermissionLevel::NotAllowed;
        rec.attributionRequired = false;

        rec.evidence.push_back(EvidenceItem{
            .sourceKind = "jukebox_nong_manifest",
            .sourceTitle = query.sourceType == SourceType::JukeboxHosted
                               ? "Jukebox Hosted / Song File Hub NONG"
                               : "Jukebox YouTube NONG",
            .sourceUrl = rec.originalSourceUrl,
            .fetchStatus = "manifest_inspected",
            .excerpt = "NONG replacement tracks bypass Newgrounds/NCS licensing checks and commonly contain commercial label masters subject to automated Content ID, Audible Magic VOD muting, and DMCA enforcement.",
            .retrievedAtIso = rec.lastVerifiedAtIso,
        });
        rec.uncertainties.emplace_back(
            "Giving credit in a description does NOT grant legal permission for commercial NONG tracks."
        );

        rec.platforms = makeUniformPlatforms(
            PermissionLevel::NotAllowed,
            PermissionLevel::NotAllowed,
            PermissionLevel::NotAllowed,
            PermissionLevel::NotAllowed,
            false,
            ClaimRisk::High,
            {},
            {
                "High likelihood of Content ID monetization/blocking claims, Shorts >60s blocks, Twitch VOD muting, or DMCA takedowns unless you hold direct permission."
            }
        );
        return rec;
    }

    // -------------------------------------------------------------------------
    // 5. Jukebox Locally Imported Audio Files & Custom Sources
    // -------------------------------------------------------------------------
    rec.rightsHolders = {rec.artist};
    rec.copyrightOwnershipStatus = "Unverified Local Audio File";
    rec.licenseName = "Unknown (Locally Imported Audio)";
    rec.overallStatus = OverallStatus::Unknown;
    rec.commercialUseStatus = PermissionLevel::Unknown;
    rec.evidence.push_back(EvidenceItem{
        .sourceKind = "jukebox_local_metadata",
        .sourceTitle = "Local NONG Metadata Only (Audio Never Uploaded)",
        .sourceUrl = "",
        .fetchStatus = "local_metadata",
        .excerpt = "Evaluated locally from Jukebox manifest metadata without uploading your audio file.",
        .retrievedAtIso = rec.lastVerifiedAtIso,
    });
    rec.uncertainties.emplace_back(
        "Local audio files have no attached public licence proof. Assume commercial/monetized use is NOT permitted unless you created the music or hold a written licence."
    );
    rec.platforms = makeUniformPlatforms(
        PermissionLevel::Unknown,
        PermissionLevel::Unknown,
        PermissionLevel::Unknown,
        PermissionLevel::Unknown,
        false,
        ClaimRisk::High,
        {"Verify the original source and licence of your local audio file before uploading or streaming."},
        {"Do not monetize unverified local audio files."}
    );
    return rec;
}

}  // namespace mrc
