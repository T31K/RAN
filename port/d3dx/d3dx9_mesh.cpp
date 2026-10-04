// D3DX meshes for the native build (plan Phase 3): ID3DXBuffer, ID3DXMesh, ID3DXPMesh, vertex
// declarations (D3DXDeclaratorFromFVF & co.), D3DXCreateMesh/FVF and D3DXGeneratePMesh.
// Behaviour follows D3DX9:
//   - Vertex and index buffers are real device buffers; pool and usage come from the
//     D3DXMESH_* options (VB_/IB_ SYSTEMMEM/MANAGED/WRITEONLY/DYNAMIC, 32BIT index format).
//   - DrawSubset draws the attribute-table ranges of that attribute id; without a table it draws
//     every run of faces carrying the id. Clone* converts vertices element by element (matching
//     usage + usage index, converted between types, missing elements zero) and keeps indices,
//     attributes and the attribute table. Locking the attribute buffer for writing drops the
//     attribute table (as D3DX does); Optimize/SetAttributeTable rebuild it.
//   - Optimize/OptimizeInplace: ATTRSORT (also implied by VERTEXCACHE and STRIPREORDER) stably
//     sorts faces by attribute, gives every attribute run its own contiguous vertex range
//     (vertices shared between runs are duplicated unless DONOTSPLIT), builds the attribute
//     table and fills the face/vertex remaps and adjacency; COMPACT drops unused vertices.
//     No vertex-cache or strip reordering beyond that.
//   - Progressive meshes are not simplified: an ID3DXPMesh always renders the full mesh and
//     reports the full counts as both minimum and maximum; SetNumFaces/SetNumVertices/Trim*
//     are accepted.
// Mesh utilities live in d3dx9_meshutil.cpp, skinning in d3dx9_skin.cpp.
#include "d3dx9_mesh_impl.h"
#include <algorithm>
#include <cmath>
#include <cstring>

namespace ran_d3dx {

// ---- vertex declarations -------------------------------------------------------------------

UINT DeclTypeSize(BYTE type)
{
    switch (type) {
    case D3DDECLTYPE_FLOAT1: return 4;
    case D3DDECLTYPE_FLOAT2: return 8;
    case D3DDECLTYPE_FLOAT3: return 12;
    case D3DDECLTYPE_FLOAT4: return 16;
    case D3DDECLTYPE_D3DCOLOR: case D3DDECLTYPE_UBYTE4: case D3DDECLTYPE_SHORT2: case D3DDECLTYPE_UBYTE4N:
    case D3DDECLTYPE_SHORT2N: case D3DDECLTYPE_USHORT2N: case D3DDECLTYPE_UDEC3: case D3DDECLTYPE_DEC3N:
    case D3DDECLTYPE_FLOAT16_2: return 4;
    case D3DDECLTYPE_SHORT4: case D3DDECLTYPE_SHORT4N: case D3DDECLTYPE_USHORT4N: case D3DDECLTYPE_FLOAT16_4: return 8;
    default: return 0;
    }
}

UINT DeclLength(const D3DVERTEXELEMENT9* decl)
{
    UINT n = 0;
    while (decl && decl[n].Stream != 0xFF && n < MAXD3DDECLLENGTH) ++n;
    return n;
}

const D3DVERTEXELEMENT9* FindElement(const D3DVERTEXELEMENT9* decl, BYTE usage, BYTE index)
{
    for (UINT i = 0, n = DeclLength(decl); i < n; ++i)
        if (decl[i].Usage == usage && decl[i].UsageIndex == index) return &decl[i];
    return nullptr;
}

namespace {

float HalfToFloat(uint16_t h)
{
    const int e = (h >> 10) & 0x1F, m = h & 0x3FF;
    float v;
    if (e == 0) v = std::ldexp((float)m, -24);
    else if (e == 31) v = m ? NAN : INFINITY;
    else v = std::ldexp((float)(m | 0x400), e - 25);
    return (h & 0x8000) ? -v : v;
}

uint16_t FloatToHalf(float f)
{
    const uint16_t s = std::signbit(f) ? 0x8000 : 0;
    const float a = std::fabs(f);
    if (std::isnan(a)) return s | 0x7E00;
    if (a >= 65520.0f) return s | 0x7C00;
    if (a < 6.103515625e-05f) return s | (uint16_t)std::lround(a * 16777216.0f);   // subnormal
    int e;
    const float fr = std::frexp(a, &e);   // a = fr * 2^e, fr in [0.5, 1)
    int m = (int)std::lround((fr * 2.0f - 1.0f) * 1024.0f);
    int exp = e - 1 + 15;
    if (m == 1024) { m = 0; ++exp; }
    if (exp >= 31) return s | 0x7C00;
    return (uint16_t)(s | (exp << 10) | m);
}

float Clamp(float v, float lo, float hi) { return v < lo ? lo : (v > hi ? hi : v); }
BYTE ToUnorm8(float v) { return (BYTE)std::lround(Clamp(v, 0.0f, 1.0f) * 255.0f); }
uint16_t ToUnorm16(float v) { return (uint16_t)std::lround(Clamp(v, 0.0f, 1.0f) * 65535.0f); }
int16_t ToSnorm16(float v) { return (int16_t)std::lround(Clamp(v, -1.0f, 1.0f) * 32767.0f); }
int16_t ToShort(float v) { return (int16_t)std::lround(Clamp(v, -32768.0f, 32767.0f)); }

} // namespace

void DecodeElement(BYTE type, const BYTE* src, float out[4])
{
    out[0] = out[1] = out[2] = 0.0f;
    out[3] = 1.0f;
    switch (type) {
    case D3DDECLTYPE_FLOAT1: case D3DDECLTYPE_FLOAT2: case D3DDECLTYPE_FLOAT3: case D3DDECLTYPE_FLOAT4:
        std::memcpy(out, src, 4 * (type - D3DDECLTYPE_FLOAT1 + 1));
        break;
    case D3DDECLTYPE_D3DCOLOR:   // B, G, R, A in memory -> (r, g, b, a)
        out[0] = src[2] / 255.0f; out[1] = src[1] / 255.0f; out[2] = src[0] / 255.0f; out[3] = src[3] / 255.0f;
        break;
    case D3DDECLTYPE_UBYTE4:
        for (int i = 0; i < 4; ++i) out[i] = (float)src[i];
        break;
    case D3DDECLTYPE_UBYTE4N:
        for (int i = 0; i < 4; ++i) out[i] = src[i] / 255.0f;
        break;
    case D3DDECLTYPE_SHORT2: case D3DDECLTYPE_SHORT4: {
        int16_t s[4];
        const int n = type == D3DDECLTYPE_SHORT2 ? 2 : 4;
        std::memcpy(s, src, 2 * n);
        for (int i = 0; i < n; ++i) out[i] = (float)s[i];
        break;
    }
    case D3DDECLTYPE_SHORT2N: case D3DDECLTYPE_SHORT4N: {
        int16_t s[4];
        const int n = type == D3DDECLTYPE_SHORT2N ? 2 : 4;
        std::memcpy(s, src, 2 * n);
        for (int i = 0; i < n; ++i) out[i] = std::max(-1.0f, s[i] / 32767.0f);
        break;
    }
    case D3DDECLTYPE_USHORT2N: case D3DDECLTYPE_USHORT4N: {
        uint16_t s[4];
        const int n = type == D3DDECLTYPE_USHORT2N ? 2 : 4;
        std::memcpy(s, src, 2 * n);
        for (int i = 0; i < n; ++i) out[i] = s[i] / 65535.0f;
        break;
    }
    case D3DDECLTYPE_UDEC3: case D3DDECLTYPE_DEC3N: {
        uint32_t v;
        std::memcpy(&v, src, 4);
        for (int i = 0; i < 3; ++i) {
            const uint32_t c = (v >> (10 * i)) & 0x3FF;
            if (type == D3DDECLTYPE_UDEC3) out[i] = (float)c;
            else out[i] = std::max(-1.0f, (float)(int32_t)((c ^ 0x200) - 0x200) / 511.0f);
        }
        break;
    }
    case D3DDECLTYPE_FLOAT16_2: case D3DDECLTYPE_FLOAT16_4: {
        uint16_t h[4];
        const int n = type == D3DDECLTYPE_FLOAT16_2 ? 2 : 4;
        std::memcpy(h, src, 2 * n);
        for (int i = 0; i < n; ++i) out[i] = HalfToFloat(h[i]);
        break;
    }
    default:
        break;
    }
}

void EncodeElement(BYTE type, const float in[4], BYTE* dst)
{
    switch (type) {
    case D3DDECLTYPE_FLOAT1: case D3DDECLTYPE_FLOAT2: case D3DDECLTYPE_FLOAT3: case D3DDECLTYPE_FLOAT4:
        std::memcpy(dst, in, 4 * (type - D3DDECLTYPE_FLOAT1 + 1));
        break;
    case D3DDECLTYPE_D3DCOLOR:
        dst[0] = ToUnorm8(in[2]); dst[1] = ToUnorm8(in[1]); dst[2] = ToUnorm8(in[0]); dst[3] = ToUnorm8(in[3]);
        break;
    case D3DDECLTYPE_UBYTE4:
        for (int i = 0; i < 4; ++i) dst[i] = (BYTE)std::lround(Clamp(in[i], 0.0f, 255.0f));
        break;
    case D3DDECLTYPE_UBYTE4N:
        for (int i = 0; i < 4; ++i) dst[i] = ToUnorm8(in[i]);
        break;
    case D3DDECLTYPE_SHORT2: case D3DDECLTYPE_SHORT4: {
        int16_t s[4];
        const int n = type == D3DDECLTYPE_SHORT2 ? 2 : 4;
        for (int i = 0; i < n; ++i) s[i] = ToShort(in[i]);
        std::memcpy(dst, s, 2 * n);
        break;
    }
    case D3DDECLTYPE_SHORT2N: case D3DDECLTYPE_SHORT4N: {
        int16_t s[4];
        const int n = type == D3DDECLTYPE_SHORT2N ? 2 : 4;
        for (int i = 0; i < n; ++i) s[i] = ToSnorm16(in[i]);
        std::memcpy(dst, s, 2 * n);
        break;
    }
    case D3DDECLTYPE_USHORT2N: case D3DDECLTYPE_USHORT4N: {
        uint16_t s[4];
        const int n = type == D3DDECLTYPE_USHORT2N ? 2 : 4;
        for (int i = 0; i < n; ++i) s[i] = ToUnorm16(in[i]);
        std::memcpy(dst, s, 2 * n);
        break;
    }
    case D3DDECLTYPE_UDEC3: case D3DDECLTYPE_DEC3N: {
        uint32_t v = 0;
        for (int i = 0; i < 3; ++i) {
            uint32_t c;
            if (type == D3DDECLTYPE_UDEC3) c = (uint32_t)std::lround(Clamp(in[i], 0.0f, 1023.0f));
            else c = (uint32_t)(int32_t)std::lround(Clamp(in[i], -1.0f, 1.0f) * 511.0f) & 0x3FF;
            v |= c << (10 * i);
        }
        std::memcpy(dst, &v, 4);
        break;
    }
    case D3DDECLTYPE_FLOAT16_2: case D3DDECLTYPE_FLOAT16_4: {
        uint16_t h[4];
        const int n = type == D3DDECLTYPE_FLOAT16_2 ? 2 : 4;
        for (int i = 0; i < n; ++i) h[i] = FloatToHalf(in[i]);
        std::memcpy(dst, h, 2 * n);
        break;
    }
    default:
        break;
    }
}

void ConvertVertices(const BYTE* src, const D3DVERTEXELEMENT9* srcDecl, UINT srcStride,
                     BYTE* dst, const D3DVERTEXELEMENT9* dstDecl, UINT dstStride, DWORD count)
{
    struct Pair { const D3DVERTEXELEMENT9* d; const D3DVERTEXELEMENT9* s; };
    std::vector<Pair> pairs;
    for (UINT i = 0, n = DeclLength(dstDecl); i < n; ++i) {
        if (dstDecl[i].Stream != 0 || dstDecl[i].Type == D3DDECLTYPE_UNUSED) continue;
        const D3DVERTEXELEMENT9* s = FindElement(srcDecl, dstDecl[i].Usage, dstDecl[i].UsageIndex);
        if (s && s->Stream == 0 && s->Type != D3DDECLTYPE_UNUSED) pairs.push_back({ &dstDecl[i], s });
    }
    for (DWORD v = 0; v < count; ++v) {
        const BYTE* sv = src + (size_t)v * srcStride;
        BYTE* dv = dst + (size_t)v * dstStride;
        std::memset(dv, 0, dstStride);
        for (const Pair& p : pairs) {
            if (p.s->Type == p.d->Type) {
                std::memcpy(dv + p.d->Offset, sv + p.s->Offset, DeclTypeSize(p.d->Type));
            } else {
                float f[4];
                DecodeElement(p.s->Type, sv + p.s->Offset, f);
                EncodeElement(p.d->Type, f, dv + p.d->Offset);
            }
        }
    }
}

// ---- ID3DXBuffer ---------------------------------------------------------------------------

namespace {

class Buffer final : public ID3DXBuffer, public RefCounted
{
public:
    explicit Buffer(DWORD bytes) : m_data(bytes ? bytes : 1, 0), m_size(bytes) {}

