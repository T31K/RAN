// Direct3D 9 on WebGL2 for the browser build.
//
// The native build gets D3D9 from DXVK (Vulkan); a page only has WebGL, so this is a D3D9
// implementation of exactly what the game uses (inventory: docs/port/web.md):
//  - fixed-function pipeline only. The device reports no vertex/pixel shader support, which
//    the engine handles everywhere (it was written for 2005 hardware); every draw runs a GLSL
//    program generated from the fixed-function state (d3d9_ffp.cpp), cached by state key.
//  - resources keep a CPU copy in their D3D format (Lock returns it) and upload to GL on
//    Unlock / first use; formats WebGL lacks are converted on upload (d3d9_format.cpp).
//  - render targets are textures/renderbuffers attached to framebuffer objects. Everything is
//    drawn upside down (D3D's top row = row 0 in memory, like D3D textures), so render target
//    textures sample the right way up; Present flips the back buffer onto the canvas.
//
// Threads: the game's threads are green threads on the page's thread (ran_green.h), so GL
// calls from any of them are fine; code below never yields while it holds GL state.
#pragma once
#include <d3d9.h>
#include <GLES3/gl3.h>
#include <GLES2/gl2ext.h>
#include <cstdint>
#include <cstring>
#include <map>
#include <memory>
#include <string>
#include <unordered_map>
#include <vector>

namespace d3d9web {

class Device;
class Direct3D;

// ---- formats (d3d9_format.cpp) ---------------------------------------------------------------

struct FormatInfo {
    D3DFORMAT format;
    int bytesPerPixel;        // 0 for block-compressed
    int blockBytes;           // DXT: bytes per 4x4 block
    GLenum internalFormat;    // GL storage
    GLenum glFormat, glType;  // upload format/type (ignored for compressed)
    bool compressed;
    bool renderable;
    bool depth;
};

const FormatInfo* GetFormatInfo(D3DFORMAT f);
bool HasS3tc();
// Row pitch and total size of one level in the D3D layout.
UINT LevelPitch(D3DFORMAT f, UINT width);
UINT LevelSize(D3DFORMAT f, UINT width, UINT height);
// Uploads one level (or a sub-rectangle of whole blocks/pixels) from D3D-layout memory to the
// currently bound texture target, converting as needed.
void UploadLevel(GLenum target, GLint level, D3DFORMAT f, UINT levelW, UINT levelH,
                 const RECT& r, const uint8_t* src, UINT srcPitch);
// GL RGBA8 rows (top-down) -> D3D format (for GetRenderTargetData / LockRect on targets).
void StoreFromRgba(D3DFORMAT f, const uint8_t* rgba, UINT w, UINT h, uint8_t* dst, UINT dstPitch);

// ---- COM helpers -----------------------------------------------------------------------------

#define D3D9WEB_UNKNOWN_IMPL                                                                \
    HRESULT STDMETHODCALLTYPE QueryInterface(REFIID, void** out) override                   \
    {                                                                                       \
        if (!out) return E_POINTER;                                                         \
        *out = this;                                                                        \
        AddRef();                                                                           \
        return S_OK;                                                                        \
    }

// Private data storage shared by resources.
struct PrivateData {
    std::map<std::string, std::vector<uint8_t>> items;
    HRESULT Set(REFGUID g, const void* d, DWORD n)
    {
        items[std::string((const char*)&g, sizeof(GUID))] = std::vector<uint8_t>((const uint8_t*)d, (const uint8_t*)d + n);
        return D3D_OK;
    }
    HRESULT Get(REFGUID g, void* d, DWORD* n)
    {
        auto it = items.find(std::string((const char*)&g, sizeof(GUID)));
        if (it == items.end()) return D3DERR_NOTFOUND;
        if (!d) { *n = (DWORD)it->second.size(); return D3D_OK; }
        if (*n < it->second.size()) { *n = (DWORD)it->second.size(); return D3DERR_MOREDATA; }
        std::memcpy(d, it->second.data(), it->second.size());
        *n = (DWORD)it->second.size();
        return D3D_OK;
    }
    HRESULT Free(REFGUID g) { items.erase(std::string((const char*)&g, sizeof(GUID))); return D3D_OK; }
};

// Methods every IDirect3DResource9 has.
#define D3D9WEB_RESOURCE_IMPL(TYPE)                                                          \
    HRESULT STDMETHODCALLTYPE GetDevice(IDirect3DDevice9** d) override;                      \
    HRESULT STDMETHODCALLTYPE SetPrivateData(REFGUID g, const void* p, DWORD n, DWORD) override { return m_private.Set(g, p, n); } \
    HRESULT STDMETHODCALLTYPE GetPrivateData(REFGUID g, void* p, DWORD* n) override { return m_private.Get(g, p, n); } \
    HRESULT STDMETHODCALLTYPE FreePrivateData(REFGUID g) override { return m_private.Free(g); } \
    DWORD STDMETHODCALLTYPE SetPriority(DWORD p) override { DWORD o = m_priority; m_priority = p; return o; } \
    DWORD STDMETHODCALLTYPE GetPriority() override { return m_priority; }                    \
    void STDMETHODCALLTYPE PreLoad() override {}                                             \
    D3DRESOURCETYPE STDMETHODCALLTYPE GetType() override { return TYPE; }

// ---- resources (d3d9_resource.cpp) -----------------------------------------------------------

class BaseTexture;

// A 2D image: a texture level / cube face level, the back buffer, a depth-stencil surface or an
// offscreen plain surface.
class Surface final : public IDirect3DSurface9 {
public:
    enum class Kind { TextureLevel, BackBuffer, DepthStencil, Offscreen };

    Surface(Device* dev, Kind kind, UINT w, UINT h, D3DFORMAT fmt, D3DPOOL pool, DWORD usage);
    ~Surface();

    D3D9WEB_UNKNOWN_IMPL
    ULONG STDMETHODCALLTYPE AddRef() override;
    ULONG STDMETHODCALLTYPE Release() override;
    D3D9WEB_RESOURCE_IMPL(D3DRTYPE_SURFACE)
    HRESULT STDMETHODCALLTYPE GetContainer(REFIID, void** out) override;
    HRESULT STDMETHODCALLTYPE GetDesc(D3DSURFACE_DESC* desc) override;
    HRESULT STDMETHODCALLTYPE LockRect(D3DLOCKED_RECT* lr, const RECT* r, DWORD flags) override;
    HRESULT STDMETHODCALLTYPE UnlockRect() override;
    HRESULT STDMETHODCALLTYPE GetDC(HDC*) override { return D3DERR_INVALIDCALL; }
    HRESULT STDMETHODCALLTYPE ReleaseDC(HDC) override { return D3DERR_INVALIDCALL; }

