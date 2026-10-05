"""item.isf (GLItemMan::LoadFile / SITEM::SaveFile) read + write.

File: 128-byte type + DWORD ver (plain), then compbyte-encoded body:
  DWORD num, num x SITEM: DWORD version, chunks [DWORD type, DWORD ver, DWORD size, size bytes]...,
  DWORD FILE_END_DATA. Exception: FILE_RANDOMITEM (10) is a raw char[64] with no ver/size.
SBASIC (type 1) body: DWORD nid, DWORD group, CString name (DWORD len + bytes incl. NUL), DWORD level,
  WORD gradeAtt, WORD gradeDef, ... (see SBASIC::SAVE).
SSUIT (type 2) body = raw ITEM::SSUIT (Win32 layout): +20 short nHitRate, +22 short nAvoidRate,
  +24 WORD damage low, +26 WORD damage high, +28 short nDefense.
"""
import struct
import sys
from rcc import E

FILE_SBASIC, FILE_SSUIT = 1, 2
FILE_END_DATA = 0xEDEDEDED
FILE_RANDOMITEM = 10


class Item:
    def __init__(self, version, chunks, end):
        self.version, self.chunks, self.end = version, chunks, end   # chunks: [type, ver, bytes]

    def chunk(self, t):
        return next(c for c in self.chunks if c[0] == t)

    @property
    def nid(self):
        n = struct.unpack_from('<I', self.chunk(FILE_SBASIC)[2], 0)[0]
        return (n & 0xFFFF, n >> 16)

    def serialize(self):
        out = struct.pack('<I', self.version)
        for t, v, data in self.chunks:
            out += struct.pack('<I', t) + data if v is None else struct.pack('<III', t, v, len(data)) + data
        return out + struct.pack('<I', self.end)


def parse(raw):
    head, body = raw[:132], raw[132:].translate(E.DECODE)
    num = struct.unpack_from('<I', body, 0)[0]
    o, items = 4, []
    for _ in range(num):
        version = struct.unpack_from('<I', body, o)[0]; o += 4
        chunks = []
        while True:
            t = struct.unpack_from('<I', body, o)[0]; o += 4
            if t == FILE_END_DATA:
                items.append(Item(version, chunks, t)); break
            if t == FILE_RANDOMITEM:            # SRANDOM_OPT::SAVE: raw char[64], no ver/size
                chunks.append([t, None, body[o:o + 64]]); o += 64
                continue
            v, size = struct.unpack_from('<II', body, o)
            chunks.append([t, v, body[o + 8:o + 8 + size]]); o += 8 + size
    return head, items, body[o:]


def serialize(head, items, tail):
    body = struct.pack('<I', len(items)) + b''.join(i.serialize() for i in items) + tail
    return head + body.translate(E.COMPBYTE)


def basic_fields(data):
    """(nid, group, name, rest-after-name) of an SBASIC body."""
    nid, group, n = struct.unpack_from('<III', data, 0)
    name = data[12:12 + n]
    return nid, group, name, data[12 + n:]


def set_basic(item, nid=None, name=None, replace=()):
    t, v, data = item.chunk(FILE_SBASIC)
    onid, group, oname, rest = basic_fields(data)
    nid = onid if nid is None else (nid[0] | (nid[1] << 16))
    nm = oname if name is None else name.encode() + b'\0'
    for a, b in replace:                     # length-prefixed CStrings inside rest
        a0, b0 = a.encode() + b'\0', b.encode() + b'\0'
        assert struct.pack('<I', len(a0)) + a0 in rest, a
        rest = rest.replace(struct.pack('<I', len(a0)) + a0, struct.pack('<I', len(b0)) + b0)
    item.chunk(FILE_SBASIC)[2] = struct.pack('<III', nid, group, len(nm)) + nm + rest


def suit(item):
    d = item.chunk(FILE_SSUIT)[2]
    hit, avoid, lo, hi, dfn = struct.unpack_from('<hhHHh', d, 20)
    return dict(hit=hit, avoid=avoid, dmg_lo=lo, dmg_hi=hi, defense=dfn, att_range=struct.unpack_from('<H', d, 16)[0])


def set_suit(item, **kw):
    s = suit(item); s.update(kw)
    d = bytearray(item.chunk(FILE_SSUIT)[2])
    struct.pack_into('<hhHHh', d, 20, s['hit'], s['avoid'], s['dmg_lo'], s['dmg_hi'], s['defense'])
    item.chunk(FILE_SSUIT)[2] = bytes(d)


if __name__ == '__main__':   # itemisf.py <glogic.rcc>: parse + round-trip check
    import zipfile
    raw = zipfile.ZipFile(sys.argv[1]).read('item.isf')
    head, items, tail = parse(raw)
    print(len(items), 'items, tail', len(tail), 'bytes')
    print('round-trip identical:', serialize(head, items, tail) == raw)
