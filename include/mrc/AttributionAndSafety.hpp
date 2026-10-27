#pragma once

#include <cstdint>
#include <span>
#include <string>
#include <string_view>

#include <mrc/Types.hpp>

namespace mrc {

/**
 * Validates that a URL is safe to open in the user's system browser.
 * Allows strictly public `https://` and `http://` URLs with no embedded credentials,
 * control characters, or private/loopback IP literals.
 */
[[nodiscard]] bool isSafeBrowserUrl(std::string_view url) noexcept;

/**
 * Validates the user-configured backend base URL.
 * Allows `https://` public endpoints or explicit `http://127.0.0.1` / `http://localhost` for local dev.
 */
[[nodiscard]] bool isSafeBackendBaseUrl(std::string_view url) noexcept;

/**
 * Generates copy-ready attribution text grounded strictly in the applicable licence.
 * Never fabricates permission text for restricted or unverified songs.
 */
[[nodiscard]] std::string formatAttributionForCopy(const SongRightsRecord& record);

/**
 * Computes lowercase 64-character hexadecimal SHA-256 digest in pure C++23.
 * Used ONLY when the user explicitly opts in (`share-local-file-hash = true`).
 */
[[nodiscard]] std::string sha256Hex(std::span<const std::uint8_t> bytes);

}  // namespace mrc
