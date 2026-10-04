// D3DX mesh loading from .x files for the native build (plan Phase 3): D3DXLoadMeshFromX,
// D3DXLoadMeshFromXof, D3DXLoadMeshHierarchyFromX and D3DXFrameCalculateBoundingSphere.
// Built only on the public D3DX interfaces: ID3DXFile (xfile.cpp) to read the file, and
// D3DXCreateMeshFVF / D3DXCreateSkinInfoFVF (d3dx9_mesh*.cpp) to build the results.
// What D3DX does and this follows:
//   - polygons are triangulated as fans; a vertex used with different normals (MeshNormals has
//     its own face indices) is duplicated, duplicates appended after the original vertices;
//   - FVF = XYZ + NORMAL (if MeshNormals) + TEX1 (if MeshTextureCoords) + DIFFUSE (if
//     MeshVertexColors); D3DXMESH_32BIT is added when more than 65535 vertices are needed;
//   - faces are grouped by material (stable, vertices untouched) and the attribute table is set,
//     like OptimizeInplace(ATTRSORT | IGNOREVERTS); adjacency comes from GenerateAdjacency(0);
//   - materials: D3DXMATERIAL array with texture names stored in the same buffer, Ambient 0;
//   - skin weights: one bone per SkinWeights object, influences applied to duplicates too;
//   - D3DXLoadMeshFromX merges every mesh in the file, transformed by its frames' matrices;
//   - animation sets are skipped (the client animates with its own loader), so the
//     animation controller is always NULL.
#include "ran_compat.h"
#include <d3dx9.h>
#include <rmxfguid.h>
#include <algorithm>
#include <cmath>
#include <cstdio>
#include <cstring>
#include <map>
#include <string>
#include <vector>

namespace ran_d3dx_templates {
#include "../../Dependency/directx/rmxftmpl.h"   // D3DRM_XTEMPLATES (defined here, so namespaced)
}

