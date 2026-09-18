# Cmd+Tab Input-Loss Fix Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Make RanOdyssey.app keep accepting mouse/keyboard input after the user Cmd+Tabs away and back, instead of requiring a relaunch.

**Architecture:** The game's DirectInput layer is re-enabled only by `WM_NCACTIVATE` — a message Wine's Mac driver does not deliver to the game's borderless fullscreen window on re-activation. Fix in our own C++ source (we build Game.exe from source via GH Actions): re-enable input from `WM_ACTIVATEAPP` as well, and add a per-frame self-heal that reacquires DirectInput whenever the window is foreground but input is marked inactive. Ship one instrumented build that both fixes AND logs the mechanism, so a single test run either confirms the fix or produces the evidence for the fallback.

**Tech Stack:** C++/MFC (VS2022 Win32 via GitHub Actions CI on `T31K/RAN` master), DirectInput8, Wine 10 winemac.drv on macOS, edits applied ONLY via latin-1 Python scripts (source is CP949).

**Spec:** This document, section "Root-cause analysis" below (investigated 2026-09-18 by reading the source; no external spec).

## Global Constraints

- **NEVER edit `.cpp`/`.h` files with the Write/Edit tools.** The source is CP949/ISO-8859 encoded; Write/Edit corrupt the Korean bytes. All edits go through Python scripts using `encoding='latin-1'` for read AND write (established repo rule, see memory `ran-project-state`).
- **`grep` silently returns nothing on some of these files on this Mac** (encoding issue). Use latin-1 Python for all searching/verifying of C++ sources, not grep.
- Build = push to `T31K/RAN` master → GitHub Actions (~6 min) → `update.command` (or `update.command --no-launch`) downloads artifacts and hot-swaps `Game.exe` into `client/`. There is no local Windows build.
- Client-only change: only `Game.exe` changes. Servers and DB are untouched. Do NOT redeploy the VPS.
- Test machine: this M3 Mac. Client runs under Wine 10 (`~/.wine-ran10-client` prefix, or the installed `/Applications/RanOdyssey.app`). Do NOT use the wine-11 `WineStaging.app` (already tried 2026-09-13; crashes RAN, doesn't fix this).
- Verification is manual+log-based (OS focus events can't be unit-tested). The "failing test" is the instrumented reproduction procedure in Task 2; the fix passes when that same procedure shows working input and the expected `[INPUTDBG]` log lines.
- Keep the `[INPUTDBG]` instrumentation in the shipped build. It is rate-limited (logs only on state *transitions*), matches the existing `[JOINDBG]` pattern in this repo, and is the diagnostics we'll want when a friend's Mac misbehaves.

---

## Root-cause analysis (already done — do NOT re-derive; verify with Task 2 logs)

Symptom: Cmd+Tab away from the game, Cmd+Tab back → mouse clicks and keyboard dead until relaunch. Known since 2026-09-13; previously misattributed to a pure Wine bug (Sikarugir #237) after a wine-11 upgrade attempt failed.

The actual chain, from source:

1. `[Client]__Game/Sources/BasicWndD3d.cpp:693` — `CBasicWnd::OnNcActivate(BOOL bActive)` is the **only** caller of `DxInputDevice::GetInstance().OnActivate(bActive)` on focus changes. (The only other caller in the whole repo is `DxInputDevice::InitDeviceObjects`, once at startup, with TRUE.)
2. `[Lib]__Engine/Sources/DxTools/DxInputDevice.cpp:704` — `OnActivate(FALSE)` does `Unacquire()` on both the keyboard and mouse DirectInput devices and sets `m_bActive = FALSE`. `OnActivate(TRUE)` re-`Acquire()`s and sets it TRUE.
3. `DxInputDevice.cpp:738` — `ProcessKeyState()` (called every frame from `FrameMove`) starts with `if ( !m_bActive ) return FALSE;`. While `m_bActive` is FALSE, **no keyboard or mouse events are ever read again**. Both devices use `DISCL_NONEXCLUSIVE|DISCL_FOREGROUND` buffered mode; ALL game input (clicks included) flows through this class.
4. The game window is created borderless: `WS_POPUP | WS_VISIBLE` fullscreen (`BasicWnd.cpp:78,137-146`). It has no non-client area. Under Wine's Mac driver, Cmd+Tab-out delivers deactivation (input dies — correct behavior so far), but Cmd+Tab-back does **not** deliver `WM_NCACTIVATE(TRUE)` to this borderless window. macOS app re-activation surfaces as app-level activation (`WM_ACTIVATEAPP`), and the game's `WM_ACTIVATEAPP` handler is a **no-op** — `BasicWnd.cpp:292-299` has the reactivation call commented out: `//m_pApp->SetActive(bActive);` and never touches `DxInputDevice`.
5. Result: `m_bActive` stays FALSE forever → input dead until relaunch.

Secondary suspect (checked by the same instrumentation): even when `m_bActive` is TRUE, per-frame `Acquire()` returns `DIERR_OTHERAPPHASPRIO` while Wine's foreground bookkeeping says another window is foreground. If Wine never re-marks the game window as foreground after Cmd+Tab (the true "wine bug" variant), reacquire fails no matter who calls `OnActivate(TRUE)`. The self-heal + logging distinguishes this (outcome C in Task 2), and Task 4 covers it.

Files involved:
- Modify: `[Lib]__Engine/Sources/DxTools/DxInputDevice.cpp` (self-heal + transition logging)
- Modify: `[Client]__Game/Sources/BasicWnd.cpp` (WM_ACTIVATEAPP → input reactivation + include)
- Modify: `[Client]__Game/Sources/BasicWndD3d.cpp` (logging in OnNcActivate / OnActivate)
- No header changes. No new files in the Windows build.
- Create: `scripts/patch-input-focus.py` (the latin-1 edit script — lives in repo for review/re-run)

---

### Task 1: Instrumented fix build

**Files:**
- Create: `scripts/patch-input-focus.py`
- Modify (via that script): `[Lib]__Engine/Sources/DxTools/DxInputDevice.cpp`, `[Client]__Game/Sources/BasicWnd.cpp`, `[Client]__Game/Sources/BasicWndD3d.cpp`

**Interfaces:**
- Consumes: existing `DxInputDevice::OnActivate(BOOL)` (public, `DxInputDevice.h:174`), `CDebugSet::ToLogFile(const char*, ...)` (available via each project's pch; writes to the client's `log.*.txt`).
- Produces: `[INPUTDBG]`-prefixed log lines that Task 2 greps for; no new symbols used by later tasks.

- [ ] **Step 1: Write the edit script**

Save exactly this as `scripts/patch-input-focus.py`. It is idempotent (skips edits already applied) and asserts every anchor is found exactly once — if an assert fires, STOP and re-read the target file with latin-1 Python; do not improvise anchors.

```python
#!/usr/bin/env python3
# -*- coding: utf-8 -*-
"""Fix Cmd+Tab input loss: reactivate DirectInput on WM_ACTIVATEAPP + per-frame
self-heal. CP949 sources -> latin-1 round-trip, ASCII-only anchors and insertions."""
import io, sys

def edit(path, pairs):
    src = io.open(path, encoding='latin-1', newline='').read()
    for old, new in pairs:
        if new in src and old not in src:
            print(f'SKIP (already applied): {path}')
            continue
        assert src.count(old) == 1, f'anchor not unique/found in {path}:\n{old!r}'
        src = src.replace(old, new)
    io.open(path, 'w', encoding='latin-1', newline='').write(src)
    print(f'OK: {path}')

# --- 1. DxInputDevice.cpp: transition-logged Acquire + self-heal ------------
DXI = '[Lib]__Engine/Sources/DxTools/DxInputDevice.cpp'

old_onactivate = """	if ( !m_pDInputKeyboardDev )	return S_FALSE;

	m_bActive = bActive;
"""
new_onactivate = """	if ( !m_pDInputKeyboardDev )	return S_FALSE;

	CDebugSet::ToLogFile ( "[INPUTDBG] DxInputDevice::OnActivate(%d)", bActive );
	m_bActive = bActive;
"""

old_process = """BOOL DxInputDevice::ProcessKeyState()
{
	HRESULT hr = S_OK;
	if ( !m_bActive )	return FALSE;

	hr =  m_pDInputKeyboardDev->Acquire();
	if ( SUCCEEDED(hr) )
"""
new_process = """BOOL DxInputDevice::ProcessKeyState()
{
	HRESULT hr = S_OK;

	//	winemac.drv never delivers WM_NCACTIVATE(TRUE) to this borderless
	//	window after Cmd+Tab, so nobody calls OnActivate(TRUE); reacquire
	//	ourselves as soon as the OS reports the window foreground again.
	BOOL bForeground = ( ::GetForegroundWindow()==m_hWnd );
	static BOOL s_bLastForeground = TRUE;
	if ( bForeground != s_bLastForeground )
	{
		CDebugSet::ToLogFile ( "[INPUTDBG] foreground=%d active=%d", bForeground, m_bActive );
		s_bLastForeground = bForeground;
	}
	if ( !m_bActive )
	{
		if ( bForeground )	OnActivate ( TRUE );
		if ( !m_bActive )	return FALSE;
	}

	hr =  m_pDInputKeyboardDev->Acquire();
	static HRESULT s_hrLastKbAcquire = (HRESULT)0xDEADBEEF;
	if ( hr != s_hrLastKbAcquire )
	{
		CDebugSet::ToLogFile ( "[INPUTDBG] kb Acquire hr=0x%08x", hr );
		s_hrLastKbAcquire = hr;
	}
	if ( SUCCEEDED(hr) )
"""

old_mouse_acq = """	hr =  m_pDInputMouseDev->Acquire();
	if ( SUCCEEDED(hr) )
	{
		m_dwMouseElements = MOUSE_BUFFER_SIZE;
"""
new_mouse_acq = """	hr =  m_pDInputMouseDev->Acquire();
	static HRESULT s_hrLastMsAcquire = (HRESULT)0xDEADBEEF;
	if ( hr != s_hrLastMsAcquire )
	{
		CDebugSet::ToLogFile ( "[INPUTDBG] mouse Acquire hr=0x%08x", hr );
		s_hrLastMsAcquire = hr;
	}
	if ( SUCCEEDED(hr) )
	{
		m_dwMouseElements = MOUSE_BUFFER_SIZE;
"""

edit(DXI, [(old_onactivate, new_onactivate),
           (old_process, new_process),
           (old_mouse_acq, new_mouse_acq)])

# --- 2. BasicWnd.cpp: WM_ACTIVATEAPP drives input reactivation -------------
BW = '[Client]__Game/Sources/BasicWnd.cpp'

old_include = '#include "DxCursor.h"\n'
new_include = '#include "DxCursor.h"\n#include "DxInputDevice.h"\n'

old_appact = """	CWnd::OnActivateApp(bActive, hTask);
	//m_pApp->SetActive(bActive);
"""
new_appact = """	CWnd::OnActivateApp(bActive, hTask);
	//	Cmd+Tab back under winemac.drv arrives as app activation only
	//	(no WM_NCACTIVATE for a borderless WS_POPUP window) - reacquire here.
	CDebugSet::ToLogFile ( "[INPUTDBG] WM_ACTIVATEAPP bActive=%d", bActive );
	DxInputDevice::GetInstance().OnActivate ( bActive );
	//m_pApp->SetActive(bActive);
"""

edit(BW, [(old_include, new_include), (old_appact, new_appact)])

# --- 3. BasicWndD3d.cpp: log the two existing activation handlers ----------
BWD = '[Client]__Game/Sources/BasicWndD3d.cpp'

old_ncact = """	DxInputDevice::GetInstance().OnActivate ( bActive );
"""
new_ncact = """	CDebugSet::ToLogFile ( "[INPUTDBG] WM_NCACTIVATE bActive=%d", bActive );
	DxInputDevice::GetInstance().OnActivate ( bActive );
"""

old_wmact = """	m_pApp->SetActive ( !bMinimized );
"""
new_wmact = """	CDebugSet::ToLogFile ( "[INPUTDBG] WM_ACTIVATE nState=%u bMin=%d", nState, (int)bMinimized );
	m_pApp->SetActive ( !bMinimized );
"""

edit(BWD, [(old_ncact, new_ncact), (old_wmact, new_wmact)])

print('All patches applied.')
```

- [ ] **Step 2: Run it and verify the diff**

```bash
cd /Users/t31k/Projects/RAN/RanOnline
python3 scripts/patch-input-focus.py
git diff --stat   # expect exactly the 3 .cpp files + the new script untracked
```

Then verify no byte damage outside the edits: `git diff` and confirm every hunk is one of the insertions above — surrounding Korean-comment bytes must appear unchanged in context lines. If anything else changed, `git checkout -- <file>` and fix the script.

- [ ] **Step 3: Compile check via CI**

```bash
git add scripts/patch-input-focus.py '[Lib]__Engine/Sources/DxTools/DxInputDevice.cpp' '[Client]__Game/Sources/BasicWnd.cpp' '[Client]__Game/Sources/BasicWndD3d.cpp'
git commit -m "fix(client): reacquire DirectInput on WM_ACTIVATEAPP + foreground self-heal (Cmd+Tab input loss)

Co-Authored-By: Claude Fable 5 <noreply@anthropic.com>"
git push origin master
gh run watch   # wait for green (~6 min)
```

Known compile risk: if `CDebugSet` is undeclared in `BasicWnd.cpp` (its pch may not pull it in — `DxInputDevice.cpp` gets it from the engine pch), add `#include "DebugSet.h"` next to the `DxInputDevice.h` include via the same script pattern (it lives in the same `DxTools` dir, so the include path already resolves). If `GetForegroundWindow` is undeclared in `DxInputDevice.cpp` (it won't be — windows.h is in the pch), same drill with `<windows.h>` — but do not add includes preemptively.

- [ ] **Step 4: Deploy the new Game.exe locally**

```bash
/Users/t31k/Projects/RAN/update.command --no-launch   # downloads artifact, swaps Game.exe into client/
```

If `update.command --no-launch` misbehaves, fallback: `gh run download <run-id> -n ran-binaries` and copy `Game.exe` into `/Users/t31k/Projects/RAN/client/` manually (kill any running client first: `WINEPREFIX=~/.wine-ran10-client wineserver -k`).

---

### Task 2: Reproduce, verify, classify

**Files:** none modified. Read: the newest `log.*.txt` under `/Users/t31k/Projects/RAN/client/` (CDebugSet::ToLogFile appends there; find it with `ls -t` on files matching `log.*.txt`, checking subdirs too).

**Interfaces:**
- Consumes: `[INPUTDBG]` log lines from Task 1.
- Produces: a verdict — outcome A (fixed via message), B (fixed via self-heal), or C (not fixed) — recorded in this plan file under this task, which decides whether Task 4 runs.

- [ ] **Step 1: Launch the client** (servers are on the VPS; login screen is enough — it uses the same DxInputDevice path, so full world entry is NOT required):

```bash
/Users/t31k/Projects/RAN/run-client.command
```

- [ ] **Step 2: The reproduction procedure (this is the test — it FAILED before this build):**
1. At the login screen, click a textbox and type a few characters — confirm input works.
2. Cmd+Tab to another app. Wait ~5 s.
3. Cmd+Tab back to the game.
4. Click and type again.
5. Repeat the cycle 3×, including once via clicking another app's window instead of Cmd+Tab.

This step needs a human at the keyboard. Ask the user to run the 5 actions; everything else in this task is yours.

- [ ] **Step 3: Read the log and classify:**

```bash
cd /Users/t31k/Projects/RAN/client && grep -h INPUTDBG $(ls -t log.*.txt | head -1)
```

- **Outcome A (expected):** input works after step 2.3; log shows `WM_ACTIVATEAPP bActive=0` on the way out and `WM_ACTIVATEAPP bActive=1` + `OnActivate(1)` on the way back. Root cause confirmed exactly as analyzed. → skip Task 4, go to Task 5.
- **Outcome B:** input works, but no `WM_ACTIVATEAPP bActive=1` line — instead `foreground=1 active=0` followed by `OnActivate(1)` (the self-heal fired). Fine: fix works via the safety net. → skip Task 4, go to Task 5.
- **Outcome C:** input still dead. Log will show `foreground=0` persisting after refocus and/or `kb/mouse Acquire hr=0x80070005` (DIERR_OTHERAPPHASPRIO) forever. This is the "Wine never re-marks the window foreground" variant. → do Task 4.
- [ ] **Step 4:** Append the outcome (letter + the relevant log lines) to this plan file under this task, and commit the plan update:

```bash
git add docs/superpowers/plans/2026-09-18-cmdtab-input-loss-fix.md
git commit -m "docs: record Cmd+Tab fix verification outcome

Co-Authored-By: Claude Fable 5 <noreply@anthropic.com>"
```

**VERIFICATION OUTCOME (2026-09-18, executed by Opus 4.8):** Fix CONFIRMED WORKING by
direct user test — input (mouse + keyboard) survives Cmd+Tab away/back on the repo
client (build `b1e3555`, CI run 35309426319 green). Outcome is A-or-B but
**indeterminate** from logs: `[INPUTDBG]` was never written because CDebugSet's log
path (`m_strLogFile`) is not initialized in the `/app_run` release path — so ToLogFile
is a silent no-op for ALL log calls in this build, not just the new ones. Immaterial:
the fix is validated behaviorally. The instrumentation is kept (harmless, consistent
with every other ToLogFile call; will emit under a debug build). Task 3 passes by
construction: cooperative-level flags (`DISCL_FOREGROUND`) were NOT changed, so
background input suppression is unchanged; the user's test covered focus→away→back.
Task 4 SKIPPED (fix worked, no fallback needed).

---

### Task 3: Regression check (no new bugs from the change)

**Files:** none.

**Interfaces:** consumes the running client from Task 2.

The change makes `OnActivate(TRUE)` fire in situations it previously didn't. Check the two things it touches beyond input:

- [ ] **Step 1:** While the game has focus, confirm normal play still works: mouse look/drag on the login screen widgets, keyboard typing, and (if logged in) WASD + clicking. `OnActivate(TRUE)` calls `InitKeyState()`, which clears held-key state — verify a held key (e.g. holding a movement key through a Cmd+Tab cycle) recovers on the next press rather than sticking.
- [ ] **Step 2:** Confirm no `[INPUTDBG]` log spam while idle and focused (the transition-only guards mean the log should be quiet — a flood means a guard is wrong; fix the script and rebuild before shipping).
- [ ] **Step 3:** Cmd+Tab out and confirm the game does NOT keep reading input while in the background (type in the other app; come back; the game's textbox must not contain those characters — `DISCL_FOREGROUND` guarantees this, this is just verification).

---

### Task 4: Fallback — ONLY if Task 2 = Outcome C (Wine never restores foreground)

Skip entirely on outcome A or B. These are ordered cheapest-first; stop at the first one that makes the Task 2 procedure pass, then record which one in this plan and proceed to Task 5. After each experiment, re-run the exact Task 2 procedure.

- [ ] **Experiment 1 — windowed mode (no rebuild):** the engine renders into a normal titled window when `bScrWindowed=1`; winemac focus handling for framed windows is the well-trodden path. Find the live config: latin-1-grep `RANPARAM.cpp` shows `bScrWindowed` is read via `cFILE.getflag` (lines 212 and 1192) — read `[Lib]__MfcEx/Sources/RANPARAM.cpp` around those lines to learn which file(s) it parses (param.ini / GameOption in the client dir), then set `bScrWindowed = 1` there and relaunch. (Runtime toggle also exists — L-Alt+Enter, `BasicWndD3d.cpp:447-455` — useful for a quick A/B once input works, useless while input is dead.)
- [ ] **Experiment 2 — Wine virtual desktop (no rebuild):** launch the client inside `wine explorer /desktop=RanOdyssey,1600x1000 Game.exe` (edit a copy of `run-client.command`). Note: the servers' launcher found `explorer /desktop` silently failed to spawn children on this setup (memory, 2026-09-13) — verify the game process actually appears; if it doesn't, abandon this experiment quickly.
- [ ] **Experiment 3 — background cooperative level (rebuild):** change both `SetCooperativeLevel` flag sets in `DxInputDevice.cpp` (constructor lines ~458-459 AND `InitDeviceObjects` line ~582) from `DISCL_NONEXCLUSIVE|DISCL_FOREGROUND[|DISCL_NOWINKEY]` to `DISCL_NONEXCLUSIVE|DISCL_BACKGROUND`, and make `ProcessKeyState`'s self-heal unconditional (`if (!m_bActive) OnActivate(TRUE);` without the foreground check). Background devices acquire regardless of Wine's foreground bookkeeping, bypassing the broken check entirely. Side effect to accept and document: the game will read input while backgrounded (Task 3 Step 3 will now "fail" — that's expected under this variant; note it in the runbook). Use a latin-1 Python script for the edit, same pattern as Task 1.
- [ ] If all three fail: STOP. Per systematic-debugging, 3 failed fixes on one bug = question the architecture — in this case that means the winemac.drv patch route (custom `winemac.so`), which is a separate project the user must green-light. Write up the log evidence and hand back.

---

### Task 5: Ship + close out

**Files:**
- Modify: `docs/RUNBOOK.md` (repo, untracked-or-tracked — check), memory files (outside repo).

- [ ] **Step 1 — ship OTA to RanOdyssey.app users:**

```bash
/Users/t31k/Projects/RAN/bump-release.command
```

This folds the new `Game.exe` into `dist/overlay/`, computes version = latest GitHub release + 1, and publishes `payload.zip` + `manifest.txt` as a new release on `T31K/RAN`. Installed apps (including `/Applications/RanOdyssey.app`) pick it up on next launch. Verify: `gh release view` shows the new tag, then launch `/Applications/RanOdyssey.app` once and confirm the updater pulls it and Cmd+Tab works in the installed app too.

- [ ] **Step 2 — runbook:** add a short section to `docs/RUNBOOK.md`: symptom ("input dead after Cmd+Tab"), status (fixed in build N, mechanism = WM_ACTIVATEAPP reacquire / self-heal per Task 2 outcome), and the diagnostic (`grep INPUTDBG` in newest `log.*.txt`).
- [ ] **Step 3 — memory:** update `~/.claude/projects/-Users-t31k-Projects-RAN-RanOnline/memory/ran-project-state.md`: replace the "Alt-tab / Cmd+Tab input-loss bug (UNSOLVED...)" paragraph with the resolution (root cause: WM_NCACTIVATE-only reactivation of DxInputDevice + winemac never sending it to the borderless window; fix commit; which outcome A/B/C verified; workaround paragraph deleted). Also update `ran-runbook-diagnostics.md` if it mentions the bug.
- [ ] **Step 4 — commit** any runbook change:

```bash
git add docs/RUNBOOK.md
git commit -m "docs(runbook): Cmd+Tab input-loss fixed; INPUTDBG diagnostics

Co-Authored-By: Claude Fable 5 <noreply@anthropic.com>"
git push origin master
```

(Note: pushing to master triggers Coolify's SERVER rebuild on the VPS — harmless, the server code is unchanged, but expect a deploy to run.)
