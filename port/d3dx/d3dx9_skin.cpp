// D3DX skinning for the native build (plan Phase 3): D3DXCreateSkinInfo/FVF and ID3DXSkinInfo.
// Behaviour follows D3DX9:
//   - Bone names, offset matrices (identity until set) and influences are stored as given;
//     GetMaxVertexInfluences/GetMaxFaceInfluences count every influence entry.
//   - UpdateSkinnedMesh copies the source vertices and replaces the position (and FLOAT3 normal)
//     of every influenced vertex by the weighted sum of its bone transforms (normals through the
//     inverse-transpose matrices when given, else the bone matrices).
//   - ConvertToBlendedMesh / ConvertToIndexedBlendedMesh build a new mesh whose attribute ids
//     are bone-combination indices (one attribute-table entry per combination, in order), each
//     combination with its own contiguous vertex range (vertices shared between combinations are
//     duplicated). Influences at or below the minimum bone influence are dropped, the rest are
//     sorted by weight and normalised. Non-indexed: at most 4 bones per face (weaker influences
//     are dropped to fit), XYZB(n-1) weights for n = max face influences, BoneId[] has n entries.
//     Indexed: at most 4 influences per vertex, XYZB(n) + LASTBETA_UBYTE4 (n-1 weights, then 4
//     palette slots), BoneId[] has paletteSize entries. Unused BoneId entries are UINT_MAX.
//     Combinations are filled first-fit per attribute in face order (no vertex-cache order).
#include "d3dx9_mesh_impl.h"
#include <algorithm>
#include <climits>
#include <cstring>
#include <string>

using namespace ran_d3dx;

namespace {

struct Influence
{
    DWORD bone;
    float weight;
};
using VertexInfluences = std::vector<std::vector<Influence>>;

void Identity(D3DXMATRIX& m)
{
    std::memset(&m, 0, sizeof(m));
    m.m[0][0] = m.m[1][1] = m.m[2][2] = m.m[3][3] = 1.0f;
}

void Normalise(std::vector<Influence>& v)
{
    float sum = 0.0f;
    for (const Influence& i : v) sum += i.weight;
    if (sum > 0.0f) for (Influence& i : v) i.weight /= sum;
}

bool Has(const std::vector<DWORD>& set, DWORD bone) { return std::find(set.begin(), set.end(), bone) != set.end(); }

// Distinct bones of a face.
std::vector<DWORD> FaceBones(const VertexInfluences& infl, const DWORD* corners)
{
    std::vector<DWORD> bones;
    for (int c = 0; c < 3; ++c)
        for (const Influence& i : infl[corners[c]])
            if (!Has(bones, i.bone)) bones.push_back(i.bone);
    std::sort(bones.begin(), bones.end());
    return bones;
}

// Drops the weakest influences of faces touching more than `limit` bones. Every vertex keeps
// its strongest bone when the limit allows; the rest are chosen by summed weight over the face.
void FitFaces(VertexInfluences& infl, const std::vector<DWORD>& indices, DWORD numFaces, DWORD limit)
{
    for (DWORD f = 0; f < numFaces; ++f) {
        const DWORD* c = &indices[3 * f];
        std::vector<DWORD> bones = FaceBones(infl, c);
        if (bones.size() <= limit) continue;
        std::vector<float> sum(bones.size(), 0.0f);
        for (int k = 0; k < 3; ++k)
            for (const Influence& i : infl[c[k]])
                sum[std::lower_bound(bones.begin(), bones.end(), i.bone) - bones.begin()] += i.weight;
        std::vector<DWORD> keep;
        for (int k = 0; k < 3 && keep.size() < limit; ++k)
            if (!infl[c[k]].empty() && !Has(keep, infl[c[k]][0].bone)) keep.push_back(infl[c[k]][0].bone);
        std::vector<size_t> byWeight(bones.size());
        for (size_t i = 0; i < bones.size(); ++i) byWeight[i] = i;
        std::stable_sort(byWeight.begin(), byWeight.end(), [&](size_t a, size_t b) { return sum[a] > sum[b]; });
        for (size_t i = 0; i < byWeight.size() && keep.size() < limit; ++i)
            if (!Has(keep, bones[byWeight[i]])) keep.push_back(bones[byWeight[i]]);
        for (int k = 0; k < 3; ++k) {
            std::vector<Influence>& v = infl[c[k]];
            v.erase(std::remove_if(v.begin(), v.end(), [&](const Influence& i) { return !Has(keep, i.bone); }), v.end());
            if (v.empty()) v.push_back({ keep[0], 1.0f });
            Normalise(v);
        }
    }
}

class SkinInfo final : public ID3DXSkinInfo, public RefCounted
{
public:
    HRESULT Init(DWORD numVertices, const D3DVERTEXELEMENT9* decl, DWORD numBones)
    {
        m_numVertices = numVertices;
        m_bones.resize(numBones);
        for (Bone& b : m_bones) Identity(b.offset);
        return SetDeclaration(decl);
    }

