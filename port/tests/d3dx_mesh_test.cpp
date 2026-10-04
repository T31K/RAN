// Tests for the native D3DX mesh layer (port/d3dx/d3dx9_mesh.cpp, d3dx9_meshutil.cpp,
// d3dx9_skin.cpp): mesh creation/locking/attribute tables/DrawSubset, Optimize remaps, clone
// format conversion, adjacency, normals, tangents, welding, skin info and blended meshes,
// progressive meshes, FVF <-> declaration. A memory-backed fake device stands in for d3d9.
#include "../d3dx/d3dx9_mesh_impl.h"
#include <climits>
#include <cmath>
#include <cstdio>
#include <cstring>
#include <vector>

static int g_failed = 0;
#define CHECK(cond) do { if (!(cond)) { std::printf("FAIL %s:%d  %s\n", __FILE__, __LINE__, #cond); ++g_failed; } } while (0)

static bool Near(float a, float b, float eps = 1e-4f) { return std::fabs(a - b) <= eps; }

// ---- fake device ---------------------------------------------------------------------------

template <class I, class Desc>
class FakeBuffer final : public I
{
public:
    FakeBuffer(UINT length, const Desc& desc) : data(length, 0xCD), desc(desc) {}
    std::vector<BYTE> data;
    Desc desc;
    ULONG refs = 1;

    STDMETHOD(QueryInterface)(REFIID, void** ppv) { *ppv = nullptr; return E_NOINTERFACE; }
    STDMETHOD_(ULONG, AddRef)() { return ++refs; }
    STDMETHOD_(ULONG, Release)() { const ULONG r = --refs; if (!r) delete this; return r; }
    STDMETHOD(GetDevice)(IDirect3DDevice9**) { return E_NOTIMPL; }
    STDMETHOD(SetPrivateData)(REFGUID, const void*, DWORD, DWORD) { return E_NOTIMPL; }
    STDMETHOD(GetPrivateData)(REFGUID, void*, DWORD*) { return E_NOTIMPL; }
    STDMETHOD(FreePrivateData)(REFGUID) { return E_NOTIMPL; }
    STDMETHOD_(DWORD, SetPriority)(DWORD) { return 0; }
    STDMETHOD_(DWORD, GetPriority)() { return 0; }
    STDMETHOD_(void, PreLoad)() {}
    STDMETHOD_(D3DRESOURCETYPE, GetType)() { return desc.Type; }
    STDMETHOD(Lock)(UINT offset, UINT, void** p, DWORD) { *p = data.data() + offset; return D3D_OK; }
    STDMETHOD(Unlock)() { return D3D_OK; }
    STDMETHOD(GetDesc)(Desc* d) { *d = desc; return D3D_OK; }
};
using FakeVB = FakeBuffer<IDirect3DVertexBuffer9, D3DVERTEXBUFFER_DESC>;
using FakeIB = FakeBuffer<IDirect3DIndexBuffer9, D3DINDEXBUFFER_DESC>;

struct DrawCall { INT base; UINT minIndex, numVertices, startIndex, primCount; };

class FakeDevice final : public IDirect3DDevice9
{
public:
    ULONG refs = 1;
    std::vector<DrawCall> draws;
    DWORD fvf = 0;
    IDirect3DVertexBuffer9* stream = nullptr;
    UINT streamStride = 0;
    IDirect3DIndexBuffer9* indices = nullptr;
    FakeVB* lastVB = nullptr;
    FakeIB* lastIB = nullptr;

    STDMETHOD(QueryInterface)(REFIID, void** ppv) { *ppv = nullptr; return E_NOINTERFACE; }
    STDMETHOD_(ULONG, AddRef)() { return ++refs; }
    STDMETHOD_(ULONG, Release)() { return --refs; }   // lives on the stack

    STDMETHOD(CreateVertexBuffer)(UINT length, DWORD usage, DWORD fvfIn, D3DPOOL pool, IDirect3DVertexBuffer9** out, HANDLE*)
    {
        D3DVERTEXBUFFER_DESC d = {};
        d.Format = D3DFMT_VERTEXDATA; d.Type = D3DRTYPE_VERTEXBUFFER; d.Usage = usage; d.Pool = pool; d.Size = length; d.FVF = fvfIn;
        *out = lastVB = new FakeVB(length, d);
        return D3D_OK;
    }
    STDMETHOD(CreateIndexBuffer)(UINT length, DWORD usage, D3DFORMAT format, D3DPOOL pool, IDirect3DIndexBuffer9** out, HANDLE*)
    {
        D3DINDEXBUFFER_DESC d = {};
        d.Format = format; d.Type = D3DRTYPE_INDEXBUFFER; d.Usage = usage; d.Pool = pool; d.Size = length;
        *out = lastIB = new FakeIB(length, d);
        return D3D_OK;
    }
    STDMETHOD(DrawIndexedPrimitive)(D3DPRIMITIVETYPE type, INT base, UINT minIndex, UINT numVertices, UINT startIndex, UINT primCount)
    {
        if (type != D3DPT_TRIANGLELIST) return D3DERR_INVALIDCALL;
        draws.push_back({ base, minIndex, numVertices, startIndex, primCount });
        return D3D_OK;
    }
    STDMETHOD(SetStreamSource)(UINT, IDirect3DVertexBuffer9* vb, UINT, UINT stride) { stream = vb; streamStride = stride; return D3D_OK; }
    STDMETHOD(SetIndices)(IDirect3DIndexBuffer9* ib) { indices = ib; return D3D_OK; }
    STDMETHOD(SetFVF)(DWORD f) { fvf = f; return D3D_OK; }
    STDMETHOD(SetVertexDeclaration)(IDirect3DVertexDeclaration9*) { return D3D_OK; }

