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
#include "input_map.h"
#include "input_queue.h"
#include "text_input.h"
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
    else SDL_StartTextInput(g_window);   // text + input-method composition events (text_input.h)
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
        if (!active) ran_platform::InputReleaseAll();   // no stuck keys after Cmd+Tab
        Send(WM_ACTIVATEAPP, active, 0);
        Send(WM_NCACTIVATE, active, 0);
        Send(WM_ACTIVATE, active ? 1 /*WA_ACTIVE*/ : 0 /*WA_INACTIVE*/, 0);
        break;
    }
    case SDL_EVENT_WINDOW_RESIZED:   // points, like GetClientRect (see ClientSize)
        Send(WM_SIZE, 0 /*SIZE_RESTORED*/, MakeLParam(e.window.data1, e.window.data2));
        break;
    case SDL_EVENT_WINDOW_MINIMIZED:
        Send(WM_SIZE, 1 /*SIZE_MINIMIZED*/, 0);
        break;
    // Keyboard and mouse go to DxInputDevice through the DirectInput stand-in (dinput_sdl.cpp);
    // keys and text also go to the focused window as messages (text_input.h: chat, login).
    case SDL_EVENT_KEY_DOWN:
    case SDL_EVENT_KEY_UP:
        ran_platform::InputKey(ran_platform::SdlScancodeToDik(e.key.scancode), e.type == SDL_EVENT_KEY_DOWN);
        if (e.type == SDL_EVENT_KEY_DOWN) ran_platform::TextKeyDown(ran_platform::SdlKeyToVk(e.key.key));
        else ran_platform::TextKeyUp(ran_platform::SdlKeyToVk(e.key.key));
        break;
    case SDL_EVENT_TEXT_EDITING:
        ran_platform::TextEditing(e.edit.text);
        break;
    case SDL_EVENT_TEXT_INPUT:
        ran_platform::TextInput(e.text.text);
        break;
    case SDL_EVENT_MOUSE_MOTION:
        ran_platform::InputMouseMove((int)e.motion.xrel, (int)e.motion.yrel);
        Send(WM_MOUSEMOVE, 0, MakeLParam((int)e.motion.x, (int)e.motion.y));
        break;
    case SDL_EVENT_MOUSE_WHEEL:
        ran_platform::InputMouseWheel((int)(e.wheel.y * 120.0f));   // WHEEL_DELTA per notch
        break;
    case SDL_EVENT_MOUSE_BUTTON_DOWN:
    case SDL_EVENT_MOUSE_BUTTON_UP: {
        // SDL: 1 left, 2 middle, 3 right, 4/5 side -> DirectInput: 0 left, 1 right, 2 middle, 3/4 side.
        static const int kMap[] = { -1, 0, 2, 1, 3, 4 };
        const int b = e.button.button < 6 ? kMap[e.button.button] : -1;
        if (b >= 0) ran_platform::InputMouseButton(b, e.type == SDL_EVENT_MOUSE_BUTTON_DOWN);
        break;
    }
    default:
        break;
    }
    return false;
}

BOOL PumpMessages(MSG* msg, BOOL wait, BOOL remove)
{
    if (msg) std::memset(msg, 0, sizeof(*msg));
    if (!remove) return SDL_HasEvents(SDL_EVENT_FIRST, SDL_EVENT_LAST) ? TRUE : FALSE;
    if (ran_compat::Ime().cancelRequested) {   // the game cancelled/completed the composition
        ran_compat::Ime().cancelRequested = false;
        if (g_window) SDL_ClearComposition(g_window);
    }
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

// GetClientRect: window size in points. The game's windowed back buffer takes this size and
// its UI/mouse coordinates use the same units; macOS scales the frame up on Retina screens.
BOOL ClientSize(HWND hwnd, SIZE* s)
{
    int w = 0, h = 0;
    if (!hwnd || (SDL_Window*)hwnd != g_window || !SDL_GetWindowSize(g_window, &w, &h)) return FALSE;
    s->cx = w;
    s->cy = h;
    return TRUE;
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
    ran_compat::ClientSizeHook() = ClientSize;

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