    STDMETHOD(QueryInterface)(THIS_ REFIID iid, LPVOID* ppv)
    {
        if (!ppv) return E_POINTER;
        if (IsEqualGUID(iid, IID_IUnknown) || IsEqualGUID(iid, IID_ID3DXSkinInfo)) { *ppv = this; AddRef(); return S_OK; }
        *ppv = nullptr;
        return E_NOINTERFACE;
    }
    STDMETHOD_(ULONG, AddRef)(THIS) { return DoAddRef(); }
    STDMETHOD_(ULONG, Release)(THIS) { return DoRelease(); }

    STDMETHOD(SetBoneInfluence)(THIS_ DWORD bone, DWORD count, CONST DWORD* vertices, CONST FLOAT* weights)
    {
        if (bone >= m_bones.size() || (count && (!vertices || !weights))) return D3DERR_INVALIDCALL;
        m_bones[bone].vertices.assign(vertices, vertices + count);
        m_bones[bone].weights.assign(weights, weights + count);
        return D3D_OK;
    }
    STDMETHOD(SetBoneVertexInfluence)(THIS_ DWORD bone, DWORD influence, float weight)
    {
        if (bone >= m_bones.size() || influence >= m_bones[bone].weights.size()) return D3DERR_INVALIDCALL;
        m_bones[bone].weights[influence] = weight;
        return D3D_OK;
    }
    STDMETHOD_(DWORD, GetNumBoneInfluences)(THIS_ DWORD bone)
    {
        return bone < m_bones.size() ? (DWORD)m_bones[bone].vertices.size() : 0;
    }
    STDMETHOD(GetBoneInfluence)(THIS_ DWORD bone, DWORD* vertices, FLOAT* weights)
    {
        if (bone >= m_bones.size() || !vertices || !weights) return D3DERR_INVALIDCALL;
        const Bone& b = m_bones[bone];
        std::copy(b.vertices.begin(), b.vertices.end(), vertices);
        std::copy(b.weights.begin(), b.weights.end(), weights);
        return D3D_OK;
    }
    STDMETHOD(GetBoneVertexInfluence)(THIS_ DWORD bone, DWORD influence, float* weight, DWORD* vertex)
    {
        if (bone >= m_bones.size() || influence >= m_bones[bone].vertices.size() || !weight || !vertex) return D3DERR_INVALIDCALL;
        *weight = m_bones[bone].weights[influence];
        *vertex = m_bones[bone].vertices[influence];
        return D3D_OK;
    }
    STDMETHOD(GetMaxVertexInfluences)(THIS_ DWORD* out)
    {
        if (!out) return D3DERR_INVALIDCALL;
        std::vector<DWORD> count(m_numVertices, 0);
        DWORD best = 0;
        for (const Bone& b : m_bones)
            for (DWORD v : b.vertices)
                if (v < m_numVertices) best = std::max(best, ++count[v]);
        *out = best;
        return D3D_OK;
    }
    STDMETHOD_(DWORD, GetNumBones)(THIS) { return (DWORD)m_bones.size(); }
    STDMETHOD(FindBoneVertexInfluenceIndex)(THIS_ DWORD bone, DWORD vertex, DWORD* index)
    {
        if (bone >= m_bones.size() || !index) return D3DERR_INVALIDCALL;
        const std::vector<DWORD>& v = m_bones[bone].vertices;
        auto it = std::find(v.begin(), v.end(), vertex);
        if (it == v.end()) return D3DERR_NOTFOUND;
        *index = (DWORD)(it - v.begin());
        return D3D_OK;
    }
    STDMETHOD(GetMaxFaceInfluences)(THIS_ LPDIRECT3DINDEXBUFFER9 ib, DWORD numFaces, DWORD* out)
    {
        if (!ib || !out) return D3DERR_INVALIDCALL;
        D3DINDEXBUFFER_DESC desc;
        void* p = nullptr;
        HRESULT hr = ib->GetDesc(&desc);
        if (FAILED(hr) || FAILED(hr = ib->Lock(0, 0, &p, D3DLOCK_READONLY))) return hr;
        std::vector<DWORD> idx((size_t)numFaces * 3);
        for (size_t i = 0; i < idx.size(); ++i)
            idx[i] = desc.Format == D3DFMT_INDEX32 ? ((const DWORD*)p)[i] : ((const WORD*)p)[i];
        ib->Unlock();
        const VertexInfluences infl = Influences(m_numVertices, false);
        DWORD best = 0;
        for (DWORD f = 0; f < numFaces; ++f) {
            const DWORD* c = &idx[3 * f];
            if (c[0] >= m_numVertices || c[1] >= m_numVertices || c[2] >= m_numVertices) continue;
            best = std::max(best, (DWORD)FaceBones(infl, c).size());
        }
        *out = best;
        return D3D_OK;
    }
    STDMETHOD(SetMinBoneInfluence)(THIS_ FLOAT minInfluence) { m_minInfluence = minInfluence; return D3D_OK; }
    STDMETHOD_(FLOAT, GetMinBoneInfluence)(THIS) { return m_minInfluence; }
    STDMETHOD(SetBoneName)(THIS_ DWORD bone, LPCSTR name)
    {
        if (bone >= m_bones.size() || !name) return D3DERR_INVALIDCALL;
        m_bones[bone].name = name;
        m_bones[bone].named = true;
        return D3D_OK;
    }
    STDMETHOD_(LPCSTR, GetBoneName)(THIS_ DWORD bone)
    {
        return bone < m_bones.size() && m_bones[bone].named ? m_bones[bone].name.c_str() : nullptr;
    }
    STDMETHOD(SetBoneOffsetMatrix)(THIS_ DWORD bone, CONST D3DXMATRIX* m)
    {
        if (bone >= m_bones.size() || !m) return D3DERR_INVALIDCALL;
        m_bones[bone].offset = *m;
        return D3D_OK;
    }
    STDMETHOD_(LPD3DXMATRIX, GetBoneOffsetMatrix)(THIS_ DWORD bone)
    {
        return bone < m_bones.size() ? &m_bones[bone].offset : nullptr;
    }
    STDMETHOD(Clone)(THIS_ LPD3DXSKININFO* out)
    {
        if (!out) return D3DERR_INVALIDCALL;
        SkinInfo* s = new SkinInfo;
        s->m_numVertices = m_numVertices;
        s->m_bones = m_bones;
        std::memcpy(s->m_decl, m_decl, sizeof(m_decl));
        s->m_fvf = m_fvf;
        s->m_minInfluence = m_minInfluence;
        *out = s;
        return D3D_OK;
    }
    // vertexRemap[new] = old: every influence on an old vertex moves to each new copy of it.
    STDMETHOD(Remap)(THIS_ DWORD numVertices, DWORD* vertexRemap)
    {
        if (!vertexRemap) return D3DERR_INVALIDCALL;
        std::vector<std::vector<DWORD>> copies(m_numVertices);
        for (DWORD n = 0; n < numVertices; ++n)
            if (vertexRemap[n] < m_numVertices) copies[vertexRemap[n]].push_back(n);
        for (Bone& b : m_bones) {
            std::vector<DWORD> verts;
            std::vector<float> weights;
            for (size_t i = 0; i < b.vertices.size(); ++i) {
                if (b.vertices[i] >= m_numVertices) continue;
                for (DWORD n : copies[b.vertices[i]]) { verts.push_back(n); weights.push_back(b.weights[i]); }
            }
            b.vertices.swap(verts);
            b.weights.swap(weights);
        }
        m_numVertices = numVertices;
        return D3D_OK;
    }
    STDMETHOD(SetFVF)(THIS_ DWORD fvf)
    {
        D3DVERTEXELEMENT9 decl[MAX_FVF_DECL_SIZE];
        HRESULT hr = D3DXDeclaratorFromFVF(fvf, decl);
        return FAILED(hr) ? hr : SetDeclaration(decl);
    }
    STDMETHOD(SetDeclaration)(THIS_ CONST D3DVERTEXELEMENT9* decl)
    {
        if (!decl) return D3DERR_INVALIDCALL;
        const UINT len = DeclLength(decl);
        if (len >= MAX_FVF_DECL_SIZE) return D3DERR_INVALIDCALL;
        std::memcpy(m_decl, decl, (len + 1) * sizeof(D3DVERTEXELEMENT9));
        if (FAILED(D3DXFVFFromDeclarator(m_decl, &m_fvf))) m_fvf = 0;
        return D3D_OK;
    }
    STDMETHOD_(DWORD, GetFVF)(THIS) { return m_fvf; }
    STDMETHOD(GetDeclaration)(THIS_ D3DVERTEXELEMENT9 decl[MAX_FVF_DECL_SIZE])
    {
        if (!decl) return D3DERR_INVALIDCALL;
        std::memcpy(decl, m_decl, (DeclLength(m_decl) + 1) * sizeof(D3DVERTEXELEMENT9));
        return D3D_OK;
    }

