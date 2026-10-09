// Load skin .x files through the native port's own D3DXLoadMeshHierarchyFromXA
// (port/d3dx/d3dx9_xload.cpp, on the fake device of port/tests/d3dx_mesh_test.cpp) and print what
// DxSkinMesh9 gets: the mesh container name ("<frame>[Mesh]" for an unnamed mesh frame), vertex /
// face / material counts, skin bones, and ConvertToIndexedBlendedMesh with the engine's HLSL
// palette size (SMeshContainer::GenerateSkinnedMeshHLSL: 26 matrices).
// Build + run: scripts/custom_mobs/xload_check.sh <file.x> ...
#define main d3dx_mesh_test_main
#include "d3dx_mesh_test.cpp"
#undef main
#include <d3dx9.h>
#include <string>

namespace {

char* Dup(const char* s)
{
    if (!s) return nullptr;
    char* d = new char[strlen(s) + 1];
    strcpy(d, s);
    return d;
}

struct Alloc : public ID3DXAllocateHierarchy {
    STDMETHOD(CreateFrame)(THIS_ const char* name, D3DXFRAME** out) override
    {
        auto* f = new D3DXFRAME();
        std::memset(f, 0, sizeof(*f));
        f->Name = Dup(name);
        D3DXMatrixIdentity(&f->TransformationMatrix);
        *out = f;
        return D3D_OK;
    }
    STDMETHOD(CreateMeshContainer)(THIS_ const char* name, const D3DXMESHDATA* md, const D3DXMATERIAL* mats,
                                   const D3DXEFFECTINSTANCE*, DWORD nmat, const DWORD*, ID3DXSkinInfo* skin,
                                   D3DXMESHCONTAINER** out) override
    {
        auto* c = new D3DXMESHCONTAINER();
        std::memset(c, 0, sizeof(*c));
        c->Name = Dup(name);
        c->MeshData = *md;
        if (md->pMesh) md->pMesh->AddRef();
        c->NumMaterials = nmat;
        c->pMaterials = new D3DXMATERIAL[nmat];
        for (DWORD i = 0; i < nmat; ++i) {
            c->pMaterials[i] = mats[i];
            c->pMaterials[i].pTextureFilename = Dup(mats[i].pTextureFilename);
        }
        c->pSkinInfo = skin;
        if (skin) skin->AddRef();
        *out = c;
        return D3D_OK;
    }
    STDMETHOD(DestroyFrame)(THIS_ LPD3DXFRAME) override { return D3D_OK; }
    STDMETHOD(DestroyMeshContainer)(THIS_ LPD3DXMESHCONTAINER) override { return D3D_OK; }
};

int g_frames = 0, g_bad = 0;

void Walk(D3DXFRAME* f, D3DXFRAME* parent)
{
    ++g_frames;
    if (f->pMeshContainer) {
        D3DXMESHCONTAINER* c = f->pMeshContainer;
        std::string frameName = (f->Name && *f->Name) ? f->Name
                              : (parent && parent->Name ? std::string(parent->Name) + "[Mesh]" : "");
        std::string contName = (c->Name && *c->Name) ? c->Name : frameName;
        ID3DXMesh* m = c->MeshData.pMesh;
        DWORD nattr = 0;
        m->GetAttributeTable(nullptr, &nattr);
        std::printf("  mesh container '%s': %lu verts %lu faces, FVF 0x%lx, %lu attribute ranges, %lu materials",
                    contName.c_str(), (unsigned long)m->GetNumVertices(), (unsigned long)m->GetNumFaces(),
                    (unsigned long)m->GetFVF(), (unsigned long)nattr, (unsigned long)c->NumMaterials);
        for (DWORD i = 0; i < c->NumMaterials; ++i)
            std::printf(" [%s]", c->pMaterials[i].pTextureFilename ? c->pMaterials[i].pTextureFilename : "-");
        std::printf("\n");
        if (c->pSkinInfo) {
            ID3DXSkinInfo* s = c->pSkinInfo;
            DWORD maxv = 0;
            s->GetMaxVertexInfluences(&maxv);
            DWORD maxFace = 0, ncomb = 0;
            ID3DXBuffer* comb = nullptr;
            ID3DXMesh* blended = nullptr;
            HRESULT hr = s->ConvertToIndexedBlendedMesh(m, D3DXMESH_MANAGED, 26, nullptr, nullptr, nullptr, nullptr,
                                                        &maxFace, &ncomb, &comb, &blended);
            std::printf("  skin: %lu bones, max %lu influences/vertex; ConvertToIndexedBlendedMesh(26): hr 0x%08lx, "
                        "%lu bone combinations\n", (unsigned long)s->GetNumBones(), (unsigned long)maxv,
                        (unsigned long)hr, (unsigned long)ncomb);
            if (FAILED(hr)) ++g_bad;
        } else {
            std::printf("  NO skin info\n");
            ++g_bad;
        }
    }
    if (f->pFrameFirstChild) Walk(f->pFrameFirstChild, f);
    if (f->pFrameSibling) Walk(f->pFrameSibling, parent);
}

}  // namespace

int main(int argc, char** argv)
{
    FakeDevice dev;
    for (int i = 1; i < argc; ++i) {
        Alloc alloc;
        D3DXFRAME* root = nullptr;
        ID3DXAnimationController* ctrl = nullptr;
        g_frames = 0;
        HRESULT hr = D3DXLoadMeshHierarchyFromXA(argv[i], D3DXMESH_MANAGED, &dev, &alloc, nullptr, &root, &ctrl);
        std::printf("%s: D3DXLoadMeshHierarchyFromXA hr 0x%08lx\n", argv[i], (unsigned long)hr);
        if (FAILED(hr) || !root) { ++g_bad; continue; }
        Walk(root, nullptr);
        D3DXVECTOR3 c;
        float r = 0;
        hr = D3DXFrameCalculateBoundingSphere(root, &c, &r);
        std::printf("  %d frames; bounding sphere hr 0x%08lx centre (%.2f %.2f %.2f) radius %.2f\n", g_frames,
                    (unsigned long)hr, c.x, c.y, c.z, r);
    }
    std::printf("%s\n", g_bad ? "FAILED" : "all loaded");
    return g_bad ? 1 : 0;
}
