// Tests for the native .x reader (port/d3dx/xfile.cpp): the same scene in text, binary 0032 and
// binary 0064 encoding must give identical objects and blobs; blob layouts byte for byte (Mesh,
// MeshFace, Material, TextureFilename's string pointer, SkinWeights, a custom template with every
// primitive and a 2-D array); references; enumeration order; both COM APIs; template
// registration (D3DRM_XTEMPLATES, RegisterEnumTemplates); errors, including every truncation.
#include "../d3dx/xfile.h"
#include <d3dx9.h>
#include <dxfile.h>
#include <rmxfguid.h>
#include "../../Dependency/directx/rmxftmpl.h"
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <string>
#include <unistd.h>
#include <vector>

using namespace ran_xfile;

static int g_failed = 0, g_checks = 0;
#define CHECK(cond) do { ++g_checks; if (!(cond)) { std::printf("FAIL %s:%d  %s\n", __FILE__, __LINE__, #cond); ++g_failed; } } while (0)

// {11111111-2222-3333-4444-555566667777}
static const GUID kCustomId = { 0x11111111, 0x2222, 0x3333, { 0x44, 0x44, 0x55, 0x55, 0x66, 0x66, 0x77, 0x77 } };
// {aaaaaaaa-bbbb-cccc-dddd-eeeeeeeeeeee}: the id of data object C1
static const GUID kC1Id = { 0xaaaaaaaa, 0xbbbb, 0xcccc, { 0xdd, 0xdd, 0xee, 0xee, 0xee, 0xee, 0xee, 0xee } };
static const GUID kSkinHeaderId = { 0x3cf169ce, 0xff7c, 0x44ab, { 0x93, 0xc0, 0xf7, 0x8f, 0x62, 0xd1, 0x72, 0xe2 } };
static const GUID kSkinWeightsId = { 0x6f0d123b, 0xbad2, 0x4167, { 0xa0, 0xd0, 0x80, 0x22, 0x4f, 0x25, 0xfa, 0xbb } };

static bool Same(const GUID& a, const GUID& b) { return std::memcmp(&a, &b, sizeof(GUID)) == 0; }

// --- the scene in text form --------------------------------------------------------------------

static const char kText[] =
    "xof 0303txt 0032\n"
    "// a comment\n"
    "# another comment\n"
    "template XSkinMeshHeader {\n"
    " <3cf169ce-ff7c-44ab-93c0-f78f62d172e2>\n"
    " WORD nMaxSkinWeightsPerVertex;\n WORD nMaxSkinWeightsPerFace;\n WORD nBones;\n"
    "}\n"
    "template SkinWeights {\n"
    " <6f0d123b-bad2-4167-a0d0-80224f25fabb>\n"
    " STRING transformNodeName;\n DWORD nWeights;\n array DWORD vertexIndices[nWeights];\n"
    " array FLOAT weights[nWeights];\n Matrix4x4 matrixOffset;\n"
    "}\n"
    "template Custom {\n"
    " <11111111-2222-3333-4444-555566667777>\n"
    " DWORD rows; DWORD cols; array WORD grid[rows][cols];\n"
    " CHAR c; UCHAR u; SWORD sw; SDWORD sd; DOUBLE dd; array STRING tags[2];\n"
    " [Material <3D82AB4D-62DA-11CF-AB39-0020AF71E433>, Frame]\n"
    "}\n"
    "Material Red {\n"
    " 1.000000;0.000000;0.000000;1.000000;;\n 5.000000;\n 0.500000;0.500000;0.500000;;\n 0.000000;0.000000;0.000000;;\n"
    " TextureFilename { \"red.dds\"; }\n"
    "}\n"
    "Frame Root {\n"
    " FrameTransformMatrix {\n"
    "  1.0,0.0,0.0,0.0,0.0,1.0,0.0,0.0,0.0,0.0,1.0,0.0,10.0,20.0,30.0,1.0;;\n"
    " }\n"
    " Mesh Box {\n"
    "  4;\n"
    "  0.0;0.0;0.0;,\n  1.0;0.0;0.0;,\n  1.0;1.0;0.0;,\n  0.0;1.0;0.0;;\n"
    "  2;\n"
    "  3;0,1,2;,\n  4;0,1,2,3;;\n"
    "  MeshNormals { 1; 0.0;0.0;1.0;; 2; 3;0,0,0;, 4;0,0,0,0;; }\n"
    "  MeshTextureCoords { 4; 0.0;0.0;, 1.0;0.0;, 1.0;1.0;, 0.0;1.0;; }\n"
    "  MeshMaterialList { 1; 2; 0, 0;; { Red } }\n"
    "  XSkinMeshHeader { 2; 4; 1; }\n"
    "  SkinWeights { \"Bone01\"; 2; 0, 3; 0.25, 0.75;\n"
    "   1.0,0.0,0.0,0.0,0.0,1.0,0.0,0.0,0.0,0.0,1.0,0.0,-1.0,-2.0,-3.0,1.0;; }\n"
    " }\n"
    " Frame Bone01 {\n"
    "  FrameTransformMatrix { 1,0,0,0,0,1,0,0,0,0,1,0,0,5,0,1;; }\n"
    " }\n"
    "}\n"
    "AnimationSet Walk {\n"
    " Animation {\n"
    "  { Bone01 }\n"
    "  AnimationKey { 0; 2; 0;4;1.0,0.0,0.0,0.0;;, 100;4;0.0,1.0,0.0,0.0;;; }\n"
    " }\n"
    "}\n"
    "Custom C1 {\n"
    " <aaaaaaaa-bbbb-cccc-dddd-eeeeeeeeeeee>\n"
    " 2; 3; 1,2,3,4,5,6; -5; 200; -2; -100000; 2.5; \"a\", \"bc\";\n"
    "}\n"
    "MeshFace {\n"
    " 3; 7,8,9;;\n"
    "}\n";

// --- the same scene in binary form -------------------------------------------------------------

