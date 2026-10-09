// The D3D9 fixed-function pipeline as generated GLSL ES 3.00 (see d3d9_web.h).
//
// A program is generated per distinct combination of the states that change its code (vertex
// layout, lighting setup, texture stage operations, texture coordinate generation, fog mode,
// alpha test function, ...); values (matrices, colours, light parameters) live in the uniform
// block FfpConstants. Formulas follow the D3D9 fixed-function pipeline documentation:
//  - lighting: emissive + ambient*globalAmbient + sum over lights of attenuation * spot *
//    (ambient + diffuse*N.L + specular*(N.H)^power), colours clamped;
//  - vertex fog (linear/exp/exp2 on camera-space z or range), or specular alpha when no mode;
//  - texture coordinates: TEXCOORDINDEX source or camera-space generation, then the texture
//    matrix on (u, v, 1, 0) for 2D input (the D3D9 quirk: translation in the third row);
//  - texture stages: every D3DTOP the game uses, plus bump-env and dot3; then specular add,
//    alpha test, fog blend.
// Coordinates: D3D clip space -> GL with y flipped (render targets are stored top row first),
// z from [0,1] to [-1,1] and D3D9's half-pixel offset applied.
#include "d3d9_web.h"
#include <algorithm>
#include <cmath>
#include <cstdarg>
#include <cstdio>

