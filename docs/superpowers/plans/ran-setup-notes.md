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

## ✅ WORKING SETUP (ODBC bridge solved)

**The hard part — ODBC from Wine to SQL Server 2022 — is solved.** Proven: `odbcprobe3.exe` → `SQLConnect ret=0, QUERY OK rows=1`.

Key findings & solution:
- CrossOver engine's odbc32 proxy is broken in wow64 (returns 309, never loads driver manager). **Must use mainline Wine 10 engine** (`WS11Wine10.0_3`) at `/Users/t31k/Projects/RAN/wine/w10`.
- Wine's "Unix driver" path (loading libtdsodbc.0.so directly) is broken — allocates handle via libodbc but calls driver directly → handle mismatch → returns 5. **Must use a native Windows FreeTDS driver** (`tdsodbc.dll`, cross-compiled with mingw, no SSL).
- Built: `/Users/t31k/Projects/RAN/wine/tds/tdsodbc.dll` (FreeTDS 1.5.19, win32, `--without-openssl`, `CFLAGS=-Wno-incompatible-pointer-types`).
- SQL 2022 accepts unencrypted connections → DSNs use `Encryption=off` (no TLS lib needed).
- **32-bit apps read `Wow6432Node`** registry — driver + DSNs must be registered there (`/tmp/ran-odbc-wow.reg` applied to `~/.wine-ran10`).
- Engine needs template Frameworks dylibs copied to `w10/wswine.bundle/lib/` (libinotify rpath).
- Server needs `MFC71ENU.DLL` (copied from mfc71kor.dll) in game-client dir.

**Runtime env (in run-servers.command / run-client.command):**
- WINE=`/Users/t31k/Projects/RAN/wine/w10/wswine.bundle/bin/wine`
- WINEPREFIX=`~/.wine-ran10`
- DYLD_FALLBACK_LIBRARY_PATH=template Frameworks:/usr/lib (for freetype)
- NO libodbc/ODBCSYSINI (pure Windows driver via registry)

**Start everything:** `docker start ran-mssql` → `run-servers.command` → click Start in each window (Session→Login→Field→Agent) → `run-client.command` → log in T31K/T31Klegend.

## FINAL FIX: odbc32 shim (connection-pooling crash)
The RAN servers call `SQLSetEnvAttr(NULL, SQL_ATTR_CONNECTION_POOLING)` which CRASHES Wine 10's odbc32 (access violation in ODBC32.dll). Fix: a shim `odbc32.dll` (`/Users/t31k/Projects/RAN/wine/odbc32.dll`, also in game-client) that:
- Forwards all 17 ODBC functions the servers import (ordinals 7,9,11,12,13,19,24,26,31,35,36,39,43,48,49,72) directly to `tdsodbc.dll` (FreeTDS reads DSN from Wow6432Node registry and connects standalone — verified).
- Implements `SQLSetEnvAttr` (@75) to no-op CONNECTION_POOLING/CP_MATCH (return SQL_SUCCESS), forwarding other attrs to the driver.
Loaded via `WINEDLLOVERRIDES=odbc32=n` (in run-servers.command). Client (Game.exe) does NOT use ODBC, so no override there.
Verified: probe with the exact server sequence → `pooling ret=0, connect ret=0, QUERY OK rows=1`. SessionServer loads clean and stays alive.

## ✅✅ LOGIN WORKS — full auth chain solved (2026-09-12)

The login flow: Client → Login (version check) → Session → Agent → `user_verify` → `GetUserInfo`. All fixed:

1. **Client version check**: `game04.ini` (login) `server_version`/`patch_version` must match client `cVer.bin` [73, 822] → set `server_version 822`, `patch_version 73`. (Login window shows Game Version 822 / Launcher 73.)

2. **`user_verify` (RanUser)**: the Agent calls it via ODBC binding param as SQL_PARAM_OUTPUT (int), reads the output param — NOT a result set, NOT return value. Success = output **1, 2, or 3**.
   - **CRITICAL FreeTDS quirk**: the proc MUST end with `SET NOCOUNT OFF` before RETURN. With `SET NOCOUNT ON` + a trailing UPDATE, FreeTDS's `SQLExecute` returns **SQL_NO_DATA (100)**, which the agent treats as DB_ERROR → "system error". With `SET NOCOUNT OFF`, `SQLExecute` returns SQL_SUCCESS (0). Verified with `/Users/t31k/Projects/RAN/wine/spprobe.exe` (replicates the agent's exact ExecuteSpInt call).
   - Client sends password as **uppercase MD5 truncated to ~19-20 chars**. Current proc is AUTH-BYPASSED (always returns 1, auto-creates account). To restore real auth, store `UPPER(MD5(pw))` truncated to UserPass char(20) and match by prefix.

