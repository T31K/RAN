// Browser build: native-only services.
//  - Telemetry (crash/F12/error reports): the native client posts them with libcurl from a
//    background thread; in the page, errors show in the console. Kept as no-ops for now.
//  - Updater: the page itself is the update (a reload loads the new build).
#include "ran_telemetry.h"
#include "../../platform/updater.h"

namespace ran_telemetry {
void Init() {}
void SetStateHook(GameState (*)()) {}
void SetNotifyHook(void (*)(const char*)) {}
void SetAccount(const char*) {}
void OnLogLine(const char*, const char*) {}
void OnPresent(void*) {}
void RequestHotkeyReport() {}
void OnFatalSignal(int) {}
} // namespace ran_telemetry

namespace ran_platform {
void StartUpdater() {}
} // namespace ran_platform
