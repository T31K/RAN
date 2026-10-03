// Uses the Microsoft DirectX SDK <d3dx9.h> the game was written against (Dependency/directx),
// on top of DXVK's native d3d9.h. Both d3d9.h files share the _D3D9_H_ guard, so the SDK's own
// #include "d3d9.h" becomes a no-op once DXVK's is in.
#pragma once
#include <d3d9.h>
#include "win32/legacy_sdk_begin.h"
#include "../../../Dependency/directx/d3dx9.h"
#include "win32/legacy_sdk_end.h"
