// DirectSound 8 for the native build: the subset the sound engine uses (CSoundManager/CSound in
// dsutil.cpp, DxBgmSound's streaming buffer, the 3D listener and 3D buffers), implemented as a
// software mixer on one SDL3 audio stream. The game's sound code is unchanged.
//   - Secondary buffers hold PCM (8/16-bit, mono/stereo, any rate) in memory; Lock hands out
//     pointers into it like DirectSound (two regions when the range wraps), so the BGM's
//     write-ahead streaming against GetCurrentPosition works as on Windows.
//   - Duplicated buffers share the sample memory and play independently.
//   - Volume/pan are DirectSound's hundredths of a dB; 3D buffers get distance rolloff
//     (DirectSound's inverse-distance model, clamped at min/max distance) and left/right pan
//     from the listener orientation. Deferred 3D settings are applied immediately.
//   - Effects (SetFX) and notifications are not provided (the client never uses them).
#include "ran_compat.h"
#include <mmsystem.h>
#include <dsound.h>
#include "dsound_mixer.h"
#include <SDL3/SDL.h>
#include <algorithm>
#include <cmath>
#include <cstdio>
#include <cstring>
#include <memory>
#include <mutex>
#include <vector>

namespace {

// Guards every buffer's state and the playing list. Never destroyed: the SDL audio thread can
// still call the mixer while static destructors run at exit.
std::mutex& g_lock = *new std::mutex;
bool g_manualOutput = false;
SDL_AudioStream* g_stream = nullptr;

float VolumeToGain(LONG mB)
{
    if (mB <= DSBVOLUME_MIN) return 0.0f;
    if (mB >= 0) return 1.0f;
    return std::pow(10.0f, (float)mB / 2000.0f);
}

struct Vec { float x, y, z; };
Vec V(const D3DVECTOR& v) { return { v.x, v.y, v.z }; }
float Dot(Vec a, Vec b) { return a.x * b.x + a.y * b.y + a.z * b.z; }
Vec Sub(Vec a, Vec b) { return { a.x - b.x, a.y - b.y, a.z - b.z }; }
Vec Cross(Vec a, Vec b) { return { a.y * b.z - a.z * b.y, a.z * b.x - a.x * b.z, a.x * b.y - a.y * b.x }; }
float Len(Vec a) { return std::sqrt(Dot(a, a)); }

class Device;
class Buffer;
std::vector<Buffer*>& Playing() { static std::vector<Buffer*>& v = *new std::vector<Buffer*>; return v; }   // see g_lock

DS3DLISTENER DefaultListener()
{
    DS3DLISTENER l = {};
    l.dwSize = sizeof(l);
    l.vOrientFront.z = 1.0f;
    l.vOrientTop.y = 1.0f;
    l.flDistanceFactor = DS3D_DEFAULTDISTANCEFACTOR;
    l.flRolloffFactor = DS3D_DEFAULTROLLOFFFACTOR;
    l.flDopplerFactor = DS3D_DEFAULTDOPPLERFACTOR;
    return l;
}

DS3DBUFFER Default3D()
{
    DS3DBUFFER b = {};
    b.dwSize = sizeof(b);
    b.dwInsideConeAngle = DS3D_DEFAULTCONEANGLE;
    b.dwOutsideConeAngle = DS3D_DEFAULTCONEANGLE;
    b.vConeOrientation.z = 1.0f;
    b.lConeOutsideVolume = DS3D_DEFAULTCONEOUTSIDEVOLUME;
    b.flMinDistance = DS3D_DEFAULTMINDISTANCE;
    b.flMaxDistance = DS3D_DEFAULTMAXDISTANCE;
    b.dwMode = DS3DMODE_NORMAL;
    return b;
}

// ---------------------------------------------------------------------------------------------
class Listener final : public IDirectSound3DListener
{
public:
    explicit Listener(IUnknown* owner) : m_owner(owner) {}
    DS3DLISTENER state = DefaultListener();

