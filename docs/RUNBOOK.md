# RAN Online EP7 — Runbook (problems solved & how to diagnose)

Playbook of every problem hit getting RAN EP7 playable on an M3 Mac (no Windows) with servers
on a VPS (MariaDB). Each entry: **Symptom → Root cause → Fix → Diagnose next time.**

Stack: client `RanOdyssey.app` (Wine+MoltenVK on Mac) → VPS `167.233.23.97` (Hetzner) →
Docker `ran-servers` (Wine+Xvfb, 4 MFC servers) + `ran-mariadb` (MariaDB 11.4).
Local `ran-mssql` docker is the original data source of truth. Build via GitHub Actions
(`T31K/RAN`, windows-2022 + MSVC), ~6min, artifact `ran-binaries`.

---

## Key locations & commands (memorize these — most diagnosis starts here)

```bash
# --- VPS access ---
ssh root@167.233.23.97

# --- Server error/console log (ALL 4 servers share one file per restart-minute) ---
#   /opt/ran/game-client/RanOnline/errlog/log.<YYYYMDDHMM>.txt   (skip the ._ AppleDouble twins)
f=$(ls -t /opt/ran/game-client/RanOnline/errlog/log.*.txt | grep -v '/\._' | head -1)
grep -a "NativeError\|JOINDBG\|ERROR" "$f" | tail -40      # DB errors + join trace

# --- MariaDB on the VPS ---
docker exec ran-mariadb mariadb -usa   -pRanDev\!2026 RanGame1 -e "SELECT ...;"   # app user
docker exec ran-mariadb mariadb -uroot -pRanDev\!2026 -e "SET GLOBAL general_log=1;"  # needs root

# --- See EXACTLY what queries the server runs (definitive DB diagnosis) ---
docker exec ran-mariadb mariadb -uroot -pRanDev\!2026 -e \
  "SET GLOBAL log_output='TABLE'; SET GLOBAL general_log=1; TRUNCATE mysql.general_log;"
#   ...reproduce...  then:
docker exec ran-mariadb mariadb -uroot -pRanDev\!2026 -N -e \
  "SELECT argument FROM mysql.general_log WHERE command_type='Query' ORDER BY event_time;"

# --- Local MSSQL (data source of truth) ---
docker exec ran-mssql /opt/mssql-tools18/bin/sqlcmd -S localhost -U sa -P 'RanDev!2026' -C -No -d RanGame1 -Q "..."

# --- Rebuild + redeploy servers after a C++ change ---
git push origin master                       # triggers CI
gh run list --repo T31K/RAN --limit 1        # get run id
deploy/redeploy.sh <run-id>                  # downloads ran-binaries, scp exes, docker restart ran-servers
```

The MFC servers log to their own `errlog/` file, NOT stdout — `/opt/ran/logs/*.log` stay empty.
The binaries carry `[JOINDBG]` instrumentation for the character-join path — grep it.

---

## 1. Movement "[1]Movement failed!" — the "1 PC" bug
- **Symptom:** could log in and enter world, but walking/traversing between maps failed.
- **Root cause:** client and all 4 servers ran on one machine (one IP). The field server's
  agent-slot check mislabeled the player's socket as the agent connection (IP collision), so the
  clientID translation was skipped and the wrong client got the move packet.
- **Fix:** `[Lib]__NetServer/Sources/s_CFieldServerMsg.cpp` — compare against the specific agent slot
  (`if (dwClient != m_dwAgentSlot)`) instead of `IsAgentSlot(dwClient)` (commit d042e81).
- **Diagnose next time:** add `[MOVEDBG]` around the move handler; if clientIDs don't match
  (103 vs 104) on a shared-IP setup, it's this. On the VPS (separate IPs) it doesn't recur.

## 2. Client dials 127.0.0.1 — "Internet Connection Error!" (can't click anything)
- **Symptom:** server-select screen shows a connection error before you can click; client isn't
  reaching the VPS.
- **Root cause:** the client reads `LoginAddress` from the **AES-encrypted `param.ini`** `[SERVER SET]`
  section (RANPARAM.cpp:1054), NOT the plaintext `param.cfg`. It was still `127.0.0.1`.
- **Fix:** decrypt param.ini, set `LoginAddress = 167.233.23.97` and `nChinaRegion = 8`
  (8 = MAX_CHINA_REGION = disable the China_Region override), re-encrypt.
  ```bash
  KEY=736b726b7268746c76646a214023777066726d666a677270676f21402324716b
  tail -c +5 param.ini | openssl enc -aes-256-ecb -d -nopad -K $KEY   # decrypt (4-byte header + AES-256-ECB)
  ```
- **Diagnose next time:** if the client won't connect, decrypt param.ini and check `LoginAddress`
  FIRST — the plaintext param.cfg is a decoy.

