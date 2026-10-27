#include <mrc/Types.hpp>

#include <algorithm>
#include <cctype>

namespace mrc {
namespace {

std::string normalizeSlug(std::string_view input) {
    std::string out;
    out.reserve(input.size());
    bool prevDash = false;
    for (unsigned char ch : input) {
        if (std::isalnum(ch)) {
            out.push_back(static_cast<char>(std::tolower(ch)));
            prevDash = false;
        } else if (!prevDash && !out.empty()) {
            out.push_back('-');
            prevDash = true;
        }
        if (out.size() >= 96) {
            break;
        }
    }
    while (!out.empty() && out.back() == '-') {
        out.pop_back();
    }
    return out.empty() ? "unknown" : out;
}

}  // namespace

std::string_view toApiString(SourceType type) noexcept {
    switch (type) {
        case SourceType::RobTopOfficial:
            return "robtop_official";
        case SourceType::GdMusicLibrary:
            return "gd_music_library";
        case SourceType::NcsLibrary:
            return "ncs_library";
        case SourceType::Newgrounds:
            return "newgrounds";
        case SourceType::JukeboxLocal:
            return "jukebox_local";
        case SourceType::JukeboxYoutube:
            return "jukebox_youtube";
        case SourceType::JukeboxHosted:
            return "jukebox_hosted";
        case SourceType::CustomLibrary:
            return "custom_library";
    }
    return "newgrounds";
}

SourceType sourceTypeFromApiString(std::string_view raw) noexcept {
    if (raw == "robtop_official") return SourceType::RobTopOfficial;
    if (raw == "gd_music_library") return SourceType::GdMusicLibrary;
    if (raw == "ncs_library") return SourceType::NcsLibrary;
    if (raw == "newgrounds") return SourceType::Newgrounds;
    if (raw == "jukebox_local") return SourceType::JukeboxLocal;
    if (raw == "jukebox_youtube") return SourceType::JukeboxYoutube;
    if (raw == "jukebox_hosted") return SourceType::JukeboxHosted;
    if (raw == "custom_library") return SourceType::CustomLibrary;
    return SourceType::CustomLibrary;
}

std::string_view toApiString(Platform platform) noexcept {
    switch (platform) {
        case Platform::YouTube:
            return "youtube";
        case Platform::TikTok:
            return "tiktok";
        case Platform::Twitch:
            return "twitch";
        case Platform::Instagram:
            return "instagram";
        case Platform::Facebook:
            return "facebook";
    }
    return "youtube";
}

std::string_view platformDisplayName(Platform platform) noexcept {
    switch (platform) {
        case Platform::YouTube:
            return "YouTube";
        case Platform::TikTok:
            return "TikTok";
        case Platform::Twitch:
            return "Twitch";
        case Platform::Instagram:
            return "Instagram";
        case Platform::Facebook:
            return "Facebook";
    }
    return "YouTube";
}

std::optional<Platform> platformFromApiString(std::string_view raw) noexcept {
    if (raw == "youtube") return Platform::YouTube;
    if (raw == "tiktok") return Platform::TikTok;
    if (raw == "twitch") return Platform::Twitch;
    if (raw == "instagram") return Platform::Instagram;
    if (raw == "facebook") return Platform::Facebook;
    return std::nullopt;
}

std::string_view toApiString(OverallStatus status) noexcept {
    switch (status) {
        case OverallStatus::Permitted:
            return "permitted";
        case OverallStatus::PermittedWithConditions:
            return "permitted_with_conditions";
        case OverallStatus::ConditionalNonCommercial:
            return "conditional_non_commercial";
        case OverallStatus::Restricted:
            return "restricted";
        case OverallStatus::ReviewRequired:
            return "review_required";
        case OverallStatus::Unknown:
            return "unknown";
    }
    return "unknown";
}

OverallStatus overallStatusFromApiString(std::string_view raw) noexcept {
    if (raw == "permitted") return OverallStatus::Permitted;
    if (raw == "permitted_with_conditions") return OverallStatus::PermittedWithConditions;
    if (raw == "conditional_non_commercial") return OverallStatus::ConditionalNonCommercial;
    if (raw == "restricted") return OverallStatus::Restricted;
    if (raw == "review_required") return OverallStatus::ReviewRequired;
    return OverallStatus::Unknown;
}

std::string_view toApiString(PermissionLevel perm) noexcept {
    switch (perm) {
        case PermissionLevel::Allowed:
            return "allowed";
        case PermissionLevel::Conditional:
            return "conditional";
        case PermissionLevel::ConditionalRevenueShare:
            return "conditional_revenue_share";
        case PermissionLevel::NotAllowed:
            return "not_allowed";
        case PermissionLevel::Unknown:
            return "unknown";
    }
    return "unknown";
}

std::string_view permissionDisplayLabel(PermissionLevel perm) noexcept {
    switch (perm) {
        case PermissionLevel::Allowed:
            return "Permitted";
        case PermissionLevel::Conditional:
            return "Conditional";
        case PermissionLevel::ConditionalRevenueShare:
            return "Revenue Share";
        case PermissionLevel::NotAllowed:
            return "Not Permitted";
        case PermissionLevel::Unknown:
            return "Unverified";
    }
    return "Unverified";
}

PermissionLevel permissionFromApiString(std::string_view raw) noexcept {
    if (raw == "allowed") return PermissionLevel::Allowed;
    if (raw == "conditional") return PermissionLevel::Conditional;
    if (raw == "conditional_revenue_share") return PermissionLevel::ConditionalRevenueShare;
    if (raw == "not_allowed") return PermissionLevel::NotAllowed;
    return PermissionLevel::Unknown;
}

std::string_view toApiString(ClaimRisk risk) noexcept {
    switch (risk) {
        case ClaimRisk::Low:
            return "low";
        case ClaimRisk::Moderate:
            return "moderate";
        case ClaimRisk::High:
            return "high";
        case ClaimRisk::Unknown:
            return "unknown";
    }
    return "unknown";
}

ClaimRisk claimRiskFromApiString(std::string_view raw) noexcept {
    if (raw == "low") return ClaimRisk::Low;
    if (raw == "moderate") return ClaimRisk::Moderate;
    if (raw == "high") return ClaimRisk::High;
    return ClaimRisk::Unknown;
}

int adjustJukeboxSongId(int songId, bool isRobtopSong) noexcept {
    return isRobtopSong ? (songId < 0 ? songId : -songId - 1) : songId;
}

SourceType classifyDefaultGdSong(
    int songId,
    bool isRobtopSong,
    bool isMusicLibrary,
    bool hasVisibleNcsLogo,
    int songObjectNongType
) noexcept {
    if (isRobtopSong || songId < 0) {
        return SourceType::RobTopOfficial;
    }
    // In Geometry Dash 2.206+, NCS tracks inside the Music Library set m_nongType == 1 and/or m_ncsLogo visible
    if (hasVisibleNcsLogo || songObjectNongType == 1) {
        return SourceType::NcsLibrary;
    }
    if (isMusicLibrary || songId >= 10000000) {
        return SourceType::GdMusicLibrary;
    }
    return SourceType::Newgrounds;
}

std::string computeClientCacheKey(const SongQuery& query) {
    if (query.sourceType == SourceType::RobTopOfficial && query.gdSongId.has_value()) {
        return "robtop:" + std::to_string(*query.gdSongId);
    }
    if (query.sourceType == SourceType::GdMusicLibrary && query.gdSongId.has_value()) {
        return "gdml:" + std::to_string(*query.gdSongId);
    }
    if (query.sourceType == SourceType::NcsLibrary) {
        if (query.gdSongId.has_value()) {
            return "ncs:gd:" + std::to_string(*query.gdSongId);
        }
        return "ncs:meta:" + normalizeSlug(query.artist) + ":" + normalizeSlug(query.title);
    }
    if (query.sourceType == SourceType::Newgrounds && query.gdSongId.has_value() && *query.gdSongId > 0) {
        return "ng:" + std::to_string(*query.gdSongId);
    }
    if (query.sourceType == SourceType::JukeboxYoutube && !query.youtubeId.empty()) {
        return "jb_yt:" + query.youtubeId;
    }
    if (query.sourceType == SourceType::JukeboxHosted && !query.sourceUrl.empty()) {
        return "jb_url:" + query.sourceUrl.substr(0, 190);
    }
    if (query.sourceType == SourceType::JukeboxLocal && !query.audioSha256.empty()) {
        return "jb_local_sha256:" + query.audioSha256;
    }
    return std::string(toApiString(query.sourceType)) + ":meta:" + normalizeSlug(query.artist) + ":" +
           normalizeSlug(query.title);
}

BadgeStyle badgeStyleForRecord(
    const std::optional<SongRightsRecord>& record,
    bool isLoading,
    bool servedFromOfflineFallback
) {
    if (isLoading) {
        return BadgeStyle{
            .shortTag = "MRC ...",
            .headline = "Checking music rights...",
            .subline = "Querying backend asynchronously",
            .color = ColorRGB{120, 175, 245},
            .staleOrOfflineWarning = false,
        };
    }

    if (!record.has_value()) {
        return BadgeStyle{
            .shortTag = "MRC ?",
            .headline = "Rights Not Checked",
            .subline = "Click to inspect song permissions",
            .color = ColorRGB{175, 180, 190},
            .staleOrOfflineWarning = servedFromOfflineFallback,
        };
    }

    const bool stale = record->isStale || servedFromOfflineFallback;
    const std::string staleSuffix = stale ? " (Cached/Stale)" : "";

    switch (record->overallStatus) {
        case OverallStatus::Permitted:
            return BadgeStyle{
                .shortTag = stale ? "MRC OK~" : "MRC OK",
                .headline = "Permitted (Check Platform Terms)" + staleSuffix,
                .subline = "Verified licence; claims still possible",
                .color = stale ? ColorRGB{150, 205, 130} : ColorRGB{62, 209, 118},
                .staleOrOfflineWarning = stale,
            };
        case OverallStatus::PermittedWithConditions:
            return BadgeStyle{
                .shortTag = stale ? "MRC CR~" : "MRC CR*",
                .headline = "Permitted With Conditions" + staleSuffix,
                .subline = record->attributionRequired
                               ? "Attribution & platform rules required"
                               : "Platform-specific conditions apply",
                .color = stale ? ColorRGB{195, 195, 110} : ColorRGB{88, 214, 141},
                .staleOrOfflineWarning = stale,
            };
        case OverallStatus::ConditionalNonCommercial:
            return BadgeStyle{
                .shortTag = "MRC NC",
                .headline = "Non-Commercial Only" + staleSuffix,
                .subline = "Monetization NOT permitted by licence",
                .color = ColorRGB{245, 176, 65},
                .staleOrOfflineWarning = stale,
            };
        case OverallStatus::Restricted:
            return BadgeStyle{
                .shortTag = "MRC !",
                .headline = "Restricted / Unlicensed" + staleSuffix,
                .subline = "High claim/takedown risk; do not monetize",
                .color = ColorRGB{231, 76, 60},
                .staleOrOfflineWarning = stale,
            };
        case OverallStatus::ReviewRequired:
            return BadgeStyle{
                .shortTag = "MRC REV",
                .headline = "Review Required" + staleSuffix,
                .subline = "Conflicting or incomplete external rights",
                .color = ColorRGB{243, 156, 18},
                .staleOrOfflineWarning = stale,
            };
        case OverallStatus::Unknown:
            return BadgeStyle{
                .shortTag = "MRC ?",
                .headline = "Unknown / Unverified" + staleSuffix,
                .subline = "Insufficient evidence; assume unverified",
                .color = ColorRGB{166, 172, 175},
                .staleOrOfflineWarning = stale,
            };
    }

    return BadgeStyle{
        .shortTag = "MRC ?",
        .headline = "Unknown",
        .subline = "Click for details",
        .color = ColorRGB{166, 172, 175},
        .staleOrOfflineWarning = stale,
    };
}

}  // namespace mrc
