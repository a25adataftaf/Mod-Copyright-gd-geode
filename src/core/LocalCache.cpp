#include <mrc/LocalCache.hpp>

#include <algorithm>
#include <cctype>
#include <fstream>
#include <sstream>

#include <mrc/JsonCodec.hpp>

namespace mrc {
namespace {

std::string toLowerCopy(std::string_view s) {
    std::string out(s.size(), '\0');
    for (std::size_t i = 0; i < s.size(); ++i) {
        out[i] = static_cast<char>(std::tolower(static_cast<unsigned char>(s[i])));
    }
    return out;
}

bool containsCaseInsensitive(std::string_view haystack, std::string_view needleLower) {
    if (needleLower.empty()) return true;
    return toLowerCopy(haystack).find(needleLower) != std::string::npos;
}

bool isMonetizableLevel(PermissionLevel perm) noexcept {
    return perm == PermissionLevel::Allowed || perm == PermissionLevel::Conditional ||
           perm == PermissionLevel::ConditionalRevenueShare;
}

}  // namespace

LocalRightsCache::LocalRightsCache(
    std::filesystem::path storageFile,
    std::int64_t defaultTtlSeconds,
    std::size_t maxEntries
)
    : m_storageFile(std::move(storageFile)),
      m_defaultTtlSeconds(defaultTtlSeconds),
      m_maxEntries(maxEntries) {}

bool LocalRightsCache::loadFromDisk() {
    std::lock_guard lock(m_mutex);
    std::error_code ec;
    if (m_storageFile.empty() || !std::filesystem::exists(m_storageFile, ec)) {
        return false;
    }

    std::ifstream ifs(m_storageFile, std::ios::binary);
    if (!ifs.is_open()) {
        return false;
    }
    std::ostringstream buffer;
    buffer << ifs.rdbuf();
    const std::string raw = buffer.str();
    if (raw.empty()) {
        return false;
    }

    auto parsed = json::parse(raw);
    if (!parsed.has_value() || !parsed->isObject()) {
        return false;
    }

    const auto* entriesVal = parsed->find("entries");
    if (!entriesVal || !entriesVal->isObject()) {
        return false;
    }

    m_entriesByKey.clear();
    for (const auto& [cacheKey, recJson] : *entriesVal->asObject()) {
        auto recExp = parseSongRightsRecord(recJson);
        if (recExp.has_value()) {
            m_entriesByKey.insert_or_assign(cacheKey, std::move(*recExp));
        }
    }
    evictIfNeededLocked();
    return true;
}

bool LocalRightsCache::saveToDisk() const {
    std::lock_guard lock(m_mutex);
    if (m_storageFile.empty()) {
        return false;
    }

    std::error_code ec;
    if (m_storageFile.has_parent_path()) {
        std::filesystem::create_directories(m_storageFile.parent_path(), ec);
    }

    json::Object entriesObj;
    for (const auto& [key, rec] : m_entriesByKey) {
        entriesObj[key] = songRightsRecordToJson(rec);
    }

    json::Object rootObj;
    rootObj["schema_version"] = static_cast<std::int64_t>(1);
    rootObj["entries"] = json::Value(std::move(entriesObj));

    const std::string serialized = json::dump(json::Value(std::move(rootObj)));
    std::filesystem::path tmpFile = m_storageFile;
    tmpFile += ".tmp";

    {
        std::ofstream ofs(tmpFile, std::ios::binary | std::ios::trunc);
        if (!ofs.is_open()) {
            return false;
        }
        ofs.write(serialized.data(), static_cast<std::streamsize>(serialized.size()));
        ofs.flush();
        if (!ofs.good()) {
            return false;
        }
    }

    std::filesystem::rename(tmpFile, m_storageFile, ec);
    if (ec) {
        // Fallback overwrite on platforms where rename over existing target fails
        std::filesystem::copy_file(
            tmpFile,
            m_storageFile,
            std::filesystem::copy_options::overwrite_existing,
            ec
        );
        std::filesystem::remove(tmpFile, ec);
        if (ec) {
            return false;
        }
    }
    return true;
}

void LocalRightsCache::put(
    std::string_view clientCacheKey,
    SongRightsRecord record,
    std::int64_t nowUnixSeconds
) {
    std::lock_guard lock(m_mutex);
    record.cachedAtUnix = nowUnixSeconds;
    record.localExpiresAtUnix = nowUnixSeconds + m_defaultTtlSeconds;
    m_entriesByKey.insert_or_assign(std::string(clientCacheKey), std::move(record));
    evictIfNeededLocked();
}

std::optional<CacheLookupResult> LocalRightsCache::get(
    std::string_view clientCacheKey,
    std::int64_t nowUnixSeconds
) const {
    std::lock_guard lock(m_mutex);
    auto it = m_entriesByKey.find(std::string(clientCacheKey));
    if (it == m_entriesByKey.end()) {
        return std::nullopt;
    }

    SongRightsRecord copy = it->second;
    const bool localExpired = copy.localExpiresAtUnix > 0 && nowUnixSeconds >= copy.localExpiresAtUnix;
    if (localExpired) {
        copy.isStale = true;
    }
    return CacheLookupResult{
        .record = std::move(copy),
        .isFresh = !copy.isStale && !localExpired,
    };
}

std::optional<CacheLookupResult> LocalRightsCache::getByCanonicalKey(
    std::string_view canonicalKey,
    std::int64_t nowUnixSeconds
) const {
    std::lock_guard lock(m_mutex);
    for (const auto& [_, rec] : m_entriesByKey) {
        if (rec.canonicalKey == canonicalKey) {
            SongRightsRecord copy = rec;
            const bool localExpired = copy.localExpiresAtUnix > 0 && nowUnixSeconds >= copy.localExpiresAtUnix;
            if (localExpired) {
                copy.isStale = true;
            }
            return CacheLookupResult{
                .record = std::move(copy),
                .isFresh = !copy.isStale && !localExpired,
            };
        }
    }
    return std::nullopt;
}

std::vector<SongRightsRecord> LocalRightsCache::search(
    const LocalSearchFilter& filter,
    std::int64_t nowUnixSeconds
) const {
    std::lock_guard lock(m_mutex);
    const std::string needle = toLowerCopy(filter.queryText);

    // Deduplicate by canonicalKey in case multiple aliases map to the same record
    std::unordered_map<std::string, SongRightsRecord> uniqueByCanonical;
    for (const auto& [cacheKey, rec] : m_entriesByKey) {
        const std::string dedupeId = rec.canonicalKey.empty() ? cacheKey : rec.canonicalKey;
        auto existing = uniqueByCanonical.find(dedupeId);
        if (existing == uniqueByCanonical.end() || rec.cachedAtUnix > existing->second.cachedAtUnix) {
            SongRightsRecord copy = rec;
            if (copy.localExpiresAtUnix > 0 && nowUnixSeconds >= copy.localExpiresAtUnix) {
                copy.isStale = true;
            }
            uniqueByCanonical.insert_or_assign(dedupeId, std::move(copy));
        }
    }

    std::vector<SongRightsRecord> matched;
    matched.reserve(uniqueByCanonical.size());

    for (auto& [_, rec] : uniqueByCanonical) {
        if (!needle.empty()) {
            const bool textHit =
                containsCaseInsensitive(rec.title, needle) ||
                containsCaseInsensitive(rec.artist, needle) ||
                containsCaseInsensitive(rec.originalArtist, needle) ||
                containsCaseInsensitive(rec.licenseName, needle) ||
                (rec.gdSongId.has_value() && std::to_string(*rec.gdSongId).find(needle) != std::string::npos);
            if (!textHit) {
                continue;
            }
        }

        if (filter.commercialAllowedOnly && !isMonetizableLevel(rec.commercialUseStatus)) {
            continue;
        }

        if (filter.requiredPlatformMonetizable.has_value()) {
            bool platformOk = false;
            for (const auto& pr : rec.platforms) {
                if (pr.platform == *filter.requiredPlatformMonetizable &&
                    isMonetizableLevel(pr.monetization)) {
                    platformOk = true;
                    break;
                }
            }
            if (!platformOk) {
                continue;
            }
        }

        matched.push_back(std::move(rec));
    }

    std::sort(matched.begin(), matched.end(), [](const SongRightsRecord& a, const SongRightsRecord& b) {
        return a.cachedAtUnix > b.cachedAtUnix;
    });
    return matched;
}

std::size_t LocalRightsCache::size() const noexcept {
    std::lock_guard lock(m_mutex);
    return m_entriesByKey.size();
}

void LocalRightsCache::clear() {
    std::lock_guard lock(m_mutex);
    m_entriesByKey.clear();
}

void LocalRightsCache::evictIfNeededLocked() {
    while (m_entriesByKey.size() > m_maxEntries) {
        auto oldestIt = m_entriesByKey.begin();
        for (auto it = m_entriesByKey.begin(); it != m_entriesByKey.end(); ++it) {
            if (it->second.cachedAtUnix < oldestIt->second.cachedAtUnix) {
                oldestIt = it;
            }
        }
        m_entriesByKey.erase(oldestIt);
    }
}

}  // namespace mrc
