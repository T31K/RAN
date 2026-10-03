// user32 constants and structs (window messages, virtual keys, styles, message-box flags) for the
// native macOS build. Values match WinUser.h. The SDL3 platform layer (Phase 2) translates real
// events into these, so game code that switches on WM_*/VK_* keeps working.
#pragma once
#include "win32/extra_types.h"

#define CW_USEDEFAULT       ((int)0x80000000)

// Window messages.
#define WM_NULL             0x0000
#define WM_CREATE           0x0001
#define WM_DESTROY          0x0002
#define WM_MOVE             0x0003
#define WM_SIZE             0x0005
#define WM_ACTIVATE         0x0006
#define WM_SETFOCUS         0x0007
#define WM_KILLFOCUS        0x0008
#define WM_ENABLE           0x000A
#define WM_PAINT            0x000F
#define WM_CLOSE            0x0010
#define WM_QUIT             0x0012
#define WM_ERASEBKGND       0x0014
#define WM_SHOWWINDOW       0x0018
#define WM_ACTIVATEAPP      0x001C
#define WM_SETCURSOR        0x0020
#define WM_MOUSEACTIVATE    0x0021
#define WM_GETMINMAXINFO    0x0024
#define WM_WINDOWPOSCHANGING 0x0046
#define WM_WINDOWPOSCHANGED 0x0047
#define WM_NCCREATE         0x0081
#define WM_NCDESTROY        0x0082
#define WM_NCHITTEST        0x0084
#define WM_NCPAINT          0x0085
#define WM_NCACTIVATE       0x0086
#define WM_GETDLGCODE       0x0087
#define WM_NCMOUSEMOVE      0x00A0
#define WM_KEYFIRST         0x0100
#define WM_KEYDOWN          0x0100
#define WM_KEYUP            0x0101
#define WM_CHAR             0x0102
#define WM_DEADCHAR         0x0103
#define WM_SYSKEYDOWN       0x0104
#define WM_SYSKEYUP         0x0105
#define WM_SYSCHAR          0x0106
#define WM_KEYLAST          0x0109
#define WM_IME_STARTCOMPOSITION 0x010D
#define WM_IME_ENDCOMPOSITION   0x010E
#define WM_IME_COMPOSITION      0x010F
#define WM_IME_KEYLAST          0x010F
#define WM_INITDIALOG       0x0110
#define WM_COMMAND          0x0111
#define WM_SYSCOMMAND       0x0112
#define WM_TIMER            0x0113
#define WM_HSCROLL          0x0114
#define WM_VSCROLL          0x0115
#define WM_INITMENU         0x0116
#define WM_MENUCHAR         0x0120
#define WM_ENTERIDLE        0x0121
#define WM_MOUSEFIRST       0x0200
#define WM_MOUSEMOVE        0x0200
#define WM_LBUTTONDOWN      0x0201
#define WM_LBUTTONUP        0x0202
#define WM_LBUTTONDBLCLK    0x0203
#define WM_RBUTTONDOWN      0x0204
#define WM_RBUTTONUP        0x0205
#define WM_RBUTTONDBLCLK    0x0206
#define WM_MBUTTONDOWN      0x0207
#define WM_MBUTTONUP        0x0208
#define WM_MBUTTONDBLCLK    0x0209
#define WM_MOUSEWHEEL       0x020A
#define WM_XBUTTONDOWN      0x020B
#define WM_XBUTTONUP        0x020C
#define WM_MOUSELAST        0x020D
#define WM_SIZING           0x0214
#define WM_CAPTURECHANGED   0x0215
#define WM_MOVING           0x0216
#define WM_POWERBROADCAST   0x0218
#define WM_ENTERSIZEMOVE    0x0231
#define WM_EXITSIZEMOVE     0x0232
#define WM_IME_SETCONTEXT   0x0281
#define WM_IME_NOTIFY       0x0282
#define WM_IME_CONTROL      0x0283
#define WM_IME_COMPOSITIONFULL 0x0284
#define WM_IME_SELECT       0x0285
#define WM_IME_CHAR         0x0286
#define WM_IME_REQUEST      0x0288
#define WM_IME_KEYDOWN      0x0290
#define WM_IME_KEYUP        0x0291
#define WM_MOUSELEAVE       0x02A3
#define WM_DISPLAYCHANGE    0x007E
#define WM_INPUTLANGCHANGE  0x0051
#define WM_USER             0x0400
#define WM_APP              0x8000

// WM_ACTIVATE / WM_SIZE parameters.
#define WA_INACTIVE         0
#define WA_ACTIVE           1
#define WA_CLICKACTIVE      2
#define SIZE_RESTORED       0
#define SIZE_MINIMIZED      1
#define SIZE_MAXIMIZED      2

// Mouse key-state flags (wParam of mouse messages).
#define MK_LBUTTON          0x0001
#define MK_RBUTTON          0x0002
#define MK_SHIFT            0x0004
#define MK_CONTROL          0x0008
#define MK_MBUTTON          0x0010
#define WHEEL_DELTA         120
#define GET_WHEEL_DELTA_WPARAM(wParam) ((short)HIWORD(wParam))
#define GET_X_LPARAM(lp)    ((int)(short)LOWORD(lp))
#define GET_Y_LPARAM(lp)    ((int)(short)HIWORD(lp))

