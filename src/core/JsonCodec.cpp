#include <mrc/JsonCodec.hpp>

#include <cctype>
#include <charconv>
#include <cmath>
#include <limits>
#include <sstream>

namespace mrc::json {
namespace {

constexpr int MAX_JSON_DEPTH = 32;

void appendUtf8Codepoint(std::string& out, std::uint32_t cp) {
    if (cp <= 0x7FU) {
        out.push_back(static_cast<char>(cp));
    } else if (cp <= 0x7FFU) {
        out.push_back(static_cast<char>(0xC0U | ((cp >> 6U) & 0x1FU)));
        out.push_back(static_cast<char>(0x80U | (cp & 0x3FU)));
    } else if (cp <= 0xFFFFU) {
        out.push_back(static_cast<char>(0xE0U | ((cp >> 12U) & 0x0FU)));
        out.push_back(static_cast<char>(0x80U | ((cp >> 6U) & 0x3FU)));
        out.push_back(static_cast<char>(0x80U | (cp & 0x3FU)));
    } else if (cp <= 0x10FFFFU) {
        out.push_back(static_cast<char>(0xF0U | ((cp >> 18U) & 0x07U)));
        out.push_back(static_cast<char>(0x80U | ((cp >> 12U) & 0x3FU)));
        out.push_back(static_cast<char>(0x80U | ((cp >> 6U) & 0x3FU)));
        out.push_back(static_cast<char>(0x80U | (cp & 0x3FU)));
    }
}

class Parser {
public:
    explicit Parser(std::string_view src) noexcept : m_src(src) {}

    std::expected<Value, std::string> parseDocument() {
        skipWs();
        auto val = parseValue(0);
        if (!val.has_value()) {
            return val;
        }
        skipWs();
        if (m_pos != m_src.size()) {
            return std::unexpected("Unexpected trailing characters in JSON input");
        }
        return val;
    }

private:
    std::string_view m_src;
    std::size_t m_pos = 0;

    void skipWs() noexcept {
        while (m_pos < m_src.size()) {
            char c = m_src[m_pos];
            if (c == ' ' || c == '\t' || c == '\r' || c == '\n') {
                ++m_pos;
            } else {
                break;
            }
        }
    }

    std::expected<std::uint16_t, std::string> parseHex4() {
        if (m_pos + 4 > m_src.size()) {
            return std::unexpected("Truncated \\uXXXX escape in JSON string");
        }
        std::uint16_t val = 0;
        for (int i = 0; i < 4; ++i) {
            char c = m_src[m_pos++];
            val <<= 4U;
            if (c >= '0' && c <= '9') {
                val |= static_cast<std::uint16_t>(c - '0');
            } else if (c >= 'a' && c <= 'f') {
                val |= static_cast<std::uint16_t>(10 + (c - 'a'));
            } else if (c >= 'A' && c <= 'F') {
                val |= static_cast<std::uint16_t>(10 + (c - 'A'));
            } else {
                return std::unexpected("Invalid hex digit in \\uXXXX escape");
            }
        }
        return val;
    }