    STDMETHOD(QueryInterface)(THIS_ REFIID iid, LPVOID* ppv)
    {
        if (!ppv) return E_POINTER;
        if (IsEqualGUID(iid, IID_IUnknown) || IsEqualGUID(iid, IID_ID3DXBuffer)) { *ppv = this; AddRef(); return S_OK; }
        *ppv = nullptr;
        return E_NOINTERFACE;
    }
    STDMETHOD_(ULONG, AddRef)(THIS) { return DoAddRef(); }
    STDMETHOD_(ULONG, Release)(THIS) { return DoRelease(); }
    STDMETHOD_(LPVOID, GetBufferPointer)(THIS) { return m_data.data(); }
    STDMETHOD_(DWORD, GetBufferSize)(THIS) { return m_size; }

private:
    std::vector<BYTE> m_data;
    DWORD m_size;
};

} // namespace

HRESULT CreateBuffer(DWORD bytes, const void* data, ID3DXBuffer** out)
{
    if (!out) return D3DERR_INVALIDCALL;
    Buffer* b = new Buffer(bytes);
    if (data && bytes) std::memcpy(b->GetBufferPointer(), data, bytes);
    *out = b;
    return D3D_OK;
}

// ---- topology helpers ----------------------------------------------------------------------

namespace {

// Union-find whose representative is always the smallest index of a set.
struct MinUnion
{
    std::vector<DWORD> parent;
    explicit MinUnion(DWORD n) : parent(n) { for (DWORD i = 0; i < n; ++i) parent[i] = i; }
    DWORD Find(DWORD x)
    {
        while (parent[x] != x) { parent[x] = parent[parent[x]]; x = parent[x]; }
        return x;
    }
    void Join(DWORD a, DWORD b)
    {
        a = Find(a); b = Find(b);
        if (a < b) parent[b] = a;
        else if (b < a) parent[a] = b;
    }
};

const D3DXVECTOR3& PositionAt(const BYTE* vertices, UINT stride, UINT offset, DWORD v)
{
    return *(const D3DXVECTOR3*)(vertices + (size_t)v * stride + offset);
}

} // namespace

std::vector<DWORD> PointRepsFromPositions(const BYTE* vertices, UINT stride, UINT positionOffset, DWORD count, float epsilon)
{
    std::vector<DWORD> order(count);
    for (DWORD i = 0; i < count; ++i) order[i] = i;
    auto pos = [&](DWORD v) -> const D3DXVECTOR3& { return PositionAt(vertices, stride, positionOffset, v); };
    std::sort(order.begin(), order.end(), [&](DWORD a, DWORD b) {
        const D3DXVECTOR3& p = pos(a); const D3DXVECTOR3& q = pos(b);
        if (p.x != q.x) return p.x < q.x;
        if (p.y != q.y) return p.y < q.y;
        if (p.z != q.z) return p.z < q.z;
        return a < b;
    });
    MinUnion u(count);
    const float eps = std::max(0.0f, epsilon);
    for (DWORD i = 0; i < count; ++i) {
        const D3DXVECTOR3& p = pos(order[i]);
        for (DWORD j = i + 1; j < count; ++j) {
            const D3DXVECTOR3& q = pos(order[j]);
            if (q.x - p.x > eps) break;
            if (std::fabs(q.y - p.y) <= eps && std::fabs(q.z - p.z) <= eps) u.Join(order[i], order[j]);
            else if (eps == 0.0f) break;   // exact matches are neighbours in sorted order
        }
    }
    std::vector<DWORD> reps(count);
    for (DWORD i = 0; i < count; ++i) reps[i] = u.Find(i);
    return reps;
}

void AdjacencyFromPointReps(const std::vector<DWORD>& indices, DWORD numFaces, const DWORD* prep, DWORD* adjacency)
{
    auto rep = [&](DWORD v) { return prep ? prep[v] : v; };
    // Every directed edge, sorted by (from, to) so the reverse edge is a binary search away.
    struct Edge { uint64_t key; DWORD faceEdge; };
    std::vector<Edge> edges;
    edges.reserve((size_t)numFaces * 3);
    for (DWORD f = 0; f < numFaces; ++f) {
        adjacency[3 * f] = adjacency[3 * f + 1] = adjacency[3 * f + 2] = UNUSED32;
        for (DWORD e = 0; e < 3; ++e) {
            const DWORD a = rep(indices[3 * f + e]), b = rep(indices[3 * f + (e + 1) % 3]);
            if (a != b) edges.push_back({ ((uint64_t)a << 32) | b, 3 * f + e });
        }
    }
    std::sort(edges.begin(), edges.end(), [](const Edge& x, const Edge& y) {
        return x.key != y.key ? x.key < y.key : x.faceEdge < y.faceEdge;
    });
    for (DWORD f = 0; f < numFaces; ++f) {
        for (DWORD e = 0; e < 3; ++e) {
            if (adjacency[3 * f + e] != UNUSED32) continue;
            const DWORD a = rep(indices[3 * f + e]), b = rep(indices[3 * f + (e + 1) % 3]);
            if (a == b) continue;
            const uint64_t key = ((uint64_t)b << 32) | a;
            auto it = std::lower_bound(edges.begin(), edges.end(), key, [](const Edge& x, uint64_t k) { return x.key < k; });
            for (; it != edges.end() && it->key == key; ++it) {
                const DWORD g = it->faceEdge / 3;
                if (g == f || adjacency[it->faceEdge] != UNUSED32) continue;
                adjacency[3 * f + e] = g;
                adjacency[it->faceEdge] = f;
                break;
            }
        }
    }
}

void PointRepsFromAdjacency(const std::vector<DWORD>& indices, DWORD numFaces, DWORD numVertices, const DWORD* adjacency, DWORD* prep)
{
    MinUnion u(numVertices);
    if (adjacency) {
        for (DWORD f = 0; f < numFaces; ++f) {
            for (DWORD e = 0; e < 3; ++e) {
                const DWORD g = adjacency[3 * f + e];
                if (g >= numFaces) continue;
                for (DWORD k = 0; k < 3; ++k) {
                    if (adjacency[3 * g + k] != f) continue;
                    // The shared edge runs v0->v1 in f and w0->w1 (reversed) in g.
                    const DWORD v0 = indices[3 * f + e], v1 = indices[3 * f + (e + 1) % 3];
                    const DWORD w0 = indices[3 * g + k], w1 = indices[3 * g + (k + 1) % 3];
                    if (v0 < numVertices && w1 < numVertices) u.Join(v0, w1);
                    if (v1 < numVertices && w0 < numVertices) u.Join(v1, w0);
                    break;
                }
            }
        }
    }
    for (DWORD v = 0; v < numVertices; ++v) prep[v] = u.Find(v);
}

Layout PlanLayout(const std::vector<DWORD>& indices, const std::vector<DWORD>& attribs, DWORD numVertices,
                  bool sort, bool split, bool compact, bool ignoreVertices)
{
    const DWORD numFaces = (DWORD)attribs.size();
    Layout L;
    L.faceOrder.resize(numFaces);
    for (DWORD f = 0; f < numFaces; ++f) L.faceOrder[f] = f;
    if (sort) std::stable_sort(L.faceOrder.begin(), L.faceOrder.end(), [&](DWORD a, DWORD b) { return attribs[a] < attribs[b]; });

    L.indices.resize((size_t)numFaces * 3);
    if (ignoreVertices) {
        for (DWORD f = 0; f < numFaces; ++f)
            for (int c = 0; c < 3; ++c) L.indices[3 * f + c] = indices[3 * L.faceOrder[f] + c];
        L.vertexSource.resize(numVertices);
        for (DWORD v = 0; v < numVertices; ++v) L.vertexSource[v] = v;
        return L;
    }

    std::vector<DWORD> newOf(numVertices, UNUSED32);
    std::vector<bool> used(numVertices, false);
    if (sort && split) {
        // Each attribute run gets its own vertices, numbered in first-use order.
        std::vector<DWORD> stamp(numVertices, UNUSED32);
        DWORD run = 0;
        for (DWORD f = 0; f < numFaces; ++f) {
            if (f > 0 && attribs[L.faceOrder[f]] != attribs[L.faceOrder[f - 1]]) ++run;
            for (int c = 0; c < 3; ++c) {
                const DWORD old = indices[3 * L.faceOrder[f] + c];
                if (old >= numVertices) { L.indices[3 * f + c] = old; continue; }
                if (stamp[old] != run) {
                    stamp[old] = run;
                    newOf[old] = (DWORD)L.vertexSource.size();
                    L.vertexSource.push_back(old);
                }
                used[old] = true;
                L.indices[3 * f + c] = newOf[old];
            }
        }
    } else if (sort) {
        for (DWORD f = 0; f < numFaces; ++f) {
            for (int c = 0; c < 3; ++c) {
                const DWORD old = indices[3 * L.faceOrder[f] + c];
                if (old >= numVertices) { L.indices[3 * f + c] = old; continue; }
                if (!used[old]) {
                    used[old] = true;
                    newOf[old] = (DWORD)L.vertexSource.size();
                    L.vertexSource.push_back(old);
                }
                L.indices[3 * f + c] = newOf[old];
            }
        }
    } else {
        for (DWORD i = 0; i < numFaces * 3; ++i) if (indices[i] < numVertices) used[indices[i]] = true;
        for (DWORD v = 0; v < numVertices; ++v) {
            if (compact && !used[v]) continue;
            newOf[v] = (DWORD)L.vertexSource.size();
            L.vertexSource.push_back(v);
        }
        for (DWORD f = 0; f < numFaces; ++f)
            for (int c = 0; c < 3; ++c) {
                const DWORD old = indices[3 * L.faceOrder[f] + c];
                L.indices[3 * f + c] = old < numVertices ? newOf[old] : old;
            }
        return L;
    }
    if (!compact) {
        for (DWORD v = 0; v < numVertices; ++v)
            if (!used[v]) L.vertexSource.push_back(v);
    }
    return L;
}

// ---- MeshCore ------------------------------------------------------------------------------

namespace {

// Options a mesh keeps (the upper byte holds D3DXMESHOPT_* flags; VB_SHARE applies to one clone).
const DWORD kMeshOptionMask = 0x00FFFFFF & ~(DWORD)(D3DXMESH_VB_SHARE | D3DXMESHOPT_DEVICEINDEPENDENT);

DWORD CommonUsage(DWORD o)
{
    DWORD u = 0;
    if (o & D3DXMESH_DONOTCLIP) u |= D3DUSAGE_DONOTCLIP;
    if (o & D3DXMESH_POINTS) u |= D3DUSAGE_POINTS;
    if (o & D3DXMESH_RTPATCHES) u |= D3DUSAGE_RTPATCHES;
    if (o & D3DXMESH_NPATCHES) u |= D3DUSAGE_NPATCHES;
    return u;
}
DWORD VbUsage(DWORD o)
{
    DWORD u = CommonUsage(o);
    if (o & D3DXMESH_VB_WRITEONLY) u |= D3DUSAGE_WRITEONLY;
    if (o & D3DXMESH_VB_DYNAMIC) u |= D3DUSAGE_DYNAMIC;
    if (o & D3DXMESH_VB_SOFTWAREPROCESSING) u |= D3DUSAGE_SOFTWAREPROCESSING;
    return u;
}
DWORD IbUsage(DWORD o)
{
    DWORD u = CommonUsage(o);
    if (o & D3DXMESH_IB_WRITEONLY) u |= D3DUSAGE_WRITEONLY;
    if (o & D3DXMESH_IB_DYNAMIC) u |= D3DUSAGE_DYNAMIC;
    if (o & D3DXMESH_IB_SOFTWAREPROCESSING) u |= D3DUSAGE_SOFTWAREPROCESSING;
    return u;
}
D3DPOOL VbPool(DWORD o)
{
    if (o & D3DXMESH_VB_SYSTEMMEM) return D3DPOOL_SYSTEMMEM;
    if (o & D3DXMESH_VB_MANAGED) return D3DPOOL_MANAGED;
    return D3DPOOL_DEFAULT;
}
D3DPOOL IbPool(DWORD o)
{
    if (o & D3DXMESH_IB_SYSTEMMEM) return D3DPOOL_SYSTEMMEM;
    if (o & D3DXMESH_IB_MANAGED) return D3DPOOL_MANAGED;
    return D3DPOOL_DEFAULT;
}

bool SameLayout(const D3DVERTEXELEMENT9* a, const D3DVERTEXELEMENT9* b)
{
    const UINT n = DeclLength(a);
    return n == DeclLength(b) && std::memcmp(a, b, n * sizeof(D3DVERTEXELEMENT9)) == 0;
}

template <class T> void SafeRelease(T*& p) { if (p) { p->Release(); p = nullptr; } }

} // namespace

MeshCore::~MeshCore()
{
    SafeRelease(vdecl);
    SafeRelease(ib);
    SafeRelease(vb);
    SafeRelease(device);
}

HRESULT MeshCore::Init(DWORD faces, DWORD vertices, DWORD opts, const D3DVERTEXELEMENT9* d,
                       IDirect3DDevice9* dev, IDirect3DVertexBuffer9* sharedVB)
{
    if (!dev || !d || !faces || !vertices) return D3DERR_INVALIDCALL;
    const UINT len = DeclLength(d);
    if (len == 0 || len >= MAX_FVF_DECL_SIZE || d[len].Stream != 0xFF) return D3DERR_INVALIDCALL;
    for (UINT i = 0; i < len; ++i)
        if (d[i].Stream != 0 || d[i].Type > D3DDECLTYPE_UNUSED) return D3DERR_INVALIDCALL;
    options = opts & kMeshOptionMask;
    if (!(options & D3DXMESH_32BIT) && vertices > 0xFFFF) return D3DERR_INVALIDCALL;
    std::memcpy(decl, d, (len + 1) * sizeof(D3DVERTEXELEMENT9));
    stride = D3DXGetDeclVertexSize(decl, 0);
    if (!stride) return D3DERR_INVALIDCALL;
    if (FAILED(D3DXFVFFromDeclarator(decl, &fvf))) fvf = 0;
    numFaces = faces;
    numVertices = vertices;
    device = dev;
    device->AddRef();
    HRESULT hr;
    if (sharedVB) {
        vb = sharedVB;
        vb->AddRef();
        sharedVertices = true;
    } else {
        hr = device->CreateVertexBuffer(vertices * stride, VbUsage(options), fvf, VbPool(options), &vb, nullptr);
        if (FAILED(hr)) return hr;
    }
    hr = device->CreateIndexBuffer(faces * 3 * (Index32() ? 4 : 2), IbUsage(options),
                                   Index32() ? D3DFMT_INDEX32 : D3DFMT_INDEX16, IbPool(options), &ib, nullptr);
    if (FAILED(hr)) return hr;
    attribs.assign(faces, 0);
    table.clear();
    return D3D_OK;
}

HRESULT MeshCore::ReadIndices(std::vector<DWORD>& out) const
{
    void* p = nullptr;
    HRESULT hr = ib->Lock(0, 0, &p, D3DLOCK_READONLY);
    if (FAILED(hr)) return hr;
    out.resize((size_t)numFaces * 3);
    if (Index32()) std::memcpy(out.data(), p, out.size() * 4);
    else for (size_t i = 0; i < out.size(); ++i) out[i] = ((const WORD*)p)[i];
    ib->Unlock();
    return D3D_OK;
}

HRESULT MeshCore::WriteIndices(const std::vector<DWORD>& in)
{
    void* p = nullptr;
    HRESULT hr = ib->Lock(0, 0, &p, 0);
    if (FAILED(hr)) return hr;
    const size_t n = std::min(in.size(), (size_t)numFaces * 3);
    if (Index32()) std::memcpy(p, in.data(), n * 4);
    else for (size_t i = 0; i < n; ++i) ((WORD*)p)[i] = (WORD)in[i];
    ib->Unlock();
    return D3D_OK;
}

HRESULT MeshCore::ReadVertices(std::vector<BYTE>& out) const
{
    void* p = nullptr;
    HRESULT hr = vb->Lock(0, 0, &p, D3DLOCK_READONLY);
    if (FAILED(hr)) return hr;
    out.assign((const BYTE*)p, (const BYTE*)p + (size_t)numVertices * stride);
    vb->Unlock();
    return D3D_OK;
}

HRESULT MeshCore::WriteVertices(const BYTE* data)
{
    void* p = nullptr;
    HRESULT hr = vb->Lock(0, 0, &p, 0);
    if (FAILED(hr)) return hr;
    std::memcpy(p, data, (size_t)numVertices * stride);
    vb->Unlock();
    return D3D_OK;
}

HRESULT MeshCore::CloneInto(DWORD opts, const D3DVERTEXELEMENT9* d, IDirect3DDevice9* dev, MeshCore& dst) const
{
    if (!d || !dev) return D3DERR_INVALIDCALL;
    // A 16-bit request for a mesh that needs 32-bit indices keeps 32-bit ones.
    if (numVertices > 0xFFFF) opts |= D3DXMESH_32BIT;
    const bool share = (opts & D3DXMESH_VB_SHARE) && dev == device && SameLayout(d, decl);
    HRESULT hr = dst.Init(numFaces, numVertices, opts, d, dev, share ? vb : nullptr);
    if (FAILED(hr)) return hr;
    if (!share) {
        std::vector<BYTE> src;
        if (FAILED(hr = ReadVertices(src))) return hr;
        std::vector<BYTE> conv((size_t)numVertices * dst.stride);
        ConvertVertices(src.data(), decl, stride, conv.data(), dst.decl, dst.stride, numVertices);
        if (FAILED(hr = dst.WriteVertices(conv.data()))) return hr;
    }
    std::vector<DWORD> idx;
    if (FAILED(hr = ReadIndices(idx))) return hr;
    if (FAILED(hr = dst.WriteIndices(idx))) return hr;
    dst.attribs = attribs;
    dst.table = table;
    return D3D_OK;
}

HRESULT MeshCore::Rearrange(const std::vector<DWORD>& faceOrder, const std::vector<DWORD>& indices,
                            const std::vector<DWORD>& vertexSource)
{
    HRESULT hr;
    const DWORD nv = (DWORD)vertexSource.size();
    if (nv == 0) return D3DERR_INVALIDCALL;
    bool identity = nv == numVertices;
    for (DWORD v = 0; identity && v < nv; ++v) identity = vertexSource[v] == v;
    if (!identity) {
        std::vector<BYTE> src;
        if (FAILED(hr = ReadVertices(src))) return hr;
        std::vector<BYTE> out((size_t)nv * stride, 0);
        for (DWORD v = 0; v < nv; ++v)
            if (vertexSource[v] < numVertices)
                std::memcpy(&out[(size_t)v * stride], &src[(size_t)vertexSource[v] * stride], stride);
        if (nv != numVertices || sharedVertices) {
            // New vertex buffer (a shared one stays with the mesh it is shared with).
            IDirect3DVertexBuffer9* nb = nullptr;
            if (FAILED(hr = device->CreateVertexBuffer(nv * stride, VbUsage(options), fvf, VbPool(options), &nb, nullptr))) return hr;
            SafeRelease(vb);
            vb = nb;
            sharedVertices = false;
        }
        numVertices = nv;
        if (FAILED(hr = WriteVertices(out.data()))) return hr;
    }
    if (!Index32() && nv > 0xFFFF) {
        // Splitting outgrew 16-bit indices: switch to 32-bit ones.
        IDirect3DIndexBuffer9* nb = nullptr;
        if (FAILED(hr = device->CreateIndexBuffer(numFaces * 12, IbUsage(options), D3DFMT_INDEX32, IbPool(options), &nb, nullptr))) return hr;
        SafeRelease(ib);
        ib = nb;
        options |= D3DXMESH_32BIT;
    }
    if (FAILED(hr = WriteIndices(indices))) return hr;
    std::vector<DWORD> attr(faceOrder.size());
    for (size_t f = 0; f < faceOrder.size(); ++f) attr[f] = attribs[faceOrder[f]];
    attribs.swap(attr);
    return D3D_OK;
}

void MeshCore::BuildTableFromRuns(const std::vector<DWORD>& indices)
{
    table.clear();
    for (DWORD f = 0; f < numFaces;) {
        D3DXATTRIBUTERANGE r;
        r.AttribId = attribs[f];
        r.FaceStart = f;
        DWORD lo = UNUSED32, hi = 0;
        for (; f < numFaces && attribs[f] == r.AttribId; ++f)
            for (int c = 0; c < 3; ++c) {
                const DWORD v = indices[3 * f + c];
                if (v >= numVertices) continue;
                lo = std::min(lo, v);
                hi = std::max(hi, v);
            }
        r.FaceCount = f - r.FaceStart;
        r.VertexStart = lo == UNUSED32 ? 0 : lo;
        r.VertexCount = lo == UNUSED32 ? 0 : hi - lo + 1;
        table.push_back(r);
    }
}

HRESULT MeshCore::Optimize(DWORD flags, const DWORD* adjIn, DWORD* adjOut, DWORD* faceRemap, std::vector<DWORD>* vertexRemap)
{
    const bool sort = (flags & (D3DXMESHOPT_ATTRSORT | D3DXMESHOPT_VERTEXCACHE | D3DXMESHOPT_STRIPREORDER)) != 0;
    const bool ignore = (flags & D3DXMESHOPT_IGNOREVERTS) != 0;
    const bool split = sort && !(flags & D3DXMESHOPT_DONOTSPLIT);
    const bool compact = (flags & D3DXMESHOPT_COMPACT) != 0;
    HRESULT hr;
    std::vector<DWORD> idx;
    if (FAILED(hr = ReadIndices(idx))) return hr;
    std::vector<DWORD> adj;
    if (adjIn) adj.assign(adjIn, adjIn + (size_t)numFaces * 3);   // adjIn may alias adjOut

    const Layout L = PlanLayout(idx, attribs, numVertices, sort, split, compact, ignore);
    const bool hadTable = !table.empty();
    if (FAILED(hr = Rearrange(L.faceOrder, L.indices, L.vertexSource))) return hr;
    if (sort || hadTable) BuildTableFromRuns(L.indices);

    if (faceRemap) std::copy(L.faceOrder.begin(), L.faceOrder.end(), faceRemap);
    if (vertexRemap) *vertexRemap = L.vertexSource;
    if (adjOut) {
        if (adjIn) {
            std::vector<DWORD> newOfFace(numFaces);
            for (DWORD f = 0; f < numFaces; ++f) newOfFace[L.faceOrder[f]] = f;
            for (DWORD f = 0; f < numFaces; ++f)
                for (int e = 0; e < 3; ++e) {
                    const DWORD n = adj[3 * L.faceOrder[f] + e];
                    adjOut[3 * f + e] = n < numFaces ? newOfFace[n] : UNUSED32;
                }
        } else {
            GenerateAdjacency(0.0f, adjOut);
        }
    }
    return D3D_OK;
}

HRESULT MeshCore::Draw(DWORD attrib)
{
    HRESULT hr;
    if (fvf) {
        device->SetFVF(fvf);
    } else {
        if (!vdecl && FAILED(hr = device->CreateVertexDeclaration(decl, &vdecl))) return hr;
        device->SetVertexDeclaration(vdecl);
    }
    device->SetStreamSource(0, vb, 0, stride);
    device->SetIndices(ib);
    if (!table.empty()) {
        for (const D3DXATTRIBUTERANGE& r : table) {
            if (r.AttribId != attrib || !r.FaceCount) continue;
            if (FAILED(hr = device->DrawIndexedPrimitive(D3DPT_TRIANGLELIST, 0, r.VertexStart, r.VertexCount, r.FaceStart * 3, r.FaceCount)))
                return hr;
        }
        return D3D_OK;
    }
    // No attribute table: every run of faces carrying the id.
    for (DWORD f = 0; f < numFaces;) {
        if (attribs[f] != attrib) { ++f; continue; }
        const DWORD start = f;
        while (f < numFaces && attribs[f] == attrib) ++f;
        if (FAILED(hr = device->DrawIndexedPrimitive(D3DPT_TRIANGLELIST, 0, 0, numVertices, start * 3, f - start))) return hr;
    }
    return D3D_OK;
}

HRESULT MeshCore::GenerateAdjacency(float epsilon, DWORD* adjacency) const
{
    if (!adjacency) return D3DERR_INVALIDCALL;
    const D3DVERTEXELEMENT9* pos = FindElement(decl, D3DDECLUSAGE_POSITION, 0);
    if (!pos || (pos->Type != D3DDECLTYPE_FLOAT3 && pos->Type != D3DDECLTYPE_FLOAT4)) return D3DERR_INVALIDCALL;
    HRESULT hr;
    std::vector<DWORD> idx;
    std::vector<BYTE> verts;
    if (FAILED(hr = ReadIndices(idx)) || FAILED(hr = ReadVertices(verts))) return hr;
    const std::vector<DWORD> reps = PointRepsFromPositions(verts.data(), stride, pos->Offset, numVertices, epsilon);
    AdjacencyFromPointReps(idx, numFaces, reps.data(), adjacency);
    return D3D_OK;
}

// ---- ID3DXMesh / ID3DXPMesh ----------------------------------------------------------------

namespace {

// Everything ID3DXBaseMesh declares, shared by both mesh classes.
template <class I>
class MeshBase : public I, public RefCounted
{
public:
    MeshCore core;

