#!/usr/bin/env python3
"""Packs the game data for the browser client as a static site of content-addressed files.

    port/web/scripts/package_files.py [client-folder] [site-dir]

Every client file is gzip'd and stored as d/<hash> (hash of its contents), so a CDN can cache it
forever and an update only adds the files that changed. The files the game reads before its
server-select screen (port/web/data/boot-files.txt, recorded with ?fstrace) are also bundled into
a few boot packs (p/<hash>, each a run of the same gzip members, < 25 MB for Cloudflare Pages):
a new player downloads those in a handful of requests and is on the login screen; everything
else streams in behind, the first map's files (world-files.txt) first.

The game builds font caches (cache/*.dds, *.table) on its first launch - ~12 s of a new player's
wait. Built once by the browser client and kept in port/build/web/fontcache, they ship in the
boot packs instead. Rebuild them after a font change: run the page on an empty profile, then
    node port/web/scripts/run_headless.mjs port/build/web/out index.html 40 --profile <empty dir> \\
      --from-start --actions "30:export game/cache port/build/web/fontcache"

manifest.json:
  { "version": ..., "files": [[path, size, hash, csize], ...],
    "packs": [{"name": "p/<hash>", "bytes": n, "files": [[index, offset], ...]}],
    "world": [index, ...] }
"""
import gzip
import hashlib
import json
import os
import sys
from concurrent.futures import ProcessPoolExecutor

ROOT = os.path.abspath(os.path.join(os.path.dirname(__file__), '..', '..', '..'))
CLIENT = os.path.abspath(sys.argv[1] if len(sys.argv) > 1 else os.path.expanduser('~/Projects/RAN/client'))
SITE = os.path.abspath(sys.argv[2] if len(sys.argv) > 2 else os.path.join(ROOT, 'port/build/web/site'))
FONTCACHE = os.path.join(ROOT, 'port/build/web/fontcache')
PACK_LIMIT = 20 << 20   # Cloudflare Pages serves files up to 25 MiB

# Windows-only files and caches the game regenerates (as package_native_app.sh leaves out).
SKIP_EXT = ('.exe', '.dll', '.pdb', '.bak', '.des', '.log')
SKIP_DIRS = ('GameGuard/', 'cache/', '$PLUGINSDIR/', '$SYSDIR/')


def list_files():
    out = []
    for dirpath, _, names in os.walk(CLIENT):
        for n in names:
            rel = os.path.relpath(os.path.join(dirpath, n), CLIENT).replace(os.sep, '/')
            if n == '.DS_Store' or n.lower().endswith(SKIP_EXT) or rel.startswith(SKIP_DIRS):
                continue
            out.append(rel)
    if os.path.isdir(FONTCACHE):
        out += ['cache/' + n for n in os.listdir(FONTCACHE) if not n.startswith('.')]
    return sorted(out)


def source(rel):
    if rel.startswith('cache/'):
        return os.path.join(FONTCACHE, rel[len('cache/'):])
    return os.path.join(CLIENT, rel)


def pack_one(rel):
    data = open(source(rel), 'rb').read()
    h = hashlib.sha256(data).hexdigest()[:24]
    dst = os.path.join(SITE, 'd', h)
    if not os.path.exists(dst):
        gz = gzip.compress(data, compresslevel=9, mtime=0)
        tmp = dst + '.tmp'
        with open(tmp, 'wb') as f:
            f.write(gz)
        os.replace(tmp, dst)
    return rel, len(data), h, os.path.getsize(dst)


def read_list(name):
    path = os.path.join(ROOT, 'port/web/data', name)
    return [l.strip() for l in open(path) if l.strip()] if os.path.exists(path) else []


def main():
    os.makedirs(os.path.join(SITE, 'd'), exist_ok=True)
    os.makedirs(os.path.join(SITE, 'p'), exist_ok=True)
    rels = list_files()
    print(f'{len(rels)} files from {CLIENT}')
    with ProcessPoolExecutor() as ex:
        files = list(ex.map(pack_one, rels, chunksize=32))
    index = {f[0].lower(): i for i, f in enumerate(files)}

    # Boot packs, in the order the game reads the files.
    boot = [index[p.lower()] for p in read_list('boot-files.txt') if p.lower() in index]
    boot += [i for i, f in enumerate(files) if f[0].startswith('cache/') and i not in set(boot)]
    packs, cur, cur_bytes = [], [], 0
    for i in boot:
        if cur and cur_bytes + files[i][3] > PACK_LIMIT:
            packs.append(cur)
            cur, cur_bytes = [], 0
        cur.append(i)
        cur_bytes += files[i][3]
    if cur:
        packs.append(cur)
    pack_meta = []
    for members in packs:
        blob, entries = bytearray(), []
        for i in members:
            entries.append([i, len(blob)])
            blob += open(os.path.join(SITE, 'd', files[i][2]), 'rb').read()
        name = 'p/' + hashlib.sha256(blob).hexdigest()[:24]
        if not os.path.exists(os.path.join(SITE, name)):
            with open(os.path.join(SITE, name), 'wb') as f:
                f.write(blob)
        pack_meta.append({'name': name, 'bytes': len(blob), 'files': entries})

    world = [index[p.lower()] for p in read_list('world-files.txt') if p.lower() in index]
    body = {'files': [list(f) for f in files], 'packs': pack_meta, 'world': world}
    version = hashlib.sha256(json.dumps(body, sort_keys=True).encode()).hexdigest()[:16]
    body['version'] = version
    with open(os.path.join(SITE, 'manifest.json'), 'w') as f:
        json.dump(body, f, separators=(',', ':'))

    # Drop blobs and packs no longer referenced (old versions).
    live = {f[2] for f in files}
    for h in os.listdir(os.path.join(SITE, 'd')):
        if h not in live:
            os.remove(os.path.join(SITE, 'd', h))
    live_packs = {p['name'][2:] for p in pack_meta}
    for h in os.listdir(os.path.join(SITE, 'p')):
        if h not in live_packs:
            os.remove(os.path.join(SITE, 'p', h))

    total = sum(f[3] for f in files)
    boot_bytes = sum(p['bytes'] for p in pack_meta)
    print(f'version {version}: {total / 1e6:.0f} MB in {len(files)} files; '
          f'login screen {boot_bytes / 1e6:.1f} MB in {len(pack_meta)} packs; '
          f'largest file {max(f[3] for f in files) / 1e6:.1f} MB -> {SITE}')


if __name__ == '__main__':
    main()
