// Tests for the native D3DX image layer (port/d3dx/d3dx9_image.cpp): DXTn decoding, DDS parsing
// (levels, cube maps), pixel-format round trips, resizing/mips, colour keys, stb-decoded files.
#include "../d3dx/d3dx9_image.h"
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <vector>

using namespace ran_d3dx;

static int g_failed = 0;
#define CHECK(cond) do { if (!(cond)) { std::printf("FAIL %s:%d  %s\n", __FILE__, __LINE__, #cond); ++g_failed; } } while (0)

static void Put16(std::vector<BYTE>& v, uint32_t x) { v.push_back((BYTE)x); v.push_back((BYTE)(x >> 8)); }
static void Put32(std::vector<BYTE>& v, uint32_t x) { for (int i = 0; i < 4; ++i) v.push_back((BYTE)(x >> (8 * i))); }

// Minimal DX9 DDS header.
static std::vector<BYTE> DdsHeader(UINT w, UINT h, UINT mips, bool fourcc, uint32_t code, uint32_t bits,
                                   uint32_t rm, uint32_t gm, uint32_t bm, uint32_t am, bool cube = false)
{
    std::vector<BYTE> v = { 'D', 'D', 'S', ' ' };
    Put32(v, 124);
    Put32(v, 0x1007 | (mips > 1 ? 0x20000 : 0));
    Put32(v, h); Put32(v, w); Put32(v, 0); Put32(v, 0); Put32(v, mips);
    for (int i = 0; i < 11; ++i) Put32(v, 0);
    Put32(v, 32);
    Put32(v, fourcc ? 0x4 : (0x40 | (am ? 0x1 : 0)));
    Put32(v, fourcc ? code : 0);
    Put32(v, bits); Put32(v, rm); Put32(v, gm); Put32(v, bm); Put32(v, am);
    Put32(v, 0x1000); Put32(v, cube ? 0xFE00 : 0); Put32(v, 0); Put32(v, 0); Put32(v, 0);
    return v;
}

