// GDI memory DCs, DIB sections, fonts and text on CoreText (declarations and contract:
// port/compat/include/win32/gdi.h). Tested by port/tests/gdi_text_test.cpp.
#include "ran_compat.h"
#pragma push_macro("interface")   // COM's `#define interface struct` breaks an IOKit field name
#undef interface
#include <CoreText/CoreText.h>
#include <CoreGraphics/CoreGraphics.h>
#pragma pop_macro("interface")
#include <algorithm>
#include <cmath>
#include <cstring>
#include <string>
#include <vector>

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
    ~Font() override { if (ct) CFRelease(ct); }
    CTFontRef ct = nullptr;
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

CFStringRef MakeCFString(const std::u16string& s)
{
    return CFStringCreateWithCharacters(nullptr, (const UniChar*)s.data(), (CFIndex)s.size());
}

// The face CoreText should use for a GDI face name: the font itself when macOS has it,
// otherwise Apple SD Gothic Neo (Korean UI font; Latin glyphs included).
CTFontRef OpenFace(const std::u16string& face, CGFloat size, bool bold)
{
    if (!IsKoreanFace(face)) {
        CFStringRef name = MakeCFString(face);
        CTFontRef f = CTFontCreateWithName(name, size, nullptr);
        bool match = false;
        if (f) {
            CFStringRef family = CTFontCopyFamilyName(f);
            CFStringRef full = CTFontCopyFullName(f);
            match = (family && CFStringCompare(family, name, kCFCompareCaseInsensitive) == kCFCompareEqualTo) ||
                    (full && CFStringCompare(full, name, kCFCompareCaseInsensitive) == kCFCompareEqualTo);
            if (family) CFRelease(family);
            if (full) CFRelease(full);
        }
        CFRelease(name);
        if (match) return f;
        if (f) CFRelease(f);
    }
    return CTFontCreateWithName(bold ? CFSTR("AppleSDGothicNeo-Bold") : CFSTR("AppleSDGothicNeo-Regular"), size, nullptr);
}

Font* MakeFont(LONG height, LONG weight, bool italic, const std::u16string& face)
{
    const bool bold = weight >= FW_SEMIBOLD;
    // Find the em size: GDI's negative height is the em height, positive the cell height.
    CGFloat em = 12;
    if (height < 0) em = (CGFloat)-height;
    else if (height > 0) {
        CTFontRef probe = OpenFace(face, 100, bold);
        const CGFloat cell = CTFontGetAscent(probe) + CTFontGetDescent(probe);
        CFRelease(probe);
        em = cell > 0 ? (CGFloat)height * 100 / cell : (CGFloat)height;
    }
    CTFontRef base = OpenFace(face, em, bold);
    CTFontSymbolicTraits traits = 0;
    if (bold) traits |= kCTFontBoldTrait;
    if (italic) traits |= kCTFontItalicTrait;
    if (traits) {
        if (CTFontRef styled = CTFontCreateCopyWithSymbolicTraits(base, em, nullptr, traits, traits)) {
            CFRelease(base);
            base = styled;
        }
    }
    Font* f = new Font();
    f->ct = base;
    f->em = (LONG)std::lround(em);
    f->ascent = (LONG)std::ceil(CTFontGetAscent(base));
    f->descent = (LONG)std::ceil(CTFontGetDescent(base));
    f->height = f->ascent + f->descent;
    f->weight = weight ? weight : FW_NORMAL;
    f->italic = italic;
    UniChar x = 'x';
    CGGlyph g = 0;
    CGSize adv = {};
    if (CTFontGetGlyphsForCharacters(base, &x, &g, 1)) CTFontGetAdvancesForGlyphs(base, kCTFontOrientationHorizontal, &g, &adv, 1);
    f->aveWidth = (LONG)std::lround(adv.width);
    f->maxWidth = (LONG)std::ceil(CTFontGetBoundingBox(base).size.width);
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

CTLineRef MakeLine(Font* f, const std::u16string& s)
{
    CFStringRef str = MakeCFString(s);
    const void* keys[] = { kCTFontAttributeName, kCTForegroundColorFromContextAttributeName };
    const void* values[] = { f->ct, kCFBooleanTrue };
    CFDictionaryRef attrs = CFDictionaryCreate(nullptr, keys, values, 2, &kCFTypeDictionaryKeyCallBacks, &kCFTypeDictionaryValueCallBacks);
    CFAttributedStringRef astr = CFAttributedStringCreate(nullptr, str, attrs);
    CTLineRef line = CTLineCreateWithAttributedString(astr);
    CFRelease(astr);
    CFRelease(attrs);
    CFRelease(str);
    return line;
}

SIZE Measure(Font* f, const std::u16string& s)
{
    SIZE size = { 0, f->height };
    if (s.empty()) return size;
    CTLineRef line = MakeLine(f, s);
    const double w = CTLineGetTypographicBounds(line, nullptr, nullptr, nullptr);
    CFRelease(line);
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
    CGColorSpaceRef gray = CGColorSpaceCreateDeviceGray();
    CGContextRef ctx = CGBitmapContextCreate(mask.data(), (size_t)w, (size_t)h, 8, (size_t)w, gray, kCGImageAlphaNone);
    CGColorSpaceRelease(gray);
    if (!ctx) return FALSE;
    CGContextSetGrayFillColor(ctx, 1.0, 1.0);
    CGContextSetShouldAntialias(ctx, true);
    CGContextSetShouldSmoothFonts(ctx, false);
    CGContextSetTextMatrix(ctx, CGAffineTransformIdentity);
    CGContextSetTextPosition(ctx, pad, pad + f->descent);   // CG origin is bottom-left
    CTLineRef line = MakeLine(f, s);
    CTLineDraw(line, ctx);
    CFRelease(line);
    CGContextRelease(ctx);

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
