// Win32 types and macros the game uses that DXVK's native <windows.h> does not provide.
#pragma once
#include <windows.h>
#include <cstdint>

typedef uintptr_t   DWORD_PTR, *PDWORD_PTR;
typedef uint64_t    DWORD64;
typedef uint64_t    DWORDLONG;
typedef DWORDLONG*  PDWORDLONG;
typedef int64_t     LONG64;
typedef uint64_t    ULONG64;
typedef uint8_t     BOOLEAN;
typedef unsigned char byte;     // rpcndr.h
typedef char*       PSTR;
typedef char        CCHAR;
typedef char*       PCHAR;
typedef WORD*       PWORD;
typedef uint64_t    QWORD;
typedef INT*        PINT;
typedef INT*        LPINT;
typedef UINT*       PUINT;
typedef ULONG*      PULONG;
typedef WORD        LANGID;
typedef DWORD       LCID;
typedef DWORD*      PDWORD;
typedef BYTE*       LPBYTE;
typedef BYTE*       PBYTE;
typedef WORD*       LPWORD;
typedef LONG*       LPLONG;
typedef UINT_PTR    WPARAM;
typedef LONG_PTR    LPARAM;
typedef LONG_PTR    LRESULT;
typedef char        TCHAR;              // MultiByte build
typedef char        _TCHAR;
typedef char*       LPTSTR;
typedef const char* LPCTSTR;
typedef void*       HICON;
typedef void*       HCURSOR;
typedef void*       HBRUSH;
typedef void*       HMENU;
typedef void*       HFONT;
typedef void*       HBITMAP;
typedef void*       HPEN;
typedef void*       HGDIOBJ;
typedef void*       HRGN;
typedef void*       HACCEL;
typedef void*       HGLOBAL;
typedef void*       HLOCAL;
typedef void*       HIMC;
typedef void*       HKL;
typedef LRESULT (WINAPI *WNDPROC)(HWND, UINT, WPARAM, LPARAM);
typedef BOOL*       LPBOOL;
typedef double      DOUBLE;
typedef GUID*       LPGUID;
typedef IUnknown*   LPUNKNOWN;
typedef GUID        CLSID;
typedef CLSID*      LPCLSID;

typedef struct _FILETIME { DWORD dwLowDateTime; DWORD dwHighDateTime; } FILETIME, *PFILETIME, *LPFILETIME;
typedef struct _SYSTEMTIME {
    WORD wYear, wMonth, wDayOfWeek, wDay, wHour, wMinute, wSecond, wMilliseconds;
} SYSTEMTIME, *PSYSTEMTIME, *LPSYSTEMTIME;

// COM stream interface: D3DX only takes it by pointer; the game never implements it.
struct IStream : public IUnknown {};
typedef IStream* LPSTREAM;

typedef struct _POINTFLOAT { FLOAT x, y; } POINTFLOAT, *LPPOINTFLOAT;
typedef struct _GLYPHMETRICSFLOAT {
    FLOAT gmfBlackBoxX, gmfBlackBoxY;
    POINTFLOAT gmfptGlyphOrigin;
    FLOAT gmfCellIncX, gmfCellIncY;
} GLYPHMETRICSFLOAT, *LPGLYPHMETRICSFLOAT;

#ifndef STDAPI
#define STDAPI              extern "C" HRESULT STDMETHODCALLTYPE
#endif
#ifndef STDAPI_
#define STDAPI_(type)       extern "C" type STDMETHODCALLTYPE
#endif
#ifndef STDMETHODIMP
#define STDMETHODIMP        HRESULT STDMETHODCALLTYPE
#endif
#ifndef DECLARE_INTERFACE_IID_
#define DECLARE_INTERFACE_IID_(iface, base, iid) DECLARE_INTERFACE_(iface, base)
#endif
#ifndef STDMETHODIMP_
#define STDMETHODIMP_(type) type STDMETHODCALLTYPE
#endif

#ifndef _TCHAR_DEFINED
#define _TCHAR_DEFINED
#endif