    STDMETHOD(UpdateSkinnedMesh)(THIS_ CONST D3DXMATRIX* bones, CONST D3DXMATRIX* invTranspose, LPCVOID src, PVOID dst)
    {
        if (!bones || !src || !dst) return D3DERR_INVALIDCALL;
        const UINT stride = D3DXGetDeclVertexSize(m_decl, 0);
        const D3DVERTEXELEMENT9* pos = FindElement(m_decl, D3DDECLUSAGE_POSITION, 0);
        const D3DVERTEXELEMENT9* nrm = FindElement(m_decl, D3DDECLUSAGE_NORMAL, 0);
        if (!pos || (pos->Type != D3DDECLTYPE_FLOAT3 && pos->Type != D3DDECLTYPE_FLOAT4)) return D3DERR_INVALIDCALL;
        if (nrm && nrm->Type != D3DDECLTYPE_FLOAT3) nrm = nullptr;
        const BYTE* s = (const BYTE*)src;
        BYTE* d = (BYTE*)dst;
        if (s != d) std::memmove(d, s, (size_t)m_numVertices * stride);
        std::vector<float> acc((size_t)m_numVertices * 6, 0.0f);
        std::vector<bool> touched(m_numVertices, false);
        for (size_t b = 0; b < m_bones.size(); ++b) {
            const D3DXMATRIX& m = bones[b];
            const D3DXMATRIX& n = invTranspose ? invTranspose[b] : bones[b];
            const Bone& bone = m_bones[b];
            for (size_t i = 0; i < bone.vertices.size(); ++i) {
                const DWORD v = bone.vertices[i];
                if (v >= m_numVertices) continue;
                const float w = bone.weights[i];
                float* a = &acc[(size_t)v * 6];
                const float* p = (const float*)(s + (size_t)v * stride + pos->Offset);
                for (int c = 0; c < 3; ++c)
                    a[c] += w * (p[0] * m.m[0][c] + p[1] * m.m[1][c] + p[2] * m.m[2][c] + m.m[3][c]);
                if (nrm) {
                    const float* q = (const float*)(s + (size_t)v * stride + nrm->Offset);
                    for (int c = 0; c < 3; ++c) a[3 + c] += w * (q[0] * n.m[0][c] + q[1] * n.m[1][c] + q[2] * n.m[2][c]);
                }
                touched[v] = true;
            }
        }
        for (DWORD v = 0; v < m_numVertices; ++v) {
            if (!touched[v]) continue;
            std::memcpy(d + (size_t)v * stride + pos->Offset, &acc[(size_t)v * 6], 12);
            if (nrm) std::memcpy(d + (size_t)v * stride + nrm->Offset, &acc[(size_t)v * 6 + 3], 12);
        }
        return D3D_OK;
    }