enum { NAME = 1, STRING = 2, INTEGER = 3, GUIDT = 5, INTEGER_LIST = 6, FLOAT_LIST = 7, OBRACE = 10, CBRACE = 11,
       OBRACKET = 14, CBRACKET = 15, COMMA = 19, SEMICOLON = 20, TEMPLATE = 31, WORD_T = 40, DWORD_T = 41,
       FLOAT_T = 42, DOUBLE_T = 43, CHAR_T = 44, UCHAR_T = 45, SWORD_T = 46, SDWORD_T = 47, LPSTR_T = 49, ARRAY = 52 };

struct Bin
{
    std::vector<BYTE> v;
    bool doubles;
    explicit Bin(bool d) : doubles(d)
    {
        const char* h = d ? "xof 0303bin 0064" : "xof 0303bin 0032";
        v.assign(h, h + 16);
    }
    void U16(WORD x) { v.push_back((BYTE)x); v.push_back((BYTE)(x >> 8)); }
    void U32(DWORD x) { for (int i = 0; i < 4; ++i) v.push_back((BYTE)(x >> (8 * i))); }
    void T(WORD t) { U16(t); }
    void Name(const char* s) { T(NAME); U32((DWORD)std::strlen(s)); v.insert(v.end(), s, s + std::strlen(s)); }
    void Str(const char* s, WORD term = SEMICOLON)
    {
        T(STRING); U32((DWORD)std::strlen(s)); v.insert(v.end(), s, s + std::strlen(s)); T(term);
    }
    void Int(DWORD x) { T(INTEGER); U32(x); }
    void Guid(const GUID& g) { T(GUIDT); const BYTE* p = (const BYTE*)&g; v.insert(v.end(), p, p + 16); }
    void Ints(std::initializer_list<DWORD> l) { T(INTEGER_LIST); U32((DWORD)l.size()); for (DWORD x : l) U32(x); }
    void Floats(std::initializer_list<double> l)
    {
        T(FLOAT_LIST); U32((DWORD)l.size());
        for (double x : l) {
            if (doubles) { const BYTE* p = (const BYTE*)&x; v.insert(v.end(), p, p + 8); }
            else { float f = (float)x; const BYTE* p = (const BYTE*)&f; v.insert(v.end(), p, p + 4); }
        }
    }
    void Member(WORD prim, const char* name) { T(prim); Name(name); T(SEMICOLON); }
    void Open(const char* type, const char* name = nullptr) { Name(type); if (name) Name(name); T(OBRACE); }
};

static std::vector<BYTE> BinaryScene(bool doubles, size_t* rootStart = nullptr, size_t* rootEnd = nullptr)
{
    Bin b(doubles);
    // templates
    b.T(TEMPLATE); b.Name("XSkinMeshHeader"); b.T(OBRACE); b.Guid(kSkinHeaderId);
    b.Member(WORD_T, "nMaxSkinWeightsPerVertex"); b.Member(WORD_T, "nMaxSkinWeightsPerFace"); b.Member(WORD_T, "nBones");
    b.T(CBRACE);
    b.T(TEMPLATE); b.Name("SkinWeights"); b.T(OBRACE); b.Guid(kSkinWeightsId);
    b.Member(LPSTR_T, "transformNodeName"); b.Member(DWORD_T, "nWeights");
    b.T(ARRAY); b.T(DWORD_T); b.Name("vertexIndices"); b.T(OBRACKET); b.Name("nWeights"); b.T(CBRACKET); b.T(SEMICOLON);
    b.T(ARRAY); b.T(FLOAT_T); b.Name("weights"); b.T(OBRACKET); b.Name("nWeights"); b.T(CBRACKET); b.T(SEMICOLON);
    b.Name("Matrix4x4"); b.Name("matrixOffset"); b.T(SEMICOLON);
    b.T(CBRACE);
    b.T(TEMPLATE); b.Name("Custom"); b.T(OBRACE); b.Guid(kCustomId);
    b.Member(DWORD_T, "rows"); b.Member(DWORD_T, "cols");
    b.T(ARRAY); b.T(WORD_T); b.Name("grid"); b.T(OBRACKET); b.Name("rows"); b.T(CBRACKET);
    b.T(OBRACKET); b.Name("cols"); b.T(CBRACKET); b.T(SEMICOLON);
    b.Member(CHAR_T, "c"); b.Member(UCHAR_T, "u"); b.Member(SWORD_T, "sw"); b.Member(SDWORD_T, "sd"); b.Member(DOUBLE_T, "dd");
    b.T(ARRAY); b.T(LPSTR_T); b.Name("tags"); b.T(OBRACKET); b.Int(2); b.T(CBRACKET); b.T(SEMICOLON);
    b.T(OBRACKET); b.Name("Material"); b.Guid(TID_D3DRMMaterial); b.T(COMMA); b.Name("Frame"); b.T(CBRACKET);
    b.T(CBRACE);

    b.Open("Material", "Red");
    b.Floats({ 1, 0, 0, 1, 5, 0.5, 0.5, 0.5, 0, 0, 0 });
    b.Open("TextureFilename"); b.Str("red.dds"); b.T(CBRACE);
    b.T(CBRACE);

    if (rootStart) *rootStart = b.v.size();
    b.Open("Frame", "Root");
    b.Open("FrameTransformMatrix"); b.Floats({ 1, 0, 0, 0, 0, 1, 0, 0, 0, 0, 1, 0, 10, 20, 30, 1 }); b.T(CBRACE);
    b.Open("Mesh", "Box");
    b.Ints({ 4 });
    b.Floats({ 0, 0, 0, 1, 0, 0, 1, 1, 0, 0, 1, 0 });
    b.Ints({ 2, 3, 0, 1, 2, 4, 0, 1, 2, 3 });   // nFaces and every face in one list
    b.Open("MeshNormals"); b.Ints({ 1 }); b.Floats({ 0, 0, 1 }); b.Ints({ 2, 3, 0, 0, 0, 4, 0, 0, 0, 0 }); b.T(CBRACE);
    b.Open("MeshTextureCoords"); b.Ints({ 4 }); b.Floats({ 0, 0, 1, 0, 1, 1, 0, 1 }); b.T(CBRACE);
    b.Open("MeshMaterialList"); b.Ints({ 1, 2, 0, 0 }); b.T(OBRACE); b.Name("Red"); b.T(CBRACE); b.T(CBRACE);
    b.Open("XSkinMeshHeader"); b.Ints({ 2, 4, 1 }); b.T(CBRACE);
    b.Open("SkinWeights"); b.Str("Bone01"); b.Ints({ 2, 0, 3 });
    b.Floats({ 0.25, 0.75, 1, 0, 0, 0, 0, 1, 0, 0, 0, 0, 1, 0, -1, -2, -3, 1 });   // weights run into the matrix
    b.T(CBRACE);
    b.T(CBRACE);   // Mesh
    b.Open("Frame", "Bone01");
    b.Open("FrameTransformMatrix"); b.Floats({ 1, 0, 0, 0, 0, 1, 0, 0, 0, 0, 1, 0, 0, 5, 0, 1 }); b.T(CBRACE);
    b.T(CBRACE);
    b.T(CBRACE);   // Root
    if (rootEnd) *rootEnd = b.v.size();

    b.Open("AnimationSet", "Walk");
    b.Open("Animation");
    b.T(OBRACE); b.Name("Bone01"); b.T(CBRACE);
    b.Open("AnimationKey");
    b.Ints({ 0, 2, 0, 4 }); b.Floats({ 1, 0, 0, 0 }); b.Ints({ 100, 4 }); b.Floats({ 0, 1, 0, 0 });
    b.T(CBRACE);
    b.T(CBRACE);
    b.T(CBRACE);

    b.Open("Custom", "C1"); b.Guid(kC1Id);
    b.Ints({ 2, 3, 1, 2, 3, 4, 5, 6, (DWORD)-5, 200, (DWORD)-2, (DWORD)-100000 });
    b.Floats({ 2.5 });
    b.Str("a", COMMA); b.Str("bc");
    b.T(CBRACE);

    b.Open("MeshFace"); b.Ints({ 3, 7, 8, 9 }); b.T(CBRACE);
    return b.v;
}

