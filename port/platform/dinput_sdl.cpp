// DirectInput 8 for the native build: the keyboard and mouse devices DxInputDevice creates,
// fed by the SDL3 event pump (input_queue.h). DxInputDevice keeps using its DirectInput code
// path unchanged (buffered GetDeviceData + Acquire), but there is nothing to lose here: Acquire
// always succeeds and focus changes only release held keys - the Wine-era Cmd+Tab input loss
// came from DirectInput acquisition and cannot happen. Joysticks: none are enumerated.
#include "ran_compat.h"
#include <dinput.h>
#include "input_queue.h"
#include <cstring>
#include <deque>
#include <mutex>

namespace {

enum class Kind { Keyboard, Mouse };

struct Event { DWORD ofs; DWORD data; };

struct State {
    std::mutex lock;
    std::deque<Event> keys, mouse;
    BYTE keyDown[256] = {};
    BYTE buttonDown[8] = {};
    DWORD sequence = 0;
    size_t keyCap = 64, mouseCap = 64;   // DIPROP_BUFFERSIZE; oldest events drop on overflow
};
State& S() { static State s; return s; }

void Push(std::deque<Event>& q, size_t cap, Event e)
{
    q.push_back(e);
    while (q.size() > cap) q.pop_front();
}

class Device final : public IDirectInputDevice8A
{
public:
    explicit Device(Kind k) : m_kind(k) {}

