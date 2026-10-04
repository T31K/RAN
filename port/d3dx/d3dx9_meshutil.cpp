// D3DX mesh utilities for the native build (plan Phase 3): normals, tangent frames, bounding
// boxes, D3DXCleanMesh, D3DXWeldVertices, D3DXValidMesh. Behaviour follows D3DX9, simplified:
//   - D3DXComputeNormals: angle-weighted face normals (front faces are clockwise, as in D3D),
//     shared across vertices the adjacency marks as one point.
//   - D3DXComputeTangentFrameEx: per-vertex tangent frames from the texture coordinates, weighted
//     by angle (or area/equal), Gram-Schmidt orthogonalised against the normal. Vertices are
//     never split, so the partial-edge/singular-point/normal-edge thresholds are ignored and the
//     vertex mapping is the identity.
//   - D3DXCleanMesh returns a copy and passes the adjacency through (no bowtie/backface fixes).
//   - D3DXWeldVertices merges vertices whose components all lie within the epsilons, then
//     attribute-sorts and compacts like D3DX (vertices shared between attribute groups stay
//     split unless D3DXWELDEPSILONS_DONOTSPLIT). WELDPARTIALMATCHES is not implemented.
//   - D3DXValidMesh checks index ranges and adjacency consistency.
#include "d3dx9_mesh_impl.h"
#include <algorithm>
#include <cmath>
#include <cstdio>
#include <cstring>
#include <string>

using namespace ran_d3dx;

namespace {

struct Vec3
{
    float x, y, z;
};
Vec3 operator-(const Vec3& a, const Vec3& b) { return { a.x - b.x, a.y - b.y, a.z - b.z }; }
Vec3 operator+(const Vec3& a, const Vec3& b) { return { a.x + b.x, a.y + b.y, a.z + b.z }; }
Vec3 operator*(const Vec3& a, float s) { return { a.x * s, a.y * s, a.z * s }; }
float Dot(const Vec3& a, const Vec3& b) { return a.x * b.x + a.y * b.y + a.z * b.z; }
Vec3 Cross(const Vec3& a, const Vec3& b) { return { a.y * b.z - a.z * b.y, a.z * b.x - a.x * b.z, a.x * b.y - a.y * b.x }; }
float Length(const Vec3& a) { return std::sqrt(Dot(a, a)); }
Vec3 Normalize(const Vec3& a)
{
    const float l = Length(a);
    return l > 0.0f ? a * (1.0f / l) : Vec3{ 0, 0, 0 };
}

// Angle at corner p between the edges to a and b.
float CornerAngle(const Vec3& p, const Vec3& a, const Vec3& b)
{
    const Vec3 u = Normalize(a - p), v = Normalize(b - p);
    return std::acos(std::max(-1.0f, std::min(1.0f, Dot(u, v))));
}

// Locked view of a mesh's vertices and indices through the public interface (works for
// ID3DXBaseMesh, i.e. also for progressive meshes).
class MeshAccess
{
public:
    explicit MeshAccess(ID3DXBaseMesh* mesh) : m_mesh(mesh) {}
    ~MeshAccess()
    {
        if (vertices) m_mesh->UnlockVertexBuffer();
    }
    HRESULT Open(bool writeVertices)
    {
        if (FAILED(m_mesh->GetDeclaration(decl))) return D3DERR_INVALIDCALL;
        stride = m_mesh->GetNumBytesPerVertex();
        numVertices = m_mesh->GetNumVertices();
        numFaces = m_mesh->GetNumFaces();
        void* p = nullptr;
        HRESULT hr = m_mesh->LockIndexBuffer(D3DLOCK_READONLY, &p);
        if (FAILED(hr)) return hr;
        indices.resize((size_t)numFaces * 3);
        if (m_mesh->GetOptions() & D3DXMESH_32BIT) std::memcpy(indices.data(), p, indices.size() * 4);
        else for (size_t i = 0; i < indices.size(); ++i) indices[i] = ((const WORD*)p)[i];
        m_mesh->UnlockIndexBuffer();
        for (DWORD& i : indices) if (i >= numVertices) return D3DERR_INVALIDCALL;
        if (FAILED(hr = m_mesh->LockVertexBuffer(writeVertices ? 0 : D3DLOCK_READONLY, &p))) return hr;
        vertices = (BYTE*)p;
        return D3D_OK;
    }
    Vec3 Read3(const D3DVERTEXELEMENT9* e, DWORD v) const
    {
        float f[4];
        DecodeElement(e->Type, vertices + (size_t)v * stride + e->Offset, f);
        return { f[0], f[1], f[2] };
    }
    void Write3(const D3DVERTEXELEMENT9* e, DWORD v, const Vec3& a, float w = 1.0f)
    {
        const float f[4] = { a.x, a.y, a.z, w };
        EncodeElement(e->Type, f, vertices + (size_t)v * stride + e->Offset);
    }

