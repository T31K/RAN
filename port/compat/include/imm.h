// Stand-in for <imm.h> (Windows IME API) in the native macOS build. The macOS input method
// (e.g. the Korean 2-set keyboard) composes text; SDL3 reports it as text-editing (composition)
// and text-input (committed) events, which the platform pump stores in ran_compat::Ime() and
// turns into WM_IME_STARTCOMPOSITION / WM_IME_COMPOSITION / WM_IME_ENDCOMPOSITION for the
// focused window - the same sequence a Windows IME produces. CIMEEdit then reads the strings
// with ImmGetCompositionStringW exactly as on Windows.
#pragma once
#include "ran_compat.h"
#include <algorithm>
#include <cstring>
#include <string>

typedef void* HIMCC;

#define IMM_ERROR_NODATA  (-1)
#define IMM_ERROR_GENERAL (-2)

#define GCS_COMPREADSTR   0x0001
#define GCS_COMPREADATTR  0x0002
#define GCS_COMPREADCLAUSE 0x0004
#define GCS_COMPSTR       0x0008
#define GCS_COMPATTR      0x0010
#define GCS_COMPCLAUSE    0x0020
#define GCS_CURSORPOS     0x0080
#define GCS_DELTASTART    0x0100
#define GCS_RESULTREADSTR 0x0200
#define GCS_RESULTREADCLAUSE 0x0400
#define GCS_RESULTSTR     0x0800
#define GCS_RESULTCLAUSE  0x1000

#define IMN_CLOSESTATUSWINDOW  0x0001
#define IMN_OPENSTATUSWINDOW   0x0002
#define IMN_CHANGECANDIDATE    0x0003
#define IMN_CLOSECANDIDATE     0x0004
#define IMN_OPENCANDIDATE      0x0005
#define IMN_SETCONVERSIONMODE  0x0006
#define IMN_SETSENTENCEMODE    0x0007
#define IMN_SETOPENSTATUS      0x0008
#define IMN_SETCANDIDATEPOS    0x0009
#define IMN_SETCOMPOSITIONFONT 0x000A
#define IMN_SETCOMPOSITIONWINDOW 0x000B
#define IMN_SETSTATUSWINDOWPOS 0x000C
#define IMN_GUIDELINE          0x000D
#define IMN_PRIVATE            0x000E

#define NI_OPENCANDIDATE      0x0010
#define NI_CLOSECANDIDATE     0x0011
#define NI_SELECTCANDIDATESTR 0x0012
#define NI_CHANGECANDIDATELIST 0x0013
#define NI_COMPOSITIONSTR     0x0015
#define CPS_COMPLETE          0x0001
#define CPS_CONVERT           0x0002
#define CPS_REVERT            0x0003
#define CPS_CANCEL            0x0004

#define IME_CMODE_ALPHANUMERIC 0x0000
#define IME_CMODE_NATIVE       0x0001
#define IME_CMODE_HANGUL       IME_CMODE_NATIVE
#define IME_CMODE_KATAKANA     0x0002
#define IME_CMODE_FULLSHAPE    0x0008
#define IME_CMODE_ROMAN        0x0010
#define IME_SMODE_NONE         0x0000
#define IME_PROP_AT_CARET      0x00010000
#define IGP_PROPERTY           0x00000004

#define ISC_SHOWUICOMPOSITIONWINDOW 0x80000000
#define ISC_SHOWUIALLCANDIDATEWINDOW 0x0000000F

typedef struct tagCANDIDATELIST {
    DWORD dwSize;
    DWORD dwStyle;
    DWORD dwCount;
    DWORD dwSelection;
    DWORD dwPageStart;
    DWORD dwPageSize;
    DWORD dwOffset[1];
} CANDIDATELIST, *PCANDIDATELIST, *LPCANDIDATELIST;

