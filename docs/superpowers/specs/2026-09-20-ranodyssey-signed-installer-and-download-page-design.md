# RAN Odyssey — Signed/Notarized Installer + Download Page

**Date:** 2026-09-20
**Status:** Approved design → implementation
**Goal:** Let a friend install RAN Odyssey by downloading a DMG that opens with
zero Gatekeeper warnings ("the stapler thing"), and give them a place to get it:
`t31k.com/ran`.

## Context / current state

- `~/Projects/RAN/dist/RanOdyssey.app` is a **3.2 MB DEV stub** — its launcher
  hardcodes `/Users/t31k/Projects/RAN/...` and the local Wine. It cannot run on
  another Mac. Not signed at all.
- `~/Projects/RAN/dist/RanOdyssey.app/Contents/Resources/launcher.sh` **already
  supports a distribution layout**: when `Resources/wine/wswine.bundle/bin/wine`
  exists it runs in dist mode using `Resources/{wine,frameworks,game,prefix-template}`
  and stages a per-user writable copy under `~/Library/Application Support/RanOdyssey`.
  No launcher code change is needed — we just have to populate `Resources/`.
- Payloads on disk:
  - `wine/w10/wswine.bundle` — 691 MB, **adhoc-signed**, ~2,778 Mach-O binaries
  - `wine/tmpl/Template-1.0.15.app/Contents/Frameworks` — 368 MB
  - `client` — 2.1 GB (includes shippable junk: `Game.pdb` 58 MB, `*.bak`,
    `cache/`, `$PLUGINSDIR`, `$SYSDIR`)
  - `~/.wine-ran10-client` — 343 MB (known-good Wine prefix)
- Signing identity present: `Developer ID Application: Teik Mun Wong (QGQYJRMCNQ)`.
- **No** notarytool credential profile exists yet.
- `bump-release.command` publishes numbered `vN` GitHub releases (cumulative
  `payload.zip`) that the app auto-pulls on launch. The installer DMG must NOT
  collide with those — it lives on a **dedicated `installer` tag**.
- `~/Projects/t31k.com` is Next.js 14 App Router (Tailwind + shadcn + motion);
  routes are `app/<name>/page.js`. Plain `nextConfig = {}`.

## Decisions (approved)

1. **Approach A**: full deep-sign → notarize → staple.
2. **Delivery**: GitHub release on `T31K/RAN`, dedicated **`installer`** tag →
   stable URL `https://github.com/T31K/RAN/releases/download/installer/RanOdyssey.dmg`.
   Slim the client hard so the DMG fits under GitHub's 2 GB/file cap.
3. **Required gate**: launch the SIGNED app locally and confirm the game runs
   before notarizing/publishing (hardened runtime can break Wine).
4. **Page**: `t31k.com/ran`, full-bleed 100%×100% background (`ran-bg.jpg`),
   download button(s) on top; OG image `ran-og.jpg`.

## Part 1 — `~/Projects/RAN/package-app.command` (reusable)

Idempotent build script. Stages into a scratch build dir, never mutates the repo
source payloads.

### 1. Assemble FAT app
Copy the app skeleton (Info.plist, MacOS/RanOdyssey, launcher.sh, updater.sh,
AppIcon.icns) into `BUILD/RanOdyssey.app`, then populate `Contents/Resources/`:
- `wine/wswine.bundle` ← `wine/w10/wswine.bundle`
- `frameworks` ← Template `Frameworks`
- `game` ← `client`, **excluding** `Game.pdb`, `*.pdb`, `*.bak`, `cache/`,
  `$PLUGINSDIR`, `$SYSDIR`, `*.log`, `logs/` (rsync `--exclude`)
- `prefix-template` ← `~/.wine-ran10-client`

Target: game ≈ 1.4 GB → app ≈ 2.8 GB → compressed DMG ≈ under 2 GB.

### 2. Deep-sign inside-out
- `entitlements.plist` with: `com.apple.security.cs.allow-jit`,
  `com.apple.security.cs.allow-unsigned-executable-memory`,
  `com.apple.security.cs.disable-library-validation`,
  `com.apple.security.cs.allow-dyld-environment-variables`.
- Enumerate every **Mach-O** file (detect by magic via `file`; **skip** Windows
  PE `.exe/.dll` — they are not Mach-O and must not be signed) plus nested
  `.framework`/`.bundle`/`.dylib`/`.so`. Sign each, deepest first:
  `codesign --force --timestamp --options runtime --entitlements entitlements.plist -s "Developer ID Application: Teik Mun Wong (QGQYJRMCNQ)"`.
  Parallelize (`xargs -P`) — ~2,778 timestamped signatures is slow serially.
- Finally sign the outer `.app` (same flags). The main executable is a zsh
  script; entitlements that matter for Wine's JIT ride on the `wine` Mach-O
  binaries we signed directly, which the launcher `exec`s.
- Verify: `codesign --verify --deep --strict --verbose=2 RanOdyssey.app`.

### 3. DMG
- `hdiutil create -volname "RAN Odyssey" -srcfolder ... -ov -format UDZO` with an
  `/Applications` symlink for drag-install.
- Sign the DMG: `codesign -s "Developer ID Application: ..." RanOdyssey.dmg`.

### 4. Required local run-test gate
- Install/launch the signed app; confirm the game window comes up and it reaches
  the server. Abort publish on failure. (Check `~/Library/Application Support/RanOdyssey/logs/launch.log`.)

### 5. Notarize + staple
- Prereq (manual, user): `xcrun notarytool store-credentials "ran-notary"
  --apple-id t31kmunwong@gmail.com --team-id QGQYJRMCNQ` (app-specific password).
- `xcrun notarytool submit RanOdyssey.dmg --keychain-profile "ran-notary" --wait`.
- On rejection: `xcrun notarytool log <id> --keychain-profile "ran-notary"`, fix,
  re-sign, resubmit.
- `xcrun stapler staple RanOdyssey.dmg`.
- Verify: `xcrun stapler validate RanOdyssey.dmg` and
  `spctl -a -vvv -t open --context context:primary-signature RanOdyssey.dmg`.

### 6. Publish
- `gh release create installer RanOdyssey.dmg -R T31K/RAN -t "RAN Odyssey Installer" -n "..."`
  (or `gh release upload installer RanOdyssey.dmg --clobber` if the tag exists).

## Part 2 — `t31k.com/ran` page

- `app/ran/page.js` (App Router). Full-viewport section, `ran-bg.jpg` as
  `background-size: cover` / `object-fit: cover`, 100vw × 100dvh, no scroll.
- Overlay: title lockup, **Download for Mac** button →
  `https://github.com/T31K/RAN/releases/download/installer/RanOdyssey.dmg`.
- Small first-launch note (right-click → Open) as a safety net even though the
  DMG is notarized.
- Assets already compressed into `public/`: `ran-bg.jpg` (594 KB),
  `ran-og.jpg` (392 KB).
- Route metadata: title/description + `openGraph.images = ['/ran-og.jpg']`.

## Risks / open items

- **Hardened runtime vs Wine**: mitigated by the 4 entitlements + the required
  local run-test gate before publish.
- **Notarization iteration**: first submit may bounce specific binaries; the
  script must make re-sign + resubmit cheap.
- **2 GB cap**: if slimmed DMG still exceeds 2 GB, fall back to a cloud host
  (Drive/R2) and point the page there instead.
- Windows PE files inside `game/` are unsigned by design (correct; not Mach-O).