    STDMETHOD(QueryInterface)(THIS_ REFIID, LPVOID* ppv) { if (ppv) *ppv = nullptr; return E_NOINTERFACE; }
    STDMETHOD_(ULONG, AddRef)(THIS) { return m_owner->AddRef(); }
    STDMETHOD_(ULONG, Release)(THIS) { return m_owner->Release(); }
    STDMETHOD(GetAllParameters)(THIS_ LPDS3DLISTENER p) { if (!p) return DSERR_INVALIDPARAM; std::lock_guard<std::mutex> g(g_lock); *p = state; return DS_OK; }
    STDMETHOD(GetDistanceFactor)(THIS_ D3DVALUE* p) { if (p) *p = state.flDistanceFactor; return DS_OK; }
    STDMETHOD(GetDopplerFactor)(THIS_ D3DVALUE* p) { if (p) *p = state.flDopplerFactor; return DS_OK; }
    STDMETHOD(GetOrientation)(THIS_ D3DVECTOR* f, D3DVECTOR* t) { if (f) *f = state.vOrientFront; if (t) *t = state.vOrientTop; return DS_OK; }
    STDMETHOD(GetPosition)(THIS_ D3DVECTOR* p) { if (p) *p = state.vPosition; return DS_OK; }
    STDMETHOD(GetRolloffFactor)(THIS_ D3DVALUE* p) { if (p) *p = state.flRolloffFactor; return DS_OK; }
    STDMETHOD(GetVelocity)(THIS_ D3DVECTOR* p) { if (p) *p = state.vVelocity; return DS_OK; }
    STDMETHOD(SetAllParameters)(THIS_ LPCDS3DLISTENER p, DWORD)
    {
        if (!p) return DSERR_INVALIDPARAM;
        std::lock_guard<std::mutex> g(g_lock);
        state = *p;
        state.dwSize = sizeof(state);
        return DS_OK;
    }
    STDMETHOD(SetDistanceFactor)(THIS_ D3DVALUE v, DWORD) { std::lock_guard<std::mutex> g(g_lock); state.flDistanceFactor = v; return DS_OK; }
    STDMETHOD(SetDopplerFactor)(THIS_ D3DVALUE v, DWORD) { std::lock_guard<std::mutex> g(g_lock); state.flDopplerFactor = v; return DS_OK; }
    STDMETHOD(SetOrientation)(THIS_ D3DVALUE fx, D3DVALUE fy, D3DVALUE fz, D3DVALUE tx, D3DVALUE ty, D3DVALUE tz, DWORD)
    {
        std::lock_guard<std::mutex> g(g_lock);
        state.vOrientFront = { fx, fy, fz };
        state.vOrientTop = { tx, ty, tz };
        return DS_OK;
    }
    STDMETHOD(SetPosition)(THIS_ D3DVALUE x, D3DVALUE y, D3DVALUE z, DWORD) { std::lock_guard<std::mutex> g(g_lock); state.vPosition = { x, y, z }; return DS_OK; }
    STDMETHOD(SetRolloffFactor)(THIS_ D3DVALUE v, DWORD) { std::lock_guard<std::mutex> g(g_lock); state.flRolloffFactor = v; return DS_OK; }
    STDMETHOD(SetVelocity)(THIS_ D3DVALUE x, D3DVALUE y, D3DVALUE z, DWORD) { std::lock_guard<std::mutex> g(g_lock); state.vVelocity = { x, y, z }; return DS_OK; }
    STDMETHOD(CommitDeferredSettings)(THIS) { return DS_OK; }

private:
    IUnknown* m_owner;
};

// ---------------------------------------------------------------------------------------------
class Device final : public IDirectSound8
{
public:
    Listener listener{ this };

