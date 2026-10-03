// Native-window focus probe: logs frame/focus changes and whether clicks hit
// the four corner targets, so Cmd+Tab behaviour can be checked automatically.
#include <SDL3/SDL.h>
#include <cstdio>
#include <cstring>
#include <cstdlib>
#include <string>

static void logFrame(SDL_Window* w)
{
    int x = 0, y = 0, cw = 0, ch = 0;
    SDL_GetWindowPosition(w, &x, &y);
    SDL_GetWindowSize(w, &cw, &ch);
    std::printf("FRAME %d %d %d %d\n", x, y, cw, ch);
    std::fflush(stdout);
}

int main(int argc, char** argv)
{
    std::string mode = "desktop-fullscreen";
    int seconds = 12;
    for (int i = 1; i + 1 < argc; i += 2) {
        if (!std::strcmp(argv[i], "--mode")) mode = argv[i + 1];
        else if (!std::strcmp(argv[i], "--seconds")) seconds = std::atoi(argv[i + 1]);
    }

    SDL_SetHint(SDL_HINT_VIDEO_MAC_FULLSCREEN_SPACES, mode == "spaces-fullscreen" ? "1" : "0");
    if (!SDL_Init(SDL_INIT_VIDEO)) { std::printf("ERROR %s\n", SDL_GetError()); return 1; }

    SDL_Window* win = SDL_CreateWindow("focus_probe", 1280, 800, SDL_WINDOW_HIGH_PIXEL_DENSITY);
    if (!win) { std::printf("ERROR %s\n", SDL_GetError()); return 1; }
    if (mode != "windowed") {
        SDL_SetWindowFullscreenMode(win, nullptr);   // nullptr = borderless desktop fullscreen
        SDL_SetWindowFullscreen(win, true);
        SDL_SyncWindow(win);
    }
    SDL_Renderer* ren = SDL_CreateRenderer(win, nullptr);
    logFrame(win);

    const Uint64 end = SDL_GetTicks() + (Uint64)seconds * 1000;
    const float kTarget = 80.0f;
    bool running = true;
    while (running && SDL_GetTicks() < end) {
        SDL_Event e;
        while (SDL_PollEvent(&e)) {
            switch (e.type) {
            case SDL_EVENT_QUIT: running = false; break;
            case SDL_EVENT_WINDOW_FOCUS_GAINED: std::printf("FOCUS gained\n"); logFrame(win); break;
            case SDL_EVENT_WINDOW_FOCUS_LOST: std::printf("FOCUS lost\n"); break;
            case SDL_EVENT_WINDOW_MOVED:
            case SDL_EVENT_WINDOW_RESIZED: logFrame(win); break;
            case SDL_EVENT_MOUSE_BUTTON_DOWN: {
                int cw = 0, ch = 0;
                SDL_GetWindowSize(win, &cw, &ch);
                const float x = e.button.x, y = e.button.y;
                const char* target = nullptr;
                if (x < kTarget && y < kTarget) target = "TL";
                else if (x > cw - kTarget && y < kTarget) target = "TR";
                else if (x < kTarget && y > ch - kTarget) target = "BL";
                else if (x > cw - kTarget && y > ch - kTarget) target = "BR";
                std::printf("CLICK %.0f %.0f %s %s\n", x, y, target ? "HIT" : "MISS", target ? target : "-");
                break;
            }
            default: break;
            }
            std::fflush(stdout);
        }
        int cw = 0, ch = 0;
        SDL_GetWindowSize(win, &cw, &ch);
        SDL_SetRenderDrawColor(ren, 20, 30, 50, 255);
        SDL_RenderClear(ren);
        SDL_SetRenderDrawColor(ren, 230, 180, 40, 255);
        const SDL_FRect r[4] = {
            {0, 0, kTarget, kTarget}, {cw - kTarget, 0, kTarget, kTarget},
            {0, ch - kTarget, kTarget, kTarget}, {cw - kTarget, ch - kTarget, kTarget, kTarget}};
        SDL_RenderFillRects(ren, r, 4);
        SDL_RenderPresent(ren);
        SDL_Delay(16);
    }
    logFrame(win);
    std::printf("DONE\n");
    SDL_Quit();
    return 0;
}