// --- helpers ----------------------------------------------------------------------------------

static TemplateSet D3drmTemplates()
{
    Document d;
    Error e = Parse(D3DRM_XTEMPLATES, D3DRM_XTEMPLATE_BYTES, TemplateSet(), d);
    CHECK(e == Error::Ok);
    return d.templates;
}

template <class T> static T At(const std::vector<BYTE>& v, size_t off) { T x; std::memcpy(&x, v.data() + off, sizeof x); return x; }

static const Object* ChildObj(const Object& o, size_t i) { return i < o.children.size() ? o.children[i].object.get() : nullptr; }

// Same structure and blobs (strings compared by content, not pointer value).
static bool SameObjects(const Object& a, const Object& b)
{
    if (a.name != b.name || a.tmpl->name != b.tmpl->name || a.hasId != b.hasId || !Same(a.id, b.id)) return false;
    if (a.data.size() != b.data.size() || a.children.size() != b.children.size()) return false;
    if (a.strings.size() != b.strings.size()) return false;
    for (size_t i = 0; i < a.strings.size(); ++i)
        if (a.strings[i] != b.strings[i]) return false;
    // Compare blobs with string pointers masked out.
    std::vector<BYTE> da = a.data, db = b.data;
    for (const std::string& s : a.strings) {
        const char* p = s.c_str();
        for (size_t off = 0; off + sizeof p <= da.size(); ++off)
            if (std::memcmp(&da[off], &p, sizeof p) == 0) std::memset(&da[off], 0, sizeof p);
    }
    for (const std::string& s : b.strings) {
        const char* p = s.c_str();
        for (size_t off = 0; off + sizeof p <= db.size(); ++off)
            if (std::memcmp(&db[off], &p, sizeof p) == 0) std::memset(&db[off], 0, sizeof p);
    }
    if (da != db) return false;
    for (size_t i = 0; i < a.children.size(); ++i) {
        const Child& ca = a.children[i];
        const Child& cb = b.children[i];
        if (ca.reference != cb.reference || ca.refName != cb.refName) return false;
        if (ca.reference) { if (!ca.target || !cb.target || ca.target->name != cb.target->name) return false; }
        else if (!SameObjects(*ca.object, *cb.object)) return false;
    }
    return true;
}

static std::string DataName(ID3DXFileData* d)
{
    SIZE_T n = 0;
    if (FAILED(d->GetName(nullptr, &n))) return "<error>";
    std::string s(n, '\0');
    if (FAILED(d->GetName(&s[0], &n))) return "<error>";
    s.resize(n ? n - 1 : 0);
    return s;
}

static ID3DXFileData* Child3(ID3DXFileData* d, SIZE_T i)
{
    ID3DXFileData* c = nullptr;
    return SUCCEEDED(d->GetChild(i, &c)) ? c : nullptr;
}

// --- tests ------------------------------------------------------------------------------------

static void TestTemplates()
{
    TemplateSet t = D3drmTemplates();
    CHECK(t.Size() == 35);
    const Template* mesh = t.Find("Mesh");
    CHECK(mesh && Same(mesh->id, TID_D3DRMMesh) && mesh->openness == Template::OPEN);
    CHECK(mesh && mesh->members.size() == 4 && mesh->members[1].array && mesh->members[1].typeName == "Vector" &&
          mesh->members[1].dims.size() == 1 && mesh->members[1].dims[0].ref == "nVertices");
    const Template* m4 = t.Find("Matrix4x4");
    CHECK(m4 && m4->members.size() == 1 && m4->members[0].prim == PRIM_FLOAT && m4->members[0].dims[0].fixed == 16);
    const Template* mml = t.Find("MeshMaterialList");
    CHECK(mml && mml->openness == Template::RESTRICTED && mml->allowed.size() == 1 && mml->allowed[0].name == "Material");
    const Template* frame = t.Find("Frame");
    CHECK(frame && Same(frame->id, TID_D3DRMFrame) && frame->members.empty());
    const Template* tf = t.Find("TextureFilename");
    CHECK(tf && tf->members.size() == 1 && tf->members[0].prim == PRIM_LPSTR);
    const Template* g = t.Find("Guid");
    CHECK(g && g->members.size() == 4 && g->members[3].prim == PRIM_UCHAR && g->members[3].dims[0].fixed == 8);
    CHECK(t.Find("AnimationKey") && Same(t.Find("AnimationKey")->id, TID_D3DRMAnimationKey));
    CHECK(t.Find("XSkinMeshHeader") == nullptr);   // not part of D3DRM's set
}

