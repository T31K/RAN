"""Offline sanity renders of the Odyssey weapons against the stock template weapon.

  preview_odyssey.py        (after make_odyssey.py; numpy + Pillow)

Rigid weapons: re-reads the written s_{m,w}_<tag>.X (text .x) and the stock template mesh,
both taken into the attach frame (gum / stick / whal) with their own frame matrices, and
draws them over each other in two orthographic views (along the thin and the wide axis):
the new weapon textured, the stock weapon as a red outline, the hand (attach origin) as a
cyan cross. The gauntlet: the skinned mesh in the bind pose, coloured by its main bone, with
the Biped forearm/hand/finger bones drawn in, next to the stock NLN0040 outline.
Writes port/build/custom_items/preview/<frame>_<sex>.png.
"""
import os
import re
import sys
import numpy as np
from PIL import Image, ImageDraw
import make_odyssey as O
import make_gauntlet as G
from make_samehada import CLIENT, OUT
from xtree import parse

PREV = os.path.join(OUT, 'preview')
SIZE = 420


def read_text_x(path):
    """Frame matrices (outermost first), vertices, faces, uvs and skin weights of our text .x."""
    s = open(path).read()
    mats = [np.array([float(x) for x in m.split(',')[:16]]).reshape(4, 4)
            for m in re.findall(r'FrameTransformMatrix \{ ([^;]+);;', s)]
    names = re.findall(r'Frame (\S*) ?\{', s)
    body = s[s.index('Mesh {'):]
    nums = re.split(r'[;,\s]+', body[body.index('{') + 1:])
    nums = [x for x in nums if x]
    nv = int(nums[0])
    v = np.array([float(x) for x in nums[1:1 + 3 * nv]]).reshape(nv, 3)
    nf = int(nums[1 + 3 * nv])
    f = np.array([int(x) for x in nums[2 + 3 * nv:2 + 3 * nv + 4 * nf]]).reshape(nf, 4)[:, 1:]
    tc = body[body.index('MeshTextureCoords'):]
    tn = [x for x in re.split(r'[;,\s{]+', tc[tc.index('{') + 1:]) if x]
    uv = np.array([float(x) for x in tn[1:1 + 2 * nv]]).reshape(nv, 2)
    skin = {}
    for m in re.finditer(r'SkinWeights \{\s*"([^"]+)";\s*(\d+);\s*([\d,]+);\s*([\d.,e\-]+);', body):
        ids = [int(x) for x in m.group(3).split(',')]
        ws = [float(x) for x in m.group(4).split(',')]
        skin[m.group(1)] = dict(zip(ids, ws))
    return names, mats, v, f, uv, skin


def raster(img, zb, p2, depth, faces, col):
    for t in faces:
        xs, ys = p2[t, 0], p2[t, 1]
        x0, x1 = int(max(xs.min(), 0)), int(min(xs.max() + 1, SIZE - 1))
        y0, y1 = int(max(ys.min(), 0)), int(min(ys.max() + 1, SIZE - 1))
        if x0 > x1 or y0 > y1:
            continue
        gx, gy = np.meshgrid(np.arange(x0, x1 + 1) + .5, np.arange(y0, y1 + 1) + .5)
        d = (ys[1] - ys[2]) * (xs[0] - xs[2]) + (xs[2] - xs[1]) * (ys[0] - ys[2])
        if abs(d) < 1e-12:
            continue
        w0 = ((ys[1] - ys[2]) * (gx - xs[2]) + (xs[2] - xs[1]) * (gy - ys[2])) / d
        w1 = ((ys[2] - ys[0]) * (gx - xs[2]) + (xs[0] - xs[2]) * (gy - ys[2])) / d
        w2 = 1 - w0 - w1
        ins = (w0 >= 0) & (w1 >= 0) & (w2 >= 0)
        z = w0 * depth[t[0]] + w1 * depth[t[1]] + w2 * depth[t[2]]
        win = zb[y0:y1 + 1, x0:x1 + 1]
        dr = ins & (z > win)
        win[dr] = z[dr]
        c = w0[..., None] * col[t[0]] + w1[..., None] * col[t[1]] + w2[..., None] * col[t[2]]
        img[y0:y1 + 1, x0:x1 + 1][dr] = c[dr]


