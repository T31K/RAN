// port/platform/game_sync.cpp: refreshing a user's game copy from a newer app bundle.
#include "../platform/game_sync.h"
#include <cstdio>
#include <cstdlib>
#include <fstream>
#include <sstream>
#include <string>
#include <sys/stat.h>
#include <unistd.h>

static int g_fail = 0;
#define CHECK(c) do { if (!(c)) { std::fprintf(stderr, "FAIL %s:%d %s\n", __FILE__, __LINE__, #c); ++g_fail; } } while (0)

static void Put(const std::string& p, const std::string& s)
{
    for (size_t at = 1; (at = p.find('/', at)) != std::string::npos; ++at) ::mkdir(p.substr(0, at).c_str(), 0755);
    std::ofstream(p) << s;
}
static std::string Get(const std::string& p)
{
    std::ifstream f(p);
    std::stringstream ss;
    ss << f.rdbuf();
    return f ? ss.str() : "<missing>";
}
static bool Exists(const std::string& p) { return ::access(p.c_str(), F_OK) == 0; }

int main()
{
    char tmpl[] = "/tmp/game_sync_test.XXXXXX";
    const std::string root = mkdtemp(tmpl);
    const std::string bundle = root + "/bundle", mine = root + "/mine";

    // v0.2 install: a full copy without a data-version stamp, plus the player's settings.
    Put(mine + "/data/glogic/glogic.rcc", "old items");
    Put(mine + "/data/skin/gone.X", "removed in the new version");
    Put(mine + "/textures/item/sword.dds", "old texture");
    Put(mine + "/option.ini", "player settings");
    Put(mine + "/cache/tex.dds", "player cache");

    Put(bundle + "/data/glogic/glogic.rcc", "new items");
    Put(bundle + "/data/skin/s_m_shd.X", "samehada");
    Put(bundle + "/textures/item/sword.dds", "new texture");
    Put(bundle + "/sounds/hit.wav", "sound");
    Put(bundle + "/option.ini", "bundle defaults");
    Put(bundle + "/.data-version", "v3\n");

    CHECK(ran_platform::GameDataStale(bundle, mine));
    CHECK(ran_platform::SyncGameData(bundle, mine));
    CHECK(Get(mine + "/data/glogic/glogic.rcc") == "new items");
    CHECK(Get(mine + "/data/skin/s_m_shd.X") == "samehada");
    CHECK(!Exists(mine + "/data/skin/gone.X"));                 // data mirrors the bundle
    CHECK(Get(mine + "/textures/item/sword.dds") == "new texture");
    CHECK(Get(mine + "/sounds/hit.wav") == "sound");
    CHECK(Get(mine + "/option.ini") == "player settings");       // root files stay the player's
    CHECK(Get(mine + "/cache/tex.dds") == "player cache");
    CHECK(Get(mine + "/.data-version") == "v3\n");
    CHECK(!ran_platform::GameDataStale(bundle, mine));            // up to date now

    // Same stamp: nothing happens even if a file differs.
    Put(mine + "/data/glogic/glogic.rcc", "edited locally");
    CHECK(!ran_platform::GameDataStale(bundle, mine));

    // New bundle version: synced again; no leftover temporary folders.
    Put(bundle + "/.data-version", "v4\n");
    CHECK(ran_platform::GameDataStale(bundle, mine));
    CHECK(ran_platform::SyncGameData(bundle, mine));
    CHECK(Get(mine + "/data/glogic/glogic.rcc") == "new items");
    CHECK(!Exists(mine + "/data.sync-new") && !Exists(mine + "/data.sync-old"));

    // A bundle without a stamp (dev build) never triggers a sync.
    ::unlink((bundle + "/.data-version").c_str());
    CHECK(!ran_platform::GameDataStale(bundle, mine));

    std::system(("rm -rf '" + root + "'").c_str());
    if (g_fail) return 1;
    std::printf("game_sync_test: ok\n");
    return 0;
}