    STDMETHOD(ConvertToBlendedMesh)(THIS_ LPD3DXMESH mesh, DWORD options, CONST DWORD* adjIn, LPDWORD adjOut, DWORD* faceRemap,
                                    LPD3DXBUFFER* vertexRemap, DWORD* maxFaceInfl, DWORD* numCombinations,
                                    LPD3DXBUFFER* combinations, LPD3DXMESH* out)
    {
        return Convert(mesh, options, false, 0, adjIn, adjOut, faceRemap, vertexRemap, maxFaceInfl, numCombinations, combinations, out);
    }
    STDMETHOD(ConvertToIndexedBlendedMesh)(THIS_ LPD3DXMESH mesh, DWORD options, DWORD paletteSize, CONST DWORD* adjIn,
                                           LPDWORD adjOut, DWORD* faceRemap, LPD3DXBUFFER* vertexRemap, DWORD* maxVertexInfl,
                                           DWORD* numCombinations, LPD3DXBUFFER* combinations, LPD3DXMESH* out)
    {
        return Convert(mesh, options, true, paletteSize, adjIn, adjOut, faceRemap, vertexRemap, maxVertexInfl, numCombinations,
                       combinations, out);
    }

private:
    struct Bone
    {
        std::string name;
        bool named = false;
        D3DXMATRIX offset;
        std::vector<DWORD> vertices;
        std::vector<float> weights;
    };

