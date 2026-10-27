# Known Issues, Limitations & Actionable Roadmap

_Last updated: 2026-10-27_

## 1. Current Known Issues & Operational Limitations

| ID | Area | Description | Current Mitigation |
|---|---|---|---|
| **KI-001** | Newgrounds Scraping (`HTTP 403`) | Direct unauthenticated HTTP fetches to `https://www.newgrounds.com/audio/listen/<id>` from automated/datacenter clients frequently receive Cloudflare `403 Forbidden`. | Backend records `fetch_status = "http_403"` and conservatively returns `unknown` (never guessing `safe`), while providing a direct clickable `Source Page` button in-game and supporting shared manual review overrides. |
| **KI-002** | Geometry Dash `2.2091` Compatibility | RobTop's mid-October 2026 `2.2091` update broke Geode v5 binary bindings across platforms; Geode v6 and Jukebox updates for `2.2091` are not yet released as stable. | Mod targets verified GD `2.2081` + Geode `5.10.1+`; weekly `upstream-compatibility-monitor.yml` tracks Geode v6 tags so migration can occur safely once upstream stabilizes. |
| **KI-003** | Local Geode Cross-Compilation in Headless Linux Containers | Building the final linked `.geode` binary requires platform SDKs (`Win64` MSVC/Clang-CL, macOS Xcode SDK, Android NDK) managed by Geode CLI / GitHub Actions. | Core C++23 logic (`include/mrc/`, `src/core/`) is decoupled and verified locally with `g++ -std=c++23 -Werror` + ASan/UBSan, while `.github/workflows/build-geode-mod.yml` builds all 5 `.geode` platform binaries on GitHub Actions. |
| **KI-004** | Third-Party False Content ID Claims | Even on verified `CC BY` or `NCS` tracks, fraudulent third-party distributors occasionally register audio in YouTube Content ID or Meta Rights Manager. | Every record and UI popup explicitly separates licence permission from `automated_claim_risk` and never guarantees zero automated claims. |

---

## 2. Prioritized Actionable Engineering Backlog

### Priority P1 — Pre-Public-Index Launch
- [ ] **P1-1: Push Repository & Run Multi-OS `.geode` Build Workflow**: Push `main` to GitHub and verify `.github/workflows/build-geode-mod.yml` produces the combined `mrc.music_rights_checker.geode` artifact across `Win64`, `MacOS`, `iOS`, `Android64`, and `Android32`.
- [ ] **P1-2: In-Game Smoke Test on GD `2.2081` + Jukebox `v3.8.0`**: Load the built `.geode` artifact in Geometry Dash `2.2081` alongside `fleym.nongd v3.8.0`, verify badge positioning on standard and NCS levels, and inspect Jukebox `NongList` rows.
- [ ] **P1-3: Seed Curated Initial Catalogue**: Populate initial `manual_review` records (with primary artist FAQ/licence URLs) for the 22 official RobTop tracks and top 50 most-used Geometry Dash Music Library artists (MDK, Waterflame, Camellia, F-777, DJVI, Kevin MacLeod, Shirobon).

### Priority P2 — Near-Term Enhancements
- [ ] **P2-1: Official/Partner Evidence Connectors**: Investigate Newgrounds API / artist-verified feeds and YouTube Data API v3 metadata lookups to reduce `http_403` unverified fallbacks without scraping blocked pages.
- [ ] **P2-2: Song File Hub Index Importer**: Add an optional scheduled backend job to cross-reference public `FlafyDev/auto-nong-indexes` JSON manifests so popular NONGs have pre-computed canonical entries and mashup warnings.
- [ ] **P2-3: Web Reviewer Console**: Add a minimal read/write admin HTML view (or CLI helper script) wrapping `/api/v1/admin/reviews` and `/api/v1/admin/songs/{id}/override` for faster community report triage.

### Priority P3 — Upcoming Ecosystem Transitions
- [ ] **P3-1: Geode v6 / Geometry Dash `2.2091` Migration Branch**: Once `geode-sdk/geode` tags stable `v6.0.0` and `Fleeym/jukebox` publishes a `2.2091`-compatible release, validate `CustomSongWidget` and `SongInfoObject` bindings on a dedicated `compat/gd-2.2091` branch before bumping `mod.json`.
