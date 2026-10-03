// Phase 2 gate P2.2: the SDL3 scancode -> DIK table agrees with the DirectX SDK's own DIK_*
// constants, and every DIK_* key the game reads is reachable from a Mac keyboard.
#include "ran_compat.h"
#include <dinput.h>
#include "../platform/input_map.h"
#include <cstdio>
#include <set>

static int g_failed = 0;
#define CHECK(cond) do { if (!(cond)) { std::printf("FAIL %s:%d  %s\n", __FILE__, __LINE__, #cond); ++g_failed; } } while (0)

struct Pair { SDL_Scancode sc; int dik; const char* name; };
#define P(sc, dik) { SDL_SCANCODE_##sc, dik, #dik }

// Expected values spelled with the SDK's names (independent of the hex in input_map.h).
static const Pair kPairs[] = {
    P(A, DIK_A), P(B, DIK_B), P(C, DIK_C), P(D, DIK_D), P(E, DIK_E), P(F, DIK_F), P(G, DIK_G),
    P(H, DIK_H), P(I, DIK_I), P(J, DIK_J), P(K, DIK_K), P(L, DIK_L), P(M, DIK_M), P(N, DIK_N),
    P(O, DIK_O), P(P, DIK_P), P(Q, DIK_Q), P(R, DIK_R), P(S, DIK_S), P(T, DIK_T), P(U, DIK_U),
    P(V, DIK_V), P(W, DIK_W), P(X, DIK_X), P(Y, DIK_Y), P(Z, DIK_Z),
    P(1, DIK_1), P(2, DIK_2), P(3, DIK_3), P(4, DIK_4), P(5, DIK_5), P(6, DIK_6), P(7, DIK_7),
    P(8, DIK_8), P(9, DIK_9), P(0, DIK_0),
    P(RETURN, DIK_RETURN), P(ESCAPE, DIK_ESCAPE), P(BACKSPACE, DIK_BACK), P(TAB, DIK_TAB),
    P(SPACE, DIK_SPACE), P(MINUS, DIK_MINUS), P(EQUALS, DIK_EQUALS), P(LEFTBRACKET, DIK_LBRACKET),
    P(RIGHTBRACKET, DIK_RBRACKET), P(BACKSLASH, DIK_BACKSLASH), P(SEMICOLON, DIK_SEMICOLON),
    P(APOSTROPHE, DIK_APOSTROPHE), P(GRAVE, DIK_GRAVE), P(COMMA, DIK_COMMA), P(PERIOD, DIK_PERIOD),
    P(SLASH, DIK_SLASH), P(CAPSLOCK, DIK_CAPITAL), P(NONUSBACKSLASH, DIK_OEM_102),
    P(F1, DIK_F1), P(F2, DIK_F2), P(F3, DIK_F3), P(F4, DIK_F4), P(F5, DIK_F5), P(F6, DIK_F6),
    P(F7, DIK_F7), P(F8, DIK_F8), P(F9, DIK_F9), P(F10, DIK_F10), P(F11, DIK_F11), P(F12, DIK_F12),
    P(F13, DIK_F13), P(F14, DIK_F14), P(F15, DIK_F15),
    P(PRINTSCREEN, DIK_SYSRQ), P(SCROLLLOCK, DIK_SCROLL), P(PAUSE, DIK_PAUSE), P(INSERT, DIK_INSERT),
    P(HOME, DIK_HOME), P(PAGEUP, DIK_PRIOR), P(DELETE, DIK_DELETE), P(END, DIK_END),
    P(PAGEDOWN, DIK_NEXT), P(RIGHT, DIK_RIGHT), P(LEFT, DIK_LEFT), P(DOWN, DIK_DOWN), P(UP, DIK_UP),
    P(NUMLOCKCLEAR, DIK_NUMLOCK), P(KP_DIVIDE, DIK_DIVIDE), P(KP_MULTIPLY, DIK_MULTIPLY),
    P(KP_MINUS, DIK_SUBTRACT), P(KP_PLUS, DIK_ADD), P(KP_ENTER, DIK_NUMPADENTER),
    P(KP_1, DIK_NUMPAD1), P(KP_2, DIK_NUMPAD2), P(KP_3, DIK_NUMPAD3), P(KP_4, DIK_NUMPAD4),
    P(KP_5, DIK_NUMPAD5), P(KP_6, DIK_NUMPAD6), P(KP_7, DIK_NUMPAD7), P(KP_8, DIK_NUMPAD8),
    P(KP_9, DIK_NUMPAD9), P(KP_0, DIK_NUMPAD0), P(KP_PERIOD, DIK_DECIMAL), P(KP_EQUALS, DIK_NUMPADEQUALS),
    P(KP_COMMA, DIK_ABNT_C2), P(APPLICATION, DIK_APPS),
    P(INTERNATIONAL1, DIK_ABNT_C1), P(INTERNATIONAL2, DIK_KANA), P(INTERNATIONAL3, DIK_YEN),
    P(INTERNATIONAL4, DIK_CONVERT), P(INTERNATIONAL5, DIK_NOCONVERT), P(LANG1, DIK_KANA), P(LANG2, DIK_KANJI),
    P(LCTRL, DIK_LCONTROL), P(LSHIFT, DIK_LSHIFT), P(LALT, DIK_LMENU), P(LGUI, DIK_LWIN),
    P(RCTRL, DIK_RCONTROL), P(RSHIFT, DIK_RSHIFT), P(RALT, DIK_RMENU), P(RGUI, DIK_RWIN),
};

