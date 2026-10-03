// MFC CString / CStringArray stand-ins for the native macOS build.
// The game is built MultiByte: CString holds CP949 bytes, TCHAR is char. Character-level
// operations skip CP949 trail bytes the way MFC's _mbs* functions do, so Korean text is
// never split or case-mapped.
#pragma once
#include <windows.h>
#include <cctype>
#include <cstdarg>
#include <cstdio>
#include <cstring>
#include <iterator>
#include <map>
#include <string>
#include <vector>

#ifndef _POSITION_DEFINED
#define _POSITION_DEFINED
struct __POSITION {};
typedef __POSITION* POSITION;   // MFC iteration cursor; NULL ends the iteration
#endif

#ifndef _TCHAR_DEFINED
#define _TCHAR_DEFINED
typedef char TCHAR;
#endif
typedef const char* LPCTSTR;
typedef char* LPTSTR;
typedef const char* LPCSTR;
typedef char* LPSTR;

namespace ran_compat {
    // CP949 lead bytes are 0x81..0xFE; the following byte belongs to the same character.
    inline bool IsLeadByte(char c) { const unsigned char u = (unsigned char)c; return u >= 0x81 && u <= 0xFE; }
}

class CString
{
public:
    CString() = default;
    CString(const char* sz) : m_str(sz ? sz : "") {}
    CString(const char* sz, int len) : m_str(sz ? sz : "", sz ? (size_t)len : 0) {}
    CString(char ch, int repeat = 1) : m_str((size_t)(repeat > 0 ? repeat : 0), ch) {}
    CString(const std::string& s) : m_str(s) {}

    int GetLength() const { return (int)m_str.size(); }
    bool IsEmpty() const { return m_str.empty(); }
    void Empty() { m_str.clear(); }
    const char* GetString() const { return m_str.c_str(); }
    operator const char*() const { return m_str.c_str(); }

    char GetAt(int i) const { return m_str[(size_t)i]; }
    char operator[](int i) const { return m_str[(size_t)i]; }
    void SetAt(int i, char ch) { m_str[(size_t)i] = ch; }

    void Format(const char* fmt, ...)
    {
        va_list ap;
        va_start(ap, fmt);
        FormatV(fmt, ap);
        va_end(ap);
    }
    void AppendFormat(const char* fmt, ...)
    {
        va_list ap;
        va_start(ap, fmt);
        std::string tail = VFormat(fmt, ap);
        va_end(ap);
        m_str += tail;
    }
    void FormatV(const char* fmt, va_list ap) { m_str = VFormat(fmt, ap); }

    CString Mid(int first) const { return Mid(first, GetLength() - first); }
    CString Mid(int first, int count) const
    {
        if (first < 0) first = 0;
        if (first >= GetLength() || count <= 0) return CString();
        return CString(m_str.substr((size_t)first, (size_t)count));
    }
    CString Left(int count) const { return count <= 0 ? CString() : CString(m_str.substr(0, (size_t)count)); }
    CString Right(int count) const
    {
        if (count <= 0) return CString();
        if (count >= GetLength()) return *this;
        return CString(m_str.substr(m_str.size() - (size_t)count));
    }

    int Find(char ch, int start = 0) const
    {
        for (int i = start < 0 ? 0 : start; i < GetLength(); ++i) {
            if (m_str[(size_t)i] == ch) return i;
            if (ran_compat::IsLeadByte(m_str[(size_t)i])) ++i;
        }
        return -1;
    }
    int Find(const char* sub, int start = 0) const
    {
        if (start < 0) start = 0;
        if (start > GetLength()) return -1;
        const size_t pos = m_str.find(sub, (size_t)start);
        return pos == std::string::npos ? -1 : (int)pos;
    }
    int ReverseFind(char ch) const
    {
        int found = -1;
        for (int i = 0; i < GetLength(); ++i) {
            if (m_str[(size_t)i] == ch) found = i;
            if (ran_compat::IsLeadByte(m_str[(size_t)i])) ++i;
        }
        return found;
    }
    int FindOneOf(const char* set) const
    {
        for (int i = 0; i < GetLength(); ++i) {
            if (std::strchr(set, m_str[(size_t)i]) && m_str[(size_t)i] != 0) return i;
            if (ran_compat::IsLeadByte(m_str[(size_t)i])) ++i;
        }
        return -1;
    }

    CString& MakeUpper() { MapAscii(::toupper); return *this; }
    CString& MakeLower() { MapAscii(::tolower); return *this; }

