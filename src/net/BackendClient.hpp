#pragma once

#include <functional>
#include <memory>
#include <optional>
#include <string>
#include <vector>

#include <Geode/utils/async.hpp>
#include <Geode/utils/web.hpp>

#include <mrc/LocalCache.hpp>
#include <mrc/Types.hpp>

namespace mrc::net {

struct ClientResolveOutcome {
    bool ok = false;
    bool servedFromOfflineFallback = false;
    std::optional<SongRightsRecord> record;
    std::string errorMessage;
};

class BackendClient {
public:
    static BackendClient& get();

    void initializeCache();
    void flushCache();

    [[nodiscard]] LocalRightsCache& cache() noexcept { return *m_cache; }
    [[nodiscard]] const LocalRightsCache& cache() const noexcept { return *m_cache; }

    [[nodiscard]] std::string configuredBaseUrl() const;
    [[nodiscard]] int configuredTimeoutSeconds() const;
    [[nodiscard]] bool isModEnabled() const;
    [[nodiscard]] bool shouldAutoCheckSongs() const;
    [[nodiscard]] bool shouldShareLocalFileHash() const;

    /**
     * Resolves rights for `query` asynchronously using `holder` to manage task lifetime.
     * Automatically cancels if the owning UI node (`holder`) is destroyed before completion.
     */
    void resolveSongRights(
        geode::async::TaskHolder<ClientResolveOutcome>& holder,
        SongQuery query,
        bool forceRefresh,
        std::function<void(ClientResolveOutcome)> onComplete
    );

    /**
     * Submits a user inaccuracy/dispute report to `POST /reviews/reports`.
     */
    void submitReviewReport(
        geode::async::TaskHolder<geode::Result<std::string>>& holder,
        std::string songId,
        std::string reason,
        std::string details,
        std::string evidenceUrl,
        std::function<void(geode::Result<std::string>)> onComplete
    );

    /**
     * Searches the backend shared song rights catalogue asynchronously (`GET /songs`),
     * merging results into the local cache and falling back to local cache search when offline.
     */
    void searchSongs(
        geode::async::TaskHolder<std::vector<SongRightsRecord>>& holder,
        LocalSearchFilter filter,
        std::function<void(std::vector<SongRightsRecord>, bool offlineFallback)> onComplete
    );

private:
    BackendClient();
    std::unique_ptr<LocalRightsCache> m_cache;
};

}  // namespace mrc::net
