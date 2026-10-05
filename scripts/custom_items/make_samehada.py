"""glb models -> RAN sword assets, held exactly like Steel Sword (SDN0024).

  make_samehada.py        (needs: numpy, ImageMagick `magick`; reads the stock client for the
                           Steel Sword bone chain and piece files)

Per variant, into port/build/custom_items:
  s_{m,w}_<tag>.X         text .x: bone chain down to 'gum' + the sword frame + mesh
  <frame>_{M,W}.cps       copies of SDN0024_{M,W}.cps with same-length names swapped
  <tex>.dds / <tex>_s.dds DXT1 diffuse / DXT5 specular (shine mask in alpha)
"""
import os
import subprocess
import numpy as np
from glb import load
from xtree import parse, find_path, matrix

CLIENT = os.environ.get('RAN_CLIENT', os.path.expanduser('~/Projects/RAN/client'))
HERE = os.path.dirname(os.path.abspath(__file__))
REPO = os.path.abspath(os.path.join(HERE, '..', '..'))
OUT = os.path.join(REPO, 'port', 'build', 'custom_items')
os.makedirs(OUT, exist_ok=True)

# set per variant by build()
MODEL, SCALE, NEW_FRAME = None, 1.0, None


def samehada_geometry():
    pos, nor, uv, idx, png = load(MODEL)
    # Long axis = blade. The pommel end is the thin one. Game swords: pommel at y=0, blade toward
    # +Y, grip axis on x=z=0.
    ax = int(np.argmax(np.ptp(pos, 0)))
    lo, hi = pos[:, ax].min(), pos[:, ax].max()
    span = hi - lo
    near_lo = pos[pos[:, ax] < lo + 0.15 * span]
    near_hi = pos[pos[:, ax] > hi - 0.15 * span]
    others = [k for k in range(3) if k != ax]
    width = lambda s: np.ptp(s[:, others[0]]) + np.ptp(s[:, others[1]])
    pommel_low = width(near_lo) < width(near_hi)
    grip = near_lo if pommel_low else near_hi
    c0, c1 = grip[:, others[0]].mean(), grip[:, others[1]].mean()
    along = (pos[:, ax] - lo) if pommel_low else (hi - pos[:, ax])
    sgn = 1.0 if pommel_low else -1.0
    v = np.c_[pos[:, others[0]] - c0, along, pos[:, others[1]] - c1] * SCALE
    n = np.c_[nor[:, others[0]], sgn * nor[:, ax], nor[:, others[1]]]
    # Whatever axis shuffling happened, wind triangles so cross(b-a, c-a) follows the vertex
    # normals - the convention of the game's own sword meshes (checked against SDN0024); the
    # opposite order draws the inside faces (see-through look).
    a, b, c = v[idx[:, 0]], v[idx[:, 1]], v[idx[:, 2]]
    agree = ((np.cross(b - a, c - a) * (n[idx[:, 0]] + n[idx[:, 1]] + n[idx[:, 2]])).sum(1) > 0).mean()
    if agree < 0.5:
        idx = idx[:, [0, 2, 1]]
    return v, n, uv, idx, png


def fmt_m(m):
    return ','.join('%.6f' % x for x in m) + ';;'


def write_x(path, chain, sdn_m, inner_m, v, n, uv, idx, texture):
    L = ['xof 0303txt 0032', '']
    ind = ''
    for f in chain:   # Scene_Root ... Bip01_R_Hand, gum
        L.append(f'{ind}Frame {f.name} {{')
        L.append(f'{ind} FrameTransformMatrix {{ {fmt_m(matrix(f))} }}')
        ind += ' '
    L.append(f'{ind}Frame {NEW_FRAME} {{')
    L.append(f'{ind} FrameTransformMatrix {{ {fmt_m(sdn_m)} }}')
    L.append(f'{ind} Frame {{')
    L.append(f'{ind}  FrameTransformMatrix {{ {fmt_m(inner_m)} }}')
    L.append(f'{ind}  Mesh {{')
    L.append(f'   {len(v)};')
    L.append(',\n'.join('   %.6f;%.6f;%.6f;' % tuple(p) for p in v) + ';')
    L.append(f'   {len(idx)};')
    L.append(',\n'.join('   3;%d,%d,%d;' % tuple(t) for t in idx) + ';')
    L.append('   MeshNormals {')
    L.append(f'    {len(n)};')
    L.append(',\n'.join('    %.6f;%.6f;%.6f;' % tuple(p) for p in n) + ';')
    L.append(f'    {len(idx)};')
    L.append(',\n'.join('    3;%d,%d,%d;' % tuple(t) for t in idx) + ';')
    L.append('   }')
    L.append('   MeshTextureCoords {')
    L.append(f'    {len(uv)};')
    L.append(',\n'.join('    %.6f;%.6f;' % tuple(p) for p in uv) + ';')
    L.append('   }')
    L.append('   MeshMaterialList {')
    L.append(f'    1;\n    {len(idx)};')
    L.append(',\n'.join('    0' for _ in idx) + ';')
    L.append('    Material {')
    L.append('     1.000000;1.000000;1.000000;1.000000;;')
    L.append('     0.000000;')
    L.append('     1.000000;1.000000;1.000000;;')
    L.append('     0.000000;0.000000;0.000000;;')
    L.append(f'     TextureFilename {{ "{texture}"; }}')
    L.append('    }')
    L.append('   }')
    L.append(f'{ind}  }}')
    L.append(f'{ind} }}')
    L.append(f'{ind}}}')
    for _ in chain:
        ind = ind[:-1]
        L.append(f'{ind}}}')
    open(path, 'w', newline='\r\n').write('\n'.join(L) + '\n')


