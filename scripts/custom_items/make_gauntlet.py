"""glb model -> RAN brawler gauntlet assets, worn like the stock Supreme gauntlets (NLN0040).

  make_gauntlet.py        (needs: numpy, Pillow, ImageMagick `magick`; reads the stock client
                           for the Biped skeleton and the NLN0040 piece files)

The stock gauntlets are one right-hand mesh skinned to Bip01_R_Forearm. The Infinity Gauntlet glb
is a rigged hand (forearm, hand, five 3-joint fingers, stones), so it is skinned to the matching
Biped bones instead and follows the brawler's fist animations:
  - the glb is posed by its own skin (rest pose), then fitted onto the game's bind skeleton with
    one similarity transform (forearm, hand and the five finger roots as correspondences);
  - every glb joint maps to a Biped bone (JOINT_MAP); weights are summed per bone.

Into port/build/custom_items:
  s_{m,w}_<tag>.X         text .x: the Biped frames + the gauntlet frame + skinned mesh
  <frame>_{M,W}.cps       copies of NLN0040_{M,W}.cps with same-length names swapped
  <tex>.dds / <tex>_s.dds DXT1 diffuse / DXT5 specular (shine mask in alpha)
  <tex>.png               the gold-tinted diffuse (render_icons.py reads it)
"""
import io
import json
import os
import struct
import subprocess
import numpy as np
from PIL import Image
from glb import _local
from xtree import parse, find_path, matrix
from make_samehada import CLIENT, REPO, OUT, fmt_m, swap_same_length

# (glb, skin index, frame, mesh tag, texture stem, texture px, scale around the hand)
# Same-length names (the piece files are patched in place): mesh file s_?_newc.X (10), frame
# NLN0040 (7), texture F_at_05.dds (11) / F_at_05_s.dds (13).
VARIANTS = [
    (f'{REPO}/assets/custom-items/infinity_gauntlet.glb', 1, 'NLN9040', 'inft', 'inf_gnt', 1024, 1.12),
]

# glb joint name prefix -> Biped bone (right hand). Stones and plates ride their parent bone.
JOINT_MAP = {
    'bip_lowerArm_R': 'Bip01_R_Forearm', 'bip_forearm_scale_R': 'Bip01_R_Forearm',
    'bip_hand_R': 'Bip01_R_Hand',
    'bip_power_stone_R': 'Bip01_R_Hand', 'bip_space_stone_R': 'Bip01_R_Hand',
    'bip_reality_stone_R': 'Bip01_R_Hand', 'bip_soul_stone_R': 'Bip01_R_Hand', 'bip_mind_stone_R': 'Bip01_R_Hand',
    'bip_thumb_0_R': 'Bip01_R_Finger0', 'bip_thumb_1_R': 'Bip01_R_Finger01', 'bip_thumb_2_R': 'Bip01_R_Finger02',
    'bip_thumb_plates_R': 'Bip01_R_Finger0', 'bip_time_stone_R': 'Bip01_R_Finger0',
    'bip_index_0_R': 'Bip01_R_Finger1', 'bip_index_1_R': 'Bip01_R_Finger11', 'bip_index_2_R': 'Bip01_R_Finger12',
    'bip_middle_0_R': 'Bip01_R_Finger2', 'bip_middle_1_R': 'Bip01_R_Finger21', 'bip_middle_2_R': 'Bip01_R_Finger22',
    'bip_ring_0_R': 'Bip01_R_Finger3', 'bip_ring_1_R': 'Bip01_R_Finger31', 'bip_ring_2_R': 'Bip01_R_Finger32',
    'bip_pinky_0_R': 'Bip01_R_Finger4', 'bip_pinky_1_R': 'Bip01_R_Finger41', 'bip_pinky_2_R': 'Bip01_R_Finger42',
}
# Correspondences for the fit: glb joint -> Biped bone origin.
FIT = ['bip_lowerArm_R', 'bip_hand_R', 'bip_thumb_0_R', 'bip_index_0_R', 'bip_middle_0_R',
       'bip_ring_0_R', 'bip_pinky_0_R']


def joint_key(name):
    """'bip_index_0_R_34' -> 'bip_index_0_R' (Sketchfab appends the node index)."""
    return name.rsplit('_', 1)[0]


