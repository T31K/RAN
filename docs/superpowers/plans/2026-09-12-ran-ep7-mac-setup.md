# RAN Online EP7 on macOS (No Windows) Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Get RAN Online EP7 (client + full 4-server stack + MSSQL database) running playable on the user's Apple M3 Mac tonight, with zero Windows machines involved.

**Architecture:** Build the Windows binaries from this repo's source on a free GitHub Actions Windows runner. Run MSSQL in Docker (linux/amd64 under Rosetta). Run the 4 server exes and the game client under Wine (free `wine-crossover` build) on macOS, with Wine ODBC DSNs pointing at the Docker database. Client data comes from the official companion GitLab repo.

**Tech Stack:** MSVC v143 / MSBuild (CI only), GitHub Actions `windows-2022`, Docker + `mcr.microsoft.com/mssql/server:2022-latest`, Homebrew `gcenx/wine/wine-crossover` + `winetricks`, `megatools` for MEGA downloads, GitLab client repo `ragezone/ran-online/game-client`.

**Spec:** This plan IS the spec (conversation-derived). Source guide: the RageZone "How to make Ran Offline Here! (EP7)" thread, saved at `~/Downloads/[Guide] How to make Ran Offline Here! (EP7) _ RaGEZONE - MMO Development Forums.html`. The original Windows-XP-based steps from that guide are reproduced in Appendix A; this plan is the macOS translation of it.

## Global Constraints

