// Per-user writable directory for the native macOS build. Everything the Windows game writes
// into "My Documents" / AppData / the registry lands here, because the signed .app bundle is
// read-only and ~/Documents would trigger a macOS privacy prompt.
//   default: ~/Library/Application Support/RanOdyssey
//   override: RAN_USER_DATA_DIR (tests, portable installs)
#pragma once
#include <cerrno>
#include <cstdlib>
#include <string>
#include <sys/stat.h>

namespace ran_compat {

    // mkdir -p
    inline bool MakeDirs(const std::string& path)
    {
        if (path.empty()) return false;
        std::string cur;
        for (size_t i = 0; i <= path.size(); ++i) {
            if (i == path.size() || path[i] == '/') {
                if (!cur.empty() && ::mkdir(cur.c_str(), 0755) != 0 && errno != EEXIST) return false;
            }
            if (i < path.size()) cur += path[i];
        }
        return true;
    }

    inline std::string UserDataDir()
    {
        std::string dir;
        if (const char* over = std::getenv("RAN_USER_DATA_DIR")) dir = over;
        else {
            const char* home = std::getenv("HOME");
            dir = std::string(home ? home : "/tmp") + "/Library/Application Support/RanOdyssey";
        }
        MakeDirs(dir);
        return dir;
    }
}
