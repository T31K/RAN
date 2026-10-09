"""Readers + writers for the skin chain of a RAN crow. Each reader walks the bytes exactly like the
C++ loader named next to it, so parsing a stock file with it proves the reader (and the writer
that mirrors it) right.

  .chf  DxSkinCharData::LOAD_Ver101 / DxSkinDataDummy::LOAD_0101   (data/skinobject/skinobject.rcc)
  .cps  DxSkinPiece::LoadPiece (ver 0x105..0x107)                    (data/skinobject/skinobject.rcc)
  .cfg  SANIMCONINFO::LoadFile (ver 0x105..0x107)                    (data/animation/animation.rcc)
  .bin  SAnimContainer::LoadAnimFromFile + SAnimation::LoadFromFile  (data/animation/, loose)
  .x    text DirectX files: skeleton (frames only) and skin (frames + skinned mesh)

Every file starts with a 128-byte type string + DWORD (CSerialFile file type): "default" + 0 for
.chf/.cps, "SANIMCONINFO" + version for .cfg, "AnimContainer" + version for .bin.
std::string = DWORD (len+1) + bytes + NUL. BOOL = 4 bytes.
"""
import re
import struct

import numpy as np

FILETYPESIZE = 128
PIECE_SIZE = 13
ACF_SZNAME, ACF_DIV, ACF_STRIKE = 128, 8, 9
ACF_LOOP = 0x10
SOUND_SIZE = 388            # sizeof(SChaSoundData): int + 10*char[32] + 10*BOOL + 11*WORD, padded
SOUND_SIZE_103 = 428        # SChaSoundData_103 (cfg 0x105): + WORD[10] buffer counts + WORD[10] types
PIECE_RHAND, EMSF_TARGET = 4, 2   # SANIMSTRIKE defaults
TICKS_PER_SEC = 4800        # UNITANIKEY_PERSEC
UNITTIME = 160              # ticks per 30 fps frame
# EMANI_MAINTYPE
AN_GUARD_N, AN_PLACID, AN_WALK, AN_RUN, AN_ATTACK, AN_SHOCK, AN_DIE = 0, 1, 2, 3, 4, 5, 6


class Reader:
    def __init__(self, data, o=0):
        self.d, self.o = data, o

    def u32(self):
        v = struct.unpack_from('<I', self.d, self.o)[0]; self.o += 4; return v

    def u16(self):
        v = struct.unpack_from('<H', self.d, self.o)[0]; self.o += 2; return v

    def f32(self):
        v = struct.unpack_from('<f', self.d, self.o)[0]; self.o += 4; return v

    def raw(self, n):
        assert self.o + n <= len(self.d), 'read past the end'
        v = self.d[self.o:self.o + n]; self.o += n; return v

    def string(self):
        n = self.u32()
        return self.raw(n).split(b'\0')[0].decode('cp949')

    def end(self):
        assert self.o == len(self.d), f'{len(self.d) - self.o} trailing bytes'


def s(text):
    b = text.encode('cp949') + b'\0'
    return struct.pack('<I', len(b)) + b


def header(typ, ver):
    return typ.encode().ljust(FILETYPESIZE, b'\0') + struct.pack('<I', ver)


def read_header(data):
    return data[:FILETYPESIZE].split(b'\0')[0].decode('latin1'), struct.unpack_from('<I', data, FILETYPESIZE)[0]


def fixed(text, n=ACF_SZNAME):
    b = text.encode('cp949')
    assert len(b) < n
    return b.ljust(n, b'\0')


# ---- .chf -------------------------------------------------------------------------------------
def write_chf(pieces, skeleton, anims, bbox_max, bbox_min, scale=1.0, world_obj=False, height=20.0,
              radius=7.0):
    """pieces: {slot: 'x.cps'}; anims: ['a_x.x', ...]. Version 0x102 (DxSkinCharData::SaveFile)."""
    out = header('default', 0) + struct.pack('<If', 0x102, scale) + struct.pack('<I', PIECE_SIZE)
    for i in range(PIECE_SIZE):
        out += struct.pack('<I', i in pieces) + (s(pieces[i]) if i in pieces else b'')
    out += s(skeleton) + struct.pack('<I', len(anims)) + b''.join(s(a) for a in anims)
    out += struct.pack('<I3f3f', 1, *bbox_max, *bbox_min)
    out += struct.pack('<II', 1, int(world_obj))
    out += struct.pack('<Iff', 1, height, radius)
    out += struct.pack('<I', 0)
    return out


