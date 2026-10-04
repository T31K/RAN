// Keyboard text for the game's edit control (CIMEEdit, behind DXInputString) in the native
// build: SDL3 key, text-input and text-editing events become the window messages Windows
// sends to the focused window - WM_KEYDOWN, WM_CHAR (TranslateMessage's characters) and the
// IME sequence WM_IME_STARTCOMPOSITION / WM_IME_COMPOSITION / WM_IME_ENDCOMPOSITION, with the
// strings in ran_compat::Ime() for ImmGetCompositionStringW. Header-only so the test can drive
// it without a window (port/tests/text_input_test.cpp).
#pragma once
#include "ran_compat.h"
#include "mfc/afx_all.h"
#include <imm.h>
#include <SDL3/SDL_keycode.h>
#include <string>

namespace ran_platform {

// Windows virtual-key code for an SDL key (0 = no equivalent).
inline UINT SdlKeyToVk(SDL_Keycode k)
{
    if (k >= SDLK_A && k <= SDLK_Z) return 'A' + (UINT)(k - SDLK_A);
    if (k >= SDLK_0 && k <= SDLK_9) return '0' + (UINT)(k - SDLK_0);
    if (k >= SDLK_F1 && k <= SDLK_F12) return VK_F1 + (UINT)(k - SDLK_F1);
    switch (k) {
    case SDLK_RETURN: case SDLK_KP_ENTER: return VK_RETURN;
    case SDLK_BACKSPACE: return VK_BACK;
    case SDLK_TAB: return VK_TAB;
    case SDLK_ESCAPE: return VK_ESCAPE;
    case SDLK_SPACE: return VK_SPACE;
    case SDLK_LEFT: return VK_LEFT;
    case SDLK_RIGHT: return VK_RIGHT;
    case SDLK_UP: return VK_UP;
    case SDLK_DOWN: return VK_DOWN;
    case SDLK_HOME: return VK_HOME;
    case SDLK_END: return VK_END;
    case SDLK_PAGEUP: return VK_PRIOR;
    case SDLK_PAGEDOWN: return VK_NEXT;
    case SDLK_INSERT: return VK_INSERT;
    case SDLK_DELETE: return VK_DELETE;
    case SDLK_LSHIFT: case SDLK_RSHIFT: return VK_SHIFT;
    case SDLK_LCTRL: case SDLK_RCTRL: return VK_CONTROL;
    case SDLK_LALT: case SDLK_RALT: return VK_MENU;
    default: return 0;
    }
}

inline std::u16string Utf8ToUtf16(const char* s)
{
    std::u16string out;
    const unsigned char* p = (const unsigned char*)(s ? s : "");
    while (*p) {
        uint32_t c = *p++;
        int extra = c >= 0xF0 ? 3 : c >= 0xE0 ? 2 : c >= 0xC0 ? 1 : 0;
        if (extra) c &= (0x3F >> extra);
        for (; extra > 0 && (*p & 0xC0) == 0x80; --extra) c = (c << 6) | (*p++ & 0x3F);
        if (c >= 0x10000) {
            c -= 0x10000;
            out += (char16_t)(0xD800 + (c >> 10));
            out += (char16_t)(0xDC00 + (c & 0x3FF));
        } else {
            out += (char16_t)c;
        }
    }
    return out;
}

inline CWnd* TextTarget() { return CWnd::GetFocus() ? CWnd::GetFocus() : CWnd::MainWindow(); }

// Key press (also auto-repeat). While the input method composes, it owns the keys (Windows
// reports them as VK_PROCESSKEY), so nothing is sent.
inline void TextKeyDown(UINT vk)
{
    CWnd* w = TextTarget();
    if (!w || !vk || ran_compat::Ime().composing) return;
    w->SendMessage(WM_KEYDOWN, vk, 1);
    // TranslateMessage: these keys also produce a character; printable ones come as text input.
    if (vk == VK_RETURN || vk == VK_BACK || vk == VK_TAB || vk == VK_ESCAPE)
        w->SendMessage(WM_CHAR, vk == VK_RETURN ? '\r' : vk == VK_BACK ? '\b' : vk == VK_TAB ? '\t' : 0x1B, 1);
}

inline void TextKeyUp(UINT vk)
{
    CWnd* w = TextTarget();
    if (w && vk && !ran_compat::Ime().composing) w->SendMessage(WM_KEYUP, vk, 0xC0000001);
}

// Composition update from the input method (empty = composition cancelled/finished).
inline void TextEditing(const char* utf8)
{
    ran_compat::ImeState& ime = ran_compat::Ime();
    CWnd* w = TextTarget();
    const std::u16string comp = Utf8ToUtf16(utf8);
    if (comp.empty()) {
        if (!ime.composing) return;
        ime.composition.clear();
        if (w) w->SendMessage(WM_IME_COMPOSITION, 0, GCS_COMPSTR);   // remove the shown composition
        ime.composing = false;
        if (w) w->SendMessage(WM_IME_ENDCOMPOSITION, 0, 0);
        return;
    }
    ime.native = true;
    if (!ime.composing) {
        ime.composing = true;
        if (w) w->SendMessage(WM_IME_STARTCOMPOSITION, 0, 0);
    }
    ime.composition = comp;
    if (w) w->SendMessage(WM_IME_COMPOSITION, comp.back(), GCS_COMPSTR | GCS_COMPATTR | GCS_CURSORPOS);
}

// Committed text. Plain ASCII typed outside a composition is WM_CHAR, like a Latin keyboard;
// anything else arrives as an IME result string.
inline void TextInput(const char* utf8)
{
    ran_compat::ImeState& ime = ran_compat::Ime();
    CWnd* w = TextTarget();
    const std::u16string text = Utf8ToUtf16(utf8);
    if (text.empty() || !w) return;
    bool ascii = true;
    for (char16_t c : text) ascii = ascii && c < 0x80;
    if (ascii && !ime.composing) {
        ime.native = false;
        for (char16_t c : text) w->SendMessage(WM_CHAR, (WPARAM)c, 1);
        return;
    }
    ime.native = !ascii;
    if (!ime.composing) {
        ime.composing = true;
        w->SendMessage(WM_IME_STARTCOMPOSITION, 0, 0);
    }
    ime.composition.clear();
    ime.result = text;
    w->SendMessage(WM_IME_COMPOSITION, text.back(), GCS_RESULTSTR);
    ime.result.clear();
    ime.composing = false;
    w->SendMessage(WM_IME_ENDCOMPOSITION, 0, 0);
}

} // namespace ran_platform
