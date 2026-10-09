"""glb model -> the full RAN asset set of one crow (monster / NPC).

  make_mob.py <mob> [<mob> ...]      mob = a key of mobs.json  (needs numpy + ImageMagick `magick`)

Per mob <tag>, into port/build/custom_mobs/mobs/<tag>/ (laid out like a client folder):
  data/skeleton/b_<tag>.x            text .x: bone frames only (DxSkeleton::LoadFile)
  data/skin/s_<tag>.x                text .x: bone frames + one skinned mesh (DxSkinMesh9)
  data/animation/a_<tag>_<role>.bin  AnimContainer 0x100: per bone 30 fps local matrix keys
  textures/mob/<tag>_<n>.dds         base color, DXT1 (DXT5 when the material has alpha)
  rcc/skinobject/<tag>.chf, <tag>.cps     -> go into data/skinobject/skinobject.rcc
  rcc/animation/a_<tag>_<role>.cfg        -> go into data/animation/animation.rcc
  crow.json                           what build_mobs.py needs for the crow.mnsf record

Coordinates: glTF is right-handed, Y up, +Z forward, column vectors. The game is left-handed,
Y up, row vectors, and a crow faces -Z in model space (GLCrowClient m_vDirOrig = (0,0,-1)).
Mirroring Z (S = diag(1,1,-1)) does both: handedness and facing. Every world matrix W of the
glTF becomes W' = T W M^-1 with M = s*S (uniform scale to game units, optional yaw) and T = M +
ground offset (feet on y = 0, centred), so bone locals stay proper rotations (det > 0; the engine
turns key matrices into quaternions) and a point p becomes T p. Child locals are M L M^-1;
triangles are re-wound per primitive to follow their normals (make_gauntlet.py convention).

Static models (no skin): one bone '<tag>_root' with every vertex weight 1, one 2-key idle anim.
Skinned models: the skin's joints become the bones (parent = nearest ancestor joint); mesh nodes
without a skin under a joint (weapons, armour) become bones of their own. Skinning is exact for
any rest/bind pose: the stored mesh is v' = T B v_mesh (B = rest skinning matrix of the most
weighted joint, so it stands upright) and each offset is M IBM_j B^-1 T^-1. Bones that move no
vertex are pruned. Clips are sampled at 30 fps into per-bone local matrices.
"""
import json
import os
import re
import shutil
import subprocess
import sys

import numpy as np

HERE = os.path.dirname(os.path.abspath(__file__))
sys.path.insert(0, HERE)
import mobfmt as F  # noqa: E402
from gltf import GLTF  # noqa: E402
from mobcfg import MODELS, OUT, load_config, mob_keys  # noqa: E402

FPS = 30
MAX_TEX = 1024

# role -> (EMANI_MAINTYPE, loop)
ROLES = {'idle': (F.AN_GUARD_N, True), 'walk': (F.AN_WALK, True), 'run': (F.AN_RUN, True),
         'attack': (F.AN_ATTACK, False), 'shock': (F.AN_SHOCK, False), 'die': (F.AN_DIE, False)}


def safe_name(name, used):
    n = re.sub(r'[^A-Za-z0-9_]', '_', name or 'bone')
    if not re.match(r'[A-Za-z_]', n):
        n = 'b_' + n
    base, k = n, 1
    while n.lower() in used:
        k += 1
        n = f'{base}_{k}'
    used.add(n.lower())
    return n


def to_row(m):
    """column-vector 4x4 -> D3D row-vector 4x4 (what .x / .bin store)."""
    return np.asarray(m).T