namespace d3d9web {

namespace {

enum Gen { kGenPass = 0, kGenNormal, kGenPosition, kGenReflect, kGenSphere };

DWORD Rs(const State& s, D3DRENDERSTATETYPE r) { return s.rs[r]; }
float RsF(const State& s, D3DRENDERSTATETYPE r) { float f; std::memcpy(&f, &s.rs[r], 4); return f; }

void Mul(const float* a, const float* b, float* out)   // row-vector convention: out = a * b
{
    float r[16];
    for (int i = 0; i < 4; ++i)
        for (int j = 0; j < 4; ++j)
            r[i * 4 + j] = a[i * 4] * b[j] + a[i * 4 + 1] * b[4 + j] + a[i * 4 + 2] * b[8 + j] + a[i * 4 + 3] * b[12 + j];
    std::memcpy(out, r, sizeof(r));
}

// Inverse-transpose of the upper 3x3 (normal matrix), stored as a 4x4.
void NormalMatrix(const float* m, float* out)
{
    const float a = m[0], b = m[1], c = m[2], d = m[4], e = m[5], f = m[6], g = m[8], h = m[9], i = m[10];
    const float A = e * i - f * h, B = -(d * i - f * g), C = d * h - e * g;
    float det = a * A + b * B + c * C;
    if (std::fabs(det) < 1e-20f) det = 1e-20f;
    const float inv = 1.0f / det;
    // inverse = adj^T / det; inverse-transpose = adj / det (cofactor matrix)
    const float cof[9] = { A, B, C,
                           -(b * i - c * h), a * i - c * g, -(a * h - b * g),
                           b * f - c * e, -(a * f - c * d), a * e - b * d };
    std::memset(out, 0, 64);
    for (int r = 0; r < 3; ++r)
        for (int k = 0; k < 3; ++k) out[r * 4 + k] = cof[r * 3 + k] * inv;
    out[15] = 1;
}

void ColorValue(const D3DCOLORVALUE& c, float* out) { out[0] = c.r; out[1] = c.g; out[2] = c.b; out[3] = c.a; }

const char* kHeader = "#version 300 es\nprecision highp float;\nprecision highp int;\n";

const char* kUniformBlock =
    "layout(std140) uniform Ffp {\n"
    "  mat4 u_wvp, u_wv, u_normalMat, u_world, u_proj;\n"
    "  mat4 u_wvBlend[4];\n"
    "  mat4 u_texMat[8];\n"
    "  vec4 u_matDiffuse, u_matAmbient, u_matSpecular, u_matEmissive, u_matPower;\n"
    "  vec4 u_globalAmbient;\n"
    "  vec4 u_lightDiffuse[8], u_lightSpecular[8], u_lightAmbient[8];\n"
    "  vec4 u_lightPos[8], u_lightDir[8], u_lightAtt[8], u_lightSpot[8];\n"
    "  vec4 u_fogParams, u_fogColor, u_tfactor, u_alphaRef;\n"
    "  vec4 u_viewport, u_pixelOffset, u_clipPlane;\n"
    "  vec4 u_bumpMat[8], u_bumpLum[8];\n"
    "  vec4 u_pointParams, u_pointScale;\n"
    "};\n";

std::string Fmt(const char* f, ...) __attribute__((format(printf, 1, 2)));
std::string Fmt(const char* f, ...)
{
    char buf[1024];
    va_list ap;
    va_start(ap, f);
    std::vsnprintf(buf, sizeof(buf), f, ap);
    va_end(ap);
    return buf;
}

} // namespace

// ---- vertex layouts --------------------------------------------------------------------------

VertexLayout LayoutFromFvf(DWORD fvf)
{
    VertexLayout l;
    UINT off = 0;
    auto add = [&](int a, GLint size, GLenum type, bool norm, UINT bytes, bool isInt = false) {
        VertexLayout::Elem& e = l.attr[a];
        e.present = true; e.offset = off; e.size = size; e.type = type; e.norm = norm; e.isInt = isInt;
        off += bytes;
    };
    const DWORD pos = fvf & D3DFVF_POSITION_MASK;
    int betas = 0;
    switch (pos) {
    case D3DFVF_XYZ: add(kAttrPos, 3, GL_FLOAT, false, 12); break;
    case D3DFVF_XYZRHW: add(kAttrPos, 4, GL_FLOAT, false, 16); l.rhw = true; break;
    case D3DFVF_XYZW: add(kAttrPos, 4, GL_FLOAT, false, 16); break;
    case D3DFVF_XYZB1: add(kAttrPos, 3, GL_FLOAT, false, 12); betas = 1; break;
    case D3DFVF_XYZB2: add(kAttrPos, 3, GL_FLOAT, false, 12); betas = 2; break;
    case D3DFVF_XYZB3: add(kAttrPos, 3, GL_FLOAT, false, 12); betas = 3; break;
    case D3DFVF_XYZB4: add(kAttrPos, 3, GL_FLOAT, false, 12); betas = 4; break;
    case D3DFVF_XYZB5: add(kAttrPos, 3, GL_FLOAT, false, 12); betas = 5; break;
    default: break;
    }
    if (betas) {
        const bool ubyte4 = (fvf & D3DFVF_LASTBETA_UBYTE4) != 0;
        const bool d3dcolor = (fvf & D3DFVF_LASTBETA_D3DCOLOR) != 0;
        const int weights = (ubyte4 || d3dcolor) ? betas - 1 : betas;
        if (weights > 0) add(kAttrWeights, weights, GL_FLOAT, false, 4u * weights);
        if (ubyte4) { add(kAttrIndices, 4, GL_UNSIGNED_BYTE, false, 4); l.indexed = true; }
        else if (d3dcolor) { add(kAttrIndices, 4, GL_UNSIGNED_BYTE, false, 4); l.indexed = true; }
        l.weights = std::min(weights, 3);
    }
    if (fvf & D3DFVF_NORMAL) add(kAttrNormal, 3, GL_FLOAT, false, 12);
    if (fvf & D3DFVF_PSIZE) add(kAttrPsize, 1, GL_FLOAT, false, 4);
    if (fvf & D3DFVF_DIFFUSE) add(kAttrDiffuse, 4, GL_UNSIGNED_BYTE, true, 4);
    if (fvf & D3DFVF_SPECULAR) add(kAttrSpecular, 4, GL_UNSIGNED_BYTE, true, 4);
    const int texCount = (int)((fvf & D3DFVF_TEXCOUNT_MASK) >> D3DFVF_TEXCOUNT_SHIFT);
    for (int i = 0; i < texCount && i < kStages; ++i) {
        const int fmt = (int)((fvf >> (16 + 2 * i)) & 3);
        const int comps = fmt == 0 ? 2 : fmt == 1 ? 3 : fmt == 2 ? 4 : 1;
        add(kAttrTex0 + i, comps, GL_FLOAT, false, 4u * comps);
        l.texSize[i] = comps;
    }
    l.stride = off;
    return l;
}

VertexLayout LayoutFromDecl(const VertexDeclaration* d)
{
    VertexLayout l;
    if (!d) return l;
    for (const D3DVERTEXELEMENT9& e : d->m_elements) {
        if (e.Stream != 0) continue;
        int a = -1;
        switch (e.Usage) {
        case D3DDECLUSAGE_POSITION: if (e.UsageIndex == 0) a = kAttrPos; break;
        case D3DDECLUSAGE_POSITIONT: a = kAttrPos; l.rhw = true; break;
        case D3DDECLUSAGE_BLENDWEIGHT: a = kAttrWeights; break;
        case D3DDECLUSAGE_BLENDINDICES: a = kAttrIndices; l.indexed = true; break;
        case D3DDECLUSAGE_NORMAL: if (e.UsageIndex == 0) a = kAttrNormal; break;
        case D3DDECLUSAGE_PSIZE: a = kAttrPsize; break;
        case D3DDECLUSAGE_COLOR: a = e.UsageIndex == 0 ? kAttrDiffuse : e.UsageIndex == 1 ? kAttrSpecular : -1; break;
        case D3DDECLUSAGE_TEXCOORD: if (e.UsageIndex < kStages) a = kAttrTex0 + e.UsageIndex; break;
        default: break;
        }
        if (a < 0) continue;
        VertexLayout::Elem& el = l.attr[a];
        el.present = true;
        el.offset = e.Offset;
        switch (e.Type) {
        case D3DDECLTYPE_FLOAT1: el.size = 1; el.type = GL_FLOAT; break;
        case D3DDECLTYPE_FLOAT2: el.size = 2; el.type = GL_FLOAT; break;
        case D3DDECLTYPE_FLOAT3: el.size = 3; el.type = GL_FLOAT; break;
        case D3DDECLTYPE_FLOAT4: el.size = 4; el.type = GL_FLOAT; break;
        case D3DDECLTYPE_D3DCOLOR: el.size = 4; el.type = GL_UNSIGNED_BYTE; el.norm = true; break;
        case D3DDECLTYPE_UBYTE4: el.size = 4; el.type = GL_UNSIGNED_BYTE; break;
        case D3DDECLTYPE_UBYTE4N: el.size = 4; el.type = GL_UNSIGNED_BYTE; el.norm = true; break;
        case D3DDECLTYPE_SHORT2: el.size = 2; el.type = GL_SHORT; break;
        case D3DDECLTYPE_SHORT4: el.size = 4; el.type = GL_SHORT; break;
        case D3DDECLTYPE_SHORT2N: el.size = 2; el.type = GL_SHORT; el.norm = true; break;
        case D3DDECLTYPE_SHORT4N: el.size = 4; el.type = GL_SHORT; el.norm = true; break;
        case D3DDECLTYPE_USHORT2N: el.size = 2; el.type = GL_UNSIGNED_SHORT; el.norm = true; break;
        case D3DDECLTYPE_USHORT4N: el.size = 4; el.type = GL_UNSIGNED_SHORT; el.norm = true; break;
        default: el.size = 4; el.type = GL_FLOAT; break;
        }
        if (a == kAttrWeights) l.weights = std::min(el.size, 3);
        if (a >= kAttrTex0) l.texSize[a - kAttrTex0] = el.size;
        if (a == kAttrPos && e.Usage == D3DDECLUSAGE_POSITION && el.size == 4) el.size = 3;
    }
    return l;
}

// ---- keys ------------------------------------------------------------------------------------

// Texture type of a stage: 0 none, 1 2D, 2 cube.
static int TexType(const State& s, int stage)
{
    IDirect3DBaseTexture9* t = s.textures[stage];
    if (!t) return 0;
    return t->GetType() == D3DRTYPE_CUBETEXTURE ? 2 : 1;
}

static bool ArgUsesTexture(DWORD arg) { return (arg & D3DTA_SELECTMASK) == D3DTA_TEXTURE; }

void BuildFfpKeys(const State& s, const VertexLayout& layout, std::vector<uint32_t>& vs, std::vector<uint32_t>& ps)
{
    // ---- pixel stages (decides how many texture coordinate sets the vertex stage outputs)
    int stages = 0;
    for (; stages < kStages; ++stages)
        if (s.tss[stages][D3DTSS_COLOROP] == D3DTOP_DISABLE) break;
    // A bump-env stage perturbs the next stage's lookup: that stage needs coordinates too.
    const bool fogOn = Rs(s, D3DRS_FOGENABLE) != 0;
    const bool clip = (Rs(s, D3DRS_CLIPPLANEENABLE) & 1) != 0 && !layout.rhw;
    const bool pointSprite = Rs(s, D3DRS_POINTSPRITEENABLE) != 0;
    const DWORD alphaFunc = Rs(s, D3DRS_ALPHATESTENABLE) ? Rs(s, D3DRS_ALPHAFUNC) : 0;

    ps.clear();
    ps.push_back((uint32_t)stages | (alphaFunc << 4) | ((fogOn ? 1u : 0u) << 8) |
                 ((Rs(s, D3DRS_SPECULARENABLE) ? 1u : 0u) << 9) | ((pointSprite ? 1u : 0u) << 10) | ((clip ? 1u : 0u) << 11));
    for (int i = 0; i < stages; ++i) {
        const DWORD* t = s.tss[i];
        ps.push_back(t[D3DTSS_COLOROP] | ((t[D3DTSS_COLORARG0] & 0xFF) << 8) | ((t[D3DTSS_COLORARG1] & 0xFF) << 16) | ((t[D3DTSS_COLORARG2] & 0xFF) << 24));
        ps.push_back(t[D3DTSS_ALPHAOP] | ((t[D3DTSS_ALPHAARG0] & 0xFF) << 8) | ((t[D3DTSS_ALPHAARG1] & 0xFF) << 16) | ((t[D3DTSS_ALPHAARG2] & 0xFF) << 24));
        const DWORD ttff = t[D3DTSS_TEXTURETRANSFORMFLAGS];
        uint32_t projCount = 0;
        if (ttff & D3DTTFF_PROJECTED) {
            projCount = ttff & 0xFF;
            if (projCount == 0 || projCount == D3DTTFF_DISABLE) {
                // Projected without a count: divide by the last input component.
                const DWORD tci = t[D3DTSS_TEXCOORDINDEX];
                projCount = (tci & 0xFFFF0000) ? 3 : (UINT)std::max(2, layout.texSize[(tci & 0xFFFF) & 7]);
            }
        }
        ps.push_back((uint32_t)TexType(s, i) | (projCount << 4));
    }

    // ---- vertex stage
    const bool lighting = Rs(s, D3DRS_LIGHTING) != 0 && !layout.rhw;
    DWORD fogMode = 0;   // vertex-computed fog: 0 from specular alpha, 1 linear, 2 exp, 3 exp2
    bool tableFog = false;
    if (fogOn) {
        if (Rs(s, D3DRS_FOGTABLEMODE) != D3DFOG_NONE) { fogMode = Rs(s, D3DRS_FOGTABLEMODE); tableFog = true; }
        else fogMode = Rs(s, D3DRS_FOGVERTEXMODE);
        if (fogMode > 3) fogMode = 0;
    }
    int blendWeights = 0;
    bool blend = false;
    const DWORD vb = Rs(s, D3DRS_VERTEXBLEND);
    if (!layout.rhw && vb != D3DVBF_DISABLE && vb != D3DVBF_0WEIGHTS) {
        blend = true;
        blendWeights = std::min<int>(vb == D3DVBF_TWEENING ? 1 : (int)vb, 3);
        blendWeights = std::min(blendWeights, layout.weights);
    }
    vs.clear();
    vs.push_back((layout.rhw ? 1u : 0u) | ((layout.attr[kAttrNormal].present ? 1u : 0u) << 1) |
                 ((layout.attr[kAttrDiffuse].present ? 1u : 0u) << 2) | ((layout.attr[kAttrSpecular].present ? 1u : 0u) << 3) |
                 ((layout.attr[kAttrPsize].present ? 1u : 0u) << 4) | ((lighting ? 1u : 0u) << 5) |
                 ((Rs(s, D3DRS_SPECULARENABLE) ? 1u : 0u) << 6) | ((Rs(s, D3DRS_LOCALVIEWER) ? 1u : 0u) << 7) |
                 ((Rs(s, D3DRS_NORMALIZENORMALS) ? 1u : 0u) << 8) | ((Rs(s, D3DRS_COLORVERTEX) ? 1u : 0u) << 9) |
                 (fogMode << 10) | ((Rs(s, D3DRS_RANGEFOGENABLE) ? 1u : 0u) << 12) | ((clip ? 1u : 0u) << 13) |
                 ((Rs(s, D3DRS_POINTSCALEENABLE) ? 1u : 0u) << 14) | ((uint32_t)blendWeights << 16) |
                 ((blend ? 1u : 0u) << 18) | ((fogOn ? 1u : 0u) << 19) | ((tableFog ? 1u : 0u) << 20) |
                 (((blend && layout.indexed && Rs(s, D3DRS_INDEXEDVERTEXBLENDENABLE)) ? 1u : 0u) << 21));
    uint32_t lights = 0;
    int nLights = 0;
    if (lighting) {
        for (const auto& kv : s.lights) {
            if (!kv.second.enabled || nLights >= kMaxLights) continue;
            const uint32_t type = kv.second.light.Type == D3DLIGHT_POINT ? 1 : kv.second.light.Type == D3DLIGHT_SPOT ? 2 : 3;
            lights |= type << (12 + 2 * nLights);
            ++nLights;
        }
    }
    vs.push_back((Rs(s, D3DRS_DIFFUSEMATERIALSOURCE) & 3) | ((Rs(s, D3DRS_AMBIENTMATERIALSOURCE) & 3) << 2) |
                 ((Rs(s, D3DRS_SPECULARMATERIALSOURCE) & 3) << 4) | ((Rs(s, D3DRS_EMISSIVEMATERIALSOURCE) & 3) << 6) |
                 ((uint32_t)nLights << 8) | lights);
    int texOut = stages;
    for (int i = 0; i < stages; ++i)
        if ((s.tss[i][D3DTSS_COLOROP] == D3DTOP_BUMPENVMAP || s.tss[i][D3DTSS_COLOROP] == D3DTOP_BUMPENVMAPLUMINANCE) && i + 1 < kStages)
            texOut = std::max(texOut, i + 2);
    vs.push_back((uint32_t)texOut);
    for (int i = 0; i < texOut; ++i) {
        const DWORD tci = s.tss[i][D3DTSS_TEXCOORDINDEX];
        const uint32_t index = tci & 7;
        uint32_t gen = kGenPass;
        switch (tci & 0xFFFF0000) {
        case D3DTSS_TCI_CAMERASPACENORMAL: gen = kGenNormal; break;
        case D3DTSS_TCI_CAMERASPACEPOSITION: gen = kGenPosition; break;
        case D3DTSS_TCI_CAMERASPACEREFLECTIONVECTOR: gen = kGenReflect; break;
        case D3DTSS_TCI_SPHEREMAP: gen = kGenSphere; break;
        default: break;
        }
        const uint32_t inSize = gen == kGenPass ? (uint32_t)layout.texSize[index] : 3;
        const DWORD ttff = s.tss[i][D3DTSS_TEXTURETRANSFORMFLAGS];
        const uint32_t count = ttff & 0xFF;
        const bool transform = count != D3DTTFF_DISABLE && !layout.rhw;
        vs.push_back(index | (gen << 3) | (inSize << 6) | (count << 9) | ((ttff & D3DTTFF_PROJECTED) ? 1u << 12 : 0) |
                     (transform ? 1u << 13 : 0));
    }
}

// ---- vertex shader ---------------------------------------------------------------------------

std::string GenerateVertexShader(const std::vector<uint32_t>& k)
{
    const uint32_t f = k[0];
    const bool rhw = f & 1, hasNormal = f & 2, hasDiffuse = f & 4, hasSpecular = f & 8, hasPsize = f & 16;
    const bool lighting = f & (1 << 5), specEnable = f & (1 << 6), localViewer = f & (1 << 7);
    const bool normalize = f & (1 << 8), colorVertex = f & (1 << 9);
    const int fogMode = (f >> 10) & 3;
    const bool rangeFog = f & (1 << 12), clip = f & (1 << 13), pointScale = f & (1 << 14);
    const int blendWeights = (f >> 16) & 3;
    const bool blend = f & (1 << 18), fogOn = f & (1 << 19), tableFog = f & (1 << 20), indexed = f & (1 << 21);
    const uint32_t m = k[1];
    const int nLights = (m >> 8) & 15;
    const int texOut = (int)k[2];

    std::string s = kHeader;
    s += kUniformBlock;
    s += "in vec4 a_pos;\nin vec4 a_weights;\nin vec4 a_indices;\nin vec3 a_normal;\nin float a_psize;\nin vec4 a_diffuse;\nin vec4 a_specular;\n";
    for (int i = 0; i < kStages; ++i) s += Fmt("in vec4 a_tex%d;\n", i);
    s += "out vec4 v_diffuse;\nout vec4 v_specular;\nout float v_fog;\nout float v_clip;\n";
    for (int i = 0; i < texOut; ++i) s += Fmt("out vec4 v_tc%d;\n", i);
    s += "void main() {\n";
    s += Fmt("  vec4 vtxDiffuse = %s;\n", hasDiffuse ? "a_diffuse.zyxw" : "vec4(1.0)");
    s += Fmt("  vec4 vtxSpecular = %s;\n", hasSpecular ? "a_specular.zyxw" : "vec4(0.0)");
    s += "  vec3 Pc = vec3(0.0);\n  vec3 Nc = vec3(0.0);\n";
    if (rhw) {
        s += "  float w = a_pos.w != 0.0 ? 1.0 / a_pos.w : 1.0;\n";
        s += "  vec2 ndc = vec2(2.0 * (a_pos.x + 0.5 - u_viewport.x) / u_viewport.z - 1.0,\n"
             "                  2.0 * (a_pos.y + 0.5 - u_viewport.y) / u_viewport.w - 1.0);\n";
        s += "  gl_Position = vec4(ndc * w, (2.0 * a_pos.z - 1.0) * w, w);\n";
        s += "  v_diffuse = vtxDiffuse;\n  v_specular = vtxSpecular;\n";
        if (fogOn) s += tableFog ? "  v_fog = 1.0;\n" : "  v_fog = vtxSpecular.a;\n";
        else s += "  v_fog = 1.0;\n";
        s += "  v_clip = 1.0;\n";
    } else {
        s += "  vec4 P = vec4(a_pos.xyz, 1.0);\n";
        if (blend) {
            s += "  float wsum = 0.0;\n  vec4 Pb = vec4(0.0);\n  vec3 Nb = vec3(0.0);\n";
            for (int i = 0; i <= blendWeights; ++i) {
                const std::string w = i < blendWeights ? Fmt("a_weights[%d]", i) : "(1.0 - wsum)";
                const std::string mat = indexed ? Fmt("u_wvBlend[int(a_indices[%d]) & 3]", i) : Fmt("u_wvBlend[%d]", i);
                s += Fmt("  { float bw = %s; mat4 bm = %s; Pb += bw * (bm * P); Nb += bw * (mat3(bm) * a_normal); %s }\n",
                         w.c_str(), mat.c_str(), i < blendWeights ? "wsum += bw;" : "");
            }
            s += "  Pc = Pb.xyz;\n  Nc = Nb;\n";
            s += "  gl_Position = u_proj * vec4(Pc, 1.0);\n";
        } else {
            s += "  Pc = (u_wv * P).xyz;\n";
            if (hasNormal) s += "  Nc = mat3(u_normalMat) * a_normal;\n";
            s += "  gl_Position = u_wvp * P;\n";
        }
        if (normalize && hasNormal) s += "  if (dot(Nc, Nc) > 0.0) Nc = normalize(Nc);\n";
        // D3D -> GL clip space (see the file comment).
        s += "  gl_Position.z = 2.0 * gl_Position.z - gl_Position.w;\n";
        s += "  gl_Position.y = -gl_Position.y;\n";
        s += "  gl_Position.xy += u_pixelOffset.xy * gl_Position.w;\n";

        // ---- lighting
        if (lighting) {
            auto src = [&](int shift, const char* material) -> std::string {
                const int v = (int)((m >> shift) & 3);
                if (colorVertex && v == D3DMCS_COLOR1 && hasDiffuse) return "vtxDiffuse";
                if (colorVertex && v == D3DMCS_COLOR2 && hasSpecular) return "vtxSpecular";
                return material;
            };
            s += "  vec4 mDiffuse = " + src(0, "u_matDiffuse") + ";\n";
            s += "  vec4 mAmbient = " + src(2, "u_matAmbient") + ";\n";
            s += "  vec4 mSpecular = " + src(4, "u_matSpecular") + ";\n";
            s += "  vec4 mEmissive = " + src(6, "u_matEmissive") + ";\n";
            s += "  vec3 ambientSum = u_globalAmbient.rgb;\n  vec3 diffuseSum = vec3(0.0);\n  vec3 specSum = vec3(0.0);\n";
            s += localViewer ? "  vec3 V = -normalize(Pc);\n" : "  vec3 V = vec3(0.0, 0.0, -1.0);\n";
            for (int i = 0; i < nLights; ++i) {
                const int type = (int)((m >> (12 + 2 * i)) & 3);   // 1 point, 2 spot, 3 directional
                s += "  {\n";
                if (type == 3) {
                    s += Fmt("    vec3 L = u_lightDir[%d].xyz;\n    float att = 1.0;\n", i);
                } else {
                    s += Fmt("    vec3 Ld = u_lightPos[%d].xyz - Pc;\n    float d = length(Ld);\n    vec3 L = d > 0.0 ? Ld / d : vec3(0.0, 0.0, 1.0);\n", i);
                    s += Fmt("    float att = d > u_lightPos[%d].w ? 0.0 : 1.0 / max(u_lightAtt[%d].x + u_lightAtt[%d].y * d + u_lightAtt[%d].z * d * d, 1e-6);\n", i, i, i, i);
                    if (type == 2) {
                        s += Fmt("    float rho = dot(u_lightDir[%d].xyz, L);\n", i);
                        s += Fmt("    float spot = rho > u_lightSpot[%d].x ? 1.0 : (rho <= u_lightSpot[%d].y ? 0.0 :\n"
                                 "      pow(max((rho - u_lightSpot[%d].y) / max(u_lightSpot[%d].x - u_lightSpot[%d].y, 1e-6), 0.0), u_lightAtt[%d].w));\n",
                                 i, i, i, i, i, i);
                        s += "    att *= spot;\n";
                    }
                }
                s += Fmt("    ambientSum += u_lightAmbient[%d].rgb * att;\n", i);
                if (hasNormal) {
                    s += "    float NdotL = max(dot(Nc, L), 0.0);\n";
                    s += Fmt("    diffuseSum += u_lightDiffuse[%d].rgb * NdotL * att;\n", i);
                    if (specEnable) {
                        s += "    vec3 H = normalize(L + V);\n";
                        s += Fmt("    if (NdotL > 0.0) specSum += u_lightSpecular[%d].rgb * pow(max(dot(Nc, H), 0.0), u_matPower.x) * att;\n", i);
                    }
                }
                s += "  }\n";
            }
            s += "  v_diffuse = vec4(clamp(mEmissive.rgb + mAmbient.rgb * ambientSum + mDiffuse.rgb * diffuseSum, 0.0, 1.0), clamp(mDiffuse.a, 0.0, 1.0));\n";
            s += specEnable ? "  v_specular = vec4(clamp(mSpecular.rgb * specSum, 0.0, 1.0), clamp(mSpecular.a, 0.0, 1.0));\n"
                            : "  v_specular = vec4(0.0, 0.0, 0.0, 1.0);\n";
        } else {
            s += "  v_diffuse = vtxDiffuse;\n  v_specular = vtxSpecular;\n";
        }

        // ---- fog
        if (fogOn) {
            s += rangeFog ? "  float fd = length(Pc);\n" : "  float fd = abs(Pc.z);\n";
            switch (fogMode) {
            case 1: s += "  v_fog = clamp((u_fogParams.y - fd) * u_fogParams.w, 0.0, 1.0);\n"; break;
            case 2: s += "  v_fog = clamp(exp(-u_fogParams.z * fd), 0.0, 1.0);\n"; break;
            case 3: s += "  v_fog = clamp(exp(-(u_fogParams.z * fd) * (u_fogParams.z * fd)), 0.0, 1.0);\n"; break;
            default: s += "  v_fog = v_specular.a;\n"; break;
            }
        } else {
            s += "  v_fog = 1.0;\n";
        }
        s += clip ? "  v_clip = dot(u_world * P, u_clipPlane);\n" : "  v_clip = 1.0;\n";
    }

    // ---- point size
    s += Fmt("  float psize = %s;\n", hasPsize ? "a_psize" : "u_pointParams.x");
    if (pointScale && !rhw)
        s += "  { float d = length(Pc); psize = u_viewport.w * psize * sqrt(1.0 / max(u_pointScale.x + u_pointScale.y * d + u_pointScale.z * d * d, 1e-6)); }\n";
    s += "  gl_PointSize = clamp(psize, max(u_pointParams.y, 1.0), u_pointParams.z);\n";

    // ---- texture coordinates
    for (int i = 0; i < texOut; ++i) {
        const uint32_t t = k[3 + i];
        const int index = (int)(t & 7), gen = (int)((t >> 3) & 7), inSize = (int)((t >> 6) & 7);
        const bool transform = (t >> 13) & 1;
        std::string in;
        switch (gen) {
        case kGenNormal: in = "vec4(Nc, 1.0)"; break;
        case kGenPosition: in = "vec4(Pc, 1.0)"; break;
        case kGenReflect: in = "vec4(reflect(normalize(Pc), Nc), 1.0)"; break;
        case kGenSphere: in = "vec4(reflect(normalize(Pc), Nc).xy * 0.5 + 0.5, 0.0, 1.0)"; break;
        default:
            // Missing components: the next one is 1, the rest 0 (so 2D input meets the matrix as
            // (u, v, 1, 0) - D3D9 puts 2D texture translation in the third row).
            switch (inSize) {
            case 1: in = Fmt("vec4(a_tex%d.x, 1.0, 0.0, 0.0)", index); break;
            case 3: in = Fmt("vec4(a_tex%d.xyz, 1.0)", index); break;
            case 4: in = Fmt("a_tex%d", index); break;
            case 0: in = "vec4(0.0, 0.0, 0.0, 1.0)"; break;
            default: in = Fmt("vec4(a_tex%d.xy, 1.0, 0.0)", index); break;
            }
            break;
        }
        if (transform) s += Fmt("  v_tc%d = u_texMat[%d] * %s;\n", i, i, in.c_str());
        else s += Fmt("  v_tc%d = %s;\n", i, in.c_str());
    }
    s += "}\n";
    return s;
}

// ---- fragment shader -------------------------------------------------------------------------

namespace {

std::string Arg(DWORD a, int stage, bool alpha)
{
    std::string v;
    switch (a & D3DTA_SELECTMASK) {
    case D3DTA_DIFFUSE: v = "diffuse"; break;
    case D3DTA_CURRENT: v = stage == 0 ? "diffuse" : "current"; break;
    case D3DTA_TEXTURE: v = Fmt("tex%d", stage); break;
    case D3DTA_TFACTOR: v = "u_tfactor"; break;
    case D3DTA_SPECULAR: v = "specular"; break;
    case D3DTA_TEMP: v = "temp"; break;
    case D3DTA_CONSTANT: v = "u_tfactor"; break;
    default: v = "current"; break;
    }
    if (a & D3DTA_COMPLEMENT) v = "(vec4(1.0) - " + v + ")";
    if (a & D3DTA_ALPHAREPLICATE) v = "vec4(" + v + ".a)";
    return v + (alpha ? ".a" : ".rgb");
}

// One blending operation; `a0/a1/a2` are the argument expressions, `T` the result type.
std::string Op(DWORD op, const std::string& a0, const std::string& a1, const std::string& a2, int stage, bool alpha,
               const std::string& a1full, const std::string& a2full)
{
    const char* sw = alpha ? ".a" : ".rgb";
    switch (op) {
    case D3DTOP_SELECTARG1: return a1;
    case D3DTOP_SELECTARG2: return a2;
    case D3DTOP_MODULATE: return a1 + " * " + a2;
    case D3DTOP_MODULATE2X: return "(" + a1 + " * " + a2 + ") * 2.0";
    case D3DTOP_MODULATE4X: return "(" + a1 + " * " + a2 + ") * 4.0";
    case D3DTOP_ADD: return a1 + " + " + a2;
    case D3DTOP_ADDSIGNED: return a1 + " + " + a2 + " - 0.5";
    case D3DTOP_ADDSIGNED2X: return "(" + a1 + " + " + a2 + " - 0.5) * 2.0";
    case D3DTOP_SUBTRACT: return a1 + " - " + a2;
    case D3DTOP_ADDSMOOTH: return a1 + " + " + a2 + " - " + a1 + " * " + a2;
    case D3DTOP_BLENDDIFFUSEALPHA: return "mix(" + a2 + ", " + a1 + ", diffuse.a)";
    case D3DTOP_BLENDTEXTUREALPHA: return Fmt("mix(%s, %s, tex%d.a)", a2.c_str(), a1.c_str(), stage);
    case D3DTOP_BLENDFACTORALPHA: return "mix(" + a2 + ", " + a1 + ", u_tfactor.a)";
    case D3DTOP_BLENDTEXTUREALPHAPM: return Fmt("%s + %s * (1.0 - tex%d.a)", a1.c_str(), a2.c_str(), stage);
    case D3DTOP_BLENDCURRENTALPHA: return std::string("mix(") + a2 + ", " + a1 + ", " + (stage == 0 ? "diffuse" : "current") + ".a)";
    case D3DTOP_PREMODULATE: return a1;
    case D3DTOP_MODULATEALPHA_ADDCOLOR:
        return alpha ? a1 : a1full + ".rgb + " + a1full + ".a * " + a2full + ".rgb";
    case D3DTOP_MODULATECOLOR_ADDALPHA:
        return alpha ? a1 : a1full + ".rgb * " + a2full + ".rgb + vec3(" + a1full + ".a)";
    case D3DTOP_MODULATEINVALPHA_ADDCOLOR:
        return alpha ? a1 : "(1.0 - " + a1full + ".a) * " + a2full + ".rgb + " + a1full + ".rgb";
    case D3DTOP_MODULATEINVCOLOR_ADDALPHA:
        return alpha ? a1 : "(vec3(1.0) - " + a1full + ".rgb) * " + a2full + ".rgb + vec3(" + a1full + ".a)";
    case D3DTOP_DOTPRODUCT3: {
        const std::string d = "clamp(dot(" + a1full + ".rgb - 0.5, " + a2full + ".rgb - 0.5) * 4.0, 0.0, 1.0)";
        return alpha ? d : "vec3(" + d + ")";
    }
    case D3DTOP_MULTIPLYADD: return a0 + " + " + a1 + " * " + a2;
    case D3DTOP_LERP: return "mix(" + a2 + ", " + a1 + ", " + a0 + ")";
    case D3DTOP_BUMPENVMAP:
    case D3DTOP_BUMPENVMAPLUMINANCE:
        return std::string(stage == 0 ? "diffuse" : "current") + sw;
    default:
        return std::string(stage == 0 ? "diffuse" : "current") + sw;
    }
}

} // namespace

// Colour a stage samples when it has no texture: D3D9 drivers return opaque black.
static const char* kNoTexture = "vec4(0.0, 0.0, 0.0, 1.0)";

std::string GenerateFragmentShader(const std::vector<uint32_t>& k)
{
    const uint32_t f = k[0];
    const int stages = (int)(f & 15);
    const DWORD alphaFunc = (f >> 4) & 15;
    const bool fog = f & (1 << 8), specAdd = f & (1 << 9), pointSprite = f & (1 << 10), clip = f & (1 << 11);

    std::string s = kHeader;
    s += kUniformBlock;
    s += "in vec4 v_diffuse;\nin vec4 v_specular;\nin float v_fog;\nin float v_clip;\n";
    int texOut = stages;
    for (int i = 0; i < stages; ++i) {
        const DWORD cop = k[1 + 3 * i] & 0xFF;
        if ((cop == D3DTOP_BUMPENVMAP || cop == D3DTOP_BUMPENVMAPLUMINANCE) && i + 1 < kStages) texOut = std::max(texOut, i + 2);
    }
    for (int i = 0; i < texOut; ++i) s += Fmt("in vec4 v_tc%d;\n", i);
    for (int i = 0; i < stages; ++i) {
        const int type = (int)(k[3 + 3 * i] & 15);
        if (type == 1) s += Fmt("uniform sampler2D u_tex%d;\n", i);
        else if (type == 2) s += Fmt("uniform samplerCube u_tex%d;\n", i);
    }
    s += "out vec4 o_color;\n";
    s += "void main() {\n";
    if (clip) s += "  if (v_clip < 0.0) discard;\n";
    s += "  vec4 diffuse = v_diffuse;\n  vec4 specular = v_specular;\n  vec4 current = diffuse;\n  vec4 temp = vec4(0.0);\n";
    s += "  vec2 bump = vec2(0.0);\n  float bumpLum = 1.0;\n";
    for (int i = 0; i < stages; ++i) {
        const uint32_t c = k[1 + 3 * i], a = k[2 + 3 * i], t = k[3 + 3 * i];
        const DWORD cop = c & 0xFF, carg0 = (c >> 8) & 0xFF, carg1 = (c >> 16) & 0xFF, carg2 = (c >> 24) & 0xFF;
        const DWORD aop = a & 0xFF, aarg0 = (a >> 8) & 0xFF, aarg1 = (a >> 16) & 0xFF, aarg2 = (a >> 24) & 0xFF;
        const int type = (int)(t & 15), proj = (int)((t >> 4) & 15);
        const DWORD prevOp = i > 0 ? (k[1 + 3 * (i - 1)] & 0xFF) : 0;
        const bool bumped = prevOp == D3DTOP_BUMPENVMAP || prevOp == D3DTOP_BUMPENVMAPLUMINANCE;
        // texture coordinates
        std::string tc = Fmt("v_tc%d", i);
        s += Fmt("  vec4 tc%d = %s;\n", i, tc.c_str());
        if (proj >= 2 && proj <= 4) s += Fmt("  tc%d.xyz /= (tc%d[%d] != 0.0 ? tc%d[%d] : 1.0);\n", i, i, proj - 1, i, proj - 1);
        if (bumped) s += Fmt("  tc%d.xy += bump;\n", i);
        if (type == 1) s += Fmt("  vec4 tex%d = texture(u_tex%d, %s);\n", i, i, pointSprite && i == 0 ? "gl_PointCoord" : Fmt("tc%d.xy", i).c_str());
        else if (type == 2) s += Fmt("  vec4 tex%d = texture(u_tex%d, tc%d.xyz);\n", i, i, i);
        else s += Fmt("  vec4 tex%d = %s;\n", i, kNoTexture);
        if (bumped && prevOp == D3DTOP_BUMPENVMAPLUMINANCE) s += Fmt("  tex%d.rgb *= bumpLum;\n", i);
        if (cop == D3DTOP_BUMPENVMAP || cop == D3DTOP_BUMPENVMAPLUMINANCE) {
            // du, dv from this stage's (signed) texture, through the bump matrix.
            s += Fmt("  bump = vec2(u_bumpMat[%d].x * tex%d.r + u_bumpMat[%d].z * tex%d.g, u_bumpMat[%d].y * tex%d.r + u_bumpMat[%d].w * tex%d.g);\n",
                     i, i, i, i, i, i, i, i);
            if (cop == D3DTOP_BUMPENVMAPLUMINANCE)
                s += Fmt("  bumpLum = clamp(tex%d.b * u_bumpLum[%d].x + u_bumpLum[%d].y, 0.0, 1.0);\n", i, i, i);
        }
        const std::string c0 = Arg(carg0, i, false), c1 = Arg(carg1, i, false), c2 = Arg(carg2, i, false);
        std::string c1f = Arg(carg1, i, false), c2f = Arg(carg2, i, false);
        c1f = c1f.substr(0, c1f.size() - 4);   // without the ".rgb" swizzle
        c2f = c2f.substr(0, c2f.size() - 4);
        const std::string color = Op(cop, c0, c1, c2, i, false, c1f, c2f);
        std::string alpha;
        if (cop == D3DTOP_DOTPRODUCT3) {
            alpha = Op(cop, "", "", "", i, true, c1f, c2f);   // D3D replicates dot3 into alpha
        } else if (aop == D3DTOP_DISABLE) {
            alpha = std::string(i == 0 ? "diffuse" : "current") + ".a";
        } else {
            const std::string a0 = Arg(aarg0, i, true), a1 = Arg(aarg1, i, true), a2 = Arg(aarg2, i, true);
            std::string a1f = Arg(aarg1, i, true), a2f = Arg(aarg2, i, true);
            a1f = a1f.substr(0, a1f.size() - 2);
            a2f = a2f.substr(0, a2f.size() - 2);
            alpha = Op(aop, a0, a1, a2, i, true, a1f, a2f);
        }
        s += "  {\n";
        s += "    vec3 rc = " + color + ";\n";
        s += "    float ra = " + alpha + ";\n";
        s += "    current = clamp(vec4(rc, ra), 0.0, 1.0);\n";
        s += "  }\n";
    }
    if (specAdd) s += "  current.rgb = clamp(current.rgb + specular.rgb, 0.0, 1.0);\n";
    if (alphaFunc && alphaFunc != D3DCMP_ALWAYS) {
        s += "  float aq = floor(current.a * 255.0 + 0.5);\n";
        const char* cmp = "true";
        switch (alphaFunc) {
        case D3DCMP_NEVER: cmp = "false"; break;
        case D3DCMP_LESS: cmp = "aq < u_alphaRef.x"; break;
        case D3DCMP_EQUAL: cmp = "aq == u_alphaRef.x"; break;
        case D3DCMP_LESSEQUAL: cmp = "aq <= u_alphaRef.x"; break;
        case D3DCMP_GREATER: cmp = "aq > u_alphaRef.x"; break;
        case D3DCMP_NOTEQUAL: cmp = "aq != u_alphaRef.x"; break;
        case D3DCMP_GREATEREQUAL: cmp = "aq >= u_alphaRef.x"; break;
        default: break;
        }
        s += Fmt("  if (!(%s)) discard;\n", cmp);
    }
    if (fog) s += "  current.rgb = mix(u_fogColor.rgb, current.rgb, clamp(v_fog, 0.0, 1.0));\n";
    s += "  o_color = current;\n}\n";
    return s;
}

// ---- constants -------------------------------------------------------------------------------

void FillFfpConstants(const State& s, FfpConstants& c)
{
    const float* world = &s.transforms[D3DTS_WORLDMATRIX(0)].m[0][0];
    const float* view = &s.transforms[D3DTS_VIEW].m[0][0];
    const float* proj = &s.transforms[D3DTS_PROJECTION].m[0][0];
    Mul(world, view, c.wv);
    Mul(c.wv, proj, c.wvp);
    NormalMatrix(c.wv, c.normalMat);
    std::memcpy(c.world, world, 64);
    std::memcpy(c.proj, proj, 64);
    for (int i = 0; i < 4; ++i) Mul(&s.transforms[D3DTS_WORLDMATRIX(i)].m[0][0], view, c.wvBlend[i]);
    for (int i = 0; i < kStages; ++i) std::memcpy(c.texMat[i], &s.transforms[D3DTS_TEXTURE0 + i].m[0][0], 64);

    ColorValue(s.material.Diffuse, c.matDiffuse);
    ColorValue(s.material.Ambient, c.matAmbient);
    ColorValue(s.material.Specular, c.matSpecular);
    ColorValue(s.material.Emissive, c.matEmissive);
    c.matPower[0] = s.material.Power;
    c.matPower[1] = c.matPower[2] = c.matPower[3] = 0;
    ColorToFloats(s.rs[D3DRS_AMBIENT], c.globalAmbient);

    int n = 0;
    if (s.rs[D3DRS_LIGHTING]) {
        for (const auto& kv : s.lights) {
            if (!kv.second.enabled || n >= kMaxLights) continue;
            const D3DLIGHT9& l = kv.second.light;
            ColorValue(l.Diffuse, c.lightDiffuse[n]);
            ColorValue(l.Specular, c.lightSpecular[n]);
            ColorValue(l.Ambient, c.lightAmbient[n]);
            // Lights are given in world space; lighting runs in camera space.
            const float p[3] = { l.Position.x, l.Position.y, l.Position.z };
            for (int j = 0; j < 3; ++j) c.lightPos[n][j] = p[0] * view[j] + p[1] * view[4 + j] + p[2] * view[8 + j] + view[12 + j];
            c.lightPos[n][3] = l.Range > 0 ? l.Range : 1e30f;
            float d[3];
            for (int j = 0; j < 3; ++j) d[j] = l.Direction.x * view[j] + l.Direction.y * view[4 + j] + l.Direction.z * view[8 + j];
            const float len = std::sqrt(d[0] * d[0] + d[1] * d[1] + d[2] * d[2]);
            // Stored as the direction towards the light (-Direction), normalised.
            for (int j = 0; j < 3; ++j) c.lightDir[n][j] = len > 0 ? -d[j] / len : 0;
            c.lightDir[n][3] = 0;
            c.lightAtt[n][0] = l.Attenuation0;
            c.lightAtt[n][1] = l.Attenuation1;
            c.lightAtt[n][2] = l.Attenuation2;
            c.lightAtt[n][3] = l.Falloff;
            if (l.Type == D3DLIGHT_POINT && l.Attenuation0 == 0 && l.Attenuation1 == 0 && l.Attenuation2 == 0) c.lightAtt[n][0] = 1;
            c.lightSpot[n][0] = std::cos(l.Theta * 0.5f);
            c.lightSpot[n][1] = std::cos(l.Phi * 0.5f);
            c.lightSpot[n][2] = c.lightSpot[n][3] = 0;
            ++n;
        }
    }

    const float fs = RsF(s, D3DRS_FOGSTART), fe = RsF(s, D3DRS_FOGEND);
    c.fogParams[0] = fs;
    c.fogParams[1] = fe;
    c.fogParams[2] = RsF(s, D3DRS_FOGDENSITY);
    c.fogParams[3] = fe != fs ? 1.0f / (fe - fs) : 0.0f;
    ColorToFloats(s.rs[D3DRS_FOGCOLOR], c.fogColor);
    ColorToFloats(s.rs[D3DRS_TEXTUREFACTOR], c.tfactor);
    c.alphaRef[0] = (float)(s.rs[D3DRS_ALPHAREF] & 0xFF);

    const D3DVIEWPORT9& vp = s.viewport;
    c.viewport[0] = (float)vp.X;
    c.viewport[1] = (float)vp.Y;
    c.viewport[2] = (float)std::max<DWORD>(vp.Width, 1);
    c.viewport[3] = (float)std::max<DWORD>(vp.Height, 1);
    c.pixelOffset[0] = 1.0f / c.viewport[2];
    c.pixelOffset[1] = 1.0f / c.viewport[3];
    std::memcpy(c.clipPlane, s.clipPlanes[0], 16);

    for (int i = 0; i < kStages; ++i) {
        float m[4];
        std::memcpy(&m[0], &s.tss[i][D3DTSS_BUMPENVMAT00], 4);
        std::memcpy(&m[1], &s.tss[i][D3DTSS_BUMPENVMAT01], 4);
        std::memcpy(&m[2], &s.tss[i][D3DTSS_BUMPENVMAT10], 4);
        std::memcpy(&m[3], &s.tss[i][D3DTSS_BUMPENVMAT11], 4);
        std::memcpy(c.bumpMat[i], m, 16);
        std::memcpy(&c.bumpLum[i][0], &s.tss[i][D3DTSS_BUMPENVLSCALE], 4);
        std::memcpy(&c.bumpLum[i][1], &s.tss[i][D3DTSS_BUMPENVLOFFSET], 4);
    }

    c.pointParams[0] = RsF(s, D3DRS_POINTSIZE);
    c.pointParams[1] = RsF(s, D3DRS_POINTSIZE_MIN);
    c.pointParams[2] = std::max(1.0f, RsF(s, D3DRS_POINTSIZE_MAX));
    c.pointScale[0] = RsF(s, D3DRS_POINTSCALE_A);
    c.pointScale[1] = RsF(s, D3DRS_POINTSCALE_B);
    c.pointScale[2] = RsF(s, D3DRS_POINTSCALE_C);
}

} // namespace d3d9web
