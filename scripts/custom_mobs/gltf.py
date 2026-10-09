"""Minimal glTF 2.0 (.glb) reader for make_mob.py: accessors, node hierarchy, skins, animation
sampling (LINEAR / STEP / CUBICSPLINE), materials and embedded images. Matrices are glTF
column-vector 4x4 numpy arrays (p' = M @ p)."""
import json
import struct

import numpy as np

_COMP = {5120: np.int8, 5121: np.uint8, 5122: np.int16, 5123: np.uint16, 5125: np.uint32, 5126: np.float32}
_NCOMP = {'SCALAR': 1, 'VEC2': 2, 'VEC3': 3, 'VEC4': 4, 'MAT4': 16}
_NORM = {np.int8: 127.0, np.uint8: 255.0, np.int16: 32767.0, np.uint16: 65535.0}


def quat_to_m3(q):
    x, y, z, w = q / np.linalg.norm(q)
    return np.array([[1 - 2 * (y * y + z * z), 2 * (x * y - z * w), 2 * (x * z + y * w)],
                     [2 * (x * y + z * w), 1 - 2 * (x * x + z * z), 2 * (y * z - x * w)],
                     [2 * (x * z - y * w), 2 * (y * z + x * w), 1 - 2 * (x * x + y * y)]])


def trs(t, r, s):
    m = np.eye(4)
    m[:3, :3] = quat_to_m3(np.asarray(r, float)) @ np.diag(s)
    m[:3, 3] = t
    return m


def slerp(a, b, u):
    a, b = np.asarray(a, float), np.asarray(b, float)
    d = float(np.dot(a, b))
    if d < 0:
        b, d = -b, -d
    if d > 0.9995:
        q = a + u * (b - a)
    else:
        th = np.arccos(d)
        q = (np.sin((1 - u) * th) * a + np.sin(u * th) * b) / np.sin(th)
    return q / np.linalg.norm(q)