    std::expected<std::string, std::string> parseString() {
        if (m_pos >= m_src.size() || m_src[m_pos] != '"') {
            return std::unexpected("Expected '\"' at start of JSON string");
        }
        ++m_pos;
        std::string out;
        while (m_pos < m_src.size()) {
            unsigned char ch = static_cast<unsigned char>(m_src[m_pos++]);
            if (ch == '"') {
                return out;
            }
            if (ch < 0x20U) {
                return std::unexpected("Unescaped control character in JSON string");
            }
            if (ch != '\\') {
                out.push_back(static_cast<char>(ch));
                continue;
            }
            if (m_pos >= m_src.size()) {
                return std::unexpected("Unterminated escape sequence in JSON string");
            }
            char esc = m_src[m_pos++];
            switch (esc) {
                case '"': out.push_back('"'); break;
                case '\\': out.push_back('\\'); break;
                case '/': out.push_back('/'); break;
                case 'b': out.push_back('\b'); break;
                case 'f': out.push_back('\f'); break;
                case 'n': out.push_back('\n'); break;
                case 'r': out.push_back('\r'); break;
                case 't': out.push_back('\t'); break;
                case 'u': {
                    auto u1 = parseHex4();
                    if (!u1.has_value()) {
                        return std::unexpected(u1.error());
                    }
                    std::uint32_t cp = *u1;
                    if (cp >= 0xD800U && cp <= 0xDBFFU) {
                        if (m_pos + 2 <= m_src.size() && m_src[m_pos] == '\\' && m_src[m_pos + 1] == 'u') {
                            m_pos += 2;
                            auto u2 = parseHex4();
                            if (!u2.has_value()) {
                                return std::unexpected(u2.error());
                            }
                            if (*u2 >= 0xDC00U && *u2 <= 0xDFFFU) {
                                cp = 0x10000U + (((cp - 0xD800U) << 10U) | (*u2 - 0xDC00U));
                            }
                        }
                    }
                    appendUtf8Codepoint(out, cp);
                    break;
                }
                default:
                    return std::unexpected("Unsupported escape character in JSON string");
            }
        }
        return std::unexpected("Unterminated JSON string");
    }

    std::expected<Value, std::string> parseNumber() {
        const std::size_t start = m_pos;
        if (m_src[m_pos] == '-') {
            ++m_pos;
        }
        if (m_pos >= m_src.size() || !std::isdigit(static_cast<unsigned char>(m_src[m_pos]))) {
            return std::unexpected("Invalid JSON number");
        }
        if (m_src[m_pos] == '0') {
            ++m_pos;
        } else {
            while (m_pos < m_src.size() && std::isdigit(static_cast<unsigned char>(m_src[m_pos]))) {
                ++m_pos;
            }
        }
        bool isFloat = false;
        if (m_pos < m_src.size() && m_src[m_pos] == '.') {
            isFloat = true;
            ++m_pos;
            if (m_pos >= m_src.size() || !std::isdigit(static_cast<unsigned char>(m_src[m_pos]))) {
                return std::unexpected("Invalid fractional part in JSON number");
            }
            while (m_pos < m_src.size() && std::isdigit(static_cast<unsigned char>(m_src[m_pos]))) {
                ++m_pos;
            }
        }
        if (m_pos < m_src.size() && (m_src[m_pos] == 'e' || m_src[m_pos] == 'E')) {
            isFloat = true;
            ++m_pos;
            if (m_pos < m_src.size() && (m_src[m_pos] == '+' || m_src[m_pos] == '-')) {
                ++m_pos;
            }
            if (m_pos >= m_src.size() || !std::isdigit(static_cast<unsigned char>(m_src[m_pos]))) {
                return std::unexpected("Invalid exponent in JSON number");
            }
            while (m_pos < m_src.size() && std::isdigit(static_cast<unsigned char>(m_src[m_pos]))) {
                ++m_pos;
            }
        }

        std::string_view token = m_src.substr(start, m_pos - start);
        if (!isFloat) {
            std::int64_t intVal = 0;
            auto [ptr, ec] = std::from_chars(token.data(), token.data() + token.size(), intVal);
            if (ec == std::errc{} && ptr == token.data() + token.size()) {
                return Value(intVal);
            }
        }
        double dblVal = 0.0;
        std::string copy(token);
        try {
            dblVal = std::stod(copy);
        } catch (...) {
            return std::unexpected("JSON number out of range");
        }
        return Value(dblVal);
    }

    std::expected<Value, std::string> parseArray(int depth) {
        ++m_pos;  // '['
        skipWs();
        Array arr;
        if (m_pos < m_src.size() && m_src[m_pos] == ']') {
            ++m_pos;
            return Value(std::move(arr));
        }
        while (true) {
            skipWs();
            auto elem = parseValue(depth + 1);
            if (!elem.has_value()) {
                return elem;
            }
            arr.push_back(std::move(*elem));
            skipWs();
            if (m_pos >= m_src.size()) {
                return std::unexpected("Unterminated JSON array");
            }
            if (m_src[m_pos] == ']') {
                ++m_pos;
                return Value(std::move(arr));
            }
            if (m_src[m_pos] != ',') {
                return std::unexpected("Expected ',' or ']' in JSON array");
            }
            ++m_pos;
        }
    }

