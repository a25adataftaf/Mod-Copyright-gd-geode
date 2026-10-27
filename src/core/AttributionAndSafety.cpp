#include <mrc/AttributionAndSafety.hpp>

#include <array>
#include <cctype>
#include <cstring>
#include <sstream>

namespace mrc {
namespace {

bool hasUnsafeChars(std::string_view s) noexcept {
    for (unsigned char c : s) {
        if (c <= 0x20 || c == 0x7f) {
            return true;
        }
    }
    return false;
}

std::string toLowerAscii(std::string_view s) {
    std::string out(s.size(), '\0');
    for (std::size_t i = 0; i < s.size(); ++i) {
        out[i] = static_cast<char>(std::tolower(static_cast<unsigned char>(s[i])));
    }
    return out;
}

struct ParsedAuthority {
    std::string scheme;
    std::string host;
    bool valid = false;
};

ParsedAuthority parseHttpAuthority(std::string_view url) noexcept {
    if (url.empty() || url.size() > 1024 || hasUnsafeChars(url)) {
        return {};
    }
    auto schemePos = url.find("://");
    if (schemePos == std::string_view::npos) {
        return {};
    }
    std::string scheme = toLowerAscii(url.substr(0, schemePos));
    if (scheme != "https" && scheme != "http") {
        return {};
    }
    std::string_view rest = url.substr(schemePos + 3);
    auto slashPos = rest.find_first_of("/?#");
    std::string_view authority = (slashPos == std::string_view::npos) ? rest : rest.substr(0, slashPos);
    if (authority.empty() || authority.find('@') != std::string_view::npos) {
        // Reject embedded userinfo credentials
        return {};
    }
    std::string_view hostPart = authority;
    if (hostPart.front() == '[') {
        auto closeBracket = hostPart.find(']');
        if (closeBracket == std::string_view::npos) {
            return {};
        }
        hostPart = hostPart.substr(1, closeBracket - 1);
    } else {
        auto colon = hostPart.find(':');
        if (colon != std::string_view::npos) {
            std::string_view portStr = hostPart.substr(colon + 1);
            if (portStr.empty()) {
                return {};
            }
            for (unsigned char ch : portStr) {
                if (!std::isdigit(ch)) {
                    return {};
                }
            }
            hostPart = hostPart.substr(0, colon);
        }
    }
    if (hostPart.empty()) {
        return {};
    }
    return ParsedAuthority{
        .scheme = std::move(scheme),
        .host = toLowerAscii(hostPart),
        .valid = true,
    };
}

bool isLoopbackOrPrivateHost(std::string_view host) noexcept {
    if (host == "localhost" || host == "127.0.0.1" || host == "::1" || host == "0.0.0.0") {
        return true;
    }
    if (host.starts_with("127.") || host.starts_with("10.") || host.starts_with("192.168.") ||
        host.starts_with("169.254.")) {
        return true;
    }
    if (host.starts_with("172.")) {
        auto secondDot = host.find('.', 4);
        if (secondDot != std::string_view::npos) {
            int secondOctet = 0;
            for (std::size_t i = 4; i < secondDot; ++i) {
                if (!std::isdigit(static_cast<unsigned char>(host[i]))) {
                    secondOctet = -1;
                    break;
                }
                secondOctet = secondOctet * 10 + (host[i] - '0');
            }
            if (secondOctet >= 16 && secondOctet <= 31) {
                return true;
            }
        }
    }
    if (host.ends_with(".local") || host.ends_with(".internal")) {
        return true;
    }
    return false;
}

constexpr std::uint32_t rotr32(std::uint32_t x, unsigned int n) noexcept {
    return (x >> n) | (x << (32U - n));
}

}  // namespace

bool isSafeBrowserUrl(std::string_view url) noexcept {
    const auto parsed = parseHttpAuthority(url);
    if (!parsed.valid) {
        return false;
    }
    if (isLoopbackOrPrivateHost(parsed.host)) {
        return false;
    }
    if (parsed.host.find('.') == std::string::npos) {
        return false;
    }
    return true;
}

bool isSafeBackendBaseUrl(std::string_view url) noexcept {
    const auto parsed = parseHttpAuthority(url);
    if (!parsed.valid) {
        return false;
    }
    if (parsed.scheme == "https") {
        return !isLoopbackOrPrivateHost(parsed.host) && parsed.host.find('.') != std::string::npos;
    }
    // Plain HTTP is permitted strictly for loopback local development
    return parsed.host == "127.0.0.1" || parsed.host == "localhost";
}

std::string formatAttributionForCopy(const SongRightsRecord& record) {
    if (!record.attributionTemplate.empty()) {
        return record.attributionTemplate;
    }

    if (record.overallStatus == OverallStatus::Restricted ||
        record.overallStatus == OverallStatus::Unknown) {
        return "";
    }

    std::ostringstream oss;
    oss << "Music: \"" << record.title << "\" by "
        << (record.originalArtist.empty() ? record.artist : record.originalArtist);
    if (!record.originalSourceUrl.empty() && isSafeBrowserUrl(record.originalSourceUrl)) {
        oss << "\nSource: " << record.originalSourceUrl;
    }
    if (!record.licenseName.empty() && record.licenseName != "Unknown") {
        oss << "\nLicence: " << record.licenseName;
        if (!record.licenseUrl.empty() && isSafeBrowserUrl(record.licenseUrl)) {
            oss << " (" << record.licenseUrl << ")";
        }
    }
    return oss.str();
}

std::string sha256Hex(std::span<const std::uint8_t> bytes) {
    static constexpr std::array<std::uint32_t, 64> K = {
        0x428a2f98U, 0x71374491U, 0xb5c0fbcfU, 0xe9b5dba5U, 0x3956c25bU, 0x59f111f1U, 0x923f82a4U, 0xab1c5ed5U,
        0xd807aa98U, 0x12835b01U, 0x243185beU, 0x550c7dc3U, 0x72be5d74U, 0x80deb1feU, 0x9bdc06a7U, 0xc19bf174U,
        0xe49b69c1U, 0xefbe4786U, 0x0fc19dc6U, 0x240ca1ccU, 0x2de92c6fU, 0x4a7484aaU, 0x5cb0a9dcU, 0x76f988daU,
        0x983e5152U, 0xa831c66dU, 0xb00327c8U, 0xbf597fc7U, 0xc6e00bf3U, 0xd5a79147U, 0x06ca6351U, 0x14292967U,
        0x27b70a85U, 0x2e1b2138U, 0x4d2c6dfcU, 0x53380d13U, 0x650a7354U, 0x766a0abbU, 0x81c2c92eU, 0x92722c85U,
        0xa2bfe8a1U, 0xa81a664bU, 0xc24b8b70U, 0xc76c51a3U, 0xd192e819U, 0xd6990624U, 0xf40e3585U, 0x106aa070U,
        0x19a4c116U, 0x1e376c08U, 0x2748774cU, 0x34b0bcb5U, 0x391c0cb3U, 0x4ed8aa4aU, 0x5b9cca4fU, 0x682e6ff3U,
        0x748f82eeU, 0x78a5636fU, 0x84c87814U, 0x8cc70208U, 0x90befffaU, 0xa4506cebU, 0xbef9a3f7U, 0xc67178f2U,
    };

    std::array<std::uint32_t, 8> H = {
        0x6a09e667U, 0xbb67ae85U, 0x3c6ef372U, 0xa54ff53aU,
        0x510e527fU, 0x9b05688cU, 0x1f83d9abU, 0x5be0cd19U,
    };

    auto processBlock = [&H](const std::uint8_t* block) noexcept {
        std::array<std::uint32_t, 64> w{};
        for (std::size_t i = 0; i < 16; ++i) {
            w[i] = (static_cast<std::uint32_t>(block[i * 4]) << 24U) |
                   (static_cast<std::uint32_t>(block[i * 4 + 1]) << 16U) |
                   (static_cast<std::uint32_t>(block[i * 4 + 2]) << 8U) |
                   static_cast<std::uint32_t>(block[i * 4 + 3]);
        }
        for (std::size_t i = 16; i < 64; ++i) {
            std::uint32_t s0 = rotr32(w[i - 15], 7) ^ rotr32(w[i - 15], 18) ^ (w[i - 15] >> 3U);
            std::uint32_t s1 = rotr32(w[i - 2], 17) ^ rotr32(w[i - 2], 19) ^ (w[i - 2] >> 10U);
            w[i] = w[i - 16] + s0 + w[i - 7] + s1;
        }

        std::uint32_t a = H[0], b = H[1], c = H[2], d = H[3];
        std::uint32_t e = H[4], f = H[5], g = H[6], h = H[7];

        for (std::size_t i = 0; i < 64; ++i) {
            std::uint32_t S1 = rotr32(e, 6) ^ rotr32(e, 11) ^ rotr32(e, 25);
            std::uint32_t ch = (e & f) ^ ((~e) & g);
            std::uint32_t temp1 = h + S1 + ch + K[i] + w[i];
            std::uint32_t S0 = rotr32(a, 2) ^ rotr32(a, 13) ^ rotr32(a, 22);
            std::uint32_t maj = (a & b) ^ (a & c) ^ (b & c);
            std::uint32_t temp2 = S0 + maj;

            h = g;
            g = f;
            f = e;
            e = d + temp1;
            d = c;
            c = b;
            b = a;
            a = temp1 + temp2;
        }

        H[0] += a;
        H[1] += b;
        H[2] += c;
        H[3] += d;
        H[4] += e;
        H[5] += f;
        H[6] += g;
        H[7] += h;
    };

    const std::size_t fullBlocks = bytes.size() / 64;
    for (std::size_t i = 0; i < fullBlocks; ++i) {
        processBlock(bytes.data() + i * 64);
    }

    std::array<std::uint8_t, 128> tail{};
    const std::size_t rem = bytes.size() % 64;
    if (rem > 0) {
        std::memcpy(tail.data(), bytes.data() + fullBlocks * 64, rem);
    }
    tail[rem] = 0x80U;
    const std::size_t totalTailLen = (rem < 56) ? 64 : 128;
    const std::uint64_t bitLen = static_cast<std::uint64_t>(bytes.size()) * 8ULL;
    for (int i = 0; i < 8; ++i) {
        tail[totalTailLen - 1 - i] = static_cast<std::uint8_t>((bitLen >> (i * 8)) & 0xFFU);
    }
    processBlock(tail.data());
    if (totalTailLen == 128) {
        processBlock(tail.data() + 64);
    }

    static constexpr char HEX[] = "0123456789abcdef";
    std::string digest;
    digest.reserve(64);
    for (std::uint32_t word : H) {
        for (int shift = 28; shift >= 0; shift -= 4) {
            digest.push_back(HEX[(word >> shift) & 0xFU]);
        }
    }
    return digest;
}

}  // namespace mrc
