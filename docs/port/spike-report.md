# Native macOS spike report (2026-10-03)

Plan: `docs/superpowers/plans/2026-10-03-native-macos-client.md`. Branch: `port/macos-spike`.

## Cmd+Tab (Task 0.2)

Automated `port/scripts/cmdtab_check.sh` (focus away to Finder and back, twice) on a native SDL3 window:

```
PASS mode=windowed           frame=[FRAME 0 61 1280 800]  focus_gained=2
PASS mode=desktop-fullscreen frame=[FRAME 0 0 1280 832]   focus_gained=2
PASS mode=spaces-fullscreen  frame=[FRAME 0 29 1280 803]  focus_gained=3
```

Window frame never changed and focus returned every time, in all three modes. The user is happy with windowed mode. Manual click-mapping check (corner targets after Cmd+Tab + minimize): pending, user to run.

## DXVK on macOS (Tasks 0.3–0.4)

- DXVK tag: **v2.7.1**, D3D9 front end only (`-Denable_d3d8/10/11/dxgi=false`), SDL3 WSI.
- Patch: `port/patches/dxvk/0001-macos-build.patch` (222 lines). Contents:
  - Build: POSIX compat header guarded for `__APPLE__`, `_NSGetExecutablePath`, one-arg `pthread_setname_np`, `size_t`/`uint64_t` overload ambiguity, missing `<cstddef>`, no GNU `--version-script` on Apple ld.
  - Runtime: load `libvulkan.1.dylib`; enable `VK_KHR_portability_enumeration` + `ENUMERATE_PORTABILITY` flag; on Apple make `geometryShader` (D3D9 only uses it for `ProcessVertices`, which RAN never calls), `shaderCullDistance`, `fillModeNonSolid` and `khrPipelineLibrary` optional; D3D9 falls back to solid fill when wireframe is unsupported; adapter check now lists every missing feature at once.
- **Vulkan driver: MoltenVK fails, KosmicKrisp works.** MoltenVK 1.4.2 lacks `VK_EXT_robustness2` (`robustBufferAccess2`, `nullDescriptor`), which DXVK hard-requires. KosmicKrisp (Homebrew `mesa` 26.2.4, `libvulkan_kosmickrisp.dylib`, device string `Apple M3 (KosmicKrisp 26.2.4)`) has them.
- Fixed-function probe (`port/probes/d3d9ff`): DXT1 texture + texture-stage MODULATE, vertex colours, fixed-function lighting with a directional light, alpha blending applied through a state block, back-buffer readback.
- **Pixel diff vs the Wine build of the same probe: 0 / 262,144 pixels differ** (`ppm_diff.py`, tol 8). Reference: `port/probes/d3d9ff/ref/out.ppm`; native: `port/probes/d3d9ff/native-kosmickrisp.png`.

Run recipe:

```bash
eval "$(port/scripts/build-dxvk.sh | grep '^DXVK_')"
DXVK_WSI_DRIVER=SDL3 \
VK_DRIVER_FILES=/opt/homebrew/opt/mesa/share/vulkan/icd.d/kosmickrisp_mesa_icd.aarch64.json \
DYLD_LIBRARY_PATH="$(brew --prefix vulkan-loader)/lib:$(brew --prefix sdl3)/lib:$(dirname "$DXVK_LIB")" \
  ./d3d9ff_probe
```

Known gaps to cover in Phase 3: wireframe draws as solid (RAN uses `D3DFILL_WIREFRAME` in toon outline / river / decal / debug paths, so check those visually); asm shaders and `.fx` effects are not yet exercised by any probe; KosmicKrisp is beta, so pin its version and bundle it.

## Compile probe (Task 0.5) - and the start of Phase 1

`port/scripts/compile_probe.sh` syntax-checks every client `.cpp` natively (macOS clang, C++14, compat headers first, DXVK native headers, the `.vcxproj` include dirs). Progress through the day, as compat work landed:

| Step | Files passing |
|---|---|
| Baseline (no compat) | 0 / 833 |
| Engine root header `dxstdafx.h` compiles | 124 |
| Serialization `DWORD`/`UINT` overloads, `CMemPool` fix | 266 |
| Winsock on BSD sockets, LZO, guard typo fix | 409 |
| ODBC/Daum/WinInet/MFC collections | 580 |
| C++14 (matches the Windows build), MFC windows | 610 / 815 (+18 tool-only files excluded) |
| Registry, shell folders, CRT io, CryptoAPI, CFile | **653 / 815 (80%)** |

