// Image decoding and pixel-format conversion for the native D3DX (see d3dx9_image.h).
#include "d3dx9_image.h"
#include <algorithm>
#include <cstring>

#define STB_IMAGE_IMPLEMENTATION
#define STBI_NO_STDIO
#define STBI_ONLY_JPEG
#define STBI_ONLY_PNG
#define STBI_ONLY_BMP
#define STBI_ONLY_TGA
#include "../vendor/stb/stb_image.h"

namespace ran_d3dx {
namespace {

inline uint32_t Argb(uint32_t a, uint32_t r, uint32_t g, uint32_t b) { return (a << 24) | (r << 16) | (g << 8) | b; }
inline uint32_t A(uint32_t p) { return p >> 24; }
inline uint32_t R(uint32_t p) { return (p >> 16) & 0xFF; }
inline uint32_t G(uint32_t p) { return (p >> 8) & 0xFF; }
inline uint32_t B(uint32_t p) { return p & 0xFF; }
inline uint32_t Expand5(uint32_t v) { return (v << 3) | (v >> 2); }
inline uint32_t Expand6(uint32_t v) { return (v << 2) | (v >> 4); }
inline uint32_t Expand4(uint32_t v) { return (v << 4) | v; }
inline uint32_t Expand1(uint32_t v) { return v ? 0xFF : 0; }
inline uint32_t Round(uint32_t v8, int bits) { return (v8 * ((1u << bits) - 1) + 127) / 255; }

inline uint16_t Rd16(const BYTE* p) { return (uint16_t)(p[0] | (p[1] << 8)); }
inline uint32_t Rd32(const BYTE* p) { return (uint32_t)p[0] | ((uint32_t)p[1] << 8) | ((uint32_t)p[2] << 16) | ((uint32_t)p[3] << 24); }
inline void Wr16(BYTE* p, uint32_t v) { p[0] = (BYTE)v; p[1] = (BYTE)(v >> 8); }
inline void Wr32(BYTE* p, uint32_t v) { p[0] = (BYTE)v; p[1] = (BYTE)(v >> 8); p[2] = (BYTE)(v >> 16); p[3] = (BYTE)(v >> 24); }

uint32_t From565(uint16_t c) { return Argb(0xFF, Expand5(c >> 11), Expand6((c >> 5) & 63), Expand5(c & 31)); }

// One 4x4 colour block (8 bytes). DXT1 blocks with c0 <= c1 have three colours + transparent.
void DecodeColorBlock(const BYTE* blk, bool dxt1, uint32_t out[16])
{
    const uint16_t c0 = Rd16(blk), c1 = Rd16(blk + 2);
    uint32_t pal[4];
    pal[0] = From565(c0);
    pal[1] = From565(c1);
    if (!dxt1 || c0 > c1) {
        pal[2] = Argb(0xFF, (2 * R(pal[0]) + R(pal[1]) + 1) / 3, (2 * G(pal[0]) + G(pal[1]) + 1) / 3, (2 * B(pal[0]) + B(pal[1]) + 1) / 3);
        pal[3] = Argb(0xFF, (R(pal[0]) + 2 * R(pal[1]) + 1) / 3, (G(pal[0]) + 2 * G(pal[1]) + 1) / 3, (B(pal[0]) + 2 * B(pal[1]) + 1) / 3);
    } else {
        pal[2] = Argb(0xFF, (R(pal[0]) + R(pal[1])) / 2, (G(pal[0]) + G(pal[1])) / 2, (B(pal[0]) + B(pal[1])) / 2);
        pal[3] = 0;
    }
    const uint32_t idx = Rd32(blk + 4);
    for (int i = 0; i < 16; ++i) out[i] = pal[(idx >> (2 * i)) & 3];
}

void DecodeBlock(D3DFORMAT f, const BYTE* blk, uint32_t out[16])
{
    if (f == D3DFMT_DXT1) { DecodeColorBlock(blk, true, out); return; }
    DecodeColorBlock(blk + 8, false, out);
    if (f == D3DFMT_DXT2 || f == D3DFMT_DXT3) {
        for (int i = 0; i < 16; ++i) {
            const uint32_t a = (blk[i / 2] >> ((i & 1) * 4)) & 0xF;
            out[i] = (out[i] & 0xFFFFFF) | (Expand4(a) << 24);
        }
        return;
    }
    // DXT4/DXT5: two endpoints + 3-bit indices.
    uint32_t a[8];
    a[0] = blk[0];
    a[1] = blk[1];
    if (a[0] > a[1]) {
        for (int i = 1; i < 7; ++i) a[i + 1] = ((7 - i) * a[0] + i * a[1] + 3) / 7;
    } else {
        for (int i = 1; i < 5; ++i) a[i + 1] = ((5 - i) * a[0] + i * a[1] + 2) / 5;
        a[6] = 0;
        a[7] = 255;
    }
    uint64_t bits = 0;
    for (int i = 0; i < 6; ++i) bits |= (uint64_t)blk[2 + i] << (8 * i);
    for (int i = 0; i < 16; ++i) out[i] = (out[i] & 0xFFFFFF) | (a[(bits >> (3 * i)) & 7] << 24);
}

uint32_t DecodePixel(D3DFORMAT f, const BYTE* p)
{
    switch (f) {
    case D3DFMT_A8R8G8B8: return Rd32(p);
    case D3DFMT_X8R8G8B8: return Rd32(p) | 0xFF000000u;
    case D3DFMT_A8B8G8R8: { const uint32_t v = Rd32(p); return Argb(A(v), B(v), G(v), R(v)); }
    case D3DFMT_X8B8G8R8: { const uint32_t v = Rd32(p); return Argb(0xFF, B(v), G(v), R(v)); }
    case D3DFMT_R8G8B8:   return Argb(0xFF, p[2], p[1], p[0]);
    case D3DFMT_R5G6B5:   return From565(Rd16(p));
    case D3DFMT_X1R5G5B5: { const uint32_t v = Rd16(p); return Argb(0xFF, Expand5((v >> 10) & 31), Expand5((v >> 5) & 31), Expand5(v & 31)); }
    case D3DFMT_A1R5G5B5: { const uint32_t v = Rd16(p); return Argb(Expand1(v >> 15), Expand5((v >> 10) & 31), Expand5((v >> 5) & 31), Expand5(v & 31)); }
    case D3DFMT_A4R4G4B4: { const uint32_t v = Rd16(p); return Argb(Expand4(v >> 12), Expand4((v >> 8) & 15), Expand4((v >> 4) & 15), Expand4(v & 15)); }
    case D3DFMT_X4R4G4B4: { const uint32_t v = Rd16(p); return Argb(0xFF, Expand4((v >> 8) & 15), Expand4((v >> 4) & 15), Expand4(v & 15)); }
    case D3DFMT_L8:       return Argb(0xFF, p[0], p[0], p[0]);
    case D3DFMT_A8L8:     return Argb(p[1], p[0], p[0], p[0]);
    case D3DFMT_A8:       return Argb(p[0], 0, 0, 0);
    default:              return 0;
    }
}

void EncodePixel(D3DFORMAT f, uint32_t v, BYTE* p)
{
    switch (f) {
    case D3DFMT_A8R8G8B8: Wr32(p, v); break;
    case D3DFMT_X8R8G8B8: Wr32(p, v | 0xFF000000u); break;
    case D3DFMT_A8B8G8R8: Wr32(p, Argb(A(v), B(v), G(v), R(v))); break;
    case D3DFMT_X8B8G8R8: Wr32(p, Argb(0xFF, B(v), G(v), R(v))); break;
    case D3DFMT_R8G8B8:   p[0] = (BYTE)B(v); p[1] = (BYTE)G(v); p[2] = (BYTE)R(v); break;
    case D3DFMT_R5G6B5:   Wr16(p, (Round(R(v), 5) << 11) | (Round(G(v), 6) << 5) | Round(B(v), 5)); break;
    case D3DFMT_X1R5G5B5: Wr16(p, 0x8000 | (Round(R(v), 5) << 10) | (Round(G(v), 5) << 5) | Round(B(v), 5)); break;
    case D3DFMT_A1R5G5B5: Wr16(p, ((A(v) >= 128) << 15) | (Round(R(v), 5) << 10) | (Round(G(v), 5) << 5) | Round(B(v), 5)); break;
    case D3DFMT_A4R4G4B4: Wr16(p, (Round(A(v), 4) << 12) | (Round(R(v), 4) << 8) | (Round(G(v), 4) << 4) | Round(B(v), 4)); break;
    case D3DFMT_X4R4G4B4: Wr16(p, 0xF000 | (Round(R(v), 4) << 8) | (Round(G(v), 4) << 4) | Round(B(v), 4)); break;
    case D3DFMT_L8:       p[0] = (BYTE)((R(v) * 77 + G(v) * 150 + B(v) * 29 + 128) >> 8); break;
    case D3DFMT_A8L8:     p[0] = (BYTE)((R(v) * 77 + G(v) * 150 + B(v) * 29 + 128) >> 8); p[1] = (BYTE)A(v); break;
    case D3DFMT_A8:       p[0] = (BYTE)A(v); break;
    default: break;
    }
}

// --- DDS --------------------------------------------------------------------------------------
constexpr uint32_t DDSD_MIPMAPCOUNT = 0x20000;
constexpr uint32_t DDPF_ALPHAPIXELS = 0x1, DDPF_ALPHA = 0x2, DDPF_FOURCC = 0x4, DDPF_RGB = 0x40, DDPF_LUMINANCE = 0x20000;
constexpr uint32_t DDSCAPS2_CUBEMAP = 0x200, DDSCAPS2_VOLUME = 0x200000;

D3DFORMAT DdsFormat(const BYTE* pf)
{
    const uint32_t flags = Rd32(pf + 4), fourcc = Rd32(pf + 8), bits = Rd32(pf + 12);
    const uint32_t rm = Rd32(pf + 16), gm = Rd32(pf + 20), bm = Rd32(pf + 24), am = Rd32(pf + 28);
    const uint32_t alpha = (flags & DDPF_ALPHAPIXELS) ? am : 0;
    if (flags & DDPF_FOURCC) {
        switch (fourcc) {
        case MAKEFOURCC('D', 'X', 'T', '1'): return D3DFMT_DXT1;
        case MAKEFOURCC('D', 'X', 'T', '2'): return D3DFMT_DXT2;
        case MAKEFOURCC('D', 'X', 'T', '3'): return D3DFMT_DXT3;
        case MAKEFOURCC('D', 'X', 'T', '4'): return D3DFMT_DXT4;
        case MAKEFOURCC('D', 'X', 'T', '5'): return D3DFMT_DXT5;
        default: return D3DFMT_UNKNOWN;
        }
    }
    if (flags & DDPF_RGB) {
        if (bits == 32 && rm == 0xFF0000 && gm == 0xFF00 && bm == 0xFF) return alpha ? D3DFMT_A8R8G8B8 : D3DFMT_X8R8G8B8;
        if (bits == 32 && rm == 0xFF && gm == 0xFF00 && bm == 0xFF0000) return alpha ? D3DFMT_A8B8G8R8 : D3DFMT_X8B8G8R8;
        if (bits == 24 && rm == 0xFF0000 && gm == 0xFF00 && bm == 0xFF) return D3DFMT_R8G8B8;
        if (bits == 16 && rm == 0xF800 && gm == 0x7E0 && bm == 0x1F) return D3DFMT_R5G6B5;
        if (bits == 16 && rm == 0x7C00 && gm == 0x3E0 && bm == 0x1F) return alpha == 0x8000 ? D3DFMT_A1R5G5B5 : D3DFMT_X1R5G5B5;
        if (bits == 16 && rm == 0xF00 && gm == 0xF0 && bm == 0xF) return alpha == 0xF000 ? D3DFMT_A4R4G4B4 : D3DFMT_X4R4G4B4;
        return D3DFMT_UNKNOWN;
    }
    if (flags & DDPF_LUMINANCE) {
        if (bits == 8) return D3DFMT_L8;
        if (bits == 16 && alpha == 0xFF00) return D3DFMT_A8L8;
        return D3DFMT_UNKNOWN;
    }
    if ((flags & DDPF_ALPHA) && bits == 8) return D3DFMT_A8;
    return D3DFMT_UNKNOWN;
}

HRESULT LoadDds(const BYTE* data, size_t size, Image& img)
{
    if (size < 128 || Rd32(data + 4) != 124) return D3DXERR_INVALIDDATA;
    const BYTE* h = data + 4;
    const uint32_t flags = Rd32(h + 4), height = Rd32(h + 8), width = Rd32(h + 12);
    const uint32_t mipCount = (flags & DDSD_MIPMAPCOUNT) ? std::max<uint32_t>(1, Rd32(h + 24)) : 1;
    const uint32_t caps2 = Rd32(h + 108);
    if (caps2 & DDSCAPS2_VOLUME) return D3DXERR_INVALIDDATA;   // volume textures: not used by the client
    const D3DFORMAT fmt = DdsFormat(h + 72);
    if (fmt == D3DFMT_UNKNOWN || !width || !height) return D3DXERR_INVALIDDATA;

    img.faces = (caps2 & DDSCAPS2_CUBEMAP) ? 6 : 1;
    img.storage = fmt;
    img.info.Width = width;
    img.info.Height = height;
    img.info.Depth = 1;
    img.info.MipLevels = mipCount;
    img.info.Format = fmt;
    img.info.ResourceType = img.faces == 6 ? D3DRTYPE_CUBETEXTURE : D3DRTYPE_TEXTURE;
    img.info.ImageFileFormat = D3DXIFF_DDS;

    size_t offset = 0;
    const size_t payload = size - 128;
    img.levels.clear();
    for (UINT face = 0; face < img.faces; ++face) {
        UINT w = width, hh = height;
        for (UINT l = 0; l < mipCount; ++l) {
            Image::Level lv;
            lv.offset = offset;
            lv.w = w;
            lv.h = hh;
            lv.pitch = RowBytes(fmt, w);
            offset += (size_t)lv.pitch * RowCount(fmt, hh);
            if (offset > payload) return D3DXERR_INVALIDDATA;
            img.levels.push_back(lv);
            w = std::max<UINT>(1, w / 2);
            hh = std::max<UINT>(1, hh / 2);
        }
    }
    img.bytes.assign(data + 128, data + 128 + offset);
    return D3D_OK;
}

} // namespace

bool IsBlockCompressed(D3DFORMAT f)
{
    return f == D3DFMT_DXT1 || f == D3DFMT_DXT2 || f == D3DFMT_DXT3 || f == D3DFMT_DXT4 || f == D3DFMT_DXT5;
}

UINT BytesPerPixel(D3DFORMAT f)
{
    switch (f) {
    case D3DFMT_A8R8G8B8: case D3DFMT_X8R8G8B8: case D3DFMT_A8B8G8R8: case D3DFMT_X8B8G8R8: return 4;
    case D3DFMT_R8G8B8: return 3;
    case D3DFMT_R5G6B5: case D3DFMT_X1R5G5B5: case D3DFMT_A1R5G5B5: case D3DFMT_A4R4G4B4:
    case D3DFMT_X4R4G4B4: case D3DFMT_A8L8: return 2;
    case D3DFMT_L8: case D3DFMT_A8: return 1;
    default: return 0;
    }
}

UINT RowBytes(D3DFORMAT f, UINT width)
{
    if (IsBlockCompressed(f)) return std::max<UINT>(1, (width + 3) / 4) * (f == D3DFMT_DXT1 ? 8 : 16);
    return width * BytesPerPixel(f);
}

UINT RowCount(D3DFORMAT f, UINT height) { return IsBlockCompressed(f) ? std::max<UINT>(1, (height + 3) / 4) : height; }

bool CanDecode(D3DFORMAT f) { return IsBlockCompressed(f) || BytesPerPixel(f) != 0; }
bool CanEncode(D3DFORMAT f) { return BytesPerPixel(f) != 0; }

bool Decode(D3DFORMAT f, const BYTE* src, UINT pitch, UINT w, UINT h, uint32_t* out)
{
    if (IsBlockCompressed(f)) {
        const UINT blockBytes = f == D3DFMT_DXT1 ? 8 : 16;
        uint32_t blk[16];
        for (UINT by = 0; by < (h + 3) / 4; ++by) {
            const BYTE* row = src + (size_t)by * pitch;
            for (UINT bx = 0; bx < (w + 3) / 4; ++bx) {
                DecodeBlock(f, row + (size_t)bx * blockBytes, blk);
                for (UINT y = 0; y < 4; ++y)
                    for (UINT x = 0; x < 4; ++x) {
                        const UINT px = bx * 4 + x, py = by * 4 + y;
                        if (px < w && py < h) out[(size_t)py * w + px] = blk[y * 4 + x];
                    }
            }
        }
        return true;
    }
    const UINT bpp = BytesPerPixel(f);
    if (!bpp) return false;
    for (UINT y = 0; y < h; ++y) {
        const BYTE* row = src + (size_t)y * pitch;
        for (UINT x = 0; x < w; ++x) out[(size_t)y * w + x] = DecodePixel(f, row + (size_t)x * bpp);
    }
    return true;
}

bool Encode(D3DFORMAT f, const uint32_t* in, UINT w, UINT h, BYTE* dst, UINT pitch)
{
    const UINT bpp = BytesPerPixel(f);
    if (!bpp) return false;
    for (UINT y = 0; y < h; ++y) {
        BYTE* row = dst + (size_t)y * pitch;
        for (UINT x = 0; x < w; ++x) EncodePixel(f, in[(size_t)y * w + x], row + (size_t)x * bpp);
    }
    return true;
}

Pixels Resize(const Pixels& src, UINT w, UINT h)
{
    if (src.w == w && src.h == h) return src;
    Pixels out;
    out.w = w;
    out.h = h;
    out.argb.resize((size_t)w * h);
    if (!src.w || !src.h) return out;
    if (w <= src.w && h <= src.h) {
        // Shrinking: average every source pixel that falls into the destination pixel.
        for (UINT y = 0; y < h; ++y) {
            const UINT y0 = (UINT)((uint64_t)y * src.h / h), y1 = std::max<UINT>(y0 + 1, (UINT)((uint64_t)(y + 1) * src.h / h));
            for (UINT x = 0; x < w; ++x) {
                const UINT x0 = (UINT)((uint64_t)x * src.w / w), x1 = std::max<UINT>(x0 + 1, (UINT)((uint64_t)(x + 1) * src.w / w));
                uint32_t sa = 0, sr = 0, sg = 0, sb = 0, n = 0;
                for (UINT sy = y0; sy < y1; ++sy)
                    for (UINT sx = x0; sx < x1; ++sx) {
                        const uint32_t p = src.argb[(size_t)sy * src.w + sx];
                        sa += A(p); sr += R(p); sg += G(p); sb += B(p); ++n;
                    }
                out.argb[(size_t)y * w + x] = Argb((sa + n / 2) / n, (sr + n / 2) / n, (sg + n / 2) / n, (sb + n / 2) / n);
            }
        }
        return out;
    }
    // Growing (either axis): bilinear on pixel centres, edges clamped.
    for (UINT y = 0; y < h; ++y) {
        float fy = ((float)y + 0.5f) * (float)src.h / (float)h - 0.5f;
        fy = std::max(0.0f, std::min(fy, (float)(src.h - 1)));
        const UINT y0 = (UINT)fy, y1 = std::min(y0 + 1, src.h - 1);
        const float ty = fy - (float)y0;
        for (UINT x = 0; x < w; ++x) {
            float fx = ((float)x + 0.5f) * (float)src.w / (float)w - 0.5f;
            fx = std::max(0.0f, std::min(fx, (float)(src.w - 1)));
            const UINT x0 = (UINT)fx, x1 = std::min(x0 + 1, src.w - 1);
            const float tx = fx - (float)x0;
            const uint32_t p00 = src.argb[(size_t)y0 * src.w + x0], p10 = src.argb[(size_t)y0 * src.w + x1];
            const uint32_t p01 = src.argb[(size_t)y1 * src.w + x0], p11 = src.argb[(size_t)y1 * src.w + x1];
            uint32_t c[4];
            for (int k = 0; k < 4; ++k) {
                const int s = 24 - 8 * k;
                const float a = (float)((p00 >> s) & 0xFF) * (1 - tx) + (float)((p10 >> s) & 0xFF) * tx;
                const float b = (float)((p01 >> s) & 0xFF) * (1 - tx) + (float)((p11 >> s) & 0xFF) * tx;
                c[k] = (uint32_t)(a * (1 - ty) + b * ty + 0.5f);
            }
            out.argb[(size_t)y * w + x] = Argb(c[0], c[1], c[2], c[3]);
        }
    }
    return out;
}

Pixels HalfSize(const Pixels& src) { return Resize(src, std::max<UINT>(1, src.w / 2), std::max<UINT>(1, src.h / 2)); }

void ApplyColorKey(Pixels& p, D3DCOLOR key)
{
    if (!key) return;
    for (uint32_t& v : p.argb) if (v == key) v = 0;
}

Pixels Image::DecodeLevel(UINT face, UINT level) const
{
    const Level& lv = At(face, level);
    Pixels p;
    p.w = lv.w;
    p.h = lv.h;
    p.argb.resize((size_t)lv.w * lv.h);
    Decode(storage, Data(face, level), lv.pitch, lv.w, lv.h, p.argb.data());
    return p;
}

HRESULT LoadImage(const void* data, size_t size, Image& out)
{
    out = Image();
    const BYTE* b = (const BYTE*)data;
    if (!b || size < 4) return D3DXERR_INVALIDDATA;
    if (std::memcmp(b, "DDS ", 4) == 0) return LoadDds(b, size, out);
    if (std::memcmp(b, "GIF", 3) == 0) return D3DXERR_INVALIDDATA;   // D3DX9 has no GIF reader

    int w = 0, h = 0, comp = 0;
    if (!stbi_info_from_memory(b, (int)size, &w, &h, &comp)) return D3DXERR_INVALIDDATA;
    stbi_uc* rgba = stbi_load_from_memory(b, (int)size, &w, &h, &comp, 4);
    if (!rgba) return D3DXERR_INVALIDDATA;

    D3DXIMAGE_FILEFORMAT ff = D3DXIFF_TGA;
    if (b[0] == 'B' && b[1] == 'M') ff = D3DXIFF_BMP;
    else if (b[0] == 0xFF && b[1] == 0xD8) ff = D3DXIFF_JPG;
    else if (b[0] == 0x89 && b[1] == 'P' && b[2] == 'N' && b[3] == 'G') ff = D3DXIFF_PNG;

    D3DFORMAT fileFormat = D3DFMT_A8R8G8B8;
    if (ff == D3DXIFF_JPG) fileFormat = D3DFMT_X8R8G8B8;
    else if (comp == 1) fileFormat = D3DFMT_L8;
    else if (comp == 2) fileFormat = D3DFMT_A8L8;
    else if (comp == 3) fileFormat = (ff == D3DXIFF_PNG) ? D3DFMT_X8R8G8B8 : D3DFMT_R8G8B8;

    out.storage = D3DFMT_A8R8G8B8;
    out.info.Width = (UINT)w;
    out.info.Height = (UINT)h;
    out.info.Depth = 1;
    out.info.MipLevels = 1;
    out.info.Format = fileFormat;
    out.info.ResourceType = D3DRTYPE_TEXTURE;
    out.info.ImageFileFormat = ff;
    Image::Level lv;
    lv.w = (UINT)w;
    lv.h = (UINT)h;
    lv.pitch = (UINT)w * 4;
    out.levels.push_back(lv);
    out.bytes.resize((size_t)w * h * 4);
    for (size_t i = 0; i < (size_t)w * h; ++i) {
        const stbi_uc* s = rgba + i * 4;
        Wr32(out.bytes.data() + i * 4, Argb(s[3], s[0], s[1], s[2]));
    }
    stbi_image_free(rgba);
    return D3D_OK;
}

} // namespace ran_d3dx
