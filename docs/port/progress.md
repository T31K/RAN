# Native macOS client - progress

Plan: `docs/superpowers/plans/2026-10-03-native-macos-client.md`. Developer guide:
`docs/port/README.md`. Branch: `port/macos-spike`. Last updated 2026-10-04.

Overall: roughly a third of the plan. Phase 1 complete, Phase 2 about half, Phase 3 started.

## Phase 1 - portable codebase, Windows still builds: COMPLETE

| Gate | Status | How to check |
|---|---|---|
| P1.1 every client source compiles natively | **Done** - 799/799 (the game's own shell files included) | `port/scripts/compile_probe.sh` -> `docs/port/compile-probe.md` |
| P1.2 Windows CI green | **Done** - every pushed checkpoint builds (Release Win32) | GitHub Actions `Build` on `port/macos-spike` |
| P1.3 network-message struct sizes == Win32 | **Done** - all 995 wire structs (+ package `FILECONTEXT`) identical to MSVC x86. Found and fixed: MFC `CTime` is 4-aligned (`_AFX_PACKING`), `ulServerIP` was `unsigned long`. 5 structs holding STL objects are listed as non-wire with reasons | `port/scripts/check-struct-sizes.sh` (golden: `port/tests/golden/msg_sizes_win32.txt`, refreshed from CI artifact `msg-sizes-win32`) |
| P1.4 CP949 text round trip | **Done** - Item/Crow/SkillStrTable + UI tables, byte-exact (55 lines already corrupt in the shipped SkillStrTable degrade like Windows) | `port/scripts/check-cp949.sh` |
| P1.5 Windows paths resolve to data files | **Done** - 4626 files x 3 spellings, also on a case-sensitive volume | `port/scripts/check-paths.sh` |

Compat/platform/D3DX unit tests: 15 suites, all passing (`port/scripts/run-tests.sh`).

## Phase 2 - platform layer (started)

- **The native client builds and links except for 29 symbols** (`port/scripts/build_native.sh`,
  list in `docs/port/link-gaps.md`): 26 D3DX mesh/texture/effect/sprite/font functions and the
  `.x` loader (Phase 3), and DirectSound (2) - the sound backend below.
- Input: **done** - DirectInput 8 implemented on SDL3 events (`port/platform/dinput_sdl.cpp`,
  test `dinput_sdl_test`); DxInputDevice is unchanged. Acquire never fails and focus loss
  releases held keys, so the Wine Cmd+Tab input bug has no counterpart.
- Startup path is the game's own: SDL3 `main` (`port/platform/main_sdl.cpp`) -> `theApp.InitInstance()`
  -> `CBasicWnd::Create` -> `CWnd::CreateEx` (SDL window, = DXVK's HWND) -> `CD3DApplication::Create`
  -> `theApp.Run()`, whose `PeekMessage/GetMessage` pump SDL events into the game's real MFC
  message maps (WM_ACTIVATEAPP / WM_SIZE / ...).
- P2.2 input map: **done** (`port/platform/input_map.h`, test `input_map_test`).
- Next: DirectSound on miniaudio (DirectSoundCreate8 / DirectSoundEnumerateA), then the D3DX
  texture/mesh loaders so the binary links, then run it against `RAN_GAME_DIR=~/Projects/RAN/client`.

## Phase 3 - graphics (started)

- D3DX math: **done** - all 35 functions the client links (`port/d3dx/d3dx9_math.cpp`, test `d3dx_math_test`).
- Remaining D3DX: texture loading (DDS/TGA/BMP), `.x` meshes (`D3DXLoadMesh*`, `DirectXFileCreate`),
  effects, sprite, font, mesh utilities, screenshots (ijl stub -> stb).