static void TestBlobs(const Document& d, const char* what)
{
    std::printf("  blobs: %s\n", what);
    CHECK(d.objects.size() == 5);
    if (d.objects.size() != 5) return;
    // Enumeration order and names.
    CHECK(d.objects[0]->name == "Red" && d.objects[1]->name == "Root" && d.objects[2]->name == "Walk" &&
          d.objects[3]->name == "C1" && d.objects[4]->name.empty());

    // Material: ColorRGBA, power, ColorRGB, ColorRGB = 11 floats.
    const Object& mat = *d.objects[0];
    CHECK(mat.data.size() == 44);
    const float matExpect[11] = { 1, 0, 0, 1, 5, 0.5f, 0.5f, 0.5f, 0, 0, 0 };
    CHECK(mat.data.size() == 44 && std::memcmp(mat.data.data(), matExpect, 44) == 0);
    // TextureFilename: one char* to a NUL-terminated string the object owns.
    const Object* tex = ChildObj(mat, 0);
    CHECK(tex && tex->data.size() == sizeof(char*));
    if (tex && tex->data.size() == sizeof(char*)) {
        const char* s = At<const char*>(tex->data, 0);
        CHECK(s && std::strcmp(s, "red.dds") == 0);
    }

    const Object& root = *d.objects[1];
    CHECK(root.children.size() == 3);
    const Object* ftm = ChildObj(root, 0);
    CHECK(ftm && ftm->data.size() == 64 && At<float>(ftm->data, 48) == 10.0f && At<float>(ftm->data, 56) == 30.0f);

    // Mesh: nVertices, 4 Vectors, nFaces, MeshFace{3;0,1,2}, MeshFace{4;0,1,2,3} = 92 bytes.
    const Object* mesh = ChildObj(root, 1);
    CHECK(mesh && mesh->name == "Box" && mesh->data.size() == 92);
    if (mesh && mesh->data.size() == 92) {
        std::vector<BYTE> expect;
        auto u = [&](DWORD x) { const BYTE* p = (const BYTE*)&x; expect.insert(expect.end(), p, p + 4); };
        auto f = [&](float x) { const BYTE* p = (const BYTE*)&x; expect.insert(expect.end(), p, p + 4); };
        u(4);
        const float verts[12] = { 0, 0, 0, 1, 0, 0, 1, 1, 0, 0, 1, 0 };
        for (float x : verts) f(x);
        u(2); u(3); u(0); u(1); u(2); u(4); u(0); u(1); u(2); u(3);
        CHECK(expect == mesh->data);
        CHECK(mesh->members.size() == 4 && mesh->members[2].name == "nFaces" && mesh->members[2].offset == 52 &&
              mesh->members[3].offset == 56 && mesh->members[3].size == 36);
    }
    if (!mesh || mesh->children.size() != 5) { CHECK(false); return; }
    CHECK(ChildObj(*mesh, 0)->tmpl->name == "MeshNormals" && ChildObj(*mesh, 0)->data.size() == 4 + 12 + 4 + 16 + 20);
    const Object* tc = ChildObj(*mesh, 1);
    CHECK(tc->data.size() == 4 + 32 && At<float>(tc->data, 4 + 8 * 2 + 4) == 1.0f);
    // MeshMaterialList with a reference child { Red }.
    const Object* mml = ChildObj(*mesh, 2);
    CHECK(mml->data.size() == 16 && At<DWORD>(mml->data, 4) == 2);
    CHECK(mml->children.size() == 1 && mml->children[0].reference && mml->children[0].refName == "Red" &&
          mml->children[0].target == d.objects[0].get());
    // XSkinMeshHeader: three WORDs, packed.
    const Object* hdr = ChildObj(*mesh, 3);
    CHECK(hdr->data.size() == 6 && At<WORD>(hdr->data, 0) == 2 && At<WORD>(hdr->data, 2) == 4 && At<WORD>(hdr->data, 4) == 1);
    CHECK(Same(hdr->tmpl->id, kSkinHeaderId));
    // SkinWeights: char*, nWeights, 2 indices, 2 weights, matrix (no padding).
    const Object* sw = ChildObj(*mesh, 4);
    const size_t P = sizeof(char*);
    CHECK(sw->data.size() == P + 4 + 8 + 8 + 64);
    if (sw->data.size() == P + 4 + 8 + 8 + 64) {
        CHECK(std::strcmp(At<const char*>(sw->data, 0), "Bone01") == 0);
        CHECK(At<DWORD>(sw->data, P) == 2 && At<DWORD>(sw->data, P + 8) == 3);
        CHECK(At<float>(sw->data, P + 12) == 0.25f && At<float>(sw->data, P + 16) == 0.75f);
        CHECK(At<float>(sw->data, P + 20 + 48) == -1.0f && At<float>(sw->data, P + 20 + 60) == 1.0f);
    }

    // AnimationKey: keyType, nKeys, then TimedFloatKeys { time, FloatKeys { nValues, values } }.
    const Object& set = *d.objects[2];
    const Object* anim = ChildObj(set, 0);
    CHECK(anim && anim->children.size() == 2 && anim->children[0].reference && anim->children[0].target &&
          anim->children[0].target->name == "Bone01");
    const Object* key = anim ? ChildObj(*anim, 1) : nullptr;
    CHECK(key && key->data.size() == 8 + 2 * (8 + 16));
    if (key && key->data.size() == 56) {
        CHECK(At<DWORD>(key->data, 4) == 2 && At<DWORD>(key->data, 32) == 100 && At<DWORD>(key->data, 36) == 4);
        CHECK(At<float>(key->data, 12 + 4) == 1.0f && At<float>(key->data, 40 + 4) == 1.0f);
    }

    // Custom: rows, cols, grid[2][3] WORDs, CHAR, UCHAR, SWORD, SDWORD, DOUBLE, 2 STRING pointers.
    const Object& c1 = *d.objects[3];
    CHECK(c1.hasId && Same(c1.id, kC1Id) && Same(c1.tmpl->id, kCustomId));
    CHECK(c1.data.size() == 8 + 12 + 1 + 1 + 2 + 4 + 8 + 2 * P);
    if (c1.data.size() == 36 + 2 * P) {
        for (int i = 0; i < 6; ++i) CHECK(At<WORD>(c1.data, 8 + 2 * i) == i + 1);
        CHECK(At<BYTE>(c1.data, 20) == 0xFB && At<BYTE>(c1.data, 21) == 200);
        CHECK(At<short>(c1.data, 22) == -2 && At<int>(c1.data, 24) == -100000 && At<double>(c1.data, 28) == 2.5);
        CHECK(std::strcmp(At<const char*>(c1.data, 36), "a") == 0 && std::strcmp(At<const char*>(c1.data, 36 + P), "bc") == 0);
    }
    // A standalone MeshFace.
    const Object& face = *d.objects[4];
    const DWORD faceExpect[4] = { 3, 7, 8, 9 };
    CHECK(face.data.size() == 16 && std::memcmp(face.data.data(), faceExpect, 16) == 0);
}

