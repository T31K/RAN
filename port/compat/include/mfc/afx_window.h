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

// Message-map hook (see BEGIN_MESSAGE_MAP in afx_all.h): FALSE = not handled.
class CCmdTarget : public CObject
{
public:
    virtual BOOL ran_OnMsg(UINT, WPARAM, LPARAM, LRESULT*) { return FALSE; }
};

class CFont;
class CDC;

class CWnd : public CCmdTarget
{
public:
    HWND m_hWnd = nullptr;

    virtual ~CWnd()
    {
        if (FocusWindow() == this) FocusWindow() = nullptr;
        if (MainWindow() == this) MainWindow() = nullptr;
    }
    HWND GetSafeHwnd() const { return m_hWnd; }
    operator HWND() const { return m_hWnd; }

    virtual BOOL Create(const char*, const char*, DWORD, const RECT&, CWnd*, UINT, void* = nullptr) { return FALSE; }
    // The game's main window: created by the platform layer (an SDL3 window; with DXVK-native
    // the SDL_Window* is the HWND D3D9 presents to). Without a hook there is no window.
    static HWND (*&CreateMainWindowHook())(const char* title, int x, int y, int w, int h, DWORD style)
    {
        static HWND (*hook)(const char*, int, int, int, int, DWORD) = nullptr;
        return hook;
    }
    BOOL CreateEx(DWORD, const char*, const char* title, DWORD style, int x, int y, int w, int h, HWND, HMENU, void* = nullptr)
    {
        if (!CreateMainWindowHook()) return FALSE;
        m_hWnd = CreateMainWindowHook()(title, x, y, w, h, style);
        if (m_hWnd) {
            MainWindow() = this;
            if (!FocusWindow()) FocusWindow() = this;   // a new top-level window is active
        }
        return m_hWnd != nullptr;
    }
    // The CWnd that owns the platform window (receives the translated SDL events).
    static CWnd*& MainWindow() { static CWnd* wnd = nullptr; return wnd; }
    virtual BOOL DestroyWindow() { m_hWnd = nullptr; return TRUE; }
    virtual LRESULT WindowProc(UINT message, WPARAM wParam, LPARAM lParam)
    {
        LRESULT result = 0;
        if (OnWndMsg(message, wParam, lParam, &result)) return result;
        return DefWindowProc(message, wParam, lParam);
    }
    virtual LRESULT DefWindowProc(UINT, WPARAM, LPARAM) { return 0; }
    virtual BOOL PreTranslateMessage(MSG*) { return FALSE; }

    BOOL ShowWindow(int) { return FALSE; }
    BOOL EnableWindow(BOOL = TRUE) { return FALSE; }
    BOOL IsWindowVisible() const { return FALSE; }
    BOOL IsWindowEnabled() const { return TRUE; }
    // Keyboard focus among the game's windows (the main window and its hidden edit control,
    // CIMEEdit): the platform pump sends key, character and IME messages to FocusWindow().
    static CWnd*& FocusWindow() { static CWnd* wnd = nullptr; return wnd; }
    CWnd* SetFocus()
    {
        CWnd* old = FocusWindow();
        if (old == this) return old;
        FocusWindow() = this;
        if (old) old->SendMessage(WM_KILLFOCUS, (WPARAM)m_hWnd);
        SendMessage(WM_SETFOCUS, old ? (WPARAM)old->m_hWnd : 0);
        return old;
    }
    static CWnd* GetFocus() { return FocusWindow(); }
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
    // Edit-box caret: the game draws its own caret in D3D, the Win32 one is never visible.
    void CreateSolidCaret(int, int) {}
    void ShowCaret() {}
    void HideCaret() {}
    static void SetCaretPos(POINT) {}
    static CPoint GetCaretPos() { return CPoint(0, 0); }
    BOOL SetWindowPos(const CWnd*, int, int, int, int, UINT) { return FALSE; }
    void GetClientRect(RECT* r) const { ::GetClientRect(m_hWnd, r); }
    void ScreenToClient(POINT*) const {}
    void ScreenToClient(RECT*) const {}
    void ClientToScreen(POINT*) const {}
    void ClientToScreen(RECT*) const {}
    void GetWindowRect(RECT* r) const { ::GetWindowRect(m_hWnd, r); }
    void Invalidate(BOOL = TRUE) {}
    void UpdateWindow() {}
    CWnd* GetDlgItem(int) const { return nullptr; }
    DWORD GetStyle() const { return 0; }
    BOOL ModifyStyle(DWORD, DWORD, UINT = 0) { return FALSE; }
    // ActiveX hosting (the embedded IE browser in Engine Common/CommonWeb). No control is ever
    // created natively, so in-game web pages stay blank until a WKWebView replacement exists.
    BOOL CreateControl(REFCLSID, const char*, DWORD, const RECT&, CWnd*, UINT, void* = nullptr, BOOL = FALSE, BSTR = nullptr) { return FALSE; }
    void InvokeHelper(LONG /*dispid*/, WORD /*flags*/, WORD /*vtRet*/, void* ret, const BYTE* /*params*/, ...)
    {
        (void)ret;   // results keep their caller-initialised values
    }
    void GetProperty(LONG, WORD, void*) const {}
    void SetProperty(LONG, WORD, ...) {}
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
    // Window-shell handlers the game's CBasicWnd chains to; the platform layer calls the game's
    // overrides directly from SDL window events.
    afx_msg void OnSize(UINT, int, int) {}
    afx_msg void OnSysCommand(UINT, LPARAM) {}
    afx_msg void OnActivate(UINT, CWnd*, BOOL) {}
    afx_msg BOOL OnNcActivate(BOOL) { return TRUE; }
    afx_msg void OnGetMinMaxInfo(MINMAXINFO*) {}
    afx_msg void OnMouseMove(UINT, CPoint) {}
    afx_msg void OnActivateApp(BOOL, DWORD) {}
    afx_msg void OnActivateApp(BOOL, HTASK) {}
    afx_msg BOOL OnSetCursor(CWnd*, UINT, UINT) { return FALSE; }
    // MFC: OnWndMsg dispatches through the message map (games override it and chain here).
    virtual BOOL OnWndMsg(UINT message, WPARAM wParam, LPARAM lParam, LRESULT* result)
    {
        LRESULT r = 0;
        const BOOL handled = ran_OnMsg(message, wParam, lParam, &r);
        if (result) *result = r;
        return handled;
    }
    virtual void PostNcDestroy() {}

