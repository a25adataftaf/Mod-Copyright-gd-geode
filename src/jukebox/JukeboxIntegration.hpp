#pragma once

#include <filesystem>
#include <optional>
#include <vector>

#include <Geode/binding/CustomSongWidget.hpp>
#include <Geode/binding/SongInfoObject.hpp>
#include <Geode/utils/async.hpp>

#include <mrc/Types.hpp>

namespace mrc::jb {

/**
 * ABI-safe runtime integration layer for Jukebox (`fleym.nongd` >= v3.8.0).
 * Reads Jukebox's persistent JSON manifests asynchronously without linking against
 * unexported PIMPL internals of `jukebox.dll` / `libjukebox.so`.
 */
class JukeboxIntegration {
public:
    [[nodiscard]] static bool isJukeboxLoaded();
    [[nodiscard]] static std::string jukeboxVersionString();
    [[nodiscard]] static std::optional<std::filesystem::path> jukeboxSaveDir();

    /**
     * Extracts baseline Geometry Dash song metadata from a `CustomSongWidget` on the main thread.
     */
    [[nodiscard]] static std::optional<SongQuery> extractDefaultSongFromWidget(CustomSongWidget* widget);

    /**
     * Resolves the effective active song (accounting for any active Jukebox NONG override)
     * on Geode's background thread pool.
     */
    static arc::Future<SongQuery> resolveActiveSongAsync(
        SongQuery defaultQuery,
        int adjustedSongId
    );

    /**
     * Loads all configured NONG entries for `adjustedSongId` on a background thread
     * for display inside Jukebox's NONG list popup.
     */
    static arc::Future<std::vector<SongQuery>> loadAllManifestSongsAsync(
        SongQuery defaultQuery,
        int adjustedSongId
    );
};

}  // namespace mrc::jb