static void TestParser()
{
    TemplateSet known = D3drmTemplates();
    Document text, bin32, bin64;
    CHECK(Parse(kText, sizeof(kText) - 1, known, text) == Error::Ok);
    std::vector<BYTE> b32 = BinaryScene(false), b64 = BinaryScene(true);
    CHECK(Parse(b32.data(), b32.size(), known, bin32) == Error::Ok);
    CHECK(Parse(b64.data(), b64.size(), known, bin64) == Error::Ok);
    TestBlobs(text, "text");
    TestBlobs(bin32, "binary 0032");
    TestBlobs(bin64, "binary 0064");
    CHECK(text.objects.size() == bin32.objects.size() && bin32.objects.size() == bin64.objects.size());
    for (size_t i = 0; i < text.objects.size() && i < bin32.objects.size() && i < bin64.objects.size(); ++i) {
        CHECK(SameObjects(*text.objects[i], *bin32.objects[i]));
        CHECK(SameObjects(*bin32.objects[i], *bin64.objects[i]));
    }
    // The file's own templates are reported (and its restriction parsed).
    CHECK(text.templates.Size() == 3 && bin32.templates.Size() == 3);
    const Template* custom = bin32.templates.Find("Custom");
    CHECK(custom && custom->openness == Template::RESTRICTED && custom->allowed.size() == 2 &&
          custom->allowed[0].hasId && Same(custom->allowed[0].id, TID_D3DRMMaterial) && custom->allowed[1].name == "Frame");
    const Template* tcustom = text.templates.Find("Custom");
    CHECK(tcustom && tcustom->allowed.size() == 2 && Same(tcustom->allowed[0].id, TID_D3DRMMaterial));

    // Without D3DRM's templates the scene's Material is unknown.
    Document none;
    CHECK(Parse(kText, sizeof(kText) - 1, TemplateSet(), none) == Error::NoTemplate);
}

static void TestErrors()
{
    TemplateSet known = D3drmTemplates();
    auto parse = [&](const std::string& s) { Document d; return Parse(s.data(), s.size(), known, d); };
    CHECK(parse("") == Error::BadFileType);
    CHECK(parse("xof 0303bin") == Error::BadFileType);
    CHECK(parse("abc 0303bin 0032") == Error::BadFileType);
    CHECK(parse("xof 0203bin 0032") == Error::BadVersion);
    CHECK(parse("xof 0303xyz 0032") == Error::BadFileType);
    CHECK(parse("xof 0303tzip0032") == Error::Compressed);
    CHECK(parse("xof 0303bzip0032") == Error::Compressed);
    CHECK(parse("xof 0303bin 0016") == Error::BadFloatSize);
    CHECK(parse("xof 0302txt 0064") == Error::Ok);   // empty file
    CHECK(parse("xof 0303txt 0032\nNoSuchType { 1; }") == Error::NoTemplate);
    CHECK(parse("xof 0303txt 0032\nFrame A { { Missing } }") == Error::BadReference);
    CHECK(parse("xof 0303txt 0032\ntemplate T { <11111111-2222-3333-4444-555566667777> array DWORD a[n]; }\nT { 1; }") == Error::BadArraySize);
    CHECK(parse("xof 0303txt 0032\nVector { 1.0; \"x\"; 3.0; }") == Error::Parse);
    CHECK(parse("xof 0303txt 0032\nMesh { 4000000000; }") == Error::BadArraySize);
    CHECK(parse("xof 0303txt 0032\nFrame A { ") == Error::Parse);
    CHECK(parse("xof 0303txt 0032\nFrame A { } }") == Error::Parse);
    CHECK(parse("xof 0303txt 0032\nTextureFilename { \"unterminated; }") == Error::Parse);
    // Forward references resolve.
    CHECK(parse("xof 0303txt 0032\nFrame A { { B } }\nFrame B { }") == Error::Ok);

    // Every truncation: never crashes; a cut inside the Root frame always fails.
    size_t rootStart = 0, rootEnd = 0;
    std::vector<BYTE> bin = BinaryScene(false, &rootStart, &rootEnd);
    int binFails = 0;
    for (size_t n = 0; n < bin.size(); ++n) {
        Document d;
        Error e = Parse(bin.data(), n, known, d);
        if (n > rootStart && n < rootEnd) { if (e != Error::Ok) ++binFails; else std::printf("  binary cut at %zu parsed\n", n); }
    }
    CHECK(binFails == (int)(rootEnd - rootStart - 1));
    const std::string text(kText);
    const size_t tStart = text.find("Frame Root") + 1, tEnd = text.find("AnimationSet Walk") - 2;
    int textFails = 0;
    for (size_t n = 0; n < text.size(); ++n) {
        Document d;
        Error e = Parse(text.data(), n, known, d);
        if (n >= tStart && n < tEnd) { if (e != Error::Ok) ++textFails; else std::printf("  text cut at %zu parsed\n", n); }
    }
    CHECK(textFails == (int)(tEnd - tStart));
}

