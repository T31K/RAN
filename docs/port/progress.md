# Native macOS client - progress

Plan: `docs/superpowers/plans/2026-10-03-native-macos-client.md`. Developer guide:
`docs/port/README.md`. Branch: `port/macos-spike`. Last updated 2026-10-04.

**The native client plays.** It logs in, enters the world, walks, chats, plays sound and
survives app switching - DXVK-native d3d9 -> KosmicKrisp (Vulkan) -> Metal on Apple Silicon,
no Wine. Screenshots: `docs/port/images/` (server select, character select, in world, chat,
after an app switch).

| Phase | Status |
|---|---|
| 1 - portable codebase, Windows still builds | **Complete** (gates P1.1-P1.6 pass) |
| 2 - platform layer | **Complete** (P2.1 login passes; window, input, text/IME, sound, cursors, files) |
| 3 - graphics | **Complete for this client** (D3DX textures/fonts/meshes/skinning/.x; no .fx files ship) |
| 4 - gameplay parity | In progress: movement, chat, UI, sound, app switching verified; combat/skills/effects and long sessions still to exercise |
| 5 - ship | Done for a first release: `package_native_app.sh --with-game ~/Projects/RAN/client --sign --dmg` builds a self-contained, Developer ID-signed app with the game data inside (825 MB DMG, connects to the VPS); the first launch clones the data into `~/Library/Application Support/RanOdyssey/game` (APFS clone: instant, no extra space); notarised + stapled. Still to do: OTA updates for the native app, retiring the Wine app |

## Phase 1 - portable codebase, Windows still builds

| Gate | Status | How to check |
|---|---|---|
| P1.1 every client source compiles natively | **Done** - 799/799 | `port/scripts/compile_probe.sh` |
| P1.2 Windows CI green | **Done** | GitHub Actions `Build` on `port/macos-spike` |
| P1.3 network-message struct sizes == Win32 | **Done** - 995/995 | `port/scripts/check-struct-sizes.sh` |
| P1.4 CP949 text round trip | **Done** | `port/scripts/check-cp949.sh` |
| P1.5 Windows paths resolve to data files | **Done** | `port/scripts/check-paths.sh` |
| P1.6 on-disk struct images == Win32 | **Done** - 291/291 (16 excluded with reasons) | `port/scripts/check-file-struct-sizes.sh` (golden from CI artifact `file-sizes-win32`) |

Clang turns some MSVC-tolerated constructs into run-time traps; these are build errors now
(`compile_one.sh`): CString through `...`, functions without a return, deleting an abstract class
through a non-virtual destructor. 64-bit bug classes found and fixed while getting in-game:
- structs read from files with 4-byte pointer slots / MSVC `std::string` / vtable images
  (`port/compat/include/win32/file_image.h`, one declaration next to each struct),
- `D3DXMATRIXA16` is 16-byte aligned on Windows (changes struct layouts),
- `size_t`/`long` fields in file formats,
- object pointers kept in `DWORD`s (`DWORD_PTR` now),
- Windows tolerating use of deleted critical sections, static destruction order.

## Phase 2 - platform layer

- Window + loop: SDL3 `main` -> the game's own `theApp`; client rect in points (Retina-safe).
- Input: DirectInput 8 on SDL3; keyboard text + macOS input method (Korean) to the game's edit
  control (`WM_CHAR` / `WM_IME_*`); game cursors (`.cur`/`.ani`).
- Sound: DirectSound 8 on an SDL3 mixer (BGM streaming, 3D effects).
- Gate P2.1: scripted login (`RAN_INPUT_SCRIPT`) -> character select -> world.

## Phase 3 - graphics

D3DX math, textures (DDS/DXTn, TGA/BMP/JPG/PNG, DXTn encoder), GDI text on CoreText (the game's
font atlas), `.x` reader (all 1361 files), meshes / progressive meshes / skin info / blended
meshes, mesh loaders and hierarchy loading. Effects are not needed (no `.fx` in the client).
Sprite / `ID3DXFont` only matter for the optional "D3DXFONT" font mode.

## Releases

- **0.2** (2026-10-05): runs on Macs that also had the classic (Wine) app - the bundled game data
  now goes to its own `~/Library/Application Support/RanOdyssey Native/game` (the classic app's
  `RanOdyssey/game` holds an older client layout: every texture failed to load, so the UI drew
  white and the 3D scene black); startup no longer crashes on Mac built-in displays that list no
  800x600 mode; frame limit off by default; Tab cycles the skill pages (Shift+Tab = Extreme
  weapon swap); app icon restored. Verified on an M1 MacBook (macOS 26.6) and an M3.
- **0.1** (2026-10-04): first native release.

## Running and testing

- `port/scripts/build_native.sh` then `port/scripts/run_native.sh` (game folder defaults to
  `~/Projects/RAN/client`); `port/scripts/package_native_app.sh` builds the app bundle.
- Unattended runs: `RAN_INPUT_SCRIPT="22:click 488 373; 31:text T31K; 49:key Return; 86:raise"`.
- Diagnostics: `RAN_TRACE_INPUT`, `RAN_TRACE_AUDIO`, `DXVK_HUD=fps`; crashes print a backtrace.
- Frame rate: the native client starts with the game's Frame Limit (30 FPS) off, so it runs at
  the display's refresh (60 FPS on an M3); the in-game graphics option still turns it on.
- Keys: F1-F3 need Fn on a Mac keyboard, so Tab also cycles the skill pages (F1 -> F2 -> F3);
  the Extreme classes' weapon swap moves from Tab to Shift+Tab. DXVK keeps one queued frame (`d3d9.maxFrameLatency = 1`, set in `main_sdl.cpp`) so
  input does not lag behind by up to three frames.