    int Compare(const char* other) const { return std::strcmp(GetString(), other); }
    int CompareNoCase(const char* other) const { return strcasecmp(GetString(), other); }

    CString& Trim() { return TrimRight().TrimLeft(); }
    CString& Trim(char ch) { return TrimRight(ch).TrimLeft(ch); }
    CString& Trim(const char* set) { return TrimRight(set).TrimLeft(set); }
    CString& TrimLeft() { return TrimLeft(" \t\r\n\v\f"); }
    CString& TrimLeft(char ch) { const char set[2] = {ch, 0}; return TrimLeft(set); }
    CString& TrimLeft(const char* set)
    {
        const size_t pos = m_str.find_first_not_of(set);
        m_str.erase(0, pos == std::string::npos ? m_str.size() : pos);
        return *this;
    }
    CString& TrimRight() { return TrimRight(" \t\r\n\v\f"); }
    CString& TrimRight(char ch) { const char set[2] = {ch, 0}; return TrimRight(set); }
    CString& TrimRight(const char* set)
    {
        const size_t pos = m_str.find_last_not_of(set);
        m_str.erase(pos == std::string::npos ? 0 : pos + 1);
        return *this;
    }

    int Replace(char from, char to)
    {
        int n = 0;
        for (int i = 0; i < GetLength(); ++i) {
            if (m_str[(size_t)i] == from) { m_str[(size_t)i] = to; ++n; }
            else if (ran_compat::IsLeadByte(m_str[(size_t)i])) ++i;
        }
        return n;
    }
    int Replace(const char* from, const char* to)
    {
        const size_t fromLen = std::strlen(from);
        if (fromLen == 0) return 0;
        const size_t toLen = std::strlen(to);
        int n = 0;
        for (size_t pos = m_str.find(from); pos != std::string::npos; pos = m_str.find(from, pos + toLen)) {
            m_str.replace(pos, fromLen, to);
            ++n;
        }
        return n;
    }
    int Remove(char ch)
    {
        int n = 0;
        std::string out;
        out.reserve(m_str.size());
        for (size_t i = 0; i < m_str.size(); ++i) {
            if (ran_compat::IsLeadByte(m_str[i]) && i + 1 < m_str.size()) { out += m_str[i]; out += m_str[++i]; continue; }
            if (m_str[i] == ch) { ++n; continue; }
            out += m_str[i];
        }
        m_str.swap(out);
        return n;
    }
    int Insert(int index, char ch) { const char s[2] = {ch, 0}; return Insert(index, s); }
    int Insert(int index, const char* sz)
    {
        if (index < 0) index = 0;
        if (index > GetLength()) index = GetLength();
        m_str.insert((size_t)index, sz);
        return GetLength();
    }
    int Delete(int index, int count = 1)
    {
        if (index >= 0 && index < GetLength() && count > 0) m_str.erase((size_t)index, (size_t)count);
        return GetLength();
    }

    // MFC contract: returns the next token and advances start; after the last token start == -1.
    CString Tokenize(const char* tokens, int& start) const
    {
        if (start < 0) return CString();
        size_t from = m_str.find_first_not_of(tokens, (size_t)start);
        if (from == std::string::npos) { start = -1; return CString(); }
        size_t to = m_str.find_first_of(tokens, from);
        if (to == std::string::npos) to = m_str.size();
        start = (int)(to + 1);
        return CString(m_str.substr(from, to - from));
    }
    CString SpanExcluding(const char* set) const
    {
        const size_t pos = m_str.find_first_of(set);
        return CString(m_str.substr(0, pos));
    }

    // Writable buffer of at least minLen chars; call ReleaseBuffer before using other members.
    char* GetBuffer(int minLen = 0)
    {
        if (minLen > GetLength()) m_str.resize((size_t)minLen);
        return m_str.data();
    }
    void ReleaseBuffer(int newLen = -1)
    {
        if (newLen < 0) newLen = (int)std::strlen(m_str.c_str());
        m_str.resize((size_t)newLen);
    }

    CString& operator+=(const char* sz) { m_str += sz; return *this; }
    CString& operator+=(char ch) { m_str += ch; return *this; }
    CString& operator+=(const CString& s) { m_str += s.m_str; return *this; }

    friend CString operator+(const CString& a, const CString& b) { return CString(a.m_str + b.m_str); }
    friend CString operator+(const CString& a, const char* b) { return CString(a.m_str + b); }
    friend CString operator+(const char* a, const CString& b) { return CString(std::string(a) + b.m_str); }
    friend CString operator+(const CString& a, char b) { return CString(a.m_str + b); }

