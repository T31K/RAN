// Small D3DX helpers for the native build (plan Phase 3): FVF sizes, bounding spheres, frame
// hierarchy teardown, D3DXCreateTexture. Pinned by port/tests/d3dx_core_test.cpp.
#include "ran_compat.h"
#include <d3dx9.h>
#include <algorithm>
#include <cmath>

extern "C" {

UINT WINAPI D3DXGetFVFVertexSize(DWORD fvf)
{
    UINT size = 0;
    switch (fvf & D3DFVF_POSITION_MASK) {
    case D3DFVF_XYZ:    size = 12; break;
    case D3DFVF_XYZRHW: size = 16; break;
    case D3DFVF_XYZW:   size = 16; break;
    case D3DFVF_XYZB1:  size = 16; break;
    case D3DFVF_XYZB2:  size = 20; break;
    case D3DFVF_XYZB3:  size = 24; break;
    case D3DFVF_XYZB4:  size = 28; break;
    case D3DFVF_XYZB5:  size = 32; break;
    default: break;
    }
    if (fvf & D3DFVF_NORMAL)   size += 12;
    if (fvf & D3DFVF_PSIZE)    size += 4;
    if (fvf & D3DFVF_DIFFUSE)  size += 4;
    if (fvf & D3DFVF_SPECULAR) size += 4;
    const UINT texCount = (fvf & D3DFVF_TEXCOUNT_MASK) >> D3DFVF_TEXCOUNT_SHIFT;
    for (UINT i = 0; i < texCount; ++i) {
        switch ((fvf >> (16 + 2 * i)) & 3) {
        case D3DFVF_TEXTUREFORMAT1: size += 4; break;
        case D3DFVF_TEXTUREFORMAT2: size += 8; break;
        case D3DFVF_TEXTUREFORMAT3: size += 12; break;
        case D3DFVF_TEXTUREFORMAT4: size += 16; break;
        }
    }
    return size;
}

// D3DX's sphere: centre = average of the positions, radius = farthest position from it.
HRESULT WINAPI D3DXComputeBoundingSphere(CONST D3DXVECTOR3* first, DWORD count, DWORD stride, D3DXVECTOR3* center, FLOAT* radius)
{
    if (!first || !center || !radius) return D3DERR_INVALIDCALL;
    const BYTE* p = (const BYTE*)first;
    D3DXVECTOR3 c(0, 0, 0);
    for (DWORD i = 0; i < count; ++i) {
        const D3DXVECTOR3* v = (const D3DXVECTOR3*)(p + (size_t)i * stride);
        c.x += v->x; c.y += v->y; c.z += v->z;
    }
    if (count) { c.x /= count; c.y /= count; c.z /= count; }
    float r2 = 0.0f;
    for (DWORD i = 0; i < count; ++i) {
        const D3DXVECTOR3* v = (const D3DXVECTOR3*)(p + (size_t)i * stride);
        const float dx = v->x - c.x, dy = v->y - c.y, dz = v->z - c.z;
        r2 = std::max(r2, dx * dx + dy * dy + dz * dz);
    }
    *center = c;
    *radius = std::sqrt(r2);
    return D3D_OK;
}

HRESULT WINAPI D3DXFrameDestroy(LPD3DXFRAME frame, LPD3DXALLOCATEHIERARCHY alloc)
{
    if (!frame || !alloc) return D3DERR_INVALIDCALL;
    if (frame->pFrameFirstChild) D3DXFrameDestroy(frame->pFrameFirstChild, alloc);
    if (frame->pFrameSibling) D3DXFrameDestroy(frame->pFrameSibling, alloc);
    for (LPD3DXMESHCONTAINER mc = frame->pMeshContainer; mc;) {
        LPD3DXMESHCONTAINER next = mc->pNextMeshContainer;
        alloc->DestroyMeshContainer(mc);
        mc = next;
    }
    return alloc->DestroyFrame(frame);
}

// D3DXCreateTexture = D3DXCheckTextureRequirements + CreateTexture. DXVK has no power-of-two or
// square restrictions, so the adjustments left are D3DX's defaults.
HRESULT WINAPI D3DXCreateTexture(LPDIRECT3DDEVICE9 device, UINT width, UINT height, UINT mips, DWORD usage,
                                 D3DFORMAT format, D3DPOOL pool, LPDIRECT3DTEXTURE9* out)
{
    if (out) *out = nullptr;
    if (!device || !out) return D3DERR_INVALIDCALL;
    if (width == 0 || width == D3DX_DEFAULT) width = (height && height != D3DX_DEFAULT) ? height : 256;
    if (height == 0 || height == D3DX_DEFAULT) height = width;
    if (format == D3DFMT_UNKNOWN || format == (D3DFORMAT)D3DX_DEFAULT) format = D3DFMT_A8R8G8B8;
    UINT full = 1;
    for (UINT s = std::max(width, height); s > 1; s >>= 1) ++full;
    if (mips == 0 || mips == D3DX_DEFAULT || mips > full) mips = full;
    if (usage & D3DUSAGE_AUTOGENMIPMAP) mips = 0;
    return device->CreateTexture(width, height, mips, usage, format, pool, out, nullptr);
}

} // extern "C"
