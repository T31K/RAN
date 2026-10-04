# Native macOS client - developer guide

The goal: run the RAN Online EP7 client as a native Apple Silicon app (M1-M4), without Wine, so
Cmd+Tab, fullscreen and input behave like any Mac app. Windows keeps building from the same
sources (CI) the whole time.

- **Plan** (phases, gates, estimates): `docs/superpowers/plans/2026-10-03-native-macos-client.md`
- **Status** (what passes today, how to check each gate): `docs/port/progress.md`
- **Remaining link gaps** (generated): `docs/port/link-gaps.md`
- **Spike report** (why this approach, DXVK/KosmicKrisp result): `docs/port/spike-report.md`
- Branch: `port/macos-spike`

## How it works

```
 game sources (unchanged MFC/Win32/D3D9 code, CP949)
        |  compiled by clang with port/compat/include first on the include path
        v
 port/compat/include   Win32 + MFC + CRT stand-ins (headers): types, files, registry, sockets,
                       CString/CTime, message maps, codepage (iconv), mmio, ...
 port/platform         SDL3 main, window + event pump, DirectInput on SDL, input map, shims
 port/d3dx             D3DX replacement (math today; textures/meshes/effects next)
        |
        v
 DXVK-native d3d9 (port/third_party/dxvk-install) -> Vulkan -> KosmicKrisp (Mesa) -> Metal
```

**Startup is the game's own.** `port/platform/main_sdl.cpp` replaces only `WinMain`:
`theApp.InitInstance()` runs unchanged, `CBasicWnd::Create` calls `CWnd::CreateEx`, which the
compat layer routes to the platform hook that opens an SDL3 window (with DXVK-native the
`SDL_Window*` *is* the `HWND`), then the game's `CD3DApplication::Create` builds the D3D9 device
on it. `theApp.Run()` is the game's loop; its `PeekMessage/GetMessage` call the platform pump,
which turns SDL events into the window messages the game's real MFC message maps handle
(`WM_ACTIVATEAPP`, `WM_SIZE`, ...) and keyboard/mouse into DirectInput buffered events.

**Input** goes through `port/platform/dinput_sdl.cpp`, a DirectInput 8 implementation over SDL
events, so `DxInputDevice` is unchanged. `Acquire` cannot fail and focus loss releases held keys:
the Wine Cmd+Tab input loss has no counterpart.

**Text** stays CP949 bytes like on Windows. `WCHAR` is `char16_t` (2 bytes, same layout as
Windows). `MultiByteToWideChar/WideCharToMultiByte` use iconv and behave like Windows on invalid
input (substitute and continue).

**Files**: the game builds Windows paths (`app\\data\\...`). `GetModuleFileName` returns the exe
path with backslashes (the game finds its folder with `ReverseFind('\\')`); every open goes
through `ran_compat::ResolvePath`, which maps separators and matches case per path component.
`RAN_GAME_DIR=<client folder>` points the native binary at a client data folder.

## Layout

| Path | What |
|---|---|
| `port/compat/include/ran_compat.h` | entry point of the compat layer; every stand-in header includes it |
| `port/compat/include/win32/*.h` | kernel (threads/events/time), files + path resolver, codepage, wchar16, winsock (poll-based WSAEventSelect), registry (file-backed), shell folders, system (user32/gdi odds), crypt (MD5/RC4), mmio |
| `port/compat/include/mfc/*.h` | CString/CStringW, CTime/CRect..., CWnd/CWinApp/controls/CDC, message maps, CFile, WinInet stubs |
| `port/compat/include/*.h` | thin stand-ins for SDK header names (`afxwin.h`, `tchar.h`, `strsafe.h`, `mmsystem.h`, `d3dx9*.h` wrappers, `lzoconf.h`/`portab.h` wrappers, ...) |
| `port/platform/` | `main_sdl.cpp`, `dinput_sdl.cpp`, `input_map.h`, `input_queue.h`, `win_shims.cpp` |
| `port/d3dx/` | `d3dx9_math.cpp` |
| `port/tests/` | unit tests (CMake/ctest), `golden/` gate tools and reference data |
| `port/scripts/` | build, probe and gate scripts (below) |
| `port/native-excludes.txt` | sources left out of the native build, each with its reason |
| `port/patches/dxvk/` | our DXVK-native macOS patch |

## Commands