static void TestD3dxApi()
{
    ID3DXFile* file = nullptr;
    CHECK(SUCCEEDED(D3DXFileCreate(&file)) && file);
    if (!file) return;
    std::vector<BYTE> bin = BinaryScene(false);
    D3DXF_FILELOADMEMORY mem = { bin.data(), bin.size() };
    ID3DXFileEnumObject* en = nullptr;
    // Templates not registered yet: Material/Frame are unknown.
    CHECK(file->CreateEnumObject(&mem, D3DXF_FILELOAD_FROMMEMORY, &en) == D3DXFERR_PARSEERROR && !en);
    CHECK(SUCCEEDED(file->RegisterTemplates(D3DRM_XTEMPLATES, D3DRM_XTEMPLATE_BYTES)));
    CHECK(SUCCEEDED(file->CreateEnumObject(&mem, D3DXF_FILELOAD_FROMMEMORY, &en)) && en);
    if (!en) { file->Release(); return; }

    SIZE_T n = 0;
    CHECK(SUCCEEDED(en->GetChildren(&n)) && n == 5);
    ID3DXFileData* root = nullptr;
    CHECK(en->GetChild(5, &root) == E_INVALIDARG && !root);
    CHECK(SUCCEEDED(en->GetChild(1, &root)) && root);
    if (!root) { en->Release(); file->Release(); return; }
    GUID type;
    CHECK(SUCCEEDED(root->GetType(&type)) && type == TID_D3DRMFrame);
    CHECK(DataName(root) == "Root" && !root->IsReference());
    SIZE_T kids = 0;
    CHECK(SUCCEEDED(root->GetChildren(&kids)) && kids == 3);
    ID3DXFileData* ftm = Child3(root, 0);
    ID3DXFileData* mesh = Child3(root, 1);
    CHECK(ftm && mesh);
    if (ftm) {
        SIZE_T size = 0; const void* p = nullptr;
        CHECK(SUCCEEDED(ftm->GetType(&type)) && type == TID_D3DRMFrameTransformMatrix);
        CHECK(SUCCEEDED(ftm->Lock(&size, &p)) && size == 64 && ((const float*)p)[12] == 10.0f);
        CHECK(SUCCEEDED(ftm->Unlock()));
        // Unnamed object: D3DX reports an empty string of size 1.
        SIZE_T len = 0;
        CHECK(SUCCEEDED(ftm->GetName(nullptr, &len)) && len == 1);
        ftm->Release();
    }
    if (mesh) {
        CHECK(SUCCEEDED(mesh->GetType(&type)) && type == TID_D3DRMMesh);
        SIZE_T size = 0; const void* p = nullptr;
        CHECK(SUCCEEDED(mesh->Lock(&size, &p)) && size == 92);
        mesh->Unlock();
        char small[2];
        SIZE_T len = sizeof small;
        CHECK(mesh->GetName(small, &len) == D3DXFERR_BADVALUE);
        ID3DXFileData* mml = Child3(mesh, 2);
        CHECK(mml && SUCCEEDED(mml->GetType(&type)) && type == TID_D3DRMMeshMaterialList);
        if (mml) {
            ID3DXFileData* ref = Child3(mml, 0);
            CHECK(ref && ref->IsReference());
            if (ref) {
                CHECK(DataName(ref) == "Red" && SUCCEEDED(ref->GetType(&type)) && type == TID_D3DRMMaterial);
                CHECK(SUCCEEDED(ref->Lock(&size, &p)) && size == 44 && ((const float*)p)[4] == 5.0f);
                SIZE_T rk = 0;
                CHECK(SUCCEEDED(ref->GetChildren(&rk)) && rk == 1);   // the material's TextureFilename
                ID3DXFileData* tex = Child3(ref, 0);
                CHECK(tex && SUCCEEDED(tex->GetType(&type)) && type == TID_D3DRMTextureFilename);
                if (tex) {
                    CHECK(SUCCEEDED(tex->Lock(&size, &p)) && size == sizeof(char*) && std::strcmp(*(char* const*)p, "red.dds") == 0);
                    tex->Release();
                }
                ref->Release();
            }
            mml->Release();
        }
        ID3DXFileData* sw = Child3(mesh, 4);
        CHECK(sw && SUCCEEDED(sw->GetType(&type)) && type == kSkinWeightsId);
        if (sw) sw->Release();
        mesh->Release();
    }
    // GetEnum / GetFile return the owners.
    ID3DXFileEnumObject* en2 = nullptr;
    CHECK(SUCCEEDED(root->GetEnum(&en2)) && en2 == en);
    if (en2) en2->Release();
    ID3DXFile* file2 = nullptr;
    CHECK(SUCCEEDED(en->GetFile(&file2)) && file2 == file);
    if (file2) file2->Release();
    root->Release();

    // Lookups by name (nested) and by id.
    ID3DXFileData* bone = nullptr;
    CHECK(SUCCEEDED(en->GetDataObjectByName("Bone01", &bone)) && bone && DataName(bone) == "Bone01");
    if (bone) bone->Release();
    CHECK(en->GetDataObjectByName("Nope", &bone) == D3DXFERR_NOTFOUND && !bone);
    ID3DXFileData* c1 = nullptr;
    CHECK(SUCCEEDED(en->GetDataObjectById(kC1Id, &c1)) && c1 && DataName(c1) == "C1");
    if (c1) {
        GUID id;
        CHECK(SUCCEEDED(c1->GetId(&id)) && id == kC1Id);
        c1->Release();
    }

    // A data object keeps its document alive after the enum and file are released.
    ID3DXFileData* walk = nullptr;
    CHECK(SUCCEEDED(en->GetChild(2, &walk)));
    en->Release();

    // RegisterEnumTemplates: templates defined by one file, used by another.
    const char defs[] = "xof 0303txt 0032\ntemplate Pair { <01234567-89ab-cdef-0123-456789abcdef> DWORD a; DWORD b; }\n";
    const char use[] = "xof 0303txt 0032\nPair P { 1; 2; }\n";
    D3DXF_FILELOADMEMORY mdefs = { defs, sizeof(defs) - 1 }, muse = { use, sizeof(use) - 1 };
    ID3DXFileEnumObject* e1 = nullptr;
    ID3DXFileEnumObject* e2 = nullptr;
    CHECK(SUCCEEDED(file->CreateEnumObject(&mdefs, D3DXF_FILELOAD_FROMMEMORY, &e1)));
    CHECK(FAILED(file->CreateEnumObject(&muse, D3DXF_FILELOAD_FROMMEMORY, &e2)));   // not registered by enumeration
    CHECK(e1 && SUCCEEDED(file->RegisterEnumTemplates(e1)));
    CHECK(SUCCEEDED(file->CreateEnumObject(&muse, D3DXF_FILELOAD_FROMMEMORY, &e2)) && e2);
    if (e1) e1->Release();
    if (e2) e2->Release();

    // Files: Windows-style path, missing file, resource, save object.
    char dir[] = "/tmp/xfile_test.XXXXXX";
    CHECK(mkdtemp(dir) != nullptr);
    std::string posix = std::string(dir) + "/Scene.x";
    FILE* f = std::fopen(posix.c_str(), "wb");
    CHECK(f);
    if (f) { std::fwrite(kText, 1, sizeof(kText) - 1, f); std::fclose(f); }
    std::string windows = std::string(dir) + "\\SCENE.X";
    ID3DXFileEnumObject* ef = nullptr;
    CHECK(SUCCEEDED(file->CreateEnumObject(windows.c_str(), D3DXF_FILELOAD_FROMFILE, &ef)) && ef);
    if (ef) { SIZE_T k = 0; ef->GetChildren(&k); CHECK(k == 5); ef->Release(); }
    CHECK(file->CreateEnumObject("no\\such\\file.x", D3DXF_FILELOAD_FROMFILE, &ef) == D3DXFERR_FILENOTFOUND);
    D3DXF_FILELOADRESOURCE res = {};
    CHECK(file->CreateEnumObject(&res, D3DXF_FILELOAD_FROMRESOURCE, &ef) == D3DXFERR_RESOURCENOTFOUND);
    ID3DXFileSaveObject* save = nullptr;
    CHECK(file->CreateSaveObject("x.x", D3DXF_FILESAVE_TOFILE, D3DXF_FILEFORMAT_TEXT, &save) == E_NOTIMPL && !save);
    std::remove(posix.c_str());
    rmdir(dir);

    file->Release();   // still alive through walk -> enum -> file
    // The AnimationSet still works after everything else is gone.
    if (walk) {
        CHECK(DataName(walk) == "Walk" && SUCCEEDED(walk->GetType(&type)) && type == TID_D3DRMAnimationSet);
        ID3DXFileData* anim = Child3(walk, 0);
        CHECK(anim && SUCCEEDED(anim->GetType(&type)) && type == TID_D3DRMAnimation);
        if (anim) {
            ID3DXFileData* r = Child3(anim, 0);
            CHECK(r && r->IsReference() && DataName(r) == "Bone01" && SUCCEEDED(r->GetType(&type)) && type == TID_D3DRMFrame);
            if (r) r->Release();
            anim->Release();
        }
        walk->Release();
    }
}

