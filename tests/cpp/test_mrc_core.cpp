#include <cassert>
#include <cstdint>
#include <filesystem>
#include <iostream>
#include <span>
#include <string>
#include <vector>

#include <mrc/AttributionAndSafety.hpp>
#include <mrc/DirectEvaluator.hpp>
#include <mrc/JsonCodec.hpp>
#include <mrc/LocalCache.hpp>
#include <mrc/Types.hpp>

namespace {

int g_passed = 0;

#define MRC_EXPECT(cond)                                                                         \
    do {                                                                                         \
        if (!(cond)) {                                                                           \
            std::cerr << "TEST FAILED at " << __FILE__ << ":" << __LINE__ << ": " #cond << "\n"; \
            std::exit(1);                                                                        \
        }                                                                                        \
    } while (0)

void testGdSongClassificationAndJukeboxIds() {
    MRC_EXPECT(mrc::adjustJukeboxSongId(0, true) == -1);
    MRC_EXPECT(mrc::adjustJukeboxSongId(21, true) == -22);
    MRC_EXPECT(mrc::adjustJukeboxSongId(-5, true) == -5);
    MRC_EXPECT(mrc::adjustJukeboxSongId(467339, false) == 467339);

    MRC_EXPECT(mrc::classifyDefaultGdSong(0, true, false, false, 0) == mrc::SourceType::RobTopOfficial);
    MRC_EXPECT(mrc::classifyDefaultGdSong(10004512, false, true, true, 0) == mrc::SourceType::NcsLibrary);
    MRC_EXPECT(mrc::classifyDefaultGdSong(10004512, false, true, false, 1) == mrc::SourceType::NcsLibrary);
    MRC_EXPECT(mrc::classifyDefaultGdSong(10004512, false, true, false, 0) == mrc::SourceType::GdMusicLibrary);
    MRC_EXPECT(mrc::classifyDefaultGdSong(467339, false, false, false, 0) == mrc::SourceType::Newgrounds);
    ++g_passed;
}

void testJukeboxManifestParsing() {
    const std::string manifest = R"JSON({
        "version": 1,
        "default": {
            "name": "Clubstep",
            "unique_id": "default-0",
            "artist": "DJ-Nate",
            "filename": "Clubstep.mp3",
            "path": "/gd/Resources/Clubstep.mp3",
            "offset": 0
        },
        "active": "sfh-hosted-1",
        "locals": [
            {
                "name": "My Bootleg Mashup",
                "unique_id": "local-99",
                "artist": "Unknown DJ",
                "filename": "local-99.mp3",
                "path": "/save/nongs/local-99.mp3",
                "offset": 0
            }
        ],
        "youtube": [
            {
                "name": "XO",
                "unique_id": "yt-xo",
                "artist": "Eden",
                "path": "/save/nongs/yt-xo.mp3",
                "offset": 0,
                "youtube_id": "tWh3dG3H35A",
                "index_id": "sfh-main"
            }
        ],
        "hosted": [
            {
                "name": "Wash Over Me",
                "unique_id": "sfh-hosted-1",
                "artist": "glaive",
                "filename": "sfh-hosted-1.mp3",
                "path": "/save/nongs/sfh-hosted-1.mp3",
                "offset": 0,
                "url": "https://api.songfilehub.com/song/abc123?download=true",
                "index_id": "song-file-hub"
            }
        ]
    })JSON";

    mrc::SongQuery defQuery;
    defQuery.sourceType = mrc::SourceType::RobTopOfficial;
    defQuery.gdSongId = 14;
    defQuery.title = "Clubstep";
    defQuery.artist = "DJ-Nate";

    auto activeRes = mrc::parseJukeboxManifestActiveSong(manifest, defQuery, "/save/nongs");
    MRC_EXPECT(activeRes.has_value());
    MRC_EXPECT(activeRes->sourceType == mrc::SourceType::JukeboxHosted);
    MRC_EXPECT(activeRes->title == "Wash Over Me");
    MRC_EXPECT(activeRes->artist == "glaive");
    MRC_EXPECT(activeRes->jukeboxIndexId == "song-file-hub");
    MRC_EXPECT(activeRes->sourceUrl == "https://api.songfilehub.com/song/abc123?download=true");

    auto allRes = mrc::parseJukeboxManifestAllSongs(manifest, defQuery, "/save/nongs");
    MRC_EXPECT(allRes.has_value());
    MRC_EXPECT(allRes->size() == 4);
    MRC_EXPECT((*allRes)[0].sourceType == mrc::SourceType::RobTopOfficial);
    MRC_EXPECT((*allRes)[1].sourceType == mrc::SourceType::JukeboxLocal);
    MRC_EXPECT((*allRes)[2].sourceType == mrc::SourceType::JukeboxYoutube);
    MRC_EXPECT((*allRes)[2].youtubeId == "tWh3dG3H35A");

    // Verify orphaned active ID falls back safely to default song
    const std::string orphanedManifest = R"JSON({
        "default": {"name": "Stereo Madness", "unique_id": "def", "artist": "ForeverBound", "path": "StereoMadness.mp3"},
        "active": "deleted-nong-id",
        "locals": [],
        "youtube": [],
        "hosted": []
    })JSON";
    auto fallbackRes = mrc::parseJukeboxManifestActiveSong(orphanedManifest, defQuery);
    MRC_EXPECT(fallbackRes.has_value());
    MRC_EXPECT(fallbackRes->sourceType == mrc::SourceType::RobTopOfficial);
    MRC_EXPECT(fallbackRes->title == "Stereo Madness");
    ++g_passed;
}

