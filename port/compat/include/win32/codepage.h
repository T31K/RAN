// MultiByteToWideChar / WideCharToMultiByte for the native macOS build, via iconv.
// CP_ACP is the game's Windows "ANSI" code page: CP949 (Korean) by default, switchable with
// ran_compat::SetAnsiCodePage for other client regions. WCHAR is 2-byte UTF-16 (char16_t).
#pragma once
#include "win32/kernel.h"
#include <cerrno>
#include <cstring>
#include <iconv.h>
#include <vector>

#ifndef CP_ACP
#define CP_ACP     0
#endif
#ifndef CP_OEMCP
#define CP_OEMCP   1
#endif
#ifndef CP_THREAD_ACP
#define CP_THREAD_ACP 3
#endif
#ifndef CP_UTF8
#define CP_UTF8    65001
#endif
#ifndef MB_PRECOMPOSED
#define MB_PRECOMPOSED 0x00000001
#endif
#ifndef MB_ERR_INVALID_CHARS
#define MB_ERR_INVALID_CHARS 0x00000008
#endif
#ifndef ERROR_INSUFFICIENT_BUFFER
#define ERROR_INSUFFICIENT_BUFFER 122u
#endif

namespace ran_compat {

    inline UINT& AnsiCodePage() { static UINT cp = 949; return cp; }
    inline void SetAnsiCodePage(UINT cp) { AnsiCodePage() = cp; }

    inline const char* IconvName(UINT cp)
    {
        if (cp == CP_ACP || cp == CP_OEMCP || cp == CP_THREAD_ACP) cp = AnsiCodePage();
        switch (cp) {
        case CP_UTF8: return "UTF-8";
        case 949:     return "CP949";
        case 874:     return "CP874";
        case 932:     return "CP932";
        case 936:     return "CP936";
        case 950:     return "CP950";
        case 1252:    return "CP1252";
        case 1258:    return "CP1258";
        default:      return "CP949";
        }
    }

    // Converts the whole input; returns false on invalid input.
    inline bool Iconv(const char* from, const char* to, const char* in, size_t inBytes, std::vector<char>& out)
    {
        iconv_t cd = ::iconv_open(to, from);
        if (cd == (iconv_t)-1) return false;
        out.assign(inBytes * 4 + 16, 0);
        char* src = const_cast<char*>(in);
        char* dst = out.data();
        size_t srcLeft = inBytes, dstLeft = out.size();
        const size_t rc = ::iconv(cd, &src, &srcLeft, &dst, &dstLeft);
        ::iconv_close(cd);
        if (rc == (size_t)-1) return false;
        out.resize(out.size() - dstLeft);
        return true;
    }
}

inline int MultiByteToWideChar(UINT cp, DWORD, const char* src, int srcLen, WCHAR* dst, int dstLen)
{
    if (!src) return 0;
    const bool withNul = srcLen < 0;
    const size_t inBytes = withNul ? std::strlen(src) : (size_t)srcLen;
    std::vector<char> out;
    if (!ran_compat::Iconv(ran_compat::IconvName(cp), "UTF-16LE", src, inBytes, out)) return 0;
    const int units = (int)(out.size() / 2) + (withNul ? 1 : 0);
    if (dstLen == 0) return units;
    if (!dst || dstLen < units) { SetLastError(ERROR_INSUFFICIENT_BUFFER); return 0; }
    std::memcpy(dst, out.data(), out.size());
    if (withNul) dst[units - 1] = 0;
    return units;
}

inline int WideCharToMultiByte(UINT cp, DWORD, const WCHAR* src, int srcLen, char* dst, int dstLen,
                               const char* /*defaultChar*/, BOOL* usedDefault)
{
    if (usedDefault) *usedDefault = FALSE;
    if (!src) return 0;
    const bool withNul = srcLen < 0;
    size_t units = 0;
    if (withNul) { while (src[units]) ++units; } else units = (size_t)srcLen;
    std::vector<char> out;
    if (!ran_compat::Iconv("UTF-16LE", ran_compat::IconvName(cp), (const char*)src, units * 2, out)) return 0;
    const int bytes = (int)out.size() + (withNul ? 1 : 0);
    if (dstLen == 0) return bytes;
    if (!dst || dstLen < bytes) { SetLastError(ERROR_INSUFFICIENT_BUFFER); return 0; }
    std::memcpy(dst, out.data(), out.size());
    if (withNul) dst[bytes - 1] = 0;
    return bytes;
}