    // Everything else is unused by the mesh layer.
    STDMETHOD(TestCooperativeLevel)() { return E_NOTIMPL; }
    STDMETHOD_(UINT, GetAvailableTextureMem)() { return 0; }
    STDMETHOD(EvictManagedResources)() { return E_NOTIMPL; }
    STDMETHOD(GetDirect3D)(IDirect3D9**) { return E_NOTIMPL; }
    STDMETHOD(GetDeviceCaps)(D3DCAPS9*) { return E_NOTIMPL; }
    STDMETHOD(GetDisplayMode)(UINT, D3DDISPLAYMODE*) { return E_NOTIMPL; }
    STDMETHOD(GetCreationParameters)(D3DDEVICE_CREATION_PARAMETERS*) { return E_NOTIMPL; }
    STDMETHOD(SetCursorProperties)(UINT, UINT, IDirect3DSurface9*) { return E_NOTIMPL; }
    STDMETHOD_(void, SetCursorPosition)(int, int, DWORD) {}
    STDMETHOD_(WINBOOL, ShowCursor)(WINBOOL) { return 0; }
    STDMETHOD(CreateAdditionalSwapChain)(D3DPRESENT_PARAMETERS*, IDirect3DSwapChain9**) { return E_NOTIMPL; }
    STDMETHOD(GetSwapChain)(UINT, IDirect3DSwapChain9**) { return E_NOTIMPL; }
    STDMETHOD_(UINT, GetNumberOfSwapChains)() { return 0; }
    STDMETHOD(Reset)(D3DPRESENT_PARAMETERS*) { return E_NOTIMPL; }
    STDMETHOD(Present)(const RECT*, const RECT*, HWND, const RGNDATA*) { return E_NOTIMPL; }
    STDMETHOD(GetBackBuffer)(UINT, UINT, D3DBACKBUFFER_TYPE, IDirect3DSurface9**) { return E_NOTIMPL; }
    STDMETHOD(GetRasterStatus)(UINT, D3DRASTER_STATUS*) { return E_NOTIMPL; }
    STDMETHOD(SetDialogBoxMode)(WINBOOL) { return E_NOTIMPL; }
    STDMETHOD_(void, SetGammaRamp)(UINT, DWORD, const D3DGAMMARAMP*) {}
    STDMETHOD_(void, GetGammaRamp)(UINT, D3DGAMMARAMP*) {}
    STDMETHOD(CreateTexture)(UINT, UINT, UINT, DWORD, D3DFORMAT, D3DPOOL, IDirect3DTexture9**, HANDLE*) { return E_NOTIMPL; }
    STDMETHOD(CreateVolumeTexture)(UINT, UINT, UINT, UINT, DWORD, D3DFORMAT, D3DPOOL, IDirect3DVolumeTexture9**, HANDLE*) { return E_NOTIMPL; }
    STDMETHOD(CreateCubeTexture)(UINT, UINT, DWORD, D3DFORMAT, D3DPOOL, IDirect3DCubeTexture9**, HANDLE*) { return E_NOTIMPL; }
    STDMETHOD(CreateRenderTarget)(UINT, UINT, D3DFORMAT, D3DMULTISAMPLE_TYPE, DWORD, WINBOOL, IDirect3DSurface9**, HANDLE*) { return E_NOTIMPL; }
    STDMETHOD(CreateDepthStencilSurface)(UINT, UINT, D3DFORMAT, D3DMULTISAMPLE_TYPE, DWORD, WINBOOL, IDirect3DSurface9**, HANDLE*) { return E_NOTIMPL; }
    STDMETHOD(UpdateSurface)(IDirect3DSurface9*, const RECT*, IDirect3DSurface9*, const POINT*) { return E_NOTIMPL; }
    STDMETHOD(UpdateTexture)(IDirect3DBaseTexture9*, IDirect3DBaseTexture9*) { return E_NOTIMPL; }
    STDMETHOD(GetRenderTargetData)(IDirect3DSurface9*, IDirect3DSurface9*) { return E_NOTIMPL; }
    STDMETHOD(GetFrontBufferData)(UINT, IDirect3DSurface9*) { return E_NOTIMPL; }
    STDMETHOD(StretchRect)(IDirect3DSurface9*, const RECT*, IDirect3DSurface9*, const RECT*, D3DTEXTUREFILTERTYPE) { return E_NOTIMPL; }
    STDMETHOD(ColorFill)(IDirect3DSurface9*, const RECT*, D3DCOLOR) { return E_NOTIMPL; }
    STDMETHOD(CreateOffscreenPlainSurface)(UINT, UINT, D3DFORMAT, D3DPOOL, IDirect3DSurface9**, HANDLE*) { return E_NOTIMPL; }
    STDMETHOD(SetRenderTarget)(DWORD, IDirect3DSurface9*) { return E_NOTIMPL; }
    STDMETHOD(GetRenderTarget)(DWORD, IDirect3DSurface9**) { return E_NOTIMPL; }
    STDMETHOD(SetDepthStencilSurface)(IDirect3DSurface9*) { return E_NOTIMPL; }
    STDMETHOD(GetDepthStencilSurface)(IDirect3DSurface9**) { return E_NOTIMPL; }
    STDMETHOD(BeginScene)() { return E_NOTIMPL; }
    STDMETHOD(EndScene)() { return E_NOTIMPL; }
    STDMETHOD(Clear)(DWORD, const D3DRECT*, DWORD, D3DCOLOR, float, DWORD) { return E_NOTIMPL; }
    STDMETHOD(SetTransform)(D3DTRANSFORMSTATETYPE, const D3DMATRIX*) { return E_NOTIMPL; }
    STDMETHOD(GetTransform)(D3DTRANSFORMSTATETYPE, D3DMATRIX*) { return E_NOTIMPL; }
    STDMETHOD(MultiplyTransform)(D3DTRANSFORMSTATETYPE, const D3DMATRIX*) { return E_NOTIMPL; }
    STDMETHOD(SetViewport)(const D3DVIEWPORT9*) { return E_NOTIMPL; }
    STDMETHOD(GetViewport)(D3DVIEWPORT9*) { return E_NOTIMPL; }
    STDMETHOD(SetMaterial)(const D3DMATERIAL9*) { return E_NOTIMPL; }
    STDMETHOD(GetMaterial)(D3DMATERIAL9*) { return E_NOTIMPL; }
    STDMETHOD(SetLight)(DWORD, const D3DLIGHT9*) { return E_NOTIMPL; }
    STDMETHOD(GetLight)(DWORD, D3DLIGHT9*) { return E_NOTIMPL; }
    STDMETHOD(LightEnable)(DWORD, WINBOOL) { return E_NOTIMPL; }
    STDMETHOD(GetLightEnable)(DWORD, WINBOOL*) { return E_NOTIMPL; }
    STDMETHOD(SetClipPlane)(DWORD, const float*) { return E_NOTIMPL; }
    STDMETHOD(GetClipPlane)(DWORD, float*) { return E_NOTIMPL; }
    STDMETHOD(SetRenderState)(D3DRENDERSTATETYPE, DWORD) { return E_NOTIMPL; }
    STDMETHOD(GetRenderState)(D3DRENDERSTATETYPE, DWORD*) { return E_NOTIMPL; }
    STDMETHOD(CreateStateBlock)(D3DSTATEBLOCKTYPE, IDirect3DStateBlock9**) { return E_NOTIMPL; }
    STDMETHOD(BeginStateBlock)() { return E_NOTIMPL; }
    STDMETHOD(EndStateBlock)(IDirect3DStateBlock9**) { return E_NOTIMPL; }
    STDMETHOD(SetClipStatus)(const D3DCLIPSTATUS9*) { return E_NOTIMPL; }
    STDMETHOD(GetClipStatus)(D3DCLIPSTATUS9*) { return E_NOTIMPL; }
    STDMETHOD(GetTexture)(DWORD, IDirect3DBaseTexture9**) { return E_NOTIMPL; }
    STDMETHOD(SetTexture)(DWORD, IDirect3DBaseTexture9*) { return E_NOTIMPL; }
    STDMETHOD(GetTextureStageState)(DWORD, D3DTEXTURESTAGESTATETYPE, DWORD*) { return E_NOTIMPL; }
    STDMETHOD(SetTextureStageState)(DWORD, D3DTEXTURESTAGESTATETYPE, DWORD) { return E_NOTIMPL; }
    STDMETHOD(GetSamplerState)(DWORD, D3DSAMPLERSTATETYPE, DWORD*) { return E_NOTIMPL; }
    STDMETHOD(SetSamplerState)(DWORD, D3DSAMPLERSTATETYPE, DWORD) { return E_NOTIMPL; }
    STDMETHOD(ValidateDevice)(DWORD*) { return E_NOTIMPL; }
    STDMETHOD(SetPaletteEntries)(UINT, const PALETTEENTRY*) { return E_NOTIMPL; }
    STDMETHOD(GetPaletteEntries)(UINT, PALETTEENTRY*) { return E_NOTIMPL; }
    STDMETHOD(SetCurrentTexturePalette)(UINT) { return E_NOTIMPL; }
    STDMETHOD(GetCurrentTexturePalette)(UINT*) { return E_NOTIMPL; }
    STDMETHOD(SetScissorRect)(const RECT*) { return E_NOTIMPL; }
    STDMETHOD(GetScissorRect)(RECT*) { return E_NOTIMPL; }
    STDMETHOD(SetSoftwareVertexProcessing)(WINBOOL) { return E_NOTIMPL; }
    STDMETHOD_(WINBOOL, GetSoftwareVertexProcessing)() { return 0; }
    STDMETHOD(SetNPatchMode)(float) { return E_NOTIMPL; }
    STDMETHOD_(float, GetNPatchMode)() { return 0; }
    STDMETHOD(DrawPrimitive)(D3DPRIMITIVETYPE, UINT, UINT) { return E_NOTIMPL; }
    STDMETHOD(DrawPrimitiveUP)(D3DPRIMITIVETYPE, UINT, const void*, UINT) { return E_NOTIMPL; }
    STDMETHOD(DrawIndexedPrimitiveUP)(D3DPRIMITIVETYPE, UINT, UINT, UINT, const void*, D3DFORMAT, const void*, UINT) { return E_NOTIMPL; }
    STDMETHOD(ProcessVertices)(UINT, UINT, UINT, IDirect3DVertexBuffer9*, IDirect3DVertexDeclaration9*, DWORD) { return E_NOTIMPL; }
    STDMETHOD(CreateVertexDeclaration)(const D3DVERTEXELEMENT9*, IDirect3DVertexDeclaration9**) { return E_NOTIMPL; }
    STDMETHOD(GetVertexDeclaration)(IDirect3DVertexDeclaration9**) { return E_NOTIMPL; }
    STDMETHOD(GetFVF)(DWORD*) { return E_NOTIMPL; }
    STDMETHOD(CreateVertexShader)(const DWORD*, IDirect3DVertexShader9**) { return E_NOTIMPL; }
    STDMETHOD(SetVertexShader)(IDirect3DVertexShader9*) { return E_NOTIMPL; }
    STDMETHOD(GetVertexShader)(IDirect3DVertexShader9**) { return E_NOTIMPL; }
    STDMETHOD(SetVertexShaderConstantF)(UINT, const float*, UINT) { return E_NOTIMPL; }
    STDMETHOD(GetVertexShaderConstantF)(UINT, float*, UINT) { return E_NOTIMPL; }
    STDMETHOD(SetVertexShaderConstantI)(UINT, const int*, UINT) { return E_NOTIMPL; }
    STDMETHOD(GetVertexShaderConstantI)(UINT, int*, UINT) { return E_NOTIMPL; }
    STDMETHOD(SetVertexShaderConstantB)(UINT, const WINBOOL*, UINT) { return E_NOTIMPL; }
    STDMETHOD(GetVertexShaderConstantB)(UINT, WINBOOL*, UINT) { return E_NOTIMPL; }
    STDMETHOD(GetStreamSource)(UINT, IDirect3DVertexBuffer9**, UINT*, UINT*) { return E_NOTIMPL; }
    STDMETHOD(SetStreamSourceFreq)(UINT, UINT) { return E_NOTIMPL; }
    STDMETHOD(GetStreamSourceFreq)(UINT, UINT*) { return E_NOTIMPL; }
    STDMETHOD(GetIndices)(IDirect3DIndexBuffer9**) { return E_NOTIMPL; }
    STDMETHOD(CreatePixelShader)(const DWORD*, IDirect3DPixelShader9**) { return E_NOTIMPL; }
    STDMETHOD(SetPixelShader)(IDirect3DPixelShader9*) { return E_NOTIMPL; }
    STDMETHOD(GetPixelShader)(IDirect3DPixelShader9**) { return E_NOTIMPL; }
    STDMETHOD(SetPixelShaderConstantF)(UINT, const float*, UINT) { return E_NOTIMPL; }
    STDMETHOD(GetPixelShaderConstantF)(UINT, float*, UINT) { return E_NOTIMPL; }
    STDMETHOD(SetPixelShaderConstantI)(UINT, const int*, UINT) { return E_NOTIMPL; }
    STDMETHOD(GetPixelShaderConstantI)(UINT, int*, UINT) { return E_NOTIMPL; }
    STDMETHOD(SetPixelShaderConstantB)(UINT, const WINBOOL*, UINT) { return E_NOTIMPL; }
    STDMETHOD(GetPixelShaderConstantB)(UINT, WINBOOL*, UINT) { return E_NOTIMPL; }
    STDMETHOD(DrawRectPatch)(UINT, const float*, const D3DRECTPATCH_INFO*) { return E_NOTIMPL; }
    STDMETHOD(DrawTriPatch)(UINT, const float*, const D3DTRIPATCH_INFO*) { return E_NOTIMPL; }
    STDMETHOD(DeletePatch)(UINT) { return E_NOTIMPL; }
    STDMETHOD(CreateQuery)(D3DQUERYTYPE, IDirect3DQuery9**) { return E_NOTIMPL; }
};

