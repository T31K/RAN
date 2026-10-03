// SDL3 scancode -> DirectInput DIK_* key code, for the native platform layer (plan Phase 2).
// DxInputDevice keeps its 256-entry DIK-indexed key array; the SDL event loop fills it through
// this table. SDL3 scancodes are USB HID keyboard usages and DIK values are PC set-1 scan codes
// (E0-prefixed keys at +0x80), so this is the standard HID -> set-1 correspondence that Windows
// itself applies. Mac keys: Command -> Windows key (DIK_LWIN/RWIN), Option -> Alt (DIK_LMENU/RMENU).
#pragma once
#include <SDL3/SDL_scancode.h>
#include <cstdint>

namespace ran_platform {

// Returns 0 for keys DirectInput has no code for.
inline uint8_t SdlScancodeToDik(SDL_Scancode sc)
{
    switch (sc) {
    // Letters (HID order a..z)
    case SDL_SCANCODE_A: return 0x1E; case SDL_SCANCODE_B: return 0x30; case SDL_SCANCODE_C: return 0x2E;
    case SDL_SCANCODE_D: return 0x20; case SDL_SCANCODE_E: return 0x12; case SDL_SCANCODE_F: return 0x21;
    case SDL_SCANCODE_G: return 0x22; case SDL_SCANCODE_H: return 0x23; case SDL_SCANCODE_I: return 0x17;
    case SDL_SCANCODE_J: return 0x24; case SDL_SCANCODE_K: return 0x25; case SDL_SCANCODE_L: return 0x26;
    case SDL_SCANCODE_M: return 0x32; case SDL_SCANCODE_N: return 0x31; case SDL_SCANCODE_O: return 0x18;
    case SDL_SCANCODE_P: return 0x19; case SDL_SCANCODE_Q: return 0x10; case SDL_SCANCODE_R: return 0x13;
    case SDL_SCANCODE_S: return 0x1F; case SDL_SCANCODE_T: return 0x14; case SDL_SCANCODE_U: return 0x16;
    case SDL_SCANCODE_V: return 0x2F; case SDL_SCANCODE_W: return 0x11; case SDL_SCANCODE_X: return 0x2D;
    case SDL_SCANCODE_Y: return 0x15; case SDL_SCANCODE_Z: return 0x2C;
    // Number row
    case SDL_SCANCODE_1: return 0x02; case SDL_SCANCODE_2: return 0x03; case SDL_SCANCODE_3: return 0x04;
    case SDL_SCANCODE_4: return 0x05; case SDL_SCANCODE_5: return 0x06; case SDL_SCANCODE_6: return 0x07;
    case SDL_SCANCODE_7: return 0x08; case SDL_SCANCODE_8: return 0x09; case SDL_SCANCODE_9: return 0x0A;
    case SDL_SCANCODE_0: return 0x0B;
    // Main block
    case SDL_SCANCODE_RETURN:       return 0x1C;   // DIK_RETURN
    case SDL_SCANCODE_ESCAPE:       return 0x01;   // DIK_ESCAPE
    case SDL_SCANCODE_BACKSPACE:    return 0x0E;   // DIK_BACK
    case SDL_SCANCODE_TAB:          return 0x0F;   // DIK_TAB
    case SDL_SCANCODE_SPACE:        return 0x39;   // DIK_SPACE
    case SDL_SCANCODE_MINUS:        return 0x0C;   // DIK_MINUS
    case SDL_SCANCODE_EQUALS:       return 0x0D;   // DIK_EQUALS
    case SDL_SCANCODE_LEFTBRACKET:  return 0x1A;   // DIK_LBRACKET
    case SDL_SCANCODE_RIGHTBRACKET: return 0x1B;   // DIK_RBRACKET
    case SDL_SCANCODE_BACKSLASH:    return 0x2B;   // DIK_BACKSLASH
    case SDL_SCANCODE_NONUSHASH:    return 0x2B;   // ISO "#~": same set-1 code as backslash
    case SDL_SCANCODE_SEMICOLON:    return 0x27;   // DIK_SEMICOLON
    case SDL_SCANCODE_APOSTROPHE:   return 0x28;   // DIK_APOSTROPHE
    case SDL_SCANCODE_GRAVE:        return 0x29;   // DIK_GRAVE
    case SDL_SCANCODE_COMMA:        return 0x33;   // DIK_COMMA
    case SDL_SCANCODE_PERIOD:       return 0x34;   // DIK_PERIOD
    case SDL_SCANCODE_SLASH:        return 0x35;   // DIK_SLASH
    case SDL_SCANCODE_CAPSLOCK:     return 0x3A;   // DIK_CAPITAL
    case SDL_SCANCODE_NONUSBACKSLASH: return 0x56; // DIK_OEM_102 (ISO key left of Z)
    // Function keys
    case SDL_SCANCODE_F1: return 0x3B; case SDL_SCANCODE_F2: return 0x3C; case SDL_SCANCODE_F3: return 0x3D;
    case SDL_SCANCODE_F4: return 0x3E; case SDL_SCANCODE_F5: return 0x3F; case SDL_SCANCODE_F6: return 0x40;
    case SDL_SCANCODE_F7: return 0x41; case SDL_SCANCODE_F8: return 0x42; case SDL_SCANCODE_F9: return 0x43;
    case SDL_SCANCODE_F10: return 0x44; case SDL_SCANCODE_F11: return 0x57; case SDL_SCANCODE_F12: return 0x58;
    case SDL_SCANCODE_F13: return 0x64; case SDL_SCANCODE_F14: return 0x65; case SDL_SCANCODE_F15: return 0x66;
    // Navigation (E0-prefixed)
    case SDL_SCANCODE_PRINTSCREEN: return 0xB7;    // DIK_SYSRQ
    case SDL_SCANCODE_SCROLLLOCK:  return 0x46;    // DIK_SCROLL
    case SDL_SCANCODE_PAUSE:       return 0xC5;    // DIK_PAUSE
    case SDL_SCANCODE_INSERT:      return 0xD2;    // DIK_INSERT
    case SDL_SCANCODE_HOME:        return 0xC7;    // DIK_HOME
    case SDL_SCANCODE_PAGEUP:      return 0xC9;    // DIK_PRIOR / DIK_PGUP
    case SDL_SCANCODE_DELETE:      return 0xD3;    // DIK_DELETE
    case SDL_SCANCODE_END:         return 0xCF;    // DIK_END
    case SDL_SCANCODE_PAGEDOWN:    return 0xD1;    // DIK_NEXT / DIK_PGDN
    case SDL_SCANCODE_RIGHT:       return 0xCD;    // DIK_RIGHT
    case SDL_SCANCODE_LEFT:        return 0xCB;    // DIK_LEFT
    case SDL_SCANCODE_DOWN:        return 0xD0;    // DIK_DOWN
    case SDL_SCANCODE_UP:          return 0xC8;    // DIK_UP
    // Keypad
    case SDL_SCANCODE_NUMLOCKCLEAR: return 0x45;   // DIK_NUMLOCK (Mac keypad "clear" key)
    case SDL_SCANCODE_KP_DIVIDE:   return 0xB5;    // DIK_DIVIDE
    case SDL_SCANCODE_KP_MULTIPLY: return 0x37;    // DIK_MULTIPLY
    case SDL_SCANCODE_KP_MINUS:    return 0x4A;    // DIK_SUBTRACT
    case SDL_SCANCODE_KP_PLUS:     return 0x4E;    // DIK_ADD
    case SDL_SCANCODE_KP_ENTER:    return 0x9C;    // DIK_NUMPADENTER
    case SDL_SCANCODE_KP_1: return 0x4F; case SDL_SCANCODE_KP_2: return 0x50; case SDL_SCANCODE_KP_3: return 0x51;
    case SDL_SCANCODE_KP_4: return 0x4B; case SDL_SCANCODE_KP_5: return 0x4C; case SDL_SCANCODE_KP_6: return 0x4D;
    case SDL_SCANCODE_KP_7: return 0x47; case SDL_SCANCODE_KP_8: return 0x48; case SDL_SCANCODE_KP_9: return 0x49;
    case SDL_SCANCODE_KP_0: return 0x52;
    case SDL_SCANCODE_KP_PERIOD:   return 0x53;    // DIK_DECIMAL
    case SDL_SCANCODE_KP_EQUALS:   return 0x8D;    // DIK_NUMPADEQUALS (Mac keypad "=")
    case SDL_SCANCODE_KP_COMMA:    return 0x7E;    // DIK_ABNT_C2 / DIK_NUMPADCOMMA position
    case SDL_SCANCODE_APPLICATION: return 0xDD;    // DIK_APPS
    // International keys (JIS / Brazilian / Korean keyboards)
    case SDL_SCANCODE_INTERNATIONAL1: return 0x73; // DIK_ABNT_C1 (JIS "ro")
    case SDL_SCANCODE_INTERNATIONAL2: return 0x70; // DIK_KANA (katakana/hiragana)
    case SDL_SCANCODE_INTERNATIONAL3: return 0x7D; // DIK_YEN
    case SDL_SCANCODE_INTERNATIONAL4: return 0x79; // DIK_CONVERT (henkan)
    case SDL_SCANCODE_INTERNATIONAL5: return 0x7B; // DIK_NOCONVERT (muhenkan)
    case SDL_SCANCODE_LANG1:          return 0x70; // Mac JIS "kana" / Korean Han-Yeong toggle -> DIK_KANA
    case SDL_SCANCODE_LANG2:          return 0x94; // Mac JIS "eisu" / Korean Hanja -> DIK_KANJI
    // Modifiers
    case SDL_SCANCODE_LCTRL:  return 0x1D;         // DIK_LCONTROL
    case SDL_SCANCODE_LSHIFT: return 0x2A;         // DIK_LSHIFT
    case SDL_SCANCODE_LALT:   return 0x38;         // DIK_LMENU / DIK_LALT  (Option)
    case SDL_SCANCODE_LGUI:   return 0xDB;         // DIK_LWIN              (Command)
    case SDL_SCANCODE_RCTRL:  return 0x9D;         // DIK_RCONTROL
    case SDL_SCANCODE_RSHIFT: return 0x36;         // DIK_RSHIFT
    case SDL_SCANCODE_RALT:   return 0xB8;         // DIK_RMENU / DIK_RALT  (Option)
    case SDL_SCANCODE_RGUI:   return 0xDC;         // DIK_RWIN              (Command)
    default: return 0;
    }
}

} // namespace ran_platform
