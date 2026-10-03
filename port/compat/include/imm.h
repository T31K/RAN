// Stand-in for <imm.h> (Windows IME API) in the native macOS build. Phase 2 feeds SDL3 text
// input/composition events into the game's IME code; until then every context query reports
// "no IME context", which the game already handles (Windows returns the same with no IME).
#pragma once
#include "ran_compat.h"

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

inline HIMC    ImmGetContext(HWND) { return nullptr; }
inline BOOL    ImmReleaseContext(HWND, HIMC) { return TRUE; }
inline HIMC    ImmAssociateContext(HWND, HIMC) { return nullptr; }
inline BOOL    ImmIsIME(HKL) { return FALSE; }
inline BOOL    ImmGetOpenStatus(HIMC) { return FALSE; }
inline BOOL    ImmSetOpenStatus(HIMC, BOOL) { return FALSE; }
inline BOOL    ImmGetConversionStatus(HIMC, LPDWORD conv, LPDWORD sent) { if (conv) *conv = 0; if (sent) *sent = 0; return FALSE; }
inline BOOL    ImmSetConversionStatus(HIMC, DWORD, DWORD) { return FALSE; }
inline LONG    ImmGetCompositionStringA(HIMC, DWORD, void*, DWORD) { return 0; }
inline LONG    ImmGetCompositionStringW(HIMC, DWORD, void*, DWORD) { return 0; }
inline BOOL    ImmSetCompositionStringA(HIMC, DWORD, const void*, DWORD, const void*, DWORD) { return FALSE; }
inline DWORD   ImmGetCandidateListA(HIMC, DWORD, LPCANDIDATELIST, DWORD) { return 0; }
inline DWORD   ImmGetCandidateListW(HIMC, DWORD, LPCANDIDATELIST, DWORD) { return 0; }
inline BOOL    ImmNotifyIME(HIMC, DWORD, DWORD, DWORD) { return FALSE; }
inline BOOL    ImmSimulateHotKey(HWND, DWORD) { return FALSE; }
inline UINT    ImmGetVirtualKey(HWND) { return 0; }
inline HWND    ImmGetDefaultIMEWnd(HWND) { return nullptr; }
inline UINT    ImmGetIMEFileNameA(HKL, char* buf, UINT len) { if (buf && len) buf[0] = 0; return 0; }
inline BOOL    ImmDisableTextFrameService(DWORD) { return TRUE; }
inline DWORD   ImmGetProperty(HKL, DWORD) { return 0; }
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
