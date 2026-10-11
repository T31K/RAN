"""Odyssey weapons: six glb models -> RAN weapon assets, each held like a stock weapon of its type.

  make_odyssey.py        (needs: numpy, Pillow, ImageMagick `magick`; reads the stock client
                          for the template frame chains, meshes and piece files)

Rigid weapons (sword, spears, staff, bow) hang on the template's own frame chain, as in
make_samehada.py, but are placed by measuring the stock mesh of EACH sex (the male and female
template meshes have different mesh-space axes and sizes - only the attach frame `gum` /
`stick` / `whal` space is the same):
  - the glb is turned into attach space with a fixed signed axis map (AXES: glb long axis ->
    attach long axis, glb wide axis -> attach wide axis, the third axis follows so the model
    keeps its handedness when going from glTF's right-handed to the game's left-handed space);
  - its length becomes the stock weapon's length x LENGTH factor;
  - the point GRIP of the way from butt to tip (on the model's own centre line there) goes to
    the hand: the attach-frame origin, on the stock weapon's centre line;
  - then back into that sex's mesh space (inverse of the template's frame matrices), so the
    template's trace points (IMAGE01/IMAGE02/STRIKE01 in the .cps) stay along the weapon.
The brawler gauntlet has no rig: it is fitted onto the Biped right forearm/hand with one
similarity transform (wrist + finger directions) and skinned by distance to the forearm, hand
and finger bones, so its fingers close with the fist animations (make_gauntlet.write_x).

Into port/build/custom_items:
  s_{m,w}_<tag>.X         text .x: template bone chain + the weapon frame + mesh
  <frame>_{M,W}.cps       copies of the template .cps with same-length names swapped
  <tex>.dds / <tex>_s.dds DXT1 diffuse / DXT5 specular (shine mask in alpha)
  <tex>.png               the diffuse (render_icons.py reads it)
"""
import io
import os
import subprocess
import numpy as np
from PIL import Image
from glb import load
from xtree import parse, find_path, matrix
import make_samehada as M
import make_gauntlet as G
from make_samehada import CLIENT, REPO, OUT, swap_same_length

MODELS = os.environ.get('ODYSSEY_MODELS', os.path.join(REPO, 'assets', 'custom-items', 'odyssey'))


class W:
    """One weapon. Same-length names (the template piece files are patched in place)."""
    def __init__(self, key, glb, template, xfile, frame, tag, tex, px, axes, grip, length, spec=0.25):
        self.key, self.glb, self.template, self.xfile = key, glb, template, xfile
        self.frame, self.tag, self.tex, self.px = frame, tag, tex, px
        self.axes, self.grip, self.length, self.spec = axes, grip, length, spec


# axes: (glb long axis toward the tip, attach long axis toward the tip,
#        glb wide axis, attach wide axis). Template meshes (attach space, hand at the origin):
#   SDN0024 Steel Sword  gum   pommel -y .. tip +y, blade wide in x      (hand 16% from pommel)
#   SRN0040 Mystic Spear stick butt -y .. head +y, head wide in x        (hand 52% from butt)
#   SRN0042 Sky Wand     stick foot -y .. head +y, guard wide in z       (hand 76% from foot)
#   BDN0050 bow          whal  limbs along z, string side +x             (hand at the grip, 49%)
#   NLN0040 gauntlet     skinned to the right forearm (see heracles_mesh)
WEAPONS = [
    W('achilles', 'achilles_sword', 'SDN0024', 'gum', 'SDN9026', 'ach', 'achilles', 512,
      ('-y', '+y', '+x', '+x'), grip=0.12, length=1.05),
    W('sparta', 'spartan_spear', 'SRN0040', 'jipang', 'SRN9041', 'sparta', 'sparta_sp', 1024,
      ('+y', '+y', '+x', '+x'), grip=0.50, length=1.05),
    W('trident', 'trident', 'SRN0040', 'jipang', 'SRN9043', 'tridnt', 'trident01', 512,
      ('+y', '+y', '+z', '+x'), grip=0.45, length=1.0, spec=0.4),
    W('odysseus', 'odysseus_bow', 'BDN0050', 'newc', 'BDN9050', 'odys', 'odysbow', 1024,
      ('+y', '+z', '+x', '+x'), grip=0.49, length=1.0, spec=0.4),
    W('circe', 'circe_staff', 'SRN0042', 'jipang', 'SRN9042', 'circes', 'circe_stf', 1024,
      ('+y', '+y', '+x', '+x'), grip=0.64, length=1.15),
]
# The gauntlet: (key, glb folder, template frame, mesh file, frame, tag, texture stem, px, roominess)
HERACLES = ('heracles', 'heracles_gauntlet', 'NLN0040', 'newc', 'NLN9041', 'herc', 'heracle', 1024, 1.05)
GLOVE_SHAPE = (1.0, 0.65, 0.95)   # glove x (to the fingertips), y (back of the hand), z (thumb side)
KNUCKLE_X, THUMB_Z = 0.42, 0.12   # glb units: the fingers start at x 0.42, the thumb bulges past z 0.12

