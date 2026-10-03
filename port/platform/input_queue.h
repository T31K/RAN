// Input events from the SDL3 event pump (main_sdl.cpp) to the DirectInput stand-in
// (dinput_sdl.cpp), which hands them to DxInputDevice through GetDeviceData exactly as
// DirectInput's buffered mode would. Keys are DIK codes (input_map.h).
#pragma once
#include <cstdint>

namespace ran_platform {
void InputKey(uint8_t dik, bool down);
void InputMouseMove(int dx, int dy);
void InputMouseWheel(int delta);          // WHEEL_DELTA (120) per notch, like DirectInput's lZ
void InputMouseButton(int button, bool down);   // 0 left, 1 right, 2 middle, 3/4 side buttons
void InputReleaseAll();                   // focus lost: every held key/button goes up
}
