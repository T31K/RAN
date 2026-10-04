// Behaviour tests for the native DirectSound (port/platform/dsound_sdl.cpp), driven the way the
// sound engine drives it (dsutil.cpp CSoundManager/CSound, DxBgmSound): create the device and a
// primary buffer, fill secondary buffers through Lock/Unlock, play/loop/duplicate them, 3D
// buffers against the listener. No audio device is opened: the test pulls the mixer directly.
#include "ran_compat.h"
#include <mmsystem.h>
#include <dsound.h>
#include "../platform/dsound_mixer.h"
#include <cmath>
#include <cstdio>
#include <cstring>
#include <vector>

static int g_failed = 0;
#define CHECK(cond) do { if (!(cond)) { std::printf("FAIL %s:%d  %s\n", __FILE__, __LINE__, #cond); ++g_failed; } } while (0)
#define CLOSE(a, b) (std::fabs((a) - (b)) < 2e-3f)

static WAVEFORMATEX Pcm(WORD channels, DWORD rate, WORD bits)
{
    WAVEFORMATEX f = {};
    f.wFormatTag = WAVE_FORMAT_PCM;
    f.nChannels = channels;
    f.nSamplesPerSec = rate;
    f.wBitsPerSample = bits;
    f.nBlockAlign = (WORD)(channels * bits / 8);
    f.nAvgBytesPerSec = rate * f.nBlockAlign;
    return f;
}

// A mono 16-bit buffer of `frames` samples, every sample = value.
static LPDIRECTSOUNDBUFFER MakeConst(LPDIRECTSOUND8 ds, DWORD frames, float value, DWORD rate = 48000, DWORD flags = DSBCAPS_CTRLVOLUME)
{
    WAVEFORMATEX f = Pcm(1, rate, 16);
    DSBUFFERDESC d = {};
    d.dwSize = sizeof(d);
    d.dwFlags = flags;
    d.dwBufferBytes = frames * 2;
    d.lpwfxFormat = &f;
    LPDIRECTSOUNDBUFFER b = nullptr;
    CHECK(ds->CreateSoundBuffer(&d, &b, nullptr) == DS_OK && b);
    if (!b) return nullptr;
    void* p1 = nullptr; DWORD n1 = 0;
    CHECK(b->Lock(0, 0, &p1, &n1, nullptr, nullptr, DSBLOCK_ENTIREBUFFER) == DS_OK && n1 == frames * 2);
    const int16_t s = (int16_t)(value * 32768.0f);
    for (DWORD i = 0; i < frames; ++i) std::memcpy((BYTE*)p1 + i * 2, &s, 2);
    CHECK(b->Unlock(p1, n1, nullptr, 0) == DS_OK);
    return b;
}

static std::vector<float> Mix(int frames)
{
    std::vector<float> out((size_t)frames * 2);
    ran_platform::DSoundMix(out.data(), frames);
    return out;
}

static bool Playing(LPDIRECTSOUNDBUFFER b)
{
    DWORD s = 0;
    b->GetStatus(&s);
    return (s & DSBSTATUS_PLAYING) != 0;
}

static int s_enumCount = 0;
static BOOL CALLBACK EnumCb(LPGUID guid, LPCSTR desc, LPCSTR, LPVOID ctx)
{
    ++s_enumCount;
    CHECK(guid == nullptr && desc && *desc && ctx == (LPVOID)&s_enumCount);
    return TRUE;
}