    STDMETHOD(QueryInterface)(THIS_ REFIID iid, LPVOID* ppv)
    {
        if (!ppv) return E_POINTER;
        if (IsEqualGUID(iid, IID_MeshCore)) { *ppv = &core; return S_OK; }   // internal, no AddRef
        if (IsEqualGUID(iid, IID_IUnknown) || IsEqualGUID(iid, IID_ID3DXBaseMesh) || IsEqualGUID(iid, OwnIID())) {
            *ppv = this; this->AddRef(); return S_OK;
        }
        *ppv = nullptr;
        return E_NOINTERFACE;
    }
    STDMETHOD_(ULONG, AddRef)(THIS) { return DoAddRef(); }
    STDMETHOD_(ULONG, Release)(THIS) { return DoRelease(); }

    STDMETHOD(DrawSubset)(THIS_ DWORD attrib) { return core.Draw(attrib); }
    STDMETHOD_(DWORD, GetNumFaces)(THIS) { return core.numFaces; }
    STDMETHOD_(DWORD, GetNumVertices)(THIS) { return core.numVertices; }
    STDMETHOD_(DWORD, GetFVF)(THIS) { return core.fvf; }
    STDMETHOD(GetDeclaration)(THIS_ D3DVERTEXELEMENT9 decl[MAX_FVF_DECL_SIZE])
    {
        if (!decl) return D3DERR_INVALIDCALL;
        std::memcpy(decl, core.decl, (DeclLength(core.decl) + 1) * sizeof(D3DVERTEXELEMENT9));
        return D3D_OK;
    }
    STDMETHOD_(DWORD, GetNumBytesPerVertex)(THIS) { return core.stride; }
    STDMETHOD_(DWORD, GetOptions)(THIS) { return core.options; }
    STDMETHOD(GetDevice)(THIS_ LPDIRECT3DDEVICE9* dev)
    {
        if (!dev) return D3DERR_INVALIDCALL;
        *dev = core.device;
        core.device->AddRef();
        return D3D_OK;
    }
    STDMETHOD(CloneMeshFVF)(THIS_ DWORD options, DWORD fvf, LPDIRECT3DDEVICE9 dev, LPD3DXMESH* out)
    {
        if (out) *out = nullptr;
        D3DVERTEXELEMENT9 decl[MAX_FVF_DECL_SIZE];
        HRESULT hr = D3DXDeclaratorFromFVF(fvf, decl);
        return FAILED(hr) ? hr : CloneMesh(options, decl, dev, out);
    }
    STDMETHOD(CloneMesh)(THIS_ DWORD options, CONST D3DVERTEXELEMENT9* decl, LPDIRECT3DDEVICE9 dev, LPD3DXMESH* out)
    {
        if (!out) return D3DERR_INVALIDCALL;
        *out = nullptr;
        ID3DXMesh* mesh;
        MeshCore* dst;
        NewMesh(&mesh, &dst);
        HRESULT hr = core.CloneInto(options, decl, dev, *dst);
        if (FAILED(hr)) { mesh->Release(); return hr; }
        *out = mesh;
        return D3D_OK;
    }
    STDMETHOD(GetVertexBuffer)(THIS_ LPDIRECT3DVERTEXBUFFER9* vb)
    {
        if (!vb) return D3DERR_INVALIDCALL;
        *vb = core.vb;
        core.vb->AddRef();
        return D3D_OK;
    }
    STDMETHOD(GetIndexBuffer)(THIS_ LPDIRECT3DINDEXBUFFER9* ib)
    {
        if (!ib) return D3DERR_INVALIDCALL;
        *ib = core.ib;
        core.ib->AddRef();
        return D3D_OK;
    }
    STDMETHOD(LockVertexBuffer)(THIS_ DWORD flags, LPVOID* data)
    {
        if (!data) return D3DERR_INVALIDCALL;
        return core.vb->Lock(0, 0, data, flags);
    }
    STDMETHOD(UnlockVertexBuffer)(THIS) { return core.vb->Unlock(); }
    STDMETHOD(LockIndexBuffer)(THIS_ DWORD flags, LPVOID* data)
    {
        if (!data) return D3DERR_INVALIDCALL;
        return core.ib->Lock(0, 0, data, flags);
    }
    STDMETHOD(UnlockIndexBuffer)(THIS) { return core.ib->Unlock(); }
    STDMETHOD(GetAttributeTable)(THIS_ D3DXATTRIBUTERANGE* table, DWORD* size)
    {
        if (!table && !size) return D3DERR_INVALIDCALL;
        if (size) *size = (DWORD)core.table.size();
        if (table && !core.table.empty()) std::memcpy(table, core.table.data(), core.table.size() * sizeof(D3DXATTRIBUTERANGE));
        return D3D_OK;
    }
    STDMETHOD(ConvertPointRepsToAdjacency)(THIS_ CONST DWORD* prep, DWORD* adjacency)
    {
        if (!adjacency) return D3DERR_INVALIDCALL;
        std::vector<DWORD> idx;
        HRESULT hr = core.ReadIndices(idx);
        if (FAILED(hr)) return hr;
        AdjacencyFromPointReps(idx, core.numFaces, prep, adjacency);
        return D3D_OK;
    }
    STDMETHOD(ConvertAdjacencyToPointReps)(THIS_ CONST DWORD* adjacency, DWORD* prep)
    {
        if (!prep) return D3DERR_INVALIDCALL;
        std::vector<DWORD> idx;
        HRESULT hr = core.ReadIndices(idx);
        if (FAILED(hr)) return hr;
        PointRepsFromAdjacency(idx, core.numFaces, core.numVertices, adjacency, prep);
        return D3D_OK;
    }
    STDMETHOD(GenerateAdjacency)(THIS_ FLOAT epsilon, DWORD* adjacency) { return core.GenerateAdjacency(epsilon, adjacency); }
    STDMETHOD(UpdateSemantics)(THIS_ D3DVERTEXELEMENT9 decl[MAX_FVF_DECL_SIZE])
    {
        if (!decl) return D3DERR_INVALIDCALL;
        const UINT len = DeclLength(decl);
        if (len == 0 || len >= MAX_FVF_DECL_SIZE || D3DXGetDeclVertexSize(decl, 0) != core.stride) return D3DERR_INVALIDCALL;
        std::memcpy(core.decl, decl, (len + 1) * sizeof(D3DVERTEXELEMENT9));
        if (FAILED(D3DXFVFFromDeclarator(core.decl, &core.fvf))) core.fvf = 0;
        SafeRelease(core.vdecl);
        return D3D_OK;
    }

protected:
    virtual REFIID OwnIID() const = 0;
};

class Mesh final : public MeshBase<ID3DXMesh>
{
public:
    STDMETHOD(LockAttributeBuffer)(THIS_ DWORD flags, DWORD** data)
    {
        if (!data) return D3DERR_INVALIDCALL;
        if (!(flags & D3DLOCK_READONLY)) core.table.clear();   // D3DX drops the table on a writing lock
        *data = core.attribs.data();
        return D3D_OK;
    }
    STDMETHOD(UnlockAttributeBuffer)(THIS) { return D3D_OK; }
    STDMETHOD(Optimize)(THIS_ DWORD flags, CONST DWORD* adjIn, DWORD* adjOut, DWORD* faceRemap,
                        LPD3DXBUFFER* vertexRemap, LPD3DXMESH* out)
    {
        if (vertexRemap) *vertexRemap = nullptr;
        if (!out) return D3DERR_INVALIDCALL;
        *out = nullptr;
        // The lower bytes of the flags are the new mesh's options.
        ID3DXMesh* clone = nullptr;
        HRESULT hr = CloneMesh(flags & 0x00FFFFFF & ~(DWORD)D3DXMESH_VB_SHARE, core.decl, core.device, &clone);
        if (FAILED(hr)) return hr;
        hr = clone->OptimizeInplace(flags, adjIn, adjOut, faceRemap, vertexRemap);
        if (FAILED(hr)) { clone->Release(); return hr; }
        *out = clone;
        return D3D_OK;
    }
    STDMETHOD(OptimizeInplace)(THIS_ DWORD flags, CONST DWORD* adjIn, DWORD* adjOut, DWORD* faceRemap, LPD3DXBUFFER* vertexRemap)
    {
        if (vertexRemap) *vertexRemap = nullptr;
        std::vector<DWORD> remap;
        HRESULT hr = core.Optimize(flags, adjIn, adjOut, faceRemap, vertexRemap ? &remap : nullptr);
        if (FAILED(hr)) return hr;
        if (vertexRemap) return CreateBuffer((DWORD)(remap.size() * sizeof(DWORD)), remap.data(), vertexRemap);
        return D3D_OK;
    }
    STDMETHOD(SetAttributeTable)(THIS_ CONST D3DXATTRIBUTERANGE* table, DWORD size)
    {
        if (!table && size) return D3DERR_INVALIDCALL;
        core.table.assign(table, table + size);
        return D3D_OK;
    }

protected:
    REFIID OwnIID() const { return IID_ID3DXMesh; }
};

class PMesh final : public MeshBase<ID3DXPMesh>
{
public:
    std::vector<DWORD> adjacency;   // as given to D3DXGeneratePMesh

