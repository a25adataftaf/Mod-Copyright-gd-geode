#pragma once

#include <cstdint>
#include <filesystem>
#include <mutex>
#include <optional>
#include <string>
#include <string_view>
#include <unordered_map>
#include <vector>

#include <mrc/Types.hpp>

namespace mrc {

struct CacheLookupResult {
    SongRightsRecord record;
    bool isFresh = false;
};

struct LocalSearchFilter {
    std::string queryText;
    bool commercialAllowedOnly = false;
    std::optional<Platform> requiredPlatformMonetizable;
};

/**
 * Thread-safe persistent local rights cache for the Geode mod.
 * Stores previously retrieved evaluations on disk so creators can view song rights
 * offline while clearly distinguishing freshly verified records from stale cached entries.
 */
class LocalRightsCache {
public:
    explicit LocalRightsCache(
        std::filesystem::path storageFile,
        std::int64_t defaultTtlSeconds = 7 * 24 * 3600,
        std::size_t maxEntries = 1024
    );

    bool loadFromDisk();
    bool saveToDisk() const;

    void put(
        std::string_view clientCacheKey,
        SongRightsRecord record,
        std::int64_t nowUnixSeconds
    );

    [[nodiscard]] std::optional<CacheLookupResult> get(
        std::string_view clientCacheKey,
        std::int64_t nowUnixSeconds
    ) const;

    [[nodiscard]] std::optional<CacheLookupResult> getByCanonicalKey(
        std::string_view canonicalKey,
        std::int64_t nowUnixSeconds
    ) const;

    [[nodiscard]] std::vector<SongRightsRecord> search(
        const LocalSearchFilter& filter,
        std::int64_t nowUnixSeconds
    ) const;

    [[nodiscard]] std::size_t size() const noexcept;
    void clear();

private:
    std::filesystem::path m_storageFile;
    std::int64_t m_defaultTtlSeconds;
    std::size_t m_maxEntries;

    mutable std::mutex m_mutex;
    std::unordered_map<std::string, SongRightsRecord> m_entriesByKey;

    void evictIfNeededLocked();
};

}  // namespace mrc