#ifndef MAKEWORD
#define MAKEWORD(a, b)  ((WORD)(((BYTE)((DWORD_PTR)(a) & 0xff)) | ((WORD)((BYTE)((DWORD_PTR)(b) & 0xff))) << 8))
#endif
#ifndef MAKELONG
#define MAKELONG(a, b)  ((LONG)(((WORD)((DWORD_PTR)(a) & 0xffff)) | ((DWORD)((WORD)((DWORD_PTR)(b) & 0xffff))) << 16))
#endif
#ifndef LOWORD
#define LOWORD(l)       ((WORD)((DWORD_PTR)(l) & 0xffff))
#endif
#ifndef HIWORD
#define HIWORD(l)       ((WORD)((DWORD_PTR)(l) >> 16))
#endif
#ifndef LOBYTE
#define LOBYTE(w)       ((BYTE)((DWORD_PTR)(w) & 0xff))
#endif
#ifndef HIBYTE
#define HIBYTE(w)       ((BYTE)((DWORD_PTR)(w) >> 8))
#endif
#ifndef RGB
#define RGB(r, g, b)    ((COLORREF)(((BYTE)(r) | ((WORD)((BYTE)(g)) << 8)) | (((DWORD)(BYTE)(b)) << 16)))
#endif
#ifndef GetRValue
#define GetRValue(rgb)  (LOBYTE(rgb))
#define GetGValue(rgb)  (LOBYTE(((WORD)(rgb)) >> 8))
#define GetBValue(rgb)  (LOBYTE((rgb) >> 16))
#endif
#ifndef MAX_PATH
#define MAX_PATH 260
#endif
// MSVC <stdlib.h> path component limits.
#define _MAX_PATH  260
#define _MAX_DRIVE 3
#define _MAX_DIR   256
#define _MAX_FNAME 256
#define _MAX_EXT   256

#ifdef __cplusplus
#include <type_traits>
// Windows' min/max. Functions instead of windows.h's macros: libc++ #undefs min/max macros
// inside its own headers, which would make them vanish part-way through a translation unit.
template <class A, class B>
constexpr std::common_type_t<A, B> min(A a, B b) { return (b < a) ? b : a; }
template <class A, class B>
constexpr std::common_type_t<A, B> max(A a, B b) { return (a < b) ? b : a; }
#endif

// Language identifiers.
#define MAKELANGID(p, s)    ((((WORD)(s)) << 10) | (WORD)(p))
#define PRIMARYLANGID(lgid) ((WORD)(lgid) & 0x3ff)
#define SUBLANGID(lgid)     ((WORD)(lgid) >> 10)
#define LANG_NEUTRAL        0x00
#define LANG_CHINESE        0x04
#define LANG_ENGLISH        0x09
#define LANG_JAPANESE       0x11
#define LANG_KOREAN         0x12
#define LANG_THAI           0x1e
#define LANG_VIETNAMESE     0x2a
#define SUBLANG_DEFAULT     0x01
#define SUBLANG_CHINESE_TRADITIONAL 0x01
#define SUBLANG_CHINESE_SIMPLIFIED  0x02
#define SUBLANG_KOREAN      0x01

#ifndef GUID_NULL
inline const GUID GUID_NULL = {0, 0, 0, {0, 0, 0, 0, 0, 0, 0, 0}};
#endif
#ifndef OPTIONAL
#define OPTIONAL
#endif
#ifndef IN
#define IN
#endif
#ifndef OUT
#define OUT
#endif
#ifndef FAR
#define FAR
#endif
#ifndef PASCAL
#define PASCAL
#endif
#ifndef NEAR
#define NEAR
#endif
#ifndef CALLBACK
#define CALLBACK
#endif
#ifndef APIENTRY
#define APIENTRY
#endif
#ifndef __cdecl
#define __cdecl
#endif
#ifndef __forceinline
#define __forceinline inline __attribute__((always_inline))
#endif
#ifndef __max
#define __max(a, b) (((a) > (b)) ? (a) : (b))
#endif
#ifndef __min
#define __min(a, b) (((a) < (b)) ? (a) : (b))
#endif
#ifndef _countof
#define _countof(a) (sizeof(a) / sizeof((a)[0]))
#endif
#ifndef _ASSERT
#include <cassert>
#define _ASSERT(e)  assert(e)
#define _ASSERTE(e) assert(e)
#endif
#ifndef UNREFERENCED_PARAMETER
#define UNREFERENCED_PARAMETER(p) (void)(p)
#endif
