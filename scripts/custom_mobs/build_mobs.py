"""Put the custom crows built by make_mob.py into a game folder's data, or build the install tree.

  build_mobs.py [--client ~/Projects/RAN/client] [<mob> ...]
      Builds port/build/custom_mobs/out/ from COPIES of the client's archives (the client is
      only read). The tree is laid out like a client folder.
  build_mobs.py --install <game-dir> [--server] [<mob> ...]
      Applies the same changes in place to <game-dir> (a client folder, or with --server a
      server's game-client folder: archives only, no loose meshes/textures). Before the first
      install into a folder its three archives are backed up once under
      port/build/custom_mobs/backups/<folder path>/.

What changes (default: every mob in mobs.json), all idempotent (ids / entries are replaced):
  data/glogic/glogic.rcc          crow.mnsf records + CrowStrTable.txt names
  data/skinobject/skinobject.rcc  + <tag>.chf, <tag>.cps
  data/animation/animation.rcc    + a_<tag>_<role>.cfg
  data/skeleton/ data/skin/ data/animation/   + b_<tag>.x, s_<tag>.x, a_<tag>_<role>.bin  (client)
  textures/mob/                   + <tag>_<n>.dds                                       (client)
Each crow is a copy of its template crow (stats, AI, drops) in the TARGET's own crow.mnsf with the
id, type, skin, body radius, talk file and attack animation replaced. No numpy needed.
"""
import argparse
import os
import shutil
import sys
import zipfile

HERE = os.path.dirname(os.path.abspath(__file__))
sys.path.insert(0, HERE)
sys.path.insert(0, os.path.join(HERE, '..', 'custom_items'))
import crowmnsf as C  # noqa: E402
from mobcfg import OUT, built_dir, load_built, load_config, mob_keys  # noqa: E402
from rcc import rewrite_zip  # noqa: E402

DEST = os.path.join(OUT, 'out')
NULL_ID = 0xFFFFFFFF
RCCS = ('glogic', 'skinobject', 'animation')


def crow_record(crows, cfg, built):
    by = {c.nid: c for c in crows}
    tmpl = by[tuple(cfg['template'])]
    crow = C.clone(tmpl, tuple(cfg['id']))
    crow.set_basic(crow_type=C.CROW_NPC if cfg['crow'] == 'npc' else C.CROW_MOB,
                   level=cfg.get('level'), hp=cfg.get('hp'))
    act = dict(strSkinObj=built['chf'], wBodyRadius=int(cfg.get('body_radius', 4)))
    if 'talk' in cfg:
        act['strTalkFile'] = cfg['talk']
    crow.set_action(**act)
    attack = next((a for a in built['anims'] if a['role'] == 'attack'), None)
    if attack:
        # SANIATTACK::Assign(cfg): m_dwETime = last key - 160, strikes = the cfg strike ticks
        ani = C.pack_aniattack(0, max(attack['end'] - C.UNITTIME, 0), C.UNITTIME, 0, C.AN_ATTACK, 0,
                               attack['strikes'])
        for i in range(3):
            crow.set_attack(i, strAniFile=attack['anim'], sAniAttack=ani)
        crow.set_attack(0, bUsed=1, emAttRgType=0, wRange=int(cfg.get('attack_range', 10)),
                        skill_id=NULL_ID, skill_lev=0)       # plain melee hit, no mob skill
        crow.set_attack(1, bUsed=0)
        crow.set_attack(2, bUsed=0)
    return crow


def apply_to(root, cfg, keys, loose=True):
    """Add the crows to the game folder `root` (has data/, and textures/ for a client)."""
    data = os.path.join(root, 'data')
    rccs = {r: os.path.join(data, r, f'{r}.rcc') for r in RCCS}
    built = {k: load_built(cfg, k) for k in keys}

    with zipfile.ZipFile(rccs['glogic']) as z:
        raw, st = z.read('crow.mnsf'), z.read('CrowStrTable.txt')
    head, ver, crows, tail = C.parse(raw)
    assert C.serialize(head, ver, crows, tail) == raw, 'crow.mnsf does not round-trip; not touching it'
    names = {}
    for k in keys:
        c = cfg[k]
        C.put(crows, crow_record(crows, c, built[k]))
        names[tuple(c['id'])] = (c['name'], c.get('desc', ''))
    prefix, text, _ = C.read_strtable(st)
    rewrite_zip(rccs['glogic'], {'crow.mnsf': C.serialize(head, ver, crows, tail),
                                 'CrowStrTable.txt': C.write_strtable(prefix, C.set_names(text, names))})

    skinobj, anim = {}, {}
    for k in keys:
        src = built_dir(cfg, k)
        for f in os.listdir(os.path.join(src, 'rcc', 'skinobject')):
            skinobj[f] = open(os.path.join(src, 'rcc', 'skinobject', f), 'rb').read()
        for f in os.listdir(os.path.join(src, 'rcc', 'animation')):
            anim[f] = open(os.path.join(src, 'rcc', 'animation', f), 'rb').read()
        if loose:
            for d in ('data/skeleton', 'data/skin', 'data/animation', 'textures/mob'):
                os.makedirs(os.path.join(root, d), exist_ok=True)
                for f in os.listdir(os.path.join(src, d)):
                    shutil.copy2(os.path.join(src, d, f), os.path.join(root, d, f))
    rewrite_zip(rccs['skinobject'], skinobj)
    rewrite_zip(rccs['animation'], anim)
    for dst in rccs.values():
        with zipfile.ZipFile(dst) as z:
            assert z.testzip() is None, dst
    for k in keys:
        c = cfg[k]
        print(f'  {c["id"][0]} {c["id"][1]}  {c["name"]:18s} {c["crow"]:3s}  {built[k]["chf"]}')


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument('--client', default=os.path.expanduser('~/Projects/RAN/client'))
    ap.add_argument('--install', metavar='GAME_DIR')
    ap.add_argument('--server', action='store_true', help='with --install: archives only')
    ap.add_argument('mobs', nargs='*')
    a = ap.parse_args()
    cfg = load_config()
    keys = mob_keys(cfg, a.mobs)
    if a.install:
        root = os.path.abspath(os.path.expanduser(a.install))
        backup = os.path.join(OUT, 'backups', root.strip('/').replace('/', '_'))
        os.makedirs(backup, exist_ok=True)
        for r in RCCS:
            p = os.path.join(root, 'data', r, f'{r}.rcc')
            assert os.path.exists(p), f'{p} not found (give the folder that holds data/)'
            if not os.path.exists(os.path.join(backup, f'{r}.rcc')):     # first install only
                shutil.copy2(p, os.path.join(backup, f'{r}.rcc'))
        print('installing into', root, '(archives backed up in', backup + ')')
        apply_to(root, cfg, keys, loose=not a.server)
        return
    shutil.rmtree(DEST, ignore_errors=True)           # no stale files from earlier builds
    for r in RCCS:
        os.makedirs(os.path.join(DEST, 'data', r), exist_ok=True)
        dst = os.path.join(DEST, 'data', r, f'{r}.rcc')
        shutil.copy2(os.path.join(a.client, 'data', r, f'{r}.rcc'), dst)
        os.chmod(dst, 0o644)
    print('building', DEST)
    apply_to(DEST, cfg, keys)


if __name__ == '__main__':
    main()
