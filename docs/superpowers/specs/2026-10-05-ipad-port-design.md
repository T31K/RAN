# iPad port — design

Date: 2026-10-05. Status: approved in chat, spec under review.

## Goal

RAN runs on the user's iPad, touch-only, installed from Xcode onto the user's own device with
their Apple developer account (no App Store, no TestFlight), and plays on the same VPS game
server as the macOS native app. iPad, Mac (and later Android) players share one world.

Success = the user logs in on the iPad, enters the world as T31K3, walks, fights, chats, and
another player on the Mac sees them.

## What the user said vs. what is assumed

- Said: iPad, touch-only, "gyros UI", developer account, install via Xcode.
- Assumed: one device (the user's) for now; friends' iPads later via ad-hoc signing.
- Open: "gyros UI" = virtual joystick, tilt camera, or both. Only affects Phase 2; decided
  before Phase 2 starts.

## Approach

The macOS native port (port/, DXVK-native + SDL3) gets an iOS target beside it. Game sources,
compat headers, D3DX replacement and the platform layer are shared; only build tooling and a
few platform files get iOS variants. No game logic changes.

| Layer | macOS today | iPad |
|---|---|---|
| Game engine | RAN's own (D3D9) | same |
| D3D9 -> Vulkan | DXVK-native | DXVK-native, cross-compiled for iOS |
| Vulkan -> Metal | KosmicKrisp | MoltenVK (Vulkan SDK, iOS xcframework) |
| Window / input / audio | SDL3 (Homebrew) | SDL3 built from source for iOS |
| Other deps | Homebrew (lzo, vorbis, ogg, curl, iconv, zlib) | built from source / iOS SDK |

## Phase 0 — triangle spike (go / no-go)

Before any game code: a minimal iOS app that creates an SDL3 window, a DXVK D3D9 device on
MoltenVK, and draws a textured triangle, installed on the iPad from Xcode. Throwaway code,
but its build setup (DXVK iOS cross-file, MoltenVK linkage, signing) is kept.

If DXVK cannot run on MoltenVK (missing features/extensions), stop and report: options are
patching DXVK's feature requirements, or KosmicKrisp on iOS if available. No further phases
until this passes.

## Phase 1 — game boots on iPad, tap = click

1. `port/scripts/build_ios_deps.sh`: cross-compiles SDL3, lzo, libogg, libvorbis, libcurl
   (SecureTransport/Network.framework TLS) for `arm64-apple-ios` into `port/third_party/ios/`.
2. `port/scripts/build_ios.sh`: same source list (`client_sources.py`) and flags as
   `build_native.sh`/`compile_one.sh`, with `-target arm64-apple-ios<min> -isysroot <iphoneos
   SDK>`, objects in `port/build/ios/`; links one static executable against DXVK (static),
   MoltenVK, SDL3 and system frameworks.
3. `port/ios/` Xcode wrapper project: bundles the binary, frameworks and the game data
   (`~/Projects/RAN/client` data/textures/sounds + config), Info.plist (landscape only,
   fullscreen, status bar hidden), signs with the user's team. Xcode Run = install + live log.
4. Platform differences (guarded by `TARGET_OS_IOS`):
   - game folder: first-run copy (APFS clone) from the bundle into the app's
     Library/Application Support, reusing `game_sync.cpp`'s `.data-version` logic;
   - excluded: Sparkle updater, macOS crash-report bits of telemetry (kept: report upload);
   - fullscreen window at native resolution; game render size scaled to it;
   - on-screen keyboard via SDL text input when a game edit box has focus (login, chat).
5. Touch: SDL's default touch->mouse events (tap = left click) — enough to log in and move.
6. Remote debugging: three-finger tap = the F12 bug report (screenshot + log to the reports
   server), so sessions on the iPad can be inspected without a cable.

Done when: login -> character select -> in world as T31K3 on the VPS server, 30 fps+.

## Phase 2 — touch controls

New `port/platform/touch_controls.cpp`: SDL3 finger events -> the same input paths the mouse
and keyboard use (`InputMouseButton`, `InputKey`, `WM_*` messages), plus an overlay drawn
after the game's frame.

- tap = left click; long-press = right click; two-finger drag = camera rotate; pinch = zoom
  (mouse wheel);
- on-screen buttons: skill bar slots (the game's hotkeys), chat (opens the keyboard),
  inventory/character windows;
- "gyros UI" per the user's answer: virtual joystick (synthesised move-to-point clicks ahead
  of the character, or the game's own movement call) and/or gyroscope camera (SDL sensor API).

Shared with a later Android target (same SDL3 events).

## Error handling

- Phase gates above; each phase ends with a run on the user's iPad.
- Missing game data / failed first-run copy: an on-screen message box (existing MessageBox
  shim) and a report to the reports server.
- Memory: watch for iOS memory-pressure kills in Phase 1; texture-quality setting lowered if
  needed.

## Testing

- Builds: `build_ios.sh` compiles the same 23-suite unit tests for the iOS simulator where they
  have no device dependency (`run-tests.sh` target flag).
- On device: user runs from Xcode; logs in Xcode console; F12/three-finger reports read via the
  reports API.
- Cross-play check: Mac client and iPad in the same map see each other.

## Out of scope

App Store/TestFlight, Android, iPhone layout, controller support, live asset download
(separate project; would let iPad and Mac get new items without app rebuilds).

## Risks

1. DXVK on MoltenVK on iOS (unsupported combination) — Phase 0 answers it.
2. Static linking requirement on iOS (no loose dylibs; frameworks only) for DXVK.
3. iPad memory limits with ~2 GB of data and 2005-era uncompressed textures.
4. 2 GB app bundle install time over Xcode on every data change (mitigation: data installed
   once; later builds skip unchanged data).
