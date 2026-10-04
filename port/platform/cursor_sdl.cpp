// Windows mouse cursors on SDL3 for the native build: the game's own cursors
// (data/editor/*.cur, select.ani via LoadCursorFromFile) become SDL colour cursors, the system
// IDC_* ids SDL system cursors; SetCursor/ShowCursor keep Windows' semantics (SetCursor(NULL)
// hides the cursor, ShowCursor is a display counter). Installed by InstallCursorHooks().
#include "ran_compat.h"
#include "cursor_decode.h"
#include <SDL3/SDL.h>
#include <cstdio>
#include <vector>

namespace {

SDL_Cursor* g_current = nullptr;
int g_showCount = 0;
SDL_Cursor* g_system[SDL_SYSTEM_CURSOR_COUNT] = {};   // shared, like Windows' system cursors

void UpdateVisibility()
{
    if (g_current && g_showCount >= 0) SDL_ShowCursor();
    else SDL_HideCursor();
}

HCURSOR LoadFile(const char* path)
{
    std::FILE* f = ran_compat::fopen_resolved(path, "rb");
    if (!f) return nullptr;
    std::vector<uint8_t> bytes;
    uint8_t buf[4096];
    size_t n;
    while ((n = std::fread(buf, 1, sizeof(buf), f)) > 0) bytes.insert(bytes.end(), buf, buf + n);
    std::fclose(f);
    ran_platform::CursorImage img;
    if (!ran_platform::DecodeCursorFile(bytes.data(), bytes.size(), img)) {
        std::fprintf(stderr, "[cursor] cannot decode %s\n", path);
        return nullptr;
    }
    SDL_Surface* s = SDL_CreateSurfaceFrom(img.width, img.height, SDL_PIXELFORMAT_ARGB8888, img.argb.data(), img.width * 4);
    if (!s) return nullptr;
    SDL_Cursor* c = SDL_CreateColorCursor(s, img.hotX, img.hotY);
    SDL_DestroySurface(s);
    return (HCURSOR)c;
}

HCURSOR LoadSystem(const char* id)
{
    const uintptr_t v = (uintptr_t)id;
    SDL_SystemCursor which = SDL_SYSTEM_CURSOR_DEFAULT;
    if (v == 32513) which = SDL_SYSTEM_CURSOR_TEXT;          // IDC_IBEAM
    else if (v == 32514) which = SDL_SYSTEM_CURSOR_WAIT;     // IDC_WAIT
    else if (v == 32515) which = SDL_SYSTEM_CURSOR_CROSSHAIR;// IDC_CROSS
    else if (v == 32649) which = SDL_SYSTEM_CURSOR_POINTER;  // IDC_HAND
    if (!g_system[which]) g_system[which] = SDL_CreateSystemCursor(which);
    return (HCURSOR)g_system[which];
}

HCURSOR Set(HCURSOR c)
{
    SDL_Cursor* old = g_current;
    g_current = (SDL_Cursor*)c;
    if (g_current) SDL_SetCursor(g_current);
    UpdateVisibility();
    return (HCURSOR)old;
}

HCURSOR Get() { return (HCURSOR)g_current; }

int Show(BOOL show)
{
    g_showCount += show ? 1 : -1;
    UpdateVisibility();
    return g_showCount;
}

BOOL Destroy(HCURSOR c)
{
    if (!c) return FALSE;
    if ((SDL_Cursor*)c == g_current) return FALSE;   // Windows refuses to destroy the cursor in use
    for (SDL_Cursor* s : g_system)
        if ((SDL_Cursor*)c == s) return TRUE;   // shared system cursors stay
    SDL_DestroyCursor((SDL_Cursor*)c);
    return TRUE;
}

} // namespace

namespace ran_platform {

void InstallCursorHooks()
{
    ran_compat::CursorHooks& h = ran_compat::Cursors();
    h.loadFile = LoadFile;
    h.loadSystem = LoadSystem;
    h.set = Set;
    h.get = Get;
    h.show = Show;
    h.destroy = Destroy;
}

} // namespace ran_platform
