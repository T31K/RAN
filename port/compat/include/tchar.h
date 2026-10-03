// Stand-in for <tchar.h> (native macOS build). The game is MultiByte, so TCHAR is char and
// every _tcs*/_t* name maps to its char function.
#pragma once
#include "ran_compat.h"
#include <cctype>
#include <cstdlib>
#include <cstring>
#include <ctime>

#define _T(x)       x
#define _TEXT(x)    x
#ifndef TEXT
#define TEXT(x)     x
#endif

#define _tcscpy     std::strcpy
#define _tcsncpy    std::strncpy
#define _tcscat     std::strcat
#define _tcsncat    std::strncat
#define _tcslen     std::strlen
#define _tcscmp     std::strcmp
#define _tcsncmp    std::strncmp
#define _tcsicmp    _stricmp
#define _tcsnicmp   _strnicmp
#define _tcschr     std::strchr
#define _tcsrchr    std::strrchr
#define _tcsstr     std::strstr
#define _tcstok     std::strtok
#define _tcsdup     _strdup
#define _tcstol     std::strtol
#define _tcstoul    std::strtoul
#define _tcstod     std::strtod
#define _tcsftime   std::strftime
#define _tcscpy_s   strcpy_s
#define _tcscat_s   strcat_s
#define _tcsncpy_s  strncpy_s
#define _stprintf   std::sprintf
#define _stprintf_s sprintf_s
#define _sntprintf  std::snprintf
#define _vsntprintf std::vsnprintf
#define _tprintf    std::printf
#define _ttoi       std::atoi
#define _tstoi      std::atoi
#define _ttol       std::atol
#define _tstof      std::atof
#define _ttoi64     _atoi64
#define _istdigit   std::isdigit
#define _istspace   std::isspace
#define _istalpha   std::isalpha
#define _totlower   std::tolower
#define _totupper   std::toupper
#define _tfopen     ran_compat::fopen_resolved
#define _tmain      main

inline char* _strdup(const char* s) { return ::strdup(s); }

inline char* _strrev(char* s)
{
    if (!s) return s;
    for (char *a = s, *b = s + std::strlen(s) - 1; a < b; ++a, --b) { const char t = *a; *a = *b; *b = t; }
    return s;
}

inline errno_t _strlwr_s(char* s, size_t size)
{
    for (size_t i = 0; s && i < size && s[i]; ++i) {
        if (ran_compat::IsLeadByte(s[i])) { ++i; continue; }   // keep CP949 trail bytes intact
        s[i] = (char)std::tolower((unsigned char)s[i]);
    }
    return 0;
}
template <size_t N> inline errno_t _strlwr_s(char (&s)[N]) { return _strlwr_s(s, N); }

inline errno_t _itoa_s(int value, char* buf, size_t size, int radix)
{
    if (!buf || size == 0 || radix < 2 || radix > 36) return 22;
    char tmp[34];
    unsigned int v = (radix == 10 && value < 0) ? (unsigned int)(-(long long)value) : (unsigned int)value;
    int n = 0;
    do { const int d = (int)(v % (unsigned)radix); tmp[n++] = (char)(d < 10 ? '0' + d : 'a' + d - 10); v /= (unsigned)radix; } while (v);
    if (radix == 10 && value < 0) tmp[n++] = '-';
    if ((size_t)n >= size) { buf[0] = 0; return 34; }
    for (int i = 0; i < n; ++i) buf[i] = tmp[n - 1 - i];
    buf[n] = 0;
    return 0;
}
template <size_t N> inline errno_t _itoa_s(int value, char (&buf)[N], int radix) { return _itoa_s(value, buf, N, radix); }
