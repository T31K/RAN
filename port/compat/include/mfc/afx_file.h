// MFC CFile on stdio for the native macOS build (Windows paths resolved like everywhere else).
#pragma once
#include "mfc/afx_window.h"

class CFile : public CObject
{
public:
    enum OpenFlags {
        modeRead = 0x0000, modeWrite = 0x0001, modeReadWrite = 0x0002,
        shareCompat = 0x0000, shareExclusive = 0x0010, shareDenyWrite = 0x0020, shareDenyRead = 0x0030, shareDenyNone = 0x0040,
        modeNoInherit = 0x0080, modeCreate = 0x1000, modeNoTruncate = 0x2000,
        typeText = 0x4000, typeBinary = 0x8000
    };
    enum SeekPosition { begin = SEEK_SET, current = SEEK_CUR, end = SEEK_END };

    CFile() = default;
    CFile(const char* path, UINT flags) { Open(path, flags); }
    ~CFile() override { Close(); }

    BOOL Open(const char* path, UINT flags, void* /*error*/ = nullptr)
    {
        Close();
        const bool create = (flags & modeCreate) != 0;
        const bool keep = (flags & modeNoTruncate) != 0;
        const UINT access = flags & 0x3;
        const char* mode = "rb";
        if (access == modeWrite) mode = create ? (keep ? "ab" : "wb") : "r+b";
        else if (access == modeReadWrite) mode = create && !keep ? "w+b" : "r+b";
        const std::string p = ran_compat::ResolvePath(path);
        m_fp = std::fopen(p.c_str(), mode);
        if (!m_fp && create && keep) m_fp = std::fopen(p.c_str(), access == modeWrite ? "wb" : "w+b");
        if (m_fp && access == modeWrite && create && keep) std::fseek(m_fp, 0, SEEK_SET);
        m_path = path ? path : "";
        return m_fp != nullptr;
    }
    UINT Read(void* buf, UINT n) { return m_fp ? (UINT)std::fread(buf, 1, n, m_fp) : 0; }
    void Write(const void* buf, UINT n) { if (m_fp) std::fwrite(buf, 1, n, m_fp); }
    ULONGLONG Seek(LONGLONG off, UINT from) { if (!m_fp) return 0; std::fseek(m_fp, (long)off, (int)from); return (ULONGLONG)std::ftell(m_fp); }
    ULONGLONG SeekToEnd() { return Seek(0, end); }
    void SeekToBegin() { Seek(0, begin); }
    ULONGLONG GetPosition() const { return m_fp ? (ULONGLONG)std::ftell(m_fp) : 0; }
    ULONGLONG GetLength() const
    {
        if (!m_fp) return 0;
        const long cur = std::ftell(m_fp);
        std::fseek(m_fp, 0, SEEK_END);
        const long len = std::ftell(m_fp);
        std::fseek(m_fp, cur, SEEK_SET);
        return (ULONGLONG)len;
    }
    void Flush() { if (m_fp) std::fflush(m_fp); }
    void Close() { if (m_fp) std::fclose(m_fp); m_fp = nullptr; }
    CString GetFilePath() const { return CString(m_path.c_str()); }

protected:
    FILE* m_fp = nullptr;
    std::string m_path;
};

class CStdioFile : public CFile
{
public:
    CStdioFile() = default;
    CStdioFile(const char* path, UINT flags) : CFile(path, flags) {}
    BOOL ReadString(CString& line)
    {
        line.Empty();
        if (!m_fp) return FALSE;
        int c;
        bool any = false;
        while ((c = std::fgetc(m_fp)) != EOF) {
            any = true;
            if (c == '\n') break;
            if (c != '\r') line += (char)c;
        }
        return any ? TRUE : FALSE;
    }
    void WriteString(const char* s) { if (m_fp && s) std::fputs(s, m_fp); }
};