    HRESULT InitFrom(const MeshCore& src, DWORD options, const D3DVERTEXELEMENT9* decl, IDirect3DDevice9* dev, const DWORD* adj)
    {
        HRESULT hr = src.CloneInto(options, decl, dev, core);
        if (FAILED(hr)) return hr;
        adjacency.resize((size_t)core.numFaces * 3);
        if (adj) std::copy(adj, adj + adjacency.size(), adjacency.begin());
        else core.GenerateAdjacency(0.0f, adjacency.data());
        return D3D_OK;
    }

    STDMETHOD(ClonePMeshFVF)(THIS_ DWORD options, DWORD fvf, LPDIRECT3DDEVICE9 dev, LPD3DXPMESH* out)
    {
        if (out) *out = nullptr;
        D3DVERTEXELEMENT9 decl[MAX_FVF_DECL_SIZE];
        HRESULT hr = D3DXDeclaratorFromFVF(fvf, decl);
        return FAILED(hr) ? hr : ClonePMesh(options, decl, dev, out);
    }
    STDMETHOD(ClonePMesh)(THIS_ DWORD options, CONST D3DVERTEXELEMENT9* decl, LPDIRECT3DDEVICE9 dev, LPD3DXPMESH* out)
    {
        if (!out) return D3DERR_INVALIDCALL;
        *out = nullptr;
        PMesh* p = new PMesh;
        HRESULT hr = p->InitFrom(core, options, decl, dev, adjacency.data());
        if (FAILED(hr)) { p->Release(); return hr; }
        *out = p;
        return D3D_OK;
    }
    // No simplification: the full mesh is both the minimum and the maximum level of detail.
    STDMETHOD(SetNumFaces)(THIS_ DWORD) { return D3D_OK; }
    STDMETHOD(SetNumVertices)(THIS_ DWORD) { return D3D_OK; }
    STDMETHOD_(DWORD, GetMaxFaces)(THIS) { return core.numFaces; }
    STDMETHOD_(DWORD, GetMinFaces)(THIS) { return core.numFaces; }
    STDMETHOD_(DWORD, GetMaxVertices)(THIS) { return core.numVertices; }
    STDMETHOD_(DWORD, GetMinVertices)(THIS) { return core.numVertices; }
    STDMETHOD(Save)(THIS_ IStream*, CONST D3DXMATERIAL*, CONST D3DXEFFECTINSTANCE*, DWORD) { return E_NOTIMPL; }
    STDMETHOD(Optimize)(THIS_ DWORD flags, DWORD* adjOut, DWORD* faceRemap, LPD3DXBUFFER* vertexRemap, LPD3DXMESH* out)
    {
        if (vertexRemap) *vertexRemap = nullptr;
        if (!out) return D3DERR_INVALIDCALL;
        *out = nullptr;
        ID3DXMesh* mesh = nullptr;
        HRESULT hr = CloneMesh(flags & 0x00FFFFFF & ~(DWORD)D3DXMESH_VB_SHARE, core.decl, core.device, &mesh);
        if (FAILED(hr)) return hr;
        hr = mesh->OptimizeInplace(flags, adjacency.data(), adjOut, faceRemap, vertexRemap);
        if (FAILED(hr)) { mesh->Release(); return hr; }
        *out = mesh;
        return D3D_OK;
    }
    // Reorders faces only: the vertex buffer may be shared with sibling clones.
    STDMETHOD(OptimizeBaseLOD)(THIS_ DWORD flags, DWORD* faceRemap)
    {
        return core.Optimize(flags | D3DXMESHOPT_IGNOREVERTS, adjacency.data(), adjacency.data(), faceRemap, nullptr);
    }
    STDMETHOD(TrimByFaces)(THIS_ DWORD, DWORD, DWORD* faceRemap, DWORD* vertRemap) { return Trim(faceRemap, vertRemap); }
    STDMETHOD(TrimByVertices)(THIS_ DWORD, DWORD, DWORD* faceRemap, DWORD* vertRemap) { return Trim(faceRemap, vertRemap); }
    STDMETHOD(GetAdjacency)(THIS_ DWORD* adj)
    {
        if (!adj) return D3DERR_INVALIDCALL;
        std::copy(adjacency.begin(), adjacency.end(), adj);
        return D3D_OK;
    }
    STDMETHOD(GenerateVertexHistory)(THIS_ DWORD* history)
    {
        if (!history) return D3DERR_INVALIDCALL;
        for (DWORD v = 0; v < core.numVertices; ++v) history[v] = v;
        return D3D_OK;
    }

protected:
    REFIID OwnIID() const { return IID_ID3DXPMesh; }

private:
    HRESULT Trim(DWORD* faceRemap, DWORD* vertRemap)
    {
        if (faceRemap) for (DWORD f = 0; f < core.numFaces; ++f) faceRemap[f] = f;
        if (vertRemap) for (DWORD v = 0; v < core.numVertices; ++v) vertRemap[v] = v;
        return D3D_OK;
    }
};

} // namespace

MeshCore* CoreOf(IUnknown* mesh)
{
    void* p = nullptr;
    if (!mesh || FAILED(mesh->QueryInterface(IID_MeshCore, &p))) return nullptr;
    return (MeshCore*)p;
}

HRESULT NewMesh(ID3DXMesh** out, MeshCore** core)
{
    Mesh* m = new Mesh;
    *out = m;
    *core = &m->core;
    return D3D_OK;
}

} // namespace ran_d3dx

