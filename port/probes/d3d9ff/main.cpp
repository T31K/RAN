// Draws one frame using the fixed-function D3D9 features RAN relies on most
// (pretransformed + lit geometry, DXT1 texture, texture-stage modulate, alpha
// blending applied via a state block), reads the back buffer back and writes
// out.ppm. Built for Windows (reference, run under Wine) and for DXVK-native.
#include <SDL3/SDL.h>
#ifdef _WIN32
#include <windows.h>
#endif
#include <d3d9.h>
#include <cstdint>
#include <cstdio>

static const int W = 512, H = 512;

struct VtxRHW    { float x, y, z, rhw; DWORD color; };
struct VtxRHWTex { float x, y, z, rhw; DWORD color; float u, v; };
struct VtxLit    { float x, y, z, nx, ny, nz; };
static const DWORD FVF_RHW    = D3DFVF_XYZRHW | D3DFVF_DIFFUSE;
static const DWORD FVF_RHWTEX = D3DFVF_XYZRHW | D3DFVF_DIFFUSE | D3DFVF_TEX1;
static const DWORD FVF_LIT    = D3DFVF_XYZ | D3DFVF_NORMAL;

static int fail(const char* what, HRESULT hr)
{
    std::printf("FAIL %s hr=0x%08lx\n", what, (unsigned long)hr);
    return 1;
}

// D3D9 maps pixel centres to integer coordinates, so shift by -0.5 for exact coverage.
static void quad(VtxRHWTex* v, float x0, float y0, float x1, float y1, DWORD c)
{
    x0 -= 0.5f; y0 -= 0.5f; x1 -= 0.5f; y1 -= 0.5f;
    v[0] = {x0, y0, 0.5f, 1.f, c, 0.f, 0.f};
    v[1] = {x1, y0, 0.5f, 1.f, c, 1.f, 0.f};
    v[2] = {x0, y1, 0.5f, 1.f, c, 0.f, 1.f};
    v[3] = {x1, y1, 0.5f, 1.f, c, 1.f, 1.f};
}

// One opaque 4x4 DXT1 block of a single RGB565 colour (colour0 = c, all indices 0).
static void solidBlock(uint8_t* dst, uint16_t c)
{
    dst[0] = (uint8_t)(c & 0xFF); dst[1] = (uint8_t)(c >> 8);
    for (int i = 2; i < 8; ++i) dst[i] = 0;
}