def read_chf(data):
    typ, _ = read_header(data)
    r = Reader(data, FILETYPESIZE + 4)
    ver = r.u32()
    assert ver in (0x101, 0x102), hex(ver)            # LoadFile: VERSION / 0x0101 -> LOAD_Ver101
    out = dict(type=typ, version=ver, scale=r.f32())
    n = r.u32()
    assert n <= PIECE_SIZE
    out['pieces'] = {}
    for i in range(n):
        if r.u32():
            out['pieces'][i] = r.string()
    out['skeleton'] = r.string()
    out['anims'] = [r.string() for _ in range(r.u32())]
    if r.u32():
        out['bbox_max'] = (r.f32(), r.f32(), r.f32())
        out['bbox_min'] = (r.f32(), r.f32(), r.f32())
    if r.u32():
        out['world_obj'] = r.u32()
        if r.u32():
            out['height'], out['radius'] = r.f32(), r.f32()
            out['dump'] = r.u32()
    r.end()
    return out


# ---- .cps -------------------------------------------------------------------------------------
def write_cps(xfile, skeleton, mesh_name, textures, piece_type=0):
    """DxSkinPiece::SavePiece, version 0x107: one SMATERIAL_PIECE (ver 0x100) per mesh material,
    no trace points, no effects."""
    out = header('default', 0) + struct.pack('<II', 0x107, 0)
    for name in (xfile, skeleton):
        b = name.encode() + b'\0'
        out += struct.pack('<I', len(b)) + b
    out += struct.pack('<II', piece_type, 0)
    b = mesh_name.encode() + b'\0'
    out += struct.pack('<I', len(b)) + b
    out += struct.pack('<I', len(textures))
    for tex in textures:
        t = tex.encode() + b'\0'
        block = struct.pack('<I', len(t)) + t + struct.pack('<II', 0, 0)
        out += struct.pack('<II', 0x100, len(block)) + block
    out += struct.pack('<II', 0, 0)     # trace points, effects
    out += struct.pack('<I', 0)         # m_dwFlag
    return out


def read_cps(data, allow_effects=False):
    typ, _ = read_header(data)
    r = Reader(data, FILETYPESIZE + 4)
    ver = r.u32()
    assert ver in (0x105, 0x106, 0x107), hex(ver)
    out = dict(type=typ, version=ver, ref=r.u32())
    out['xfile'] = r.raw(r.u32()).split(b'\0')[0].decode()
    out['skeleton'] = r.raw(r.u32()).split(b'\0')[0].decode()
    out['piece_type'] = r.u32()
    out['weapon_back'] = r.u32() if ver >= 0x106 else 0      # added in 0x106
    out['mesh'] = r.raw(r.u32()).split(b'\0')[0].decode()
    out['materials'] = []
    for _ in range(r.u32()):
        mver, block = r.u32(), r.u32()
        start = r.o
        if mver == 0x100:
            tex = r.raw(r.u32()).split(b'\0')[0].decode('cp949')
            out['materials'].append(dict(texture=tex, flags=r.u32(), zbias=r.u32()))
        assert r.o - start == block, 'material block size'
        r.o = start + block
    out['traces'] = []
    for _ in range(r.u32()):
        out['traces'].append(r.string())
        # SVERTEXINFLU::LoadFile: DWORD ver, DWORD size (not the bytes that follow), DWORD index,
        # vec3 vector, vec3 normal, DWORD nbone, DWORD[nbone] bones, float[nbone] weights
        r.u32(); r.u32(); r.u32(); r.raw(24)
        nb = r.u32()
        if nb != 0xFFFFFFFF:            # COL_ERR = no bones
            r.raw(8 * nb)
    out['effects'] = r.u32()
    if out['effects'] and allow_effects:
        return out                      # effect bodies are per-type; stop here
    assert out['effects'] == 0, 'effects present'
    out['flag'] = r.u32()
    r.end()
    return out