// ---- helpers -------------------------------------------------------------------------------

struct Vtx { float x, y, z, nx, ny, nz, u, v; };   // D3DFVF_XYZ | D3DFVF_NORMAL | D3DFVF_TEX1
const DWORD kFVF = D3DFVF_XYZ | D3DFVF_NORMAL | D3DFVF_TEX1;

// A unit quad in the XZ plane (clockwise seen from +y), u along x, v along z.
const Vtx kQuad[4] = {
    { 0, 0, 0, 0, 0, 0, 0, 0 }, { 0, 0, 1, 0, 0, 0, 0, 1 }, { 1, 0, 1, 0, 0, 0, 1, 1 }, { 1, 0, 0, 0, 0, 0, 1, 0 },
};

static ID3DXMesh* MakeMesh(FakeDevice& dev, const Vtx* v, DWORD nv, const WORD* idx, DWORD nf, const DWORD* attr, DWORD options = D3DXMESH_MANAGED)
{
    ID3DXMesh* mesh = nullptr;
    if (FAILED(D3DXCreateMeshFVF(nf, nv, options, kFVF, &dev, &mesh))) return nullptr;
    void* p;
    mesh->LockVertexBuffer(0, &p); std::memcpy(p, v, nv * sizeof(Vtx)); mesh->UnlockVertexBuffer();
    mesh->LockIndexBuffer(0, &p); std::memcpy(p, idx, nf * 3 * sizeof(WORD)); mesh->UnlockIndexBuffer();
    DWORD* a;
    mesh->LockAttributeBuffer(0, &a); for (DWORD f = 0; f < nf; ++f) a[f] = attr ? attr[f] : 0; mesh->UnlockAttributeBuffer();
    return mesh;
}

static std::vector<Vtx> Vertices(ID3DXBaseMesh* m)
{
    std::vector<Vtx> out(m->GetNumVertices());
    void* p;
    m->LockVertexBuffer(D3DLOCK_READONLY, &p); std::memcpy(out.data(), p, out.size() * sizeof(Vtx)); m->UnlockVertexBuffer();
    return out;
}

static std::vector<DWORD> Indices(ID3DXBaseMesh* m)
{
    std::vector<DWORD> out(m->GetNumFaces() * 3);
    void* p;
    m->LockIndexBuffer(D3DLOCK_READONLY, &p);
    for (size_t i = 0; i < out.size(); ++i)
        out[i] = (m->GetOptions() & D3DXMESH_32BIT) ? ((DWORD*)p)[i] : ((WORD*)p)[i];
    m->UnlockIndexBuffer();
    return out;
}

static std::vector<D3DXATTRIBUTERANGE> Table(ID3DXBaseMesh* m)
{
    DWORD n = 0;
    m->GetAttributeTable(nullptr, &n);
    std::vector<D3DXATTRIBUTERANGE> t(n);
    if (n) m->GetAttributeTable(t.data(), &n);
    return t;
}

static bool SamePos(const Vtx& a, const Vtx& b) { return a.x == b.x && a.y == b.y && a.z == b.z; }

// ---- tests ---------------------------------------------------------------------------------