    std::expected<Value, std::string> parseObject(int depth) {
        ++m_pos;  // '{'
        skipWs();
        Object obj;
        if (m_pos < m_src.size() && m_src[m_pos] == '}') {
            ++m_pos;
            return Value(std::move(obj));
        }
        while (true) {
            skipWs();
            auto key = parseString();
            if (!key.has_value()) {
                return std::unexpected(key.error());
            }
            skipWs();
            if (m_pos >= m_src.size() || m_src[m_pos] != ':') {
                return std::unexpected("Expected ':' after JSON object key");
            }
            ++m_pos;
            skipWs();
            auto val = parseValue(depth + 1);
            if (!val.has_value()) {
                return val;
            }
            obj.insert_or_assign(std::move(*key), std::move(*val));
            skipWs();
            if (m_pos >= m_src.size()) {
                return std::unexpected("Unterminated JSON object");
            }
            if (m_src[m_pos] == '}') {
                ++m_pos;
                return Value(std::move(obj));
            }
            if (m_src[m_pos] != ',') {
                return std::unexpected("Expected ',' or '}' in JSON object");
            }
            ++m_pos;
        }
    }

    std::expected<Value, std::string> parseValue(int depth) {
        if (depth > MAX_JSON_DEPTH) {
            return std::unexpected("JSON nesting depth exceeds security limit");
        }
        skipWs();
        if (m_pos >= m_src.size()) {
            return std::unexpected("Unexpected end of JSON input");
        }
        char c = m_src[m_pos];
        if (c == '"') {
            auto s = parseString();
            if (!s.has_value()) {
                return std::unexpected(s.error());
            }
            return Value(std::move(*s));
        }
        if (c == '{') {
            return parseObject(depth);
        }
        if (c == '[') {
            return parseArray(depth);
        }
        if (c == '-' || std::isdigit(static_cast<unsigned char>(c))) {
            return parseNumber();
        }
        if (m_src.substr(m_pos, 4) == "true") {
            m_pos += 4;
            return Value(true);
        }
        if (m_src.substr(m_pos, 5) == "false") {
            m_pos += 5;
            return Value(false);
        }
        if (m_src.substr(m_pos, 4) == "null") {
            m_pos += 4;
            return Value(nullptr);
        }
        return std::unexpected("Unexpected token in JSON input");
    }
};

void escapeString(std::string& out, std::string_view sv) {
    out.push_back('"');
    static constexpr char HEX[] = "0123456789abcdef";
    for (unsigned char c : sv) {
        switch (c) {
            case '"': out += "\\\""; break;
            case '\\': out += "\\\\"; break;
            case '\b': out += "\\b"; break;
            case '\f': out += "\\f"; break;
            case '\n': out += "\\n"; break;
            case '\r': out += "\\r"; break;
            case '\t': out += "\\t"; break;
            default:
                if (c < 0x20U) {
                    out += "\\u00";
                    out.push_back(HEX[(c >> 4U) & 0xFU]);
                    out.push_back(HEX[c & 0xFU]);
                } else {
                    out.push_back(static_cast<char>(c));
                }
                break;
        }
    }
    out.push_back('"');
}

void dumpImpl(std::string& out, const Value& v) {
    if (v.isNull()) {
        out += "null";
    } else if (v.isBool()) {
        out += v.asBool() ? "true" : "false";
    } else if (v.isInt()) {
        out += std::to_string(v.asInt());
    } else if (std::holds_alternative<double>(v.data)) {
        double d = std::get<double>(v.data);
        if (!std::isfinite(d)) {
            out += "null";
        } else {
            std::ostringstream oss;
            oss << d;
            out += oss.str();
        }
    } else if (v.isString()) {
        escapeString(out, std::get<std::string>(v.data));
    } else if (const auto* arr = v.asArray()) {
        out.push_back('[');
        for (std::size_t i = 0; i < arr->size(); ++i) {
            if (i > 0) out.push_back(',');
            dumpImpl(out, (*arr)[i]);
        }
        out.push_back(']');
    } else if (const auto* obj = v.asObject()) {
        out.push_back('{');
        bool first = true;
        for (const auto& [k, child] : *obj) {
            if (!first) out.push_back(',');
            first = false;
            escapeString(out, k);
            out.push_back(':');
            dumpImpl(out, child);
        }
        out.push_back('}');
    }
}

}  // namespace

bool Value::asBool(bool fallback) const noexcept {
    if (const auto* b = std::get_if<bool>(&data)) {
        return *b;
    }
    return fallback;
}

std::int64_t Value::asInt(std::int64_t fallback) const noexcept {
    if (const auto* i = std::get_if<std::int64_t>(&data)) {
        return *i;
    }
    if (const auto* d = std::get_if<double>(&data)) {
        return static_cast<std::int64_t>(*d);
    }
    return fallback;
}

std::string Value::asString(std::string_view fallback) const {
    if (const auto* s = std::get_if<std::string>(&data)) {
        return *s;
    }
    return std::string(fallback);
}

const Array* Value::asArray() const noexcept {
    return std::get_if<Array>(&data);
}

const Object* Value::asObject() const noexcept {
    return std::get_if<Object>(&data);
}

const Value* Value::find(std::string_view key) const noexcept {
    const auto* obj = asObject();
    if (!obj) {
        return nullptr;
    }
    auto it = obj->find(key);
    if (it == obj->end()) {
        return nullptr;
    }
    return &it->second;
}

std::expected<Value, std::string> parse(std::string_view input) {
    Parser p(input);
    return p.parseDocument();
}

std::string dump(const Value& value) {
    std::string out;
    out.reserve(256);
    dumpImpl(out, value);
    return out;
}

}  // namespace mrc::json

