// Everything the MFC stand-in headers (afx.h, afxwin.h, ...) provide. MFC is a Windows UI
// framework; the game uses its value types (CString, CTime, CRect, ...) and a few window/GDI
// classes that the Phase 2 SDL3 platform layer replaces.
#pragma once
#include "ran_compat.h"
#include "mfc/afx_window.h"
#include "mfc/afx_file.h"
#include "mfc/afx_inet.h"

inline void AfxThrowUserException() { throw new CException(); }

#define OFN_READONLY        0x00000001
#define OFN_OVERWRITEPROMPT 0x00000002
#define OFN_HIDEREADONLY    0x00000004
#define OFN_NOCHANGEDIR     0x00000008
#define OFN_ALLOWMULTISELECT 0x00000200
#define OFN_FILEMUSTEXIST   0x00001000

typedef struct tagOFNA {
    DWORD lStructSize; HWND hwndOwner; HINSTANCE hInstance;
    const char* lpstrFilter; char* lpstrCustomFilter; DWORD nMaxCustFilter, nFilterIndex;
    char* lpstrFile; DWORD nMaxFile; char* lpstrFileTitle; DWORD nMaxFileTitle;
    const char* lpstrInitialDir; const char* lpstrTitle; DWORD Flags;
    WORD nFileOffset, nFileExtension; const char* lpstrDefExt;
} OPENFILENAME, *LPOPENFILENAME;

// MFC common file dialog: the native game never shows one (editor/debug paths only).
class CFileDialog : public CDialog
{
public:
    OPENFILENAME m_ofn = {};
    CFileDialog(BOOL, const char* = nullptr, const char* = nullptr, DWORD = 0, const char* = nullptr, CWnd* = nullptr, DWORD = 0) {}
    INT_PTR DoModal() override { return IDCANCEL; }
    CString GetPathName() const { return CString(); }
    CString GetFileName() const { return CString(); }
    CString GetFileExt() const { return CString(); }
};

inline int AfxMessageBox(const char* text, UINT type = MB_OK, UINT = 0) { return MessageBoxA(nullptr, text, "RanOnline", type); }
inline HINSTANCE AfxGetInstanceHandle() { return (HINSTANCE)GetModuleHandle(nullptr); }
inline HINSTANCE AfxGetResourceHandle() { return AfxGetInstanceHandle(); }

// MFC diagnostics. TRACE/ASSERT/VERIFY follow MFC: active only in debug builds.
#undef TRACE
#undef ASSERT
#undef VERIFY
#undef ASSERT_VALID
#ifdef _DEBUG
#define TRACE(...)      std::fprintf(stderr, __VA_ARGS__)
#define ASSERT(e)       assert(e)
#define VERIFY(e)       assert(e)
#else
#define TRACE(...)      ((void)0)
#define ASSERT(e)       ((void)0)
#define VERIFY(e)       ((void)(e))
#endif
#define TRACE0 TRACE
#define TRACE1 TRACE
#define TRACE2 TRACE
#define TRACE3 TRACE
#define ASSERT_VALID(p) ((void)0)
#define DEBUG_NEW new

#define DECLARE_DYNAMIC(c)
#define DECLARE_DYNCREATE(c)
#define DECLARE_SERIAL(c)
#define IMPLEMENT_DYNAMIC(c, b)
#define IMPLEMENT_DYNCREATE(c, b)
#define IMPLEMENT_SERIAL(c, b, s)

// MFC message maps, as a switch: BEGIN_MESSAGE_MAP defines Class::ran_OnMsg, each entry is a
// case that calls the class's handler (MFC's argument cracking), and unhandled messages chain
// to the base class. CWnd::WindowProc / SendMessage route through it, so the platform layer
// delivers WM_ACTIVATEAPP, WM_SIZE, WM_KEYDOWN, WM_CHAR... to the game's own handlers.
// (An entry may appear once per map; ON_COMMAND is not routed - no menus/commands natively.)
#define DECLARE_MESSAGE_MAP() \
    public: BOOL ran_OnMsg(UINT ranMsg_, WPARAM ranW_, LPARAM ranL_, LRESULT* ranRes_) override;
#define BEGIN_MESSAGE_MAP(theClass, baseClass) \
    BOOL theClass::ran_OnMsg(UINT ranMsg_, WPARAM ranW_, LPARAM ranL_, LRESULT* ranRes_) \
    { typedef baseClass ranBase_; (void)ranW_; (void)ranL_; *ranRes_ = 0; switch (ranMsg_) { default: break;
#define END_MESSAGE_MAP() \
    } return ranBase_::ran_OnMsg(ranMsg_, ranW_, ranL_, ranRes_); }
