// Everything the MFC stand-in headers (afx.h, afxwin.h, ...) provide. MFC is a Windows UI
// framework; the game uses its value types (CString, CTime, CRect, ...) and a few window/GDI
// classes that the Phase 2 SDL3 platform layer replaces.
#pragma once
#include "ran_compat.h"
#include "mfc/afx_window.h"

// MFC diagnostics. TRACE/ASSERT/VERIFY follow MFC: active only in debug builds.
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