    friend bool operator==(const CString& a, const CString& b) { return a.m_str == b.m_str; }
    friend bool operator==(const CString& a, const char* b) { return a.m_str == b; }
    friend bool operator==(const char* a, const CString& b) { return b.m_str == a; }
    friend bool operator!=(const CString& a, const CString& b) { return a.m_str != b.m_str; }
    friend bool operator!=(const CString& a, const char* b) { return a.m_str != b; }
    friend bool operator<(const CString& a, const CString& b) { return a.m_str < b.m_str; }
    friend bool operator>(const CString& a, const CString& b) { return a.m_str > b.m_str; }

private:
    static std::string VFormat(const char* fmt, va_list ap)
    {
        va_list ap2;
        va_copy(ap2, ap);
        const int n = std::vsnprintf(nullptr, 0, fmt, ap2);
        va_end(ap2);
        if (n <= 0) return std::string();
        std::string out((size_t)n, '\0');
        std::vsnprintf(out.data(), (size_t)n + 1, fmt, ap);
        return out;
    }
    void MapAscii(int (*fn)(int))
    {
        for (size_t i = 0; i < m_str.size(); ++i) {
            if (ran_compat::IsLeadByte(m_str[i])) { ++i; continue; }
            const unsigned char u = (unsigned char)m_str[i];
            if (u < 0x80) m_str[i] = (char)fn(u);
        }
    }

    std::string m_str;
};

// Wide CString. WCHAR is char16_t in the native build (2 bytes like Windows; see
// win32_types.h), so this is a std::u16string underneath.
class CStringW
{
public:
    CStringW() = default;
    CStringW(const char16_t* sz) { if (sz) m_str = sz; }
    CStringW(const char16_t* sz, int len) { if (sz && len > 0) m_str.assign(sz, (size_t)len); }
    CStringW(char16_t ch, int repeat = 1) : m_str((size_t)(repeat > 0 ? repeat : 0), ch) {}

    int GetLength() const { return (int)m_str.size(); }
    bool IsEmpty() const { return m_str.empty(); }
    void Empty() { m_str.clear(); }
    const char16_t* GetString() const { return m_str.c_str(); }
    operator const char16_t*() const { return GetString(); }

    char16_t GetAt(int i) const { return m_str[(size_t)i]; }
    char16_t operator[](int i) const { return m_str[(size_t)i]; }
    void SetAt(int i, char16_t ch) { m_str[(size_t)i] = ch; }

    CStringW Mid(int first) const { return Mid(first, GetLength() - first); }
    CStringW Mid(int first, int count) const
    {
        if (first < 0) first = 0;
        if (first >= GetLength() || count <= 0) return CStringW();
        return FromU16(m_str.substr((size_t)first, (size_t)count));
    }
    CStringW Left(int count) const { return count <= 0 ? CStringW() : FromU16(m_str.substr(0, (size_t)count)); }
    CStringW Right(int count) const
    {
        if (count <= 0) return CStringW();
        if (count >= GetLength()) return *this;
        return FromU16(m_str.substr(m_str.size() - (size_t)count));
    }
    int Find(char16_t ch, int start = 0) const
    {
        const size_t pos = m_str.find(ch, (size_t)(start < 0 ? 0 : start));
        return pos == std::u16string::npos ? -1 : (int)pos;
    }
    int Insert(int index, char16_t ch)
    {
        if (index < 0) index = 0;
        if (index > GetLength()) index = GetLength();
        m_str.insert((size_t)index, 1, ch);
        return GetLength();
    }
    int Insert(int index, const char16_t* sz)
    {
        if (index < 0) index = 0;
        if (index > GetLength()) index = GetLength();
        m_str.insert((size_t)index, sz);
        return GetLength();
    }
    int Delete(int index, int count = 1)
    {
        if (index >= 0 && index < GetLength() && count > 0) m_str.erase((size_t)index, (size_t)count);
        return GetLength();
    }

    CStringW& operator+=(char16_t ch) { m_str += ch; return *this; }
    CStringW& operator+=(const char16_t* sz) { if (sz) m_str += sz; return *this; }
    CStringW& operator+=(const CStringW& s) { m_str += s.m_str; return *this; }
    friend CStringW operator+(const CStringW& a, const CStringW& b) { CStringW r(a); r += b; return r; }
    friend bool operator==(const CStringW& a, const CStringW& b) { return a.m_str == b.m_str; }
    friend bool operator!=(const CStringW& a, const CStringW& b) { return a.m_str != b.m_str; }

private:
    static CStringW FromU16(std::u16string s) { CStringW r; r.m_str = std::move(s); return r; }
    std::u16string m_str;
};
typedef CString CStringA;

