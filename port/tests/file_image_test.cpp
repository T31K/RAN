// Tests for the Windows file images of structs with pointer members (win32/file_image.h): a
// stream with the typed ReadBuffer/WriteBuffer overloads reads 4-byte pointer slots from a
// Windows-layout image and writes the same image back; other types pass through unchanged.
#include "ran_compat.h"
#include <cstdio>
#include <cstring>
#include <vector>

static int g_failed = 0;
#define CHECK(cond) do { if (!(cond)) { std::printf("FAIL %s:%d  %s\n", __FILE__, __LINE__, #cond); ++g_failed; } } while (0)

// Like DXOCMATERIAL: 68 bytes, a texture pointer, a path (332 bytes on Windows x86).
struct Material
{
    float colors[17];
    void* pTexture;
    char szTexture[260];
};
RAN_WIN32_IMAGE_POINTERS(Material, pTexture, szTexture, 68, 332)

class Holder
{
protected:   // like DxEffectTiling::POINTEX: nested in a non-public section
    struct Point
    {
        void* pPoint;
        float pos[3];
        DWORD material, color;
        RAN_WIN32_IMAGE_POINTERS_MEMBER(Point, pPoint, pos, 0, 24)
    };
public:
    static bool RoundTrip();
};

// A minimal game stream: bytes in a vector, plus the native-build overloads.
struct Stream
{
    std::vector<BYTE> bytes;
    size_t at = 0;
    BOOL ReadBuffer(void* p, DWORD n)
    {
        if (at + n > bytes.size()) return FALSE;
        std::memcpy(p, bytes.data() + at, n);
        at += n;
        return TRUE;
    }
    BOOL WriteBuffer(const void* p, DWORD n)
    {
        bytes.insert(bytes.end(), (const BYTE*)p, (const BYTE*)p + n);
        return TRUE;
    }
    RAN_STREAM_IMAGE_OVERLOADS
};

static void Put(std::vector<BYTE>& v, const void* p, size_t n) { v.insert(v.end(), (const BYTE*)p, (const BYTE*)p + n); }

bool Holder::RoundTrip()
{
    std::vector<BYTE> image;
    const DWORD junkPtr = 0xDEADBEEF;
    const float pos[3] = { 1, 2, 3 };
    const DWORD mat = 7, col = 0x00FFFFFF;
    Put(image, &junkPtr, 4); Put(image, pos, 12); Put(image, &mat, 4); Put(image, &col, 4);
    Stream s;
    s.bytes = image;
    Point p[1];
    p[0].pPoint = (void*)0x1;
    const bool read = s.ReadBuffer(p, sizeof(Point)) && s.at == 24;
    const bool fields = p[0].pPoint == nullptr && p[0].pos[2] == 3 && p[0].material == 7 && p[0].color == 0x00FFFFFF;
    Stream w;
    w.WriteBuffer(p, sizeof(Point));
    image[0] = image[1] = image[2] = image[3] = 0;   // pointer slots are written as 0
    return read && fields && w.bytes == image;
}

int main()
{
    CHECK(sizeof(Material) > 332);   // the native layout really is bigger

    // Two Windows-layout elements: colours, a junk 4-byte pointer, the path.
    std::vector<BYTE> image;
    for (int e = 0; e < 2; ++e) {
        float colors[17];
        for (int i = 0; i < 17; ++i) colors[i] = (float)(e * 100 + i);
        const DWORD junkPtr = 0x12345678;
        char path[260] = {};
        std::snprintf(path, sizeof(path), "tex%d.dds", e);
        Put(image, colors, sizeof(colors));
        Put(image, &junkPtr, 4);
        Put(image, path, sizeof(path));
    }
    CHECK(image.size() == 2 * 332);

    Stream s;
    s.bytes = image;
    Material m[2];
    CHECK(s.ReadBuffer(m, sizeof(Material) * 2));
    CHECK(s.at == image.size());
    CHECK(m[0].colors[16] == 16 && m[1].colors[0] == 100 && m[1].colors[16] == 116);
    CHECK(m[0].pTexture == nullptr && m[1].pTexture == nullptr);
    CHECK(std::strcmp(m[0].szTexture, "tex0.dds") == 0 && std::strcmp(m[1].szTexture, "tex1.dds") == 0);

    // Writing produces the Windows image again (pointer slots zeroed).
    m[0].pTexture = &m[1];
    Stream w;
    CHECK(w.WriteBuffer(m, sizeof(Material) * 2));
    std::vector<BYTE> expect = image;
    std::memset(expect.data() + 68, 0, 4);
    std::memset(expect.data() + 332 + 68, 0, 4);
    CHECK(w.bytes == expect);

    // Nested types declare their image from inside.
    CHECK(Holder::RoundTrip());

    // Types without an image, and partial reads, pass through unchanged.
    Stream p;
    const DWORD values[2] = { 1, 2 };
    p.WriteBuffer(values, sizeof(values));
    CHECK(p.bytes.size() == 8);
    DWORD back[2] = {};
    CHECK(p.ReadBuffer(back, sizeof(back)) && back[1] == 2);
    Stream q;
    q.bytes = image;
    Material one;
    CHECK(q.ReadBuffer(&one, 100) && q.at == 100);   // not a whole element: raw bytes

    if (g_failed) { std::printf("%d check(s) failed\n", g_failed); return 1; }
    std::printf("file_image_test: all checks passed\n");
    return 0;
}