    Device* m_device;
    Kind m_kind;
    UINT m_width, m_height;
    D3DFORMAT m_format;
    D3DPOOL m_pool;
    DWORD m_usage;
    // TextureLevel: the owning texture (references are forwarded to it), level and cube face.
    BaseTexture* m_container = nullptr;
    UINT m_level = 0;
    GLenum m_face = GL_TEXTURE_2D;   // GL_TEXTURE_2D or GL_TEXTURE_CUBE_MAP_POSITIVE_X + n
    // Offscreen surfaces own their bytes; texture levels use the container's.
    std::vector<uint8_t> m_own;
    // DepthStencil / BackBuffer: the GL renderbuffer.
    GLuint m_renderbuffer = 0;
    bool m_locked = false;
    RECT m_lockRect = {};

    uint8_t* Bytes();          // CPU copy (D3D layout); allocated on demand
    UINT Pitch() const { return LevelPitch(m_format, m_width); }
    // Color attachment for rendering (textures with RENDERTARGET usage, the back buffer).
    bool IsRenderTarget() const;
    // Reads the GL contents back into the CPU copy (render targets).
    void ReadBack();

private:
    ULONG m_ref = 1;
    PrivateData m_private;
    DWORD m_priority = 0;
};

class BaseTexture {
public:
    virtual ~BaseTexture() = default;
    virtual ULONG AddRefT() = 0;
    virtual ULONG ReleaseT() = 0;
    GLenum m_target = GL_TEXTURE_2D;
    GLuint m_gl = 0;
    UINT m_levels = 1;
    UINT m_width = 1, m_height = 1;
    D3DFORMAT m_format = D3DFMT_UNKNOWN;
    D3DPOOL m_pool = D3DPOOL_MANAGED;
    DWORD m_usage = 0;
    // CPU copy of every level (cube: face-major), D3D layout; empty when released after upload.
    std::vector<std::vector<uint8_t>> m_data;
    // Levels (index face*levels+level) whose CPU copy is newer than the GL texture.
    std::vector<bool> m_dirty;
    std::vector<RECT> m_dirtyRect;
    // Locked levels and their rectangles. A lock can span suspensions (the game reads a file
    // into locked memory, and other green threads draw meanwhile), so a locked level is never
    // uploaded or released; its changes count once it is unlocked.
    std::vector<int> m_locks;
    std::vector<RECT> m_lockRect;
    void Lock(UINT index, const RECT& r, bool readOnly);
    void Unlock(UINT index);
    std::vector<Surface*> m_surfaces;
    bool m_anyDirty = false;
    Device* m_device = nullptr;
    UINT m_lod = 0;
    // Called before the texture is sampled or drawn into: uploads dirty levels.
    void Flush();
    uint8_t* LevelBytes(UINT index);   // allocates the CPU copy on demand
    void MarkDirty(UINT index, const RECT& r);
    UINT Faces() const { return m_target == GL_TEXTURE_CUBE_MAP ? 6 : 1; }
    UINT LevelW(UINT l) const { return std::max(1u, m_width >> l); }
    UINT LevelH(UINT l) const { return std::max(1u, m_height >> l); }
    void CreateStorage();
    void DestroyStorage();
};

class Texture final : public IDirect3DTexture9, public BaseTexture {
public:
    Texture(Device* dev, UINT w, UINT h, UINT levels, DWORD usage, D3DFORMAT fmt, D3DPOOL pool);
    ~Texture();
    D3D9WEB_UNKNOWN_IMPL
    ULONG STDMETHODCALLTYPE AddRef() override { return ++m_ref; }
    ULONG STDMETHODCALLTYPE Release() override;
    ULONG AddRefT() override { return AddRef(); }
    ULONG ReleaseT() override { return Release(); }
    D3D9WEB_RESOURCE_IMPL(D3DRTYPE_TEXTURE)
    DWORD STDMETHODCALLTYPE SetLOD(DWORD l) override { DWORD o = m_lod; m_lod = l; return o; }
    DWORD STDMETHODCALLTYPE GetLOD() override { return m_lod; }
    DWORD STDMETHODCALLTYPE GetLevelCount() override { return m_levels; }
    HRESULT STDMETHODCALLTYPE SetAutoGenFilterType(D3DTEXTUREFILTERTYPE) override { return D3D_OK; }
    D3DTEXTUREFILTERTYPE STDMETHODCALLTYPE GetAutoGenFilterType() override { return D3DTEXF_LINEAR; }
    void STDMETHODCALLTYPE GenerateMipSubLevels() override;
    HRESULT STDMETHODCALLTYPE GetLevelDesc(UINT level, D3DSURFACE_DESC* desc) override;
    HRESULT STDMETHODCALLTYPE GetSurfaceLevel(UINT level, IDirect3DSurface9** out) override;
    HRESULT STDMETHODCALLTYPE LockRect(UINT level, D3DLOCKED_RECT* lr, const RECT* r, DWORD flags) override;
    HRESULT STDMETHODCALLTYPE UnlockRect(UINT level) override;
    HRESULT STDMETHODCALLTYPE AddDirtyRect(const RECT*) override { return D3D_OK; }

private:
    ULONG m_ref = 1;
    PrivateData m_private;
    DWORD m_priority = 0;
};

class CubeTexture final : public IDirect3DCubeTexture9, public BaseTexture {
public:
    CubeTexture(Device* dev, UINT edge, UINT levels, DWORD usage, D3DFORMAT fmt, D3DPOOL pool);
    ~CubeTexture();
    D3D9WEB_UNKNOWN_IMPL
    ULONG STDMETHODCALLTYPE AddRef() override { return ++m_ref; }
    ULONG STDMETHODCALLTYPE Release() override;
    ULONG AddRefT() override { return AddRef(); }
    ULONG ReleaseT() override { return Release(); }
    D3D9WEB_RESOURCE_IMPL(D3DRTYPE_CUBETEXTURE)
    DWORD STDMETHODCALLTYPE SetLOD(DWORD l) override { DWORD o = m_lod; m_lod = l; return o; }
    DWORD STDMETHODCALLTYPE GetLOD() override { return m_lod; }
    DWORD STDMETHODCALLTYPE GetLevelCount() override { return m_levels; }
    HRESULT STDMETHODCALLTYPE SetAutoGenFilterType(D3DTEXTUREFILTERTYPE) override { return D3D_OK; }
    D3DTEXTUREFILTERTYPE STDMETHODCALLTYPE GetAutoGenFilterType() override { return D3DTEXF_LINEAR; }
    void STDMETHODCALLTYPE GenerateMipSubLevels() override {}
    HRESULT STDMETHODCALLTYPE GetLevelDesc(UINT level, D3DSURFACE_DESC* desc) override;
    HRESULT STDMETHODCALLTYPE GetCubeMapSurface(D3DCUBEMAP_FACES face, UINT level, IDirect3DSurface9** out) override;
    HRESULT STDMETHODCALLTYPE LockRect(D3DCUBEMAP_FACES face, UINT level, D3DLOCKED_RECT* lr, const RECT* r, DWORD flags) override;
    HRESULT STDMETHODCALLTYPE UnlockRect(D3DCUBEMAP_FACES face, UINT level) override;
    HRESULT STDMETHODCALLTYPE AddDirtyRect(D3DCUBEMAP_FACES, const RECT*) override { return D3D_OK; }

private:
    ULONG m_ref = 1;
    PrivateData m_private;
    DWORD m_priority = 0;
};

// Vertex and index buffers: a CPU copy plus a GL buffer updated on use.
class Buffer {
public:
    Device* m_dev = nullptr;
    std::vector<uint8_t> m_data;
    GLuint m_gl = 0;
    GLenum m_target = GL_ARRAY_BUFFER;
    UINT m_size = 0;
    DWORD m_usage = 0;
    D3DPOOL m_pool = D3DPOOL_MANAGED;
    UINT m_dirtyLo = 0, m_dirtyHi = 0;   // byte range not yet uploaded
    bool m_discarded = false;
    bool m_uploadedOnce = false;
    int m_locks = 0;   // see BaseTexture::m_locks
    void Flush();
    void Lock(UINT offset, UINT size, void** out, DWORD flags);
    void Unlock() { if (m_locks > 0) --m_locks; }
    void CreateGl();
    void DestroyGl();
};

class VertexBuffer final : public IDirect3DVertexBuffer9, public Buffer {
public:
    VertexBuffer(Device* dev, UINT len, DWORD usage, DWORD fvf, D3DPOOL pool);
    ~VertexBuffer();
    D3D9WEB_UNKNOWN_IMPL
    ULONG STDMETHODCALLTYPE AddRef() override { return ++m_ref; }
    ULONG STDMETHODCALLTYPE Release() override { const ULONG r = --m_ref; if (!r) delete this; return r; }
    D3D9WEB_RESOURCE_IMPL(D3DRTYPE_VERTEXBUFFER)
    HRESULT STDMETHODCALLTYPE Lock(UINT off, UINT size, void** out, DWORD flags) override { Buffer::Lock(off, size, out, flags); return D3D_OK; }
    HRESULT STDMETHODCALLTYPE Unlock() override { Buffer::Unlock(); return D3D_OK; }
    HRESULT STDMETHODCALLTYPE GetDesc(D3DVERTEXBUFFER_DESC* d) override;
    Device* m_device;
    DWORD m_fvf;

private:
    ULONG m_ref = 1;
    PrivateData m_private;
    DWORD m_priority = 0;
};

class IndexBuffer final : public IDirect3DIndexBuffer9, public Buffer {
public:
    IndexBuffer(Device* dev, UINT len, DWORD usage, D3DFORMAT fmt, D3DPOOL pool);
    ~IndexBuffer();
    D3D9WEB_UNKNOWN_IMPL
    ULONG STDMETHODCALLTYPE AddRef() override { return ++m_ref; }
    ULONG STDMETHODCALLTYPE Release() override { const ULONG r = --m_ref; if (!r) delete this; return r; }
    D3D9WEB_RESOURCE_IMPL(D3DRTYPE_INDEXBUFFER)
    HRESULT STDMETHODCALLTYPE Lock(UINT off, UINT size, void** out, DWORD flags) override { Buffer::Lock(off, size, out, flags); return D3D_OK; }
    HRESULT STDMETHODCALLTYPE Unlock() override { Buffer::Unlock(); return D3D_OK; }
    HRESULT STDMETHODCALLTYPE GetDesc(D3DINDEXBUFFER_DESC* d) override;
    Device* m_device;
    D3DFORMAT m_format;

private:
    ULONG m_ref = 1;
    PrivateData m_private;
    DWORD m_priority = 0;
};

class VertexDeclaration final : public IDirect3DVertexDeclaration9 {
public:
    VertexDeclaration(Device* dev, const D3DVERTEXELEMENT9* e);
    D3D9WEB_UNKNOWN_IMPL
    ULONG STDMETHODCALLTYPE AddRef() override { return ++m_ref; }
    ULONG STDMETHODCALLTYPE Release() override { const ULONG r = --m_ref; if (!r) delete this; return r; }
    HRESULT STDMETHODCALLTYPE GetDevice(IDirect3DDevice9** d) override;
    HRESULT STDMETHODCALLTYPE GetDeclaration(D3DVERTEXELEMENT9* e, UINT* n) override;
    Device* m_device;
    std::vector<D3DVERTEXELEMENT9> m_elements;   // without the END element
    DWORD m_fvf = 0;                             // equivalent FVF when one exists (0 otherwise)

private:
    ULONG m_ref = 1;
};

// Shader objects exist so the creation calls succeed; the device reports no shader support and
// the game never binds them for drawing (a bound shader makes draws a no-op).
class Shader final : public IDirect3DVertexShader9 {
public:
    Shader(Device* dev, const DWORD* code);
    D3D9WEB_UNKNOWN_IMPL
    ULONG STDMETHODCALLTYPE AddRef() override { return ++m_ref; }
    ULONG STDMETHODCALLTYPE Release() override { const ULONG r = --m_ref; if (!r) delete this; return r; }
    HRESULT STDMETHODCALLTYPE GetDevice(IDirect3DDevice9** d) override;
    HRESULT STDMETHODCALLTYPE GetFunction(void* data, UINT* size) override;
    Device* m_device;
    std::vector<DWORD> m_code;

private:
    ULONG m_ref = 1;
};

// ---- device state ----------------------------------------------------------------------------

constexpr int kStages = 8;
constexpr int kMaxLights = 8;          // simultaneously enabled lights
constexpr int kRenderStates = 256;
constexpr int kStageStates = 33;
constexpr int kSamplerStates = 14;

struct LightSlot {
    D3DLIGHT9 light = {};
    bool enabled = false;
    bool set = false;
};

struct StreamBinding {
    VertexBuffer* buffer = nullptr;
    UINT offset = 0, stride = 0;
};

// Everything a state block can capture. The device keeps one ("current") and each state block
// keeps one plus the masks of what it records.
struct State {
    DWORD rs[kRenderStates] = {};
    DWORD tss[kStages][kStageStates] = {};
    DWORD samp[kStages][kSamplerStates] = {};
    IDirect3DBaseTexture9* textures[kStages] = {};
    D3DMATRIX transforms[512] = {};       // view 2, proj 3, texture 16-23, world 256-511
    D3DMATERIAL9 material = {};
    D3DVIEWPORT9 viewport = {};
    float clipPlanes[6][4] = {};
    DWORD fvf = 0;
    VertexDeclaration* decl = nullptr;
    StreamBinding streams[1];
    IndexBuffer* indices = nullptr;
    std::map<DWORD, LightSlot> lights;
};

struct StateMask {
    std::vector<bool> rs = std::vector<bool>(kRenderStates);
    std::vector<bool> tss = std::vector<bool>(kStages * kStageStates);
    std::vector<bool> samp = std::vector<bool>(kStages * kSamplerStates);
    std::vector<bool> textures = std::vector<bool>(kStages);
    std::vector<bool> transforms = std::vector<bool>(512);
    std::vector<bool> clipPlanes = std::vector<bool>(6);
    std::map<DWORD, bool> lights;   // index -> recorded (light and enable)
    bool material = false, viewport = false, fvf = false, decl = false, streams = false, indices = false;
    bool vs = false, ps = false;
};

class StateBlock final : public IDirect3DStateBlock9 {
public:
    explicit StateBlock(Device* dev) : m_device(dev) {}
    D3D9WEB_UNKNOWN_IMPL
    ULONG STDMETHODCALLTYPE AddRef() override { return ++m_ref; }
    ULONG STDMETHODCALLTYPE Release() override { const ULONG r = --m_ref; if (!r) delete this; return r; }
    HRESULT STDMETHODCALLTYPE GetDevice(IDirect3DDevice9** d) override;
    HRESULT STDMETHODCALLTYPE Capture() override;
    HRESULT STDMETHODCALLTYPE Apply() override;
    Device* m_device;
    State m_state;
    StateMask m_mask;

private:
    ULONG m_ref = 1;
};

// ---- fixed-function programs (d3d9_ffp.cpp) --------------------------------------------------

// GL attribute locations of the fixed-function programs.
enum Attrib { kAttrPos = 0, kAttrWeights, kAttrIndices, kAttrNormal, kAttrPsize, kAttrDiffuse, kAttrSpecular, kAttrTex0 };

// What a vertex carries, from an FVF or a declaration (stream 0 only - all the game uses).
struct VertexLayout {
    struct Elem { bool present = false; UINT offset = 0; GLint size = 0; GLenum type = GL_FLOAT; bool norm = false; bool isInt = false; };
    Elem attr[kAttrTex0 + kStages];
    bool rhw = false;          // pretransformed (XYZRHW / POSITIONT)
    int weights = 0;           // blend weights stored in the vertex
    bool indexed = false;      // UBYTE4 matrix indices (LASTBETA_UBYTE4)
    int texSize[kStages] = {}; // components of each texcoord set (0 = absent)
    UINT stride = 0;           // FVF stride (declarations use the stream stride)
};
VertexLayout LayoutFromFvf(DWORD fvf);
VertexLayout LayoutFromDecl(const VertexDeclaration* d);

// The uniform block of every fixed-function program (std140; all members are vec4-aligned).
struct FfpConstants {
    float wvp[16], wv[16], normalMat[16], world[16], proj[16];
    float wvBlend[4][16];
    float texMat[kStages][16];
    float matDiffuse[4], matAmbient[4], matSpecular[4], matEmissive[4], matPower[4];
    float globalAmbient[4];
    float lightDiffuse[kMaxLights][4], lightSpecular[kMaxLights][4], lightAmbient[kMaxLights][4];
    float lightPos[kMaxLights][4], lightDir[kMaxLights][4], lightAtt[kMaxLights][4], lightSpot[kMaxLights][4];
    float fogParams[4], fogColor[4], tfactor[4], alphaRef[4];
    float viewport[4], pixelOffset[4], clipPlane[4];
    float bumpMat[kStages][4], bumpLum[kStages][4];
    float pointParams[4], pointScale[4];
};

struct FfpProgram {
    GLuint program = 0;
};

// Builds the program keys for the current state (d3d9_ffp.cpp).
void BuildFfpKeys(const State& s, const VertexLayout& layout, std::vector<uint32_t>& vsKey, std::vector<uint32_t>& psKey);
// GLSL for the keys; empty on an unsupported combination.
std::string GenerateVertexShader(const std::vector<uint32_t>& vsKey);
std::string GenerateFragmentShader(const std::vector<uint32_t>& psKey);
// Fills the uniform block from the state.
void FillFfpConstants(const State& s, FfpConstants& c);

// ---- the device ------------------------------------------------------------------------------

class Device final : public IDirect3DDevice9 {
public:
    Device(Direct3D* d3d, HWND window, DWORD behavior, D3DPRESENT_PARAMETERS* pp, int glContext);
    ~Device();

