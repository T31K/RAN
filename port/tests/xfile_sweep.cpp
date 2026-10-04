// Opt-in check of the .x reader (port/d3dx/xfile.cpp) against real game data: parses every .x
// file under a folder through D3DXFileCreate (and DirectXFileCreate), walks every data object
// and checks that each Mesh-family blob's size matches the counts inside it.
// Not part of the default test run (needs the client data):
//   port/build/tests-xfile/xfile_sweep ~/Projects/RAN/client
#include "../d3dx/xfile.h"
#include <d3dx9.h>
#include <dxfile.h>
#include <rmxfguid.h>
#include "../../Dependency/directx/rmxftmpl.h"
#include <cstdio>
#include <cstring>
#include <filesystem>
#include <map>
#include <string>
#include <vector>

namespace fs = std::filesystem;

namespace {

std::map<std::string, int> g_typeCounts;
int g_refs = 0, g_restrictionViolations = 0;

DWORD U32(const BYTE* p, size_t off) { DWORD v; std::memcpy(&v, p + off, 4); return v; }

// Expected blob size of the common templates, computed from the counts in the blob; 0 = not checked.
bool CheckBlob(const GUID& type, const BYTE* d, size_t n, std::string& why)
{
    auto need = [&](size_t off) { return off <= n; };
    size_t expect = 0;
    if (type == TID_D3DRMMesh || type == TID_D3DRMMeshNormals) {
        if (!need(4)) { why = "short"; return false; }
        size_t off = 4 + (size_t)U32(d, 0) * 12;
        if (!need(off + 4)) { why = "short vertices"; return false; }
        DWORD nf = U32(d, off); off += 4;
        for (DWORD i = 0; i < nf; ++i) {
            if (!need(off + 4)) { why = "short faces"; return false; }
            off += 4 + (size_t)U32(d, off) * 4;
        }
        expect = off;
    } else if (type == TID_D3DRMMeshTextureCoords) {
        expect = n >= 4 ? 4 + (size_t)U32(d, 0) * 8 : 4;
    } else if (type == TID_D3DRMMeshMaterialList) {
        expect = n >= 8 ? 8 + (size_t)U32(d, 4) * 4 : 8;
    } else if (type == TID_D3DRMFrameTransformMatrix) {
        expect = 64;
    } else if (type == TID_D3DRMMaterial) {
        expect = 16 + 4 + 12 + 12;
    } else if (type == TID_D3DRMTextureFilename) {
        expect = sizeof(char*);
        if (n == expect) {
            const char* s; std::memcpy(&s, d, sizeof s);
            if (!s) { why = "null string"; return false; }
        }
    } else if (type == TID_D3DRMAnimationKey) {
        if (n < 8) { why = "short"; return false; }
        size_t off = 8;
        for (DWORD i = 0, k = U32(d, 4); i < k; ++i) {
            if (!need(off + 8)) { why = "short keys"; return false; }
            off += 8 + (size_t)U32(d, off + 4) * 4;
        }
        expect = off;
    } else if (type == TID_D3DRMMeshVertexColors) {
        expect = n >= 4 ? 4 + (size_t)U32(d, 0) * 20 : 4;
    } else {
        return true;
    }
    if (expect != n) {
        char buf[96];
        std::snprintf(buf, sizeof buf, "size %zu, counts say %zu", n, expect);
        why = buf;
        return false;
    }
    return true;
}

bool Walk(ID3DXFileData* data, const std::string& path, int depth)
{
    GUID type;
    SIZE_T size = 0;
    const void* blob = nullptr;
    if (FAILED(data->GetType(&type)) || FAILED(data->Lock(&size, &blob))) { std::printf("%s: GetType/Lock failed\n", path.c_str()); return false; }
    SIZE_T nameLen = 0;
    data->GetName(nullptr, &nameLen);
    std::string why;
    bool ok = true;
    if (data->IsReference()) {
        ++g_refs;
    } else if (!CheckBlob(type, (const BYTE*)blob, size, why)) {
        std::printf("%s: blob check failed at depth %d: %s\n", path.c_str(), depth, why.c_str());
        ok = false;
    }
    data->Unlock();
    if (data->IsReference()) return ok;   // do not descend into referenced objects
    SIZE_T n = 0;
    data->GetChildren(&n);
    for (SIZE_T i = 0; i < n; ++i) {
        ID3DXFileData* c = nullptr;
        if (FAILED(data->GetChild(i, &c))) { std::printf("%s: GetChild failed\n", path.c_str()); return false; }
        ok = Walk(c, path, depth + 1) && ok;
        c->Release();
    }
    return ok;
}

void CountTypes(const ran_xfile::Object& o)
{
    ++g_typeCounts[o.tmpl->name];
    for (const ran_xfile::Child& c : o.children) {
        if (!c.object) continue;
        const ran_xfile::Template& t = *o.tmpl;
        bool allowed = t.openness == ran_xfile::Template::OPEN;
        if (t.openness == ran_xfile::Template::RESTRICTED)
            for (const auto& a : t.allowed)
                if (a.name == c.object->tmpl->name) allowed = true;
        if (!allowed) ++g_restrictionViolations;
        CountTypes(*c.object);
    }
}

} // namespace

