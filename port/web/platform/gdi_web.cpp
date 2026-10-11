// GDI memory DCs, DIB sections, fonts and text for the browser build (declarations and contract:
// port/compat/include/win32/gdi.h; the native counterpart is port/platform/gdi_coretext.cpp).
// Glyphs come from the page's 2D canvas text (gdi.js): the browser has the Korean UI fonts, so
// nothing is shipped. Text is rendered white on black into a coverage mask and blended into the
// DIB here, exactly as the CoreText version does.
#include "ran_compat.h"
#include <emscripten.h>
#include <algorithm>
#include <cmath>
#include <cstring>
#include <string>
#include <vector>

extern "C" {
// gdi.js
int ran_gdi_mac_font();
void ran_gdi_font_metrics(const char* css, float* out);   // out: ascent, descent, xWidth, maxWidth (px)
float ran_gdi_measure(const char* css, const char16_t* text, int len);
void ran_gdi_draw(const char* css, const char16_t* text, int len, int w, int h, float x, float baseline, unsigned char* mask);
}

namespace {

enum class Kind : uint32_t { DC = 0x43444741, Bitmap = 0x504D4247, Font = 0x544E4647 };

struct Object { Kind kind; explicit Object(Kind k) : kind(k) {} virtual ~Object() = default; };

struct Bitmap : Object
{
    Bitmap() : Object(Kind::Bitmap) {}
    int width = 0, height = 0, bpp = 32, stride = 0;
    bool topDown = true;
    std::vector<BYTE> bits;

