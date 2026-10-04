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

HRESULT WINAPI D3DXCreateEffectFromFileA(LPDIRECT3DDEVICE9, LPCSTR, CONST D3DXMACRO*, LPD3DXINCLUDE, DWORD, LPD3DXEFFECTPOOL,
                                         LPD3DXEFFECT* out, LPD3DXBUFFER* err)
{ D3DX_MISSING("D3DXCreateEffectFromFileA"); Clear(out); Clear(err); return E_NOTIMPL; }

HRESULT WINAPI D3DXCreateFontIndirectA(LPDIRECT3DDEVICE9, CONST D3DXFONT_DESCA*, LPD3DXFONT* out)
{ D3DX_MISSING("D3DXCreateFontIndirectA"); Clear(out); return E_NOTIMPL; }

HRESULT WINAPI D3DXCreateSprite(LPDIRECT3DDEVICE9, LPD3DXSPRITE* out)
{ D3DX_MISSING("D3DXCreateSprite"); Clear(out); return E_NOTIMPL; }

} // extern "C"