class Mob:
    def __init__(self, key, cfg, models):
        self.key, self.cfg = key, cfg
        self.tag = cfg['tag']
        self.g = GLTF(os.path.join(models, cfg['glb']))
        self.out = os.path.join(OUT, 'mobs', self.tag)
        self.static = not self.g.j.get('skins')
        self.S = np.diag([1.0, 1.0, -1.0, 1.0])

    # ---- geometry -------------------------------------------------------------------------
    def bones_setup(self):
        g = self.g
        used = set()
        if self.static:
            self.joints = []
            self.bone = {None: safe_name(self.tag + '_root', used)}
            self.parent = {self.bone[None]: None}
            return
        skin = g.j['skins'][self.cfg.get('skin', 0)]
        self.skin = skin
        self.joints = list(skin['joints'])
        jset = set(self.joints)
        self.bone = {j: safe_name(g.nodes[j].get('name', f'joint{j}'), used) for j in self.joints}
        self.parent = {}
        for j in self.joints:
            p = g.parent.get(j)
            while p is not None and p not in jset:
                p = g.parent.get(p)
            self.parent[self.bone[j]] = self.bone[p] if p is not None else None
        # mesh nodes without a skin under a joint (weapons, armour plates): each becomes a bone of
        # its own under the nearest bone above it, so it keeps its exact (animated) node transform
        self.rigid = set()
        for ni in g.scene_nodes():
            nd = g.nodes[ni]
            if 'mesh' in nd and 'skin' not in nd and self.nearest_bone(g.parent.get(ni)) is not None:
                self.rigid.add(ni)
        for ni in sorted(self.rigid, key=self.node_depth):
            self.bone[ni] = safe_name(g.nodes[ni].get('name', f'node{ni}'), used)
            self.parent[self.bone[ni]] = self.bone[self.nearest_bone(g.parent.get(ni))]
        self.bone_node = {b: j for j, b in self.bone.items()}
        self.B = self.bind_frame()

    def nearest_bone(self, i):
        while i is not None and i not in self.bone:
            i = self.g.parent.get(i)
        return i

    def node_depth(self, i):
        d = 0
        while i in self.g.parent:
            i = self.g.parent[i]
            d += 1
        return d

    def ibm(self):
        if self.static:
            return None
        if 'inverseBindMatrices' in self.skin:
            return self.g.acc(self.skin['inverseBindMatrices']).reshape(-1, 4, 4).transpose(0, 2, 1)
        return np.tile(np.eye(4), (len(self.joints), 1, 1))

    def bind_frame(self):
        """B: maps bind-space (mesh-space) vertices into the glTF world for the mesh stored in the
        .x file. Any B is exact (the offsets cancel it out); B = the rest-pose skinning matrix
        W_j * IBM_j of the joint carrying the most weight stands the stored mesh upright in its
        rest pose (exactly the rest pose when rest == bind)."""
        g = self.g
        tot = np.zeros(len(self.joints))
        for ni in g.scene_nodes():
            nd = g.nodes[ni]
            if 'mesh' in nd and nd.get('skin') == self.cfg.get('skin', 0):
                for p in g.j['meshes'][nd['mesh']]['primitives']:
                    if 'JOINTS_0' in p['attributes']:
                        np.add.at(tot, g.acc(p['attributes']['JOINTS_0']).astype(int).ravel(),
                                  g.acc(p['attributes']['WEIGHTS_0']).ravel())
        k = int(np.argmax(tot))
        W = g.worlds(g.locals_at())
        return W[self.joints[k]] @ self.ibm()[k]

    def gather_mesh(self, loc):
        """The mesh, glTF space. Returns dict: pw = positions posed at `loc` (world), pb / nb =
        positions / normals in the bind frame B (static models: rest world), uv, faces, material
        per face, per primitive face ranges, and per vertex up to 4 (bone name, weight)."""
        g = self.g
        W = g.worlds(loc)
        Wr = g.worlds(g.locals_at())
        P, PB, NB, U, I, Mt, J, Wt, ranges = [], [], [], [], [], [], [], [], []
        base = 0
        nface = 0
        if not self.static:
            ibm = self.ibm()
            skinmat = np.array([W[j] @ ibm[k] for k, j in enumerate(self.joints)])
            B = self.B
        mats = []
        for ni in g.scene_nodes():
            nd = g.nodes[ni]
            if 'mesh' not in nd:
                continue
            skinned = 'skin' in nd and not self.static
            if skinned and nd['skin'] != self.cfg.get('skin', 0):
                continue
            if not skinned and not self.static and ni not in self.rigid:
                print(f'  skipping mesh node {nd.get("name")!r}: not under a joint')
                continue
            for p in g.j['meshes'][nd['mesh']]['primitives']:
                if p.get('mode', 4) != 4:
                    continue
                a = p['attributes']
                pos = g.acc(a['POSITION'])
                nor = g.acc(a['NORMAL']) if 'NORMAL' in a else None
                uv = g.acc(a['TEXCOORD_0']) if 'TEXCOORD_0' in a else np.zeros((len(pos), 2))
                idx = g.acc(p['indices']).astype(int).reshape(-1, 3) if 'indices' in p else \
                    np.arange(len(pos)).reshape(-1, 3)
                if nor is None:
                    nor = np.zeros_like(pos)
                    tri = pos[idx]
                    fn = np.cross(tri[:, 1] - tri[:, 0], tri[:, 2] - tri[:, 0])
                    for k in range(3):
                        np.add.at(nor, idx[:, k], fn)
                    nor /= np.maximum(np.linalg.norm(nor, axis=1, keepdims=True), 1e-12)
                ph = np.c_[pos, np.ones(len(pos))]
                if skinned:
                    jj = g.acc(a['JOINTS_0']).astype(int)
                    ww = g.acc(a['WEIGHTS_0'])
                    ww = np.where(ww < 1e-4, 0, ww)
                    ww = ww / ww.sum(1, keepdims=True)
                    m = np.einsum('vk,vkij->vij', ww, skinmat[jj])
                    pw = np.einsum('vij,vj->vi', m, ph)[:, :3]
                    bind = B                                # vertex in B frame = B * v_mesh
                    names = [[self.bone[self.joints[k]] for k in row] for row in jj]
                else:
                    pw = (ph @ W[ni].T)[:, :3]
                    bind = Wr[ni]                           # rest world; its bone is the node
                    bname = self.bone[None] if self.static else self.bone[ni]
                    names = [[bname] * 4 for _ in range(len(pos))]
                    ww = np.tile([1.0, 0, 0, 0], (len(pos), 1))
                pb = (ph @ bind.T)[:, :3]
                nb = nor @ np.linalg.inv(bind[:3, :3])       # inverse transpose (row vectors)
                nb /= np.maximum(np.linalg.norm(nb, axis=1, keepdims=True), 1e-12)
                mi = p.get('material')
                if mi not in mats:
                    mats.append(mi)
                P.append(pw); PB.append(pb); NB.append(nb); U.append(uv[:, :2]); I.append(idx + base)
                Mt.append(np.full(len(idx), mats.index(mi))); J += names; Wt.append(ww)
                ranges.append((nface, nface + len(idx)))
                base += len(pos)
                nface += len(idx)
        self.materials = mats
        return dict(pw=np.vstack(P), pb=np.vstack(PB), nb=np.vstack(NB), uv=np.vstack(U), idx=np.vstack(I),
                    mat=np.concatenate(Mt), names=J, w=np.vstack(Wt), ranges=ranges)

    def pose_rest_skin_check(self):
        """How far the rest pose (node TRS) is from the bind pose (only informative: the offsets
        are built from the inverse bind matrices, so either way the skinning is exact)."""
        if self.static or 'inverseBindMatrices' not in self.skin:
            return 0.0
        W = self.g.worlds(self.g.locals_at())
        sk = np.array([W[j] @ m for j, m in zip(self.joints, self.ibm())])
        return float(np.abs(sk - sk[0]).max() / max(np.abs(sk[0]).max(), 1e-9))

    def offset(self, bone):
        """Game offset matrix (column form) of a bone: mesh space -> bone space, so that
        W'_j(t) * Off_j * v' == T * W_j(t) * IBM_j * v_mesh exactly (W' = T W M^-1, v' = T B v_mesh):
        Off_j = M * IBM_j * B^-1 * T^-1. Rigid node bones: the inverse of their rest world
        (v' = T W_n(rest) v). Static: identity."""
        if self.static:
            return np.eye(4)
        node = self.bone_node[bone]
        if node in self.rigid:
            return np.linalg.inv(self.T @ self.g.worlds(self.g.locals_at())[node] @ self.Minv)
        k = self.joints.index(node)
        return self.M @ self.ibm()[k] @ np.linalg.inv(self.B) @ np.linalg.inv(self.T)

    def setup_transform(self):
        """M = s*S and the ground offset, from the idle pose (or rest): feet on y=0, centred."""
        cfg = self.cfg
        clips = cfg.get('clips', {})
        loc = self.g.locals_at(self.g.anim_index(clips['idle']), self.g.anim_range(self.g.anim_index(clips['idle']))[0]) \
            if clips.get('idle') else self.g.locals_at()
        pos = self.gather_mesh(loc)['pw']
        pg = pos * [1, 1, -1]
        lo, hi = pg.min(0), pg.max(0)
        h = hi[1] - lo[1]
        self.s = cfg['height'] / h
        self.M = self.S * np.array([self.s, self.s, self.s, 1.0])
        c = (lo + hi) / 2 * self.s
        self.T = self.M.copy()
        self.T[:3, 3] = [-c[0], -lo[1] * self.s, -c[2]]
        if cfg.get('yaw'):          # extra turn (degrees, about +Y) if the model does not face +Z
            a = np.radians(cfg['yaw'])
            R = np.eye(4)
            R[[0, 0, 2, 2], [0, 2, 0, 2]] = [np.cos(a), np.sin(a), -np.sin(a), np.cos(a)]
            self.T = R @ self.T
            self.M = R @ self.M
        self.Minv = np.linalg.inv(self.M)
        self.idle_v = (np.c_[pos, np.ones(len(pos))] @ self.T.T)[:, :3]

    def game_worlds(self, loc):
        """{bone: game world 4x4 (column form)} for glTF locals `loc`."""
        if self.static:
            return {self.bone[None]: np.eye(4)}
        W = self.g.worlds(loc)
        return {b: self.T @ W[n] @ self.Minv for n, b in self.bone.items()}

    def game_locals(self, gw):
        out = {}
        for b, p in self.parent.items():
            out[b] = gw[b] if p is None else np.linalg.inv(gw[p]) @ gw[b]
        return out

    # ---- outputs --------------------------------------------------------------------------
    def build(self):
        cfg, tag = self.cfg, self.tag
        shutil.rmtree(self.out, ignore_errors=True)
        for d in ('data/skeleton', 'data/skin', 'data/animation', 'textures/mob', 'rcc/skinobject', 'rcc/animation'):
            os.makedirs(os.path.join(self.out, d), exist_ok=True)
        self.bones_setup()
        self.setup_transform()
        dev = self.pose_rest_skin_check()
        print(f'{self.key}: {"static" if self.static else f"{len(self.joints)} joints"}, scale {self.s:.4g}, '
              f'rest-vs-bind skin matrix spread {dev:.2g}')

        rest = self.g.locals_at()
        m = self.gather_mesh(rest)
        uv, idx, mat_ids, jn, ww = m['uv'], m['idx'].copy(), m['mat'], m['names'], m['w']
        v = (np.c_[m['pb'], np.ones(len(m['pb']))] @ self.T.T)[:, :3]
        n = m['nb'] @ np.linalg.inv(self.M[:3, :3])
        n /= np.maximum(np.linalg.norm(n, axis=1, keepdims=True), 1e-12)
        # wind each primitive so cross(b-a, c-a) follows its normals (game convention)
        a, b, c = v[idx[:, 0]], v[idx[:, 1]], v[idx[:, 2]]
        good = (np.cross(b - a, c - a) * (n[idx[:, 0]] + n[idx[:, 1]] + n[idx[:, 2]])).sum(1) > 0
        for f0, f1 in m['ranges']:
            if good[f0:f1].mean() < 0.5:
                idx[f0:f1] = idx[f0:f1][:, [0, 2, 1]]

        gw_rest = self.game_worlds(rest)
        local_rest = {b: to_row(m) for b, m in self.game_locals(gw_rest).items()}
        weights = {}
        for vi in range(len(v)):
            for k in range(4):
                if ww[vi, k] > 0:
                    w = weights.setdefault(jn[vi][k], {})
                    w[vi] = w.get(vi, 0.0) + float(ww[vi, k])
        # renormalise after merging duplicate bones (weights printed to 6 decimals)
        tot = np.zeros(len(v))
        for w in weights.values():
            for vi, x in w.items():
                tot[vi] += x
        for w in weights.values():
            for vi in w:
                w[vi] /= tot[vi]
        offsets = {bn: to_row(self.offset(bn)) for bn in weights}
        # drop bones that move nothing (no weights, nothing weighted below them): fewer tracks
        keep = set()
        for bn in weights:
            while bn and bn not in keep:
                keep.add(bn)
                bn = self.parent[bn]
        if len(keep) < len(self.parent):
            print(f'  pruned {len(self.parent) - len(keep)} unused bones, {len(keep)} left')
            self.parent = {b: p for b, p in self.parent.items() if b in keep}
            local_rest = {b: m for b, m in local_rest.items() if b in keep}
        kids = {}
        roots = []
        for bn, p in self.parent.items():
            (kids.setdefault(p, []) if p else roots).append(bn)

        textures = self.write_textures()
        skel = f'b_{tag}.x'
        skin = f's_{tag}.x'
        open(os.path.join(self.out, 'data/skeleton', skel), 'w', newline='').write(
            F.write_skeleton_x(roots, kids, local_rest))
        mesh_frame = f'{tag}_mesh'
        open(os.path.join(self.out, 'data/skin', skin), 'w', newline='').write(
            F.write_skin_x(roots, kids, local_rest, mesh_frame, v, n, uv, idx, mat_ids, textures, weights, offsets))
        bb_lo, bb_hi = self.idle_v.min(0), self.idle_v.max(0)       # the pose players see
        print(f'  {len(v)} verts, {len(idx)} tris, {len(weights)} weighted bones, {len(textures)} materials, '
              f'idle bbox {bb_lo.round(2)} .. {bb_hi.round(2)}')

        anims = self.write_anims(skel, gw_rest)
        scale = cfg.get('chf_scale', 1.0)
        rad = float(max(np.abs(bb_lo[[0, 2]]).max(), np.abs(bb_hi[[0, 2]]).max())) * scale
        hgt = float(bb_hi[1]) * scale
        chf = F.write_chf({0: f'{tag}.cps'}, skel, [a['anim'] for a in anims],
                          (rad, hgt, rad), (-rad, 0.0, -rad), scale=scale,
                          world_obj=cfg.get('world_obj', False), height=hgt, radius=rad)
        open(os.path.join(self.out, 'rcc/skinobject', f'{tag}.chf'), 'wb').write(chf)
        cps = F.write_cps(skin, skel, f'{mesh_frame}[Mesh]', textures)
        open(os.path.join(self.out, 'rcc/skinobject', f'{tag}.cps'), 'wb').write(cps)
        json.dump(dict(key=self.key, tag=tag, chf=f'{tag}.chf', anims=anims, height=hgt, radius=rad,
                       verts=len(v), tris=len(idx), bones=len(self.parent), textures=textures),
                  open(os.path.join(self.out, 'crow.json'), 'w'), indent=1)
        self.preview(self.idle_v, idx)

    def write_textures(self):
        names = []
        for k, mi in enumerate(self.materials):
            img, factor, alpha = self.g.material_image(mi)
            name = f'{self.tag}_{k}.dds'
            dst = os.path.join(self.out, 'textures/mob', name)
            comp = 'dxt5' if alpha in ('BLEND', 'MASK') else 'dxt1'
            if img is None:
                rgb = 'rgb(%d,%d,%d)' % tuple(int(round(255 * min(max(x, 0), 1))) for x in factor[:3])
                cmd = ['magick', '-size', '8x8', f'xc:{rgb}', '-define', f'dds:compression={comp}',
                       '-define', 'dds:mipmaps=3', dst]
                subprocess.run(cmd, check=True)
            else:
                src = os.path.join(self.out, f'{self.tag}_{k}.src')
                open(src, 'wb').write(img)
                w, h = (int(x) for x in subprocess.run(['magick', 'identify', '-format', '%w %h', src + '[0]'],
                                                       capture_output=True, check=True, text=True).stdout.split())
                px = min(MAX_TEX, 1 << int(np.ceil(np.log2(max(w, h)))))
                cmd = ['magick', src + '[0]', '-resize', f'{px}x{px}!']
                if tuple(factor[:3]) != (1, 1, 1):      # baseColorFactor tints the texture
                    cmd += ['-color-matrix', '%f 0 0 0 %f 0 0 0 %f' % tuple(factor[:3])]
                if comp == 'dxt1':
                    cmd += ['-alpha', 'off']
                cmd += ['-define', f'dds:compression={comp}', '-define', f'dds:mipmaps={int(np.log2(px))}', dst]
                subprocess.run(cmd, check=True)
                os.remove(src)
            names.append(name)
        return names

    def write_anims(self, skel, gw_rest):
        cfg, tag = self.cfg, self.tag
        out = []
        clips = cfg.get('clips', {}) if not self.static else {}
        roles = [r for r in ROLES if clips.get(r)] if clips else ['idle']
        for role in roles:
            main, loop = ROLES[role]
            name = f'a_{tag}_{role}.x'
            if self.static:
                end = 31 * F.UNITTIME          # like a_gateholl_stay: 31 frames
                ident = np.eye(4)
                tracks = [(self.bone[None], [(0, ident), (end, ident)])]
            else:
                k = self.g.anim_index(clips[role])
                t0, t1 = self.g.anim_range(k)
                nfr = max(int(round((t1 - t0) * FPS)), 1) + 1
                frames = []
                for f in range(nfr):
                    t = min(t0 + f / FPS, t1)
                    frames.append(self.game_worlds(self.g.locals_at(k, t)))
                if role in cfg.get('in_place', []):
                    self.strip_root_motion(frames)
                loc = [self.game_locals(gw) for gw in frames]
                end = (nfr - 1) * F.UNITTIME
                tracks = [(b, [(f * F.UNITTIME, to_row(loc[f][b])) for f in range(nfr)]) for b in self.parent]
            open(os.path.join(self.out, 'data/animation', f'a_{tag}_{role}.bin'), 'wb').write(F.write_bin(tracks))
            strikes = []
            if role == 'attack':
                strikes = [int(round(x * end / F.UNITTIME)) * F.UNITTIME for x in cfg.get('strikes', [0.5])]
            cfgb = F.write_cfg(name, skel, end, main, loop=loop, strikes=strikes)
            open(os.path.join(self.out, 'rcc/animation', f'a_{tag}_{role}.cfg'), 'wb').write(cfgb)
            out.append(dict(role=role, anim=name, main=main, loop=loop, end=end, strikes=strikes))
            print(f'  anim {role:6s} {name:28s} {len(tracks[0][1]):4d} keys x {len(tracks)} bones, '
                  f'{end / F.TICKS_PER_SEC:.2f} s' + (f', strikes {strikes}' if strikes else ''))
        return out

    def strip_root_motion(self, frames):
        """Keep the clip in place (the server moves the crow): the shallowest bone whose horizontal
        position travels more than 5% of the model height from the first to the last frame is the
        root mover; its constant-velocity travel (start->end, linear in time) is subtracted from it
        and everything under it. Sway around the path stays."""
        bones = list(self.parent)
        net = {b: np.linalg.norm(frames[-1][b][[0, 2], 3] - frames[0][b][[0, 2], 3]) for b in bones}
        movers = [b for b in bones if net[b] > 0.05 * self.cfg['height']]
        if not movers:
            print('  in place: no root travel found')
            return
        mover = min(movers, key=self.depth)
        sub = {b for b in bones if self.is_under(b, mover)}
        p0 = frames[0][mover][[0, 2], 3].copy()
        p1 = frames[-1][mover][[0, 2], 3].copy()
        n = len(frames) - 1
        for k, f in enumerate(frames):
            d = (p1 - p0) * (k / n)
            for b in sub:
                f[b] = f[b].copy()
                f[b][[0, 2], 3] -= d
        print(f'  in place: removed {net[mover]:.1f} units of travel of {mover} (and {len(sub) - 1} bones under it)')

    def depth(self, b):
        d = 0
        while self.parent[b]:
            b = self.parent[b]
            d += 1
        return d

    def is_under(self, b, top):
        while b:
            if b == top:
                return True
            b = self.parent[b]
        return False

    def preview(self, v, idx):
        """Front (looking down +Z at the crow's face: the crow faces -Z) and side silhouettes."""
        from preview import render
        render(v, idx, os.path.join(self.out, f'{self.tag}_preview.png'), title=f'{self.key} (front | side)')


def main():
    cfg = load_config()
    for k in mob_keys(cfg, sys.argv[1:]):
        Mob(k, cfg[k], MODELS).build()


if __name__ == '__main__':
    main()
