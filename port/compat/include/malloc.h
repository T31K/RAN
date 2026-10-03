// Stand-in for the Windows SDK header of the same name (native macOS build).
#pragma once
#include "ran_compat.h"
#include <alloca.h>
#include <cstdlib>
#define _alloca alloca
#define _malloca alloca
#define _freea(p) ((void)0)
