# Changelog

All notable changes to **Music Rights Checker** are documented in this file.

The format is based on [Keep a Changelog](https://keepachangelog.com/en/1.1.0/),
and this project adheres to [Semantic Versioning](https://semver.org/spec/v2.0.0.html).

## [Unreleased]

### Planned
- Optional authenticated Newgrounds API / creator-verified OAuth evidence connector for songs whose public pages are blocked by anti-bot challenges on datacenter IPs.
- Geode v6 / Geometry Dash `2.2091` target verification once Geode v6 reaches stable release.

## [0.1.0] - 2026-10-27

### Added
- **Geode C++23 Mod (`mrc.music_rights_checker`)**:
  - Non-invasive `CustomSongWidget` music rights status badge (`MRC OK`, `MRC CR*`, `MRC NC`, `MRC !`, `MRC REV`, `MRC ?`) with configurable visibility in compact cells.
  - ABI-safe Jukebox (`fleym.nongd >=v3.8.0`) integration reading `<jukebox_save_dir>/manifest/<adjustedId>.json` asynchronously on Geode's worker pool (`default`, `locals`, `youtube`, and `hosted` / Song File Hub entries).
  - Non-invasive row inspection buttons (`MRC`) and catalogue shortcut inside Jukebox's `NongList` popup via public Geode Node IDs (`geode.node-ids >=v1.23.0`).
  - Detailed song rights popup (`RightsDetailPopup`) displaying song identity, original artist, rights holders, copyright ownership, licence, per-platform permissions (`YouTube`, `TikTok`, `Twitch`, `Instagram`, `Facebook`), supporting evidence, uncertainties, conflicting data, `last_verified_at` UTC timestamp, and mandatory non-legal-advice disclaimer.
  - One-click verified attribution copy (`Copy Credit`), safe public source page launcher (`Source Page`), live re-check (`Refresh`), and user inaccuracy reporting modal (`Report Issue`).
  - Searchable and filterable rights browser popup (`RightsBrowserPopup`) supporting commercial/monetization filtering and per-platform filtering across server and cached records.
  - Thread-safe persistent local disk cache (`rights_cache.json`) with TTL expiration and explicit `[CACHED / STALE RECORD]` / `MRC ...~` indicators when offline or expired.
- **Centralized Research Backend (`backend/`, API `v1`)**:
  - Versioned FastAPI service (`/api/v1/health`, `/api/v1/songs/resolve`, `/api/v1/songs`, `/api/v1/songs/{id}`, `/api/v1/songs/{id}/audit`, `/api/v1/jobs/{job_id}`, `/api/v1/reviews/reports`, and admin review/override/revalidate/metrics endpoints).
  - SQLAlchemy 2.1 async persistence with reversible Alembic schema migrations (`0001_initial_schema`) for `songs`, `platform_permissions`, `evidence_records`, `research_jobs`, `review_reports`, and `audit_logs`.
  - Canonical song deduplication, single-flight `asyncio.Lock` request coalescing, bounded background job queue, and configurable cache TTL + force-refresh cooldowns.
  - Evidence-first rights evaluator covering NCS releases, Creative Commons variants (`CC0`, `CC BY`, `CC BY-SA`, `CC BY-ND`, `CC BY-NC`, `CC BY-NC-SA`, `CC BY-NC-ND`), RobTop official tracks, Geometry Dash Music Library tracks, Jukebox hosted/YouTube/local NONGs, and remix/mashup/cover uncertainty detection.
  - Provider-agnostic AI research adapter (`openai`, `anthropic`, `gemini`, or `none`) with daily call budget caps and deterministic conservative legal guardrails.
- **CI/CD, Operations & Documentation**:
  - Multi-platform Geode `.geode` build workflow (`Win64`, `MacOS`, `iOS`, `Android64`, `Android32`), C++23 + ASan/UBSan and Python `pytest` CI workflow, and scheduled weekly upstream compatibility monitor.
  - Hardened non-root `Dockerfile`, `docker-compose.yml` (PostgreSQL 17), `scripts/check_upstream_compatibility.py`, and `scripts/backup_and_verify_db.py`.
  - Complete English architecture, compatibility, licensing research, ADR, deployment, backup/recovery, testing, and release management documentation.

### Security
- Enforced strict client/backend trust boundary: zero private AI or database credentials exist in the Geode mod.
- Local NONG audio files are never uploaded; optional SHA-256 local file digest sharing (`share-local-file-hash`) is disabled by default.
- Added untrusted HTML/metadata sanitization, prompt-injection detection (`<untrusted_evidence_json>` isolation and automatic LLM bypass when injection patterns appear), markdown link-injection escaping in `MDTextArea`, SSRF DNS/IP validation, sliding-window IP rate limiters, and constant-time admin token verification.
