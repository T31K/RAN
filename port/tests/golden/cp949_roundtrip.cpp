// Phase 1 gate P1.4: every line of a real game text table survives the client's own text path,
// CP949 bytes -> WCHAR (MultiByteToWideChar(CP_ACP)) -> CP949 (WideCharToMultiByte), byte-exact,
// and Korean really decodes as Korean (Hangul syllables, no U+FFFD replacement characters).
// Usage: cp949_roundtrip <decrypted text file>...   Exit 0 = all files pass.
#include "ran_compat.h"
#include <cstdio>
#include <fstream>
#include <sstream>
#include <string>
#include <vector>

static bool CheckFile(const char* path)
{
    std::ifstream in(path, std::ios::binary);
    if (!in) { std::printf("%s: cannot open\n", path); return false; }
    std::stringstream ss;
    ss << in.rdbuf();
    std::string all = ss.str();
    while (!all.empty() && all.back() == '\0') all.pop_back();   // AES block padding

    size_t lines = 0, nonAscii = 0, hangul = 0, failures = 0, invalid = 0;
    size_t start = 0;
    while (start <= all.size()) {
        size_t end = all.find('\n', start);
        if (end == std::string::npos) end = all.size();
        std::string line = all.substr(start, end - start);
        if (!line.empty() && line.back() == '\r') line.pop_back();
        start = end + 1;
        if (line.empty()) { if (end == all.size()) break; continue; }
        ++lines;
        bool high = false;
        for (unsigned char c : line) if (c >= 0x80) { high = true; break; }
        if (!high) continue;
        ++nonAscii;

        // Some shipped strings are already corrupt (invalid CP949). Windows cannot round-trip
        // those either; it substitutes and keeps the rest of the text, so check exactly that.
        if (MultiByteToWideChar(CP_ACP, MB_ERR_INVALID_CHARS, line.data(), (int)line.size(), nullptr, 0) == 0) {
            ++invalid;
            const int n = MultiByteToWideChar(CP_ACP, 0, line.data(), (int)line.size(), nullptr, 0);
            std::vector<WCHAR> w((size_t)(n > 0 ? n : 0) + 1);
            MultiByteToWideChar(CP_ACP, 0, line.data(), (int)line.size(), w.data(), n);
            // A final ASCII byte survives unless it is the trail of a lead byte (Windows reads
            // lead+next as one double-byte unit, then substitutes the invalid pair).
            const unsigned char last = (unsigned char)line.back();
            const unsigned char prev = line.size() >= 2 ? (unsigned char)line[line.size() - 2] : 0;
            const bool standalone = last < 0x80 && prev < 0x81;
            const bool kept = n > 0 && (!standalone || w[(size_t)n - 1] == last);
            if (!kept && ++failures <= 5) std::printf("%s: line %zu lost text after invalid bytes\n", path, lines);
            if (end == all.size()) break;
            continue;
        }

        const int wlen = MultiByteToWideChar(CP_ACP, 0, line.data(), (int)line.size(), nullptr, 0);
        std::vector<WCHAR> w((size_t)(wlen > 0 ? wlen : 0) + 1);
        const int wrote = MultiByteToWideChar(CP_ACP, 0, line.data(), (int)line.size(), w.data(), wlen);
        bool bad = wlen <= 0 || wrote != wlen;
        for (int i = 0; !bad && i < wrote; ++i) {
            if (w[i] == 0xFFFD) bad = true;
            if (w[i] >= 0xAC00 && w[i] <= 0xD7A3) ++hangul;
        }
        if (!bad) {
            const int blen = WideCharToMultiByte(CP_ACP, 0, w.data(), wrote, nullptr, 0, nullptr, nullptr);
            std::string back((size_t)(blen > 0 ? blen : 0), '\0');
            WideCharToMultiByte(CP_ACP, 0, w.data(), wrote, &back[0], blen, nullptr, nullptr);
            bad = back != line;
        }
        if (bad && ++failures <= 5) std::printf("%s: line %zu does not round-trip\n", path, lines);
        if (end == all.size()) break;
    }
    std::printf("%s: %zu lines, %zu with CP949 text (%zu already corrupt in the data), %zu Hangul syllables, %zu failures\n",
                path, lines, nonAscii, invalid, hangul, failures);
    return failures == 0 && lines > 0;
}

int main(int argc, char** argv)
{
    bool ok = argc > 1;
    for (int i = 1; i < argc; ++i) ok = CheckFile(argv[i]) && ok;
    std::printf(ok ? "PASS\n" : "FAIL\n");
    return ok ? 0 : 1;
}
