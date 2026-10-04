// Internal interface of the native D3DX mesh layer: d3dx9_mesh.cpp (ID3DXBuffer, ID3DXMesh,
// ID3DXPMesh, vertex declarations), d3dx9_meshutil.cpp (normals, tangents, weld/clean/valid,
// progressive meshes) and d3dx9_skin.cpp (ID3DXSkinInfo, blended meshes).
//
// A mesh is a MeshCore - real device vertex/index buffers like D3DX (pool and usage from the
// D3DXMESH_* options), a CPU attribute buffer and an attribute table - wrapped by the COM class
// for ID3DXMesh or ID3DXPMesh. Every mesh this layer creates answers QueryInterface(IID_MeshCore)
// with its core (no AddRef), which the free functions use to rearrange geometry in place.
//
// For the .x loader: create meshes with D3DXCreateMesh/D3DXCreateMeshFVF, fill them through
// LockVertexBuffer/LockIndexBuffer/LockAttributeBuffer, then OptimizeInplace(D3DXMESHOPT_ATTRSORT)
// to get the attribute table D3DXLoadMeshFromX meshes come with. Skin data goes through
// D3DXCreateSkinInfoFVF + SetBoneName/SetBoneOffsetMatrix/SetBoneInfluence.
#pragma once
#include "ran_compat.h"
#include <d3dx9.h>
#include <atomic>
#include <vector>

namespace ran_d3dx {

// {6E1F4C52-8B0D-4A57-9C39-2F0B7A6D1E01}: private, returns the MeshCore of our meshes.
constexpr GUID IID_MeshCore = { 0x6e1f4c52, 0x8b0d, 0x4a57, { 0x9c, 0x39, 0x2f, 0x0b, 0x7a, 0x6d, 0x1e, 0x01 } };

// ---- vertex declarations -------------------------------------------------------------------

UINT DeclTypeSize(BYTE type);
UINT DeclLength(const D3DVERTEXELEMENT9* decl);   // elements before D3DDECL_END
const D3DVERTEXELEMENT9* FindElement(const D3DVERTEXELEMENT9* decl, BYTE usage, BYTE index);
// One element as four floats (missing components 0, w 1, like the vertex fetch) and back.
void DecodeElement(BYTE type, const BYTE* src, float out[4]);
void EncodeElement(BYTE type, const float in[4], BYTE* dst);
// Re-lays out vertices: destination elements take the source element with the same usage and
// usage index (converted between types); elements the source lacks are zero.
void ConvertVertices(const BYTE* src, const D3DVERTEXELEMENT9* srcDecl, UINT srcStride,
                     BYTE* dst, const D3DVERTEXELEMENT9* dstDecl, UINT dstStride, DWORD count);

// ---- buffers -------------------------------------------------------------------------------

// A D3DX buffer holding a copy of `data` (zeroed when data is null).
HRESULT CreateBuffer(DWORD bytes, const void* data, ID3DXBuffer** out);

// ---- meshes --------------------------------------------------------------------------------

struct MeshCore
{
    IDirect3DDevice9* device = nullptr;
    IDirect3DVertexBuffer9* vb = nullptr;
    IDirect3DIndexBuffer9* ib = nullptr;
    IDirect3DVertexDeclaration9* vdecl = nullptr;   // created on first draw of an FVF-less layout
    D3DVERTEXELEMENT9 decl[MAX_FVF_DECL_SIZE];
    DWORD fvf = 0;            // 0 when the declaration has no FVF equivalent
    DWORD stride = 0;
    DWORD options = 0;        // D3DXMESH_* flags (optimize flags stripped)
    DWORD numFaces = 0;
    DWORD numVertices = 0;
    bool sharedVertices = false;   // vb belongs to a D3DXMESH_VB_SHARE clone family
    std::vector<DWORD> attribs;                    // one attribute id per face
    std::vector<D3DXATTRIBUTERANGE> table;          // empty until Optimize/SetAttributeTable

    MeshCore() = default;
    MeshCore(const MeshCore&) = delete;
    MeshCore& operator=(const MeshCore&) = delete;
    ~MeshCore();

