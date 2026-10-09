// IDirect3D9 for the browser build: one adapter (the page's WebGL2), capabilities of a
// fixed-function-only card, display modes, device creation (see d3d9_web.h).
#include "d3d9_web.h"
#include <emscripten.h>
#include <emscripten/html5.h>
#include <cstdio>

namespace d3d9web {

std::vector<D3DDISPLAYMODE> Direct3D::Modes()
{
    static const UINT sizes[][2] = { { 640, 480 }, { 800, 600 }, { 1024, 768 }, { 1152, 864 }, { 1280, 720 }, { 1280, 768 },
                                     { 1280, 800 }, { 1280, 960 }, { 1280, 1024 }, { 1360, 768 }, { 1366, 768 }, { 1440, 900 },
                                     { 1600, 900 }, { 1600, 1200 }, { 1680, 1050 }, { 1920, 1080 }, { 1920, 1200 },
                                     { 2560, 1440 } };
    D3DDISPLAYMODE screen;
    GetAdapterDisplayMode(0, &screen);
    std::vector<D3DDISPLAYMODE> out;
    for (const auto& s : sizes)
        if (s[0] <= std::max(screen.Width, 1024u) && s[1] <= std::max(screen.Height, 768u))
            out.push_back({ s[0], s[1], 60, D3DFMT_X8R8G8B8 });
    return out;
}

HRESULT Direct3D::GetAdapterIdentifier(UINT, DWORD, D3DADAPTER_IDENTIFIER9* id)
{
    if (!id) return D3DERR_INVALIDCALL;
    std::memset(id, 0, sizeof(*id));
    std::snprintf(id->Driver, sizeof(id->Driver), "webgl2");
    std::snprintf(id->Description, sizeof(id->Description), "WebGL 2 (RAN browser D3D9)");
    std::snprintf(id->DeviceName, sizeof(id->DeviceName), "\\\\.\\DISPLAY1");
    id->VendorId = 0x10DE;
    id->DeviceId = 0x0001;
    id->WHQLLevel = 1;
    return D3D_OK;
}

UINT Direct3D::GetAdapterModeCount(UINT, D3DFORMAT fmt)
{
    return fmt == D3DFMT_X8R8G8B8 ? (UINT)Modes().size() : 0;
}

HRESULT Direct3D::EnumAdapterModes(UINT, D3DFORMAT fmt, UINT mode, D3DDISPLAYMODE* out)
{
    if (!out || fmt != D3DFMT_X8R8G8B8) return D3DERR_INVALIDCALL;
    const auto modes = Modes();
    if (mode >= modes.size()) return D3DERR_INVALIDCALL;
    *out = modes[mode];
    return D3D_OK;
}

HRESULT Direct3D::GetAdapterDisplayMode(UINT, D3DDISPLAYMODE* mode)
{
    if (!mode) return D3DERR_INVALIDCALL;
    mode->Width = (UINT)EM_ASM_INT({ return Math.floor(screen.width || window.innerWidth); });
    mode->Height = (UINT)EM_ASM_INT({ return Math.floor(screen.height || window.innerHeight); });
    mode->RefreshRate = 60;
    mode->Format = D3DFMT_X8R8G8B8;
    return D3D_OK;
}

HRESULT Direct3D::CheckDeviceType(UINT, D3DDEVTYPE type, D3DFORMAT display, D3DFORMAT backBuffer, WINBOOL)
{
    if (type != D3DDEVTYPE_HAL) return D3DERR_NOTAVAILABLE;
    if (display != D3DFMT_X8R8G8B8 && display != D3DFMT_UNKNOWN) return D3DERR_NOTAVAILABLE;
    if (backBuffer != D3DFMT_X8R8G8B8 && backBuffer != D3DFMT_A8R8G8B8 && backBuffer != D3DFMT_UNKNOWN) return D3DERR_NOTAVAILABLE;
    return D3D_OK;
}

HRESULT Direct3D::CheckDeviceFormat(UINT, D3DDEVTYPE, D3DFORMAT, DWORD usage, D3DRESOURCETYPE type, D3DFORMAT fmt)
{
    const FormatInfo* fi = GetFormatInfo(fmt);
    if (!fi || fmt == D3DFMT_INDEX16 || fmt == D3DFMT_INDEX32) return D3DERR_NOTAVAILABLE;
    if (usage & D3DUSAGE_DEPTHSTENCIL) return fi->depth && type == D3DRTYPE_SURFACE ? D3D_OK : D3DERR_NOTAVAILABLE;
    if (fi->depth) return D3DERR_NOTAVAILABLE;
    if ((usage & D3DUSAGE_RENDERTARGET) && !fi->renderable) return D3DERR_NOTAVAILABLE;
    if (type == D3DRTYPE_VOLUMETEXTURE) return D3DERR_NOTAVAILABLE;
    if (usage & D3DUSAGE_AUTOGENMIPMAP) return fi->compressed ? D3DOK_NOAUTOGEN : D3D_OK;
    return D3D_OK;
}

HRESULT Direct3D::CheckDeviceMultiSampleType(UINT, D3DDEVTYPE, D3DFORMAT, WINBOOL, D3DMULTISAMPLE_TYPE t, DWORD* q)
{
    if (q) *q = 1;
    return t == D3DMULTISAMPLE_NONE ? D3D_OK : D3DERR_NOTAVAILABLE;
}

HRESULT Direct3D::CheckDepthStencilMatch(UINT, D3DDEVTYPE, D3DFORMAT, D3DFORMAT, D3DFORMAT ds)
{
    const FormatInfo* fi = GetFormatInfo(ds);
    return fi && fi->depth ? D3D_OK : D3DERR_NOTAVAILABLE;
}

void Direct3D::FillCaps(D3DCAPS9* c)
{
    std::memset(c, 0, sizeof(*c));
    c->DeviceType = D3DDEVTYPE_HAL;
    c->AdapterOrdinal = 0;
    c->Caps2 = D3DCAPS2_DYNAMICTEXTURES | D3DCAPS2_CANAUTOGENMIPMAP;
    c->Caps3 = D3DCAPS3_ALPHA_FULLSCREEN_FLIP_OR_DISCARD;
    c->PresentationIntervals = D3DPRESENT_INTERVAL_IMMEDIATE | D3DPRESENT_INTERVAL_ONE;
    c->DevCaps = D3DDEVCAPS_EXECUTESYSTEMMEMORY | D3DDEVCAPS_EXECUTEVIDEOMEMORY | D3DDEVCAPS_TLVERTEXSYSTEMMEMORY |
                 D3DDEVCAPS_TLVERTEXVIDEOMEMORY | D3DDEVCAPS_TEXTURESYSTEMMEMORY | D3DDEVCAPS_TEXTUREVIDEOMEMORY |
                 D3DDEVCAPS_DRAWPRIMTLVERTEX | D3DDEVCAPS_CANRENDERAFTERFLIP | D3DDEVCAPS_TEXTURENONLOCALVIDMEM |
                 D3DDEVCAPS_DRAWPRIMITIVES2 | D3DDEVCAPS_DRAWPRIMITIVES2EX | D3DDEVCAPS_HWTRANSFORMANDLIGHT |
                 D3DDEVCAPS_CANBLTSYSTONONLOCAL | D3DDEVCAPS_HWRASTERIZATION;
    c->PrimitiveMiscCaps = D3DPMISCCAPS_MASKZ | D3DPMISCCAPS_CULLNONE | D3DPMISCCAPS_CULLCW | D3DPMISCCAPS_CULLCCW |
                           D3DPMISCCAPS_COLORWRITEENABLE | D3DPMISCCAPS_CLIPTLVERTS | D3DPMISCCAPS_TSSARGTEMP |
                           D3DPMISCCAPS_BLENDOP | D3DPMISCCAPS_SEPARATEALPHABLEND;
    c->RasterCaps = D3DPRASTERCAPS_DITHER | D3DPRASTERCAPS_ZTEST | D3DPRASTERCAPS_FOGVERTEX | D3DPRASTERCAPS_FOGTABLE |
                    D3DPRASTERCAPS_MIPMAPLODBIAS | D3DPRASTERCAPS_ZFOG | D3DPRASTERCAPS_WFOG | D3DPRASTERCAPS_FOGRANGE |
                    D3DPRASTERCAPS_ANISOTROPY | D3DPRASTERCAPS_SCISSORTEST | D3DPRASTERCAPS_DEPTHBIAS |
                    D3DPRASTERCAPS_SLOPESCALEDEPTHBIAS;
    c->ZCmpCaps = c->AlphaCmpCaps = 0xFF;
    c->SrcBlendCaps = c->DestBlendCaps = 0x1FFF;
    c->ShadeCaps = D3DPSHADECAPS_COLORGOURAUDRGB | D3DPSHADECAPS_SPECULARGOURAUDRGB | D3DPSHADECAPS_ALPHAGOURAUDBLEND |
                   D3DPSHADECAPS_FOGGOURAUD;
    c->TextureCaps = D3DPTEXTURECAPS_PERSPECTIVE | D3DPTEXTURECAPS_ALPHA | D3DPTEXTURECAPS_PROJECTED |
                     D3DPTEXTURECAPS_CUBEMAP | D3DPTEXTURECAPS_MIPMAP | D3DPTEXTURECAPS_MIPCUBEMAP;
    c->TextureFilterCaps = c->CubeTextureFilterCaps = D3DPTFILTERCAPS_MINFPOINT | D3DPTFILTERCAPS_MINFLINEAR |
                                                      D3DPTFILTERCAPS_MINFANISOTROPIC | D3DPTFILTERCAPS_MIPFPOINT |
                                                      D3DPTFILTERCAPS_MIPFLINEAR | D3DPTFILTERCAPS_MAGFPOINT |
                                                      D3DPTFILTERCAPS_MAGFLINEAR;
    c->TextureAddressCaps = D3DPTADDRESSCAPS_WRAP | D3DPTADDRESSCAPS_MIRROR | D3DPTADDRESSCAPS_CLAMP |
                            D3DPTADDRESSCAPS_BORDER | D3DPTADDRESSCAPS_INDEPENDENTUV | D3DPTADDRESSCAPS_MIRRORONCE;
    c->LineCaps = D3DLINECAPS_TEXTURE | D3DLINECAPS_ZTEST | D3DLINECAPS_BLEND | D3DLINECAPS_ALPHACMP | D3DLINECAPS_FOG;
    c->MaxTextureWidth = c->MaxTextureHeight = 4096;
    c->MaxTextureRepeat = 8192;
    c->MaxTextureAspectRatio = 4096;
    c->MaxAnisotropy = 16;
    c->MaxVertexW = 1e10f;
    c->GuardBandLeft = c->GuardBandTop = -32768.0f;
    c->GuardBandRight = c->GuardBandBottom = 32768.0f;
    c->StencilCaps = 0xFF;
    c->FVFCaps = 8 | D3DFVFCAPS_PSIZE;
    c->TextureOpCaps = 0x03FFFFFF;
    c->MaxTextureBlendStages = c->MaxSimultaneousTextures = kStages;
    c->VertexProcessingCaps = D3DVTXPCAPS_TEXGEN | D3DVTXPCAPS_MATERIALSOURCE7 | D3DVTXPCAPS_DIRECTIONALLIGHTS |
                              D3DVTXPCAPS_POSITIONALLIGHTS | D3DVTXPCAPS_LOCALVIEWER | D3DVTXPCAPS_TEXGEN_SPHEREMAP;
    c->MaxActiveLights = kMaxLights;
    c->MaxUserClipPlanes = 1;
    c->MaxVertexBlendMatrices = 4;
    c->MaxVertexBlendMatrixIndex = 0;
    c->MaxPointSize = 256.0f;
    c->MaxPrimitiveCount = 0xFFFFF;
    c->MaxVertexIndex = 0xFFFFFF;
    c->MaxStreams = 1;
    c->MaxStreamStride = 508;
    // No programmable shaders: the engine takes its fixed-function paths.
    c->VertexShaderVersion = D3DVS_VERSION(0, 0);
    c->PixelShaderVersion = D3DPS_VERSION(0, 0);
    c->DevCaps2 = D3DDEVCAPS2_STREAMOFFSET;
    c->NumSimultaneousRTs = 1;
    c->StretchRectFilterCaps = D3DPTFILTERCAPS_MINFPOINT | D3DPTFILTERCAPS_MINFLINEAR | D3DPTFILTERCAPS_MAGFPOINT |
                               D3DPTFILTERCAPS_MAGFLINEAR;
    c->DeclTypes = D3DDTCAPS_UBYTE4 | D3DDTCAPS_UBYTE4N | D3DDTCAPS_SHORT2N | D3DDTCAPS_SHORT4N |
                   D3DDTCAPS_USHORT2N | D3DDTCAPS_USHORT4N;
    c->NumberOfAdaptersInGroup = 1;
}

HRESULT Direct3D::GetDeviceCaps(UINT, D3DDEVTYPE type, D3DCAPS9* caps)
{
    if (!caps) return D3DERR_INVALIDCALL;
    if (type != D3DDEVTYPE_HAL) return D3DERR_NOTAVAILABLE;
    FillCaps(caps);
    return D3D_OK;
}

HRESULT Direct3D::CreateDevice(UINT, D3DDEVTYPE type, HWND focus, DWORD behavior, D3DPRESENT_PARAMETERS* pp, IDirect3DDevice9** out)
{
    if (!out || !pp) return D3DERR_INVALIDCALL;
    *out = nullptr;
    if (type != D3DDEVTYPE_HAL) return D3DERR_NOTAVAILABLE;
    if (m_device) {   // one device per page: hand back the existing one, reset to the new parameters
        m_device->Reset(pp);
        m_device->AddRef();
        *out = m_device;
        return D3D_OK;
    }
    EmscriptenWebGLContextAttributes attrs;
    emscripten_webgl_init_context_attributes(&attrs);
    attrs.majorVersion = 2;
    attrs.minorVersion = 0;
    attrs.alpha = false;
    attrs.depth = false;
    attrs.stencil = false;
    attrs.antialias = false;
    attrs.premultipliedAlpha = false;
    attrs.preserveDrawingBuffer = false;
    attrs.powerPreference = EM_WEBGL_POWER_PREFERENCE_HIGH_PERFORMANCE;
    const EMSCRIPTEN_WEBGL_CONTEXT_HANDLE ctx = emscripten_webgl_create_context("#canvas", &attrs);
    if (ctx <= 0) {
        std::printf("[d3d9] WebGL 2 is not available (%d)\n", (int)ctx);
        return D3DERR_NOTAVAILABLE;
    }
    emscripten_webgl_make_context_current(ctx);
    m_device = new Device(this, focus, behavior, pp, (int)ctx);
    pp->BackBufferWidth = m_device->m_pp.BackBufferWidth;
    pp->BackBufferHeight = m_device->m_pp.BackBufferHeight;
    pp->BackBufferFormat = m_device->m_pp.BackBufferFormat;
    *out = m_device;
    return D3D_OK;
}

} // namespace d3d9web

extern "C" IDirect3D9* WINAPI Direct3DCreate9(UINT)
{
    static d3d9web::Direct3D* d3d = new d3d9web::Direct3D();
    d3d->AddRef();
    return d3d;
}
