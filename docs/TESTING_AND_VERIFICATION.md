# Testing & Verification Guide

Music Rights Checker maintains automated tests across both the **C++23 Geode Mod Core** and the **Python 3.13 FastAPI Backend**, plus manifest consistency and backup rehearsal drills.

---

## 1. C++23 Mod Core & Sanitizer Suite (`tests/cpp/test_mrc_core.cpp`)

### What Is Tested
- Official RobTop negative song ID mapping (`adjustJukeboxSongId`) and Geometry Dash `2.2081` source classification (`robtop_official`, `ncs_library`, `gd_music_library`, `newgrounds`).
- Jukebox `v3.8.0` manifest JSON parsing (`default`, `locals`, `youtube`, `hosted` / Song File Hub active replacement resolution, full song list extraction, and orphaned `active` fallback).
- RFC 8259 JSON parser edge cases and security limits (UTF-16 surrogate pairs, escaped strings, rejection of nesting depth `> 32`, rejection of unescaped control characters).
- `/api/v1/songs/resolve` serialization & deserialization roundtrip.
- `LocalRightsCache` atomic disk persistence, TTL expiration, mandatory staleness badge transition (`MRC CR*` -> `MRC CR~` when expired), and platform/monetization search filtering.
- Browser & backend URL SSRF/scheme validation (`isSafeBrowserUrl`, `isSafeBackendBaseUrl`), NIST FIPS 180-4 SHA-256 vectors (`sha256Hex`), and refusal to fabricate attribution for restricted songs.

### Commands
```bash
# Release build with strict warnings-as-errors
g++ -std=c++23 -Wall -Wextra -Wpedantic -Werror -O2 \
  -Iinclude \
  src/core/Types.cpp \
  src/core/JsonCodec.cpp \
  src/core/LocalCache.cpp \
  src/core/AttributionAndSafety.cpp \
  tests/cpp/test_mrc_core.cpp \
  -o /tmp/mrc_core_tests && /tmp/mrc_core_tests

# AddressSanitizer + UndefinedBehaviorSanitizer build
g++ -std=c++23 -Wall -Wextra -Wpedantic -Werror -O1 -g \
  -fsanitize=address,undefined -fno-omit-frame-pointer \
  -Iinclude \
  src/core/Types.cpp \
  src/core/JsonCodec.cpp \
  src/core/LocalCache.cpp \
  src/core/AttributionAndSafety.cpp \
  tests/cpp/test_mrc_core.cpp \
  -o /tmp/mrc_core_tests_asan && /tmp/mrc_core_tests_asan
```

---

## 2. Python FastAPI Backend, Database & Security Suite (`backend/tests/`)

### What Is Tested (`13` test cases across `2` test modules)
- **`test_rights_backend.py`**:
  1. `/api/v1/health` operational check.
  2. NCS song evaluation: verifies conditional monetization for independent creators, attribution copy generation, and separate platform records across YouTube, TikTok, Twitch, Instagram, and Facebook.
  3. Creative Commons `CC BY-NC-ND` evaluation: verifies non-commercial + no-derivatives guardrails force `restricted` / `not_allowed` for video synchronization and monetization.
  4. Unverified Jukebox YouTube NONG + mashup/remix detection: verifies conservative `unknown` / `review_required` fallback and high claim risk.
  5. Prompt-injection resistance: verifies adversarial instructions inside untrusted song metadata cannot trick the evaluator into returning `permitted`.
  6. SSRF protection: blocks `127.0.0.1`, `169.254.169.254`, `192.168.x.x`, non-HTTP schemes, and embedded URL credentials.
  7. Async background job lifecycle (`202 Accepted` -> `/api/v1/jobs/{job_id}`) and `10`-way concurrent single-flight deduplication (`external_fetch_count == 1`).
  8. Search/filtering (`/api/v1/songs`), user inaccuracy report submission (`/api/v1/reviews/reports`), admin authentication enforcement (`401`), manual review override (`/api/v1/admin/songs/{id}/override`), protection of manual overrides against public `force_refresh`, and append-only audit trail (`/api/v1/songs/{id}/audit`).
  9. Per-IP sliding-window rate limiting (`429 Too Many Requests` + `Retry-After`).
- **`test_resilience_and_providers.py`**:
  10. Provider-agnostic AI adapter markdown-fenced JSON parsing, daily budget exhaustion fallback, HTTP timeout resilience, malformed JSON handling, and automatic LLM bypass when security flags are raised.
  11. Fail-closed (`503`) admin security when `MRC_ADMIN_API_TOKEN` is unconfigured.
  12. Simulated database outage on `/api/v1/health` (`503` degraded status without leaking DSNs or stack traces).
  13. Admin forced revalidation (`POST /api/v1/admin/songs/{id}/revalidate`) and canonical metadata normalization.

### Commands
```bash
# Reversible Alembic schema migration check
cd backend
MRC_DATABASE_URL="sqlite+aiosqlite:////tmp/alembic_verify.db" PYTHONPATH=.. ../.venv/bin/alembic upgrade head
MRC_DATABASE_URL="sqlite+aiosqlite:////tmp/alembic_verify.db" PYTHONPATH=.. ../.venv/bin/alembic downgrade base
MRC_DATABASE_URL="sqlite+aiosqlite:////tmp/alembic_verify.db" PYTHONPATH=.. ../.venv/bin/alembic upgrade head
cd ..

# Full pytest run
PYTHONPATH=. .venv/bin/pytest backend/tests -v
```

---

## 3. Operational & Compatibility Verification Scripts

```bash
# Offline manifest & changelog SemVer check
python3 scripts/check_upstream_compatibility.py --offline-manifest-check

# Live upstream Geode / Jukebox / NodeIDs tag compatibility check
python3 scripts/check_upstream_compatibility.py --live

# Live SQLite online backup + automated restore integrity rehearsal
python3 scripts/backup_and_verify_db.py --sqlite-path /tmp/alembic_verify.db --output-dir /tmp/mrc_backups
```
