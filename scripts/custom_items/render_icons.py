"""Inventory icons for the custom weapons, rendered from their own models.

  render_icons.py        (after make_samehada.py; needs numpy + Pillow + ImageMagick)

Writes port/build/custom_items/custom_items.dds: a 512x256 icon sheet like the game's
(textures/gui/*.dds, 35 px cells, uncompressed RGB) with one cell per weapon in ICONS order.
Style copied from the stock sword icons: black outline, light top edge, darker left/bottom edge,
1 px inner shadow, maroon gradient, the weapon diagonal (handle bottom-left, tip top-right).
"""
import os
import subprocess
import numpy as np
from PIL import Image
import make_samehada as M

CELL = 35
SS = 8                      # supersampling of the 31x31 interior
SHEET = 'custom_items'
ICONS = [                   # (variant index in make_samehada.VARIANTS or 'gauntlet', cell x, cell y, painted art)
    (0, 0, 0, 'icon_samehada.png'),              # Samehada
    (1, 1, 0, 'icon_samehada_unleashed.png'),    # Samehada Unleashed
    ('gauntlet', 2, 0, 'icon_infinity_gauntlet.png'),   # Infinity Gauntlet
]
# Painted art (assets/custom-items, made with nano-banana from the 3D renders + RAN's own sword
# icons as the style reference) wins over the 3D render when present.
ART = os.path.join(M.REPO, 'assets', 'custom-items')


def background():
    """35x35 RGB cell: frame + maroon gradient (lighter top-left, darker bottom-right)."""
    img = np.zeros((CELL, CELL, 3), np.float32)
    yy, xx = np.mgrid[0:CELL, 0:CELL]
    t = np.clip((xx + yy * 1.4) / (CELL * 2.4), 0, 1)[..., None]
    top, bottom = np.array([112, 68, 76]), np.array([34, 8, 12])
    img[:] = top * (1 - t) + bottom * t
    img[0, :] = img[-1, :] = img[:, 0] = img[:, -1] = 0                       # outline
    img[1, 1:-1] = (183, 176, 167)                                              # top edge
    img[2:-1, 1] = img[-2, 1:-1] = (100, 91, 86)                                # left / bottom edge
    img[1:-1, -2] = (100, 91, 86)
    img[2, 2:-2] = img[2:-2, 2] = (23, 20, 17)                                  # inner shadow
    return img


def render(v, n, uv, idx, tex, size):
    """Orthographic render looking at the blade's flat side, rotated to the 45-degree icon pose.
    Returns RGBA float image size x size."""
    # Turn the blade 30 degrees about its long axis so its thickness and spikes read, then lay
    # it diagonal.
    b = np.deg2rad(30.0)
    spin = np.array([[np.cos(b), 0, np.sin(b)], [0, 1, 0], [-np.sin(b), 0, np.cos(b)]])
    v = v @ spin.T
    n = n @ spin.T
    a = np.deg2rad(-45.0)
    rot = np.array([[np.cos(a), -np.sin(a)], [np.sin(a), np.cos(a)]])
    p2 = v[:, :2] @ rot.T                     # model x/y -> screen; the viewer is at -z
    lo, hi = p2.min(0), p2.max(0)
    scale = (size * 0.94) / (hi - lo).max()
    c = (lo + hi) / 2
    sx = (p2[:, 0] - c[0]) * scale + size / 2
    sy = size / 2 - (p2[:, 1] - c[1]) * scale  # screen y grows downwards
    sz = -v[:, 2]
    nn = n / np.maximum(np.linalg.norm(n, axis=1, keepdims=True), 1e-9)
    nr = np.c_[nn[:, :2] @ rot.T, nn[:, 2]]
    # Key light from the upper left, on the viewer's side (-z): n . light > 0 means lit.
    light = np.array([-0.4, 0.6, -0.7]); light /= np.linalg.norm(light)
    diffuse = np.clip(nr @ light, 0, 1)
    half = light + np.array([0, 0, -1.0]); half /= np.linalg.norm(half)
    spec = np.clip(nr @ half, 0, 1) ** 24
    shade = 0.40 + 0.85 * diffuse + 0.5 * spec

    th, tw = tex.shape[:2]
    color = np.zeros((size, size, 3), np.float32)
    alpha = np.zeros((size, size), np.float32)
    depth = np.full((size, size), -1e9, np.float32)
    for i0, i1, i2 in idx:
        xs, ys = sx[[i0, i1, i2]], sy[[i0, i1, i2]]
        x0, x1 = int(max(np.floor(xs.min()), 0)), int(min(np.ceil(xs.max()), size - 1))
        y0, y1 = int(max(np.floor(ys.min()), 0)), int(min(np.ceil(ys.max()), size - 1))
        if x0 > x1 or y0 > y1:
            continue
        gx, gy = np.meshgrid(np.arange(x0, x1 + 1) + 0.5, np.arange(y0, y1 + 1) + 0.5)
        d = (ys[1] - ys[2]) * (xs[0] - xs[2]) + (xs[2] - xs[1]) * (ys[0] - ys[2])
        if abs(d) < 1e-12:
            continue
        w0 = ((ys[1] - ys[2]) * (gx - xs[2]) + (xs[2] - xs[1]) * (gy - ys[2])) / d
        w1 = ((ys[2] - ys[0]) * (gx - xs[2]) + (xs[0] - xs[2]) * (gy - ys[2])) / d
        w2 = 1 - w0 - w1
        inside = (w0 >= 0) & (w1 >= 0) & (w2 >= 0)
        if not inside.any():
            continue
        z = w0 * sz[i0] + w1 * sz[i1] + w2 * sz[i2]
        win = depth[y0:y1 + 1, x0:x1 + 1]
        draw = inside & (z > win)
        if not draw.any():
            continue
        u = (w0 * uv[i0, 0] + w1 * uv[i1, 0] + w2 * uv[i2, 0]) % 1.0
        t = (w0 * uv[i0, 1] + w1 * uv[i1, 1] + w2 * uv[i2, 1]) % 1.0
        texel = tex[(t * (th - 1)).astype(int), (u * (tw - 1)).astype(int)]
        s = (w0 * shade[i0] + w1 * shade[i1] + w2 * shade[i2])[..., None]
        win[draw] = z[draw]
        color[y0:y1 + 1, x0:x1 + 1][draw] = np.clip(texel * s, 0, 255)[draw]
        alpha[y0:y1 + 1, x0:x1 + 1][draw] = 1
    return color, alpha