#define ON_WM_PAINT()       case WM_PAINT:      OnPaint(); return TRUE;
#define ON_WM_CHAR()        case WM_CHAR:       OnChar((UINT)ranW_, (UINT)(ranL_ & 0xFFFF), (UINT)(ranL_ >> 16)); return TRUE;
#define ON_WM_KEYDOWN()     case WM_KEYDOWN:    OnKeyDown((UINT)ranW_, (UINT)(ranL_ & 0xFFFF), (UINT)(ranL_ >> 16)); return TRUE;
#define ON_WM_KEYUP()       case WM_KEYUP:      OnKeyUp((UINT)ranW_, (UINT)(ranL_ & 0xFFFF), (UINT)(ranL_ >> 16)); return TRUE;
#define ON_WM_SETFOCUS()    case WM_SETFOCUS:   OnSetFocus(nullptr); return TRUE;
#define ON_WM_KILLFOCUS()   case WM_KILLFOCUS:  OnKillFocus(nullptr); return TRUE;
#define ON_WM_SIZE()        case WM_SIZE:       OnSize((UINT)ranW_, (int)(short)(ranL_ & 0xFFFF), (int)(short)((ranL_ >> 16) & 0xFFFF)); return TRUE;
#define ON_WM_TIMER()       case WM_TIMER:      OnTimer((UINT)ranW_); return TRUE;
#define ON_WM_ACTIVATEAPP() case WM_ACTIVATEAPP: OnActivateApp((BOOL)ranW_, 0); return TRUE;
#define ON_WM_ACTIVATE()    case WM_ACTIVATE:   OnActivate((UINT)(ranW_ & 0xFFFF), nullptr, (BOOL)((ranW_ >> 16) & 0xFFFF)); return TRUE;
#define ON_WM_SETCURSOR()   case WM_SETCURSOR:  *ranRes_ = OnSetCursor(nullptr, (UINT)(ranL_ & 0xFFFF), (UINT)(ranL_ >> 16)); return TRUE;
#define ON_WM_MOUSEMOVE()   case WM_MOUSEMOVE:  OnMouseMove((UINT)ranW_, CPoint((int)(short)(ranL_ & 0xFFFF), (int)(short)((ranL_ >> 16) & 0xFFFF))); return TRUE;
#define ON_WM_GETMINMAXINFO() case WM_GETMINMAXINFO: OnGetMinMaxInfo((MINMAXINFO*)ranL_); return TRUE;
#define ON_WM_NCACTIVATE()  case WM_NCACTIVATE: *ranRes_ = OnNcActivate((BOOL)ranW_); return TRUE;
#define ON_WM_SYSCOMMAND()  case WM_SYSCOMMAND: OnSysCommand((UINT)ranW_, ranL_); return TRUE;
#define ON_WM_CREATE()
#define ON_WM_DESTROY()     case WM_DESTROY:    OnDestroy(); return TRUE;
#define ON_MESSAGE(msg, fn) case msg:           *ranRes_ = fn(ranW_, ranL_); return TRUE;
#define ON_COMMAND(id, fn)
#define RUNTIME_CLASS(c) nullptr
#define DECLARE_EVENTSINK_MAP()
#define BEGIN_EVENTSINK_MAP(c, b)
#define END_EVENTSINK_MAP()
#define ON_EVENT(c, id, dispid, fn, params)
#define DECLARE_DISPATCH_MAP()
#define BEGIN_DISPATCH_MAP(c, b)
#define END_DISPATCH_MAP()

// OLE automation dispatch flags / parameter-type strings used by MFC ActiveX wrappers.
typedef LONG DISPID;
#define DISPID_REFRESH     (-550)
#define DISPID_HWND        (-515)
#define DISPID_READYSTATE  (-525)
#define AFX_IDW_PANE_FIRST 0xE900
#define DISPATCH_METHOD         0x1
#define DISPATCH_PROPERTYGET    0x2
#define DISPATCH_PROPERTYPUT    0x4
#define VT_I2        2
#define VT_R4        4
#define VT_R8        5
#define VT_DISPATCH  9
#define VT_BOOL      11
#define VT_VARIANT   12
#define VT_UNKNOWN   13
#define VTS_I2       "\x02"
#define VTS_I4       "\x03"
#define VTS_R4       "\x04"
#define VTS_R8       "\x05"
#define VTS_BSTR     "\x08"
#define VTS_DISPATCH "\x09"
#define VTS_BOOL     "\x0B"
#define VTS_VARIANT  "\x0C"
#define VTS_PI4      "\x43"
#define VTS_PBOOL    "\x4B"
#define VTS_PVARIANT "\x4C"
#define VTS_NONE     ""
