// D3DX texture functions for the native build (plan Phase 3): load textures and cube maps from
// files/memory, copy/convert surfaces, save textures and surfaces. Behaviour follows D3DX9:
//   - Width/Height D3DX_DEFAULT = the file's size rounded up to a power of two, 0 or
//     D3DX_DEFAULT_NONPOW2 = the file's size; MipLevels 0/D3DX_DEFAULT = full chain,
//     D3DX_FROM_FILE = the file's count; Format UNKNOWN = the file's format (a format the
//     device lacks becomes A8R8G8B8 or X8R8G8B8, as D3DXCheckTextureRequirements does).
//   - DDS data whose format, size and mip count already fit is uploaded as is (DXTn stays
//     compressed). Anything else is decoded to 32-bit, scaled, colour-keyed and mipmapped
//     with a box filter. Re-compressing to DXTn is not provided: a DXTn request that needs
//     conversion gets an uncompressed 32-bit texture instead (same look, more memory).
// Image decoding lives in d3dx9_image.cpp.
#include "d3dx9_image.h"
#include <d3dx9.h>
#include <algorithm>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <fstream>
#include <iterator>
#include <string>

#define STB_IMAGE_WRITE_IMPLEMENTATION
#define STBI_WRITE_NO_STDIO
#include "../vendor/stb/stb_image_write.h"

using namespace ran_d3dx;