def view(new, faces, col, stock, sfaces, a, b, c, extra=(), label=''):
    """Orthographic view: screen right = axis a, up = axis b, depth = axis c (larger = nearer)."""
    allp = np.vstack([new, stock])
    lo, hi = allp[:, [a, b]].min(0), allp[:, [a, b]].max(0)
    sc = SIZE * 0.86 / max((hi - lo).max(), 1e-6)
    ctr = (lo + hi) / 2
    tos = lambda p: np.c_[(p[:, a] - ctr[0]) * sc + SIZE / 2, SIZE / 2 - (p[:, b] - ctr[1]) * sc]
    img = np.full((SIZE, SIZE, 3), 32, np.float32)
    zb = np.full((SIZE, SIZE), -1e9)
    raster(img, zb, tos(new), new[:, c], faces, col)
    im = Image.fromarray(np.clip(img, 0, 255).astype(np.uint8))
    dr = ImageDraw.Draw(im)
    sp = tos(stock)
    edges = set()
    for t in sfaces:                                   # stock silhouette-ish wire, sparse
        for e in ((t[0], t[1]), (t[1], t[2]), (t[2], t[0])):
            edges.add(tuple(sorted(e)))
    for i, j in edges:
        dr.line([tuple(sp[i]), tuple(sp[j])], fill=(230, 60, 60), width=1)
    for pts, color in extra:
        q = tos(np.array(pts))
        dr.line([tuple(p) for p in q], fill=color, width=2)
    o = tos(np.zeros((1, 3)))[0]
    dr.line([(o[0] - 7, o[1]), (o[0] + 7, o[1])], fill=(0, 255, 255), width=2)
    dr.line([(o[0], o[1] - 7), (o[0], o[1] + 7)], fill=(0, 255, 255), width=2)
    dr.text((4, 4), label, fill=(255, 255, 0))
    dr.text((4, SIZE - 14), f'1 grid = {1 / sc * 50:.2f} u per 50px', fill=(160, 160, 160))
    return im


def tex_colors(tex, uv, shade=None):
    t = np.asarray(Image.open(f'{OUT}/{tex}.png').convert('RGB').resize((256, 256)), np.float32)
    return t[((uv[:, 1] % 1) * 255).astype(int), ((uv[:, 0] % 1) * 255).astype(int)]


def rigid(w, sex):
    names, mats, v, f, uv, _ = read_text_x(f'{OUT}/s_{sex}_{w.tag}.X')
    # the last two matrices are the weapon frame and its inner frame
    T = mats[-1] @ mats[-2]
    new = O.to_attach(v, T)
    path, Mf, Mi, sv = O.stock_piece(f's_{sex}_{w.xfile}.X', w.template)
    stock = O.to_attach(sv, Mi @ Mf)
    sf = O_stock_faces(f's_{sex}_{w.xfile}.X', w.template)
    col = tex_colors(w.tex, uv)
    la, wa = O.AX[w.axes[1][1]], O.AX[w.axes[3][1]]
    ta = ({0, 1, 2} - {la, wa}).pop()
    a1 = view(new, f, col, stock, sf, wa, la, ta, label=f'{w.frame} {sex}: look along thin axis (red = {w.template})')
    a2 = view(new, f, col, stock, sf, ta, la, wa, label=f'{w.frame} {sex}: look along wide axis')
    out = Image.new('RGB', (SIZE * 2, SIZE))
    out.paste(a1, (0, 0)); out.paste(a2, (SIZE, 0))
    return out


def O_stock_faces(xfile, frame):
    from xtree import find_path
    path = find_path(parse(f'{CLIENT}/data/skin/{xfile}'), frame)
    inner = [k for k in path[-1].kids if k.typ == 'Frame'][0]
    mesh = [k for k in inner.kids if k.typ == 'Mesh'][0]
    fl = [v for t, v in mesh.data if t == 'ILIST'][1]
    out, o = [], 1
    for _ in range(fl[0]):
        k = fl[o]
        fc = fl[o + 1:o + 1 + k]
        out += [[fc[0], fc[i], fc[i + 1]] for i in range(1, k - 1)]
        o += 1 + k
    return np.array(out)