    D3D9WEB_UNKNOWN_IMPL
    ULONG STDMETHODCALLTYPE AddRef() override { return ++m_ref; }
    ULONG STDMETHODCALLTYPE Release() override { return --m_ref; }   // lives as long as the page

    HRESULT STDMETHODCALLTYPE TestCooperativeLevel() override { return D3D_OK; }
    UINT STDMETHODCALLTYPE GetAvailableTextureMem() override { return 512u << 20; }
    HRESULT STDMETHODCALLTYPE EvictManagedResources() override { return D3D_OK; }
    HRESULT STDMETHODCALLTYPE GetDirect3D(IDirect3D9** out) override;
    HRESULT STDMETHODCALLTYPE GetDeviceCaps(D3DCAPS9* caps) override;
    HRESULT STDMETHODCALLTYPE GetDisplayMode(UINT, D3DDISPLAYMODE* mode) override;
    HRESULT STDMETHODCALLTYPE GetCreationParameters(D3DDEVICE_CREATION_PARAMETERS* p) override;
    HRESULT STDMETHODCALLTYPE SetCursorProperties(UINT, UINT, IDirect3DSurface9*) override { return D3D_OK; }
    void STDMETHODCALLTYPE SetCursorPosition(int, int, DWORD) override {}
    WINBOOL STDMETHODCALLTYPE ShowCursor(WINBOOL show) override { WINBOOL o = m_cursorShown; m_cursorShown = show; return o; }
    HRESULT STDMETHODCALLTYPE CreateAdditionalSwapChain(D3DPRESENT_PARAMETERS*, IDirect3DSwapChain9**) override { return D3DERR_NOTAVAILABLE; }
    HRESULT STDMETHODCALLTYPE GetSwapChain(UINT, IDirect3DSwapChain9** out) override { if (out) *out = nullptr; return D3DERR_NOTAVAILABLE; }
    UINT STDMETHODCALLTYPE GetNumberOfSwapChains() override { return 1; }
    HRESULT STDMETHODCALLTYPE Reset(D3DPRESENT_PARAMETERS* pp) override;
    HRESULT STDMETHODCALLTYPE Present(const RECT*, const RECT*, HWND, const RGNDATA*) override;
    HRESULT STDMETHODCALLTYPE GetBackBuffer(UINT, UINT, D3DBACKBUFFER_TYPE, IDirect3DSurface9** out) override;
    HRESULT STDMETHODCALLTYPE GetRasterStatus(UINT, D3DRASTER_STATUS* s) override { if (s) std::memset(s, 0, sizeof(*s)); return D3D_OK; }
    HRESULT STDMETHODCALLTYPE SetDialogBoxMode(WINBOOL) override { return D3D_OK; }
    void STDMETHODCALLTYPE SetGammaRamp(UINT, DWORD, const D3DGAMMARAMP*) override {}
    void STDMETHODCALLTYPE GetGammaRamp(UINT, D3DGAMMARAMP* r) override;
    HRESULT STDMETHODCALLTYPE CreateTexture(UINT w, UINT h, UINT levels, DWORD usage, D3DFORMAT fmt, D3DPOOL pool, IDirect3DTexture9** out, HANDLE*) override;
    HRESULT STDMETHODCALLTYPE CreateVolumeTexture(UINT, UINT, UINT, UINT, DWORD, D3DFORMAT, D3DPOOL, IDirect3DVolumeTexture9** out, HANDLE*) override { if (out) *out = nullptr; return D3DERR_NOTAVAILABLE; }
    HRESULT STDMETHODCALLTYPE CreateCubeTexture(UINT edge, UINT levels, DWORD usage, D3DFORMAT fmt, D3DPOOL pool, IDirect3DCubeTexture9** out, HANDLE*) override;
    HRESULT STDMETHODCALLTYPE CreateVertexBuffer(UINT len, DWORD usage, DWORD fvf, D3DPOOL pool, IDirect3DVertexBuffer9** out, HANDLE*) override;
    HRESULT STDMETHODCALLTYPE CreateIndexBuffer(UINT len, DWORD usage, D3DFORMAT fmt, D3DPOOL pool, IDirect3DIndexBuffer9** out, HANDLE*) override;
    HRESULT STDMETHODCALLTYPE CreateRenderTarget(UINT w, UINT h, D3DFORMAT fmt, D3DMULTISAMPLE_TYPE, DWORD, WINBOOL, IDirect3DSurface9** out, HANDLE*) override;
    HRESULT STDMETHODCALLTYPE CreateDepthStencilSurface(UINT w, UINT h, D3DFORMAT fmt, D3DMULTISAMPLE_TYPE, DWORD, WINBOOL, IDirect3DSurface9** out, HANDLE*) override;
    HRESULT STDMETHODCALLTYPE UpdateSurface(IDirect3DSurface9* src, const RECT* srcRect, IDirect3DSurface9* dst, const POINT* dstPoint) override;
    HRESULT STDMETHODCALLTYPE UpdateTexture(IDirect3DBaseTexture9* src, IDirect3DBaseTexture9* dst) override;
    HRESULT STDMETHODCALLTYPE GetRenderTargetData(IDirect3DSurface9* rt, IDirect3DSurface9* dst) override;
    HRESULT STDMETHODCALLTYPE GetFrontBufferData(UINT, IDirect3DSurface9* dst) override;
    HRESULT STDMETHODCALLTYPE StretchRect(IDirect3DSurface9* src, const RECT* srcRect, IDirect3DSurface9* dst, const RECT* dstRect, D3DTEXTUREFILTERTYPE filter) override;
    HRESULT STDMETHODCALLTYPE ColorFill(IDirect3DSurface9* s, const RECT* r, D3DCOLOR c) override;
    HRESULT STDMETHODCALLTYPE CreateOffscreenPlainSurface(UINT w, UINT h, D3DFORMAT fmt, D3DPOOL pool, IDirect3DSurface9** out, HANDLE*) override;
    HRESULT STDMETHODCALLTYPE SetRenderTarget(DWORD index, IDirect3DSurface9* rt) override;
    HRESULT STDMETHODCALLTYPE GetRenderTarget(DWORD index, IDirect3DSurface9** out) override;
    HRESULT STDMETHODCALLTYPE SetDepthStencilSurface(IDirect3DSurface9* ds) override;
    HRESULT STDMETHODCALLTYPE GetDepthStencilSurface(IDirect3DSurface9** out) override;
    HRESULT STDMETHODCALLTYPE BeginScene() override { return D3D_OK; }
    HRESULT STDMETHODCALLTYPE EndScene() override { return D3D_OK; }
    HRESULT STDMETHODCALLTYPE Clear(DWORD count, const D3DRECT* rects, DWORD flags, D3DCOLOR color, float z, DWORD stencil) override;
    HRESULT STDMETHODCALLTYPE SetTransform(D3DTRANSFORMSTATETYPE t, const D3DMATRIX* m) override;
    HRESULT STDMETHODCALLTYPE GetTransform(D3DTRANSFORMSTATETYPE t, D3DMATRIX* m) override;
    HRESULT STDMETHODCALLTYPE MultiplyTransform(D3DTRANSFORMSTATETYPE t, const D3DMATRIX* m) override;
    HRESULT STDMETHODCALLTYPE SetViewport(const D3DVIEWPORT9* vp) override;
    HRESULT STDMETHODCALLTYPE GetViewport(D3DVIEWPORT9* vp) override;
    HRESULT STDMETHODCALLTYPE SetMaterial(const D3DMATERIAL9* m) override;
    HRESULT STDMETHODCALLTYPE GetMaterial(D3DMATERIAL9* m) override;
    HRESULT STDMETHODCALLTYPE SetLight(DWORD index, const D3DLIGHT9* l) override;
    HRESULT STDMETHODCALLTYPE GetLight(DWORD index, D3DLIGHT9* l) override;
    HRESULT STDMETHODCALLTYPE LightEnable(DWORD index, WINBOOL enable) override;
    HRESULT STDMETHODCALLTYPE GetLightEnable(DWORD index, WINBOOL* enable) override;
    HRESULT STDMETHODCALLTYPE SetClipPlane(DWORD index, const float* plane) override;
    HRESULT STDMETHODCALLTYPE GetClipPlane(DWORD index, float* plane) override;
    HRESULT STDMETHODCALLTYPE SetRenderState(D3DRENDERSTATETYPE s, DWORD v) override;
    HRESULT STDMETHODCALLTYPE GetRenderState(D3DRENDERSTATETYPE s, DWORD* v) override;
    HRESULT STDMETHODCALLTYPE CreateStateBlock(D3DSTATEBLOCKTYPE type, IDirect3DStateBlock9** out) override;
    HRESULT STDMETHODCALLTYPE BeginStateBlock() override;
    HRESULT STDMETHODCALLTYPE EndStateBlock(IDirect3DStateBlock9** out) override;
    HRESULT STDMETHODCALLTYPE SetClipStatus(const D3DCLIPSTATUS9*) override { return D3D_OK; }
    HRESULT STDMETHODCALLTYPE GetClipStatus(D3DCLIPSTATUS9* s) override { if (s) std::memset(s, 0, sizeof(*s)); return D3D_OK; }
    HRESULT STDMETHODCALLTYPE GetTexture(DWORD stage, IDirect3DBaseTexture9** out) override;
    HRESULT STDMETHODCALLTYPE SetTexture(DWORD stage, IDirect3DBaseTexture9* tex) override;
    HRESULT STDMETHODCALLTYPE GetTextureStageState(DWORD stage, D3DTEXTURESTAGESTATETYPE t, DWORD* v) override;
    HRESULT STDMETHODCALLTYPE SetTextureStageState(DWORD stage, D3DTEXTURESTAGESTATETYPE t, DWORD v) override;
    HRESULT STDMETHODCALLTYPE GetSamplerState(DWORD s, D3DSAMPLERSTATETYPE t, DWORD* v) override;
    HRESULT STDMETHODCALLTYPE SetSamplerState(DWORD s, D3DSAMPLERSTATETYPE t, DWORD v) override;
    HRESULT STDMETHODCALLTYPE ValidateDevice(DWORD* passes) override { if (passes) *passes = 1; return D3D_OK; }
    HRESULT STDMETHODCALLTYPE SetPaletteEntries(UINT, const PALETTEENTRY*) override { return D3D_OK; }
    HRESULT STDMETHODCALLTYPE GetPaletteEntries(UINT, PALETTEENTRY*) override { return D3DERR_INVALIDCALL; }
    HRESULT STDMETHODCALLTYPE SetCurrentTexturePalette(UINT) override { return D3D_OK; }
    HRESULT STDMETHODCALLTYPE GetCurrentTexturePalette(UINT* p) override { if (p) *p = 0; return D3D_OK; }
    HRESULT STDMETHODCALLTYPE SetScissorRect(const RECT* r) override { if (r) m_scissor = *r; m_rasterDirty = true; return D3D_OK; }
    HRESULT STDMETHODCALLTYPE GetScissorRect(RECT* r) override { if (r) *r = m_scissor; return D3D_OK; }
    HRESULT STDMETHODCALLTYPE SetSoftwareVertexProcessing(WINBOOL sw) override { m_softwareVP = sw; return D3D_OK; }
    WINBOOL STDMETHODCALLTYPE GetSoftwareVertexProcessing() override { return m_softwareVP; }
    HRESULT STDMETHODCALLTYPE SetNPatchMode(float) override { return D3D_OK; }
    float STDMETHODCALLTYPE GetNPatchMode() override { return 0; }
    HRESULT STDMETHODCALLTYPE DrawPrimitive(D3DPRIMITIVETYPE type, UINT start, UINT count) override;
    HRESULT STDMETHODCALLTYPE DrawIndexedPrimitive(D3DPRIMITIVETYPE type, INT baseVertex, UINT minIndex, UINT numVertices, UINT startIndex, UINT count) override;
    HRESULT STDMETHODCALLTYPE DrawPrimitiveUP(D3DPRIMITIVETYPE type, UINT count, const void* data, UINT stride) override;
    HRESULT STDMETHODCALLTYPE DrawIndexedPrimitiveUP(D3DPRIMITIVETYPE type, UINT minIndex, UINT numVertices, UINT count, const void* indices, D3DFORMAT fmt, const void* data, UINT stride) override;
    HRESULT STDMETHODCALLTYPE ProcessVertices(UINT, UINT, UINT, IDirect3DVertexBuffer9*, IDirect3DVertexDeclaration9*, DWORD) override { return D3DERR_INVALIDCALL; }
    HRESULT STDMETHODCALLTYPE CreateVertexDeclaration(const D3DVERTEXELEMENT9* e, IDirect3DVertexDeclaration9** out) override;
    HRESULT STDMETHODCALLTYPE SetVertexDeclaration(IDirect3DVertexDeclaration9* d) override;
    HRESULT STDMETHODCALLTYPE GetVertexDeclaration(IDirect3DVertexDeclaration9** out) override;
    HRESULT STDMETHODCALLTYPE SetFVF(DWORD fvf) override;
    HRESULT STDMETHODCALLTYPE GetFVF(DWORD* fvf) override;
    HRESULT STDMETHODCALLTYPE CreateVertexShader(const DWORD* code, IDirect3DVertexShader9** out) override;
    HRESULT STDMETHODCALLTYPE SetVertexShader(IDirect3DVertexShader9* s) override;
    HRESULT STDMETHODCALLTYPE GetVertexShader(IDirect3DVertexShader9** out) override;
    HRESULT STDMETHODCALLTYPE SetVertexShaderConstantF(UINT, const float*, UINT) override { return D3D_OK; }
    HRESULT STDMETHODCALLTYPE GetVertexShaderConstantF(UINT, float*, UINT) override { return D3D_OK; }
    HRESULT STDMETHODCALLTYPE SetVertexShaderConstantI(UINT, const int*, UINT) override { return D3D_OK; }
    HRESULT STDMETHODCALLTYPE GetVertexShaderConstantI(UINT, int*, UINT) override { return D3D_OK; }
    HRESULT STDMETHODCALLTYPE SetVertexShaderConstantB(UINT, const WINBOOL*, UINT) override { return D3D_OK; }
    HRESULT STDMETHODCALLTYPE GetVertexShaderConstantB(UINT, WINBOOL*, UINT) override { return D3D_OK; }
    HRESULT STDMETHODCALLTYPE SetStreamSource(UINT stream, IDirect3DVertexBuffer9* vb, UINT offset, UINT stride) override;
    HRESULT STDMETHODCALLTYPE GetStreamSource(UINT stream, IDirect3DVertexBuffer9** out, UINT* offset, UINT* stride) override;
    HRESULT STDMETHODCALLTYPE SetStreamSourceFreq(UINT, UINT) override { return D3D_OK; }
    HRESULT STDMETHODCALLTYPE GetStreamSourceFreq(UINT, UINT* d) override { if (d) *d = 1; return D3D_OK; }
    HRESULT STDMETHODCALLTYPE SetIndices(IDirect3DIndexBuffer9* ib) override;
    HRESULT STDMETHODCALLTYPE GetIndices(IDirect3DIndexBuffer9** out) override;
    HRESULT STDMETHODCALLTYPE CreatePixelShader(const DWORD* code, IDirect3DPixelShader9** out) override;
    HRESULT STDMETHODCALLTYPE SetPixelShader(IDirect3DPixelShader9* s) override;
    HRESULT STDMETHODCALLTYPE GetPixelShader(IDirect3DPixelShader9** out) override;
    HRESULT STDMETHODCALLTYPE SetPixelShaderConstantF(UINT, const float*, UINT) override { return D3D_OK; }
    HRESULT STDMETHODCALLTYPE GetPixelShaderConstantF(UINT, float*, UINT) override { return D3D_OK; }
    HRESULT STDMETHODCALLTYPE SetPixelShaderConstantI(UINT, const int*, UINT) override { return D3D_OK; }
    HRESULT STDMETHODCALLTYPE GetPixelShaderConstantI(UINT, int*, UINT) override { return D3D_OK; }
    HRESULT STDMETHODCALLTYPE SetPixelShaderConstantB(UINT, const WINBOOL*, UINT) override { return D3D_OK; }
    HRESULT STDMETHODCALLTYPE GetPixelShaderConstantB(UINT, WINBOOL*, UINT) override { return D3D_OK; }
    HRESULT STDMETHODCALLTYPE DrawRectPatch(UINT, const float*, const D3DRECTPATCH_INFO*) override { return D3DERR_INVALIDCALL; }
    HRESULT STDMETHODCALLTYPE DrawTriPatch(UINT, const float*, const D3DTRIPATCH_INFO*) override { return D3DERR_INVALIDCALL; }
    HRESULT STDMETHODCALLTYPE DeletePatch(UINT) override { return D3D_OK; }
    HRESULT STDMETHODCALLTYPE CreateQuery(D3DQUERYTYPE, IDirect3DQuery9** out) override { if (out) *out = nullptr; return D3DERR_NOTAVAILABLE; }