# template texture names in the .cps (diffuse, specular) - swapped for the weapon's own
TEMPLATE_TEX = {
    'SDN0024': ('Sword_03.dds', 'Sword_03_s.dds'),
    'SRN0040': ('Spear_001.DDS', 'Spear_001_s.DDS'),
    'SRN0042': ('Staff_001.DDS', 'Staff_001_s.DDS'),
    'BDN0050': ('bow_003.dds', 'bow_003_s.dds'),
    'NLN0040': ('F_at_05.dds', 'F_at_05_s.dds'),
}


def glb_path(folder):
    return os.path.join(MODELS, folder, 'model.glb')


# ---------------------------------------------------------------- stock meshes

def stock_piece(xfile, frame):
    """Template piece of a stock binary .x: (frame chain, frame matrix, inner matrix, mesh vertices)."""
    path = find_path(parse(f'{CLIENT}/data/skin/{xfile}'), frame)
    inner = [k for k in path[-1].kids if k.typ == 'Frame'][0]
    mesh = [k for k in inner.kids if k.typ == 'Mesh'][0]
    n = [v for t, v in mesh.data if t == 'ILIST'][0][0]
    v = np.array([v for t, v in mesh.data if t == 'FLIST'][0]).reshape(n, 3)
    return path, np.array(matrix(path[-1])).reshape(4, 4), np.array(matrix(inner)).reshape(4, 4), v


def to_attach(v, T):
    return (np.c_[v, np.ones(len(v))] @ T)[:, :3]


# ---------------------------------------------------------------- rigid weapons

AX = {'x': 0, 'y': 1, 'z': 2}


def axis_map(axes):
    """Signed 3x3 P with attach = P @ glb. The third axis gets the sign that makes det(P) = -1:
    glTF is right-handed, the game left-handed, so a reflection keeps the model unmirrored."""
    gl, al, gw, aw = axes
    P = np.zeros((3, 3))
    P[AX[al[1]], AX[gl[1]]] = (1 if gl[0] == '+' else -1) * (1 if al[0] == '+' else -1)
    P[AX[aw[1]], AX[gw[1]]] = (1 if gw[0] == '+' else -1) * (1 if aw[0] == '+' else -1)
    gt = ({0, 1, 2} - {AX[gl[1]], AX[gw[1]]}).pop()
    at = ({0, 1, 2} - {AX[al[1]], AX[aw[1]]}).pop()
    P[at, gt] = 1
    if np.linalg.det(P) > 0:
        P[at, gt] = -1
    return P


def centre(v, la, at, half):
    """Robust centre line point (the two cross coordinates) of the vertices within `half` of
    L = at along axis la; widens the window until it holds some vertices."""
    while True:
        s = v[np.abs(v[:, la] - at) <= half]
        if len(s) >= 8:
            return np.median(s, 0)
        half *= 1.6