namespace {

// --- reading data blobs -------------------------------------------------------------------------

class Reader
{
public:
    Reader(const void* p, size_t n) : m_p((const BYTE*)p), m_n(n) {}
    bool Ok() const { return m_ok; }
    DWORD U32() { DWORD v = 0; Read(&v, 4); return v; }
    WORD U16() { WORD v = 0; Read(&v, 2); return v; }
    float F32() { float v = 0; Read(&v, 4); return v; }
    const char* Str() { const char* s = nullptr; Read(&s, sizeof(s)); return s; }
    D3DXVECTOR3 Vec3() { D3DXVECTOR3 v; v.x = F32(); v.y = F32(); v.z = F32(); return v; }
    D3DXMATRIX Matrix() { D3DXMATRIX m; for (int i = 0; i < 16; ++i) ((float*)&m)[i] = F32(); return m; }
    D3DCOLORVALUE Color(bool alpha)
    {
        D3DCOLORVALUE c;
        c.r = F32(); c.g = F32(); c.b = F32();
        c.a = alpha ? F32() : 1.0f;
        return c;
    }

private:
    void Read(void* out, size_t n)
    {
        if (m_at + n > m_n) { m_ok = false; std::memset(out, 0, n); return; }
        std::memcpy(out, m_p + m_at, n);   // blobs are packed: no alignment assumed
        m_at += n;
    }
    const BYTE* m_p;
    size_t m_n, m_at = 0;
    bool m_ok = true;
};

// Lock()/Unlock() of an ID3DXFileData for the scope.
struct Locked
{
    explicit Locked(ID3DXFileData* d) : data(d)
    {
        if (FAILED(d->Lock(&size, (const void**)&ptr))) ptr = nullptr;
    }
    ~Locked() { if (ptr) data->Unlock(); }
    ID3DXFileData* data;
    const BYTE* ptr = nullptr;
    SIZE_T size = 0;
};

GUID TypeOf(ID3DXFileData* d)
{
    GUID g = {};
    d->GetType(&g);
    return g;
}

std::string NameOf(ID3DXFileData* d, bool* has = nullptr)
{
    SIZE_T n = 0;
    if (has) *has = false;
    if (FAILED(d->GetName(nullptr, &n)) || n <= 1) return std::string();
    std::string s(n, '\0');
    if (FAILED(d->GetName(&s[0], &n))) return std::string();
    s.resize(std::strlen(s.c_str()));
    if (has) *has = !s.empty();
    return s;
}

template <class F> void ForEachChild(ID3DXFileData* d, F f)
{
    SIZE_T n = 0;
    if (FAILED(d->GetChildren(&n))) return;
    for (SIZE_T i = 0; i < n; ++i) {
        ID3DXFileData* c = nullptr;
        if (SUCCEEDED(d->GetChild(i, &c)) && c) {
            f(c);
            c->Release();
        }
    }
}

// --- one mesh as read from the file -----------------------------------------------------------

struct Material
{
    D3DMATERIAL9 mat = {};
    std::string texture;
    bool hasTexture = false;
};

struct Bone
{
    std::string name;
    D3DXMATRIX offset;
    std::vector<DWORD> vertices;
    std::vector<float> weights;
};

struct SourceMesh
{
    std::string name;
    std::vector<D3DXVECTOR3> pos;
    std::vector<std::vector<DWORD>> faces;
    std::vector<D3DXVECTOR3> normals;
    std::vector<std::vector<DWORD>> normalFaces;
    std::vector<D3DXVECTOR2> uv;
    std::vector<D3DCOLOR> colors;              // per position, when MeshVertexColors is present
    std::vector<DWORD> faceMaterial;           // per polygon
    std::vector<Material> materials;
    std::vector<Bone> bones;
};

HRESULT ReadMaterial(ID3DXFileData* d, Material& m)
{
    Locked l(d);
    if (!l.ptr) return D3DXFERR_BADVALUE;
    Reader r(l.ptr, l.size);
    m.mat.Diffuse = r.Color(true);
    m.mat.Power = r.F32();
    m.mat.Specular = r.Color(false);
    m.mat.Emissive = r.Color(false);
    if (!r.Ok()) return D3DXFERR_BADVALUE;
    ForEachChild(d, [&](ID3DXFileData* c) {
        if (TypeOf(c) != TID_D3DRMTextureFilename) return;
        Locked t(c);
        if (!t.ptr) return;
        Reader tr(t.ptr, t.size);
        const char* s = tr.Str();
        if (tr.Ok() && s) { m.texture = s; m.hasTexture = true; }
    });
    return D3D_OK;
}

void ReadFaces(Reader& r, DWORD count, std::vector<std::vector<DWORD>>& out)
{
    out.resize(count);
    for (DWORD f = 0; f < count && r.Ok(); ++f) {
        const DWORD n = r.U32();
        if (!r.Ok() || n > 1024) { out.clear(); return; }
        out[f].resize(n);
        for (DWORD i = 0; i < n; ++i) out[f][i] = r.U32();
    }
}

HRESULT ReadMesh(ID3DXFileData* d, SourceMesh& m)
{
    m.name = NameOf(d);
    {
        Locked l(d);
        if (!l.ptr) return D3DXFERR_BADVALUE;
        Reader r(l.ptr, l.size);
        const DWORD nv = r.U32();
        m.pos.resize(nv);
        for (DWORD i = 0; i < nv && r.Ok(); ++i) m.pos[i] = r.Vec3();
        ReadFaces(r, r.U32(), m.faces);
        if (!r.Ok()) return D3DXFERR_BADVALUE;
    }
    HRESULT hr = D3D_OK;
    ForEachChild(d, [&](ID3DXFileData* c) {
        if (FAILED(hr)) return;
        const GUID t = TypeOf(c);
        Locked l(c);
        if (!l.ptr) return;
        Reader r(l.ptr, l.size);
        if (t == TID_D3DRMMeshNormals) {
            const DWORD n = r.U32();
            m.normals.resize(n);
            for (DWORD i = 0; i < n && r.Ok(); ++i) m.normals[i] = r.Vec3();
            ReadFaces(r, r.U32(), m.normalFaces);
            if (!r.Ok() || m.normalFaces.size() != m.faces.size()) { m.normals.clear(); m.normalFaces.clear(); }
        } else if (t == TID_D3DRMMeshTextureCoords) {
            const DWORD n = r.U32();
            m.uv.resize(n);
            for (DWORD i = 0; i < n && r.Ok(); ++i) { m.uv[i].x = r.F32(); m.uv[i].y = r.F32(); }
            if (!r.Ok() || n != m.pos.size()) m.uv.clear();
        } else if (t == TID_D3DRMMeshVertexColors) {
            const DWORD n = r.U32();
            m.colors.assign(m.pos.size(), 0xFFFFFFFF);
            for (DWORD i = 0; i < n && r.Ok(); ++i) {
                const DWORD idx = r.U32();
                const D3DCOLORVALUE cv = r.Color(true);
                if (idx < m.colors.size()) m.colors[idx] = D3DCOLOR_COLORVALUE(cv.r, cv.g, cv.b, cv.a);
            }
        } else if (t == TID_D3DRMMeshMaterialList) {
            const DWORD nMat = r.U32();
            const DWORD nIdx = r.U32();
            m.faceMaterial.resize(nIdx);
            for (DWORD i = 0; i < nIdx && r.Ok(); ++i) m.faceMaterial[i] = r.U32();
            ForEachChild(c, [&](ID3DXFileData* mc) {
                if (TypeOf(mc) != TID_D3DRMMaterial) return;
                Material mat;
                if (SUCCEEDED(ReadMaterial(mc, mat))) m.materials.push_back(mat);
            });
            if (m.materials.size() > nMat) m.materials.resize(nMat);
        } else if (t == DXFILEOBJ_SkinWeights) {
            Bone b;
            const char* name = r.Str();
            b.name = name ? name : "";
            const DWORD n = r.U32();
            b.vertices.resize(n);
            b.weights.resize(n);
            for (DWORD i = 0; i < n && r.Ok(); ++i) b.vertices[i] = r.U32();
            for (DWORD i = 0; i < n && r.Ok(); ++i) b.weights[i] = r.F32();
            b.offset = r.Matrix();
            if (r.Ok()) m.bones.push_back(b);
            else hr = D3DXFERR_BADVALUE;
        }
    });
    return hr;
}

// --- triangle mesh with D3DX's vertex layout ----------------------------------------------------

struct BuiltMesh
{
    DWORD fvf = D3DFVF_XYZ;
    std::vector<D3DXVECTOR3> pos, normals;
    std::vector<D3DXVECTOR2> uv;
    std::vector<D3DCOLOR> colors;
    std::vector<DWORD> origin;      // output vertex -> source position index
    std::vector<DWORD> indices;     // 3 per triangle
    std::vector<DWORD> attribs;     // per triangle
    std::vector<Material> materials;
    std::vector<Bone> bones;        // vertices refer to source positions
};

void AppendMesh(BuiltMesh& out, const SourceMesh& m, const D3DXMATRIX* transform)
{
    const DWORD base = (DWORD)out.pos.size();
    const DWORD matBase = (DWORD)out.materials.size();
    const DWORD nPos = (DWORD)m.pos.size();
    const bool hasNormals = !m.normals.empty();

    // Output vertices 0..nPos-1 are the source positions; a position seen with a second normal
    // gets a duplicate appended at the end.
    std::vector<long> normalOf(nPos, -1);
    std::map<std::pair<DWORD, DWORD>, DWORD> dups;
    std::vector<DWORD> verts(nPos);
    for (DWORD i = 0; i < nPos; ++i) verts[i] = i;
    std::vector<std::pair<DWORD, long>> extra;   // (source position, normal)

    auto corner = [&](size_t face, size_t k) -> DWORD {
        const DWORD p = m.faces[face][k];
        if (!hasNormals) return p;
        const DWORD n = m.normalFaces[face].size() > k ? m.normalFaces[face][k] : 0;
        if (normalOf[p] < 0 || normalOf[p] == (long)n) { normalOf[p] = (long)n; return p; }
        auto it = dups.find(std::make_pair(p, n));
        if (it != dups.end()) return it->second;
        const DWORD idx = nPos + (DWORD)extra.size();
        extra.push_back(std::make_pair(p, (long)n));
        dups[std::make_pair(p, n)] = idx;
        return idx;
    };

    for (size_t f = 0; f < m.faces.size(); ++f) {
        const std::vector<DWORD>& poly = m.faces[f];
        bool valid = poly.size() >= 3;
        for (DWORD p : poly) valid = valid && p < nPos;
        if (valid && hasNormals) for (DWORD n : m.normalFaces[f]) valid = valid && n < m.normals.size();
        if (!valid) continue;
        const DWORD attrib = m.faceMaterial.empty() ? 0 : m.faceMaterial[std::min(f, m.faceMaterial.size() - 1)];
        const DWORD v0 = corner(f, 0);
        for (size_t k = 1; k + 1 < poly.size(); ++k) {
            out.indices.push_back(base + v0);
            out.indices.push_back(base + corner(f, k));
            out.indices.push_back(base + corner(f, k + 1));
            out.attribs.push_back(matBase + attrib);
        }
    }

    D3DXMATRIX normalMatrix;
    if (transform) {
        D3DXMatrixInverse(&normalMatrix, nullptr, transform);
        D3DXMatrixTranspose(&normalMatrix, &normalMatrix);
    }
    const DWORD count = nPos + (DWORD)extra.size();
    for (DWORD v = 0; v < count; ++v) {
        const DWORD src = v < nPos ? v : extra[v - nPos].first;
        D3DXVECTOR3 p = m.pos[src];
        if (transform) D3DXVec3TransformCoord(&p, &p, transform);
        out.pos.push_back(p);
        out.origin.push_back(src);
        D3DXVECTOR3 n(0, 0, 0);
        if (hasNormals) {
            const long ni = v < nPos ? normalOf[v] : extra[v - nPos].second;
            if (ni >= 0) n = m.normals[(size_t)ni];
            if (transform) { D3DXVec3TransformNormal(&n, &n, &normalMatrix); D3DXVec3Normalize(&n, &n); }
        }
        out.normals.push_back(n);
        out.uv.push_back(m.uv.empty() ? D3DXVECTOR2(0, 0) : m.uv[src]);
        out.colors.push_back(m.colors.empty() ? 0xFFFFFFFF : m.colors[src]);
    }
    if (hasNormals) out.fvf |= D3DFVF_NORMAL;
    if (!m.uv.empty()) out.fvf |= D3DFVF_TEX1;
    if (!m.colors.empty()) out.fvf |= D3DFVF_DIFFUSE;
    for (const Material& mat : m.materials) out.materials.push_back(mat);
    for (Bone b : m.bones) {
        for (DWORD& v : b.vertices) v += base;
        out.bones.push_back(b);
    }
}

HRESULT MakeBuffer(size_t bytes, ID3DXBuffer** out)
{
    return D3DXCreateBuffer((DWORD)std::max<size_t>(bytes, 1), out);
}

// Creates the ID3DXMesh (and the outputs D3DX hands back with it).
HRESULT CreateMesh(BuiltMesh& b, DWORD options, LPDIRECT3DDEVICE9 device, ID3DXMesh** meshOut, ID3DXBuffer** adjOut,
                   ID3DXBuffer** matOut, ID3DXBuffer** fxOut, DWORD* numMatOut)
{
    const DWORD nVerts = (DWORD)b.pos.size(), nFaces = (DWORD)b.attribs.size();
    if (!nVerts || !nFaces) return E_FAIL;

    // Group faces by material, keeping file order within a material (ATTRSORT | IGNOREVERTS).
    std::vector<DWORD> order(nFaces);
    for (DWORD i = 0; i < nFaces; ++i) order[i] = i;
    std::stable_sort(order.begin(), order.end(), [&](DWORD a, DWORD c) { return b.attribs[a] < b.attribs[c]; });

    if (nVerts > 0xFFFF) options |= D3DXMESH_32BIT;
    ID3DXMesh* mesh = nullptr;
    HRESULT hr = D3DXCreateMeshFVF(nFaces, nVerts, options, b.fvf, device, &mesh);
    if (FAILED(hr)) return hr;

    BYTE* vb = nullptr;
    const UINT stride = D3DXGetFVFVertexSize(b.fvf);
    if (SUCCEEDED(hr = mesh->LockVertexBuffer(0, (void**)&vb))) {
        for (DWORD v = 0; v < nVerts; ++v) {
            BYTE* p = vb + (size_t)v * stride;
            std::memcpy(p, &b.pos[v], 12); p += 12;
            if (b.fvf & D3DFVF_NORMAL) { std::memcpy(p, &b.normals[v], 12); p += 12; }
            if (b.fvf & D3DFVF_DIFFUSE) { std::memcpy(p, &b.colors[v], 4); p += 4; }
            if (b.fvf & D3DFVF_TEX1) { std::memcpy(p, &b.uv[v], 8); p += 8; }
        }
        mesh->UnlockVertexBuffer();
    }
    void* ib = nullptr;
    if (SUCCEEDED(hr) && SUCCEEDED(hr = mesh->LockIndexBuffer(0, &ib))) {
        const bool wide = (mesh->GetOptions() & D3DXMESH_32BIT) != 0;
        for (DWORD f = 0; f < nFaces; ++f)
            for (int k = 0; k < 3; ++k) {
                const DWORD idx = b.indices[(size_t)order[f] * 3 + k];
                if (wide) ((DWORD*)ib)[f * 3 + k] = idx;
                else ((WORD*)ib)[f * 3 + k] = (WORD)idx;
            }
        mesh->UnlockIndexBuffer();
    }
    DWORD* attr = nullptr;
    std::vector<D3DXATTRIBUTERANGE> table;
    if (SUCCEEDED(hr) && SUCCEEDED(hr = mesh->LockAttributeBuffer(0, &attr))) {
        for (DWORD f = 0; f < nFaces; ++f) {
            const DWORD a = b.attribs[order[f]];
            attr[f] = a;
            if (table.empty() || table.back().AttribId != a) {
                D3DXATTRIBUTERANGE r = { a, f, 0, 0xFFFFFFFF, 0 };
                table.push_back(r);
            }
            D3DXATTRIBUTERANGE& r = table.back();
            ++r.FaceCount;
            for (int k = 0; k < 3; ++k) {
                const DWORD idx = b.indices[(size_t)order[f] * 3 + k];
                const DWORD end = std::max(r.VertexStart == 0xFFFFFFFF ? 0 : r.VertexStart + r.VertexCount, idx + 1);
                r.VertexStart = std::min(r.VertexStart, idx);
                r.VertexCount = end - r.VertexStart;
            }
        }
        mesh->UnlockAttributeBuffer();
        hr = mesh->SetAttributeTable(table.data(), (DWORD)table.size());
    }
    // Faces were reordered: keep the per-face data in the same order for the callers below.
    std::vector<DWORD> sortedIdx(b.indices.size()), sortedAttr(nFaces);
    for (DWORD f = 0; f < nFaces; ++f) {
        sortedAttr[f] = b.attribs[order[f]];
        for (int k = 0; k < 3; ++k) sortedIdx[(size_t)f * 3 + k] = b.indices[(size_t)order[f] * 3 + k];
    }
    b.indices.swap(sortedIdx);
    b.attribs.swap(sortedAttr);
    if (FAILED(hr)) { mesh->Release(); return hr; }

    if (adjOut) {
        *adjOut = nullptr;
        if (SUCCEEDED(MakeBuffer(sizeof(DWORD) * 3 * nFaces, adjOut)))
            mesh->GenerateAdjacency(0.0f, (DWORD*)(*adjOut)->GetBufferPointer());
    }
    const DWORD nMat = (DWORD)b.materials.size();
    if (numMatOut) *numMatOut = nMat;
    if (matOut) {
        *matOut = nullptr;
        size_t bytes = sizeof(D3DXMATERIAL) * nMat;
        for (const Material& m : b.materials) if (m.hasTexture) bytes += m.texture.size() + 1;
        if (nMat && SUCCEEDED(MakeBuffer(bytes, matOut))) {
            BYTE* base = (BYTE*)(*matOut)->GetBufferPointer();
            D3DXMATERIAL* mats = (D3DXMATERIAL*)base;
            char* strings = (char*)(base + sizeof(D3DXMATERIAL) * nMat);
            for (DWORD i = 0; i < nMat; ++i) {
                mats[i].MatD3D = b.materials[i].mat;
                mats[i].pTextureFilename = nullptr;
                if (b.materials[i].hasTexture) {
                    std::memcpy(strings, b.materials[i].texture.c_str(), b.materials[i].texture.size() + 1);
                    mats[i].pTextureFilename = strings;
                    strings += b.materials[i].texture.size() + 1;
                }
            }
        }
    }
    if (fxOut) {
        *fxOut = nullptr;
        if (nMat && SUCCEEDED(MakeBuffer(sizeof(D3DXEFFECTINSTANCE) * nMat, fxOut)))
            std::memset((*fxOut)->GetBufferPointer(), 0, sizeof(D3DXEFFECTINSTANCE) * nMat);
    }
    *meshOut = mesh;
    return D3D_OK;
}

HRESULT CreateSkin(const BuiltMesh& b, ID3DXSkinInfo** out)
{
    *out = nullptr;
    if (b.bones.empty()) return D3D_OK;
    ID3DXSkinInfo* skin = nullptr;
    HRESULT hr = D3DXCreateSkinInfoFVF((DWORD)b.pos.size(), b.fvf, (DWORD)b.bones.size(), &skin);
    if (FAILED(hr)) return hr;
    // Influences name source positions; every output vertex made from that position gets them.
    std::multimap<DWORD, DWORD> copies;
    for (DWORD v = 0; v < (DWORD)b.origin.size(); ++v) copies.insert(std::make_pair(b.origin[v], v));
    for (DWORD i = 0; i < (DWORD)b.bones.size(); ++i) {
        const Bone& bone = b.bones[i];
        std::vector<DWORD> verts;
        std::vector<float> weights;
        for (size_t k = 0; k < bone.vertices.size(); ++k) {
            auto range = copies.equal_range(bone.vertices[k]);
            for (auto it = range.first; it != range.second; ++it) {
                verts.push_back(it->second);
                weights.push_back(bone.weights[k]);
            }
        }
        skin->SetBoneName(i, bone.name.c_str());
        skin->SetBoneOffsetMatrix(i, &bone.offset);
        if (!verts.empty()) skin->SetBoneInfluence(i, (DWORD)verts.size(), verts.data(), weights.data());
    }
    *out = skin;
    return D3D_OK;
}

// --- files ------------------------------------------------------------------------------------

HRESULT OpenFile(LPCSTR path, ID3DXFile** file, ID3DXFileEnumObject** en)
{
    *file = nullptr;
    *en = nullptr;
    HRESULT hr = D3DXFileCreate(file);
    if (FAILED(hr)) return hr;
    (*file)->RegisterTemplates(ran_d3dx_templates::D3DRM_XTEMPLATES, D3DRM_XTEMPLATE_BYTES);
    (*file)->RegisterTemplates(XSKINEXP_TEMPLATES, sizeof(XSKINEXP_TEMPLATES) - 1);
    (*file)->RegisterTemplates(XEXTENSIONS_TEMPLATES, sizeof(XEXTENSIONS_TEMPLATES) - 1);
    hr = (*file)->CreateEnumObject(path, D3DXF_FILELOAD_FROMFILE, en);
    if (FAILED(hr)) { (*file)->Release(); *file = nullptr; }
    return hr;
}

D3DXMATRIX FrameMatrix(ID3DXFileData* frame)
{
    D3DXMATRIX m;
    D3DXMatrixIdentity(&m);
    ForEachChild(frame, [&](ID3DXFileData* c) {
        if (TypeOf(c) != TID_D3DRMFrameTransformMatrix) return;
        Locked l(c);
        if (!l.ptr) return;
        Reader r(l.ptr, l.size);
        const D3DXMATRIX v = r.Matrix();
        if (r.Ok()) m = v;
    });
    return m;
}

// Every mesh under `d` (a Frame or Mesh), transformed by the frame matrices (D3DXLoadMeshFromX).
HRESULT CollectMeshes(ID3DXFileData* d, const D3DXMATRIX& parent, BuiltMesh& out, bool& any)
{
    const GUID t = TypeOf(d);
    if (t == TID_D3DRMMesh) {
        SourceMesh m;
        HRESULT hr = ReadMesh(d, m);
        if (FAILED(hr)) return hr;
        AppendMesh(out, m, &parent);
        any = true;
        return D3D_OK;
    }
    if (t != TID_D3DRMFrame) return D3D_OK;
    D3DXMATRIX world = FrameMatrix(d) * parent;
    HRESULT hr = D3D_OK;
    ForEachChild(d, [&](ID3DXFileData* c) { if (SUCCEEDED(hr)) hr = CollectMeshes(c, world, out, any); });
    return hr;
}

// --- hierarchy --------------------------------------------------------------------------------

HRESULT LoadFrame(ID3DXFileData* d, DWORD options, LPDIRECT3DDEVICE9 device, LPD3DXALLOCATEHIERARCHY alloc, LPD3DXFRAME* out);

HRESULT LoadMeshContainer(ID3DXFileData* d, DWORD options, LPDIRECT3DDEVICE9 device, LPD3DXALLOCATEHIERARCHY alloc,
                          LPD3DXMESHCONTAINER* out)
{
    *out = nullptr;
    SourceMesh src;
    HRESULT hr = ReadMesh(d, src);
    if (FAILED(hr)) return hr;
    BuiltMesh b;
    AppendMesh(b, src, nullptr);
    ID3DXMesh* mesh = nullptr;
    ID3DXBuffer *adj = nullptr, *mats = nullptr, *fx = nullptr;
    DWORD nMat = 0;
    hr = CreateMesh(b, options, device, &mesh, &adj, &mats, &fx, &nMat);
    if (FAILED(hr)) return hr;
    ID3DXSkinInfo* skin = nullptr;
    hr = CreateSkin(b, &skin);
    if (SUCCEEDED(hr)) {
        D3DXMESHDATA data;
        data.Type = D3DXMESHTYPE_MESH;
        data.pMesh = mesh;
        bool named = false;
        const std::string name = NameOf(d, &named);
        hr = alloc->CreateMeshContainer(named ? name.c_str() : nullptr, &data,
                                        mats ? (const D3DXMATERIAL*)mats->GetBufferPointer() : nullptr,
                                        fx ? (const D3DXEFFECTINSTANCE*)fx->GetBufferPointer() : nullptr, nMat,
                                        adj ? (const DWORD*)adj->GetBufferPointer() : nullptr, skin, out);
    }
    if (skin) skin->Release();
    if (adj) adj->Release();
    if (mats) mats->Release();
    if (fx) fx->Release();
    mesh->Release();
    return hr;
}

void AppendSibling(LPD3DXFRAME* first, LPD3DXFRAME f)
{
    while (*first) first = &(*first)->pFrameSibling;
    *first = f;
}

HRESULT LoadFrame(ID3DXFileData* d, DWORD options, LPDIRECT3DDEVICE9 device, LPD3DXALLOCATEHIERARCHY alloc, LPD3DXFRAME* out)
{
    *out = nullptr;
    bool named = false;
    const std::string name = NameOf(d, &named);
    LPD3DXFRAME frame = nullptr;
    HRESULT hr = alloc->CreateFrame(named ? name.c_str() : nullptr, &frame);
    if (FAILED(hr)) return hr;
    if (!frame) return E_FAIL;
    D3DXMatrixIdentity(&frame->TransformationMatrix);
    ForEachChild(d, [&](ID3DXFileData* c) {
        if (FAILED(hr)) return;
        const GUID t = TypeOf(c);
        if (t == TID_D3DRMFrameTransformMatrix) {
            Locked l(c);
            if (!l.ptr) return;
            Reader r(l.ptr, l.size);
            const D3DXMATRIX m = r.Matrix();
            if (r.Ok()) frame->TransformationMatrix = m;
        } else if (t == TID_D3DRMMesh) {
            LPD3DXMESHCONTAINER mc = nullptr;
            hr = LoadMeshContainer(c, options, device, alloc, &mc);
            if (SUCCEEDED(hr) && mc) {
                LPD3DXMESHCONTAINER* tail = &frame->pMeshContainer;
                while (*tail) tail = &(*tail)->pNextMeshContainer;
                *tail = mc;
            }
        } else if (t == TID_D3DRMFrame) {
            LPD3DXFRAME child = nullptr;
            hr = LoadFrame(c, options, device, alloc, &child);
            if (SUCCEEDED(hr) && child) AppendSibling(&frame->pFrameFirstChild, child);
        }
    });
    if (FAILED(hr)) { D3DXFrameDestroy(frame, alloc); return hr; }
    *out = frame;
    return D3D_OK;
}

// Positions of every mesh in the hierarchy, in the root's space.
void GatherPositions(const D3DXFRAME* f, const D3DXMATRIX& parent, std::vector<D3DXVECTOR3>& out)
{
    for (; f; f = f->pFrameSibling) {
        const D3DXMATRIX world = f->TransformationMatrix * parent;
        for (const D3DXMESHCONTAINER* mc = f->pMeshContainer; mc; mc = mc->pNextMeshContainer) {
            ID3DXMesh* mesh = mc->MeshData.pMesh;
            if (mc->MeshData.Type != D3DXMESHTYPE_MESH || !mesh) continue;
            const DWORD n = mesh->GetNumVertices(), stride = mesh->GetNumBytesPerVertex();
            BYTE* vb = nullptr;
            if (FAILED(mesh->LockVertexBuffer(D3DLOCK_READONLY, (void**)&vb))) continue;
            for (DWORD v = 0; v < n; ++v) {
                D3DXVECTOR3 p;
                std::memcpy(&p, vb + (size_t)v * stride, sizeof(p));
                D3DXVec3TransformCoord(&p, &p, &world);
                out.push_back(p);
            }
            mesh->UnlockVertexBuffer();
        }
        GatherPositions(f->pFrameFirstChild, world, out);
    }
}

} // namespace

