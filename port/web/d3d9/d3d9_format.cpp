// D3D formats on WebGL2: storage choice and upload conversions (see d3d9_web.h).
//  - 32-bit BGRA (A8R8G8B8/X8R8G8B8) and everything without a GL equivalent becomes RGBA8.
//  - R5G6B5, A1R5G5B5, A4R4G4B4 stay 16-bit (bits rotated into GL's RGBA order).
//  - DXT1-5 upload compressed when the browser has S3TC, else are decoded to RGBA8.
//  - V8U8-family bump maps become signed RG8.
#include "d3d9_web.h"
#include <emscripten/html5.h>
#include <algorithm>

namespace d3d9web {

namespace {

const FormatInfo kFormats[] = {
    // format              bpp blk  internal              glFormat  glType                      comp  rt     depth
    { D3DFMT_A8R8G8B8,     4,  0,  GL_RGBA8,              GL_RGBA,  GL_UNSIGNED_BYTE,           false, true,  false },
    { D3DFMT_X8R8G8B8,     4,  0,  GL_RGBA8,              GL_RGBA,  GL_UNSIGNED_BYTE,           false, true,  false },
    { D3DFMT_A8B8G8R8,     4,  0,  GL_RGBA8,              GL_RGBA,  GL_UNSIGNED_BYTE,           false, true,  false },
    { D3DFMT_X8B8G8R8,     4,  0,  GL_RGBA8,              GL_RGBA,  GL_UNSIGNED_BYTE,           false, true,  false },
    { D3DFMT_R8G8B8,       3,  0,  GL_RGBA8,              GL_RGBA,  GL_UNSIGNED_BYTE,           false, false, false },
    { D3DFMT_R5G6B5,       2,  0,  GL_RGB565,             GL_RGB,   GL_UNSIGNED_SHORT_5_6_5,    false, true,  false },
    { D3DFMT_X1R5G5B5,     2,  0,  GL_RGB5_A1,            GL_RGBA,  GL_UNSIGNED_SHORT_5_5_5_1,  false, true,  false },
    { D3DFMT_A1R5G5B5,     2,  0,  GL_RGB5_A1,            GL_RGBA,  GL_UNSIGNED_SHORT_5_5_5_1,  false, true,  false },
    { D3DFMT_A4R4G4B4,     2,  0,  GL_RGBA4,              GL_RGBA,  GL_UNSIGNED_SHORT_4_4_4_4,  false, true,  false },
    { D3DFMT_X4R4G4B4,     2,  0,  GL_RGBA4,              GL_RGBA,  GL_UNSIGNED_SHORT_4_4_4_4,  false, true,  false },
    { D3DFMT_A8,           1,  0,  GL_RGBA8,              GL_RGBA,  GL_UNSIGNED_BYTE,           false, false, false },
    { D3DFMT_L8,           1,  0,  GL_RGBA8,              GL_RGBA,  GL_UNSIGNED_BYTE,           false, false, false },
    { D3DFMT_A8L8,         2,  0,  GL_RGBA8,              GL_RGBA,  GL_UNSIGNED_BYTE,           false, false, false },
    { D3DFMT_A8R3G3B2,     2,  0,  GL_RGBA8,              GL_RGBA,  GL_UNSIGNED_BYTE,           false, false, false },
    { D3DFMT_V8U8,         2,  0,  GL_RG8_SNORM,          GL_RG,    GL_BYTE,                    false, false, false },
    { D3DFMT_L6V5U5,       2,  0,  GL_RG8_SNORM,          GL_RG,    GL_BYTE,                    false, false, false },
    { D3DFMT_X8L8V8U8,     4,  0,  GL_RG8_SNORM,          GL_RG,    GL_BYTE,                    false, false, false },
    { D3DFMT_DXT1,         0,  8,  GL_COMPRESSED_RGBA_S3TC_DXT1_EXT, 0, 0,                      true,  false, false },
    { D3DFMT_DXT2,         0, 16,  GL_COMPRESSED_RGBA_S3TC_DXT3_EXT, 0, 0,                      true,  false, false },
    { D3DFMT_DXT3,         0, 16,  GL_COMPRESSED_RGBA_S3TC_DXT3_EXT, 0, 0,                      true,  false, false },
    { D3DFMT_DXT4,         0, 16,  GL_COMPRESSED_RGBA_S3TC_DXT5_EXT, 0, 0,                      true,  false, false },
    { D3DFMT_DXT5,         0, 16,  GL_COMPRESSED_RGBA_S3TC_DXT5_EXT, 0, 0,                      true,  false, false },
    { D3DFMT_D24S8,        4,  0,  GL_DEPTH24_STENCIL8,   0, 0,                                 false, false, true  },
    { D3DFMT_D24X8,        4,  0,  GL_DEPTH24_STENCIL8,   0, 0,                                 false, false, true  },
    { D3DFMT_D24X4S4,      4,  0,  GL_DEPTH24_STENCIL8,   0, 0,                                 false, false, true  },
    { D3DFMT_D16,          2,  0,  GL_DEPTH24_STENCIL8,   0, 0,                                 false, false, true  },
    { D3DFMT_D15S1,        2,  0,  GL_DEPTH24_STENCIL8,   0, 0,                                 false, false, true  },
    { D3DFMT_D32,          4,  0,  GL_DEPTH24_STENCIL8,   0, 0,                                 false, false, true  },
    { D3DFMT_INDEX16,      2,  0,  0, 0, 0,                                                     false, false, false },
    { D3DFMT_INDEX32,      4,  0,  0, 0, 0,                                                     false, false, false },
};

int g_s3tc = -1;

inline uint8_t Expand5(unsigned v) { return (uint8_t)((v << 3) | (v >> 2)); }
inline uint8_t Expand6(unsigned v) { return (uint8_t)((v << 2) | (v >> 4)); }

// One DXT colour block (8 bytes) -> 16 RGBA pixels. dxt1: 1-bit alpha mode allowed.
void DecodeColorBlock(const uint8_t* b, uint8_t out[16][4], bool dxt1)
{
    const unsigned c0 = b[0] | (b[1] << 8), c1 = b[2] | (b[3] << 8);
    uint8_t pal[4][4];
    pal[0][0] = Expand5(c0 >> 11); pal[0][1] = Expand6((c0 >> 5) & 63); pal[0][2] = Expand5(c0 & 31); pal[0][3] = 255;
    pal[1][0] = Expand5(c1 >> 11); pal[1][1] = Expand6((c1 >> 5) & 63); pal[1][2] = Expand5(c1 & 31); pal[1][3] = 255;
    if (c0 > c1 || !dxt1) {
        for (int i = 0; i < 3; ++i) {
            pal[2][i] = (uint8_t)((2 * pal[0][i] + pal[1][i] + 1) / 3);
            pal[3][i] = (uint8_t)((pal[0][i] + 2 * pal[1][i] + 1) / 3);
        }
        pal[2][3] = pal[3][3] = 255;
    } else {
        for (int i = 0; i < 3; ++i) { pal[2][i] = (uint8_t)((pal[0][i] + pal[1][i]) / 2); pal[3][i] = 0; }
        pal[2][3] = 255;
        pal[3][3] = 0;
    }
    const uint32_t bits = b[4] | (b[5] << 8) | (b[6] << 16) | ((uint32_t)b[7] << 24);
    for (int i = 0; i < 16; ++i) std::memcpy(out[i], pal[(bits >> (2 * i)) & 3], 4);
}

void DecodeDxt(D3DFORMAT f, const uint8_t* src, UINT srcPitch, UINT w, UINT h, std::vector<uint8_t>& rgba)
{
    rgba.assign((size_t)w * h * 4, 0);
    const UINT bw = (w + 3) / 4, bh = (h + 3) / 4;
    const int blockBytes = f == D3DFMT_DXT1 ? 8 : 16;
    for (UINT by = 0; by < bh; ++by)
        for (UINT bx = 0; bx < bw; ++bx) {
            const uint8_t* b = src + (size_t)by * srcPitch + (size_t)bx * blockBytes;
            uint8_t px[16][4];
            if (f == D3DFMT_DXT1) {
                DecodeColorBlock(b, px, true);
            } else {
                DecodeColorBlock(b + 8, px, false);
                if (f == D3DFMT_DXT2 || f == D3DFMT_DXT3) {
                    for (int i = 0; i < 16; ++i) {
                        const unsigned a = (b[i / 2] >> ((i & 1) * 4)) & 15;
                        px[i][3] = (uint8_t)(a * 17);
                    }
                } else {
                    uint8_t a[8];
                    a[0] = b[0]; a[1] = b[1];
                    if (a[0] > a[1]) for (int i = 1; i < 7; ++i) a[i + 1] = (uint8_t)(((7 - i) * a[0] + i * a[1]) / 7);
                    else { for (int i = 1; i < 5; ++i) a[i + 1] = (uint8_t)(((5 - i) * a[0] + i * a[1]) / 5); a[6] = 0; a[7] = 255; }
                    uint64_t bits = 0;
                    for (int i = 0; i < 6; ++i) bits |= (uint64_t)b[2 + i] << (8 * i);
                    for (int i = 0; i < 16; ++i) px[i][3] = a[(bits >> (3 * i)) & 7];
                }
            }
            for (int y = 0; y < 4; ++y)
                for (int x = 0; x < 4; ++x) {
                    const UINT X = bx * 4 + x, Y = by * 4 + y;
                    if (X < w && Y < h) std::memcpy(&rgba[((size_t)Y * w + X) * 4], px[y * 4 + x], 4);
                }
        }
}

// D3D pixels -> the GL upload layout of FormatInfo (RGBA8 / packed 16-bit / RG8 snorm).
void ConvertRect(D3DFORMAT f, const uint8_t* src, UINT srcPitch, UINT w, UINT h, std::vector<uint8_t>& out)
{
    const FormatInfo* fi = GetFormatInfo(f);
    switch (f) {
    case D3DFMT_R5G6B5:
        out.resize((size_t)w * h * 2);
        for (UINT y = 0; y < h; ++y) std::memcpy(&out[(size_t)y * w * 2], src + (size_t)y * srcPitch, (size_t)w * 2);
        return;
    case D3DFMT_A1R5G5B5:
    case D3DFMT_X1R5G5B5:
        out.resize((size_t)w * h * 2);
        for (UINT y = 0; y < h; ++y) {
            const uint16_t* s = (const uint16_t*)(src + (size_t)y * srcPitch);
            uint16_t* d = (uint16_t*)&out[(size_t)y * w * 2];
            for (UINT x = 0; x < w; ++x) {
                const uint16_t v = s[x];
                d[x] = (uint16_t)(((v & 0x7FFF) << 1) | (f == D3DFMT_X1R5G5B5 ? 1 : (v >> 15)));
            }
        }
        return;
    case D3DFMT_A4R4G4B4:
    case D3DFMT_X4R4G4B4:
        out.resize((size_t)w * h * 2);
        for (UINT y = 0; y < h; ++y) {
            const uint16_t* s = (const uint16_t*)(src + (size_t)y * srcPitch);
            uint16_t* d = (uint16_t*)&out[(size_t)y * w * 2];
            for (UINT x = 0; x < w; ++x) {
                const uint16_t v = s[x];
                d[x] = (uint16_t)(((v & 0x0FFF) << 4) | (f == D3DFMT_X4R4G4B4 ? 0xF : (v >> 12)));
            }
        }
        return;
    case D3DFMT_V8U8:
        out.resize((size_t)w * h * 2);
        for (UINT y = 0; y < h; ++y) std::memcpy(&out[(size_t)y * w * 2], src + (size_t)y * srcPitch, (size_t)w * 2);
        return;
    case D3DFMT_X8L8V8U8:
        out.resize((size_t)w * h * 2);
        for (UINT y = 0; y < h; ++y)
            for (UINT x = 0; x < w; ++x) {
                out[((size_t)y * w + x) * 2] = src[(size_t)y * srcPitch + x * 4];
                out[((size_t)y * w + x) * 2 + 1] = src[(size_t)y * srcPitch + x * 4 + 1];
            }
        return;
    case D3DFMT_L6V5U5:
        out.resize((size_t)w * h * 2);
        for (UINT y = 0; y < h; ++y) {
            const uint16_t* s = (const uint16_t*)(src + (size_t)y * srcPitch);
            for (UINT x = 0; x < w; ++x) {
                const int u = (int)((s[x] & 31) << 27) >> 27, v = (int)(((s[x] >> 5) & 31) << 27) >> 27;
                out[((size_t)y * w + x) * 2] = (uint8_t)(int8_t)(u * 127 / 15);
                out[((size_t)y * w + x) * 2 + 1] = (uint8_t)(int8_t)(v * 127 / 15);
            }
        }
        return;
    default:
        break;
    }
    (void)fi;
    out.resize((size_t)w * h * 4);
    for (UINT y = 0; y < h; ++y) {
        const uint8_t* s = src + (size_t)y * srcPitch;
        uint8_t* d = &out[(size_t)y * w * 4];
        switch (f) {
        case D3DFMT_A8R8G8B8:
            for (UINT x = 0; x < w; ++x, s += 4, d += 4) { d[0] = s[2]; d[1] = s[1]; d[2] = s[0]; d[3] = s[3]; }
            break;
        case D3DFMT_X8R8G8B8:
            for (UINT x = 0; x < w; ++x, s += 4, d += 4) { d[0] = s[2]; d[1] = s[1]; d[2] = s[0]; d[3] = 255; }
            break;
        case D3DFMT_A8B8G8R8:
            std::memcpy(d, s, (size_t)w * 4);
            break;
        case D3DFMT_X8B8G8R8:
            for (UINT x = 0; x < w; ++x, s += 4, d += 4) { d[0] = s[0]; d[1] = s[1]; d[2] = s[2]; d[3] = 255; }
            break;
        case D3DFMT_R8G8B8:
            for (UINT x = 0; x < w; ++x, s += 3, d += 4) { d[0] = s[2]; d[1] = s[1]; d[2] = s[0]; d[3] = 255; }
            break;
        case D3DFMT_A8:   // D3D samples A8 as (0, 0, 0, a)
            for (UINT x = 0; x < w; ++x, d += 4) { d[0] = d[1] = d[2] = 0; d[3] = s[x]; }
            break;
        case D3DFMT_L8:
            for (UINT x = 0; x < w; ++x, d += 4) { d[0] = d[1] = d[2] = s[x]; d[3] = 255; }
            break;
        case D3DFMT_A8L8:
            for (UINT x = 0; x < w; ++x, s += 2, d += 4) { d[0] = d[1] = d[2] = s[0]; d[3] = s[1]; }
            break;
        case D3DFMT_A8R3G3B2:
            for (UINT x = 0; x < w; ++x, s += 2, d += 4) {
                const unsigned v = s[0];
                d[0] = (uint8_t)(((v >> 5) & 7) * 255 / 7);
                d[1] = (uint8_t)(((v >> 2) & 7) * 255 / 7);
                d[2] = (uint8_t)((v & 3) * 255 / 3);
                d[3] = s[1];
            }
            break;
        default:
            std::memset(d, 255, (size_t)w * 4);
            break;
        }
    }
}

} // namespace

const FormatInfo* GetFormatInfo(D3DFORMAT f)
{
    for (const FormatInfo& fi : kFormats)
        if (fi.format == f) return &fi;
    return nullptr;
}

bool HasS3tc()
{
    if (g_s3tc < 0) {
        const EMSCRIPTEN_WEBGL_CONTEXT_HANDLE ctx = emscripten_webgl_get_current_context();
        g_s3tc = (ctx && emscripten_webgl_enable_extension(ctx, "WEBGL_compressed_texture_s3tc")) ? 1 : 0;
    }
    return g_s3tc == 1;
}

UINT LevelPitch(D3DFORMAT f, UINT width)
{
    const FormatInfo* fi = GetFormatInfo(f);
    if (!fi) return width * 4;
    if (fi->compressed) return std::max(1u, (width + 3) / 4) * (UINT)fi->blockBytes;
    return width * (UINT)fi->bytesPerPixel;
}

UINT LevelSize(D3DFORMAT f, UINT width, UINT height)
{
    const FormatInfo* fi = GetFormatInfo(f);
    if (fi && fi->compressed) return LevelPitch(f, width) * std::max(1u, (height + 3) / 4);
    return LevelPitch(f, width) * height;
}

void UploadLevel(GLenum target, GLint level, D3DFORMAT f, UINT levelW, UINT levelH,
                 const RECT& r, const uint8_t* src, UINT srcPitch)
{
    const FormatInfo* fi = GetFormatInfo(f);
    if (!fi) return;
    glPixelStorei(GL_UNPACK_ALIGNMENT, 1);
    if (fi->compressed) {
        // Whole blocks: widen the rectangle to block bounds (clamped to the level).
        const UINT x0 = (UINT)r.left & ~3u, y0 = (UINT)r.top & ~3u;
        const UINT x1 = std::min(levelW, ((UINT)r.right + 3) & ~3u), y1 = std::min(levelH, ((UINT)r.bottom + 3) & ~3u);
        const uint8_t* start = src + (size_t)(y0 / 4) * srcPitch + (size_t)(x0 / 4) * fi->blockBytes;
        const UINT w = x1 - x0, h = y1 - y0;
        if (HasS3tc()) {
            const UINT rowBytes = std::max(1u, (w + 3) / 4) * (UINT)fi->blockBytes, rows = std::max(1u, (h + 3) / 4);
            std::vector<uint8_t> packed((size_t)rowBytes * rows);
            for (UINT y = 0; y < rows; ++y) std::memcpy(&packed[(size_t)y * rowBytes], start + (size_t)y * srcPitch, rowBytes);
            glCompressedTexSubImage2D(target, level, (GLint)x0, (GLint)y0, (GLsizei)w, (GLsizei)h, fi->internalFormat,
                                      (GLsizei)packed.size(), packed.data());
        } else {
            std::vector<uint8_t> rgba;
            DecodeDxt(f, start, srcPitch, w, h, rgba);
            glTexSubImage2D(target, level, (GLint)x0, (GLint)y0, (GLsizei)w, (GLsizei)h, GL_RGBA, GL_UNSIGNED_BYTE, rgba.data());
        }
        return;
    }
    const UINT w = (UINT)(r.right - r.left), h = (UINT)(r.bottom - r.top);
    if (!w || !h) return;
    const uint8_t* start = src + (size_t)r.top * srcPitch + (size_t)r.left * fi->bytesPerPixel;
    std::vector<uint8_t> conv;
    ConvertRect(f, start, srcPitch, w, h, conv);
    glTexSubImage2D(target, level, r.left, r.top, (GLsizei)w, (GLsizei)h, fi->glFormat, fi->glType, conv.data());
}

void StoreFromRgba(D3DFORMAT f, const uint8_t* rgba, UINT w, UINT h, uint8_t* dst, UINT dstPitch)
{
    for (UINT y = 0; y < h; ++y) {
        const uint8_t* s = rgba + (size_t)y * w * 4;
        uint8_t* d = dst + (size_t)y * dstPitch;
        for (UINT x = 0; x < w; ++x, s += 4) {
            switch (f) {
            case D3DFMT_A8R8G8B8: case D3DFMT_X8R8G8B8:
                d[x * 4] = s[2]; d[x * 4 + 1] = s[1]; d[x * 4 + 2] = s[0]; d[x * 4 + 3] = f == D3DFMT_X8R8G8B8 ? 255 : s[3];
                break;
            case D3DFMT_A8B8G8R8: case D3DFMT_X8B8G8R8:
                std::memcpy(d + x * 4, s, 4);
                break;
            case D3DFMT_R5G6B5:
                ((uint16_t*)d)[x] = (uint16_t)(((s[0] >> 3) << 11) | ((s[1] >> 2) << 5) | (s[2] >> 3));
                break;
            case D3DFMT_A1R5G5B5: case D3DFMT_X1R5G5B5:
                ((uint16_t*)d)[x] = (uint16_t)(((s[3] >> 7) << 15) | ((s[0] >> 3) << 10) | ((s[1] >> 3) << 5) | (s[2] >> 3));
                break;
            case D3DFMT_A4R4G4B4: case D3DFMT_X4R4G4B4:
                ((uint16_t*)d)[x] = (uint16_t)(((s[3] >> 4) << 12) | ((s[0] >> 4) << 8) | ((s[1] >> 4) << 4) | (s[2] >> 4));
                break;
            default:
                break;
            }
        }
    }
}

} // namespace d3d9web
