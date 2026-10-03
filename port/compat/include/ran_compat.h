// Single entry point of the native macOS compat layer. Every stand-in Windows/MFC header
// in this directory (afx.h, tchar.h, basetsd.h, ...) includes this file.
#pragma once
// The game and the legacy DirectX SDK headers test WIN32 (not _WIN32) for "Windows API
// available"; e.g. ddraw.h declares its own 64-bit `long HRESULT` when WIN32 is missing.
// _WIN32 stays undefined so DXVK, libc++ and the macOS SDK keep their native paths.
#ifndef WIN32
#define WIN32 1
#endif
#include <windows.h>            // DXVK native: base types, COM macros, GUID, IUnknown, RECT/POINT
#include "win32/extra_types.h"
#include "win32/gdi_types.h"
#include "win32/user_types.h"
#include "win32/kernel.h"
#include "win32/files.h"
#include "mfc/afx_string.h"
#include "mfc/afx_types.h"