    STDMETHOD(QueryInterface)(THIS_ REFIID, LPVOID* ppv) { if (ppv) { *ppv = this; AddRef(); } return S_OK; }
    STDMETHOD_(ULONG, AddRef)(THIS) { return ++m_refs; }
    STDMETHOD_(ULONG, Release)(THIS) { const ULONG r = --m_refs; if (!r) delete this; return r; }
    STDMETHOD(GetCapabilities)(THIS_ LPDIDEVCAPS) { return DIERR_UNSUPPORTED; }
    STDMETHOD(EnumObjects)(THIS_ LPDIENUMDEVICEOBJECTSCALLBACKA, LPVOID, DWORD) { return DI_OK; }
    STDMETHOD(GetProperty)(THIS_ REFGUID, LPDIPROPHEADER) { return DIERR_UNSUPPORTED; }
    STDMETHOD(SetProperty)(THIS_ REFGUID prop, LPCDIPROPHEADER hdr)
    {
        if (&prop == &DIPROP_BUFFERSIZE && hdr) {
            const DWORD n = ((const DIPROPDWORD*)hdr)->dwData;
            std::lock_guard<std::mutex> g(S().lock);
            (m_kind == Kind::Keyboard ? S().keyCap : S().mouseCap) = n ? n : 1;
        }
        return DI_OK;
    }
    STDMETHOD(Acquire)(THIS) { return DI_OK; }
    STDMETHOD(Unacquire)(THIS) { return DI_OK; }
    STDMETHOD(GetDeviceState)(THIS_ DWORD cb, LPVOID data)
    {
        if (!data) return DIERR_INVALIDPARAM;
        std::lock_guard<std::mutex> g(S().lock);
        if (m_kind == Kind::Keyboard) {
            std::memcpy(data, S().keyDown, cb < 256 ? cb : 256);
        } else {
            std::memset(data, 0, cb);
            DIMOUSESTATE2 st = {};
            std::memcpy(st.rgbButtons, S().buttonDown, sizeof(st.rgbButtons));
            std::memcpy(data, &st, cb < sizeof(st) ? cb : sizeof(st));
        }
        return DI_OK;
    }
    STDMETHOD(GetDeviceData)(THIS_ DWORD cbObjectData, LPDIDEVICEOBJECTDATA rgdod, LPDWORD pdwInOut, DWORD flags)
    {
        if (!pdwInOut) return DIERR_INVALIDPARAM;
        std::lock_guard<std::mutex> g(S().lock);
        std::deque<Event>& q = m_kind == Kind::Keyboard ? S().keys : S().mouse;
        const bool peek = (flags & DIGDD_PEEK) != 0;
        DWORD n = 0;
        while (n < *pdwInOut && n < q.size()) {
            if (rgdod) {
                DIDEVICEOBJECTDATA* d = (DIDEVICEOBJECTDATA*)((BYTE*)rgdod + (size_t)n * cbObjectData);
                std::memset(d, 0, cbObjectData);
                d->dwOfs = q[n].ofs;
                d->dwData = q[n].data;
                d->dwTimeStamp = timeGetTime();
                d->dwSequence = ++S().sequence;
            }
            ++n;
        }
        if (!peek) q.erase(q.begin(), q.begin() + n);
        *pdwInOut = n;
        return DI_OK;
    }
    STDMETHOD(SetDataFormat)(THIS_ LPCDIDATAFORMAT) { return DI_OK; }
    STDMETHOD(SetEventNotification)(THIS_ HANDLE) { return DI_OK; }
    STDMETHOD(SetCooperativeLevel)(THIS_ HWND, DWORD) { return DI_OK; }
    STDMETHOD(GetObjectInfo)(THIS_ LPDIDEVICEOBJECTINSTANCEA, DWORD, DWORD) { return DIERR_UNSUPPORTED; }
    STDMETHOD(GetDeviceInfo)(THIS_ LPDIDEVICEINSTANCEA) { return DIERR_UNSUPPORTED; }
    STDMETHOD(RunControlPanel)(THIS_ HWND, DWORD) { return DIERR_UNSUPPORTED; }
    STDMETHOD(Initialize)(THIS_ HINSTANCE, DWORD, REFGUID) { return DI_OK; }
    STDMETHOD(CreateEffect)(THIS_ REFGUID, LPCDIEFFECT, LPDIRECTINPUTEFFECT*, LPUNKNOWN) { return DIERR_UNSUPPORTED; }
    STDMETHOD(EnumEffects)(THIS_ LPDIENUMEFFECTSCALLBACKA, LPVOID, DWORD) { return DI_OK; }
    STDMETHOD(GetEffectInfo)(THIS_ LPDIEFFECTINFOA, REFGUID) { return DIERR_UNSUPPORTED; }
    STDMETHOD(GetForceFeedbackState)(THIS_ LPDWORD) { return DIERR_UNSUPPORTED; }
    STDMETHOD(SendForceFeedbackCommand)(THIS_ DWORD) { return DIERR_UNSUPPORTED; }
    STDMETHOD(EnumCreatedEffectObjects)(THIS_ LPDIENUMCREATEDEFFECTOBJECTSCALLBACK, LPVOID, DWORD) { return DI_OK; }
    STDMETHOD(Escape)(THIS_ LPDIEFFESCAPE) { return DIERR_UNSUPPORTED; }
    STDMETHOD(Poll)(THIS) { return DI_NOEFFECT; }
    STDMETHOD(SendDeviceData)(THIS_ DWORD, LPCDIDEVICEOBJECTDATA, LPDWORD, DWORD) { return DIERR_UNSUPPORTED; }
    STDMETHOD(EnumEffectsInFile)(THIS_ LPCSTR, LPDIENUMEFFECTSINFILECALLBACK, LPVOID, DWORD) { return DIERR_UNSUPPORTED; }
    STDMETHOD(WriteEffectToFile)(THIS_ LPCSTR, DWORD, LPDIFILEEFFECT, DWORD) { return DIERR_UNSUPPORTED; }
    STDMETHOD(BuildActionMap)(THIS_ LPDIACTIONFORMATA, LPCSTR, DWORD) { return DIERR_UNSUPPORTED; }
    STDMETHOD(SetActionMap)(THIS_ LPDIACTIONFORMATA, LPCSTR, DWORD) { return DIERR_UNSUPPORTED; }
    STDMETHOD(GetImageInfo)(THIS_ LPDIDEVICEIMAGEINFOHEADERA) { return DIERR_UNSUPPORTED; }

private:
    Kind m_kind;
    ULONG m_refs = 1;
};

class DirectInput final : public IDirectInput8A
{
public:
    STDMETHOD(QueryInterface)(THIS_ REFIID, LPVOID* ppv) { if (ppv) { *ppv = this; AddRef(); } return S_OK; }
    STDMETHOD_(ULONG, AddRef)(THIS) { return ++m_refs; }
    STDMETHOD_(ULONG, Release)(THIS) { const ULONG r = --m_refs; if (!r) delete this; return r; }
    STDMETHOD(CreateDevice)(THIS_ REFGUID guid, LPDIRECTINPUTDEVICE8A* out, LPUNKNOWN)
    {
        if (!out) return DIERR_INVALIDPARAM;
        if (IsEqualGUID(guid, GUID_SysKeyboard)) { *out = new Device(Kind::Keyboard); return DI_OK; }
        if (IsEqualGUID(guid, GUID_SysMouse))    { *out = new Device(Kind::Mouse); return DI_OK; }
        *out = nullptr;
        return DIERR_DEVICENOTREG;
    }
    STDMETHOD(EnumDevices)(THIS_ DWORD, LPDIENUMDEVICESCALLBACKA, LPVOID, DWORD) { return DI_OK; }   // no joysticks
    STDMETHOD(GetDeviceStatus)(THIS_ REFGUID) { return DI_OK; }
    STDMETHOD(RunControlPanel)(THIS_ HWND, DWORD) { return DIERR_UNSUPPORTED; }
    STDMETHOD(Initialize)(THIS_ HINSTANCE, DWORD) { return DI_OK; }
    STDMETHOD(FindDevice)(THIS_ REFGUID, LPCSTR, LPGUID) { return DIERR_DEVICENOTREG; }
    STDMETHOD(EnumDevicesBySemantics)(THIS_ LPCSTR, LPDIACTIONFORMATA, LPDIENUMDEVICESBYSEMANTICSCBA, LPVOID, DWORD) { return DI_OK; }
    STDMETHOD(ConfigureDevices)(THIS_ LPDICONFIGUREDEVICESCALLBACK, LPDICONFIGUREDEVICESPARAMSA, DWORD, LPVOID) { return DIERR_UNSUPPORTED; }

private:
    ULONG m_refs = 1;
};

} // namespace

