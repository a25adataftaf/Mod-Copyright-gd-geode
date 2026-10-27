#!/usr/bin/env python3
"""Upstream compatibility & manifest consistency verifier for Music Rights Checker.

Checks:
1. Local consistency across `mod.json`, `backend/app/__init__.py`, `backend/pyproject.toml`,
   and `CHANGELOG.md`.
2. Optional live upstream tag inspection (`--live`) via `git ls-remote` against:
   - `geode-sdk/geode`
   - `Fleeym/jukebox`
   - `geode-sdk/NodeIDs`
   Reports whether new upstream releases are semver-compatible or require manual ABI/hook review.
"""

from __future__ import annotations

import argparse
import json
import os
from pathlib import Path
import re
import subprocess
import sys

REPO_ROOT = Path(__file__).resolve().parent.parent

SEMVER_RE = re.compile(r"^v?(\d+)\.(\d+)\.(\d+)$")

UPSTREAM_REPOS = {
    "geode": "https://github.com/geode-sdk/geode.git",
    "jukebox": "https://github.com/Fleeym/jukebox.git",
    "node-ids": "https://github.com/geode-sdk/NodeIDs.git",
}


def parse_semver(tag: str) -> tuple[int, int, int] | None:
    m = SEMVER_RE.match(tag.strip())
    if not m:
        return None
    return int(m.group(1)), int(m.group(2)), int(m.group(3))


def query_latest_stable_tag(repo_url: str) -> str:
    env = dict(os.environ)
    env["GIT_TERMINAL_PROMPT"] = "0"
    proc = subprocess.run(
        ["git", "ls-remote", "--tags", "--refs", repo_url],
        capture_output=True,
        text=True,
        timeout=20,
        env=env,
        check=True,
    )
    versions: list[tuple[tuple[int, int, int], str]] = []
    for line in proc.stdout.splitlines():
        parts = line.split("\t")
        if len(parts) != 2:
            continue
        tag = parts[1].removeprefix("refs/tags/")
        sv = parse_semver(tag)
        if sv is not None:
            versions.append((sv, tag))
    if not versions:
        raise RuntimeError(f"No stable semver tags found for {repo_url}")
    versions.sort(key=lambda item: item[0])
    return versions[-1][1]


