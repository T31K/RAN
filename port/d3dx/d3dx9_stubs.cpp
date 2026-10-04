// D3DX functions the native client links but that are not implemented yet (plan Phase 3).
// Each one fails cleanly - out-pointers cleared, an error HRESULT returned, one log line the
// first time - so the game takes its own "could not load" path instead of crashing, and the
// first native runs show which of them it actually reaches. Every entry here moves to a real
// implementation (d3dx9_tex.cpp, d3dx9_mesh.cpp, ...) and disappears from this file.
#include "ran_compat.h"
#include <d3dx9.h>
#include <dxfile.h>
#include <cstdio>

namespace {
void Missing(const char* name)
{
    // One line per function (static per call site via the macro below).
    std::fprintf(stderr, "[d3dx] not implemented yet: %s\n", name);
}
template <class T> void Clear(T** p) { if (p) *p = nullptr; }
} // namespace

#define D3DX_MISSING(name) do { static bool once = false; if (!once) { once = true; Missing(name); } } while (0)

extern "C" {

HRESULT WINAPI D3DXCleanMesh(D3DXCLEANTYPE, LPD3DXMESH, CONST DWORD*, LPD3DXMESH* out, DWORD*, LPD3DXBUFFER* err)
{ D3DX_MISSING("D3DXCleanMesh"); Clear(out); Clear(err); return E_NOTIMPL; }

HRESULT WINAPI D3DXComputeNormals(LPD3DXBASEMESH, CONST DWORD*)
{ D3DX_MISSING("D3DXComputeNormals"); return E_NOTIMPL; }

HRESULT WINAPI D3DXComputeTangentFrameEx(ID3DXMesh*, DWORD, DWORD, DWORD, DWORD, DWORD, DWORD, DWORD, DWORD, DWORD,
                                         CONST DWORD*, FLOAT, FLOAT, FLOAT, ID3DXMesh** out, ID3DXBuffer** map)
{ D3DX_MISSING("D3DXComputeTangentFrameEx"); Clear(out); Clear(map); return E_NOTIMPL; }

HRESULT WINAPI D3DXCreateEffectFromFileA(LPDIRECT3DDEVICE9, LPCSTR, CONST D3DXMACRO*, LPD3DXINCLUDE, DWORD, LPD3DXEFFECTPOOL,
                                         LPD3DXEFFECT* out, LPD3DXBUFFER* err)
{ D3DX_MISSING("D3DXCreateEffectFromFileA"); Clear(out); Clear(err); return E_NOTIMPL; }

HRESULT WINAPI D3DXCreateFontIndirectA(LPDIRECT3DDEVICE9, CONST D3DXFONT_DESCA*, LPD3DXFONT* out)
{ D3DX_MISSING("D3DXCreateFontIndirectA"); Clear(out); return E_NOTIMPL; }

HRESULT WINAPI D3DXCreateMeshFVF(DWORD, DWORD, DWORD, DWORD, LPDIRECT3DDEVICE9, LPD3DXMESH* out)
{ D3DX_MISSING("D3DXCreateMeshFVF"); Clear(out); return E_NOTIMPL; }

HRESULT WINAPI D3DXCreateSprite(LPDIRECT3DDEVICE9, LPD3DXSPRITE* out)
{ D3DX_MISSING("D3DXCreateSprite"); Clear(out); return E_NOTIMPL; }

HRESULT WINAPI D3DXFileCreate(ID3DXFile** out)
{ D3DX_MISSING("D3DXFileCreate"); Clear(out); return E_NOTIMPL; }

HRESULT WINAPI D3DXFrameCalculateBoundingSphere(CONST D3DXFRAME*, LPD3DXVECTOR3, FLOAT*)
{ D3DX_MISSING("D3DXFrameCalculateBoundingSphere"); return E_NOTIMPL; }

HRESULT WINAPI D3DXGeneratePMesh(LPD3DXMESH, CONST DWORD*, CONST D3DXATTRIBUTEWEIGHTS*, CONST FLOAT*, DWORD, DWORD, LPD3DXPMESH* out)
{ D3DX_MISSING("D3DXGeneratePMesh"); Clear(out); return E_NOTIMPL; }

HRESULT WINAPI D3DXLoadMeshFromXA(LPCSTR, DWORD, LPDIRECT3DDEVICE9, LPD3DXBUFFER* adj, LPD3DXBUFFER* mat, LPD3DXBUFFER* fx,
                                  DWORD* n, LPD3DXMESH* out)
{ D3DX_MISSING("D3DXLoadMeshFromXA"); Clear(adj); Clear(mat); Clear(fx); if (n) *n = 0; Clear(out); return E_NOTIMPL; }

HRESULT WINAPI D3DXLoadMeshFromXof(LPD3DXFILEDATA, DWORD, LPDIRECT3DDEVICE9, LPD3DXBUFFER* adj, LPD3DXBUFFER* mat,
                                   LPD3DXBUFFER* fx, DWORD* n, LPD3DXMESH* out)
{ D3DX_MISSING("D3DXLoadMeshFromXof"); Clear(adj); Clear(mat); Clear(fx); if (n) *n = 0; Clear(out); return E_NOTIMPL; }

HRESULT WINAPI D3DXLoadMeshHierarchyFromXA(LPCSTR, DWORD, LPDIRECT3DDEVICE9, LPD3DXALLOCATEHIERARCHY, LPD3DXLOADUSERDATA,
                                           LPD3DXFRAME* frame, LPD3DXANIMATIONCONTROLLER* anim)
{ D3DX_MISSING("D3DXLoadMeshHierarchyFromXA"); Clear(frame); Clear(anim); return E_NOTIMPL; }

HRESULT WINAPI D3DXValidMesh(LPD3DXMESH, CONST DWORD*, LPD3DXBUFFER* err)
{ D3DX_MISSING("D3DXValidMesh"); Clear(err); return E_NOTIMPL; }

HRESULT WINAPI D3DXWeldVertices(LPD3DXMESH, DWORD, CONST D3DXWELDEPSILONS*, CONST DWORD*, DWORD*, DWORD*, LPD3DXBUFFER* remap)
{ D3DX_MISSING("D3DXWeldVertices"); Clear(remap); return E_NOTIMPL; }

HRESULT WINAPI DirectXFileCreate(LPDIRECTXFILE* out)
{ D3DX_MISSING("DirectXFileCreate"); Clear(out); return E_NOTIMPL; }

} // extern "C"
