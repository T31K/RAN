// MFC window/control/GDI classes for the native macOS build.
//
// The game's real window, input and IME move to the SDL3 platform layer in Phase 2. Until then
// these classes keep the code that mentions them compiling: window operations are no-ops on a
// window that does not exist, and the virtual message hooks (WindowProc, OnChar, OnKeyDown, ...)
// stay virtual so Phase 2 can drive classes like CIMEEdit with synthesized messages.
#pragma once
#include "ran_compat.h"
#include <glob.h>
#include <sys/stat.h>

#ifndef afx_msg
#define afx_msg   // MFC marker for message-handler declarations
#endif

class CObject
{
public:
    virtual ~CObject() = default;
};

class CCmdTarget : public CObject {};

class CFont;
class CDC;

class CWnd : public CCmdTarget
{
public:
    HWND m_hWnd = nullptr;

    virtual ~CWnd() = default;
    HWND GetSafeHwnd() const { return m_hWnd; }
    operator HWND() const { return m_hWnd; }

    virtual BOOL Create(const char*, const char*, DWORD, const RECT&, CWnd*, UINT, void* = nullptr) { return FALSE; }
    virtual BOOL DestroyWindow() { m_hWnd = nullptr; return TRUE; }
    virtual LRESULT WindowProc(UINT message, WPARAM wParam, LPARAM lParam) { return DefWindowProc(message, wParam, lParam); }
    virtual LRESULT DefWindowProc(UINT, WPARAM, LPARAM) { return 0; }
    virtual BOOL PreTranslateMessage(MSG*) { return FALSE; }

    BOOL ShowWindow(int) { return FALSE; }
    BOOL EnableWindow(BOOL = TRUE) { return FALSE; }
    BOOL IsWindowVisible() const { return FALSE; }
    BOOL IsWindowEnabled() const { return TRUE; }
    CWnd* SetFocus() { return nullptr; }
    static CWnd* GetFocus() { return nullptr; }
    CWnd* GetParent() const { return nullptr; }
    BOOL PostMessage(UINT, WPARAM = 0, LPARAM = 0) { return FALSE; }
    LRESULT SendMessage(UINT message, WPARAM wParam = 0, LPARAM lParam = 0) { return WindowProc(message, wParam, lParam); }
    void SetFont(CFont*, BOOL = TRUE) {}
    CFont* GetFont() const { return nullptr; }
    void SetWindowText(const char* text) { m_strText = text ? text : ""; }
    void GetWindowText(CString& out) const { out = m_strText; }
    int GetWindowText(char* buf, int max) const
    {
        if (!buf || max <= 0) return 0;
        const int n = m_strText.GetLength() < max - 1 ? m_strText.GetLength() : max - 1;
        std::memcpy(buf, m_strText.GetString(), (size_t)n);
        buf[n] = 0;
        return n;
    }
    int GetWindowTextLength() const { return m_strText.GetLength(); }
    void MoveWindow(int, int, int, int, BOOL = TRUE) {}
    void MoveWindow(const RECT*, BOOL = TRUE) {}
    BOOL SetWindowPos(const CWnd*, int, int, int, int, UINT) { return FALSE; }
    void GetClientRect(RECT* r) const { if (r) SetRectEmpty(r); }
    void GetWindowRect(RECT* r) const { if (r) SetRectEmpty(r); }
    void Invalidate(BOOL = TRUE) {}
    void UpdateWindow() {}
    CWnd* GetDlgItem(int) const { return nullptr; }
    DWORD GetStyle() const { return 0; }
    BOOL ModifyStyle(DWORD, DWORD, UINT = 0) { return FALSE; }
    UINT_PTR SetTimer(UINT_PTR id, UINT, void*) { return id; }
    BOOL KillTimer(UINT_PTR) { return TRUE; }
    CDC* GetDC() { return nullptr; }
    int ReleaseDC(CDC*) { return 0; }

protected:
    afx_msg void OnKeyDown(UINT, UINT, UINT) {}
    afx_msg void OnKeyUp(UINT, UINT, UINT) {}
    afx_msg void OnChar(UINT, UINT, UINT) {}
    afx_msg void OnSetFocus(CWnd*) {}
    afx_msg void OnKillFocus(CWnd*) {}
    afx_msg void OnPaint() {}
    afx_msg int OnCreate(void*) { return 0; }
    afx_msg void OnDestroy() {}
    afx_msg void OnTimer(UINT_PTR) {}