static void TestLegacyApi()
{
    LPDIRECTXFILE file = nullptr;
    CHECK(SUCCEEDED(DirectXFileCreate(&file)) && file);
    if (!file) return;
    CHECK(SUCCEEDED(file->RegisterTemplates(D3DRM_XTEMPLATES, D3DRM_XTEMPLATE_BYTES)));
    DXFILELOADMEMORY mem = { (LPVOID)kText, (DWORD)(sizeof(kText) - 1) };
    LPDIRECTXFILEENUMOBJECT en = nullptr;
    CHECK(SUCCEEDED(file->CreateEnumObject(&mem, DXFILELOAD_FROMMEMORY, &en)) && en);
    if (!en) { file->Release(); return; }

    // Top-level order.
    const GUID* expectTypes[5] = { &TID_D3DRMMaterial, &TID_D3DRMFrame, &TID_D3DRMAnimationSet, &kCustomId, &TID_D3DRMMeshFace };
    LPDIRECTXFILEDATA objs[5] = {};
    for (int i = 0; i < 5; ++i) {
        CHECK(SUCCEEDED(en->GetNextDataObject(&objs[i])) && objs[i]);
        const GUID* type = nullptr;
        if (objs[i]) CHECK(SUCCEEDED(objs[i]->GetType(&type)) && type && *type == *expectTypes[i]);
    }
    LPDIRECTXFILEDATA extra = nullptr;
    CHECK(en->GetNextDataObject(&extra) == DXFILEERR_NOMOREOBJECTS && !extra);

    // Legacy names: size includes the NUL; unnamed objects report 0.
    DWORD len = 0;
    CHECK(objs[1] && SUCCEEDED(objs[1]->GetName(nullptr, &len)) && len == 5);
    char name[8] = {};
    len = sizeof name;
    CHECK(objs[1] && SUCCEEDED(objs[1]->GetName(name, &len)) && std::strcmp(name, "Root") == 0);
    len = 99;
    CHECK(objs[4] && SUCCEEDED(objs[4]->GetName(nullptr, &len)) && len == 0);

    // GetData whole and by member, on the Mesh (second child of Root).
    if (objs[1]) {
        LPDIRECTXFILEOBJECT o = nullptr;
        LPDIRECTXFILEDATA mesh = nullptr;
        CHECK(SUCCEEDED(objs[1]->GetNextObject(&o)));   // FrameTransformMatrix
        if (o) o->Release();
        CHECK(SUCCEEDED(objs[1]->GetNextObject(&o)) && o);
        if (o) {
            CHECK(SUCCEEDED(o->QueryInterface(IID_IDirectXFileData, (LPVOID*)&mesh)) && mesh);
            o->Release();
        }
        if (mesh) {
            DWORD size = 0; void* p = nullptr;
            CHECK(SUCCEEDED(mesh->GetData(nullptr, &size, &p)) && size == 92);
            BYTE* whole = (BYTE*)p;
            CHECK(SUCCEEDED(mesh->GetData("nFaces", &size, &p)) && size == 4 && p == whole + 52 && *(DWORD*)p == 2);
            CHECK(SUCCEEDED(mesh->GetData("vertices", &size, &p)) && size == 48 && p == whole + 4);
            CHECK(SUCCEEDED(mesh->GetData("faces", &size, &p)) && size == 36);
            CHECK(FAILED(mesh->GetData("nope", &size, &p)));
            mesh->Release();
        }
    }

    // Animation: a reference to Bone01, then an AnimationKey; then no more objects.
    if (objs[2]) {
        LPDIRECTXFILEOBJECT o = nullptr;
        LPDIRECTXFILEDATA anim = nullptr;
        CHECK(SUCCEEDED(objs[2]->GetNextObject(&o)) && o);
        if (o) { o->QueryInterface(IID_IDirectXFileData, (LPVOID*)&anim); o->Release(); }
        CHECK(anim);
        if (anim) {
            LPDIRECTXFILEDATAREFERENCE ref = nullptr;
            LPDIRECTXFILEDATA data = nullptr;
            CHECK(SUCCEEDED(anim->GetNextObject(&o)) && o);
            if (o) {
                CHECK(SUCCEEDED(o->QueryInterface(IID_IDirectXFileDataReference, (LPVOID*)&ref)) && ref);
                CHECK(o->QueryInterface(IID_IDirectXFileData, (LPVOID*)&data) == E_NOINTERFACE && !data);
                o->Release();
            }
            if (ref) {
                len = 0;
                CHECK(SUCCEEDED(ref->GetName(nullptr, &len)) && len == 7);
                LPDIRECTXFILEDATA bone = nullptr;
                CHECK(SUCCEEDED(ref->Resolve(&bone)) && bone);
                if (bone) {
                    const GUID* type = nullptr;
                    CHECK(SUCCEEDED(bone->GetType(&type)) && *type == TID_D3DRMFrame);
                    char bn[16]; len = sizeof bn;
                    CHECK(SUCCEEDED(bone->GetName(bn, &len)) && std::strcmp(bn, "Bone01") == 0);
                    bone->Release();
                }
                ref->Release();
            }
            CHECK(SUCCEEDED(anim->GetNextObject(&o)) && o);
            if (o) {
                CHECK(o->QueryInterface(IID_IDirectXFileDataReference, (LPVOID*)&ref) == E_NOINTERFACE);
                CHECK(SUCCEEDED(o->QueryInterface(IID_IDirectXFileData, (LPVOID*)&data)) && data);
                o->Release();
            }
            if (data) {
                const GUID* type = nullptr;
                DWORD size = 0; void* p = nullptr;
                CHECK(SUCCEEDED(data->GetType(&type)) && *type == TID_D3DRMAnimationKey);
                CHECK(SUCCEEDED(data->GetData(nullptr, &size, &p)) && size == 56 && ((DWORD*)p)[1] == 2);
                data->Release();
            }
            CHECK(anim->GetNextObject(&o) == DXFILEERR_NOMOREOBJECTS && !o);
            anim->Release();
        }
    }
    for (LPDIRECTXFILEDATA d : objs) if (d) d->Release();

    LPDIRECTXFILEDATA found = nullptr;
    CHECK(SUCCEEDED(en->GetDataObjectByName("Bone01", &found)) && found);
    if (found) found->Release();
    CHECK(SUCCEEDED(en->GetDataObjectById(kC1Id, &found)) && found);
    if (found) found->Release();
    en->Release();

    // Errors map to DXFILEERR_*.
    const char bad[] = "xof 0303tzip0032";
    DXFILELOADMEMORY mbad = { (LPVOID)bad, 16 };
    CHECK(file->CreateEnumObject(&mbad, DXFILELOAD_FROMMEMORY, &en) == DXFILEERR_BADFILECOMPRESSIONTYPE);
    const char unknown[] = "xof 0303txt 0032\nWhat { 1; }";
    DXFILELOADMEMORY munk = { (LPVOID)unknown, (DWORD)(sizeof(unknown) - 1) };
    CHECK(file->CreateEnumObject(&munk, DXFILELOAD_FROMMEMORY, &en) == DXFILEERR_NOTEMPLATE);
    CHECK(file->CreateEnumObject((LPVOID)"no\\such.x", DXFILELOAD_FROMFILE, &en) == DXFILEERR_FILENOTFOUND);
    CHECK(file->CreateEnumObject((LPVOID)"res", DXFILELOAD_FROMRESOURCE, &en) == DXFILEERR_RESOURCENOTFOUND);
    LPDIRECTXFILESAVEOBJECT save = nullptr;
    CHECK(file->CreateSaveObject("x.x", DXFILEFORMAT_TEXT, &save) == E_NOTIMPL && !save);
    // Legacy: templates of an enumerated file stay registered.
    const char defs[] = "xof 0303txt 0032\ntemplate Pair { <01234567-89ab-cdef-0123-456789abcdef> DWORD a; DWORD b; }\n";
    const char use[] = "xof 0303txt 0032\nPair P { 1; 2; }\n";
    DXFILELOADMEMORY mdefs = { (LPVOID)defs, (DWORD)(sizeof(defs) - 1) }, muse = { (LPVOID)use, (DWORD)(sizeof(use) - 1) };
    CHECK(SUCCEEDED(file->CreateEnumObject(&mdefs, DXFILELOAD_FROMMEMORY, &en)));
    if (en) en->Release();
    CHECK(SUCCEEDED(file->CreateEnumObject(&muse, DXFILELOAD_FROMMEMORY, &en)));
    if (en) en->Release();
    file->Release();
}

int main()
{
    std::printf("templates\n");   TestTemplates();
    std::printf("parser\n");      TestParser();
    std::printf("errors\n");      TestErrors();
    std::printf("D3DX API\n");    TestD3dxApi();
    std::printf("legacy API\n");  TestLegacyApi();
    std::printf("%s: %d checks, %d failed\n", g_failed ? "FAILED" : "OK", g_checks, g_failed);
    return g_failed ? 1 : 0;
}