    STDMETHOD(QueryInterface)(THIS_ REFIID riid, LPVOID* ppv)
    {
        if (!ppv) return E_POINTER;
        if (IsEqualGUID(riid, IID_IUnknown) || IsEqualGUID(riid, IID_IDirectSound) || IsEqualGUID(riid, IID_IDirectSound8)) {
            *ppv = this; AddRef(); return S_OK;
        }
        *ppv = nullptr;
        return E_NOINTERFACE;
    }
    STDMETHOD_(ULONG, AddRef)(THIS) { return ++m_refs; }
    STDMETHOD_(ULONG, Release)(THIS) { const ULONG r = --m_refs; if (!r) delete this; return r; }
    STDMETHOD(CreateSoundBuffer)(THIS_ LPCDSBUFFERDESC desc, LPDIRECTSOUNDBUFFER* out, LPUNKNOWN);
    STDMETHOD(GetCaps)(THIS_ LPDSCAPS caps)
    {
        if (!caps) return DSERR_INVALIDPARAM;
        const DWORD size = caps->dwSize;
        std::memset(caps, 0, sizeof(*caps));
        caps->dwSize = size;
        caps->dwFlags = DSCAPS_PRIMARYMONO | DSCAPS_PRIMARYSTEREO | DSCAPS_PRIMARY8BIT | DSCAPS_PRIMARY16BIT |
                        DSCAPS_SECONDARYMONO | DSCAPS_SECONDARYSTEREO | DSCAPS_SECONDARY8BIT | DSCAPS_SECONDARY16BIT;
        caps->dwMinSecondarySampleRate = DSBFREQUENCY_MIN;
        caps->dwMaxSecondarySampleRate = DSBFREQUENCY_MAX;
        caps->dwPrimaryBuffers = 1;
        return DS_OK;
    }
    STDMETHOD(DuplicateSoundBuffer)(THIS_ LPDIRECTSOUNDBUFFER original, LPDIRECTSOUNDBUFFER* out);
    STDMETHOD(SetCooperativeLevel)(THIS_ HWND, DWORD) { return DS_OK; }
    STDMETHOD(Compact)(THIS) { return DS_OK; }
    STDMETHOD(GetSpeakerConfig)(THIS_ LPDWORD p) { if (p) *p = DSSPEAKER_STEREO; return DS_OK; }
    STDMETHOD(SetSpeakerConfig)(THIS_ DWORD) { return DS_OK; }
    STDMETHOD(Initialize)(THIS_ LPCGUID) { return DSERR_ALREADYINITIALIZED; }
    STDMETHOD(VerifyCertification)(THIS_ LPDWORD p) { if (p) *p = DS_CERTIFIED; return DS_OK; }

private:
    ULONG m_refs = 1;
};

// ---------------------------------------------------------------------------------------------
struct Samples
{
    std::vector<BYTE> bytes;
    WAVEFORMATEX fmt = {};
};

class Buffer final : public IDirectSoundBuffer8
{
public:
    Buffer(Device* dev, DWORD flags, std::shared_ptr<Samples> data)
        : m_dev(dev), m_flags(flags), m_data(std::move(data)) { m_dev->AddRef(); }
    ~Buffer() { m_dev->Release(); }

    bool IsPrimary() const { return (m_flags & DSBCAPS_PRIMARYBUFFER) != 0; }

    // --- IUnknown
    STDMETHOD(QueryInterface)(THIS_ REFIID riid, LPVOID* ppv)
    {
        if (!ppv) return E_POINTER;
        *ppv = nullptr;
        if (IsEqualGUID(riid, IID_IUnknown) || IsEqualGUID(riid, IID_IDirectSoundBuffer) ||
            (!IsPrimary() && IsEqualGUID(riid, IID_IDirectSoundBuffer8))) {
            *ppv = static_cast<IDirectSoundBuffer8*>(this); AddRef(); return S_OK;
        }
        if (IsPrimary() && IsEqualGUID(riid, IID_IDirectSound3DListener)) {
            *ppv = &m_dev->listener; m_dev->listener.AddRef(); return S_OK;
        }
        if (!IsPrimary() && (m_flags & DSBCAPS_CTRL3D) && IsEqualGUID(riid, IID_IDirectSound3DBuffer)) {
            *ppv = &m_3d; AddRef(); return S_OK;
        }
        return E_NOINTERFACE;
    }
    STDMETHOD_(ULONG, AddRef)(THIS) { return ++m_refs; }
    STDMETHOD_(ULONG, Release)(THIS)
    {
        const ULONG r = --m_refs;
        if (!r) {
            {
                std::lock_guard<std::mutex> g(g_lock);
                auto& p = Playing();
                p.erase(std::remove(p.begin(), p.end(), this), p.end());
            }
            delete this;
        }
        return r;
    }

