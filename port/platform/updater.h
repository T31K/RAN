// Automatic updates (Sparkle) for the native app - see updater.cpp.
#pragma once

namespace ran_platform {

// Starts Sparkle's standard updater when the app bundle carries Sparkle.framework (release
// builds). Call after SDL_Init(VIDEO), which creates the NSApplication. RAN_NO_UPDATES=1 skips it.
void StartUpdater();

} // namespace ran_platform