    BYTE* Pixel(int x, int y) { return bits.data() + (size_t)(topDown ? y : height - 1 - y) * stride + (size_t)x * (bpp / 8); }
};

struct Font : Object
{
    Font() : Object(Kind::Font) {}
    std::string css;   // canvas font shorthand
    LONG em = 12, ascent = 0, descent = 0, height = 0, aveWidth = 0, maxWidth = 0, weight = FW_NORMAL;
    bool italic = false;
};

struct DC : Object
{
    DC() : Object(Kind::DC) {}
    Bitmap* bitmap = nullptr;
    Font* font = nullptr;
    COLORREF text = RGB(0, 0, 0), bk = RGB(255, 255, 255);
    int bkMode = OPAQUE;
    UINT align = TA_TOP | TA_LEFT;
};

template <class T> T* As(void* h, Kind k)
{
    Object* o = static_cast<Object*>(h);
    return (o && o->kind == k) ? static_cast<T*>(o) : nullptr;
}

// --- fonts ------------------------------------------------------------------------------------

const char* const kKoreanStack = "\"Malgun Gothic\", \"Apple SD Gothic Neo\", \"AppleSDGothicNeo\", \"Noto Sans KR\", "
                                 "\"Noto Sans CJK KR\", \"Nanum Gothic\", Gulim, Dotum, sans-serif";

bool IsKoreanFace(const std::u16string& f)
{
    static const char16_t* const names[] = {
        u"Gulim", u"GulimChe", u"Dotum", u"DotumChe", u"Batang", u"BatangChe", u"Gungsuh", u"GungsuhChe",
        u"굴림", u"굴림체", u"돋움", u"돋움체",
        u"바탕", u"바탕체", u"궁서", u"궁서체",
        u"Malgun Gothic", u"맑은 고딕", u"MS Sans Serif", u"System" };
    for (const char16_t* n : names)
        if (f.size() == std::char_traits<char16_t>::length(n) &&
            std::equal(f.begin(), f.end(), n, [](char16_t a, char16_t b) { return std::tolower(a) == std::tolower(b); }))
            return true;
    return f.empty();
}

std::string FontCss(const std::u16string& face, double em, bool bold, bool italic)
{
    // On a Mac, text looks like the Mac app's: the game asks for Tahoma, which the Mac app's
    // CoreText lookup does not resolve, so it shows Apple SD Gothic Neo. The canvas draws that
    // font heavier than CoreText (antialiased, no smoothing) does: Light (+ the coverage lift in
    // gdi.js) matches its Regular, and the canvas Regular its Bold (measured ink: 0.99x, 1.05x).
    // Elsewhere (no Apple SD Gothic Neo) the requested face, as the original Windows client.
    static const bool mac = ran_gdi_mac_font() != 0;
    std::string family;
    const char* weight = bold ? "bold " : "";
    if (mac) {
        family = "\"Apple SD Gothic Neo\", \"AppleSDGothicNeo\", ";
        weight = bold ? "" : "300 ";
    } else if (!IsKoreanFace(face)) {
        family = "\"";
        for (char16_t c : face) if (c < 0x80 && c != '"') family += (char)c;
        family += "\", ";
    }
    family += kKoreanStack;
    char head[64];
    std::snprintf(head, sizeof(head), "%s%s%.2fpx ", italic ? "italic " : "", weight, em);
    return head + family;
}

Font* MakeFont(LONG height, LONG weight, bool italic, const std::u16string& face)
{
    const bool bold = weight >= FW_SEMIBOLD;
    // GDI's negative height is the em height, positive the cell (ascent + descent) height.
    double em = 12;
    if (height < 0) em = (double)-height;
    else if (height > 0) {
        float m[4] = {};
        ran_gdi_font_metrics(FontCss(face, 100, bold, italic).c_str(), m);
        const double cell = (double)m[0] + m[1];
        em = cell > 0 ? (double)height * 100 / cell : (double)height;
    }
    Font* f = new Font();
    f->css = FontCss(face, em, bold, italic);
    static const bool trace = EM_ASM_INT({ return location.search.indexOf('fonttrace') >= 0 ? 1 : 0; }) != 0;
    if (trace) std::printf("[gdi] font height %ld -> %s\n", (long)height, f->css.c_str());
    float m[4] = {};
    ran_gdi_font_metrics(f->css.c_str(), m);
    f->em = (LONG)std::lround(em);
    f->ascent = (LONG)std::ceil(m[0]);
    f->descent = (LONG)std::ceil(m[1]);
    f->height = f->ascent + f->descent;
    f->weight = weight ? weight : FW_NORMAL;
    f->italic = italic;
    f->aveWidth = (LONG)std::lround(m[2]);
    f->maxWidth = (LONG)std::ceil(m[3]);
    return f;
}

Font* DefaultFont()
{
    static Font* f = MakeFont(-12, FW_NORMAL, false, u"");   // GDI's DEFAULT_GUI_FONT size
    return f;
}

Font* CurrentFont(DC* dc) { return (dc && dc->font) ? dc->font : DefaultFont(); }

std::u16string FromAnsi(const char* text, int count)
{
    if (!text) return std::u16string();
    if (count < 0) count = (int)std::strlen(text);
    if (count == 0) return std::u16string();
    std::u16string w((size_t)count, u'\0');
    const int n = MultiByteToWideChar(CP_ACP, 0, text, count, (WCHAR*)&w[0], count);
    w.resize(n > 0 ? (size_t)n : 0);
    return w;
}

std::u16string FromWide(const WCHAR* text, int count)
{
    if (!text) return std::u16string();
    if (count < 0) count = (int)std::char_traits<char16_t>::length((const char16_t*)text);
    return std::u16string((const char16_t*)text, (size_t)count);
}

SIZE Measure(Font* f, const std::u16string& s)
{
    SIZE size = { 0, f->height };
    if (s.empty()) return size;
    const double w = ran_gdi_measure(f->css.c_str(), s.data(), (int)s.size());
    size.cx = (LONG)std::ceil(w - 0.01);
    return size;
}

// --- drawing into the DIB ---------------------------------------------------------------------

void PutPixel(Bitmap* b, int x, int y, COLORREF c)
{
    if (x < 0 || y < 0 || x >= b->width || y >= b->height) return;
    BYTE* p = b->Pixel(x, y);
    p[0] = GetBValue(c);
    p[1] = GetGValue(c);
    p[2] = GetRValue(c);
    if (b->bpp == 32) p[3] = 0;
}

void FillRect(Bitmap* b, RECT r, COLORREF c)
{
    r.left = std::max<LONG>(r.left, 0);
    r.top = std::max<LONG>(r.top, 0);
    r.right = std::min<LONG>(r.right, b->width);
    r.bottom = std::min<LONG>(r.bottom, b->height);
    for (LONG y = r.top; y < r.bottom; ++y)
        for (LONG x = r.left; x < r.right; ++x) PutPixel(b, (int)x, (int)y, c);
}

void Blend(Bitmap* b, int x, int y, COLORREF c, unsigned coverage)
{
    if (x < 0 || y < 0 || x >= b->width || y >= b->height || !coverage) return;
    BYTE* p = b->Pixel(x, y);
    const unsigned inv = 255 - coverage;
    p[0] = (BYTE)((p[0] * inv + GetBValue(c) * coverage + 127) / 255);
    p[1] = (BYTE)((p[1] * inv + GetGValue(c) * coverage + 127) / 255);
    p[2] = (BYTE)((p[2] * inv + GetRValue(c) * coverage + 127) / 255);
    if (b->bpp == 32) p[3] = 0;
}

BOOL DrawText16(DC* dc, int x, int y, UINT options, const RECT* rect, const std::u16string& s)
{
    if (!dc) return FALSE;
    Bitmap* b = dc->bitmap;
    if (!b) return TRUE;   // nothing selected: GDI draws into the 1x1 default bitmap
    Font* f = CurrentFont(dc);
    if (dc->align & TA_BASELINE) y -= f->ascent;
    if ((options & ETO_OPAQUE) && rect) FillRect(b, *rect, dc->bk);
    const SIZE ext = Measure(f, s);
    if (dc->bkMode == OPAQUE) FillRect(b, RECT{ x, y, x + ext.cx, y + ext.cy }, dc->bk);
    if (s.empty()) return TRUE;

    // Coverage mask with a margin for overhangs (italics, accents), baseline at ascent.
    const int pad = 2 + (int)(f->em / 4);
    const int w = ext.cx + 2 * pad, h = ext.cy + 2 * pad;
    std::vector<BYTE> mask((size_t)w * h, 0);
    ran_gdi_draw(f->css.c_str(), s.data(), (int)s.size(), w, h, (float)pad, (float)(pad + f->ascent), mask.data());

    RECT clip = { 0, 0, b->width, b->height };
    if ((options & ETO_CLIPPED) && rect) {
        clip.left = std::max(clip.left, rect->left);
        clip.top = std::max(clip.top, rect->top);
        clip.right = std::min(clip.right, rect->right);
        clip.bottom = std::min(clip.bottom, rect->bottom);
    }
    for (int my = 0; my < h; ++my)
        for (int mx = 0; mx < w; ++mx) {
            const unsigned c = mask[(size_t)my * w + mx];
            const int px = x - pad + mx, py = y - pad + my;
            if (c && px >= clip.left && px < clip.right && py >= clip.top && py < clip.bottom) Blend(b, px, py, dc->text, c);
        }
    return TRUE;
}

template <class TM> void FillMetrics(Font* f, TM* tm)
{
    std::memset(tm, 0, sizeof(*tm));
    tm->tmHeight = f->height;
    tm->tmAscent = f->ascent;
    tm->tmDescent = f->descent;
    tm->tmInternalLeading = std::max<LONG>(0, f->height - f->em);
    tm->tmAveCharWidth = f->aveWidth;
    tm->tmMaxCharWidth = f->maxWidth;
    tm->tmWeight = f->weight;
    tm->tmItalic = f->italic ? 1 : 0;
    tm->tmDigitizedAspectX = tm->tmDigitizedAspectY = 96;
    tm->tmFirstChar = 0x20;
    tm->tmLastChar = 0xFF;
    tm->tmDefaultChar = '?';
    tm->tmBreakChar = ' ';
    tm->tmPitchAndFamily = VARIABLE_PITCH;
    tm->tmCharSet = HANGUL_CHARSET;
}

} // namespace

