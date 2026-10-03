// Native implementations of the few functions the client calls from Windows-only units that
// are excluded from the native build (port/native-excludes.txt): DXUT, the DirectX version
// probe, DxErr and BugTrap.
#include "ran_compat.h"
#include "mfc/afx_all.h"
#include <d3d9.h>
#include <d3dx9.h>
#include <dxerr9.h>
#include "../../Dependency/common/ijl.h"
#include <cstdio>
#include <string>
// The game spells these D3DMATERIALQ / D3DLIGHTQ (macros in dxstdafx.h for the D3D9 types).
typedef D3DMATERIAL9 D3DMATERIALQ;
typedef D3DLIGHT9 D3DLIGHTQ;

// DXUT keeps its own device/window state, filled only by DXUTInit/DXUTCreateDevice, which the
// game never calls (it runs CD3DApplication). On Windows these therefore return NULL as well.
IDirect3DDevice9* DXUTGetD3DDevice() { return nullptr; }
HWND DXUTGetHWNDDeviceFullScreen() { return nullptr; }

// DXUTmisc.cpp helpers (same code as the DirectX SDK sample framework).
VOID D3DUtil_InitMaterial(D3DMATERIALQ& mtrl, FLOAT r, FLOAT g, FLOAT b, FLOAT a)
{
    ZeroMemory(&mtrl, sizeof(D3DMATERIALQ));
    mtrl.Diffuse.r = mtrl.Ambient.r = r;
    mtrl.Diffuse.g = mtrl.Ambient.g = g;
    mtrl.Diffuse.b = mtrl.Ambient.b = b;
    mtrl.Diffuse.a = mtrl.Ambient.a = a;
}

VOID D3DUtil_InitLight(D3DLIGHTQ& light, D3DLIGHTTYPE ltType, FLOAT x, FLOAT y, FLOAT z)
{
    ZeroMemory(&light, sizeof(D3DLIGHTQ));
    light.Type = ltType;
    light.Diffuse.r = 1.0f;
    light.Diffuse.g = 1.0f;
    light.Diffuse.b = 1.0f;
    D3DXVECTOR3 dir(x, y, z);
    D3DXVec3Normalize((D3DXVECTOR3*)&light.Direction, &dir);
    light.Position.x = x;
    light.Position.y = y;
    light.Position.z = z;
    light.Range = 1000.0f;
}

D3DXMATRIX D3DUtil_GetCubeMapViewMatrix(DWORD dwFace)
{
    D3DXVECTOR3 vEyePt(0.0f, 0.0f, 0.0f), vLookDir(0.0f, 0.0f, 1.0f), vUpDir(0.0f, 1.0f, 0.0f);
    switch (dwFace) {
    case D3DCUBEMAP_FACE_POSITIVE_X: vLookDir = D3DXVECTOR3( 1, 0, 0); vUpDir = D3DXVECTOR3(0, 1, 0); break;
    case D3DCUBEMAP_FACE_NEGATIVE_X: vLookDir = D3DXVECTOR3(-1, 0, 0); vUpDir = D3DXVECTOR3(0, 1, 0); break;
    case D3DCUBEMAP_FACE_POSITIVE_Y: vLookDir = D3DXVECTOR3( 0, 1, 0); vUpDir = D3DXVECTOR3(0, 0,-1); break;
    case D3DCUBEMAP_FACE_NEGATIVE_Y: vLookDir = D3DXVECTOR3( 0,-1, 0); vUpDir = D3DXVECTOR3(0, 0, 1); break;
    case D3DCUBEMAP_FACE_POSITIVE_Z: vLookDir = D3DXVECTOR3( 0, 0, 1); vUpDir = D3DXVECTOR3(0, 1, 0); break;
    case D3DCUBEMAP_FACE_NEGATIVE_Z: vLookDir = D3DXVECTOR3( 0, 0,-1); vUpDir = D3DXVECTOR3(0, 1, 0); break;
    }
    D3DXMATRIX matView;
    D3DXMatrixLookAtLH(&matView, &vEyePt, &vLookDir, &vUpDir);
    return matView;
}

// getdxver.cpp: the native renderer is D3D9 (DXVK), i.e. what the game calls "DirectX 9.0c".
HRESULT getdxversion(DWORD* pdwDirectXVersion, TCHAR* strDirectXVersion, int cchDirectXVersion)
{
    if (pdwDirectXVersion) *pdwDirectXVersion = 0x00090003;
    if (strDirectXVersion && cchDirectXVersion > 0) std::snprintf(strDirectXVersion, (size_t)cchDirectXVersion, "9.0c");
    return S_OK;
}

// dxerr9: only used for log lines.
const char* WINAPI DXGetErrorString9A(HRESULT hr)
{
    static thread_local char buf[32];
    std::snprintf(buf, sizeof(buf), "HRESULT 0x%08X", (unsigned)hr);
    return buf;
}

// Intel JPEG Library (screenshots in DxGrapUtils): not available natively; the screenshot code
// takes its existing error path. Phase 3 writes screenshots with stb_image_write instead.
IJLERR IJL_STDCALL ijlInit(JPEG_CORE_PROPERTIES*) { return IJL_EXCEPTION_DETECTED; }
IJLERR IJL_STDCALL ijlFree(JPEG_CORE_PROPERTIES*) { return IJL_OK; }

// BugTrap crash reporting: replaced by a native crash handler in plan Phase 4.
namespace BUG_TRAP {
void BugTrapInstall(const std::string&, bool, bool) {}
}
