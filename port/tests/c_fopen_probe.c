/* Compiled as C with -include ran_c_compat.h, like the game's C sources (minizip's ioapi.c):
 * plain fopen must accept a Windows-style path. Used by win32_files_test. */
#include <stdio.h>

int c_fopen_probe(const char* windowsPath, char* out, int size)
{
    FILE* f = fopen(windowsPath, "rb");
    if (!f) return -1;
    const int n = (int)fread(out, 1, (size_t)size, f);
    fclose(f);
    return n;
}
