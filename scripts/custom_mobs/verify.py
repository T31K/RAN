"""Check everything build_mobs.py wrote, without the game.

  verify.py [--client ~/Projects/RAN/client] [--stock] [<mob> ...]

1. crow.mnsf / CrowStrTable.txt in out/data/glogic/glogic.rcc: round-trips byte for byte, every
   stock record is unchanged, the new records carry the right id / type / skin / attack anim, and
   the names resolve.
2. Per mob: re-parses the .chf/.cps (skinobject.rcc), every .cfg (animation.rcc), .bin, skeleton
   and skin .x with the readers in mobfmt.py (which mirror the C++ loaders) and cross-checks
   them (file references, bone names, offset = inverse bind, key times vs cfg times, textures).
3. Plays every animation the way the engine does (bone local = key matrix, matCombined = local *
   parent, vertex = sum w * v * offset * matCombined) from the written files only, compares the
   result with the glTF sampled at the same time (max vertex error), and renders a contact sheet
   per mob (port/build/custom_mobs/mobs/<tag>/<tag>_anims.png).
--stock: also runs the readers over every stock .chf/.cps/.cfg/.bin of the client, to prove
   them on real data.
"""
import argparse
import os
import subprocess
import sys
import tempfile
import zipfile

import numpy as np

HERE = os.path.dirname(os.path.abspath(__file__))
sys.path.insert(0, HERE)
import crowmnsf as C  # noqa: E402
import mobfmt as F  # noqa: E402
from make_mob import Mob  # noqa: E402
from mobcfg import MODELS, OUT, load_config, mob_keys  # noqa: E402
from preview import render_textured, view  # noqa: E402

DEST = os.path.join(OUT, 'out')
FAIL = []


def check(cond, msg):
    if not cond:
        FAIL.append(msg)
        print('  FAIL', msg)
    return cond


def verify_crows(cfg, keys, client):
    print('== crow.mnsf / CrowStrTable.txt')
    with zipfile.ZipFile(os.path.join(DEST, 'data/glogic/glogic.rcc')) as z:
        assert z.testzip() is None
        raw, st = z.read('crow.mnsf'), z.read('CrowStrTable.txt')
    with zipfile.ZipFile(os.path.join(client, 'data/glogic/glogic.rcc')) as z:
        sraw = z.read('crow.mnsf')
        snames = set(z.namelist())
    with zipfile.ZipFile(os.path.join(DEST, 'data/glogic/glogic.rcc')) as z:
        check(set(z.namelist()) == snames, 'glogic.rcc entry list changed')
    head, ver, crows, tail = C.parse(raw)
    check(C.serialize(head, ver, crows, tail) == raw, 'crow.mnsf round-trip')
    _, _, stock, _ = C.parse(sraw)
    new_ids = {tuple(cfg[k]['id']) for k in keys}
    by = {c.nid: c for c in crows}
    for c in stock:
        if c.nid in new_ids:
            continue
        check(c.nid in by and by[c.nid].serialize() == c.serialize(), f'stock crow {c.nid} changed')
    check(len(crows) == len({c.nid for c in stock} | new_ids), 'crow count')
    prefix, text, pad = C.read_strtable(st)
    check(C.write_strtable(prefix, text, pad) == st, 'CrowStrTable round-trip')
    names = C.names(text)
    for k in keys:
        m = cfg[k]
        nid = tuple(m['id'])
        c = by.get(nid)
        if not check(c is not None, f'{k}: crow {nid} missing'):
            continue
        typ = C.CROW_NPC if m['crow'] == 'npc' else C.CROW_MOB
        act = c.chunk(C.FILE_SACTION).fields()
        att = c.attacks()
        print(f'  {nid} {names.get(nid)!r} type {c.crow_type} name {c.name} skin {c.skin} radius {act["wBodyRadius"]} '
              f'attack0 {C.pystr(att[0]["strAniFile"])!r} used {[a["bUsed"] for a in att]} '
              f'{C.unpack_aniattack(att[0]["sAniAttack"])}')
        check(c.crow_type == typ, f'{k}: crow type')
        check(c.name == 'CN_%03d_%03d' % nid, f'{k}: SBASIC name')
        check(c.skin == f'{m["tag"]}.chf', f'{k}: skin')
        check(names.get(nid) == m['name'], f'{k}: CrowStrTable name')
        tmpl = by[tuple(m['template'])]
        check(c.chunk(C.FILE_SBASIC).data[48:] == tmpl.chunk(C.FILE_SBASIC).data[48:] or 'hp' in m or 'level' in m,
              f'{k}: stats differ from template')


