// Behaviour tests for the DirectInput 8 stand-in (port/platform/dinput_sdl.cpp), driven the
// way DxInputDevice drives it: create keyboard/mouse devices, set the buffer size, Acquire, read
// buffered events with GetDeviceData.
#include "ran_compat.h"
#include <dinput.h>
#include "../platform/input_queue.h"
#include <cstdio>

static int g_failed = 0;
#define CHECK(cond) do { if (!(cond)) { std::printf("FAIL %s:%d  %s\n", __FILE__, __LINE__, #cond); ++g_failed; } } while (0)

static DWORD Read(LPDIRECTINPUTDEVICE8A dev, DIDEVICEOBJECTDATA* out, DWORD max)
{
    DWORD n = max;
    CHECK(dev->GetDeviceData(sizeof(DIDEVICEOBJECTDATA), out, &n, 0) == DI_OK);
    return n;
}

int main()
{
    LPDIRECTINPUT8A di = nullptr;
    CHECK(DirectInput8Create(nullptr, DIRECTINPUT_VERSION, IID_IDirectInput8, (void**)&di, nullptr) == DI_OK && di);
    LPDIRECTINPUTDEVICE8A kb = nullptr, mouse = nullptr, pad = nullptr;
    CHECK(di->CreateDevice(GUID_SysKeyboard, &kb, nullptr) == DI_OK && kb);
    CHECK(di->CreateDevice(GUID_SysMouse, &mouse, nullptr) == DI_OK && mouse);
    CHECK(di->CreateDevice(GUID_Joystick, &pad, nullptr) != DI_OK && !pad);
    CHECK(kb->SetDataFormat(&c_dfDIKeyboard) == DI_OK && mouse->SetDataFormat(&c_dfDIMouse2) == DI_OK);
    DIPROPDWORD buf = {};
    buf.diph.dwSize = sizeof(DIPROPDWORD); buf.diph.dwHeaderSize = sizeof(DIPROPHEADER); buf.diph.dwHow = DIPH_DEVICE;
    buf.dwData = 4;
    CHECK(kb->SetProperty(DIPROP_BUFFERSIZE, &buf.diph) == DI_OK);
    CHECK(kb->Acquire() == DI_OK && mouse->Acquire() == DI_OK);

    DIDEVICEOBJECTDATA d[16];
    // Key edges, in order; SDL key-repeat downs are not edges.
    ran_platform::InputKey(DIK_W, true);
    ran_platform::InputKey(DIK_W, true);
    ran_platform::InputKey(DIK_LSHIFT, true);
    ran_platform::InputKey(DIK_W, false);
    DWORD n = Read(kb, d, 16);
    CHECK(n == 3);
    CHECK(d[0].dwOfs == DIK_W && d[0].dwData == 0x80);
    CHECK(d[1].dwOfs == DIK_LSHIFT && d[1].dwData == 0x80);
    CHECK(d[2].dwOfs == DIK_W && d[2].dwData == 0);
    CHECK(d[1].dwSequence > d[0].dwSequence);
    CHECK(Read(kb, d, 16) == 0);   // drained
    BYTE state[256] = {};
    CHECK(kb->GetDeviceState(sizeof(state), state) == DI_OK && state[DIK_LSHIFT] == 0x80 && state[DIK_W] == 0);

    // Buffer size 4: the oldest events are dropped on overflow.
    for (int i = 0; i < 3; ++i) { ran_platform::InputKey(DIK_A, true); ran_platform::InputKey(DIK_A, false); }
    n = Read(kb, d, 16);
    CHECK(n == 4 && d[3].dwOfs == DIK_A && d[3].dwData == 0);

    // Peek leaves the events queued; a small read takes only what fits.
    ran_platform::InputKey(DIK_1, true); ran_platform::InputKey(DIK_2, true);
    DWORD p = 16;
    CHECK(kb->GetDeviceData(sizeof(DIDEVICEOBJECTDATA), d, &p, DIGDD_PEEK) == DI_OK && p == 2);
    CHECK(Read(kb, d, 1) == 1 && d[0].dwOfs == DIK_1);
    CHECK(Read(kb, d, 16) == 1 && d[0].dwOfs == DIK_2);

    // Mouse: relative axes, wheel, buttons.
    ran_platform::InputMouseMove(5, -3);
    ran_platform::InputMouseWheel(120);
    ran_platform::InputMouseButton(1, true);
    n = Read(mouse, d, 16);
    CHECK(n == 4);
    CHECK(d[0].dwOfs == DIMOFS_X && (LONG)d[0].dwData == 5);
    CHECK(d[1].dwOfs == DIMOFS_Y && (LONG)d[1].dwData == -3);
    CHECK(d[2].dwOfs == DIMOFS_Z && (LONG)d[2].dwData == 120);
    CHECK(d[3].dwOfs == DIMOFS_BUTTON1 && d[3].dwData == 0x80);

    // Focus loss (Cmd+Tab): everything held comes up, Acquire keeps working afterwards.
    ran_platform::InputReleaseAll();
    n = Read(kb, d, 16);
    CHECK(n == 3);   // LSHIFT, 1, 2 up
    for (DWORD i = 0; i < n; ++i) CHECK(d[i].dwData == 0);
    n = Read(mouse, d, 16);
    CHECK(n == 1 && d[0].dwOfs == DIMOFS_BUTTON1 && d[0].dwData == 0);
    CHECK(kb->Unacquire() == DI_OK && kb->Acquire() == DI_OK);
    CHECK(di->EnumDevices(DI8DEVCLASS_GAMECTRL, nullptr, nullptr, DIEDFL_ATTACHEDONLY) == DI_OK);

    kb->Release(); mouse->Release(); di->Release();
    if (g_failed) { std::printf("%d check(s) failed\n", g_failed); return 1; }
    std::printf("dinput_sdl_test: all checks passed\n");
    return 0;
}
