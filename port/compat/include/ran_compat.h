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

// On Windows DWORD (unsigned long) and UINT (unsigned int) are distinct types; here both are
// uint32_t. Game code that overloads on both compiles the UINT overload out under this macro.
#define RAN_DWORD_IS_UINT 1

// unixODBC (headers for the server-side DbAction code): use our Windows types instead of its
// own, which would make DWORD 8 bytes.
#define ALREADY_HAVE_WINDOWS_TYPE 1
#include "win32/extra_types.h"
#include "win32/gdi_types.h"
#include "win32/user_types.h"
#include "win32/odbc_types.h"
#include "win32/kernel.h"
#include "win32/fpu.h"
#include "win32/files.h"
#include "win32/codepage.h"
#include "win32/wchar16.h"
#include "win32/crt_io.h"
#include "win32/registry.h"
#include "win32/shell.h"
#include "win32/system.h"
#include "win32/crypt.h"
#include "mfc/afx_string.h"
#include "mfc/afx_types.h"

// POSIX macros whose names the game uses as identifiers (enum values etc.). The system headers
// that define them are pulled in here first, so the #undef sticks (they are include-guarded).
#include <csignal>
#undef SS_DISABLE      // <sys/signal.h>; DxEffectMan.h enum value
#undef SS_ONSTACK