static void TestDeclarations()
{
    D3DVERTEXELEMENT9 d[MAX_FVF_DECL_SIZE];
    const DWORD skinned = D3DFVF_XYZB4 | D3DFVF_LASTBETA_UBYTE4 | D3DFVF_NORMAL | D3DFVF_TEX1;
    CHECK(D3DXDeclaratorFromFVF(skinned, d) == D3D_OK);
    CHECK(D3DXGetDeclLength(d) == 5);
    CHECK(d[0].Usage == D3DDECLUSAGE_POSITION && d[0].Type == D3DDECLTYPE_FLOAT3 && d[0].Offset == 0);
    CHECK(d[1].Usage == D3DDECLUSAGE_BLENDWEIGHT && d[1].Type == D3DDECLTYPE_FLOAT3 && d[1].Offset == 12);
    CHECK(d[2].Usage == D3DDECLUSAGE_BLENDINDICES && d[2].Type == D3DDECLTYPE_UBYTE4 && d[2].Offset == 24);
    CHECK(d[3].Usage == D3DDECLUSAGE_NORMAL && d[3].Offset == 28 && d[4].Usage == D3DDECLUSAGE_TEXCOORD && d[4].Offset == 40);
    CHECK(d[5].Stream == 0xFF);
    CHECK(D3DXGetDeclVertexSize(d, 0) == 48 && D3DXGetDeclVertexSize(d, 1) == 0);

    const DWORD fvfs[] = {
        D3DFVF_XYZ, kFVF, skinned, D3DFVF_XYZB1 | D3DFVF_LASTBETA_UBYTE4 | D3DFVF_NORMAL | D3DFVF_TEX1,
        D3DFVF_XYZB2 | D3DFVF_NORMAL | D3DFVF_TEX1, D3DFVF_XYZRHW | D3DFVF_DIFFUSE | D3DFVF_SPECULAR | D3DFVF_TEX2,
        D3DFVF_XYZ | D3DFVF_PSIZE | D3DFVF_DIFFUSE | D3DFVF_TEX3 | D3DFVF_TEXCOORDSIZE3(1) | D3DFVF_TEXCOORDSIZE1(2),
        D3DFVF_XYZW | D3DFVF_NORMAL | D3DFVF_TEX1 | D3DFVF_TEXCOORDSIZE4(0),
        D3DFVF_XYZB5 | D3DFVF_LASTBETA_D3DCOLOR,
    };
    for (DWORD f : fvfs) {
        DWORD back = 0;
        CHECK(D3DXDeclaratorFromFVF(f, d) == D3D_OK);
        CHECK(D3DXGetDeclVertexSize(d, 0) == D3DXGetFVFVertexSize(f));
        CHECK(D3DXFVFFromDeclarator(d, &back) == D3D_OK && back == f);
    }
    // No FVF for tangents, and none for elements out of FVF order.
    const D3DVERTEXELEMENT9 tangent[] = {
        { 0, 0, D3DDECLTYPE_FLOAT3, 0, D3DDECLUSAGE_POSITION, 0 }, { 0, 12, D3DDECLTYPE_FLOAT3, 0, D3DDECLUSAGE_TANGENT, 0 }, D3DDECL_END()
    };
    const D3DVERTEXELEMENT9 swapped[] = {
        { 0, 0, D3DDECLTYPE_FLOAT3, 0, D3DDECLUSAGE_NORMAL, 0 }, { 0, 12, D3DDECLTYPE_FLOAT3, 0, D3DDECLUSAGE_POSITION, 0 }, D3DDECL_END()
    };
    DWORD fvf = 1;
    CHECK(D3DXFVFFromDeclarator(tangent, &fvf) == D3DERR_INVALIDCALL && fvf == 0);
    CHECK(D3DXFVFFromDeclarator(swapped, &fvf) == D3DERR_INVALIDCALL);
    CHECK(D3DXDeclaratorFromFVF(D3DFVF_XYZB5, d) == D3DERR_INVALIDCALL);   // five weights do not fit FLOAT4

    ID3DXBuffer* b = nullptr;
    CHECK(D3DXCreateBuffer(12, &b) == D3D_OK && b && b->GetBufferSize() == 12 && b->GetBufferPointer());
    CHECK(((BYTE*)b->GetBufferPointer())[11] == 0);
    b->Release();
}

static void TestCreateDraw(FakeDevice& dev)
{
    const WORD idx[6] = { 0, 1, 2, 0, 2, 3 };
    const DWORD attr[2] = { 0, 1 };
    ID3DXMesh* m = MakeMesh(dev, kQuad, 4, idx, 2, attr);
    CHECK(m != nullptr);
    if (!m) return;
    CHECK(m->GetNumFaces() == 2 && m->GetNumVertices() == 4 && m->GetFVF() == kFVF && m->GetNumBytesPerVertex() == 32);
    CHECK(m->GetOptions() == D3DXMESH_MANAGED);
    CHECK(dev.lastVB->desc.Pool == D3DPOOL_MANAGED && dev.lastVB->desc.FVF == kFVF && dev.lastVB->desc.Size == 128);
    CHECK(dev.lastIB->desc.Format == D3DFMT_INDEX16 && dev.lastIB->desc.Size == 12);
    IDirect3DDevice9* got = nullptr;
    CHECK(m->GetDevice(&got) == D3D_OK && got == &dev);
    got->Release();

    // No attribute table: DrawSubset draws the faces carrying the id.
    DWORD n = 99;
    CHECK(m->GetAttributeTable(nullptr, &n) == D3D_OK && n == 0);
    dev.draws.clear();
    CHECK(m->DrawSubset(1) == D3D_OK);
    CHECK(dev.draws.size() == 1 && dev.draws[0].startIndex == 3 && dev.draws[0].primCount == 1 && dev.draws[0].numVertices == 4);
    CHECK(dev.fvf == kFVF && dev.streamStride == 32 && dev.stream && dev.indices);
    dev.draws.clear();
    CHECK(m->DrawSubset(7) == D3D_OK && dev.draws.empty());

    // With a table: the range of that id.
    const D3DXATTRIBUTERANGE t[2] = { { 0, 0, 1, 0, 3 }, { 1, 1, 1, 0, 4 } };
    CHECK(m->SetAttributeTable(t, 2) == D3D_OK);
    CHECK(Table(m).size() == 2 && Table(m)[1].VertexCount == 4);
    dev.draws.clear();
    CHECK(m->DrawSubset(0) == D3D_OK);
    CHECK(dev.draws.size() == 1 && dev.draws[0].startIndex == 0 && dev.draws[0].primCount == 1 &&
          dev.draws[0].minIndex == 0 && dev.draws[0].numVertices == 3);
    // A writing lock of the attribute buffer drops the table (D3DX), a read-only one keeps it.
    DWORD* a;
    m->LockAttributeBuffer(D3DLOCK_READONLY, &a); m->UnlockAttributeBuffer();
    CHECK(Table(m).size() == 2);
    m->LockAttributeBuffer(0, &a); m->UnlockAttributeBuffer();
    CHECK(Table(m).empty());
    m->Release();

    // Options: 32-bit indices, system memory, write-only, dynamic.
    ID3DXMesh* m32 = nullptr;
    CHECK(D3DXCreateMeshFVF(1, 3, D3DXMESH_32BIT | D3DXMESH_SYSTEMMEM | D3DXMESH_WRITEONLY, kFVF, &dev, &m32) == D3D_OK);
    CHECK(dev.lastIB->desc.Format == D3DFMT_INDEX32 && dev.lastIB->desc.Pool == D3DPOOL_SYSTEMMEM && dev.lastIB->desc.Size == 12);
    CHECK(dev.lastVB->desc.Pool == D3DPOOL_SYSTEMMEM && (dev.lastVB->desc.Usage & D3DUSAGE_WRITEONLY));
    m32->Release();
    CHECK(D3DXCreateMeshFVF(1, 3, D3DXMESH_VB_DYNAMIC | D3DXMESH_IB_MANAGED, kFVF, &dev, &m32) == D3D_OK);
    CHECK(dev.lastVB->desc.Pool == D3DPOOL_DEFAULT && (dev.lastVB->desc.Usage & D3DUSAGE_DYNAMIC) && dev.lastIB->desc.Pool == D3DPOOL_MANAGED);
    m32->Release();
    ID3DXMesh* bad = (ID3DXMesh*)1;
    CHECK(D3DXCreateMeshFVF(0, 3, 0, kFVF, &dev, &bad) == D3DERR_INVALIDCALL && bad == nullptr);
    CHECK(D3DXCreateMeshFVF(1, 70000, 0, kFVF, &dev, &bad) == D3DERR_INVALIDCALL);
}