def place(w, sex):
    """The weapon's vertices/normals in the template mesh space of `sex`, plus the attach-space
    copy (for previews) and the template's frame chain and matrices."""
    pos, nor, uv, idx, png = load(glb_path(w.glb))
    P = axis_map(w.axes)
    u, nu = pos @ P.T, nor @ P.T
    la = AX[w.axes[1][1]]

    path, Mf, Mi, sv = stock_piece(f's_{sex}_{w.xfile}.X', w.template)
    T = Mi @ Mf                                      # mesh -> attach (row vectors)
    sa = to_attach(sv, T)
    slen = np.ptp(sa[:, la])
    nlo, nhi = u[:, la].min(), u[:, la].max()
    s = w.length * slen / (nhi - nlo)
    g = nlo + w.grip * (nhi - nlo)
    cn = centre(u, la, g, 0.02 * (nhi - nlo))        # the model's centre line at its grip
    cs = centre(sa, la, 0.0, 0.08 * slen)            # the stock weapon's centre line at the hand
    cn[la], cs[la] = g, 0.0
    a = (u - cn) * s + cs                            # attach space
    v = to_attach(a, np.linalg.inv(T))               # template mesh space
    n = nu @ np.linalg.inv(T)[:3, :3]
    n /= np.maximum(np.linalg.norm(n, axis=1, keepdims=True), 1e-9)
    # wind triangles so cross(b-a, c-a) follows the vertex normals, like the game's meshes
    p0, p1, p2 = v[idx[:, 0]], v[idx[:, 1]], v[idx[:, 2]]
    if ((np.cross(p1 - p0, p2 - p0) * (n[idx[:, 0]] + n[idx[:, 1]] + n[idx[:, 2]])).sum(1) > 0).mean() < 0.5:
        idx = idx[:, [0, 2, 1]]
    return dict(v=v, n=n, uv=uv, idx=idx.astype(int), png=png, attach=a, stock_attach=sa,
                path=path, Mf=Mf, Mi=Mi, scale=s, la=la)


# ---------------------------------------------------------------- the gauntlet

ARM_BONES = ['Bip01_R_Forearm', 'Bip01_R_Hand']
FINGERS = [[f'Bip01_R_Finger{k}', f'Bip01_R_Finger{k}1', f'Bip01_R_Finger{k}2', f'Bip01_R_Finger{k}Nub'] for k in range(5)]