    // Creates the buffers. sharedVB (same layout) is used instead of a new vertex buffer.
    HRESULT Init(DWORD faces, DWORD vertices, DWORD options, const D3DVERTEXELEMENT9* decl,
                 IDirect3DDevice9* device, IDirect3DVertexBuffer9* sharedVB = nullptr);
    bool Index32() const { return (options & D3DXMESH_32BIT) != 0; }

    // Whole-buffer access for the mesh tools (indices are always widened to 32 bits).
    HRESULT ReadIndices(std::vector<DWORD>& out) const;
    HRESULT WriteIndices(const std::vector<DWORD>& in);
    HRESULT ReadVertices(std::vector<BYTE>& out) const;
    HRESULT WriteVertices(const BYTE* data);

    // A copy of this mesh in another layout and/or with other options (attributes and table kept).
    HRESULT CloneInto(DWORD options, const D3DVERTEXELEMENT9* decl, IDirect3DDevice9* device, MeshCore& dst) const;

    // Rewrites the geometry: new face f is old face faceOrder[f] with corner indices
    // indices[3f..3f+2] (new vertex numbering); new vertex v is old vertex vertexSource[v].
    // New buffers are created when the vertex count changes.
    HRESULT Rearrange(const std::vector<DWORD>& faceOrder, const std::vector<DWORD>& indices,
                      const std::vector<DWORD>& vertexSource);

    // ID3DXMesh::OptimizeInplace (vertexRemap: new vertex -> old vertex).
    HRESULT Optimize(DWORD flags, const DWORD* adjIn, DWORD* adjOut, DWORD* faceRemap, std::vector<DWORD>* vertexRemap);

    // One attribute range per run of equal attribute ids in face order.
    void BuildTableFromRuns(const std::vector<DWORD>& indices);

    HRESULT Draw(DWORD attrib);
    HRESULT GenerateAdjacency(float epsilon, DWORD* adjacency) const;
};

// The core behind any mesh this layer created (null for foreign objects).
MeshCore* CoreOf(IUnknown* mesh);

// New ID3DXMesh objects; the core is filled by the caller through the returned pointer.
HRESULT NewMesh(ID3DXMesh** out, MeshCore** core);

// ---- topology helpers (shared by the mesh methods and the utilities) -----------------------

// Point representatives: vertices whose positions lie within epsilon share the smallest index.
std::vector<DWORD> PointRepsFromPositions(const BYTE* vertices, UINT stride, UINT positionOffset, DWORD count, float epsilon);
// Edge matching on point representatives (prep null = every vertex its own).
void AdjacencyFromPointReps(const std::vector<DWORD>& indices, DWORD numFaces, const DWORD* prep, DWORD* adjacency);
void PointRepsFromAdjacency(const std::vector<DWORD>& indices, DWORD numFaces, DWORD numVertices, const DWORD* adjacency, DWORD* prep);

// Face/vertex order of an optimize step: faces stably sorted by attribute when sorting; vertices
// grouped per attribute run (duplicated when shared between runs) when splitting, in first-use
// order when only sorting, original order otherwise; unused vertices dropped when compacting,
// else appended.
struct Layout
{
    std::vector<DWORD> faceOrder;      // new face -> old face
    std::vector<DWORD> indices;        // new corner indices in new numbering
    std::vector<DWORD> vertexSource;   // new vertex -> old vertex
};
Layout PlanLayout(const std::vector<DWORD>& indices, const std::vector<DWORD>& attribs, DWORD numVertices,
                  bool sort, bool split, bool compact, bool ignoreVertices);

// Small reference-counted base for the COM objects of this layer.
class RefCounted
{
public:
    virtual ~RefCounted() = default;
protected:
    ULONG DoAddRef() { return ++m_refs; }
    ULONG DoRelease() { const ULONG r = --m_refs; if (!r) delete this; return r; }
private:
    std::atomic<ULONG> m_refs{ 1 };
};

} // namespace ran_d3dx