# ---- .cfg -------------------------------------------------------------------------------------
def write_cfg(anim, skeleton, end_time, main, sub=0, loop=False, strikes=(), version=0x107):
    """SANIMCONINFO::SaveFile. Times in ticks (160 per 30 fps frame); end_time = the last key's
    tick. Like the stock files: m_dwETimeOrig = last key, m_dwETime = last key - 160 (a looped
    clip whose last frame repeats the first plays seamlessly). strikes: tick list (<= 9), each
    SANIMSTRIKE = (piece, effect, tick); unused slots keep the constructor's (RHAND, TARGET, 0)."""
    out = header('SANIMCONINFO', version) + fixed(anim) + fixed(skeleton)
    out += struct.pack('<5I', ACF_LOOP if loop else 0, 0, max(end_time - UNITTIME, 0), end_time, UNITTIME)
    out += struct.pack('<II', main, sub)
    out += struct.pack('<H', 0) + b'\0' * (2 * ACF_DIV)
    st = list(strikes)[:ACF_STRIKE]
    out += struct.pack('<H', len(st))
    out += b''.join(struct.pack('<III', 0, 0, t) for t in st)      # as the stock mob cfgs
    out += b''.join(struct.pack('<III', PIECE_RHAND, EMSF_TARGET, 0) for _ in range(ACF_STRIKE - len(st)))
    out += b'\0' * SOUND_SIZE
    out += struct.pack('<I', 0)                     # effect list
    if version >= 0x107:
        out += struct.pack('<I', 0)                 # DxAniScale: no entries
    return out


def read_cfg(data, allow_effects=False):
    typ, ver = read_header(data)
    assert typ == 'SANIMCONINFO' and ver in (0x105, 0x106, 0x107), (typ, hex(ver))
    r = Reader(data, FILETYPESIZE + 4)
    out = dict(version=ver, name=r.raw(ACF_SZNAME).split(b'\0')[0].decode('cp949'),
               skeleton=r.raw(ACF_SZNAME).split(b'\0')[0].decode('cp949'))
    out['flag'], out['stime'], out['etime'], out['etime_orig'], out['unittime'] = (r.u32() for _ in range(5))
    out['main'], out['sub'] = r.u32(), r.u32()
    out['div'] = r.u16(); r.raw(2 * ACF_DIV)
    n = r.u16()
    strikes = [struct.unpack('<III', r.raw(12)) for _ in range(ACF_STRIKE)]
    out['strikes'] = [st[2] for st in strikes[:n]]
    out['strike_raw'] = strikes[:n]
    r.raw(SOUND_SIZE if ver >= 0x106 else SOUND_SIZE_103)
    out['effects'] = r.u32()
    if out['effects']:
        assert allow_effects, 'effects present'
        return out
    if ver >= 0x107:
        out['aniscale'] = r.u32()
        assert out['aniscale'] == 0
    r.end()
    return out


# ---- .bin -------------------------------------------------------------------------------------
KEY_SIZE = 80     # SMatrixKey: DWORD time + 12 pad (D3DXMATRIXA16 is 16-aligned) + 64-byte matrix


def write_bin(tracks):
    """tracks: [(bone, [(tick, 4x4 row-vector local matrix), ...]), ...]. Container 0x100,
    SAnimation 0x100 with matrix keys only (ConvertMatToQuat builds the quaternion keys)."""
    out = header('AnimContainer', 0x100) + struct.pack('<I', len(tracks))
    for bone, keys in tracks:
        out += struct.pack('<IIIII', 0x100, 0, 0, 0, len(keys))
        for t, m in keys:
            out += struct.pack('<I', t) + b'\0' * 12 + np.asarray(m, '<f4').reshape(16).tobytes()
        b = bone.encode() + b'\0'
        out += struct.pack('<I', len(b)) + b
    return out


