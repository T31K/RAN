// Software mixer behind the native DirectSound (dsound_sdl.cpp). The SDL3 audio callback pulls
// from DSoundMix; tests call it directly after DSoundUseManualOutput so no device is opened.
#pragma once

namespace ran_platform {

// Output format of the mixer: interleaved stereo float at this rate.
constexpr int kDSoundOutputRate = 48000;

// Mix `frames` stereo frames of every playing buffer into `out` (overwrites, 2 floats/frame).
void DSoundMix(float* out, int frames);

// Tests: never open an SDL audio device; the caller drives DSoundMix.
void DSoundUseManualOutput();

} // namespace ran_platform