    D3DVERTEXELEMENT9 decl[MAX_FVF_DECL_SIZE];
    DWORD stride = 0, numVertices = 0, numFaces = 0;
    std::vector<DWORD> indices;
    BYTE* vertices = nullptr;

private:
    ID3DXBaseMesh* m_mesh;
};

// The epsilon D3DXWeldVertices uses for one vertex element (negative = must match exactly).
float WeldEpsilon(const D3DXWELDEPSILONS& e, const D3DVERTEXELEMENT9& el)
{
    switch (el.Usage) {
    case D3DDECLUSAGE_POSITION: return e.Position;
    case D3DDECLUSAGE_BLENDWEIGHT: return e.BlendWeights;
    case D3DDECLUSAGE_NORMAL: return e.Normal;
    case D3DDECLUSAGE_PSIZE: return e.PSize;
    case D3DDECLUSAGE_COLOR: return el.UsageIndex == 0 ? e.Diffuse : (el.UsageIndex == 1 ? e.Specular : -1.0f);
    case D3DDECLUSAGE_TEXCOORD: return el.UsageIndex < 8 ? e.Texcoord[el.UsageIndex] : -1.0f;
    case D3DDECLUSAGE_TANGENT: return e.Tangent;
    case D3DDECLUSAGE_BINORMAL: return e.Binormal;
    case D3DDECLUSAGE_TESSFACTOR: return e.TessFactor;
    default: return -1.0f;
    }
}

bool WeldMatch(const BYTE* a, const BYTE* b, const D3DVERTEXELEMENT9* decl, UINT n, const D3DXWELDEPSILONS& eps)
{
    for (UINT i = 0; i < n; ++i) {
        const D3DVERTEXELEMENT9& el = decl[i];
        const float e = WeldEpsilon(eps, el);
        if (e < 0.0f) {
            if (std::memcmp(a + el.Offset, b + el.Offset, DeclTypeSize(el.Type)) != 0) return false;
            continue;
        }
        float fa[4], fb[4];
        DecodeElement(el.Type, a + el.Offset, fa);
        DecodeElement(el.Type, b + el.Offset, fb);
        for (int c = 0; c < 4; ++c)
            if (!(std::fabs(fa[c] - fb[c]) <= e)) return false;
    }
    return true;
}

} // namespace