#ifndef CFS_POINT
#define CFS_DEFAULT   0x0000
#define CFS_RECT      0x0001
#define CFS_POINT     0x0002
#define CFS_FORCE_POSITION 0x0020
#define CFS_CANDIDATEPOS   0x0040
#define CFS_EXCLUDE   0x0080
#endif
typedef struct tagCOMPOSITIONFORM {
    DWORD dwStyle;
    POINT ptCurrentPos;
    RECT  rcArea;
} COMPOSITIONFORM, *LPCOMPOSITIONFORM;

typedef struct tagCANDIDATEFORM {
    DWORD dwIndex;
    DWORD dwStyle;
    POINT ptCurrentPos;
    RECT  rcArea;
} CANDIDATEFORM, *LPCANDIDATEFORM;

typedef struct tagINPUTCONTEXT {
    HWND    hWnd;
    BOOL    fOpen;
    POINT   ptStatusWndPos;
    POINT   ptSoftKbdPos;
    DWORD   fdwConversion;
    DWORD   fdwSentence;
    union { LOGFONTA A; LOGFONTW W; } lfFont;
    COMPOSITIONFORM cfCompForm;
    CANDIDATEFORM   cfCandForm[4];
    HIMCC   hCompStr;
    HIMCC   hCandInfo;
    HIMCC   hGuideLine;
    HIMCC   hPrivate;
    DWORD   dwNumMsgBuf;
    HIMCC   hMsgBuf;
    DWORD   fdwInit;
    DWORD   dwReserve[3];
} INPUTCONTEXT, *PINPUTCONTEXT, *LPINPUTCONTEXT;

namespace ran_compat {
// The single input context: what the input method is composing and what it last committed.
struct ImeState
{
    std::u16string composition;   // GCS_COMPSTR
    std::u16string result;        // GCS_RESULTSTR
    bool composing = false;       // between WM_IME_STARTCOMPOSITION and WM_IME_ENDCOMPOSITION
    bool native = false;          // last text came from a non-Latin input method (Hangul mode)
    bool cancelRequested = false; // ImmNotifyIME(CPS_CANCEL/COMPLETE): the pump clears the IME
};
inline ImeState& Ime() { static ImeState s; return s; }

// Copies `bytes` of `src` into a caller buffer the way ImmGetCompositionString does: with no
// buffer (or size 0) it returns the size needed, otherwise the bytes copied.
inline LONG ImeCopy(const void* src, size_t bytes, void* buf, DWORD size)
{
    if (!buf || !size) return (LONG)bytes;
    const size_t n = std::min<size_t>(bytes, size);
    std::memcpy(buf, src, n);
    return (LONG)n;
}
} // namespace ran_compat