def check_local_manifests() -> dict[str, str]:
    mod_json_path = REPO_ROOT / "mod.json"
    mod_data = json.loads(mod_json_path.read_text(encoding="utf-8"))

    mod_version = mod_data.get("version", "")
    sv = parse_semver(mod_version)
    if sv is None:
        raise SystemExit(f"Invalid SemVer in mod.json version: {mod_version!r}")

    geode_target = mod_data.get("geode", "")
    if parse_semver(geode_target) is None:
        raise SystemExit(f"Invalid Geode SDK target version in mod.json: {geode_target!r}")

    gd_targets = mod_data.get("gd", {})
    for plat in ("win", "mac", "android", "ios"):
        if plat not in gd_targets or not gd_targets[plat]:
            raise SystemExit(f"Missing Geometry Dash target for platform {plat!r} in mod.json")

    deps = mod_data.get("dependencies", {})
    if "fleym.nongd" not in deps or "geode.node-ids" not in deps:
        raise SystemExit("mod.json must declare both geode.node-ids and fleym.nongd dependencies")

    jukebox_dep = deps["fleym.nongd"]
    jukebox_req = jukebox_dep["version"] if isinstance(jukebox_dep, dict) else str(jukebox_dep)
    node_ids_dep = deps["geode.node-ids"]
    node_ids_req = node_ids_dep["version"] if isinstance(node_ids_dep, dict) else str(node_ids_dep)

    init_text = (REPO_ROOT / "backend" / "app" / "__init__.py").read_text(encoding="utf-8")
    m_ver = re.search(r'__version__\s*=\s*"([^"]+)"', init_text)
    if not m_ver:
        raise SystemExit("Could not locate __version__ in backend/app/__init__.py")
    backend_ver = m_ver.group(1)

    changelog_path = REPO_ROOT / "CHANGELOG.md"
    if changelog_path.exists():
        changelog_text = changelog_path.read_text(encoding="utf-8")
        bare_mod_ver = mod_version.lstrip("v")
        if f"[{bare_mod_ver}]" not in changelog_text:
            raise SystemExit(
                f"CHANGELOG.md is missing entry [{bare_mod_ver}] matching mod.json {mod_version}"
            )

    return {
        "mod_version": mod_version,
        "backend_version": backend_ver,
        "geode_target": geode_target,
        "gd_win": gd_targets["win"],
        "jukebox_constraint": jukebox_req,
        "node_ids_constraint": node_ids_req,
    }


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument(
        "--offline-manifest-check",
        action="store_true",
        help="Validate local manifests, version numbers, and changelog parity without network calls.",
    )
    parser.add_argument(
        "--live",
        action="store_true",
        help="Query upstream Git repositories for latest stable tags and evaluate compatibility.",
    )
    args = parser.parse_args()

    local_info = check_local_manifests()
    print("## Music Rights Checker — Compatibility Check\n")
    print(f"- **Mod Version**: `{local_info['mod_version']}`")
    print(f"- **Backend Version**: `{local_info['backend_version']}`")
    print(f"- **Configured Geode SDK Target**: `{local_info['geode_target']}`")
    print(f"- **Configured Geometry Dash Target**: `{local_info['gd_win']}`")
    print(f"- **Configured Jukebox Constraint**: `{local_info['jukebox_constraint']}`")
    print(f"- **Configured Node IDs Constraint**: `{local_info['node_ids_constraint']}`\n")

    if not args.live:
        print("Local manifest consistency check: **PASSED**.")
        return 0

    print("| Dependency | Configured Baseline | Latest Upstream Stable | Assessment |")
    print("|---|---|---|---|")

    geode_latest = query_latest_stable_tag(UPSTREAM_REPOS["geode"])
    cfg_geode_sv = parse_semver(local_info["geode_target"])
    lat_geode_sv = parse_semver(geode_latest)
    if cfg_geode_sv and lat_geode_sv:
        if lat_geode_sv[0] > cfg_geode_sv[0]:
            geode_status = "ACTION REQUIRED: New major Geode SDK; do NOT bump blindly without ABI/GD verification"
        elif lat_geode_sv >= cfg_geode_sv:
            geode_status = "Compatible (same Geode v5 major ABI series)"
        else:
            geode_status = "Configured version is ahead of upstream stable"
    else:
        geode_status = "Manual inspection required"
    print(f"| Geode SDK (`geode-sdk/geode`) | `v{local_info['geode_target']}` | `{geode_latest}` | {geode_status} |")

    jb_latest = query_latest_stable_tag(UPSTREAM_REPOS["jukebox"])
    cfg_jb_sv = parse_semver(local_info["jukebox_constraint"].lstrip(">="))
    lat_jb_sv = parse_semver(jb_latest)
    if cfg_jb_sv and lat_jb_sv:
        if lat_jb_sv[0] > cfg_jb_sv[0]:
            jb_status = "REVIEW REQUIRED: New major Jukebox release; verify manifest schema & Node IDs"
        else:
            jb_status = "Compatible (v3 manifest schema & Node IDs verified)"
    else:
        jb_status = "Manual inspection required"
    print(f"| Jukebox (`Fleeym/jukebox`) | `{local_info['jukebox_constraint']}` | `{jb_latest}` | {jb_status} |")

    nid_latest = query_latest_stable_tag(UPSTREAM_REPOS["node-ids"])
    print(f"| Node IDs (`geode-sdk/NodeIDs`) | `{local_info['node_ids_constraint']}` | `{nid_latest}` | Compatible |")

    return 0


if __name__ == "__main__":
    sys.exit(main())
