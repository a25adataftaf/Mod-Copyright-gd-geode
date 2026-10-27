#pragma once

#include <cstdint>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

namespace mrc {

enum class SourceType {
    RobTopOfficial,
    GdMusicLibrary,
    NcsLibrary,
    Newgrounds,
    JukeboxLocal,
    JukeboxYoutube,
    JukeboxHosted,
    CustomLibrary,
};

enum class Platform {
    YouTube,
    TikTok,
    Twitch,
    Instagram,
    Facebook,
};

enum class OverallStatus {
    Permitted,
    PermittedWithConditions,
    ConditionalNonCommercial,
    Restricted,
    ReviewRequired,
    Unknown,
};

enum class PermissionLevel {
    Allowed,
    Conditional,
    ConditionalRevenueShare,
    NotAllowed,
    Unknown,
};

enum class ClaimRisk {
    Low,
    Moderate,
    High,
    Unknown,
};

struct ColorRGB {
    std::uint8_t r = 180;
    std::uint8_t g = 180;
    std::uint8_t b = 180;
};

struct BadgeStyle {
    std::string shortTag;
    std::string headline;
    std::string subline;
    ColorRGB color;
    bool staleOrOfflineWarning = false;
};

struct SongQuery {
    SourceType sourceType = SourceType::Newgrounds;
    std::optional<int> gdSongId;
    std::string jukeboxUniqueId;
    std::string jukeboxIndexId;
    std::string title;
    std::string artist = "Unknown Artist";
    std::string sourceUrl;
    std::string youtubeId;
    std::string localFilePath;
    std::string audioSha256;
};

struct PlatformRights {
    Platform platform = Platform::YouTube;
    PermissionLevel videoUpload = PermissionLevel::Unknown;
    PermissionLevel livestream = PermissionLevel::Unknown;
    PermissionLevel commercialUse = PermissionLevel::Unknown;
    PermissionLevel monetization = PermissionLevel::Unknown;
    bool attributionRequired = false;
    ClaimRisk automatedClaimRisk = ClaimRisk::Unknown;
    std::vector<std::string> conditions;
    std::vector<std::string> restrictions;
    std::string policyReferenceUrl;
};

struct EvidenceItem {
    std::string sourceKind;
    std::string sourceTitle;
    std::string sourceUrl;
    std::string fetchStatus;
    std::string excerpt;
    std::string retrievedAtIso;
};

struct SongRightsRecord {
    std::string id;
    std::string canonicalKey;
    SourceType sourceType = SourceType::Newgrounds;
    std::optional<int> gdSongId;
    std::string title;
    std::string artist;
    std::string originalArtist;
    std::vector<std::string> rightsHolders;
    std::string originalSourceUrl;
    std::string licenseName = "Unknown";
    std::string licenseUrl;
    std::string copyrightOwnershipStatus = "unknown";
    OverallStatus overallStatus = OverallStatus::Unknown;
    PermissionLevel commercialUseStatus = PermissionLevel::Unknown;
    bool attributionRequired = false;
    std::string attributionTemplate;
    std::vector<PlatformRights> platforms;
    std::vector<EvidenceItem> evidence;
    std::vector<std::string> uncertainties;
    std::vector<std::string> conflictingInformation;
    std::string verificationMethod = "conservative_fallback";
    std::string reviewerNotes;
    std::string lastVerifiedAtIso;
    std::string expiresAtIso;
    std::int64_t cachedAtUnix = 0;
    std::int64_t localExpiresAtUnix = 0;
    bool isStale = false;
    std::string disclaimer;
};

struct ResolveApiResult {
    std::string resolutionStatus;  // "ready" or "processing"
    bool cached = false;
    std::string jobId;
    std::optional<SongRightsRecord> record;
};

// Conversions and helpers
[[nodiscard]] std::string_view toApiString(SourceType type) noexcept;
[[nodiscard]] SourceType sourceTypeFromApiString(std::string_view raw) noexcept;

[[nodiscard]] std::string_view toApiString(Platform platform) noexcept;
[[nodiscard]] std::string_view platformDisplayName(Platform platform) noexcept;
[[nodiscard]] std::optional<Platform> platformFromApiString(std::string_view raw) noexcept;

[[nodiscard]] std::string_view toApiString(OverallStatus status) noexcept;
[[nodiscard]] OverallStatus overallStatusFromApiString(std::string_view raw) noexcept;

[[nodiscard]] std::string_view toApiString(PermissionLevel perm) noexcept;
[[nodiscard]] std::string_view permissionDisplayLabel(PermissionLevel perm) noexcept;
[[nodiscard]] PermissionLevel permissionFromApiString(std::string_view raw) noexcept;

[[nodiscard]] std::string_view toApiString(ClaimRisk risk) noexcept;
[[nodiscard]] ClaimRisk claimRiskFromApiString(std::string_view raw) noexcept;

/**
 * Matches Jukebox v3.8.0's `NongManager::adjustSongID(id, robtop)` mapping:
 * official RobTop song IDs map to negative IDs `(-id - 1)`.
 */
[[nodiscard]] int adjustJukeboxSongId(int songId, bool isRobtopSong) noexcept;

/**
 * Classifies an un-replaced in-game Geometry Dash song into its authoritative source category.
 */
[[nodiscard]] SourceType classifyDefaultGdSong(
    int songId,
    bool isRobtopSong,
    bool isMusicLibrary,
    bool hasVisibleNcsLogo,
    int songObjectNongType
) noexcept;

/**
 * Builds a deterministic local cache lookup key matching backend canonical deduplication rules.
 */
[[nodiscard]] std::string computeClientCacheKey(const SongQuery& query);

/**
 * Computes non-misleading visual badge styling for in-game widgets.
 * Never presents stale or offline cached entries as freshly verified.
 */
[[nodiscard]] BadgeStyle badgeStyleForRecord(
    const std::optional<SongRightsRecord>& record,
    bool isLoading,
    bool servedFromOfflineFallback
);

}  // namespace mrc
