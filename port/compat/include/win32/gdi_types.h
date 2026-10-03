// GDI font types/constants used by the game's font code and D3DX's ID3DXFont declarations
// (native macOS build). Text is rasterised with CoreText in Phase 3; these keep the
// declarations and the data that flows through them intact.
#pragma once
#include <windows.h>

#ifndef LF_FACESIZE
#define LF_FACESIZE 32
#endif

typedef struct tagTEXTMETRICA {
    LONG tmHeight, tmAscent, tmDescent, tmInternalLeading, tmExternalLeading;
    LONG tmAveCharWidth, tmMaxCharWidth, tmWeight, tmOverhang;
    LONG tmDigitizedAspectX, tmDigitizedAspectY;
    BYTE tmFirstChar, tmLastChar, tmDefaultChar, tmBreakChar;
    BYTE tmItalic, tmUnderlined, tmStruckOut, tmPitchAndFamily, tmCharSet;
} TEXTMETRICA, *LPTEXTMETRICA;
typedef TEXTMETRICA TEXTMETRIC;
typedef LPTEXTMETRICA LPTEXTMETRIC;

typedef struct tagTEXTMETRICW {
    LONG tmHeight, tmAscent, tmDescent, tmInternalLeading, tmExternalLeading;
    LONG tmAveCharWidth, tmMaxCharWidth, tmWeight, tmOverhang;
    LONG tmDigitizedAspectX, tmDigitizedAspectY;
    WCHAR tmFirstChar, tmLastChar, tmDefaultChar, tmBreakChar;
    BYTE tmItalic, tmUnderlined, tmStruckOut, tmPitchAndFamily, tmCharSet;
} TEXTMETRICW, *LPTEXTMETRICW;
typedef TEXTMETRICA TEXTMETRIC;
typedef LPTEXTMETRICA LPTEXTMETRIC;

typedef struct tagLOGFONTA {
    LONG lfHeight, lfWidth, lfEscapement, lfOrientation, lfWeight;
    BYTE lfItalic, lfUnderline, lfStrikeOut, lfCharSet;
    BYTE lfOutPrecision, lfClipPrecision, lfQuality, lfPitchAndFamily;
    char lfFaceName[LF_FACESIZE];
} LOGFONTA, *LPLOGFONTA;
typedef struct tagLOGFONTW {
    LONG lfHeight, lfWidth, lfEscapement, lfOrientation, lfWeight;
    BYTE lfItalic, lfUnderline, lfStrikeOut, lfCharSet;
    BYTE lfOutPrecision, lfClipPrecision, lfQuality, lfPitchAndFamily;
    WCHAR lfFaceName[LF_FACESIZE];
} LOGFONTW, *LPLOGFONTW;
typedef LOGFONTA LOGFONT;
typedef LPLOGFONTA LPLOGFONT;

#define FW_DONTCARE   0
#define FW_THIN       100
#define FW_LIGHT      300
#define FW_NORMAL     400
#define FW_MEDIUM     500
#define FW_SEMIBOLD   600
#define FW_BOLD       700
#define FW_HEAVY      900

#define ANSI_CHARSET        0
#define DEFAULT_CHARSET     1
#define SHIFTJIS_CHARSET    128
#define HANGUL_CHARSET      129
#define GB2312_CHARSET      134
#define CHINESEBIG5_CHARSET 136
#define THAI_CHARSET        222
#define VIETNAMESE_CHARSET  163
#define HEBREW_CHARSET      177
#define ARABIC_CHARSET      178
#define RUSSIAN_CHARSET     204

#define OUT_DEFAULT_PRECIS   0
#define OUT_TT_PRECIS        4
#define CLIP_DEFAULT_PRECIS  0
#define DEFAULT_QUALITY      0
#define ANTIALIASED_QUALITY  4
#define NONANTIALIASED_QUALITY 3
#define DEFAULT_PITCH        0
#define FIXED_PITCH          1
#define VARIABLE_PITCH       2
#define FF_DONTCARE          0

inline void* CreateFontIndirect(const LOGFONTA*) { return nullptr; }   // GDI fonts: CoreText in Phase 3
#define CreateFontIndirectA CreateFontIndirect

// GDI text rasterisation used by d3dfont.cpp / D3DFontX.cpp to bake glyph textures. Phase 3
// replaces these with CoreText (same metrics contract: extents in pixels, glyphs drawn into the
// DIB section); until then they measure nothing and draw nothing.
#ifndef ETO_OPAQUE
#define ETO_OPAQUE  0x0002
#define ETO_CLIPPED 0x0004
#endif
inline HFONT CreateFont(int, int, int, int, int, DWORD, DWORD, DWORD, DWORD, DWORD, DWORD, DWORD, DWORD, const char*) { return nullptr; }
#define CreateFontA CreateFont
inline HFONT CreateFontW(int, int, int, int, int, DWORD, DWORD, DWORD, DWORD, DWORD, DWORD, DWORD, DWORD, const WCHAR*) { return nullptr; }
inline BOOL GetTextExtentPoint32(HDC, const char*, int, LPSIZE size) { if (size) { size->cx = 0; size->cy = 0; } return FALSE; }
inline BOOL GetTextExtentPoint32W(HDC, const WCHAR*, int, LPSIZE size) { if (size) { size->cx = 0; size->cy = 0; } return FALSE; }
#define GetTextExtentPoint32A GetTextExtentPoint32
inline BOOL ExtTextOut(HDC, int, int, UINT, const RECT*, const char*, UINT, const INT*) { return FALSE; }
inline BOOL ExtTextOutW(HDC, int, int, UINT, const RECT*, const WCHAR*, UINT, const INT*) { return FALSE; }
#define ExtTextOutA ExtTextOut
inline BOOL TextOutW(HDC, int, int, const WCHAR*, int) { return FALSE; }
inline COLORREF SetTextColor(HDC, COLORREF) { return 0; }
inline COLORREF SetBkColor(HDC, COLORREF) { return 0; }