// Virtual keys.
#define VK_LBUTTON   0x01
#define VK_RBUTTON   0x02
#define VK_CANCEL    0x03
#define VK_MBUTTON   0x04
#define VK_BACK      0x08
#define VK_TAB       0x09
#define VK_CLEAR     0x0C
#define VK_RETURN    0x0D
#define VK_SHIFT     0x10
#define VK_CONTROL   0x11
#define VK_MENU      0x12
#define VK_PAUSE     0x13
#define VK_CAPITAL   0x14
#define VK_HANGUL    0x15
#define VK_HANJA     0x19
#define VK_ESCAPE    0x1B
#define VK_CONVERT   0x1C
#define VK_NONCONVERT 0x1D
#define VK_PROCESSKEY 0xE5
#define VK_SPACE     0x20
#define VK_PRIOR     0x21
#define VK_NEXT      0x22
#define VK_END       0x23
#define VK_HOME      0x24
#define VK_LEFT      0x25
#define VK_UP        0x26
#define VK_RIGHT     0x27
#define VK_DOWN      0x28
#define VK_SELECT    0x29
#define VK_SNAPSHOT  0x2C
#define VK_INSERT    0x2D
#define VK_DELETE    0x2E
#define VK_LWIN      0x5B
#define VK_RWIN      0x5C
#define VK_APPS      0x5D
#define VK_NUMPAD0   0x60
#define VK_NUMPAD1   0x61
#define VK_NUMPAD2   0x62
#define VK_NUMPAD3   0x63
#define VK_NUMPAD4   0x64
#define VK_NUMPAD5   0x65
#define VK_NUMPAD6   0x66
#define VK_NUMPAD7   0x67
#define VK_NUMPAD8   0x68
#define VK_NUMPAD9   0x69
#define VK_MULTIPLY  0x6A
#define VK_ADD       0x6B
#define VK_SEPARATOR 0x6C
#define VK_SUBTRACT  0x6D
#define VK_DECIMAL   0x6E
#define VK_DIVIDE    0x6F
#define VK_F1        0x70
#define VK_F2        0x71
#define VK_F3        0x72
#define VK_F4        0x73
#define VK_F5        0x74
#define VK_F6        0x75
#define VK_F7        0x76
#define VK_F8        0x77
#define VK_F9        0x78
#define VK_F10       0x79
#define VK_F11       0x7A
#define VK_F12       0x7B
#define VK_NUMLOCK   0x90
#define VK_SCROLL    0x91
#define VK_LSHIFT    0xA0
#define VK_RSHIFT    0xA1
#define VK_LCONTROL  0xA2
#define VK_RCONTROL  0xA3
#define VK_LMENU     0xA4
#define VK_RMENU     0xA5
#define VK_OEM_1     0xBA
#define VK_OEM_PLUS  0xBB
#define VK_OEM_COMMA 0xBC
#define VK_OEM_MINUS 0xBD
#define VK_OEM_PERIOD 0xBE
#define VK_OEM_2     0xBF
#define VK_OEM_3     0xC0
#define VK_OEM_4     0xDB
#define VK_OEM_5     0xDC
#define VK_OEM_6     0xDD
#define VK_OEM_7     0xDE

// Window styles.
#define WS_OVERLAPPED       0x00000000L
#define WS_POPUP            0x80000000L
#define WS_CHILD            0x40000000L
#define WS_MINIMIZE         0x20000000L
#define WS_VISIBLE          0x10000000L
#define WS_DISABLED         0x08000000L
#define WS_CLIPSIBLINGS     0x04000000L
#define WS_CLIPCHILDREN     0x02000000L
#define WS_MAXIMIZE         0x01000000L
#define WS_CAPTION          0x00C00000L
#define WS_BORDER           0x00800000L
#define WS_DLGFRAME         0x00400000L
#define WS_VSCROLL          0x00200000L
#define WS_HSCROLL          0x00100000L
#define WS_SYSMENU          0x00080000L
#define WS_THICKFRAME       0x00040000L
#define WS_MINIMIZEBOX      0x00020000L
#define WS_MAXIMIZEBOX      0x00010000L
#define WS_OVERLAPPEDWINDOW (WS_OVERLAPPED | WS_CAPTION | WS_SYSMENU | WS_THICKFRAME | WS_MINIMIZEBOX | WS_MAXIMIZEBOX)
#define WS_EX_TOPMOST       0x00000008L
#define WS_EX_APPWINDOW     0x00040000L

// ShowWindow / SetWindowPos.
#define SW_HIDE             0
#define SW_SHOWNORMAL       1
#define SW_NORMAL           1
#define SW_SHOWMINIMIZED    2
#define SW_SHOWMAXIMIZED    3
#define SW_MAXIMIZE         3
#define SW_SHOW             5
#define SW_MINIMIZE         6
#define SW_RESTORE          9
#define SWP_NOSIZE          0x0001
#define SWP_NOMOVE          0x0002
#define SWP_NOZORDER        0x0004
#define SWP_NOACTIVATE      0x0010
#define SWP_FRAMECHANGED    0x0020
#define SWP_SHOWWINDOW      0x0040
#define HWND_TOP            ((HWND)0)
#define HWND_TOPMOST        ((HWND)(intptr_t)-1)
#define HWND_NOTOPMOST      ((HWND)(intptr_t)-2)
#define GWL_STYLE           (-16)
#define GWL_EXSTYLE         (-20)