namespace {

UINT Pow2(UINT v) { UINT p = 1; while (p < v) p <<= 1; return p; }

UINT ResolveSize(UINT requested, UINT file)
{
    if (requested == 0 || requested == D3DX_DEFAULT_NONPOW2 || requested == D3DX_FROM_FILE) return file;
    if (requested == D3DX_DEFAULT) return Pow2(file);
    return requested;
}

UINT FullChain(UINT w, UINT h)
{
    UINT n = 1;
    for (UINT s = std::max(w, h); s > 1; s >>= 1) ++n;
    return n;
}

UINT ResolveMips(UINT requested, UINT fileMips, UINT full)
{
    if (requested == D3DX_FROM_FILE) return std::min(fileMips, full);
    if (requested == 0 || requested == D3DX_DEFAULT) return full;
    return std::min(requested, full);
}

bool HasAlpha(D3DFORMAT f)
{
    switch (f) {
    case D3DFMT_A8R8G8B8: case D3DFMT_A8B8G8R8: case D3DFMT_A1R5G5B5: case D3DFMT_A4R4G4B4:
    case D3DFMT_A8L8: case D3DFMT_A8: case D3DFMT_DXT1: case D3DFMT_DXT2: case D3DFMT_DXT3:
    case D3DFMT_DXT4: case D3DFMT_DXT5: return true;
    default: return false;
    }
}

bool DeviceSupports(LPDIRECT3DDEVICE9 dev, D3DFORMAT f, DWORD usage, D3DRESOURCETYPE type)
{
    IDirect3D9* d3d = nullptr;
    if (FAILED(dev->GetDirect3D(&d3d)) || !d3d) return true;
    D3DDEVICE_CREATION_PARAMETERS cp = {};
    dev->GetCreationParameters(&cp);
    const HRESULT hr = d3d->CheckDeviceFormat(cp.AdapterOrdinal, cp.DeviceType, D3DFMT_X8R8G8B8,
                                              usage & (D3DUSAGE_RENDERTARGET | D3DUSAGE_DYNAMIC), type, f);
    d3d->Release();
    return SUCCEEDED(hr);
}

// The texture format D3DX would pick for this image and request.
D3DFORMAT ChooseFormat(LPDIRECT3DDEVICE9 dev, D3DFORMAT requested, const Image& img, DWORD usage, D3DRESOURCETYPE type)
{
    D3DFORMAT f = requested;
    if (f == D3DFMT_UNKNOWN || (UINT)f == D3DX_DEFAULT || (UINT)f == D3DX_FROM_FILE) f = img.info.Format;
    // Diagnostic switch (M1 white-texture investigation): upload DXTn files decoded to 32-bit.
    static const bool decodeDxt = std::getenv("RAN_DXT_DECODE") != nullptr;
    static const bool solid = std::getenv("RAN_TEX_SOLID") != nullptr;   // see FillFace
    if (solid || (decodeDxt && IsBlockCompressed(f))) return D3DFMT_A8R8G8B8;
    if (DeviceSupports(dev, f, usage, type)) return f;
    return HasAlpha(f) ? D3DFMT_A8R8G8B8 : D3DFMT_X8R8G8B8;
}

bool ReadFile(LPCSTR path, std::vector<BYTE>& out)
{
    if (!path) return false;
    std::ifstream in(ran_compat::ResolvePath(path), std::ios::binary);
    if (!in) return false;
    out.assign(std::istreambuf_iterator<char>(in), std::istreambuf_iterator<char>());
    return true;
}

void CopyRows(BYTE* dst, UINT dstPitch, const BYTE* src, UINT srcPitch, UINT rowBytes, UINT rows)
{
    for (UINT r = 0; r < rows; ++r) std::memcpy(dst + (size_t)r * dstPitch, src + (size_t)r * srcPitch, rowBytes);
}

// How one face of a texture is filled: either the file's own levels, or decoded pixels.
struct Plan
{
    D3DFORMAT format;
    UINT w, h, mips;
    bool direct;
};

Plan MakePlan(LPDIRECT3DDEVICE9 dev, const Image& img, UINT w, UINT h, UINT mipsReq, D3DFORMAT fmtReq, DWORD usage,
              D3DCOLOR key, D3DRESOURCETYPE type)
{
    Plan p;
    p.w = w;
    p.h = h;
    // Diagnostic switch (M1 white-texture investigation): load only the top mip level.
    static const bool noMips = std::getenv("RAN_TEX_NOMIPS") != nullptr;
    p.mips = (usage & D3DUSAGE_AUTOGENMIPMAP) || noMips ? 1 : ResolveMips(mipsReq, img.info.MipLevels, FullChain(w, h));
    p.format = ChooseFormat(dev, fmtReq, img, usage, type);
    p.direct = img.storage == p.format && img.info.Width == w && img.info.Height == h &&
               p.mips <= img.info.MipLevels && !key;
    if (!p.direct && !CanEncode(p.format)) p.format = (HasAlpha(p.format) || key) ? D3DFMT_A8R8G8B8 : D3DFMT_X8R8G8B8;
    return p;
}

// Fills mip levels [0, plan.mips) of one face through lock/unlock callbacks.
template <class Lock, class Unlock>
HRESULT FillFace(const Image& img, UINT face, const Plan& plan, D3DCOLOR key, Lock lock, Unlock unlock)
{
    if (plan.direct) {
        for (UINT l = 0; l < plan.mips; ++l) {
            const Image::Level& lv = img.At(face, l);
            D3DLOCKED_RECT lr;
            HRESULT hr = lock(l, &lr);
            if (FAILED(hr)) return hr;
            CopyRows((BYTE*)lr.pBits, (UINT)lr.Pitch, img.Data(face, l), lv.pitch, RowBytes(img.storage, lv.w), RowCount(img.storage, lv.h));
            unlock(l);
        }
        return D3D_OK;
    }
    Pixels px = Resize(img.DecodeLevel(face, 0), plan.w, plan.h);
    ApplyColorKey(px, key);
    // Diagnostic switch (M1 white-texture investigation): every loaded texture solid magenta.
    static const bool solid = std::getenv("RAN_TEX_SOLID") != nullptr;
    if (solid) std::fill(px.argb.begin(), px.argb.end(), 0xFFFF00FFu);
    for (UINT l = 0; l < plan.mips; ++l) {
        D3DLOCKED_RECT lr;
        HRESULT hr = lock(l, &lr);
        if (FAILED(hr)) return hr;
        Encode(plan.format, px.argb.data(), px.w, px.h, (BYTE*)lr.pBits, (UINT)lr.Pitch);
        unlock(l);
        if (l + 1 < plan.mips) px = HalfSize(px);
    }
    return D3D_OK;
}

HRESULT CreateTexture(LPDIRECT3DDEVICE9 dev, const Image& img, UINT width, UINT height, UINT mips, DWORD usage, D3DFORMAT format,
                      D3DPOOL pool, D3DCOLOR key, D3DXIMAGE_INFO* info, LPDIRECT3DTEXTURE9* out)
{
    if (img.faces != 1) return D3DXERR_INVALIDDATA;
    const Plan plan = MakePlan(dev, img, ResolveSize(width, img.info.Width), ResolveSize(height, img.info.Height), mips, format,
                               usage, key, D3DRTYPE_TEXTURE);
    // Default-pool textures cannot be locked: fill a system-memory copy and upload it.
    const bool staged = pool == D3DPOOL_DEFAULT && !(usage & D3DUSAGE_DYNAMIC);
    LPDIRECT3DTEXTURE9 tex = nullptr, fill = nullptr;
    HRESULT hr = dev->CreateTexture(plan.w, plan.h, (usage & D3DUSAGE_AUTOGENMIPMAP) ? 0 : plan.mips, usage, plan.format, pool, &tex, nullptr);
    if (FAILED(hr)) return hr;
    if (staged) {
        hr = dev->CreateTexture(plan.w, plan.h, plan.mips, 0, plan.format, D3DPOOL_SYSTEMMEM, &fill, nullptr);
        if (FAILED(hr)) { tex->Release(); return hr; }
    } else {
        fill = tex;
        fill->AddRef();
    }
    hr = FillFace(img, 0, plan, key,
                  [&](UINT l, D3DLOCKED_RECT* lr) { return fill->LockRect(l, lr, nullptr, 0); },
                  [&](UINT l) { fill->UnlockRect(l); });
    if (SUCCEEDED(hr) && staged) hr = dev->UpdateTexture(fill, tex);
    fill->Release();
    if (FAILED(hr)) { tex->Release(); return hr; }
    if (info) *info = img.info;
    *out = tex;
    return D3D_OK;
}

HRESULT CreateCubeTexture(LPDIRECT3DDEVICE9 dev, const Image& img, UINT size, UINT mips, DWORD usage, D3DFORMAT format,
                          D3DPOOL pool, D3DCOLOR key, D3DXIMAGE_INFO* info, LPDIRECT3DCUBETEXTURE9* out)
{
    if (img.faces != 6) return D3DXERR_INVALIDDATA;   // D3DX also needs a cube-map file
    const UINT edge = ResolveSize(size, img.info.Width);
    const Plan plan = MakePlan(dev, img, edge, edge, mips, format, usage, key, D3DRTYPE_CUBETEXTURE);
    const bool staged = pool == D3DPOOL_DEFAULT && !(usage & D3DUSAGE_DYNAMIC);
    LPDIRECT3DCUBETEXTURE9 tex = nullptr, fill = nullptr;
    HRESULT hr = dev->CreateCubeTexture(edge, (usage & D3DUSAGE_AUTOGENMIPMAP) ? 0 : plan.mips, usage, plan.format, pool, &tex, nullptr);
    if (FAILED(hr)) return hr;
    if (staged) {
        hr = dev->CreateCubeTexture(edge, plan.mips, 0, plan.format, D3DPOOL_SYSTEMMEM, &fill, nullptr);
        if (FAILED(hr)) { tex->Release(); return hr; }
    } else {
        fill = tex;
        fill->AddRef();
    }
    for (UINT face = 0; face < 6 && SUCCEEDED(hr); ++face) {
        const D3DCUBEMAP_FACES f = (D3DCUBEMAP_FACES)face;
        hr = FillFace(img, face, plan, key,
                      [&](UINT l, D3DLOCKED_RECT* lr) { return fill->LockRect(f, l, lr, nullptr, 0); },
                      [&](UINT l) { fill->UnlockRect(f, l); });
    }
    if (SUCCEEDED(hr) && staged) hr = dev->UpdateTexture(fill, tex);
    fill->Release();
    if (FAILED(hr)) { tex->Release(); return hr; }
    if (info) *info = img.info;
    *out = tex;
    return D3D_OK;
}

// Read access to any surface: lock it, or copy a render target / default-pool surface to
// system memory first.
struct ReadLock
{
    IDirect3DSurface9* surface = nullptr;   // the one that is locked (AddRef'd)
    D3DSURFACE_DESC desc = {};
    D3DLOCKED_RECT lr = {};
    bool locked = false;