// Every DIK_* the client source reads (grep of [Client]__Game and [Lib]__*, 2026-10-04).
#define D(x) { x, #x }
static const struct { int dik; const char* name; } kGameKeys[] = {
    D(DIK_0), D(DIK_1), D(DIK_2), D(DIK_3), D(DIK_4), D(DIK_5), D(DIK_6), D(DIK_7), D(DIK_8), D(DIK_9),
    D(DIK_A), D(DIK_ABNT_C1), D(DIK_ABNT_C2), D(DIK_ADD), D(DIK_APOSTROPHE), D(DIK_APPS), D(DIK_B),
    D(DIK_BACK), D(DIK_BACKSLASH), D(DIK_BACKSPACE), D(DIK_C), D(DIK_CAPITAL), D(DIK_CAPSLOCK),
    D(DIK_COMMA), D(DIK_CONVERT), D(DIK_D), D(DIK_DECIMAL), D(DIK_DELETE), D(DIK_DIVIDE), D(DIK_DOWN),
    D(DIK_E), D(DIK_END), D(DIK_EQUALS), D(DIK_ESCAPE), D(DIK_F), D(DIK_F1), D(DIK_F10), D(DIK_F11),
    D(DIK_F12), D(DIK_F13), D(DIK_F14), D(DIK_F15), D(DIK_F2), D(DIK_F3), D(DIK_F4), D(DIK_F5),
    D(DIK_F6), D(DIK_F7), D(DIK_F8), D(DIK_F9), D(DIK_G), D(DIK_GRAVE), D(DIK_H), D(DIK_HOME), D(DIK_I),
    D(DIK_INSERT), D(DIK_J), D(DIK_K), D(DIK_KANA), D(DIK_KANJI), D(DIK_L), D(DIK_LALT),
    D(DIK_LBRACKET), D(DIK_LCONTROL), D(DIK_LEFT), D(DIK_LMENU), D(DIK_LSHIFT), D(DIK_LWIN), D(DIK_M),
    D(DIK_MINUS), D(DIK_MULTIPLY), D(DIK_N), D(DIK_NEXT), D(DIK_NOCONVERT), D(DIK_NUMLOCK),
    D(DIK_NUMPAD0), D(DIK_NUMPAD1), D(DIK_NUMPAD2), D(DIK_NUMPAD3), D(DIK_NUMPAD4), D(DIK_NUMPAD5),
    D(DIK_NUMPAD6), D(DIK_NUMPAD7), D(DIK_NUMPAD8), D(DIK_NUMPAD9), D(DIK_NUMPADENTER), D(DIK_O),
    D(DIK_P), D(DIK_PAUSE), D(DIK_PERIOD), D(DIK_PGDN), D(DIK_PGUP), D(DIK_PRIOR), D(DIK_Q), D(DIK_R),
    D(DIK_RBRACKET), D(DIK_RCONTROL), D(DIK_RETURN), D(DIK_RIGHT), D(DIK_RMENU), D(DIK_RSHIFT),
    D(DIK_RWIN), D(DIK_S), D(DIK_SCROLL), D(DIK_SEMICOLON), D(DIK_SLASH), D(DIK_SPACE),
    D(DIK_SUBTRACT), D(DIK_SYSRQ), D(DIK_T), D(DIK_TAB), D(DIK_U), D(DIK_UP), D(DIK_V), D(DIK_W),
    D(DIK_X), D(DIK_Y), D(DIK_YEN), D(DIK_Z),
};

int main()
{
    // 1. Table values equal the SDK constants.
    for (const Pair& p : kPairs) {
        const int got = ran_platform::SdlScancodeToDik(p.sc);
        if (got != p.dik) { std::printf("FAIL scancode %d -> 0x%02X, expected %s (0x%02X)\n", (int)p.sc, got, p.name, p.dik); ++g_failed; }
    }
    // 2. Every key the game reads can be produced.
    std::set<int> reachable;
    for (int sc = 0; sc < SDL_SCANCODE_COUNT; ++sc) {
        const int d = ran_platform::SdlScancodeToDik((SDL_Scancode)sc);
        if (d) reachable.insert(d);
    }
    for (const auto& k : kGameKeys)
        if (!reachable.count(k.dik)) { std::printf("FAIL game key %s (0x%02X) has no Mac key\n", k.name, k.dik); ++g_failed; }
    // 3. Unmapped keys report 0 (never a stray DIK).
    CHECK(ran_platform::SdlScancodeToDik(SDL_SCANCODE_UNKNOWN) == 0);
    CHECK(ran_platform::SdlScancodeToDik(SDL_SCANCODE_MUTE) == 0);

    if (g_failed) { std::printf("%d check(s) failed\n", g_failed); return 1; }
    std::printf("input_map_test: %zu mappings match the SDK, all %zu game keys reachable\n",
                sizeof(kPairs) / sizeof(kPairs[0]), sizeof(kGameKeys) / sizeof(kGameKeys[0]));
    return 0;
}
