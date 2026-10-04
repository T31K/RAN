// Uses the Microsoft DirectX SDK <d3dx9math.h> the game was written against (Dependency/directx),
// on top of DXVK's native d3d9.h. Both d3d9.h files share the _D3D9_H_ guard, so the SDK's own
// #include "d3d9.h" becomes a no-op once DXVK's is in.
#pragma once
#include <d3d9.h>
#include "win32/legacy_sdk_begin.h"
#include "../../../Dependency/directx/d3dx9math.h"
#include "win32/legacy_sdk_end.h"

// The SDK aligns D3DXMATRIXA16 to 16 bytes only under MSVC (`__declspec(align(16))` behind
// _MSC_VER), so natively it was a plain 4-aligned matrix. Structs that hold one are 16-aligned
// on Windows - e.g. the animation key SMatrixKey is 80 bytes there (68 otherwise) and is read
// from map files by sizeof - so the native build uses the Windows alignment too.
#undef D3DX_ALIGN16
#define D3DX_ALIGN16 __attribute__((aligned(16)))
typedef _D3DXMATRIXA16 ran_D3DXMATRIXA16 __attribute__((aligned(16)));
#define D3DXMATRIXA16 ran_D3DXMATRIXA16
#define LPD3DXMATRIXA16 ran_D3DXMATRIXA16*
static_assert(alignof(D3DXMATRIXA16) == 16, "D3DXMATRIXA16 must be 16-byte aligned like MSVC's");