| Command | What it does |
|---|---|
| `port/scripts/run-tests.sh` | builds and runs every unit test (15 suites) |
| `port/scripts/build_native.sh` | compiles all native sources (incremental on source mtime - wipe `port/build/native/obj` after compat header changes), links `port/build/native/ran_client`, writes `docs/port/link-gaps.md` while symbols are missing |
| `port/scripts/compile_probe.sh` | syntax-checks every client source, writes `docs/port/compile-probe.md` |
| `port/scripts/compile_one.sh <file> [errors]` | one file with the native flags (`RAN_OBJ_OUT=x.o` to build an object) |
| `port/scripts/check-struct-sizes.sh` | P1.3: 995 network-message structs vs the Win32 sizes in `port/tests/golden/msg_sizes_win32.txt` |
| `port/scripts/check-cp949.sh` | P1.4: string tables round-trip through the text conversion |
| `port/scripts/check-paths.sh` | P1.5: every data file opens from Windows-style paths, also on a case-sensitive volume |
| `python3 port/scripts/client_sources.py` | the native source list (from the `.vcxproj` files minus excludes) |
| `python3 port/scripts/gen_struct_sizes.py` | regenerates the struct list for P1.3 |

Requirements (all free, Homebrew): `sdl3`, `mesa` (KosmicKrisp Vulkan driver), `libvorbis`,
`libogg`, `lzo`, `unixodbc` (headers only), `cmake`, `ninja`, `meson` (DXVK build).

Windows CI (`.github/workflows/build.yml`) builds the solution on every manual run/push to
master and produces the golden struct sizes (artifact `msg-sizes-win32`). The repo is public, so
Actions minutes are free.

## Rules for changing game sources

1. **Sources are CP949.** Never edit them directly with a UTF-8 tool: `iconv -f CP949 -t UTF-8`
   -> edit -> `iconv -f UTF-8 -t CP949`, then `git diff --numstat` must show only your lines.
2. **Windows behaviour must not change.** Prefer fixing the compat layer. When the source must
   change, write what MSVC already did, in standard C++: a reference bound to a temporary
   becomes a copy, `std::tolower` -> `::tolower`, gotos over initialisations get the
   declaration hoisted or a block scope, `inline` out-of-line definitions used from other files
   lose `inline`, `unsigned long` in on-disk/on-wire structs becomes `ULONG`/`LONG`.
3. **Platform-only code** in game sources goes under `#ifdef _WIN32` (always defined on
   Windows, never in the native build; the compat layer defines `WIN32` but not `_WIN32`).
4. Every compat behaviour gets a test in `port/tests/`.

## Traps already hit

- `<mach-o/dyld.h>` redefines `TRUE/FALSE` as an enum - never include it (declare what you need).
- `-fshort-wchar` silently breaks libc++; `-fms-compatibility` breaks Apple's SDK headers. Neither is used.
- MFC classes are `#pragma pack(4)` on Windows (`_AFX_PACKING`): `CTime` is 4-aligned (fixed in compat).
- `[Lib]` in a PowerShell path is a wildcard: use `-LiteralPath`.
- Some sources have upper-case extensions (`SHA.CPP`): always take sources from the `.vcxproj`.
- The exe project's objects must be linked directly (that is what brings in the global `theApp`).
- `NET_UPDATE_TRACINGCHAR` carries a `std::string` over the wire (also on Windows); the client only reads `updateNum`, which is safe.
- Shipped `SkillStrTable.txt` has 55 lines of invalid CP949; that is data damage, not a conversion bug.

## Running, testing, packaging

| Command | What it does |
|---|---|
| `port/scripts/run_native.sh [client-dir]` | runs the native client with DXVK on KosmicKrisp |
| `RAN_INPUT_SCRIPT="22:click 488 373; 31:text T31K; 49:key Return; 86:raise"` | scripted input for unattended runs (seconds: action) |
| `RAN_TRACE_INPUT=1`, `RAN_TRACE_AUDIO=1`, `DXVK_HUD=fps` | diagnostics |
| `port/scripts/last_crash.py` | backtrace from the newest macOS crash report (crashes also print one to stderr) |
| `port/scripts/package_native_app.sh [--with-game dir]` | self-contained `port/build/RanOdyssey Native.app` (libraries relinked into the bundle) |
| `port/scripts/check-file-struct-sizes.sh` | gate P1.6: on-disk struct images vs MSVC x86 |

## Next steps

1. Phase 4: exercise combat, skills, effects, inventory/NPC shops, map changes and long sessions;
   fix what differs from the Windows client.
2. Phase 5: Developer ID signing + notarisation + DMG for the native app (reuse the
   `package-app.command` flow), OTA updates as for the Wine app, then retire the Wine bundle.
3. Optional: a native `ID3DXFont`/`ID3DXSprite` (only the "D3DXFONT" font mode uses them),
   Developer ID-signed builds in CI.