HDC CreateCompatibleDC(HDC) { return (HDC) new DC(); }

BOOL DeleteDC(HDC h)
{
    DC* dc = As<DC>(h, Kind::DC);
    if (!dc) return FALSE;
    delete dc;
    return TRUE;
}

HGDIOBJ SelectObject(HDC h, HGDIOBJ obj)
{
    DC* dc = As<DC>(h, Kind::DC);
    if (!dc || !obj) return nullptr;
    if (Bitmap* b = As<Bitmap>(obj, Kind::Bitmap)) { Bitmap* old = dc->bitmap; dc->bitmap = b; return old; }
    if (Font* f = As<Font>(obj, Kind::Font)) { Font* old = dc->font; dc->font = f; return old; }
    return nullptr;   // brushes/pens/stock objects: nothing to select in a memory DC here
}

BOOL DeleteObject(HGDIOBJ obj)
{
    Object* o = static_cast<Object*>(obj);
    if (!o || (o->kind != Kind::Bitmap && o->kind != Kind::Font)) return FALSE;
    if (o->kind == Kind::Font && o == DefaultFont()) return TRUE;
    // ?gdidump: text bitmaps (the game's glyph atlases, 256+ wide) are logged as PNG data URLs
    // when the game frees them - what the font textures are cut from.
    if (o->kind == Kind::Bitmap) {
        Bitmap* b = static_cast<Bitmap*>(o);
        if (b->width >= 256 && b->bpp == 32 && EM_ASM_INT({ return location.search.indexOf('gdidump') >= 0 ? 1 : 0; }))
            EM_ASM({
                const w = $1;
                const h = $2;
                const c = new OffscreenCanvas(w, h);
                const ctx = c.getContext('2d');
                const img = ctx.createImageData(w, h);
                for (let y = 0; y < h; y++)
                    for (let x = 0; x < w; x++) {
                        const s = $0 + ($4 ? y : h - 1 - y) * $3 + x * 4;
                        const d = (y * w + x) * 4;
                        img.data[d] = HEAPU8[s + 2]; img.data[d + 1] = HEAPU8[s + 1]; img.data[d + 2] = HEAPU8[s]; img.data[d + 3] = 255;
                    }
                ctx.putImageData(img, 0, 0);
                c.convertToBlob().then((blob) => {
                    const r = new FileReader();
                    r.onload = () => out('[gdidump] ' + w + 'x' + h + ' ' + r.result);
                    r.readAsDataURL(blob);
                });
            }, b->bits.data(), b->width, b->height, b->stride, b->topDown ? 1 : 0);
    }
    delete o;
    return TRUE;
}

