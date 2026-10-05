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
#include <csignal>
#include <cstdio>
#include <cstdlib>
#include <execinfo.h>
#include <copyfile.h>
#include <sys/stat.h>
#include <ctime>
extern "C" int _NSGetExecutablePath(char* buf, uint32_t* bufsize);   // <mach-o/dyld.h> redefines FALSE
#include <algorithm>
#include <string>
#include <unistd.h>
#include <vector>

namespace ran_platform { void InstallCursorHooks(); }   // cursor_sdl.cpp

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

// RAN_INPUT_SCRIPT="2:click 488 373; 4:text T31K; 5:key TAB; ..." - scripted input for
// unattended test runs: at each time (seconds after the first message pump) the action goes
// through the same path as real input (click / rclick = move the cursor there, press, release the
// left / right button; move = cursor only; text = committed text input; key = press and release
// an SDL key name).
struct ScriptStep { double at; SDL_Event event; std::string text; bool warp; float x, y; };
std::vector<ScriptStep>& Script() { static std::vector<ScriptStep> s; return s; }

void LoadInputScript()
{
    const char* env = std::getenv("RAN_INPUT_SCRIPT");
    if (!env) return;
    std::string all(env);
    size_t pos = 0;
    while (pos < all.size()) {
        size_t end = all.find(';', pos);
        if (end == std::string::npos) end = all.size();
        std::string item = all.substr(pos, end - pos);
        pos = end + 1;
        const size_t colon = item.find(':');
        if (colon == std::string::npos) continue;
        const double t = std::atof(item.substr(0, colon).c_str());
        std::string cmd = item.substr(colon + 1);
        while (!cmd.empty() && cmd[0] == ' ') cmd.erase(0, 1);
        const size_t sp = cmd.find(' ');
        const std::string op = cmd.substr(0, sp), arg = sp == std::string::npos ? "" : cmd.substr(sp + 1);
        ScriptStep s = {};
        s.at = t;
        if (op == "move") {   // move the cursor only (hover tooltips): move <x> <y>
            std::sscanf(arg.c_str(), "%f %f", &s.x, &s.y);
            s.warp = true;
            Script().push_back(s);
        } else if (op == "click" || op == "rclick") {
            float x = 0, y = 0;
            std::sscanf(arg.c_str(), "%f %f", &x, &y);
            s.warp = true; s.x = x; s.y = y;
            Script().push_back(s);
            ScriptStep down = {}, up = {};
            down.at = t + 0.15; down.event.type = SDL_EVENT_MOUSE_BUTTON_DOWN;
            down.event.button.button = op == "rclick" ? SDL_BUTTON_RIGHT : SDL_BUTTON_LEFT;
            down.event.button.x = x; down.event.button.y = y;
            up = down; up.at = t + 0.3; up.event.type = SDL_EVENT_MOUSE_BUTTON_UP;
            Script().push_back(down);
            Script().push_back(up);
        } else if (op == "raise") {   // bring the game window to the front (app switching tests)
            s.event.type = SDL_EVENT_USER;
            Script().push_back(s);
        } else if (op == "resize") {  // resize the window (device reset tests): resize <w> <h>
            s.event.type = SDL_EVENT_USER;
            s.event.user.code = 1;
            std::sscanf(arg.c_str(), "%f %f", &s.x, &s.y);
            Script().push_back(s);
        } else if (op == "text") {
            s.event.type = SDL_EVENT_TEXT_INPUT;
            s.text = arg;
            Script().push_back(s);
        } else if (op == "key") {
            const SDL_Keycode k = SDL_GetKeyFromName(arg.c_str());
            s.event.type = SDL_EVENT_KEY_DOWN;
            s.event.key.key = k;
            s.event.key.scancode = SDL_GetScancodeFromKey(k, nullptr);
            s.event.key.down = true;
            Script().push_back(s);
            ScriptStep up = s;
            up.at = t + 0.1;
            up.event.type = SDL_EVENT_KEY_UP;
            up.event.key.down = false;
            Script().push_back(up);
        }
    }
    std::stable_sort(Script().begin(), Script().end(), [](const ScriptStep& a, const ScriptStep& b) { return a.at < b.at; });
    std::fprintf(stderr, "[platform] input script: %zu steps\n", Script().size());
}

void RunInputScript()
{
    static const Uint64 start = SDL_GetTicks();
    static size_t next = 0;
    const double now = (double)(SDL_GetTicks() - start) / 1000.0;
    while (next < Script().size() && Script()[next].at <= now) {
        ScriptStep& s = Script()[next++];
        if (s.warp) { if (g_window) SDL_WarpMouseInWindow(g_window, s.x, s.y); continue; }
        if (s.event.type == SDL_EVENT_USER) {
            if (g_window && s.event.user.code == 1) SDL_SetWindowSize(g_window, (int)s.x, (int)s.y);
            else if (g_window) SDL_RaiseWindow(g_window);
            continue;
        }
        if (s.event.type == SDL_EVENT_TEXT_INPUT) s.event.text.text = s.text.c_str();
        Dispatch(s.event);
    }
}