    CString m_strText;
};

// MFC application object. The SDL3 platform layer (Phase 2) owns the event loop; PumpMessage
// lets long-running code (the loading thread) keep the window responsive through that hook.
class CWinThread : public CCmdTarget
{
public:
    CWnd* m_pMainWnd = nullptr;
    virtual BOOL PumpMessage() { return ran_PumpMessageHook() ? ran_PumpMessageHook()() : TRUE; }
    static BOOL (*&ran_PumpMessageHook())() { static BOOL (*hook)() = nullptr; return hook; }
};
// As in MFC, the one global application object registers itself on construction and
// AfxGetApp() returns it (the game's `CBasicApp theApp`). The platform main calls
// InitInstance / OnIdle / ExitInstance on it.
class CWinApp;
namespace ran_compat { inline CWinApp*& CurrentApp() { static CWinApp* app = nullptr; return app; } }
class CWinApp : public CWinThread
{
public:
    CWinApp() { ran_compat::CurrentApp() = this; }
    virtual ~CWinApp() { if (ran_compat::CurrentApp() == this) ran_compat::CurrentApp() = nullptr; }
    const char* m_pszAppName = "RanOnline";
    HINSTANCE m_hInstance = nullptr;
    char* m_lpCmdLine = const_cast<char*>("");
    virtual BOOL InitInstance() { return TRUE; }
    virtual int ExitInstance() { return 0; }
    virtual BOOL OnIdle(LONG) { return FALSE; }
    virtual int Run() { return 0; }
    afx_msg void OnHelp() {}
    HICON LoadIcon(UINT) const { return nullptr; }
    HICON LoadIcon(const char*) const { return nullptr; }
};
inline CWinApp* AfxGetApp()
{
    if (!ran_compat::CurrentApp()) { static CWinApp fallback; }   // registers itself
    return ran_compat::CurrentApp();
}
inline void AfxEnableControlContainer() {}
inline const char* AfxGetAppName() { return AfxGetApp()->m_pszAppName; }
inline const char* AfxRegisterWndClass(UINT, HCURSOR = nullptr, HBRUSH = nullptr, HICON = nullptr) { return "RanOnlineWnd"; }
#ifndef ID_HELP
#define ID_HELP 0xE146
#endif
inline CWnd* AfxGetMainWnd() { return AfxGetApp()->m_pMainWnd; }

