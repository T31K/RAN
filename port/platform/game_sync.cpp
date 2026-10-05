// See game_sync.h.
#include "game_sync.h"
#include <copyfile.h>
#include <cstdio>
#include <fstream>
#include <removefile.h>
#include <sstream>
#include <sys/stat.h>
#include <unistd.h>

namespace ran_platform {
namespace {

const char* const kStamp = "/.data-version";
const char* const kContent[] = { "data", "textures", "sounds" };

bool ReadFile(const std::string& path, std::string& out)
{
    std::ifstream f(path, std::ios::binary);
    if (!f) return false;
    std::stringstream ss;
    ss << f.rdbuf();
    out = ss.str();
    return true;
}

bool IsDirectory(const std::string& p)
{
    struct stat st;
    return ::stat(p.c_str(), &st) == 0 && S_ISDIR(st.st_mode);
}

void RemoveTree(const std::string& p)
{
    if (::access(p.c_str(), F_OK) == 0) removefile(p.c_str(), nullptr, REMOVEFILE_RECURSIVE);
}

} // namespace

bool GameDataStale(const std::string& bundledGame, const std::string& userGame)
{
    std::string want, have;
    if (!ReadFile(bundledGame + kStamp, want)) return false;   // dev bundle: nothing to compare
    return !ReadFile(userGame + kStamp, have) || have != want;
}

bool SyncGameData(const std::string& bundledGame, const std::string& userGame)
{
    bool ok = true;
    for (const char* name : kContent) {
        const std::string src = bundledGame + "/" + name, dst = userGame + "/" + name;
        if (!IsDirectory(src)) continue;
        // Clone next to the live folder, then swap: the game never sees a half-copied folder,
        // and an interrupted sync is simply redone on the next launch (the stamp is copied last).
        const std::string fresh = dst + ".sync-new", old = dst + ".sync-old";
        RemoveTree(fresh);
        RemoveTree(old);
        if (copyfile(src.c_str(), fresh.c_str(), nullptr, COPYFILE_ALL | COPYFILE_RECURSIVE | COPYFILE_CLONE) != 0) {
            std::fprintf(stderr, "[platform] game sync: could not copy %s\n", src.c_str());
            RemoveTree(fresh);
            ok = false;
            continue;
        }
        if (IsDirectory(dst) && ::rename(dst.c_str(), old.c_str()) != 0) { RemoveTree(fresh); ok = false; continue; }
        if (::rename(fresh.c_str(), dst.c_str()) != 0) {
            ::rename(old.c_str(), dst.c_str());   // put the old folder back
            ok = false;
            continue;
        }
        RemoveTree(old);
    }
    if (ok) ok = copyfile((bundledGame + kStamp).c_str(), (userGame + kStamp).c_str(), nullptr, COPYFILE_ALL) == 0;
    return ok;
}

} // namespace ran_platform