int main()
{
    ran_platform::DSoundUseManualOutput();

    CHECK(DirectSoundEnumerate((LPDSENUMCALLBACK)EnumCb, &s_enumCount) == DS_OK && s_enumCount == 1);

    LPDIRECTSOUND8 ds = nullptr;
    CHECK(DirectSoundCreate8(nullptr, &ds, nullptr) == DS_OK && ds);
    CHECK(ds->SetCooperativeLevel(nullptr, DSSCL_PRIORITY) == DS_OK);

    // Primary buffer (CSoundManager::SetPrimaryBufferFormat, Get3DListenerInterface).
    DSBUFFERDESC pd = {};
    pd.dwSize = sizeof(pd);
    pd.dwFlags = DSBCAPS_PRIMARYBUFFER | DSBCAPS_CTRL3D;
    LPDIRECTSOUNDBUFFER primary = nullptr;
    CHECK(ds->CreateSoundBuffer(&pd, &primary, nullptr) == DS_OK && primary);
    WAVEFORMATEX pf = Pcm(2, 22050, 16);
    CHECK(primary->SetFormat(&pf) == DS_OK);
    LPDIRECTSOUND3DLISTENER listener = nullptr;
    CHECK(primary->QueryInterface(IID_IDirectSound3DListener, (void**)&listener) == DS_OK && listener);
    LPDIRECTSOUND3DBUFFER no3d = nullptr;
    CHECK(primary->QueryInterface(IID_IDirectSound3DBuffer, (void**)&no3d) == E_NOINTERFACE && !no3d);
    primary->Release();   // the listener stays valid (it belongs to the device)

    // Unsupported formats are refused like DirectSound refuses them.
    {
        WAVEFORMATEX f = Pcm(1, 44100, 24);
        DSBUFFERDESC d = {}; d.dwSize = sizeof(d); d.dwBufferBytes = 3000; d.lpwfxFormat = &f;
        LPDIRECTSOUNDBUFFER b = nullptr;
        CHECK(ds->CreateSoundBuffer(&d, &b, nullptr) == DSERR_BADFORMAT && !b);
    }

    // One-shot playback: plays to the end, then stops and rewinds.
    LPDIRECTSOUNDBUFFER one = MakeConst(ds, 480, 0.5f);
    CHECK(!Playing(one));
    CHECK(Mix(16)[0] == 0.0f);   // silence until Play
    CHECK(one->Play(0, 0, 0) == DS_OK && Playing(one));
    {
        auto o = Mix(100);
        CHECK(CLOSE(o[0], 0.5f) && CLOSE(o[1], 0.5f) && CLOSE(o[199], 0.5f));
        DWORD play = 0, write = 0;
        CHECK(one->GetCurrentPosition(&play, &write) == DS_OK && play == 200 && write > play);
        o = Mix(400);
        CHECK(CLOSE(o[2 * 379], 0.5f) && o[2 * 380] == 0.0f);   // 380 frames were left
        CHECK(!Playing(one));
        CHECK(one->GetCurrentPosition(&play, nullptr) == DS_OK && play == 0);
    }

    // Volume (-20 dB = x0.1) and pan (full right silences the left channel).
    CHECK(one->SetVolume(-2000) == DS_OK);
    LONG v = 0;
    CHECK(one->GetVolume(&v) == DS_OK && v == -2000);
    CHECK(one->SetVolume(DSBVOLUME_MIN - 1) == DSERR_INVALIDPARAM);
    one->Play(0, 0, 0);
    {
        auto o = Mix(10);
        CHECK(CLOSE(o[0], 0.05f) && CLOSE(o[1], 0.05f));
    }
    one->Stop();
    one->SetVolume(DSBVOLUME_MAX);
    LPDIRECTSOUNDBUFFER pan = MakeConst(ds, 480, 0.5f, 48000, DSBCAPS_CTRLPAN);
    CHECK(pan->SetPan(DSBPAN_RIGHT) == DS_OK);
    pan->Play(0, 0, 0);
    {
        auto o = Mix(10);
        CHECK(CLOSE(o[0], 0.0f) && CLOSE(o[1], 0.5f));
    }
    pan->Release();   // releasing a playing buffer takes it out of the mix
    CHECK(Mix(10)[1] == 0.0f);

    // Looping (BGM, ambient sounds): keeps playing, the cursor wraps.
    one->SetCurrentPosition(0);
    CHECK(one->Play(0, 0, DSBPLAY_LOOPING) == DS_OK);
    {
        auto o = Mix(1000);
        CHECK(CLOSE(o[2 * 999], 0.5f));
        DWORD st = 0;
        CHECK(one->GetStatus(&st) == DS_OK && st == (DSBSTATUS_PLAYING | DSBSTATUS_LOOPING));
        DWORD play = 0;
        one->GetCurrentPosition(&play, nullptr);
        CHECK(play == (1000 % 480) * 2);
    }
    one->Stop();
    CHECK(!Playing(one));

    // Frequency: a 24 kHz buffer advances half a frame per 48 kHz output frame.
    {
        LPDIRECTSOUNDBUFFER slow = MakeConst(ds, 480, 0.25f, 24000);
        DWORD f = 0;
        CHECK(slow->GetFrequency(&f) == DS_OK && f == 24000);
        slow->Play(0, 0, 0);
        Mix(100);
        DWORD play = 0;
        slow->GetCurrentPosition(&play, nullptr);
        CHECK(play == 50 * 2);
        CHECK(slow->SetFrequency(48000) == DS_OK);
        Mix(100);
        slow->GetCurrentPosition(&play, nullptr);
        CHECK(play == 150 * 2);
        slow->Release();
    }

    // Lock across the end of the buffer hands out two regions (BGM streaming).
    {
        void *p1 = nullptr, *p2 = nullptr; DWORD n1 = 0, n2 = 0;
        CHECK(one->Lock(900, 200, &p1, &n1, &p2, &n2, 0) == DS_OK);
        CHECK(n1 == 60 && n2 == 140 && p2 != nullptr && (BYTE*)p1 - (BYTE*)p2 == 900);
        one->Unlock(p1, n1, p2, n2);
        CHECK(one->Lock(960, 10, &p1, &n1, &p2, &n2, 0) == DSERR_INVALIDPARAM);
    }

    // Duplicates share the samples and play independently (CSound's concurrent copies).
    {
        LPDIRECTSOUNDBUFFER a = MakeConst(ds, 480, 0.25f);
        LPDIRECTSOUNDBUFFER b = nullptr;
        CHECK(ds->DuplicateSoundBuffer(a, &b) == DS_OK && b);
        a->Play(0, 0, 0);
        b->Play(0, 0, 0);
        CHECK(CLOSE(Mix(10)[0], 0.5f));
        a->Release();   // b keeps the shared samples alive
        CHECK(CLOSE(Mix(10)[0], 0.25f));
        b->Release();
    }

    // 3D buffers: distance rolloff from min distance, side panning, mute at max distance.
    {
        LPDIRECTSOUNDBUFFER b = MakeConst(ds, 4800, 0.5f, 48000, DSBCAPS_CTRL3D | DSBCAPS_CTRLVOLUME | DSBCAPS_MUTE3DATMAXDISTANCE);
        LPDIRECTSOUND3DBUFFER b3 = nullptr;
        CHECK(b->QueryInterface(IID_IDirectSound3DBuffer, (void**)&b3) == DS_OK && b3);
        DS3DBUFFER p = {};
        p.dwSize = sizeof(p);
        CHECK(b3->GetAllParameters(&p) == DS_OK && p.flMinDistance == 1.0f && p.dwMode == DS3DMODE_NORMAL);
        listener->SetPosition(0, 0, 0, DS3D_DEFERRED);
        listener->SetOrientation(0, 0, 1, 0, 1, 0, DS3D_DEFERRED);
        listener->CommitDeferredSettings();
        b3->SetPosition(0, 0, 0.5f, DS3D_IMMEDIATE);   // in front, inside min distance
        b->Play(0, 0, DSBPLAY_LOOPING);
        auto o = Mix(10);
        CHECK(CLOSE(o[0], 0.5f) && CLOSE(o[1], 0.5f));
        b3->SetPosition(10, 0, 0, DS3D_IMMEDIATE);       // 10 units to the right: 1/10, panned right
        o = Mix(10);
        CHECK(CLOSE(o[1], 0.05f) && CLOSE(o[0], 0.015f));
        b3->SetMaxDistance(5.0f, DS3D_IMMEDIATE);        // beyond max distance: muted
        o = Mix(10);
        CHECK(o[0] == 0.0f && o[1] == 0.0f);
        b3->SetMode(DS3DMODE_DISABLE, DS3D_IMMEDIATE);   // 3D off: plain 2D
        o = Mix(10);
        CHECK(CLOSE(o[0], 0.5f));
        b3->Release();
        b->Release();
    }

    listener->Release();
    one->Release();
    CHECK(ds->Release() == 0);

    if (g_failed) { std::printf("%d check(s) failed\n", g_failed); return 1; }
    std::printf("dsound_sdl_test: all checks passed\n");
    return 0;
}
