"""crow.mnsf (GLCrowDataMan::LoadFile / SCROWDATA::LoadFile) + CrowStrTable.txt read / write.

crow.mnsf (inside data/glogic/glogic.rcc):
  128-byte file type "GLCROW" + DWORD file version (0x0102), plain. The rest is compbyte encoded
  (same byte substitution as item.isf): DWORD 0x0102, DWORD crow count, then per crow
  DWORD 0x0101 and chunks [DWORD type, DWORD ver, DWORD size, data] ending with 0xEDEDEDED.

    1 SBASIC  ver 0x110: raw SCROWBASIC (124 bytes, Win32 layout, see BASIC_* offsets below)
    2 SACTION ver 0x103..0x108: field by field (SCROWACTION::LOAD10x); size = bytes written
    3 SGEN    ver 0x102: SCROWGEN::LOAD (DWORD, DWORD, WORD, WORD, 2 strings). Its size field is
              sizeof(SCROWGEN) and NOT the bytes written, so the loader (and this parser) walk fields.
    4 SATTACK ver 0x105: 3 x SCROWATTACK::LOAD (strAniFile + raw 44-byte SANIATTACK inside)

  std::string = DWORD (len+1) + bytes incl. NUL. BOOL = 4 bytes, bool = 1 byte.

Untouched chunks keep their exact bytes (round-trip is byte-identical). Patched SACTION/SATTACK
chunks are re-encoded from fields, with a correct size.

CrowStrTable.txt: 4-byte prefix + AES-256-ECB text (CP949/latin1), lines "CN_mmm_sss\\tName" and
"CD_mmm_sss\\tDescription", NUL padded to 16.

  crowmnsf.py <glogic.rcc>       round-trip both files + print a summary
"""
import copy
import os
import struct
import sys

sys.path.insert(0, os.path.join(os.path.dirname(os.path.abspath(__file__)), '..', 'custom_items'))
from rcc import E, aes  # noqa: E402

FILE_SBASIC, FILE_SACTION, FILE_SGEN, FILE_SATTACK = 1, 2, 3, 4
FILE_END_DATA = 0xEDEDEDED
CROW_NPC, CROW_MOB = 1, 2

# SCROWBASIC (0x110) offsets: sNativeID DWORD (wMainID | wSubID << 16), char m_szName[33] @4,
# EMTRIBE @40, EMCROW @44, EMBRIGHT @48, WORD level @52, DWORD HP @56, WORD MP @60, WORD SP @62, ...
BASIC_NAME, BASIC_CROW, BASIC_LEVEL, BASIC_HP = 4, 44, 52, 56
SANIATTACK_SIZE = 44     # 4 DWORD (stime, etime, unittime, flags), 2 enums, WORD count, WORD[8], pad 2
ACF_DIV = 8
ANIATTACK_FMT = '<IIIIIIH8H2x'
UNITTIME = 160           # animation ticks per 30 fps frame
AN_ATTACK = 4            # EMANI_MAINTYPE
assert struct.calcsize(ANIATTACK_FMT) == SANIATTACK_SIZE
PATTERN_SIZE = 12        # SCROWPATTERN: float, enum, DWORD

# ---- field schemas -------------------------------------------------------------------------
# kinds: H WORD, I DWORD/BOOL/enum, f float, S std::string, (raw, n) n raw bytes,
#        P pattern list (DWORD n + n*12 bytes)
_ACT_HEAD = [('wBodyRadius', 'H'), ('strSkinObj', 'S'), ('strTalkFile', 'S'), ('dwActFlag', 'I'),
             ('strSale0', 'S'), ('strSale1', 'S'), ('strSale2', 'S'), ('bAfterFall_NoBody', 'I'),
             ('strBirthEffect', 'S'), ('strFallingEffect', 'S'), ('strBlowEffect', 'S')]
_ACT_MOVE = [('emMoveType', 'I'), ('fDriftHeight', 'f'), ('fWalkVelo', 'f'), ('bRun', 'I'),
             ('fRunVelo', 'f'), ('fLiveTime', 'f'), ('fActionDNRate', 'f'), ('bMobLink', 'I'),
             ('sMobLinkID', 'I')]
