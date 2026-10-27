# Release Management, Checklists & Draft Release Notes

## 1. Versioning Policy

Music Rights Checker follows **Semantic Versioning 2.0.0 (`MAJOR.MINOR.PATCH`)**:
- **MAJOR (`X.0.0`)**: Breaking `/api/v1` contract changes, incompatible local cache schema changes without automatic fallback, or major Geode SDK ABI transitions (e.g., Geode v5 -> Geode v6).
- **MINOR (`0.X.0`)**: New supported music sources, UI views, backwards-compatible API fields, or verified Geometry Dash / Jukebox minor target updates.
- **PATCH (`0.0.X`)**: Bug fixes, licence parser refinements, security hardening, and documentation updates.

**Standing Rule**: Never publish a public GitHub release, Geode Index PR, or production deployment without completing the Pre-Release Checklist and obtaining explicit maintainer authorization.

---

## 2. Pre-Release Checklist

- [ ] `mod.json` `"version"`, `CMakeLists.txt` `project(... VERSION ...)`, `backend/app/__init__.py` `__version__`, and `backend/pyproject.toml` `version` match the target SemVer version.
- [ ] `CHANGELOG.md` has a dated section `## [X.Y.Z] - YYYY-MM-DD` summarizing all user-visible changes, compatibility notes, security fixes, and known limitations in English.
- [ ] `python3 scripts/check_upstream_compatibility.py --live` passes and confirms Geode SDK, Geometry Dash, Jukebox, and Node IDs constraints are accurate.
- [ ] C++23 core tests pass under both `-O2 -Werror` and `-fsanitize=address,undefined`.
- [ ] `pytest backend/tests -v` passes with zero failures and zero unhandled warnings.
- [ ] Alembic migrations pass `upgrade head` -> `downgrade base` -> `upgrade head` cleanly, and `scripts/backup_and_verify_db.py` passes restore verification.
- [ ] `.github/workflows/build-geode-mod.yml` succeeds on all 5 targets (`Win64`, `MacOS`, `iOS`, `Android64`, `Android32`) and produces a valid `.geode` artifact.
- [ ] Manual smoke verification completed in Geometry Dash `2.2081` with Jukebox `v3.8.0+`:
  - Official RobTop track, GD Music Library track, NCS track, Newgrounds track, and active Jukebox NONG (`hosted` / `youtube` / `local`) resolve without UI freeze.
  - Offline mode displays cached status with `[CACHED / STALE RECORD]` notice rather than crashing.
- [ ] `git status` is clean and zero secrets or `.env` files are tracked.

---

## 3. Post-Release Verification & Rollback Plan

### Post-Release Checks
1. Verify backend `GET /api/v1/health` returns `200 OK` and expected `version`.
2. Download the published `.geode` artifact into a clean Geometry Dash `2.2081` + Geode `5.10.1+` profile and resolve one NCS track and one Jukebox NONG track end-to-end.
3. Monitor backend error rates, rate-limit `429` counters, and `MRC_AI_DAILY_BUDGET_CALLS` consumption over the first 24 hours via `/api/v1/admin/metrics`.

### Rollback Plan
- **Mod Regression**: Mark the GitHub Release as pre-release/withdrawn, point users to the previous stable `.geode` tag, and ship a `PATCH` hotfix rather than mutating historical tags.
- **Backend Regression**: Revert container deployment to the prior image tag; if a schema migration must be reverted, run `scripts/backup_and_verify_db.py` first and execute `alembic downgrade -1`.

---

## 4. Prepared Release Notes — `v0.1.0` (**UNPUBLISHED DRAFT**)

> *Status: Prepared draft only. Not published externally pending explicit user authorization and multi-OS GitHub Actions `.geode` binary verification.*

### Music Rights Checker `v0.1.0` (Initial Public Preview)

**Music Rights Checker** brings evidence-based music copyright, creator monetization, and platform-policy inspection to Geometry Dash (`2.2081`) with native support for **Jukebox (`fleym.nongd >=v3.8.0`)**.

#### Highlights
- **In-Game Rights Badges & Detailed Breakdown**: Inspect copyright ownership, licence terms, attribution rules, and separate permissions across **YouTube**, **TikTok**, **Twitch**, **Instagram**, and **Facebook** directly from `CustomSongWidget` and Jukebox's NONG list.
- **Full Source Coverage**: Supports RobTop official tracks, Geometry Dash Music Library, NCS releases, Newgrounds Audio Portal songs, and Jukebox local/YouTube/hosted (Song File Hub) NONG replacements.
- **Copy-Ready Attribution & Offline Cache**: One-click clipboard attribution for eligible tracks, plus local disk caching with explicit staleness timestamps for offline play.
- **Centralized & Privacy-Respecting Backend**: Shared deduplicated song database (`/api/v1`) with zero client-embedded secrets and zero uploading of local audio files.

#### Compatibility
- **Geometry Dash**: `2.2081` (`Win64`, `macOS`, `iOS`, `Android64`, `Android32`)
- **Geode SDK**: `>=5.10.1` (tested compatible with `5.10.1`–`5.11.2`)
- **Jukebox**: `>=v3.8.0` (optional; recommended for NONG detection)

#### Known Limitations
- Unauthenticated datacenter requests to `newgrounds.com` may encounter Cloudflare `403` challenges; blocked tracks conservatively report `unknown` until verified via source link or manual review.
- Geometry Dash `2.2091` is not yet targeted while upstream Geode v6 is in development.
- Informational guidance only, not legal advice; automated Content ID / Audible Magic claims remain possible on all platforms.