int main()
{
    // DXT1: red/blue endpoints, indices 0,1,2,3 repeating per row.
    {
        BYTE blk[8];
        blk[0] = 0x00; blk[1] = 0xF8;   // c0 = pure red (565)
        blk[2] = 0x1F; blk[3] = 0x00;   // c1 = pure blue
        blk[4] = blk[5] = blk[6] = blk[7] = 0xE4;   // 0b11100100: 0,1,2,3 per row
        uint32_t px[16];
        CHECK(Decode(D3DFMT_DXT1, blk, 8, 4, 4, px));
        CHECK(px[0] == 0xFFFF0000 && px[1] == 0xFF0000FF);
        CHECK(px[2] == 0xFFAA0055 && px[3] == 0xFF5500AA);   // 2/3-1/3 blends
        // c0 <= c1: three colours + transparent black.
        std::swap(blk[0], blk[2]); std::swap(blk[1], blk[3]);
        CHECK(Decode(D3DFMT_DXT1, blk, 8, 4, 4, px));
        CHECK(px[2] == 0xFF7F007F && px[3] == 0x00000000);
    }
    // DXT5 alpha: a0=255, a1=0, index 1 (=a1) for pixel 0, index 0 for the rest.
    {
        BYTE blk[16] = {};
        blk[0] = 255; blk[1] = 0;
        blk[2] = 0x01;                  // pixel 0 -> index 1
        blk[8] = 0xFF; blk[9] = 0xFF;   // colour c0 = white
        uint32_t px[16];
        CHECK(Decode(D3DFMT_DXT5, blk, 16, 4, 4, px));
        CHECK(px[0] == 0x00FFFFFF && px[1] == 0xFFFFFFFF);
    }
    // DXT3 explicit alpha.
    {
        BYTE blk[16] = {};
        blk[0] = 0xF0;                  // pixel 0 alpha 0, pixel 1 alpha 15
        uint32_t px[16];
        CHECK(Decode(D3DFMT_DXT3, blk, 16, 4, 4, px));
        CHECK((px[0] >> 24) == 0 && (px[1] >> 24) == 0xFF);
    }

    // Uncompressed round trips.
    {
        const uint32_t in[4] = { 0xFFFF0000, 0x8000FF00, 0x000000FF, 0xFFFFFFFF };
        const D3DFORMAT exact[] = { D3DFMT_A8R8G8B8, D3DFMT_A8B8G8R8 };
        for (D3DFORMAT f : exact) {
            BYTE buf[16]; uint32_t out[4];
            CHECK(Encode(f, in, 4, 1, buf, 16) && Decode(f, buf, 16, 4, 1, out));
            CHECK(std::memcmp(in, out, sizeof(in)) == 0);
        }
        BYTE b16[8]; uint32_t out[4];
        CHECK(Encode(D3DFMT_R5G6B5, in, 4, 1, b16, 8) && Decode(D3DFMT_R5G6B5, b16, 8, 4, 1, out));
        CHECK(out[0] == 0xFFFF0000 && out[1] == 0xFF00FF00 && out[3] == 0xFFFFFFFF);
        CHECK(Encode(D3DFMT_A1R5G5B5, in, 4, 1, b16, 8) && Decode(D3DFMT_A1R5G5B5, b16, 8, 4, 1, out));
        CHECK(out[1] == 0xFF00FF00 && out[2] == 0x000000FF);   // alpha 0x80 rounds up to 1
        CHECK(Encode(D3DFMT_A4R4G4B4, in, 4, 1, b16, 8) && Decode(D3DFMT_A4R4G4B4, b16, 8, 4, 1, out));
        CHECK(out[1] == 0x8800FF00);
        BYTE b24[12];
        CHECK(Encode(D3DFMT_R8G8B8, in, 4, 1, b24, 12) && Decode(D3DFMT_R8G8B8, b24, 12, 4, 1, out));
        CHECK(out[1] == 0xFF00FF00);
        CHECK(CanEncode(D3DFMT_DXT1) && CanDecode(D3DFMT_DXT5));
        CHECK(RowBytes(D3DFMT_DXT1, 6) == 16 && RowCount(D3DFMT_DXT5, 5) == 2 && RowBytes(D3DFMT_R5G6B5, 3) == 6);
    }

    // DXTn encoding round trips (font atlases are converted A4R4G4B4 -> DXT2 by the game).
    {
        auto near = [](uint32_t a, uint32_t b, int tol) {
            for (int s = 0; s < 32; s += 8)
                if (std::abs((int)((a >> s) & 0xFF) - (int)((b >> s) & 0xFF)) > tol) return false;
            return true;
        };
        uint32_t px[16], out[16];
        BYTE blk[16];
        for (int i = 0; i < 16; ++i) px[i] = (i & 1) ? 0xFFFFFFFF : 0xFF000000;   // black/white
        CHECK(Encode(D3DFMT_DXT1, px, 4, 4, blk, 8) && Decode(D3DFMT_DXT1, blk, 8, 4, 4, out));
        bool ok = true;
        for (int i = 0; i < 16; ++i) ok &= near(out[i], px[i], 24);
        CHECK(ok);
        for (int i = 0; i < 16; ++i) px[i] = i < 8 ? 0x00000000 : 0xFFFF0000;      // punch-through alpha
        CHECK(Encode(D3DFMT_DXT1, px, 4, 4, blk, 8) && Decode(D3DFMT_DXT1, blk, 8, 4, 4, out));
        CHECK(out[0] == 0 && near(out[15], 0xFFFF0000, 16));
        for (int i = 0; i < 16; ++i) px[i] = ((uint32_t)(i * 17) << 24) | 0x00FFFFFF;  // alpha ramp
        CHECK(Encode(D3DFMT_DXT5, px, 4, 4, blk, 16) && Decode(D3DFMT_DXT5, blk, 16, 4, 4, out));
        ok = true;
        for (int i = 0; i < 16; ++i) ok &= std::abs((int)(out[i] >> 24) - i * 17) <= 20 && (out[i] & 0xFFFFFF) == 0xFFFFFF;
        CHECK(ok);
        for (int i = 0; i < 16; ++i) px[i] = 0x80FFFFFF;                           // DXT2 premultiplies
        CHECK(Encode(D3DFMT_DXT2, px, 4, 4, blk, 16) && Decode(D3DFMT_DXT2, blk, 16, 4, 4, out));
        CHECK(near(out[0], 0x88808080, 12));
        // A 6x5 surface: partial edge blocks repeat the last texels, nothing written out of range.
        std::vector<uint32_t> img(30, 0xFF00FF00);
        std::vector<BYTE> dst(2 * 2 * 16 + 1, 0xCD);
        CHECK(Encode(D3DFMT_DXT3, img.data(), 6, 5, dst.data(), 32) && dst.back() == 0xCD);
        std::vector<uint32_t> back(30);
        CHECK(Decode(D3DFMT_DXT3, dst.data(), 32, 6, 5, back.data()) && near(back[29], 0xFF00FF00, 8));
    }

    // Resize and mips.
    {
        Pixels p;
        p.w = 4; p.h = 2;
        p.argb = { 0xFF000000, 0xFF0000FF, 0xFF000000, 0xFF000000,
                   0xFF0000FF, 0xFF000000, 0xFF0000FF, 0xFF0000FF };
        Pixels h = HalfSize(p);
        CHECK(h.w == 2 && h.h == 1 && h.argb[0] == 0xFF000080 && h.argb[1] == 0xFF000080);
        Pixels one = HalfSize(HalfSize(h));
        CHECK(one.w == 1 && one.h == 1);
        Pixels up = Resize(one, 3, 3);
        CHECK(up.w == 3 && up.argb[4] == one.argb[0]);
        Pixels key = p;
        ApplyColorKey(key, 0xFF000000);
        CHECK(key.argb[0] == 0 && key.argb[1] == 0xFF0000FF);
    }

    // DDS: 4x4 A8R8G8B8 with 3 levels.
    {
        std::vector<BYTE> f = DdsHeader(4, 4, 3, false, 0, 32, 0xFF0000, 0xFF00, 0xFF, 0xFF000000);
        for (int i = 0; i < 16 + 4 + 1; ++i) Put32(f, 0x80000000u | (uint32_t)i);
        Image img;
        CHECK(LoadImage(f.data(), f.size(), img) == D3D_OK);
        CHECK(img.info.Width == 4 && img.info.MipLevels == 3 && img.info.Format == D3DFMT_A8R8G8B8);
        CHECK(img.info.ImageFileFormat == D3DXIFF_DDS && img.faces == 1 && img.storage == D3DFMT_A8R8G8B8);
        CHECK(img.At(0, 1).w == 2 && img.At(0, 1).offset == 64 && img.At(0, 2).offset == 80);
        CHECK(img.DecodeLevel(0, 2).argb[0] == 0x80000014);
        // Truncated payload is rejected.
        f.resize(f.size() - 4);
        CHECK(LoadImage(f.data(), f.size(), img) == D3DXERR_INVALIDDATA);
    }
    // DDS: DXT1 8x8 single level, R5G6B5, cube map.
    {
        std::vector<BYTE> f = DdsHeader(8, 8, 1, true, MAKEFOURCC('D', 'X', 'T', '1'), 0, 0, 0, 0, 0);
        f.resize(f.size() + 4 * 8);
        Image img;
        CHECK(LoadImage(f.data(), f.size(), img) == D3D_OK && img.storage == D3DFMT_DXT1 && img.At(0, 0).pitch == 16);
        std::vector<BYTE> g = DdsHeader(2, 1, 1, false, 0, 16, 0xF800, 0x7E0, 0x1F, 0);
        Put16(g, 0xF800); Put16(g, 0x001F);
        CHECK(LoadImage(g.data(), g.size(), img) == D3D_OK && img.info.Format == D3DFMT_R5G6B5);
        CHECK(img.DecodeLevel(0, 0).argb[1] == 0xFF0000FF);
        std::vector<BYTE> c = DdsHeader(4, 4, 1, true, MAKEFOURCC('D', 'X', 'T', '5'), 0, 0, 0, 0, 0, true);
        c.resize(c.size() + 6 * 16);
        CHECK(LoadImage(c.data(), c.size(), img) == D3D_OK && img.faces == 6 && img.info.ResourceType == D3DRTYPE_CUBETEXTURE);
        CHECK(img.At(5, 0).offset == 5 * 16);
    }
    // BMP (24-bit, bottom-up, 2x1: blue, red) through stb.
    {
        std::vector<BYTE> b = { 'B', 'M' };
        Put32(b, 14 + 40 + 8); Put32(b, 0); Put32(b, 14 + 40);
        Put32(b, 40); Put32(b, 2); Put32(b, 1); Put16(b, 1); Put16(b, 24); Put32(b, 0); Put32(b, 8);
        Put32(b, 2835); Put32(b, 2835); Put32(b, 0); Put32(b, 0);
        b.insert(b.end(), { 0xFF, 0x00, 0x00, 0x00, 0x00, 0xFF, 0, 0 });   // BGR BGR + padding
        Image img;
        CHECK(LoadImage(b.data(), b.size(), img) == D3D_OK);
        CHECK(img.info.ImageFileFormat == D3DXIFF_BMP && img.info.Format == D3DFMT_R8G8B8 && img.info.Width == 2);
        const Pixels p = img.DecodeLevel(0, 0);
        CHECK(p.argb[0] == 0xFF0000FF && p.argb[1] == 0xFFFF0000);
    }
    // Garbage and GIF are refused like D3DX refuses them.
    {
        Image img;
        const BYTE junk[16] = { 1, 2, 3 };
        CHECK(LoadImage(junk, sizeof(junk), img) == D3DXERR_INVALIDDATA);
        const BYTE gif[16] = { 'G', 'I', 'F', '8', '9', 'a' };
        CHECK(LoadImage(gif, sizeof(gif), img) == D3DXERR_INVALIDDATA);
    }

    if (g_failed) { std::printf("%d check(s) failed\n", g_failed); return 1; }
    std::printf("d3dx_image_test: all checks passed\n");
    return 0;
}