## 3. MSSQL → MariaDB migration is THREE parts (missing any = broken)
- **Symptom (part 3 missed):** login works but character select is empty though "N cards left" shows.
- **Root cause:** migration did schema + procs but **not the DATA**. `ChaInfo` was empty.
- **Fix:** all three —
  1. Schema: `migration/convert-schema.py` (107 tables).
  2. Procs (61) + views (13): `migration/mariadb/*.sql`.
  3. **Data seed:** binary-safe export (blobs→`0x` hex via
     `CONVERT(VARCHAR(MAX),CONVERT(VARBINARY(MAX),col),1)`, empty blob→`''`, dates style 121);
     load with `SET sql_mode='NO_BACKSLASH_ESCAPES'` + `FOREIGN_KEY_CHECKS=0`. Gameplay tables only
     (UserInfo, ChaInfo, UserInven, VehicleInfo, PetInfo, PetInven, SkillTableRef, ShopItemMap) —
     skip logs. After load: `UPDATE UserInfo SET UserLoginState=0; UPDATE ChaInfo SET ChaOnline=0;`
     to clear stale online flags.
- **Diagnose next time:** `SELECT COUNT(*) FROM ChaInfo` on the VPS. Empty ⇒ data never seeded.
  `image` columns can't `CONVERT(...,1)` directly — cast to `VARBINARY(MAX)` first.

## 4. ODBC bridge (game ↔ MariaDB) — 32-bit driver
- **Symptom:** `load_odbc failed to open library libodbc.so.2`.
- **Root cause:** the servers are 32-bit; Wine needs the **i386** ODBC stack.
- **Fix:** `odbc-mariadb:i386` + `libodbc2:i386` (amd64 odbc-mariadb conflicts, so i386-only),
  `WINEDLLOVERRIDES=odbc32=b` (Wine builtin odbc32), DSN `OPTION=2` (FLAG_FOUND_ROWS = MSSQL
  `@@ROWCOUNT` matched-rows semantics). Driver `/usr/lib/i386-linux-gnu/odbc/libmaodbc.so`.
- **Diagnose next time:** build the `ranprobe.exe` mingw probe (SQLConnect → SELECT COUNT). If the
  64-bit probe works but 32-bit fails, it's a missing i386 lib.

## 5. Login fail: `NativeError:1064 ... 'Exec sp_Extreme 1'`
- **Symptom:** "System error caused log in fail" right after connecting.
- **Root cause:** inline MSSQL `Exec ProcName args` syntax — MariaDB rejects it (needs `CALL`). My
  earlier T-SQL scan searched NOLOCK/UPDLOCK/TOP but not `Exec `. Also `sp_Extreme` was never ported.
- **Fix:** inline each **single-statement** proc as direct SQL (commit ebce62a). These callers read
  the result with a fetch loop that never calls `SQLMoreResults`, so a `CALL` (which appends a
  trailing status result) would poison the pooled connection — inlining avoids that.
  - `sp_Extreme` ×6 → `SELECT 0 AS ExtremeM, 0 AS ExtremeW`
  - `sp_SelectVehicle` → inline SELECT; `sp_UserAttendLog` → safe 0-row (dumped body was a stub);
    `sp_InsertUserLastInfo` → direct INSERT. ChaSave's two are dead code (`#if TW_PARAM/_RELEASED/HK_PARAM`).
- **Diagnose next time:** `grep -rn '"Exec \|"EXEC ' [Lib]__NetServer/Sources`. `{call proc(...)}`
  (ODBC escape) is fine; bare `Exec`/`EXEC` is not.

## 6. Other T-SQL that breaks MariaDB at runtime
- `WITH (NOLOCK)` / `WITH (UPDLOCK)` inline hints → 1064. Removed all (commit 552ca8b/d91edd3).
- `TOP N` → `LIMIT N`; `SCOPE_IDENTITY()`/`@@IDENTITY` → `LAST_INSERT_ID()`; `getdate()` → `NOW()`.
- Views not migrated: `NativeError:1146 viewGuildInfo doesn't exist` → ported 13 views
  (strip `dbo.`/brackets, drop `TOP (100) PERCENT`).
- (Historical, MSSQL/FreeTDS era only) `user_verify` output param needed a trailing `SET NOCOUNT OFF`
  or FreeTDS returned `SQL_NO_DATA` and the agent treated it as DB_ERROR. Gone with MariaDB.

## 7. Character select empty after seeding
- **Symptom:** seeded characters, but the panel is still empty on the screen already open.
- **Root cause:** the client **cached** the pre-seed empty list. Also note the enumerator filters
  `WHERE UserNum=? AND SGNum=? AND ChaDeleted=0` — characters must match the server's SGNum
  (defaults to 0 when `param.cfg` is absent) and be on the right UserNum.
- **Fix:** fully close and reopen the client to re-fetch the list. Verify the account→UserNum and
  the char's SGNum/ChaDeleted with `general_log` if still empty.
- **Diagnose next time:** run the enumerator by hand:
  `SELECT ChaNum FROM ChaInfo WHERE UserNum=<n> AND SGNum=0 AND ChaDeleted=0;`

