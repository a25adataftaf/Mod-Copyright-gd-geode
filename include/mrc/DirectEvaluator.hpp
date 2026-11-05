#pragma once

#include <optional>
#include <string>
#include <string_view>

#include <mrc/Types.hpp>

namespace mrc {

struct DirectFetchedPage {
    std::string url;
    int httpStatus = 0;
    std::string rawBody;
};

/**
 * Pure C++23 evidence-driven direct rights evaluator.
 *
 * Used by the Geode client when no centralized backend URL is configured (standalone mode)
 * or when the configured backend is temporarily unreachable, without requiring any private
 * API keys and without ever fabricating legal certainty.
 */
class DirectEvaluator {
public:
    /**
     * Returns true if `title`, `artist`, or `extraText` contains word-bounded indicators
     * of a remix, mashup, bootleg, cover, nightcore, or derivative edit.
     */
    [[nodiscard]] static bool hasRemixOrMashupSignals(
        std::string_view title,
        std::string_view artist,
        std::string_view extraText = ""
    ) noexcept;

    /**
     * Strips HTML scripts/styles/tags, decodes basic HTML entities, and bounds text length.
     */
    [[nodiscard]] static std::string sanitizeHtmlExcerpt(
        std::string_view rawHtml,
        std::size_t maxChars = 420
    );

    /**
     * Extracts the official NCS copy-ready attribution block (`#panel-copy2`) from an `ncs.io`
     * track HTML page if present.
     */
    [[nodiscard]] static std::string extractNcsAttributionFromHtml(std::string_view ncsHtml);

    /**
     * Evaluates a song conservatively from its `SongQuery` metadata and optional directly
     * fetched public licence page (`newgrounds.com/audio/listen/<id>` or `ncs.io/<slug>`).
     */
    [[nodiscard]] static SongRightsRecord evaluate(
        const SongQuery& query,
        const std::optional<DirectFetchedPage>& fetchedPage,
        std::string_view verifiedAtIsoUtc
    );
};

}  // namespace mrc