    HRESULT Open(IDirect3DSurface9* src)
    {
        src->GetDesc(&desc);
        if (SUCCEEDED(src->LockRect(&lr, nullptr, D3DLOCK_READONLY))) {
            surface = src;
            surface->AddRef();
            locked = true;
            return D3D_OK;
        }
        IDirect3DDevice9* dev = nullptr;
        src->GetDevice(&dev);
        HRESULT hr = dev->CreateOffscreenPlainSurface(desc.Width, desc.Height, desc.Format, D3DPOOL_SYSTEMMEM, &surface, nullptr);
        if (SUCCEEDED(hr)) hr = dev->GetRenderTargetData(src, surface);
        dev->Release();
        if (FAILED(hr)) return hr;
        hr = surface->LockRect(&lr, nullptr, D3DLOCK_READONLY);
        locked = SUCCEEDED(hr);
        return hr;
    }
    ~ReadLock()
    {
        if (locked) surface->UnlockRect();
        if (surface) surface->Release();
    }
};

HRESULT ReadSurface(IDirect3DSurface9* src, const RECT* rect, Pixels& out, D3DSURFACE_DESC* descOut = nullptr)
{
    ReadLock rl;
    HRESULT hr = rl.Open(src);
    if (FAILED(hr)) return hr;
    if (descOut) *descOut = rl.desc;
    const RECT r = rect ? *rect : RECT{ 0, 0, (LONG)rl.desc.Width, (LONG)rl.desc.Height };
    if (!CanDecode(rl.desc.Format) || r.right <= r.left || r.bottom <= r.top) return D3DERR_INVALIDCALL;
    const D3DFORMAT f = rl.desc.Format;
    if (IsBlockCompressed(f) && ((r.left | r.top) & 3)) return D3DERR_INVALIDCALL;
    const BYTE* base = (const BYTE*)rl.lr.pBits;
    const size_t rowOffset = IsBlockCompressed(f) ? (size_t)(r.top / 4) * rl.lr.Pitch + (size_t)(r.left / 4) * (f == D3DFMT_DXT1 ? 8 : 16)
                                                  : (size_t)r.top * rl.lr.Pitch + (size_t)r.left * BytesPerPixel(f);
    out.w = (UINT)(r.right - r.left);
    out.h = (UINT)(r.bottom - r.top);
    out.argb.resize((size_t)out.w * out.h);
    Decode(f, base + rowOffset, (UINT)rl.lr.Pitch, out.w, out.h, out.argb.data());
    return D3D_OK;
}

// Whole surface -> file bytes in the requested container.
HRESULT EncodeFile(D3DXIMAGE_FILEFORMAT ff, IDirect3DSurface9* surface, const RECT* rect, std::vector<BYTE>& file)
{
    auto sink = [](void* ctx, void* data, int size) {
        auto* v = (std::vector<BYTE>*)ctx;
        v->insert(v->end(), (BYTE*)data, (BYTE*)data + size);
    };
    Pixels px;
    D3DSURFACE_DESC desc;
    HRESULT hr = ReadSurface(surface, rect, px, &desc);
    if (FAILED(hr)) return hr;
    std::vector<BYTE> rgba(px.argb.size() * 4);
    for (size_t i = 0; i < px.argb.size(); ++i) {
        const uint32_t v = px.argb[i];
        rgba[i * 4 + 0] = (BYTE)(v >> 16);
        rgba[i * 4 + 1] = (BYTE)(v >> 8);
        rgba[i * 4 + 2] = (BYTE)v;
        rgba[i * 4 + 3] = (BYTE)(v >> 24);
    }
    const int w = (int)px.w, h = (int)px.h;
    int ok = 0;
    switch (ff) {
    case D3DXIFF_BMP: case D3DXIFF_DIB: ok = stbi_write_bmp_to_func(sink, &file, w, h, 4, rgba.data()); break;
    case D3DXIFF_TGA: ok = stbi_write_tga_to_func(sink, &file, w, h, 4, rgba.data()); break;
    case D3DXIFF_PNG: ok = stbi_write_png_to_func(sink, &file, w, h, 4, rgba.data(), w * 4); break;
    case D3DXIFF_JPG: ok = stbi_write_jpg_to_func(sink, &file, w, h, 4, rgba.data(), 90); break;
    default: return D3DERR_INVALIDCALL;
    }
    return ok ? D3D_OK : E_FAIL;
}

void Put32(std::vector<BYTE>& v, uint32_t x) { for (int i = 0; i < 4; ++i) v.push_back((BYTE)(x >> (8 * i))); }

// DDS header for an uncompressed 32-bit or DXTn texture (the formats the client saves).
bool DdsHeader(D3DFORMAT f, UINT w, UINT h, UINT mips, bool cube, std::vector<BYTE>& out)
{
    uint32_t pfFlags = 0, fourcc = 0, bits = 0, rm = 0, gm = 0, bm = 0, am = 0;
    switch (f) {
    case D3DFMT_DXT1: fourcc = MAKEFOURCC('D', 'X', 'T', '1'); break;
    case D3DFMT_DXT2: fourcc = MAKEFOURCC('D', 'X', 'T', '2'); break;
    case D3DFMT_DXT3: fourcc = MAKEFOURCC('D', 'X', 'T', '3'); break;
    case D3DFMT_DXT4: fourcc = MAKEFOURCC('D', 'X', 'T', '4'); break;
    case D3DFMT_DXT5: fourcc = MAKEFOURCC('D', 'X', 'T', '5'); break;
    case D3DFMT_A8R8G8B8: pfFlags = 0x41; bits = 32; rm = 0xFF0000; gm = 0xFF00; bm = 0xFF; am = 0xFF000000; break;
    case D3DFMT_X8R8G8B8: pfFlags = 0x40; bits = 32; rm = 0xFF0000; gm = 0xFF00; bm = 0xFF; break;
    case D3DFMT_R5G6B5:   pfFlags = 0x40; bits = 16; rm = 0xF800; gm = 0x7E0; bm = 0x1F; break;
    case D3DFMT_A1R5G5B5: pfFlags = 0x41; bits = 16; rm = 0x7C00; gm = 0x3E0; bm = 0x1F; am = 0x8000; break;
    case D3DFMT_A4R4G4B4: pfFlags = 0x41; bits = 16; rm = 0xF00; gm = 0xF0; bm = 0xF; am = 0xF000; break;
    default: return false;
    }
    if (fourcc) pfFlags = 0x4;
    const bool compressed = IsBlockCompressed(f);
    out.insert(out.end(), { 'D', 'D', 'S', ' ' });
    Put32(out, 124);
    Put32(out, 0x1 | 0x2 | 0x4 | 0x1000 | (mips > 1 ? 0x20000 : 0) | (compressed ? 0x80000 : 0x8));
    Put32(out, h);
    Put32(out, w);
    Put32(out, compressed ? RowBytes(f, w) * RowCount(f, h) : RowBytes(f, w));
    Put32(out, 0);
    Put32(out, mips);
    for (int i = 0; i < 11; ++i) Put32(out, 0);
    Put32(out, 32);
    Put32(out, pfFlags);
    Put32(out, fourcc);
    Put32(out, bits);
    Put32(out, rm); Put32(out, gm); Put32(out, bm); Put32(out, am);
    Put32(out, 0x1000 | (mips > 1 ? 0x400008 : 0) | (cube ? 0x8 : 0));
    Put32(out, cube ? 0xFE00 : 0);
    Put32(out, 0); Put32(out, 0); Put32(out, 0);
    return true;
}

void AppendLevel(IDirect3DSurface9* s, D3DFORMAT f, std::vector<BYTE>& out)
{
    ReadLock rl;
    if (FAILED(rl.Open(s))) return;
    const UINT row = RowBytes(f, rl.desc.Width), rows = RowCount(f, rl.desc.Height);
    for (UINT r = 0; r < rows; ++r) {
        const BYTE* p = (const BYTE*)rl.lr.pBits + (size_t)r * rl.lr.Pitch;
        out.insert(out.end(), p, p + row);
    }
}

HRESULT WriteFile(LPCSTR path, const std::vector<BYTE>& bytes)
{
    if (!path) return D3DERR_INVALIDCALL;
    std::FILE* f = ran_compat::fopen_resolved(path, "wb");
    if (!f) return D3DERR_INVALIDCALL;
    const bool ok = std::fwrite(bytes.data(), 1, bytes.size(), f) == bytes.size();
    std::fclose(f);
    return ok ? D3D_OK : E_FAIL;
}

} // namespace

