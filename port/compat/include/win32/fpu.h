// MSVC floating-point control (_controlfp_s) and SSE aligned allocation for the native macOS
// build. ARM64 has no x87: the game's MSVC control word is kept bit-for-bit per thread (so its
// per-frame "float accuracy" check sees FLOAT_SET), and the rounding bits are applied with
// fesetround. Precision and infinity control need nothing: ARM float math is already IEEE
// single precision.
#pragma once
#include <cfenv>
#include <cstdlib>

#define _MCW_EM       0x0008001Fu
#define _EM_INEXACT   0x00000001u
#define _EM_UNDERFLOW 0x00000002u
#define _EM_OVERFLOW  0x00000004u
#define _EM_ZERODIVIDE 0x00000008u
#define _EM_INVALID   0x00000010u
#define _EM_DENORMAL  0x00080000u
#define _MCW_RC       0x00000300u
#define _RC_NEAR      0x00000000u
#define _RC_DOWN      0x00000100u
#define _RC_UP        0x00000200u
#define _RC_CHOP      0x00000300u
#define _MCW_PC       0x00030000u
#define _PC_64        0x00000000u
#define _PC_53        0x00010000u
#define _PC_24        0x00020000u
#define _MCW_IC       0x00040000u
#define _IC_PROJECTIVE 0x00000000u
#define _IC_AFFINE    0x00040000u
#define _CW_DEFAULT   (_RC_NEAR | _PC_53 | _EM_INVALID | _EM_ZERODIVIDE | _EM_OVERFLOW | _EM_UNDERFLOW | _EM_INEXACT | _EM_DENORMAL)

namespace ran_compat {
    inline unsigned int& ControlWord() { thread_local unsigned int cw = _CW_DEFAULT; return cw; }
}

inline int _controlfp_s(unsigned int* current, unsigned int newValue, unsigned int mask)
{
    unsigned int& cw = ran_compat::ControlWord();
    if (mask) {
        cw = (cw & ~mask) | (newValue & mask);
        if (mask & _MCW_RC) {
            switch (cw & _MCW_RC) {
            case _RC_NEAR: std::fesetround(FE_TONEAREST); break;
            case _RC_DOWN: std::fesetround(FE_DOWNWARD); break;
            case _RC_UP:   std::fesetround(FE_UPWARD); break;
            default:       std::fesetround(FE_TOWARDZERO); break;
            }
        }
    }
    if (current) *current = cw;
    return 0;
}

inline unsigned int _controlfp(unsigned int newValue, unsigned int mask)
{
    unsigned int cw = 0;
    _controlfp_s(&cw, newValue, mask);
    return cw;
}

inline void* _mm_malloc(size_t size, size_t align)
{
    void* p = nullptr;
    if (align < sizeof(void*)) align = sizeof(void*);
    return ::posix_memalign(&p, align, size) == 0 ? p : nullptr;
}
inline void _mm_free(void* p) { std::free(p); }
inline void* _aligned_malloc(size_t size, size_t align) { return _mm_malloc(size, align); }
inline void _aligned_free(void* p) { std::free(p); }
