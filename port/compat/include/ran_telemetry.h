// Error reports from the native client (port/platform/telemetry.cpp): crashes, F12 bug reports
// and logged game errors go to the shared backend (api.kaleidoscopical.com/ran/reports) with the
// account, Mac, macOS, app version, a screenshot and the game log, so problems on players' Macs
// can be read remotely. RAN_TELEMETRY=0 turns everything off.
#pragma once

namespace ran_telemetry {

// Current character and map, asked for on the game thread when a report is built.
struct GameState {
    const char* character;   // "" before a character is in the world
    int mapMain, mapSub;     // -1 when no map is loaded
};

void Init();                                        // platform: after the game folder is set
void SetStateHook(GameState (*hook)());             // game: where the player is
void SetNotifyHook(void (*hook)(const char* text)); // game: show a line in the chat box
void SetAccount(const char* account);               // game: the login ID, on login
void OnLogLine(const char* logFile, const char* line); // game: each CDebugSet::ToLogFile line
void OnPresent(void* d3d9Device);                   // game: each Present (screenshots, notices)
void RequestHotkeyReport();                         // platform: F12
void OnFatalSignal(int sig);                        // platform: crash handler (signal-safe)

} // namespace ran_telemetry
