# RAN remote error reports — design

Date: 2026-10-05 · Status: draft for review

## Goal

When any player (the user's brother, his friends, …) says "X is broken", Claude can pull that
player's logs, screenshots and device details by account name — no SSH, no asking them to zip
folders. Reports are for Claude to debug with; there is no human-facing UI.

Scope: the **native macOS client** (RanOdyssey Native) and a small ingest/query API on
main-server. The Wine "classic" app and the game servers are out of scope (servers are already
readable on the VPS).

## What a report contains

Every report carries a **context** block:

| Field | Source |
|---|---|
| `account`, `character`, `map` | game state once logged in; `anon` / empty before login |
| `install_id` | random UUID created on first launch, stored in App Support |
| `app_version`, `app_build` | bundle `CFBundleShortVersionString` / `CFBundleVersion` |
| `macos` | `sw_vers`-equivalent (`NSProcessInfo.operatingSystemVersionString`) |
| `mac_model`, `chip`, `ram_gb` | `sysctl hw.model`, `machdep.cpu.brand_string`, `hw.memsize` |
| `gpu` | Metal device name |
| `displays` | each display's size + scale, which one the game uses, current game resolution |
| `game_dir` | game data folder in use (the M1 white-screen lesson) |
| `uptime_s`, `timestamp` | process uptime, UTC time |

Plus, per type:

- **`crash`** — backtrace, signal, and the matching macOS `.ips` crash report.
- **`hotkey`** — screenshot (PNG) + last 500 errlog lines.
- **`error`** — the triggering log line, screenshot (PNG) + last 500 errlog lines.

## Client (native build only, `#ifndef _WIN32`)

New `port/platform/telemetry.cpp` (+ header). Responsibilities:

1. **Context collection** — gathered once at startup; account/character/map updated by the game
   when they change.
2. **Crash** — the existing fatal-signal handler (`port/platform/main_sdl.cpp` `OnFatalSignal`)
   additionally writes the backtrace + last-known context to
   `~/Library/Application Support/RanOdyssey Native/reports/pending-crash.txt`, using only
   async-signal-safe calls (pre-opened fd, `write`, `backtrace_symbols_fd`). On the **next
   launch** telemetry finds the file, attaches the newest `ran_client-*.ips` from
   `~/Library/Logs/DiagnosticReports` that is newer than the crash time, and uploads it.
3. **Hotkey F12** — plain F12 (Ctrl+F12 stays the engine profiler). Captures the back buffer via
   the existing snapshot code (`port/platform/snapshot.cpp`, extended to return pixels for PNG
   encoding), attaches the errlog tail, queues the report, prints "Report sent" (or "Report
   saved, will send later") to the in-game chat.
4. **Game errors** — CDebugSet's log write path calls a telemetry hook for lines containing
   `ERROR` or `fail` (case-insensitive). Throttled: at most 1 report per 60 s and 10 per
   session; a line already reported this session (after stripping digits) is skipped.
5. **Upload** — a single background worker thread; reports are first written to
   `reports/queue/` on disk, then POSTed with libcurl (system library). Success deletes the
   file; failure leaves it for the next launch. The game thread never blocks on the network.
   Queue capped at 50 MB (oldest dropped).

Off switch: `RAN_TELEMETRY=0` disables everything (for local dev runs).

## Server (main-server, `routes/ran.js`, mounted at `/ran`)

- `POST /ran/reports` — multipart: `report` (JSON) + optional `screenshot` (PNG) + optional
  `ips` (text). Auth: `X-Ran-Key` = ingest key compiled into the app (env `RAN_INGEST_KEY`).
  Limits: 5 MB total, 30 requests/min per IP. Stores to R2 bucket **`ran-reports`** at
  `reports/<account>/<YYYYMMDD-HHMMSS>-<type>-<shortid>/` as `report.json`, `screenshot.png`,
  `crash.ips`. Returns `{ id }`.
- `GET /ran/reports?account=&type=&since=&limit=` — newest first, JSON list of
  `{ id, account, type, timestamp, app_version, summary }`. Auth: `Authorization: Bearer
  <RAN_ADMIN_KEY>`.
- `GET /ran/reports/:id` — `report.json` contents plus signed URLs for the screenshot and `.ips`.
  Same admin auth.

No database: R2 key layout is the index (list by `reports/<account>/` prefix). Volume is tiny
(a few friends), well inside R2's free tier. The existing account-scoped R2 token covers the new
bucket; the bucket itself is created once with wrangler.

## Debug workflow (what this enables)

"Brother says he can't travel" → `curl -H "Authorization: Bearer $RAN_ADMIN_KEY"
'https://api.kaleidoscopical.com/ran/reports?account=mozart1234'` → read the newest hotkey/error
report: screenshot, log tail, version, Mac.

## Testing

- main-server: tests for ingest auth, size cap, R2 key layout, list filters (mock S3 client).
- native: unit test for context/payload building and the error-line throttle
  (`port/tests`, run via `run-tests.sh`).
- end to end: local native client → F12 → report visible via the GET API; forced crash
  (`RAN_TELEMETRY_TEST_CRASH=1`) → relaunch → crash report with `.ips` visible.

## Rollout

1. Deploy main-server (push `master`; Coolify auto-deploys), set `RAN_INGEST_KEY` /
   `RAN_ADMIN_KEY` env in Coolify, create the `ran-reports` bucket.
2. Ship native v0.3 via `package_native_app.sh --sign --dmg --notarize`.
3. Players install v0.3 once; from then on all reports are visible.

## Privacy note

Reports include the account name, Mac model and in-game screenshots — no passwords, no files
outside the game folders. Players are the user's family/friends.