int main(int argc, char** argv)
{
    if (argc < 2) { std::printf("usage: xfile_sweep <folder>\n"); return 2; }
    ID3DXFile* file = nullptr;
    if (FAILED(D3DXFileCreate(&file)) || FAILED(file->RegisterTemplates(D3DRM_XTEMPLATES, D3DRM_XTEMPLATE_BYTES))) {
        std::printf("D3DXFileCreate/RegisterTemplates failed\n");
        return 1;
    }
    LPDIRECTXFILE legacy = nullptr;
    if (FAILED(DirectXFileCreate(&legacy)) || FAILED(legacy->RegisterTemplates(D3DRM_XTEMPLATES, D3DRM_XTEMPLATE_BYTES))) {
        std::printf("DirectXFileCreate/RegisterTemplates failed\n");
        return 1;
    }
    ran_xfile::TemplateSet base;
    {
        ran_xfile::Document d;
        ran_xfile::Parse(D3DRM_XTEMPLATES, D3DRM_XTEMPLATE_BYTES, ran_xfile::TemplateSet(), d);
        base = d.templates;
    }
    int files = 0, failed = 0, binary = 0, text = 0;
    size_t objects = 0;
    for (const auto& e : fs::recursive_directory_iterator(argv[1])) {
        if (!e.is_regular_file()) continue;
        std::string ext = e.path().extension().string();
        if (ext != ".x" && ext != ".X") continue;
        const std::string path = e.path().string();
        ++files;
        bool ok = true;
        ID3DXFileEnumObject* en = nullptr;
        HRESULT hr = file->CreateEnumObject(path.c_str(), D3DXF_FILELOAD_FROMFILE, &en);
        if (FAILED(hr)) {
            std::printf("%s: CreateEnumObject 0x%08x\n", path.c_str(), (unsigned)hr);
            ok = false;
        } else {
            SIZE_T n = 0;
            en->GetChildren(&n);
            objects += n;
            for (SIZE_T i = 0; i < n; ++i) {
                ID3DXFileData* d = nullptr;
                if (FAILED(en->GetChild(i, &d))) { ok = false; break; }
                ok = Walk(d, path, 0) && ok;
                d->Release();
            }
            en->Release();
        }
        // Legacy API on the same file: top-level objects must enumerate too.
        LPDIRECTXFILEENUMOBJECT len = nullptr;
        hr = legacy->CreateEnumObject((LPVOID)path.c_str(), DXFILELOAD_FROMFILE, &len);
        if (FAILED(hr)) {
            std::printf("%s: legacy CreateEnumObject 0x%08x\n", path.c_str(), (unsigned)hr);
            ok = false;
        } else {
            LPDIRECTXFILEDATA d = nullptr;
            while (SUCCEEDED(len->GetNextDataObject(&d))) d->Release();
            len->Release();
        }
        // Internal API: type histogram and child-restriction statistics.
        {
            FILE* f = std::fopen(path.c_str(), "rb");
            std::vector<BYTE> bytes;
            if (f) {
                std::fseek(f, 0, SEEK_END);
                bytes.resize((size_t)std::ftell(f));
                std::fseek(f, 0, SEEK_SET);
                if (std::fread(bytes.data(), 1, bytes.size(), f) != bytes.size()) bytes.clear();
                std::fclose(f);
            }
            if (bytes.size() >= 16) (std::memcmp(&bytes[8], "bin ", 4) == 0 ? binary : text)++;
            ran_xfile::Document d;
            if (ran_xfile::Parse(bytes.data(), bytes.size(), base, d) == ran_xfile::Error::Ok)
                for (const auto& o : d.objects) CountTypes(*o);
        }
        if (!ok) ++failed;
    }
    file->Release();
    legacy->Release();
    std::printf("files: %d (binary %d, text %d), top-level objects: %zu, references: %d, failures: %d\n",
                files, binary, text, objects, g_refs, failed);
    std::printf("children outside their parent's restriction: %d\n", g_restrictionViolations);
    for (const auto& t : g_typeCounts) std::printf("  %-28s %d\n", t.first.c_str(), t.second);
    return failed ? 1 : 0;
}
