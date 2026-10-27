# Contributing to Music Rights Checker

Thank you for helping improve **Music Rights Checker** for Geometry Dash creators, streamers, and mod users.

## Core Engineering Rules

1. **English-Only Repository**: All code comments, commit messages, pull requests, UI strings, changelogs, and documentation must be written in clear English.
2. **Evidence Over Guessing**: Never hardcode or assume that a song, artist, or music library is "safe to monetize" across all platforms without verifiable licence evidence. Uncertain, missing, or conflicting evidence must resolve to `unknown` or `review_required`.
3. **Strict Trust Boundary**: Never add API keys, admin tokens, or direct LLM provider calls to the Geode C++ client. All privileged research, rate limiting, and secret management belong strictly in `backend/`.
4. **Respect User Privacy**: Never upload local audio files from a user's machine or mobile device unless an explicit, documented, opt-in mechanism is designed and approved.
5. **Non-Invasive Modding**: Do not link against unexported symbols of other Geode mods (such as Jukebox's internal PIMPL classes), and never block Geometry Dash's main thread with synchronous network or disk I/O.
6. **Verified Upstream Compatibility**: Do not bump Geode SDK, Geometry Dash, Jukebox, or Python dependency versions blindly. Update `docs/COMPATIBILITY.md` and run `scripts/check_upstream_compatibility.py` before proposing dependency changes.

## Local Development Workflow

### Running Backend Checks

```bash
python3 -m venv .venv
source .venv/bin/activate
pip install -r backend/requirements.txt

# Verify reversible database migrations
cd backend
MRC_DATABASE_URL="sqlite+aiosqlite:////tmp/dev_check.db" PYTHONPATH=.. alembic upgrade head
MRC_DATABASE_URL="sqlite+aiosqlite:////tmp/dev_check.db" PYTHONPATH=.. alembic downgrade base
MRC_DATABASE_URL="sqlite+aiosqlite:////tmp/dev_check.db" PYTHONPATH=.. alembic upgrade head
cd ..

# Execute test suite
PYTHONPATH=. pytest backend/tests -v
```

### Running C++23 Core & Manifest Checks

Portable core logic under `include/mrc/` and `src/core/` must remain free of Cocos2d/Windows-only headers so it can be verified standalone on any C++23 compiler with sanitizers enabled:

```bash
g++ -std=c++23 -Wall -Wextra -Wpedantic -Werror -O1 -g \
  -fsanitize=address,undefined -fno-omit-frame-pointer \
  -Iinclude \
  src/core/Types.cpp \
  src/core/JsonCodec.cpp \
  src/core/LocalCache.cpp \
  src/core/AttributionAndSafety.cpp \
  tests/cpp/test_mrc_core.cpp \
  -o /tmp/mrc_core_tests_asan && /tmp/mrc_core_tests_asan

python3 scripts/check_upstream_compatibility.py --offline-manifest-check
```

## Submitting Changes

1. Create a focused feature or bugfix branch from `main`.
2. Add or update unit/regression tests covering every behavioural change or bug fix.
3. Update `CHANGELOG.md` under `## [Unreleased]` following Keep a Changelog categories (`Added`, `Changed`, `Deprecated`, `Removed`, `Fixed`, `Security`).
4. Ensure all tests and manifest checks pass with zero warnings before opening a pull request.