int main(int, char**)
{
    if (!SDL_Init(SDL_INIT_VIDEO)) { std::printf("FAIL SDL_Init %s\n", SDL_GetError()); return 1; }
    SDL_Window* win = SDL_CreateWindow("d3d9ff_probe", W, H, 0);
    if (!win) { std::printf("FAIL SDL_CreateWindow %s\n", SDL_GetError()); return 1; }
#ifdef _WIN32
    HWND hwnd = (HWND)SDL_GetPointerProperty(SDL_GetWindowProperties(win), SDL_PROP_WINDOW_WIN32_HWND_POINTER, nullptr);
#else
    HWND hwnd = (HWND)win;   // DXVK-native: the window handle IS the SDL_Window*
#endif

    IDirect3D9* d3d = Direct3DCreate9(D3D_SDK_VERSION);
    if (!d3d) { std::printf("FAIL Direct3DCreate9\n"); return 1; }
    D3DPRESENT_PARAMETERS pp = {};
    pp.Windowed = TRUE;
    pp.SwapEffect = D3DSWAPEFFECT_DISCARD;
    pp.BackBufferFormat = D3DFMT_X8R8G8B8;
    pp.BackBufferWidth = W;
    pp.BackBufferHeight = H;
    pp.hDeviceWindow = hwnd;
    pp.EnableAutoDepthStencil = TRUE;
    pp.AutoDepthStencilFormat = D3DFMT_D24S8;
    pp.PresentationInterval = D3DPRESENT_INTERVAL_ONE;
    IDirect3DDevice9* dev = nullptr;
    HRESULT hr = d3d->CreateDevice(D3DADAPTER_DEFAULT, D3DDEVTYPE_HAL, hwnd,
                                   D3DCREATE_MIXED_VERTEXPROCESSING, &pp, &dev);
    if (FAILED(hr)) return fail("CreateDevice", hr);

    // 8x8 DXT1 checker: red/green on the top block row, green/red below.
    IDirect3DTexture9* tex = nullptr;
    hr = dev->CreateTexture(8, 8, 1, 0, D3DFMT_DXT1, D3DPOOL_MANAGED, &tex, nullptr);
    if (FAILED(hr)) return fail("CreateTexture DXT1", hr);
    D3DLOCKED_RECT lr;
    tex->LockRect(0, &lr, nullptr, 0);
    uint8_t* bits = (uint8_t*)lr.pBits;
    solidBlock(bits + 0, 0xF800); solidBlock(bits + 8, 0x07E0);
    solidBlock(bits + lr.Pitch + 0, 0x07E0); solidBlock(bits + lr.Pitch + 8, 0xF800);
    tex->UnlockRect(0);

    // State block that turns on standard alpha blending (RAN records hundreds of these).
    IDirect3DStateBlock9* blend = nullptr;
    dev->BeginStateBlock();
    dev->SetRenderState(D3DRS_ALPHABLENDENABLE, TRUE);
    dev->SetRenderState(D3DRS_SRCBLEND, D3DBLEND_SRCALPHA);
    dev->SetRenderState(D3DRS_DESTBLEND, D3DBLEND_INVSRCALPHA);
    hr = dev->EndStateBlock(&blend);
    if (FAILED(hr)) return fail("EndStateBlock", hr);

    dev->Clear(0, nullptr, D3DCLEAR_TARGET | D3DCLEAR_ZBUFFER, D3DCOLOR_XRGB(32, 64, 96), 1.0f, 0);
    dev->BeginScene();
    dev->SetRenderState(D3DRS_ZENABLE, FALSE);
    dev->SetRenderState(D3DRS_CULLMODE, D3DCULL_NONE);
    dev->SetRenderState(D3DRS_ALPHABLENDENABLE, FALSE);

    // 1) Vertex-coloured pretransformed triangle (top right).
    dev->SetRenderState(D3DRS_LIGHTING, FALSE);
    dev->SetTexture(0, nullptr);
    dev->SetTextureStageState(0, D3DTSS_COLOROP, D3DTOP_SELECTARG1);
    dev->SetTextureStageState(0, D3DTSS_COLORARG1, D3DTA_DIFFUSE);
    dev->SetTextureStageState(0, D3DTSS_ALPHAOP, D3DTOP_SELECTARG1);
    dev->SetTextureStageState(0, D3DTSS_ALPHAARG1, D3DTA_DIFFUSE);
    VtxRHW tri[3] = {
        {300.f, 40.f, 0.5f, 1.f, D3DCOLOR_XRGB(255, 0, 0)},
        {480.f, 40.f, 0.5f, 1.f, D3DCOLOR_XRGB(0, 255, 0)},
        {390.f, 200.f, 0.5f, 1.f, D3DCOLOR_XRGB(0, 0, 255)}};
    dev->SetFVF(FVF_RHW);
    dev->DrawPrimitiveUP(D3DPT_TRIANGLELIST, 1, tri, sizeof(VtxRHW));

    // 2) DXT1 checker, MODULATE with half-grey diffuse, point sampling (top left).
    dev->SetTexture(0, tex);
    dev->SetSamplerState(0, D3DSAMP_MINFILTER, D3DTEXF_POINT);
    dev->SetSamplerState(0, D3DSAMP_MAGFILTER, D3DTEXF_POINT);
    dev->SetTextureStageState(0, D3DTSS_COLOROP, D3DTOP_MODULATE);
    dev->SetTextureStageState(0, D3DTSS_COLORARG1, D3DTA_TEXTURE);
    dev->SetTextureStageState(0, D3DTSS_COLORARG2, D3DTA_DIFFUSE);
    VtxRHWTex q[4];
    quad(q, 32.f, 32.f, 288.f, 288.f, D3DCOLOR_XRGB(128, 128, 128));
    dev->SetFVF(FVF_RHWTEX);
    dev->DrawPrimitiveUP(D3DPT_TRIANGLESTRIP, 2, q, sizeof(VtxRHWTex));

    // 3) Fixed-function lit quad: blue material, one directional light (bottom right).
    D3DMATRIX ident = {{{1,0,0,0, 0,1,0,0, 0,0,1,0, 0,0,0,1}}};
    const float zn = 1.f, zf = 100.f, Q = zf / (zf - zn);
    D3DMATRIX proj = {{{1,0,0,0, 0,1,0,0, 0,0,Q,1, 0,0,-zn * Q,0}}};   // 90° FOV, aspect 1, LH
    dev->SetTransform(D3DTS_WORLD, &ident);
    dev->SetTransform(D3DTS_VIEW, &ident);
    dev->SetTransform(D3DTS_PROJECTION, &proj);
    dev->SetRenderState(D3DRS_LIGHTING, TRUE);
    dev->SetRenderState(D3DRS_AMBIENT, 0);
    D3DLIGHT9 light = {};
    light.Type = D3DLIGHT_DIRECTIONAL;
    light.Diffuse = {1.f, 1.f, 1.f, 1.f};
    light.Direction = {0.f, 0.f, 1.f};
    dev->SetLight(0, &light);
    dev->LightEnable(0, TRUE);
    D3DMATERIAL9 mtl = {};
    mtl.Diffuse = {0.f, 0.f, 1.f, 1.f};
    dev->SetMaterial(&mtl);
    dev->SetTexture(0, nullptr);
    dev->SetTextureStageState(0, D3DTSS_COLOROP, D3DTOP_SELECTARG1);
    dev->SetTextureStageState(0, D3DTSS_COLORARG1, D3DTA_DIFFUSE);
    VtxLit lit[4] = {
        {1.5f, -1.5f, 5.f, 0.f, 0.f, -1.f}, {3.5f, -1.5f, 5.f, 0.f, 0.f, -1.f},
        {1.5f, -3.5f, 5.f, 0.f, 0.f, -1.f}, {3.5f, -3.5f, 5.f, 0.f, 0.f, -1.f}};
    dev->SetFVF(FVF_LIT);
    dev->DrawPrimitiveUP(D3DPT_TRIANGLESTRIP, 2, lit, sizeof(VtxLit));
    dev->SetRenderState(D3DRS_LIGHTING, FALSE);

    // 4) 50%-alpha white quad over everything, blending enabled by applying the state block.
    blend->Apply();
    dev->SetTextureStageState(0, D3DTSS_ALPHAOP, D3DTOP_SELECTARG1);
    dev->SetTextureStageState(0, D3DTSS_ALPHAARG1, D3DTA_DIFFUSE);
    quad(q, 160.f, 160.f, 400.f, 400.f, D3DCOLOR_ARGB(128, 255, 255, 255));
    dev->SetFVF(FVF_RHWTEX);
    dev->DrawPrimitiveUP(D3DPT_TRIANGLESTRIP, 2, q, sizeof(VtxRHWTex));
    dev->EndScene();

    // Read the back buffer back and write out.ppm (X8R8G8B8 = B,G,R,X in memory).
    IDirect3DSurface9 *bb = nullptr, *sys = nullptr;
    dev->GetBackBuffer(0, 0, D3DBACKBUFFER_TYPE_MONO, &bb);
    hr = dev->CreateOffscreenPlainSurface(W, H, D3DFMT_X8R8G8B8, D3DPOOL_SYSTEMMEM, &sys, nullptr);
    if (FAILED(hr)) return fail("CreateOffscreenPlainSurface", hr);
    hr = dev->GetRenderTargetData(bb, sys);
    if (FAILED(hr)) return fail("GetRenderTargetData", hr);
    sys->LockRect(&lr, nullptr, D3DLOCK_READONLY);
    FILE* f = std::fopen("out.ppm", "wb");
    std::fprintf(f, "P6\n%d %d\n255\n", W, H);
    for (int y = 0; y < H; ++y) {
        const uint8_t* row = (const uint8_t*)lr.pBits + y * lr.Pitch;
        for (int x = 0; x < W; ++x) {
            const uint8_t rgb[3] = {row[x * 4 + 2], row[x * 4 + 1], row[x * 4 + 0]};
            std::fwrite(rgb, 1, 3, f);
        }
    }
    std::fclose(f);
    sys->UnlockRect();
    dev->Present(nullptr, nullptr, nullptr, nullptr);
    std::printf("OK wrote out.ppm\n");

    sys->Release(); bb->Release(); blend->Release(); tex->Release(); dev->Release(); d3d->Release();
    SDL_Quit();
    return 0;
}