BOOL PumpMessages(MSG* msg, BOOL wait, BOOL remove)
{
    if (!Script().empty()) RunInputScript();
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

bool IsDir(const std::string& p)
{
    struct stat st;
    return ::stat(p.c_str(), &st) == 0 && S_ISDIR(st.st_mode);
}

// Inside an app bundle (Contents/MacOS/ran_client) the bundle provides what run_native.sh
// sets up by hand: the Vulkan driver manifest (Contents/Resources/vulkan/icd.d), and the game
// folder - RAN_GAME_DIR if set; else, for a bundle that carries the game, this user's copy of
// it in "RanOdyssey Native/game"; else the classic RanOdyssey app's per-user copy. The working
// directory becomes the game folder, like launching Game.exe.
void ConfigureFromBundle()
{
    char exe[4096];
    uint32_t size = sizeof(exe);
    if (_NSGetExecutablePath(exe, &size) != 0) return;
    std::string contents(exe);
    contents = contents.substr(0, contents.rfind('/'));              // .../Contents/MacOS
    contents = contents.substr(0, contents.rfind('/'));              // .../Contents
    const std::string icd = contents + "/Resources/vulkan/icd.d/kosmickrisp_icd.json";
    if (!std::getenv("VK_DRIVER_FILES") && ::access(icd.c_str(), R_OK) == 0) setenv("VK_DRIVER_FILES", icd.c_str(), 1);
    if (!std::getenv("RAN_GAME_DIR")) {
        const char* home = std::getenv("HOME");
        const std::string support = std::string(home ? home : "") + "/Library/Application Support";
        const std::string bundled = contents + "/Resources/game";
        if (IsDir(bundled + "/data")) {
            // The game writes next to its data (options, caches) and an installed app is
            // read-only, so each user gets a copy - an APFS clone: instant, sharing the blocks
            // with the bundle. Its own folder: the classic (Wine) app's "RanOdyssey/game" holds
            // an older client layout that this one cannot load. The marker is written only
            // after a complete copy, so an interrupted first launch is redone.
            const std::string mine = support + "/RanOdyssey Native/game";
            const std::string marker = mine + "/.ran-native-copy";
            if (::access(marker.c_str(), F_OK) != 0) {
                if (IsDir(mine)) ::rename(mine.c_str(), (mine + ".incomplete." + std::to_string((long)::time(nullptr))).c_str());
                const std::string parent = mine.substr(0, mine.rfind('/'));
                for (size_t at = 1; at != std::string::npos; ) {   // mkdir -p
                    at = parent.find('/', at + 1);
                    ::mkdir(parent.substr(0, at).c_str(), 0755);
                }
                if (copyfile(bundled.c_str(), mine.c_str(), nullptr, COPYFILE_ALL | COPYFILE_RECURSIVE | COPYFILE_CLONE) == 0) {
                    if (FILE* m = std::fopen(marker.c_str(), "w")) std::fclose(m);
                } else {
                    std::fprintf(stderr, "[platform] could not copy the game data to %s\n", mine.c_str());
                }
            }
            setenv("RAN_GAME_DIR", (::access(marker.c_str(), F_OK) == 0 ? mine : bundled).c_str(), 1);
        } else {
            const std::string classic = support + "/RanOdyssey/game";
            if (IsDir(classic + "/data")) setenv("RAN_GAME_DIR", classic.c_str(), 1);
        }
    }
    if (const char* dir = std::getenv("RAN_GAME_DIR")) {
        if (::chdir(dir) != 0) std::fprintf(stderr, "[platform] cannot enter game folder %s\n", dir);
    }
}

// Fatal signals print the native backtrace to stderr before the default action (macOS also
// writes a crash report, but throttles repeats of the same crash).
void OnFatalSignal(int sig)
{
    const char head[] = "\n[platform] fatal signal - backtrace:\n";
    write(2, head, sizeof(head) - 1);
    void* frames[64];
    const int n = backtrace(frames, 64);
    backtrace_symbols_fd(frames, n, 2);
    signal(sig, SIG_DFL);
    raise(sig);
}

int main(int argc, char** argv)
{
    for (int sig : { SIGSEGV, SIGBUS, SIGILL, SIGTRAP, SIGABRT, SIGFPE }) signal(sig, OnFatalSignal);
    signal(SIGTERM, [](int) { _exit(0); });   // kill/quit from outside: no static teardown either
    ConfigureFromBundle();
    setenv("DXVK_WSI_DRIVER", "SDL3", 0);
    // The game runs at 30 FPS by default (its Frame Limit option); with DXVK's default queue of
    // three frames that is up to ~100 ms between a click and the frame that shows it. One
    // queued frame keeps input responsive (the GPU is mostly idle anyway).
    setenv("DXVK_CONFIG", "d3d9.maxFrameLatency = 1", 0);
    if (!SDL_Init(SDL_INIT_VIDEO | SDL_INIT_EVENTS)) {
        std::fprintf(stderr, "[platform] SDL_Init failed: %s\n", SDL_GetError());
        return 1;
    }
    CWnd::CreateMainWindowHook() = CreateMainWindow;
    ran_compat::MessageHook() = PumpMessages;
    ran_compat::CursorPosHook() = CursorPos;
    ran_compat::ClientSizeHook() = ClientSize;
    ran_platform::InstallCursorHooks();
    LoadInputScript();

    CWinApp* app = AfxGetApp();   // the game's theApp (CBasicApp)
    static std::string cmdLine;
    for (int i = 1; i < argc; ++i) { if (i > 1) cmdLine += ' '; cmdLine += argv[i]; }
    app->m_lpCmdLine = const_cast<char*>(cmdLine.c_str());

    int rc = 1;
    if (app->InitInstance()) rc = app->Run();
    else std::fprintf(stderr, "[platform] InitInstance failed (is RAN_GAME_DIR set to the client folder?)\n");
    if (g_window) SDL_DestroyWindow(g_window);
    SDL_Quit();
    // The game has saved and shut down in ExitInstance (inside Run). Static destructors across
    // its files run in a different order than on Windows (DxGlobalStage's network client uses
    // a mutex that is already gone), so end the process here without them.
    std::fflush(nullptr);
    _exit(rc);
}