    // --- IDirectSoundBuffer
    STDMETHOD(GetCaps)(THIS_ LPDSBCAPS caps)
    {
        if (!caps) return DSERR_INVALIDPARAM;
        caps->dwFlags = m_flags | DSBCAPS_LOCSOFTWARE;
        caps->dwBufferBytes = (DWORD)m_data->bytes.size();
        caps->dwUnlockTransferRate = 0;
        caps->dwPlayCpuOverhead = 0;
        return DS_OK;
    }
    STDMETHOD(GetCurrentPosition)(THIS_ LPDWORD play, LPDWORD write)
    {
        std::lock_guard<std::mutex> g(g_lock);
        const DWORD size = (DWORD)m_data->bytes.size();
        const DWORD align = BlockAlign();
        const DWORD p = size ? ((DWORD)m_pos * align) % size : 0;
        DWORD w = p;
        if (m_playing && size) {
            // DirectSound keeps the write cursor a little ahead of the play cursor (~15 ms).
            const DWORD ahead = std::max<DWORD>(align, (DWORD)(m_data->fmt.nAvgBytesPerSec * 15 / 1000) / align * align);
            w = (p + ahead) % size;
        }
        if (play) *play = p;
        if (write) *write = w;
        return DS_OK;
    }
    STDMETHOD(GetFormat)(THIS_ LPWAVEFORMATEX fmt, DWORD allocated, LPDWORD written)
    {
        const DWORD n = (DWORD)sizeof(WAVEFORMATEX);
        if (written) *written = n;
        if (fmt) {
            if (allocated < n) return DSERR_INVALIDPARAM;
            *fmt = m_data->fmt;
            fmt->cbSize = 0;
        }
        return DS_OK;
    }
    STDMETHOD(GetVolume)(THIS_ LPLONG v) { if (!v) return DSERR_INVALIDPARAM; *v = m_volume; return DS_OK; }
    STDMETHOD(GetPan)(THIS_ LPLONG v) { if (!v) return DSERR_INVALIDPARAM; *v = m_pan; return DS_OK; }
    STDMETHOD(GetFrequency)(THIS_ LPDWORD v) { if (!v) return DSERR_INVALIDPARAM; *v = m_freq ? m_freq : m_data->fmt.nSamplesPerSec; return DS_OK; }
    STDMETHOD(GetStatus)(THIS_ LPDWORD s)
    {
        if (!s) return DSERR_INVALIDPARAM;
        std::lock_guard<std::mutex> g(g_lock);
        *s = m_playing ? (DSBSTATUS_PLAYING | (m_looping ? DSBSTATUS_LOOPING : 0)) : 0;
        return DS_OK;
    }
    STDMETHOD(Initialize)(THIS_ LPDIRECTSOUND, LPCDSBUFFERDESC) { return DSERR_ALREADYINITIALIZED; }
    STDMETHOD(Lock)(THIS_ DWORD offset, DWORD bytes, LPVOID* p1, LPDWORD n1, LPVOID* p2, LPDWORD n2, DWORD flags)
    {
        if (IsPrimary()) return DSERR_PRIOLEVELNEEDED;
        const DWORD size = (DWORD)m_data->bytes.size();
        if (!p1 || !n1 || !size) return DSERR_INVALIDPARAM;
        if (flags & DSBLOCK_FROMWRITECURSOR) GetCurrentPosition(nullptr, &offset);
        if (flags & DSBLOCK_ENTIREBUFFER) bytes = size;
        if (offset >= size || bytes > size || bytes == 0) return DSERR_INVALIDPARAM;
        BYTE* base = m_data->bytes.data();
        const DWORD first = std::min(bytes, size - offset);
        *p1 = base + offset;
        *n1 = first;
        if (p2) *p2 = (bytes > first) ? base : nullptr;
        if (n2) *n2 = bytes - first;   // without a second region DirectSound truncates at the end
        return DS_OK;
    }
    STDMETHOD(Play)(THIS_ DWORD, DWORD, DWORD flags)
    {
        if (IsPrimary()) return DS_OK;
        std::lock_guard<std::mutex> g(g_lock);
        m_looping = (flags & DSBPLAY_LOOPING) != 0;
        if (!m_playing) {
            m_playing = true;
            Playing().push_back(this);
        }
        return DS_OK;
    }
    STDMETHOD(SetCurrentPosition)(THIS_ DWORD pos)
    {
        std::lock_guard<std::mutex> g(g_lock);
        const DWORD size = (DWORD)m_data->bytes.size();
        if (size) m_pos = (double)((pos % size) / BlockAlign());
        return DS_OK;
    }
    STDMETHOD(SetFormat)(THIS_ LPCWAVEFORMATEX fmt)
    {
        // The primary buffer's format only matters to a hardware mixer; ours mixes in float.
        if (!fmt) return DSERR_INVALIDPARAM;
        return IsPrimary() ? DS_OK : DSERR_INVALIDCALL;
    }
    STDMETHOD(SetVolume)(THIS_ LONG v)
    {
        if (v < DSBVOLUME_MIN || v > DSBVOLUME_MAX) return DSERR_INVALIDPARAM;
        std::lock_guard<std::mutex> g(g_lock);
        m_volume = v;
        return DS_OK;
    }
    STDMETHOD(SetPan)(THIS_ LONG v)
    {
        if (v < DSBPAN_LEFT || v > DSBPAN_RIGHT) return DSERR_INVALIDPARAM;
        std::lock_guard<std::mutex> g(g_lock);
        m_pan = v;
        return DS_OK;
    }
    STDMETHOD(SetFrequency)(THIS_ DWORD f)
    {
        if (f != DSBFREQUENCY_ORIGINAL && (f < DSBFREQUENCY_MIN || f > DSBFREQUENCY_MAX)) return DSERR_INVALIDPARAM;
        std::lock_guard<std::mutex> g(g_lock);
        m_freq = f;
        return DS_OK;
    }
    STDMETHOD(Stop)(THIS)
    {
        std::lock_guard<std::mutex> g(g_lock);
        StopLocked();
        return DS_OK;
    }
    STDMETHOD(Unlock)(THIS_ LPVOID, DWORD, LPVOID, DWORD) { return DS_OK; }
    STDMETHOD(Restore)(THIS) { return DS_OK; }