ACTION_SCHEMA = {
    0x106: _ACT_HEAD + [('emActionUP', 'I'), ('emActionDN', 'I')] + _ACT_MOVE,
    0x107: _ACT_HEAD + _ACT_MOVE + [('patterns', 'P')],
    0x108: _ACT_HEAD + _ACT_MOVE + [('fMobLinkScale', 'f'), ('fMobLinkDelay', 'f'), ('patterns', 'P')],
}
GEN_SCHEMA = {0x102: [('dwGenMoney', 'I'), ('sGenItemID', 'I'), ('wGenMoney_Rate', 'H'),
                      ('wGenItem_Rate', 'H'), ('strGenItem', 'S'), ('strQtGenItem', 'S')]}
_ATT_ONE = [('bUsed', 'I'), ('emAttRgType', 'I'), ('wRange', 'H'), ('sDamage', 'I'), ('fDelay', 'f'),
            ('wUse_SP', 'H'), ('strAniFile', 'S'), ('sAniAttack', ('raw', SANIATTACK_SIZE)),
            ('skill_id', 'I'), ('skill_lev', 'H'), ('skill_tar', 'I'), ('emBLOW_TYPE', 'I'),
            ('fBLOW_RATE', 'f'), ('fBLOW_LIFE', 'f'), ('fBLOW_VAR1', 'f'), ('fBLOW_VAR2', 'f'),
            ('strSelfBodyEffect', 'S'), ('strTargBodyEffect', 'S'), ('strTargetEffect', 'S')]
ATTACK_SCHEMA = {0x105: [(f'{k}{i}', t) for i in range(3) for k, t in _ATT_ONE]}


def read_fields(buf, o, schema):
    """Parse fields like the C++ LOAD; returns (dict, new offset). Strings keep their raw bytes
    (the loader keeps only up to the first NUL; we keep it all for a byte-identical rewrite)."""
    out = {}
    for name, kind in schema:
        if kind == 'H':
            out[name] = struct.unpack_from('<H', buf, o)[0]; o += 2
        elif kind == 'I':
            out[name] = struct.unpack_from('<I', buf, o)[0]; o += 4
        elif kind == 'f':
            out[name] = struct.unpack_from('<f', buf, o)[0]; o += 4
        elif kind == 'S':
            n = struct.unpack_from('<I', buf, o)[0]
            out[name] = bytes(buf[o + 4:o + 4 + n]); o += 4 + n
        elif kind == 'P':
            n = struct.unpack_from('<I', buf, o)[0]
            out[name] = [bytes(buf[o + 4 + k * PATTERN_SIZE:o + 4 + (k + 1) * PATTERN_SIZE]) for k in range(n)]
            o += 4 + n * PATTERN_SIZE
        else:
            n = kind[1]
            out[name] = bytes(buf[o:o + n]); o += n
    return out, o


def write_fields(vals, schema):
    out = b''
    for name, kind in schema:
        v = vals[name]
        if kind == 'H':
            out += struct.pack('<H', v)
        elif kind == 'I':
            out += struct.pack('<I', v)
        elif kind == 'f':
            out += struct.pack('<f', v)
        elif kind == 'S':
            out += struct.pack('<I', len(v)) + v
        elif kind == 'P':
            out += struct.pack('<I', len(v)) + b''.join(v)
        else:
            assert len(v) == kind[1]
            out += v
    return out


def cstr(s):
    """Python str -> std::string payload (bytes incl. the NUL)."""
    return (s.encode('cp949') if isinstance(s, str) else s) + b'\0'


def pystr(b):
    return b.split(b'\0')[0].decode('cp949', 'replace')


SCHEMAS = {FILE_SACTION: ACTION_SCHEMA, FILE_SGEN: GEN_SCHEMA, FILE_SATTACK: ATTACK_SCHEMA}


class Chunk:
    """type, ver, the size field as stored, and the data bytes the loader consumes."""

    def __init__(self, typ, ver, size, data):
        self.type, self.ver, self.size, self.data = typ, ver, size, data

    def fields(self):
        if self.type == FILE_SBASIC:
            raise ValueError('SBASIC is a raw struct; use Crow.basic helpers')
        v, o = read_fields(self.data, 0, SCHEMAS[self.type][self.ver])
        assert o == len(self.data)
        return v

    def set_fields(self, vals):
        self.data = write_fields(vals, SCHEMAS[self.type][self.ver])
        if self.type != FILE_SGEN:           # SGEN's size field is sizeof(SCROWGEN); keep it
            self.size = len(self.data)

    def serialize(self):
        return struct.pack('<III', self.type, self.ver, self.size) + self.data