namespace mrc {
namespace {

std::string getOptString(const json::Value& obj, std::string_view key, std::string_view def = "") {
    const auto* v = obj.find(key);
    if (!v || v->isNull()) {
        return std::string(def);
    }
    return v->asString(def);
}

std::vector<std::string> getStringArray(const json::Value& obj, std::string_view key) {
    std::vector<std::string> result;
    const auto* v = obj.find(key);
    if (!v) return result;
    const auto* arr = v->asArray();
    if (!arr) return result;
    result.reserve(arr->size());
    for (const auto& item : *arr) {
        if (item.isString()) {
            result.push_back(item.asString());
        }
    }
    return result;
}

std::filesystem::path resolveJukeboxEntryPath(
    const json::Value& entry,
    const std::filesystem::path& jukeboxNongsDir
) {
    std::string filename = getOptString(entry, "filename");
    if (!filename.empty() && !jukeboxNongsDir.empty()) {
        return jukeboxNongsDir / filename;
    }
    std::string rawPath = getOptString(entry, "path");
    if (!rawPath.empty()) {
        return std::filesystem::path(rawPath);
    }
    return {};
}

}  // namespace

std::string serializeResolveRequest(const SongQuery& query, bool asyncMode, bool forceRefresh) {
    json::Object req;
    req["source_type"] = std::string(toApiString(query.sourceType));
    if (query.gdSongId.has_value()) {
        req["gd_song_id"] = static_cast<std::int64_t>(*query.gdSongId);
    }
    if (!query.jukeboxUniqueId.empty()) {
        req["jukebox_unique_id"] = query.jukeboxUniqueId;
    }
    if (!query.jukeboxIndexId.empty()) {
        req["jukebox_index_id"] = query.jukeboxIndexId;
    }
    req["title"] = query.title.empty() ? "Unknown Title" : query.title;
    req["artist"] = query.artist.empty() ? "Unknown Artist" : query.artist;
    if (!query.sourceUrl.empty()) {
        req["source_url"] = query.sourceUrl;
    }
    if (!query.youtubeId.empty()) {
        req["youtube_id"] = query.youtubeId;
    }
    if (!query.audioSha256.empty()) {
        req["audio_sha256"] = query.audioSha256;
    }
    req["async_mode"] = asyncMode;
    req["force_refresh"] = forceRefresh;
    return json::dump(json::Value(std::move(req)));
}

std::string serializeReviewReportRequest(
    const std::string& songId,
    std::string_view reason,
    std::string_view details,
    std::string_view suggestedLicense,
    std::string_view evidenceUrl,
    std::string_view reporterClient
) {
    json::Object req;
    req["song_id"] = songId;
    req["reason"] = std::string(reason);
    req["details"] = std::string(details);
    if (!suggestedLicense.empty()) {
        req["suggested_license"] = std::string(suggestedLicense);
    }
    if (!evidenceUrl.empty()) {
        req["evidence_url"] = std::string(evidenceUrl);
    }
    req["reporter_client"] = std::string(reporterClient);
    return json::dump(json::Value(std::move(req)));
}

std::expected<SongRightsRecord, std::string> parseSongRightsRecord(const json::Value& obj) {
    if (!obj.isObject()) {
        return std::unexpected("Expected song rights JSON object");
    }

    SongRightsRecord rec;
    rec.id = getOptString(obj, "id");
    rec.canonicalKey = getOptString(obj, "canonical_key");
    rec.sourceType = sourceTypeFromApiString(getOptString(obj, "source_type", "newgrounds"));
    if (const auto* gdId = obj.find("gd_song_id"); gdId && gdId->isInt()) {
        rec.gdSongId = static_cast<int>(gdId->asInt());
    }
    rec.title = getOptString(obj, "title", "Unknown Title");
    rec.artist = getOptString(obj, "artist", "Unknown Artist");
    rec.originalArtist = getOptString(obj, "original_artist");
    rec.rightsHolders = getStringArray(obj, "rights_holders");
    rec.originalSourceUrl = getOptString(obj, "original_source_url");
    rec.licenseName = getOptString(obj, "license_name", "Unknown");
    rec.licenseUrl = getOptString(obj, "license_url");
    rec.copyrightOwnershipStatus = getOptString(obj, "copyright_ownership_status", "unknown");
    rec.overallStatus = overallStatusFromApiString(getOptString(obj, "overall_status", "unknown"));
    rec.commercialUseStatus = permissionFromApiString(getOptString(obj, "commercial_use_status", "unknown"));
    if (const auto* attrReq = obj.find("attribution_required"); attrReq && attrReq->isBool()) {
        rec.attributionRequired = attrReq->asBool();
    }
    rec.attributionTemplate = getOptString(obj, "attribution_template");

    if (const auto* platsVal = obj.find("platforms"); platsVal && platsVal->isArray()) {
        for (const auto& pItem : *platsVal->asArray()) {
            if (!pItem.isObject()) continue;
            auto platEnum = platformFromApiString(getOptString(pItem, "platform"));
            if (!platEnum.has_value()) continue;
            PlatformRights pr;
            pr.platform = *platEnum;
            pr.videoUpload = permissionFromApiString(getOptString(pItem, "video_upload", "unknown"));
            pr.livestream = permissionFromApiString(getOptString(pItem, "livestream", "unknown"));
            pr.commercialUse = permissionFromApiString(getOptString(pItem, "commercial_use", "unknown"));
            pr.monetization = permissionFromApiString(getOptString(pItem, "monetization", "unknown"));
            if (const auto* ar = pItem.find("attribution_required"); ar && ar->isBool()) {
                pr.attributionRequired = ar->asBool();
            }
            pr.automatedClaimRisk = claimRiskFromApiString(getOptString(pItem, "automated_claim_risk", "unknown"));
            pr.conditions = getStringArray(pItem, "conditions");
            pr.restrictions = getStringArray(pItem, "restrictions");
            pr.policyReferenceUrl = getOptString(pItem, "policy_reference_url");
            rec.platforms.push_back(std::move(pr));
        }
    }

    if (const auto* evVal = obj.find("evidence"); evVal && evVal->isArray()) {
        for (const auto& eItem : *evVal->asArray()) {
            if (!eItem.isObject()) continue;
            EvidenceItem ev;
            ev.sourceKind = getOptString(eItem, "source_kind");
            ev.sourceTitle = getOptString(eItem, "source_title");
            ev.sourceUrl = getOptString(eItem, "source_url");
            ev.fetchStatus = getOptString(eItem, "fetch_status");
            ev.excerpt = getOptString(eItem, "excerpt");
            ev.retrievedAtIso = getOptString(eItem, "retrieved_at");
            rec.evidence.push_back(std::move(ev));
        }
    }

    rec.uncertainties = getStringArray(obj, "uncertainties");
    rec.conflictingInformation = getStringArray(obj, "conflicting_information");
    rec.verificationMethod = getOptString(obj, "verification_method", "conservative_fallback");
    rec.reviewerNotes = getOptString(obj, "reviewer_notes");
    rec.lastVerifiedAtIso = getOptString(obj, "last_verified_at");
    rec.expiresAtIso = getOptString(obj, "expires_at");
    if (const auto* ca = obj.find("cached_at_unix"); ca && ca->isInt()) {
        rec.cachedAtUnix = ca->asInt();
    }
    if (const auto* le = obj.find("local_expires_at_unix"); le && le->isInt()) {
        rec.localExpiresAtUnix = le->asInt();
    }
    if (const auto* stale = obj.find("is_stale"); stale && stale->isBool()) {
        rec.isStale = stale->asBool();
    }
    rec.disclaimer = getOptString(obj, "disclaimer");

    if (rec.canonicalKey.empty() && rec.title.empty()) {
        return std::unexpected("Song rights record is missing both canonical_key and title");
    }
    return rec;
}

json::Value songRightsRecordToJson(const SongRightsRecord& record) {
    json::Object obj;
    obj["id"] = record.id;
    obj["canonical_key"] = record.canonicalKey;
    obj["source_type"] = std::string(toApiString(record.sourceType));
    if (record.gdSongId.has_value()) {
        obj["gd_song_id"] = static_cast<std::int64_t>(*record.gdSongId);
    }
    obj["title"] = record.title;
    obj["artist"] = record.artist;
    obj["original_artist"] = record.originalArtist;

    auto toJsonArray = [](const std::vector<std::string>& items) {
        json::Array arr;
        arr.reserve(items.size());
        for (const auto& s : items) {
            arr.emplace_back(s);
        }
        return json::Value(std::move(arr));
    };

    obj["rights_holders"] = toJsonArray(record.rightsHolders);
    obj["original_source_url"] = record.originalSourceUrl;
    obj["license_name"] = record.licenseName;
    obj["license_url"] = record.licenseUrl;
    obj["copyright_ownership_status"] = record.copyrightOwnershipStatus;
    obj["overall_status"] = std::string(toApiString(record.overallStatus));
    obj["commercial_use_status"] = std::string(toApiString(record.commercialUseStatus));
    obj["attribution_required"] = record.attributionRequired;
    obj["attribution_template"] = record.attributionTemplate;

    json::Array plats;
    for (const auto& pr : record.platforms) {
        json::Object po;
        po["platform"] = std::string(toApiString(pr.platform));
        po["video_upload"] = std::string(toApiString(pr.videoUpload));
        po["livestream"] = std::string(toApiString(pr.livestream));
        po["commercial_use"] = std::string(toApiString(pr.commercialUse));
        po["monetization"] = std::string(toApiString(pr.monetization));
        po["attribution_required"] = pr.attributionRequired;
        po["automated_claim_risk"] = std::string(toApiString(pr.automatedClaimRisk));
        po["conditions"] = toJsonArray(pr.conditions);
        po["restrictions"] = toJsonArray(pr.restrictions);
        po["policy_reference_url"] = pr.policyReferenceUrl;
        plats.emplace_back(std::move(po));
    }
    obj["platforms"] = json::Value(std::move(plats));

    json::Array evs;
    for (const auto& ev : record.evidence) {
        json::Object eo;
        eo["source_kind"] = ev.sourceKind;
        eo["source_title"] = ev.sourceTitle;
        eo["source_url"] = ev.sourceUrl;
        eo["fetch_status"] = ev.fetchStatus;
        eo["excerpt"] = ev.excerpt;
        eo["retrieved_at"] = ev.retrievedAtIso;
        evs.emplace_back(std::move(eo));
    }
    obj["evidence"] = json::Value(std::move(evs));

    obj["uncertainties"] = toJsonArray(record.uncertainties);
    obj["conflicting_information"] = toJsonArray(record.conflictingInformation);
    obj["verification_method"] = record.verificationMethod;
    obj["reviewer_notes"] = record.reviewerNotes;
    obj["last_verified_at"] = record.lastVerifiedAtIso;
    obj["expires_at"] = record.expiresAtIso;
    obj["cached_at_unix"] = record.cachedAtUnix;
    obj["local_expires_at_unix"] = record.localExpiresAtUnix;
    obj["is_stale"] = record.isStale;
    obj["disclaimer"] = record.disclaimer;

    return json::Value(std::move(obj));
}

std::expected<ResolveApiResult, std::string> parseResolveResponse(std::string_view body) {
    auto rootExp = json::parse(body);
    if (!rootExp.has_value()) {
        return std::unexpected(rootExp.error());
    }
    const auto& root = *rootExp;
    if (!root.isObject()) {
        return std::unexpected("Expected JSON object from /songs/resolve");
    }

    ResolveApiResult res;
    res.resolutionStatus = getOptString(root, "resolution_status", "ready");
    if (const auto* c = root.find("cached"); c && c->isBool()) {
        res.cached = c->asBool();
    }
    res.jobId = getOptString(root, "job_id");
    if (const auto* recVal = root.find("record"); recVal && recVal->isObject()) {
        auto parsedRec = parseSongRightsRecord(*recVal);
        if (!parsedRec.has_value()) {
            return std::unexpected(parsedRec.error());
        }
        res.record = std::move(*parsedRec);
    }
    return res;
}

std::expected<std::vector<SongRightsRecord>, std::string> parseSongListResponse(std::string_view body) {
    auto rootExp = json::parse(body);
    if (!rootExp.has_value()) {
        return std::unexpected(rootExp.error());
    }
    const auto* items = rootExp->find("items");
    if (!items || !items->isArray()) {
        return std::unexpected("Expected 'items' array in /songs response");
    }
    std::vector<SongRightsRecord> out;
    out.reserve(items->asArray()->size());
    for (const auto& item : *items->asArray()) {
        auto parsed = parseSongRightsRecord(item);
        if (parsed.has_value()) {
            out.push_back(std::move(*parsed));
        }
    }
    return out;
}

std::expected<std::vector<SongQuery>, std::string> parseJukeboxManifestAllSongs(
    std::string_view manifestJson,
    const SongQuery& fallbackDefault,
    const std::filesystem::path& jukeboxNongsDir
) {
    auto rootExp = json::parse(manifestJson);
    if (!rootExp.has_value()) {
        return std::unexpected(rootExp.error());
    }
    const auto& root = *rootExp;
    if (!root.isObject()) {
        return std::unexpected("Jukebox manifest root must be a JSON object");
    }

    std::vector<SongQuery> allSongs;

    const auto* defObj = root.find("default");
    if (!defObj || !defObj->isObject()) {
        return std::unexpected("Jukebox manifest is missing 'default' object");
    }

    SongQuery defQuery = fallbackDefault;
    defQuery.jukeboxUniqueId = getOptString(*defObj, "unique_id");
    if (std::string defName = getOptString(*defObj, "name"); !defName.empty()) {
        defQuery.title = std::move(defName);
    }
    if (std::string defArtist = getOptString(*defObj, "artist"); !defArtist.empty()) {
        defQuery.artist = std::move(defArtist);
    }
    allSongs.push_back(std::move(defQuery));

    if (const auto* localsVal = root.find("locals"); localsVal && localsVal->isArray()) {
        for (const auto& entry : *localsVal->asArray()) {
            if (!entry.isObject()) continue;
            SongQuery sq;
            sq.sourceType = SourceType::JukeboxLocal;
            sq.gdSongId = fallbackDefault.gdSongId;
            sq.jukeboxUniqueId = getOptString(entry, "unique_id");
            sq.title = getOptString(entry, "name", "Local NONG");
            sq.artist = getOptString(entry, "artist", "Unknown Artist");
            auto resolvedPath = resolveJukeboxEntryPath(entry, jukeboxNongsDir);
            if (!resolvedPath.empty()) {
                sq.localFilePath = resolvedPath.string();
            }
            allSongs.push_back(std::move(sq));
        }
    }

    if (const auto* ytVal = root.find("youtube"); ytVal && ytVal->isArray()) {
        for (const auto& entry : *ytVal->asArray()) {
            if (!entry.isObject()) continue;
            SongQuery sq;
            sq.sourceType = SourceType::JukeboxYoutube;
            sq.gdSongId = fallbackDefault.gdSongId;
            sq.jukeboxUniqueId = getOptString(entry, "unique_id");
            sq.jukeboxIndexId = getOptString(entry, "index_id");
            sq.title = getOptString(entry, "name", "YouTube NONG");
            sq.artist = getOptString(entry, "artist", "Unknown Artist");
            sq.youtubeId = getOptString(entry, "youtube_id");
            if (!sq.youtubeId.empty()) {
                sq.sourceUrl = "https://www.youtube.com/watch?v=" + sq.youtubeId;
            }
            auto resolvedPath = resolveJukeboxEntryPath(entry, jukeboxNongsDir);
            if (!resolvedPath.empty()) {
                sq.localFilePath = resolvedPath.string();
            }
            allSongs.push_back(std::move(sq));
        }
    }

    if (const auto* hostedVal = root.find("hosted"); hostedVal && hostedVal->isArray()) {
        for (const auto& entry : *hostedVal->asArray()) {
            if (!entry.isObject()) continue;
            SongQuery sq;
            sq.sourceType = SourceType::JukeboxHosted;
            sq.gdSongId = fallbackDefault.gdSongId;
            sq.jukeboxUniqueId = getOptString(entry, "unique_id");
            sq.jukeboxIndexId = getOptString(entry, "index_id");
            sq.title = getOptString(entry, "name", "Hosted NONG");
            sq.artist = getOptString(entry, "artist", "Unknown Artist");
            sq.sourceUrl = getOptString(entry, "url");
            auto resolvedPath = resolveJukeboxEntryPath(entry, jukeboxNongsDir);
            if (!resolvedPath.empty()) {
                sq.localFilePath = resolvedPath.string();
            }
            allSongs.push_back(std::move(sq));
        }
    }

    return allSongs;
}

std::expected<SongQuery, std::string> parseJukeboxManifestActiveSong(
    std::string_view manifestJson,
    const SongQuery& fallbackDefault,
    const std::filesystem::path& jukeboxNongsDir
) {
    auto rootExp = json::parse(manifestJson);
    if (!rootExp.has_value()) {
        return std::unexpected(rootExp.error());
    }
    std::string activeUniqueId = getOptString(*rootExp, "active");

    auto allExp = parseJukeboxManifestAllSongs(manifestJson, fallbackDefault, jukeboxNongsDir);
    if (!allExp.has_value()) {
        return std::unexpected(allExp.error());
    }
    const auto& allSongs = *allExp;
    if (allSongs.empty()) {
        return fallbackDefault;
    }

    if (activeUniqueId.empty() || activeUniqueId == allSongs.front().jukeboxUniqueId) {
        return allSongs.front();
    }

    for (const auto& song : allSongs) {
        if (song.jukeboxUniqueId == activeUniqueId) {
            return song;
        }
    }

    // Matches Jukebox's own fallback behaviour when `active` points to a deleted entry
    return allSongs.front();
}

}  // namespace mrc