    // --- IDirectSoundBuffer8
    STDMETHOD(SetFX)(THIS_ DWORD count, LPDSEFFECTDESC, LPDWORD results)
    {
        if (count == 0) return DS_OK;
        if (results) for (DWORD i = 0; i < count; ++i) results[i] = DSFXR_UNKNOWN;
        return DSERR_CONTROLUNAVAIL;
    }
    STDMETHOD(AcquireResources)(THIS_ DWORD, DWORD, LPDWORD) { return DS_OK; }
    STDMETHOD(GetObjectInPath)(THIS_ REFGUID, DWORD, REFGUID, LPVOID* ppv) { if (ppv) *ppv = nullptr; return DSERR_OBJECTNOTFOUND; }

    // --- used by the device and the mixer (caller holds g_lock where noted)
    std::shared_ptr<Samples> Data() const { return m_data; }
    DWORD Flags() const { return m_flags; }
    void CopySettingsFrom(const Buffer& o) { m_volume = o.m_volume; m_pan = o.m_pan; m_freq = o.m_freq; m_3d.state = o.m_3d.state; }
    void MixLocked(float* out, int frames);

private:
    class Buffer3D final : public IDirectSound3DBuffer
    {
    public:
        explicit Buffer3D(Buffer* owner) : m_owner(owner) {}
        DS3DBUFFER state = Default3D();

