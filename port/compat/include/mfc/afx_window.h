// MFC window classes for the native macOS build. The game's window, input and dialogs move to
// the SDL3 platform layer in Phase 2; until then these keep the declarations that mention them
// (editor/debug helpers, the Game shell) compiling. Members are added as the code needs them.
#pragma once
#include "ran_compat.h"

class CObject
{
public:
    virtual ~CObject() = default;
};

class CCmdTarget : public CObject {};

class CWnd : public CCmdTarget
{
public:
    HWND m_hWnd = nullptr;

    HWND GetSafeHwnd() const { return m_hWnd; }
    operator HWND() const { return m_hWnd; }
};