def _read_track(r):
    ver = r.u32()
    tr = dict(version=ver)
    if ver == 0x100:
        tr['pos'] = r.u32(); r.raw(16 * tr['pos'])
        tr['rot'] = r.u32(); r.raw(20 * tr['rot'])
        tr['scl'] = r.u32(); r.raw(16 * tr['scl'])
        n = r.u32()
        tr['keys'] = [(struct.unpack('<I', k[:4])[0], np.frombuffer(k[16:], '<f4').reshape(4, 4))
                      for k in (r.raw(KEY_SIZE) for _ in range(n))]
        tr['bone'] = r.string()
    elif ver == 0x101:
        tr['pos'] = r.u32(); r.raw(16 * tr['pos'])
        tr['rot'] = r.u32(); r.raw(20 * tr['rot'])
        tr['scl'] = r.u32(); r.raw(16 * tr['scl'])
        n = r.u32()
        tr['keys'] = [(struct.unpack('<I', k[:4])[0], np.frombuffer(k[16:], '<f4').reshape(4, 4))
                      for k in (r.raw(KEY_SIZE) for _ in range(n))]
        tr['quat'] = r.u32(); r.raw(36 * tr['quat'])   # SQuatPosKey: DWORD + 2 vec3 + QUATCOMP(8)
        tr['bone'] = r.string()
    elif ver == 0x102:
        tr['bone'] = r.string()
        tr['rot'] = r.u32(); r.raw(20 * tr['rot'])
        tr['pos'] = r.u32(); r.raw(16 * tr['pos'])
        r.u32()
        tr['quat'] = r.u32(); r.raw(36 * tr['quat'])
        r.u32()
        n = r.u32()
        tr['keys'] = [(struct.unpack('<I', k[:4])[0], np.frombuffer(k[16:], '<f4').reshape(4, 4))
                      for k in (r.raw(KEY_SIZE) for _ in range(n))]
        tr['scl'] = r.u32(); r.raw(16 * tr['scl'])
    else:
        raise ValueError(hex(ver))
    return tr


def read_bin(data):
    typ, ver = read_header(data)
    assert typ == 'AnimContainer', typ
    r = Reader(data, FILETYPESIZE + 4)
    if ver == 0x100:
        lists = [[_read_track(r) for _ in range(r.u32())]]
    elif ver in (0x101, 0x102):
        lists = [[_read_track(r) for _ in range(r.u32())] for _ in range(2)]
        if ver == 0x102:      # Load(): UPBODY list first, then the main list
            lists = lists[::-1]
    else:
        raise ValueError(hex(ver))
    r.end()
    return dict(version=ver, tracks=lists[0], upbody=lists[1] if len(lists) > 1 else [])


# ---- text .x ----------------------------------------------------------------------------------
def fmt_m(m):
    return ','.join('%.6f' % x for x in np.asarray(m).reshape(16)) + ';;'


X_NAME = re.compile(r'^[A-Za-z_][A-Za-z0-9_]*$')

# The skin templates, declared in the file like the stock (binary) skins do, so the file also parses
# with a reader that only registered D3DRM_XTEMPLATES (D3DXLoadMeshHierarchyFromX registers these
# itself; the same GUID/layout registered twice is fine).
SKIN_TEMPLATES = '''
template XSkinMeshHeader {
 <3cf169ce-ff7c-44ab-93c0-f78f62d172e2>
 WORD nMaxSkinWeightsPerVertex;
 WORD nMaxSkinWeightsPerFace;
 WORD nBones;
}

template SkinWeights {
 <6f0d123b-bad2-4167-a0d0-80224f25fabb>
 STRING transformNodeName;
 DWORD nWeights;
 array DWORD vertexIndices[nWeights];
 array FLOAT weights[nWeights];
 Matrix4x4 matrixOffset;
}
'''


def write_frames(L, bone, kids, local, ind):
    """bone hierarchy as nested Frames with FrameTransformMatrix (row-vector local)."""
    L.append(f'{ind}Frame {bone} {{')
    L.append(f'{ind} FrameTransformMatrix {{ {fmt_m(local[bone])} }}')
    for k in kids.get(bone, []):
        write_frames(L, k, kids, local, ind + ' ')
    L.append(f'{ind}}}')


def write_skeleton_x(roots, kids, local):
    for b in local:
        assert X_NAME.match(b), b
    L = ['xof 0303txt 0032', '']
    for r in roots:
        write_frames(L, r, kids, local, '')
    return '\r\n'.join(L) + '\r\n'


