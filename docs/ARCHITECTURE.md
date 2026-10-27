# System Architecture — Music Rights Checker

## 1. Architectural Overview

Music Rights Checker is split across two strictly separated runtime components:

1. **Untrusted Public Client (`mrc.music_rights_checker` Geode Mod, C++23)**:
   - Runs inside Geometry Dash (`2.2081`) on Geode SDK (`>=5.10.1`).
   - Inspects `CustomSongWidget` and Jukebox (`fleym.nongd >=v3.8.0`) JSON manifests on Geode's background worker pool.
   - Communicates exclusively over HTTPS (or `http://127.0.0.1` in local development) with the centralized v1 API using `geode::utils::web::WebRequest` and `geode::async::TaskHolder`.
   - Maintains a thread-safe local cache (`<mod_save_dir>/rights_cache.json`) for instant repeat lookups and offline access, visibly flagging stale or offline-served records.
2. **Trusted Centralized Service (`backend/`, Python 3.13 + FastAPI + SQLAlchemy 2.1)**:
   - Owns canonical song deduplication, shared licence records, outbound evidence fetching, optional AI-assisted research synthesis, rate limiting, daily AI cost budgets, manual review overrides, and append-only audit logs.
   - Stores durable state in PostgreSQL 17 (production) or SQLite (local development and CI) managed via reversible Alembic migrations.

```text
+-------------------------------------------------------------------+
| Geometry Dash 2.2081 + Geode SDK 5.10.1+                          |
|                                                                   |
|  CustomSongWidget / Jukebox NongList                              |
|         |                                                         |
|         v (async background manifest read, zero ABI coupling)     |
|  JukeboxIntegration  --->  mrc::LocalRightsCache (rights_cache)   |
|         |                                                         |
|         v (non-blocking TaskHolder<WebResponse>)                  |
|  BackendClient  ----------------------------------------+         |
+---------------------------------------------------------|---------+
                                                          | HTTPS JSON (/api/v1)
+---------------------------------------------------------v---------+
| Centralized FastAPI Backend (backend/)                            |
|                                                                   |
|  IP SlidingWindowRateLimiter  ->  /api/v1/songs/resolve           |
|                                         |                         |
|                                         v                         |
|  RightsService (Canonical Key + Single-Flight asyncio.Lock)       |
|      |                     |                       |              |
|      v                     v                       v              |
|  PostgreSQL / SQLite   Safe Evidence           Configurable AI    |
|  (songs, platforms,    Collectors (SSRF &      Adapter (Daily     |
|   evidence, jobs,      HTML Sanitizer)         Budget + Guardrail)|
|   reviews, audits)                                                |
+-------------------------------------------------------------------+
```

---

## 2. Canonical Song Deduplication

Because thousands of players open the same rated levels and Jukebox NONG indexes, `backend/app/research/identity.py` and `src/core/Types.cpp` compute deterministic canonical keys:

| Source Type | Canonical Key Pattern | Example |
|---|---|---|
| `robtop_official` | `robtop:<gd_song_id>` | `robtop:14` |
| `gd_music_library` | `gdml:<gd_song_id>` | `gdml:10004512` |
| `ncs_library` (in-game) | `ncs:gd:<gd_song_id>` | `ncs:gd:10012345` |
| `ncs_library` (catalogue) | `ncs:meta:<artist_slug>:<title_slug>` | `ncs:meta:warriyo:mortals` |
| `newgrounds` | `ng:<gd_song_id>` | `ng:467339` |
| `jukebox_youtube` | `jb_yt:<youtube_11char_id>` | `jb_yt:tWh3dG3H35A` |
| `jukebox_hosted` | `jb_url:<normalized_url>` | `jb_url:https://api.songfilehub.com/song/abc` |
| `jukebox_local` (opt-in hash) | `jb_local_sha256:<64hex>` | `jb_local_sha256:ba7816...` |
| `jukebox_local` (metadata only) | `jukebox_local:meta:<artist>:<title>` | `jukebox_local:meta:eden:xo` |

When 50 concurrent clients request an uncached song simultaneously, `RightsService` acquires a per-`canonical_key` `asyncio.Lock` inside a bounded LRU lock registry, executes external evidence collection once, persists the shared `SongRecord`, and serves the stored result to all waiting callers.

---

## 3. Database Schema (`0001_initial_schema`)

1. **`songs`**: Canonical deduplicated track record (`id`, `canonical_key`, `source_type`, `gd_song_id`, `title`, `artist`, `original_artist`, `rights_holders_json`, `original_source_url`, `license_name`, `license_url`, `copyright_ownership_status`, `overall_status`, `commercial_use_status`, `attribution_required`, `attribution_template`, `uncertainties_json`, `conflicting_information_json`, `verification_method`, `reviewer_notes`, `last_verified_at`, `expires_at`, `refresh_Lock_until`).
2. **`platform_permissions`**: Normalized per-platform row (`song_id`, `platform` unique constraint across `youtube`, `tiktok`, `twitch`, `instagram`, `facebook`) storing `video_upload`, `livestream`, `commercial_use`, `monetization`, `attribution_required`, `automated_claim_risk`, `conditions_json`, `restrictions_json`, `policy_reference_url`.
3. **`evidence_records`**: Verifiable snapshots of retrieved licence pages, metadata inputs, and security checks (`source_kind`, `source_title`, `source_url`, `fetch_status`, `excerpt`, `content_sha256`, `retrieved_at`).
4. **`research_jobs`**: Durable background evaluation queue (`status`: `queued` -> `running` -> `completed`/`failed`, `canonical_key`, `request_payload_json`, `song_id`, `error_message`).
5. **`review_reports`**: User-submitted inaccuracy/dispute reports (`song_id`, `status`: `open`/`resolved`/`dismissed`, `reason`, `details`, `suggested_license`, `evidence_url`, `resolution_notes`).
6. **`audit_logs`**: Append-only history of automated evaluations, manual overrides, and forced revalidations (`song_id`, `actor`, `action`, `summary`, `previous_snapshot_json`, `new_snapshot_json`, `created_at`).

---

## 4. Asynchronous Request & Polling Lifecycle

1. Mod checks `LocalRightsCache` using `computeClientCacheKey(query)`.
   - If present and `nowUnix < localExpiresAtUnix` and `!forceRefresh`, UI renders immediately (`0` network calls).
2. Otherwise, mod spawns a coroutine inside `TaskHolder<ClientResolveOutcome>` calling `POST /api/v1/songs/resolve` (`async_mode: true`).
3. If the backend already has a fresh shared record, it responds `200 OK` (`resolution_status: "ready"`).
4. If live research is needed, the backend enqueues a bounded `ResearchJobRecord`, responds `202 Accepted` (`resolution_status: "processing"`, `job_id`), and processes evidence + evaluation in the background worker pool.
5. The mod coroutine polls `GET /api/v1/jobs/{job_id}` using non-blocking `arc::sleep` backoff (`0.9s -> 1.8s -> 3.6s -> 4.0s`) and updates the badge/popup on the main thread when complete—automatically aborting if the player closes the menu first.
6. Manual reviewer overrides (`verification_method = "manual_review"`) are protected against accidental overwrite by public `force_refresh` calls; only authorized admins (`POST /api/v1/admin/songs/{id}/revalidate` or `/override`) can supersede them.