inline HIMC    ImmGetContext(HWND) { return (HIMC)&ran_compat::Ime(); }
inline BOOL    ImmReleaseContext(HWND, HIMC) { return TRUE; }
inline HIMC    ImmAssociateContext(HWND, HIMC) { return (HIMC)&ran_compat::Ime(); }
inline BOOL    ImmIsIME(HKL) { return TRUE; }
inline BOOL    ImmGetOpenStatus(HIMC) { return TRUE; }
inline BOOL    ImmSetOpenStatus(HIMC, BOOL) { return TRUE; }
inline BOOL    ImmGetConversionStatus(HIMC, LPDWORD conv, LPDWORD sent)
{
    if (conv) *conv = ran_compat::Ime().native ? IME_CMODE_NATIVE : IME_CMODE_ALPHANUMERIC;
    if (sent) *sent = IME_SMODE_NONE;
    return TRUE;
}
// The input source belongs to macOS (the user switches it with the system shortcut).
inline BOOL    ImmSetConversionStatus(HIMC, DWORD, DWORD) { return TRUE; }
inline LONG    ImmGetCompositionStringW(HIMC, DWORD index, void* buf, DWORD size)
{
    const ran_compat::ImeState& s = ran_compat::Ime();
    switch (index) {
    case GCS_RESULTSTR: return ran_compat::ImeCopy(s.result.data(), s.result.size() * 2, buf, size);
    case GCS_COMPSTR:   return ran_compat::ImeCopy(s.composition.data(), s.composition.size() * 2, buf, size);
    case GCS_COMPATTR: {   // every composing character is ATTR_INPUT (0)
        const std::string attrs(s.composition.size(), '\0');
        return ran_compat::ImeCopy(attrs.data(), attrs.size(), buf, size);
    }
    case GCS_COMPCLAUSE: {   // one clause covering the whole composition
        const DWORD clause[2] = { 0, (DWORD)s.composition.size() };
        return ran_compat::ImeCopy(clause, s.composition.empty() ? 0 : sizeof(clause), buf, size);
    }
    case GCS_CURSORPOS: return (LONG)s.composition.size();
    default: return IMM_ERROR_NODATA;
    }
}
inline LONG    ImmGetCompositionStringA(HIMC himc, DWORD index, void* buf, DWORD size)
{
    if (index != GCS_RESULTSTR && index != GCS_COMPSTR) return ImmGetCompositionStringW(himc, index, buf, size);
    const std::u16string& w = index == GCS_RESULTSTR ? ran_compat::Ime().result : ran_compat::Ime().composition;
    if (w.empty()) return 0;
    std::string mb(w.size() * 2 + 1, '\0');
    const int n = WideCharToMultiByte(CP_ACP, 0, (const WCHAR*)w.data(), (int)w.size(), &mb[0], (int)mb.size(), nullptr, nullptr);
    return ran_compat::ImeCopy(mb.data(), n > 0 ? (size_t)n : 0, buf, size);
}
inline BOOL    ImmSetCompositionStringA(HIMC, DWORD, const void*, DWORD, const void*, DWORD) { return FALSE; }
inline DWORD   ImmGetCandidateListA(HIMC, DWORD, LPCANDIDATELIST, DWORD) { return 0; }
inline DWORD   ImmGetCandidateListW(HIMC, DWORD, LPCANDIDATELIST, DWORD) { return 0; }
inline BOOL    ImmNotifyIME(HIMC, DWORD action, DWORD index, DWORD)
{
    if (action == NI_COMPOSITIONSTR && (index == CPS_CANCEL || index == CPS_COMPLETE)) {
        ran_compat::Ime().composition.clear();
        ran_compat::Ime().cancelRequested = true;
    }
    return TRUE;
}
inline BOOL    ImmSimulateHotKey(HWND, DWORD) { return FALSE; }
inline UINT    ImmGetVirtualKey(HWND) { return 0; }
inline HWND    ImmGetDefaultIMEWnd(HWND) { return nullptr; }
inline UINT    ImmGetIMEFileNameA(HKL, char* buf, UINT len) { if (buf && len) buf[0] = 0; return 0; }
inline BOOL    ImmDisableTextFrameService(DWORD) { return TRUE; }
// The game draws the composition inline at its caret (level-3 IME), like the Korean IME.
inline DWORD   ImmGetProperty(HKL, DWORD index) { return index == IGP_PROPERTY ? IME_PROP_AT_CARET : 0; }
inline BOOL    ImmSetCompositionWindow(HIMC, LPCOMPOSITIONFORM) { return FALSE; }
inline BOOL    ImmSetCandidateWindow(HIMC, LPCANDIDATEFORM) { return FALSE; }
inline LPINPUTCONTEXT ImmLockIMC(HIMC) { return nullptr; }
inline BOOL    ImmUnlockIMC(HIMC) { return FALSE; }
inline void*   ImmLockIMCC(HIMCC) { return nullptr; }
inline BOOL    ImmUnlockIMCC(HIMCC) { return FALSE; }
#define ImmGetCompositionString ImmGetCompositionStringA
#define ImmSetCompositionString ImmSetCompositionStringA
#define ImmGetCandidateList     ImmGetCandidateListA
#define ImmGetIMEFileName       ImmGetIMEFileNameA
