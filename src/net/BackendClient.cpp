#include "BackendClient.hpp"

#include <cctype>
#include <chrono>
#include <cstdint>
#include <ctime>
#include <filesystem>
#include <fstream>
#include <iomanip>
#include <sstream>

#include <arc/time/Sleep.hpp>
#include <Geode/loader/Log.hpp>
#include <Geode/loader/Mod.hpp>

#include <mrc/AttributionAndSafety.hpp>
#include <mrc/DirectEvaluator.hpp>
#include <mrc/JsonCodec.hpp>

namespace mrc::net {
namespace {

std::int64_t currentUnixSeconds() noexcept {
    return static_cast<std::int64_t>(std::chrono::duration_cast<std::chrono::seconds>(
                                         std::chrono::system_clock::now().time_since_epoch()
    )
                                         .count());
}

std::string currentIsoUtcString() {
    const std::time_t now = std::chrono::system_clock::to_time_t(std::chrono::system_clock::now());
    std::tm utcTm{};
#if defined(_WIN32)
    gmtime_s(&utcTm, &now);
#else
    gmtime_r(&now, &utcTm);
#endif
    std::ostringstream oss;
    oss << std::put_time(&utcTm, "%Y-%m-%dT%H:%M:%SZ");
    return oss.str();
}

std::string stripTrailingSlash(std::string url) {
    while (!url.empty() && std::isspace(static_cast<unsigned char>(url.back()))) {
        url.pop_back();
    }
    while (!url.empty() && std::isspace(static_cast<unsigned char>(url.front()))) {
        url.erase(url.begin());
    }
    while (!url.empty() && url.back() == '/') {
        url.pop_back();
    }
    return url;
}

bool isUnconfiguredOrPlaceholderBackendUrl(std::string_view url) noexcept {
    if (url.empty()) {
        return true;
    }
    // Treat legacy v0.1.0 placeholder domain or .example domains as unconfigured standalone mode
    if (url.find("api.musicrightschecker.org") != std::string_view::npos ||
        url.find(".example") != std::string_view::npos) {
        return true;
    }
    return false;
}

std::string buildNcsCandidateUrl(std::string_view title) {
    std::string slug;
    slug.reserve(title.size());
    bool prevHyphen = false;
    for (unsigned char c : title) {
        if (std::isalnum(c)) {
            slug.push_back(static_cast<char>(c));
            prevHyphen = false;
        } else if ((c == ' ' || c == '-' || c == '_') && !slug.empty() && !prevHyphen) {
            slug.push_back('-');
            prevHyphen = true;
        }
        if (slug.size() >= 64) {
            break;
        }
    }
    while (!slug.empty() && slug.back() == '-') {
        slug.pop_back();
    }
    if (slug.empty()) {
        return "";
    }
    return "https://ncs.io/" + slug;
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

arc::Future<SongRightsRecord> inspectDirectlyCoroutine(
    const SongQuery& query,
    int timeoutSec
) {
    std::optional<DirectFetchedPage> fetchedPage;
    std::string targetFetchUrl;

    if (query.sourceType == SourceType::Newgrounds && query.gdSongId.has_value() && *query.gdSongId > 0) {
        targetFetchUrl = "https://www.newgrounds.com/audio/listen/" + std::to_string(*query.gdSongId);
    } else if (query.sourceType == SourceType::NcsLibrary) {
        targetFetchUrl = buildNcsCandidateUrl(query.title);
    }

    if (!targetFetchUrl.empty() && isSafeBrowserUrl(targetFetchUrl)) {
        geode::utils::web::WebRequest req;
        req.timeout(std::chrono::seconds(std::clamp(timeoutSec, 5, 12)))
            .userAgent("Mozilla/5.0 (compatible; MusicRightsChecker-Geode/0.1.1)")
            .header("Accept", "text/html,application/xhtml+xml");

        geode::utils::web::WebResponse resp = co_await req.get(targetFetchUrl);
        DirectFetchedPage page;
        page.url = targetFetchUrl;
        page.httpStatus = resp.code();
        if (resp.ok()) {
            auto strRes = resp.string();
            if (strRes.isOk()) {
                std::string body = strRes.unwrap();
                if (body.size() > 512ULL * 1024ULL) {
                    body.resize(512ULL * 1024ULL);
                }
                page.rawBody = std::move(body);
            }
        }
        fetchedPage = std::move(page);
    }

    co_return DirectEvaluator::evaluate(query, fetchedPage, currentIsoUtcString());
}

arc::Future<ClientResolveOutcome> performResolveCoroutine(
    SongQuery query,
    bool forceRefresh,
    bool computeLocalHash,
    bool allowDirectFallback,
    std::string rawBaseUrl,
    int timeoutSec,
    LocalRightsCache* cachePtr
) {
    const std::string baseUrl = stripTrailingSlash(std::move(rawBaseUrl));
    const bool hasCustomBackend = !isUnconfiguredOrPlaceholderBackendUrl(baseUrl);

    if (hasCustomBackend && computeLocalHash && query.sourceType == SourceType::JukeboxLocal &&
        query.audioSha256.empty() && !query.localFilePath.empty()) {
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

    // Standalone mode: no centralized backend URL configured -> evaluate directly from public evidence & rules
    if (!hasCustomBackend) {
        if (!allowDirectFallback) {
            if (cachedLookup.has_value()) {
                SongRightsRecord staleCopy = cachedLookup->record;
                staleCopy.isStale = true;
                co_return ClientResolveOutcome{
                    .ok = true,
                    .servedFromOfflineFallback = true,
                    .record = std::move(staleCopy),
                    .errorMessage = "No Backend API URL configured and standalone inspector is disabled.",
                };
            }
            co_return ClientResolveOutcome{
                .ok = false,
                .servedFromOfflineFallback = false,
                .record = std::nullopt,
                .errorMessage = "Configure a Backend API URL or enable Standalone Direct Source Inspector in Mod Settings.",
            };
        }

        SongRightsRecord directRec = co_await inspectDirectlyCoroutine(query, timeoutSec);
        cachePtr->put(clientKey, directRec, currentUnixSeconds());
        (void)cachePtr->saveToDisk();
        co_return ClientResolveOutcome{
            .ok = true,
            .servedFromOfflineFallback = false,
            .record = std::move(directRec),
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

    const std::string resolveEndpoint = baseUrl + "/songs/resolve";
    const std::string payload = serializeResolveRequest(query, true, forceRefresh);

    geode::utils::web::WebRequest req;
    req.timeout(std::chrono::seconds(timeoutSec))
        .userAgent("MusicRightsChecker-Geode/0.1.1")
        .header("Content-Type", "application/json")
        .header("Accept", "application/json")
        .bodyString(payload);

    geode::utils::web::WebResponse resp = co_await req.post(resolveEndpoint);

    auto fallbackToCacheOrDirect = [&](std::string errMsg) -> arc::Future<ClientResolveOutcome> {
        if (cachedLookup.has_value()) {
            SongRightsRecord fallbackRec = cachedLookup->record;
            fallbackRec.isStale = true;
            co_return ClientResolveOutcome{
                .ok = true,
                .servedFromOfflineFallback = true,
                .record = std::move(fallbackRec),
                .errorMessage = std::move(errMsg),
            };
        }
        if (allowDirectFallback) {
            SongRightsRecord directRec = co_await inspectDirectlyCoroutine(query, timeoutSec);
            cachePtr->put(clientKey, directRec, currentUnixSeconds());
            (void)cachePtr->saveToDisk();
            co_return ClientResolveOutcome{
                .ok = true,
                .servedFromOfflineFallback = false,
                .record = std::move(directRec),
                .errorMessage = "Centralized backend unreachable (" + errMsg + "); evaluated via built-in direct inspector.",
            };
        }
        co_return ClientResolveOutcome{
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
        co_return co_await fallbackToCacheOrDirect(std::move(msg));
    }

    auto bodyStrRes = resp.string();
    if (!bodyStrRes.isOk()) {
        co_return co_await fallbackToCacheOrDirect("Failed to decode UTF-8 response from backend");
    }

    auto parsedRes = parseResolveResponse(bodyStrRes.unwrap());
    if (!parsedRes.has_value()) {
        co_return co_await fallbackToCacheOrDirect("Invalid backend JSON payload: " + parsedRes.error());
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

    if (parsedRes->resolutionStatus == "processing" && !parsedRes->jobId.empty()) {
        const std::string jobEndpoint = baseUrl + "/jobs/" + parsedRes->jobId;
        std::uint64_t delayMs = 900;
        for (int attempt = 0; attempt < 6; ++attempt) {
            co_await arc::sleep(asp::Duration::fromMillis(delayMs));
            delayMs = std::min<std::uint64_t>(delayMs * 2, 4000);

            geode::utils::web::WebRequest pollReq;
            pollReq.timeout(std::chrono::seconds(timeoutSec))
                .userAgent("MusicRightsChecker-Geode/0.1.1")
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
                co_return co_await fallbackToCacheOrDirect("Background rights research failed: " + detail);
            }
        }
        co_return co_await fallbackToCacheOrDirect("Rights research is still queued on the server.");
    }

    co_return co_await fallbackToCacheOrDirect("Backend returned neither a completed record nor a valid job ID.");
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

bool BackendClient::isDirectFallbackEnabled() const {
    return geode::Mod::get()->getSettingValue<bool>("direct-fallback-enabled");
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
            isDirectFallbackEnabled(),
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
    const std::string baseUrl = stripTrailingSlash(configuredBaseUrl());
    const int timeoutSec = configuredTimeoutSeconds();

    holder.spawn(
        [baseUrl, timeoutSec, songId = std::move(songId), reason = std::move(reason),
         details = std::move(details), evidenceUrl = std::move(evidenceUrl)]()
            -> arc::Future<geode::Result<std::string>> {
            if (isUnconfiguredOrPlaceholderBackendUrl(baseUrl) || !isSafeBackendBaseUrl(baseUrl)) {
                co_return geode::Err("Configure a centralized Backend API URL in Mod Settings to submit server reports.");
            }
            const std::string endpoint = baseUrl + "/reviews/reports";
            const std::string body = serializeReviewReportRequest(
                songId,
                reason,
                details,
                "",
                evidenceUrl,
                "mrc.music_rights_checker/0.1.1"
            );

            geode::utils::web::WebRequest req;
            req.timeout(std::chrono::seconds(timeoutSec))
                .userAgent("MusicRightsChecker-Geode/0.1.1")
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
    const std::string baseUrl = stripTrailingSlash(configuredBaseUrl());
    const int timeoutSec = configuredTimeoutSeconds();
    LocalRightsCache* cachePtr = m_cache.get();

    auto usedFallbackFlag = std::make_shared<bool>(false);

    holder.spawn(
        [baseUrl, timeoutSec, filter, cachePtr, usedFallbackFlag]()
            -> arc::Future<std::vector<SongRightsRecord>> {
            if (isUnconfiguredOrPlaceholderBackendUrl(baseUrl) || !isSafeBackendBaseUrl(baseUrl)) {
                *usedFallbackFlag = true;
                co_return cachePtr->search(filter, currentUnixSeconds());
            }

            const std::string endpoint = baseUrl + "/songs";
            geode::utils::web::WebRequest req;
            req.timeout(std::chrono::seconds(timeoutSec))
                .userAgent("MusicRightsChecker-Geode/0.1.1")
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