        STDMETHOD(QueryInterface)(THIS_ REFIID riid, LPVOID* ppv) { return m_owner->QueryInterface(riid, ppv); }
        STDMETHOD_(ULONG, AddRef)(THIS) { return m_owner->AddRef(); }
        STDMETHOD_(ULONG, Release)(THIS) { return m_owner->Release(); }
        STDMETHOD(GetAllParameters)(THIS_ LPDS3DBUFFER p) { if (!p) return DSERR_INVALIDPARAM; std::lock_guard<std::mutex> g(g_lock); *p = state; return DS_OK; }
        STDMETHOD(GetConeAngles)(THIS_ LPDWORD i, LPDWORD o) { if (i) *i = state.dwInsideConeAngle; if (o) *o = state.dwOutsideConeAngle; return DS_OK; }
        STDMETHOD(GetConeOrientation)(THIS_ D3DVECTOR* v) { if (v) *v = state.vConeOrientation; return DS_OK; }
        STDMETHOD(GetConeOutsideVolume)(THIS_ LPLONG v) { if (v) *v = state.lConeOutsideVolume; return DS_OK; }
        STDMETHOD(GetMaxDistance)(THIS_ D3DVALUE* v) { if (v) *v = state.flMaxDistance; return DS_OK; }
        STDMETHOD(GetMinDistance)(THIS_ D3DVALUE* v) { if (v) *v = state.flMinDistance; return DS_OK; }
        STDMETHOD(GetMode)(THIS_ LPDWORD v) { if (v) *v = state.dwMode; return DS_OK; }
        STDMETHOD(GetPosition)(THIS_ D3DVECTOR* v) { if (v) *v = state.vPosition; return DS_OK; }
        STDMETHOD(GetVelocity)(THIS_ D3DVECTOR* v) { if (v) *v = state.vVelocity; return DS_OK; }
        STDMETHOD(SetAllParameters)(THIS_ LPCDS3DBUFFER p, DWORD)
        {
            if (!p) return DSERR_INVALIDPARAM;
            std::lock_guard<std::mutex> g(g_lock);
            state = *p;
            state.dwSize = sizeof(state);
            return DS_OK;
        }
        STDMETHOD(SetConeAngles)(THIS_ DWORD i, DWORD o, DWORD) { std::lock_guard<std::mutex> g(g_lock); state.dwInsideConeAngle = i; state.dwOutsideConeAngle = o; return DS_OK; }
        STDMETHOD(SetConeOrientation)(THIS_ D3DVALUE x, D3DVALUE y, D3DVALUE z, DWORD) { std::lock_guard<std::mutex> g(g_lock); state.vConeOrientation = { x, y, z }; return DS_OK; }
        STDMETHOD(SetConeOutsideVolume)(THIS_ LONG v, DWORD) { std::lock_guard<std::mutex> g(g_lock); state.lConeOutsideVolume = v; return DS_OK; }
        STDMETHOD(SetMaxDistance)(THIS_ D3DVALUE v, DWORD) { std::lock_guard<std::mutex> g(g_lock); state.flMaxDistance = v; return DS_OK; }
        STDMETHOD(SetMinDistance)(THIS_ D3DVALUE v, DWORD) { std::lock_guard<std::mutex> g(g_lock); state.flMinDistance = v; return DS_OK; }
        STDMETHOD(SetMode)(THIS_ DWORD v, DWORD) { std::lock_guard<std::mutex> g(g_lock); state.dwMode = v; return DS_OK; }
        STDMETHOD(SetPosition)(THIS_ D3DVALUE x, D3DVALUE y, D3DVALUE z, DWORD) { std::lock_guard<std::mutex> g(g_lock); state.vPosition = { x, y, z }; return DS_OK; }
        STDMETHOD(SetVelocity)(THIS_ D3DVALUE x, D3DVALUE y, D3DVALUE z, DWORD) { std::lock_guard<std::mutex> g(g_lock); state.vVelocity = { x, y, z }; return DS_OK; }

    private:
        Buffer* m_owner;
    };

    DWORD BlockAlign() const { return m_data->fmt.nBlockAlign ? m_data->fmt.nBlockAlign : 1; }
    void StopLocked()
    {
        if (!m_playing) return;
        m_playing = false;
        auto& p = Playing();
        p.erase(std::remove(p.begin(), p.end(), this), p.end());
    }
    float Sample(size_t frame, int channel) const
    {
        const WAVEFORMATEX& f = m_data->fmt;
        const BYTE* s = m_data->bytes.data() + frame * f.nBlockAlign;
        const int c = (f.nChannels > 1) ? channel : 0;
        if (f.wBitsPerSample == 8) return ((float)s[c] - 128.0f) / 128.0f;
        int16_t v;
        std::memcpy(&v, s + c * 2, 2);
        return (float)v / 32768.0f;
    }

