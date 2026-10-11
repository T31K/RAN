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

    // Converts the whole input. Invalid/unmappable input is handled like Windows without
    // MB_ERR_INVALID_CHARS: the bad unit is replaced and conversion carries on (returns false
    // only if `strict`). `fromWide` selects how a bad unit is skipped and what replaces it:
    //   multibyte -> UTF-16: a DBCS lead byte with a usable trail (>= 0x41) is skipped as a pair,
    //                        anything else as one byte; replaced by U+30FB, the default Unicode
    //                        character of the Windows DBCS code-page tables.
    //   UTF-16 -> multibyte: one UTF-16 unit (two for a surrogate pair) becomes '?', and
    //                        *usedDefault is set, like WideCharToMultiByte's lpUsedDefaultChar.
    inline bool Iconv(const char* from, const char* to, const char* in, size_t inBytes, std::vector<char>& out,
                      bool fromWide = false, bool strict = false, BOOL* usedDefault = nullptr)
    {
        iconv_t cd = ::iconv_open(to, from);
        if (cd == (iconv_t)-1) return false;
        out.assign(inBytes * 4 + 16, 0);
        char* src = const_cast<char*>(in);
        char* dst = out.data();
        size_t srcLeft = inBytes, dstLeft = out.size();
        while (srcLeft > 0) {
            if (::iconv(cd, &src, &srcLeft, &dst, &dstLeft) != (size_t)-1) break;
            if (errno != EILSEQ && errno != EINVAL) { ::iconv_close(cd); return false; }   // E2BIG cannot happen (4x)
            if (strict) { ::iconv_close(cd); return false; }
            size_t skip;
            if (fromWide) {
                const unsigned u = srcLeft >= 2 ? ((unsigned char)src[0] | ((unsigned char)src[1] << 8)) : 0;
                skip = (u >= 0xD800 && u <= 0xDBFF && srcLeft >= 4) ? 4 : (srcLeft >= 2 ? 2 : srcLeft);
                *dst++ = '?'; --dstLeft;
                if (usedDefault) *usedDefault = TRUE;
            } else {
                const unsigned char lead = (unsigned char)src[0];
                const unsigned char trail = srcLeft >= 2 ? (unsigned char)src[1] : 0;
                skip = (lead >= 0x81 && lead <= 0xFE && trail >= 0x41) ? 2 : 1;
                *dst++ = (char)0xFB; *dst++ = (char)0x30; dstLeft -= 2;   // U+30FB, little-endian
            }
            src += skip; srcLeft -= skip;
            ::iconv(cd, nullptr, nullptr, nullptr, nullptr);   // reset shift state
        }
        ::iconv_close(cd);
        out.resize(out.size() - dstLeft);
        return true;
    }

#ifdef __EMSCRIPTEN__
    // musl's iconv (the browser build) decodes the DBCS code pages but cannot encode to them.
    // The encoder is a reverse table built once by decoding every one- and two-byte code:
    // entry = 0 (unmapped), 0x10000 | byte, or 0x20000 | lead << 8 | trail.
    inline const std::vector<uint32_t>* DbcsTable(const char* name)
    {
        static std::vector<std::pair<std::string, std::vector<uint32_t>>> tables;
        for (auto& t : tables)
            if (t.first == name) return t.second.empty() ? nullptr : &t.second;
        tables.push_back({ name, {} });
        iconv_t cd = ::iconv_open("UTF-16LE", name);
        if (cd == (iconv_t)-1) return nullptr;
        std::vector<uint32_t>& t = tables.back().second;
        t.assign(65536, 0);
        auto decode = [&](const unsigned char* in, size_t n) -> int {
            char16_t u[4];
            char* src = (char*)in;
            char* dst = (char*)u;
            size_t srcLeft = n, dstLeft = sizeof(u);
            ::iconv(cd, nullptr, nullptr, nullptr, nullptr);
            if (::iconv(cd, &src, &srcLeft, &dst, &dstLeft) == (size_t)-1 || srcLeft || dstLeft != sizeof(u) - 2) return -1;
            return u[0] == 0xFFFD ? -1 : (int)u[0];
        };
        for (unsigned b = 0; b < 0x100; ++b) {
            const unsigned char in[1] = { (unsigned char)b };
            const int u = decode(in, 1);
            if (u >= 0 && !t[u]) t[u] = 0x10000u | b;
        }
        for (unsigned lead = 0x81; lead <= 0xFE; ++lead)
            for (unsigned trail = 0x40; trail <= 0xFE; ++trail) {
                const unsigned char in[2] = { (unsigned char)lead, (unsigned char)trail };
                const int u = decode(in, 2);
                if (u >= 0 && !t[u]) t[u] = 0x20000u | (lead << 8) | trail;
            }
        ::iconv_close(cd);
        return &t;
    }

    // UTF-16 -> a DBCS code page with the table; false if the code page has none.
    inline bool EncodeDbcs(const char* name, const char16_t* src, size_t units, std::vector<char>& out, BOOL* usedDefault)
    {
        const std::vector<uint32_t>* t = DbcsTable(name);
        if (!t) return false;
        out.clear();
        for (size_t i = 0; i < units; ++i) {
            const uint32_t e = (*t)[src[i]];
            if (src[i] == 0) out.push_back(0);
            else if (e >> 16 == 1) out.push_back((char)(e & 0xFF));
            else if (e >> 16 == 2) { out.push_back((char)((e >> 8) & 0xFF)); out.push_back((char)(e & 0xFF)); }
            else {
                out.push_back('?');
                if (usedDefault) *usedDefault = TRUE;
                if (src[i] >= 0xD800 && src[i] <= 0xDBFF && i + 1 < units) ++i;   // surrogate pair
            }
        }
        return true;
    }
#endif
}
#ifndef ERROR_NO_UNICODE_TRANSLATION
#define ERROR_NO_UNICODE_TRANSLATION 1113u
#endif

inline int MultiByteToWideChar(UINT cp, DWORD flags, const char* src, int srcLen, WCHAR* dst, int dstLen)
{
    if (!src) return 0;
    const bool withNul = srcLen < 0;
    const size_t inBytes = withNul ? std::strlen(src) : (size_t)srcLen;
    std::vector<char> out;
    if (!ran_compat::Iconv(ran_compat::IconvName(cp), "UTF-16LE", src, inBytes, out, false, (flags & MB_ERR_INVALID_CHARS) != 0)) {
        SetLastError(ERROR_NO_UNICODE_TRANSLATION);
        return 0;
    }
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
#ifdef __EMSCRIPTEN__
    const char* name = ran_compat::IconvName(cp);
    const bool dbcs = std::strcmp(name, "UTF-8") != 0 && std::strcmp(name, "CP1252") != 0 && std::strcmp(name, "CP1258") != 0;
    if (dbcs && ran_compat::EncodeDbcs(name, (const char16_t*)src, units, out, usedDefault)) {
        // encoded with the reverse table
    } else
#endif
    if (!ran_compat::Iconv("UTF-16LE", ran_compat::IconvName(cp), (const char*)src, units * 2, out, true, false, usedDefault)) return 0;
    const int bytes = (int)out.size() + (withNul ? 1 : 0);
    if (dstLen == 0) return bytes;
    if (!dst || dstLen < bytes) { SetLastError(ERROR_INSUFFICIENT_BUFFER); return 0; }
    std::memcpy(dst, out.data(), out.size());
    if (withNul) dst[bytes - 1] = 0;
    return bytes;
}
