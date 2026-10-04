// Tests for the Windows cursor decoder (port/platform/cursor_decode.h): .cur DIB images with
// the AND mask, palettes, hotspots, .ani first frames; plus the client's own cursor files when
// the client folder is present (~/Projects/RAN/client/data/editor).
#include "../platform/cursor_decode.h"
#include <cstdio>
#include <cstdlib>
#include <string>
#include <vector>

static int g_failed = 0;
#define CHECK(cond) do { if (!(cond)) { std::printf("FAIL %s:%d  %s\n", __FILE__, __LINE__, #cond); ++g_failed; } } while (0)

static void P16(std::vector<uint8_t>& v, uint32_t x) { v.push_back((uint8_t)x); v.push_back((uint8_t)(x >> 8)); }
static void P32(std::vector<uint8_t>& v, uint32_t x) { for (int i = 0; i < 4; ++i) v.push_back((uint8_t)(x >> (8 * i))); }

// 2x2 24-bit cursor, hotspot (1,0): top row red + transparent, bottom row green + blue.
static std::vector<uint8_t> MakeCur()
{
    std::vector<uint8_t> dib;
    P32(dib, 40); P32(dib, 2); P32(dib, 4); P16(dib, 1); P16(dib, 24); P32(dib, 0);
    P32(dib, 0); P32(dib, 0); P32(dib, 0); P32(dib, 0); P32(dib, 0);
    // XOR rows, bottom-up, 6 bytes + 2 padding each.
    dib.insert(dib.end(), { 0x00, 0xFF, 0x00, 0xFF, 0x00, 0x00, 0, 0 });   // bottom: green, blue
    dib.insert(dib.end(), { 0x00, 0x00, 0xFF, 0x00, 0x00, 0x00, 0, 0 });   // top: red, (masked)
    // AND rows, bottom-up, 4 bytes each: top row second pixel transparent.
    dib.insert(dib.end(), { 0x00, 0, 0, 0 });
    dib.insert(dib.end(), { 0x40, 0, 0, 0 });
    std::vector<uint8_t> f;
    P16(f, 0); P16(f, 2); P16(f, 1);
    f.push_back(2); f.push_back(2); f.push_back(0); f.push_back(0);
    P16(f, 1); P16(f, 0); P32(f, (uint32_t)dib.size()); P32(f, 22);
    f.insert(f.end(), dib.begin(), dib.end());
    return f;
}

static std::vector<uint8_t> ReadAll(const std::string& path)
{
    std::vector<uint8_t> v;
    if (std::FILE* f = std::fopen(path.c_str(), "rb")) {
        uint8_t b[4096];
        size_t n;
        while ((n = std::fread(b, 1, sizeof(b), f)) > 0) v.insert(v.end(), b, b + n);
        std::fclose(f);
    }
    return v;
}

int main()
{
    using ran_platform::CursorImage;
    using ran_platform::DecodeCursorFile;

    const std::vector<uint8_t> cur = MakeCur();
    CursorImage img;
    CHECK(DecodeCursorFile(cur.data(), cur.size(), img));
    CHECK(img.width == 2 && img.height == 2 && img.hotX == 1 && img.hotY == 0);
    CHECK(img.argb.size() == 4 && img.argb[0] == 0xFFFF0000 && img.argb[1] == 0);
    CHECK(img.argb[2] == 0xFF00FF00 && img.argb[3] == 0xFF0000FF);

    // The same image as the first frame of an animated cursor.
    std::vector<uint8_t> ani = { 'R', 'I', 'F', 'F' };
    P32(ani, 0);
    ani.insert(ani.end(), { 'A', 'C', 'O', 'N', 'a', 'n', 'i', 'h' });
    P32(ani, 4); P32(ani, 0);
    ani.insert(ani.end(), { 'L', 'I', 'S', 'T' });
    P32(ani, (uint32_t)(4 + 8 + cur.size()));
    ani.insert(ani.end(), { 'f', 'r', 'a', 'm', 'i', 'c', 'o', 'n' });
    P32(ani, (uint32_t)cur.size());
    ani.insert(ani.end(), cur.begin(), cur.end());
    CursorImage frame;
    CHECK(DecodeCursorFile(ani.data(), ani.size(), frame) && frame.argb == img.argb && frame.hotX == 1);

    // Truncated and foreign data are refused.
    CHECK(!DecodeCursorFile(cur.data(), cur.size() - 3, img));
    const uint8_t junk[8] = { 'B', 'M', 0, 0, 0, 0, 0, 0 };
    CHECK(!DecodeCursorFile(junk, sizeof(junk), img));

    // The client's cursors, when the data folder is here.
    const char* home = std::getenv("HOME");
    const std::string dir = std::string(home ? home : "") + "/Projects/RAN/client/data/editor/";
    for (const char* name : { "normal.cur", "attack.cur", "scroll.cur", "talk.cur", "select.ani" }) {
        const std::vector<uint8_t> data = ReadAll(dir + name);
        if (data.empty()) continue;
        CursorImage c;
        const bool ok = DecodeCursorFile(data.data(), data.size(), c);
        CHECK(ok && c.width >= 16 && c.height >= 16);
        int opaque = 0;
        for (uint32_t p : c.argb) opaque += (p >> 24) != 0;
        CHECK(opaque > 10 && opaque < (int)c.argb.size());
        if (!ok) std::printf("  could not decode %s\n", name);
    }

    if (g_failed) { std::printf("%d check(s) failed\n", g_failed); return 1; }
    std::printf("cursor_decode_test: all checks passed\n");
    return 0;
}
