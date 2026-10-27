# Deployment, Configuration & Operations Guide

## 1. Environment Variables (`backend/.env`)

Copy `backend/.env.example` to `backend/.env` (or inject via your secret manager). **Never commit `.env` to Git.**

| Variable | Default | Required in Prod | Description |
|---|---|---|---|
| `MRC_ENVIRONMENT` | `development` | Yes (`production`) | Runtime profile (`development`, `staging`, `production`) |
| `MRC_DATABASE_URL` | `sqlite+aiosqlite:///./mrc_rights.db` | Yes | Async SQLAlchemy DSN (`postgresql+asyncpg://user:pass@host:5432/db` in prod) |
| `MRC_ADMIN_API_TOKEN` | `""` | Yes (`>=16` chars) | Bearer token required for `/api/v1/admin/*`; endpoints fail closed (`503`) if unset |
| `MRC_AI_PROVIDER` | `none` | Optional | `none` (rule-engine only), `openai`, `anthropic`, or `gemini` |
| `MRC_AI_API_KEY` | `""` | Only if AI enabled | Server-side LLM API key; never exposed to clients or logs |
| `MRC_AI_MODEL` | `""` | Optional | Provider model override (defaults to `gpt-4.1-mini` / `claude-3-5-haiku-latest` / `gemini-2.5-flash`) |
| `MRC_AI_DAILY_BUDGET_CALLS` | `250` | Recommended | Hard daily UTC cap on outbound LLM calls; automatically falls back to deterministic rule engine when reached |
| `MRC_AI_TIMEOUT_SECONDS` | `15.0` | Optional | Timeout per outbound AI synthesis request |
| `MRC_ENABLE_EXTERNAL_FETCH` | `true` | Optional | Enables safe SSRF-guarded HTTP collection of public licence pages |
| `MRC_CACHE_TTL_HOURS` | `168` (7 days) | Optional | Shared database cache freshness window before automatic revalidation |
| `MRC_FORCE_REFRESH_COOLDOWN_MINUTES` | `30` | Optional | Cooldown preventing abusive repeat `force_refresh=true` calls per song |
| `MRC_RATE_LIMIT_RESOLVE_PER_MINUTE` | `30` | Recommended | Sliding-window requests/minute per client IP on `/songs/resolve` |
| `MRC_RATE_LIMIT_REPORTS_PER_HOUR` | `10` | Recommended | Sliding-window reports/hour per client IP on `/reviews/reports` |
| `MRC_MAX_CONCURRENT_RESEARCH_JOBS` | `4` | Optional | Semaphore bounding concurrent outbound research tasks |

---

## 2. Production Deployment (Docker Compose + PostgreSQL 17 + Reverse Proxy)

1. Provision a Linux host behind an HTTPS reverse proxy (Caddy, Nginx, or Cloudflare Tunnel) terminating TLS 1.2/1.3.
2. Create a root `.env` file with strong `POSTGRES_PASSWORD` and `MRC_ADMIN_API_TOKEN` (`openssl rand -hex 32`), and add `asyncpg` when building for PostgreSQL if not bundled in your custom image.
3. Launch services:
   ```bash
   docker compose up -d --build
   ```
4. Verify health and database connectivity:
   ```bash
   curl -fsS https://api.yourdomain.example/api/v1/health
   ```
   Expected response (`200 OK`):
   ```json
   {
     "status": "ok",
     "version": "0.1.0",
     "api_version": "v1",
     "database": "connected",
     "ai_provider_mode": "deterministic_only"
   }
   ```

> **Reverse Proxy Header Note**: Ensure your reverse proxy overwrites `X-Forwarded-For` with the true remote client IP (or strip untrusted `X-Forwarded-For` headers at the edge) so per-IP rate limiting reflects actual client addresses.

---

## 3. Cost Control & Abuse Prevention Strategy

Public Geometry Dash mods can experience sudden traffic spikes when a popular creator showcases a level. Music Rights Checker controls operating costs at four layers:

1. **Client Persistent Cache (`rights_cache.json`)**: Repeat visits to levels with the same song trigger zero HTTP requests during the 7-day freshness window.
2. **Shared Canonical Database Records**: Once *any* player resolves `ng:467339` or `ncs:gd:10012345`, every subsequent player receives the shared database record immediately without external scraping or AI calls.
3. **Single-Flight Coalescing + Refresh Cooldowns**: Concurrent cache misses for the same canonical song share one `asyncio.Lock`, and `force_refresh=true` is throttled by `MRC_FORCE_REFRESH_COOLDOWN_MINUTES`.
4. **Hard Daily AI Call Cap (`MRC_AI_DAILY_BUDGET_CALLS`)**: `DailyBudgetTracker` enforces a strict ceiling on daily LLM invocations. When the daily cap is reached, the service remains 100% online and continues serving cached records and deterministic rule-engine evaluations.

---

## 4. Operational Runbook & Incident Response

### Checking Queue & Budget Metrics
```bash
curl -fsS -H "Authorization: Bearer $MRC_ADMIN_API_TOKEN" \
  https://api.yourdomain.example/api/v1/admin/metrics
```

### Handling User Inaccuracy Reports
1. List open reports:
   ```bash
   curl -fsS -H "Authorization: Bearer $MRC_ADMIN_API_TOKEN" \
     "https://api.yourdomain.example/api/v1/admin/reviews?status_filter=open"
   ```
2. Verify the primary artist/label licence URL manually.
3. Apply an authoritative override (automatically logged in `audit_logs` and protected from public `force_refresh` overwrites):
   ```bash
   curl -fsS -X POST -H "Authorization: Bearer $MRC_ADMIN_API_TOKEN" \
     -H "Content-Type: application/json" \
     https://api.yourdomain.example/api/v1/admin/songs/<SONG_UUID>/override \
     -d '{ ... }'
   ```

### Emergency Rollback Procedure
1. **Application Rollback**: Re-deploy the previous tagged container image.
2. **Database Schema Rollback** (only if a newly applied migration caused a regression and a verified pre-migration backup was taken):
   ```bash
   # Always take a snapshot backup BEFORE downgrading
   python3 scripts/backup_and_verify_db.py --postgres-url "$PG_DUMP_URL" --output-dir ./backups
   cd backend && PYTHONPATH=.. alembic downgrade -1
   ```