void testJsonParserSecurityAndUnicode() {
    auto unicodeParsed = mrc::json::parse(R"({"symbol":"\uD834\uDD1E","escaped":"line1\nline2"})");
    MRC_EXPECT(unicodeParsed.has_value());
    MRC_EXPECT(unicodeParsed->find("symbol")->asString() == "\xF0\x9D\x84\x9E");
    MRC_EXPECT(unicodeParsed->find("escaped")->asString() == "line1\nline2");

    // Excessive nesting depth must be rejected cleanly without stack overflow
    std::string deepJson(40, '[');
    deepJson += "1";
    deepJson += std::string(40, ']');
    auto deepRes = mrc::json::parse(deepJson);
    MRC_EXPECT(!deepRes.has_value());

    // Unescaped control character in string must be rejected
    std::string badCtrl = "{\"a\":\"hello\x01world\"}";
    MRC_EXPECT(!mrc::json::parse(badCtrl).has_value());
    ++g_passed;
}

void testApiCodecAndCacheLifecycle() {
    const std::string sampleResolveJson = R"JSON({
        "resolution_status": "ready",
        "cached": false,
        "record": {
            "id": "11111111-2222-3333-4444-555555555555",
            "canonical_key": "ncs:gd:10012345",
            "source_type": "ncs_library",
            "gd_song_id": 10012345,
            "title": "Mortals",
            "artist": "Warriyo",
            "original_artist": "Warriyo",
            "rights_holders": ["Warriyo", "NoCopyrightSounds (NCS)"],
            "original_source_url": "https://ncs.io/Mortals",
            "license_name": "NCS Creator Usage License (Independent Creators Only)",
            "license_url": "https://ncs.io/usage-policy",
            "copyright_ownership_status": "NCS / Participating Artist & Label",
            "overall_status": "permitted_with_conditions",
            "commercial_use_status": "conditional",
            "attribution_required": true,
            "attribution_template": "Song: Warriyo - Mortals [NCS Release]\nMusic provided by NoCopyrightSounds",
            "platforms": [
                {
                    "platform": "youtube",
                    "video_upload": "allowed",
                    "livestream": "allowed",
                    "commercial_use": "conditional",
                    "monetization": "conditional",
                    "attribution_required": true,
                    "automated_claim_risk": "low",
                    "conditions": ["Copy exact NCS attribution into description"],
                    "restrictions": ["Brands require commercial licence"],
                    "policy_reference_url": "https://ncs.io/usage-policy"
                },
                {
                    "platform": "twitch",
                    "video_upload": "allowed",
                    "livestream": "allowed",
                    "commercial_use": "conditional",
                    "monetization": "conditional",
                    "attribution_required": true,
                    "automated_claim_risk": "low",
                    "conditions": ["Credit NCS in stream panel"],
                    "restrictions": [],
                    "policy_reference_url": "https://ncs.io/usage-policy"
                }
            ],
            "evidence": [
                {
                    "source_kind": "ncs_release_page",
                    "source_title": "NCS Policy",
                    "source_url": "https://ncs.io/usage-policy",
                    "fetch_status": "verified",
                    "excerpt": "Independent creators may monetize with credit.",
                    "retrieved_at": "2026-10-27T12:00:00Z"
                }
            ],
            "uncertainties": [],
            "conflicting_information": [],
            "verification_method": "rule_engine",
            "last_verified_at": "2026-10-27T12:00:00Z",
            "expires_at": "2026-11-03T12:00:00Z",
            "is_stale": false,
            "disclaimer": "Informational guidance only, not legal advice."
        }
    })JSON";

    auto apiRes = mrc::parseResolveResponse(sampleResolveJson);
    MRC_EXPECT(apiRes.has_value());
    MRC_EXPECT(apiRes->record.has_value());
    MRC_EXPECT(apiRes->record->overallStatus == mrc::OverallStatus::PermittedWithConditions);
    MRC_EXPECT(apiRes->record->platforms.size() == 2);

    // Badge when fresh
    auto freshBadge = mrc::badgeStyleForRecord(apiRes->record, false, false);
    MRC_EXPECT(!freshBadge.staleOrOfflineWarning);
    MRC_EXPECT(freshBadge.shortTag == "MRC CR*");

    // Persist in LocalRightsCache and verify staleness transition
    const auto cacheFile = std::filesystem::temp_directory_path() / "mrc_test_rights_cache.json";
    std::error_code ec;
    std::filesystem::remove(cacheFile, ec);

    {
        mrc::LocalRightsCache cache(cacheFile, 3600, 2);
        cache.put("ncs:gd:10012345", *apiRes->record, 1000000);
        MRC_EXPECT(cache.saveToDisk());
    }

    {
        mrc::LocalRightsCache reloaded(cacheFile, 3600, 2);
        MRC_EXPECT(reloaded.loadFromDisk());
        MRC_EXPECT(reloaded.size() == 1);

        auto freshHit = reloaded.get("ncs:gd:10012345", 1000500);
        MRC_EXPECT(freshHit.has_value());
        MRC_EXPECT(freshHit->isFresh);
        MRC_EXPECT(!freshHit->record.isStale);

        // After TTL expires, cache MUST mark entry stale and badge MUST show stale warning
        auto expiredHit = reloaded.get("ncs:gd:10012345", 1005000);
        MRC_EXPECT(expiredHit.has_value());
        MRC_EXPECT(!expiredHit->isFresh);
        MRC_EXPECT(expiredHit->record.isStale);

        auto staleBadge = mrc::badgeStyleForRecord(expiredHit->record, false, false);
        MRC_EXPECT(staleBadge.staleOrOfflineWarning);
        MRC_EXPECT(staleBadge.shortTag == "MRC CR~");

        // Search and filter
        mrc::LocalSearchFilter filter;
        filter.queryText = "warriyo";
        filter.commercialAllowedOnly = true;
        filter.requiredPlatformMonetizable = mrc::Platform::YouTube;
        auto searchHits = reloaded.search(filter, 1000500);
        MRC_EXPECT(searchHits.size() == 1);

        filter.requiredPlatformMonetizable = mrc::Platform::TikTok;
        auto emptyHits = reloaded.search(filter, 1000500);
        MRC_EXPECT(emptyHits.empty());
    }

    std::filesystem::remove(cacheFile, ec);
    ++g_passed;
}