def heracles_mesh(tree, roomy):
    """The BB gauntlet glb is a LEFT glove in glTF space (fingers +x, back of the hand +y, thumb
    +z). Read as left-handed coordinates as is, that is a right glove - what the game wants.
    Fit: glove wrist -> Bip01_R_Hand origin, fingers -> the hand-to-knuckles direction, thumb
    side -> pinky-to-index direction; scale wrist-to-fingertip onto the Biped's.
    Skin: behind the wrist -> forearm (blending into the hand); in front of it each vertex goes
    to its nearest two of the hand and 15 finger bone segments, laid out on the glove."""
    pos, nor, uv, idx, png = load(glb_path(HERACLES[1]))
    bw = lambda b: G.bone_world(tree, b)[3, :3]
    hand = bw('Bip01_R_Hand')
    knuckles = np.mean([bw(f[0]) for f in FINGERS[1:]], 0)
    f_dir = knuckles - hand; f_dir /= np.linalg.norm(f_dir)
    t_dir = bw('Bip01_R_Finger1') - bw('Bip01_R_Finger4')
    t_dir -= f_dir * (t_dir @ f_dir); t_dir /= np.linalg.norm(t_dir)
    u_dir = np.cross(t_dir, f_dir)                  # back of the hand (T x F, see docstring)
    R = np.c_[f_dir, u_dir, t_dir]                  # glove x, y, z -> Biped directions

    wrist_x = 0.0                                    # narrowest point between cuff and palm
    sl = pos[np.abs(pos[:, 0] - wrist_x) < 0.03]
    wrist = np.array([wrist_x, (sl[:, 1].min() + sl[:, 1].max()) / 2, (sl[:, 2].min() + sl[:, 2].max()) / 2])
    glove_len = pos[:, 0].max() - wrist_x
    mid_tip = bw('Bip01_R_Finger2Nub')
    s = np.linalg.norm(mid_tip - hand) / glove_len * roomy
    # The glove is stubbier than the Biped hand (length/width 2.1 vs 3.2): at full length its
    # back-to-palm thickness is ~2.5x the bare wrist (m_bs_hand: 0.45 x 0.7 units), so it is
    # flattened back-to-palm and slightly narrowed.
    S = s * np.array(GLOVE_SHAPE)
    v = hand + ((pos - wrist) * S) @ R.T
    n = (nor / S) @ R.T
    n /= np.maximum(np.linalg.norm(n, axis=1, keepdims=True), 1e-9)
    a, b, c = v[idx[:, 0]], v[idx[:, 1]], v[idx[:, 2]]
    if ((np.cross(b - a, c - a) * (n[idx[:, 0]] + n[idx[:, 1]] + n[idx[:, 2]])).sum(1) > 0).mean() < 0.5:
        idx = idx[:, [0, 2, 1]]

    # Bone segments for the weights are laid out on the glove itself (its fingers are straight,
    # the Biped's fan out differently per sex): four evenly spaced straight fingers from the
    # knuckle line to the tips, the thumb along its own bulge, each split into three phalanges
    # in the Biped's bone length ratios. Distances in the shaped glove space.
    segs = [('Bip01_R_Hand', wrist, None)]
    fing = pos[pos[:, 0] > KNUCKLE_X + 0.02]
    z0, z1 = np.quantile(fing[:, 2], [0.05, 0.95])
    y_root = np.median(pos[(np.abs(pos[:, 0] - KNUCKLE_X) < 0.03) & (pos[:, 2] < THUMB_Z), 1])
    tip_x = pos[:, 0].max()
    y_tip = np.median(pos[pos[:, 0] > tip_x - 0.06, 1])
    roots = []
    for k, chain in enumerate(FINGERS[1:]):
        z = z1 - (k + 0.5) / 4 * (z1 - z0)                  # index (k=0) on the thumb side
        roots.append(([KNUCKLE_X, y_root, z], [tip_x, y_tip, z], chain))
    th = pos[(pos[:, 2] > THUMB_Z) & (pos[:, 0] > 0.0) & (pos[:, 0] < KNUCKLE_X + 0.03)]
    roots.append((th[th[:, 0] < 0.12].mean(0), th[th[:, 0] > KNUCKLE_X - 0.08].mean(0), FINGERS[0]))
    segs[0] = ('Bip01_R_Hand', wrist, np.mean([r[0] for r in roots[:4]], 0))
    for r0, r1, chain in roots:
        r0, r1 = np.asarray(r0), np.asarray(r1)
        lens = np.array([np.linalg.norm(bw(b1) - bw(b0)) for b0, b1 in zip(chain[:-1], chain[1:])])
        cut = np.r_[0, np.cumsum(lens) / lens.sum()]
        for b, t0, t1 in zip(chain[:-1], cut[:-1], cut[1:]):
            segs.append((b, r0 + (r1 - r0) * t0, r0 + (r1 - r0) * t1))
    shaped = pos * GLOVE_SHAPE
    d = np.stack([seg_dist(shaped, p0 * GLOVE_SHAPE, p1 * GLOVE_SHAPE) for _, p0, p1 in segs], 1)
    # the sleeve flares well away from the forearm bone: everything behind the wrist rides the
    # forearm (blended into the hand over the last 15% of the forearm)
    along = (v - hand) @ f_dir
    flen = np.linalg.norm(hand - bw('Bip01_R_Forearm'))
    bones = {}
    for vi in range(len(v)):
        if along[vi] < -0.15 * flen:
            w = {'Bip01_R_Forearm': 1.0}
        elif along[vi] < 0:
            t = -along[vi] / (0.15 * flen)
            w = {'Bip01_R_Forearm': t, 'Bip01_R_Hand': 1 - t}
        else:
            k = np.argsort(d[vi])[:2]
            iw = 1.0 / np.maximum(d[vi, k], 1e-3) ** 4
            iw /= iw.sum()
            w = {}
            for kk, ww in zip(k, iw):
                if ww > 0.1:
                    w[segs[kk][0]] = w.get(segs[kk][0], 0) + ww
            tot = sum(w.values())
            w = {kk: ww / tot for kk, ww in w.items()}
        for bone, ww in w.items():
            if ww > 1e-4:
                bones.setdefault(bone, {})[vi] = ww
    return v, n, uv, idx.astype(int), bones, png


def seg_dist(p, a, b):
    ab = b - a
    t = np.clip(((p - a) @ ab) / (ab @ ab), 0, 1)
    return np.linalg.norm(p - (a + t[:, None] * ab), axis=1)


# ---------------------------------------------------------------- textures and pieces

def write_cps(template, xfile, sex, tag, frame, tex):
    src = subprocess.run(['unzip', '-p', f'{CLIENT}/data/skinobject/skinobject.rcc', f'{template}_{sex.upper()}.cps'],
                         capture_output=True, check=True).stdout
    diffuse, specular = TEMPLATE_TEX[template]
    cps = swap_same_length(src, [
        (f's_{sex}_{xfile}.X'.encode(), f's_{sex}_{tag}.X'.encode()),
        (f'{template}[Mesh]'.encode(), f'{frame}[Mesh]'.encode()),
        (specular.encode(), f'{tex}_s.dds'.encode()),
        (diffuse.encode(), f'{tex}.dds'.encode()),
    ])
    open(f'{OUT}/{frame}_{sex.upper()}.cps', 'wb').write(cps)


