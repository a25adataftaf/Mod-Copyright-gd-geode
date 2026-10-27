# Backup, Restore & Disaster Recovery Plan

## 1. Scope of Backed-Up Data

### Included in Backups
- **`songs`**: Canonical deduplicated song records, verified licences, attribution templates, and manual review notes.
- **`platform_permissions`**: Per-platform (`youtube`, `tiktok`, `twitch`, `instagram`, `facebook`) evaluation rows.
- **`evidence_records`**: Primary licence excerpts, SHA-256 content hashes, and retrieval timestamps.
- **`review_reports`**: User-submitted inaccuracy reports and admin resolution notes.
- **`audit_logs`**: Full historical snapshots (`previous_snapshot_json` and `new_snapshot_json`) of automated evaluations and manual overrides.
- **`research_jobs`** & **`alembic_version`**: Job history and exact schema revision marker.

### Explicitly Excluded from Data Backups
- **Secrets & Credentials** (`MRC_AI_API_KEY`, `MRC_ADMIN_API_TOKEN`, database passwords, TLS private keys): Managed strictly via secret manager / host `.env` and rotated independently—never stored inside database tables or committed to Git.
- **User Local Audio Files**: Never collected or stored by the service.

---

## 2. Backup Schedule & Retention Policy

| Environment | Frequency | Retention | Storage Location |
|---|---|---|---|
| **Production PostgreSQL** | Every 6 hours + mandatory pre-migration snapshot | 14 daily, 8 weekly, 6 monthly | Encrypted object storage in a separate availability zone |
| **Self-Hosted / Local SQLite** | Daily + mandatory pre-migration snapshot | Last 14 snapshots | Separate disk/directory outside container volume |

---

## 3. Executing & Verifying Backups (`scripts/backup_and_verify_db.py`)

A backup is considered valid **only** after automated restore/integrity verification succeeds.

### SQLite Online Backup + Automated Restore Rehearsal
```bash
python3 scripts/backup_and_verify_db.py \
  --sqlite-path backend/mrc_rights.db \
  --output-dir ./backups
```
What this script does automatically:
1. Uses SQLite's online `Connection.backup()` API so concurrent readers/writers never produce a torn file.
2. Computes a SHA-256 digest of the backup archive.
3. Restores the archive into an isolated temporary rehearsal database, runs `PRAGMA integrity_check`, verifies all 6 core MRC schema tables exist, records row counts, and writes a signed `.manifest.json`.

### PostgreSQL Custom-Format Backup + TOC Verification
```bash
python3 scripts/backup_and_verify_db.py \
  --postgres-url "postgresql://mrc:PASSWORD@127.0.0.1:5432/mrc_rights" \
  --output-dir ./backups
```

---

## 4. Recovery Scenarios

### Scenario A: Full Database Node Loss
1. Provision a clean PostgreSQL 17 instance (or empty SQLite volume).
2. Verify backup archive SHA-256 against its `.manifest.json`:
   ```bash
   sha256sum -c <(jq -r '"\(.sha256)  backups/\(.backup_file)"' backups/mrc_pg_backup_<TS>.manifest.json)
   ```
3. Restore archive:
   - PostgreSQL: `pg_restore --clean --if-exists --no-owner --dbname="$POSTGRES_URL" backups/mrc_pg_backup_<TS>.dump`
   - SQLite: Stop `mrc-backend`, copy verified `.sqlite3` backup into place, and restart `mrc-backend`.
4. Confirm `GET /api/v1/health` returns `"database": "connected"` and spot-check `/api/v1/songs`.

### Scenario B: Bad Manual Override or Corrupted Single Song Record
Because `audit_logs` stores complete JSON snapshots (`previous_snapshot_json`) before every manual override or revalidation:
1. Inspect song history via `GET /api/v1/songs/{song_id}/audit`.
2. Re-apply the prior verified snapshot via `POST /api/v1/admin/songs/{song_id}/override`, or trigger fresh re-evaluation via `POST /api/v1/admin/songs/{song_id}/revalidate`—without taking the service offline or rolling back unrelated songs.

### Scenario C: Mandatory Pre-Migration Safety Protocol
Before running `alembic upgrade head` in staging or production:
1. Execute `scripts/backup_and_verify_db.py` and confirm `"restore_verification": "passed"`.
2. Test `alembic upgrade head` against a copy of the snapshot first.
3. Apply the migration to production and verify `/api/v1/health`.