    CString m_strText;
};

class CEdit : public CWnd
{
public:
    void SetSel(int, int, BOOL = FALSE) {}
    void GetSel(int& start, int& end) const { start = end = 0; }
    void ReplaceSel(const char*, BOOL = FALSE) {}
    void LimitText(int = 0) {}
    void SetLimitText(UINT) {}
    UINT GetLimitText() const { return 0; }
    void Clear() {}
};

class CStatic : public CWnd {};
class CButton : public CWnd
{
public:
    int GetCheck() const { return 0; }
    void SetCheck(int) {}
};
class CComboBox : public CWnd {};
class CListBox : public CWnd {};
class CProgressCtrl : public CWnd {};

struct CCreateContext {};

class CDialog : public CWnd
{
public:
    CDialog() = default;
    explicit CDialog(UINT, CWnd* = nullptr) {}
    virtual INT_PTR DoModal() { return IDCANCEL; }
    virtual BOOL OnInitDialog() { return TRUE; }
    virtual void OnOK() {}
    virtual void OnCancel() {}
    void EndDialog(int) {}
    BOOL UpdateData(BOOL = TRUE) { return TRUE; }
};

// GDI objects: handles only.
class CGdiObject : public CObject
{
public:
    HANDLE m_hObject = nullptr;
    BOOL DeleteObject() { m_hObject = nullptr; return TRUE; }
};
class CPen : public CGdiObject {};
class CBrush : public CGdiObject {};
class CFont : public CGdiObject
{
public:
    BOOL CreateFontIndirect(const LOGFONTA*) { return FALSE; }
};

typedef struct tagBITMAP {
    LONG bmType, bmWidth, bmHeight, bmWidthBytes;
    WORD bmPlanes, bmBitsPixel;
    void* bmBits;
} BITMAP, *PBITMAP, *LPBITMAP;

class CBitmap : public CGdiObject
{
public:
    int GetBitmap(BITMAP* bm) const { if (bm) std::memset(bm, 0, sizeof(*bm)); return 0; }
};

class CDC : public CObject
{
public:
    HDC m_hDC = nullptr;
};

#define IMAGE_BITMAP        0
#define LR_LOADFROMFILE     0x00000010
#define LR_CREATEDIBSECTION 0x00002000
inline HANDLE LoadImage(HINSTANCE, const char*, UINT, int, int, UINT) { return nullptr; }   // Phase 3: screenshots go through stb
#define LoadImageA LoadImage

// MFC CFileFind on glob(): FindFile(pattern) then FindNextFile() until it returns FALSE.
class CFileFind : public CObject
{
public:
    ~CFileFind() override { Close(); }

    BOOL FindFile(const char* pattern = nullptr, DWORD = 0)
    {
        Close();
        std::string pat = ran_compat::ResolvePath(pattern ? pattern : "*");
        if (pat.size() >= 3 && pat.compare(pat.size() - 3, 3, "*.*") == 0) pat.replace(pat.size() - 3, 3, "*");
        m_open = ::glob(pat.c_str(), GLOB_NOSORT, nullptr, &m_glob) == 0;
        m_index = 0;
        return m_open && m_glob.gl_pathc > 0;
    }
    // Advances to the next match; returns FALSE when the current match is the last one
    // (MFC's contract - the last match is still readable after FALSE).
    BOOL FindNextFile()
    {
        if (!m_open || m_index >= m_glob.gl_pathc) return FALSE;
        m_current = m_glob.gl_pathv[m_index++];
        return m_index < m_glob.gl_pathc;
    }
    CString GetFilePath() const { return CString(m_current.c_str()); }
    CString GetFileName() const
    {
        const size_t slash = m_current.find_last_of('/');
        return CString((slash == std::string::npos ? m_current : m_current.substr(slash + 1)).c_str());
    }
    BOOL IsDirectory() const
    {
        struct stat st;
        return ::stat(m_current.c_str(), &st) == 0 && S_ISDIR(st.st_mode);
    }
    BOOL IsDots() const { const CString n = GetFileName(); return n == "." || n == ".."; }
    void Close()
    {
        if (m_open) ::globfree(&m_glob);
        m_open = false;
        m_current.clear();
    }

private:
    glob_t m_glob = {};
    bool m_open = false;
    size_t m_index = 0;
    std::string m_current;
};
