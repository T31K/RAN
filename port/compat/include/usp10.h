// Stand-in for <usp10.h> (Uniscribe) in the native macOS build. DXUT's GUI loads Uniscribe at
// run time and falls back when it is missing; these declarations keep it compiling and the
// functions report "not implemented". Text shaping moves to CoreText in Phase 3.
#pragma once
#include "ran_compat.h"

#ifndef E_NOTIMPL
#define E_NOTIMPL ((HRESULT)0x80004001L)
#endif

typedef void* SCRIPT_STRING_ANALYSIS;

typedef struct tag_SCRIPT_CONTROL {
    DWORD uDefaultLanguage    : 16;
    DWORD fContextDigits      : 1;
    DWORD fInvertPreBoundDir  : 1;
    DWORD fInvertPostBoundDir : 1;
    DWORD fLinkStringBefore   : 1;
    DWORD fLinkStringAfter    : 1;
    DWORD fNeutralOverride    : 1;
    DWORD fNumericOverride    : 1;
    DWORD fLegacyBidiClass    : 1;
    DWORD fMergeNeutralItems  : 1;
    DWORD fReserved           : 7;
} SCRIPT_CONTROL;

typedef struct tag_SCRIPT_STATE {
    WORD uBidiLevel         : 5;
    WORD fOverrideDirection : 1;
    WORD fInhibitSymSwap    : 1;
    WORD fCharShape         : 1;
    WORD fDigitSubstitute   : 1;
    WORD fInhibitLigate     : 1;
    WORD fDisplayZWG        : 1;
    WORD fArabicNumContext  : 1;
    WORD fGcpClusters       : 1;
    WORD fReserved          : 1;
    WORD fEngineReserved    : 2;
} SCRIPT_STATE;

typedef struct tag_SCRIPT_LOGATTR {
    BYTE fSoftBreak  : 1;
    BYTE fWhiteSpace : 1;
    BYTE fCharStop   : 1;
    BYTE fWordStop   : 1;
    BYTE fInvalid    : 1;
    BYTE fReserved   : 3;
} SCRIPT_LOGATTR;

typedef struct tag_SCRIPT_TABDEF {
    int  cTabStops;
    int  iScale;
    int* pTabStops;
    int  iTabOrigin;
} SCRIPT_TABDEF;

typedef struct tag_SCRIPT_DIGITSUBSTITUTE {
    DWORD NationalDigitLanguage    : 16;
    DWORD TraditionalDigitLanguage : 16;
    DWORD DigitSubstitute          : 8;
    DWORD dwReserved;
} SCRIPT_DIGITSUBSTITUTE;

#define SSA_PASSWORD 0x00000001
#define SSA_TAB      0x00000002
#define SSA_CLIP     0x00000004
#define SSA_FIT      0x00000008
#define SSA_DZWG     0x00000010
#define SSA_FALLBACK 0x00000020
#define SSA_BREAK    0x00000040
#define SSA_GLYPHS   0x00000080
#define SSA_RTL      0x00000100
#define SSA_GCP      0x00000200
#define SSA_HOTKEY   0x00000400
#define SSA_METAFILE 0x00000800
#define SSA_LINK     0x00001000

inline HRESULT ScriptApplyDigitSubstitution(const SCRIPT_DIGITSUBSTITUTE*, SCRIPT_CONTROL*, SCRIPT_STATE*) { return E_NOTIMPL; }
inline HRESULT ScriptStringAnalyse(HDC, const void*, int, int, int, DWORD, int, SCRIPT_CONTROL*, SCRIPT_STATE*,
                                   const int*, SCRIPT_TABDEF*, const BYTE*, SCRIPT_STRING_ANALYSIS*) { return E_NOTIMPL; }
inline HRESULT ScriptStringFree(SCRIPT_STRING_ANALYSIS*) { return E_NOTIMPL; }
inline HRESULT ScriptStringCPtoX(SCRIPT_STRING_ANALYSIS, int, BOOL, int*) { return E_NOTIMPL; }
inline HRESULT ScriptStringXtoCP(SCRIPT_STRING_ANALYSIS, int, int*, int*) { return E_NOTIMPL; }
inline const SCRIPT_LOGATTR* ScriptString_pLogAttr(SCRIPT_STRING_ANALYSIS) { return nullptr; }
inline const int* ScriptString_pcOutChars(SCRIPT_STRING_ANALYSIS) { return nullptr; }
