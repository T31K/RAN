"""Tiny offline renderer for checking converted meshes: z-buffered, flat shaded, two views side by
side (front: looking along +Z at a crow's face, which points to -Z; side: looking along -X).
Game space is left-handed, Y up. Writes a PNG through ImageMagick."""
import os
import subprocess
import tempfile

import numpy as np

FONT = '/System/Library/Fonts/Supplemental/Arial.ttf'

def _raster(v2, depth, shade, idx, W, H, img, zbuf):
    for t in range(len(idx)):
        a, b, c = idx[t]
        p = v2[[a, b, c]]
        x0, y0 = np.floor(p.min(0)).astype(int)
        x1, y1 = np.ceil(p.max(0)).astype(int)
        x0, y0 = max(x0, 0), max(y0, 0)
        x1, y1 = min(x1, W - 1), min(y1, H - 1)
        if x0 > x1 or y0 > y1:
            continue
        xs, ys = np.meshgrid(np.arange(x0, x1 + 1) + 0.5, np.arange(y0, y1 + 1) + 0.5)
        d = (p[1, 1] - p[2, 1]) * (p[0, 0] - p[2, 0]) + (p[2, 0] - p[1, 0]) * (p[0, 1] - p[2, 1])
        if abs(d) < 1e-12:
            continue
        l0 = ((p[1, 1] - p[2, 1]) * (xs - p[2, 0]) + (p[2, 0] - p[1, 0]) * (ys - p[2, 1])) / d
        l1 = ((p[2, 1] - p[0, 1]) * (xs - p[2, 0]) + (p[0, 0] - p[2, 0]) * (ys - p[2, 1])) / d
        l2 = 1 - l0 - l1
        inside = (l0 >= 0) & (l1 >= 0) & (l2 >= 0)
        if not inside.any():
            continue
        z = l0 * depth[a] + l1 * depth[b] + l2 * depth[c]
        yy, xx = np.nonzero(inside)
        yy, xx = yy + y0, xx + x0
        zz = z[inside]
        closer = zz < zbuf[yy, xx]
        zbuf[yy[closer], xx[closer]] = zz[closer]
        img[yy[closer], xx[closer]] = shade[t]


def view(v, idx, axis_h, axis_d, flip_h, size, extent, center):
    W = H = size
    img = np.full((H, W), 40, np.uint8)
    zbuf = np.full((H, W), np.inf)
    sc = (size * 0.9) / extent
    h = (v[:, axis_h] - center[axis_h]) * (-1 if flip_h else 1)
    v2 = np.c_[h * sc + W / 2, -(v[:, 1] - center[1]) * sc + H / 2]
    depth = v[:, axis_d] * (1 if axis_d == 2 else -1)
    tri = v[idx]
    fn = np.cross(tri[:, 1] - tri[:, 0], tri[:, 2] - tri[:, 0])
    fn /= np.maximum(np.linalg.norm(fn, axis=1, keepdims=True), 1e-12)
    light = np.array([0.4, 0.7, -0.6]) if axis_d == 2 else np.array([0.6, 0.7, 0.3])
    light /= np.linalg.norm(light)
    shade = (60 + 190 * np.abs(fn @ light)).astype(np.uint8)
    _raster(v2, depth, shade, idx, W, H, img, zbuf)
    # ground line y=0
    gy = int(round(-(0 - center[1]) * sc + H / 2))
    if 0 <= gy < H:
        img[gy, ::4] = 255
    return img


def load_rgb(path, px=256):
    raw = subprocess.run(['magick', path + '[0]', '-resize', f'{px}x{px}!', '-depth', '8', 'rgb:-'],
                         capture_output=True, check=True).stdout
    return np.frombuffer(raw, np.uint8).reshape(px, px, 3)


