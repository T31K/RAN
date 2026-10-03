// Include before a legacy DirectX SDK header (ddraw.h, dinput.h, dsound.h, dxfile.h, ...).
// Those headers treat "_WIN32 undefined" as "no COM" and then `#define IUnknown void`, which
// breaks every COM interface after them. DXVK provides COM natively, so _WIN32 is defined
// only while the SDK header is read; win32/legacy_sdk_end.h removes it again.
// No include guard: used once per wrapped header.
#include "ran_compat.h"
#ifndef HMONITOR_DECLARED
#define HMONITOR_DECLARED     // DXVK already defines HMONITOR
#endif
#ifndef _WIN32
#define _WIN32 1
#define RAN_LEGACY_SDK_DEFINED_WIN32
#endif