extern "C" {

HRESULT WINAPI D3DXComputeBoundingBox(CONST D3DXVECTOR3* first, DWORD count, DWORD stride, D3DXVECTOR3* mn, D3DXVECTOR3* mx)
{
    if (!first || !mn || !mx) return D3DERR_INVALIDCALL;
    const BYTE* p = (const BYTE*)first;
    *mn = *mx = *first;
    for (DWORD i = 1; i < count; ++i) {
        const D3DXVECTOR3& v = *(const D3DXVECTOR3*)(p + (size_t)i * stride);
        mn->x = std::min(mn->x, v.x); mn->y = std::min(mn->y, v.y); mn->z = std::min(mn->z, v.z);
        mx->x = std::max(mx->x, v.x); mx->y = std::max(mx->y, v.y); mx->z = std::max(mx->z, v.z);
    }
    return D3D_OK;
}

HRESULT WINAPI D3DXComputeNormals(LPD3DXBASEMESH mesh, CONST DWORD* adjacency)
{
    if (!mesh) return D3DERR_INVALIDCALL;
    MeshAccess m(mesh);
    HRESULT hr = m.Open(true);
    if (FAILED(hr)) return hr;
    const D3DVERTEXELEMENT9* pos = FindElement(m.decl, D3DDECLUSAGE_POSITION, 0);
    const D3DVERTEXELEMENT9* nrm = FindElement(m.decl, D3DDECLUSAGE_NORMAL, 0);
    if (!pos || !nrm) return D3DERR_INVALIDCALL;
    std::vector<DWORD> prep(m.numVertices);
    PointRepsFromAdjacency(m.indices, m.numFaces, m.numVertices, adjacency, prep.data());
    std::vector<Vec3> sum(m.numVertices, Vec3{ 0, 0, 0 });
    for (DWORD f = 0; f < m.numFaces; ++f) {
        const DWORD* i = &m.indices[3 * f];
        const Vec3 p[3] = { m.Read3(pos, i[0]), m.Read3(pos, i[1]), m.Read3(pos, i[2]) };
        const Vec3 n = Normalize(Cross(p[1] - p[0], p[2] - p[0]));
        for (int c = 0; c < 3; ++c) {
            const float w = CornerAngle(p[c], p[(c + 1) % 3], p[(c + 2) % 3]);
            sum[prep[i[c]]] = sum[prep[i[c]]] + n * w;
        }
    }
    for (DWORD v = 0; v < m.numVertices; ++v) m.Write3(nrm, v, Normalize(sum[prep[v]]), 0.0f);
    return D3D_OK;
}

HRESULT WINAPI D3DXComputeTangentFrameEx(ID3DXMesh* mesh, DWORD texSemantic, DWORD texIndex, DWORD uSemantic, DWORD uIndex,
                                         DWORD vSemantic, DWORD vIndex, DWORD nSemantic, DWORD nIndex, DWORD options,
                                         CONST DWORD* adjacency, FLOAT, FLOAT, FLOAT, ID3DXMesh** out, ID3DXBuffer** mapping)
{
    if (out) *out = nullptr;
    if (mapping) *mapping = nullptr;
    const bool inPlace = (options & D3DXTANGENT_GENERATE_IN_PLACE) != 0;
    if (!mesh || (!inPlace && !out)) return D3DERR_INVALIDCALL;

    ID3DXMesh* target = mesh;
    HRESULT hr;
    if (!inPlace) {
        D3DVERTEXELEMENT9 decl[MAX_FVF_DECL_SIZE];
        IDirect3DDevice9* dev = nullptr;
        if (FAILED(hr = mesh->GetDeclaration(decl)) || FAILED(hr = mesh->GetDevice(&dev))) return hr;
        hr = mesh->CloneMesh(mesh->GetOptions(), decl, dev, &target);
        dev->Release();
        if (FAILED(hr)) return hr;
    }
    auto fail = [&](HRESULT e) { if (target != mesh) target->Release(); return e; };

    {
        MeshAccess m(target);
        if (FAILED(hr = m.Open(true))) return fail(hr);
        auto find = [&](DWORD sem, DWORD idx) -> const D3DVERTEXELEMENT9* {
            return sem == (DWORD)D3DX_DEFAULT ? nullptr : FindElement(m.decl, (BYTE)sem, (BYTE)idx);
        };
        const D3DVERTEXELEMENT9* pos = FindElement(m.decl, D3DDECLUSAGE_POSITION, 0);
        const D3DVERTEXELEMENT9* tex = find(texSemantic, texIndex);
        const D3DVERTEXELEMENT9* uOut = find(uSemantic, uIndex);
        const D3DVERTEXELEMENT9* vOut = find(vSemantic, vIndex);
        const D3DVERTEXELEMENT9* nOut = find(nSemantic, nIndex);
        const bool calcNormals = (options & D3DXTANGENT_CALCULATE_NORMALS) != 0;
        if (!pos || (uSemantic != (DWORD)D3DX_DEFAULT && !uOut) || (vSemantic != (DWORD)D3DX_DEFAULT && !vOut) ||
            (nSemantic != (DWORD)D3DX_DEFAULT && !nOut) || ((uOut || vOut) && !tex) || (calcNormals && !nOut))
            return fail(D3DERR_INVALIDCALL);

        std::vector<DWORD> prep(m.numVertices);
        PointRepsFromAdjacency(m.indices, m.numFaces, m.numVertices, adjacency, prep.data());
        std::vector<Vec3> sumU(m.numVertices, Vec3{ 0, 0, 0 }), sumV = sumU, sumN = sumU;
        const float wind = (options & D3DXTANGENT_WIND_CW) ? -1.0f : 1.0f;
        for (DWORD f = 0; f < m.numFaces; ++f) {
            const DWORD* i = &m.indices[3 * f];
            const Vec3 p[3] = { m.Read3(pos, i[0]), m.Read3(pos, i[1]), m.Read3(pos, i[2]) };
            const Vec3 e1 = p[1] - p[0], e2 = p[2] - p[0];
            const Vec3 cross = Cross(e1, e2) * wind;
            const Vec3 n = Normalize(cross);
            Vec3 du{ 0, 0, 0 }, dv{ 0, 0, 0 };
            if (tex) {
                const Vec3 t[3] = { m.Read3(tex, i[0]), m.Read3(tex, i[1]), m.Read3(tex, i[2]) };
                const float u1 = t[1].x - t[0].x, v1 = t[1].y - t[0].y, u2 = t[2].x - t[0].x, v2 = t[2].y - t[0].y;
                const float r = u1 * v2 - u2 * v1;
                if (std::fabs(r) > 1e-20f) {
                    du = Normalize((e1 * v2 - e2 * v1) * (1.0f / r));
                    dv = Normalize((e2 * u1 - e1 * u2) * (1.0f / r));
                }
            }
            for (int c = 0; c < 3; ++c) {
                float w;
                if (options & D3DXTANGENT_WEIGHT_EQUAL) w = 1.0f;
                else if (options & D3DXTANGENT_WEIGHT_BY_AREA) w = 0.5f * Length(cross);
                else w = CornerAngle(p[c], p[(c + 1) % 3], p[(c + 2) % 3]);
                sumU[i[c]] = sumU[i[c]] + du * w;
                sumV[i[c]] = sumV[i[c]] + dv * w;
                sumN[prep[i[c]]] = sumN[prep[i[c]]] + n * w;
            }
        }
        const bool normalize = !(options & D3DXTANGENT_DONT_NORMALIZE_PARTIALS);
        const bool ortho = !(options & D3DXTANGENT_DONT_ORTHOGONALIZE);
        const bool fromV = (options & D3DXTANGENT_ORTHOGONALIZE_FROM_V) != 0;
        for (DWORD v = 0; v < m.numVertices; ++v) {
            const Vec3 n = calcNormals ? Normalize(sumN[prep[v]]) : (nOut ? Normalize(m.Read3(nOut, v)) : Vec3{ 0, 0, 0 });
            Vec3 u = sumU[v], w = sumV[v];
            const float lu = Length(u), lw = Length(w);
            if (ortho && Length(n) > 0.0f) {
                // Gram-Schmidt: the first partial against the normal, the second against both.
                Vec3& a = fromV ? w : u;
                Vec3& b = fromV ? u : w;
                a = Normalize(a - n * Dot(n, a));
                b = b - n * Dot(n, b);
                b = Normalize(b - a * Dot(a, b));
                if (!normalize) { u = u * lu; w = w * lw; }
            } else if (normalize) {
                u = Normalize(u);
                w = Normalize(w);
            }
            // w of a 4-component tangent: handedness of the frame.
            const float hand = Dot(Cross(n, u), w) < 0.0f ? -1.0f : 1.0f;
            if (uOut) m.Write3(uOut, v, u, hand);
            if (vOut) m.Write3(vOut, v, w, hand);
            if (nOut && calcNormals) m.Write3(nOut, v, n, 0.0f);
        }
    }

    if (mapping) {
        const DWORD nv = target->GetNumVertices();
        std::vector<DWORD> identity(nv);
        for (DWORD v = 0; v < nv; ++v) identity[v] = v;
        if (FAILED(hr = CreateBuffer(nv * sizeof(DWORD), identity.data(), mapping))) return fail(hr);
    }
    if (!inPlace) *out = target;
    return D3D_OK;
}

HRESULT WINAPI D3DXComputeTangentFrame(ID3DXMesh* mesh, DWORD options)
{
    return D3DXComputeTangentFrameEx(mesh, D3DDECLUSAGE_TEXCOORD, 0, D3DDECLUSAGE_TANGENT, 0, D3DDECLUSAGE_BINORMAL, 0,
                                     D3DDECLUSAGE_NORMAL, 0, options | D3DXTANGENT_GENERATE_IN_PLACE, nullptr,
                                     0.01f, 0.25f, 0.01f, nullptr, nullptr);
}

// A copy of the mesh; bowtie and back-facing fixes are not performed.
HRESULT WINAPI D3DXCleanMesh(D3DXCLEANTYPE, LPD3DXMESH in, CONST DWORD* adjIn, LPD3DXMESH* out, DWORD* adjOut, LPD3DXBUFFER* errors)
{
    if (errors) *errors = nullptr;
    if (!out) return D3DERR_INVALIDCALL;
    *out = nullptr;
    MeshCore* core = CoreOf(in);
    if (!core) return D3DERR_INVALIDCALL;
    HRESULT hr = in->CloneMesh(core->options, core->decl, core->device, out);
    if (FAILED(hr)) return hr;
    if (adjOut) {
        if (adjIn) std::memmove(adjOut, adjIn, (size_t)core->numFaces * 3 * sizeof(DWORD));
        else core->GenerateAdjacency(0.0f, adjOut);
    }
    return D3D_OK;
}

HRESULT WINAPI D3DXWeldVertices(LPD3DXMESH mesh, DWORD flags, CONST D3DXWELDEPSILONS* epsilons, CONST DWORD* adjIn,
                                DWORD* adjOut, DWORD* faceRemap, LPD3DXBUFFER* vertexRemap)
{
    if (vertexRemap) *vertexRemap = nullptr;
    MeshCore* core = CoreOf(mesh);
    if (!core) return D3DERR_INVALIDCALL;
    D3DXWELDEPSILONS eps;
    if (epsilons) {
        eps = *epsilons;
    } else {
        // D3DX: no epsilons = 1e-6 for every component.
        eps.Position = eps.BlendWeights = eps.Normal = eps.PSize = eps.Specular = eps.Diffuse = 1.0e-6f;
        for (float& t : eps.Texcoord) t = 1.0e-6f;
        eps.Tangent = eps.Binormal = eps.TessFactor = 1.0e-6f;
    }
    const D3DVERTEXELEMENT9* pos = FindElement(core->decl, D3DDECLUSAGE_POSITION, 0);
    if (!pos || (pos->Type != D3DDECLTYPE_FLOAT3 && pos->Type != D3DDECLTYPE_FLOAT4)) return D3DERR_INVALIDCALL;
    HRESULT hr;
    std::vector<DWORD> idx;
    std::vector<BYTE> verts;
    if (FAILED(hr = core->ReadIndices(idx)) || FAILED(hr = core->ReadVertices(verts))) return hr;
    const DWORD nv = core->numVertices, nf = core->numFaces;
    std::vector<DWORD> adj;
    if (adjIn) adj.assign(adjIn, adjIn + (size_t)nf * 3);   // adjIn may alias adjOut

    // Candidates: vertices sorted by position; only neighbours within the position epsilon compare.
    auto P = [&](DWORD v) { return (const float*)(&verts[(size_t)v * core->stride + pos->Offset]); };
    std::vector<DWORD> order(nv);
    for (DWORD v = 0; v < nv; ++v) order[v] = v;
    std::sort(order.begin(), order.end(), [&](DWORD a, DWORD b) {
        const float* p = P(a); const float* q = P(b);
        if (p[0] != q[0]) return p[0] < q[0];
        if (p[1] != q[1]) return p[1] < q[1];
        if (p[2] != q[2]) return p[2] < q[2];
        return a < b;
    });
    std::vector<DWORD> rep(nv);
    for (DWORD v = 0; v < nv; ++v) rep[v] = v;
    auto find = [&](DWORD x) { while (rep[x] != x) { rep[x] = rep[rep[x]]; x = rep[x]; } return x; };
    auto join = [&](DWORD a, DWORD b) { a = find(a); b = find(b); if (a < b) rep[b] = a; else if (b < a) rep[a] = b; };
    const float pe = std::max(0.0f, eps.Position);
    const UINT declLen = DeclLength(core->decl);
    for (DWORD i = 0; i < nv; ++i) {
        const float* p = P(order[i]);
        for (DWORD j = i + 1; j < nv; ++j) {
            const float* q = P(order[j]);
            if (q[0] - p[0] > pe) break;
            if (pe == 0.0f && (q[1] != p[1] || q[2] != p[2])) break;   // exact: equal positions are contiguous
            if (WeldMatch(&verts[(size_t)order[i] * core->stride], &verts[(size_t)order[j] * core->stride], core->decl, declLen, eps))
                join(order[i], order[j]);
        }
    }
    if (flags & D3DXWELDEPSILONS_WELDALL) {
        std::vector<DWORD> prep(nv);
        if (adjIn) PointRepsFromAdjacency(idx, nf, nv, adj.data(), prep.data());
        else prep = PointRepsFromPositions(verts.data(), core->stride, pos->Offset, nv, eps.Position);
        for (DWORD v = 0; v < nv; ++v) join(v, prep[v]);
    }
    for (DWORD& i : idx) if (i < nv) i = find(i);
    if (FAILED(hr = core->WriteIndices(idx))) return hr;

    // D3DX then attribute-sorts: welded vertices used by several attribute groups split again.
    DWORD opt = D3DXMESHOPT_ATTRSORT;
    if (!(flags & D3DXWELDEPSILONS_DONOTREMOVEVERTICES)) opt |= D3DXMESHOPT_COMPACT;
    if (flags & D3DXWELDEPSILONS_DONOTSPLIT) opt |= D3DXMESHOPT_DONOTSPLIT;
    std::vector<DWORD> remap;
    if (FAILED(hr = core->Optimize(opt, adjIn ? adj.data() : nullptr, adjOut, faceRemap, &remap))) return hr;
    if (vertexRemap) return CreateBuffer((DWORD)(remap.size() * sizeof(DWORD)), remap.data(), vertexRemap);
    return D3D_OK;
}

HRESULT WINAPI D3DXValidMesh(LPD3DXMESH mesh, CONST DWORD* adjacency, LPD3DXBUFFER* errors)
{
    if (errors) *errors = nullptr;
    MeshCore* core = CoreOf(mesh);
    if (!core) return D3DERR_INVALIDCALL;
    std::vector<DWORD> idx;
    HRESULT hr = core->ReadIndices(idx);
    if (FAILED(hr)) return hr;
    const DWORD nf = core->numFaces;
    std::string msg;
    char line[128];
    for (DWORD f = 0; f < nf; ++f) {
        for (int c = 0; c < 3; ++c) {
            if (idx[3 * f + c] >= core->numVertices) {
                std::snprintf(line, sizeof(line), "face %u: vertex index %u out of range\n", f, idx[3 * f + c]);
                msg += line;
            }
        }
        if (!adjacency) continue;
        for (int e = 0; e < 3; ++e) {
            const DWORD g = adjacency[3 * f + e];
            if (g == UNUSED32) continue;
            if (g >= nf || g == f) {
                std::snprintf(line, sizeof(line), "face %u: invalid neighbour %u\n", f, g);
                msg += line;
            } else if (adjacency[3 * g] != f && adjacency[3 * g + 1] != f && adjacency[3 * g + 2] != f) {
                std::snprintf(line, sizeof(line), "face %u: neighbour %u does not list it back\n", f, g);
                msg += line;
            }
        }
    }
    if (msg.empty()) return D3D_OK;
    if (errors) CreateBuffer((DWORD)msg.size() + 1, msg.c_str(), errors);
    return D3DXERR_INVALIDMESH;
}

} // extern "C"
