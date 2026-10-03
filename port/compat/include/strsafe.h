// Stand-in for <strsafe.h> (native macOS build). Same contract as Windows: results are always
// NUL-terminated, truncation returns STRSAFE_E_INSUFFICIENT_BUFFER with the truncated text in
// the buffer. Wide variants work on 2-byte WCHAR (-fshort-wchar) without libc's 4-byte wide
// functions; the wide printf follows MSVC's W-printf rules (%s = wide, %S = narrow).
#pragma once
#include "ran_compat.h"
#include <cstdarg>
#include <cstdio>
#include <cstring>
#include <string>

#define STRSAFE_E_INSUFFICIENT_BUFFER ((HRESULT)0x8007007AL)
#define STRSAFE_E_INVALID_PARAMETER   ((HRESULT)0x80070057L)
#define STRSAFE_E_END_OF_FILE         ((HRESULT)0x80070026L)
#define STRSAFE_MAX_CCH 2147483647

// ---- narrow ----
inline HRESULT StringCchCopyN(char* dst, size_t cch, const char* src, size_t maxSrc)
{
    if (!dst || cch == 0 || cch > STRSAFE_MAX_CCH) return STRSAFE_E_INVALID_PARAMETER;
    size_t i = 0;
    for (; i < maxSrc && src[i]; ++i) {
        if (i + 1 >= cch) { dst[i] = 0; return STRSAFE_E_INSUFFICIENT_BUFFER; }
        dst[i] = src[i];
    }
    dst[i] = 0;
    return S_OK;
}
inline HRESULT StringCchCopy(char* dst, size_t cch, const char* src) { return StringCchCopyN(dst, cch, src, (size_t)-1); }
inline HRESULT StringCchCat(char* dst, size_t cch, const char* src)
{
    if (!dst || cch == 0 || cch > STRSAFE_MAX_CCH) return STRSAFE_E_INVALID_PARAMETER;
    const size_t used = strnlen(dst, cch);
    if (used >= cch) return STRSAFE_E_INVALID_PARAMETER;
    return StringCchCopy(dst + used, cch - used, src);
}
inline HRESULT StringCchVPrintf(char* dst, size_t cch, const char* fmt, va_list ap)
{
    if (!dst || cch == 0 || cch > STRSAFE_MAX_CCH) return STRSAFE_E_INVALID_PARAMETER;
    const int n = std::vsnprintf(dst, cch, fmt, ap);
    if (n < 0) { dst[0] = 0; return STRSAFE_E_INVALID_PARAMETER; }
    return (size_t)n >= cch ? STRSAFE_E_INSUFFICIENT_BUFFER : S_OK;
}
inline HRESULT StringCchPrintf(char* dst, size_t cch, const char* fmt, ...)
{
    va_list ap;
    va_start(ap, fmt);
    const HRESULT hr = StringCchVPrintf(dst, cch, fmt, ap);
    va_end(ap);
    return hr;
}
inline HRESULT StringCchLength(const char* s, size_t maxCch, size_t* len)
{
    if (!s || maxCch > STRSAFE_MAX_CCH) return STRSAFE_E_INVALID_PARAMETER;
    const size_t n = strnlen(s, maxCch);
    if (n >= maxCch) return STRSAFE_E_INVALID_PARAMETER;
    if (len) *len = n;
    return S_OK;
}
inline HRESULT StringCbCopy(char* dst, size_t cb, const char* src) { return StringCchCopy(dst, cb, src); }
inline HRESULT StringCbCat(char* dst, size_t cb, const char* src) { return StringCchCat(dst, cb, src); }
inline HRESULT StringCbVPrintf(char* dst, size_t cb, const char* fmt, va_list ap) { return StringCchVPrintf(dst, cb, fmt, ap); }
inline HRESULT StringCbPrintf(char* dst, size_t cb, const char* fmt, ...)
{
    va_list ap;
    va_start(ap, fmt);
    const HRESULT hr = StringCchVPrintf(dst, cb, fmt, ap);
    va_end(ap);
    return hr;
}
inline HRESULT StringCbLength(const char* s, size_t maxCb, size_t* len) { return StringCchLength(s, maxCb, len); }

#define StringCchCopyA     StringCchCopy
#define StringCchCopyNA    StringCchCopyN
#define StringCchCatA      StringCchCat
#define StringCchPrintfA   StringCchPrintf
#define StringCchVPrintfA  StringCchVPrintf
#define StringCchLengthA   StringCchLength
#define StringCbCopyA      StringCbCopy
#define StringCbCatA       StringCbCat
#define StringCbPrintfA    StringCbPrintf
#define StringCbVPrintfA   StringCbVPrintf

