// Native macOS entry point (plan Phase 2): replaces MFC's WinMain. The game's own CBasicApp
// (theApp) still does all of its startup in InitInstance and runs its own frame loop in Run;
// the compat layer calls back into this file for the three things Windows used to provide:
//   - the main window: CBasicWnd::Create -> CWnd::CreateEx -> CreateMainWindow (an SDL3 window;
//     with DXVK-native the SDL_Window* is the HWND that D3D9 presents to),
//   - the message pump: PeekMessage/GetMessage -> PumpMessages, which turns SDL events into the
//     window messages the game's message maps handle (WM_ACTIVATEAPP, WM_SIZE, ...) and WM_QUIT,
//   - the cursor position: GetCursorPos -> window-relative SDL mouse position.
// Environment: RAN_GAME_DIR=<client folder with data/> (see GetModuleFileName in win32/files.h).
#include "ran_compat.h"
#include "mfc/afx_all.h"
#include <SDL3/SDL.h>
#include <cstdio>
#include <cstdlib>
#include <string>

namespace {

SDL_Window* g_window = nullptr;

HWND CreateMainWindow(const char* title, int, int, int w, int h, DWORD)
{
    if (w < 640 || h < 480) { w = 1024; h = 768; }
    g_window = SDL_CreateWindow(title && *title ? title : "Ran Online", w, h,
                                SDL_WINDOW_VULKAN | SDL_WINDOW_RESIZABLE | SDL_WINDOW_HIGH_PIXEL_DENSITY);
    if (!g_window) std::fprintf(stderr, "[platform] SDL_CreateWindow failed: %s\n", SDL_GetError());
    return (HWND)g_window;
}

LPARAM MakeLParam(int lo, int hi) { return (LPARAM)(((DWORD)(WORD)lo) | (((DWORD)(WORD)hi) << 16)); }

void Send(UINT message, WPARAM w, LPARAM l)
{
    if (CWnd* wnd = CWnd::MainWindow()) wnd->SendMessage(message, w, l);
}

// Returns true if the app should quit.
bool Dispatch(const SDL_Event& e)
{
    switch (e.type) {
    case SDL_EVENT_QUIT:
    case SDL_EVENT_WINDOW_CLOSE_REQUESTED:
        return true;
    case SDL_EVENT_WINDOW_FOCUS_GAINED:
    case SDL_EVENT_WINDOW_FOCUS_LOST: {
        const BOOL active = e.type == SDL_EVENT_WINDOW_FOCUS_GAINED;
        Send(WM_ACTIVATEAPP, active, 0);
        Send(WM_NCACTIVATE, active, 0);
        Send(WM_ACTIVATE, active ? 1 /*WA_ACTIVE*/ : 0 /*WA_INACTIVE*/, 0);
        break;
    }
    case SDL_EVENT_WINDOW_PIXEL_SIZE_CHANGED:
        Send(WM_SIZE, 0 /*SIZE_RESTORED*/, MakeLParam(e.window.data1, e.window.data2));
        break;
    case SDL_EVENT_WINDOW_MINIMIZED:
        Send(WM_SIZE, 1 /*SIZE_MINIMIZED*/, 0);
        break;
    case SDL_EVENT_MOUSE_MOTION:
        Send(WM_MOUSEMOVE, 0, MakeLParam((int)e.motion.x, (int)e.motion.y));
        break;
    default:
        break;
    }
    return false;
}

BOOL PumpMessages(MSG* msg, BOOL wait, BOOL remove)
{
    if (msg) std::memset(msg, 0, sizeof(*msg));
    if (!remove) return SDL_HasEvents(SDL_EVENT_FIRST, SDL_EVENT_LAST) ? TRUE : FALSE;
    SDL_Event e;
    bool got = wait ? SDL_WaitEventTimeout(&e, 50) : SDL_PollEvent(&e);
    while (got) {
        if (Dispatch(e)) {
            if (msg) msg->message = WM_QUIT;
            return TRUE;
        }
        got = SDL_PollEvent(&e);
    }
    return FALSE;
}

BOOL CursorPos(POINT* p)
{
    float x = 0, y = 0;
    SDL_GetMouseState(&x, &y);
    if (p) { p->x = (LONG)x; p->y = (LONG)y; }
    return TRUE;
}

} // namespace

int main(int argc, char** argv)
{
    setenv("DXVK_WSI_DRIVER", "SDL3", 0);
    if (!SDL_Init(SDL_INIT_VIDEO | SDL_INIT_EVENTS)) {
        std::fprintf(stderr, "[platform] SDL_Init failed: %s\n", SDL_GetError());
        return 1;
    }
    CWnd::CreateMainWindowHook() = CreateMainWindow;
    ran_compat::MessageHook() = PumpMessages;
    ran_compat::CursorPosHook() = CursorPos;

    CWinApp* app = AfxGetApp();   // the game's theApp (CBasicApp)
    static std::string cmdLine;
    for (int i = 1; i < argc; ++i) { if (i > 1) cmdLine += ' '; cmdLine += argv[i]; }
    app->m_lpCmdLine = const_cast<char*>(cmdLine.c_str());

    int rc = 1;
    if (app->InitInstance()) rc = app->Run();
    else std::fprintf(stderr, "[platform] InitInstance failed (is RAN_GAME_DIR set to the client folder?)\n");
    if (g_window) SDL_DestroyWindow(g_window);
    SDL_Quit();
    return rc;
}