def load_skinned(path, skin):
    """Rest-pose (skinned) positions/normals of every mesh bound to `skin`, plus joint weights and
    the joints' world matrices (glTF, column vectors)."""
    d = open(path, 'rb').read()
    n = struct.unpack('<I', d[12:16])[0]
    j = json.loads(d[20:20 + n])
    b0 = 20 + n
    bin_ = d[b0 + 8:b0 + 8 + struct.unpack('<I', d[b0:b0 + 4])[0]]

    def acc(i):
        a = j['accessors'][i]
        bv = j['bufferViews'][a['bufferView']]
        comp = {5126: 'f', 5125: 'I', 5123: 'H', 5121: 'B'}[a['componentType']]
        k = {'SCALAR': 1, 'VEC2': 2, 'VEC3': 3, 'VEC4': 4, 'MAT4': 16}[a['type']]
        off = bv.get('byteOffset', 0) + a.get('byteOffset', 0)
        stride = bv.get('byteStride', 0) or struct.calcsize(comp) * k
        return np.array([struct.unpack_from('<%d%s' % (k, comp), bin_, off + i * stride) for i in range(a['count'])])

    nodes = j['nodes']
    parent = {c: i for i, nd in enumerate(nodes) for c in nd.get('children', [])}

    def world(i):
        m = _local(nodes[i])
        while i in parent:
            i = parent[i]
            m = _local(nodes[i]) @ m
        return m

    sk = j['skins'][skin]
    ibm = acc(sk['inverseBindMatrices']).reshape(-1, 4, 4).transpose(0, 2, 1)
    jm = np.array([world(jn) @ ibm[k] for k, jn in enumerate(sk['joints'])])
    P, N, U, I, J, W = [], [], [], [], [], []
    base = 0
    for nd in nodes:
        if nd.get('skin') != skin:
            continue
        for p in j['meshes'][nd['mesh']]['primitives']:
            a = p['attributes']
            pos, nor, uv = acc(a['POSITION']), acc(a['NORMAL']), acc(a['TEXCOORD_0'])
            idx = acc(p['indices']).reshape(-1, 3)
            jj = acc(a['JOINTS_0']).astype(int)
            ww = acc(a['WEIGHTS_0'])
            ww = ww / ww.sum(1, keepdims=True)
            m = np.einsum('vk,vkij->vij', ww, jm[jj])
            pw = np.einsum('vij,vj->vi', m, np.c_[pos, np.ones(len(pos))])[:, :3]
            nw = np.einsum('vij,vj->vi', m[:, :3, :3], nor)
            P.append(pw); N.append(nw); U.append(uv); I.append(idx + base); J.append(jj); W.append(ww)
            base += len(pos)
    names = [joint_key(nodes[jn]['name']) for jn in sk['joints']]
    joints = {names[k]: world(jn)[:3, 3] for k, jn in enumerate(sk['joints'])}
    mat = j['materials'][j['meshes'][next(nd['mesh'] for nd in nodes if nd.get('skin') == skin)]['primitives'][0]['material']]
    img = j['images'][j['textures'][mat['pbrMetallicRoughness']['baseColorTexture']['index']]['source']]
    bv = j['bufferViews'][img['bufferView']]
    png = bin_[bv.get('byteOffset', 0):bv.get('byteOffset', 0) + bv['byteLength']]
    return np.vstack(P), np.vstack(N), np.vstack(U), np.vstack(I), np.vstack(J), np.vstack(W), names, joints, png


def bone_world(tree, name):
    """Bind-pose world matrix of a frame (D3D row vectors: local * parent)."""
    m = np.eye(4)
    for f in find_path(tree, name):
        m = np.array(matrix(f)).reshape(4, 4) @ m
    return m


def similarity(src, dst):
    """Umeyama: s, R, t with dst ~ s * R @ src + t. Reflections allowed (glTF is right-handed,
    the game's .x data left-handed)."""
    ms, md = src.mean(0), dst.mean(0)
    a, b = src - ms, dst - md
    u, sv, vt = np.linalg.svd(b.T @ a / len(src))
    r = u @ vt
    s = sv.sum() / (a ** 2).sum() * len(src)
    return s, r, md - s * r @ ms


