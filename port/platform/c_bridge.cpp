// C entry points of the compat layer, for the game's C sources (see ran_c_compat.h).
#include "ran_compat.h"
#include "ran_c_compat.h"

extern "C" FILE* ran_fopen(const char* path, const char* mode)
{
    return ran_compat::fopen_resolved(path, mode);
}
