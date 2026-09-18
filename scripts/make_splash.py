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
