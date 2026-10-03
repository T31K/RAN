// Wraps LZO's example <portab.h> (Dependency/common, pulls in miniacc.h) for the native macOS
// build, for the same reason as lzoconf.h here: miniacc detects the OS from WIN32 and would
// assume 32-bit Windows. Hidden while the header is read; MinLzo.cpp gets the POSIX path.
#pragma once
#pragma push_macro("WIN32")
#undef WIN32
#include_next "portab.h"
#pragma pop_macro("WIN32")
