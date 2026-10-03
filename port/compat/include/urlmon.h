// Stand-in for <urlmon.h> (native macOS build). URLDownloadToFile is only used by the old
// in-game HTTP patcher; the native app updates through the launcher's OTA, so it reports failure.
#pragma once
#include "ran_compat.h"

inline HRESULT URLDownloadToFileA(IUnknown*, const char*, const char*, DWORD, void*) { return E_FAIL; }
#define URLDownloadToFile URLDownloadToFileA
