// Behaviour tests for the MFC CString / CStringArray stand-ins (MultiByte build, CP949 bytes).
#include "mfc/afx_string.h"
#include <cstdio>
#include <cstring>

static int g_failed = 0;
#define CHECK(cond) do { if (!(cond)) { std::printf("FAIL %s:%d  %s\n", __FILE__, __LINE__, #cond); ++g_failed; } } while (0)

// A CP949 double-byte character whose trail byte is an ASCII letter (0xB0 0x61 = lead + 'a'), then 'x'.
static const char kDbcsWithAsciiTrail[] = { (char)0xB0, 'a', 'x', 0 };

int main()
{
    // Construction, length, conversion.
    CString s("hello");
    CHECK(s.GetLength() == 5);
    CHECK(std::strcmp(s.GetString(), "hello") == 0);
    CHECK(std::strcmp((LPCTSTR)s, "hello") == 0);
    CHECK(!s.IsEmpty());
    CString e;
    CHECK(e.IsEmpty() && e.GetLength() == 0 && e.GetString()[0] == 0);

    // Format / AppendFormat.
    CString f;
    f.Format("%d-%s-%.1f", 7, "ab", 1.5);
    CHECK(f == "7-ab-1.5");
    f.AppendFormat("|%03d", 5);
    CHECK(f == "7-ab-1.5|005");
    CString big;
    big.Format("%s", std::string(5000, 'z').c_str());
    CHECK(big.GetLength() == 5000);

    // Element access.
    CHECK(s.GetAt(1) == 'e');
    CHECK(s[4] == 'o');
    s.SetAt(0, 'j');
    CHECK(s == "jello");

    // Substrings.
    CString t("abcdef");
    CHECK(t.Mid(2) == "cdef");
    CHECK(t.Mid(2, 2) == "cd");
    CHECK(t.Mid(10) == "");
    CHECK(t.Left(3) == "abc");
    CHECK(t.Right(2) == "ef");
    CHECK(t.Left(99) == "abcdef");

    // Searching.
    CHECK(t.Find('c') == 2);
    CHECK(t.Find("de") == 3);
    CHECK(t.Find('z') == -1);
    CHECK(t.Find('a', 1) == -1);
    CHECK(CString("a/b/c").ReverseFind('/') == 3);
    CHECK(t.FindOneOf("xyd") == 3);

    // DBCS: trail byte 'a' must not be found or case-mapped (MFC MultiByte uses _mbs* functions).
    CString k(kDbcsWithAsciiTrail);
    CHECK(k.Find('a') == -1);
    CHECK(k.Find('x') == 2);
    k.MakeUpper();
    CHECK((unsigned char)k[0] == 0xB0 && k[1] == 'a' && k[2] == 'X');

    // Case.
    CString c("MiXeD");
    c.MakeLower();
    CHECK(c == "mixed");
    c.MakeUpper();
    CHECK(c == "MIXED");
    CHECK(CString("Hello").CompareNoCase("hELLO") == 0);
    CHECK(CString("a").Compare("b") < 0);

    // Trim.
    CString w("  pad \t\n");
    w.Trim();
    CHECK(w == "pad");
    CString wl("  x  ");
    wl.TrimLeft();
    CHECK(wl == "x  ");
    wl.TrimRight();
    CHECK(wl == "x");
    CString wc("xxhixx");
    wc.Trim('x');
    CHECK(wc == "hi");

    // Replace / Remove / Insert / Delete.
    CString r("a.b.c");
    CHECK(r.Replace(".", "::") == 2);
    CHECK(r == "a::b::c");
    CHECK(r.Replace(':', '-') == 4);
    CHECK(r == "a--b--c");
    CHECK(r.Remove('-') == 4);
    CHECK(r == "abc");
    r.Insert(1, "XY");
    CHECK(r == "aXYbc");
    r.Insert(0, '_');
    CHECK(r == "_aXYbc");
    r.Delete(1, 3);
    CHECK(r == "_bc");
    r.Empty();
    CHECK(r.IsEmpty());

    // Tokenize: same contract as MFC (start becomes -1 after the last token).
    CString tok("a,,b;c");
    int pos = 0;
    CString t1 = tok.Tokenize(",;", pos);
    CString t2 = tok.Tokenize(",;", pos);
    CString t3 = tok.Tokenize(",;", pos);
    CString t4 = tok.Tokenize(",;", pos);
    CHECK(t1 == "a" && t2 == "b" && t3 == "c" && t4 == "" && pos == -1);
    CHECK(CString("abc;def").SpanExcluding(";") == "abc");

    // GetBuffer / ReleaseBuffer.
    CString b;
    char* p = b.GetBuffer(16);
    std::strcpy(p, "raw");
    b.ReleaseBuffer();
    CHECK(b == "raw" && b.GetLength() == 3);
    p = b.GetBuffer(16);
    p[0] = 'R';
    b.ReleaseBuffer(2);
    CHECK(b == "Ra");

    // Operators.
    CString x = CString("ab") + "cd";
    x += 'e';
    x += CString("f");
    CHECK(x == "abcdef");
    CHECK("z" + CString("y") == "zy");
    CHECK(CString("a") < CString("b"));
    CHECK(x != "nope");

    // CStringArray.
    CStringArray arr;
    CHECK(arr.Add("one") == 0);
    arr.Add("two");
    arr.Add(CString("three"));
    CHECK(arr.GetSize() == 3 && arr.GetCount() == 3);
    CHECK(arr.GetAt(1) == "two" && arr[2] == "three");
    arr.InsertAt(0, "zero");
    CHECK(arr[0] == "zero" && arr.GetSize() == 4);
    arr.RemoveAt(1);
    CHECK(arr[1] == "two");
    arr.SetAt(0, "Z");
    CHECK(arr[0] == "Z");
    arr.SetSize(5);
    CHECK(arr.GetSize() == 5 && arr[4].IsEmpty());
    arr.RemoveAll();
    CHECK(arr.GetSize() == 0);

    // CMapStringToString.
    CMapStringToString map;
    CHECK(map.IsEmpty() && map.GetCount() == 0);
    map.SetAt("k1", "v1");
    map["k2"] = "v2";
    CString got;
    CHECK(map.Lookup("k1", got) && got == "v1");
    CHECK(map.Lookup("k2", got) && got == "v2");
    CHECK(!map.Lookup("nope", got));
    int seen = 0;
    for (POSITION pos = map.GetStartPosition(); pos != NULL; ) {
        CString key, val;
        map.GetNextAssoc(pos, key, val);
        CHECK((key == "k1" && val == "v1") || (key == "k2" && val == "v2"));
        ++seen;
    }
    CHECK(seen == 2);
    CHECK(map.RemoveKey("k1") && !map.RemoveKey("k1") && map.GetCount() == 1);
    map.RemoveAll();
    CHECK(map.IsEmpty());

    // CMapStringToPtr (same contract, void* values).
    CMapStringToPtr ptrs;
    int target = 7;
    ptrs.SetAt("x", &target);
    void* pv = nullptr;
    CHECK(ptrs.Lookup("x", pv) && pv == &target && !ptrs.Lookup("y", pv));
    POSITION pp = ptrs.GetStartPosition();
    CString pk;
    void* pval = nullptr;
    ptrs.GetNextAssoc(pp, pk, pval);
    CHECK(pk == "x" && pval == &target && pp == NULL);

    // CArray / CUIntArray.
    CArray<int, int> ia;
    CHECK(ia.Add(5) == 0 && ia.Add(6) == 1 && ia.GetSize() == 2 && ia[1] == 6);
    ia.SetAt(0, 9);
    ia.InsertAt(1, 8);
    CHECK(ia.GetAt(0) == 9 && ia[1] == 8 && ia[2] == 6);
    ia.RemoveAt(0);
    CHECK(ia.GetSize() == 2 && ia.GetData()[0] == 8);
    ia.SetSize(4);
    CHECK(ia.GetSize() == 4 && ia.GetUpperBound() == 3);
    ia.RemoveAll();
    CHECK(ia.IsEmpty());
    CUIntArray ua;
    ua.Add(3u);
    CHECK(ua.GetCount() == 1 && ua[0] == 3u);

    // CStringList.
    CStringList list;
    list.AddTail("b");
    list.AddTail("c");
    list.AddHead("a");
    CHECK(list.GetCount() == 3 && list.GetHead() == "a" && list.GetTail() == "c");
    CString joined;
    for (POSITION pos = list.GetHeadPosition(); pos != NULL; ) joined += list.GetNext(pos);
    CHECK(joined == "abc");
    CHECK(list.RemoveHead() == "a" && list.GetCount() == 2);
    list.RemoveAll();
    CHECK(list.IsEmpty());

    if (g_failed == 0) std::printf("PASS cstring_test\n");
    return g_failed == 0 ? 0 : 1;
}
