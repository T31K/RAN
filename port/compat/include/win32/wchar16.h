// CRT wide-string functions for the 2-byte WCHAR (char16_t) of the native macOS build.
// On Windows wchar_t is 2 bytes and the game calls wcslen/wcscpy/_wcsicmp... on WCHAR text.
// macOS wchar_t is 4 bytes, so these overloads sit next to the libc wchar_t versions (which
// stay untouched) and pick themselves for WCHAR arguments. Comparisons are on unsigned code
// units like the Windows CRT; case folding covers ASCII like _wcsicmp in the "C" locale.
#pragma once
#include <cstdlib>
#include <cwchar>
#include <string>

namespace ran_compat {
    inline char16_t FoldAscii16(char16_t c) { return (c >= u'A' && c <= u'Z') ? (char16_t)(c - u'A' + u'a') : c; }
    // ASCII digits/signs/hex/exponents only, which is all a number can contain: narrows the
    // numeric prefix so the CRT parser can do the work, and maps the end pointer back.
    inline std::string NarrowNumber16(const char16_t* s, size_t* used)
    {
        std::string out;
        size_t i = 0;
        while (s[i] && s[i] < 0x80) { out += (char)s[i]; ++i; }
        *used = i;
        return out;
    }
}

inline size_t wcslen(const char16_t* s) { size_t n = 0; while (s[n]) ++n; return n; }
inline char16_t* wcscpy(char16_t* d, const char16_t* s) { char16_t* r = d; while ((*d++ = *s++)) {} return r; }
inline char16_t* wcsncpy(char16_t* d, const char16_t* s, size_t n)
{
    size_t i = 0;
    for (; i < n && s[i]; ++i) d[i] = s[i];
    for (; i < n; ++i) d[i] = 0;
    return d;
}
inline char16_t* wcscat(char16_t* d, const char16_t* s) { wcscpy(d + wcslen(d), s); return d; }
inline int wcsncmp(const char16_t* a, const char16_t* b, size_t n)
{
    for (size_t i = 0; i < n; ++i) {
        if (a[i] != b[i]) return a[i] < b[i] ? -1 : 1;
        if (!a[i]) return 0;
    }
    return 0;
}
inline int wcscmp(const char16_t* a, const char16_t* b) { return wcsncmp(a, b, (size_t)-1); }
inline int _wcsnicmp(const char16_t* a, const char16_t* b, size_t n)
{
    for (size_t i = 0; i < n; ++i) {
        const char16_t x = ran_compat::FoldAscii16(a[i]), y = ran_compat::FoldAscii16(b[i]);
        if (x != y) return x < y ? -1 : 1;
        if (!x) return 0;
    }
    return 0;
}
inline int _wcsicmp(const char16_t* a, const char16_t* b) { return _wcsnicmp(a, b, (size_t)-1); }
#define wcsicmp _wcsicmp
#define _wcsicmp_l(a, b, loc) _wcsicmp(a, b)
inline const char16_t* wcschr(const char16_t* s, char16_t c) { for (;; ++s) { if (*s == c) return s; if (!*s) return nullptr; } }
inline char16_t* wcschr(char16_t* s, char16_t c) { return const_cast<char16_t*>(wcschr((const char16_t*)s, c)); }
inline const char16_t* wcsrchr(const char16_t* s, char16_t c)
{
    const char16_t* last = nullptr;
    for (;; ++s) { if (*s == c) last = s; if (!*s) return last; }
}
inline char16_t* wcsrchr(char16_t* s, char16_t c) { return const_cast<char16_t*>(wcsrchr((const char16_t*)s, c)); }
inline const char16_t* wcsstr(const char16_t* s, const char16_t* sub)
{
    const size_t n = wcslen(sub);
    if (!n) return s;
    for (; *s; ++s) if (wcsncmp(s, sub, n) == 0) return s;
    return nullptr;
}
inline char16_t* wcsstr(char16_t* s, const char16_t* sub) { return const_cast<char16_t*>(wcsstr((const char16_t*)s, sub)); }

inline long wcstol(const char16_t* s, char16_t** end, int base)
{
    size_t used = 0;
    const std::string n = ran_compat::NarrowNumber16(s, &used);
    char* e = nullptr;
    const long v = std::strtol(n.c_str(), &e, base);
    if (end) *end = const_cast<char16_t*>(s) + (e - n.c_str());
    return v;
}
inline unsigned long wcstoul(const char16_t* s, char16_t** end, int base)
{
    size_t used = 0;
    const std::string n = ran_compat::NarrowNumber16(s, &used);
    char* e = nullptr;
    const unsigned long v = std::strtoul(n.c_str(), &e, base);
    if (end) *end = const_cast<char16_t*>(s) + (e - n.c_str());
    return v;
}
inline double wcstod(const char16_t* s, char16_t** end)
{
    size_t used = 0;
    const std::string n = ran_compat::NarrowNumber16(s, &used);
    char* e = nullptr;
    const double v = std::strtod(n.c_str(), &e);
    if (end) *end = const_cast<char16_t*>(s) + (e - n.c_str());
    return v;
}
inline int _wtoi(const char16_t* s) { return (int)wcstol(s, nullptr, 10); }
inline long _wtol(const char16_t* s) { return wcstol(s, nullptr, 10); }
inline double _wtof(const char16_t* s) { return wcstod(s, nullptr); }

inline int lstrcmpW(const char16_t* a, const char16_t* b) { return wcscmp(a, b); }
inline int lstrcmpiW(const char16_t* a, const char16_t* b) { return _wcsicmp(a, b); }
inline char16_t* lstrcpyW(char16_t* d, const char16_t* s) { return wcscpy(d, s); }
inline char16_t* lstrcatW(char16_t* d, const char16_t* s) { return wcscat(d, s); }