HBITMAP CreateDIBSection(HDC, const void* info, UINT, void** bits, HANDLE, DWORD)
{
    if (bits) *bits = nullptr;
    const BITMAPINFOHEADER* h = info ? &((const BITMAPINFO*)info)->bmiHeader : nullptr;
    if (!h || h->biWidth <= 0 || h->biHeight == 0 || (h->biBitCount != 32 && h->biBitCount != 24) || h->biCompression != BI_RGB)
        return nullptr;
    Bitmap* b = new Bitmap();
    b->width = (int)h->biWidth;
    b->height = (int)std::labs(h->biHeight);
    b->topDown = h->biHeight < 0;
    b->bpp = h->biBitCount;
    b->stride = ((b->width * b->bpp / 8) + 3) & ~3;
    b->bits.assign((size_t)b->stride * b->height, 0);
    if (bits) *bits = b->bits.data();
    return (HBITMAP)b;
}

int SetBkMode(HDC h, int mode)
{
    DC* dc = As<DC>(h, Kind::DC);
    if (!dc) return 0;
    const int old = dc->bkMode;
    dc->bkMode = mode;
    return old;
}

COLORREF SetTextColor(HDC h, COLORREF c)
{
    DC* dc = As<DC>(h, Kind::DC);
    if (!dc) return CLR_INVALID;
    const COLORREF old = dc->text;
    dc->text = c;
    return old;
}

