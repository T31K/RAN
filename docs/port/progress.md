# Native macOS client - progress

Plan: `docs/superpowers/plans/2026-10-03-native-macos-client.md`. Branch: `port/macos-spike`.
Last updated 2026-10-04.

## Phase 1 - portable codebase, Windows still builds

| Gate | Status | How to check |
|---|---|---|
| P1.1 every client source compiles natively | **Done** - 796/796 (+ the 3 shell files, now back in) | `port/scripts/compile_probe.sh` -> `docs/port/compile-probe.md` |
| P1.2 Windows CI green | **Done** - every pushed checkpoint builds (Release Win32) | GitHub Actions `Build` on `port/macos-spike` |
| P1.3 network-message struct sizes == Win32 | Harness done (1000 structs incl. package `FILECONTEXT`); golden file from CI pending | `port/scripts/check-struct-sizes.sh` (needs `port/tests/golden/msg_sizes_win32.txt` from the CI artifact `msg-sizes-win32`) |
| P1.4 CP949 text round trip | **Done** - Item/Crow/SkillStrTable + UI tables, byte-exact (55 lines already corrupt in the shipped SkillStrTable degrade like Windows) | `port/scripts/check-cp949.sh` |
| P1.5 Windows paths resolve to data files | **Done** - 4626 files x 3 spellings, also on a case-sensitive volume | `port/scripts/check-paths.sh` |

Compat-layer unit tests: 14 suites, all passing (`port/scripts/run-tests.sh`).

## Phase 2 - platform layer (started)

- **The native client builds and links except for 32 symbols** (`port/scripts/build_native.sh`,
  list in `docs/port/link-gaps.md`): 26 D3DX mesh/texture/effect/sprite/font functions and the
  `.x` loader (Phase 3), DirectInput (3) and DirectSound (2) - the input/sound backends below.
- Startup path is the game's own: SDL3 `main` (`port/platform/main_sdl.cpp`) -> `theApp.InitInstance()`
  -> `CBasicWnd::Create` -> `CWnd::CreateEx` (SDL window, = DXVK's HWND) -> `CD3DApplication::Create`
  -> `theApp.Run()`, whose `PeekMessage/GetMessage` pump SDL events into the game's real MFC
  message maps (WM_ACTIVATEAPP / WM_SIZE / ...).
- P2.2 input map: **done** (`port/platform/input_map.h`, test `input_map_test`).
- Next: DxInputDevice on SDL keyboard/mouse state (replaces DirectInput8Create), DirectSound on
  miniaudio (DirectSoundCreate8), then run the binary against `RAN_GAME_DIR=~/Projects/RAN/client`.

## Phase 3 - graphics (started)

- D3DX math: **done** - all 35 functions the client links (`port/d3dx/d3dx9_math.cpp`, test `d3dx_math_test`).
- Remaining D3DX: texture loading (DDS/TGA/BMP), `.x` meshes (`D3DXLoadMesh*`, `DirectXFileCreate`),
  effects, sprite, font, mesh utilities, screenshots (ijl stub -> stb).