    // Per-vertex influences, strongest first; `filtered` drops weights at or below the minimum
    // and normalises the rest (the blended-mesh view).
    VertexInfluences Influences(DWORD numVertices, bool filtered) const
    {
        VertexInfluences infl(numVertices);
        for (DWORD b = 0; b < m_bones.size(); ++b) {
            const Bone& bone = m_bones[b];
            for (size_t i = 0; i < bone.vertices.size(); ++i) {
                if (bone.vertices[i] >= numVertices) continue;
                if (filtered && !(bone.weights[i] > m_minInfluence)) continue;
                infl[bone.vertices[i]].push_back({ b, bone.weights[i] });
            }
        }
        if (filtered) {
            for (std::vector<Influence>& v : infl) {
                std::stable_sort(v.begin(), v.end(), [](const Influence& a, const Influence& b) { return a.weight > b.weight; });
                Normalise(v);
            }
        }
        return infl;
    }

    HRESULT Convert(LPD3DXMESH mesh, DWORD options, bool indexed, DWORD paletteSize, const DWORD* adjIn, DWORD* adjOut,
                    DWORD* faceRemap, LPD3DXBUFFER* vertexRemapOut, DWORD* maxInflOut, DWORD* numCombinationsOut,
                    LPD3DXBUFFER* combinationsOut, LPD3DXMESH* out)
    {
        if (vertexRemapOut) *vertexRemapOut = nullptr;
        if (combinationsOut) *combinationsOut = nullptr;
        if (out) *out = nullptr;
        MeshCore* src = CoreOf(mesh);
        if (!src || !out || (indexed && paletteSize == 0)) return D3DERR_INVALIDCALL;
        const DWORD nf = src->numFaces, nv = src->numVertices;
        HRESULT hr;
        std::vector<DWORD> idx;
        std::vector<BYTE> verts;
        if (FAILED(hr = src->ReadIndices(idx)) || FAILED(hr = src->ReadVertices(verts))) return hr;
        for (DWORD i : idx) if (i >= nv) return D3DERR_INVALIDCALL;
        std::vector<DWORD> adj;
        if (adjIn) adj.assign(adjIn, adjIn + (size_t)nf * 3);   // adjIn may alias adjOut

        // Influences, fitted to what the vertex format and the palette can carry.
        VertexInfluences infl = Influences(nv, true);
        if (indexed) {
            for (std::vector<Influence>& v : infl)
                if (v.size() > 4) { v.resize(4); Normalise(v); }
            if (paletteSize < 12) FitFaces(infl, idx, nf, paletteSize);
        } else {
            FitFaces(infl, idx, nf, 4);
        }
        DWORD maxVertex = 1, maxFace = 1;
        for (const std::vector<Influence>& v : infl) maxVertex = std::max(maxVertex, (DWORD)v.size());
        for (DWORD f = 0; f < nf; ++f) maxFace = std::max(maxFace, (DWORD)FaceBones(infl, &idx[3 * f]).size());
        const DWORD limit = indexed ? paletteSize : maxFace;
        const DWORD slots = indexed ? paletteSize : maxFace;   // BoneId entries per combination

        // Bone combinations: first fit per attribute, attributes ascending, faces in order.
        struct Combo { DWORD attrib; std::vector<DWORD> bones; };
        std::vector<Combo> combos;
        std::vector<DWORD> faceCombo(nf);
        std::vector<DWORD> byAttrib(nf);
        for (DWORD f = 0; f < nf; ++f) byAttrib[f] = f;
        std::stable_sort(byAttrib.begin(), byAttrib.end(), [&](DWORD a, DWORD b) { return src->attribs[a] < src->attribs[b]; });
        size_t firstOfAttrib = 0;
        for (DWORD k = 0; k < nf; ++k) {
            const DWORD f = byAttrib[k];
            const DWORD attrib = src->attribs[f];
            if (k == 0 || attrib != src->attribs[byAttrib[k - 1]]) firstOfAttrib = combos.size();
            const std::vector<DWORD> bones = FaceBones(infl, &idx[3 * f]);
            size_t c = firstOfAttrib;
            for (; c < combos.size(); ++c) {
                size_t extra = 0;
                for (DWORD b : bones) if (!Has(combos[c].bones, b)) ++extra;
                if (combos[c].bones.size() + extra <= limit) break;
            }
            if (c == combos.size()) combos.push_back({ attrib, {} });
            for (DWORD b : bones) if (!Has(combos[c].bones, b)) combos[c].bones.push_back(b);
            faceCombo[f] = (DWORD)c;
        }

        // Faces ordered by combination, each combination with its own vertices.
        const Layout L = PlanLayout(idx, faceCombo, nv, true, true, true, false);
        const DWORD newVerts = (DWORD)L.vertexSource.size();
        std::vector<DWORD> vertexCombo(newVerts, 0);
        for (DWORD f = 0; f < nf; ++f)
            for (int c = 0; c < 3; ++c) vertexCombo[L.indices[3 * f + c]] = faceCombo[L.faceOrder[f]];

        // Output layout: the source elements with the blend data right after the position.
        const DWORD weights = (indexed ? maxVertex : maxFace) - 1;
        D3DVERTEXELEMENT9 decl[MAX_FVF_DECL_SIZE];
        UINT n = 0;
        WORD offset = 0;
        auto add = [&](const D3DVERTEXELEMENT9& e) {
            if (n + 1 >= MAX_FVF_DECL_SIZE) return;
            decl[n] = e;
            decl[n].Stream = 0;
            decl[n].Offset = offset;
            offset = (WORD)(offset + DeclTypeSize(e.Type));
            ++n;
        };
        for (UINT i = 0, len = DeclLength(src->decl); i < len; ++i) {
            const D3DVERTEXELEMENT9& e = src->decl[i];
            if (e.Usage == D3DDECLUSAGE_BLENDWEIGHT || e.Usage == D3DDECLUSAGE_BLENDINDICES) continue;
            add(e);
            if (e.Usage == D3DDECLUSAGE_POSITION && e.UsageIndex == 0) {
                if (weights) add({ 0, 0, (BYTE)(D3DDECLTYPE_FLOAT1 + weights - 1), D3DDECLMETHOD_DEFAULT, D3DDECLUSAGE_BLENDWEIGHT, 0 });
                if (indexed) add({ 0, 0, D3DDECLTYPE_UBYTE4, D3DDECLMETHOD_DEFAULT, D3DDECLUSAGE_BLENDINDICES, 0 });
            }
        }
        const D3DVERTEXELEMENT9 end = D3DDECL_END();
        decl[n] = end;
        const D3DVERTEXELEMENT9* weightEl = FindElement(decl, D3DDECLUSAGE_BLENDWEIGHT, 0);
        const D3DVERTEXELEMENT9* indexEl = FindElement(decl, D3DDECLUSAGE_BLENDINDICES, 0);

        ID3DXMesh* result;
        MeshCore* dst;
        NewMesh(&result, &dst);
        if (newVerts > 0xFFFF) options |= D3DXMESH_32BIT;
        if (FAILED(hr = dst->Init(nf, newVerts, options, decl, src->device))) { result->Release(); return hr; }

        std::vector<BYTE> gathered((size_t)newVerts * src->stride);
        for (DWORD v = 0; v < newVerts; ++v)
            std::memcpy(&gathered[(size_t)v * src->stride], &verts[(size_t)L.vertexSource[v] * src->stride], src->stride);
        std::vector<BYTE> outVerts((size_t)newVerts * dst->stride);
        ConvertVertices(gathered.data(), src->decl, src->stride, outVerts.data(), dst->decl, dst->stride, newVerts);
        for (DWORD v = 0; v < newVerts; ++v) {
            BYTE* p = &outVerts[(size_t)v * dst->stride];
            const std::vector<Influence>& vi = infl[L.vertexSource[v]];
            const std::vector<DWORD>& palette = combos[vertexCombo[v]].bones;
            float w[4] = { 0, 0, 0, 0 };
            BYTE slot[4] = { 0, 0, 0, 0 };
            if (indexed) {
                for (size_t k = 0; k < vi.size() && k < 4; ++k) {
                    w[k] = vi[k].weight;
                    slot[k] = (BYTE)(std::find(palette.begin(), palette.end(), vi[k].bone) - palette.begin());
                }
            } else {
                // Weight slot k belongs to the combination's k-th bone.
                for (size_t k = 0; k < palette.size() && k < 4; ++k)
                    for (const Influence& i : vi)
                        if (i.bone == palette[k]) w[k] = i.weight;
            }
            if (weightEl) std::memcpy(p + weightEl->Offset, w, 4 * weights);
            if (indexEl) std::memcpy(p + indexEl->Offset, slot, 4);
        }
        if (FAILED(hr = dst->WriteVertices(outVerts.data())) || FAILED(hr = dst->WriteIndices(L.indices))) {
            result->Release();
            return hr;
        }
        for (DWORD f = 0; f < nf; ++f) dst->attribs[f] = faceCombo[L.faceOrder[f]];
        dst->BuildTableFromRuns(L.indices);

        // Bone combination table: the structs, then the BoneId arrays they point into.
        const size_t headBytes = combos.size() * sizeof(D3DXBONECOMBINATION);
        ID3DXBuffer* table = nullptr;
        if (FAILED(hr = CreateBuffer((DWORD)(headBytes + combos.size() * slots * sizeof(DWORD)), nullptr, &table))) {
            result->Release();
            return hr;
        }
        BYTE* tb = (BYTE*)table->GetBufferPointer();
        D3DXBONECOMBINATION* bc = (D3DXBONECOMBINATION*)tb;
        DWORD* ids = (DWORD*)(tb + headBytes);
        for (size_t c = 0; c < combos.size(); ++c) {
            bc[c].AttribId = combos[c].attrib;
            bc[c].FaceStart = bc[c].FaceCount = bc[c].VertexStart = bc[c].VertexCount = 0;
            for (const D3DXATTRIBUTERANGE& r : dst->table) {
                if (r.AttribId != c) continue;
                bc[c].FaceStart = r.FaceStart; bc[c].FaceCount = r.FaceCount;
                bc[c].VertexStart = r.VertexStart; bc[c].VertexCount = r.VertexCount;
            }
            bc[c].BoneId = ids + c * slots;
            for (DWORD s = 0; s < slots; ++s) bc[c].BoneId[s] = s < combos[c].bones.size() ? combos[c].bones[s] : UINT_MAX;
        }

        if (faceRemap) std::copy(L.faceOrder.begin(), L.faceOrder.end(), faceRemap);
        if (adjOut) {
            if (adjIn) {
                std::vector<DWORD> newOfFace(nf);
                for (DWORD f = 0; f < nf; ++f) newOfFace[L.faceOrder[f]] = f;
                for (DWORD f = 0; f < nf; ++f)
                    for (int e = 0; e < 3; ++e) {
                        const DWORD g = adj[3 * L.faceOrder[f] + e];
                        adjOut[3 * f + e] = g < nf ? newOfFace[g] : UNUSED32;
                    }
            } else {
                dst->GenerateAdjacency(0.0f, adjOut);
            }
        }
        if (vertexRemapOut) CreateBuffer(newVerts * sizeof(DWORD), L.vertexSource.data(), vertexRemapOut);
        if (maxInflOut) *maxInflOut = indexed ? maxVertex : maxFace;
        if (numCombinationsOut) *numCombinationsOut = (DWORD)combos.size();
        if (combinationsOut) *combinationsOut = table;
        else table->Release();
        *out = result;
        return D3D_OK;
    }

