// Image decoding and pixel-format conversion behind the native D3DX texture functions
// (d3dx9_tex.cpp). Pixels in memory are 32-bit 0xAARRGGBB, which is D3DFMT_A8R8G8B8's layout.
// Tested by port/tests/d3dx_image_test.cpp.
#pragma once
#include "ran_compat.h"
#include <d3d9.h>
#include <d3dx9tex.h>
#include <cstdint>
#include <vector>

namespace ran_d3dx {

// --- pixel formats ---------------------------------------------------------------------------
bool IsBlockCompressed(D3DFORMAT f);           // DXT1..DXT5
UINT BytesPerPixel(D3DFORMAT f);               // 0 for block-compressed or unknown formats
UINT RowBytes(D3DFORMAT f, UINT width);        // bytes in one row (one block row when compressed)
UINT RowCount(D3DFORMAT f, UINT height);       // rows (block rows when compressed)
bool CanDecode(D3DFORMAT f);
bool CanEncode(D3DFORMAT f);

// Decode a w x h rect stored as `f` (rows `pitch` bytes apart) into 0xAARRGGBB pixels.
bool Decode(D3DFORMAT f, const BYTE* src, UINT pitch, UINT w, UINT h, uint32_t* out);
// Encode 0xAARRGGBB pixels into an uncompressed format.
bool Encode(D3DFORMAT f, const uint32_t* in, UINT w, UINT h, BYTE* dst, UINT pitch);

// --- 32-bit images ----------------------------------------------------------------------------
struct Pixels
{
    UINT w = 0, h = 0;
    std::vector<uint32_t> argb;
};
Pixels Resize(const Pixels& src, UINT w, UINT h);   // bilinear (box when shrinking by 2^n)
Pixels HalfSize(const Pixels& src);                 // next mip level, 2x2 box filter
void ApplyColorKey(Pixels& p, D3DCOLOR key);        // key -> transparent black, like D3DX

// --- image files ------------------------------------------------------------------------------
struct Image
{
    D3DXIMAGE_INFO info = {};    // as D3DX reports it (Format = the file's pixel format)
    D3DFORMAT storage = D3DFMT_UNKNOWN;   // format of `levels` data (DDS) or A8R8G8B8 (decoded)
    UINT faces = 1;              // 6 for a cube map
    // Per face, per mip level: the level's bytes inside `bytes`.
    struct Level { size_t offset = 0; UINT w = 0, h = 0, pitch = 0; };
    std::vector<Level> levels;   // faces * info.MipLevels entries, face-major
    std::vector<BYTE> bytes;

    const Level& At(UINT face, UINT level) const { return levels[face * info.MipLevels + level]; }
    const BYTE* Data(UINT face, UINT level) const { return bytes.data() + At(face, level).offset; }
    Pixels DecodeLevel(UINT face, UINT level) const;
};

// DDS (DX9 headers: DXTn, 16/24/32-bit RGB, L8/A8L8/A8, cube maps), TGA, BMP, JPG, PNG, GIF.
HRESULT LoadImage(const void* data, size_t size, Image& out);

} // namespace ran_d3dx