void testUrlSafetyAttributionAndSha256() {
    MRC_EXPECT(mrc::isSafeBrowserUrl("https://ncs.io/usage-policy"));
    MRC_EXPECT(mrc::isSafeBrowserUrl("https://www.newgrounds.com/audio/listen/467339"));
    MRC_EXPECT(!mrc::isSafeBrowserUrl("javascript:alert(1)"));
    MRC_EXPECT(!mrc::isSafeBrowserUrl("file:///etc/passwd"));
    MRC_EXPECT(!mrc::isSafeBrowserUrl("https://admin:secret@ncs.io/"));
    MRC_EXPECT(!mrc::isSafeBrowserUrl("http://127.0.0.1:8000/admin"));
    MRC_EXPECT(!mrc::isSafeBrowserUrl("http://169.254.169.254/latest/meta-data"));

    MRC_EXPECT(mrc::isSafeBackendBaseUrl("https://api.musicrightschecker.org/api/v1"));
    MRC_EXPECT(mrc::isSafeBackendBaseUrl("http://127.0.0.1:8000/api/v1"));
    MRC_EXPECT(mrc::isSafeBackendBaseUrl("http://localhost:8000/api/v1"));
    MRC_EXPECT(!mrc::isSafeBackendBaseUrl("http://api.musicrightschecker.org/api/v1"));
    MRC_EXPECT(!mrc::isSafeBackendBaseUrl("https://10.0.0.5/api/v1"));

    // NIST FIPS 180-4 SHA-256 test vectors
    const std::span<const std::uint8_t> emptySpan{};
    MRC_EXPECT(mrc::sha256Hex(emptySpan) == "e3b0c44298fc1c149afbf4c8996fb92427ae41e4649b934ca495991b7852b855");

    const std::string abc = "abc";
    const std::span<const std::uint8_t> abcSpan(
        reinterpret_cast<const std::uint8_t*>(abc.data()),
        abc.size()
    );
    MRC_EXPECT(mrc::sha256Hex(abcSpan) == "ba7816bf8f01cfea414140de5dae2223b00361a396177a9cb410ff61f20015ad");

    // Restricted song must NOT fabricate a fake attribution block
    mrc::SongRightsRecord restrictedRec;
    restrictedRec.title = "Commercial Pop Hit";
    restrictedRec.artist = "Major Label";
    restrictedRec.overallStatus = mrc::OverallStatus::Restricted;
    MRC_EXPECT(mrc::formatAttributionForCopy(restrictedRec).empty());
    ++g_passed;
}

