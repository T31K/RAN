// Stand-in for the Windows SDK header of the same name (native macOS build).
#pragma once
#include "ran_compat.h"
#include <cassert>
#define _ASSERT(e) assert(e)
#define _ASSERTE(e) assert(e)
#define _CrtSetDbgFlag(f) 0
#define _CrtDumpMemoryLeaks() 0
#define _CRTDBG_ALLOC_MEM_DF 0
#define _CRTDBG_LEAK_CHECK_DF 0