// ---- wide (2-byte WCHAR) ----
inline HRESULT StringCchCopyNW(WCHAR* dst, size_t cch, const WCHAR* src, size_t maxSrc)
{
    if (!dst || cch == 0 || cch > STRSAFE_MAX_CCH) return STRSAFE_E_INVALID_PARAMETER;
    size_t i = 0;
    for (; i < maxSrc && src[i]; ++i) {
        if (i + 1 >= cch) { dst[i] = 0; return STRSAFE_E_INSUFFICIENT_BUFFER; }
        dst[i] = src[i];
    }
    dst[i] = 0;
    return S_OK;
}
inline HRESULT StringCchCopyW(WCHAR* dst, size_t cch, const WCHAR* src) { return StringCchCopyNW(dst, cch, src, (size_t)-1); }
inline HRESULT StringCchLengthW(const WCHAR* s, size_t maxCch, size_t* len)
{
    if (!s || maxCch > STRSAFE_MAX_CCH) return STRSAFE_E_INVALID_PARAMETER;
    size_t n = 0;
    while (n < maxCch && s[n]) ++n;
    if (n >= maxCch) return STRSAFE_E_INVALID_PARAMETER;
    if (len) *len = n;
    return S_OK;
}
inline HRESULT StringCchCatW(WCHAR* dst, size_t cch, const WCHAR* src)
{
    size_t used = 0;
    if (!dst || StringCchLengthW(dst, cch, &used) != S_OK) return STRSAFE_E_INVALID_PARAMETER;
    return StringCchCopyW(dst + used, cch - used, src);
}

namespace ran_compat {
    // MSVC W-printf: %s/%c take wide, %S/%C narrow, %hs narrow, %ls wide. Numbers go through
    // the narrow vsnprintf one conversion at a time, then widen (they are ASCII).
    inline std::u16string WideVFormat(const WCHAR* fmt, va_list ap)
    {
        std::u16string out;
        auto widen = [&](const char* s) { while (*s) out += (char16_t)(unsigned char)*s++; };
        for (const WCHAR* p = fmt; *p; ++p) {
            if (*p != L'%') { out += (char16_t)*p; continue; }
            if (p[1] == L'%') { out += u'%'; ++p; continue; }
            std::string spec = "%";
            ++p;
            while (*p && std::strchr("-+ #0123456789.*", (char)*p)) {
                if (*p == L'*') spec += std::to_string(va_arg(ap, int));
                else spec += (char)*p;
                ++p;
            }
            std::string len;
            while (*p == L'h' || *p == L'l' || *p == L'L' || *p == L'I' || *p == L'z' ||
                   *p == L'6' || *p == L'4' || *p == L'3' || *p == L'2') { len += (char)*p; ++p; }
            const char conv = (char)*p;
            if (!conv) break;
            char tmp[512];
            switch (conv) {
            case 's': case 'S': {
                const bool narrow = (conv == 'S' && len != "l") || len == "h";
                if (narrow) { const char* s = va_arg(ap, const char*); widen(s ? s : "(null)"); }
                else { const WCHAR* s = va_arg(ap, const WCHAR*); if (s) while (*s) out += (char16_t)*s++; }
                break;
            }
            case 'c': case 'C': out += (char16_t)va_arg(ap, int); break;
            case 'd': case 'i': case 'u': case 'x': case 'X': case 'o': {
                const bool wide64 = len == "ll" || len == "I64" || len == "l" || len == "z" || len == "I";
                if (wide64) std::snprintf(tmp, sizeof(tmp), (spec + "ll" + conv).c_str(), va_arg(ap, long long));
                else std::snprintf(tmp, sizeof(tmp), (spec + conv).c_str(), va_arg(ap, int));
                widen(tmp);
                break;
            }
            case 'f': case 'F': case 'e': case 'E': case 'g': case 'G': case 'a': case 'A':
                std::snprintf(tmp, sizeof(tmp), (spec + conv).c_str(), va_arg(ap, double));
                widen(tmp);
                break;
            case 'p':
                std::snprintf(tmp, sizeof(tmp), "%p", va_arg(ap, void*));
                widen(tmp);
                break;
            default:
                out += (char16_t)conv;
                break;
            }
        }
        return out;
    }
}

inline HRESULT StringCchVPrintfW(WCHAR* dst, size_t cch, const WCHAR* fmt, va_list ap)
{
    if (!dst || cch == 0 || cch > STRSAFE_MAX_CCH) return STRSAFE_E_INVALID_PARAMETER;
    const std::u16string s = ran_compat::WideVFormat(fmt, ap);
    const size_t n = s.size() < cch ? s.size() : cch - 1;
    for (size_t i = 0; i < n; ++i) dst[i] = (WCHAR)s[i];
    dst[n] = 0;
    return s.size() >= cch ? STRSAFE_E_INSUFFICIENT_BUFFER : S_OK;
}
inline HRESULT StringCchPrintfW(WCHAR* dst, size_t cch, const WCHAR* fmt, ...)
{
    va_list ap;
    va_start(ap, fmt);
    const HRESULT hr = StringCchVPrintfW(dst, cch, fmt, ap);
    va_end(ap);
    return hr;
}
inline HRESULT StringCbCopyW(WCHAR* dst, size_t cb, const WCHAR* src) { return StringCchCopyW(dst, cb / sizeof(WCHAR), src); }
inline HRESULT StringCbCatW(WCHAR* dst, size_t cb, const WCHAR* src) { return StringCchCatW(dst, cb / sizeof(WCHAR), src); }
inline HRESULT StringCbPrintfW(WCHAR* dst, size_t cb, const WCHAR* fmt, ...)
{
    va_list ap;
    va_start(ap, fmt);
    const HRESULT hr = StringCchVPrintfW(dst, cb / sizeof(WCHAR), fmt, ap);
    va_end(ap);
    return hr;
}
