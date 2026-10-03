// Behaviour tests for MultiByteToWideChar/WideCharToMultiByte (CP949 <-> UTF-16, UTF-8) and
// the CStringW stand-in. Korean text must survive the round trip byte-for-byte.
#include "ran_compat.h"
#include <cstdio>
#include <cstring>

static int g_failed = 0;
#define CHECK(cond) do { if (!(cond)) { std::printf("FAIL %s:%d  %s\n", __FILE__, __LINE__, #cond); ++g_failed; } } while (0)

// "한글A" in CP949, UTF-16 and UTF-8.
static const char kCp949[] = { (char)0xC7, (char)0xD1, (char)0xB1, (char)0xDB, 'A', 0 };
static const WCHAR kUtf16[] = { 0xD55C, 0xAE00, 'A', 0 };
static const char kUtf8[] = { (char)0xED, (char)0x95, (char)0x9C, (char)0xEA, (char)0xB8, (char)0x80, 'A', 0 };

int main()
{
    // Size query with -1 counts the terminator (Windows contract).
    CHECK(MultiByteToWideChar(CP_ACP, 0, kCp949, -1, nullptr, 0) == 4);

    WCHAR w[16] = {};
    CHECK(MultiByteToWideChar(CP_ACP, 0, kCp949, -1, w, 16) == 4);
    CHECK(std::memcmp(w, kUtf16, sizeof(kUtf16)) == 0);

    // Explicit length, no terminator written or counted.
    WCHAR w2[16] = {};
    CHECK(MultiByteToWideChar(949, 0, kCp949, 4, w2, 16) == 2 && w2[0] == 0xD55C && w2[1] == 0xAE00 && w2[2] == 0);

    // Back to CP949.
    char a[16] = {};
    CHECK(WideCharToMultiByte(CP_ACP, 0, kUtf16, -1, nullptr, 0, nullptr, nullptr) == 6);
    CHECK(WideCharToMultiByte(CP_ACP, 0, kUtf16, -1, a, 16, nullptr, nullptr) == 6);
    CHECK(std::strcmp(a, kCp949) == 0);

    // UTF-8 both ways.
    WCHAR w3[16] = {};
    CHECK(MultiByteToWideChar(CP_UTF8, 0, kUtf8, -1, w3, 16) == 4 && std::memcmp(w3, kUtf16, sizeof(kUtf16)) == 0);
    char u[16] = {};
    CHECK(WideCharToMultiByte(CP_UTF8, 0, kUtf16, -1, u, 16, nullptr, nullptr) == 8 && std::strcmp(u, kUtf8) == 0);

    // Too-small buffer fails (returns 0) like Windows.
    WCHAR tiny[2];
    CHECK(MultiByteToWideChar(CP_ACP, 0, kCp949, -1, tiny, 2) == 0);

    // CStringW basics on 2-byte WCHAR.
    CStringW s(kUtf16);
    CHECK(s.GetLength() == 3);
    CHECK(s.GetAt(0) == 0xD55C && s[2] == 'A');
    CHECK(std::memcmp((const WCHAR*)s, kUtf16, sizeof(kUtf16)) == 0);
    CStringW t = s.Left(2);
    CHECK(t.GetLength() == 2 && t.Mid(1).GetAt(0) == 0xAE00);
    t += u'B';
    CHECK(t.GetLength() == 3 && t.Right(1)[0] == 'B');
    t.Insert(0, u'X');
    CHECK(t[0] == 'X' && t.GetLength() == 4);
    t.Delete(0, 1);
    CHECK(t[0] == 0xD55C);
    CHECK(t.Find(0xAE00) == 1 && t.Find('Z') == -1);
    CStringW copy = t;
    CHECK(copy == t && !(copy != t));
    t.Empty();
    CHECK(t.IsEmpty() && t.GetString()[0] == 0);
    CStringW lit(u"abc");
    CHECK(lit.GetLength() == 3 && lit[1] == 'b');

    // Invalid CP949 (real game data has some): replaced, conversion carries on - nothing after
    // the bad bytes may be lost. "A" + C9 21 (lead byte + ASCII trail) + "B" + FF + "\xC7\xD1" (한).
    {
        const char bad[] = "A\xC9!B\xFF\xC7\xD1";
        WCHAR w[16] = {};
        const int n = MultiByteToWideChar(CP_ACP, 0, bad, -1, w, 16);
        CHECK(n == 7);   // A, U+30FB, !, B, U+30FB, 한, NUL
        CHECK(w[0] == 'A' && w[1] == 0x30FB && w[2] == '!' && w[3] == 'B' && w[4] == 0x30FB && w[5] == 0xD55C && w[6] == 0);
        // Strict mode reports the error instead.
        CHECK(MultiByteToWideChar(CP_ACP, MB_ERR_INVALID_CHARS, bad, -1, w, 16) == 0);
        CHECK(GetLastError() == ERROR_NO_UNICODE_TRANSLATION);
        // A character CP949 cannot encode becomes '?' and reports the default char.
        const WCHAR emoji[] = { 'x', 0xD83D, 0xDE00, 'y', 0 };
        char mb[16] = {};
        BOOL usedDefault = FALSE;
        CHECK(WideCharToMultiByte(CP_ACP, 0, emoji, -1, mb, 16, nullptr, &usedDefault) == 4);
        CHECK(std::string(mb) == "x?y" && usedDefault == TRUE);
    }

    if (g_failed == 0) std::printf("PASS codepage_test\n");
    return g_failed == 0 ? 0 : 1;
}