extern "C" {

HRESULT WINAPI D3DXCreateTextureFromFileInMemoryEx(LPDIRECT3DDEVICE9 dev, LPCVOID data, UINT size, UINT width, UINT height,
                                                   UINT mips, DWORD usage, D3DFORMAT format, D3DPOOL pool, DWORD, DWORD,
                                                   D3DCOLOR key, D3DXIMAGE_INFO* info, PALETTEENTRY*, LPDIRECT3DTEXTURE9* out)
{
    if (out) *out = nullptr;
    if (!dev || !data || !size || !out) return D3DERR_INVALIDCALL;
    Image img;
    HRESULT hr = LoadImage(data, size, img);
    if (FAILED(hr)) return hr;
    return CreateTexture(dev, img, width, height, mips, usage, format, pool, key, info, out);
}

HRESULT WINAPI D3DXCreateTextureFromFileExA(LPDIRECT3DDEVICE9 dev, LPCSTR file, UINT width, UINT height, UINT mips, DWORD usage,
                                            D3DFORMAT format, D3DPOOL pool, DWORD filter, DWORD mipFilter, D3DCOLOR key,
                                            D3DXIMAGE_INFO* info, PALETTEENTRY* pal, LPDIRECT3DTEXTURE9* out)
{
    if (out) *out = nullptr;
    std::vector<BYTE> bytes;
    if (!ReadFile(file, bytes)) return D3DXERR_INVALIDDATA;
    return D3DXCreateTextureFromFileInMemoryEx(dev, bytes.data(), (UINT)bytes.size(), width, height, mips, usage, format, pool,
                                               filter, mipFilter, key, info, pal, out);
}

HRESULT WINAPI D3DXCreateCubeTextureFromFileInMemoryEx(LPDIRECT3DDEVICE9 dev, LPCVOID data, UINT dataSize, UINT size, UINT mips,
                                                       DWORD usage, D3DFORMAT format, D3DPOOL pool, DWORD, DWORD, D3DCOLOR key,
                                                       D3DXIMAGE_INFO* info, PALETTEENTRY*, LPDIRECT3DCUBETEXTURE9* out)
{
    if (out) *out = nullptr;
    if (!dev || !data || !dataSize || !out) return D3DERR_INVALIDCALL;
    Image img;
    HRESULT hr = LoadImage(data, dataSize, img);
    if (FAILED(hr)) return hr;
    return CreateCubeTexture(dev, img, size, mips, usage, format, pool, key, info, out);
}

HRESULT WINAPI D3DXCreateCubeTextureFromFileExA(LPDIRECT3DDEVICE9 dev, LPCSTR file, UINT size, UINT mips, DWORD usage,
                                                D3DFORMAT format, D3DPOOL pool, DWORD filter, DWORD mipFilter, D3DCOLOR key,
                                                D3DXIMAGE_INFO* info, PALETTEENTRY* pal, LPDIRECT3DCUBETEXTURE9* out)
{
    if (out) *out = nullptr;
    std::vector<BYTE> bytes;
    if (!ReadFile(file, bytes)) return D3DXERR_INVALIDDATA;
    return D3DXCreateCubeTextureFromFileInMemoryEx(dev, bytes.data(), (UINT)bytes.size(), size, mips, usage, format, pool, filter,
                                                   mipFilter, key, info, pal, out);
}

HRESULT WINAPI D3DXLoadSurfaceFromSurface(LPDIRECT3DSURFACE9 dst, CONST PALETTEENTRY*, CONST RECT* dstRect, LPDIRECT3DSURFACE9 src,
                                          CONST PALETTEENTRY*, CONST RECT* srcRect, DWORD, D3DCOLOR key)
{
    if (!dst || !src) return D3DERR_INVALIDCALL;
    Pixels px;
    HRESULT hr = ReadSurface(src, srcRect, px);
    if (FAILED(hr)) return hr;
    D3DSURFACE_DESC dd;
    dst->GetDesc(&dd);
    const RECT r = dstRect ? *dstRect : RECT{ 0, 0, (LONG)dd.Width, (LONG)dd.Height };
    if (r.right <= r.left || r.bottom <= r.top) return D3DERR_INVALIDCALL;
    if (!CanEncode(dd.Format)) {
        std::fprintf(stderr, "[d3dx] D3DXLoadSurfaceFromSurface: no encoder for format %u\n", (unsigned)dd.Format);
        return E_NOTIMPL;
    }
    px = Resize(px, (UINT)(r.right - r.left), (UINT)(r.bottom - r.top));
    ApplyColorKey(px, key);

    // Write into the destination, through a system-memory copy when it cannot be locked.
    D3DLOCKED_RECT lr;
    if (SUCCEEDED(dst->LockRect(&lr, &r, 0))) {
        Encode(dd.Format, px.argb.data(), px.w, px.h, (BYTE*)lr.pBits, (UINT)lr.Pitch);
        dst->UnlockRect();
        return D3D_OK;
    }
    IDirect3DDevice9* dev = nullptr;
    dst->GetDevice(&dev);
    IDirect3DSurface9* tmp = nullptr;
    hr = dev->CreateOffscreenPlainSurface(px.w, px.h, dd.Format, D3DPOOL_SYSTEMMEM, &tmp, nullptr);
    if (SUCCEEDED(hr)) hr = tmp->LockRect(&lr, nullptr, 0);
    if (SUCCEEDED(hr)) {
        Encode(dd.Format, px.argb.data(), px.w, px.h, (BYTE*)lr.pBits, (UINT)lr.Pitch);
        tmp->UnlockRect();
        const POINT at = { r.left, r.top };
        hr = dev->UpdateSurface(tmp, nullptr, dst, &at);
    }
    if (tmp) tmp->Release();
    dev->Release();
    return hr;
}

HRESULT WINAPI D3DXSaveSurfaceToFileA(LPCSTR file, D3DXIMAGE_FILEFORMAT ff, LPDIRECT3DSURFACE9 surface, CONST PALETTEENTRY*,
                                      CONST RECT* rect)
{
    if (!file || !surface) return D3DERR_INVALIDCALL;
    std::vector<BYTE> bytes;
    if (ff == D3DXIFF_DDS) {
        D3DSURFACE_DESC d;
        surface->GetDesc(&d);
        if (rect || !DdsHeader(d.Format, d.Width, d.Height, 1, false, bytes)) return D3DERR_INVALIDCALL;
        AppendLevel(surface, d.Format, bytes);
    } else {
        HRESULT hr = EncodeFile(ff, surface, rect, bytes);
        if (FAILED(hr)) return hr;
    }
    return WriteFile(file, bytes);
}

HRESULT WINAPI D3DXSaveTextureToFileA(LPCSTR file, D3DXIMAGE_FILEFORMAT ff, LPDIRECT3DBASETEXTURE9 base, CONST PALETTEENTRY*)
{
    if (!file || !base) return D3DERR_INVALIDCALL;
    std::vector<BYTE> bytes;
    const D3DRESOURCETYPE type = base->GetType();
    if (type == D3DRTYPE_TEXTURE) {
        IDirect3DTexture9* tex = (IDirect3DTexture9*)base;
        if (ff != D3DXIFF_DDS) {
            IDirect3DSurface9* top = nullptr;
            HRESULT hr = tex->GetSurfaceLevel(0, &top);
            if (FAILED(hr)) return hr;
            hr = EncodeFile(ff, top, nullptr, bytes);
            top->Release();
            if (FAILED(hr)) return hr;
            return WriteFile(file, bytes);
        }
        D3DSURFACE_DESC d;
        tex->GetLevelDesc(0, &d);
        const UINT levels = tex->GetLevelCount();
        if (!DdsHeader(d.Format, d.Width, d.Height, levels, false, bytes)) return D3DERR_INVALIDCALL;
        for (UINT l = 0; l < levels; ++l) {
            IDirect3DSurface9* s = nullptr;
            if (FAILED(tex->GetSurfaceLevel(l, &s))) return D3DERR_INVALIDCALL;
            AppendLevel(s, d.Format, bytes);
            s->Release();
        }
        return WriteFile(file, bytes);
    }
    if (type == D3DRTYPE_CUBETEXTURE && ff == D3DXIFF_DDS) {
        IDirect3DCubeTexture9* cube = (IDirect3DCubeTexture9*)base;
        D3DSURFACE_DESC d;
        cube->GetLevelDesc(0, &d);
        const UINT levels = cube->GetLevelCount();
        if (!DdsHeader(d.Format, d.Width, d.Height, levels, true, bytes)) return D3DERR_INVALIDCALL;
        for (UINT face = 0; face < 6; ++face)
            for (UINT l = 0; l < levels; ++l) {
                IDirect3DSurface9* s = nullptr;
                if (FAILED(cube->GetCubeMapSurface((D3DCUBEMAP_FACES)face, l, &s))) return D3DERR_INVALIDCALL;
                AppendLevel(s, d.Format, bytes);
                s->Release();
            }
        return WriteFile(file, bytes);
    }
    return D3DERR_INVALIDCALL;
}

} // extern "C"