def painted_icon(path):
    """Painted square art (weapon on a dark background) scaled into the cell interior."""
    img = Image.open(path).convert('RGB')
    side = min(img.size)
    img = img.crop(((img.width - side) // 2, (img.height - side) // 2,
                    (img.width + side) // 2, (img.height + side) // 2))
    inner = CELL - 5
    small = np.asarray(img.resize((inner, inner), Image.LANCZOS), np.float32)
    cell = background()
    cell[3:3 + inner, 3:3 + inner] = small
    return np.clip(cell, 0, 255).astype(np.uint8)


def gauntlet_geometry():
    """The gauntlet as fitted on the male skeleton, turned so the fingers point along +Y (the
    sword-blade axis render() lays diagonal) and the back of the hand with the stones faces the
    viewer."""
    import make_gauntlet as G
    from xtree import parse
    glb, skin, frame, tag, tex, px, scale = G.VARIANTS[0]
    v, n, uv, idx, bones, png = G.gauntlet_mesh(glb, skin, parse(f'{M.CLIENT}/data/skin/s_m_newc.x'), scale)
    turn = np.array([[0, 0, 1.0], [-1, 0, 0], [0, -1, 0]])   # fingers (-x) -> +y, then a quarter roll
    return (v - v.mean(0)) @ turn.T, n @ turn.T, uv, idx, tex


def icon(variant):
    if variant == 'gauntlet':
        v, n, uv, idx, tex = gauntlet_geometry()
    else:
        glb, scale, frame, tag, tex, px = M.VARIANTS[variant]
        M.MODEL, M.SCALE, M.NEW_FRAME = os.path.expanduser(glb), scale, frame
        v, n, uv, idx, png = M.samehada_geometry()
    texture = np.asarray(Image.open(f'{M.OUT}/{tex}.png').convert('RGB'), np.float32)
    inner = CELL - 4                                   # inside frame + shadow
    color, alpha = render(v, n, uv, idx.astype(int), texture, inner * SS)
    # box-filter down to the cell interior
    color = color.reshape(inner, SS, inner, SS, 3)
    alpha = alpha.reshape(inner, SS, inner, SS)
    a = alpha.mean((1, 3))
    rgb = (color * alpha[..., None]).sum((1, 3)) / np.maximum(alpha.sum((1, 3))[..., None], 1e-6)
    cell = background()
    region = cell[3:3 + inner, 3:3 + inner]
    k = min(inner, CELL - 4 - 1)                       # keep the right/bottom edges intact
    region[:k, :k] = region[:k, :k] * (1 - a[:k, :k, None]) + rgb[:k, :k] * a[:k, :k, None]
    return np.clip(cell, 0, 255).astype(np.uint8)


def main():
    sheet = np.zeros((256, 512, 3), np.uint8)
    for variant, cx, cy, art in ICONS:
        painted = os.path.join(ART, art)
        cell = painted_icon(painted) if os.path.exists(painted) else icon(variant)
        sheet[cy * CELL:(cy + 1) * CELL, cx * CELL:(cx + 1) * CELL] = cell
        Image.fromarray(sheet[cy * CELL:(cy + 1) * CELL, cx * CELL:(cx + 1) * CELL]).resize((140, 140), Image.NEAREST) \
            .save(f'{M.OUT}/icon_{variant}.png')
    png = f'{M.OUT}/{SHEET}.png'
    Image.fromarray(sheet).save(png)
    # same layout as the stock sheets: uncompressed, no alpha, no mipmaps
    subprocess.run(['magick', png, '-alpha', 'off', '-define', 'dds:compression=none', '-define', 'dds:mipmaps=0',
                    f'{M.OUT}/{SHEET}.dds'], check=True)
    print('icons ->', f'{M.OUT}/{SHEET}.dds')


if __name__ == '__main__':
    main()
