# Native macOS client - progress

Plan: `docs/superpowers/plans/2026-10-03-native-macos-client.md`. Developer guide:
`docs/port/README.md`. Branch: `port/macos-spike`. Last updated 2026-10-04.

Overall: about half of the plan. Phase 1 complete, Phase 2 most of the way, Phase 3 well along.

## Phase 1 - portable codebase, Windows still builds: COMPLETE

| Gate | Status | How to check |
|---|---|---|
| P1.1 every client source compiles natively | **Done** - 799/799 (the game's own shell files included) | `port/scripts/compile_probe.sh` -> `docs/port/compile-probe.md` |
| P1.2 Windows CI green | **Done** - every pushed checkpoint builds (Release Win32) | GitHub Actions `Build` on `port/macos-spike` |
| P1.3 network-message struct sizes == Win32 | **Done** - all 995 wire structs (+ package `FILECONTEXT`) identical to MSVC x86 | `port/scripts/check-struct-sizes.sh` |
| P1.4 CP949 text round trip | **Done** | `port/scripts/check-cp949.sh` |
| P1.5 Windows paths resolve to data files | **Done** - also on a case-sensitive volume | `port/scripts/check-paths.sh` |

Unit tests: 20+ suites, all passing (`port/scripts/run-tests.sh`).

Clang turns two MSVC-tolerated constructs into run-time traps; both are now build errors
(`-Werror=non-pod-varargs -Werror=return-type` in `compile_one.sh`) and every site is fixed:
CString passed through `...` (compat `Format` templates + forwarding templates on the game's
own printf-style functions) and functions that fell off the end without a return.

## Phase 2 - platform layer

- Window + game loop: SDL3 `main` -> the game's own `theApp.InitInstance()/Run()`; SDL events
  become the game's window messages.
- Input: **done** - DirectInput 8 on SDL3 events; focus loss releases held keys (no Cmd+Tab
  input loss).
- Text input: **done** - keyboard text and the macOS input method (Korean composition) reach the
  game's edit control (`CIMEEdit`) as `WM_CHAR` / `WM_IME_*`, with `ImmGetCompositionString`
  serving the strings (`port/platform/text_input.h`, `port/compat/include/imm.h`).
- Sound: **done** - DirectSound 8 on an SDL3 software mixer (buffers, duplicates, looping,
  streaming Lock, 3D listener/buffers) (`port/platform/dsound_sdl.cpp`).
- Files from C sources (minizip opening `glogic.rcc`) resolve Windows paths too.
- Run it: `port/scripts/run_native.sh` (client folder defaults to `~/Projects/RAN/client`);
  after a crash `port/scripts/last_crash.py` prints the backtrace from the macOS crash report.
- Next: gate P2.1 (log in against the VPS server) once models load.

## Phase 3 - graphics

- D3DX math: **done**.
- Textures: **done** - DDS (DXT1-5 uploaded as is, 16/24/32-bit), TGA/BMP/JPG/PNG (stb), cube
  maps, D3DX size/mip/format rules, DXTn encoder, surface copy/convert, save to DDS/BMP/PNG/JPG.
- Text: **done** - GDI memory DCs, DIB sections, fonts and `ExtTextOut` on CoreText, which the
  game's font atlas (`d3dfont.cpp`) is baked with; Korean faces map to Apple SD Gothic Neo.
- `.x` files: **done** - reader for text/binary .x behind `DirectXFileCreate`/`D3DXFileCreate`;
  all 1361 client .x files parse.
- Meshes: in progress - `ID3DXMesh`/`ID3DXPMesh`/`ID3DXSkinInfo`, mesh utilities, and the .x
  mesh loaders (`D3DXLoadMeshFromX`, `D3DXLoadMeshHierarchyFromX`).
- Effects: not needed by this client (it ships no `.fx` files; D3DX fails the same way on
  Windows). Sprite / `ID3DXFont`: only used by the optional "D3DXFONT" font mode.
- First native runs: SDL window + D3D9 device on Apple M3 through DXVK/KosmicKrisp, textures
  and logic data load, the lobby stage starts loading its map.