3. **`GetUserInfo`**: Stage 1 `SELECT ... FROM UserInfo WHERE UserID` (RanUser), Stage 2 `Exec sp_Extreme <UserNum>` (RanGame1). **`sp_Extreme` was broken/missing** — replaced with a stub `SELECT 0 AS ExtremeM, 0 AS ExtremeW` in RanGame1. Account needs `ChaRemain > 0` to create characters.

Diagnosis method that cracked it: subagent read `[Lib]__NetServer/Sources/s_COdbcSupervisor.cpp` (ExecuteSpInt) + `s_COdbcUserGetUserInfo.cpp` (GetUserInfo) + `s_CDbActionUser.cpp` (result mapping), then a mingw ODBC probe (`spprobe.c`) replicated the exact call to catch the SQL_NO_DATA return.

**Accounts**: admin/admin, T31K/T31Klegend (auth currently bypassed — any password works).

## ✅ CLIENT SWAP — guide-matched v822 client (2026-09-13)

Root cause of "character data failed to be processed": the GitLab client was a DIFFERENT build than the guide's prebuilt server → mismatched `glogic.rcc` (class/stat table) → char creation stored garbage (Class=64, HP=3.9M).

**Fix — downloaded + installed the guide's actual matched client** (per forum "first links"):
- `ranonline_full_ep6_v.789.exe` (690 MB) + `ranonline_patch_ep6_v.789_to_ep7_v.822.exe` (53 MB) from MEGA → both NSIS installers, extracted on Mac with `7z x` (no Windows needed).
- Base cVer.bin = [70, 789]; after patch = **[73, 822]** (matches server game04.ini `server_version 822 / patch_version 73`).
- Installed to **`/Users/t31k/Projects/RAN/client`** (moved from downloads/client-v789).
- Guide step "copy game.exe from server to client": copied Server Files' `Game.exe` (4538880 B) over the PH `game.exe` (1463330 B).
- Copied `MFC71ENU.DLL` into client (server Game.exe needs it).
- `param.ini`: decrypted (AES-256-ECB, key hex 736b726b7268746c76646a214023777066726d666a677270676f21402324716b, 4-byte header 03000000), set `LoginAddress = 127.0.0.1`, zero-padded to 16B, re-encrypted, verified round-trip.
- **Synced server `glogic.rcc`** to the guide client's (`a027babd…`, was GitLab `2c89a778…`) so client↔server class tables match. Old one backed up at /tmp/glogic.rcc.server.bak. (level.rcc already matched; glogicserver.rcc unchanged.)
- `run-client.command` now `cd /Users/t31k/Projects/RAN/client`.

**Forum download checklist (first thread) — all 10 accounted for:** items 1-3 (MS .NET 2 / SQL 2005 Express / SSMS) not needed on Mac (Docker SQL 2022 + Wine); client v789 + patch v822 = installed; DB backups / Server files / glogicserver.rcc / REditor4 / rzcheck = already had.

**Next verify:** start servers (Session→Login→Field→Agent), launch client, create a FRESH character, confirm it saves valid data (Class 1-8, sane HP/stats) and enters the world.

## 🏆 COMPLETE — IN THE WORLD (2026-09-13). Full victory + modding foundation

**Status: RAN Online EP7 fully playable on M3 Mac, zero Windows, 100% self-compiled.**
Character enters world, plays, custom cheat commands work. All from our own build:
GitHub Actions (VS2022, Win32) compiles all 23 projects → download artifacts → run under Wine 10.

### The four root-cause fixes nobody in the community ever found (all committed to T31K/RAN):
1. **FreeTDS: `SQL_SUCCESS_WITH_INFO` treated as fatal error** in 58 places across the ODBC
   layer (code written for SQL Native Client). Fixed: only SQL_ERROR is an error.
2. **NULL-blob infinite loop**: `ReadImage`'s SQLGetData chunk loop only breaks on
   `lSize==0` or SQL_NO_DATA; a NULL blob yields lSize=-1 (SQL_NULL_DATA) → infinite loop →
   DB worker thread hangs forever (this is why 2nd join attempts found a dead thread).
   Data fix: `UPDATE ChaInfo SET ChaCoolTime=0x WHERE NULL`. (Code fix still worth doing: break on lSize<=0.)
