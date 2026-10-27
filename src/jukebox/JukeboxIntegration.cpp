#include "JukeboxIntegration.hpp"

#include <fstream>
#include <sstream>

#include <Geode/loader/Loader.hpp>
#include <Geode/loader/Mod.hpp>
#include <Geode/utils/cocos.hpp>

#include <mrc/JsonCodec.hpp>

namespace mrc::jb {
namespace {

std::optional<std::string> readTextFileBounded(const std::filesystem::path& p) {
    std::error_code ec;
    if (!std::filesystem::is_regular_file(p, ec)) {
        return std::nullopt;
    }
    const auto sz = std::filesystem::file_size(p, ec);
    if (ec || sz == 0 || sz > 4ULL * 1024ULL * 1024ULL) {
        return std::nullopt;
    }
    std::ifstream ifs(p, std::ios::binary);
    if (!ifs.is_open()) {
        return std::nullopt;
    }
    std::ostringstream oss;
    oss << ifs.rdbuf();
    return oss.str();
}

}  // namespace

bool JukeboxIntegration::isJukeboxLoaded() {
    return geode::Loader::get()->getLoadedMod("fleym.nongd") != nullptr;
}

std::string JukeboxIntegration::jukeboxVersionString() {
    if (auto* mod = geode::Loader::get()->getLoadedMod("fleym.nongd")) {
        return mod->getVersion().toVString();
    }
    return "not-installed";
}

std::optional<std::filesystem::path> JukeboxIntegration::jukeboxSaveDir() {
    if (auto* mod = geode::Loader::get()->getLoadedMod("fleym.nongd")) {
        return mod->getSaveDir();
    }
    return std::nullopt;
}

std::optional<SongQuery> JukeboxIntegration::extractDefaultSongFromWidget(CustomSongWidget* widget) {
    if (!widget) {
        return std::nullopt;
    }

    SongInfoObject* info = widget->m_songInfoObject;
    const bool isRobtop = widget->m_isRobtopSong;
    const bool isMusicLib = widget->m_isMusicLibrary;
    const bool hasVisibleNcs = (widget->m_ncsLogo != nullptr && widget->m_ncsLogo->isVisible());
    const int nongType = (info != nullptr) ? info->m_nongType : 0;

    int rawSongId = widget->m_customSongID;
    if (rawSongId == 0 && info != nullptr) {
        rawSongId = info->m_songID;
    }

    SongQuery query;
    query.gdSongId = rawSongId;
    query.sourceType = classifyDefaultGdSong(
        rawSongId,
        isRobtop,
        isMusicLib,
        hasVisibleNcs,
        nongType
    );

    if (info != nullptr) {
        query.title = std::string(info->m_songName);
        query.artist = std::string(info->m_artistName);
        if (!info->m_youtubeVideo.empty()) {
            query.youtubeId = std::string(info->m_youtubeVideo);
        }
    }

    // Fallback to UI labels if SongInfoObject has not populated names yet (e.g. RobTop songs)
    if (query.title.empty() && widget->m_songLabel != nullptr && widget->m_songLabel->getString() != nullptr) {
        query.title = widget->m_songLabel->getString();
    }
    if (query.artist.empty() && widget->m_artistLabel != nullptr && widget->m_artistLabel->getString() != nullptr) {
        std::string rawArtist = widget->m_artistLabel->getString();
        if (rawArtist.starts_with("By ")) {
            rawArtist = rawArtist.substr(3);
        }
        query.artist = std::move(rawArtist);
    }

    if (query.title.empty()) {
        query.title = isRobtop ? ("Official Song #" + std::to_string(rawSongId + 1))
                               : ("Song " + std::to_string(rawSongId));
    }
    if (query.artist.empty()) {
        query.artist = "Unknown Artist";
    }

    if (query.sourceType == SourceType::Newgrounds && rawSongId > 0) {
        query.sourceUrl = "https://www.newgrounds.com/audio/listen/" + std::to_string(rawSongId);
    }

    return query;
}

arc::Future<SongQuery> JukeboxIntegration::resolveActiveSongAsync(
    SongQuery defaultQuery,
    int adjustedSongId
) {
    auto saveDirOpt = jukeboxSaveDir();
    if (!saveDirOpt.has_value()) {
        co_return defaultQuery;
    }

    const std::filesystem::path manifestFile =
        *saveDirOpt / "manifest" / (std::to_string(adjustedSongId) + ".json");
    const std::filesystem::path nongsDir = *saveDirOpt / "nongs";

    co_return co_await geode::async::runtime().spawnBlocking<SongQuery>(
        [manifestFile, nongsDir, defaultQuery = std::move(defaultQuery)]() mutable -> SongQuery {
            auto textOpt = readTextFileBounded(manifestFile);
            if (!textOpt.has_value()) {
                return defaultQuery;
            }
            auto parsed = parseJukeboxManifestActiveSong(*textOpt, defaultQuery, nongsDir);
            if (!parsed.has_value()) {
                return defaultQuery;
            }
            return std::move(*parsed);
        }
    );
}

arc::Future<std::vector<SongQuery>> JukeboxIntegration::loadAllManifestSongsAsync(
    SongQuery defaultQuery,
    int adjustedSongId
) {
    auto saveDirOpt = jukeboxSaveDir();
    if (!saveDirOpt.has_value()) {
        co_return std::vector<SongQuery>{defaultQuery};
    }

    const std::filesystem::path manifestFile =
        *saveDirOpt / "manifest" / (std::to_string(adjustedSongId) + ".json");
    const std::filesystem::path nongsDir = *saveDirOpt / "nongs";

    co_return co_await geode::async::runtime().spawnBlocking<std::vector<SongQuery>>(
        [manifestFile, nongsDir, defaultQuery = std::move(defaultQuery)]() mutable -> std::vector<SongQuery> {
            auto textOpt = readTextFileBounded(manifestFile);
            if (!textOpt.has_value()) {
                return {defaultQuery};
            }
            auto parsed = parseJukeboxManifestAllSongs(*textOpt, defaultQuery, nongsDir);
            if (!parsed.has_value()) {
                return {defaultQuery};
            }
            return std::move(*parsed);
        }
    );
}

}  // namespace mrc::jb
