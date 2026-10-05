import json,struct,numpy as np
def load(path):
    d=open(path,'rb').read()
    L=struct.unpack('<I',d[12:16])[0]; j=json.loads(d[20:20+L])
    b0=20+L; BL=struct.unpack('<I',d[b0:b0+4])[0]; bin_=d[b0+8:b0+8+BL]
    def acc(i):
        a=j['accessors'][i]; bv=j['bufferViews'][a['bufferView']]
        comp={5126:'f',5125:'I',5123:'H',5121:'B'}[a['componentType']]
        n={'SCALAR':1,'VEC2':2,'VEC3':3,'VEC4':4}[a['type']]
        off=bv.get('byteOffset',0)+a.get('byteOffset',0); stride=bv.get('byteStride',0) or struct.calcsize(comp)*n
        out=[struct.unpack_from('<%d%s'%(n,comp),bin_,off+k*stride) for k in range(a['count'])]
        return np.array(out)
    if len(j['meshes'])>1 or any('matrix' in n or 'rotation' in n or 'scale' in n or 'translation' in n for n in j['nodes']):
        return _merge_world(j,acc,bin_)
    p=j['meshes'][0]['primitives'][0]
    pos=acc(p['attributes']['POSITION']); nor=acc(p['attributes']['NORMAL']); uv=acc(p['attributes']['TEXCOORD_0']); idx=acc(p['indices']).reshape(-1,3)
    img=j['images'][0]; bv=j['bufferViews'][img['bufferView']]; png=bin_[bv.get('byteOffset',0):bv.get('byteOffset',0)+bv['byteLength']]
    return pos,nor,uv,idx,png


def _local(n):
    if 'matrix' in n:
        return np.array(n['matrix']).reshape(4, 4).T          # glTF is column-major
    T = np.eye(4); R = np.eye(4); S = np.eye(4)
    if 'translation' in n: T[:3, 3] = n['translation']
    if 'rotation' in n:
        x, y, z, w = n['rotation']
        R[:3, :3] = [[1-2*(y*y+z*z), 2*(x*y-z*w), 2*(x*z+y*w)],
                     [2*(x*y+z*w), 1-2*(x*x+z*z), 2*(y*z-x*w)],
                     [2*(x*z-y*w), 2*(y*z+x*w), 1-2*(x*x+y*y)]]
    if 'scale' in n: S[:3, :3] = np.diag(n['scale'])
    return T @ R @ S


def _merge_world(j, acc, bin_):
    """All mesh instances of the default scene, transformed to world space, merged (one material)."""
    P, N, U, I = [], [], [], []
    base = 0
    def walk(i, parent):
        nonlocal base
        n = j['nodes'][i]
        W = parent @ _local(n)
        if 'mesh' in n:
            for p in j['meshes'][n['mesh']]['primitives']:
                pos = acc(p['attributes']['POSITION']); nor = acc(p['attributes']['NORMAL'])
                uv = acc(p['attributes']['TEXCOORD_0']); idx = acc(p['indices']).reshape(-1, 3)
                pw = (np.c_[pos, np.ones(len(pos))] @ W.T)[:, :3]
                nw = nor @ np.linalg.inv(W[:3, :3])                # inverse-transpose for normals
                nw /= np.linalg.norm(nw, axis=1, keepdims=True)
                if np.linalg.det(W[:3, :3]) < 0: idx = idx[:, [0, 2, 1]]
                P.append(pw); N.append(nw); U.append(uv); I.append(idx + base); base += len(pos)
        for c in n.get('children', []): walk(c, W)
    for r in j['scenes'][j.get('scene', 0)]['nodes']: walk(r, np.eye(4))
    img = j['images'][0]; bv = j['bufferViews'][img['bufferView']]
    png = bin_[bv.get('byteOffset', 0):bv.get('byteOffset', 0) + bv['byteLength']]
    return np.vstack(P), np.vstack(N), np.vstack(U), np.vstack(I), png
