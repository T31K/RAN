# Native macOS Client (Off Wine) Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Ship RAN Online EP7 as a native Apple Silicon `.app` with no Wine anywhere, so Cmd+Tab, fullscreen, and focus behave like any Mac game.

**Why:** The two problems to solve are (1) Cmd+Tab / focus breakage and (2) dependence on Wine. Both come from the same place: the game's Win32 window and DirectInput run through Wine's Mac driver (`winemac.drv`), which mis-delivers activation messages (`WM_NCACTIVATE` never arrives after Cmd+Tab back) and lets the borderless window drift (see `2026-09-18-cmdtab-input-loss-fix.md`, runbook #9/#10, commit 213125e). Every fix so far is a patch over Wine behavior. A native Cocoa window (via SDL3) gets focus events straight from macOS, so that whole class of bugs disappears instead of being patched one by one.

**Architecture:** Keep the existing C++ game code and its Direct3D 9 calls; replace the layers *under* it. SDL3 provides window, input, IME and the event loop. A Direct3D 9 implementation that runs on Metal provides graphics: **Path A** = DXVK-native (D3D9→Vulkan) + MoltenVK (Vulkan→Metal); **Path B** (fallback) = our own D3D9-subset shim written directly on Metal. A thin compat layer supplies the Win32/MFC types and functions the game uses. The Windows (Win32) build keeps building and shipping under Wine the whole time; the native build replaces it only at parity (Phase 5).

**Tech Stack:** C++20, clang (Xcode CLT), CMake ≥ 3.24 + Ninja, SDL3, DXVK-native + MoltenVK (Path A) or Metal-cpp (Path B), miniaudio (sound), stb_image (texture decode), CoreText (fonts), Python 3 stdlib (tooling/tests). Existing Windows build: VS2022 msbuild via `.github/workflows/build.yml`.

**Spec:** No separate spec. Decisions and their evidence are in "Measured facts" and "Architecture decisions" below (measured from source on 2026-10-03). Executors read this whole document before starting any task.

## Global Constraints

- **No Wine at runtime in the native build.** Not winelib, not CrossOver, not Game Porting Toolkit / D3DMetal (all are Wine; GPTK's D3DMetal also only covers D3D11/12, not our D3D9).
- **Target: macOS 14+ on arm64 only.** No Intel, no universal binary. Dev/test machine is the M3 Mac on macOS 26.
- **The Windows Win32 build must stay green on every commit** (`.github/workflows/build.yml`, `msbuild RanOnline.sln -p:Configuration=Release -p:Platform=Win32`). The Wine-shipped client stays the release path until Phase 5 sign-off.
- **The server is not touched.** The native client must put the exact same bytes on the wire as the 32-bit Windows client (all packet structs are `#pragma pack(1)`, 19 pack sites).
- **Source encoding is CP949** (Korean comments/literals). Never let an editor re-encode a file. Edit flow: `iconv -f CP949 -t UTF-8` → Edit tool → `iconv -f UTF-8 -t CP949`, then confirm `git diff --stat` only shows the intended lines. Use `grep -a` on sources (Korean bytes trip binary detection).
- **Free dependencies only:** SDL3 (zlib), DXVK (zlib), MoltenVK (Apache-2.0), miniaudio (MIT-0/public domain), stb (public domain). Repo `T31K/RAN` is **public**, so GitHub Actions macOS/Windows runners are free. If the repo is ever made private, stop and ask before adding/continuing macOS CI jobs (10× minute multiplier).
- **No browser automation.** Visual checks are pixel-diff scripts against reference screenshots, or the user looks.
- New native-port code lives under `port/` (probes, compat layer, platform layer, scripts). Third-party checkouts go in `port/third_party/` (gitignored); our patches to them go in `port/patches/<project>/`.

## Review Focus

These are the failure modes most likely to hurt a player that no single task's happy-path test covers. Each has an owning test.

1. **Cmd+Tab away/back, minimize/restore, fullscreen toggle, display sleep/wake**: window keeps its frame, clicks land where the cursor is, keyboard works. *(Owner: Task 0.2 probe test; re-run as Phase 4 gate G4.1 on the real game.)*
2. **Korean text**: chat typed with the macOS Korean IME, character names, and ItemStrTable strings must render correctly and send the same CP949 bytes as the Windows client. *(Owner: Phase 2 test P2.4 IME byte-compare; Phase 3 test P3.6 font render.)*
3. **64-bit layout drift**: `long`/`wchar_t`/pointer/`bool` fields silently change struct size on arm64 → server rejects or misreads packets, or data files misparse. *(Owner: Phase 1 test P1.3 golden-`sizeof` check against the Win32 build.)*
4. **File paths**: data references like `Textures\\Foo.DDS` with backslashes and mixed case → assets silently missing (invisible/pink). *(Owner: Phase 1 test P1.5 path resolver on the full `client/data` tree.)*
5. **Read-only app bundle**: a signed `.app` can't be written to, but the game writes cache/logs/settings next to its exe. *(Owner: Phase 5 test P5.2 launch from a read-only copy.)*

---

## Measured facts (2026-10-03, from source — do not re-derive)

Client code (excluding servers/tools): ~580k lines.

| Module | Lines | Notes |
|---|---|---|
| `[Lib]__Engine` | 250k | Renderer, meshes, effects, terrain, DXUT, input, fonts |
| `[Lib]__RanClient` | 196k | Game logic (shared with server) |
| `[Lib]__RanClientUI` | 79k | UI; `CString` in 268 files |
| `[Lib]__MfcEx` | 21k | MFC helpers |
| `[Lib]__EngineUI` | 18k | UI controls, IME edit |
| `[Lib]__NetClient` | 7k | One socket, `WSAEventSelect` + `recv/send/connect/ioctlsocket` |
| `[Lib]__EngineSound` | 5k | DirectSound + Ogg Vorbis |
| `[Client]__Game` | 2k | MFC `CWinApp`/`CWnd` shell (`BasicWnd*.cpp`) |

**Graphics (the crux):** fixed-function D3D9. Device-call counts: `SetRenderState` 1661, `SetTextureStageState` 894, `SetTexture` 320, state blocks (`Begin/EndStateBlock`) 438, `SetTransform` 287, `SetSamplerState` 261, `SetFVF` 185, `LightEnable/SetLight/SetMaterial` ~250, `DrawPrimitiveUP/DrawIndexedPrimitive(UP)/DrawPrimitive` ~220. Programmable: vertex/pixel shaders in 13 files (asm via `D3DXAssembleShader`), `ID3DXEffect` from `.fx` in ~10 call sites (post-process, water, nature). ~70 distinct device methods in total.

**D3DX:** ~100 distinct functions. Mostly math (`D3DXVECTOR3` 1088 uses, `D3DXMatrixMultiply` 129, `D3DXVec3TransformCoord` 142…). Loaders: `D3DXCreateTextureFromFile*` (~12 sites), `D3DXLoadMeshFromX*` (~9), `D3DXCreateEffectFromFile*` (~6), `D3DXCreateFont*` (3), `D3DXCreateSprite` (3).

**Text:** GDI-rasterized fonts in `DxTools/d3dfont.cpp`, `DxTools/D3DFontX.cpp`, `TextTexture/TextTexture.cpp`. IME in `Common/DXInputString.cpp`, `Common/IMEEdit.cpp`, `DXUTgui.cpp`.

**Win32 surface (files using it):** threads (`CreateThread`/`_beginthreadex`) 20, `CRITICAL_SECTION` 9, timers ~14, registry 4, `MessageBox` 69, `GetModuleFileName` 8, `PostMessage` 5, IME 2–3, `MultiByteToWideChar`/`WideCharToMultiByte` 11. Inline asm in 8 files, SEH `__try` in 3, SSE intrinsics in 6.

**Packets:** `GLMsg/` headers use fixed-size types (`__time64_t` ×36, `LONGLONG` ×38, `BOOL` ×33, `bool` ×133, `float` ×30) under `#pragma pack(1)`. A couple of structs hold pointers (`GLCHARAG_DATA*`, `CByteStream*`) — verify they never go on the wire.

**Head start:** the original repo's branch `upstream/x64-support` (commit 06d1594, "X64 Architecture Support Engine Lib + C++20") already 64-bit-proofs `SerialFile`, `SerialMemory`, `ByteStream`, `basestream`, `seqrandom`, `DxSkinMesh9_CPU` (inline asm removed), `RENDERPARAM`, `DebugSet`, `SAnimation`, `DxFrameMesh`. Cherry-pick its source changes (not its `.tlog` build junk).

**Assets:** `~/Projects/RAN/client` = 2.1 GB; formats include DDS/TGA textures, `.x` meshes, RAN's own serialized formats (`.rcc` = ZIP).

## Architecture decisions

1. **Keep the D3D9 call sites; replace D3D9 underneath.** Rewriting ~600 files of fixed-function rendering against a new API is the slowest, riskiest option. A D3D9 implementation on Metal lets the game code mostly stay as is.
2. **Path A first, Path B as fallback, decided by the Phase 0 spike.** DXVK's D3D9 front end already emulates fixed-function, state blocks and asm shaders, and is battle-tested on thousands of D3D9 games. Its risk is macOS: DXVK-native targets Linux and needs Vulkan features MoltenVK may lack. Path B (own Metal shim over the ~70 methods we use, fixed-function done with an uber-shader) is certain to work but adds an estimated 6–8 weeks.
3. **SDL3 for platform.** Window, fullscreen (desktop/Spaces), focus, keyboard, mouse, IME text events, message boxes. It is also the window system DXVK-native supports.
4. **Fixed-size Win32 types.** `DWORD`=`uint32_t`, `LONG`=`int32_t`, `BOOL`=`int32_t`, and compile with `-fshort-wchar` so `wchar_t`/`WCHAR` stay 2 bytes like Windows (keeps file and packet layouts identical). The compat layer provides its own 2-byte `wcs*` functions because libc's assume 4-byte `wchar_t`.
5. **Keep CP949 bytes in memory, as on Windows.** Strings are converted to Unicode only at the edges: CoreText for drawing, SDL text input → CP949 for the IME.
6. **Shaders and effects are precompiled on Windows CI** (`D3DXAssembleShader`/`D3DXCreateEffect` → bytecode blobs), so the Mac build never needs D3DX's shader compiler.

## Phase overview and timeline (solo + AI, estimates)

| Phase | What | Estimate | Exit criterion |
|---|---|---|---|
| 0 | Spike: Cmd+Tab probe, DXVK-on-Mac probe, compile probe, go/no-go | 2 weeks | `docs/port/spike-report.md` with a decision |
| 1 | Portable codebase, Windows still builds | 4–6 weeks | All client libs compile on macOS clang; P1.3 sizes match; Win32 CI green |
| 2 | Platform layer (SDL3 window/input/IME, miniaudio, sockets) | 3–4 weeks | Native binary logs in to the VPS server and receives the character list |
| 3 | Graphics (D3D9-on-Metal, D3DX subset, fonts) | 8–12 weeks (Path A) / +6–8 (Path B) | Login → char select → in-world on every map, pixel-diff vs Wine reference |
| 4 | Gameplay parity + Cmd+Tab gates | 4–6 weeks | Parity checklist passes |
| 5 | Ship: native `.app`, sign/notarize, OTA | 1–2 weeks | Friends install the native build; Wine build retired |

**Total: roughly 5–7 months.** Phase 3 carries the schedule risk; the spike exists to shrink that before committing.

---

# Phase 0 — Spike (detailed tasks)

Work on a branch: `git checkout -b port/macos-spike master`.

### Task 0.1: API inventory tool

Gives every later phase an exact, re-runnable list of what must be replaced, and a progress counter (counts should fall as compat code lands).

**Files:**
- Create: `port/scripts/inventory.py`
- Create: `port/scripts/test_inventory.py`
- Create (generated): `docs/port/api-inventory.md`
- Modify: `.gitignore` (add `port/third_party/`, `port/build/`, `port/**/build/`)

**Interfaces:**
- Produces: `scan(root: pathlib.Path, dirs: list[str]) -> dict[str, collections.Counter]` (keys: `d3d9_method`, `d3dx_call`, `win32_call`, `mfc_type`); `render(counts) -> str` (markdown); CLI `python3 port/scripts/inventory.py <repo-root> > docs/port/api-inventory.md`.

- [ ] **Step 1: Write the failing test**

```python
# port/scripts/test_inventory.py
import pathlib, tempfile, unittest
import inventory

SAMPLE = (
    "void f() {\n"
    "  pd3dDevice->SetRenderState(D3DRS_LIGHTING, FALSE);\n"
    "  pd3dDevice->SetRenderState(D3DRS_ZENABLE, TRUE);\n"
    "  m_pd3dDevice->SetTexture(0, NULL);\n"
    "  D3DXMatrixIdentity(&m);\n"
    "  CString s; MessageBox(NULL, \"x\", \"y\", 0);\n"
    "  // \xb1\xe2\xba\xbb CP949 comment bytes\n"
    "}\n"
)

class InventoryTest(unittest.TestCase):
    def test_counts_calls_in_cp949_source(self):
        with tempfile.TemporaryDirectory() as d:
            mod = pathlib.Path(d, "[Lib]__Engine", "Sources")
            mod.mkdir(parents=True)
            (mod / "a.cpp").write_bytes(SAMPLE.encode("latin-1"))
            counts = inventory.scan(pathlib.Path(d), ["[Lib]__Engine"])
        self.assertEqual(counts["d3d9_method"]["SetRenderState"], 2)
        self.assertEqual(counts["d3d9_method"]["SetTexture"], 1)
        self.assertEqual(counts["d3dx_call"]["D3DXMatrixIdentity"], 1)
        self.assertEqual(counts["win32_call"]["MessageBox"], 1)
        self.assertEqual(counts["mfc_type"]["CString"], 1)

    def test_render_lists_each_category(self):
        import collections
        counts = {k: collections.Counter() for k in inventory.CATEGORIES}
        counts["d3d9_method"]["SetRenderState"] = 3
        md = inventory.render(counts)
        self.assertIn("## d3d9_method", md)
        self.assertIn("| SetRenderState | 3 |", md)

if __name__ == "__main__":
    unittest.main()
```

- [ ] **Step 2: Run test to verify it fails**

Run: `cd port/scripts && python3 -m unittest test_inventory -v`
Expected: FAIL with `ModuleNotFoundError: No module named 'inventory'`

- [ ] **Step 3: Write the implementation**

```python
#!/usr/bin/env python3
# port/scripts/inventory.py
"""Count the Windows-only APIs the RAN client uses. Read-only; sources are CP949, read as latin-1."""
import collections
import pathlib
import re
import sys

CLIENT_DIRS = [
    "[Client]__Game", "[Lib]__Engine", "[Lib]__EngineSound", "[Lib]__EngineUI",
    "[Lib]__MfcEx", "[Lib]__NetClient", "[Lib]__RanClient", "[Lib]__RanClientUI",
]

PATTERNS = {
    "d3d9_method": re.compile(r"\b(?:\w*[Dd]evice\w*|pd3d\w*|m_pd3d\w*)\s*->\s*([A-Z]\w+)\s*\("),
    "d3dx_call": re.compile(r"\b(D3DX[A-Z]\w*)\s*\("),
    "win32_call": re.compile(
        r"\b(CreateThread|_beginthreadex|EnterCriticalSection|InitializeCriticalSection|"
        r"WaitForSingleObject|GetTickCount|timeGetTime|QueryPerformanceCounter|"
        r"RegOpenKeyEx\w*|RegQueryValueEx\w*|CreateFile\w*|GetModuleFileName\w*|"
        r"MessageBox\w*|PostMessage\w*|SendMessage\w*|GetAsyncKeyState|Imm[A-Z]\w*|"
        r"WSAEventSelect|ioctlsocket|DirectSoundCreate\w*|DirectInput8Create|"
        r"MultiByteToWideChar|WideCharToMultiByte)\s*\("),
    "mfc_type": re.compile(
        r"\b(CString|CWinApp|CWnd|CFile|CArray|CList|CMap|CPoint|CRect|CSize|CTime|"
        r"AfxGetApp|AfxMessageBox)\b"),
}
CATEGORIES = list(PATTERNS)
SUFFIXES = {".cpp", ".h", ".hpp", ".inl"}


def scan(root, dirs):
    counts = {k: collections.Counter() for k in CATEGORIES}
    for d in dirs:
        for path in sorted(pathlib.Path(root, d).rglob("*")):
            if path.suffix.lower() not in SUFFIXES or not path.is_file():
                continue
            text = path.read_text(encoding="latin-1")
            for key, pat in PATTERNS.items():
                counts[key].update(pat.findall(text))
    return counts


def render(counts):
    out = ["# RAN client Windows API inventory", "",
           "Generated by `port/scripts/inventory.py`. Re-run after each compat milestone.", ""]
    for key in CATEGORIES:
        c = counts[key]
        out += [f"## {key}", "", f"{len(c)} distinct, {sum(c.values())} uses.", "",
                "| Name | Uses |", "|---|---|"]
        out += [f"| {name} | {n} |" for name, n in c.most_common()]
        out.append("")
    return "\n".join(out)


if __name__ == "__main__":
    root = pathlib.Path(sys.argv[1] if len(sys.argv) > 1 else ".")
    print(render(scan(root, CLIENT_DIRS)))
```

- [ ] **Step 4: Run tests to verify they pass**

Run: `cd port/scripts && python3 -m unittest test_inventory -v`
Expected: 2 tests PASS

- [ ] **Step 5: Generate the inventory and sanity-check it**

Run: `mkdir -p docs/port && python3 port/scripts/inventory.py . > docs/port/api-inventory.md && grep -A3 "^| SetRenderState" docs/port/api-inventory.md`
Expected: `SetRenderState` present with a count in the same ballpark as the measured 1661 (the regex is slightly broader, so ±15% is fine).

- [ ] **Step 6: Commit**

```bash
git add port/scripts/inventory.py port/scripts/test_inventory.py docs/port/api-inventory.md .gitignore
git commit -m "port: API inventory tool + generated inventory"
```

### Task 0.2: Cmd+Tab focus probe (the core user goal)

Proves that a native SDL3 window survives Cmd+Tab, minimize and fullscreen with no frame drift and correct click mapping, before any game code is ported.

**Files:**
- Create: `port/probes/focus/CMakeLists.txt`
- Create: `port/probes/focus/main.cpp`
- Create: `port/scripts/cmdtab_check.sh`

**Interfaces:**
- Produces: `focus_probe --mode <windowed|desktop-fullscreen|spaces-fullscreen> --seconds N`, which writes lines to stdout: `FRAME <x> <y> <w> <h>`, `FOCUS gained|lost`, `CLICK <x> <y> HIT|MISS <target>`, `DONE`. `cmdtab_check.sh <probe-binary> <mode>` exits 0 on PASS.

- [ ] **Step 1: Install SDL3 and build tools (free, Homebrew)**

Run: `brew install sdl3 ninja`
Expected: `brew list --versions sdl3` prints a 3.x version.

- [ ] **Step 2: Write the probe**

```cmake
# port/probes/focus/CMakeLists.txt
cmake_minimum_required(VERSION 3.24)
project(focus_probe CXX)
set(CMAKE_CXX_STANDARD 20)
find_package(SDL3 REQUIRED CONFIG)
add_executable(focus_probe main.cpp)
target_link_libraries(focus_probe PRIVATE SDL3::SDL3)
```

```cpp
// port/probes/focus/main.cpp
// Native-window focus probe: logs frame/focus changes and whether clicks hit
// the four corner targets, so Cmd+Tab behaviour can be checked automatically.
#include <SDL3/SDL.h>
#include <cstdio>
#include <cstring>
#include <cstdlib>
#include <string>

static void logFrame(SDL_Window* w)
{
    int x = 0, y = 0, cw = 0, ch = 0;
    SDL_GetWindowPosition(w, &x, &y);
    SDL_GetWindowSize(w, &cw, &ch);
    std::printf("FRAME %d %d %d %d\n", x, y, cw, ch);
    std::fflush(stdout);
}

int main(int argc, char** argv)
{
    std::string mode = "desktop-fullscreen";
    int seconds = 12;
    for (int i = 1; i + 1 < argc; i += 2) {
        if (!std::strcmp(argv[i], "--mode")) mode = argv[i + 1];
        else if (!std::strcmp(argv[i], "--seconds")) seconds = std::atoi(argv[i + 1]);
    }

    SDL_SetHint(SDL_HINT_VIDEO_MAC_FULLSCREEN_SPACES, mode == "spaces-fullscreen" ? "1" : "0");
    if (!SDL_Init(SDL_INIT_VIDEO)) { std::printf("ERROR %s\n", SDL_GetError()); return 1; }

    SDL_Window* win = SDL_CreateWindow("focus_probe", 1280, 800, SDL_WINDOW_HIGH_PIXEL_DENSITY);
    if (!win) { std::printf("ERROR %s\n", SDL_GetError()); return 1; }
    if (mode != "windowed") {
        SDL_SetWindowFullscreenMode(win, nullptr);   // nullptr = borderless desktop fullscreen
        SDL_SetWindowFullscreen(win, true);
        SDL_SyncWindow(win);
    }
    SDL_Renderer* ren = SDL_CreateRenderer(win, nullptr);
    logFrame(win);

    const Uint64 end = SDL_GetTicks() + (Uint64)seconds * 1000;
    const float kTarget = 80.0f;
    bool running = true;
    while (running && SDL_GetTicks() < end) {
        SDL_Event e;
        while (SDL_PollEvent(&e)) {
            switch (e.type) {
            case SDL_EVENT_QUIT: running = false; break;
            case SDL_EVENT_WINDOW_FOCUS_GAINED: std::printf("FOCUS gained\n"); logFrame(win); break;
            case SDL_EVENT_WINDOW_FOCUS_LOST: std::printf("FOCUS lost\n"); break;
            case SDL_EVENT_WINDOW_MOVED:
            case SDL_EVENT_WINDOW_RESIZED: logFrame(win); break;
            case SDL_EVENT_MOUSE_BUTTON_DOWN: {
                int cw = 0, ch = 0;
                SDL_GetWindowSize(win, &cw, &ch);
                const float x = e.button.x, y = e.button.y;
                const char* target = nullptr;
                if (x < kTarget && y < kTarget) target = "TL";
                else if (x > cw - kTarget && y < kTarget) target = "TR";
                else if (x < kTarget && y > ch - kTarget) target = "BL";
                else if (x > cw - kTarget && y > ch - kTarget) target = "BR";
                std::printf("CLICK %.0f %.0f %s %s\n", x, y, target ? "HIT" : "MISS", target ? target : "-");
                break;
            }
            default: break;
            }
            std::fflush(stdout);
        }
        int cw = 0, ch = 0;
        SDL_GetWindowSize(win, &cw, &ch);
        SDL_SetRenderDrawColor(ren, 20, 30, 50, 255);
        SDL_RenderClear(ren);
        SDL_SetRenderDrawColor(ren, 230, 180, 40, 255);
        const SDL_FRect r[4] = {
            {0, 0, kTarget, kTarget}, {cw - kTarget, 0, kTarget, kTarget},
            {0, ch - kTarget, kTarget, kTarget}, {cw - kTarget, ch - kTarget, kTarget, kTarget}};
        SDL_RenderFillRects(ren, r, 4);
        SDL_RenderPresent(ren);
        SDL_Delay(16);
    }
    logFrame(win);
    std::printf("DONE\n");
    SDL_Quit();
    return 0;
}
```

- [ ] **Step 3: Build it**

Run: `cmake -S port/probes/focus -B port/probes/focus/build -G Ninja && cmake --build port/probes/focus/build`
Expected: `port/probes/focus/build/focus_probe` exists.

- [ ] **Step 4: Write the automated Cmd+Tab check**

```bash
#!/bin/bash
# port/scripts/cmdtab_check.sh <probe-binary> <mode>
# Switches focus away and back twice (as Cmd+Tab does), then checks the window
# frame never changed and focus came back each time. Needs Terminal to have
# Accessibility permission (System Settings → Privacy & Security → Accessibility).
set -euo pipefail
BIN="$1"; MODE="${2:-desktop-fullscreen}"
LOG="$(mktemp)"
"$BIN" --mode "$MODE" --seconds 14 > "$LOG" &
PID=$!
sleep 3
for _ in 1 2; do
  osascript -e 'tell application "Finder" to activate'
  sleep 2
  osascript -e "tell application \"System Events\" to set frontmost of (first process whose unix id is $PID) to true"
  sleep 2
done
wait "$PID"
FIRST="$(grep '^FRAME' "$LOG" | head -1)"
LAST="$(grep '^FRAME' "$LOG" | tail -1)"
DISTINCT="$(grep '^FRAME' "$LOG" | sort -u | wc -l | tr -d ' ')"
GAINED="$(grep -c '^FOCUS gained' "$LOG" || true)"
if [ "$FIRST" = "$LAST" ] && [ "$DISTINCT" = "1" ] && [ "$GAINED" -ge 2 ]; then
  echo "PASS mode=$MODE frame=[$FIRST] focus_gained=$GAINED"
else
  echo "FAIL mode=$MODE first=[$FIRST] last=[$LAST] distinct_frames=$DISTINCT focus_gained=$GAINED"
  cat "$LOG"
  exit 1
fi
```

Run: `chmod +x port/scripts/cmdtab_check.sh`

- [ ] **Step 5: Run the check in all three modes**

Run: `for m in windowed desktop-fullscreen spaces-fullscreen; do port/scripts/cmdtab_check.sh port/probes/focus/build/focus_probe $m; done`
Expected: three `PASS` lines. In `spaces-fullscreen` the first `FRAME` line is logged after the Spaces animation; if it FAILs only because the first frame is the pre-fullscreen size, add `sleep 1` before the initial `logFrame` and re-run. Record whatever happens; a real FAIL here is a spike finding, not something to paper over.

- [ ] **Step 6: Manual click-mapping check (ask the user)**

Ask the user to run `port/probes/focus/build/focus_probe --mode desktop-fullscreen --seconds 40 | tee /tmp/focus.log`, Cmd+Tab away and back 3 times, minimize with Cmd+M and restore from the Dock once, then click each of the 4 yellow corner squares.
Expected in the log: four `CLICK … HIT TL|TR|BL|BR` lines after the last `FOCUS gained`, and no `MISS` for clicks on the squares. This is the check the Wine build fails today.

- [ ] **Step 7: Commit**

```bash
git add port/probes/focus port/scripts/cmdtab_check.sh
git commit -m "port: SDL3 Cmd+Tab focus probe + automated check"
```

### Task 0.3: Build DXVK-native for macOS (Path A feasibility)

**Timebox: 3 working days.** If DXVK still can't create a D3D9 device on MoltenVK by then, stop, record why in the spike report, and Path B becomes the plan.

**Files:**
- Create: `port/scripts/build-dxvk.sh`
- Create (if needed): `port/patches/dxvk/*.patch`

**Interfaces:**
- Produces: `port/third_party/dxvk-install/` containing `lib/libdxvk_d3d9.dylib` and the D3D9 native headers; the script prints `DXVK_TAG=…`, `DXVK_INCLUDE=…` and `DXVK_LIB=…` lines that Task 0.4's CMake consumes.

- [ ] **Step 1: Install the toolchain (free, Homebrew)**

Run: `brew install meson ninja glslang molten-vk vulkan-loader sdl3`
Expected: `ls "$(brew --prefix molten-vk)"/etc/vulkan/icd.d/MoltenVK_icd.json` exists.

- [ ] **Step 2: Write the build script**

```bash
#!/bin/bash
# port/scripts/build-dxvk.sh — build DXVK's D3D9 front end natively for macOS (SDL3 WSI).
set -euo pipefail
ROOT="$(cd "$(dirname "$0")/../.." && pwd)"
TP="$ROOT/port/third_party"
SRC="$TP/dxvk"
OUT="$TP/dxvk-install"
mkdir -p "$TP"
TAG="${DXVK_TAG:-$(git ls-remote --tags --refs https://github.com/doitsujin/dxvk 'v2.*' | sed 's#.*/##' | sort -V | tail -1)}"
if [ ! -d "$SRC" ]; then
  git clone --depth 1 --branch "$TAG" --recurse-submodules https://github.com/doitsujin/dxvk "$SRC"
fi
for p in "$ROOT"/port/patches/dxvk/*.patch; do
  [ -e "$p" ] || continue
  git -C "$SRC" apply --check "$p" 2>/dev/null && git -C "$SRC" apply "$p"
done
WSI_OPTS="-Dnative_sdl3=enabled -Dnative_sdl2=disabled -Dnative_glfw=disabled"
grep -q "native_sdl3" "$SRC/meson_options.txt" || WSI_OPTS="-Dnative_sdl2=enabled -Dnative_glfw=disabled"
meson setup "$SRC/build-mac" "$SRC" --buildtype=release --prefix="$OUT" \
  -Denable_d3d9=true -Denable_d3d10=false -Denable_d3d11=false -Denable_dxgi=false $WSI_OPTS \
  --reconfigure 2>/dev/null || \
meson setup "$SRC/build-mac" "$SRC" --buildtype=release --prefix="$OUT" \
  -Denable_d3d9=true -Denable_d3d10=false -Denable_d3d11=false -Denable_dxgi=false $WSI_OPTS
ninja -C "$SRC/build-mac"
ninja -C "$SRC/build-mac" install
echo "DXVK_TAG=$TAG"
echo "DXVK_INCLUDE=$(dirname "$(find "$OUT/include" -name d3d9.h | head -1)")"
echo "DXVK_LIB=$(find "$OUT/lib" -name 'libdxvk_d3d9*.dylib' | head -1)"
```

Run: `chmod +x port/scripts/build-dxvk.sh`

- [ ] **Step 3: Run it and work through macOS build errors**

Run: `port/scripts/build-dxvk.sh 2>&1 | tee /tmp/dxvk-build.log`
Expected on the first try: compile errors in Linux-only code. Known likely spots: `src/util/util_env.cpp` (`/proc/self/exe` → use `_NSGetExecutablePath`), thread naming (`pthread_setname_np` has a one-argument form on macOS), `src/util/sync/sync_futex*` (no futex on macOS → use the existing generic/condition-variable path), `.so` vs `.dylib` loading of `libvulkan` (load `libvulkan.1.dylib` from `$(brew --prefix vulkan-loader)/lib`). Fix each as a minimal change in `port/third_party/dxvk`, then save it with `git -C port/third_party/dxvk diff > port/patches/dxvk/0001-macos-build.patch` (one patch file per topic). Repeat until the script prints the three `DXVK_*` lines.

- [ ] **Step 4: Commit the script and patches**

```bash
git add port/scripts/build-dxvk.sh port/patches/dxvk
git commit -m "port: build DXVK d3d9 natively on macOS (+ macOS patches)"
```

### Task 0.4: Fixed-function D3D9 probe — native vs Wine pixel diff

Renders one scene using RAN's most-used fixed-function features, on (a) DXVK-native on macOS and (b) the same program built for Windows and run under our existing Wine, then compares pixels. (b) is the reference because it's what players see today.

**Files:**
- Create: `port/probes/d3d9ff/main.cpp`
- Create: `port/probes/d3d9ff/CMakeLists.txt`
- Create: `port/scripts/ppm_diff.py`
- Create: `port/scripts/test_ppm_diff.py`
- Create: `.github/workflows/port-probe.yml`

**Interfaces:**
- Consumes: `DXVK_INCLUDE`, `DXVK_LIB` from Task 0.3.
- Produces: `d3d9ff_probe` that writes `out.ppm` (P6, 512×512) and exits 0; `ppm_diff.py a.ppm b.ppm [--tol 8] [--max-bad 0.005]` exits 0 when the fraction of pixels with any channel differing by more than `tol` is ≤ `max-bad`.

- [ ] **Step 1: Write the failing diff-tool test**

```python
# port/scripts/test_ppm_diff.py
import pathlib, tempfile, unittest
import ppm_diff

def write_ppm(path, w, h, rgb):
    path.write_bytes(b"P6\n%d %d\n255\n" % (w, h) + bytes(rgb) * (w * h))

class PpmDiffTest(unittest.TestCase):
    def test_identical_images_have_no_bad_pixels(self):
        with tempfile.TemporaryDirectory() as d:
            a, b = pathlib.Path(d, "a.ppm"), pathlib.Path(d, "b.ppm")
            write_ppm(a, 4, 4, (10, 20, 30)); write_ppm(b, 4, 4, (10, 20, 30))
            self.assertEqual(ppm_diff.compare(a, b, tol=8), (0, 16))

    def test_small_differences_within_tolerance_pass(self):
        with tempfile.TemporaryDirectory() as d:
            a, b = pathlib.Path(d, "a.ppm"), pathlib.Path(d, "b.ppm")
            write_ppm(a, 4, 4, (10, 20, 30)); write_ppm(b, 4, 4, (15, 20, 30))
            self.assertEqual(ppm_diff.compare(a, b, tol=8), (0, 16))

    def test_large_difference_counts_every_pixel(self):
        with tempfile.TemporaryDirectory() as d:
            a, b = pathlib.Path(d, "a.ppm"), pathlib.Path(d, "b.ppm")
            write_ppm(a, 4, 4, (0, 0, 0)); write_ppm(b, 4, 4, (0, 0, 255))
            self.assertEqual(ppm_diff.compare(a, b, tol=8), (16, 16))

    def test_size_mismatch_raises(self):
        with tempfile.TemporaryDirectory() as d:
            a, b = pathlib.Path(d, "a.ppm"), pathlib.Path(d, "b.ppm")
            write_ppm(a, 4, 4, (0, 0, 0)); write_ppm(b, 2, 2, (0, 0, 0))
            with self.assertRaises(ValueError):
                ppm_diff.compare(a, b, tol=8)

if __name__ == "__main__":
    unittest.main()
```

- [ ] **Step 2: Run it to verify it fails**

Run: `cd port/scripts && python3 -m unittest test_ppm_diff -v`
Expected: FAIL with `ModuleNotFoundError: No module named 'ppm_diff'`

- [ ] **Step 3: Write the diff tool**

```python
#!/usr/bin/env python3
# port/scripts/ppm_diff.py — compare two binary PPMs written by our probes.
import argparse
import pathlib
import sys


def read_ppm(path):
    data = pathlib.Path(path).read_bytes()
    magic, dims, maxval, pixels = data.split(b"\n", 3)
    if magic != b"P6" or maxval != b"255":
        raise ValueError(f"{path}: not an 8-bit P6 PPM")
    w, h = (int(v) for v in dims.split())
    if len(pixels) != w * h * 3:
        raise ValueError(f"{path}: expected {w * h * 3} pixel bytes, got {len(pixels)}")
    return w, h, pixels


def compare(a, b, tol):
    wa, ha, pa = read_ppm(a)
    wb, hb, pb = read_ppm(b)
    if (wa, ha) != (wb, hb):
        raise ValueError(f"size mismatch {wa}x{ha} vs {wb}x{hb}")
    bad = 0
    for i in range(0, len(pa), 3):
        if max(abs(pa[i] - pb[i]), abs(pa[i + 1] - pb[i + 1]), abs(pa[i + 2] - pb[i + 2])) > tol:
            bad += 1
    return bad, wa * ha


if __name__ == "__main__":
    ap = argparse.ArgumentParser()
    ap.add_argument("a"); ap.add_argument("b")
    ap.add_argument("--tol", type=int, default=8)
    ap.add_argument("--max-bad", type=float, default=0.005)
    args = ap.parse_args()
    bad, total = compare(args.a, args.b, args.tol)
    frac = bad / total
    print(f"{bad}/{total} pixels differ by more than {args.tol} ({frac:.4%})")
    sys.exit(0 if frac <= args.max_bad else 1)
```

- [ ] **Step 4: Run the tests to verify they pass**

Run: `cd port/scripts && python3 -m unittest test_ppm_diff -v`
Expected: 4 tests PASS

- [ ] **Step 5: Write the probe**

```cmake
# port/probes/d3d9ff/CMakeLists.txt
cmake_minimum_required(VERSION 3.24)
project(d3d9ff_probe CXX)
set(CMAKE_CXX_STANDARD 20)
include(FetchContent)
if(WIN32)
  FetchContent_Declare(SDL3 GIT_REPOSITORY https://github.com/libsdl-org/SDL GIT_TAG release-3.2.0)
  set(SDL_STATIC ON CACHE BOOL "" FORCE)
  set(SDL_SHARED OFF CACHE BOOL "" FORCE)
  FetchContent_MakeAvailable(SDL3)
  add_executable(d3d9ff_probe main.cpp)
  target_link_libraries(d3d9ff_probe PRIVATE SDL3::SDL3-static d3d9)
else()
  find_package(SDL3 REQUIRED CONFIG)
  if(NOT DXVK_INCLUDE OR NOT DXVK_LIB)
    message(FATAL_ERROR "Pass -DDXVK_INCLUDE=... -DDXVK_LIB=... (printed by port/scripts/build-dxvk.sh)")
  endif()
  add_executable(d3d9ff_probe main.cpp)
  target_include_directories(d3d9ff_probe PRIVATE ${DXVK_INCLUDE} ${DXVK_INCLUDE}/../windows ${DXVK_INCLUDE}/../directx)
  target_link_libraries(d3d9ff_probe PRIVATE SDL3::SDL3 ${DXVK_LIB})
endif()
```

```cpp
// port/probes/d3d9ff/main.cpp
// Draws one frame using the fixed-function D3D9 features RAN relies on most
// (pretransformed + lit geometry, DXT1 texture, texture-stage modulate, alpha
// blending applied via a state block), reads the back buffer back and writes
// out.ppm. Built for Windows (reference, run under Wine) and for DXVK-native.
#include <SDL3/SDL.h>
#ifdef _WIN32
#include <windows.h>
#endif
#include <d3d9.h>
#include <cstdint>
#include <cstdio>

static const int W = 512, H = 512;

struct VtxRHW    { float x, y, z, rhw; DWORD color; };
struct VtxRHWTex { float x, y, z, rhw; DWORD color; float u, v; };
struct VtxLit    { float x, y, z, nx, ny, nz; };
static const DWORD FVF_RHW    = D3DFVF_XYZRHW | D3DFVF_DIFFUSE;
static const DWORD FVF_RHWTEX = D3DFVF_XYZRHW | D3DFVF_DIFFUSE | D3DFVF_TEX1;
static const DWORD FVF_LIT    = D3DFVF_XYZ | D3DFVF_NORMAL;

static int fail(const char* what, HRESULT hr)
{
    std::printf("FAIL %s hr=0x%08lx\n", what, (unsigned long)hr);
    return 1;
}

// D3D9 maps pixel centres to integer coordinates, so shift by -0.5 for exact coverage.
static void quad(VtxRHWTex* v, float x0, float y0, float x1, float y1, DWORD c)
{
    x0 -= 0.5f; y0 -= 0.5f; x1 -= 0.5f; y1 -= 0.5f;
    v[0] = {x0, y0, 0.5f, 1.f, c, 0.f, 0.f};
    v[1] = {x1, y0, 0.5f, 1.f, c, 1.f, 0.f};
    v[2] = {x0, y1, 0.5f, 1.f, c, 0.f, 1.f};
    v[3] = {x1, y1, 0.5f, 1.f, c, 1.f, 1.f};
}

// One opaque 4x4 DXT1 block of a single RGB565 colour (colour0 = c, all indices 0).
static void solidBlock(uint8_t* dst, uint16_t c)
{
    dst[0] = (uint8_t)(c & 0xFF); dst[1] = (uint8_t)(c >> 8);
    for (int i = 2; i < 8; ++i) dst[i] = 0;
}

int main(int, char**)
{
    if (!SDL_Init(SDL_INIT_VIDEO)) { std::printf("FAIL SDL_Init %s\n", SDL_GetError()); return 1; }
    SDL_Window* win = SDL_CreateWindow("d3d9ff_probe", W, H, 0);
    if (!win) { std::printf("FAIL SDL_CreateWindow %s\n", SDL_GetError()); return 1; }
#ifdef _WIN32
    HWND hwnd = (HWND)SDL_GetPointerProperty(SDL_GetWindowProperties(win), SDL_PROP_WINDOW_WIN32_HWND_POINTER, nullptr);
#else
    HWND hwnd = (HWND)win;   // DXVK-native: the window handle IS the SDL_Window*
#endif

    IDirect3D9* d3d = Direct3DCreate9(D3D_SDK_VERSION);
    if (!d3d) { std::printf("FAIL Direct3DCreate9\n"); return 1; }
    D3DPRESENT_PARAMETERS pp = {};
    pp.Windowed = TRUE;
    pp.SwapEffect = D3DSWAPEFFECT_DISCARD;
    pp.BackBufferFormat = D3DFMT_X8R8G8B8;
    pp.BackBufferWidth = W;
    pp.BackBufferHeight = H;
    pp.hDeviceWindow = hwnd;
    pp.EnableAutoDepthStencil = TRUE;
    pp.AutoDepthStencilFormat = D3DFMT_D24S8;
    pp.PresentationInterval = D3DPRESENT_INTERVAL_ONE;
    IDirect3DDevice9* dev = nullptr;
    HRESULT hr = d3d->CreateDevice(D3DADAPTER_DEFAULT, D3DDEVTYPE_HAL, hwnd,
                                   D3DCREATE_MIXED_VERTEXPROCESSING, &pp, &dev);
    if (FAILED(hr)) return fail("CreateDevice", hr);

    // 8x8 DXT1 checker: red/green on the top block row, green/red below.
    IDirect3DTexture9* tex = nullptr;
    hr = dev->CreateTexture(8, 8, 1, 0, D3DFMT_DXT1, D3DPOOL_MANAGED, &tex, nullptr);
    if (FAILED(hr)) return fail("CreateTexture DXT1", hr);
    D3DLOCKED_RECT lr;
    tex->LockRect(0, &lr, nullptr, 0);
    uint8_t* bits = (uint8_t*)lr.pBits;
    solidBlock(bits + 0, 0xF800); solidBlock(bits + 8, 0x07E0);
    solidBlock(bits + lr.Pitch + 0, 0x07E0); solidBlock(bits + lr.Pitch + 8, 0xF800);
    tex->UnlockRect(0);

    // State block that turns on standard alpha blending (RAN records hundreds of these).
    IDirect3DStateBlock9* blend = nullptr;
    dev->BeginStateBlock();
    dev->SetRenderState(D3DRS_ALPHABLENDENABLE, TRUE);
    dev->SetRenderState(D3DRS_SRCBLEND, D3DBLEND_SRCALPHA);
    dev->SetRenderState(D3DRS_DESTBLEND, D3DBLEND_INVSRCALPHA);
    hr = dev->EndStateBlock(&blend);
    if (FAILED(hr)) return fail("EndStateBlock", hr);

    dev->Clear(0, nullptr, D3DCLEAR_TARGET | D3DCLEAR_ZBUFFER, D3DCOLOR_XRGB(32, 64, 96), 1.0f, 0);
    dev->BeginScene();
    dev->SetRenderState(D3DRS_ZENABLE, FALSE);
    dev->SetRenderState(D3DRS_CULLMODE, D3DCULL_NONE);
    dev->SetRenderState(D3DRS_ALPHABLENDENABLE, FALSE);

    // 1) Vertex-coloured pretransformed triangle (top right).
    dev->SetRenderState(D3DRS_LIGHTING, FALSE);
    dev->SetTexture(0, nullptr);
    dev->SetTextureStageState(0, D3DTSS_COLOROP, D3DTOP_SELECTARG1);
    dev->SetTextureStageState(0, D3DTSS_COLORARG1, D3DTA_DIFFUSE);
    dev->SetTextureStageState(0, D3DTSS_ALPHAOP, D3DTOP_SELECTARG1);
    dev->SetTextureStageState(0, D3DTSS_ALPHAARG1, D3DTA_DIFFUSE);
    VtxRHW tri[3] = {
        {300.f, 40.f, 0.5f, 1.f, D3DCOLOR_XRGB(255, 0, 0)},
        {480.f, 40.f, 0.5f, 1.f, D3DCOLOR_XRGB(0, 255, 0)},
        {390.f, 200.f, 0.5f, 1.f, D3DCOLOR_XRGB(0, 0, 255)}};
    dev->SetFVF(FVF_RHW);
    dev->DrawPrimitiveUP(D3DPT_TRIANGLELIST, 1, tri, sizeof(VtxRHW));

    // 2) DXT1 checker, MODULATE with half-grey diffuse, point sampling (top left).
    dev->SetTexture(0, tex);
    dev->SetSamplerState(0, D3DSAMP_MINFILTER, D3DTEXF_POINT);
    dev->SetSamplerState(0, D3DSAMP_MAGFILTER, D3DTEXF_POINT);
    dev->SetTextureStageState(0, D3DTSS_COLOROP, D3DTOP_MODULATE);
    dev->SetTextureStageState(0, D3DTSS_COLORARG1, D3DTA_TEXTURE);
    dev->SetTextureStageState(0, D3DTSS_COLORARG2, D3DTA_DIFFUSE);
    VtxRHWTex q[4];
    quad(q, 32.f, 32.f, 288.f, 288.f, D3DCOLOR_XRGB(128, 128, 128));
    dev->SetFVF(FVF_RHWTEX);
    dev->DrawPrimitiveUP(D3DPT_TRIANGLESTRIP, 2, q, sizeof(VtxRHWTex));

    // 3) Fixed-function lit quad: blue material, one directional light (bottom right).
    D3DMATRIX ident = {{{1,0,0,0, 0,1,0,0, 0,0,1,0, 0,0,0,1}}};
    const float zn = 1.f, zf = 100.f, Q = zf / (zf - zn);
    D3DMATRIX proj = {{{1,0,0,0, 0,1,0,0, 0,0,Q,1, 0,0,-zn * Q,0}}};   // 90° FOV, aspect 1, LH
    dev->SetTransform(D3DTS_WORLD, &ident);
    dev->SetTransform(D3DTS_VIEW, &ident);
    dev->SetTransform(D3DTS_PROJECTION, &proj);
    dev->SetRenderState(D3DRS_LIGHTING, TRUE);
    dev->SetRenderState(D3DRS_AMBIENT, 0);
    D3DLIGHT9 light = {};
    light.Type = D3DLIGHT_DIRECTIONAL;
    light.Diffuse = {1.f, 1.f, 1.f, 1.f};
    light.Direction = {0.f, 0.f, 1.f};
    dev->SetLight(0, &light);
    dev->LightEnable(0, TRUE);
    D3DMATERIAL9 mtl = {};
    mtl.Diffuse = {0.f, 0.f, 1.f, 1.f};
    dev->SetMaterial(&mtl);
    dev->SetTexture(0, nullptr);
    dev->SetTextureStageState(0, D3DTSS_COLOROP, D3DTOP_SELECTARG1);
    dev->SetTextureStageState(0, D3DTSS_COLORARG1, D3DTA_DIFFUSE);
    VtxLit lit[4] = {
        {1.5f, -1.5f, 5.f, 0.f, 0.f, -1.f}, {3.5f, -1.5f, 5.f, 0.f, 0.f, -1.f},
        {1.5f, -3.5f, 5.f, 0.f, 0.f, -1.f}, {3.5f, -3.5f, 5.f, 0.f, 0.f, -1.f}};
    dev->SetFVF(FVF_LIT);
    dev->DrawPrimitiveUP(D3DPT_TRIANGLESTRIP, 2, lit, sizeof(VtxLit));
    dev->SetRenderState(D3DRS_LIGHTING, FALSE);

    // 4) 50%-alpha white quad over everything, blending enabled by applying the state block.
    blend->Apply();
    dev->SetTextureStageState(0, D3DTSS_ALPHAOP, D3DTOP_SELECTARG1);
    dev->SetTextureStageState(0, D3DTSS_ALPHAARG1, D3DTA_DIFFUSE);
    quad(q, 160.f, 160.f, 400.f, 400.f, D3DCOLOR_ARGB(128, 255, 255, 255));
    dev->SetFVF(FVF_RHWTEX);
    dev->DrawPrimitiveUP(D3DPT_TRIANGLESTRIP, 2, q, sizeof(VtxRHWTex));
    dev->EndScene();

    // Read the back buffer back and write out.ppm (X8R8G8B8 = B,G,R,X in memory).
    IDirect3DSurface9 *bb = nullptr, *sys = nullptr;
    dev->GetBackBuffer(0, 0, D3DBACKBUFFER_TYPE_MONO, &bb);
    hr = dev->CreateOffscreenPlainSurface(W, H, D3DFMT_X8R8G8B8, D3DPOOL_SYSTEMMEM, &sys, nullptr);
    if (FAILED(hr)) return fail("CreateOffscreenPlainSurface", hr);
    hr = dev->GetRenderTargetData(bb, sys);
    if (FAILED(hr)) return fail("GetRenderTargetData", hr);
    sys->LockRect(&lr, nullptr, D3DLOCK_READONLY);
    FILE* f = std::fopen("out.ppm", "wb");
    std::fprintf(f, "P6\n%d %d\n255\n", W, H);
    for (int y = 0; y < H; ++y) {
        const uint8_t* row = (const uint8_t*)lr.pBits + y * lr.Pitch;
        for (int x = 0; x < W; ++x) {
            const uint8_t rgb[3] = {row[x * 4 + 2], row[x * 4 + 1], row[x * 4 + 0]};
            std::fwrite(rgb, 1, 3, f);
        }
    }
    std::fclose(f);
    sys->UnlockRect();
    dev->Present(nullptr, nullptr, nullptr, nullptr);
    std::printf("OK wrote out.ppm\n");

    sys->Release(); bb->Release(); blend->Release(); tex->Release(); dev->Release(); d3d->Release();
    SDL_Quit();
    return 0;
}
```

- [ ] **Step 6: Add a CI job that builds the Windows reference binary**

```yaml
# .github/workflows/port-probe.yml
name: Port probe (Windows reference)
on:
  workflow_dispatch:
  push:
    paths: ["port/probes/d3d9ff/**"]
jobs:
  build:
    runs-on: windows-2022
    steps:
      - uses: actions/checkout@v4
      - name: Configure (Win32)
        run: cmake -S port/probes/d3d9ff -B build -A Win32
      - name: Build
        run: cmake --build build --config Release
      - uses: actions/upload-artifact@v4
        with:
          name: d3d9ff-probe-win32
          path: build/Release/d3d9ff_probe.exe
```

- [ ] **Step 7: Produce the reference image under the existing Wine**

Run (after pushing the branch and running the workflow):
```bash
gh workflow run port-probe.yml --ref port/macos-spike
gh run watch "$(gh run list --workflow port-probe.yml --limit 1 --json databaseId -q '.[0].databaseId')"
mkdir -p port/probes/d3d9ff/ref && cd port/probes/d3d9ff/ref
gh run download --name d3d9ff-probe-win32
WINEPREFIX="$HOME/.wine-ran10-client" /Users/t31k/Projects/RAN/wine/w10/wswine.bundle/bin/wine d3d9ff_probe.exe
```
Expected: `OK wrote out.ppm` and `port/probes/d3d9ff/ref/out.ppm` exists. (Same Wine binary and prefix as `~/Projects/RAN/run-client.command`.) Ask the user to open `out.ppm` in Preview and confirm: grey-tinted red/green checker top-left, RGB triangle top-right, blue square bottom-right, translucent white square over the middle.

- [ ] **Step 8: Build and run the native probe on DXVK**

Run:
```bash
eval "$(port/scripts/build-dxvk.sh | grep '^DXVK_')"
cmake -S port/probes/d3d9ff -B port/probes/d3d9ff/build -G Ninja -DDXVK_INCLUDE="$DXVK_INCLUDE" -DDXVK_LIB="$DXVK_LIB"
cmake --build port/probes/d3d9ff/build
cd port/probes/d3d9ff/build
DXVK_WSI_DRIVER=SDL3 VK_DRIVER_FILES="$(brew --prefix molten-vk)/etc/vulkan/icd.d/MoltenVK_icd.json" \
  DYLD_LIBRARY_PATH="$(brew --prefix vulkan-loader)/lib:$(dirname "$DXVK_LIB")" ./d3d9ff_probe
```
Expected: `OK wrote out.ppm`. A `FAIL <call> hr=…` line names the first D3D9 call DXVK/MoltenVK can't do. Record it in the spike report; if it can't be fixed within the Task 0.3 timebox, that's the Path B trigger.

- [ ] **Step 9: Compare native vs Wine**

Run: `python3 port/scripts/ppm_diff.py port/probes/d3d9ff/ref/out.ppm port/probes/d3d9ff/build/out.ppm`
Expected: exit 0 (≤ 0.5% of pixels differ by more than 8/255). If it fails, save both images plus a note of which region differs (checker / triangle / lit quad / blend) for the report.

- [ ] **Step 10: Commit**

```bash
git add port/probes/d3d9ff/main.cpp port/probes/d3d9ff/CMakeLists.txt port/probes/d3d9ff/ref/out.ppm \
        port/scripts/ppm_diff.py port/scripts/test_ppm_diff.py .github/workflows/port-probe.yml
git commit -m "port: fixed-function D3D9 probe, Wine reference image, pixel-diff tool"
```

### Task 0.5: Compat types + engine compile probe

Measures how much of the engine compiles on macOS clang today, and fixes the Win32 type sizes that all later work depends on.

**Files:**
- Create: `port/compat/include/win32_types.h`
- Create: `port/tests/compat_types_test.cpp`
- Create: `port/scripts/compile_probe.sh`
- Create (generated): `docs/port/compile-probe.md`

**Interfaces:**
- Produces: `win32_types.h` with fixed-size `BYTE WORD DWORD LONG ULONG BOOL INT UINT LONGLONG ULONGLONG __time64_t HRESULT WCHAR`, `TRUE/FALSE`, `S_OK/S_FALSE/E_FAIL`, `SUCCEEDED/FAILED`, `MAX_PATH`. Phase 1 grows this into the full compat layer (and reconciles it with DXVK's `windows_base.h` if Path A wins, so types are defined once).

- [ ] **Step 1: Write the failing test**

```cpp
// port/tests/compat_types_test.cpp — sizes must match the 32-bit Windows build,
// because these types appear in pack(1) packets and serialized data files.
#include "win32_types.h"

static_assert(sizeof(BYTE) == 1);
static_assert(sizeof(WORD) == 2);
static_assert(sizeof(DWORD) == 4);
static_assert(sizeof(LONG) == 4);
static_assert(sizeof(ULONG) == 4);
static_assert(sizeof(BOOL) == 4);
static_assert(sizeof(INT) == 4);
static_assert(sizeof(UINT) == 4);
static_assert(sizeof(LONGLONG) == 8);
static_assert(sizeof(ULONGLONG) == 8);
static_assert(sizeof(__time64_t) == 8);
static_assert(sizeof(HRESULT) == 4);
static_assert(sizeof(WCHAR) == 2);
static_assert(sizeof(wchar_t) == 2, "compile with -fshort-wchar");
static_assert(SUCCEEDED(S_OK) && !SUCCEEDED(E_FAIL));
int main() { return 0; }
```

- [ ] **Step 2: Run it to verify it fails**

Run: `clang++ -std=c++20 -fshort-wchar -Iport/compat/include port/tests/compat_types_test.cpp -o /tmp/compat_types_test`
Expected: FAIL with `'win32_types.h' file not found`

- [ ] **Step 3: Write the header**

```cpp
// port/compat/include/win32_types.h — fixed-size Win32 base types for the native macOS build.
// Sizes match the 32-bit Windows build (see port/tests/compat_types_test.cpp).
#pragma once
#include <cstdint>

typedef uint8_t   BYTE;
typedef uint16_t  WORD;
typedef uint32_t  DWORD;
typedef int32_t   LONG;
typedef uint32_t  ULONG;
typedef int32_t   BOOL;
typedef int32_t   INT;
typedef uint32_t  UINT;
typedef int64_t   LONGLONG;
typedef uint64_t  ULONGLONG;
typedef int64_t   __time64_t;
typedef int32_t   HRESULT;
typedef wchar_t   WCHAR;   // 2 bytes: the build uses -fshort-wchar

#ifndef TRUE
#define TRUE 1
#endif
#ifndef FALSE
#define FALSE 0
#endif
#define S_OK            ((HRESULT)0)
#define S_FALSE         ((HRESULT)1)
#define E_FAIL          ((HRESULT)0x80004005L)
#define SUCCEEDED(hr)   (((HRESULT)(hr)) >= 0)
#define FAILED(hr)      (((HRESULT)(hr)) < 0)
#define MAX_PATH        260
```

- [ ] **Step 4: Run the test to verify it passes**

Run: `clang++ -std=c++20 -fshort-wchar -Iport/compat/include port/tests/compat_types_test.cpp -o /tmp/compat_types_test && /tmp/compat_types_test && echo PASS`
Expected: `PASS`

- [ ] **Step 5: Write the compile probe**

```bash
#!/bin/bash
# port/scripts/compile_probe.sh — syntax-check every client .cpp with macOS clang and
# histogram the first error per file. Measurement only; it changes no sources.
set -uo pipefail
ROOT="$(cd "$(dirname "$0")/../.." && pwd)"
cd "$ROOT"
OUT="docs/port/compile-probe.md"
TMP="$(mktemp -d)"
DIRS=("[Lib]__Engine" "[Lib]__EngineUI" "[Lib]__EngineSound" "[Lib]__NetClient" "[Lib]__RanClient" "[Lib]__RanClientUI" "[Lib]__MfcEx" "[Client]__Game")
INCS=(-Iport/compat/include -IDependency -IDependency/directx -IDependency/NetGlobal)
for d in "${DIRS[@]}"; do while IFS= read -r inc; do INCS+=("-I$inc"); done < <(find "$d" -type d -name Sources); done
TOTAL=0; OK=0
while IFS= read -r -d '' f; do
  TOTAL=$((TOTAL+1))
  if clang++ -std=c++20 -fsyntax-only -fshort-wchar -fms-extensions -Wno-everything -ferror-limit=1 \
       -include win32_types.h "${INCS[@]}" "$f" 2> "$TMP/err" ; then
    OK=$((OK+1))
  else
    grep -m1 'error:' "$TMP/err" | sed -E 's/^.*error: //; s/'"'"'[^'"'"']*'"'"'/X/g' >> "$TMP/firsts"
  fi
done < <(find "${DIRS[@]}" -name '*.cpp' -print0)
{
  echo "# Compile probe (macOS clang, syntax only)"
  echo
  echo "Generated by \`port/scripts/compile_probe.sh\` on $(date +%F). $OK / $TOTAL files pass."
  echo
  echo "## Top first-errors (identifiers normalised to X)"
  echo
  echo "| Count | First error |"
  echo "|---|---|"
  sort "$TMP/firsts" | uniq -c | sort -rn | head -40 | awk '{c=$1; $1=""; sub(/^ /,""); print "| " c " | " $0 " |"}'
} > "$OUT"
cat "$OUT" | head -20
```

Run: `chmod +x port/scripts/compile_probe.sh && port/scripts/compile_probe.sh`
Expected: a pass count near zero on this first run (most files include `<windows.h>`/`<afx*.h>`), and an error histogram dominated by missing headers. That histogram is the Phase 1 work queue. The probe takes several minutes.

- [ ] **Step 6: Commit**

```bash
git add port/compat/include/win32_types.h port/tests/compat_types_test.cpp port/scripts/compile_probe.sh docs/port/compile-probe.md
git commit -m "port: fixed-size Win32 types + engine compile probe baseline"
```

### Task 0.6: Spike report and go/no-go

**Files:**
- Create: `docs/port/spike-report.md`

- [ ] **Step 1: Write the report with these exact sections, filled from Tasks 0.1–0.5**

```markdown
# Native macOS spike report (YYYY-MM-DD)

## Cmd+Tab (Task 0.2)
- windowed / desktop-fullscreen / spaces-fullscreen: PASS|FAIL (paste cmdtab_check.sh lines)
- Manual click mapping after Cmd+Tab + minimize: HIT×4 | details

## DXVK on macOS (Tasks 0.3–0.4)
- DXVK tag: …   Patches needed: N (list files)
- Device creation on MoltenVK: OK | FAIL <call> hr=…
- Pixel diff vs Wine: <bad>/<total> (<pct>) → PASS|FAIL; differing regions: …

## Compile probe (Task 0.5)
- Files passing syntax check: OK / TOTAL
- Top 5 error classes: …

## Decision
- [ ] GO Path A (DXVK): Cmd+Tab PASS and DXVK diff PASS
- [ ] GO Path B (own Metal shim, +6–8 weeks in Phase 3): Cmd+Tab PASS, DXVK blocked
- [ ] STOP: Cmd+Tab FAILs natively too (then the window problem is macOS-level and porting would not fix it)

## Revised Phase 1–5 estimate
…
```

- [ ] **Step 2: Commit, and ask the user to review the decision**

```bash
git add docs/port/spike-report.md
git commit -m "port: spike report + go/no-go decision"
```

---

# Phases 1–5 — Roadmap

Each phase gets its own step-by-step plan file (`docs/superpowers/plans/<date>-native-mac-phaseN-<name>.md`) written when the phase starts, because its exact tasks depend on the spike results and the previous phase's inventory/probe numbers. Listed here: scope, design, and the tests that gate the phase (including the Review Focus tests).

## Phase 1 — Portable codebase, Windows still builds (4–6 weeks)

**Scope**
- Add a root `CMakeLists.txt` that builds the client libraries + `Game` on macOS (clang, arm64, `-fshort-wchar -fms-extensions`), alongside the untouched `.sln`/`.vcxproj` files.
- Cherry-pick the source changes from `upstream/x64-support` (06d1594): SerialFile/SerialMemory/ByteStream/basestream/seqrandom, inline-asm removal in DxSkinMesh9_CPU/RENDERPARAM/DebugSet, SAnimation, DxFrameMesh. Leave out `.tlog`/build files.
- Grow `port/compat/` into: `windows.h` facade (types, `HWND`/`HANDLE` as opaque pointers, `MAX_PATH`, string helpers `_stricmp/_snprintf/strcpy_s/StringCchCopy`), 2-byte `wcs*` functions, `CRITICAL_SECTION` → `std::recursive_mutex`, `CreateThread/_beginthreadex/WaitForSingleObject` → `std::thread` + events, `GetTickCount/timeGetTime/QueryPerformanceCounter` → `std::chrono::steady_clock`, registry → an ini file under the user data dir, `GetModuleFileName` → `_NSGetExecutablePath`, `MessageBox` → `SDL_ShowSimpleMessageBox`, `MultiByteToWideChar/WideCharToMultiByte` → iconv (CP949 ↔ UTF-16).
- MFC: a minimal `CString` (CP949 bytes, `Format/GetLength/Mid/Find/MakeLower…` as used), `CPoint/CRect/CSize`, `CTime` over `__time64_t`, `CArray/CList/CMap` over STL. The `CWinApp/CWnd` shell in `[Client]__Game` is replaced in Phase 2, not shimmed.
- Inline asm (remaining files), SEH `__try` (3 files → plain code or signal handler), SSE intrinsics (6 files → `sse2neon.h` (MIT) or scalar).
- Anti-cheat/vendor DLL code (`Protection.cpp`, GameGuard, EGames plugin): compiled out on macOS via `#ifdef _WIN32`.

**Gating tests**
- **P1.1** `compile_probe.sh`: 100% of client `.cpp` files pass the syntax check on macOS.
- **P1.2** Win32 CI (`build.yml`) green on every commit of the phase.
- **P1.3 (Review Focus #3)** Golden-size check: a generated `port/tests/msg_sizes.cpp` (struct names extracted by a script from `GLMsg/*.h` and the other `#pragma pack(1)` headers) prints `sizeof` of every packet struct. CI builds it as Win32 and commits the output as `port/tests/golden/msg_sizes_win32.txt`; the macOS build runs the same program and the test fails on any difference. Also covers structs read from data files (`SITEM`, `GLCHARLOGIC` save blocks, etc.).
- **P1.4** CP949 round-trip: `CString` + `MultiByteToWideChar` shim converts every line of the decrypted `ItemStrTable.txt` CP949→UTF-16→CP949 byte-identically.
- **P1.5 (Review Focus #4)** Path resolver: walk every path string the loaders build (log them from a Wine run with `[PATHDBG]`, or extract from data files) and resolve each against `client/data` with backslash→slash + case-insensitive lookup; 0 unresolved paths that exist on Windows.

## Phase 2 — Platform layer (3–4 weeks)

**Scope**
- Replace the MFC `CBasicApp/CBasicWnd` shell (`[Client]__Game/Sources/Basic*.cpp`) with an SDL3 `main()` that creates the window (desktop-fullscreen by default, windowed option), runs the game's existing frame loop (`FrameMove/Render`), and forwards SDL window events (focus, resize, minimize) to the same engine hooks `WM_ACTIVATEAPP`/`WM_SIZE` reach today.
- Input: keep `DxInputDevice`'s public API; replace its DirectInput internals with SDL keyboard/mouse state (`SDL_Scancode` → `DIK_*` table, mouse buttons/wheel/relative motion). Focus loss clears pressed keys; focus gain re-syncs. No acquire/unacquire step exists anymore, which is where the Wine Cmd+Tab input bug lived.
- IME: `SDL_StartTextInput` + `SDL_EVENT_TEXT_EDITING/TEXT_INPUT` drive `DXInputString`/`IMEEdit` (composition string + committed text), converted UTF-8 → CP949.
- Sound: `[Lib]__EngineSound` keeps its API; DirectSound/DirectSound3D buffers → miniaudio (2D + 3D positional, volume, loop). Ogg Vorbis decoding stays.
- Network: `[Lib]__NetClient` `WSAEventSelect` loop → non-blocking BSD socket + `poll()`; `ioctlsocket` → `fcntl/ioctl`. Same buffers and framing.
- User data dir: `~/Library/Application Support/RanOdyssey/` for cache, logs (`errlog`), settings; game data read from the bundle.

**Gating tests**
- **P2.1** Headless login: the native binary (rendering stubbed) connects to the production login server on the VPS with a test account and logs the received character list; byte-compare the login packets with a Wine-client capture (`[JOINDBG]`).
- **P2.2** Input mapping unit test: every `SDL_Scancode` the game uses maps to the same `DIK_*` as the Windows keyboard layout.
- **P2.3** `cmdtab_check.sh` adapted to the game binary: frame unchanged and input live after 2 Cmd+Tab cycles.
- **P2.4 (Review Focus #2)** IME byte test: feed SDL text events for a fixed Korean sentence; the chat packet's `szChatMsg` bytes equal the Windows client's for the same sentence.

## Phase 3 — Graphics (8–12 weeks Path A; +6–8 weeks Path B)

**Scope**
- Device creation through SDL3 + DXVK (Path A) or our Metal shim (Path B). `d3dapp.cpp`/DXUT device enumeration simplified to one adapter/mode list from SDL.
- D3DX replacement library `port/d3dx/`: math (vectors/matrices/quaternions/planes, ~60 functions, each unit-tested against values captured from real D3DX on Windows CI), texture loading (DDS parser + stb_image for TGA/BMP/JPG/PNG; `D3DXCreateTextureFromFile*`, cube/volume variants), `D3DXLoadMeshFromX*` (text/binary `.x` used by ~9 sites), `D3DXCreateSprite`, `D3DXCreateLine`, `D3DXSaveTextureToFile` (screenshots → PNG).
- Shaders/effects: Windows CI job assembles every asm shader and compiles every `.fx` used by the 13 shader files / ~10 effect sites into bytecode blobs shipped in the data dir; native build loads blobs. `ID3DXEffect` replaced by a small runtime that sets the compiled VS/PS + constants for the techniques actually used; heavy optional effects (post-process, water reflection) can launch disabled behind a setting and be enabled one by one.
- Fonts: `d3dfont.cpp`, `D3DFontX.cpp`, `TextTexture.cpp` rasterize glyphs with CoreText (CP949 → UTF-16 → CTLine into a bitmap) instead of GDI, keeping their texture-atlas output format.
- Path B only: `port/d3d9metal/` implements the ~70 device methods in `docs/port/api-inventory.md`; fixed-function T&L and texture stages via one uber-shader keyed by a state hash; state blocks as recorded state deltas; DXT via Metal BC formats.

**Gating tests** (all vs reference screenshots from the Wine client, `ppm_diff.py`)
- **P3.1** Login screen. **P3.2** Character select with each class model. **P3.3** In world: one town, one field, one dungeon. **P3.4** Every map in `mapslist.ini` loads and renders a frame with no D3D errors (scripted teleport tour using the area-move bypass). **P3.5** Effects gallery: each skill effect file spawned once. **P3.6 (Review Focus #2)** UI text: chat, item tooltips, NPC dialog in Korean and English render with no tofu boxes. **P3.7** 60 fps at 1440×900 on the M3 in a busy town.

## Phase 4 — Gameplay parity (4–6 weeks)

**Scope:** play-through checklist against the Wine build; fix what differs. Crash reporting: replace BugTrap with a signal handler writing a backtrace to the user data dir.

**Gating tests**
- **G4.1 (Review Focus #1)** Cmd+Tab ×10, minimize/restore, fullscreen↔windowed toggle, display sleep/wake, unplug/plug external monitor: no frame drift, clicks hit, keyboard works, no device-lost black screen.
- **G4.2** Checklist: create char, move, combat, skills (incl. `maxskills`), items (`getitem`), trade, party, chat (Korean IME), area move, pets, quests, shop, upgrade (`maxupgrade`), death/revive, logout/login, exit (no monitor blackout).
- **G4.3** 2-hour soak: no crash, memory flat.

## Phase 5 — Ship (1–2 weeks)

**Scope:** `RanOdyssey.app` with the native binary + data, `Info.plist` (min macOS 14, arm64), hardened runtime; reuse `~/Projects/RAN/package-app.command` signing/notarizing/stapling (drop the Wine-prefix tarball steps); `bump-release.command` OTA ships the native binary; download page at t31k.com/ran points to the native DMG. Keep the Wine build downloadable for one release cycle.

**Gating tests**
- **P5.1** `spctl --assess --type execute -vv RanOdyssey.app` → accepted, notarized.
- **P5.2 (Review Focus #5)** Launch from a read-only copy of the app (`chflags uchg` or a mounted DMG): game runs, writes cache/logs/settings to Application Support, nothing written inside the bundle.
- **P5.3** Fresh-Mac install by a friend: install → login → play with no Terminal steps.

---

## Self-review notes

- Coverage: both user goals (Cmd+Tab, off Wine) have direct gates (Task 0.2, P2.3, G4.1; Global Constraint "no Wine" + P5.1). Every measured dependency layer (graphics, D3DX, text, input, IME, sound, network, Win32, MFC, inline asm/SEH/SSE, anti-cheat) maps to a phase.
- Placeholders: Phases 1–5 are intentionally roadmap-level; each gets its own detailed plan at phase start, informed by the spike's numbers. Phase 0 is complete and executable as written.
- Names are consistent across tasks: `inventory.py scan/render`, `ppm_diff.py compare`, `cmdtab_check.sh`, `build-dxvk.sh` `DXVK_INCLUDE/DXVK_LIB`, `win32_types.h`.
