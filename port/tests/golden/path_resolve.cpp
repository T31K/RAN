// Phase 1 gate P1.5: every file under a client directory can be opened through the path the
// Windows game would build for it: backslash separators, ".\" prefixes, and any letter case
// (Windows is case-insensitive, the data's spelling is inconsistent). ResolvePath must land on
// the same file (same inode). Run on a case-sensitive volume too (check-paths.sh does both).
// Usage: path_resolve <client dir>   Exit 0 = every file resolves.
#include "ran_compat.h"
#include <cctype>
#include <cstdio>
#include <filesystem>
#include <string>
#include <sys/stat.h>
#include <unistd.h>

namespace fs = std::filesystem;

static std::string WindowsSpelling(const std::string& rel, int variant)
{
    std::string out = variant == 2 ? ".\\" : "";
    for (size_t i = 0; i < rel.size(); ++i) {
        char c = rel[i] == '/' ? '\\' : rel[i];
        if (std::isalpha((unsigned char)c)) {
            if (variant == 0) c = (char)std::toupper((unsigned char)c);
            else if (variant == 1) c = (char)std::tolower((unsigned char)c);
            else c = (i % 2) ? (char)std::toupper((unsigned char)c) : (char)std::tolower((unsigned char)c);
        }
        out += c;
    }
    return out;
}

int main(int argc, char** argv)
{
    if (argc < 2) { std::printf("usage: path_resolve <client dir>\n"); return 2; }
    const fs::path root = fs::canonical(argv[1]);
    if (::chdir(root.c_str()) != 0) { std::printf("cannot enter %s\n", root.c_str()); return 2; }

    size_t files = 0, failures = 0;
    for (const auto& e : fs::recursive_directory_iterator(".", fs::directory_options::skip_permission_denied)) {
        if (!e.is_regular_file()) continue;
        const std::string rel = e.path().lexically_normal().generic_string();
        struct stat want;
        if (::stat(rel.c_str(), &want) != 0) continue;
        ++files;
        for (int v = 0; v < 3; ++v) {
            const std::string win = WindowsSpelling(rel, v);
            const std::string got = ran_compat::ResolvePath(win.c_str());
            struct stat st;
            const bool ok = ::stat(got.c_str(), &st) == 0 && st.st_ino == want.st_ino && st.st_dev == want.st_dev;
            if (!ok && ++failures <= 10) std::printf("not resolved: %s -> %s\n", win.c_str(), got.c_str());
        }
    }
    std::printf("%s: %zu files x 3 Windows spellings, %zu failures\n", root.c_str(), files, failures);
    std::printf(failures == 0 && files > 0 ? "PASS\n" : "FAIL\n");
    return failures == 0 && files > 0 ? 0 : 1;
}
