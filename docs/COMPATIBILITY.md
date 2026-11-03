# Compatibility Matrix & Upstream Maintenance Policy

_Last verified: 2026-10-27_

## 1. Verified Target Matrix

| Dependency / Platform | Configured Target | Verified Compatible Range | Status & Notes |
|---|---|---|---|
| **Geometry Dash (Windows 64-bit)** | `2.2081` | `2.2081` | Current Geode v5 supported GD build |
| **Geometry Dash (macOS arm64/x86_64)** | `2.2081` | `2.2081` | Dual-arch universal build via Geode SDK |
| **Geometry Dash (Android64 / Android32)** | `2.2081` | `2.2081` | Built via Geode Android NDK toolchain |
| **Geometry Dash (iOS arm64)** | `2.2081` | `2.2081` | Built via Geode iOS SDK toolchain |
| **Geometry Dash `2.2091` (Oct 2026)** | **Not targeted yet** | Unsupported until Geode v6 | Upstream Geode v5 does not target `2.2091`; do **not** change `mod.json` until Geode v6 & Jukebox publish verified `2.2091` releases |
| **Geode SDK (`geode-sdk/geode`)** | `5.10.1` | `5.10.1` – `5.11.2` | Same Geode v5 ABI family; CI pins `geode-sdk/bindings` to verified `2.2081` ref `1605f500d053` prior to Geode v6 / `2.2091` codegen refactor |
| **Jukebox (`fleym.nongd`)** | `>=v3.8.0` (`required: false`) | `v3.8.0` (manifest schema v1) | Integrates via `<jukebox_save_dir>/manifest/<adjustedId>.json` and public `NongList` Node IDs |
| **Node IDs (`geode.node-ids`)** | `>=v1.23.0` (`required: true`) | `v1.23.0+` | Required for stable `CustomSongWidget` and popup layouts |
| **C++ Toolchain** | C++23 | GCC 14.2+, Clang 18+, MSVC 19.38+ | Portable core verified locally on `g++ 14.2.0` (`-Wall -Wextra -Wpedantic -Werror` + ASan/UBSan) |
| **Backend Python Runtime** | Python `3.13` | Python `3.12` – `3.13` | Verified on Python `3.13.16` with `fastapi==0.162.0`, `sqlalchemy[asyncio]==2.1.6`, `greenlet==3.5.6`, `alembic==1.20.1` |

---

## 2. Why Jukebox Integration Avoids Private C++ ABI Linking

During upstream inspection of `Fleeym/jukebox` (`v3.8.0`), we verified that:
- `jukebox::Nongs` and `jukebox::NongManager` use internal PIMPL structures (`m_impl`) whose methods are **not** marked `DLL_EXPORT` across shared library boundaries (confirmed also by upstream `ethanrjs/OBSJukebox` `v1.2.3`).
- Conversely, Jukebox's on-disk JSON manifest (`<jukebox_save_dir>/manifest/<adjustedSongId>.json`) and Cocos2d/Geode Node IDs (`"NongList"`, `"buttons-menu"`, `"song-info-label"`, `"artist-label"`, `"metadata-label"`) are stable, human-readable, and completely decoupled from compiler-specific C++ name mangling.

Therefore, Music Rights Checker integrates with Jukebox using **asynchronous manifest inspection + public Geode Node IDs**, and marks `"fleym.nongd"` as `"required": false` in `mod.json` so players can also check Newgrounds, NCS, RobTop, and GD Music Library tracks even if Jukebox is temporarily disabled.

---

## 3. Upstream Monitoring & Safe Upgrade Procedure

Upstream versions are checked weekly by `.github/workflows/upstream-compatibility-monitor.yml` running `scripts/check_upstream_compatibility.py --live`.

### Mandatory Upgrade Rules
1. **Never Auto-Bump Blindly**: Automated bots or maintainers must never bump `mod.json` `"geode"` or `"gd"` fields without verifying:
   - Geode SDK changelog and migration guide for breaking API/hook changes.
   - `GeometryDash.bro` bindings for `CustomSongWidget` and `SongInfoObject` member/vtable stability.
   - Jukebox `nong_serialize.hpp` and `nong_cell_ui.cpp` for manifest JSON keys and Node IDs.
2. **Preserve Known-Good Targets**: When a new Geometry Dash patch (such as `2.2091`) breaks mod loaders before Geode releases a stable update, keep `main` targeting the verified working release (`2.2081` / Geode `5.10.1+`) and test pre-release SDKs on an isolated branch (`compat/geode-v6`).
3. **Verify Before Merging**:
   - Run `python3 scripts/check_upstream_compatibility.py --live`.
   - Run C++23 core + ASan/UBSan tests and full `pytest` suite.
   - Confirm all 5 platform targets (`Win64`, `MacOS`, `iOS`, `Android64`, `Android32`) succeed in `.github/workflows/build-geode-mod.yml`.
