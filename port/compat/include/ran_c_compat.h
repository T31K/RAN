/* Force-included into the game's C sources in the native build (build_native.sh, -include).
 * C code cannot use the C++ compat layer, but it opens files with Windows paths too (minizip's
 * ioapi.c opens "...\\data\\glogic\\glogic.rcc"): fopen goes through the same path resolver
 * as the C++ side (ran_compat::ResolvePath; implemented in port/platform/c_bridge.cpp). */
#ifndef RAN_C_COMPAT_H
#define RAN_C_COMPAT_H
#include <stdio.h>
#ifdef __cplusplus
extern "C" {
#endif
FILE* ran_fopen(const char* path, const char* mode);
#ifdef __cplusplus
}
#endif
#ifndef __cplusplus
#define fopen ran_fopen
#endif
#endif