def render_textured(v, idx, uv, mat_ids, textures, path, size=420):
    """Front view (viewer at -Z), texture sampled at the interpolated UV (D3D: origin top left),
    lit with a flat N.L term. Proves the UVs and the texture orientation."""
    v = np.asarray(v, float)
    lo, hi = v.min(0), v.max(0)
    center, extent = (lo + hi) / 2, max((hi - lo).max(), 1e-6)
    texs = [load_rgb(t) for t in textures]
    W = H = size
    img = np.full((H, W, 3), 40, np.uint8)
    zbuf = np.full((H, W), np.inf)
    sc = size * 0.9 / extent
    v2 = np.c_[(v[:, 0] - center[0]) * sc + W / 2, -(v[:, 1] - center[1]) * sc + H / 2]
    tri = v[idx]
    fn = np.cross(tri[:, 1] - tri[:, 0], tri[:, 2] - tri[:, 0])
    fn /= np.maximum(np.linalg.norm(fn, axis=1, keepdims=True), 1e-12)
    light = np.array([0.4, 0.7, -0.6])
    light /= np.linalg.norm(light)
    lit = 0.45 + 0.55 * np.abs(fn @ light)
    for t in range(len(idx)):
        a, b, c = idx[t]
        p = v2[[a, b, c]]
        x0, y0 = np.maximum(np.floor(p.min(0)).astype(int), 0)
        x1, y1 = np.minimum(np.ceil(p.max(0)).astype(int), [W - 1, H - 1])
        if x0 > x1 or y0 > y1:
            continue
        xs, ys = np.meshgrid(np.arange(x0, x1 + 1) + 0.5, np.arange(y0, y1 + 1) + 0.5)
        d = (p[1, 1] - p[2, 1]) * (p[0, 0] - p[2, 0]) + (p[2, 0] - p[1, 0]) * (p[0, 1] - p[2, 1])
        if abs(d) < 1e-12:
            continue
        l0 = ((p[1, 1] - p[2, 1]) * (xs - p[2, 0]) + (p[2, 0] - p[1, 0]) * (ys - p[2, 1])) / d
        l1 = ((p[2, 1] - p[0, 1]) * (xs - p[2, 0]) + (p[0, 0] - p[2, 0]) * (ys - p[2, 1])) / d
        l2 = 1 - l0 - l1
        inside = (l0 >= 0) & (l1 >= 0) & (l2 >= 0)
        if not inside.any():
            continue
        z = (l0 * v[a, 2] + l1 * v[b, 2] + l2 * v[c, 2])[inside]
        yy, xx = np.nonzero(inside)
        yy, xx = yy + y0, xx + x0
        closer = z < zbuf[yy, xx]
        if not closer.any():
            continue
        u = (l0 * uv[a, 0] + l1 * uv[b, 0] + l2 * uv[c, 0])[inside][closer]
        w = (l0 * uv[a, 1] + l1 * uv[b, 1] + l2 * uv[c, 1])[inside][closer]
        tex = texs[mat_ids[t]]
        tx = (np.mod(u, 1) * (tex.shape[1] - 1)).astype(int)
        ty = (np.mod(w, 1) * (tex.shape[0] - 1)).astype(int)
        yy, xx = yy[closer], xx[closer]
        zbuf[yy, xx] = z[closer]
        img[yy, xx] = (tex[ty, tx] * lit[t]).astype(np.uint8)
    fd, ppm = tempfile.mkstemp(suffix='.ppm')
    with os.fdopen(fd, 'wb') as f:
        f.write(b'P6 %d %d 255\n' % (W, H) + img.tobytes())
    subprocess.run(['magick', ppm, path], check=True)
    os.remove(ppm)
    return path


def render(v, idx, path, size=420, title=None):
    v = np.asarray(v, float)
    lo, hi = v.min(0), v.max(0)
    center = (lo + hi) / 2
    extent = max((hi - lo).max(), 1e-6)
    front = view(v, idx, 0, 2, False, size, extent, center)   # x right, viewer at -Z
    side = view(v, idx, 2, 0, False, size, extent, center)    # viewer at +X looking -X: +Z right,
    #                                                           so a crow's face (-Z) is on the left
    img = np.concatenate([front, np.full((size, 6), 255, np.uint8), side], 1)
    fd, pgm = tempfile.mkstemp(suffix='.pgm')
    with os.fdopen(fd, 'wb') as f:
        f.write(b'P5 %d %d 255\n' % (img.shape[1], img.shape[0]) + img.tobytes())
    cmd = ['magick', pgm]
    if title and os.path.exists(FONT):
        cmd += ['-font', FONT, '-gravity', 'north', '-fill', 'white', '-pointsize', '14', '-annotate', '+0+4', title]
    subprocess.run(cmd + [path], check=True)
    os.remove(pgm)
    return path