// Message boxes.
#define MB_OK               0x00000000L
#define MB_OKCANCEL         0x00000001L
#define MB_YESNOCANCEL      0x00000003L
#define MB_YESNO            0x00000004L
#define MB_RETRYCANCEL      0x00000005L
#define MB_ICONHAND         0x00000010L
#define MB_ICONERROR        MB_ICONHAND
#define MB_ICONSTOP         MB_ICONHAND
#define MB_ICONQUESTION     0x00000020L
#define MB_ICONEXCLAMATION  0x00000030L
#define MB_ICONWARNING      MB_ICONEXCLAMATION
#define MB_ICONASTERISK     0x00000040L
#define MB_ICONINFORMATION  MB_ICONASTERISK
#define MB_SYSTEMMODAL      0x00001000L
#define MB_TOPMOST          0x00040000L
#define IDOK                1
#define IDCANCEL            2
#define IDABORT             3
#define IDRETRY             4
#define IDIGNORE            5
#define IDYES               6
#define IDNO                7

// System metrics.
#define SM_CXSCREEN         0
#define SM_CYSCREEN         1

// DrawText flags.
#define DT_TOP          0x00000000
#define DT_LEFT         0x00000000
#define DT_CENTER       0x00000001
#define DT_RIGHT        0x00000002
#define DT_VCENTER      0x00000004
#define DT_BOTTOM       0x00000008
#define DT_WORDBREAK    0x00000010
#define DT_SINGLELINE   0x00000020
#define DT_EXPANDTABS   0x00000040
#define DT_NOCLIP       0x00000100
#define DT_CALCRECT     0x00000400
#define DT_NOPREFIX     0x00000800
#define DT_END_ELLIPSIS 0x00008000

// user32 rectangle helpers (right/bottom exclusive, like Windows).
inline BOOL PtInRect(const RECT* r, POINT p) { return p.x >= r->left && p.x < r->right && p.y >= r->top && p.y < r->bottom; }
inline BOOL SetRect(RECT* r, int l, int t, int rr, int b) { r->left = l; r->top = t; r->right = rr; r->bottom = b; return TRUE; }
inline BOOL SetRectEmpty(RECT* r) { return SetRect(r, 0, 0, 0, 0); }
inline BOOL CopyRect(RECT* d, const RECT* s) { *d = *s; return TRUE; }
inline BOOL IsRectEmpty(const RECT* r) { return r->right <= r->left || r->bottom <= r->top; }
inline BOOL OffsetRect(RECT* r, int dx, int dy) { r->left += dx; r->right += dx; r->top += dy; r->bottom += dy; return TRUE; }
inline BOOL InflateRect(RECT* r, int dx, int dy) { r->left -= dx; r->right += dx; r->top -= dy; r->bottom += dy; return TRUE; }
inline BOOL EqualRect(const RECT* a, const RECT* b) { return a->left == b->left && a->top == b->top && a->right == b->right && a->bottom == b->bottom; }
inline BOOL IntersectRect(RECT* d, const RECT* a, const RECT* b)
{
    d->left = a->left > b->left ? a->left : b->left;
    d->top = a->top > b->top ? a->top : b->top;
    d->right = a->right < b->right ? a->right : b->right;
    d->bottom = a->bottom < b->bottom ? a->bottom : b->bottom;
    if (IsRectEmpty(d)) { SetRectEmpty(d); return FALSE; }
    return TRUE;
}
inline BOOL UnionRect(RECT* d, const RECT* a, const RECT* b)
{
    if (IsRectEmpty(a)) { *d = *b; return !IsRectEmpty(b); }
    if (IsRectEmpty(b)) { *d = *a; return TRUE; }
    d->left = a->left < b->left ? a->left : b->left;
    d->top = a->top < b->top ? a->top : b->top;
    d->right = a->right > b->right ? a->right : b->right;
    d->bottom = a->bottom > b->bottom ? a->bottom : b->bottom;
    return TRUE;
}

// lstr* on 2-byte WCHAR / narrow strings.
inline int lstrlenW(const WCHAR* s) { int n = 0; if (s) while (s[n]) ++n; return n; }
inline int lstrlenA(const char* s) { return s ? (int)std::strlen(s) : 0; }
#define lstrlen lstrlenA

typedef struct tagMSG {
    HWND   hwnd;
    UINT   message;
    WPARAM wParam;
    LPARAM lParam;
    DWORD  time;
    POINT  pt;
} MSG, *PMSG, *LPMSG;

typedef struct tagMINMAXINFO {
    POINT ptReserved, ptMaxSize, ptMaxPosition, ptMinTrackSize, ptMaxTrackSize;
} MINMAXINFO, *LPMINMAXINFO;