namespace ran_platform {
void InputKey(uint8_t dik, bool down)
{
    if (!dik) return;
    std::lock_guard<std::mutex> g(S().lock);
    if ((S().keyDown[dik] != 0) == down) return;   // SDL key repeat: DirectInput reports edges only
    S().keyDown[dik] = down ? 0x80 : 0;
    Push(S().keys, S().keyCap, { dik, (DWORD)(down ? 0x80 : 0) });
}
void InputMouseMove(int dx, int dy)
{
    std::lock_guard<std::mutex> g(S().lock);
    if (dx) Push(S().mouse, S().mouseCap, { (DWORD)DIMOFS_X, (DWORD)dx });
    if (dy) Push(S().mouse, S().mouseCap, { (DWORD)DIMOFS_Y, (DWORD)dy });
}
void InputMouseWheel(int delta)
{
    if (!delta) return;
    std::lock_guard<std::mutex> g(S().lock);
    Push(S().mouse, S().mouseCap, { (DWORD)DIMOFS_Z, (DWORD)delta });
}
void InputMouseButton(int button, bool down)
{
    if (button < 0 || button > 7) return;
    std::lock_guard<std::mutex> g(S().lock);
    S().buttonDown[button] = down ? 0x80 : 0;
    Push(S().mouse, S().mouseCap, { (DWORD)(DIMOFS_BUTTON0 + button), (DWORD)(down ? 0x80 : 0) });
}
void InputReleaseAll()
{
    for (int k = 1; k < 256; ++k) {
        bool held;
        { std::lock_guard<std::mutex> g(S().lock); held = S().keyDown[k] != 0; }
        if (held) InputKey((uint8_t)k, false);
    }
    for (int b = 0; b < 8; ++b) {
        bool held;
        { std::lock_guard<std::mutex> g(S().lock); held = S().buttonDown[b] != 0; }
        if (held) InputMouseButton(b, false);
    }
}
} // namespace ran_platform

// The game passes these to SetDataFormat; the formats are implied by the device kind.
extern "C" {
const DIDATAFORMAT c_dfDIKeyboard = { sizeof(DIDATAFORMAT), sizeof(DIOBJECTDATAFORMAT), DIDF_RELAXIS, 256, 0, nullptr };
const DIDATAFORMAT c_dfDIMouse2 = { sizeof(DIDATAFORMAT), sizeof(DIOBJECTDATAFORMAT), DIDF_RELAXIS, sizeof(DIMOUSESTATE2), 0, nullptr };

HRESULT WINAPI DirectInput8Create(HINSTANCE, DWORD, REFIID, LPVOID* ppvOut, LPUNKNOWN)
{
    if (!ppvOut) return DIERR_INVALIDPARAM;
    *ppvOut = static_cast<IDirectInput8A*>(new DirectInput());
    return DI_OK;
}
}
