# RAN EP7 Mac Setup — Live Notes

## Task 0 inventory (downloaded + extracted)

Location: `/Users/t31k/Projects/RAN/downloads/extracted/`

**Database backups** (`Database/`) — dated **Oct 21 2011** (⚠️ likely SQL 2005/2008 → conversion contingency probable):
- `RanGame1.bak` (4.5 MB)
- `RanUser.bak` (6.3 MB)
- `RanShop.bak` (4.5 MB)
- `RanLog.bak` (184 MB)

**Server Files** (`Server Files/`) — prebuilt ORIGINAL exes (MFC 7.1 / VS2003, `mfc71.dll`+`msvcr71.dll` present) → Task 1 fallback:
- `SessionServer.exe`, `LoginServer.exe`, `FieldServer.exe`, `AgentServer.exe`, `Game.exe`
- `cfg/game01.ini`..`game04.ini` (see key mapping below)
- runtime DLLs: `d3dx9_26-30.dll`, `mfc71*.dll`, `msvcr71.dll`, `msvcp71.dll`, `tbb.dll`, `ijl15.dll`, `bugtrap.dll`, `msvbvm60.dll`

**Tools:** `rzcheck/rzcheck.exe` (+Longinfo.txt), `REditor04.exe`, `glogicserver.rcc` (536 KB)

## Config key names (from `cfg/game01.ini` = Session server)
- `server_ip` (already `127.0.0.1`), `server_service_port 5113`, `server_control_port 2001`
- ODBC keys per DB block: `<x>_odbc_name` (DSN name), `<x>_odbc_user` (`sa`), `<x>_odbc_pass` (ref value `1234`), `<x>_odbc_database`, `<x>_odbc_pool_size`, `<x>_odbc_response_time`
- Blocks seen: `user_odbc_*` (RanUser), `log_odbc_*` (RanLog) — game02-04.ini expected to add RanGame1/RanShop.

## Decisions
- **SA password:** keeping strong `RanDev!2026` (SQL 2022 rejects weak `1234`). Must update every `*_odbc_pass` in the inis from `1234` → `RanDev!2026`.
- **Repo name:** `T31K/RAN` (user request), public, for CI.

## BLOCKER (Task 4): Wine unavailable via Homebrew
All WineHQ casks (`wine-stable`, `wine@staging`, `wine@devel`) **disabled on macOS 2026-09-01** (fail Gatekeeper). `gcenx/wine` tap no longer ships `wine-crossover` (only `game-porting-toolkit`).
Pending user decision on runtime approach (free direct WineHQ .pkg + Gatekeeper bypass vs CrossOver trial vs GPTK).