def write_skin_x(roots, kids, local, mesh_frame, v, n, uv, idx, mat_ids, textures, weights, offsets):
    """Skin .x: the bone frames (as in stock skins) + Frame <mesh_frame> { Frame { Mesh } } so the
    mesh container is named '<mesh_frame>[Mesh]' (DxSkinMesh9::SetupNameOnMeshContainer).
    weights: {bone: {vertex: w}}, offsets: {bone: 4x4 row-vector mesh->bone matrix}."""
    assert X_NAME.match(mesh_frame)
    ident = fmt_m(np.eye(4))
    L = ['xof 0303txt 0032', SKIN_TEMPLATES]
    # ONE top-level frame holding the bones and the mesh frame, like stock skins: the engine names
    # an unnamed mesh frame after its parent, but SFrame::FindParentFrame only searches downward
    # from the first top-level frame - a mesh frame that is a top-level sibling stays unnamed and
    # DxSkinMesh9::FindMeshContainer crashes on its NULL name (checked by check_mesh_names.py).
    L += ['Frame Scene_Root {', f' FrameTransformMatrix {{ {ident} }}']
    for r in roots:
        write_frames(L, r, kids, local, ' ')
    L += [f'Frame {mesh_frame} {{', f' FrameTransformMatrix {{ {ident} }}', ' Frame {',
          f'  FrameTransformMatrix {{ {ident} }}', '  Mesh {']
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
    L.append(f'    {len(textures)};\n    {len(idx)};')
    L.append(',\n'.join('    %d' % m for m in mat_ids) + ';')
    for tex in textures:
        L.append('    Material {')
        L.append('     1.000000;1.000000;1.000000;1.000000;;')
        L.append('     0.000000;')
        L.append('     1.000000;1.000000;1.000000;;')
        L.append('     0.000000;0.000000;0.000000;;')
        L.append(f'     TextureFilename {{ "{tex}"; }}')
        L.append('    }')
    L.append('   }')
    per_vertex = np.zeros(len(v), int)
    for w in weights.values():
        per_vertex[list(w)] += 1
    mx = int(per_vertex.max())
    L.append(f'   XSkinMeshHeader {{ {mx}; {min(3 * mx, 12)}; {len(weights)}; }}')
    for bone, w in weights.items():
        ids = sorted(w)
        L.append('   SkinWeights {')
        L.append(f'    "{bone}";')
        L.append(f'    {len(ids)};')
        L.append('    ' + ','.join(str(i) for i in ids) + ';')
        L.append('    ' + ','.join('%.6f' % w[i] for i in ids) + ';')
        L.append(f'    {fmt_m(offsets[bone])}')
        L.append('   }')
    L += ['  }', ' }', '}', '}']      # Mesh, inner frame, mesh frame, Scene_Root
    return '\r\n'.join(L) + '\r\n'


# A small text .x reader (enough for the templates above), to re-parse what we wrote.
_TOK = re.compile(r'"[^"]*"|[{};,]|[^\s{};,"]+')


class XObj:
    def __init__(self, typ, name):
        self.typ, self.name, self.kids, self.vals = typ, name, [], []

    def find(self, typ):
        for k in self.kids:
            if k.typ == typ:
                yield k
            yield from k.find(typ)


def parse_text_x(text):
    assert text.startswith('xof 0303txt 0032'), 'not a text .x'
    toks = _TOK.findall(text[16:])
    root = XObj('root', None)
    stack, pend, i = [root], [], 0
    while i < len(toks):
        t = toks[i]
        if t == '{':
            if len(pend) and pend[-1][0] in '"' and pend[0] not in ('Frame', 'Mesh'):
                pass
            typ = pend[0] if pend else None
            name = pend[1] if len(pend) > 1 else None
            o = XObj(typ, name)
            stack[-1].kids.append(o)
            stack.append(o)
            pend = []
        elif t == '}':
            stack[-1].vals += pend
            stack.pop()
            pend = []
        elif t in ';,':
            if pend:
                stack[-1].vals += pend
                pend = []
        else:
            if t.startswith('"'):
                stack[-1].vals.append(t)
            elif re.match(r'^[-+0-9.eE]+$', t):
                stack[-1].vals.append(float(t))
            else:
                pend.append(t)
        i += 1
    assert len(stack) == 1, 'unbalanced braces'
    return root


