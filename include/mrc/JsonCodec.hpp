#pragma once

#include <cstdint>
#include <expected>
#include <filesystem>
#include <map>
#include <optional>
#include <string>
#include <string_view>
#include <variant>
#include <vector>

#include <mrc/Types.hpp>

namespace mrc::json {

struct Value;
using Array = std::vector<Value>;
using Object = std::map<std::string, Value, std::less<>>;

struct Value {
    using Storage = std::variant<std::monostate, bool, std::int64_t, double, std::string, Array, Object>;
    Storage data{};

    Value() = default;
    Value(std::nullptr_t) : data(std::monostate{}) {}
    Value(bool b) : data(b) {}
    Value(int i) : data(static_cast<std::int64_t>(i)) {}
    Value(std::int64_t i) : data(i) {}
    Value(double d) : data(d) {}
    Value(std::string s) : data(std::move(s)) {}
    Value(std::string_view sv) : data(std::string(sv)) {}
    Value(const char* s) : data(std::string(s ? s : "")) {}
    Value(Array arr) : data(std::move(arr)) {}
    Value(Object obj) : data(std::move(obj)) {}

    [[nodiscard]] bool isNull() const noexcept { return std::holds_alternative<std::monostate>(data); }
    [[nodiscard]] bool isBool() const noexcept { return std::holds_alternative<bool>(data); }
    [[nodiscard]] bool isInt() const noexcept { return std::holds_alternative<std::int64_t>(data); }
    [[nodiscard]] bool isNumber() const noexcept {
        return std::holds_alternative<std::int64_t>(data) || std::holds_alternative<double>(data);
    }
    [[nodiscard]] bool isString() const noexcept { return std::holds_alternative<std::string>(data); }
    [[nodiscard]] bool isArray() const noexcept { return std::holds_alternative<Array>(data); }
    [[nodiscard]] bool isObject() const noexcept { return std::holds_alternative<Object>(data); }

    [[nodiscard]] bool asBool(bool fallback = false) const noexcept;
    [[nodiscard]] std::int64_t asInt(std::int64_t fallback = 0) const noexcept;
    [[nodiscard]] std::string asString(std::string_view fallback = "") const;
    [[nodiscard]] const Array* asArray() const noexcept;
    [[nodiscard]] const Object* asObject() const noexcept;
    [[nodiscard]] const Value* find(std::string_view key) const noexcept;
};

[[nodiscard]] std::expected<Value, std::string> parse(std::string_view input);
[[nodiscard]] std::string dump(const Value& value);

}  // namespace mrc::json

namespace mrc {

[[nodiscard]] std::string serializeResolveRequest(
    const SongQuery& query,
    bool asyncMode = true,
    bool forceRefresh = false
);

[[nodiscard]] std::string serializeReviewReportRequest(
    const std::string& songId,
    std::string_view reason,
    std::string_view details,
    std::string_view suggestedLicense = "",
    std::string_view evidenceUrl = "",
    std::string_view reporterClient = "mrc.music_rights_checker/0.1.0"
);

[[nodiscard]] std::expected<SongRightsRecord, std::string> parseSongRightsRecord(const json::Value& obj);
[[nodiscard]] json::Value songRightsRecordToJson(const SongRightsRecord& record);

[[nodiscard]] std::expected<ResolveApiResult, std::string> parseResolveResponse(std::string_view body);
[[nodiscard]] std::expected<std::vector<SongRightsRecord>, std::string> parseSongListResponse(std::string_view body);

/**
 * Parses a Jukebox (`fleym.nongd` >= v3.8.0) `<adjustedSongId>.json` manifest file
 * and resolves the currently active song into a `SongQuery`.
 * If `active` points to the default song, returns `fallbackDefault`.
 */
[[nodiscard]] std::expected<SongQuery, std::string> parseJukeboxManifestActiveSong(
    std::string_view manifestJson,
    const SongQuery& fallbackDefault,
    const std::filesystem::path& jukeboxNongsDir = {}
);

/**
 * Parses all entries (`default`, `locals`, `youtube`, `hosted`) from a Jukebox manifest
 * so UI lists (e.g. `NongList`) can attach rights inspection buttons to every listed song.
 */
[[nodiscard]] std::expected<std::vector<SongQuery>, std::string> parseJukeboxManifestAllSongs(
    std::string_view manifestJson,
    const SongQuery& fallbackDefault,
    const std::filesystem::path& jukeboxNongsDir = {}
);

}  // namespace mrc