def write_textures(png, tex, px, spec):
    Image.open(io.BytesIO(png)).convert('RGB').save(f'{OUT}/{tex}.png')
    size, mips = f'{px}x{px}', str(int(np.log2(px)))
    subprocess.run(['magick', f'{OUT}/{tex}.png', '-resize', size, '-define', 'dds:compression=dxt1',
                    '-define', f'dds:mipmaps={mips}', f'{OUT}/{tex}.dds'], check=True)
    # specular: the game reads the shine mask from ALPHA (see make_samehada.py)
    subprocess.run(['magick', f'{OUT}/{tex}.png', '-resize', size, '(', '+clone', '-colorspace', 'gray',
                    '-evaluate', 'multiply', str(spec), ')', '-alpha', 'off', '-compose', 'CopyOpacity', '-composite',
                    '-define', 'dds:compression=dxt5', '-define', f'dds:mipmaps={mips}', f'{OUT}/{tex}_s.dds'], check=True)


def build_rigid(w):
    for sex in ('m', 'w'):
        p = place(w, sex)
        a = p['attach']
        print(f'{w.frame} {sex} scale {p["scale"]:.3f} attach bbox {a.min(0).round(2)} {a.max(0).round(2)} '
              f'stock {p["stock_attach"].min(0).round(2)} {p["stock_attach"].max(0).round(2)} '
              f'{len(p["v"])} verts {len(p["idx"])} tris')
        M.write_x(f'{OUT}/s_{sex}_{w.tag}.X', p['path'][:-1], p['Mf'].ravel(), p['Mi'].ravel(),
                  p['v'], p['n'], p['uv'], p['idx'], f'{w.tex}.dds', frame=w.frame)
        write_cps(w.template, w.xfile, sex, w.tag, w.frame, w.tex)
    write_textures(p['png'], w.tex, w.px, w.spec)


def build_heracles():
    key, folder, template, xfile, frame, tag, tex, px, roomy = HERACLES
    for sex in ('m', 'w'):
        tree = parse(f'{CLIENT}/data/skin/s_{sex}_newc.x')
        v, n, uv, idx, bones, png = heracles_mesh(tree, roomy)
        print(f'{frame} {sex} bbox {v.min(0).round(2)} {v.max(0).round(2)} {len(v)} verts {len(idx)} tris '
              f'{len(bones)} bones')
        G.write_x(f'{OUT}/s_{sex}_{tag}.X', tree, frame, v, n, uv, idx, bones, f'{tex}.dds')
        write_cps(template, xfile, sex, tag, frame, tex)
    write_textures(png, tex, px, 0.3)


def pieces():
    """(frame, mesh tag, texture stem) of every Odyssey model (install.py)."""
    return [(w.frame, w.tag, w.tex) for w in WEAPONS] + [(HERACLES[4], HERACLES[5], HERACLES[6])]


def icon_geometry(key):
    """Model for render_icons.py: long axis +Y (tip up), wide axis +X, centred."""
    if key == 'heracles':
        tree = parse(f'{CLIENT}/data/skin/s_m_newc.x')
        v, n, uv, idx, bones, png = heracles_mesh(tree, HERACLES[8])
        hand = G.bone_world(tree, 'Bip01_R_Hand')[3, :3]
        # Biped fingers point along -x, back of the hand +y: fingers -> +y, back of hand -> -z
        turn = np.array([[0, 0, 1.0], [-1, 0, 0], [0, -1, 0]])
        return (v - hand) @ turn.T, n @ turn.T, uv, idx, HERACLES[6]
    w = next(w for w in WEAPONS if w.key == key)
    pos, nor, uv, idx, png = load(glb_path(w.glb))
    P = axis_map(w.axes)
    la, wa = AX[w.axes[1][1]], AX[w.axes[3][1]]
    ta = ({0, 1, 2} - {la, wa}).pop()
    Q = np.zeros((3, 3)); Q[0, wa] = 1; Q[1, la] = 1; Q[2, ta] = 1     # attach -> (wide, long, thin)
    if np.linalg.det(Q) < 0:
        Q[2, ta] = -1
    v = pos @ P.T @ Q.T
    return v - (v.min(0) + v.max(0)) / 2, nor @ P.T @ Q.T, uv, idx.astype(int), w.tex


def main():
    for w in WEAPONS:
        build_rigid(w)
    build_heracles()
    print('wrote', sorted(os.listdir(OUT)))


if __name__ == '__main__':
    main()