using namespace ran_d3dx;

extern "C" {

HRESULT WINAPI D3DXCreateBuffer(DWORD bytes, LPD3DXBUFFER* out)
{
    return CreateBuffer(bytes, nullptr, out);
}

UINT WINAPI D3DXGetDeclLength(CONST D3DVERTEXELEMENT9* decl)
{
    return DeclLength(decl);
}

UINT WINAPI D3DXGetDeclVertexSize(CONST D3DVERTEXELEMENT9* decl, DWORD stream)
{
    UINT size = 0;
    for (UINT i = 0, n = DeclLength(decl); i < n; ++i)
        if (decl[i].Stream == stream) size = std::max(size, (UINT)decl[i].Offset + DeclTypeSize(decl[i].Type));
    return size;
}

HRESULT WINAPI D3DXDeclaratorFromFVF(DWORD fvf, D3DVERTEXELEMENT9 decl[MAX_FVF_DECL_SIZE])
{
    if (!decl) return D3DERR_INVALIDCALL;
    UINT n = 0;
    WORD offset = 0;
    auto add = [&](BYTE type, BYTE usage, BYTE index) {
        decl[n].Stream = 0;
        decl[n].Offset = offset;
        decl[n].Type = type;
        decl[n].Method = D3DDECLMETHOD_DEFAULT;
        decl[n].Usage = usage;
        decl[n].UsageIndex = index;
        offset = (WORD)(offset + DeclTypeSize(type));
        ++n;
    };
    const DWORD pos = fvf & D3DFVF_POSITION_MASK;
    switch (pos) {
    case 0: break;
    case D3DFVF_XYZ: add(D3DDECLTYPE_FLOAT3, D3DDECLUSAGE_POSITION, 0); break;
    case D3DFVF_XYZW: add(D3DDECLTYPE_FLOAT4, D3DDECLUSAGE_POSITION, 0); break;
    case D3DFVF_XYZRHW: add(D3DDECLTYPE_FLOAT4, D3DDECLUSAGE_POSITIONT, 0); break;
    case D3DFVF_XYZB1: case D3DFVF_XYZB2: case D3DFVF_XYZB3: case D3DFVF_XYZB4: case D3DFVF_XYZB5: {
        add(D3DDECLTYPE_FLOAT3, D3DDECLUSAGE_POSITION, 0);
        DWORD weights = (pos - D3DFVF_XYZB1) / 2 + 1;
        BYTE indexType = D3DDECLTYPE_UNUSED;
        if (fvf & D3DFVF_LASTBETA_UBYTE4) indexType = D3DDECLTYPE_UBYTE4;
        else if (fvf & D3DFVF_LASTBETA_D3DCOLOR) indexType = D3DDECLTYPE_D3DCOLOR;
        if (indexType != D3DDECLTYPE_UNUSED) --weights;
        if (weights > 4) return D3DERR_INVALIDCALL;
        if (weights) add((BYTE)(D3DDECLTYPE_FLOAT1 + weights - 1), D3DDECLUSAGE_BLENDWEIGHT, 0);
        if (indexType != D3DDECLTYPE_UNUSED) add(indexType, D3DDECLUSAGE_BLENDINDICES, 0);
        break;
    }
    default:
        return D3DERR_INVALIDCALL;
    }
    if (fvf & D3DFVF_NORMAL) add(D3DDECLTYPE_FLOAT3, D3DDECLUSAGE_NORMAL, 0);
    if (fvf & D3DFVF_PSIZE) add(D3DDECLTYPE_FLOAT1, D3DDECLUSAGE_PSIZE, 0);
    if (fvf & D3DFVF_DIFFUSE) add(D3DDECLTYPE_D3DCOLOR, D3DDECLUSAGE_COLOR, 0);
    if (fvf & D3DFVF_SPECULAR) add(D3DDECLTYPE_D3DCOLOR, D3DDECLUSAGE_COLOR, 1);
    const DWORD texCount = (fvf & D3DFVF_TEXCOUNT_MASK) >> D3DFVF_TEXCOUNT_SHIFT;
    if (texCount > 8) return D3DERR_INVALIDCALL;
    for (DWORD i = 0; i < texCount; ++i) {
        static const BYTE kTexType[4] = { D3DDECLTYPE_FLOAT2, D3DDECLTYPE_FLOAT3, D3DDECLTYPE_FLOAT4, D3DDECLTYPE_FLOAT1 };
        add(kTexType[(fvf >> (16 + 2 * i)) & 3], D3DDECLUSAGE_TEXCOORD, (BYTE)i);
    }
    const D3DVERTEXELEMENT9 end = D3DDECL_END();
    decl[n] = end;
    return D3D_OK;
}

HRESULT WINAPI D3DXFVFFromDeclarator(CONST D3DVERTEXELEMENT9* decl, DWORD* out)
{
    if (!decl || !out) return D3DERR_INVALIDCALL;
    *out = 0;
    // Build the FVF the elements suggest, then require that it maps back to exactly this layout.
    DWORD pos = 0, weights = 0, lastBeta = 0, fvf = 0, texCount = 0;
    const UINT n = DeclLength(decl);
    for (UINT i = 0; i < n; ++i) {
        const D3DVERTEXELEMENT9& e = decl[i];
        if (e.Stream != 0) return D3DERR_INVALIDCALL;
        if (e.Usage == D3DDECLUSAGE_POSITION && e.UsageIndex == 0)
            pos = e.Type == D3DDECLTYPE_FLOAT4 ? D3DFVF_XYZW : D3DFVF_XYZ;
        else if (e.Usage == D3DDECLUSAGE_POSITIONT && e.UsageIndex == 0) pos = D3DFVF_XYZRHW;
        else if (e.Usage == D3DDECLUSAGE_BLENDWEIGHT && e.UsageIndex == 0 && e.Type <= D3DDECLTYPE_FLOAT4)
            weights = e.Type - D3DDECLTYPE_FLOAT1 + 1;
        else if (e.Usage == D3DDECLUSAGE_BLENDINDICES && e.UsageIndex == 0 && e.Type == D3DDECLTYPE_UBYTE4)
            lastBeta = D3DFVF_LASTBETA_UBYTE4;
        else if (e.Usage == D3DDECLUSAGE_BLENDINDICES && e.UsageIndex == 0 && e.Type == D3DDECLTYPE_D3DCOLOR)
            lastBeta = D3DFVF_LASTBETA_D3DCOLOR;
        else if (e.Usage == D3DDECLUSAGE_NORMAL && e.UsageIndex == 0) fvf |= D3DFVF_NORMAL;
        else if (e.Usage == D3DDECLUSAGE_PSIZE && e.UsageIndex == 0) fvf |= D3DFVF_PSIZE;
        else if (e.Usage == D3DDECLUSAGE_COLOR && e.UsageIndex == 0) fvf |= D3DFVF_DIFFUSE;
        else if (e.Usage == D3DDECLUSAGE_COLOR && e.UsageIndex == 1) fvf |= D3DFVF_SPECULAR;
        else if (e.Usage == D3DDECLUSAGE_TEXCOORD && e.UsageIndex < 8) {
            DWORD format;
            switch (e.Type) {
            case D3DDECLTYPE_FLOAT1: format = D3DFVF_TEXTUREFORMAT1; break;
            case D3DDECLTYPE_FLOAT2: format = D3DFVF_TEXTUREFORMAT2; break;
            case D3DDECLTYPE_FLOAT3: format = D3DFVF_TEXTUREFORMAT3; break;
            case D3DDECLTYPE_FLOAT4: format = D3DFVF_TEXTUREFORMAT4; break;
            default: return D3DERR_INVALIDCALL;
            }
            fvf |= format << (16 + 2 * e.UsageIndex);
            texCount = std::max(texCount, (DWORD)e.UsageIndex + 1);
        } else {
            return D3DERR_INVALIDCALL;
        }
    }
    const DWORD blend = weights + (lastBeta ? 1 : 0);
    if (blend) {
        if (pos != D3DFVF_XYZ || blend > 5) return D3DERR_INVALIDCALL;
        pos = D3DFVF_XYZB1 + 2 * (blend - 1);
    }
    fvf |= pos | lastBeta | (texCount << D3DFVF_TEXCOUNT_SHIFT);
    D3DVERTEXELEMENT9 check[MAX_FVF_DECL_SIZE];
    if (FAILED(D3DXDeclaratorFromFVF(fvf, check)) || DeclLength(check) != n) return D3DERR_INVALIDCALL;
    for (UINT i = 0; i < n; ++i) {
        if (check[i].Offset != decl[i].Offset || check[i].Type != decl[i].Type || check[i].Usage != decl[i].Usage ||
            check[i].UsageIndex != decl[i].UsageIndex)
            return D3DERR_INVALIDCALL;
    }
    *out = fvf;
    return D3D_OK;
}

HRESULT WINAPI D3DXCreateMesh(DWORD faces, DWORD vertices, DWORD options, CONST D3DVERTEXELEMENT9* decl,
                              LPDIRECT3DDEVICE9 device, LPD3DXMESH* out)
{
    if (!out) return D3DERR_INVALIDCALL;
    *out = nullptr;
    ID3DXMesh* mesh;
    MeshCore* core;
    NewMesh(&mesh, &core);
    HRESULT hr = core->Init(faces, vertices, options, decl, device);
    if (FAILED(hr)) { mesh->Release(); return hr; }
    *out = mesh;
    return D3D_OK;
}

HRESULT WINAPI D3DXCreateMeshFVF(DWORD faces, DWORD vertices, DWORD options, DWORD fvf, LPDIRECT3DDEVICE9 device, LPD3DXMESH* out)
{
    if (out) *out = nullptr;
    D3DVERTEXELEMENT9 decl[MAX_FVF_DECL_SIZE];
    HRESULT hr = D3DXDeclaratorFromFVF(fvf, decl);
    return FAILED(hr) ? hr : D3DXCreateMesh(faces, vertices, options, decl, device, out);
}

// The progressive mesh is the full mesh (no simplification); MinValue/Options/weights are ignored.
HRESULT WINAPI D3DXGeneratePMesh(LPD3DXMESH mesh, CONST DWORD* adjacency, CONST D3DXATTRIBUTEWEIGHTS*, CONST FLOAT*,
                                 DWORD, DWORD, LPD3DXPMESH* out)
{
    if (!out) return D3DERR_INVALIDCALL;
    *out = nullptr;
    MeshCore* src = CoreOf(mesh);
    if (!src) return D3DERR_INVALIDCALL;
    PMesh* p = new PMesh;
    HRESULT hr = p->InitFrom(*src, src->options, src->decl, src->device, adjacency);
    if (FAILED(hr)) { p->Release(); return hr; }
    *out = p;
    return D3D_OK;
}

} // extern "C"
