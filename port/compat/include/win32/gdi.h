// GDI device contexts, DIB sections and text for the native build: what the game's font code
// uses to bake glyph textures (d3dfont.cpp builds its glyph atlas with CreateCompatibleDC +
// CreateDIBSection + CreateFont + ExtTextOut, then reads the DIB bits; TextTexture.cpp and the
// IME do the same). Implemented with CoreText in port/platform/gdi_coretext.cpp:
//   - a memory DC draws into the DIB section selected into it (32 or 24 bits per pixel, top-down
//     or bottom-up); pixels are written as 0x00RRGGBB like GDI (the top byte stays 0),
//   - text is antialiased coverage from CoreText, blended in the text colour; with the default
//     OPAQUE background mode the text cell is filled with the background colour first,
//   - fonts: lfHeight < 0 is the em height in pixels, > 0 the cell height (ascent + descent),
//     as in GDI at 96 dpi; Korean face names (Gulim/Dotum/Batang/...) map to Apple SD Gothic Neo
//     and CoreText's fallback covers any script the chosen face lacks,
//   - text arguments of the A functions are CP949, like the game.
// Window DCs (GetDC) stay null in the native build.
#pragma once
#include <windows.h>
#include "extra_types.h"
#include "gdi_types.h"

#ifndef CLR_INVALID
#define CLR_INVALID 0xFFFFFFFF
#endif
#ifndef GDI_ERROR
#define GDI_ERROR 0xFFFFFFFFu
#endif
#ifndef TA_TOP
#define TA_TOP      0
#define TA_LEFT     0
#define TA_BASELINE 24
#endif

HDC CreateCompatibleDC(HDC);
BOOL DeleteDC(HDC);
HGDIOBJ SelectObject(HDC, HGDIOBJ);
BOOL DeleteObject(HGDIOBJ);
HBITMAP CreateDIBSection(HDC, const void* bitmapInfo, UINT usage, void** bits, HANDLE section, DWORD offset);
int SetBkMode(HDC, int mode);
COLORREF SetTextColor(HDC, COLORREF);
COLORREF SetBkColor(HDC, COLORREF);
UINT SetTextAlign(HDC, UINT align);

HFONT CreateFont(int height, int width, int escapement, int orientation, int weight, DWORD italic, DWORD underline,
                 DWORD strikeOut, DWORD charSet, DWORD outPrecision, DWORD clipPrecision, DWORD quality,
                 DWORD pitchAndFamily, const char* face);
HFONT CreateFontW(int height, int width, int escapement, int orientation, int weight, DWORD italic, DWORD underline,
                  DWORD strikeOut, DWORD charSet, DWORD outPrecision, DWORD clipPrecision, DWORD quality,
                  DWORD pitchAndFamily, const WCHAR* face);
HFONT CreateFontIndirect(const LOGFONTA*);
HFONT CreateFontIndirectW(const LOGFONTW*);
#define CreateFontA CreateFont
#define CreateFontIndirectA CreateFontIndirect

BOOL GetTextExtentPoint32(HDC, const char* text, int count, LPSIZE size);
BOOL GetTextExtentPoint32W(HDC, const WCHAR* text, int count, LPSIZE size);
#define GetTextExtentPoint32A GetTextExtentPoint32
BOOL GetTextMetricsA(HDC, LPTEXTMETRICA);   // d3dx9core.h later #defines GetTextMetrics to the A name
BOOL GetTextMetricsW(HDC, LPTEXTMETRICW);
inline BOOL GetTextMetrics(HDC dc, LPTEXTMETRICA tm) { return GetTextMetricsA(dc, tm); }
BOOL ExtTextOut(HDC, int x, int y, UINT options, const RECT* rect, const char* text, UINT count, const INT* dx);
BOOL ExtTextOutW(HDC, int x, int y, UINT options, const RECT* rect, const WCHAR* text, UINT count, const INT* dx);
#define ExtTextOutA ExtTextOut
BOOL TextOut(HDC, int x, int y, const char* text, int count);
BOOL TextOutW(HDC, int x, int y, const WCHAR* text, int count);
#define TextOutA TextOut