    // ---- internals shared with the resources / state blocks / ffp
    State m_state;
    StateBlock* m_recording = nullptr;
    void ApplyState(const State& s, const StateMask& m);       // state block Apply
    void CaptureState(State& s, const StateMask& m);           // state block Capture

    // GL helpers
    void BindTextureForUpload(GLenum target, GLuint tex);     // binds on the scratch unit
    void ForgetTexture(GLuint tex);
    void ForgetBuffer(GLuint buf);
    GLuint FramebufferFor(Surface* color, Surface* depth);
    void ForgetSurface(Surface* s);
    Surface* m_backBuffer = nullptr;
    Surface* m_autoDepth = nullptr;
    D3DPRESENT_PARAMETERS m_pp = {};
    bool m_hasS3tc = false;

private:
    friend class Direct3D;
    ULONG m_ref = 1;
    Direct3D* m_d3d;
    HWND m_window;
    DWORD m_behavior;
    int m_context;
    WINBOOL m_cursorShown = TRUE;
    WINBOOL m_softwareVP = FALSE;
    RECT m_scissor = {};
    IDirect3DVertexShader9* m_vs = nullptr;
    IDirect3DPixelShader9* m_ps = nullptr;
    Surface* m_rt = nullptr;
    Surface* m_ds = nullptr;
    std::map<std::pair<Surface*, Surface*>, GLuint> m_fbos;