class GLTF:
    def __init__(self, path):
        d = open(path, 'rb').read()
        assert d[:4] == b'glTF', 'not a .glb'
        n = struct.unpack_from('<I', d, 12)[0]
        self.j = json.loads(d[20:20 + n])
        b0 = 20 + n
        self.bin = d[b0 + 8:b0 + 8 + struct.unpack_from('<I', d, b0)[0]] if b0 < len(d) else b''
        self.nodes = self.j['nodes']
        self.parent = {c: i for i, nd in enumerate(self.nodes) for c in nd.get('children', [])}
        self._acc = {}

    def acc(self, i):
        if i in self._acc:
            return self._acc[i]
        a = self.j['accessors'][i]
        dt = _COMP[a['componentType']]
        k = _NCOMP[a['type']]
        cnt = a['count']
        if 'bufferView' in a:
            bv = self.j['bufferViews'][a['bufferView']]
            off = bv.get('byteOffset', 0) + a.get('byteOffset', 0)
            item = np.dtype(dt).itemsize * k
            stride = bv.get('byteStride', 0) or item
            raw = np.frombuffer(self.bin, np.uint8, count=stride * (cnt - 1) + item, offset=off)
            rows = np.lib.stride_tricks.as_strided(raw, (cnt, item), (stride, 1))
            out = np.ascontiguousarray(rows).view(dt).reshape(cnt, k).astype(np.float64)
        else:
            out = np.zeros((cnt, k))
        if 'sparse' in a:
            sp = a['sparse']
            iv = self.j['bufferViews'][sp['indices']['bufferView']]
            idx = np.frombuffer(self.bin, _COMP[sp['indices']['componentType']], sp['count'],
                                iv.get('byteOffset', 0) + sp['indices'].get('byteOffset', 0))
            vv = self.j['bufferViews'][sp['values']['bufferView']]
            val = np.frombuffer(self.bin, dt, sp['count'] * k,
                                vv.get('byteOffset', 0) + sp['values'].get('byteOffset', 0)).reshape(-1, k)
            out[idx.astype(int)] = val
        if a.get('normalized') and dt in _NORM:
            out = out / _NORM[dt]
        self._acc[i] = out
        return out

    # ---- transforms -------------------------------------------------------------------------
    def rest_trs(self, i):
        nd = self.nodes[i]
        if 'matrix' in nd:
            return np.array(nd['matrix'], float).reshape(4, 4).T
        return (np.array(nd.get('translation', [0, 0, 0]), float), np.array(nd.get('rotation', [0, 0, 0, 1]), float),
                np.array(nd.get('scale', [1, 1, 1]), float))

    def locals_at(self, anim=None, t=0.0):
        """{node: local 4x4} at time t of animation index `anim` (None = rest pose)."""
        loc = {}
        over = {}
        if anim is not None:
            a = self.j['animations'][anim]
            for ch in a['channels']:
                tgt = ch['target']
                if 'node' not in tgt or tgt['path'] not in ('translation', 'rotation', 'scale'):
                    continue
                smp = a['samplers'][ch['sampler']]
                over.setdefault(tgt['node'], {})[tgt['path']] = self.sample(smp, tgt['path'], t)
        for i in range(len(self.nodes)):
            r = self.rest_trs(i)
            if isinstance(r, np.ndarray):
                loc[i] = r          # matrix nodes are not animatable
                continue
            tt, rr, ss = r
            o = over.get(i, {})
            loc[i] = trs(o.get('translation', tt), o.get('rotation', rr), o.get('scale', ss))
        return loc

    def sample(self, smp, path, t):
        times = self.acc(smp['input'])[:, 0]
        vals = self.acc(smp['output'])
        interp = smp.get('interpolation', 'LINEAR')
        if interp == 'CUBICSPLINE':
            vals = vals.reshape(len(times), 3, -1)[:, 1]          # keep the value, drop tangents
        if len(times) == 1 or t <= times[0]:
            v = vals[0]
        elif t >= times[-1]:
            v = vals[-1]
        else:
            k = int(np.searchsorted(times, t, side='right')) - 1
            u = (t - times[k]) / (times[k + 1] - times[k])
            if interp == 'STEP':
                v = vals[k]
            elif path == 'rotation':
                v = slerp(vals[k], vals[k + 1], u)
            else:
                v = vals[k] * (1 - u) + vals[k + 1] * u
        return np.array(v, float)

    def worlds(self, loc):
        out = {}

        def w(i):
            if i not in out:
                out[i] = loc[i] if i not in self.parent else w(self.parent[i]) @ loc[i]
            return out[i]
        for i in range(len(self.nodes)):
            w(i)
        return out

    def scene_nodes(self):
        """Nodes reachable from the default scene."""
        seen = []
        stack = list(self.j['scenes'][self.j.get('scene', 0)]['nodes'])
        while stack:
            i = stack.pop()
            seen.append(i)
            stack += self.nodes[i].get('children', [])
        return seen

    def anim_index(self, name):
        for k, a in enumerate(self.j.get('animations', [])):
            if a.get('name') == name:
                return k
        raise KeyError(f'no animation {name!r}; have {[a.get("name") for a in self.j.get("animations", [])]}')

    def anim_range(self, k):
        a = self.j['animations'][k]
        ts = [self.acc(s['input'])[:, 0] for s in a['samplers']]
        return min(float(t.min()) for t in ts), max(float(t.max()) for t in ts)

    # ---- materials / images -----------------------------------------------------------------
    def material_image(self, mi):
        """(png/jpg bytes or None, rgba factor, alpha mode) of material mi's base color."""
        if mi is None:
            return None, (1, 1, 1, 1), 'OPAQUE'
        m = self.j['materials'][mi]
        pbr = m.get('pbrMetallicRoughness', {})
        sg = m.get('extensions', {}).get('KHR_materials_pbrSpecularGlossiness', {})
        tex = pbr.get('baseColorTexture') or sg.get('diffuseTexture')
        factor = tuple(pbr.get('baseColorFactor') or sg.get('diffuseFactor') or (1, 1, 1, 1))
        img = None
        if tex is not None:
            src = self.j['textures'][tex['index']].get('source')
            if src is not None:
                im = self.j['images'][src]
                bv = self.j['bufferViews'][im['bufferView']]
                img = self.bin[bv.get('byteOffset', 0):bv.get('byteOffset', 0) + bv['byteLength']]
        return img, factor, m.get('alphaMode', 'OPAQUE')
