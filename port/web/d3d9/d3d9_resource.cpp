// Surfaces, textures, buffers, declarations, shader objects and state blocks (see d3d9_web.h).
#include "d3d9_web.h"
#include <algorithm>
#include <cstdio>

namespace d3d9web {

BaseTexture* AsBase(IDirect3DBaseTexture9* t)
{
    if (!t) return nullptr;
    if (t->GetType() == D3DRTYPE_CUBETEXTURE) return static_cast<CubeTexture*>(static_cast<IDirect3DCubeTexture9*>(t));
    return static_cast<Texture*>(static_cast<IDirect3DTexture9*>(t));
}

Surface* AsSurface(IDirect3DSurface9* s) { return static_cast<Surface*>(s); }

// ---- Surface ---------------------------------------------------------------------------------

Surface::Surface(Device* dev, Kind kind, UINT w, UINT h, D3DFORMAT fmt, D3DPOOL pool, DWORD usage)
    : m_device(dev), m_kind(kind), m_width(w), m_height(h), m_format(fmt), m_pool(pool), m_usage(usage)
{
    if (kind == Kind::DepthStencil || kind == Kind::BackBuffer) {
        glGenRenderbuffers(1, &m_renderbuffer);
        glBindRenderbuffer(GL_RENDERBUFFER, m_renderbuffer);
        glRenderbufferStorage(GL_RENDERBUFFER, kind == Kind::DepthStencil ? GL_DEPTH24_STENCIL8 : GL_RGBA8, (GLsizei)w, (GLsizei)h);
    }
}

Surface::~Surface()
{
    if (m_device) m_device->ForgetSurface(this);
    if (m_renderbuffer) glDeleteRenderbuffers(1, &m_renderbuffer);
}

ULONG Surface::AddRef()
{
    if (m_container) return m_container->AddRefT();
    return ++m_ref;
}

ULONG Surface::Release()
{
    if (m_container) return m_container->ReleaseT();
    const ULONG r = --m_ref;
    if (!r) delete this;
    return r;
}

HRESULT Surface::GetDevice(IDirect3DDevice9** d)
{
    if (!d) return D3DERR_INVALIDCALL;
    *d = m_device;
    m_device->AddRef();
    return D3D_OK;
}

HRESULT Surface::GetContainer(REFIID, void** out)
{
    if (!out) return D3DERR_INVALIDCALL;
    if (m_container) {
        if (m_container->m_target == GL_TEXTURE_CUBE_MAP) *out = static_cast<IDirect3DCubeTexture9*>(static_cast<CubeTexture*>(m_container));
        else *out = static_cast<IDirect3DTexture9*>(static_cast<Texture*>(m_container));
        m_container->AddRefT();
        return D3D_OK;
    }
    *out = m_device;
    m_device->AddRef();
    return D3D_OK;
}

HRESULT Surface::GetDesc(D3DSURFACE_DESC* d)
{
    if (!d) return D3DERR_INVALIDCALL;
    std::memset(d, 0, sizeof(*d));
    d->Format = m_format;
    d->Type = D3DRTYPE_SURFACE;
    d->Usage = m_usage;
    d->Pool = m_pool;
    d->MultiSampleType = D3DMULTISAMPLE_NONE;
    d->Width = m_width;
    d->Height = m_height;
    return D3D_OK;
}

bool Surface::IsRenderTarget() const
{
    return m_kind == Kind::BackBuffer || (m_kind == Kind::TextureLevel && (m_usage & D3DUSAGE_RENDERTARGET));
}

uint8_t* Surface::Bytes()
{
    if (m_kind == Kind::TextureLevel) {
        const UINT face = m_face == GL_TEXTURE_2D ? 0 : (UINT)(m_face - GL_TEXTURE_CUBE_MAP_POSITIVE_X);
        return m_container->LevelBytes(face * m_container->m_levels + m_level);
    }
    if (m_own.empty()) m_own.assign(LevelSize(m_format, m_width, m_height), 0);
    return m_own.data();
}

void Surface::ReadBack()
{
    if (!IsRenderTarget()) return;
    const GLuint fbo = m_device->FramebufferFor(this, nullptr);
    glBindFramebuffer(GL_FRAMEBUFFER, fbo);
    std::vector<uint8_t> rgba((size_t)m_width * m_height * 4);
    glPixelStorei(GL_PACK_ALIGNMENT, 1);
    glReadPixels(0, 0, (GLsizei)m_width, (GLsizei)m_height, GL_RGBA, GL_UNSIGNED_BYTE, rgba.data());
    m_device->BindTextureForUpload(GL_TEXTURE_2D, 0);   // resyncs the cached framebuffer binding too
    StoreFromRgba(m_format, rgba.data(), m_width, m_height, Bytes(), Pitch());
}

HRESULT Surface::LockRect(D3DLOCKED_RECT* lr, const RECT* r, DWORD flags)
{
    if (!lr) return D3DERR_INVALIDCALL;
    if (m_kind == Kind::DepthStencil) return D3DERR_INVALIDCALL;
    if (m_kind == Kind::TextureLevel) {
        const UINT face = m_face == GL_TEXTURE_2D ? 0 : (UINT)(m_face - GL_TEXTURE_CUBE_MAP_POSITIVE_X);
        if (m_container->m_target == GL_TEXTURE_CUBE_MAP)
            return static_cast<CubeTexture*>(m_container)->LockRect((D3DCUBEMAP_FACES)face, m_level, lr, r, flags);
        return static_cast<Texture*>(m_container)->LockRect(m_level, lr, r, flags);
    }
    if (m_kind == Kind::BackBuffer) ReadBack();
    m_lockRect = r ? *r : RECT{ 0, 0, (LONG)m_width, (LONG)m_height };
    const FormatInfo* fi = GetFormatInfo(m_format);
    const UINT bpp = fi && !fi->compressed ? (UINT)fi->bytesPerPixel : 4;
    lr->Pitch = (INT)Pitch();
    lr->pBits = Bytes() + (size_t)m_lockRect.top * Pitch() + (size_t)m_lockRect.left * bpp;
    m_locked = true;
    return D3D_OK;
}

HRESULT Surface::UnlockRect()
{
    if (m_kind == Kind::TextureLevel) {
        const UINT face = m_face == GL_TEXTURE_2D ? 0 : (UINT)(m_face - GL_TEXTURE_CUBE_MAP_POSITIVE_X);
        if (m_container->m_target == GL_TEXTURE_CUBE_MAP)
            return static_cast<CubeTexture*>(m_container)->UnlockRect((D3DCUBEMAP_FACES)face, m_level);
        return static_cast<Texture*>(m_container)->UnlockRect(m_level);
    }
    m_locked = false;
    return D3D_OK;
}

// ---- BaseTexture -----------------------------------------------------------------------------

uint8_t* BaseTexture::LevelBytes(UINT index)
{
    if (index >= m_data.size()) return nullptr;
    auto& d = m_data[index];
    if (d.empty()) {
        const UINT level = index % m_levels;
        d.assign(LevelSize(m_format, LevelW(level), LevelH(level)), 0);
    }
    return d.data();
}

void BaseTexture::MarkDirty(UINT index, const RECT& r)
{
    if (index >= m_dirty.size()) return;
    if (m_dirty[index]) {
        RECT& d = m_dirtyRect[index];
        d.left = std::min(d.left, r.left);
        d.top = std::min(d.top, r.top);
        d.right = std::max(d.right, r.right);
        d.bottom = std::max(d.bottom, r.bottom);
    } else {
        m_dirty[index] = true;
        m_dirtyRect[index] = r;
    }
    m_anyDirty = true;
}

void BaseTexture::CreateStorage()
{
    const UINT faces = Faces();
    m_data.resize(faces * m_levels);
    m_dirty.assign(faces * m_levels, false);
    m_dirtyRect.assign(faces * m_levels, RECT{});
    if (m_pool == D3DPOOL_SYSTEMMEM || m_pool == D3DPOOL_SCRATCH) return;
    const FormatInfo* fi = GetFormatInfo(m_format);
    GLenum internal = fi ? fi->internalFormat : GL_RGBA8;
    if (fi && fi->compressed && !m_device->m_hasS3tc) internal = GL_RGBA8;
    if (m_usage & D3DUSAGE_RENDERTARGET) internal = GL_RGBA8;
    if (fi && fi->depth) internal = GL_DEPTH24_STENCIL8;
    glGenTextures(1, &m_gl);
    m_device->BindTextureForUpload(m_target, m_gl);
    glTexStorage2D(m_target, (GLsizei)m_levels, internal, (GLsizei)m_width, (GLsizei)m_height);
    if (m_usage & D3DUSAGE_RENDERTARGET) {
        // Render targets start black, like D3D's (GL storage is undefined until written).
        std::vector<uint8_t> zero((size_t)m_width * m_height * 4, 0);
        glPixelStorei(GL_UNPACK_ALIGNMENT, 1);
        for (UINT f = 0; f < faces; ++f)
            glTexSubImage2D(m_target == GL_TEXTURE_CUBE_MAP ? GL_TEXTURE_CUBE_MAP_POSITIVE_X + f : GL_TEXTURE_2D, 0, 0, 0,
                            (GLsizei)m_width, (GLsizei)m_height, GL_RGBA, GL_UNSIGNED_BYTE, zero.data());
    }
}

void BaseTexture::DestroyStorage()
{
    if (m_gl) {
        m_device->ForgetTexture(m_gl);
        glDeleteTextures(1, &m_gl);
        m_gl = 0;
    }
    for (Surface* s : m_surfaces) {
        if (!s) continue;
        s->m_container = nullptr;
        m_device->ForgetSurface(s);
        s->m_device = nullptr;
        delete s;
    }
    m_surfaces.clear();
}

void BaseTexture::Flush()
{
    if (!m_anyDirty || !m_gl) return;
    m_anyDirty = false;
    m_device->BindTextureForUpload(m_target, m_gl);
    const FormatInfo* fi = GetFormatInfo(m_format);
    for (UINT i = 0; i < m_dirty.size(); ++i) {
        if (!m_dirty[i]) continue;
        m_dirty[i] = false;
        const UINT face = i / m_levels, level = i % m_levels;
        const GLenum target = m_target == GL_TEXTURE_CUBE_MAP ? GL_TEXTURE_CUBE_MAP_POSITIVE_X + face : GL_TEXTURE_2D;
        if (m_data[i].empty()) continue;
        RECT r = m_dirtyRect[i];
        r.right = std::min<LONG>(r.right, (LONG)LevelW(level));
        r.bottom = std::min<LONG>(r.bottom, (LONG)LevelH(level));
        UploadLevel(target, (GLint)level, m_format, LevelW(level), LevelH(level), r, m_data[i].data(), LevelPitch(m_format, LevelW(level)));
        // Read-only art (block-compressed, managed) does not need its CPU copy after upload.
        if (fi && fi->compressed && m_pool == D3DPOOL_MANAGED) {
            m_data[i].clear();
            m_data[i].shrink_to_fit();
        }
    }
}

// ---- Texture ---------------------------------------------------------------------------------

static UINT FullLevels(UINT w, UINT h)
{
    UINT n = 1;
    while (w > 1 || h > 1) { w = std::max(1u, w / 2); h = std::max(1u, h / 2); ++n; }
    return n;
}

Texture::Texture(Device* dev, UINT w, UINT h, UINT levels, DWORD usage, D3DFORMAT fmt, D3DPOOL pool)
{
    m_device = dev;
    m_target = GL_TEXTURE_2D;
    m_width = std::max(1u, w);
    m_height = std::max(1u, h);
    m_levels = levels ? std::min(levels, FullLevels(m_width, m_height)) : FullLevels(m_width, m_height);
    if (usage & D3DUSAGE_AUTOGENMIPMAP) m_levels = 1;
    m_usage = usage;
    m_format = fmt;
    m_pool = pool;
    CreateStorage();
    m_surfaces.assign(m_levels, nullptr);
}

Texture::~Texture() { DestroyStorage(); }

ULONG Texture::Release()
{
    const ULONG r = --m_ref;
    if (!r) delete this;
    return r;
}

HRESULT Texture::GetDevice(IDirect3DDevice9** d)
{
    if (!d) return D3DERR_INVALIDCALL;
    *d = m_device;
    m_device->AddRef();
    return D3D_OK;
}

void Texture::GenerateMipSubLevels()
{
    if (!m_gl || m_levels < 2) return;
    Flush();
    m_device->BindTextureForUpload(GL_TEXTURE_2D, m_gl);
    glGenerateMipmap(GL_TEXTURE_2D);
}

HRESULT Texture::GetLevelDesc(UINT level, D3DSURFACE_DESC* d)
{
    if (level >= m_levels || !d) return D3DERR_INVALIDCALL;
    std::memset(d, 0, sizeof(*d));
    d->Format = m_format;
    d->Type = D3DRTYPE_SURFACE;
    d->Usage = m_usage;
    d->Pool = m_pool;
    d->Width = LevelW(level);
    d->Height = LevelH(level);
    return D3D_OK;
}

HRESULT Texture::GetSurfaceLevel(UINT level, IDirect3DSurface9** out)
{
    if (level >= m_levels || !out) return D3DERR_INVALIDCALL;
    if (!m_surfaces[level]) {
        Surface* s = new Surface(m_device, Surface::Kind::TextureLevel, LevelW(level), LevelH(level), m_format, m_pool, m_usage);
        s->m_container = this;
        s->m_level = level;
        s->m_face = GL_TEXTURE_2D;
        m_surfaces[level] = s;
    }
    *out = m_surfaces[level];
    AddRef();
    return D3D_OK;
}

HRESULT Texture::LockRect(UINT level, D3DLOCKED_RECT* lr, const RECT* r, DWORD flags)
{
    if (level >= m_levels || !lr) return D3DERR_INVALIDCALL;
    if (m_usage & D3DUSAGE_RENDERTARGET) {
        // Lockable only through GetRenderTargetData in D3D9; read back for robustness.
        IDirect3DSurface9* s = nullptr;
        GetSurfaceLevel(level, &s);
        AsSurface(s)->ReadBack();
        s->Release();
    }
    const FormatInfo* fi = GetFormatInfo(m_format);
    RECT rect = r ? *r : RECT{ 0, 0, (LONG)LevelW(level), (LONG)LevelH(level) };
    const UINT pitch = LevelPitch(m_format, LevelW(level));
    uint8_t* base = LevelBytes(level);
    if (fi && fi->compressed) lr->pBits = base + (size_t)(rect.top / 4) * pitch + (size_t)(rect.left / 4) * fi->blockBytes;
    else lr->pBits = base + (size_t)rect.top * pitch + (size_t)rect.left * (fi ? fi->bytesPerPixel : 4);
    lr->Pitch = (INT)pitch;
    if (!(flags & D3DLOCK_READONLY)) MarkDirty(level, rect);
    return D3D_OK;
}

HRESULT Texture::UnlockRect(UINT level)
{
    if (level >= m_levels) return D3DERR_INVALIDCALL;
    return D3D_OK;
}

// ---- CubeTexture -----------------------------------------------------------------------------

CubeTexture::CubeTexture(Device* dev, UINT edge, UINT levels, DWORD usage, D3DFORMAT fmt, D3DPOOL pool)
{
    m_device = dev;
    m_target = GL_TEXTURE_CUBE_MAP;
    m_width = m_height = std::max(1u, edge);
    m_levels = levels ? std::min(levels, FullLevels(m_width, m_height)) : FullLevels(m_width, m_height);
    m_usage = usage;
    m_format = fmt;
    m_pool = pool;
    CreateStorage();
    m_surfaces.assign(6 * m_levels, nullptr);
}

CubeTexture::~CubeTexture() { DestroyStorage(); }

ULONG CubeTexture::Release()
{
    const ULONG r = --m_ref;
    if (!r) delete this;
    return r;
}

HRESULT CubeTexture::GetDevice(IDirect3DDevice9** d)
{
    if (!d) return D3DERR_INVALIDCALL;
    *d = m_device;
    m_device->AddRef();
    return D3D_OK;
}

HRESULT CubeTexture::GetLevelDesc(UINT level, D3DSURFACE_DESC* d)
{
    if (level >= m_levels || !d) return D3DERR_INVALIDCALL;
    std::memset(d, 0, sizeof(*d));
    d->Format = m_format;
    d->Type = D3DRTYPE_SURFACE;
    d->Usage = m_usage;
    d->Pool = m_pool;
    d->Width = d->Height = LevelW(level);
    return D3D_OK;
}

HRESULT CubeTexture::GetCubeMapSurface(D3DCUBEMAP_FACES face, UINT level, IDirect3DSurface9** out)
{
    if (level >= m_levels || (UINT)face >= 6 || !out) return D3DERR_INVALIDCALL;
    const UINT idx = (UINT)face * m_levels + level;
    if (!m_surfaces[idx]) {
        Surface* s = new Surface(m_device, Surface::Kind::TextureLevel, LevelW(level), LevelH(level), m_format, m_pool, m_usage);
        s->m_container = this;
        s->m_level = level;
        s->m_face = GL_TEXTURE_CUBE_MAP_POSITIVE_X + (GLenum)face;
        m_surfaces[idx] = s;
    }
    *out = m_surfaces[idx];
    AddRef();
    return D3D_OK;
}

HRESULT CubeTexture::LockRect(D3DCUBEMAP_FACES face, UINT level, D3DLOCKED_RECT* lr, const RECT* r, DWORD flags)
{
    if (level >= m_levels || (UINT)face >= 6 || !lr) return D3DERR_INVALIDCALL;
    const UINT idx = (UINT)face * m_levels + level;
    const FormatInfo* fi = GetFormatInfo(m_format);
    RECT rect = r ? *r : RECT{ 0, 0, (LONG)LevelW(level), (LONG)LevelH(level) };
    const UINT pitch = LevelPitch(m_format, LevelW(level));
    uint8_t* base = LevelBytes(idx);
    if (fi && fi->compressed) lr->pBits = base + (size_t)(rect.top / 4) * pitch + (size_t)(rect.left / 4) * fi->blockBytes;
    else lr->pBits = base + (size_t)rect.top * pitch + (size_t)rect.left * (fi ? fi->bytesPerPixel : 4);
    lr->Pitch = (INT)pitch;
    if (!(flags & D3DLOCK_READONLY)) MarkDirty(idx, rect);
    return D3D_OK;
}

HRESULT CubeTexture::UnlockRect(D3DCUBEMAP_FACES, UINT) { return D3D_OK; }

// ---- buffers ---------------------------------------------------------------------------------

void Buffer::CreateGl()
{
    // System-memory buffers are drawn from too (software vertex processing), so they get a GL
    // buffer like the others.
    if (m_pool == D3DPOOL_SCRATCH) return;
    glGenBuffers(1, &m_gl);
}

void Buffer::DestroyGl()
{
    if (!m_gl) return;
    m_dev->ForgetBuffer(m_gl);
    glDeleteBuffers(1, &m_gl);
    m_gl = 0;
}

void Buffer::Lock(UINT offset, UINT size, void** out, DWORD flags)
{
    if (m_data.size() < m_size) m_data.resize(m_size);
    if (offset > m_size) offset = m_size;
    if (size == 0 || offset + size > m_size) size = m_size - offset;
    if (out) *out = m_data.data() + offset;
    if (flags & D3DLOCK_READONLY) return;
    if (flags & D3DLOCK_DISCARD) m_discarded = true;
    if (m_dirtyHi > m_dirtyLo) {
        m_dirtyLo = std::min(m_dirtyLo, offset);
        m_dirtyHi = std::max(m_dirtyHi, offset + size);
    } else {
        m_dirtyLo = offset;
        m_dirtyHi = offset + size;
    }
}

void Buffer::Flush()
{
    if (!m_gl || m_dirtyHi <= m_dirtyLo) return;
    if (m_target == GL_ELEMENT_ARRAY_BUFFER) m_dev->BindElementBuffer(m_gl);
    else m_dev->BindArrayBuffer(m_gl);
    const GLenum hint = (m_usage & D3DUSAGE_DYNAMIC) ? GL_DYNAMIC_DRAW : GL_STATIC_DRAW;
    if (m_discarded || (m_dirtyLo == 0 && m_dirtyHi >= m_size) || !m_uploadedOnce) {
        glBufferData(m_target, m_size, m_data.data(), hint);   // orphans the old storage
        m_uploadedOnce = true;
    } else {
        glBufferSubData(m_target, m_dirtyLo, m_dirtyHi - m_dirtyLo, m_data.data() + m_dirtyLo);
    }
    m_discarded = false;
    m_dirtyLo = m_dirtyHi = 0;
}

VertexBuffer::VertexBuffer(Device* dev, UINT len, DWORD usage, DWORD fvf, D3DPOOL pool)
    : m_device(dev), m_fvf(fvf)
{
    m_dev = dev;
    m_size = len;
    m_usage = usage;
    m_pool = pool;
    m_target = GL_ARRAY_BUFFER;
    m_data.assign(len, 0);
    CreateGl();
}

VertexBuffer::~VertexBuffer() { DestroyGl(); }

HRESULT VertexBuffer::GetDevice(IDirect3DDevice9** d)
{
    if (!d) return D3DERR_INVALIDCALL;
    *d = m_device;
    m_device->AddRef();
    return D3D_OK;
}

HRESULT VertexBuffer::GetDesc(D3DVERTEXBUFFER_DESC* d)
{
    if (!d) return D3DERR_INVALIDCALL;
    d->Format = D3DFMT_VERTEXDATA;
    d->Type = D3DRTYPE_VERTEXBUFFER;
    d->Usage = m_usage;
    d->Pool = m_pool;
    d->Size = m_size;
    d->FVF = m_fvf;
    return D3D_OK;
}

IndexBuffer::IndexBuffer(Device* dev, UINT len, DWORD usage, D3DFORMAT fmt, D3DPOOL pool)
    : m_device(dev), m_format(fmt)
{
    m_dev = dev;
    m_size = len;
    m_usage = usage;
    m_pool = pool;
    m_target = GL_ELEMENT_ARRAY_BUFFER;
    m_data.assign(len, 0);
    CreateGl();
}

IndexBuffer::~IndexBuffer() { DestroyGl(); }

HRESULT IndexBuffer::GetDevice(IDirect3DDevice9** d)
{
    if (!d) return D3DERR_INVALIDCALL;
    *d = m_device;
    m_device->AddRef();
    return D3D_OK;
}

HRESULT IndexBuffer::GetDesc(D3DINDEXBUFFER_DESC* d)
{
    if (!d) return D3DERR_INVALIDCALL;
    d->Format = m_format;
    d->Type = D3DRTYPE_INDEXBUFFER;
    d->Usage = m_usage;
    d->Pool = m_pool;
    d->Size = m_size;
    return D3D_OK;
}

// ---- declarations, shaders, state blocks -----------------------------------------------------

VertexDeclaration::VertexDeclaration(Device* dev, const D3DVERTEXELEMENT9* e) : m_device(dev)
{
    for (; e && e->Stream != 0xFF; ++e) m_elements.push_back(*e);
}

HRESULT VertexDeclaration::GetDevice(IDirect3DDevice9** d)
{
    if (!d) return D3DERR_INVALIDCALL;
    *d = m_device;
    m_device->AddRef();
    return D3D_OK;
}

HRESULT VertexDeclaration::GetDeclaration(D3DVERTEXELEMENT9* e, UINT* n)
{
    if (!n) return D3DERR_INVALIDCALL;
    const UINT count = (UINT)m_elements.size() + 1;
    if (e) {
        for (UINT i = 0; i + 1 < count; ++i) e[i] = m_elements[i];
        e[count - 1] = D3DVERTEXELEMENT9{ 0xFF, 0, D3DDECLTYPE_UNUSED, 0, 0, 0 };
    }
    *n = count;
    return D3D_OK;
}

Shader::Shader(Device* dev, const DWORD* code) : m_device(dev)
{
    // Token stream up to and including the END token (0x0000FFFF).
    for (const DWORD* p = code; p; ++p) {
        m_code.push_back(*p);
        if (*p == 0x0000FFFF || m_code.size() > (1u << 16)) break;
    }
}

HRESULT Shader::GetDevice(IDirect3DDevice9** d)
{
    if (!d) return D3DERR_INVALIDCALL;
    *d = m_device;
    m_device->AddRef();
    return D3D_OK;
}

HRESULT Shader::GetFunction(void* data, UINT* size)
{
    if (!size) return D3DERR_INVALIDCALL;
    if (data) std::memcpy(data, m_code.data(), std::min<size_t>(*size, m_code.size() * 4));
    *size = (UINT)(m_code.size() * 4);
    return D3D_OK;
}

HRESULT StateBlock::GetDevice(IDirect3DDevice9** d)
{
    if (!d) return D3DERR_INVALIDCALL;
    *d = m_device;
    m_device->AddRef();
    return D3D_OK;
}

HRESULT StateBlock::Capture()
{
    m_device->CaptureState(m_state, m_mask);
    return D3D_OK;
}

HRESULT StateBlock::Apply()
{
    m_device->ApplyState(m_state, m_mask);
    return D3D_OK;
}

} // namespace d3d9web
