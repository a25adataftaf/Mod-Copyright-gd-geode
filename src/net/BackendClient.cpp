#include "BackendClient.hpp"

#include <chrono>
#include <ctime>
#include <filesystem>
#include <fstream>

#include <Geode/loader/Log.hpp>
#include <Geode/loader/Mod.hpp>

#include <mrc/AttributionAndSafety.hpp>
#include <mrc/JsonCodec.hpp>

namespace mrc::net {
namespace {

std::int64_t currentUnixSeconds() noexcept {
    return static_cast<std::int64_t>(std::chrono::duration_cast<std::chrono::seconds>(
                                         std::chrono::system_clock::now().time_since_epoch()
    )
                                         .count());
}

std::string stripTrailingSlash(std::string url) {
    while (!url.empty() && url.back() == '/') {
        url.pop_back();
    }
    return url;
}

std::string computeOptionalLocalSha256(const std::string& filePath) {
    if (filePath.empty()) {
        return "";
    }
    std::error_code ec;
    const std::filesystem::path p(filePath);
    if (!std::filesystem::is_regular_file(p, ec)) {
        return "";
    }
    const auto fsize = std::filesystem::file_size(p, ec);
    // Cap local hashing at 64 MiB to avoid excessive background disk reads
    if (ec || fsize == 0 || fsize > 64ULL * 1024ULL * 1024ULL) {
        return "";
    }
    std::ifstream ifs(p, std::ios::binary);
    if (!ifs.is_open()) {
        return "";
    }
    std::vector<std::uint8_t> buf(static_cast<std::size_t>(fsize));
    ifs.read(reinterpret_cast<char*>(buf.data()), static_cast<std::streamsize>(buf.size()));
    if (!ifs) {
        return "";
    }
    return sha256Hex(buf);
}

arc::Future<ClientResolveOutcome> performResolveCoroutine(
    SongQuery query,
    bool forceRefresh,
    bool computeLocalHash,
    std::string baseUrl,
    int timeoutSec,
    LocalRightsCache* cachePtr
) {
    if (computeLocalHash && query.sourceType == SourceType::JukeboxLocal &&
        query.audioSha256.empty() && !query.localFilePath.empty()) {
        // Compute opt-in SHA-256 digest on Geode's background worker pool, never on the main thread
        query.audioSha256 = co_await geode::async::runtime().spawnBlocking<std::string>(
            [path = query.localFilePath]() { return computeOptionalLocalSha256(path); }
        );
    }

    const std::string clientKey = computeClientCacheKey(query);
    const std::int64_t nowUnix = currentUnixSeconds();
    const auto cachedLookup = cachePtr->get(clientKey, nowUnix);

    if (!forceRefresh && cachedLookup.has_value() && cachedLookup->isFresh) {
        co_return ClientResolveOutcome{
            .ok = true,
            .servedFromOfflineFallback = false,
            .record = cachedLookup->record,
            .errorMessage = "",
        };
    }

    if (!isSafeBackendBaseUrl(baseUrl)) {
        if (cachedLookup.has_value()) {
            SongRightsRecord fallbackRec = cachedLookup->record;
            fallbackRec.isStale = true;
            co_return ClientResolveOutcome{
                .ok = true,
                .servedFromOfflineFallback = true,
                .record = std::move(fallbackRec),
                .errorMessage = "Configured backend URL failed HTTPS safety check; showing cached record.",
            };
        }
        co_return ClientResolveOutcome{
            .ok = false,
            .servedFromOfflineFallback = false,
            .record = std::nullopt,
            .errorMessage = "Invalid Backend API URL in mod settings (must use https:// or http://127.0.0.1).",
        };
    }

    const std::string resolveEndpoint = stripTrailingSlash(baseUrl) + "/songs/resolve";
    const std::string payload = serializeResolveRequest(query, true, forceRefresh);

    geode::utils::web::WebRequest req;
    req.timeout(std::chrono::seconds(timeoutSec))
        .userAgent("MusicRightsChecker-Geode/0.1.0")
        .header("Content-Type", "application/json")
        .header("Accept", "application/json")
        .bodyString(payload);

    geode::utils::web::WebResponse resp = co_await req.post(resolveEndpoint);

    auto fallbackOrError = [&](std::string errMsg) -> ClientResolveOutcome {
        if (cachedLookup.has_value()) {
            SongRightsRecord fallbackRec = cachedLookup->record;
            fallbackRec.isStale = true;
            return ClientResolveOutcome{
                .ok = true,
                .servedFromOfflineFallback = true,
                .record = std::move(fallbackRec),
                .errorMessage = std::move(errMsg),
            };
        }
        return ClientResolveOutcome{
            .ok = false,
            .servedFromOfflineFallback = false,
            .record = std::nullopt,
            .errorMessage = std::move(errMsg),
        };
    };

    if (!resp.ok()) {
        std::string msg = "Backend HTTP " + std::to_string(resp.code());
        if (!resp.errorMessage().empty()) {
            msg += ": " + std::string(resp.errorMessage());
        }
        co_return fallbackOrError(std::move(msg));
    }

    auto bodyStrRes = resp.string();
    if (!bodyStrRes.isOk()) {
        co_return fallbackOrError("Failed to decode UTF-8 response from backend");
    }

    auto parsedRes = parseResolveResponse(bodyStrRes.unwrap());
    if (!parsedRes.has_value()) {
        co_return fallbackOrError("Invalid backend JSON payload: " + parsedRes.error());
    }

    if (parsedRes->resolutionStatus == "ready" && parsedRes->record.has_value()) {
        cachePtr->put(clientKey, *parsedRes->record, currentUnixSeconds());
        (void)cachePtr->saveToDisk();
        co_return ClientResolveOutcome{
            .ok = true,
            .servedFromOfflineFallback = false,
            .record = std::move(parsedRes->record),
            .errorMessage = "",
        };
    }

    // Handle 202 Accepted asynchronous job polling with bounded exponential backoff
    if (parsedRes->resolutionStatus == "processing" && !parsedRes->jobId.empty()) {
        const std::string jobEndpoint = stripTrailingSlash(baseUrl) + "/jobs/" + parsedRes->jobId;
        int delayMs = 900;
        for (int attempt = 0; attempt < 6; ++attempt) {
            co_await arc::sleep(asp::Duration::fromMillis(delayMs));
            delayMs = std::min(delayMs * 2, 4000);

            geode::utils::web::WebRequest pollReq;
            pollReq.timeout(std::chrono::seconds(timeoutSec))
                .userAgent("MusicRightsChecker-Geode/0.1.0")
                .header("Accept", "application/json");

            geode::utils::web::WebResponse pollResp = co_await pollReq.get(jobEndpoint);
            if (!pollResp.ok()) {
                break;
            }
            auto pollBody = pollResp.string();
            if (!pollBody.isOk()) {
                break;
            }
            auto pollJson = json::parse(pollBody.unwrap());
            if (!pollJson.has_value() || !pollJson->isObject()) {
                break;
            }
            const std::string jobState =
                pollJson->find("status") ? pollJson->find("status")->asString() : "failed";
            if (jobState == "completed") {
                if (const auto* recNode = pollJson->find("record"); recNode && recNode->isObject()) {
                    auto finalRec = parseSongRightsRecord(*recNode);
                    if (finalRec.has_value()) {
                        cachePtr->put(clientKey, *finalRec, currentUnixSeconds());
                        (void)cachePtr->saveToDisk();
                        co_return ClientResolveOutcome{
                            .ok = true,
                            .servedFromOfflineFallback = false,
                            .record = std::move(*finalRec),
                            .errorMessage = "",
                        };
                    }
                }
                break;
            }
            if (jobState == "failed") {
                const std::string detail =
                    pollJson->find("error_message") ? pollJson->find("error_message")->asString() : "Job failed";
                co_return fallbackOrError("Background rights research failed: " + detail);
            }
        }
        co_return fallbackOrError("Rights research is still queued on the server; try refreshing shortly.");
    }

    co_return fallbackOrError("Backend returned neither a completed record nor a valid job ID.");
}

}  // namespace

BackendClient& BackendClient::get() {
    static BackendClient instance;
    return instance;
}

BackendClient::BackendClient() {
    const auto cachePath = geode::Mod::get()->getSaveDir() / "rights_cache.json";
    m_cache = std::make_unique<LocalRightsCache>(cachePath, 7 * 24 * 3600, 1024);
}

void BackendClient::initializeCache() {
    if (m_cache->loadFromDisk()) {
        geode::log::info("Loaded {} cached music rights records from disk.", m_cache->size());
    }
}

void BackendClient::flushCache() {
    (void)m_cache->saveToDisk();
}

std::string BackendClient::configuredBaseUrl() const {
    return geode::Mod::get()->getSettingValue<std::string>("backend-url");
}

int BackendClient::configuredTimeoutSeconds() const {
    auto val = static_cast<int>(geode::Mod::get()->getSettingValue<std::int64_t>("request-timeout-seconds"));
    return std::clamp(val, 5, 60);
}

bool BackendClient::isModEnabled() const {
    return geode::Mod::get()->getSettingValue<bool>("enabled");
}

bool BackendClient::shouldAutoCheckSongs() const {
    return geode::Mod::get()->getSettingValue<bool>("auto-check-songs");
}

bool BackendClient::shouldShareLocalFileHash() const {
    return geode::Mod::get()->getSettingValue<bool>("share-local-file-hash");
}

void BackendClient::resolveSongRights(
    geode::async::TaskHolder<ClientResolveOutcome>& holder,
    SongQuery query,
    bool forceRefresh,
    std::function<void(ClientResolveOutcome)> onComplete
) {
    holder.spawn(
        performResolveCoroutine(
            std::move(query),
            forceRefresh,
            shouldShareLocalFileHash(),
            configuredBaseUrl(),
            configuredTimeoutSeconds(),
            m_cache.get()
        ),
        std::move(onComplete)
    );
}

void BackendClient::submitReviewReport(
    geode::async::TaskHolder<geode::Result<std::string>>& holder,
    std::string songId,
    std::string reason,
    std::string details,
    std::string evidenceUrl,
    std::function<void(geode::Result<std::string>)> onComplete
) {
    const std::string baseUrl = configuredBaseUrl();
    const int timeoutSec = configuredTimeoutSeconds();

    holder.spawn(
        [baseUrl, timeoutSec, songId = std::move(songId), reason = std::move(reason),
         details = std::move(details), evidenceUrl = std::move(evidenceUrl)]()
            -> arc::Future<geode::Result<std::string>> {
            if (!isSafeBackendBaseUrl(baseUrl)) {
                co_return geode::Err("Configured Backend API URL is invalid or insecure.");
            }
            const std::string endpoint = stripTrailingSlash(baseUrl) + "/reviews/reports";
            const std::string body = serializeReviewReportRequest(
                songId,
                reason,
                details,
                "",
                evidenceUrl
            );

            geode::utils::web::WebRequest req;
            req.timeout(std::chrono::seconds(timeoutSec))
                .userAgent("MusicRightsChecker-Geode/0.1.0")
                .header("Content-Type", "application/json")
                .header("Accept", "application/json")
                .bodyString(body);

            geode::utils::web::WebResponse resp = co_await req.post(endpoint);
            if (!resp.ok()) {
                co_return geode::Err("Report submission failed (HTTP " + std::to_string(resp.code()) + ")");
            }
            co_return geode::Ok(std::string("Report submitted for manual verification."));
        }(),
        std::move(onComplete)
    );
}

void BackendClient::searchSongs(
    geode::async::TaskHolder<std::vector<SongRightsRecord>>& holder,
    LocalSearchFilter filter,
    std::function<void(std::vector<SongRightsRecord>, bool offlineFallback)> onComplete
) {
    const std::string baseUrl = configuredBaseUrl();
    const int timeoutSec = configuredTimeoutSeconds();
    LocalRightsCache* cachePtr = m_cache.get();

    auto usedFallbackFlag = std::make_shared<bool>(false);

    holder.spawn(
        [baseUrl, timeoutSec, filter, cachePtr, usedFallbackFlag]()
            -> arc::Future<std::vector<SongRightsRecord>> {
            if (!isSafeBackendBaseUrl(baseUrl)) {
                *usedFallbackFlag = true;
                co_return cachePtr->search(filter, currentUnixSeconds());
            }

            const std::string endpoint = stripTrailingSlash(baseUrl) + "/songs";
            geode::utils::web::WebRequest req;
            req.timeout(std::chrono::seconds(timeoutSec))
                .userAgent("MusicRightsChecker-Geode/0.1.0")
                .header("Accept", "application/json")
                .param("limit", 40);

            if (!filter.queryText.empty()) {
                req.param("q", filter.queryText);
            }
            if (filter.commercialAllowedOnly) {
                req.param("commercial_allowed", "true");
            }
            if (filter.requiredPlatformMonetizable.has_value()) {
                req.param("platform", std::string(toApiString(*filter.requiredPlatformMonetizable)));
            }

            geode::utils::web::WebResponse resp = co_await req.get(endpoint);
            if (!resp.ok()) {
                *usedFallbackFlag = true;
                co_return cachePtr->search(filter, currentUnixSeconds());
            }
            auto bodyStr = resp.string();
            if (!bodyStr.isOk()) {
                *usedFallbackFlag = true;
                co_return cachePtr->search(filter, currentUnixSeconds());
            }
            auto parsedList = parseSongListResponse(bodyStr.unwrap());
            if (!parsedList.has_value()) {
                *usedFallbackFlag = true;
                co_return cachePtr->search(filter, currentUnixSeconds());
            }

            const std::int64_t nowUnix = currentUnixSeconds();
            for (const auto& rec : *parsedList) {
                if (!rec.canonicalKey.empty()) {
                    cachePtr->put(rec.canonicalKey, rec, nowUnix);
                }
            }
            (void)cachePtr->saveToDisk();
            co_return std::move(*parsedList);
        }(),
        [cb = std::move(onComplete), usedFallbackFlag](std::vector<SongRightsRecord> records) {
            cb(std::move(records), *usedFallbackFlag);
        }
    );
}

}  // namespace mrc::net