- **NO Windows machine, VM, or paid service anywhere.** GitHub Actions on a PUBLIC repo (free unlimited minutes), Docker (free), wine-crossover Homebrew cask (free), CrossOver **trial** only as graphics fallback (free 14 days — tell the user before installing it).
- **NEVER use `mcp__claude-in-chrome__*` browser tools** (user's standing rule). All web access via curl/gh/git/megatools.
- **Do not push anything to `yexiuph/RanOnline`** (upstream, not ours). A new public repo under the `T31K` GitHub account is pre-approved by the user for CI builds.
- **Timeboxes are hard gates** (goal is playing TONIGHT). Every task lists its fallback; take the fallback when the timebox expires, leave a `TODO` note in `docs/superpowers/plans/ran-setup-notes.md`, and move on.
- Directory layout is load-bearing: source repo at `/Users/t31k/Projects/RAN/RanOnline`, client data at `/Users/t31k/Projects/RAN/game-client` (the vcxproj `OutDir` is `$(SolutionDir)..\game-client\` — binaries land directly in the client folder).
- Fixed credentials for tonight (localhost-only, fine): SQL `sa` password = `RanDev!2026`. Wine prefix = `~/.wine-ran` (32-bit-capable wow64 prefix).
- All 4 databases: `RanGame1`, `RanUser`, `RanLog`, `RanShop`. Server launch order: **Session → Login → Field → Agent** (per guide).
- `git commit` in the source repo after each config/code change that works; commit messages end with `Co-Authored-By: Claude Fable 5 <noreply@anthropic.com>`.

---

### Task 0: Downloads and tool install (run first, everything else depends on it)

**Files:**
- Create: `/Users/t31k/Projects/RAN/downloads/` (staging dir for MEGA files)

**Interfaces:**
- Produces: extracted `Database backups` (4× `.bak`), `Server files` folder (reference `cfg/*.ini` + prebuilt exes fallback), `glogicserver.rcc`, `rzcheck.exe`, `REditor4.exe`, and installed tools: `megatools`, `wine`, `winetricks`, `unar`.

- [ ] **Step 1: Install tools via Homebrew** (all free)

```bash
brew install megatools unar
brew install --cask --no-quarantine gcenx/wine/wine-crossover
brew install winetricks
wine --version   # expect wine-crossover version string, e.g. wine-24.x
```

If `gcenx/wine/wine-crossover` fails to install on this macOS version, fallback: `brew install --cask --no-quarantine wine-stable` (official Wine build; also free).

- [ ] **Step 2: Download the 5 needed MEGA files** into `/Users/t31k/Projects/RAN/downloads/`

```bash
mkdir -p /Users/t31k/Projects/RAN/downloads && cd /Users/t31k/Projects/RAN/downloads
megadl 'https://mega.nz/file/yK5UnTCL#znvj9lNO6XBeAahfk6RCyc-q1oLfcEB75mzSKZGMFYI'   # Database backups
megadl 'https://mega.nz/file/XOJW3bCR#QJOJYD-8B2OdJQNvQBO5Ry0jTa9IO65GmgL0SE87d94'   # Server files
megadl 'https://mega.nz/file/3aomhZ6I#RjdjVxb3c8cy3bTIEeeykt67GT5t1WPJGgRxDLUnuE0'   # glogicserver.rcc
megadl 'https://mega.nz/file/nPZ0zSpJ#fH2pYRDEE4IYI_d8HkmwqpsgZ78UiOhp3i6abVKNp6w'   # rzcheck
megadl 'https://mega.nz/file/qfgFiILA#D-JPZNdHXEtemfksPC1VpmsyOJ7CHnFF4lJPH8KWoNE'   # REditor4
```

(Two more exist in the guide — Client v789 `https://mega.nz/file/7CBQnIRa#JvvUuMakhvLWhU4dPfB1X1tsNX4Mu1VonQm0h5GzUbI` and Patch v822 `https://mega.nz/file/SagAjRxQ#r8Y55vePyKqu-uYfE4pqMJAuMTEDU4w3k_849n1dvPo`. Do NOT download them now; only if Task 2's GitLab client turns out broken.)

If `megadl` hits a free-transfer quota mid-way, note which files are missing and continue with what arrived; retry the rest later (quota resets) or ask the user to grab them in a browser.

- [ ] **Step 3: Extract everything**

```bash
cd /Users/t31k/Projects/RAN/downloads
for f in *.rar *.zip *.7z; do [ -e "$f" ] && unar -o extracted/ "$f"; done
find extracted -iname "*.bak" -o -iname "*.rcc" -o -iname "rzcheck*" -o -iname "REditor*" | sort
```

Expected: 4 `.bak` files (names ≈ RanGame1/RanUser/RanLog/RanShop), `glogicserver.rcc`, the two tools, and a server-files folder containing `cfg/` with `game01.ini`–`game04.ini` (maybe named `sv_*.ini`/similar — list whatever `cfg` contains) plus prebuilt exes.

- [ ] **Step 4: Record inventory** — write the actual filenames/paths found into `docs/superpowers/plans/ran-setup-notes.md` (later tasks refer to "the .bak files" etc.; pin down real names here). Commit the notes file.

---

### Task 1: CI build of the source (runs in parallel with Tasks 0/2/3 — kick it off early)

**Files:**
- Already drafted (uncommitted): `.github/workflows/build.yml` in the source repo — review it, keep as-is unless broken
- Modify: git remotes of `/Users/t31k/Projects/RAN/RanOnline`

**Interfaces:**
- Produces: GitHub Actions artifact `ran-binaries` containing `game.exe` + server exes (the solution's OutDir contents). Downloaded to `/Users/t31k/Projects/RAN/ci-binaries/`.

**Timebox: 90 minutes of iteration from first push.** Fallback: use the prebuilt exes from the `Server files` download (Task 0) + note that source-tinkering resumes another day.

- [ ] **Step 1: Create the user's public repo and push** (pre-approved)

```bash
cd /Users/t31k/Projects/RAN/RanOnline
git remote rename origin upstream
git add .github/workflows/build.yml
git commit -m "ci: add Windows build workflow

Co-Authored-By: Claude Fable 5 <noreply@anthropic.com>"
gh repo create T31K/RanOnline --public --source . --remote origin --push
```

- [ ] **Step 2: Watch the run**

```bash
gh run list --limit 1
gh run watch --exit-status   # poll; a full build of ~30 C++ projects may take 20–40 min
```

- [ ] **Step 3: If the build fails, iterate (this is expected — budget 2–4 rounds).** Known likely failure classes and fixes:
  - **MFC missing** (`fatal error RC1015`/`afxwin.h not found`): windows-2022 image normally has MFC v143. If not, add before the msbuild step:
    ```yaml
      - name: Ensure MFC
        shell: pwsh
        run: |
          $vs = & "${env:ProgramFiles(x86)}\Microsoft Visual Studio\Installer\vswhere.exe" -latest -property installationPath
          Start-Process -Wait "${env:ProgramFiles(x86)}\Microsoft Visual Studio\Installer\vs_installer.exe" -ArgumentList "modify --installPath `"$vs`" --add Microsoft.VisualStudio.Component.VC.ATLMFC --quiet --norestart"
    ```
  - **`YXLauncher` (C#) fails**: it's optional (client launches with `game.exe /app_run`). Remove it from the build by building the solution with `-p:ExcludeProjects` unsupported — instead do: `dotnet nuget` restore issues → just delete the project from a CI-only solution copy:
    ```yaml
      - name: Drop launcher from solution
        shell: pwsh
        run: dotnet sln RanOnline.sln remove YXLauncher/YXLauncher.csproj
    ```
    (If `dotnet sln` refuses the old csproj, edit `RanOnline.sln` in the repo to remove the YXLauncher project block and commit.)
  - **Codepage warnings C4819** (Korean comments): warnings only — ignore. If they surface as *errors*, append `-p:AdditionalOptions="/utf-8"`? NO — do not do that (it changes narrow-string literals); instead add `-p:ForceImportBeforeCppTargets` is overkill — simply set the runner codepage first: `chcp 949` in a step before msbuild via `shell: cmd`.
  - **Tool projects (`[Tool]__*`) fail but client+servers succeed**: acceptable for tonight. Change the msbuild step to build only what we need, one call per project (quote the bracketed paths):
    ```yaml
        run: |
          msbuild "[Client]__Game/[Client]__Game.vcxproj" -m -p:Configuration=Release -p:Platform=Win32
          msbuild "[Server]__Login/[Server]__Login.vcxproj" -m -p:Configuration=Release -p:Platform=Win32
          msbuild "[Server]__Session/[Server]__Session.vcxproj" -m -p:Configuration=Release -p:Platform=Win32
          msbuild "[Server]__Agent/[Server]__Agent.vcxproj" -m -p:Configuration=Release -p:Platform=Win32
          msbuild "[Server]__Field/[Server]__Field.vcxproj" -m -p:Configuration=Release -p:Platform=Win32
    ```
    (Project references pull the Lib projects in automatically.)
  - Commit each fix with a `ci:` message and re-watch.

- [ ] **Step 4: Download the artifact**

```bash
mkdir -p /Users/t31k/Projects/RAN/ci-binaries
cd /Users/t31k/Projects/RAN/RanOnline
gh run download --name ran-binaries --dir /Users/t31k/Projects/RAN/ci-binaries
ls -la /Users/t31k/Projects/RAN/ci-binaries
```

Expected: `game.exe` (client) plus 4 server executables (names will be like the project names or `RanLoginSvr.exe` style — whatever the build produced; record actual names in the notes file).

- [ ] **Step 5: Verify Win32 PE format**: `file /Users/t31k/Projects/RAN/ci-binaries/*.exe` → "PE32 executable ... Intel 80386". Commit nothing (binaries stay out of git).

---

### Task 2: Client data from GitLab

**Files:**
- Create: `/Users/t31k/Projects/RAN/game-client/` (clone)

**Interfaces:**
- Produces: full client data tree (`data/`, `textures/`, `sounds/`, `cfg/`, DX9 DLLs, `param.ini`, `option.ini`) at the exact path the build outputs into.

- [ ] **Step 1: Clone**

```bash
cd /Users/t31k/Projects/RAN
git clone https://gitlab.com/ragezone/ran-online/game-client.git game-client
du -sh game-client
```

Expected: multi-GB working tree. If the clone is suspiciously tiny (<100 MB), the repo may use LFS: `cd game-client && git lfs install && git lfs pull` (`brew install git-lfs` first). If STILL tiny, fallback: download Client v789 + Patch v822 from the MEGA links in Task 0 Step 2 and extract the installers with `unar` (InstallShield/NSIS archives usually unpack; if not, install them inside the Wine prefix in Task 4: `wine setup.exe`).

- [ ] **Step 2: Overlay pieces**

```bash
# built binaries into the client tree (this mirrors the vcxproj OutDir behavior)
cp /Users/t31k/Projects/RAN/ci-binaries/*.exe /Users/t31k/Projects/RAN/game-client/
cp /Users/t31k/Projects/RAN/ci-binaries/*.dll /Users/t31k/Projects/RAN/game-client/ 2>/dev/null || true
# server logic data
mkdir -p /Users/t31k/Projects/RAN/game-client/data/glogicserver
cp "$(find /Users/t31k/Projects/RAN/downloads -iname glogicserver.rcc | head -1)" /Users/t31k/Projects/RAN/game-client/data/glogicserver/
ls /Users/t31k/Projects/RAN/game-client/data/glogicserver/
```

Note: the GitLab clone may already contain `data/glogicserver/` — if it already has a `glogicserver.rcc`, keep the GitLab one and stash the MEGA one as `.mega-version` (compare sizes, note in notes file).

---

### Task 3: MSSQL in Docker + database restore

**Files:**
- Create: `/Users/t31k/Projects/RAN/db/restore.sql` (generated per .bak)

**Interfaces:**
- Produces: SQL Server listening on `127.0.0.1:1433`, login `sa`/`RanDev!2026`, databases `RanGame1`, `RanUser`, `RanLog`, `RanShop` restored and ONLINE.

**Timebox: 60 minutes.** Fallback: the CI-conversion contingency in Step 5 (adds ~40 min but is near-certain to work).

- [ ] **Step 1: Start SQL Server** (free Developer edition; amd64 image runs via Rosetta — ensure Docker Desktop → Settings → "Use Rosetta for x86_64/amd64 emulation" is on)

```bash
docker run -d --name ran-mssql --platform linux/amd64 \
  -e ACCEPT_EULA=Y -e MSSQL_SA_PASSWORD='RanDev!2026' \
  -p 1433:1433 -v ran-mssql-data:/var/opt/mssql \
  mcr.microsoft.com/mssql/server:2022-latest
sleep 30 && docker logs ran-mssql | tail -5   # expect "SQL Server is now ready for client connections"
```

If the 2022 image crash-loops under emulation, try `mcr.microsoft.com/azure-sql-edge:latest` (ARM-native) with the same env/ports — but prefer 2022 (Edge lacks some RESTORE features).

- [ ] **Step 2: Copy .baks in and inspect one**

```bash
docker exec ran-mssql mkdir -p /var/opt/mssql/backup
for f in $(find /Users/t31k/Projects/RAN/downloads/extracted -iname "*.bak"); do docker cp "$f" ran-mssql:/var/opt/mssql/backup/; done
docker exec ran-mssql /opt/mssql-tools18/bin/sqlcmd -C -S localhost -U sa -P 'RanDev!2026' \
  -Q "RESTORE HEADERONLY FROM DISK='/var/opt/mssql/backup/RanGame1.bak'"
```

(Adjust filename to the real one from Task 0's inventory. If `mssql-tools18` path is missing, try `/opt/mssql-tools/bin/sqlcmd` without `-C`.)

Read the `SoftwareVersionMajor` column: **9 = SQL 2005** (→ Step 5 contingency required), **10+ = 2008+** (→ Step 3 works directly).

- [ ] **Step 3: Restore all four** (if version ≥ 10). For EACH bak: list files, then restore with MOVE:

```bash
DB=RanGame1; BAK=/var/opt/mssql/backup/RanGame1.bak   # repeat for RanUser, RanLog, RanShop
docker exec ran-mssql /opt/mssql-tools18/bin/sqlcmd -C -S localhost -U sa -P 'RanDev!2026' \
  -Q "RESTORE FILELISTONLY FROM DISK='$BAK'"
```

Take each `LogicalName` from the output and build the restore (RanGame1 typically has TWO data files, `RanGame1P_Data` and `RanGame1S_Data` — the guide's rename step; include a MOVE for every row):

```sql
RESTORE DATABASE RanGame1 FROM DISK='/var/opt/mssql/backup/RanGame1.bak' WITH REPLACE,
  MOVE 'RanGame1P_Data' TO '/var/opt/mssql/data/RanGame1P.mdf',
  MOVE 'RanGame1S_Data' TO '/var/opt/mssql/data/RanGame1S.ndf',
  MOVE 'RanGame1_Log'   TO '/var/opt/mssql/data/RanGame1_log.ldf';
```

(Use the actual logical names printed — do not assume these exact strings.)

- [ ] **Step 4: Verify**

```bash
docker exec ran-mssql /opt/mssql-tools18/bin/sqlcmd -C -S localhost -U sa -P 'RanDev!2026' \
  -Q "SELECT name, state_desc, compatibility_level FROM sys.databases WHERE name LIKE 'Ran%'"
```

Expected: 4 rows, all ONLINE. Then peek at the user table for Task 6: `sqlcmd ... -d RanUser -Q "SELECT TOP 5 TABLE_NAME FROM INFORMATION_SCHEMA.TABLES"` and record table names in the notes file.

- [ ] **Step 5 (CONTINGENCY — only if HEADERONLY said version 9 / restore errors 3169):** Convert 2005-format baks → 2022-format via a one-off GitHub Actions job (SQL Server 2014 can restore 2005 baks; 2022 can restore 2014 baks). Add `.github/workflows/convert-db.yml` to the repo:

```yaml
name: Convert DB backups
on: workflow_dispatch
jobs:
  convert:
    runs-on: windows-2022
    steps:
      - name: Install SQL Server 2014 Express
        shell: pwsh
        run: |
          Invoke-WebRequest -Uri "https://download.microsoft.com/download/E/A/E/EAE6F7FC-767A-4038-A954-49B8B05D04EB/ExpressAndTools%2064BIT/SQLEXPRWT_x64_ENU.exe" -OutFile sql2014.exe
          Start-Process -Wait .\sql2014.exe -ArgumentList "/Q /ACTION=Install /FEATURES=SQLEngine /INSTANCENAME=SQL14 /SECURITYMODE=SQL /SAPWD=RanDev!2026 /SQLSVCACCOUNT=`"NT AUTHORITY\SYSTEM`" /IACCEPTSQLSERVERLICENSETERMS /TCPENABLED=1"
      - name: Fetch baks from workflow inputs
        shell: pwsh
        run: |
          # The runner can't reach the local Mac. Upload the baks first:
          # locally run:  gh release create baks-temp downloads/extracted/*.bak --repo T31K/RanOnline --notes temp
          gh release download baks-temp --repo T31K/RanOnline --dir baks
        env:
          GH_TOKEN: ${{ github.token }}
      - name: Restore on 2014 and re-backup
        shell: pwsh
        run: |
          $s = ".\SQL14"
          Get-ChildItem baks\*.bak | ForEach-Object {
            $db = $_.BaseName
            $list = sqlcmd -S $s -U sa -P 'RanDev!2026' -Q "RESTORE FILELISTONLY FROM DISK='$($_.FullName)'" -h -1 -W
            # build MOVE clauses generically
            $moves = (sqlcmd -S $s -U sa -P 'RanDev!2026' -h -1 -W -Q "SET NOCOUNT ON; RESTORE FILELISTONLY FROM DISK='$($_.FullName)'" | ForEach-Object { ($_ -split '\s{2,}')[0] } | Where-Object { $_ } | ForEach-Object -Begin {$i=0} -Process { $i++; "MOVE '$_' TO 'C:\temp\$db$i.dat'" }) -join ', '
            New-Item -ItemType Directory -Force C:\temp | Out-Null
            sqlcmd -S $s -U sa -P 'RanDev!2026' -Q "RESTORE DATABASE [$db] FROM DISK='$($_.FullName)' WITH REPLACE, $moves"
            sqlcmd -S $s -U sa -P 'RanDev!2026' -Q "BACKUP DATABASE [$db] TO DISK='C:\temp\$db-2014.bak'"
          }
      - uses: actions/upload-artifact@v4
        with: { name: converted-baks, path: C:\temp\*-2014.bak }
```

Locally: `gh release create baks-temp <baks> --repo T31K/RanOnline --notes temp` → `gh workflow run convert-db.yml` → `gh run watch` → `gh run download --name converted-baks` → **`gh release delete baks-temp --yes` immediately after** (don't leave DB dumps published) → redo Steps 2–4 with the converted baks. If the PowerShell MOVE-clause parsing misbehaves, hand-write the MOVE clauses per database from the FILELISTONLY output — there are only 4 databases.

---

### Task 4: Wine prefix + ODBC bridge

**Files:**
- Create: `~/.wine-ran` (prefix), `/Users/t31k/Projects/RAN/db/ran-dsn.reg`

**Interfaces:**
- Consumes: SQL Server on `127.0.0.1:1433` (Task 3).
- Produces: Wine prefix where `wine odbcad32` shows 4 User DSNs — `RanGame1`, `RanUser`, `RanLog`, `RanShop` — each test-connecting successfully.

**Timebox: 60 minutes on the ODBC bridge.** Fallback order: (a) `native_mdac`, (b) Microsoft ODBC Driver 11 x86 MSI, (c) msodbcsql 13.

- [ ] **Step 1: Create prefix and base runtimes**

```bash
export WINEPREFIX=~/.wine-ran
wineboot -i
winetricks -q vcrun2022        # v143 CRT for our built exes
winetricks -q native_mdac      # classic "SQL Server" ODBC driver (sqlsrv32)
```

- [ ] **Step 2: Register the 4 DSNs.** Write `/Users/t31k/Projects/RAN/db/ran-dsn.reg`:

```reg
REGEDIT4

[HKEY_CURRENT_USER\Software\ODBC\ODBC.INI\ODBC Data Sources]
"RanGame1"="SQL Server"
"RanUser"="SQL Server"
"RanLog"="SQL Server"
"RanShop"="SQL Server"

[HKEY_CURRENT_USER\Software\ODBC\ODBC.INI\RanGame1]
"Driver"="C:\\windows\\system32\\sqlsrv32.dll"
"Server"="127.0.0.1,1433"
"Database"="RanGame1"

[HKEY_CURRENT_USER\Software\ODBC\ODBC.INI\RanUser]
"Driver"="C:\\windows\\system32\\sqlsrv32.dll"
"Server"="127.0.0.1,1433"
"Database"="RanUser"

[HKEY_CURRENT_USER\Software\ODBC\ODBC.INI\RanLog]
"Driver"="C:\\windows\\system32\\sqlsrv32.dll"
"Server"="127.0.0.1,1433"
"Database"="RanLog"

[HKEY_CURRENT_USER\Software\ODBC\ODBC.INI\RanShop]
"Driver"="C:\\windows\\system32\\sqlsrv32.dll"
"Server"="127.0.0.1,1433"
"Database"="RanShop"
```

```bash
WINEPREFIX=~/.wine-ran wine regedit /Users/t31k/Projects/RAN/db/ran-dsn.reg
```

- [ ] **Step 3: Test a DSN connection.** Quick probe via a 5-line test — create `/Users/t31k/Projects/RAN/db/odbctest.vbs`:

```vbs
Set c = CreateObject("ADODB.Connection")
c.Open "DSN=RanUser;UID=sa;PWD=RanDev!2026"
Set r = c.Execute("SELECT COUNT(*) FROM INFORMATION_SCHEMA.TABLES")
WScript.Echo "OK tables=" & r.Fields(0).Value
c.Close
```

```bash
WINEPREFIX=~/.wine-ran wine cscript /Users/t31k/Projects/RAN/db/odbctest.vbs
```

Expected: `OK tables=<n>`. **Known risk:** the old `sqlsrv32` driver speaks ancient TDS; SQL Server 2022 may refuse it ("client version not supported" / pre-login handshake errors). If so:
  - Fallback (b): download Microsoft ODBC Driver 11 for SQL Server **x86** MSI: `curl -LO https://download.microsoft.com/download/5/7/2/57249A3A-19D6-4901-ACCE-80924ABEB267/ENU/x86/msodbcsql.msi` then `WINEPREFIX=~/.wine-ran wine msiexec /i msodbcsql.msi IACCEPTMSODBCSQLLICENSETERMS=YES /qn`, and change every `"Driver"` line in the .reg to the installed driver (find it: `WINEPREFIX=~/.wine-ran wine reg query "HKLM\\Software\\ODBC\\ODBCINST.INI\\ODBC Driver 11 for SQL Server" /v Driver`), plus `"ODBC Data Sources"` values to `"ODBC Driver 11 for SQL Server"`. Re-run regedit + test.
  - Fallback (c): same with ODBC Driver 13.1 x86 if 11 won't install.
  - Record which driver won in the notes file — Task 5's ini may need the matching driver name.

---

### Task 5: Server + client configuration

**Files:**
- Modify: `/Users/t31k/Projects/RAN/game-client/cfg/*.ini` (or wherever the server inis live — see Step 1)
- Modify: `/Users/t31k/Projects/RAN/game-client/param.ini`

**Interfaces:**
- Consumes: DSN names from Task 4; server exe names from Task 1/notes.
- Produces: configs pointing everything at `127.0.0.1`, DB auth `sa`/`RanDev!2026`.

- [ ] **Step 1: Learn the exact ini keys FROM THE SOURCE** (do not guess):

```bash
cd /Users/t31k/Projects/RAN/RanOnline
grep -rn "GetPrivateProfileString\|GetPrivateProfileInt" "[Server]__Login/Sources" "[Server]__Session/Sources" "[Server]__Agent/Sources" "[Server]__Field/Sources" --include=*.cpp -l
# then read the hits to find: which .ini filename each server loads, and the key names for IP/port/ODBC DSN/UID/PWD
```

Also inspect the reference inis that shipped in the `Server files` download (`downloads/extracted/.../cfg/`). The guide says: "go to cfg then update all IP addresses on game01-04.ini … also update the database settings".

- [ ] **Step 2: Write the inis.** Copy the reference `cfg` inis into the location the source expects (likely `game-client/cfg/` since servers run from the client-data folder — confirm against the source's ini path from Step 1). Set every IP field to `127.0.0.1`, every ODBC/DSN field to the Task 4 DSN names, UID `sa`, PWD `RanDev!2026`.

- [ ] **Step 3: Client `param.ini` → LoginAdress.** First check whether our built client reads it encrypted:

```bash
grep -rn "param.ini\|LoginAdress\|LoginAddress" "[Lib]__RanClient/Sources" "[Client]__Game/Sources" --include=*.cpp --include=*.h | head -20
```

  - If the loader has a plaintext path (or an encryption flag we control), edit `game-client/param.ini` as plain text, set `LoginAdress` to `127.0.0.1`, rebuild client via CI if a source flag change is needed.
  - Otherwise follow the guide's tool path under Wine: `WINEPREFIX=~/.wine-ran wine downloads/extracted/.../REditor4.exe` (open `param.ini`, answer Yes to decrypt, edit `LoginAdress`, save unencrypted), then encrypt with `WINEPREFIX=~/.wine-ran wine .../rzcheck.exe h` (run bare `rzcheck.exe h` first — it prints usage).

- [ ] **Step 4: Commit** any source changes (`git add -A && git commit -m "feat: local-server config support ..."` with attribution line); the `game-client` tree is not ours to push — leave its changes uncommitted or commit locally only, never push to the GitLab origin.

---

### Task 6: Launch, account, play

**Files:**
- Create: `/Users/t31k/Projects/RAN/start-servers.sh`

**Interfaces:**
- Consumes: everything above.
- Produces: a running game you can log into.

- [ ] **Step 1: Launch script** — `/Users/t31k/Projects/RAN/start-servers.sh` (adjust exe names to the actual ones recorded in notes):

```bash
#!/bin/zsh
export WINEPREFIX=~/.wine-ran
cd /Users/t31k/Projects/RAN/game-client
# Guide's order: Session -> Login -> Field -> Agent, a few seconds apart
wine SessionServer.exe &  sleep 8
wine LoginServer.exe   &  sleep 8
wine FieldServer.exe   &  sleep 8
wine AgentServer.exe   &
wait
```

`chmod +x` it, run it, and watch output. Each server should log a successful DB connect and bind its port. Verify listeners: `lsof -iTCP -sTCP:LISTEN -P | grep wine`.

Debug loop for crashes: re-run the failing one alone with `WINEDEBUG=+odbc,+seh wine <exe> 2>&1 | tail -50`. DB errors → Task 4/5 config; missing-DLL errors → copy the named DLL from the `Server files` prebuilt folder into `game-client/`.

- [ ] **Step 2: Create a game account.** Find the login query first:

```bash
grep -rn "SELECT\|EXEC\|sp_" "[Server]__Login/Sources" --include=*.cpp | grep -i "user\|pwd\|pass" | head
```

That shows the table/proc and password hashing (commonly plain MD5 in EP7). Then insert (adjust table/columns to what the source actually reads):

```bash
docker exec ran-mssql /opt/mssql-tools18/bin/sqlcmd -C -S localhost -U sa -P 'RanDev!2026' -d RanUser \
  -Q "INSERT INTO <UserTable> (UserID, Passwd) VALUES ('t31k', '<hash-or-plain-per-source>')"
```

- [ ] **Step 3: Launch the client**

```bash
cd /Users/t31k/Projects/RAN/game-client
WINEPREFIX=~/.wine-ran wine game.exe /app_run
```

Expected: login screen renders. Log in with the Step 2 account, create a character, enter the world.

Graphics fallback ladder if the window is black/crashes in D3D: 1) `winetricks -q d3dx9` (native d3dx9 DLLs — though the client folder already ships `d3dx9_26–30.dll`); 2) toggle `winetricks renderer=gl` vs default; 3) install the **CrossOver 14-day trial** (free — but TELL THE USER it becomes paid after trial) and run the same client folder in a CrossOver 32-bit bottle, which has the most polished macOS D3D path.

- [ ] **Step 4: Victory lap** — write final state (what runs, exact commands, quirks) into `docs/superpowers/plans/ran-setup-notes.md`, commit, and tell the user how to start everything with 2 commands (`docker start ran-mssql` + `start-servers.sh`) and launch the game.

---

## Appendix A: Original guide steps (Windows XP reference)

The RageZone guide this plan translates. Useful when a server misbehaves — it defines the *intended* end-state:

1. Windows XP SP3 VM, .NET 2, MSSQL 2005 Express (Mixed Mode, sa password, TCP/IP remote connections enabled), SSMS Express.
2. Install client v789 + patch v822. Create server and client folders.
3. Copy client `data` folder to server folder. In `cfg`, update all IPs in `game01-04.ini` to the server IP + database settings. Copy `game.exe` from server files into the client, replacing the PH one. Create `data/glogicserver/` in the server folder and put `glogicserver.rcc` in it.
4. Restore DBs: copy .baks to the MSSQL Backup dir; create empty `RanGame1`, `RanUser`, `RanLog`, `RanShop`; for RanGame1, rename data file `RanGame1_Data`→`RanGame1P_Data` and add `RanGame1S_Data`; restore each from device with Overwrite.
5. ODBC: User DSNs named `RanGame1`, `RanLog`, `RanShop`, `RanUser` via SQL Native Client to `RANSERVER\SQLEXPRESS`, each defaulting to its DB.
6. `param.ini`: open with REditor4, decrypt, set `LoginAdress` to the server address, save unencrypted, re-encrypt with `rzcheck` (`rzcheck.exe h` for usage).
7. Launch order: Session, Login, Field, Agent. Client: shortcut to `game.exe` with `/app_run`.
