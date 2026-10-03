// Behaviour tests for the 2-byte WCHAR (char16_t) string functions: same results as the Windows
// CRT on UTF-16 text, including Korean (code units >= U+8000 must not be sign-mangled).
#include "ran_compat.h"
#include <cstdio>
#include <cwchar>

static int g_failed = 0;
#define CHECK(cond) do { if (!(cond)) { std::printf("FAIL %s:%d  %s\n", __FILE__, __LINE__, #cond); ++g_failed; } } while (0)

int main()
{
    const WCHAR* hello = u"Hello 한글 World";   // "Hello 한글 World"
    CHECK(wcslen(hello) == 14);
    CHECK(wcslen(u"") == 0);

    WCHAR buf[32] = {};
    CHECK(wcscpy(buf, hello) == buf && wcscmp(buf, hello) == 0);
    CHECK(wcscat(buf, u"!") == buf && wcslen(buf) == 15 && buf[14] == u'!');
    WCHAR small[6] = {};
    wcsncpy(small, hello, 5);
    CHECK(wcscmp(small, u"Hello") == 0);

    CHECK(wcscmp(u"abc", u"abd") < 0 && wcscmp(u"abd", u"abc") > 0);
    CHECK(wcscmp(u"한", u"a") > 0);            // unsigned compare, like Windows
    CHECK(wcsncmp(u"abcX", u"abcY", 3) == 0);
    CHECK(_wcsicmp(u"HeLLo", u"hello") == 0 && _wcsicmp(u"a", u"B") < 0);
    CHECK(_wcsnicmp(u"DATA\\x", u"data/y", 4) == 0);
    CHECK(lstrcmpW(u"same", u"same") == 0 && lstrcmpiW(u"SAME", u"same") == 0);

    CHECK(wcschr(hello, u'글') == hello + 7);
    CHECK(wcschr(hello, u'z') == nullptr);
    const WCHAR* path = u"a\\b\\c.dds";
    CHECK(wcsrchr(path, u'\\') == path + 3);
    CHECK(wcsstr(hello, u"한글") == hello + 6);
    CHECK(wcsstr(hello, u"nope") == nullptr);

    CHECK(_wtoi(u"  -42xyz") == -42);
    WCHAR* end = nullptr;
    CHECK(wcstol(u"0x1F rest", &end, 16) == 31 && end && *end == u' ');
    CHECK(wcstoul(u"4000000000", nullptr, 10) == 4000000000UL);
    CHECK(wcstod(u"2.5e1", nullptr) == 25.0);

    // The libc wchar_t versions are still there and untouched.
    CHECK(std::wcslen(L"abc") == 3);

    if (g_failed) { std::printf("%d check(s) failed\n", g_failed); return 1; }
    std::printf("wchar16_test: all checks passed\n");
    return 0;
}