def swap_same_length(data, pairs):
    for a, b in pairs:
        assert len(a) == len(b), (a, b)
        assert a in data, a
        data = data.replace(a, b)
    return data


# Same-length names everywhere (the piece files are patched in place): mesh file s_?_gum.X (9),
# frame SDN0024 (7), texture Sword_03.dds (12) / Sword_03_s.dds (14).
VARIANTS = [
    # (glb, scale, frame, mesh tag, texture stem, texture px)
    (f'{REPO}/assets/custom-items/samehada.glb', 1.0, 'SDN9024', 'shd', 'samehada', 512),            # Samehada
    (f'{REPO}/assets/custom-items/samehada_unleashed.glb', 1.3, 'SDN9025', 'sh2', 'samehad2', 1024), # Samehada Unleashed
]


# Both Samehadas share one scale colour: a muted slate-indigo halfway between the bandaged
# model's blue-grey (hue 240, sat 0.19) and the unleashed model's purple (hue 264, sat 0.54).
SCALE_HSV = (252 / 360, 0.36, 0.41)


def recolor_scales(png_path):
    """Move the blue/purple scale pixels to SCALE_HSV, keeping their light/dark variation; the
    white bandages and teeth and the gold handle (other hues / no saturation) stay."""
    from PIL import Image
    img = Image.open(png_path).convert('RGB')
    hsv = np.asarray(img.convert('HSV'), np.float32) / 255.0
    h, s, v = hsv[..., 0], hsv[..., 1], hsv[..., 2]
    scales = (h > 0.55) & (h < 0.83) & (s > 0.08)
    if not scales.any():
        return
    ms, mv = np.median(s[scales]), np.median(v[scales])
    h = np.where(scales, SCALE_HSV[0], h)
    s = np.where(scales, np.clip(s * SCALE_HSV[1] / ms, 0, 1), s)
    v = np.where(scales, np.clip(v * SCALE_HSV[2] / mv, 0, 1), v)
    out = np.stack([h, s, v], -1)
    Image.fromarray((out * 255).round().astype(np.uint8), 'HSV').convert('RGB').save(png_path)


def build(glb, scale, frame, tag, tex, px):
    global MODEL, SCALE, NEW_FRAME
    MODEL, SCALE, NEW_FRAME = os.path.expanduser(glb), scale, frame
    v, n, uv, idx, png = samehada_geometry()
    print(frame, 'bbox', v.min(0).round(2), v.max(0).round(2), len(v), 'verts', len(idx), 'tris')
    for sex in ('m', 'w'):
        tree = parse(f'{CLIENT}/data/skin/s_{sex}_gum.X')
        path = find_path(tree, 'SDN0024')
        sdn = path[-1]
        inner = [k for k in sdn.kids if k.typ == 'Frame'][0]
        write_x(f'{OUT}/s_{sex}_{tag}.X', path[:-1], matrix(sdn), matrix(inner), v, n, uv, idx, f'{tex}.dds')

        src = subprocess.run(['unzip', '-p', f'{CLIENT}/data/skinobject/skinobject.rcc', f'SDN0024_{sex.upper()}.cps'],
                             capture_output=True, check=True).stdout
        cps = swap_same_length(src, [
            (f's_{sex}_gum.X'.encode(), f's_{sex}_{tag}.X'.encode()),
            (b'SDN0024[Mesh]', f'{frame}[Mesh]'.encode()),
            (b'Sword_03_s.dds', f'{tex}_s.dds'.encode()),
            (b'Sword_03.dds', f'{tex}.dds'.encode()),
        ])
        open(f'{OUT}/{frame}_{sex.upper()}.cps', 'wb').write(cps)

    open(f'{OUT}/{tex}.png', 'wb').write(png)
    recolor_scales(f'{OUT}/{tex}.png')
    size = f'{px}x{px}'
    mips = str(int(np.log2(px)))
    subprocess.run(['magick', f'{OUT}/{tex}.png', '-resize', size, '-define', 'dds:compression=dxt1',
                    '-define', f'dds:mipmaps={mips}', f'{OUT}/{tex}.dds'], check=True)
    # Specular map: the game reads the shine mask from ALPHA (Sword_03_s.dds is DXT5, alpha mean
    # 0.24). Without alpha (DXT1) the whole blade shines at full strength -> uniform gold glow.
    subprocess.run(['magick', f'{OUT}/{tex}.png', '-resize', size, '(', '+clone', '-colorspace', 'gray',
                    '-evaluate', 'multiply', '0.25', ')', '-alpha', 'off', '-compose', 'CopyOpacity', '-composite',
                    '-define', 'dds:compression=dxt5', '-define', f'dds:mipmaps={mips}', f'{OUT}/{tex}_s.dds'], check=True)


def main():
    for v in VARIANTS:
        build(*v)
    print('wrote', sorted(os.listdir(OUT)))


if __name__ == '__main__':
    main()
