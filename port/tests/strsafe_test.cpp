// Behaviour tests for the <strsafe.h> stand-in, including the 2-byte wide variants
// (the build uses -fshort-wchar, so libc's 4-byte wide functions must not be used).
#include <strsafe.h>
#include <cstdio>
#include <cstring>

static int g_failed = 0;
#define CHECK(cond) do { if (!(cond)) { std::printf("FAIL %s:%d  %s\n", __FILE__, __LINE__, #cond); ++g_failed; } } while (0)

static bool WEq(const WCHAR* a, const char* ascii)
{
    for (; *ascii; ++a, ++ascii) if (*a != (WCHAR)(unsigned char)*ascii) return false;
    return *a == 0;
}

static HRESULT VPrintfHelper(char* dst, size_t cch, const char* fmt, ...)
{
    va_list ap;
    va_start(ap, fmt);
    const HRESULT hr = StringCchVPrintf(dst, cch, fmt, ap);
    va_end(ap);
    return hr;
}

int main()
{
    static_assert(sizeof(WCHAR) == 2, "build must use -fshort-wchar");

    // Narrow copy / cat / truncation.
    char buf[8];
    CHECK(StringCchCopy(buf, 8, "abc") == S_OK && std::strcmp(buf, "abc") == 0);
    CHECK(StringCchCat(buf, 8, "def") == S_OK && std::strcmp(buf, "abcdef") == 0);
    CHECK(StringCchCat(buf, 8, "XYZ") == STRSAFE_E_INSUFFICIENT_BUFFER && std::strcmp(buf, "abcdefX") == 0);
    CHECK(StringCchCopy(buf, 8, "0123456789") == STRSAFE_E_INSUFFICIENT_BUFFER && std::strcmp(buf, "0123456") == 0);
    CHECK(StringCchCopyN(buf, 8, "hello", 2) == S_OK && std::strcmp(buf, "he") == 0);
    CHECK(StringCbCopy(buf, sizeof(buf), "cb") == S_OK && std::strcmp(buf, "cb") == 0);
    CHECK(StringCchCopy(buf, 0, "x") == STRSAFE_E_INVALID_PARAMETER);

    // Printf.
    CHECK(StringCchPrintf(buf, 8, "%d-%s", 42, "ab") == S_OK && std::strcmp(buf, "42-ab") == 0);
    CHECK(StringCchPrintf(buf, 8, "%s", "toolongvalue") == STRSAFE_E_INSUFFICIENT_BUFFER && std::strcmp(buf, "toolong") == 0);
    CHECK(VPrintfHelper(buf, 8, "%03d", 7) == S_OK && std::strcmp(buf, "007") == 0);
    CHECK(StringCbPrintf(buf, sizeof(buf), "%c%c", 'o', 'k') == S_OK && std::strcmp(buf, "ok") == 0);

    size_t len = 0;
    CHECK(StringCchLength("hello", 100, &len) == S_OK && len == 5);
    CHECK(StringCchLength("hello", 3, &len) == STRSAFE_E_INVALID_PARAMETER);

    // Wide (2-byte) variants.
    WCHAR w[16];
    const WCHAR src[] = {'h', 'i', 0};
    CHECK(StringCchCopyW(w, 16, src) == S_OK && WEq(w, "hi"));
    const WCHAR more[] = {'!', '!', 0};
    CHECK(StringCchCatW(w, 16, more) == S_OK && WEq(w, "hi!!"));
    CHECK(StringCchLengthW(w, 16, &len) == S_OK && len == 4);
    WCHAR tiny[3];
    CHECK(StringCchCopyW(tiny, 3, w) == STRSAFE_E_INSUFFICIENT_BUFFER && WEq(tiny, "hi"));

    // Wide printf: %s is a wide string, %S a narrow one (MSVC W-printf rules), numbers as usual.
    const WCHAR name[] = {'R', 'A', 'N', 0};
    CHECK(StringCchPrintfW(w, 16, L"%s:%d", name, 7) == S_OK && WEq(w, "RAN:7"));
    CHECK(StringCchPrintfW(w, 16, L"%S/%.1f/%x", "ep", 2.5, 255) == S_OK && WEq(w, "ep/2.5/ff"));
    CHECK(StringCchPrintfW(w, 16, L"100%%") == S_OK && WEq(w, "100%"));
    CHECK(StringCchPrintfW(tiny, 3, L"%d", 12345) == STRSAFE_E_INSUFFICIENT_BUFFER && WEq(tiny, "12"));

    if (g_failed == 0) std::printf("PASS strsafe_test\n");
    return g_failed == 0 ? 0 : 1;
}
