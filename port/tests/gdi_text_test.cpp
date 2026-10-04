// Tests for the native GDI text path (port/platform/gdi_coretext.cpp), driven the way
// d3dfont.cpp bakes its glyph atlas: memory DC + 32-bit DIB section + CreateFont + ExtTextOut.
#include "ran_compat.h"
#include <cstdio>
#include <cstring>

static int g_failed = 0;
#define CHECK(cond) do { if (!(cond)) { std::printf("FAIL %s:%d  %s\n", __FILE__, __LINE__, #cond); ++g_failed; } } while (0)

static HBITMAP MakeDib(HDC dc, int w, int h, DWORD** bits)
{
    BITMAPINFO bmi;
    std::memset(&bmi, 0, sizeof(bmi));
    bmi.bmiHeader.biSize = sizeof(BITMAPINFOHEADER);
    bmi.bmiHeader.biWidth = w;
    bmi.bmiHeader.biHeight = h;   // negative = top-down
    bmi.bmiHeader.biPlanes = 1;
    bmi.bmiHeader.biCompression = BI_RGB;
    bmi.bmiHeader.biBitCount = 32;
    return CreateDIBSection(dc, &bmi, DIB_RGB_COLORS, (void**)bits, nullptr, 0);
}

// Pixels of the rect that are non-zero.
static int Lit(const DWORD* bits, int stride, int x0, int y0, int x1, int y1)
{
    int n = 0;
    for (int y = y0; y < y1; ++y)
        for (int x = x0; x < x1; ++x) n += bits[y * stride + x] != 0;
    return n;
}