The compat layer (`port/compat/include`) is test-driven: 10 suites, all passing (`port/scripts/run-tests.sh`), including CP949-safe `CString`, CP949<->UTF-16 conversion with real Korean text, Winsock event semantics against a real TCP server, the x87 control-word emulation, the registry, and path resolution on case-sensitive layouts.

Game-source changes so far are small and guarded so the Windows build is unchanged (Windows CI green on every push): `gassert.h` debug break, `DebugSet.h` clock, `dxstdafx.h` `RAN_W` literal macro, 4 DXUT wide literals, `#ifndef RAN_DWORD_IS_UINT` around 12 duplicate stream overloads, `GLVEHICLE.h` include-guard typo (a real bug), `GLItem.h` redundant union initializer, `CMemPool.h` shadowed template parameter.

### Findings that changed the plan

- **`-fshort-wchar` is unsafe on macOS**: libc++ silently mis-handles `char16_t`/`wchar_t` (e.g. `u16string::find` misses every character >= U+8000 - all Korean). Replaced by `WCHAR = char16_t` (still 2 bytes), patched into DXVK's `windows_base.h`; mismatches now fail to compile instead of corrupting text.
- **`-fms-compatibility` breaks Apple's SDK headers** (hides `__GNUC__`). The native build uses plain clang + `-fms-extensions`; MSVC-only C++ is fixed in source.
- **C++14, not C++20**: the `.vcxproj` files set no `LanguageStandard`, so VS2022 builds the game as C++14. Matching it avoids `std::byte` clashing with the game's `byte` and keeps `auto_ptr`/`random_shuffle`.
- **64-bit hazards are now compile errors**: `DWORD`/`UINT` collapse to one type (handled), and every place that streams a `size_t`/`unsigned long` into a save file is now an ambiguous-overload error (12 sites) that needs an explicit `DWORD` cast to keep the 4-byte file format. `SFileSystem.h` computes file offsets with `sizeof(long)` (4 on Windows, 8 here) - must become a fixed 4.
- **Server code compiles into the client** (`GLGaeaServer.h` -> `s_CServer.h` -> DB/ODBC, Daum crypt). It only needs to compile; the linker drops what the client never calls.

### Remaining compile errors (162 files)

Long tail, mostly MSVC permissive-mode C++ that needs small source edits: `goto` jumping over initializations (18), temporaries bound to non-const references (~19), size_t-into-file streaming (12, the 64-bit fixes above), `std::transform` with overloaded `tolower` (7), SSE `__m128` (5, -> `sse2neon` or scalar), plus a few dozen single Windows APIs. See `docs/port/compile-probe.md` for the live list.

## Decision

- [x] **GO Path A (DXVK + KosmicKrisp)**: Cmd+Tab PASS and DXVK diff PASS.
- [ ] GO Path B (own Metal shim): not needed.
- [ ] STOP: not triggered; native windows behave correctly.

## Revised estimate

Path B (own Metal D3D9 shim, +6-8 weeks) is off the table, and Phase 1 went much faster than the human-paced estimate: 80% of the client compiles natively after one day, with the compat layer test-covered.

| Phase | Original | Now | Why |
|---|---|---|---|
| 0 Spike | 2 weeks | done | all three checks passed |
| 1 Portable codebase | 4-6 weeks | ~1 week left | long tail of C++ fixes + link the libraries + the 64-bit file-format audit |
| 2 Platform (SDL3, input, IME, sound, sockets) | 3-4 weeks | 2-3 weeks | sockets already done and tested |
| 3 Graphics | 8-12 weeks | 4-6 weeks | DXVK proven; work is D3DX (math, textures, meshes, effects), fonts via CoreText, shader blobs |
| 4 Parity | 4-6 weeks | 3-4 weeks | needs real play-testing time, M1/M2 testers |
| 5 Ship | 1-2 weeks | 1 week | reuse package-app.command |

**Revised total: roughly 2.5-3.5 months** (was 5-7). The parts that cannot be compressed are the visual comparisons of every map/effect/UI and real play-testing.

## Follow-ups recorded during the spike

- Confirm the CryptoAPI RC4 key length (40 vs 128 bit) against a Windows-generated vector, so settings files carry over from the Wine install (`win32/crypt.h`).
- In-game web pages (`CommonWeb.h`, embedded IE) need a WKWebView replacement or to be switched off.
- Screenshot saving uses the Intel JPEG Library (Windows DLL); rewrite on stb in Phase 3.
- Manual Cmd+Tab click-mapping check (Task 0.2 step 6) still to be run by the user.
