// Browser build: the game's CLOCKCHECK (DebugSet.h) uses mach_absolute_time() on non-x86.
#pragma once
#include <time.h>

static inline unsigned long long mach_absolute_time(void)
{
    struct timespec ts;
    clock_gettime(CLOCK_MONOTONIC, &ts);
    return (unsigned long long)ts.tv_sec * 1000000000ull + (unsigned long long)ts.tv_nsec;
}
