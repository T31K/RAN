// See updater.h. Sparkle reads its settings from Info.plist (written by
// port/scripts/package_native_app.sh): the appcast URL (SUFeedURL, a GitHub release), the EdDSA
// public key that update archives must be signed with (SUPublicEDKey), and silent mode
// (SUAutomaticallyUpdate): updates download in the background and install when the game quits.
//
// Sparkle is Objective-C; it is driven through the runtime so this file needs no Objective-C
// (and none of its BOOL, which the game's Windows types also define).
#include "updater.h"
#include <cstdio>
#include <cstdlib>
#include <dlfcn.h>

extern "C" void* objc_getClass(const char*);
extern "C" void* sel_registerName(const char*);
extern "C" void objc_msgSend(void);

namespace ran_platform {

void StartUpdater()
{
    if (std::getenv("RAN_NO_UPDATES")) return;
    // Release bundles carry Sparkle.framework next to the other libraries; dev builds do not.
    if (!dlopen("@executable_path/../Frameworks/Sparkle.framework/Sparkle", RTLD_NOW)) return;
    void* cls = objc_getClass("SPUStandardUpdaterController");
    if (!cls) return;
    auto send = (void* (*)(void*, void*))objc_msgSend;
    auto init = (void* (*)(void*, void*, bool, void*, void*))objc_msgSend;
    void* controller = send(cls, sel_registerName("alloc"));
    controller = init(controller, sel_registerName("initWithStartingUpdater:updaterDelegate:userDriverDelegate:"), true,
                      nullptr, nullptr);
    static void* s_keep = controller;   // lives for the whole run
    (void)s_keep;
    std::fprintf(stderr, "[platform] automatic updates: %s\n", controller ? "on" : "could not start");
}

} // namespace ran_platform