static void TestOptimize(FakeDevice& dev)
{
    // Five vertices, faces with attributes 1, 0, 1 sharing vertices 2 and 4 across groups.
    Vtx v[5];
    for (int i = 0; i < 5; ++i) v[i] = { (float)i, 0, 0, 0, 1, 0, 0, 0 };
    const WORD idx[9] = { 0, 1, 2, 2, 3, 4, 0, 2, 4 };
    const DWORD attr[3] = { 1, 0, 1 };
    ID3DXMesh* m = MakeMesh(dev, v, 5, idx, 3, attr);
    DWORD adj[9], adjOut[9], faceRemap[3];
    CHECK(m->GenerateAdjacency(0.0f, adj) == D3D_OK);
    ID3DXBuffer* vremap = nullptr;
    CHECK(m->OptimizeInplace(D3DXMESHOPT_ATTRSORT, adj, adjOut, faceRemap, &vremap) == D3D_OK);
    CHECK(faceRemap[0] == 1 && faceRemap[1] == 0 && faceRemap[2] == 2);
    CHECK(vremap && vremap->GetBufferSize() == 7 * sizeof(DWORD));
    if (vremap) {
        const DWORD* r = (const DWORD*)vremap->GetBufferPointer();
        const DWORD want[7] = { 2, 3, 4, 0, 1, 2, 4 };   // group 0 first; 2 and 4 duplicated for group 1
        CHECK(std::memcmp(r, want, sizeof(want)) == 0);
        vremap->Release();
    }
    CHECK(m->GetNumVertices() == 7);
    const std::vector<Vtx> nv = Vertices(m);
    CHECK(nv[0].x == 2 && nv[3].x == 0 && nv[5].x == 2 && nv[6].x == 4);
    const std::vector<DWORD> ni = Indices(m);
    const DWORD wantIdx[9] = { 0, 1, 2, 3, 4, 5, 3, 5, 6 };
    CHECK(std::memcmp(ni.data(), wantIdx, sizeof(wantIdx)) == 0);
    const std::vector<D3DXATTRIBUTERANGE> t = Table(m);
    CHECK(t.size() == 2);
    if (t.size() == 2) {
        CHECK(t[0].AttribId == 0 && t[0].FaceStart == 0 && t[0].FaceCount == 1 && t[0].VertexStart == 0 && t[0].VertexCount == 3);
        CHECK(t[1].AttribId == 1 && t[1].FaceStart == 1 && t[1].FaceCount == 2 && t[1].VertexStart == 3 && t[1].VertexCount == 4);
    }
    DWORD* a;
    m->LockAttributeBuffer(D3DLOCK_READONLY, &a);
    CHECK(a[0] == 0 && a[1] == 1 && a[2] == 1);
    m->UnlockAttributeBuffer();
    // Adjacency follows the faces: old face 2 (now 2) touched old face 0 (now 1) and 1 (now 0).
    CHECK((adjOut[6] == 1 || adjOut[7] == 1 || adjOut[8] == 1) && (adjOut[6] == 0 || adjOut[7] == 0 || adjOut[8] == 0));
    dev.draws.clear();
    m->DrawSubset(1);
    CHECK(dev.draws.size() == 1 && dev.draws[0].startIndex == 3 && dev.draws[0].primCount == 2 && dev.draws[0].minIndex == 3);
    m->Release();

    // DONOTSPLIT: shared vertices stay single; Optimize (not in place) returns a new mesh.
    m = MakeMesh(dev, v, 5, idx, 3, attr);
    ID3DXMesh* opt = nullptr;
    CHECK(m->Optimize(D3DXMESHOPT_ATTRSORT | D3DXMESHOPT_DONOTSPLIT | D3DXMESH_MANAGED, nullptr, nullptr, nullptr, &vremap, &opt) == D3D_OK);
    CHECK(opt && opt != m && opt->GetNumVertices() == 5 && m->GetNumVertices() == 5 && Table(m).empty());
    if (vremap) {
        const DWORD want[5] = { 2, 3, 4, 0, 1 };
        CHECK(std::memcmp(vremap->GetBufferPointer(), want, sizeof(want)) == 0);
        vremap->Release();
    }
    if (opt) opt->Release();
    m->Release();

    // COMPACT drops the unused vertex 4.
    const WORD idx2[6] = { 0, 1, 2, 0, 2, 3 };
    m = MakeMesh(dev, v, 5, idx2, 2, nullptr);
    CHECK(m->OptimizeInplace(D3DXMESHOPT_COMPACT, nullptr, nullptr, nullptr, nullptr) == D3D_OK);
    CHECK(m->GetNumVertices() == 4 && Vertices(m)[3].x == 3);
    m->Release();
}