def gauntlet(sex):
    key, folder, template, xfile, frame, tag, tex, px, roomy = O.HERACLES
    names, mats, v, f, uv, skin = read_text_x(f'{OUT}/s_{sex}_{tag}.X')
    tree = parse(f'{CLIENT}/data/skin/s_{sex}_newc.x')
    m_root = mats[0]
    world = O.to_attach(v, m_root)                       # mesh -> bind world (Scene_Root)
    path, Mf, Mi, sv = O.stock_piece(f's_{sex}_newc.x', template)
    stock = O.to_attach(sv, Mi @ Mf @ m_root)
    sf = O_stock_faces(f's_{sex}_newc.x', template)
    palette = {}
    main = {}
    for bone, ws in skin.items():
        for i, wv in ws.items():
            if wv > main.get(i, (None, 0))[1]:
                main[i] = (bone, wv)
    rng = np.random.default_rng(3)
    col = np.zeros((len(v), 3))
    base = tex_colors(tex, uv)
    for i in range(len(v)):
        bone = main.get(i, ('none', 0))[0]
        if bone not in palette:
            palette[bone] = rng.uniform(60, 255, 3)
        col[i] = 0.45 * base[i] + 0.55 * palette[bone]
    bw = lambda b: G.bone_world(tree, b)[3, :3]
    lines = [([bw('Bip01_R_Forearm'), bw('Bip01_R_Hand')], (255, 255, 255))]
    for chain in O.FINGERS:
        lines.append(([bw('Bip01_R_Hand')] + [bw(b) for b in chain], (255, 255, 255)))
    hand = bw('Bip01_R_Hand')
    shift = lambda p: p - hand                          # hand at the cross
    lines = [([shift(p) for p in pts], c) for pts, c in lines]
    a1 = view(shift(world), f, col, shift(stock), sf, 0, 2, 1, lines, f'{frame} {sex}: from above (+y), red = {template}')
    a2 = view(shift(world), f, col, shift(stock), sf, 0, 1, 2, lines, f'{frame} {sex}: from the side')
    out = Image.new('RGB', (SIZE * 2, SIZE))
    out.paste(a1, (0, 0)); out.paste(a2, (SIZE, 0))
    return out


def fist(sex, curl=-70.0):
    """The skinned gauntlet with every finger bone bent `curl` degrees about its local Y (the
    Biped flex axis; the thumb 60%): the glove's fingers must close as whole fingers, no tearing."""
    tag = O.HERACLES[5]
    names, mats, v, f, uv, skin = read_text_x(f'{OUT}/s_{sex}_{tag}.X')
    tree = parse(f'{CLIENT}/data/skin/s_{sex}_newc.x')
    bind, posed = {}, {}

    def rot_y(a):
        c, s = np.cos(np.deg2rad(a)), np.sin(np.deg2rad(a))
        r = np.eye(4)
        r[2, 2], r[2, 0], r[0, 2], r[0, 0] = c, s, -s, c
        return r

    def walk(node, pb, pp):
        for k in node.kids:
            if k.typ != 'Frame':
                continue
            loc = np.array(G.matrix(k)).reshape(4, 4)
            lp = loc
            if k.name and 'Finger' in k.name and 'Nub' not in k.name:
                lp = rot_y(curl * (0.6 if 'Finger0' in k.name else 1.0)) @ loc
            b, p = loc @ pb, lp @ pp
            if k.name:
                bind[k.name], posed[k.name] = b, p
            walk(k, b, p)
    walk(tree, np.eye(4), np.eye(4))
    vh = np.c_[v, np.ones(len(v))]
    out, tot = np.zeros((len(v), 3)), np.zeros(len(v))
    for bone, ws in skin.items():
        m = mats[0] @ np.linalg.inv(bind[bone]) @ posed[bone]
        ids = np.array(list(ws))
        w = np.array([ws[i] for i in ids])
        out[ids] += w[:, None] * (vh[ids] @ m)[:, :3]
        tot[ids] += w
    assert np.allclose(tot, 1, atol=1e-3), (tot.min(), tot.max())
    hand = bind['Bip01_R_Hand'][3, :3]
    col = tex_colors(O.HERACLES[6], uv)
    p = out - hand
    a1 = view(p, f, col, p[:1], np.zeros((0, 3), int), 0, 1, 2, label=f'{O.HERACLES[4]} {sex}: fingers curled {curl:.0f} deg, side')
    a2 = view(p, f, col, p[:1], np.zeros((0, 3), int), 0, 2, 1, label='from above')
    img = Image.new('RGB', (SIZE * 2, SIZE))
    img.paste(a1, (0, 0)); img.paste(a2, (SIZE, 0))
    return img


def main():
    os.makedirs(PREV, exist_ok=True)
    only = sys.argv[1:]
    for w in O.WEAPONS:
        if only and w.key not in only:
            continue
        for sex in 'mw':
            rigid(w, sex).save(f'{PREV}/{w.frame}_{sex}.png')
    if not only or 'heracles' in only:
        for sex in 'mw':
            gauntlet(sex).save(f'{PREV}/{O.HERACLES[4]}_{sex}.png')
            fist(sex).save(f'{PREV}/{O.HERACLES[4]}_{sex}_fist.png')
    print('previews ->', PREV)


if __name__ == '__main__':
    main()