def x_frames(obj, parent=None, out=None):
    """{frame name: (parent, 4x4 local)} over named frames."""
    out = {} if out is None else out
    for k in obj.kids:
        if k.typ == 'Frame':
            m = next(c for c in k.kids if c.typ == 'FrameTransformMatrix')
            assert len(m.vals) == 16, 'FrameTransformMatrix needs 16 floats'
            if k.name:
                assert k.name not in out, f'duplicate frame {k.name}'
                out[k.name] = (parent, np.array(m.vals, float).reshape(4, 4))
            x_frames(k, k.name or parent, out)
    return out


def read_skin_x(text):
    """Mirror of what DxSkinMesh9 needs: frames, the mesh (counts consistent), material list,
    skin weights per bone (normalised), offset matrices, mesh container name."""
    root = parse_text_x(text)
    frames = x_frames(root)
    meshes = list(root.find('Mesh'))
    assert len(meshes) == 1, 'one mesh per skin file'
    mesh = meshes[0]
    vals = mesh.vals
    nv = int(vals[0])
    v = np.array(vals[1:1 + 3 * nv], float).reshape(nv, 3)
    nf = int(vals[1 + 3 * nv])
    f = np.array(vals[2 + 3 * nv:], float).reshape(nf, 4)
    assert (f[:, 0] == 3).all() and f[:, 1:].max() < nv and f[:, 1:].min() >= 0
    nrm = next(mesh.find('MeshNormals')).vals
    assert int(nrm[0]) == nv
    tc = next(mesh.find('MeshTextureCoords')).vals
    assert int(tc[0]) == nv and len(tc) == 1 + 2 * nv
    ml = next(mesh.find('MeshMaterialList'))
    nmat, nfm = int(ml.vals[0]), int(ml.vals[1])
    ids = np.array(ml.vals[2:], int)
    assert nfm == nf and len(ids) == nf and ids.max() < nmat
    mats = list(ml.find('Material'))
    assert len(mats) == nmat
    texs = [next(m.find('TextureFilename')).vals[0].strip('"') for m in mats]
    hdr = next(mesh.find('XSkinMeshHeader')).vals
    sws = list(mesh.find('SkinWeights'))
    assert int(hdr[2]) == len(sws)
    wsum = np.zeros(nv)
    per = np.zeros(nv, int)
    weights, offsets = {}, {}
    for sw in sws:
        bone = sw.vals[0].strip('"')
        k = int(sw.vals[1])
        ii = np.array(sw.vals[2:2 + k], int)
        ww = np.array(sw.vals[2 + k:2 + 2 * k], float)
        m = np.array(sw.vals[2 + 2 * k:], float)
        assert len(m) == 16, 'offset matrix'
        assert bone not in weights, f'duplicate SkinWeights {bone}'
        wsum[ii] += ww
        per[ii] += 1
        weights[bone] = dict(zip(ii.tolist(), ww.tolist()))
        offsets[bone] = m.reshape(4, 4)
    assert per.max() <= int(hdr[0]), 'XSkinMeshHeader max weights per vertex'
    assert np.abs(wsum - 1).max() < 1e-3, 'weights do not sum to 1'
    # the mesh's frame chain: Frame <name> { Frame { Mesh } } -> container "<name>[Mesh]"
    holder = None
    for fr in root.find('Frame'):
        for k in fr.kids:
            if k.typ == 'Frame' and not k.name and any(c.typ == 'Mesh' for c in k.kids):
                holder = fr.name
    return dict(frames=frames, verts=v, faces=f[:, 1:].astype(int), mat_ids=ids, textures=texs,
                uv=np.array(tc[1:], float).reshape(nv, 2),
                weights=weights, offsets=offsets, max_per_vertex=int(hdr[0]),
                mesh_container=f'{holder}[Mesh]' if holder else None)


def read_skeleton_x(text):
    root = parse_text_x(text)
    assert not list(root.find('Mesh')), 'skeleton has a mesh'
    return x_frames(root)


def world_matrices(frames):
    """Row-vector world matrices (local * parent world) of {name: (parent, local)}."""
    out = {}

    def w(b):
        if b not in out:
            p, m = frames[b]
            out[b] = m if p is None else m @ w(p)
        return out[b]
    for b in frames:
        w(b)
    return out