extern "C" {

HRESULT WINAPI D3DXLoadMeshFromXof(LPD3DXFILEDATA data, DWORD options, LPDIRECT3DDEVICE9 device, LPD3DXBUFFER* adjacency,
                                   LPD3DXBUFFER* materials, LPD3DXBUFFER* effects, DWORD* numMaterials, LPD3DXMESH* mesh)
{
    if (mesh) *mesh = nullptr;
    if (!data || !device || !mesh) return D3DERR_INVALIDCALL;
    if (TypeOf(data) != TID_D3DRMMesh) return D3DXFERR_BADOBJECT;
    SourceMesh src;
    HRESULT hr = ReadMesh(data, src);
    if (FAILED(hr)) return hr;
    BuiltMesh b;
    AppendMesh(b, src, nullptr);
    return CreateMesh(b, options, device, mesh, adjacency, materials, effects, numMaterials);
}

HRESULT WINAPI D3DXLoadMeshFromXA(LPCSTR file, DWORD options, LPDIRECT3DDEVICE9 device, LPD3DXBUFFER* adjacency,
                                  LPD3DXBUFFER* materials, LPD3DXBUFFER* effects, DWORD* numMaterials, LPD3DXMESH* mesh)
{
    if (mesh) *mesh = nullptr;
    if (!file || !device || !mesh) return D3DERR_INVALIDCALL;
    ID3DXFile* xf = nullptr;
    ID3DXFileEnumObject* en = nullptr;
    HRESULT hr = OpenFile(file, &xf, &en);
    if (FAILED(hr)) return hr;
    BuiltMesh b;
    bool any = false;
    D3DXMATRIX identity;
    D3DXMatrixIdentity(&identity);
    SIZE_T n = 0;
    en->GetChildren(&n);
    for (SIZE_T i = 0; i < n && SUCCEEDED(hr); ++i) {
        ID3DXFileData* d = nullptr;
        if (FAILED(en->GetChild(i, &d)) || !d) continue;
        hr = CollectMeshes(d, identity, b, any);
        d->Release();
    }
    en->Release();
    xf->Release();
    if (FAILED(hr)) return hr;
    if (!any) return D3DXFERR_NOMOREOBJECTS;
    return CreateMesh(b, options, device, mesh, adjacency, materials, effects, numMaterials);
}

HRESULT WINAPI D3DXLoadMeshHierarchyFromXA(LPCSTR file, DWORD options, LPDIRECT3DDEVICE9 device, LPD3DXALLOCATEHIERARCHY alloc,
                                           LPD3DXLOADUSERDATA, LPD3DXFRAME* root, LPD3DXANIMATIONCONTROLLER* anim)
{
    if (root) *root = nullptr;
    if (anim) *anim = nullptr;
    if (!file || !device || !alloc || !root) return D3DERR_INVALIDCALL;
    ID3DXFile* xf = nullptr;
    ID3DXFileEnumObject* en = nullptr;
    HRESULT hr = OpenFile(file, &xf, &en);
    if (FAILED(hr)) return hr;
    LPD3DXFRAME first = nullptr;
    LPD3DXFRAME meshFrame = nullptr;   // holds meshes that sit at the top level of the file
    SIZE_T n = 0;
    en->GetChildren(&n);
    for (SIZE_T i = 0; i < n && SUCCEEDED(hr); ++i) {
        ID3DXFileData* d = nullptr;
        if (FAILED(en->GetChild(i, &d)) || !d) continue;
        const GUID t = TypeOf(d);
        if (t == TID_D3DRMFrame) {
            LPD3DXFRAME f = nullptr;
            hr = LoadFrame(d, options, device, alloc, &f);
            if (SUCCEEDED(hr) && f) AppendSibling(&first, f);
        } else if (t == TID_D3DRMMesh) {
            if (!meshFrame && SUCCEEDED(hr = alloc->CreateFrame(nullptr, &meshFrame)) && meshFrame) {
                D3DXMatrixIdentity(&meshFrame->TransformationMatrix);
                AppendSibling(&first, meshFrame);
            }
            LPD3DXMESHCONTAINER mc = nullptr;
            if (SUCCEEDED(hr) && meshFrame) hr = LoadMeshContainer(d, options, device, alloc, &mc);
            if (SUCCEEDED(hr) && mc) {
                LPD3DXMESHCONTAINER* tail = &meshFrame->pMeshContainer;
                while (*tail) tail = &(*tail)->pNextMeshContainer;
                *tail = mc;
            }
        }
        d->Release();
    }
    en->Release();
    xf->Release();
    if (FAILED(hr)) {
        if (first) D3DXFrameDestroy(first, alloc);
        return hr;
    }
    if (!first) return E_FAIL;
    *root = first;
    return D3D_OK;
}

HRESULT WINAPI D3DXFrameCalculateBoundingSphere(CONST D3DXFRAME* root, LPD3DXVECTOR3 center, FLOAT* radius)
{
    if (!root || !center || !radius) return D3DERR_INVALIDCALL;
    std::vector<D3DXVECTOR3> points;
    D3DXMATRIX identity;
    D3DXMatrixIdentity(&identity);
    GatherPositions(root, identity, points);
    if (points.empty()) {
        *center = D3DXVECTOR3(0, 0, 0);
        *radius = 0.0f;
        return D3D_OK;
    }
    return D3DXComputeBoundingSphere(points.data(), (DWORD)points.size(), sizeof(D3DXVECTOR3), center, radius);
}

} // extern "C"