COLORREF SetBkColor(HDC h, COLORREF c)
{
    DC* dc = As<DC>(h, Kind::DC);
    if (!dc) return CLR_INVALID;
    const COLORREF old = dc->bk;
    dc->bk = c;
    return old;
}

UINT SetTextAlign(HDC h, UINT align)
{
    DC* dc = As<DC>(h, Kind::DC);
    if (!dc) return GDI_ERROR;
    const UINT old = dc->align;
    dc->align = align;
    return old;
}

HFONT CreateFont(int height, int, int, int, int weight, DWORD italic, DWORD, DWORD, DWORD, DWORD, DWORD, DWORD, DWORD,
                 const char* face)
{
    return (HFONT)MakeFont(height, weight, italic != 0, FromAnsi(face, -1));
}

HFONT CreateFontW(int height, int, int, int, int weight, DWORD italic, DWORD, DWORD, DWORD, DWORD, DWORD, DWORD, DWORD,
                  const WCHAR* face)
{
    return (HFONT)MakeFont(height, weight, italic != 0, FromWide(face, -1));
}

HFONT CreateFontIndirect(const LOGFONTA* lf)
{
    if (!lf) return nullptr;
    char face[LF_FACESIZE + 1] = {};
    std::memcpy(face, lf->lfFaceName, LF_FACESIZE);
    return (HFONT)MakeFont(lf->lfHeight, lf->lfWeight, lf->lfItalic != 0, FromAnsi(face, -1));
}

HFONT CreateFontIndirectW(const LOGFONTW* lf)
{
    if (!lf) return nullptr;
    WCHAR face[LF_FACESIZE + 1] = {};
    std::memcpy(face, lf->lfFaceName, sizeof(lf->lfFaceName));
    return (HFONT)MakeFont(lf->lfHeight, lf->lfWeight, lf->lfItalic != 0, FromWide(face, -1));
}

BOOL GetTextExtentPoint32(HDC h, const char* text, int count, LPSIZE size)
{
    if (!size) return FALSE;
    DC* dc = As<DC>(h, Kind::DC);
    if (!dc) return FALSE;
    *size = Measure(CurrentFont(dc), FromAnsi(text, count));
    return TRUE;
}

BOOL GetTextExtentPoint32W(HDC h, const WCHAR* text, int count, LPSIZE size)
{
    if (!size) return FALSE;
    DC* dc = As<DC>(h, Kind::DC);
    if (!dc) return FALSE;
    *size = Measure(CurrentFont(dc), FromWide(text, count));
    return TRUE;
}

BOOL GetTextMetricsA(HDC h, LPTEXTMETRICA tm)
{
    DC* dc = As<DC>(h, Kind::DC);
    if (!dc || !tm) return FALSE;
    FillMetrics(CurrentFont(dc), tm);
    return TRUE;
}

BOOL GetTextMetricsW(HDC h, LPTEXTMETRICW tm)
{
    DC* dc = As<DC>(h, Kind::DC);
    if (!dc || !tm) return FALSE;
    FillMetrics(CurrentFont(dc), tm);
    return TRUE;
}

BOOL ExtTextOut(HDC h, int x, int y, UINT options, const RECT* rect, const char* text, UINT count, const INT*)
{
    return DrawText16(As<DC>(h, Kind::DC), x, y, options, rect, FromAnsi(text, (int)count));
}

BOOL ExtTextOutW(HDC h, int x, int y, UINT options, const RECT* rect, const WCHAR* text, UINT count, const INT*)
{
    return DrawText16(As<DC>(h, Kind::DC), x, y, options, rect, FromWide(text, (int)count));
}

BOOL TextOut(HDC h, int x, int y, const char* text, int count) { return ExtTextOut(h, x, y, 0, nullptr, text, (UINT)count, nullptr); }
BOOL TextOutW(HDC h, int x, int y, const WCHAR* text, int count) { return ExtTextOutW(h, x, y, 0, nullptr, text, (UINT)count, nullptr); }