static void TestCloneAdjacency(FakeDevice& dev)
{
    const WORD idx[6] = { 0, 1, 2, 0, 2, 3 };
    const DWORD attr[2] = { 0, 1 };
    Vtx q[4];
    std::memcpy(q, kQuad, sizeof(q));
    q[2].nx = 0.5f;
    ID3DXMesh* m = MakeMesh(dev, q, 4, idx, 2, attr);
    m->OptimizeInplace(D3DXMESHOPT_ATTRSORT | D3DXMESHOPT_DONOTSPLIT, nullptr, nullptr, nullptr, nullptr);

    // FVF conversion: position kept, normal dropped, tex0 widened to 3 (z = 0), diffuse/tex1 zero.
    const DWORD fvf2 = D3DFVF_XYZ | D3DFVF_DIFFUSE | D3DFVF_TEX2 | D3DFVF_TEXCOORDSIZE3(0);
    ID3DXMesh* c = nullptr;
    CHECK(m->CloneMeshFVF(D3DXMESH_SYSTEMMEM, fvf2, &dev, &c) == D3D_OK && c);
    if (c) {
        struct V2 { float x, y, z; DWORD diffuse; float u, v, w, s, t; };
        CHECK(c->GetFVF() == fvf2 && c->GetNumBytesPerVertex() == sizeof(V2) && c->GetOptions() == D3DXMESH_SYSTEMMEM);
        void* p;
        c->LockVertexBuffer(D3DLOCK_READONLY, &p);
        const V2* w = (const V2*)p;
        CHECK(w[2].x == 1 && w[2].z == 1 && w[2].u == 1 && w[2].v == 1 && w[2].w == 0 && w[2].diffuse == 0 && w[2].s == 0);
        c->UnlockVertexBuffer();
        CHECK(Indices(c) == Indices(m) && Table(c).size() == 2);
        DWORD* a;
        c->LockAttributeBuffer(D3DLOCK_READONLY, &a);
        CHECK(a[1] == 1);
        c->UnlockAttributeBuffer();
        // Back to the original layout: the normal comes back as zero.
        ID3DXMesh* back = nullptr;
        CHECK(c->CloneMeshFVF(D3DXMESH_MANAGED, kFVF, &dev, &back) == D3D_OK);
        if (back) { CHECK(Vertices(back)[2].u == 1 && Vertices(back)[2].nx == 0); back->Release(); }
        c->Release();
    }
    // Type conversion through CloneMesh: FLOAT3 normal -> D3DCOLOR (r = 0.5 -> 0x80), FLOAT16 texcoord.
    const D3DVERTEXELEMENT9 decl[] = {
        { 0, 0, D3DDECLTYPE_FLOAT3, 0, D3DDECLUSAGE_POSITION, 0 },
        { 0, 12, D3DDECLTYPE_D3DCOLOR, 0, D3DDECLUSAGE_NORMAL, 0 },
        { 0, 16, D3DDECLTYPE_FLOAT16_2, 0, D3DDECLUSAGE_TEXCOORD, 0 },
        D3DDECL_END()
    };
    CHECK(m->CloneMesh(D3DXMESH_MANAGED, decl, &dev, &c) == D3D_OK && c);
    if (c) {
        CHECK(c->GetFVF() == 0 && c->GetNumBytesPerVertex() == 20);
        void* p;
        c->LockVertexBuffer(D3DLOCK_READONLY, &p);
        const BYTE* b = (const BYTE*)p + 2 * 20;
        DWORD color; WORD half[2];
        std::memcpy(&color, b + 12, 4); std::memcpy(half, b + 16, 4);
        CHECK(color == 0xFF800000 && half[0] == 0x3C00 && half[1] == 0x3C00);
        c->UnlockVertexBuffer();
        // FVF-less meshes draw through a vertex declaration (the fake device has none to give).
        CHECK(c->DrawSubset(0) == E_NOTIMPL);
        c->Release();
    }

    // Adjacency of the quad: the diagonal 0-2 is shared.
    DWORD adj[6];
    CHECK(m->GenerateAdjacency(0.0f, adj) == D3D_OK);
    CHECK(adj[0] == UNUSED32 && adj[1] == UNUSED32 && adj[2] == 1 && adj[3] == 0 && adj[4] == UNUSED32 && adj[5] == UNUSED32);
    m->Release();

    // Six vertices, no sharing: adjacency still found through equal positions, and point reps.
    const Vtx six[6] = { kQuad[0], kQuad[1], kQuad[2], kQuad[0], kQuad[2], kQuad[3] };
    const WORD idx6[6] = { 0, 1, 2, 3, 4, 5 };
    m = MakeMesh(dev, six, 6, idx6, 2, nullptr);
    CHECK(m->GenerateAdjacency(0.0f, adj) == D3D_OK && adj[2] == 1 && adj[3] == 0);
    DWORD prep[6];
    CHECK(m->ConvertAdjacencyToPointReps(adj, prep) == D3D_OK);
    CHECK(prep[0] == 0 && prep[2] == 2 && prep[3] == 0 && prep[4] == 2 && prep[5] == 5 && prep[1] == 1);
    DWORD adj2[6];
    CHECK(m->ConvertPointRepsToAdjacency(prep, adj2) == D3D_OK && std::memcmp(adj, adj2, sizeof(adj)) == 0);
    CHECK(m->ConvertPointRepsToAdjacency(nullptr, adj2) == D3D_OK && adj2[2] == UNUSED32);
    // Positions a hair apart join only with a large enough epsilon.
    Vtx* p;
    m->LockVertexBuffer(0, (void**)&p); p[3].x = 0.001f; m->UnlockVertexBuffer();
    CHECK(m->GenerateAdjacency(0.0f, adj) == D3D_OK && adj[2] == UNUSED32);
    CHECK(m->GenerateAdjacency(0.01f, adj) == D3D_OK && adj[2] == 1);
    m->Release();
}

static void TestNormalsTangents(FakeDevice& dev)
{
    const WORD idx[6] = { 0, 1, 2, 0, 2, 3 };
    ID3DXMesh* m = MakeMesh(dev, kQuad, 4, idx, 2, nullptr);
    CHECK(D3DXComputeNormals(m, nullptr) == D3D_OK);
    for (const Vtx& v : Vertices(m)) CHECK(Near(v.nx, 0) && Near(v.ny, 1) && Near(v.nz, 0));

    // The game's FilterMesh: clone to a tangent layout, then ComputeTangentFrameEx.
    const D3DVERTEXELEMENT9 decl[] = {
        { 0, 0, D3DDECLTYPE_FLOAT3, 0, D3DDECLUSAGE_POSITION, 0 },
        { 0, 12, D3DDECLTYPE_FLOAT3, 0, D3DDECLUSAGE_NORMAL, 0 },
        { 0, 24, D3DDECLTYPE_FLOAT2, 0, D3DDECLUSAGE_TEXCOORD, 0 },
        { 0, 32, D3DDECLTYPE_FLOAT3, 0, D3DDECLUSAGE_TANGENT, 0 },
        { 0, 44, D3DDECLTYPE_FLOAT4, 0, D3DDECLUSAGE_BLENDWEIGHT, 0 },
        { 0, 60, D3DDECLTYPE_FLOAT4, 0, D3DDECLUSAGE_BLENDINDICES, 0 },
        D3DDECL_END()
    };
    ID3DXMesh* t = nullptr;
    ID3DXMesh* out = nullptr;
    ID3DXBuffer* map = nullptr;
    CHECK(m->CloneMesh(m->GetOptions(), decl, &dev, &t) == D3D_OK && t);
    CHECK(D3DXComputeTangentFrameEx(t, D3DDECLUSAGE_TEXCOORD, 0, D3DDECLUSAGE_TANGENT, 0, D3DX_DEFAULT, 0, D3DDECLUSAGE_NORMAL, 0,
                                    0, nullptr, -1, 0, -1, &out, &map) == D3D_OK && out && out != t);
    if (out) {
        void* p;
        out->LockVertexBuffer(D3DLOCK_READONLY, &p);
        for (int i = 0; i < 4; ++i) {
            const float* f = (const float*)((const BYTE*)p + i * 76 + 32);
            CHECK(Near(f[0], 1) && Near(f[1], 0) && Near(f[2], 0));
        }
        out->UnlockVertexBuffer();
        out->Release();
    }
    CHECK(map && map->GetBufferSize() == 16 && ((DWORD*)map->GetBufferPointer())[3] == 3);
    if (map) map->Release();
    // In place with computed normals and a binormal output into a missing element fails cleanly.
    CHECK(D3DXComputeTangentFrameEx(t, D3DDECLUSAGE_TEXCOORD, 0, D3DDECLUSAGE_TANGENT, 0, D3DDECLUSAGE_BINORMAL, 0,
                                    D3DDECLUSAGE_NORMAL, 0, D3DXTANGENT_GENERATE_IN_PLACE, nullptr, -1, 0, -1, nullptr, nullptr) == D3DERR_INVALIDCALL);
    t->Release();

    D3DXVECTOR3 mn, mx;
    CHECK(D3DXComputeBoundingBox((const D3DXVECTOR3*)&kQuad[0].x, 4, sizeof(Vtx), &mn, &mx) == D3D_OK);
    CHECK(mn.x == 0 && mn.z == 0 && mx.x == 1 && mx.z == 1 && mx.y == 0);
    m->Release();
}

