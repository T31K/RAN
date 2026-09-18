#!/usr/bin/env python3
# -*- coding: utf-8 -*-
"""Fix Cmd+Tab input loss: reactivate DirectInput on WM_ACTIVATEAPP + per-frame
self-heal. CP949 sources -> latin-1 round-trip, ASCII-only anchors and insertions."""
import io, sys

def edit(path, pairs):
    src = io.open(path, encoding='latin-1', newline='').read()
    for old, new in pairs:
        if new in src:
            # already applied (works even when `old` is a substring of `new`)
            print(f'SKIP (already applied): {path}')
            continue
        assert src.count(old) == 1, f'anchor not unique/found in {path}:\n{old!r}'
        src = src.replace(old, new)
    io.open(path, 'w', encoding='latin-1', newline='').write(src)
    print(f'OK: {path}')

# --- 1. DxInputDevice.cpp: transition-logged Acquire + self-heal ------------
DXI = '[Lib]__Engine/Sources/DxTools/DxInputDevice.cpp'

old_onactivate = """	if ( !m_pDInputKeyboardDev )	return S_FALSE;

	m_bActive = bActive;
"""
new_onactivate = """	if ( !m_pDInputKeyboardDev )	return S_FALSE;

	CDebugSet::ToLogFile ( "[INPUTDBG] DxInputDevice::OnActivate(%d)", bActive );
	m_bActive = bActive;
"""

old_process = """BOOL DxInputDevice::ProcessKeyState()
{
	HRESULT hr = S_OK;
	if ( !m_bActive )	return FALSE;

	hr =  m_pDInputKeyboardDev->Acquire();
	if ( SUCCEEDED(hr) )
"""
new_process = """BOOL DxInputDevice::ProcessKeyState()
{
	HRESULT hr = S_OK;

	//	winemac.drv never delivers WM_NCACTIVATE(TRUE) to this borderless
	//	window after Cmd+Tab, so nobody calls OnActivate(TRUE); reacquire
	//	ourselves as soon as the OS reports the window foreground again.
	BOOL bForeground = ( ::GetForegroundWindow()==m_hWnd );
	static BOOL s_bLastForeground = TRUE;
	if ( bForeground != s_bLastForeground )
	{
		CDebugSet::ToLogFile ( "[INPUTDBG] foreground=%d active=%d", bForeground, m_bActive );
		s_bLastForeground = bForeground;
	}
	if ( !m_bActive )
	{
		if ( bForeground )	OnActivate ( TRUE );
		if ( !m_bActive )	return FALSE;
	}

	hr =  m_pDInputKeyboardDev->Acquire();
	static HRESULT s_hrLastKbAcquire = (HRESULT)0xDEADBEEF;
	if ( hr != s_hrLastKbAcquire )
	{
		CDebugSet::ToLogFile ( "[INPUTDBG] kb Acquire hr=0x%08x", hr );
		s_hrLastKbAcquire = hr;
	}
	if ( SUCCEEDED(hr) )
"""

old_mouse_acq = """	hr =  m_pDInputMouseDev->Acquire();
	if ( SUCCEEDED(hr) )
	{
		m_dwMouseElements = MOUSE_BUFFER_SIZE;
"""
new_mouse_acq = """	hr =  m_pDInputMouseDev->Acquire();
	static HRESULT s_hrLastMsAcquire = (HRESULT)0xDEADBEEF;
	if ( hr != s_hrLastMsAcquire )
	{
		CDebugSet::ToLogFile ( "[INPUTDBG] mouse Acquire hr=0x%08x", hr );
		s_hrLastMsAcquire = hr;
	}
	if ( SUCCEEDED(hr) )
	{
		m_dwMouseElements = MOUSE_BUFFER_SIZE;
"""

edit(DXI, [(old_onactivate, new_onactivate),
           (old_process, new_process),
           (old_mouse_acq, new_mouse_acq)])

# --- 2. BasicWnd.cpp: WM_ACTIVATEAPP drives input reactivation -------------
BW = '[Client]__Game/Sources/BasicWnd.cpp'

old_include = '#include "DxCursor.h"\n'
new_include = '#include "DxCursor.h"\n#include "DxInputDevice.h"\n#include "DebugSet.h"\n'

old_appact = """	CWnd::OnActivateApp(bActive, hTask);
	//m_pApp->SetActive(bActive);
"""
new_appact = """	CWnd::OnActivateApp(bActive, hTask);
	//	Cmd+Tab back under winemac.drv arrives as app activation only
	//	(no WM_NCACTIVATE for a borderless WS_POPUP window) - reacquire here.
	CDebugSet::ToLogFile ( "[INPUTDBG] WM_ACTIVATEAPP bActive=%d", bActive );
	DxInputDevice::GetInstance().OnActivate ( bActive );
	//m_pApp->SetActive(bActive);
"""

edit(BW, [(old_include, new_include), (old_appact, new_appact)])

# --- 3. BasicWndD3d.cpp: log the two existing activation handlers ----------
BWD = '[Client]__Game/Sources/BasicWndD3d.cpp'

old_ncact = """	DxInputDevice::GetInstance().OnActivate ( bActive );
"""
new_ncact = """	CDebugSet::ToLogFile ( "[INPUTDBG] WM_NCACTIVATE bActive=%d", bActive );
	DxInputDevice::GetInstance().OnActivate ( bActive );
"""

old_wmact = """	m_pApp->SetActive ( !bMinimized );
"""
new_wmact = """	CDebugSet::ToLogFile ( "[INPUTDBG] WM_ACTIVATE nState=%u bMin=%d", nState, (int)bMinimized );
	m_pApp->SetActive ( !bMinimized );
"""

edit(BWD, [(old_ncact, new_ncact), (old_wmact, new_wmact)])

# --- 4. DxResponseMan.cpp: don't spawn Notepad with the log on client exit --
#	DebugSet.cpp: `if (m_bLogWrite && m_bLogFileFinalOpen) system("notepad ...")`
#	on shutdown. Our [INPUTDBG] logging sets m_bLogWrite, so Notepad popped up
#	every close. bLogFileFinalOpen=true is a dev convenience; a shipped game
#	must never auto-open Notepad, so pass false.
DRM = '[Lib]__Engine/Sources/DxResponseMan.cpp'
edit(DRM, [("""	CDebugSet::OneTimeSceneInit ( szPROFILE, true );""",
            """	CDebugSet::OneTimeSceneInit ( szPROFILE, false );""")])

print('All patches applied.')
