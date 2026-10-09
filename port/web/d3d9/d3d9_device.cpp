// The D3D9 device on WebGL2 (see d3d9_web.h): state, state blocks, resources, render targets,
// clears, draws and Present.
#include "d3d9_web.h"
#include "ran_green.h"
#include <emscripten.h>
#include <emscripten/html5.h>
#include <algorithm>
#include <cmath>
#include <cstdarg>
#include <cstdio>

namespace d3d9web {

namespace {

inline DWORD FloatBits(float f) { DWORD d; std::memcpy(&d, &f, 4); return d; }

void Identity(D3DMATRIX& m)
{
    std::memset(&m, 0, sizeof(m));
    m._11 = m._22 = m._33 = m._44 = 1.0f;
}

template <class T> void Assign(T*& slot, T* v)
{
    if (slot == v) return;
    if (v) v->AddRef();
    if (slot) slot->Release();
    slot = v;
}

GLenum GlCompare(DWORD f)
{
    if (f < 1 || f > 8) return GL_ALWAYS;
    return GL_NEVER + (f - 1);   // NEVER LESS EQUAL LEQUAL GREATER NOTEQUAL GEQUAL ALWAYS, same order
}

GLenum GlBlend(DWORD b, bool rtHasAlpha)
{
    switch (b) {
    case D3DBLEND_ZERO: return GL_ZERO;
    case D3DBLEND_ONE: return GL_ONE;
    case D3DBLEND_SRCCOLOR: return GL_SRC_COLOR;
    case D3DBLEND_INVSRCCOLOR: return GL_ONE_MINUS_SRC_COLOR;
    case D3DBLEND_SRCALPHA: return GL_SRC_ALPHA;
    case D3DBLEND_INVSRCALPHA: return GL_ONE_MINUS_SRC_ALPHA;
    // A target without alpha (X8R8G8B8 back buffer) reads destination alpha as 1.
    case D3DBLEND_DESTALPHA: return rtHasAlpha ? GL_DST_ALPHA : GL_ONE;
    case D3DBLEND_INVDESTALPHA: return rtHasAlpha ? GL_ONE_MINUS_DST_ALPHA : GL_ZERO;
    case D3DBLEND_DESTCOLOR: return GL_DST_COLOR;
    case D3DBLEND_INVDESTCOLOR: return GL_ONE_MINUS_DST_COLOR;
    case D3DBLEND_SRCALPHASAT: return GL_SRC_ALPHA_SATURATE;
    case D3DBLEND_BLENDFACTOR: return GL_CONSTANT_COLOR;
    case D3DBLEND_INVBLENDFACTOR: return GL_ONE_MINUS_CONSTANT_COLOR;
    default: return GL_ONE;
    }
}

GLenum GlBlendOp(DWORD op)
{
    switch (op) {
    case D3DBLENDOP_SUBTRACT: return GL_FUNC_SUBTRACT;
    case D3DBLENDOP_REVSUBTRACT: return GL_FUNC_REVERSE_SUBTRACT;
    case D3DBLENDOP_MIN: return GL_MIN;
    case D3DBLENDOP_MAX: return GL_MAX;
    default: return GL_FUNC_ADD;
    }
}

GLenum GlAddress(DWORD a)
{
    switch (a) {
    case D3DTADDRESS_MIRROR: case D3DTADDRESS_MIRRORONCE: return GL_MIRRORED_REPEAT;
    case D3DTADDRESS_CLAMP: case D3DTADDRESS_BORDER: return GL_CLAMP_TO_EDGE;
    default: return GL_REPEAT;
    }
}

bool FormatHasAlpha(D3DFORMAT f)
{
    return f == D3DFMT_A8R8G8B8 || f == D3DFMT_A1R5G5B5 || f == D3DFMT_A4R4G4B4 || f == D3DFMT_A8B8G8R8;
}

GLenum GlPrimitive(D3DPRIMITIVETYPE t, UINT count, GLsizei& n)
{
    switch (t) {
    case D3DPT_POINTLIST: n = (GLsizei)count; return GL_POINTS;
    case D3DPT_LINELIST: n = (GLsizei)count * 2; return GL_LINES;
    case D3DPT_LINESTRIP: n = (GLsizei)count + 1; return GL_LINE_STRIP;
    case D3DPT_TRIANGLELIST: n = (GLsizei)count * 3; return GL_TRIANGLES;
    case D3DPT_TRIANGLESTRIP: n = (GLsizei)count + 2; return GL_TRIANGLE_STRIP;
    case D3DPT_TRIANGLEFAN: n = (GLsizei)count + 2; return GL_TRIANGLE_FAN;
    default: n = 0; return GL_TRIANGLES;
    }
}

bool g_traceGl = false;

} // namespace

// ?gltrace: one frame in every 600 is logged call by call (render targets, clears, draws).
bool Device::LogFrame() const { return g_traceGl && m_frames % 600 == 300; }

void Device::FrameLog(const char* fmt, ...)
{
    char line[512];
    va_list ap;
    va_start(ap, fmt);
    std::vsnprintf(line, sizeof(line), fmt, ap);
    va_end(ap);
    m_frameLog += "  ";
    m_frameLog += line;
    m_frameLog += '\n';
}

// ---- construction ----------------------------------------------------------------------------

Device::Device(Direct3D* d3d, HWND window, DWORD behavior, D3DPRESENT_PARAMETERS* pp, int glContext)
    : m_d3d(d3d), m_window(window), m_behavior(behavior), m_context(glContext)
{
    m_pp = *pp;
    g_traceGl = EM_ASM_INT({ return (typeof location !== 'undefined' && location.search.indexOf('gltrace') >= 0) ? 1 : 0; }) != 0;
    InitGl();

    State& s = m_state;
    DWORD* rs = s.rs;
    rs[D3DRS_ZENABLE] = m_pp.EnableAutoDepthStencil ? D3DZB_TRUE : D3DZB_FALSE;
    rs[D3DRS_FILLMODE] = D3DFILL_SOLID;
    rs[D3DRS_SHADEMODE] = D3DSHADE_GOURAUD;
    rs[D3DRS_ZWRITEENABLE] = TRUE;
    rs[D3DRS_LASTPIXEL] = TRUE;
    rs[D3DRS_SRCBLEND] = D3DBLEND_ONE;
    rs[D3DRS_DESTBLEND] = D3DBLEND_ZERO;
    rs[D3DRS_CULLMODE] = D3DCULL_CCW;
    rs[D3DRS_ZFUNC] = D3DCMP_LESSEQUAL;
    rs[D3DRS_ALPHAFUNC] = D3DCMP_ALWAYS;
    rs[D3DRS_FOGEND] = FloatBits(1.0f);
    rs[D3DRS_FOGDENSITY] = FloatBits(1.0f);
    rs[D3DRS_STENCILFAIL] = rs[D3DRS_STENCILZFAIL] = rs[D3DRS_STENCILPASS] = D3DSTENCILOP_KEEP;
    rs[D3DRS_STENCILFUNC] = D3DCMP_ALWAYS;
    rs[D3DRS_STENCILMASK] = rs[D3DRS_STENCILWRITEMASK] = 0xFFFFFFFF;
    rs[D3DRS_TEXTUREFACTOR] = 0xFFFFFFFF;
    rs[D3DRS_CLIPPING] = TRUE;
    rs[D3DRS_LIGHTING] = TRUE;
    rs[D3DRS_COLORVERTEX] = TRUE;
    rs[D3DRS_LOCALVIEWER] = TRUE;
    rs[D3DRS_DIFFUSEMATERIALSOURCE] = D3DMCS_COLOR1;
    rs[D3DRS_SPECULARMATERIALSOURCE] = D3DMCS_COLOR2;
    rs[D3DRS_AMBIENTMATERIALSOURCE] = D3DMCS_MATERIAL;
    rs[D3DRS_EMISSIVEMATERIALSOURCE] = D3DMCS_MATERIAL;
    rs[D3DRS_POINTSIZE] = FloatBits(1.0f);
    rs[D3DRS_POINTSIZE_MIN] = FloatBits(1.0f);
    rs[D3DRS_POINTSCALE_A] = FloatBits(1.0f);
    rs[D3DRS_MULTISAMPLEANTIALIAS] = TRUE;
    rs[D3DRS_MULTISAMPLEMASK] = 0xFFFFFFFF;
    rs[D3DRS_POINTSIZE_MAX] = FloatBits(64.0f);
    rs[D3DRS_COLORWRITEENABLE] = rs[D3DRS_COLORWRITEENABLE1] = rs[D3DRS_COLORWRITEENABLE2] = rs[D3DRS_COLORWRITEENABLE3] = 0xF;
    rs[D3DRS_BLENDOP] = rs[D3DRS_BLENDOPALPHA] = D3DBLENDOP_ADD;
    rs[D3DRS_BLENDFACTOR] = 0xFFFFFFFF;
    rs[D3DRS_SRCBLENDALPHA] = D3DBLEND_ONE;
    rs[D3DRS_DESTBLENDALPHA] = D3DBLEND_ZERO;
    rs[D3DRS_CCW_STENCILFAIL] = rs[D3DRS_CCW_STENCILZFAIL] = rs[D3DRS_CCW_STENCILPASS] = D3DSTENCILOP_KEEP;
    rs[D3DRS_CCW_STENCILFUNC] = D3DCMP_ALWAYS;
    for (int i = 0; i < kStages; ++i) {
        DWORD* t = s.tss[i];
        t[D3DTSS_COLOROP] = i == 0 ? D3DTOP_MODULATE : D3DTOP_DISABLE;
        t[D3DTSS_COLORARG1] = D3DTA_TEXTURE;
        t[D3DTSS_COLORARG2] = D3DTA_CURRENT;
        t[D3DTSS_ALPHAOP] = i == 0 ? D3DTOP_SELECTARG1 : D3DTOP_DISABLE;
        t[D3DTSS_ALPHAARG1] = D3DTA_TEXTURE;
        t[D3DTSS_ALPHAARG2] = D3DTA_CURRENT;
        t[D3DTSS_TEXCOORDINDEX] = (DWORD)i;
        t[D3DTSS_TEXTURETRANSFORMFLAGS] = D3DTTFF_DISABLE;
        t[D3DTSS_COLORARG0] = t[D3DTSS_ALPHAARG0] = t[D3DTSS_RESULTARG] = D3DTA_CURRENT;
        DWORD* sp = s.samp[i];
        sp[D3DSAMP_ADDRESSU] = sp[D3DSAMP_ADDRESSV] = sp[D3DSAMP_ADDRESSW] = D3DTADDRESS_WRAP;
        sp[D3DSAMP_MAGFILTER] = sp[D3DSAMP_MINFILTER] = D3DTEXF_POINT;
        sp[D3DSAMP_MIPFILTER] = D3DTEXF_NONE;
        sp[D3DSAMP_MAXANISOTROPY] = 1;
    }
    for (D3DMATRIX& m : s.transforms) Identity(m);
    s.viewport = { 0, 0, m_pp.BackBufferWidth, m_pp.BackBufferHeight, 0.0f, 1.0f };
}

Device::~Device() {}

void Device::InitGl()
{
    m_hasS3tc = HasS3tc();
    const EMSCRIPTEN_WEBGL_CONTEXT_HANDLE ctx = emscripten_webgl_get_current_context();
    m_hasAniso = ctx && emscripten_webgl_enable_extension(ctx, "EXT_texture_filter_anisotropic");
    glGenBuffers(1, &m_ubo);
    glBindBuffer(GL_UNIFORM_BUFFER, m_ubo);
    glBufferData(GL_UNIFORM_BUFFER, sizeof(FfpConstants), nullptr, GL_DYNAMIC_DRAW);
    glBindBufferBase(GL_UNIFORM_BUFFER, 0, m_ubo);
    glGenBuffers(1, &m_upVertices);
    BindArrayBuffer(m_upVertices);
    glBufferData(GL_ARRAY_BUFFER, kUpVertexSize, nullptr, GL_STREAM_DRAW);
    glGenBuffers(1, &m_upIndices);
    BindElementBuffer(m_upIndices);
    glBufferData(GL_ELEMENT_ARRAY_BUFFER, kUpIndexSize, nullptr, GL_STREAM_DRAW);
    // D3D decides winding in screen space (y down). With the clip-space y negation, GL's window
    // coordinates equal D3D's screen coordinates numerically; GL reads them y-up, so D3D's
    // clockwise front faces are counter-clockwise here.
    glFrontFace(GL_CCW);
    glDisable(GL_STENCIL_TEST);
    glDisable(GL_DITHER);
    CreateBackBuffer();
    std::printf("[d3d9] WebGL2 device %ux%u, S3TC %s, anisotropy %s\n", m_pp.BackBufferWidth, m_pp.BackBufferHeight,
                m_hasS3tc ? "yes" : "no (decoded)", m_hasAniso ? "yes" : "no");
}

void Device::CreateBackBuffer()
{
    if (m_pp.BackBufferWidth == 0 || m_pp.BackBufferHeight == 0) {
        int w = 0, h = 0;
        emscripten_get_canvas_element_size("#canvas", &w, &h);
        m_pp.BackBufferWidth = (UINT)std::max(w, 1);
        m_pp.BackBufferHeight = (UINT)std::max(h, 1);
    }
    if (m_pp.BackBufferFormat == D3DFMT_UNKNOWN) m_pp.BackBufferFormat = D3DFMT_X8R8G8B8;
    const UINT w = m_pp.BackBufferWidth, h = m_pp.BackBufferHeight;
    Assign(m_rt, (Surface*)nullptr);
    Assign(m_ds, (Surface*)nullptr);
    if (m_backBuffer) m_backBuffer->Release();
    if (m_autoDepth) m_autoDepth->Release();
    m_backBuffer = new Surface(this, Surface::Kind::BackBuffer, w, h, m_pp.BackBufferFormat, D3DPOOL_DEFAULT, D3DUSAGE_RENDERTARGET);
    m_autoDepth = nullptr;
    if (m_pp.EnableAutoDepthStencil) {
        const D3DFORMAT df = m_pp.AutoDepthStencilFormat ? m_pp.AutoDepthStencilFormat : D3DFMT_D24S8;
        m_autoDepth = new Surface(this, Surface::Kind::DepthStencil, w, h, df, D3DPOOL_DEFAULT, D3DUSAGE_DEPTHSTENCIL);
    }
    Assign(m_rt, m_backBuffer);
    Assign(m_ds, m_autoDepth);
    m_fboDirty = true;
    // The canvas is the swap chain: its drawing buffer takes the back buffer size.
    emscripten_set_canvas_element_size("#canvas", (int)w, (int)h);
}

// ---- GL helpers ------------------------------------------------------------------------------

void Device::BindArrayBuffer(GLuint b)
{
    if (m_gl.arrayBuffer == b) return;
    glBindBuffer(GL_ARRAY_BUFFER, b);
    m_gl.arrayBuffer = b;
}

void Device::BindElementBuffer(GLuint b)
{
    if (m_gl.elementBuffer == b) return;
    glBindBuffer(GL_ELEMENT_ARRAY_BUFFER, b);
    m_gl.elementBuffer = b;
}

void Device::BindTextureForUpload(GLenum target, GLuint tex)
{
    // The last unit is reserved for uploads, so stage bindings stay valid.
    const int unit = kStages;
    if (m_gl.activeUnit != unit) { glActiveTexture(GL_TEXTURE0 + unit); m_gl.activeUnit = unit; }
    if (tex) glBindTexture(target, tex);
    // Callers may also have bound framebuffers (read-backs): rebind lazily.
    m_fboDirty = true;
    m_gl.fbo = ~0u;
}

void Device::ForgetTexture(GLuint tex)
{
    for (int i = 0; i < kStages; ++i)
        if (m_gl.textures[i] == tex) m_gl.textures[i] = 0;
    for (auto it = m_fbos.begin(); it != m_fbos.end();) {
        Surface* c = it->first.first;
        if (c && c->m_container && c->m_container->m_gl == tex) {
            glDeleteFramebuffers(1, &it->second);
            if (m_gl.fbo == it->second) m_gl.fbo = ~0u;
            it = m_fbos.erase(it);
        } else {
            ++it;
        }
    }
}

void Device::ForgetBuffer(GLuint buf)
{
    if (m_gl.arrayBuffer == buf) m_gl.arrayBuffer = ~0u;
    if (m_gl.elementBuffer == buf) m_gl.elementBuffer = ~0u;
    for (auto& a : m_gl.attribs)
        if (a.buffer == buf) a.buffer = ~0u;
}

void Device::ForgetSurface(Surface* s)
{
    for (auto it = m_fbos.begin(); it != m_fbos.end();) {
        if (it->first.first == s || it->first.second == s) {
            glDeleteFramebuffers(1, &it->second);
            if (m_gl.fbo == it->second) m_gl.fbo = ~0u;
            it = m_fbos.erase(it);
        } else {
            ++it;
        }
    }
    if (m_rt == s) { m_rt = nullptr; m_fboDirty = true; }
    if (m_ds == s) { m_ds = nullptr; m_fboDirty = true; }
}

GLuint Device::FramebufferFor(Surface* color, Surface* depth)
{
    auto key = std::make_pair(color, depth);
    auto it = m_fbos.find(key);
    if (it != m_fbos.end()) return it->second;
    GLuint fbo = 0;
    glGenFramebuffers(1, &fbo);
    glBindFramebuffer(GL_FRAMEBUFFER, fbo);
    m_gl.fbo = fbo;
    if (color) {
        if (color->m_kind == Surface::Kind::TextureLevel && color->m_container) {
            color->m_container->Flush();
            glFramebufferTexture2D(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0, color->m_face, color->m_container->m_gl, (GLint)color->m_level);
        } else if (color->m_renderbuffer) {
            glFramebufferRenderbuffer(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0, GL_RENDERBUFFER, color->m_renderbuffer);
        }
    }
    if (depth && depth->m_renderbuffer)
        glFramebufferRenderbuffer(GL_FRAMEBUFFER, GL_DEPTH_STENCIL_ATTACHMENT, GL_RENDERBUFFER, depth->m_renderbuffer);
    const GLenum status = glCheckFramebufferStatus(GL_FRAMEBUFFER);
    if (status != GL_FRAMEBUFFER_COMPLETE)
        std::printf("[d3d9] incomplete framebuffer 0x%x (color %ux%u, depth %ux%u)\n", status,
                    color ? color->m_width : 0, color ? color->m_height : 0, depth ? depth->m_width : 0, depth ? depth->m_height : 0);
    m_fbos[key] = fbo;
    return fbo;
}

void Device::BindFramebuffer()
{
    if (!m_fboDirty && m_gl.fbo != ~0u) return;
    m_fboDirty = false;
    // A depth buffer smaller than the target would clip rendering: leave it out then.
    Surface* depth = m_ds;
    if (depth && m_rt && (depth->m_width < m_rt->m_width || depth->m_height < m_rt->m_height)) depth = nullptr;
    const GLuint fbo = FramebufferFor(m_rt, depth);
    if (m_gl.fbo != fbo) { glBindFramebuffer(GL_FRAMEBUFFER, fbo); m_gl.fbo = fbo; }
    m_boundDepth = depth != nullptr;
}

void Device::ApplyViewport()
{
    const D3DVIEWPORT9& vp = m_state.viewport;
    const GLint v[4] = { (GLint)vp.X, (GLint)vp.Y, (GLint)vp.Width, (GLint)vp.Height };
    if (std::memcmp(v, m_gl.viewport, sizeof(v)) != 0) {
        glViewport(v[0], v[1], v[2], v[3]);
        std::memcpy(m_gl.viewport, v, sizeof(v));
    }
    if (m_gl.depthRange[0] != vp.MinZ || m_gl.depthRange[1] != vp.MaxZ) {
        glDepthRangef(vp.MinZ, vp.MaxZ);
        m_gl.depthRange[0] = vp.MinZ;
        m_gl.depthRange[1] = vp.MaxZ;
    }
}

static void SetCap(GLenum cap, bool on, int& cached)
{
    if (cached == (int)on) return;
    if (on) glEnable(cap); else glDisable(cap);
    cached = on;
}

void Device::ApplyRasterState()
{
    const DWORD* rs = m_state.rs;
    const bool rtAlpha = m_rt ? FormatHasAlpha(m_rt->m_format) : false;
    // blending
    const bool blend = rs[D3DRS_ALPHABLENDENABLE] != 0;
    SetCap(GL_BLEND, blend, m_gl.blend);
    if (blend) {
        DWORD sb = rs[D3DRS_SRCBLEND], db = rs[D3DRS_DESTBLEND];
        if (sb == D3DBLEND_BOTHSRCALPHA) { sb = D3DBLEND_SRCALPHA; db = D3DBLEND_INVSRCALPHA; }
        if (sb == D3DBLEND_BOTHINVSRCALPHA) { sb = D3DBLEND_INVSRCALPHA; db = D3DBLEND_SRCALPHA; }
        DWORD sa = sb, da = db, opA = rs[D3DRS_BLENDOP];
        if (rs[D3DRS_SEPARATEALPHABLENDENABLE]) { sa = rs[D3DRS_SRCBLENDALPHA]; da = rs[D3DRS_DESTBLENDALPHA]; opA = rs[D3DRS_BLENDOPALPHA]; }
        const GLenum s1 = GlBlend(sb, rtAlpha), d1 = GlBlend(db, rtAlpha), s2 = GlBlend(sa, rtAlpha), d2 = GlBlend(da, rtAlpha);
        if (s1 != m_gl.blendSrc || d1 != m_gl.blendDst || s2 != m_gl.blendSrcA || d2 != m_gl.blendDstA) {
            glBlendFuncSeparate(s1, d1, s2, d2);
            m_gl.blendSrc = s1; m_gl.blendDst = d1; m_gl.blendSrcA = s2; m_gl.blendDstA = d2;
        }
        const GLenum e1 = GlBlendOp(rs[D3DRS_BLENDOP]), e2 = GlBlendOp(opA);
        if (e1 != m_gl.blendEq || e2 != m_gl.blendEqA) {
            glBlendEquationSeparate(e1, e2);
            m_gl.blendEq = e1; m_gl.blendEqA = e2;
        }
        if (rs[D3DRS_BLENDFACTOR] != m_gl.blendColor) {
            float c[4];
            ColorToFloats(rs[D3DRS_BLENDFACTOR], c);
            glBlendColor(c[0], c[1], c[2], c[3]);
            m_gl.blendColor = rs[D3DRS_BLENDFACTOR];
        }
    }
    // depth
    const bool depthTest = rs[D3DRS_ZENABLE] != D3DZB_FALSE && m_boundDepth;
    SetCap(GL_DEPTH_TEST, depthTest, m_gl.depthTest);
    const int depthMask = (depthTest && rs[D3DRS_ZWRITEENABLE]) ? 1 : 0;
    if (m_gl.depthMask != depthMask) { glDepthMask(depthMask ? GL_TRUE : GL_FALSE); m_gl.depthMask = depthMask; }
    const GLenum zf = GlCompare(rs[D3DRS_ZFUNC]);
    if (depthTest && zf != m_gl.depthFunc) { glDepthFunc(zf); m_gl.depthFunc = zf; }
    // culling (front faces are D3D's clockwise; see InitGl)
    const DWORD cull = rs[D3DRS_CULLMODE];
    SetCap(GL_CULL_FACE, cull == D3DCULL_CW || cull == D3DCULL_CCW, m_gl.cull);
    const GLenum face = cull == D3DCULL_CW ? GL_FRONT : GL_BACK;
    if ((cull == D3DCULL_CW || cull == D3DCULL_CCW) && face != m_gl.cullFace) { glCullFace(face); m_gl.cullFace = face; }
    // colour write mask
    const int mask = (int)(rs[D3DRS_COLORWRITEENABLE] & 0xF);
    if (mask != m_gl.colorMask) {
        glColorMask((mask & 1) != 0, (mask & 2) != 0, (mask & 4) != 0, (mask & 8) != 0);
        m_gl.colorMask = mask;
    }
    // depth bias: D3D's is in depth units, GL's units are the smallest resolvable difference.
    float bias, slope;
    std::memcpy(&bias, &rs[D3DRS_DEPTHBIAS], 4);
    std::memcpy(&slope, &rs[D3DRS_SLOPESCALEDEPTHBIAS], 4);
    const bool offset = bias != 0.0f || slope != 0.0f;
    SetCap(GL_POLYGON_OFFSET_FILL, offset, m_gl.polyOffset);
    if (offset && (m_gl.polyFactor != slope || m_gl.polyUnits != bias)) {
        glPolygonOffset(slope, bias * 16777215.0f);
        m_gl.polyFactor = slope;
        m_gl.polyUnits = bias;
    }
    // scissor
    const bool scissor = rs[D3DRS_SCISSORTESTENABLE] != 0;
    SetCap(GL_SCISSOR_TEST, scissor, m_gl.scissorTest);
    if (scissor) {
        const GLint r[4] = { m_scissor.left, m_scissor.top, m_scissor.right - m_scissor.left, m_scissor.bottom - m_scissor.top };
        if (std::memcmp(r, m_gl.scissor, sizeof(r)) != 0) { glScissor(r[0], r[1], r[2], r[3]); std::memcpy(m_gl.scissor, r, sizeof(r)); }
    }
}

GLuint Device::SamplerFor(int stage)
{
    const DWORD* sp = m_state.samp[stage];
    const std::vector<uint32_t> key = { sp[D3DSAMP_ADDRESSU], sp[D3DSAMP_ADDRESSV], sp[D3DSAMP_ADDRESSW],
                                        sp[D3DSAMP_MAGFILTER], sp[D3DSAMP_MINFILTER], sp[D3DSAMP_MIPFILTER],
                                        sp[D3DSAMP_MAXANISOTROPY], sp[D3DSAMP_MAXMIPLEVEL] };
    auto it = m_samplerObjects.find(key);
    if (it != m_samplerObjects.end()) return it->second;
    GLuint s = 0;
    glGenSamplers(1, &s);
    glSamplerParameteri(s, GL_TEXTURE_WRAP_S, (GLint)GlAddress(key[0]));
    glSamplerParameteri(s, GL_TEXTURE_WRAP_T, (GLint)GlAddress(key[1]));
    glSamplerParameteri(s, GL_TEXTURE_WRAP_R, (GLint)GlAddress(key[2]));
    const bool magLinear = key[3] == D3DTEXF_LINEAR || key[3] == D3DTEXF_ANISOTROPIC;
    const bool minLinear = key[4] == D3DTEXF_LINEAR || key[4] == D3DTEXF_ANISOTROPIC;
    glSamplerParameteri(s, GL_TEXTURE_MAG_FILTER, magLinear ? GL_LINEAR : GL_NEAREST);
    GLint minf;
    switch (key[5]) {
    case D3DTEXF_POINT: minf = minLinear ? GL_LINEAR_MIPMAP_NEAREST : GL_NEAREST_MIPMAP_NEAREST; break;
    case D3DTEXF_LINEAR: case D3DTEXF_ANISOTROPIC: minf = minLinear ? GL_LINEAR_MIPMAP_LINEAR : GL_NEAREST_MIPMAP_LINEAR; break;
    default: minf = minLinear ? GL_LINEAR : GL_NEAREST; break;
    }
    glSamplerParameteri(s, GL_TEXTURE_MIN_FILTER, minf);
    if (key[6] > 1 && m_hasAniso && (key[4] == D3DTEXF_ANISOTROPIC || key[3] == D3DTEXF_ANISOTROPIC))
        glSamplerParameterf(s, 0x84FE /*GL_TEXTURE_MAX_ANISOTROPY_EXT*/, (float)std::min<uint32_t>(key[6], 16));
    if (key[7]) glSamplerParameterf(s, GL_TEXTURE_MIN_LOD, (float)key[7]);
    m_samplerObjects[key] = s;
    return s;
}

// ---- program cache ---------------------------------------------------------------------------

static GLuint Compile(GLenum type, const std::string& src)
{
    const GLuint sh = glCreateShader(type);
    const char* p = src.c_str();
    glShaderSource(sh, 1, &p, nullptr);
    glCompileShader(sh);
    GLint ok = 0;
    glGetShaderiv(sh, GL_COMPILE_STATUS, &ok);
    if (!ok) {
        char log[2048] = {};
        glGetShaderInfoLog(sh, sizeof(log), nullptr, log);
        std::printf("[d3d9] shader compile failed: %s\n%s\n", log, src.c_str());
    }
    return sh;
}

const FfpProgram& Device::ProgramFor(const std::vector<uint32_t>& vsKey, const std::vector<uint32_t>& psKey)
{
    std::string key((const char*)vsKey.data(), vsKey.size() * 4);
    key.push_back('|');
    key.append((const char*)psKey.data(), psKey.size() * 4);
    auto it = m_programs.find(key);
    if (it != m_programs.end()) return it->second;
    FfpProgram p;
    const GLuint vs = Compile(GL_VERTEX_SHADER, GenerateVertexShader(vsKey));
    const GLuint fs = Compile(GL_FRAGMENT_SHADER, GenerateFragmentShader(psKey));
    p.program = glCreateProgram();
    glAttachShader(p.program, vs);
    glAttachShader(p.program, fs);
    static const char* const names[] = { "a_pos", "a_weights", "a_indices", "a_normal", "a_psize", "a_diffuse", "a_specular" };
    for (int i = 0; i < kAttrTex0; ++i) glBindAttribLocation(p.program, (GLuint)i, names[i]);
    for (int i = 0; i < kStages; ++i) glBindAttribLocation(p.program, (GLuint)(kAttrTex0 + i), ("a_tex" + std::to_string(i)).c_str());
    glLinkProgram(p.program);
    GLint ok = 0;
    glGetProgramiv(p.program, GL_LINK_STATUS, &ok);
    if (!ok) {
        char log[2048] = {};
        glGetProgramInfoLog(p.program, sizeof(log), nullptr, log);
        std::printf("[d3d9] program link failed: %s\n", log);
    }
    glDeleteShader(vs);
    glDeleteShader(fs);
    const GLuint block = glGetUniformBlockIndex(p.program, "Ffp");
    if (block != GL_INVALID_INDEX) glUniformBlockBinding(p.program, block, 0);
    glUseProgram(p.program);
    m_gl.program = p.program;
    for (int i = 0; i < kStages; ++i) {
        const GLint loc = glGetUniformLocation(p.program, ("u_tex" + std::to_string(i)).c_str());
        if (loc >= 0) glUniform1i(loc, i);
    }
    if (m_programs.size() % 50 == 0) std::printf("[d3d9] %zu fixed-function programs\n", m_programs.size() + 1);
    return m_programs.emplace(key, p).first->second;
}

// ---- drawing ---------------------------------------------------------------------------------

void Device::ApplyTextures(int stages)
{
    for (int i = 0; i < stages && i < kStages; ++i) {
        BaseTexture* t = AsBase(m_state.textures[i]);
        GLuint gl = 0;
        GLenum target = GL_TEXTURE_2D;
        if (t) {
            t->Flush();
            gl = t->m_gl;
            target = t->m_target;
        }
        if (!gl) continue;   // the program samples nothing for an empty stage
        if (m_gl.textures[i] != gl || m_gl.textureTargets[i] != target) {
            if (m_gl.activeUnit != i) { glActiveTexture(GL_TEXTURE0 + i); m_gl.activeUnit = i; }
            glBindTexture(target, gl);
            m_gl.textures[i] = gl;
            m_gl.textureTargets[i] = target;
        }
        const GLuint smp = SamplerFor(i);
        if (m_gl.samplers[i] != smp) { glBindSampler((GLuint)i, smp); m_gl.samplers[i] = smp; }
    }
}

void Device::SetupAttributes(const VertexLayout& l, GLuint vbo, uintptr_t base, UINT stride)
{
    for (int a = 0; a < kAttrTex0 + kStages; ++a) {
        GlCache::Attrib& c = m_gl.attribs[a];
        const VertexLayout::Elem& e = l.attr[a];
        if (!e.present) {
            if (c.enabled) { glDisableVertexAttribArray((GLuint)a); c.enabled = false; }
            continue;
        }
        if (!c.enabled) { glEnableVertexAttribArray((GLuint)a); c.enabled = true; }
        const uintptr_t off = base + e.offset;
        if (c.buffer != vbo || c.size != e.size || c.type != e.type || c.norm != e.norm || c.stride != (GLsizei)stride || c.offset != off) {
            BindArrayBuffer(vbo);
            glVertexAttribPointer((GLuint)a, e.size, e.type, e.norm ? GL_TRUE : GL_FALSE, (GLsizei)stride, (const void*)off);
            c.buffer = vbo; c.size = e.size; c.type = e.type; c.norm = e.norm; c.stride = (GLsizei)stride; c.offset = off;
        }
    }
}

bool Device::PrepareDraw(GLuint vbo, uintptr_t base, UINT stride)
{
    ++m_stats.draws;
    ++m_stats.byFvf[m_state.decl ? 0x3FF : (m_state.fvf & 0x3FE)];
    if (m_vs || m_ps) { ++m_stats.skipShader; return false; }   // shaders are never reported; nothing to run them with
    BindFramebuffer();
    ApplyRasterState();
    ApplyViewport();
    const VertexLayout layout = m_state.decl ? LayoutFromDecl(m_state.decl) : LayoutFromFvf(m_state.fvf);
    if (!layout.attr[kAttrPos].present) { ++m_stats.skipNoPos; return false; }
    if (m_rt != m_backBuffer) ++m_stats.offscreen;
    if (!stride) stride = layout.stride;
    BuildFfpKeys(m_state, layout, m_vsKey, m_psKey);
    if (LogFrame()) {
        const DWORD* rs = m_state.rs;
        FrameLog("Draw fvf %lx%s rhw %d tex0 %d op %lu/%lu light %lu fog %lu(%lx) z %lu/%lu blend %lu %lu>%lu cull %lu atest %lu vp %lux%lu+%lu+%lu",
                 (unsigned long)m_state.fvf, m_state.decl ? " decl" : "", layout.rhw ? 1 : 0, m_state.textures[0] ? 1 : 0,
                 (unsigned long)m_state.tss[0][D3DTSS_COLOROP], (unsigned long)m_state.tss[1][D3DTSS_COLOROP],
                 (unsigned long)rs[D3DRS_LIGHTING], (unsigned long)rs[D3DRS_FOGENABLE], (unsigned long)rs[D3DRS_FOGCOLOR],
                 (unsigned long)rs[D3DRS_ZENABLE], (unsigned long)rs[D3DRS_ZWRITEENABLE], (unsigned long)rs[D3DRS_ALPHABLENDENABLE],
                 (unsigned long)rs[D3DRS_SRCBLEND], (unsigned long)rs[D3DRS_DESTBLEND], (unsigned long)rs[D3DRS_CULLMODE],
                 (unsigned long)rs[D3DRS_ALPHATESTENABLE], (unsigned long)m_state.viewport.Width, (unsigned long)m_state.viewport.Height,
                 (unsigned long)m_state.viewport.X, (unsigned long)m_state.viewport.Y);
    }
    const FfpProgram& p = ProgramFor(m_vsKey, m_psKey);
    if (m_gl.program != p.program) { glUseProgram(p.program); m_gl.program = p.program; }
    FfpConstants c;
    FillFfpConstants(m_state, c);
    glBindBuffer(GL_UNIFORM_BUFFER, m_ubo);
    glBufferSubData(GL_UNIFORM_BUFFER, 0, sizeof(c), &c);
    ApplyTextures((int)(m_psKey[0] & 15));
    SetupAttributes(layout, vbo, base, stride);
    return true;
}

HRESULT Device::DrawPrimitive(D3DPRIMITIVETYPE type, UINT start, UINT count)
{
    VertexBuffer* vb = m_state.streams[0].buffer;
    if (!vb || !count) return D3D_OK;
    vb->Flush();
    if (!PrepareDraw(vb->m_gl, m_state.streams[0].offset, m_state.streams[0].stride)) return D3D_OK;
    GLsizei n;
    const GLenum mode = GlPrimitive(type, count, n);
    glDrawArrays(mode, (GLint)start, n);
    return D3D_OK;
}

HRESULT Device::DrawIndexedPrimitive(D3DPRIMITIVETYPE type, INT baseVertex, UINT, UINT, UINT startIndex, UINT count)
{
    VertexBuffer* vb = m_state.streams[0].buffer;
    IndexBuffer* ib = m_state.indices;
    ++m_stats.indexed;
    if (!vb || !ib || !count) {
        static int reported = 0;
        if (g_traceGl && reported++ < 10)
            std::printf("[d3d9] indexed draw dropped: vb %p ib %p count %u recording %p fvf %lx\n", (void*)vb, (void*)ib, count,
                        (void*)m_recording, (unsigned long)m_state.fvf);
        return D3D_OK;
    }
    vb->Flush();
    ib->Flush();
    const UINT stride = m_state.streams[0].stride;
    const intptr_t base = (intptr_t)m_state.streams[0].offset + (intptr_t)baseVertex * (intptr_t)stride;
    if (base < 0) return D3D_OK;
    if (!PrepareDraw(vb->m_gl, (uintptr_t)base, stride)) return D3D_OK;
    BindElementBuffer(ib->m_gl);
    GLsizei n;
    const GLenum mode = GlPrimitive(type, count, n);
    const bool i32 = ib->m_format == D3DFMT_INDEX32;
    if (LogFrame() && m_state.fvf == 0x112) {
        // Where the first vertex lands (CPU), and the buffers' state.
        const UINT idx = i32 ? ((const uint32_t*)ib->m_data.data())[startIndex] : ((const uint16_t*)ib->m_data.data())[startIndex];
        const float* p = (const float*)(vb->m_data.data() + base + (size_t)idx * stride);
        FfpConstants c;
        FillFfpConstants(m_state, c);
        float clip[4];
        for (int j = 0; j < 4; ++j) clip[j] = p[0] * c.wvp[j] + p[1] * c.wvp[4 + j] + p[2] * c.wvp[8 + j] + c.wvp[12 + j];
        FrameLog("  idx %u/%u v %.1f %.1f %.1f -> clip %.1f %.1f %.1f %.1f; vb %u bytes locks %d dirty %u-%u gl %u, ib locks %d, base %ld stride %u count %u",
                 idx, (unsigned)(ib->m_size / (i32 ? 4 : 2)), p[0], p[1], p[2], clip[0], clip[1], clip[2], clip[3], vb->m_size, vb->m_locks,
                 vb->m_dirtyLo, vb->m_dirtyHi, vb->m_gl, ib->m_locks, (long)base, stride, count);
        const DWORD* rs = m_state.rs;
        FrameLog("    fog %lu table %lu vertex %lu start %.1f end %.1f range %lu | blend %lu %lu>%lu op %lu | atest %lu func %lu ref %lu | colorwrite %lx | pskey %08x vskey %08x tf %08lx mat a %.2f",
                 (unsigned long)rs[D3DRS_FOGENABLE], (unsigned long)rs[D3DRS_FOGTABLEMODE], (unsigned long)rs[D3DRS_FOGVERTEXMODE],
                 c.fogParams[0], c.fogParams[1], (unsigned long)rs[D3DRS_RANGEFOGENABLE], (unsigned long)rs[D3DRS_ALPHABLENDENABLE],
                 (unsigned long)rs[D3DRS_SRCBLEND], (unsigned long)rs[D3DRS_DESTBLEND], (unsigned long)rs[D3DRS_BLENDOP],
                 (unsigned long)rs[D3DRS_ALPHATESTENABLE], (unsigned long)rs[D3DRS_ALPHAFUNC], (unsigned long)rs[D3DRS_ALPHAREF],
                 (unsigned long)rs[D3DRS_COLORWRITEENABLE], m_psKey[0], m_vsKey[0], (unsigned long)rs[D3DRS_TEXTUREFACTOR], m_state.material.Diffuse.a);
    }
    glDrawElements(mode, n, i32 ? GL_UNSIGNED_INT : GL_UNSIGNED_SHORT, (const void*)(uintptr_t)(startIndex * (i32 ? 4 : 2)));
    return D3D_OK;
}

uintptr_t Device::StreamUp(GLuint buf, UINT& pos, UINT size, const void* data, UINT bufSize, UINT align, GLenum target)
{
    pos = (pos + align - 1) / align * align;
    if (pos + size > bufSize) {
        glBufferData(target, bufSize, nullptr, GL_STREAM_DRAW);   // orphan: new storage
        pos = 0;
    }
    glBufferSubData(target, pos, size, data);
    const uintptr_t at = pos;
    pos += size;
    (void)buf;
    return at;
}

HRESULT Device::DrawPrimitiveUP(D3DPRIMITIVETYPE type, UINT count, const void* data, UINT stride)
{
    ++m_stats.up;
    if (!data || !count || !stride) return D3D_OK;
    GLsizei n;
    const GLenum mode = GlPrimitive(type, count, n);
    const UINT size = (UINT)n * stride;
    if (size > kUpVertexSize) return D3D_OK;
    BindArrayBuffer(m_upVertices);
    const uintptr_t at = StreamUp(m_upVertices, m_upVertexPos, size, data, kUpVertexSize, std::max(4u, stride), GL_ARRAY_BUFFER);
    // D3D9: the UP call leaves stream 0 unset.
    Assign(m_state.streams[0].buffer, (VertexBuffer*)nullptr);
    m_state.streams[0].offset = m_state.streams[0].stride = 0;
    if (!PrepareDraw(m_upVertices, 0, stride)) return D3D_OK;
    // Start the draw at the copied vertices (offsets in the attribute pointers stay 0-based).
    glDrawArrays(mode, (GLint)(at / stride), n);
    return D3D_OK;
}

HRESULT Device::DrawIndexedPrimitiveUP(D3DPRIMITIVETYPE type, UINT minIndex, UINT numVertices, UINT count,
                                       const void* indices, D3DFORMAT fmt, const void* data, UINT stride)
{
    if (!data || !indices || !count || !stride) return D3D_OK;
    GLsizei n;
    const GLenum mode = GlPrimitive(type, count, n);
    const UINT isize = fmt == D3DFMT_INDEX32 ? 4 : 2;
    const UINT vbytes = (minIndex + numVertices) * stride, ibytes = (UINT)n * isize;
    if (vbytes > kUpVertexSize || ibytes > kUpIndexSize) return D3D_OK;
    BindArrayBuffer(m_upVertices);
    const uintptr_t vat = StreamUp(m_upVertices, m_upVertexPos, vbytes, data, kUpVertexSize, std::max(4u, stride), GL_ARRAY_BUFFER);
    BindElementBuffer(m_upIndices);
    const uintptr_t iat = StreamUp(m_upIndices, m_upIndexPos, ibytes, indices, kUpIndexSize, 4, GL_ELEMENT_ARRAY_BUFFER);
    Assign(m_state.streams[0].buffer, (VertexBuffer*)nullptr);
    m_state.streams[0].offset = m_state.streams[0].stride = 0;
    Assign(m_state.indices, (IndexBuffer*)nullptr);
    if (!PrepareDraw(m_upVertices, vat, stride)) return D3D_OK;
    BindElementBuffer(m_upIndices);
    glDrawElements(mode, n, isize == 4 ? GL_UNSIGNED_INT : GL_UNSIGNED_SHORT, (const void*)iat);
    return D3D_OK;
}

// ---- clears, copies, present -----------------------------------------------------------------

HRESULT Device::Clear(DWORD count, const D3DRECT* rects, DWORD flags, D3DCOLOR color, float z, DWORD stencil)
{
    if (LogFrame()) FrameLog("Clear flags %lx color %08lx z %.2f rects %lu", (unsigned long)flags, (unsigned long)color, z, (unsigned long)count);
    BindFramebuffer();
    GLbitfield mask = 0;
    if (flags & D3DCLEAR_TARGET) {
        float c[4];
        ColorToFloats(color, c);
        glClearColor(c[0], c[1], c[2], c[3]);
        if (m_gl.colorMask != 0xF) { glColorMask(GL_TRUE, GL_TRUE, GL_TRUE, GL_TRUE); m_gl.colorMask = 0xF; }
        mask |= GL_COLOR_BUFFER_BIT;
    }
    if ((flags & D3DCLEAR_ZBUFFER) && m_boundDepth) {
        glClearDepthf(z);
        if (m_gl.depthMask != 1) { glDepthMask(GL_TRUE); m_gl.depthMask = 1; }
        mask |= GL_DEPTH_BUFFER_BIT;
    }
    if ((flags & D3DCLEAR_STENCIL) && m_boundDepth) {
        glClearStencil((GLint)stencil);
        glStencilMask(0xFF);
        mask |= GL_STENCIL_BUFFER_BIT;
    }
    if (!mask) return D3D_OK;
    // Clears cover the viewport (or the given rectangles inside it).
    SetCap(GL_SCISSOR_TEST, true, m_gl.scissorTest);
    const D3DVIEWPORT9& vp = m_state.viewport;
    auto clearRect = [&](LONG l, LONG t, LONG r, LONG b) {
        l = std::max<LONG>(l, (LONG)vp.X);
        t = std::max<LONG>(t, (LONG)vp.Y);
        r = std::min<LONG>(r, (LONG)(vp.X + vp.Width));
        b = std::min<LONG>(b, (LONG)(vp.Y + vp.Height));
        if (r <= l || b <= t) return;
        glScissor(l, t, r - l, b - t);
        glClear(mask);
    };
    if (count && rects) for (DWORD i = 0; i < count; ++i) clearRect(rects[i].x1, rects[i].y1, rects[i].x2, rects[i].y2);
    else clearRect((LONG)vp.X, (LONG)vp.Y, (LONG)(vp.X + vp.Width), (LONG)(vp.Y + vp.Height));
    m_gl.scissor[0] = -1;   // the scissor rectangle changed under the cache
    m_rasterDirty = true;
    return D3D_OK;
}

HRESULT Device::Present(const RECT*, const RECT*, HWND, const RGNDATA*)
{
    // Back buffer -> canvas, flipped (the back buffer is stored top row first).
    const GLuint src = FramebufferFor(m_backBuffer, nullptr);
    int cw = 0, ch = 0;
    emscripten_get_canvas_element_size("#canvas", &cw, &ch);
    glBindFramebuffer(GL_READ_FRAMEBUFFER, src);
    glBindFramebuffer(GL_DRAW_FRAMEBUFFER, 0);
    SetCap(GL_SCISSOR_TEST, false, m_gl.scissorTest);
    const GLint w = (GLint)m_backBuffer->m_width, h = (GLint)m_backBuffer->m_height;
    glBlitFramebuffer(0, 0, w, h, 0, ch, cw, 0, GL_COLOR_BUFFER_BIT, (cw == w && ch == h) ? GL_NEAREST : GL_LINEAR);
    glBindFramebuffer(GL_FRAMEBUFFER, 0);
    m_gl.fbo = ~0u;
    m_fboDirty = true;
    if (!m_frameLog.empty()) {
        std::printf("[d3d9] frame %u log:\n%s", m_frames, m_frameLog.c_str());
        m_frameLog.clear();
    }
    ++m_frames;
    if (g_traceGl && m_frames % 120 == 0) {
        std::printf("[d3d9] frame %u: %u draws (%u skipped: shader %u, no position %u), %u offscreen, %u indexed, %u UP, fvf %#x\n",
                    m_frames, m_stats.draws, m_stats.skipShader + m_stats.skipNoPos, m_stats.skipShader, m_stats.skipNoPos,
                    m_stats.offscreen, m_stats.indexed, m_stats.up, (unsigned)m_state.fvf);
        std::string hist;
        for (unsigned f = 0; f < 0x400; ++f)
            if (m_stats.byFvf[f]) { char b[32]; std::snprintf(b, sizeof(b), " %x:%u", f, m_stats.byFvf[f]); hist += b; }
        std::printf("[d3d9]   draws by fvf (this frame):%s\n", hist.c_str());
    }
    m_stats = {};
    // The browser shows the canvas once this task ends: wait for the next frame.
    ran_green::WaitFrame();
    return D3D_OK;
}

HRESULT Device::StretchRect(IDirect3DSurface9* srcI, const RECT* srcRect, IDirect3DSurface9* dstI, const RECT* dstRect, D3DTEXTUREFILTERTYPE filter)
{
    Surface* src = AsSurface(srcI);
    Surface* dst = AsSurface(dstI);
    if (!src || !dst) return D3DERR_INVALIDCALL;
    if (LogFrame()) FrameLog("StretchRect %ux%u -> %ux%u", src->m_width, src->m_height, dst->m_width, dst->m_height);
    const RECT sr = srcRect ? *srcRect : RECT{ 0, 0, (LONG)src->m_width, (LONG)src->m_height };
    const RECT dr = dstRect ? *dstRect : RECT{ 0, 0, (LONG)dst->m_width, (LONG)dst->m_height };
    if (!src->IsRenderTarget()) {
        // CPU surface -> texture: copy the bytes (same size only, like UpdateSurface).
        POINT p = { dr.left, dr.top };
        return UpdateSurface(srcI, &sr, dstI, &p);
    }
    if (!dst->IsRenderTarget() && dst->m_kind != Surface::Kind::TextureLevel) return D3DERR_INVALIDCALL;
    const GLuint rf = FramebufferFor(src, nullptr);
    const GLuint df = FramebufferFor(dst, nullptr);
    glBindFramebuffer(GL_READ_FRAMEBUFFER, rf);
    glBindFramebuffer(GL_DRAW_FRAMEBUFFER, df);
    SetCap(GL_SCISSOR_TEST, false, m_gl.scissorTest);
    glBlitFramebuffer(sr.left, sr.top, sr.right, sr.bottom, dr.left, dr.top, dr.right, dr.bottom, GL_COLOR_BUFFER_BIT,
                      filter == D3DTEXF_LINEAR ? GL_LINEAR : GL_NEAREST);
    glBindFramebuffer(GL_FRAMEBUFFER, 0);
    m_gl.fbo = ~0u;
    m_fboDirty = true;
    return D3D_OK;
}

HRESULT Device::UpdateSurface(IDirect3DSurface9* srcI, const RECT* srcRect, IDirect3DSurface9* dstI, const POINT* dstPoint)
{
    Surface* src = AsSurface(srcI);
    Surface* dst = AsSurface(dstI);
    if (!src || !dst || src->m_format != dst->m_format) return D3DERR_INVALIDCALL;
    const RECT sr = srcRect ? *srcRect : RECT{ 0, 0, (LONG)src->m_width, (LONG)src->m_height };
    const POINT dp = dstPoint ? *dstPoint : POINT{ 0, 0 };
    const FormatInfo* fi = GetFormatInfo(src->m_format);
    if (!fi) return D3DERR_INVALIDCALL;
    const UINT bw = fi->compressed ? 4 : 1, bpb = fi->compressed ? (UINT)fi->blockBytes : (UINT)fi->bytesPerPixel;
    const UINT rows = (UINT)(sr.bottom - sr.top + bw - 1) / bw, rowBytes = (UINT)(sr.right - sr.left + bw - 1) / bw * bpb;
    const uint8_t* s = src->Bytes() + (size_t)(sr.top / bw) * src->Pitch() + (size_t)(sr.left / bw) * bpb;
    uint8_t* d = dst->Bytes() + (size_t)(dp.y / (LONG)bw) * dst->Pitch() + (size_t)(dp.x / (LONG)bw) * bpb;
    for (UINT y = 0; y < rows; ++y) std::memcpy(d + (size_t)y * dst->Pitch(), s + (size_t)y * src->Pitch(), rowBytes);
    if (dst->m_container) {
        const UINT face = dst->m_face == GL_TEXTURE_2D ? 0 : (UINT)(dst->m_face - GL_TEXTURE_CUBE_MAP_POSITIVE_X);
        dst->m_container->MarkDirty(face * dst->m_container->m_levels + dst->m_level,
                                    RECT{ dp.x, dp.y, dp.x + (sr.right - sr.left), dp.y + (sr.bottom - sr.top) });
    }
    return D3D_OK;
}

HRESULT Device::UpdateTexture(IDirect3DBaseTexture9* srcI, IDirect3DBaseTexture9* dstI)
{
    BaseTexture* src = AsBase(srcI);
    BaseTexture* dst = AsBase(dstI);
    if (!src || !dst || src->m_format != dst->m_format || src->Faces() != dst->Faces()) return D3DERR_INVALIDCALL;
    // Levels are matched from the top (the source may have more).
    const UINT skip = src->m_levels > dst->m_levels ? src->m_levels - dst->m_levels : 0;
    for (UINT f = 0; f < dst->Faces(); ++f)
        for (UINT l = 0; l < dst->m_levels && l + skip < src->m_levels; ++l) {
            const UINT si = f * src->m_levels + l + skip, di = f * dst->m_levels + l;
            if (src->m_data[si].empty()) continue;
            dst->m_data[di] = src->m_data[si];
            dst->MarkDirty(di, RECT{ 0, 0, (LONG)dst->LevelW(l), (LONG)dst->LevelH(l) });
        }
    return D3D_OK;
}

HRESULT Device::GetRenderTargetData(IDirect3DSurface9* rtI, IDirect3DSurface9* dstI)
{
    Surface* rt = AsSurface(rtI);
    Surface* dst = AsSurface(dstI);
    if (!rt || !dst || !rt->IsRenderTarget()) return D3DERR_INVALIDCALL;
    const GLuint fbo = FramebufferFor(rt, nullptr);
    glBindFramebuffer(GL_FRAMEBUFFER, fbo);
    const UINT w = std::min(rt->m_width, dst->m_width), h = std::min(rt->m_height, dst->m_height);
    std::vector<uint8_t> rgba((size_t)w * h * 4);
    glPixelStorei(GL_PACK_ALIGNMENT, 1);
    glReadPixels(0, 0, (GLsizei)w, (GLsizei)h, GL_RGBA, GL_UNSIGNED_BYTE, rgba.data());
    m_gl.fbo = ~0u;
    m_fboDirty = true;
    StoreFromRgba(dst->m_format, rgba.data(), w, h, dst->Bytes(), dst->Pitch());
    if (dst->m_container) {
        const UINT face = dst->m_face == GL_TEXTURE_2D ? 0 : (UINT)(dst->m_face - GL_TEXTURE_CUBE_MAP_POSITIVE_X);
        dst->m_container->MarkDirty(face * dst->m_container->m_levels + dst->m_level, RECT{ 0, 0, (LONG)w, (LONG)h });
    }
    return D3D_OK;
}

HRESULT Device::GetFrontBufferData(UINT, IDirect3DSurface9* dst)
{
    return GetRenderTargetData(m_backBuffer, dst);
}

HRESULT Device::ColorFill(IDirect3DSurface9* sI, const RECT* r, D3DCOLOR c)
{
    Surface* s = AsSurface(sI);
    if (!s) return D3DERR_INVALIDCALL;
    if (!s->IsRenderTarget()) return D3DERR_INVALIDCALL;
    const GLuint fbo = FramebufferFor(s, nullptr);
    glBindFramebuffer(GL_FRAMEBUFFER, fbo);
    m_gl.fbo = ~0u;
    m_fboDirty = true;
    float f[4];
    ColorToFloats(c, f);
    glClearColor(f[0], f[1], f[2], f[3]);
    if (m_gl.colorMask != 0xF) { glColorMask(GL_TRUE, GL_TRUE, GL_TRUE, GL_TRUE); m_gl.colorMask = 0xF; }
    const RECT rr = r ? *r : RECT{ 0, 0, (LONG)s->m_width, (LONG)s->m_height };
    SetCap(GL_SCISSOR_TEST, true, m_gl.scissorTest);
    glScissor(rr.left, rr.top, rr.right - rr.left, rr.bottom - rr.top);
    m_gl.scissor[0] = -1;
    glClear(GL_COLOR_BUFFER_BIT);
    return D3D_OK;
}

// ---- resources -------------------------------------------------------------------------------

HRESULT Device::CreateTexture(UINT w, UINT h, UINT levels, DWORD usage, D3DFORMAT fmt, D3DPOOL pool, IDirect3DTexture9** out, HANDLE*)
{
    if (!out) return D3DERR_INVALIDCALL;
    *out = nullptr;
    if (!GetFormatInfo(fmt)) { std::printf("[d3d9] CreateTexture: format %u not supported\n", (unsigned)fmt); return D3DERR_INVALIDCALL; }
    *out = new Texture(this, w, h, levels, usage, fmt, pool);
    return D3D_OK;
}

HRESULT Device::CreateCubeTexture(UINT edge, UINT levels, DWORD usage, D3DFORMAT fmt, D3DPOOL pool, IDirect3DCubeTexture9** out, HANDLE*)
{
    if (!out) return D3DERR_INVALIDCALL;
    *out = nullptr;
    if (!GetFormatInfo(fmt)) return D3DERR_INVALIDCALL;
    *out = new CubeTexture(this, edge, levels, usage, fmt, pool);
    return D3D_OK;
}

HRESULT Device::CreateVertexBuffer(UINT len, DWORD usage, DWORD fvf, D3DPOOL pool, IDirect3DVertexBuffer9** out, HANDLE*)
{
    if (!out || !len) return D3DERR_INVALIDCALL;
    *out = new VertexBuffer(this, len, usage, fvf, pool);
    return D3D_OK;
}

HRESULT Device::CreateIndexBuffer(UINT len, DWORD usage, D3DFORMAT fmt, D3DPOOL pool, IDirect3DIndexBuffer9** out, HANDLE*)
{
    if (!out || !len) return D3DERR_INVALIDCALL;
    *out = new IndexBuffer(this, len, usage, fmt, pool);
    return D3D_OK;
}

HRESULT Device::CreateRenderTarget(UINT w, UINT h, D3DFORMAT fmt, D3DMULTISAMPLE_TYPE, DWORD, WINBOOL, IDirect3DSurface9** out, HANDLE*)
{
    if (!out) return D3DERR_INVALIDCALL;
    // A standalone render target is a one-level render-target texture's surface.
    Texture* t = new Texture(this, w, h, 1, D3DUSAGE_RENDERTARGET, fmt, D3DPOOL_DEFAULT);
    t->GetSurfaceLevel(0, out);
    t->Release();   // the surface keeps the texture alive
    return D3D_OK;
}

HRESULT Device::CreateDepthStencilSurface(UINT w, UINT h, D3DFORMAT fmt, D3DMULTISAMPLE_TYPE, DWORD, WINBOOL, IDirect3DSurface9** out, HANDLE*)
{
    if (!out) return D3DERR_INVALIDCALL;
    *out = new Surface(this, Surface::Kind::DepthStencil, w, h, fmt, D3DPOOL_DEFAULT, D3DUSAGE_DEPTHSTENCIL);
    return D3D_OK;
}

HRESULT Device::CreateOffscreenPlainSurface(UINT w, UINT h, D3DFORMAT fmt, D3DPOOL pool, IDirect3DSurface9** out, HANDLE*)
{
    if (!out) return D3DERR_INVALIDCALL;
    *out = new Surface(this, Surface::Kind::Offscreen, w, h, fmt, pool, 0);
    return D3D_OK;
}

HRESULT Device::CreateVertexDeclaration(const D3DVERTEXELEMENT9* e, IDirect3DVertexDeclaration9** out)
{
    if (!out || !e) return D3DERR_INVALIDCALL;
    *out = new VertexDeclaration(this, e);
    return D3D_OK;
}

HRESULT Device::CreateVertexShader(const DWORD* code, IDirect3DVertexShader9** out)
{
    if (!out || !code) return D3DERR_INVALIDCALL;
    *out = new Shader(this, code);
    return D3D_OK;
}

HRESULT Device::CreatePixelShader(const DWORD* code, IDirect3DPixelShader9** out)
{
    if (!out || !code) return D3DERR_INVALIDCALL;
    *out = (IDirect3DPixelShader9*)(IDirect3DVertexShader9*)new Shader(this, code);
    return D3D_OK;
}

// ---- render targets --------------------------------------------------------------------------

HRESULT Device::GetBackBuffer(UINT, UINT, D3DBACKBUFFER_TYPE, IDirect3DSurface9** out)
{
    if (!out) return D3DERR_INVALIDCALL;
    *out = m_backBuffer;
    m_backBuffer->AddRef();
    return D3D_OK;
}

HRESULT Device::SetRenderTarget(DWORD index, IDirect3DSurface9* rtI)
{
    if (index != 0) return rtI ? D3DERR_INVALIDCALL : D3D_OK;
    Surface* rt = AsSurface(rtI);
    if (!rt) return D3DERR_INVALIDCALL;
    if (!rt->IsRenderTarget()) return D3DERR_INVALIDCALL;
    if (LogFrame()) FrameLog("RT %s %ux%u fmt %u", rt == m_backBuffer ? "backbuffer" : "texture", rt->m_width, rt->m_height, (unsigned)rt->m_format);
    Assign(m_rt, rt);
    m_fboDirty = true;
    // D3D9: a new render target resets the viewport to cover it.
    m_state.viewport = { 0, 0, rt->m_width, rt->m_height, 0.0f, 1.0f };
    return D3D_OK;
}

HRESULT Device::GetRenderTarget(DWORD index, IDirect3DSurface9** out)
{
    if (!out) return D3DERR_INVALIDCALL;
    *out = index == 0 ? m_rt : nullptr;
    if (!*out) return D3DERR_NOTFOUND;
    (*out)->AddRef();
    return D3D_OK;
}

HRESULT Device::SetDepthStencilSurface(IDirect3DSurface9* ds)
{
    Assign(m_ds, AsSurface(ds));
    m_fboDirty = true;
    return D3D_OK;
}

HRESULT Device::GetDepthStencilSurface(IDirect3DSurface9** out)
{
    if (!out) return D3DERR_INVALIDCALL;
    *out = m_ds;
    if (!m_ds) return D3DERR_NOTFOUND;
    m_ds->AddRef();
    return D3D_OK;
}

HRESULT Device::Reset(D3DPRESENT_PARAMETERS* pp)
{
    if (!pp) return D3DERR_INVALIDCALL;
    m_pp = *pp;
    CreateBackBuffer();
    pp->BackBufferWidth = m_pp.BackBufferWidth;
    pp->BackBufferHeight = m_pp.BackBufferHeight;
    pp->BackBufferFormat = m_pp.BackBufferFormat;
    m_state.viewport = { 0, 0, m_pp.BackBufferWidth, m_pp.BackBufferHeight, 0.0f, 1.0f };
    std::printf("[d3d9] reset %ux%u\n", m_pp.BackBufferWidth, m_pp.BackBufferHeight);
    return D3D_OK;
}

// ---- simple state ----------------------------------------------------------------------------

HRESULT Device::GetDirect3D(IDirect3D9** out)
{
    if (!out) return D3DERR_INVALIDCALL;
    *out = m_d3d;
    m_d3d->AddRef();
    return D3D_OK;
}

HRESULT Device::GetDeviceCaps(D3DCAPS9* caps)
{
    if (!caps) return D3DERR_INVALIDCALL;
    Direct3D::FillCaps(caps);
    return D3D_OK;
}

HRESULT Device::GetDisplayMode(UINT, D3DDISPLAYMODE* mode)
{
    return m_d3d->GetAdapterDisplayMode(0, mode);
}

HRESULT Device::GetCreationParameters(D3DDEVICE_CREATION_PARAMETERS* p)
{
    if (!p) return D3DERR_INVALIDCALL;
    p->AdapterOrdinal = 0;
    p->DeviceType = D3DDEVTYPE_HAL;
    p->hFocusWindow = m_window;
    p->BehaviorFlags = m_behavior;
    return D3D_OK;
}

void Device::GetGammaRamp(UINT, D3DGAMMARAMP* r)
{
    if (!r) return;
    for (int i = 0; i < 256; ++i) r->red[i] = r->green[i] = r->blue[i] = (WORD)(i * 257);
}

// State setters record into the state block being recorded instead of changing the device
// (D3D9's BeginStateBlock semantics).
State& Device::Target() { return m_recording ? m_recording->m_state : m_state; }

HRESULT Device::SetRenderState(D3DRENDERSTATETYPE s, DWORD v)
{
    if ((UINT)s >= kRenderStates) return D3DERR_INVALIDCALL;
    if (m_recording) m_recording->m_mask.rs[s] = true;
    Target().rs[s] = v;
    return D3D_OK;
}

HRESULT Device::GetRenderState(D3DRENDERSTATETYPE s, DWORD* v)
{
    if ((UINT)s >= kRenderStates || !v) return D3DERR_INVALIDCALL;
    *v = m_state.rs[s];
    return D3D_OK;
}

HRESULT Device::SetTextureStageState(DWORD stage, D3DTEXTURESTAGESTATETYPE t, DWORD v)
{
    if (stage >= (DWORD)kStages || (UINT)t >= (UINT)kStageStates) return D3DERR_INVALIDCALL;
    if (m_recording) m_recording->m_mask.tss[stage * kStageStates + t] = true;
    Target().tss[stage][t] = v;
    return D3D_OK;
}

HRESULT Device::GetTextureStageState(DWORD stage, D3DTEXTURESTAGESTATETYPE t, DWORD* v)
{
    if (stage >= (DWORD)kStages || (UINT)t >= (UINT)kStageStates || !v) return D3DERR_INVALIDCALL;
    *v = m_state.tss[stage][t];
    return D3D_OK;
}

HRESULT Device::SetSamplerState(DWORD sampler, D3DSAMPLERSTATETYPE t, DWORD v)
{
    if (sampler >= (DWORD)kStages || (UINT)t >= (UINT)kSamplerStates) return D3D_OK;   // displacement-map samplers: ignored
    if (m_recording) m_recording->m_mask.samp[sampler * kSamplerStates + t] = true;
    Target().samp[sampler][t] = v;
    return D3D_OK;
}

HRESULT Device::GetSamplerState(DWORD sampler, D3DSAMPLERSTATETYPE t, DWORD* v)
{
    if (sampler >= (DWORD)kStages || (UINT)t >= (UINT)kSamplerStates || !v) return D3DERR_INVALIDCALL;
    *v = m_state.samp[sampler][t];
    return D3D_OK;
}

HRESULT Device::SetTexture(DWORD stage, IDirect3DBaseTexture9* tex)
{
    if (stage >= D3DVERTEXTEXTURESAMPLER0) return D3D_OK;
    if (stage >= (DWORD)kStages) return D3DERR_INVALIDCALL;
    if (m_recording) m_recording->m_mask.textures[stage] = true;
    Assign(Target().textures[stage], tex);
    return D3D_OK;
}

HRESULT Device::GetTexture(DWORD stage, IDirect3DBaseTexture9** out)
{
    if (stage >= (DWORD)kStages || !out) return D3DERR_INVALIDCALL;
    *out = m_state.textures[stage];
    if (*out) (*out)->AddRef();
    return D3D_OK;
}

HRESULT Device::SetTransform(D3DTRANSFORMSTATETYPE t, const D3DMATRIX* m)
{
    if ((UINT)t >= 512 || !m) return D3DERR_INVALIDCALL;
    if (m_recording) m_recording->m_mask.transforms[t] = true;
    Target().transforms[t] = *m;
    return D3D_OK;
}

HRESULT Device::GetTransform(D3DTRANSFORMSTATETYPE t, D3DMATRIX* m)
{
    if ((UINT)t >= 512 || !m) return D3DERR_INVALIDCALL;
    *m = m_state.transforms[t];
    return D3D_OK;
}

HRESULT Device::MultiplyTransform(D3DTRANSFORMSTATETYPE t, const D3DMATRIX* m)
{
    if ((UINT)t >= 512 || !m) return D3DERR_INVALIDCALL;
    D3DMATRIX r;
    const D3DMATRIX& a = m_state.transforms[t];
    for (int i = 0; i < 4; ++i)
        for (int j = 0; j < 4; ++j)
            r.m[i][j] = a.m[i][0] * m->m[0][j] + a.m[i][1] * m->m[1][j] + a.m[i][2] * m->m[2][j] + a.m[i][3] * m->m[3][j];
    return SetTransform(t, &r);
}

HRESULT Device::SetViewport(const D3DVIEWPORT9* vp)
{
    if (!vp) return D3DERR_INVALIDCALL;
    if (m_recording) m_recording->m_mask.viewport = true;
    Target().viewport = *vp;
    return D3D_OK;
}

HRESULT Device::GetViewport(D3DVIEWPORT9* vp)
{
    if (!vp) return D3DERR_INVALIDCALL;
    *vp = m_state.viewport;
    return D3D_OK;
}

HRESULT Device::SetMaterial(const D3DMATERIAL9* m)
{
    if (!m) return D3DERR_INVALIDCALL;
    if (m_recording) m_recording->m_mask.material = true;
    Target().material = *m;
    return D3D_OK;
}

HRESULT Device::GetMaterial(D3DMATERIAL9* m)
{
    if (!m) return D3DERR_INVALIDCALL;
    *m = m_state.material;
    return D3D_OK;
}

HRESULT Device::SetLight(DWORD index, const D3DLIGHT9* l)
{
    if (!l) return D3DERR_INVALIDCALL;
    if (m_recording) m_recording->m_mask.lights[index] = true;
    LightSlot& slot = Target().lights[index];
    if (m_recording && !slot.set && m_state.lights.count(index)) slot.enabled = m_state.lights[index].enabled;
    slot.light = *l;
    slot.set = true;
    return D3D_OK;
}

HRESULT Device::GetLight(DWORD index, D3DLIGHT9* l)
{
    auto it = m_state.lights.find(index);
    if (!l || it == m_state.lights.end() || !it->second.set) return D3DERR_INVALIDCALL;
    *l = it->second.light;
    return D3D_OK;
}

HRESULT Device::LightEnable(DWORD index, WINBOOL enable)
{
    State& s = Target();
    if (m_recording) m_recording->m_mask.lights[index] = true;
    LightSlot& slot = s.lights[index];
    if (!slot.set) {
        // D3D9: enabling a light that was never set uses the default directional light.
        if (m_recording && m_state.lights.count(index) && m_state.lights[index].set) {
            slot.light = m_state.lights[index].light;
        } else {
            std::memset(&slot.light, 0, sizeof(slot.light));
            slot.light.Type = D3DLIGHT_DIRECTIONAL;
            slot.light.Diffuse = { 1, 1, 1, 0 };
            slot.light.Direction = { 0, 0, 1 };
        }
        slot.set = true;
    }
    slot.enabled = enable != FALSE;
    return D3D_OK;
}

HRESULT Device::GetLightEnable(DWORD index, WINBOOL* enable)
{
    auto it = m_state.lights.find(index);
    if (!enable || it == m_state.lights.end() || !it->second.set) return D3DERR_INVALIDCALL;
    *enable = it->second.enabled ? 128 : 0;   // D3D9 returns 128 for enabled lights
    return D3D_OK;
}

HRESULT Device::SetClipPlane(DWORD index, const float* plane)
{
    if (index >= 6 || !plane) return D3DERR_INVALIDCALL;
    if (m_recording) m_recording->m_mask.clipPlanes[index] = true;
    std::memcpy(Target().clipPlanes[index], plane, 16);
    return D3D_OK;
}

HRESULT Device::GetClipPlane(DWORD index, float* plane)
{
    if (index >= 6 || !plane) return D3DERR_INVALIDCALL;
    std::memcpy(plane, m_state.clipPlanes[index], 16);
    return D3D_OK;
}

HRESULT Device::SetFVF(DWORD fvf)
{
    if (m_recording) { m_recording->m_mask.fvf = true; m_recording->m_mask.decl = true; }
    Target().fvf = fvf;
    Assign(Target().decl, (VertexDeclaration*)nullptr);
    return D3D_OK;
}

HRESULT Device::GetFVF(DWORD* fvf)
{
    if (!fvf) return D3DERR_INVALIDCALL;
    *fvf = m_state.decl ? 0 : m_state.fvf;
    return D3D_OK;
}

HRESULT Device::SetVertexDeclaration(IDirect3DVertexDeclaration9* d)
{
    if (m_recording) { m_recording->m_mask.fvf = true; m_recording->m_mask.decl = true; }
    Assign(Target().decl, static_cast<VertexDeclaration*>(d));
    if (d) Target().fvf = 0;
    return D3D_OK;
}

HRESULT Device::GetVertexDeclaration(IDirect3DVertexDeclaration9** out)
{
    if (!out) return D3DERR_INVALIDCALL;
    *out = m_state.decl;
    if (*out) (*out)->AddRef();
    return D3D_OK;
}

HRESULT Device::SetStreamSource(UINT stream, IDirect3DVertexBuffer9* vb, UINT offset, UINT stride)
{
    if (stream != 0) return D3D_OK;   // the game uses stream 0 only
    if (m_recording) m_recording->m_mask.streams = true;
    StreamBinding& b = Target().streams[0];
    Assign(b.buffer, static_cast<VertexBuffer*>(vb));
    b.offset = offset;
    b.stride = stride;
    return D3D_OK;
}

HRESULT Device::GetStreamSource(UINT stream, IDirect3DVertexBuffer9** out, UINT* offset, UINT* stride)
{
    if (!out) return D3DERR_INVALIDCALL;
    const StreamBinding& b = m_state.streams[0];
    *out = stream == 0 ? b.buffer : nullptr;
    if (*out) (*out)->AddRef();
    if (offset) *offset = stream == 0 ? b.offset : 0;
    if (stride) *stride = stream == 0 ? b.stride : 0;
    return D3D_OK;
}

HRESULT Device::SetIndices(IDirect3DIndexBuffer9* ib)
{
    if (m_recording) m_recording->m_mask.indices = true;
    Assign(Target().indices, static_cast<IndexBuffer*>(ib));
    return D3D_OK;
}

HRESULT Device::GetIndices(IDirect3DIndexBuffer9** out)
{
    if (!out) return D3DERR_INVALIDCALL;
    *out = m_state.indices;
    if (*out) (*out)->AddRef();
    return D3D_OK;
}

HRESULT Device::SetVertexShader(IDirect3DVertexShader9* s)
{
    if (m_recording) m_recording->m_mask.vs = true;
    if (!m_recording) Assign(m_vs, s);
    return D3D_OK;
}

HRESULT Device::GetVertexShader(IDirect3DVertexShader9** out)
{
    if (!out) return D3DERR_INVALIDCALL;
    *out = m_vs;
    if (m_vs) m_vs->AddRef();
    return D3D_OK;
}

HRESULT Device::SetPixelShader(IDirect3DPixelShader9* s)
{
    if (m_recording) m_recording->m_mask.ps = true;
    if (!m_recording) Assign(m_ps, s);
    return D3D_OK;
}

HRESULT Device::GetPixelShader(IDirect3DPixelShader9** out)
{
    if (!out) return D3DERR_INVALIDCALL;
    *out = m_ps;
    if (m_ps) m_ps->AddRef();
    return D3D_OK;
}

// ---- state blocks ----------------------------------------------------------------------------

HRESULT Device::BeginStateBlock()
{
    if (m_recording) return D3DERR_INVALIDCALL;
    m_recording = new StateBlock(this);
    return D3D_OK;
}

HRESULT Device::EndStateBlock(IDirect3DStateBlock9** out)
{
    if (!m_recording || !out) return D3DERR_INVALIDCALL;
    *out = m_recording;
    m_recording = nullptr;
    return D3D_OK;
}

HRESULT Device::CreateStateBlock(D3DSTATEBLOCKTYPE type, IDirect3DStateBlock9** out)
{
    if (!out) return D3DERR_INVALIDCALL;
    StateBlock* b = new StateBlock(this);
    StateMask& m = b->m_mask;
    const bool all = type == D3DSBT_ALL;
    std::fill(m.rs.begin(), m.rs.end(), true);
    std::fill(m.tss.begin(), m.tss.end(), true);
    std::fill(m.samp.begin(), m.samp.end(), true);
    if (all) {
        std::fill(m.textures.begin(), m.textures.end(), true);
        std::fill(m.transforms.begin(), m.transforms.end(), true);
        std::fill(m.clipPlanes.begin(), m.clipPlanes.end(), true);
        m.material = m.viewport = m.streams = m.indices = m.decl = m.fvf = true;
        for (const auto& kv : m_state.lights) m.lights[kv.first] = true;
    }
    CaptureState(b->m_state, m);
    *out = b;
    return D3D_OK;
}

void Device::CaptureState(State& s, const StateMask& m)
{
    for (int i = 0; i < kRenderStates; ++i) if (m.rs[i]) s.rs[i] = m_state.rs[i];
    for (int st = 0; st < kStages; ++st) {
        for (int t = 0; t < kStageStates; ++t) if (m.tss[st * kStageStates + t]) s.tss[st][t] = m_state.tss[st][t];
        for (int t = 0; t < kSamplerStates; ++t) if (m.samp[st * kSamplerStates + t]) s.samp[st][t] = m_state.samp[st][t];
        if (m.textures[st]) Assign(s.textures[st], m_state.textures[st]);
    }
    for (int i = 0; i < 512; ++i) if (m.transforms[i]) s.transforms[i] = m_state.transforms[i];
    for (int i = 0; i < 6; ++i) if (m.clipPlanes[i]) std::memcpy(s.clipPlanes[i], m_state.clipPlanes[i], 16);
    for (const auto& kv : m.lights) {
        auto it = m_state.lights.find(kv.first);
        if (it != m_state.lights.end()) s.lights[kv.first] = it->second;
    }
    if (m.material) s.material = m_state.material;
    if (m.viewport) s.viewport = m_state.viewport;
    if (m.fvf) s.fvf = m_state.fvf;
    if (m.decl) Assign(s.decl, m_state.decl);
    if (m.streams) {
        Assign(s.streams[0].buffer, m_state.streams[0].buffer);
        s.streams[0].offset = m_state.streams[0].offset;
        s.streams[0].stride = m_state.streams[0].stride;
    }
    if (m.indices) Assign(s.indices, m_state.indices);
}

void Device::ApplyState(const State& s, const StateMask& m)
{
    if (m_recording) return;   // D3D9: Apply while recording is not recorded; nothing to do
    for (int i = 0; i < kRenderStates; ++i) if (m.rs[i]) m_state.rs[i] = s.rs[i];
    for (int st = 0; st < kStages; ++st) {
        for (int t = 0; t < kStageStates; ++t) if (m.tss[st * kStageStates + t]) m_state.tss[st][t] = s.tss[st][t];
        for (int t = 0; t < kSamplerStates; ++t) if (m.samp[st * kSamplerStates + t]) m_state.samp[st][t] = s.samp[st][t];
        if (m.textures[st]) Assign(m_state.textures[st], s.textures[st]);
    }
    for (int i = 0; i < 512; ++i) if (m.transforms[i]) m_state.transforms[i] = s.transforms[i];
    for (int i = 0; i < 6; ++i) if (m.clipPlanes[i]) std::memcpy(m_state.clipPlanes[i], s.clipPlanes[i], 16);
    for (const auto& kv : m.lights) {
        auto it = s.lights.find(kv.first);
        if (it == s.lights.end()) continue;
        LightSlot& dst = m_state.lights[kv.first];
        if (it->second.set) { dst.light = it->second.light; dst.set = true; }
        dst.enabled = it->second.enabled;
    }
    if (m.material) m_state.material = s.material;
    if (m.viewport) m_state.viewport = s.viewport;
    if (m.fvf) m_state.fvf = s.fvf;
    if (m.decl) Assign(m_state.decl, s.decl);
    if (m.streams) {
        Assign(m_state.streams[0].buffer, s.streams[0].buffer);
        m_state.streams[0].offset = s.streams[0].offset;
        m_state.streams[0].stride = s.streams[0].stride;
    }
    if (m.indices) Assign(m_state.indices, s.indices);
}

} // namespace d3d9web