    // GL state cache
    struct GlCache {
        GLuint fbo = ~0u;
        GLuint program = ~0u;
        GLuint arrayBuffer = ~0u, elementBuffer = ~0u;
        GLuint textures[kStages] = {};
        GLenum textureTargets[kStages] = {};
        GLuint samplers[kStages] = {};
        int activeUnit = -1;
        GLint viewport[4] = {-1, -1, -1, -1};
        float depthRange[2] = {-1, -1};
        int blend = -1, depthTest = -1, depthMask = -1, cull = -1, scissorTest = -1, polyOffset = -1;
        GLenum blendSrc = 0, blendDst = 0, blendSrcA = 0, blendDstA = 0, blendEq = 0, blendEqA = 0;
        GLenum depthFunc = 0, cullFace = 0;
        GLint scissor[4] = {-1, -1, -1, -1};
        int colorMask = -1;
        float polyFactor = 0, polyUnits = 0;
        DWORD blendColor = 0xFFFFFFFF;
        struct Attrib { bool enabled = false; GLuint buffer = ~0u; GLint size = 0; GLenum type = 0; bool norm = false; GLsizei stride = 0; uintptr_t offset = 0; } attribs[16];
    } m_gl;
    bool m_rasterDirty = true;
    bool m_fboDirty = true;
    bool m_boundDepth = false;
    bool m_hasAniso = false;
    unsigned m_frames = 0;
    struct Stats { unsigned draws, skipShader, skipNoPos, offscreen, indexed, up, uploads; unsigned byFvf[0x400]; } m_stats = {};   // ?gltrace, ?perf
    std::string m_frameLog;
    bool LogFrame() const;
    void FrameLog(const char* fmt, ...) __attribute__((format(printf, 2, 3)));

