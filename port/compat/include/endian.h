// Stand-in for glibc <endian.h> (native macOS build; Apple Silicon is little-endian).
#pragma once
#include <machine/endian.h>
#ifndef __LITTLE_ENDIAN
#define __LITTLE_ENDIAN 1234
#endif
#ifndef __BIG_ENDIAN
#define __BIG_ENDIAN 4321
#endif
#ifndef __BYTE_ORDER
#define __BYTE_ORDER __LITTLE_ENDIAN
#endif
