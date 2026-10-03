// Behaviour tests for the MSVC _controlfp_s emulation (x87 control word on ARM64).
#include "ran_compat.h"
#include <cfenv>
#include <cstdio>

static int g_failed = 0;
#define CHECK(cond) do { if (!(cond)) { std::printf("FAIL %s:%d  %s\n", __FILE__, __LINE__, #cond); ++g_failed; } } while (0)

int main()
{
    unsigned int cw = 0;

    // Default word matches MSVC's x86 _CW_DEFAULT, and round-to-nearest is in effect.
    CHECK(_controlfp_s(&cw, 0, 0) == 0 && cw == _CW_DEFAULT);
    CHECK(std::fegetround() == FE_TONEAREST);

    // The game's SetControlfp(true) sequence yields exactly its FLOAT_SET (0xe021f).
    _controlfp_s(&cw, _PC_24, _MCW_PC);
    _controlfp_s(&cw, _RC_UP, _MCW_RC);
    _controlfp_s(&cw, _IC_AFFINE, _MCW_IC);
    unsigned int now = 0;
    _controlfp_s(&now, 0, 0);
    CHECK(now == 0xe021fu);
    CHECK(std::fegetround() == FE_UPWARD);

    // The rounding mode really reaches the FPU: an inexact quotient differs between up and down.
    volatile float one = 1.0f, three = 3.0f;
    const float up = one / three;
    _controlfp_s(&cw, _RC_DOWN, _MCW_RC);
    const float down = one / three;
    CHECK(up > down);

    // Other rounding modes map through; reset restores the default.
    _controlfp_s(&cw, _RC_CHOP, _MCW_RC);
    CHECK(std::fegetround() == FE_TOWARDZERO);
    _controlfp_s(&cw, _RC_DOWN, _MCW_RC);
    CHECK(std::fegetround() == FE_DOWNWARD);
    _controlfp_s(&cw, _CW_DEFAULT, 0xfffff);
    _controlfp_s(&now, 0, 0);
    CHECK(now == _CW_DEFAULT);
    CHECK(std::fegetround() == FE_TONEAREST);

    // Aligned allocation for the SSE-era _mm_malloc users.
    void* p = _mm_malloc(100, 16);
    CHECK(p != nullptr && ((uintptr_t)p % 16) == 0);
    _mm_free(p);

    if (g_failed == 0) std::printf("PASS fpu_test\n");
    return g_failed == 0 ? 0 : 1;
}
