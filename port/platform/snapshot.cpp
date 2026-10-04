// Frame snapshots for remote testing (native build only): the game saves its own back buffer, so
// no screen-recording permission is involved.
//   RAN_SNAPSHOT_AT="20,30"   seconds after the first frame at which to save a frame
//   RAN_SNAPSHOT_DIR=/tmp     where to write ran_snap_<seconds>.tga (default /tmp)
// Called from CD3DApplication::Present() just before the frame is presented.
#include <d3d9.h>
#include <chrono>
#include <cstdio>
#include <cstdlib>
#include <string>
#include <vector>

namespace {

struct Schedule
{
    std::vector<int> seconds;
    std::string dir = "/tmp";
    size_t next = 0;
    std::chrono::steady_clock::time_point start;
    bool started = false;
};

Schedule& Get()
{
    static Schedule s = [] {
        Schedule r;
        if (const char* at = std::getenv("RAN_SNAPSHOT_AT")) {
            for (const char* p = at; *p;) {
                char* end = nullptr;
                const long v = std::strtol(p, &end, 10);
                if (end == p) break;
                r.seconds.push_back((int)v);
                p = *end ? end + 1 : end;
            }
        }
        if (const char* d = std::getenv("RAN_SNAPSHOT_DIR")) r.dir = d;
        return r;
    }();
    return s;
}

// 32-bit uncompressed TGA, top-left origin, from a locked X8R8G8B8/A8R8G8B8 surface.
bool WriteTga(const std::string& path, const D3DLOCKED_RECT& lr, UINT w, UINT h)
{
    FILE* f = std::fopen(path.c_str(), "wb");
    if (!f) return false;
    unsigned char hdr[18] = {};
    hdr[2] = 2;
    hdr[12] = (unsigned char)(w & 0xFF); hdr[13] = (unsigned char)(w >> 8);
    hdr[14] = (unsigned char)(h & 0xFF); hdr[15] = (unsigned char)(h >> 8);
    hdr[16] = 32;
    hdr[17] = 0x20 | 8;
    std::fwrite(hdr, 1, sizeof(hdr), f);
    std::vector<unsigned char> row(w * 4);
    for (UINT y = 0; y < h; ++y) {
        const unsigned char* src = (const unsigned char*)lr.pBits + (size_t)y * lr.Pitch;
        for (UINT x = 0; x < w; ++x) {
            row[x * 4 + 0] = src[x * 4 + 0];
            row[x * 4 + 1] = src[x * 4 + 1];
            row[x * 4 + 2] = src[x * 4 + 2];
            row[x * 4 + 3] = 255;
        }
        std::fwrite(row.data(), 1, row.size(), f);
    }
    std::fclose(f);
    return true;
}

void Save(IDirect3DDevice9* dev, const std::string& path)
{
    IDirect3DSurface9* bb = nullptr;
    if (FAILED(dev->GetBackBuffer(0, 0, D3DBACKBUFFER_TYPE_MONO, &bb)) || !bb) return;
    D3DSURFACE_DESC d;
    bb->GetDesc(&d);
    IDirect3DSurface9* sys = nullptr;
    if (SUCCEEDED(dev->CreateOffscreenPlainSurface(d.Width, d.Height, d.Format, D3DPOOL_SYSTEMMEM, &sys, nullptr)) &&
        SUCCEEDED(dev->GetRenderTargetData(bb, sys))) {
        D3DLOCKED_RECT lr;
        if (SUCCEEDED(sys->LockRect(&lr, nullptr, D3DLOCK_READONLY))) {
            const bool ok = WriteTga(path, lr, d.Width, d.Height);
            sys->UnlockRect();
            std::fprintf(stderr, "[snapshot] %s %s (%ux%u)\n", ok ? "saved" : "FAILED", path.c_str(), d.Width, d.Height);
        }
    }
    if (sys) sys->Release();
    bb->Release();
}

} // namespace

void RanNativeSnapshot(IDirect3DDevice9* dev)
{
    Schedule& s = Get();
    if (!dev || s.next >= s.seconds.size()) return;
    const auto now = std::chrono::steady_clock::now();
    if (!s.started) { s.start = now; s.started = true; }
    const double t = std::chrono::duration<double>(now - s.start).count();
    if (t < s.seconds[s.next]) return;
    Save(dev, s.dir + "/ran_snap_" + std::to_string(s.seconds[s.next]) + ".tga");
    ++s.next;
}
