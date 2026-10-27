# Music Rights, Licensing & Platform Policy Research Notes

_Last verified: 2026-10-27_

This document records the authoritative licensing and platform-enforcement rules encoded into `backend/app/research/evaluator.py` and surfaced in the Geode UI.

---

## 1. Fundamental Legal & Enforcement Distinctions

Music Rights Checker strictly separates five concepts that are frequently conflated by players and creators:

1. **Copyright Ownership**: Who owns the musical composition (publishing) and master sound recording.
2. **In-Game Usage Permission**: Whether an artist/label permitted RobTop Games to distribute a track inside Geometry Dash. **In-game permission does NOT automatically grant external video upload or monetization rights.**
3. **Creator Video / Livestream Usage Permission**: Whether a public licence (e.g., Creative Commons or NCS Creator Usage Policy) permits synchronizing the track in user-generated videos or streams.
4. **Commercial & Ad Monetization Permission**: Whether the creator may earn YouTube Partner Program ad revenue, Twitch subscriptions/bits/ads, TikTok Creator Rewards, or brand sponsorships while using the music.
5. **Automated Enforcement Systems**:
   - **YouTube**: Content ID (monetize/track/mute/block claims, plus blocking of Shorts >60 seconds containing claimed music) vs. formal DMCA copyright takedown strikes.
   - **Twitch**: Audible Magic automated VOD/Clip muting vs. live DMCA takedown notices and repeat-infringer account suspension.
   - **TikTok**: Commercial Music Library (CML) vs. General Music Library restrictions for business/promotional accounts and videos exceeding 60 seconds.
   - **Instagram & Facebook (Meta)**: Meta Rights Manager automated muting/blocking on Reels, Stories, and Live broadcasts; Meta Sound Collection applies only to tracks sourced from Meta's own library.

**Core Invariant**: Even when a song is genuinely licensed for creator monetization, third-party distributors or fraudulent Content ID registrants can still trigger automated claims, and the **absence of an automated claim never proves legal permission**.

---

## 2. Music Source Profiles in Geometry Dash

### A. NoCopyrightSounds (`ncs_library`)
- **Primary Source**: `https://ncs.io/usage-policy` and track pages `https://ncs.io/<TrackSlug>` (`#panel-copy2` attribution block).
- **Verified Policy**:
  - **Independent Creators** on YouTube and Twitch are permitted to upload, livestream, and monetize content **provided exact NCS attribution and track links are included** in the video description or Twitch stream panel.
  - **Brands, Commercial Organizations, Games, TV/Film, and Paid Ads** are **not** covered by the free creator policy and require a paid commercial licence from NCS (`licensing@ncs.io`).
  - **TikTok, Instagram, and Facebook**: Creators should attach the official NCS sound via the platform's native audio picker where available and include track attribution to minimize automated claim friction.

### B. Newgrounds Audio Portal (`newgrounds`)
- **Primary Source**: `https://www.newgrounds.com/audio/listen/<songID>` licensing block (Creative Commons icons/links + artist usage flags).
- **Creative Commons Breakdown**:
  - `CC0 1.0` / Public Domain: Commercial and non-commercial use permitted without mandatory attribution (credit still recommended).
  - `CC BY`: Commercial and non-commercial video synchronization permitted **if** full TASL attribution (Title, Author, Source URL, Licence link) is provided and no additional restrictions are implied.
  - `CC BY-SA`: ShareAlike copyleft obligations conflict with standard proprietary YouTube/TikTok/Twitch video licensing unless the entire video is released under a compatible licence -> classified as **`review_required` / `conditional`**.
  - `CC BY-ND`: **NoDerivatives** prohibits synchronizing music into a video (which creates an adaptation/derivative work under CC legal text) -> classified as **`restricted` / `not_allowed`** unless separate written artist permission exists.
  - `CC BY-NC` / `CC BY-NC-SA` / `CC BY-NC-ND`: **NonCommercial** explicitly prohibits ad monetization, sponsorships, monetization programs, and commercial channels -> classified as **`conditional_non_commercial` (`monetization = not_allowed`)**.
- **Operational Observation (2026-10-27)**: Direct unauthenticated HTTP fetches to `www.newgrounds.com/audio/listen/<id>` from datacenter IP ranges frequently receive Cloudflare HTTP `403`. When live page retrieval is blocked and no cached/manual verification exists, the backend **never guesses**—it records `fetch_status = "http_403"` and returns `unknown` with explicit instructions to check the Newgrounds track page directly or submit verified evidence.

### C. Geometry Dash Music Library (`gd_music_library`, IDs `>= 10,000,000`) & Official RobTop Tracks (`robtop_official`)
- **Primary Source**: Official Geometry Dash Music FAQ (`https://www.robtopgames.com/faq/en/answers/music/`).
- **Verified Policy**:
  - RobTop Games licenses tracks from independent artists and catalogues (e.g., Camellia, MDK, Shirobon, Kevin MacLeod, Waterflame, DJVI, F-777, Boom Kitty) for **in-game playback**.
  - External video upload and ad monetization rights vary **per artist and distributor**: some artists explicitly allow creator gameplay monetization with credit, others route YouTube ad revenue to themselves via Content ID (`conditional_revenue_share`), and others prohibit commercial reuse.
  - Unless artist-specific licence evidence or a verified manual review record is stored for that track, Music Rights Checker conservatively classifies external monetization as `review_required` / `conditional`.

### D. Jukebox NONG Replacements (`jukebox_hosted`, `jukebox_youtube`, `jukebox_local`)
- **Context**: NONG ("Not On Newgrounds") replacements imported via Jukebox (`fleym.nongd`)—including community indexes such as Song File Hub (`api.songfilehub.com`) or YouTube downloads—frequently replace an in-game placeholder song with mainstream commercial label releases (UMG, Sony, WMG, anime/game OSTs) or unverified remixes/mashups.
- **Verified Policy**:
  - Unless verified licence evidence proves an open/creator-friendly licence, Jukebox NONG tracks default to **`restricted`** (when known commercial/hosted unlicensed distribution is detected) or **`unknown` / `review_required`** with `high` automated claim risk across YouTube, TikTok, Twitch, Instagram, and Facebook.
  - **Mashups, Remixes, Nightcore, and Covers**: Automatically flagged by `has_remix_or_mashup_signals` because derivative audio requires clearance for **every** underlying composition and master sample; permission from the remixer alone is legally insufficient.
