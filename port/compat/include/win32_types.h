// Fixed-size Win32 base types for the native macOS build.
// Sizes match the 32-bit Windows build (see port/tests/compat_types_test.cpp).
#pragma once
#include <cstdint>

typedef uint8_t   BYTE;
typedef uint16_t  WORD;
typedef uint32_t  DWORD;
typedef int32_t   LONG;
typedef uint32_t  ULONG;
typedef int32_t   BOOL;
typedef int32_t   INT;
typedef uint32_t  UINT;
typedef int64_t   LONGLONG;
typedef uint64_t  ULONGLONG;
typedef int64_t   __time64_t;
typedef int32_t   HRESULT;
typedef wchar_t   WCHAR;   // 2 bytes: the build uses -fshort-wchar

#ifndef TRUE
#define TRUE 1
#endif
#ifndef FALSE
#define FALSE 0
#endif
#define S_OK            ((HRESULT)0)
#define S_FALSE         ((HRESULT)1)
#define E_FAIL          ((HRESULT)0x80004005L)
#define SUCCEEDED(hr)   (((HRESULT)(hr)) >= 0)
#define FAILED(hr)      (((HRESULT)(hr)) < 0)
#define MAX_PATH        260
