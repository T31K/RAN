// Wraps LZO's <lzoconf.h> (Dependency/lzo) for the native macOS build. LZO detects the OS from
// WIN32, which the compat layer defines for the game code; LZO would then assume 32-bit Windows
// (4-byte unsigned long) and stop with "this should not happen". LZO is portable, so WIN32 is
// hidden while its headers are read. The compressed format does not depend on the platform.
#pragma once
#pragma push_macro("WIN32")
#undef WIN32
#include_next <lzoconf.h>
#pragma pop_macro("WIN32")
