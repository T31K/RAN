// Windows cursor files (.cur, and the first frame of an animated .ani) decoded to 32-bit
// pixels for SDL colour cursors (cursor_sdl.cpp). Handles the DIB images Windows cursors use:
// 1/4/8/24/32 bits per pixel with the AND transparency mask. Tested by cursor_decode_test.cpp.
#pragma once
#include <cstddef>
#include <cstdint>
#include <cstring>
#include <vector>

namespace ran_platform {

struct CursorImage
{
    int width = 0, height = 0, hotX = 0, hotY = 0;
    std::vector<uint32_t> argb;   // 0xAARRGGBB, top row first
};

namespace cursor_detail {
inline uint32_t U32(const uint8_t* p) { return (uint32_t)p[0] | ((uint32_t)p[1] << 8) | ((uint32_t)p[2] << 16) | ((uint32_t)p[3] << 24); }
inline uint16_t U16(const uint8_t* p) { return (uint16_t)(p[0] | (p[1] << 8)); }
}

// One DIB (BITMAPINFOHEADER + palette + XOR image + AND mask), height = 2 * image height.
inline bool DecodeCursorDib(const uint8_t* p, size_t n, CursorImage& out)
{
    using namespace cursor_detail;
    if (n < 40 || U32(p) < 40) return false;
    const int w = (int)U32(p + 4);
    const int h = (int)U32(p + 8) / 2;
    const int bpp = U16(p + 14);
    if (w <= 0 || h <= 0 || w > 256 || h > 256 || U32(p + 16) != 0 /*BI_RGB*/) return false;
    if (bpp != 1 && bpp != 4 && bpp != 8 && bpp != 24 && bpp != 32) return false;
    size_t at = U32(p);
    uint32_t colors = bpp <= 8 ? (U32(p + 32) ? U32(p + 32) : (1u << bpp)) : 0;
    const uint8_t* palette = p + at;
    at += (size_t)colors * 4;
    const size_t xorStride = (((size_t)w * bpp + 31) / 32) * 4, andStride = (((size_t)w + 31) / 32) * 4;
    if (at + xorStride * h + andStride * h > n) return false;
    const uint8_t* xorBits = p + at;
    const uint8_t* andBits = xorBits + xorStride * h;
    out.width = w;
    out.height = h;
    out.argb.assign((size_t)w * h, 0);
    bool anyAlpha = false;
    if (bpp == 32)
        for (int y = 0; y < h && !anyAlpha; ++y)
            for (int x = 0; x < w; ++x) anyAlpha |= xorBits[y * xorStride + x * 4 + 3] != 0;
    for (int y = 0; y < h; ++y) {
        const uint8_t* row = xorBits + (size_t)(h - 1 - y) * xorStride;   // bottom-up
        const uint8_t* mask = andBits + (size_t)(h - 1 - y) * andStride;
        for (int x = 0; x < w; ++x) {
            uint32_t b = 0, g = 0, r = 0, a = 255;
            if (bpp == 32) { b = row[x * 4]; g = row[x * 4 + 1]; r = row[x * 4 + 2]; if (anyAlpha) a = row[x * 4 + 3]; }
            else if (bpp == 24) { b = row[x * 3]; g = row[x * 3 + 1]; r = row[x * 3 + 2]; }
            else {
                const int perByte = 8 / bpp;
                const uint32_t idx = (row[x / perByte] >> ((perByte - 1 - x % perByte) * bpp)) & ((1u << bpp) - 1);
                if (idx < colors) { b = palette[idx * 4]; g = palette[idx * 4 + 1]; r = palette[idx * 4 + 2]; }
            }
            // AND bit set: transparent (an inverted pixel when the colour is not black - shown
            // as transparent; SDL colour cursors cannot invert the screen).
            if (!anyAlpha && ((mask[x / 8] >> (7 - x % 8)) & 1)) a = 0;
            out.argb[(size_t)y * w + x] = a ? ((a << 24) | (r << 16) | (g << 8) | b) : 0;
        }
    }
    return true;
}

// A .cur/.ico file (largest image) or an .ani file (its first frame).
inline bool DecodeCursorFile(const uint8_t* p, size_t n, CursorImage& out)
{
    using namespace cursor_detail;
    if (n >= 12 && std::memcmp(p, "RIFF", 4) == 0 && std::memcmp(p + 8, "ACON", 4) == 0) {
        // Walk the chunks (descending into LIST) to the first "icon" frame.
        size_t at = 12;
        while (at + 8 <= n) {
            const uint32_t size = U32(p + at + 4);
            if (std::memcmp(p + at, "LIST", 4) == 0) { at += 12; continue; }
            if (std::memcmp(p + at, "icon", 4) == 0) return at + 8 + size <= n && DecodeCursorFile(p + at + 8, size, out);
            at += 8 + size + (size & 1);
        }
        return false;
    }
    if (n < 6 || U16(p) != 0 || (U16(p + 2) != 1 && U16(p + 2) != 2)) return false;
    const bool isCursor = U16(p + 2) == 2;
    const int count = U16(p + 4);
    int best = -1, bestSize = -1;
    for (int i = 0; i < count; ++i) {
        const uint8_t* e = p + 6 + i * 16;
        if ((size_t)(e + 16 - p) > n) return false;
        const int size = e[0] ? e[0] : 256;
        if (size > bestSize) { bestSize = size; best = i; }
    }
    if (best < 0) return false;
    const uint8_t* e = p + 6 + best * 16;
    const uint32_t bytes = U32(e + 8), offset = U32(e + 12);
    if ((size_t)offset + bytes > n || !DecodeCursorDib(p + offset, bytes, out)) return false;
    out.hotX = isCursor ? U16(e + 4) : out.width / 2;
    out.hotY = isCursor ? U16(e + 6) : out.height / 2;
    return true;
}

} // namespace ran_platform