def engine_pose(frames, tracks, tick):
    """World (matCombined, row vectors) of every bone at `tick`, from skeleton frames + .bin keys."""
    local = {}
    for b, (p, m) in frames.items():
        local[b] = m
    for tr in tracks:
        keys = tr['keys']
        k = max(i for i, (t, _) in enumerate(keys) if t <= tick) if keys[0][0] <= tick else 0
        local[tr['bone']] = np.asarray(keys[k][1], float)
    out = {}

    def w(b):
        if b not in out:
            p = frames[b][0]
            out[b] = local[b] if p is None else local[b] @ w(p)
        return out[b]
    for b in frames:
        w(b)
    return out


def skin_vertices(skin, world):
    v = np.c_[skin['verts'], np.ones(len(skin['verts']))]
    acc = np.zeros((len(v), 4))
    for bone, ws in skin['weights'].items():
        ids = np.array(list(ws))
        wt = np.array([ws[i] for i in ids])
        acc[ids] += wt[:, None] * (v[ids] @ skin['offsets'][bone] @ world[bone])
    return acc[:, :3]


def verify_mob(key, m, models):
    tag = m['tag']
    print(f'== {key} ({tag})')
    zs = zipfile.ZipFile(os.path.join(DEST, 'data/skinobject/skinobject.rcc'))
    za = zipfile.ZipFile(os.path.join(DEST, 'data/animation/animation.rcc'))
    chf = F.read_chf(zs.read(f'{tag}.chf'))
    print(f'  chf: scale {chf["scale"]} pieces {chf["pieces"]} skeleton {chf["skeleton"]} anims {len(chf["anims"])} '
          f'bbox {chf["bbox_min"]}..{chf["bbox_max"]} world_obj {chf["world_obj"]} height {chf["height"]:.1f}')
    check(chf['version'] == 0x102, 'chf version')
    cps_name = chf['pieces'][0]
    cps = F.read_cps(zs.read(cps_name))
    print(f'  cps: {cps["xfile"]} / {cps["skeleton"]} mesh {cps["mesh"]} type {cps["piece_type"]} '
          f'materials {[x["texture"] for x in cps["materials"]]}')
    check(cps['skeleton'] == chf['skeleton'], 'cps skeleton == chf skeleton (FG_BONEFILE_CHECK)')
    check(cps['piece_type'] in chf['pieces'] and chf['pieces'][cps['piece_type']] == cps_name, 'piece slot == piece type')
    skel_text = open(os.path.join(DEST, 'data/skeleton', chf['skeleton'])).read()
    frames = F.read_skeleton_x(skel_text)
    skin = F.read_skin_x(open(os.path.join(DEST, 'data/skin', cps['xfile'])).read())
    print(f'  skeleton: {len(frames)} bones; skin: {len(skin["verts"])} verts {len(skin["faces"])} tris '
          f'{len(skin["weights"])} weighted bones, max {skin["max_per_vertex"]} weights/vertex, container {skin["mesh_container"]}')
    check(skin['mesh_container'] == cps['mesh'], 'cps mesh name == skin mesh container')
    check(len(skin['textures']) == len(cps['materials']), 'cps material count == skin materials')
    check(skin['max_per_vertex'] <= 4, 'at most 4 weights per vertex')
    for t in skin['textures']:
        p = os.path.join(DEST, 'textures/mob', t)
        if check(os.path.exists(p), f'texture {t}'):
            ident = subprocess.run(['magick', 'identify', '-format', '%m %wx%h %[compression]\\n', p],
                                   capture_output=True, text=True).stdout.split('\n')[0]
            check(open(p, 'rb').read(4) == b'DDS ', f'{t} is a DDS')
            print(f'  texture {t}: {ident}')
    for bone in skin['weights']:
        check(bone in frames, f'skin bone {bone} in skeleton')
    sk_frames = skin['frames']
    check(all(b in sk_frames for b in frames), 'skin file carries the skeleton frames')
    tex_png = os.path.join(OUT, 'mobs', tag, f'{tag}_textured.png')
    render_textured(skin['verts'], skin['faces'], skin['uv'], skin['mat_ids'],
                    [os.path.join(DEST, 'textures/mob', t) for t in skin['textures']], tex_png)
    print(f'  textured front view (from the written .x + .dds): {tex_png}')

    mob = Mob(key, m, models)
    mob.bones_setup()
    mob.setup_transform()
    # skeleton bind pose (no animation) * offsets == the glTF rest pose
    pos = mob.gather_mesh(mob.g.locals_at())['pw']
    ref = (np.c_[pos, np.ones(len(pos))] @ mob.T.T)[:, :3]
    err = np.abs(skin_vertices(skin, F.world_matrices(frames)) - ref).max()
    print(f'  skeleton bind pose x offsets vs glTF rest pose: max vertex error {err:.4f}')
    check(err < 0.01 * m['height'], 'bind pose matches the glTF rest pose')
    sheets = []
    clip_roles = {}
    for a in chf['anims']:
        cfg = F.read_cfg(za.read(a[:-2] + '.cfg'))
        check(cfg['name'] == a and cfg['skeleton'] == chf['skeleton'], f'{a}: cfg names')
        b = F.read_bin(open(os.path.join(DEST, 'data/animation', a[:-2] + '.bin'), 'rb').read())
        tracks = b['tracks']
        last = max(tr['keys'][-1][0] for tr in tracks)
        check(last == cfg['etime_orig'] and cfg['etime'] == max(last - F.UNITTIME, 0), f'{a}: key times vs cfg')
        check(all(tr['bone'] in frames for tr in tracks), f'{a}: track bones in skeleton')
        m3 = np.array([k[1][:3, :3] for tr in tracks for k in tr['keys']], float)
        check(np.isfinite(m3).all() and (np.linalg.det(m3) > 0).all(), f'{a}: key matrices are proper (det > 0)')
        # the engine turns each key into scale/quaternion/translation (D3DXMatrixToSQT ->
        # decomp_affine) and rebuilds scale * rotation * translation: a key survives that only if
        # its scale runs along the bone's own axes (row form m = K R  <=>  m m^T diagonal)
        q = m3 @ m3.transpose(0, 2, 1)
        diag = np.einsum('kii->ki', q)
        off = np.abs(q - np.einsum('ki,ij->kij', diag, np.eye(3))).max((1, 2)) / diag.max(1)
        sv = np.linalg.svd(m3, compute_uv=False)
        spread = float((sv.max(1) / sv.min(1)).max() - 1)
        check(float(off.max()) < 1e-3, f'{a}: key scale is not along the bone axes ({off.max():.2g})')
        if spread > 1e-3:
            print(f'  note {a}: non-uniform (axis) scale in keys, up to {spread:.2g}')
        role = a[len(f'a_{tag}_'):-2]
        clip_roles[role] = cfg
        nkeys = len(tracks[0]['keys'])
        # compare with the glTF at a few frames
        errs = []
        poses = []
        for f in range(0, nkeys, max(1, (nkeys - 1) // 5)):
            tick = tracks[0]['keys'][f][0]
            vg = skin_vertices(skin, engine_pose(frames, tracks, tick))
            poses.append(vg)
            ref = None
            if mob.static:
                ref = skin['verts']
            elif role not in m.get('in_place', []):     # in-place clips differ from the glTF on purpose
                k = mob.g.anim_index(m['clips'][role])
                t0, t1 = mob.g.anim_range(k)
                pos = mob.gather_mesh(mob.g.locals_at(k, min(t0 + tick / F.TICKS_PER_SEC, t1)))['pw']
                ref = (np.c_[pos, np.ones(len(pos))] @ mob.T.T)[:, :3]
            if ref is not None:
                errs.append(np.abs(vg - ref).max())
        if role in m.get('in_place', []):
            # in-place clips: the mesh must end where it started (no root travel left)
            last_pose = skin_vertices(skin, engine_pose(frames, tracks, tracks[0]['keys'][-1][0]))
            travel = np.linalg.norm(last_pose.mean(0)[[0, 2]] - poses[0].mean(0)[[0, 2]])
            print(f'  anim {role}: in place, centroid travel over the clip {travel:.3f}')
            check(travel < 0.05 * m['height'], f'{a}: still travels {travel:.2f}')
        print(f'  anim {role:6s} cfg main {cfg["main"]} loop {bool(cfg["flag"] & F.ACF_LOOP)} '
              f'etime {cfg["etime"]} strikes {cfg["strikes"]} keys {nkeys} x {len(tracks)} bones; '
              f'max vertex error vs glTF {max(errs) if errs else float("nan"):.4f} (model height {m["height"]})')
        if errs:
            check(max(errs) < 0.01 * m['height'], f'{a}: engine-mirrored pose matches glTF')
        sheets.append((role, poses))
    # contact sheet: one row per anim, front views
    rows = []
    allv = np.vstack([p for _, ps in sheets for p in ps])
    lo, hi = allv.min(0), allv.max(0)
    ext = (hi - lo).max()
    for role, poses in sheets:
        imgs = [view(p, skin['faces'], 0, 2, False, 200, ext, (lo + hi) / 2) for p in poses]
        imgs += [np.full((200, 200), 40, np.uint8)] * (6 - len(imgs))
        rows.append(np.concatenate(imgs[:6], 1))
    img = np.concatenate(rows, 0)
    fd, pgm = tempfile.mkstemp(suffix='.pgm')
    with os.fdopen(fd, 'wb') as fh:
        fh.write(b'P5 %d %d 255\n' % (img.shape[1], img.shape[0]) + img.tobytes())
    sheet = os.path.join(OUT, 'mobs', tag, f'{tag}_anims.png')
    subprocess.run(['magick', pgm, sheet], check=True)
    os.remove(pgm)
    print(f'  contact sheet ({", ".join(r for r, _ in sheets)}; rows top to bottom): {sheet}')
    # what the crow's attack record says vs the attack cfg
    if 'attack' in clip_roles:
        with zipfile.ZipFile(os.path.join(DEST, 'data/glogic/glogic.rcc')) as z:
            _, _, crows, _ = C.parse(z.read('crow.mnsf'))
        c = next(c for c in crows if c.nid == tuple(m['id']))
        ani = C.unpack_aniattack(c.attacks()[0]['sAniAttack'])
        cf = clip_roles['attack']
        check(ani['etime'] == cf['etime'] and ani['strikes'] == cf['strikes'] and ani['main'] == F.AN_ATTACK,
              'crow SANIATTACK == attack cfg (SANIATTACK::Assign)')


def verify_stock(client):
    import collections
    print('== readers over the stock client data')
    res = collections.Counter()
    with zipfile.ZipFile(os.path.join(client, 'data/skinobject/skinobject.rcc')) as z:
        for n in z.namelist():
            d = z.read(n)
            ext = n.lower()[-4:]
            try:
                if ext == '.chf':
                    F.read_chf(d); res['chf parsed to the end'] += 1
                elif ext == '.cps':
                    r = F.read_cps(d, allow_effects=True)
                    res['cps (0x105-0x107) ' + ('parsed up to the effect list' if r['effects'] else 'parsed to the end')] += 1
            except AssertionError as e:
                res[f'{ext} skipped: {e}'] += 1
    with zipfile.ZipFile(os.path.join(client, 'data/animation/animation.rcc')) as z:
        for n in z.namelist():
            if n.lower().endswith('.cfg'):
                try:
                    r = F.read_cfg(z.read(n), allow_effects=True)
                    res['cfg (0x105-0x107) ' + ('parsed up to effects' if r['effects'] else 'parsed to the end')] += 1
                except AssertionError as e:
                    res[f'cfg skipped: {str(e)[:40]}'] += 1
    adir = os.path.join(client, 'data/animation')
    for n in sorted(os.listdir(adir)):
        if n.lower().endswith('.bin'):
            try:
                F.read_bin(open(os.path.join(adir, n), 'rb').read()); res['bin parsed to the end'] += 1
            except Exception as e:  # noqa: BLE001
                res[f'bin failed: {str(e)[:40]}'] += 1
    for k, v in sorted(res.items()):
        print(f'  {v:5d}  {k}')
    check(not any(k.startswith('bin failed') or k.startswith('chf skipped') for k in res), 'stock readers')


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument('--client', default=os.path.expanduser('~/Projects/RAN/client'))
    ap.add_argument('--stock', action='store_true')
    ap.add_argument('mobs', nargs='*')
    a = ap.parse_args()
    cfg = load_config()
    keys = mob_keys(cfg, a.mobs)
    if a.stock:
        verify_stock(a.client)
    verify_crows(cfg, keys, a.client)
    for k in keys:
        verify_mob(k, cfg[k], MODELS)
    print('\nRESULT:', 'all checks passed' if not FAIL else f'{len(FAIL)} FAILED: {FAIL}')
    sys.exit(1 if FAIL else 0)


if __name__ == '__main__':
    main()
