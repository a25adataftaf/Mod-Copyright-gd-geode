# Security Policy

## Supported Versions

| Version | Supported |
|---|---|
| `0.1.x` | Yes |

## Reporting a Vulnerability

**Do not open public GitHub issues for suspected security vulnerabilities.**

Please report security issues privately using GitHub Private Vulnerability Reporting (`Security -> Advisories -> Report a vulnerability`) on the repository, or contact the maintainers via the security contact listed in the organization profile.

Include:
- Affected component (`Geode C++ client`, `FastAPI backend`, `CI/CD workflow`, or `deployment configuration`).
- Exact version or Git commit SHA.
- Minimal reproduction steps or proof-of-concept input.
- Potential impact (e.g., SSRF, prompt injection bypassing deterministic guardrails, unauthorized admin override, crash/memory corruption in JSON parsing, or sensitive data exposure).

We aim to acknowledge reports within **72 hours** and publish coordinated fixes with `CHANGELOG.md` `Security` advisories.

## Security Architecture Summary

### 1. Client/Server Trust Boundary
- The Geode mod (`mrc.music_rights_checker`) is treated as an **untrusted public client**.
- Private AI provider keys, database credentials, and admin review tokens exist strictly as server-side environment variables (`MRC_AI_API_KEY`, `MRC_DATABASE_URL`, `MRC_ADMIN_API_TOKEN`) and are excluded from Git via `.gitignore`.

### 2. Local Audio Privacy
- Locally imported NONG audio files (`jukebox_local`) are **never uploaded** to the backend.
- Optional local SHA-256 hashing (`share-local-file-hash`) is **disabled by default**, capped at 64 MiB on a background worker thread, and transmits only a 64-character hex digest when opted in by the user.

### 3. Untrusted External Content & Prompt-Injection Defenses
- Song titles, artist names, Jukebox manifest strings, Newgrounds/NCS HTML, and user dispute submissions are treated as **untrusted input**.
- HTML scripts, styles, comments, and control characters are stripped and length-bounded before storage or analysis (`backend/app/security/sanitizer.py`).
- Prompt-injection heuristics (`detect_prompt_injection_signals`) flag adversarial instructions in external webpages or metadata; flagged evidence bypasses LLM synthesis and forces `review_required` / `unknown` status.
- Even when AI synthesis runs, deterministic post-processing guardrails (`evaluate_song_rights`) prevent any LLM response from upgrading a song to `permitted` without verified primary licence evidence, or overriding `NC` / `ND` / remix / restricted source constraints.
- On the client, untrusted text rendered inside Geode's `MDTextArea` is markdown-escaped (`escapeMarkdownUntrusted`) to block malicious `mod:`, `level:`, `user:`, or external link injection, and browser launches validate public `https://` / `http://` authorities via `mrc::isSafeBrowserUrl`.

### 4. SSRF, Rate Limiting & Admin Access Control
- Outbound backend fetches validate schemes, ports (`80`/`443`), embedded credentials, and DNS-resolved IP addresses (`validate_public_http_url`), blocking RFC 1918 private networks, loopback (`127.0.0.0/8`, `::1`), and cloud metadata endpoints (`169.254.169.254`) without following redirects automatically.
- Public resolve and report endpoints enforce bounded per-IP sliding-window rate limits (`429 Too Many Requests` with `Retry-After`).
- Administrative review and override endpoints fail closed (`503`) if `MRC_ADMIN_API_TOKEN` is unset or shorter than 16 characters, verify bearer tokens using `hmac.compare_digest`, and record every state change in `audit_logs`.