class CStringArray
{
public:
    int GetSize() const { return (int)m_items.size(); }
    int GetCount() const { return (int)m_items.size(); }
    int GetUpperBound() const { return GetSize() - 1; }
    bool IsEmpty() const { return m_items.empty(); }
    int Add(const CString& s) { m_items.push_back(s); return GetSize() - 1; }
    const CString& GetAt(int i) const { return m_items[(size_t)i]; }
    CString& ElementAt(int i) { return m_items[(size_t)i]; }
    void SetAt(int i, const CString& s) { m_items[(size_t)i] = s; }
    CString& operator[](int i) { return m_items[(size_t)i]; }
    const CString& operator[](int i) const { return m_items[(size_t)i]; }
    void SetSize(int n, int /*growBy*/ = -1) { m_items.resize(n > 0 ? (size_t)n : 0); }
    void RemoveAll() { m_items.clear(); }
    void InsertAt(int i, const CString& s, int count = 1) { m_items.insert(m_items.begin() + i, (size_t)count, s); }
    void RemoveAt(int i, int count = 1) { m_items.erase(m_items.begin() + i, m_items.begin() + i + count); }

private:
    std::vector<CString> m_items;
};

// POSITION is (index + 1) into the container's iteration order; NULL ends iteration.
namespace ran_compat {
    inline POSITION PosFromIndex(size_t i) { return reinterpret_cast<POSITION>(i + 1); }
    inline size_t IndexFromPos(POSITION p) { return reinterpret_cast<size_t>(p) - 1; }
}

class CMapStringToString
{
public:
    int GetCount() const { return (int)m_map.size(); }
    int GetSize() const { return (int)m_map.size(); }
    bool IsEmpty() const { return m_map.empty(); }
    void SetAt(const char* key, const char* value) { m_map[key] = value; }
    CString& operator[](const char* key) { return m_map[key]; }
    BOOL Lookup(const char* key, CString& value) const
    {
        const auto it = m_map.find(key);
        if (it == m_map.end()) return FALSE;
        value = it->second;
        return TRUE;
    }
    BOOL RemoveKey(const char* key) { return m_map.erase(key) ? TRUE : FALSE; }
    void RemoveAll() { m_map.clear(); }
    POSITION GetStartPosition() const { return m_map.empty() ? nullptr : ran_compat::PosFromIndex(0); }
    void GetNextAssoc(POSITION& pos, CString& key, CString& value) const
    {
        const size_t i = ran_compat::IndexFromPos(pos);
        auto it = std::next(m_map.begin(), (std::ptrdiff_t)i);
        key = it->first;
        value = it->second;
        pos = (i + 1 < m_map.size()) ? ran_compat::PosFromIndex(i + 1) : nullptr;
    }

private:
    std::map<CString, CString> m_map;
};

class CStringList
{
public:
    int GetCount() const { return (int)m_items.size(); }
    int GetSize() const { return (int)m_items.size(); }
    bool IsEmpty() const { return m_items.empty(); }
    POSITION AddHead(const CString& s) { m_items.insert(m_items.begin(), s); return ran_compat::PosFromIndex(0); }
    POSITION AddTail(const CString& s) { m_items.push_back(s); return ran_compat::PosFromIndex(m_items.size() - 1); }
    CString& GetHead() { return m_items.front(); }
    CString& GetTail() { return m_items.back(); }
    CString RemoveHead() { CString s = m_items.front(); m_items.erase(m_items.begin()); return s; }
    CString RemoveTail() { CString s = m_items.back(); m_items.pop_back(); return s; }
    void RemoveAll() { m_items.clear(); }
    POSITION GetHeadPosition() const { return m_items.empty() ? nullptr : ran_compat::PosFromIndex(0); }
    CString& GetNext(POSITION& pos)
    {
        const size_t i = ran_compat::IndexFromPos(pos);
        pos = (i + 1 < m_items.size()) ? ran_compat::PosFromIndex(i + 1) : nullptr;
        return m_items[i];
    }
    CString& GetAt(POSITION pos) { return m_items[ran_compat::IndexFromPos(pos)]; }

private:
    std::vector<CString> m_items;
};
