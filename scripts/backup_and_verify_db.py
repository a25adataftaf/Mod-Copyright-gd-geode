#!/usr/bin/env python3
"""Database backup & restore verification script for Music Rights Checker.

Supports:
1. Online atomic SQLite backups using Python's `sqlite3.Connection.backup()` API,
   SHA-256 checksum manifest generation, and automated restore verification on an
   isolated temporary database (`PRAGMA integrity_check` + core table verification).
2. PostgreSQL `pg_dump` custom-format backup generation (`--postgres-url`) with
   `pg_restore --list` archive TOC verification.
"""

from __future__ import annotations

import argparse
from datetime import datetime, timezone
import hashlib
import json
from pathlib import Path
import sqlite3
import subprocess
import sys
import tempfile


EXPECTED_TABLES = {
    "songs",
    "platform_permissions",
    "evidence_records",
    "research_jobs",
    "review_reports",
    "audit_logs",
}


def sha256_file(path: Path) -> str:
    digest = hashlib.sha256()
    with path.open("rb") as fh:
        for chunk in iter(lambda: fh.read(65536), b""):
            digest.update(chunk)
    return digest.hexdigest()


def backup_and_verify_sqlite(source_db: Path, output_dir: Path) -> Path:
    if not source_db.is_file():
        raise FileNotFoundError(f"Source SQLite database not found: {source_db}")

    output_dir.mkdir(parents=True, exist_ok=True)
    ts = datetime.now(timezone.utc).strftime("%Y%m%dT%H%M%SZ")
    backup_file = output_dir / f"mrc_backup_{ts}.sqlite3"
    manifest_file = output_dir / f"mrc_backup_{ts}.manifest.json"

    with sqlite3.connect(str(source_db)) as src_conn:
        with sqlite3.connect(str(backup_file)) as dst_conn:
            src_conn.backup(dst_conn)

    checksum = sha256_file(backup_file)

    # Restore rehearsal into an isolated temporary file to prove recoverability
    row_counts: dict[str, int] = {}
    with tempfile.TemporaryDirectory() as tmpdir:
        rehearsal_db = Path(tmpdir) / "restore_rehearsal.sqlite3"
        with sqlite3.connect(str(backup_file)) as bkp_conn:
            with sqlite3.connect(str(rehearsal_db)) as reh_conn:
                bkp_conn.backup(reh_conn)

        with sqlite3.connect(str(rehearsal_db)) as verify_conn:
            integrity = verify_conn.execute("PRAGMA integrity_check").fetchone()
            if not integrity or integrity[0] != "ok":
                raise RuntimeError(f"Restore rehearsal failed PRAGMA integrity_check: {integrity}")

            present_tables = {
                row[0]
                for row in verify_conn.execute(
                    "SELECT name FROM sqlite_master WHERE type='table'"
                ).fetchall()
            }
            missing = EXPECTED_TABLES - present_tables
            if missing:
                raise RuntimeError(f"Backup is missing expected MRC schema tables: {sorted(missing)}")

            for tbl in sorted(EXPECTED_TABLES):
                cnt = verify_conn.execute(f"SELECT COUNT(*) FROM {tbl}").fetchone()[0]
                row_counts[tbl] = int(cnt)

    manifest = {
        "created_at_utc": ts,
        "engine": "sqlite3",
        "backup_file": backup_file.name,
        "size_bytes": backup_file.stat().st_size,
        "sha256": checksum,
        "restore_verification": "passed",
        "table_row_counts": row_counts,
    }
    manifest_file.write_text(json.dumps(manifest, indent=2) + "\n", encoding="utf-8")
    print(f"Backup created and restore-verified: {backup_file}")
    print(f"Manifest written: {manifest_file} (SHA-256: {checksum})")
    return backup_file


def backup_and_verify_postgres(pg_url: str, output_dir: Path) -> Path:
    output_dir.mkdir(parents=True, exist_ok=True)
    ts = datetime.now(timezone.utc).strftime("%Y%m%dT%H%M%SZ")
    dump_file = output_dir / f"mrc_pg_backup_{ts}.dump"
    manifest_file = output_dir / f"mrc_pg_backup_{ts}.manifest.json"

    subprocess.run(
        ["pg_dump", "--format=custom", "--no-owner", "--no-privileges", f"--file={dump_file}", pg_url],
        check=True,
    )
    toc = subprocess.run(
        ["pg_restore", "--list", str(dump_file)],
        capture_output=True,
        text=True,
        check=True,
    )
    checksum = sha256_file(dump_file)
    manifest = {
        "created_at_utc": ts,
        "engine": "postgresql",
        "backup_file": dump_file.name,
        "size_bytes": dump_file.stat().st_size,
        "sha256": checksum,
        "toc_entries": len(toc.stdout.splitlines()),
        "restore_toc_verification": "passed",
    }
    manifest_file.write_text(json.dumps(manifest, indent=2) + "\n", encoding="utf-8")
    print(f"PostgreSQL backup created and TOC-verified: {dump_file}")
    return dump_file


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    group = parser.add_mutually_exclusive_group(required=True)
    group.add_argument("--sqlite-path", type=Path, help="Path to SQLite database file to back up.")
    group.add_argument("--postgres-url", type=str, help="PostgreSQL connection URI for pg_dump.")
    parser.add_argument(
        "--output-dir",
        type=Path,
        default=Path("./backups"),
        help="Directory where backup archives and manifests are written.",
    )
    args = parser.parse_args()

    if args.sqlite_path:
        backup_and_verify_sqlite(args.sqlite_path, args.output_dir)
    else:
        backup_and_verify_postgres(args.postgres_url, args.output_dir)
    return 0


if __name__ == "__main__":
    sys.exit(main())