int main()
{
    HDC dc = CreateCompatibleDC(nullptr);
    CHECK(dc != nullptr);
    DWORD* bits = nullptr;
    HBITMAP bmp = MakeDib(dc, 128, -32, &bits);
    CHECK(bmp && bits && bits[0] == 0 && bits[128 * 32 - 1] == 0);
    CHECK(SelectObject(dc, bmp) == nullptr);

    // Same call as d3dfont.cpp: 9pt at 96 dpi = 12 px em height.
    const int height = -MulDiv(9, GetDeviceCaps(dc, LOGPIXELSY), 72);
    CHECK(height == -12);
    HFONT font = CreateFont(height, 0, 0, 0, FW_NORMAL, FALSE, FALSE, FALSE, DEFAULT_CHARSET, OUT_DEFAULT_PRECIS,
                            CLIP_DEFAULT_PRECIS, ANTIALIASED_QUALITY, VARIABLE_PITCH, "Tahoma");
    CHECK(font != nullptr);
    SelectObject(dc, font);

    TEXTMETRIC tm;
    CHECK(GetTextMetrics(dc, &tm) && tm.tmHeight == tm.tmAscent + tm.tmDescent);
    CHECK(tm.tmHeight >= 12 && tm.tmHeight <= 17 && tm.tmInternalLeading >= 0);

    SIZE a, ab, han, empty;
    CHECK(GetTextExtentPoint32(dc, "A", 1, &a) && a.cx > 3 && a.cy == tm.tmHeight);
    CHECK(GetTextExtentPoint32(dc, "AB", 2, &ab) && ab.cx > a.cx);
    const char hangul[] = "\xC7\xD1";   // CP949 for U+D55C (Korean "han"), drawn via font fallback
    CHECK(GetTextExtentPoint32(dc, hangul, 2, &han) && han.cx >= 8);
    CHECK(GetTextExtentPoint32(dc, "", 0, &empty) && empty.cx == 0 && empty.cy == tm.tmHeight);

    // Transparent white text: glyph pixels lit, top byte 0, nothing outside the text cell.
    SetBkMode(dc, TRANSPARENT);
    SetTextColor(dc, RGB(255, 255, 255));
    CHECK(ExtTextOut(dc, 4, 4, ETO_OPAQUE, nullptr, "A", 1, nullptr));
    const int inside = Lit(bits, 128, 0, 0, 4 + a.cx + 4, 4 + a.cy + 2);
    CHECK(inside > 5);
    CHECK(Lit(bits, 128, 4 + a.cx + 6, 0, 128, 32) == 0);
    bool topByteZero = true, white = false;
    for (int i = 0; i < 128 * 32; ++i) {
        if (bits[i] >> 24) topByteZero = false;
        if (bits[i] == 0x00FFFFFF) white = true;
    }
    CHECK(topByteZero && white);

    // Korean text draws too.
    CHECK(ExtTextOut(dc, 40, 4, 0, nullptr, hangul, 2, nullptr));
    CHECK(Lit(bits, 128, 40, 0, 40 + han.cx + 2, 32) > 10);

    // OPAQUE background fills the text cell with the background colour.
    SetBkMode(dc, OPAQUE);
    SetBkColor(dc, RGB(1, 2, 3));
    SetTextColor(dc, RGB(255, 0, 0));
    CHECK(ExtTextOut(dc, 80, 10, 0, nullptr, "H", 1, nullptr));
    CHECK(bits[10 * 128 + 80] == 0x00010203);   // top-left of the cell: background
    bool red = false;
    for (int i = 0; i < 128 * 32; ++i) red |= ((bits[i] >> 16) & 0xFF) > 200 && (bits[i] & 0xFFFF) < 0x0808;
    CHECK(red);

    // Bottom-up DIB: row 0 of the buffer is the bottom of the image.
    HDC dc2 = CreateCompatibleDC(nullptr);
    DWORD* up = nullptr;
    HBITMAP bmp2 = MakeDib(dc2, 32, 32, &up);
    SelectObject(dc2, bmp2);
    SelectObject(dc2, font);
    SetBkMode(dc2, TRANSPARENT);
    SetTextColor(dc2, RGB(255, 255, 255));
    ExtTextOut(dc2, 0, 0, 0, nullptr, "W", 1, nullptr);   // top of the image
    CHECK(Lit(up, 32, 0, 32 - 16, 32, 32) > 5 && Lit(up, 32, 0, 0, 32, 8) == 0);

    // Cell-height fonts (positive height), Korean face names, wide entry points.
    HFONT cell = CreateFont(16, 0, 0, 0, FW_BOLD, FALSE, FALSE, FALSE, HANGUL_CHARSET, 0, 0, 0, 0, "\xB1\xBC\xB8\xB2");   // "Gulim" in CP949
    CHECK(cell != nullptr);
    HFONT prev = (HFONT)SelectObject(dc, cell);
    CHECK(prev == font);
    CHECK(GetTextMetrics(dc, &tm) && tm.tmHeight >= 15 && tm.tmHeight <= 18 && tm.tmWeight == FW_BOLD);
    SIZE w;
    const WCHAR wide[] = { 0xD55C, 'a', 0 };
    CHECK(GetTextExtentPoint32W(dc, wide, 2, &w) && w.cx > 8);
    CHECK(ExtTextOutW(dc, 0, 0, 0, nullptr, wide, 2, nullptr));

    // Unsupported bitmaps are refused; deletes succeed.
    BITMAPINFO bad = {};
    bad.bmiHeader.biWidth = 4; bad.bmiHeader.biHeight = 4; bad.bmiHeader.biBitCount = 8;
    void* none = (void*)1;
    CHECK(CreateDIBSection(dc, &bad, DIB_RGB_COLORS, &none, nullptr, 0) == nullptr && none == nullptr);
    CHECK(DeleteObject(cell) && DeleteObject(font) && DeleteObject(bmp) && DeleteObject(bmp2));
    CHECK(DeleteDC(dc) && DeleteDC(dc2));

    if (g_failed) { std::printf("%d check(s) failed\n", g_failed); return 1; }
    std::printf("gdi_text_test: all checks passed\n");
    return 0;
}