    ULONG m_refs = 1;
    Device* m_dev;
    DWORD m_flags;
    std::shared_ptr<Samples> m_data;
    Buffer3D m_3d{ this };
    bool m_playing = false, m_looping = false;
    double m_pos = 0.0;                         // play position in frames
    LONG m_volume = DSBVOLUME_MAX, m_pan = DSBPAN_CENTER;
    DWORD m_freq = DSBFREQUENCY_ORIGINAL;
};

void Buffer::MixLocked(float* out, int frames)
{
    const WAVEFORMATEX& f = m_data->fmt;
    const size_t total = f.nBlockAlign ? m_data->bytes.size() / f.nBlockAlign : 0;
    if (!total) { StopLocked(); return; }

    float gl = VolumeToGain(m_volume), gr = gl;
    if (m_pan > 0) gl *= VolumeToGain(-m_pan);
    if (m_pan < 0) gr *= VolumeToGain(m_pan);

    if ((m_flags & DSBCAPS_CTRL3D) && m_3d.state.dwMode != DS3DMODE_DISABLE) {
        const DS3DLISTENER& L = m_dev->listener.state;
        const DS3DBUFFER& B = m_3d.state;
        // Head-relative sources are already in listener space; otherwise project onto the
        // listener axes (D3D left-handed: right = top x front).
        Vec rel = V(B.vPosition);
        Vec front = { 0, 0, 1 }, top = { 0, 1, 0 };
        if (B.dwMode != DS3DMODE_HEADRELATIVE) {
            rel = Sub(V(B.vPosition), V(L.vPosition));
            if (Len(V(L.vOrientFront)) > 0) front = V(L.vOrientFront);
            if (Len(V(L.vOrientTop)) > 0) top = V(L.vOrientTop);
        }
        Vec right = Cross(top, front);
        const float rl = Len(right);
        const float d = Len(rel);
        const float minD = B.flMinDistance > 0 ? B.flMinDistance : DS3D_DEFAULTMINDISTANCE;
        const float maxD = std::max(minD, B.flMaxDistance);
        float att = 1.0f;
        if ((m_flags & DSBCAPS_MUTE3DATMAXDISTANCE) && d > maxD) att = 0.0f;
        else {
            const float dc = std::min(std::max(d, minD), maxD);
            att = minD / (minD + L.flRolloffFactor * (dc - minD));
        }
        float side = (d > 1e-4f && rl > 1e-6f) ? Dot(rel, right) / (d * rl) : 0.0f;
        side = std::max(-1.0f, std::min(1.0f, side));
        gl *= att * (side > 0 ? 1.0f - 0.7f * side : 1.0f);
        gr *= att * (side < 0 ? 1.0f + 0.7f * side : 1.0f);
    }

    const double step = (double)(m_freq ? m_freq : f.nSamplesPerSec) / ran_platform::kDSoundOutputRate;
    for (int i = 0; i < frames; ++i) {
        const size_t i0 = (size_t)m_pos;
        size_t i1 = i0 + 1;
        if (i1 >= total) i1 = m_looping ? 0 : i0;
        const float t = (float)(m_pos - (double)i0);
        const float l = Sample(i0, 0) + (Sample(i1, 0) - Sample(i0, 0)) * t;
        const float r = Sample(i0, 1) + (Sample(i1, 1) - Sample(i0, 1)) * t;
        out[2 * i] += l * gl;
        out[2 * i + 1] += r * gr;
        m_pos += step;
        if (m_pos >= (double)total) {
            if (m_looping) m_pos = std::fmod(m_pos, (double)total);
            else { m_pos = 0.0; StopLocked(); return; }
        }
    }
}

HRESULT Device::CreateSoundBuffer(LPCDSBUFFERDESC desc, LPDIRECTSOUNDBUFFER* out, LPUNKNOWN)
{
    if (!desc || !out) return DSERR_INVALIDPARAM;
    *out = nullptr;
    auto data = std::make_shared<Samples>();
    if (desc->dwFlags & DSBCAPS_PRIMARYBUFFER) {
        if (desc->dwBufferBytes || desc->lpwfxFormat) return DSERR_INVALIDPARAM;
        data->fmt.wFormatTag = WAVE_FORMAT_PCM;
        data->fmt.nChannels = 2;
        data->fmt.nSamplesPerSec = ran_platform::kDSoundOutputRate;
        data->fmt.wBitsPerSample = 16;
        data->fmt.nBlockAlign = 4;
        data->fmt.nAvgBytesPerSec = ran_platform::kDSoundOutputRate * 4;
    } else {
        const WAVEFORMATEX* f = desc->lpwfxFormat;
        if (!f || desc->dwBufferBytes < DSBSIZE_MIN || desc->dwBufferBytes > DSBSIZE_MAX) return DSERR_INVALIDPARAM;
        if (f->wFormatTag != WAVE_FORMAT_PCM || (f->nChannels != 1 && f->nChannels != 2) ||
            (f->wBitsPerSample != 8 && f->wBitsPerSample != 16) ||
            f->nBlockAlign != f->nChannels * f->wBitsPerSample / 8 || !f->nSamplesPerSec)
            return DSERR_BADFORMAT;
        data->fmt = *f;
        data->fmt.cbSize = 0;
        // Fresh buffers are silence (8-bit PCM is unsigned).
        data->bytes.assign(desc->dwBufferBytes, f->wBitsPerSample == 8 ? 0x80 : 0x00);
    }
    *out = new Buffer(this, desc->dwFlags, std::move(data));
    return DS_OK;
}

HRESULT Device::DuplicateSoundBuffer(LPDIRECTSOUNDBUFFER original, LPDIRECTSOUNDBUFFER* out)
{
    if (!original || !out) return DSERR_INVALIDPARAM;
    Buffer* src = static_cast<Buffer*>(original);
    if (src->IsPrimary()) return DSERR_INVALIDCALL;
    Buffer* b = new Buffer(this, src->Flags(), src->Data());
    {
        std::lock_guard<std::mutex> g(g_lock);
        b->CopySettingsFrom(*src);
    }
    *out = b;
    return DS_OK;
}

void SDLCALL Feed(void*, SDL_AudioStream* stream, int additional, int)
{
    static thread_local std::vector<float> mix;
    const int frames = additional / (int)(2 * sizeof(float));
    if (frames <= 0) return;
    mix.resize((size_t)frames * 2);
    ran_platform::DSoundMix(mix.data(), frames);
    SDL_PutAudioStreamData(stream, mix.data(), frames * (int)(2 * sizeof(float)));
}

void OpenOutput()
{
    static bool tried = false;
    if (tried || g_manualOutput) return;
    tried = true;
    if (!SDL_InitSubSystem(SDL_INIT_AUDIO)) {
        std::fprintf(stderr, "[dsound] SDL audio init failed: %s - sound is muted\n", SDL_GetError());
        return;
    }
    SDL_AudioSpec spec = { SDL_AUDIO_F32, 2, ran_platform::kDSoundOutputRate };
    g_stream = SDL_OpenAudioDeviceStream(SDL_AUDIO_DEVICE_DEFAULT_PLAYBACK, &spec, Feed, nullptr);
    if (!g_stream) {
        std::fprintf(stderr, "[dsound] no audio device: %s - sound is muted\n", SDL_GetError());
        return;
    }
    SDL_ResumeAudioStreamDevice(g_stream);
}

} // namespace