    DWORD m_numVertices = 0;
    std::vector<Bone> m_bones;
    D3DVERTEXELEMENT9 m_decl[MAX_FVF_DECL_SIZE];
    DWORD m_fvf = 0;
    float m_minInfluence = 0.0f;
};

} // namespace

extern "C" {

HRESULT WINAPI D3DXCreateSkinInfo(DWORD numVertices, CONST D3DVERTEXELEMENT9* decl, DWORD numBones, LPD3DXSKININFO* out)
{
    if (!out || !decl) return D3DERR_INVALIDCALL;
    *out = nullptr;
    SkinInfo* s = new SkinInfo;
    HRESULT hr = s->Init(numVertices, decl, numBones);
    if (FAILED(hr)) { s->Release(); return hr; }
    *out = s;
    return D3D_OK;
}

HRESULT WINAPI D3DXCreateSkinInfoFVF(DWORD numVertices, DWORD fvf, DWORD numBones, LPD3DXSKININFO* out)
{
    if (out) *out = nullptr;
    D3DVERTEXELEMENT9 decl[MAX_FVF_DECL_SIZE];
    HRESULT hr = D3DXDeclaratorFromFVF(fvf, decl);
    return FAILED(hr) ? hr : D3DXCreateSkinInfo(numVertices, decl, numBones, out);
}

} // extern "C"