static void TestWeldCleanValid(FakeDevice& dev)
{
    const Vtx six[6] = { kQuad[0], kQuad[1], kQuad[2], kQuad[0], kQuad[2], kQuad[3] };
    const WORD idx6[6] = { 0, 1, 2, 3, 4, 5 };
    D3DXWELDEPSILONS eps;
    std::memset(&eps, 0, sizeof(eps));

    // One attribute: the duplicates of vertices 0 and 2 merge.
    ID3DXMesh* m = MakeMesh(dev, six, 6, idx6, 2, nullptr);
    DWORD adj[6], faceRemap[2];
    m->GenerateAdjacency(0.0f, adj);
    ID3DXBuffer* vremap = nullptr;
    CHECK(D3DXWeldVertices(m, 0, &eps, adj, adj, faceRemap, &vremap) == D3D_OK);
    CHECK(m->GetNumVertices() == 4 && faceRemap[0] == 0 && faceRemap[1] == 1);
    const std::vector<DWORD> wi = Indices(m);
    const std::vector<Vtx> wv = Vertices(m);
    CHECK(wi[3] == wi[0] && wi[4] == wi[2]);
    CHECK(SamePos(wv[wi[5]], kQuad[3]) && SamePos(wv[wi[1]], kQuad[1]));
    CHECK(vremap && vremap->GetBufferSize() == 16);
    if (vremap) vremap->Release();
    CHECK(adj[2] == 1 && adj[3] == 0);
    CHECK(D3DXValidMesh(m, adj, nullptr) == D3D_OK);
    m->Release();

    // Different attributes: the faces keep their own copies (D3DX splits again on the sort).
    const DWORD attr[2] = { 0, 1 };
    m = MakeMesh(dev, six, 6, idx6, 2, attr);
    CHECK(D3DXWeldVertices(m, 0, &eps, nullptr, nullptr, nullptr, nullptr) == D3D_OK && m->GetNumVertices() == 6);
    CHECK(D3DXWeldVertices(m, D3DXWELDEPSILONS_DONOTSPLIT, &eps, nullptr, nullptr, nullptr, nullptr) == D3D_OK && m->GetNumVertices() == 4);
    m->Release();

    // A differing normal blocks the weld unless the normal epsilon covers it.
    Vtx bent[6];
    std::memcpy(bent, six, sizeof(bent));
    bent[3].ny = 0.5f;
    m = MakeMesh(dev, bent, 6, idx6, 2, nullptr);
    CHECK(D3DXWeldVertices(m, 0, &eps, nullptr, nullptr, nullptr, nullptr) == D3D_OK && m->GetNumVertices() == 5);
    eps.Normal = 1.0f;
    CHECK(D3DXWeldVertices(m, 0, &eps, nullptr, nullptr, nullptr, nullptr) == D3D_OK && m->GetNumVertices() == 4);

    // CleanMesh: a copy, adjacency passed through (in and out may be the same array).
    m->GenerateAdjacency(0.0f, adj);
    ID3DXMesh* clean = nullptr;
    const DWORD before[6] = { adj[0], adj[1], adj[2], adj[3], adj[4], adj[5] };
    CHECK(D3DXCleanMesh(D3DXCLEAN_SIMPLIFICATION, m, adj, &clean, adj, nullptr) == D3D_OK && clean && clean != m);
    CHECK(std::memcmp(before, adj, sizeof(adj)) == 0);
    if (clean) { CHECK(clean->GetNumFaces() == 2 && clean->GetNumVertices() == 4); clean->Release(); }

    // ValidMesh reports broken adjacency.
    adj[2] = 5;
    ID3DXBuffer* errors = nullptr;
    CHECK(D3DXValidMesh(m, adj, &errors) == D3DXERR_INVALIDMESH && errors && std::strstr((const char*)errors->GetBufferPointer(), "face 0"));
    if (errors) errors->Release();
    m->Release();
}

static void TestSkin(FakeDevice& dev)
{
    // Two bones over the quad: v0 bone 0, v1 half/half, v2 mostly bone 1, v3 bone 1.
    ID3DXSkinInfo* skin = nullptr;
    CHECK(D3DXCreateSkinInfoFVF(4, kFVF, 2, &skin) == D3D_OK && skin);
    if (!skin) return;
    const DWORD v0[3] = { 0, 1, 2 }, v1[3] = { 1, 2, 3 };
    const float w0[3] = { 1.0f, 0.5f, 0.25f }, w1[3] = { 0.5f, 0.75f, 1.0f };
    CHECK(skin->SetBoneInfluence(0, 3, v0, w0) == D3D_OK && skin->SetBoneInfluence(1, 3, v1, w1) == D3D_OK);
    CHECK(skin->SetBoneInfluence(2, 3, v1, w1) == D3DERR_INVALIDCALL);
    CHECK(skin->GetNumBones() == 2 && skin->GetNumBoneInfluences(1) == 3 && skin->GetFVF() == kFVF);
    DWORD gv[3]; float gw[3];
    CHECK(skin->GetBoneInfluence(1, gv, gw) == D3D_OK && gv[2] == 3 && gw[1] == 0.75f);
    DWORD maxV = 0, maxF = 0, at = 0;
    CHECK(skin->GetMaxVertexInfluences(&maxV) == D3D_OK && maxV == 2);
    CHECK(skin->FindBoneVertexInfluenceIndex(1, 3, &at) == D3D_OK && at == 2);
    CHECK(skin->FindBoneVertexInfluenceIndex(0, 3, &at) == D3DERR_NOTFOUND);
    CHECK(skin->GetBoneName(0) == nullptr && skin->SetBoneName(0, "Bip01") == D3D_OK && std::strcmp(skin->GetBoneName(0), "Bip01") == 0);
    CHECK(skin->GetBoneOffsetMatrix(1)->_11 == 1 && skin->GetBoneOffsetMatrix(1)->_41 == 0);
    D3DXMATRIX off;
    std::memset(&off, 0, sizeof(off));
    off._11 = off._22 = off._33 = off._44 = 2;
    CHECK(skin->SetBoneOffsetMatrix(1, &off) == D3D_OK && skin->GetBoneOffsetMatrix(1)->_22 == 2);

    const WORD idx[6] = { 0, 1, 2, 0, 2, 3 };
    ID3DXMesh* m = MakeMesh(dev, kQuad, 4, idx, 2, nullptr);
    IDirect3DIndexBuffer9* ib = nullptr;
    m->GetIndexBuffer(&ib);
    CHECK(skin->GetMaxFaceInfluences(ib, 2, &maxF) == D3D_OK && maxF == 2);
    ib->Release();
    DWORD adj[6];
    m->GenerateAdjacency(0.0f, adj);

    // Indexed: XYZB2 + UBYTE4 indices (the game's 0x1118 layout), one combination.
    DWORD infl = 0, combos = 0, adjOut[6], faceRemap[2];
    ID3DXBuffer* table = nullptr;
    ID3DXBuffer* vremap = nullptr;
    ID3DXMesh* blended = nullptr;
    CHECK(skin->ConvertToIndexedBlendedMesh(m, D3DXMESH_MANAGED | D3DXMESHOPT_VERTEXCACHE, 26, adj, adjOut, faceRemap, &vremap,
                                            &infl, &combos, &table, &blended) == D3D_OK);
    CHECK(blended && infl == 2 && combos == 1 && table && vremap);
    if (blended && table && vremap) {
        CHECK(blended->GetFVF() == 0x1118 && blended->GetOptions() == D3DXMESH_MANAGED);
        const D3DXBONECOMBINATION* bc = (const D3DXBONECOMBINATION*)table->GetBufferPointer();
        CHECK(bc[0].AttribId == 0 && bc[0].FaceStart == 0 && bc[0].FaceCount == 2 && bc[0].VertexStart == 0 && bc[0].VertexCount == 4);
        CHECK(bc[0].BoneId[0] == 0 && bc[0].BoneId[1] == 1 && bc[0].BoneId[2] == UINT_MAX && bc[0].BoneId[25] == UINT_MAX);
        const std::vector<D3DXATTRIBUTERANGE> t = Table(blended);
        CHECK(t.size() == 1 && t[0].AttribId == 0 && t[0].FaceCount == 2);
        struct BV { float x, y, z, w; BYTE i[4]; float nx, ny, nz, u, v; };
        CHECK(blended->GetNumBytesPerVertex() == sizeof(BV));
        const DWORD* r = (const DWORD*)vremap->GetBufferPointer();
        void* p;
        blended->LockVertexBuffer(D3DLOCK_READONLY, &p);
        const BV* bv = (const BV*)p;
        for (DWORD n = 0; n < blended->GetNumVertices(); ++n) {
            switch (r[n]) {
            case 0: CHECK(Near(bv[n].w, 1) && bv[n].i[0] == 0); break;
            case 1: CHECK(Near(bv[n].w, 0.5f) && bv[n].i[0] + bv[n].i[1] == 1); break;
            case 2: CHECK(Near(bv[n].w, 0.75f) && bv[n].i[0] == 1 && bv[n].i[1] == 0); break;   // strongest first
            case 3: CHECK(Near(bv[n].w, 1) && bv[n].i[0] == 1); break;
            default: CHECK(!"bad remap");
            }
            CHECK(SamePos(Vtx{ bv[n].x, bv[n].y, bv[n].z, 0, 0, 0, 0, 0 }, kQuad[r[n]]) && bv[n].u == kQuad[r[n]].u);
        }
        blended->UnlockVertexBuffer();
        CHECK(adjOut[faceRemap[0] == 0 ? 2 : 3] != UNUSED32);
    }
    if (table) table->Release();
    if (vremap) vremap->Release();
    if (blended) blended->Release();

    // Non-indexed with two attributes: one combination per attribute, shared vertices split.
    DWORD* a;
    m->LockAttributeBuffer(0, &a); a[1] = 1; m->UnlockAttributeBuffer();
    CHECK(skin->ConvertToBlendedMesh(m, D3DXMESH_MANAGED, adj, nullptr, nullptr, &vremap, &infl, &combos, &table, &blended) == D3D_OK);
    CHECK(blended && infl == 2 && combos == 2);
    if (blended && table && vremap) {
        CHECK(blended->GetFVF() == (D3DFVF_XYZB1 | D3DFVF_NORMAL | D3DFVF_TEX1) && blended->GetNumVertices() == 6);
        const D3DXBONECOMBINATION* bc = (const D3DXBONECOMBINATION*)table->GetBufferPointer();
        CHECK(bc[0].AttribId == 0 && bc[1].AttribId == 1 && bc[1].FaceStart == 1 && bc[1].VertexStart == 3 && bc[1].VertexCount == 3);
        CHECK(bc[0].BoneId[0] == 0 && bc[0].BoneId[1] == 1);
        const std::vector<D3DXATTRIBUTERANGE> t = Table(blended);
        CHECK(t.size() == 2 && t[0].AttribId == 0 && t[1].AttribId == 1 && t[1].FaceStart == 1);
        struct BV { float x, y, z, w, nx, ny, nz, u, v; };
        const DWORD* r = (const DWORD*)vremap->GetBufferPointer();
        void* p;
        blended->LockVertexBuffer(D3DLOCK_READONLY, &p);
        const BV* bv = (const BV*)p;
        for (DWORD n = 0; n < 6; ++n) {
            // Weight slot 0 = BoneId[0] = bone 0.
            const float want[4] = { 1.0f, 0.5f, 0.25f, 0.0f };
            CHECK(Near(bv[n].w, want[r[n]]));
        }
        blended->UnlockVertexBuffer();
    }
    if (table) table->Release();
    if (vremap) vremap->Release();
    if (blended) blended->Release();
    m->Release();

    // Software skinning: bone 1 moves up by 10.
    D3DXMATRIX bones[2];
    std::memset(bones, 0, sizeof(bones));
    for (D3DXMATRIX& b : bones) b._11 = b._22 = b._33 = b._44 = 1;
    bones[1]._42 = 10;
    Vtx skinned[4];
    CHECK(skin->UpdateSkinnedMesh(bones, nullptr, kQuad, skinned) == D3D_OK);
    CHECK(Near(skinned[0].y, 0) && Near(skinned[1].y, 5) && Near(skinned[2].y, 7.5f) && Near(skinned[3].y, 10));
    CHECK(skinned[2].x == 1 && skinned[2].u == 1 && skinned[2].nx == 0);

    // Clone and Remap (new -> old, reversed): bone 1 now covers vertices 2, 1, 0.
    ID3DXSkinInfo* copy = nullptr;
    CHECK(skin->Clone(&copy) == D3D_OK && copy && std::strcmp(copy->GetBoneName(0), "Bip01") == 0);
    DWORD remap[4] = { 3, 2, 1, 0 };
    CHECK(copy->Remap(4, remap) == D3D_OK);
    CHECK(copy->GetBoneInfluence(1, gv, gw) == D3D_OK && gv[0] == 2 && gv[1] == 1 && gv[2] == 0 && gw[2] == 1.0f);
    CHECK(skin->GetBoneInfluence(1, gv, gw) == D3D_OK && gv[0] == 1);   // the original is untouched
    copy->Release();
    skin->Release();
}