3. **THE "can't run on 1 PC" legend (since 2011)**: client injects anti-tamper garbage bytes
   between packet header and body (`SendMsgAddGarbageValue`, GARBAGE_DATA table); field server
   strips them ONLY for "real clients" — but `CClientField::SetAcceptedClient` classifies by IP,
   and on one host client IP == agent IP (127.0.0.1) → client misclassified as NET_SLOT_AGENT →
   strip skipped → join-identity packet read as garbage (emType garbage) → "Character data failed
   to be processed". Fixed by disabling the scheme symmetrically (GetGarbageMsg returns 0 +
   getOneMsg passes unstripped messages through).
4. **Chat prefixes**: client eats '/' (dxincommand console, also '&'); '!' switches to Alliance
   channel (never reaches field's ChatMsgProc). Only NORMAL chat (letter-led, All tab) reliably
   reaches GLGaeaServer::ChatMsgProc (type=2). Hence plain-word commands.

### Custom mods added (GLGaeaServerMsg.cpp ChatMsgProc, CHAT_TYPE_NORMAL):
- **`getitem <MID> <SID> [count]`** — spawns any item into inventory (replicates
  RequestChargedItem2Inven: GetItem→FindInsrtable→InsertItem→SNETPC_INVEN_INSERT).
- **`maxskills [rank]`** — maxes all class skills (m_ExpSkills insert + SNETPC_REQ_SKILLUP_FB
  per skill + INIT_DATA + passive broadcast).
- Item codes = the IN_MID_SID names in ItemStrTable. Dragon Swrd=0 14, Chu King=0 21,
  Purplish Dragon(15D)=1 120, A Damascus Vest/Pants/Gloves=52 20/55 20/34 20.
- DB cheats: level/statpoints/skillpoints/gold direct UPDATE on RanGame1.dbo.ChaInfo
  (ChaLevel, ChaStRemain, ChaSkillPoint, ChaMoney). Char must be LOGGED OUT first.

### Keys to the kingdom (crypto/formats):
- All encrypted game files (param.ini, ItemStrTable.txt, mapslist.ini, comment.ini...):
  4-byte version header + **AES-256-ECB**, key = "skrkrhtlvdj!@#wpfrmfjgrpgo!@#$qk"
  (hex 736b726b7268746c76646a214023777066726d666a677270676f21402324716b).
  Decrypt: `tail -c +5 file | openssl enc -aes-256-ecb -d -nopad -K <hex>`.
- **.rcc files are plain ZIPs** (glogic.rcc, glogicserver.rcc) — unzip/edit/rezip.
- BGM = plain .ogg in client/sounds/bgm; per-map BGM field inside encrypted mapslist.ini;
  login music = gameword LOBY_BGM → intro.ogg (user swapped m3c.ogg over it).
- CrowStrTable.txt (mob names), moblogic*.bin, .crowsale in glogic.rcc; MobEditor.exe built.

### Build/run loop (the modding workflow):
edit source → commit+push master → GH Actions (~6min) → `gh run download -R T31K/RAN` →
wineserver -k → copy exes into game-client/ (client Game.exe into client/ if client-side change)
→ restart servers → test. Servers auto-connect on Start (Connect Session/Field = reconnect-only).
Server needs mfc140.dll (extracted from MS vc_redist nested CABs, stashed wine/vcredist/).
Source is ISO-8859/CP949 — edit ONLY via latin-1 python scripts, never Write/Edit tools.
CDebugSet::ToLogFile → errlog; console tee `[CONSOLE t]` added; grep needs -a (binary detection).

### Legal note (user asked about monetizing):
Leaked proprietary source (Min Communications) — reskin/sell = infringement; free w/ friends =
tolerated gray zone. Monetizable: the STORY (video/thread/blog), skills, or a from-scratch
spiritual successor. User drafted a tweet crediting Claude Fable.

### Future roadmap the user wants:
- Auto-start servers (no clicking) + one-shot play.command + docker restart policy
- `spawnmob` command + custom cloned boss with custom drops
- Remove [JOINDBG]/[CHATDBG] debug logging once stable; restore real password auth
- Music: remap remaining zones to 2004 tracks (m1/m3c/m6a/m7a/s1) via mapslist.ini
