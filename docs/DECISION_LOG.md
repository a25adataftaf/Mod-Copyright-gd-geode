# Architectural Decision Log (ADR)

This log documents consequential engineering, security, and legal-modelling decisions made for **Music Rights Checker**.

---

## ADR-001: Repository Root Geode Mod + `backend/` Service Monorepo Layout
- **Date**: 2026-10-27
- **Status**: Accepted
- **Context**: Geode CLI (`geode build`), `geode-sdk/build-geode-mod@main`, and the official Geode Mods Index expect `mod.json`, `CMakeLists.txt`, `about.md`, `CHANGELOG.md`, and `logo.png` at the repository root. Meanwhile, the centralized research backend requires an isolated Python package, Alembic migrations, Dockerfile, and test suite.
- **Decision**: Place the Geode C++23 mod at the repository root (`include/mrc/`, `src/`, `mod.json`, `CMakeLists.txt`) and isolate the centralized API service under `backend/`.
- **Rejected Alternatives**:
  - *Separate `client/` subdirectory for `mod.json`*: Complicates Geode Index submission and standard Geode developer tooling.
  - *Embedding AI research inside the C++ mod*: Rejected immediately because shipping private API keys in a client binary is a critical credential leak vulnerability and causes duplicate per-user API spend.

---

## ADR-002: ABI-Safe Jukebox Integration via Asynchronous Manifest Parsing + Public Node IDs
- **Date**: 2026-10-27
- **Status**: Accepted
- **Context**: Jukebox (`fleym.nongd` `v3.8.0`) uses internal PIMPL classes (`Nongs::Impl`, `NongManager::Impl`) whose member functions are not exported across DLL/SO boundaries. Linking directly against unexported C++ symbols causes linker errors or crashes across minor Jukebox updates.
- **Decision**: Integrate with Jukebox via its stable on-disk JSON manifest (`<jukebox_save_dir>/manifest/<adjustedId>.json`, parsed on Geode's background worker pool) and its public Geode Node IDs (`"NongList"`, `"buttons-menu"`, `"song-info-label"`, `"artist-label"`, `"metadata-label"`), while marking `"fleym.nongd"` as `"required": false` in `mod.json`.
- **Consequences**: Zero cross-DLL ABI fragility; full visibility into `default`, `locals`, `youtube`, and `hosted` (Song File Hub) NONG tracks; and graceful standalone operation even if Jukebox is disabled.

---

## ADR-003: Portable Zero-Dependency C++23 Core Library (`include/mrc/`, `src/core/`)
- **Date**: 2026-10-27
- **Status**: Accepted
- **Context**: Full Geode `.geode` builds depend on platform-specific Geometry Dash headers and Cocos2d toolchains, which makes fast sanitizer runs (`-fsanitize=address,undefined`) difficult if business logic is tangled with UI classes.
- **Decision**: Implement all domain models, song classification, Jukebox manifest parsing, RFC 8259 JSON parsing/serialization, local disk caching, URL safety checks, attribution formatting, and FIPS 180-4 SHA-256 hashing in pure standard C++23 (`include/mrc/`, `src/core/`) with zero Geode/Cocos2d header dependencies.
- **Consequences**: `tests/cpp/test_mrc_core.cpp` compiles and runs in seconds on any standard Linux/macOS/Windows C++23 compiler with `-Wall -Wextra -Wpedantic -Werror` and ASan/UBSan enabled.

---

## ADR-004: Conservative Deterministic Guardrails Over Unconstrained LLM Verdicts
- **Date**: 2026-10-27
- **Status**: Accepted
- **Context**: LLMs can hallucinate permissive licences or be manipulated via prompt injection embedded in external song descriptions or HTML pages.
- **Decision**:
  1. Wrap external excerpts inside `<untrusted_evidence_json>` tags and strip HTML/control characters before analysis.
  2. Automatically skip LLM synthesis when `detect_prompt_injection_signals` triggers, forcing `review_required` / `unknown`.
  3. Apply deterministic post-LLM guardrails in `evaluate_song_rights`: an AI provider can **never** upgrade a song to `permitted` without verified primary licence evidence, and can **never** override `CC-NC`, `CC-ND`, remix/mashup sample risks, or restricted source rules.

---

## ADR-005: Privacy-Preserving Handling of Local NONG Files
- **Date**: 2026-10-27
- **Status**: Accepted
- **Context**: Jukebox users frequently import local `.mp3`/`.ogg` files (`jukebox_local`) that may include personal recordings, unreleased creator previews, or private audio.
- **Decision**: Never upload local audio files under any circumstance. Provide an explicit **off-by-default** privacy toggle (`share-local-file-hash = false`) that, only when enabled by the user, computes a local SHA-256 digest on a background worker thread to match existing shared metadata records.

---

## ADR-006: Target Geometry Dash `2.2081` & Geode `5.10.1+` (Defer `2.2091` Until Geode v6)
- **Date**: 2026-10-27
- **Status**: Accepted
- **Context**: Geometry Dash `2.2091` released in mid-October 2026 and broke binary compatibility with Geode v5; as of 2026-10-27, Geode stable (`v5.11.2`) and Jukebox (`v3.8.0`) target GD `2.2081` while Geode v6 is in development.
- **Decision**: Target GD `2.2081` and Geode `5.10.1` (verified compatible through `5.11.2`), and monitor upstream releases weekly via `scripts/check_upstream_compatibility.py` rather than fabricating unverified `2.2091` bindings.

---

## ADR-007: Explicit Async SQL Child-Row Replacement in SQLAlchemy 2.1
- **Date**: 2026-10-27
- **Status**: Accepted
- **Context**: During backend integration testing on SQLAlchemy `2.1.6` (`aiosqlite`), mutating ORM relationship collections (`song.platform_permissions.clear()`) after `session.flush()` triggered synchronous lazy loading (`sqlalchemy.exc.MissingGreenlet`).
- **Decision**: Replace child rows (`PlatformPermissionRecord`, `EvidenceRecord`) using explicit async SQL `delete(...)` + `session.add(...)` + `session.expire_all()`, followed by explicit `selectinload(...)` reload.
- **Consequences**: Deterministic async database behaviour across both SQLite and PostgreSQL `asyncpg` with zero implicit lazy-load hazards.
