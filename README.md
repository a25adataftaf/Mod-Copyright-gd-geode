# Music Rights Checker

**Music Rights Checker** (`mrc.music_rights_checker`) is an open-source Geometry Dash mod for [Geode SDK](https://geode-sdk.org/) integrated with [Jukebox (`fleym.nongd`)](https://github.com/Fleeym/jukebox) and backed by a centralized, evidence-driven music rights research service.

It helps Geometry Dash players, level creators, livestreamers, and video editors evaluate whether a track can be uploaded, streamed, used commercially, or monetized across **YouTube**, **TikTok**, **Twitch**, **Instagram**, and **Facebook**.

> **Important Legal & Enforcement Notice**
>
> Music Rights Checker provides **evidence-based informational guidance, not legal advice**. Copyright ownership, creator usage licences, commercial ad monetization rights, platform music policies, and automated copyright enforcement systems (YouTube Content ID, Twitch Audible Magic VOD muting, TikTok audio muting, and Meta Rights Manager) are distinct mechanisms. A permissive creator licence does **not** guarantee the absence of automated claims, and the absence of an automated claim **never** proves legal permission to use or monetize a song. When evidence is missing, ambiguous, or contradictory, Music Rights Checker conservatively reports `unknown` or `review_required` rather than `safe`.

---

## Core Capabilities

### Geode Mod (`include/mrc/`, `src/`)
- **Non-Invasive In-Game Badge**: Adds a compact rights status button (`MRC OK`, `MRC CR*`, `MRC NC`, `MRC !`, `MRC REV`, `MRC ?`) to `CustomSongWidget` without overlapping Geometry Dash controls or Jukebox's NONG pin.
- **ABI-Safe Jukebox Integration**: Automatically detects active NONG replacements (`locals`, `youtube`, and `hosted` tracks such as Song File Hub) by reading Jukebox `v3.8.0+` JSON manifests on Geode's background worker pool and attaching inspection controls inside Jukebox's `NongList` popup via public Node IDs.
- **Supported Song Sources**:
  - Official RobTop Geometry Dash tracks (`robtop_official`)
  - Geometry Dash Music Library tracks (`gd_music_library`)
  - NoCopyrightSounds (NCS) in-game and web catalogue releases (`ncs_library`)
  - Newgrounds Audio Portal songs (`newgrounds`)
  - Jukebox hosted NONG tracks, including Song File Hub (`jukebox_hosted`)
  - Jukebox YouTube NONG tracks (`jukebox_youtube`)
  - Jukebox locally imported audio files (`jukebox_local`)
- **Detailed Per-Platform Breakdown**: Evaluates **YouTube**, **TikTok**, **Twitch**, **Instagram**, and **Facebook** independently across video upload, livestreaming, commercial use, monetization, attribution requirements, and automated claim risk.
- **One-Click Attribution Copy**: Copies verified credit blocks (such as official NCS attribution or Creative Commons TASL credits) directly to the clipboard, while refusing to fabricate fake credits for restricted or unverified tracks.
- **Offline Cache & Built-In Standalone Direct Inspector**: Works out of the box even with no backend server configured by inspecting public Newgrounds/NCS licence pages and platform policy rules directly in C++23 (`DirectEvaluator`), persisting results in `<mod_save_dir>/rights_cache.json` with `last_verified_at` UTC timestamps and `[CACHED / STALE RECORD]` indicators, and automatically upgrading to a centralized `/api/v1` backend whenever `Backend API URL` is set.
- **Search, Filter & Dispute Reporting**: Includes an in-game catalogue browser (`Browse & Filter Song Rights`) to filter tracks by commercial monetization and target platform, plus an in-game modal to report inaccurate licence classifications.
- **Privacy by Default**: Never embeds private AI credentials in the client binary and **never uploads local audio files**. An optional, off-by-default setting (`share-local-file-hash`) allows sending only a local SHA-256 digest to match shared community records.

### Centralized Research Backend (`backend/`)
- **Versioned HTTP API (`/api/v1`)**: Built with Python 3.13, FastAPI, SQLAlchemy 2.1 Async, and Alembic migrations (PostgreSQL 17 in production; SQLite in local development and CI).
- **Canonical Song Deduplication & Single-Flight Locking**: Shares verified licence evaluations across all mod users and coalesces concurrent requests for the same song so external pages and AI budgets are queried once.
- **Evidence-First Research Engine**: Combines authoritative source parsers (e.g., NCS release pages and usage policy, Creative Commons licence URLs, remix/mashup heuristics) with an optional provider-agnostic AI research adapter (`openai`, `anthropic`, `gemini`, or `none`) bounded by strict daily call budgets and conservative legal guardrails.
- **Security Hardening**: Sanitizes all external HTML/metadata, detects prompt-injection attempts inside untrusted song descriptions, isolates evidence inside `<untrusted_evidence_json>` blocks, blocks SSRF against private/loopback networks, enforces sliding-window IP rate limits, and protects manual review overrides with constant-time admin token verification and append-only audit logs.

---

## Verified Compatibility Baseline (2026-10-27)

| Component | Target / Constraint | Status | Notes |
|---|---|---|---|
| **Geometry Dash** | `2.2081` (Win64, macOS, Android32/64, iOS) | Verified target | GD `2.2091` (Oct 2026) awaits upstream Geode v6 support; do not bump blindly |
| **Geode SDK** | `5.10.1` (`mod.json`), compatible through `5.11.2` | Verified target | Uses Geode v5 `Popup`, `TaskHolder`, `WebRequest`, and `arc` coroutines |
| **Jukebox (`fleym.nongd`)** | `>=v3.8.0` (optional dependency) | Verified schema & Node IDs | Integrates via manifest JSON v1 and public `NongList` Node IDs |
| **Node IDs (`geode.node-ids`)** | `>=v1.23.0` | Required dependency | Provides stable UI node hooks |
| **C++ Standard** | C++23 | Verified (`g++ 14.2.0`) | Core library compiles standalone with `-Wall -Wextra -Wpedantic -Werror` |
| **Backend Runtime** | Python `3.13` + FastAPI `0.162.0` + SQLAlchemy `2.1.6` | Verified (`pytest` 13/13) | PostgreSQL 17 recommended for production |

See [`docs/COMPATIBILITY.md`](docs/COMPATIBILITY.md) for the full compatibility matrix and upstream monitoring workflow.

---

## Repository Structure

```text
.
├── mod.json                        # Geode v5 mod manifest, dependencies, and user settings
├── CMakeLists.txt                  # Geode multi-platform build + standalone C++23 core test mode
├── about.md                        # In-game Geode mod description
├── logo.png                        # 256x256 Geode mod icon
├── include/mrc/                    # Portable C++23 core headers (Types, JsonCodec, LocalCache, Safety)
├── src/
│   ├── core/                       # Platform-independent C++23 core implementation
│   ├── jukebox/                    # Asynchronous Jukebox v3.8.0+ manifest & widget integration
│   ├── net/                        # Non-blocking Geode WebRequest client & job poller
│   ├── ui/                         # Geode v5 detail popup, report modal, and search/filter browser
│   ├── hooks/                      # CustomSongWidget badge hook
│   └── main.cpp                    # Mod lifecycle & Jukebox NongList row integration
├── tests/cpp/                      # Standalone C++23 unit & regression test suite
├── backend/                        # Centralized FastAPI + SQLAlchemy 2.1 + Alembic service
│   ├── app/                        # API v1 routes, DB models, security, research engine, services
│   ├── alembic/                    # Reversible database schema migrations
│   ├── tests/                      # Automated pytest unit, integration, security & resilience suite
│   ├── Dockerfile                  # Non-root production container image
│   └── requirements.txt            # Exact verified pinned Python dependencies
├── scripts/                        # Upstream compatibility checker & DB backup/restore verifier
├── docs/                           # Architecture, research notes, ADRs, operations, recovery, release docs
└── .github/workflows/              # Geode multi-OS build, C++/Python CI, and weekly upstream monitor
```

---

## Quickstart

### 1. Run Backend Locally

```bash
python3 -m venv .venv
source .venv/bin/activate
pip install -r backend/requirements.txt

cp backend/.env.example backend/.env
# Edit backend/.env to set a strong MRC_ADMIN_API_TOKEN if enabling admin review endpoints

cd backend
PYTHONPATH=.. alembic upgrade head
PYTHONPATH=.. uvicorn app.main:app --host 127.0.0.1 --port 8000
```

Verify service health:

```bash
curl -s http://127.0.0.1:8000/api/v1/health
```

### 2. Run Automated Tests

```bash
# Run Python backend unit, integration, concurrency, and security tests
PYTHONPATH=. .venv/bin/pytest backend/tests -v

# Compile and run C++23 mod core & Jukebox manifest tests
g++ -std=c++23 -Wall -Wextra -Wpedantic -Werror -O2 \
  -Iinclude \
  src/core/Types.cpp \
  src/core/JsonCodec.cpp \
  src/core/LocalCache.cpp \
  src/core/AttributionAndSafety.cpp \
  tests/cpp/test_mrc_core.cpp \
  -o /tmp/mrc_core_tests && /tmp/mrc_core_tests
```

### 3. Build the Geode `.geode` Package

Install [Geode CLI](https://docs.geode-sdk.org/getting-started/cpp-stuff/) and Geode SDK `>= 5.10.1`, set `GEODE_SDK`, and run:

```bash
geode build
```

Point the mod's **Backend API URL** setting to your deployed HTTPS endpoint (or `http://127.0.0.1:8000/api/v1` during local development).

---

## Documentation Index

- [Architecture & Data Model](docs/ARCHITECTURE.md)
- [Compatibility Matrix & Upstream Policy](docs/COMPATIBILITY.md)
- [Music Licensing & Platform Policy Research](docs/RESEARCH_NOTES.md)
- [Architectural Decision Log (ADRs)](docs/DECISION_LOG.md)
- [Deployment, Cost Controls & Operations](docs/DEPLOYMENT_AND_OPERATIONS.md)
- [Backup, Restore & Disaster Recovery](docs/BACKUP_AND_RECOVERY.md)
- [Testing & Verification Guide](docs/TESTING_AND_VERIFICATION.md)
- [Release Management & Checklists](docs/RELEASE_MANAGEMENT.md)
- [Known Issues & Actionable Roadmap](docs/KNOWN_ISSUES_AND_ROADMAP.md)
- [Contributing Guidelines](CONTRIBUTING.md)
- [Security Policy](SECURITY.md)
- [Changelog](CHANGELOG.md)

## License

Released under the [MIT License](LICENSE).