def gauntlet_mesh(glb, skin, tree, scale):
    """Gauntlet vertices in the game's bind world, normals, uv, faces, and per-bone weights."""
    pos, nor, uv, idx, jj, ww, names, joints, png = load_skinned(glb, skin)
    src = np.array([joints[k] for k in FIT])
    dst = np.array([bone_world(tree, JOINT_MAP[k])[3, :3] for k in FIT])
    s, r, t = similarity(src, dst)
    fit = np.linalg.norm((s * src @ r.T + t) - dst, axis=1)
    print('  fit scale %.3f det %+.0f, residual per joint' % (s, np.linalg.det(r)), fit.round(2))
    hand = dst[1]
    v = s * pos @ r.T + t
    v = hand + (v - hand) * scale            # a little roomier than the hand so it covers it
    n = nor @ r.T
    n /= np.linalg.norm(n, axis=1, keepdims=True)
    # Wind triangles so cross(b-a, c-a) follows the vertex normals, like the game's own meshes.
    a, b, c = v[idx[:, 0]], v[idx[:, 1]], v[idx[:, 2]]
    if ((np.cross(b - a, c - a) * (n[idx[:, 0]] + n[idx[:, 1]] + n[idx[:, 2]])).sum(1) > 0).mean() < 0.5:
        idx = idx[:, [0, 2, 1]]
    bones = {}
    for k in range(4):
        for vi in np.nonzero(ww[:, k] > 1e-4)[0]:
            bone = JOINT_MAP[names[jj[vi, k]]]
            w = bones.setdefault(bone, {})
            w[vi] = w.get(vi, 0.0) + ww[vi, k]
    return v, n, uv, idx, bones, png


def write_frames(L, frame, ind):
    """Frame hierarchy (matrices only, no meshes) of the stock skeleton."""
    L.append(f'{ind}Frame {frame.name} {{')
    L.append(f'{ind} FrameTransformMatrix {{ {fmt_m(matrix(frame))} }}')
    for k in frame.kids:
        if k.typ == 'Frame' and k.name:
            write_frames(L, k, ind + ' ')
    L.append(f'{ind}}}')


def write_x(path, tree, frame, v, n, uv, idx, bones, texture):
    root = next(k for k in tree.kids if k.name == 'Scene_Root')
    m_root = np.array(matrix(root)).reshape(4, 4)
    to_mesh = np.linalg.inv(m_root)             # mesh frames below Scene_Root are identity
    vm = (np.c_[v, np.ones(len(v))] @ to_mesh)[:, :3]
    nm = n @ to_mesh[:3, :3]
    nm /= np.linalg.norm(nm, axis=1, keepdims=True)
    ident = fmt_m(np.eye(4).ravel())
    L = ['xof 0303txt 0032', '', 'Frame Scene_Root {', f' FrameTransformMatrix {{ {fmt_m(matrix(root))} }}']
    write_frames(L, next(k for k in root.kids if k.name == 'Bip01'), ' ')
    L += [f' Frame {frame} {{', f'  FrameTransformMatrix {{ {ident} }}', '  Frame {',
          f'   FrameTransformMatrix {{ {ident} }}', '   Mesh {']
    L.append(f'    {len(vm)};')
    L.append(',\n'.join('    %.6f;%.6f;%.6f;' % tuple(p) for p in vm) + ';')
    L.append(f'    {len(idx)};')
    L.append(',\n'.join('    3;%d,%d,%d;' % tuple(t) for t in idx) + ';')
    L.append('    MeshNormals {')
    L.append(f'     {len(nm)};')
    L.append(',\n'.join('     %.6f;%.6f;%.6f;' % tuple(p) for p in nm) + ';')
    L.append(f'     {len(idx)};')
    L.append(',\n'.join('     3;%d,%d,%d;' % tuple(t) for t in idx) + ';')
    L.append('    }')
    L.append('    MeshTextureCoords {')
    L.append(f'     {len(uv)};')
    L.append(',\n'.join('     %.6f;%.6f;' % tuple(p) for p in uv) + ';')
    L.append('    }')
    L.append('    MeshMaterialList {')
    L.append(f'     1;\n     {len(idx)};')
    L.append(',\n'.join('     0' for _ in idx) + ';')
    L.append('     Material {')
    L.append('      1.000000;1.000000;1.000000;1.000000;;')
    L.append('      0.000000;')
    L.append('      1.000000;1.000000;1.000000;;')
    L.append('      0.000000;0.000000;0.000000;;')
    L.append(f'      TextureFilename {{ "{texture}"; }}')
    L.append('     }')
    L.append('    }')
    per_vertex = np.zeros(len(vm), int)
    for w in bones.values():
        per_vertex[list(w)] += 1
    L.append(f'    XSkinMeshHeader {{ {per_vertex.max()}; {min(3 * per_vertex.max(), 12)}; {len(bones)}; }}')
    for bone, w in bones.items():
        ids = sorted(w)
        # mesh space -> bone space: back to world (Scene_Root), then the bone's inverse bind
        offset = m_root @ np.linalg.inv(bone_world(tree, bone))
        L.append('    SkinWeights {')
        L.append(f'     "{bone}";')
        L.append(f'     {len(ids)};')
        L.append('     ' + ','.join(str(i) for i in ids) + ';')
        L.append('     ' + ','.join('%.6f' % w[i] for i in ids) + ';')
        L.append(f'     {fmt_m(offset.ravel())}')
        L.append('    }')
    L += ['   }', '  }', ' }', '}']
    open(path, 'w', newline='\r\n').write('\n'.join(L) + '\n')