namespace ran_platform {

void DSoundMix(float* out, int frames)
{
    std::memset(out, 0, sizeof(float) * 2 * (size_t)frames);
    std::lock_guard<std::mutex> g(g_lock);
    // MixLocked may remove finished buffers from the list: iterate over a copy.
    const std::vector<Buffer*> playing = Playing();
    for (Buffer* b : playing) b->MixLocked(out, frames);
    for (int i = 0; i < frames * 2; ++i) out[i] = std::max(-1.0f, std::min(1.0f, out[i]));
}

void DSoundUseManualOutput() { g_manualOutput = true; }

} // namespace ran_platform

extern "C" HRESULT WINAPI DirectSoundCreate8(LPCGUID, LPDIRECTSOUND8* ppDS8, LPUNKNOWN outer)
{
    if (!ppDS8) return DSERR_INVALIDPARAM;
    *ppDS8 = nullptr;
    if (outer) return DSERR_NOAGGREGATION;
    OpenOutput();
    *ppDS8 = new Device();
    return DS_OK;
}

extern "C" HRESULT WINAPI DirectSoundEnumerateA(LPDSENUMCALLBACKA callback, LPVOID context)
{
    if (!callback) return DSERR_INVALIDPARAM;
    // One device, the system default output (DirectSound lists it with a NULL GUID).
    callback(nullptr, "Primary Sound Driver", "", context);
    return DS_OK;
}