class Crow:
    def __init__(self, version, chunks):
        self.version, self.chunks = version, chunks

    def chunk(self, t):
        return next(c for c in self.chunks if c.type == t)

    # SBASIC ------------------------------------------------------------------------------
    @property
    def nid(self):
        n = struct.unpack_from('<I', self.chunk(FILE_SBASIC).data, 0)[0]
        return (n & 0xFFFF, n >> 16)

    @property
    def name(self):
        return self.chunk(FILE_SBASIC).data[BASIC_NAME:BASIC_NAME + 33].split(b'\0')[0].decode('latin1')

    @property
    def crow_type(self):
        return struct.unpack_from('<I', self.chunk(FILE_SBASIC).data, BASIC_CROW)[0]

    def set_basic(self, nid=None, crow_type=None, level=None, hp=None):
        c = self.chunk(FILE_SBASIC)
        d = bytearray(c.data)
        if nid is not None:
            struct.pack_into('<I', d, 0, nid[0] | (nid[1] << 16))
            # SCROWDATA::SaveFile always rewrites the name as CN_mmm_sss (the string-table key)
            d[BASIC_NAME:BASIC_NAME + 33] = ('CN_%03d_%03d' % nid).encode().ljust(33, b'\0')
        if crow_type is not None:
            struct.pack_into('<I', d, BASIC_CROW, crow_type)
        if level is not None:
            struct.pack_into('<H', d, BASIC_LEVEL, level)
        if hp is not None:
            struct.pack_into('<I', d, BASIC_HP, hp)
        c.data = bytes(d)

    # SACTION -----------------------------------------------------------------------------
    @property
    def skin(self):
        return pystr(self.chunk(FILE_SACTION).fields()['strSkinObj'])

    def set_action(self, **kw):
        c = self.chunk(FILE_SACTION)
        v = c.fields()
        for k, val in kw.items():
            assert k in v, k
            v[k] = cstr(val) if k.startswith('str') else val
        c.set_fields(v)

    # SATTACK -----------------------------------------------------------------------------
    def attacks(self):
        v = self.chunk(FILE_SATTACK).fields()
        return [{k[:-1]: val for k, val in v.items() if k.endswith(str(i))} for i in range(3)]

    def set_attack(self, i, **kw):
        c = self.chunk(FILE_SATTACK)
        v = c.fields()
        for k, val in kw.items():
            key = f'{k}{i}'
            assert key in v, key
            v[key] = cstr(val) if k.startswith('str') else val
        c.set_fields(v)

    def serialize(self):
        return struct.pack('<I', self.version) + b''.join(c.serialize() for c in self.chunks) + \
            struct.pack('<I', FILE_END_DATA)


def pack_aniattack(stime, etime, unittime, flags, main, sub, strikes):
    """SANIATTACK (raw 44 bytes): what SANIATTACK::Assign(SANIMCONINFO) produces. The server hits
    at each m_wDivFrame tick (GLCrow::PreAttackProc) and the attack lasts etime - stime ticks."""
    div = list(strikes)[:ACF_DIV] + [0] * (ACF_DIV - len(strikes))
    return struct.pack(ANIATTACK_FMT, stime, etime, unittime, flags, main, sub, min(len(strikes), ACF_DIV), *div)


def unpack_aniattack(b):
    s, e, u, fl, m, sb, n, *div = struct.unpack(ANIATTACK_FMT, b)
    return dict(stime=s, etime=e, unittime=u, flags=fl, main=m, sub=sb, strikes=div[:n])


def parse(raw):
    head = raw[:132]
    body = raw[132:].translate(E.DECODE)
    ver, num = struct.unpack_from('<II', body, 0)
    o, crows = 8, []
    for _ in range(num):
        version = struct.unpack_from('<I', body, o)[0]; o += 4
        chunks = []
        while True:
            t = struct.unpack_from('<I', body, o)[0]; o += 4
            if t == FILE_END_DATA:
                break
            v, size = struct.unpack_from('<II', body, o); o += 8
            if t == FILE_SBASIC:
                n = size
            else:
                _, end = read_fields(body, o, SCHEMAS[t][v])
                n = end - o
            chunks.append(Chunk(t, v, size, body[o:o + n])); o += n
        crows.append(Crow(version, chunks))
    return head, ver, crows, body[o:]


