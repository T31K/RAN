"""Add Samehada (0_22) and Samehada Unleashed (0_23) to a glogic.rcc, cloned from ChuWang Sword (0_17).

  add_items.py <glogic.rcc> [<output glogic.rcc>]      (in place when no output is given)

Only item.isf and ItemStrTable.txt change; every other archive entry is copied as is.
Idempotent: existing 0_22 / 0_23 records and string entries are replaced. The same file goes to
the client (data/glogic/glogic.rcc) and the server (/opt/ran/game-client/data/glogic/glogic.rcc).
"""
import copy
import re
import shutil
import struct
import sys
import zipfile
import itemisf as I
from rcc import rewrite_zip, aes

TEMPLATE = (0, 17)       # ChuWang Sword: last permanent MID-0 sword that has wearing models
NAMES_AFTER = (0, 21)    # string table entries go after the last MID-0 entry
NEW = [
    # nid, name, description, piece prefix, damage low/high, hit, avoid
    ((0, 22), 'Samehada',
     'The living shark-skin greatsword of the Seven Swordsmen of the Mist. It shaves and devours chakra.',
     'SDN9024', 420, 520, 30, 0),
    ((0, 23), 'Samehada Unleashed',
     'Samehada with its bandages torn away. Its scales bristle and its teeth hunger for every cut.',
     'SDN9025', 560, 690, 40, 5),
]


def set_wearing(item, prefix):
    """Point every *_m / *_w .cps wearing file of the SBASIC chunk at <prefix>_m.cps / <prefix>_w.cps."""
    t, v, data = item.chunk(I.FILE_SBASIC)
    out, pos = b'', 0
    for m in re.finditer(rb'([\w\[\]]+\.cps)\x00', data):
        s = m.start(1)
        (n,) = struct.unpack_from('<I', data, s - 4)
        if n != len(m.group(1)) + 1:
            continue
        sex = b'w' if re.search(rb'_w(_|\.)', m.group(1), re.I) else b'm'
        new = prefix.encode() + b'_' + sex + b'.cps\x00'
        out += data[pos:s - 4] + struct.pack('<I', len(new)) + new
        pos = m.end()
    item.chunk(I.FILE_SBASIC)[2] = out + data[pos:]


def build_isf(raw):
    head, items, tail = I.parse(raw)
    by = {i.nid: i for i in items}
    for nid, *_ in NEW:
        if nid in by:
            items.remove(by[nid])
    tpl = by[TEMPLATE]
    for nid, name, desc, prefix, lo, hi, hit, avoid in NEW:
        it = copy.deepcopy(tpl)
        I.set_basic(it, nid=nid, name='IN_%03d_%03d' % nid)
        set_wearing(it, prefix)
        I.set_suit(it, dmg_lo=lo, dmg_hi=hi, hit=hit, avoid=avoid)
        items.append(it)
    return I.serialize(head, items, tail)


def build_names(raw):
    plain = aes(raw[4:], True).rstrip(b'\0')
    for nid, name, desc, *_ in NEW:
        key = b'%03d_%03d' % nid
        plain = re.sub(rb'IN_' + key + rb'\t[^\r\n]*\r\nID_' + key + rb'\t[^\r\n]*\r\n', b'', plain)
    anchor = b'ID_%03d_%03d\t' % NAMES_AFTER
    i = plain.index(anchor)
    i = plain.index(b'\r\n', i) + 2
    add = b''.join(b'IN_%03d_%03d\t' % nid + name.encode() + b'\r\nID_%03d_%03d\t' % nid + desc.encode() + b'\r\n'
                   for nid, name, desc, *_ in NEW)
    plain = plain[:i] + add + plain[i:]
    plain += b'\0' * (-len(plain) % 16)
    return raw[:4] + aes(plain, False)


def apply(src, dst):
    with zipfile.ZipFile(src) as z:
        isf, names = z.read('item.isf'), z.read('ItemStrTable.txt')
    if src != dst:
        shutil.copy2(src, dst)
    rewrite_zip(dst, {'item.isf': build_isf(isf), 'ItemStrTable.txt': build_names(names)})

    # verify by re-reading
    with zipfile.ZipFile(dst) as z:
        assert z.testzip() is None
        head, items, tail = I.parse(z.read('item.isf'))
        nm = I.E.read_names(z)
        by = {i.nid: i for i in items}
        for nid, name, *_ in NEW:
            it = by[nid]
            cps = re.findall(rb'[\w\[\]]+\.cps', it.chunk(I.FILE_SBASIC)[2])
            print(nid, repr(nm.get(nid)), I.suit(it), sorted({c.decode() for c in cps}))
        print(len(items), 'items')


if __name__ == '__main__':
    apply(sys.argv[1], sys.argv[2] if len(sys.argv) > 2 else sys.argv[1])