# Thanos gold: the model's metal is an untinted pinkish brown. Its low-saturation pixels move to
# this hue/saturation (keeping their light/dark variation); the six saturated stones stay.
GOLD_HSV = (40 / 360, 0.78, 0.60)


def gold_tint(png_path):
    img = Image.open(png_path).convert('RGB')
    hsv = np.asarray(img.convert('HSV'), np.float32) / 255.0
    h, s, v = hsv[..., 0], hsv[..., 1], hsv[..., 2]
    # pinkish-brown metal (red hues) and the near-grey crevices; the pale yellow Mind Stone is
    # a yellow hue and stays
    metal = ((s < 0.45) & ((h < 0.09) | (h > 0.9))) | (s < 0.08)
    mv = np.median(v[metal])
    h = np.where(metal, GOLD_HSV[0], h)
    s = np.where(metal, np.clip(GOLD_HSV[1] * (0.6 + 0.4 * v / mv), 0, 1), s)
    v = np.where(metal, np.clip(v * GOLD_HSV[2] / mv, 0, 1), v)
    out = np.stack([h, s, v], -1)
    Image.fromarray((out * 255).round().astype(np.uint8), 'HSV').convert('RGB').save(png_path)


def build(glb, skin, frame, tag, tex, px, scale):
    glb = os.path.expanduser(glb)
    for sex in ('m', 'w'):
        tree = parse(f'{CLIENT}/data/skin/s_{sex}_newc.x')
        print(frame, sex)
        v, n, uv, idx, bones, png = gauntlet_mesh(glb, skin, tree, scale)
        print('  bbox', v.min(0).round(2), v.max(0).round(2), len(v), 'verts', len(idx), 'tris', len(bones), 'bones')
        write_x(f'{OUT}/s_{sex}_{tag}.X', tree, frame, v, n, uv, idx, bones, f'{tex}.dds')

        src = subprocess.run(['unzip', '-p', f'{CLIENT}/data/skinobject/skinobject.rcc', f'NLN0040_{sex.upper()}.cps'],
                             capture_output=True, check=True).stdout
        cps = swap_same_length(src, [
            (f's_{sex}_newc.X'.encode(), f's_{sex}_{tag}.X'.encode()),
            (b'NLN0040[Mesh]', f'{frame}[Mesh]'.encode()),
            (b'F_at_05_s.dds', f'{tex}_s.dds'.encode()),
            (b'F_at_05.dds', f'{tex}.dds'.encode()),
        ])
        open(f'{OUT}/{frame}_{sex.upper()}.cps', 'wb').write(cps)

    Image.open(io.BytesIO(png)).convert('RGB').save(f'{OUT}/{tex}.png')
    gold_tint(f'{OUT}/{tex}.png')
    size = f'{px}x{px}'
    mips = str(int(np.log2(px)))
    subprocess.run(['magick', f'{OUT}/{tex}.png', '-resize', size, '-define', 'dds:compression=dxt1',
                    '-define', f'dds:mipmaps={mips}', f'{OUT}/{tex}.dds'], check=True)
    # Specular: shine mask in ALPHA (see make_samehada.py). Polished gold shines more than the
    # sword steel: 0.45 of the brightness.
    subprocess.run(['magick', f'{OUT}/{tex}.png', '-resize', size, '(', '+clone', '-colorspace', 'gray',
                    '-evaluate', 'multiply', '0.45', ')', '-alpha', 'off', '-compose', 'CopyOpacity', '-composite',
                    '-define', 'dds:compression=dxt5', '-define', f'dds:mipmaps={mips}', f'{OUT}/{tex}_s.dds'], check=True)


def main():
    for v in VARIANTS:
        build(*v)
    print('wrote', sorted(os.listdir(OUT)))


if __name__ == '__main__':
    main()