def serialize(head, ver, crows, tail=b''):
    body = struct.pack('<II', ver, len(crows)) + b''.join(c.serialize() for c in crows) + tail
    return head + body.translate(E.COMPBYTE)


def clone(crow, nid):
    c = copy.deepcopy(crow)
    c.set_basic(nid=nid)
    return c


def put(crows, crow):
    """Insert/replace by native id, keeping the file sorted by (mid, sid) like GLCrowDataMan::SaveFile."""
    crows[:] = [c for c in crows if c.nid != crow.nid] + [crow]
    crows.sort(key=lambda c: c.nid)


# ---- CrowStrTable.txt ----------------------------------------------------------------------
def read_strtable(raw):
    """-> (4-byte prefix, plain text bytes without the NUL padding, padding length)."""
    plain = aes(raw[4:], True)
    text = plain.rstrip(b'\0')
    return raw[:4], text, len(plain) - len(text)


def write_strtable(prefix, text, pad=None):
    if pad is None:
        pad = -len(text) % 16
    plain = text + b'\0' * pad
    assert len(plain) % 16 == 0
    return prefix + aes(plain, False)


def set_names(text, entries):
    """entries: {(mid, sid): (name, description)}. Replaces existing CN_/CD_ lines for those ids
    and appends new ones at the end (the loader reads key/value lines in any order)."""
    lines = text.split(b'\r\n')
    keys = {b'%03d_%03d' % k for k in entries}
    lines = [ln for ln in lines if not (ln[:2] in (b'CN', b'CD') and ln[3:10] in keys)]
    while lines and lines[-1] == b'':
        lines.pop()
    for (mid, sid), (name, desc) in sorted(entries.items()):
        lines.append(b'CN_%03d_%03d\t' % (mid, sid) + name.encode('cp949'))
        lines.append(b'CD_%03d_%03d\t' % (mid, sid) + desc.encode('cp949'))
    return b'\r\n'.join(lines) + b'\r\n'


def names(text):
    out = {}
    for ln in text.split(b'\r\n'):
        if ln.startswith(b'CN_') and b'\t' in ln:
            k, v = ln.split(b'\t', 1)
            out[(int(k[3:6]), int(k[7:10]))] = v.decode('cp949', 'replace').strip()
    return out


def selftest(rcc):
    import zipfile
    with zipfile.ZipFile(rcc) as z:
        raw, st = z.read('crow.mnsf'), z.read('CrowStrTable.txt')
    head, ver, crows, tail = parse(raw)
    assert serialize(head, ver, crows, tail) == raw, 'crow.mnsf round-trip differs'
    # re-encode every SACTION/SATTACK from fields too: must also be identical
    for c in crows:
        for ch in c.chunks:
            if ch.type != FILE_SBASIC:
                before = (ch.data, ch.size)
                ch.set_fields(ch.fields())
                assert (ch.data, ch.size) == before, (c.nid, ch.type, ch.ver)
    assert serialize(head, ver, crows, tail) == raw
    prefix, text, pad = read_strtable(st)
    assert write_strtable(prefix, text, pad) == st, 'CrowStrTable round-trip differs'
    vers = {}
    for c in crows:
        for ch in c.chunks:
            vers[(ch.type, ch.ver)] = vers.get((ch.type, ch.ver), 0) + 1
    mids = sorted({c.nid[0] for c in crows})
    print(f'{len(crows)} crows, file ver {ver:#x}, tail {len(tail)} B, chunk versions {vers}')
    print(f'main ids used: {mids[0]}..{mids[-1]}, free from {mids[-1] + 1}; gaps:',
          [m for m in range(mids[-1]) if m not in mids])
    print('round-trip identical: crow.mnsf and CrowStrTable.txt (also field re-encode)')
    return head, ver, crows, tail


if __name__ == '__main__':
    selftest(sys.argv[1])