## 8. "Stuck" on the loading screen (world load)
- **Symptom:** select character → Start → loading bar sits for minutes.
- **Root cause:** NOT a bug. Server-side the join fully succeeds (`[JOINDBG] Gaea::CreatePC SUCCESS`,
  field shows `Character 1`). The client is doing the **first-time map load under Wine+MoltenVK** —
  compiling Metal shaders and generating texture caches (`~/Library/Application Support/RanOdyssey/game/cache/*.dds`).
  First load of a map is slow; later loads are fast (cached).
- **Fix:** wait 5+ minutes on the FIRST load; don't close it. Subsequent loads are quick.
- **Diagnose next time:** (a) errlog `[JOINDBG]` reaching `MsgGameJoin` + `Character 1` = server done;
  (b) watch `cache/*.dds` count growing = client actively loading; if it flatlines for minutes AND
  server shows the char dropped, then it's a real hang — investigate the specific map/asset.

---

## 9. Input dead after Cmd+Tab (mouse clicks / keyboard stop registering)
- **Symptom:** Cmd+Tab out of the game and back → mouse clicks and keyboard do nothing; used to require a relaunch.
- **Root cause:** NOT a pure Wine bug. All input flows through `DxInputDevice` (buffered DInput8), which was
  re-enabled only by `WM_NCACTIVATE` — a message winemac.drv never sends to the borderless `WS_POPUP` game
  window on Cmd+Tab-back. macOS reactivation arrives as `WM_ACTIVATEAPP`, whose handler was a no-op, so
  `m_bActive` stayed FALSE and `ProcessKeyState()` early-returned forever.
- **Fix:** FIXED in build `b1e3555` (2026-09-18). `WM_ACTIVATEAPP` now reacquires DirectInput, plus a
  per-frame self-heal in `ProcessKeyState()` that reacquires whenever `GetForegroundWindow()==m_hWnd` but
  input is inactive. Shipped to `RanOdyssey.app` via `bump-release.command`.
- **Diagnose next time:** if it recurs, run a DEBUG build (release `/app_run` doesn't init CDebugSet's log
  file) and `grep INPUTDBG` the newest `log.*.txt` — `foreground=1 active=0` with `Acquire hr=0x8007001e`
  (OTHERAPPHASPRIO) persisting means Wine isn't restoring window foreground (the harder variant; see the
  fix plan's Task 4 fallbacks: windowed mode / virtual desktop / `DISCL_BACKGROUND`).

---

## 10. External monitor blacks out for a few seconds when the game closes (clamshell mode)
- **Symptom:** quit/kill the client while the MacBook lid is closed (external monitor only) → monitor goes
  black for several seconds (looks like the Mac died; audio keeps playing). Happens ~2/3 of exits.
- **Root cause:** NOT the game, NOT Wine — a macOS 26 (Tahoe, verified on 26.6.2) bug. ~3s after the game
  process dies, macOS fires a **phantom "lid opened" event** (`loginwindow: clamshellStateChanged closed=0`
  with the lid untouched), hotplugs the built-in display back in, marks it main, then flips back ~1s later.
  Two display-topology reconfigs back-to-back force the external monitor to re-sync → black. Verified
  2026-09-20 with `log stream` (WindowServer/powerd/loginwindow): 5 of 8 game exits fired the event, zero
  baseline events otherwise; wine notepad exit never triggers it (needs the GPU-heavy game).
- **Ruled out (all tested):** `Mac Driver\CaptureDisplaysForFullscreen=n` (key isn't even in this
  wswine.bundle's winemac binary), window focus at exit, graceful vs SIGKILL exit, keeping the wine
  session alive past game exit (sentinel process — passed 2 lucky runs, failed the retest; reverted).
- **Do:** nothing — it recovers by itself in seconds. To avoid it entirely: open the lid before quitting.
  Re-test after each macOS update (Tahoe clamshell handling is broadly reported broken).
- **Diagnose next time:** `/usr/bin/log show --last 5m --predicate 'process == "loginwindow"' | grep clamshellStateChanged`
  right after an exit — phantom `closed=0` with the lid closed confirms it's still this bug. (Note: `log`
  must be invoked as `/usr/bin/log` in zsh — `log` alone hits the zsh builtin.)

---

## Quick decision tree
- Input dead after Cmd+Tab → **#9** (fixed in build b1e3555; update the app if on an older build).
- Monitor blacks out right after quitting the game (lid closed) → **#10** (macOS Tahoe bug; wait it out).
- Can't click / connection error → **#2** (decrypt param.ini, check LoginAddress).
- "System error caused log in fail" → errlog `NativeError` → **#5/#6** (T-SQL/proc syntax) or
  (historical) user_verify.
- Login OK, no characters, "N cards left" → **#3** (data seeded?) then **#7** (client cache / SGNum).
- In-world but can't move → **#1** (1-PC, shared IP only).
- Stuck loading → **#8** (be patient on first load; confirm via JOINDBG + cache growth).