    // Per-draw data (the constants, DrawPrimitiveUP vertices and indices) goes to buffers the GPU
    // is done with. Rewriting a buffer an earlier draw still reads makes the browser's GL (ANGLE
    // on Metal/D3D) copy the whole buffer or stall, on every draw - that was most of the frame
    // time. So each upload takes its own buffer from the pool of the current frame; a frame's
    // pool is reused three frames later, after the GPU finished it.
    enum TransientKind { kTransientUniform, kTransientVertex, kTransientIndex, kTransientKinds };
    static constexpr unsigned kTransientFrames = 3;
    struct TransientPool { std::vector<std::pair<GLuint, UINT>> buffers; size_t next = 0; };
    TransientPool m_transient[kTransientFrames][kTransientKinds];
    GLuint Transient(TransientKind kind, const void* data, UINT size);
    static constexpr UINT kUpVertexSize = 8u << 20, kUpIndexSize = 2u << 20;   // largest UP draw

    // fixed-function programs and their constants (uniform buffer)
    GLuint m_ubo = 0;                 // bound at binding 0, holding m_uploadedConstants
    FfpConstants m_uploadedConstants;
    bool m_constantsValid = false;
    std::unordered_map<std::string, FfpProgram> m_programs;
    std::map<std::vector<uint32_t>, GLuint> m_samplerObjects;
    std::vector<uint32_t> m_vsKey, m_psKey;