static void TestPMesh(FakeDevice& dev)
{
    const WORD idx[6] = { 0, 1, 2, 0, 2, 3 };
    ID3DXMesh* m = MakeMesh(dev, kQuad, 4, idx, 2, nullptr);
    DWORD adj[6];
    m->GenerateAdjacency(0.0001f, adj);
    ID3DXPMesh* pm = nullptr;
    CHECK(D3DXGeneratePMesh(m, adj, nullptr, nullptr, 1, D3DXMESHSIMP_VERTEX, &pm) == D3D_OK && pm);
    if (!pm) { m->Release(); return; }
    CHECK(pm->GetMinVertices() == 4 && pm->GetMaxVertices() == 4 && pm->GetMinFaces() == 2 && pm->GetMaxFaces() == 2);
    CHECK(pm->GetFVF() == kFVF && pm->GetNumFaces() == 2);
    ID3DXPMesh* clone = nullptr;
    CHECK(pm->ClonePMeshFVF(D3DXMESH_MANAGED | D3DXMESH_VB_SHARE, pm->GetFVF(), &dev, &clone) == D3D_OK && clone);
    if (clone) {
        IDirect3DVertexBuffer9 *a = nullptr, *b = nullptr;
        pm->GetVertexBuffer(&a); clone->GetVertexBuffer(&b);
        CHECK(a == b);   // VB_SHARE
        a->Release(); b->Release();
        CHECK(clone->GetOptions() == D3DXMESH_MANAGED);
        CHECK(clone->TrimByVertices(4, 4, nullptr, nullptr) == D3D_OK);
        CHECK(clone->OptimizeBaseLOD(D3DXMESHOPT_VERTEXCACHE, nullptr) == D3D_OK);
        CHECK(clone->SetNumVertices(4) == D3D_OK && clone->SetNumFaces(1) == D3D_OK && clone->GetNumFaces() == 2);
        ID3DXMesh* flat = nullptr;
        CHECK(clone->CloneMeshFVF(D3DXMESH_MANAGED, kFVF, &dev, &flat) == D3D_OK && flat);
        if (flat) {
            CHECK(flat->GetNumFaces() == 2 && flat->GetNumVertices() == 4 && SamePos(Vertices(flat)[2], kQuad[2]));
            DWORD fa[6];
            flat->GenerateAdjacency(0.0001f, fa);
            CHECK(flat->OptimizeInplace(D3DXMESHOPT_COMPACT | D3DXMESHOPT_VERTEXCACHE, fa, nullptr, nullptr, nullptr) == D3D_OK);
            CHECK(Table(flat).size() == 1 && Table(flat)[0].VertexCount == 4);
            flat->Release();
        }
        DWORD got[6];
        CHECK(clone->GetAdjacency(got) == D3D_OK && std::memcmp(got, adj, sizeof(adj)) == 0);
        clone->Release();
    }
    CHECK(pm->DrawSubset(0) == D3D_OK);
    pm->Release();
    m->Release();
}

int main()
{
    FakeDevice dev;
    TestDeclarations();
    TestCreateDraw(dev);
    TestOptimize(dev);
    TestCloneAdjacency(dev);
    TestNormalsTangents(dev);
    TestWeldCleanValid(dev);
    TestSkin(dev);
    TestPMesh(dev);
    CHECK(dev.refs == 1);   // every mesh released its device reference

    if (g_failed) { std::printf("%d check(s) failed\n", g_failed); return 1; }
    std::printf("d3dx_mesh_test: all checks passed\n");
    return 0;
}