class CEdit : public CWnd
{
public:
    // MFC CEdit::Create(style, rect, parent, id). Reports success: the game's CIMEEdit must
    // initialise, and the Phase 2 platform layer feeds it input instead of a Win32 edit box.
    BOOL Create(DWORD, const RECT&, CWnd*, UINT) { return TRUE; }
    using CWnd::Create;
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
// List and combo boxes keep their items in memory (strings, item data, selection), so code that
// fills a list and reads it back behaves as on Windows; nothing is drawn natively.
class CListItemsWnd : public CWnd
{
public:
    void ResetContent() { m_items.clear(); m_data.clear(); m_cur = -1; }
    int AddString(const char* s) { m_items.push_back(CString(s ? s : "")); m_data.push_back(0); return (int)m_items.size() - 1; }
    int InsertString(int i, const char* s)
    {
        if (i < 0 || i > (int)m_items.size()) i = (int)m_items.size();
        m_items.insert(m_items.begin() + i, CString(s ? s : ""));
        m_data.insert(m_data.begin() + i, 0);
        return i;
    }
    int DeleteString(UINT i)
    {
        if (i >= m_items.size()) return -1;
        m_items.erase(m_items.begin() + i);
        m_data.erase(m_data.begin() + i);
        if (m_cur >= (int)m_items.size()) m_cur = -1;
        return (int)m_items.size();
    }
    int GetCount() const { return (int)m_items.size(); }
    int GetCurSel() const { return m_cur; }
    int SetCurSel(int i) { m_cur = (i >= 0 && i < (int)m_items.size()) ? i : -1; return m_cur; }
    DWORD_PTR GetItemData(int i) const { return (i >= 0 && i < (int)m_data.size()) ? m_data[i] : 0; }
    int SetItemData(int i, DWORD_PTR d) { if (i < 0 || i >= (int)m_data.size()) return -1; m_data[i] = d; return 0; }
    int FindStringExact(int, const char* s) const
    {
        for (size_t i = 0; i < m_items.size(); ++i) if (s && m_items[i].CompareNoCase(s) == 0) return (int)i;
        return -1;
    }
protected:
    int TextAt(int i, CString& out) const { if (i < 0 || i >= (int)m_items.size()) return -1; out = m_items[i]; return out.GetLength(); }
    int TextAt(int i, char* out) const
    {
        if (!out || i < 0 || i >= (int)m_items.size()) return -1;
        std::strcpy(out, m_items[i].GetString());
        return m_items[i].GetLength();
    }
    std::vector<CString> m_items;
    std::vector<DWORD_PTR> m_data;
    int m_cur = -1;
};
class CComboBox : public CListItemsWnd
{
public:
    int GetLBText(int i, CString& out) const { return TextAt(i, out); }
    int GetLBText(int i, char* out) const { return TextAt(i, out); }
    int GetLBTextLen(int i) const { return (i >= 0 && i < (int)m_items.size()) ? m_items[i].GetLength() : -1; }
};
class CListBox : public CListItemsWnd
{
public:
    int GetText(int i, CString& out) const { return TextAt(i, out); }
    int GetText(int i, char* out) const { return TextAt(i, out); }
    int GetTextLen(int i) const { return (i >= 0 && i < (int)m_items.size()) ? m_items[i].GetLength() : -1; }
};
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
#ifndef PS_SOLID
#define PS_SOLID 0
#define PS_DASH  1
#define PS_DOT   2
#endif
class CPen : public CGdiObject
{
public:
    CPen() = default;
    CPen(int, int, COLORREF) {}
};
class CBrush : public CGdiObject
{
public:
    static CBrush* FromHandle(HBRUSH) { static CBrush b; return &b; }
};
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
    DWORD GetBitmapBits(DWORD, void*) const { return 0; }
};

// Device contexts of Win32 windows. The edit-box paint code that uses them only draws into a
// window that never appears natively, so drawing is a no-op; text metrics are zero.
class CDC : public CObject
{
public:
    HDC m_hDC = nullptr;
    operator HDC() const { return m_hDC; }
    CPen* SelectObject(CPen* p) { return p; }
    CFont* SelectObject(CFont* f) { return f; }
    CBrush* SelectObject(CBrush* b) { return b; }
    CPoint MoveTo(int x, int y) { return CPoint(x, y); }
    BOOL LineTo(int, int) { return TRUE; }
    BOOL GetTextMetrics(TEXTMETRICA* tm) const { if (tm) std::memset(tm, 0, sizeof(*tm)); return FALSE; }
    BOOL GetTextMetricsA(TEXTMETRICA* tm) const { return GetTextMetrics(tm); }   // after a GetTextMetrics->A macro
    void InvertRect(const RECT*) {}
    void FillRect(const RECT*, CBrush*) {}
};
class CPaintDC : public CDC { public: explicit CPaintDC(CWnd*) {} };
class CClientDC : public CDC { public: explicit CClientDC(CWnd*) {} };

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
