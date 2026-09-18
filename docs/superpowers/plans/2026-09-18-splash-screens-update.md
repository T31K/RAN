# Splash Screens Update Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Replace the client's loading/splash screens with the two new "RAN Odyssey Online" artworks (red blood-moon + blue moonlit), as a **data-only swap** — no code change, no client rebuild.

**Architecture:** The client draws every loading splash from `textures/gui/loading_0NN.tga` files resolved by basename at runtime. We generate new 1024×1024 24-bit TGAs that put the new art in the top 1024×768 region while byte-preserving the progress-bar sprite strip in rows 769–800, then overwrite the 12 `loading_0NN.tga` files in the deployed client.

**Tech Stack:** Python 3 + Pillow (read/compose only; TGA written manually with `struct` to match the original byte format exactly).

**Spec:** No separate spec (bounded task). The verified research below is the spec; every claim carries a `file:line` reference into this repo's source.

## Verified Research Findings (READ FIRST — this is why naive swaps fail)

All verified in source on 2026-09-18. Use `grep -a` on these files (Korean bytes trip grep's binary detection).

**Which file shows when:**

| Moment | Image used | Where decided |
|---|---|---|
| Client start → login screen | **random `loading_011.tga` or `loading_012.tga`** (`rand()%2 + 11`) | `[Lib]__RanClientUI/Sources/LoaderUI/LoadingThread.cpp:162-168` (name empty → random) |
| Login → character select (lobby) | **`loading_002.tga`** hardcoded | `[Lib]__RanClient/Sources/DxGlobalStage.cpp:495-497` |
| Lobby → game world | per-map name, **field 18** of each `MAP` entry in `mapslist.ini` (inside `glogic.rcc`); currently references `loading_003.tga`…`loading_010.tga` across 23 entries; empty/`"null"` → random 011/012 | `GLMapList.cpp:160`, `DxGlobalStage.cpp:499-516`, `GLGaeaClient.cpp:427` etc. |
| Mid-map warp / bus | `mid_loading.tga` / `bus_mid_loading.tga` — **out of scope** (different layout) | `LoadingMapThread.cpp:30,41` |

**How the file is found and loaded (why a loose-file swap DOES work here, unlike the .ogg case):**
- `TextureManager::LoadTexture` resolves the name via `g_FileTree.FindPathName()` — a tree of **every file under `\Textures`, indexed by unique basename**, built once at client startup (`[Lib]__Engine/Sources/DxTools/TextureManager.cpp:153`, tree: `CFileFindTree` in `[Lib]__Engine/Sources/Common/CFileFind.*`).
- Plain `.tga`/`.dds` load unencrypted via D3DX; only `.mtf` files go through the XOR/encrypt path (`TextureContainer::IsEncrypt`, `TextureManager.cpp:136-145`; `EncryptTextureDef.h`).
- The `game-client/cache/` dir is **font glyphs only** (`DxResponseMan.cpp:206`) — no texture cache to invalidate.
- Load failures are logged as `ERROR : TextureManager::LoadTexture() <name>` → `game-client/RanOnline/errlog/*.txt` (`TextureManager.cpp:1242`).

**The texture layout contract (why "just export a 1024×1024 image" breaks the loading bar):**
- Only texture rows **0–768** (top-down coords) are drawn as the fullscreen artwork: `LoadingThread.cpp:220-231` (`fRealImageX=1024, fRealImageY=768, fImageSize=1023`).
- The **progress bar sprites are cut from the SAME texture**: bar fill at `(0,769)` size `582×9`, bar background at `(0,791)` size `582×9` (`LoadingThread.cpp:240-243`). If rows 769–800 don't contain the bar art, the loading bar disappears or shows garbage.
- Optional `OVER15.tga` age-rating overlay is a separate texture — untouched.

**Exact file format (all 12 originals are byte-identical in shape):**
- TGA type **2** (uncompressed true-color), **24 bpp**, **1024×1024**, descriptor `0x00` (bottom-left origin ⇒ rows stored bottom-up), 18-byte header, no ID field, 26-byte `TRUEVISION-XFILE` footer ⇒ **3,145,772 bytes** exactly.
- Replacements must match this (D3DX tolerates more, but matching removes every unknown).

**Trap discovered during research (do NOT violate):** backups of the original TGAs must live **OUTSIDE** `game-client/textures/` — the file tree indexes every file under `\Textures` recursively **by unique basename**; a backup copy named `loading_011.tga` in any subfolder creates a duplicate-name collision and the client may load the wrong (old) file. That is this project's `.ogg` failure mode.

## Global Constraints

- Data-only change: **no C++ edits, no client rebuild, no GH Actions run.**
- Deployed client dir: `/Users/t31k/Projects/RAN/game-client/` (NOT inside this repo).
- Output TGAs: type 2, 24 bpp, 1024×1024, descriptor 0x00, bottom-up rows, footer preserved ⇒ exactly 3,145,772 bytes.
- Rows 769–1023 of every output = byte-identical to the original (preserves bar sprites at y 769–800).
- Backups go to `/Users/t31k/Projects/RAN/backups/splash-originals/` (outside the Textures tree).
- Art mapping rule: **odd-numbered slots = RED art, even-numbered slots = BLUE art** ⇒ boot splash randomly alternates red (`loading_011`) / blue (`loading_012`); lobby (`loading_002`) = blue.
- Art fit default: **center-crop the square art to 4:3** then Lanczos-resize to 1024×768, with a per-image `--bias` knob to shift the crop window up/down (title vs. characters). `--fit squash` is available as an alternative (keeps full art, 25% vertical squash). Executor previews before installing.
- Commit to this repo: the script, the plan, the source art. Generated TGAs and the deployed client are not git-tracked — never `git add` anything under `game-client/`.

---

### Task 1: Stage the source artwork

**Files:**
- Create: `assets/splash/red.png` (blood-moon artwork)
- Create: `assets/splash/blue.png` (moonlit artwork)

**Interfaces:**
- Produces: two RGB image files at those exact paths; Task 2's script hardcodes nothing — takes them as CLI args, but Task 4 invokes it with these paths.

- [ ] **Step 1: Get the two images from the user as files**

The two artworks were pasted into the chat as images — they do not exist on disk yet. Ask the user to drop the two PNG files into `assets/splash/` in this repo as `red.png` (blood-moon) and `blue.png` (moonlit). Create the directory first:

```bash
mkdir -p /Users/t31k/Projects/RAN/RanOnline/assets/splash
```

- [ ] **Step 2: Verify the inputs**

```bash
cd /Users/t31k/Projects/RAN/RanOnline && python3 -c "
from PIL import Image
for n in ('assets/splash/red.png','assets/splash/blue.png'):
    im = Image.open(n); print(n, im.size, im.mode)
"
```

Expected: both files open; size roughly square (≈1232×1232); any mode is fine (script converts to RGB). If Pillow is missing: `python3 -m pip install --user pillow`.

- [ ] **Step 3: Commit**

```bash
cd /Users/t31k/Projects/RAN/RanOnline
git add assets/splash/red.png assets/splash/blue.png
git commit -m "assets: add RAN Odyssey splash artwork (red/blue)"
```

---

### Task 2: Write the splash TGA generator script

**Files:**
- Create: `scripts/make_splash.py`

**Interfaces:**
- Produces: CLI `python3 scripts/make_splash.py <art.png> <original.tga> <out.tga> [--bias PX] [--fit crop|squash] [--preview out.png]`
- Guarantees (self-checked, script exits non-zero on violation): output is 3,145,772 bytes; header == original header; rows 769–1023 == original bytes.

- [ ] **Step 1: Write the script**

```python
#!/usr/bin/env python3
"""Compose new splash art into a RAN loading_0NN.tga, preserving the
progress-bar sprite strip (texture rows 769-800) and the exact TGA format.

Format contract (verified against the EP7 client source, LoadingThread.cpp):
- TGA type 2, 24bpp, 1024x1024, descriptor 0x00 (rows stored bottom-up)
- rows 0-768 (top-down) = fullscreen artwork; rows 769+ = bar sprites
"""
import argparse, struct, sys
from PIL import Image

W = H = 1024
ART_H = 768          # art occupies top-down rows 0..ART_H-1
HDR = 18
BODY = W * H * 3     # 3,145,728
FOOTER = 26          # TRUEVISION-XFILE footer

def read_tga(path):
    data = open(path, 'rb').read()
    idlen, cmap, imgtype = data[0], data[1], data[2]
    w, h = struct.unpack('<HH', data[12:16])
    bpp, desc = data[16], data[17]
    assert (imgtype, w, h, bpp, desc, idlen, cmap) == (2, W, H, 24, 0, 0, 0), \
        f"{path}: unexpected TGA shape {(imgtype, w, h, bpp, desc, idlen, cmap)}"
    assert len(data) == HDR + BODY + FOOTER, f"{path}: size {len(data)}"
    return data

def rows_topdown(body):
    """Split raster into rows and flip: stored bottom-up -> top-down list of row bytes."""
    rows = [body[i*W*3:(i+1)*W*3] for i in range(H)]
    return rows[::-1]

def fit_art(img, mode, bias):
    img = img.convert('RGB')
    w, h = img.size
    if mode == 'squash':
        return img.resize((W, ART_H), Image.LANCZOS)
    crop_h = round(w * ART_H / W)          # 4:3 window in source pixels
    if crop_h > h:                          # source wider than 4:3: crop width instead
        crop_w = round(h * W / ART_H)
        left = (w - crop_w) // 2
        return img.crop((left, 0, left + crop_w, h)).resize((W, ART_H), Image.LANCZOS)
    top = (h - crop_h) // 2 + bias
    top = max(0, min(h - crop_h, top))
    return img.crop((0, top, w, top + crop_h)).resize((W, ART_H), Image.LANCZOS)

def main():
    ap = argparse.ArgumentParser()
    ap.add_argument('art'); ap.add_argument('original'); ap.add_argument('out')
    ap.add_argument('--bias', type=int, default=0,
                    help='crop-window shift in source px; negative = keep more of the top')
    ap.add_argument('--fit', choices=['crop', 'squash'], default='crop')
    ap.add_argument('--preview', help='also save a PNG preview of the final 1024x1024')
    a = ap.parse_args()

    orig = read_tga(a.original)
    orig_rows = rows_topdown(orig[HDR:HDR + BODY])

    art = fit_art(Image.open(a.art), a.fit, a.bias)
    art_px = art.tobytes()                  # RGB, top-down
    # TGA stores BGR: swap channels per pixel
    bgr = bytearray(art_px)
    bgr[0::3], bgr[2::3] = art_px[2::3], art_px[0::3]
    new_rows = [bytes(bgr[y*W*3:(y+1)*W*3]) for y in range(ART_H)]

    out_rows = new_rows + orig_rows[ART_H:]          # keep rows 768..1023 verbatim
    body = b''.join(out_rows[::-1])                  # back to bottom-up storage
    blob = orig[:HDR] + body + orig[HDR + BODY:]     # original header + footer

    # -- self-verification (the contract) --
    assert len(blob) == len(orig) == HDR + BODY + FOOTER
    assert blob[:HDR] == orig[:HDR]
    check = rows_topdown(blob[HDR:HDR + BODY])
    assert check[ART_H:] == orig_rows[ART_H:], "bar strip not preserved"

    open(a.out, 'wb').write(blob)
    if a.preview:
        flat = b''.join(check)
        img = Image.frombytes('RGB', (W, H), bytes(flat))
        b, g, r = img.split(); Image.merge('RGB', (r, g, b)).save(a.preview)
    print(f"OK {a.out} ({len(blob)} bytes, fit={a.fit}, bias={a.bias})")

if __name__ == '__main__':
    sys.exit(main())
```

- [ ] **Step 2: Smoke-test the identity properties against a real original (no art dependency)**

Run it once with any solid image to prove the format contract holds:

```bash
cd /Users/t31k/Projects/RAN/RanOnline
S=/private/tmp/claude-501/-Users-t31k-Projects-RAN-RanOnline/2dd1ec50-6a98-4bc6-bf9f-c9837fc13a52/scratchpad
python3 -c "from PIL import Image; Image.new('RGB',(1232,1232),(200,30,40)).save('$S/test.png')"
python3 scripts/make_splash.py "$S/test.png" \
  /Users/t31k/Projects/RAN/game-client/textures/gui/loading_011.tga \
  "$S/test_out.tga" --preview "$S/test_out.png"
```

Expected: `OK ... (3145772 bytes ...)`. The script's internal asserts already verify header + bar-strip preservation; a failure raises `AssertionError`.

- [ ] **Step 3: Verify the bar strip survived, independently of the script**

```bash
S=/private/tmp/claude-501/-Users-t31k-Projects-RAN-RanOnline/2dd1ec50-6a98-4bc6-bf9f-c9837fc13a52/scratchpad
python3 - "$S/test_out.tga" /Users/t31k/Projects/RAN/game-client/textures/gui/loading_011.tga <<'EOF'
import sys
a, b = (open(p,'rb').read() for p in sys.argv[1:3])
W,H,HDR = 1024,1024,18
rows = lambda d: [d[HDR+i*W*3:HDR+(i+1)*W*3] for i in range(H)][::-1]
assert rows(a)[768:] == rows(b)[768:], "STRIP DIFFERS"
assert rows(a)[:768] != rows(b)[:768], "art region unchanged?!"
print("strip preserved, art replaced - OK")
EOF
```

Expected: `strip preserved, art replaced - OK`

- [ ] **Step 4: Commit**

```bash
cd /Users/t31k/Projects/RAN/RanOnline
git add scripts/make_splash.py
git commit -m "feat(tools): splash TGA generator preserving loading-bar strip"
```

---

### Task 3: Back up the original TGAs (outside the Textures tree)

**Files:**
- Create: `/Users/t31k/Projects/RAN/backups/splash-originals/loading_001.tga` … `loading_012.tga` (copies)

**Interfaces:**
- Produces: pristine originals for rollback and as the strip/template source for Task 4.

- [ ] **Step 1: Copy (NOT move) the originals**

```bash
mkdir -p /Users/t31k/Projects/RAN/backups/splash-originals
cp -n /Users/t31k/Projects/RAN/game-client/textures/gui/loading_0*.tga \
      /Users/t31k/Projects/RAN/backups/splash-originals/
ls -la /Users/t31k/Projects/RAN/backups/splash-originals/
```

Expected: 12 files, each 3,145,772 bytes. **Do not** create any backup under `game-client/textures/` (duplicate-basename trap — see research findings).

- [ ] **Step 2: Verify no stray copies inside the Textures tree**

```bash
find /Users/t31k/Projects/RAN/game-client/textures -name "loading_0*.tga" | wc -l
```

Expected: exactly `12` (the live files only).

---

### Task 4: Generate and install the 12 new splash TGAs

**Files:**
- Modify (overwrite): `/Users/t31k/Projects/RAN/game-client/textures/gui/loading_001.tga` … `loading_012.tga`

**Interfaces:**
- Consumes: `scripts/make_splash.py` (Task 2), originals in `backups/splash-originals/` (Task 3), art in `assets/splash/` (Task 1).

- [ ] **Step 1: Generate all 12 into a staging dir with previews**

Odd slot → red, even slot → blue (so boot = red 011 / blue 012, lobby 002 = blue):

```bash
cd /Users/t31k/Projects/RAN/RanOnline
S=/private/tmp/claude-501/-Users-t31k-Projects-RAN-RanOnline/2dd1ec50-6a98-4bc6-bf9f-c9837fc13a52/scratchpad/splash-out
mkdir -p "$S"
for i in $(seq -w 1 12); do
  n="loading_0$i.tga"
  case $((10#$i % 2)) in 1) art=assets/splash/red.png;; 0) art=assets/splash/blue.png;; esac
  python3 scripts/make_splash.py "$art" \
    "/Users/t31k/Projects/RAN/backups/splash-originals/$n" \
    "$S/$n" --preview "$S/loading_0$i.png"
done
ls -la "$S"
```

Expected: 12 `OK` lines; every `.tga` is 3,145,772 bytes.

- [ ] **Step 2: Eyeball the previews and tune the crop**

Open `$S/loading_011.png` and `$S/loading_012.png` (e.g. `open "$S"/loading_011.png "$S"/loading_012.png`). The 4:3 center-crop cuts ≈154 px off the top and bottom of the square art — if the "RAN ODYSSEY" lettering or faces are clipped, regenerate with `--bias` (negative keeps more of the top, e.g. `--bias -120`), or fall back to `--fit squash`. **Show the previews to the user and get an explicit OK before Step 3.**

- [ ] **Step 3: Install**

```bash
S=/private/tmp/claude-501/-Users-t31k-Projects-RAN-RanOnline/2dd1ec50-6a98-4bc6-bf9f-c9837fc13a52/scratchpad/splash-out
cp "$S"/loading_0*.tga /Users/t31k/Projects/RAN/game-client/textures/gui/
ls -la /Users/t31k/Projects/RAN/game-client/textures/gui/loading_0*.tga
```

Expected: 12 files, today's timestamp, 3,145,772 bytes each.

- [ ] **Step 4: Note the bias values used**

If any `--bias`/`--fit` deviated from defaults, record the exact regeneration command per file in a comment block at the top of this plan's "Outcome" section (added in Task 5), so the swap is reproducible.

---

### Task 5: Verify in the running client

**Interfaces:**
- Consumes: installed TGAs (Task 4). The client rebuilds its texture file tree at startup, so a **fresh client launch** is required.

- [ ] **Step 1: Clear a marker in the errlog, launch, and exercise all three splash paths**

Launch the game as usual (`/Users/t31k/Projects/RAN/play.command`). Then:
1. Client start → login screen: new art appears (red or blue, random). Restart once or twice to see the other one if desired.
2. Log in → character select: **blue** art (loading_002).
3. Enter the world: new art (per-map slot), with the **progress bar visible and filling** at the bottom center.

- [ ] **Step 2: Check the errlog for load failures**

```bash
grep -a "TextureManager::LoadTexture" /Users/t31k/Projects/RAN/game-client/RanOnline/errlog/*.txt | tail -5
```

Expected: no new `ERROR : TextureManager::LoadTexture() loading_0*` lines from this session. (Per the runbook, errlog is the first diagnostic stop.)

- [ ] **Step 3: Rollback path (only if broken)**

```bash
cp /Users/t31k/Projects/RAN/backups/splash-originals/loading_0*.tga \
   /Users/t31k/Projects/RAN/game-client/textures/gui/
```

- [ ] **Step 4: Record the outcome and commit**

Append an "## Outcome" section to this plan (what was seen in-game, any bias values from Task 4 Step 4), then:

```bash
cd /Users/t31k/Projects/RAN/RanOnline
git add docs/superpowers/plans/2026-09-18-splash-screens-update.md
git commit -m "docs: splash screen swap outcome"
```

---

## Out of scope (noted for later)

- `mid_loading.tga` / `bus_mid_loading.tga` (mid-map warp screens, different sprite layout in `LoadingMapThread.cpp`) and `loading.tga` (RLE, apparently unreferenced).
- `logo1_a.dds` / `0923logo2_a.dds` — tiny (64×16 / 64×64) in-world logo textures, not splash screens.
- Re-pointing `mapslist.ini` field 18 at specific images per map (would need an `.rcc` repack; unnecessary since we replace all 12 slots).

## Outcome (2026-09-18)

**Status: installed, pending in-game visual confirmation by user.**

- Source art: `~/Downloads/{red,blue}.png` (1254×1254 RGB) → committed to `assets/splash/`.
  Note: the Downloads **filenames are swapped vs. content** — `red.png` holds the *blue moonlit* art, `blue.png` holds the *red blood-moon* art. Kept the file names as-is; slot mapping below reflects actual content.
- Fit chosen: `--fit crop --bias -110` for all 12 (4:3 center-crop shifted up 110 source px so the "RAN ODYSSEY ONLINE" title gets headroom; costs a little more of the characters' lower legs, which sit in the preserved bottom strip anyway). Verified via previews of slots 011/012.
- Slot mapping: odd slots (`assets/splash/red.png` = blue moonlit) / even slots (`assets/splash/blue.png` = red blood-moon). Boot splash randomly shows one of 011/012 → alternates the two artworks; char-select (`loading_002`) shows the red blood-moon art.
- Regenerate any file reproducibly, e.g.:
  `python3 scripts/make_splash.py assets/splash/red.png backups/splash-originals/loading_011.tga OUT.tga --bias -110`
- Bar strip (texture rows 768–1023) byte-preserved on all 12 → loading bar unaffected.
- **Known cosmetic:** the preserved bottom strip still carries the stock `Copyright 2003~2004 DaumGame / Min Communication` line (it shares the strip with the progress-bar sprites). Left intact to avoid disturbing the bar. Removing/replacing it would be a separate, careful strip-only edit.
- Rollback: `cp backups/splash-originals/loading_0*.tga game-client/textures/gui/`