    State& Target();
    void InitGl();
    void CreateBackBuffer();
    void BindFramebuffer();
    void ApplyRasterState();
    void ApplyViewport();
    bool PrepareDraw(GLuint vbo, uintptr_t base, UINT stride);
    void ApplyTextures(int stages);
    void SetupAttributes(const VertexLayout& layout, GLuint vbo, uintptr_t base, UINT stride);
    GLuint SamplerFor(int stage);
    const FfpProgram& ProgramFor(const std::vector<uint32_t>& vsKey, const std::vector<uint32_t>& psKey);

public:
    void BindArrayBuffer(GLuint b);
    void BindElementBuffer(GLuint b);
};

class Direct3D final : public IDirect3D9 {
public:
    D3D9WEB_UNKNOWN_IMPL
    ULONG STDMETHODCALLTYPE AddRef() override { return ++m_ref; }
    ULONG STDMETHODCALLTYPE Release() override { return --m_ref; }
    HRESULT STDMETHODCALLTYPE RegisterSoftwareDevice(void*) override { return D3DERR_INVALIDCALL; }
    UINT STDMETHODCALLTYPE GetAdapterCount() override { return 1; }
    HRESULT STDMETHODCALLTYPE GetAdapterIdentifier(UINT, DWORD, D3DADAPTER_IDENTIFIER9* id) override;
    UINT STDMETHODCALLTYPE GetAdapterModeCount(UINT, D3DFORMAT fmt) override;
    HRESULT STDMETHODCALLTYPE EnumAdapterModes(UINT, D3DFORMAT fmt, UINT mode, D3DDISPLAYMODE* out) override;
    HRESULT STDMETHODCALLTYPE GetAdapterDisplayMode(UINT, D3DDISPLAYMODE* mode) override;
    HRESULT STDMETHODCALLTYPE CheckDeviceType(UINT, D3DDEVTYPE, D3DFORMAT, D3DFORMAT, WINBOOL) override;
    HRESULT STDMETHODCALLTYPE CheckDeviceFormat(UINT, D3DDEVTYPE, D3DFORMAT, DWORD usage, D3DRESOURCETYPE type, D3DFORMAT fmt) override;
    HRESULT STDMETHODCALLTYPE CheckDeviceMultiSampleType(UINT, D3DDEVTYPE, D3DFORMAT, WINBOOL, D3DMULTISAMPLE_TYPE t, DWORD* q) override;
    HRESULT STDMETHODCALLTYPE CheckDepthStencilMatch(UINT, D3DDEVTYPE, D3DFORMAT, D3DFORMAT, D3DFORMAT ds) override;
    HRESULT STDMETHODCALLTYPE CheckDeviceFormatConversion(UINT, D3DDEVTYPE, D3DFORMAT, D3DFORMAT) override { return D3D_OK; }
    HRESULT STDMETHODCALLTYPE GetDeviceCaps(UINT, D3DDEVTYPE, D3DCAPS9* caps) override;
    HMONITOR STDMETHODCALLTYPE GetAdapterMonitor(UINT) override { return nullptr; }
    HRESULT STDMETHODCALLTYPE CreateDevice(UINT, D3DDEVTYPE, HWND focus, DWORD behavior, D3DPRESENT_PARAMETERS* pp, IDirect3DDevice9** out) override;

    static void FillCaps(D3DCAPS9* caps);
    Device* m_device = nullptr;

private:
    ULONG m_ref = 1;
    std::vector<D3DDISPLAYMODE> Modes();
};

// D3DCOLOR (0xAARRGGBB) -> RGBA floats.
inline void ColorToFloats(D3DCOLOR c, float* out)
{
    out[0] = ((c >> 16) & 255) / 255.0f;
    out[1] = ((c >> 8) & 255) / 255.0f;
    out[2] = (c & 255) / 255.0f;
    out[3] = ((c >> 24) & 255) / 255.0f;
}

BaseTexture* AsBase(IDirect3DBaseTexture9* t);
Surface* AsSurface(IDirect3DSurface9* s);

} // namespace d3d9web