void testDirectStandaloneEvaluator() {
    // Word-boundary check: "Discovery" must NOT match "cover", while "(Nightcore Cover)" MUST match
    MRC_EXPECT(!mrc::DirectEvaluator::hasRemixOrMashupSignals("Discovery", "ElectroArtist"));
    MRC_EXPECT(mrc::DirectEvaluator::hasRemixOrMashupSignals("Faded (Nightcore Cover)", "Uploader"));

    // 1. NCS track with live #panel-copy2 HTML excerpt
    mrc::SongQuery ncsQuery;
    ncsQuery.sourceType = mrc::SourceType::NcsLibrary;
    ncsQuery.gdSongId = 10055555;
    ncsQuery.title = "Recall";
    ncsQuery.artist = "Jim Yosef";

    mrc::DirectFetchedPage ncsPage{
        .url = "https://ncs.io/Recall",
        .httpStatus = 200,
        .rawBody = R"(<html><script>evil()</script><body><p class="p-copy" id="panel-copy2">Song: Jim Yosef - Recall [NCS Release]<br>Music provided by NoCopyrightSounds<br>Free Download/Stream: http://ncs.io/Recall</p></body></html>)",
    };

    auto ncsRec = mrc::DirectEvaluator::evaluate(ncsQuery, ncsPage, "2026-11-03T12:00:00Z");
    MRC_EXPECT(ncsRec.overallStatus == mrc::OverallStatus::PermittedWithConditions);
    MRC_EXPECT(ncsRec.platforms.size() == 5);
    MRC_EXPECT(ncsRec.attributionTemplate.find("Jim Yosef - Recall [NCS Release]") != std::string::npos);
    MRC_EXPECT(ncsRec.attributionTemplate.find("evil()") == std::string::npos);

    // 2. Newgrounds CC BY-NC-ND must evaluate to Restricted / NotAllowed
    mrc::SongQuery ngQuery;
    ngQuery.sourceType = mrc::SourceType::Newgrounds;
    ngQuery.gdSongId = 467339;
    ngQuery.title = "At the Speed of Light";
    ngQuery.artist = "Dimrain47";

    mrc::DirectFetchedPage ngNcNdPage{
        .url = "https://www.newgrounds.com/audio/listen/467339",
        .httpStatus = 200,
        .rawBody = R"(<div class="licensing"><a href="https://creativecommons.org/licenses/by-nc-nd/3.0/">CC BY-NC-ND</a></div>)",
    };
    auto ngRestricted = mrc::DirectEvaluator::evaluate(ngQuery, ngNcNdPage, "2026-11-03T12:00:00Z");
    MRC_EXPECT(ngRestricted.overallStatus == mrc::OverallStatus::Restricted);
    MRC_EXPECT(ngRestricted.commercialUseStatus == mrc::PermissionLevel::NotAllowed);

    // 3. Newgrounds CC BY 4.0 must evaluate to PermittedWithConditions (or ReviewRequired if remix)
    mrc::DirectFetchedPage ngCcByPage{
        .url = "https://www.newgrounds.com/audio/listen/467339",
        .httpStatus = 200,
        .rawBody = R"(<a href="https://creativecommons.org/licenses/by/4.0/">Attribution 4.0</a>)",
    };
    auto ngPermitted = mrc::DirectEvaluator::evaluate(ngQuery, ngCcByPage, "2026-11-03T12:00:00Z");
    MRC_EXPECT(ngPermitted.overallStatus == mrc::OverallStatus::PermittedWithConditions);
    MRC_EXPECT(ngPermitted.attributionRequired);

    // 4. Newgrounds HTTP 403 block must conservatively fall back to Unknown (never Permitted)
    mrc::DirectFetchedPage ngBlockedPage{
        .url = "https://www.newgrounds.com/audio/listen/467339",
        .httpStatus = 403,
        .rawBody = "Forbidden",
    };
    auto ngUnknown = mrc::DirectEvaluator::evaluate(ngQuery, ngBlockedPage, "2026-11-03T12:00:00Z");
    MRC_EXPECT(ngUnknown.overallStatus == mrc::OverallStatus::Unknown);

    // 5. RobTop Official & Jukebox Hosted NONG
    mrc::SongQuery robQuery;
    robQuery.sourceType = mrc::SourceType::RobTopOfficial;
    robQuery.gdSongId = 14;
    robQuery.title = "Clubstep";
    robQuery.artist = "DJ-Nate";
    auto robRec = mrc::DirectEvaluator::evaluate(robQuery, std::nullopt, "2026-11-03T12:00:00Z");
    MRC_EXPECT(robRec.overallStatus == mrc::OverallStatus::ReviewRequired);
    MRC_EXPECT(robRec.platforms.size() == 5);

    mrc::SongQuery sfhQuery;
    sfhQuery.sourceType = mrc::SourceType::JukeboxHosted;
    sfhQuery.title = "Wash Over Me";
    sfhQuery.artist = "glaive";
    sfhQuery.sourceUrl = "https://api.songfilehub.com/song/abc?download=true";
    auto sfhRec = mrc::DirectEvaluator::evaluate(sfhQuery, std::nullopt, "2026-11-03T12:00:00Z");
    MRC_EXPECT(sfhRec.overallStatus == mrc::OverallStatus::Restricted);
    ++g_passed;
}

}  // namespace

int main() {
    testGdSongClassificationAndJukeboxIds();
    testJukeboxManifestParsing();
    testJsonParserSecurityAndUnicode();
    testApiCodecAndCacheLifecycle();
    testUrlSafetyAttributionAndSha256();
    testDirectStandaloneEvaluator();
    std::cout << "All " << g_passed << " C++23 Music Rights Checker core test suites passed.\n";
    return 0;
}
