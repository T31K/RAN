#!/usr/bin/env python3
"""Snap the full-size game window back after Wine moves/resizes it (Cmd+Tab).

Source files are CP949; read/write as latin-1 so Korean bytes pass through.
Idempotent: skips if the helper is already present.
"""
import sys

PATH = "[Client]__Game/Sources/BasicWndD3d.cpp"

HELPER = r'''
//	Wine/macOS can leave the full-size game window at the wrong position or
//	size after Cmd+Tab back. Mouse coords are window-relative, so every click
//	then lands off target (UI hit-tests miss). Log the window state, and put
//	the window back where DxGlobalStage::ToFullSize() would.
static void LogWindowState ( HWND hWnd, const char* szTag )
{
	RECT rtWnd, rtClient, rtClip;
	::GetWindowRect ( hWnd, &rtWnd );
	::GetClientRect ( hWnd, &rtClient );
	::GetClipCursor ( &rtClip );
	POINT ptOrg = { 0, 0 };
	::ClientToScreen ( hWnd, &ptOrg );
	POINT ptCur;
	::GetCursorPos ( &ptCur );
	CDebugSet::ToLogFile ( "[WNDDBG] %s wnd=%d,%d,%d,%d client=%dx%d org=%d,%d cursor=%d,%d clip=%d,%d,%d,%d screen=%dx%d style=0x%08x fullsize=%d",
		szTag, rtWnd.left, rtWnd.top, rtWnd.right, rtWnd.bottom, rtClient.right, rtClient.bottom,
		ptOrg.x, ptOrg.y, ptCur.x, ptCur.y, rtClip.left, rtClip.top, rtClip.right, rtClip.bottom,
		GetSystemMetrics(SM_CXSCREEN), GetSystemMetrics(SM_CYSCREEN),
		(unsigned)::GetWindowLong ( hWnd, GWL_STYLE ), (int)RANPARAM::bScrWndFullSize );
}

static void FixFullSizeWindow ( HWND hWnd )
{
	if ( !hWnd || !RANPARAM::bScrWindowed || !RANPARAM::bScrWndFullSize )	return;
	if ( ::GetForegroundWindow() != hWnd || ::IsIconic ( hWnd ) )			return;

	int nScreenX = GetSystemMetrics(SM_CXSCREEN);
	int nScreenY = GetSystemMetrics(SM_CYSCREEN);
	if ( nScreenX / float(nScreenY) > (16.0f/9.0f) )	nScreenX = nScreenX/2;

	RECT rtWnd;
	::GetWindowRect ( hWnd, &rtWnd );
	if ( rtWnd.left==0 && rtWnd.top==0 && rtWnd.right==nScreenX && rtWnd.bottom==nScreenY )	return;

	static DWORD s_dwLastFix = 0;
	DWORD dwNow = ::GetTickCount ();
	if ( dwNow - s_dwLastFix < 500 )	return;
	s_dwLastFix = dwNow;

	LogWindowState ( hWnd, "drifted -> fix" );
	::SetWindowLong ( hWnd, GWL_STYLE, WS_POPUP | WS_VISIBLE );
	::MoveWindow ( hWnd, 0, 0, nScreenX, nScreenY, TRUE );
	LogWindowState ( hWnd, "after fix" );
}

HRESULT CBasicWnd::FrameMove()
{
	PROFILE_BLOCKSTART();

	PROFILE_BEGIN("FrameMove");

	FixFullSizeWindow ( CWnd::GetSafeHwnd () );
'''

OLD_FM = '''
HRESULT CBasicWnd::FrameMove()
{
	PROFILE_BLOCKSTART();

	PROFILE_BEGIN("FrameMove");
'''

OLD_ACT = '''	CDebugSet::ToLogFile ( "[INPUTDBG] WM_ACTIVATE nState=%u bMin=%d", nState, (int)bMinimized );
'''
NEW_ACT = OLD_ACT + '''	LogWindowState ( CWnd::GetSafeHwnd (), nState!=WA_INACTIVE ? "WM_ACTIVATE on" : "WM_ACTIVATE off" );
'''

src = open(PATH, encoding="latin-1", newline="").read()
if "FixFullSizeWindow" in src:
    print("already patched"); sys.exit(0)
nl = "\r\n" if "\r\n" in src else "\n"
fix = lambda s: s.replace("\n", nl)
for old in (OLD_FM, OLD_ACT):
    assert src.count(fix(old)) == 1, f"anchor not unique/missing: {old[:50]!r}"
src = src.replace(fix(OLD_FM), fix(HELPER), 1).replace(fix(OLD_ACT), fix(NEW_ACT), 1)
open(PATH, "w", encoding="latin-1", newline="").write(src)
print("patched", PATH)
