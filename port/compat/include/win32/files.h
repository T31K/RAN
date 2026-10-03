// Path resolution + Win32/CRT file stand-ins for the native macOS build.
// The game builds Windows paths ("data\\textures\\Foo.DDS"). ResolvePath turns them into
// POSIX paths and, when the exact spelling does not exist (case-sensitive volumes), matches
// each component case-insensitively against what is on disk. Requires win32/kernel.h.
#pragma once
#include "win32/kernel.h"
#include <cerrno>
#include <cstdio>
#include <dirent.h>
#include <mach-o/dyld.h>
#include <string>
#include <sys/stat.h>
#include <unistd.h>
#include <vector>

#ifndef FILE_ATTRIBUTE_READONLY
#define FILE_ATTRIBUTE_READONLY  0x01u
#endif
#ifndef FILE_ATTRIBUTE_DIRECTORY
#define FILE_ATTRIBUTE_DIRECTORY 0x10u
#endif
#ifndef FILE_ATTRIBUTE_NORMAL
#define FILE_ATTRIBUTE_NORMAL    0x80u
#endif
#ifndef INVALID_FILE_ATTRIBUTES
#define INVALID_FILE_ATTRIBUTES  0xFFFFFFFFu
#endif
#ifndef GENERIC_READ
#define GENERIC_READ             0x80000000u
#endif
#ifndef GENERIC_WRITE
#define GENERIC_WRITE            0x40000000u
#endif
#ifndef FILE_SHARE_READ
#define FILE_SHARE_READ          0x1u
#endif
#ifndef FILE_SHARE_WRITE
#define FILE_SHARE_WRITE         0x2u
#endif
#ifndef CREATE_NEW
#define CREATE_NEW               1u
#endif
#ifndef CREATE_ALWAYS
#define CREATE_ALWAYS            2u
#endif
#ifndef OPEN_EXISTING
#define OPEN_EXISTING            3u
#endif
#ifndef OPEN_ALWAYS
#define OPEN_ALWAYS              4u
#endif
#ifndef TRUNCATE_EXISTING
#define TRUNCATE_EXISTING        5u
#endif
#ifndef FILE_BEGIN
#define FILE_BEGIN               0u
#endif
#ifndef FILE_CURRENT
#define FILE_CURRENT             1u
#endif
#ifndef FILE_END
#define FILE_END                 2u
#endif
#ifndef INVALID_HANDLE_VALUE
#define INVALID_HANDLE_VALUE     ((HANDLE)(intptr_t)-1)
#endif
#ifndef INVALID_SET_FILE_POINTER
#define INVALID_SET_FILE_POINTER 0xFFFFFFFFu
#endif
#ifndef ERROR_FILE_NOT_FOUND
#define ERROR_FILE_NOT_FOUND     2u
#endif
#ifndef ERROR_ALREADY_EXISTS
#define ERROR_ALREADY_EXISTS     183u
#endif

namespace ran_compat {

    inline bool PathExists(const std::string& p)
    {
        struct stat st;
        return ::stat(p.c_str(), &st) == 0;
    }

    // Backslashes -> slashes, doubled separators collapsed.
    inline std::string NormalizeSlashes(const char* winPath)
    {
        std::string p;
        p.reserve(std::strlen(winPath));
        for (const char* c = winPath; *c; ++c) {
            const char ch = (*c == '\\') ? '/' : *c;
            if (ch == '/' && !p.empty() && p.back() == '/') continue;
            p += ch;
        }
        return p;
    }

    // Matches every component against the directory listing (exact name first, then
    // case-insensitive), so the result is the on-disk spelling on any volume. Components
    // that do not exist are kept as given.
    inline std::string ResolvePathOnDisk(const std::string& p)
    {
        if (p.empty()) return p;
        const bool absolute = p[0] == '/';
        std::vector<std::string> parts;
        size_t start = absolute ? 1 : 0;
        while (start <= p.size()) {
            const size_t slash = p.find('/', start);
            const size_t end = slash == std::string::npos ? p.size() : slash;
            if (end > start) parts.push_back(p.substr(start, end - start));
            if (slash == std::string::npos) break;
            start = slash + 1;
        }

        std::string out = absolute ? "/" : "";
        for (size_t i = 0; i < parts.size(); ++i) {
            const std::string& part = parts[i];
            std::string candidate = out + part;
            if (part != "." && part != "..") {
                const std::string dirPath = out.empty() ? "." : out;
                if (DIR* dir = ::opendir(dirPath.c_str())) {
                    std::string caseMatch;
                    bool exact = false;
                    while (dirent* ent = ::readdir(dir)) {
                        if (part == ent->d_name) { exact = true; break; }
                        if (caseMatch.empty() && strcasecmp(ent->d_name, part.c_str()) == 0) caseMatch = ent->d_name;
                    }
                    ::closedir(dir);
                    if (!exact && !caseMatch.empty()) candidate = out + caseMatch;
                }
            }
            out = candidate;
            if (i + 1 < parts.size()) out += '/';
        }
        return out;
    }

    // Windows path -> POSIX path. Fast path: the normalised path exists as spelled (always the
    // case on the default case-insensitive APFS). Otherwise resolve component by component.
    inline std::string ResolvePath(const char* winPath)
    {
        const std::string p = NormalizeSlashes(winPath);
        if (p.empty() || PathExists(p)) return p;
        return ResolvePathOnDisk(p);
    }

    inline FILE* fopen_resolved(const char* path, const char* mode) { return std::fopen(ResolvePath(path).c_str(), mode); }

    struct KFile : KObject {
        FILE* f = nullptr;
        bool IsSignaled() const override { return true; }
        ~KFile() override { if (f) std::fclose(f); }
    };
}

inline errno_t fopen_s(FILE** out, const char* path, const char* mode)
{
    if (!out) return EINVAL;
    *out = ran_compat::fopen_resolved(path, mode);
    return *out ? 0 : errno;
}

inline DWORD GetFileAttributes(const char* path)
{
    struct stat st;
    if (::stat(ran_compat::ResolvePath(path).c_str(), &st) != 0) { SetLastError(ERROR_FILE_NOT_FOUND); return INVALID_FILE_ATTRIBUTES; }
    DWORD attrs = S_ISDIR(st.st_mode) ? FILE_ATTRIBUTE_DIRECTORY : FILE_ATTRIBUTE_NORMAL;
    if (!(st.st_mode & S_IWUSR)) attrs |= FILE_ATTRIBUTE_READONLY;
    return attrs;
}
#define GetFileAttributesA GetFileAttributes

inline BOOL PathFileExists(const char* path) { return ran_compat::PathExists(ran_compat::ResolvePath(path)) ? TRUE : FALSE; }
#define PathFileExistsA PathFileExists

inline BOOL CreateDirectory(const char* path, SECURITY_ATTRIBUTES*)
{
    const std::string p = ran_compat::ResolvePath(path);
    if (::mkdir(p.c_str(), 0755) == 0) return TRUE;
    SetLastError(errno == EEXIST ? ERROR_ALREADY_EXISTS : ERROR_FILE_NOT_FOUND);
    return FALSE;
}
#define CreateDirectoryA CreateDirectory

inline BOOL DeleteFile(const char* path) { return ::unlink(ran_compat::ResolvePath(path).c_str()) == 0 ? TRUE : FALSE; }
#define DeleteFileA DeleteFile

inline BOOL CopyFile(const char* from, const char* to, BOOL failIfExists)
{
    const std::string dst = ran_compat::ResolvePath(to);
    if (failIfExists && ran_compat::PathExists(dst)) { SetLastError(ERROR_ALREADY_EXISTS); return FALSE; }
    FILE* in = ran_compat::fopen_resolved(from, "rb");
    if (!in) return FALSE;
    FILE* out = std::fopen(dst.c_str(), "wb");
    if (!out) { std::fclose(in); return FALSE; }
    char buf[65536];
    size_t n;
    while ((n = std::fread(buf, 1, sizeof(buf), in)) > 0) std::fwrite(buf, 1, n, out);
    std::fclose(in);
    std::fclose(out);
    return TRUE;
}
#define CopyFileA CopyFile

inline DWORD GetCurrentDirectory(DWORD size, char* buf)
{
    char tmp[4096];
    if (!::getcwd(tmp, sizeof(tmp))) return 0;
    const DWORD len = (DWORD)std::strlen(tmp);
    if (!buf || size <= len) return len + 1;
    std::memcpy(buf, tmp, len + 1);
    return len;
}
#define GetCurrentDirectoryA GetCurrentDirectory

inline BOOL SetCurrentDirectory(const char* path) { return ::chdir(ran_compat::ResolvePath(path).c_str()) == 0 ? TRUE : FALSE; }
#define SetCurrentDirectoryA SetCurrentDirectory

inline DWORD GetModuleFileName(HMODULE, char* buf, DWORD size)
{
    char tmp[4096];
    uint32_t len = sizeof(tmp);
    if (_NSGetExecutablePath(tmp, &len) != 0) return 0;
    char real[4096];
    const char* path = ::realpath(tmp, real) ? real : tmp;
    const size_t n = std::strlen(path);
    if (!buf || size == 0) return 0;
    const size_t copy = n < size - 1 ? n : size - 1;
    std::memcpy(buf, path, copy);
    buf[copy] = 0;
    return (DWORD)copy;
}
#define GetModuleFileNameA GetModuleFileName

inline HANDLE CreateFile(const char* path, DWORD access, DWORD, SECURITY_ATTRIBUTES*, DWORD disposition, DWORD, HANDLE)
{
    const std::string p = ran_compat::ResolvePath(path);
    const bool exists = ran_compat::PathExists(p);
    const bool wantWrite = (access & GENERIC_WRITE) != 0;
    const char* mode = nullptr;
    switch (disposition) {
    case CREATE_NEW:        if (exists) { SetLastError(ERROR_ALREADY_EXISTS); return INVALID_HANDLE_VALUE; } mode = "w+b"; break;
    case CREATE_ALWAYS:     mode = "w+b"; break;
    case OPEN_EXISTING:     if (!exists) { SetLastError(ERROR_FILE_NOT_FOUND); return INVALID_HANDLE_VALUE; } mode = wantWrite ? "r+b" : "rb"; break;
    case OPEN_ALWAYS:       mode = exists ? (wantWrite ? "r+b" : "rb") : "w+b"; break;
    case TRUNCATE_EXISTING: if (!exists) { SetLastError(ERROR_FILE_NOT_FOUND); return INVALID_HANDLE_VALUE; } mode = "w+b"; break;
    default: return INVALID_HANDLE_VALUE;
    }
    FILE* f = std::fopen(p.c_str(), mode);
    if (!f) { SetLastError(ERROR_FILE_NOT_FOUND); return INVALID_HANDLE_VALUE; }
    auto k = std::make_shared<ran_compat::KFile>();
    k->f = f;
    return ran_compat::MakeHandle(k);
}
#define CreateFileA CreateFile

namespace ran_compat {
    inline FILE* FileOf(HANDLE h)
    {
        if (!h || h == INVALID_HANDLE_VALUE) return nullptr;
        auto* k = dynamic_cast<KFile*>(Obj(h));
        return k ? k->f : nullptr;
    }
}

inline BOOL ReadFile(HANDLE h, void* buf, DWORD n, LPDWORD read, void*)
{
    FILE* f = ran_compat::FileOf(h);
    if (!f) return FALSE;
    const size_t got = std::fread(buf, 1, n, f);
    if (read) *read = (DWORD)got;
    return std::ferror(f) ? FALSE : TRUE;
}

inline BOOL WriteFile(HANDLE h, const void* buf, DWORD n, LPDWORD written, void*)
{
    FILE* f = ran_compat::FileOf(h);
    if (!f) return FALSE;
    const size_t put = std::fwrite(buf, 1, n, f);
    if (written) *written = (DWORD)put;
    return put == n ? TRUE : FALSE;
}

inline DWORD GetFileSize(HANDLE h, LPDWORD high)
{
    FILE* f = ran_compat::FileOf(h);
    if (!f) return INVALID_FILE_ATTRIBUTES;
    std::fflush(f);
    struct stat st;
    if (::fstat(fileno(f), &st) != 0) return INVALID_FILE_ATTRIBUTES;
    if (high) *high = (DWORD)((uint64_t)st.st_size >> 32);
    return (DWORD)st.st_size;
}

inline DWORD SetFilePointer(HANDLE h, LONG distance, LONG* high, DWORD method)
{
    FILE* f = ran_compat::FileOf(h);
    if (!f) return INVALID_SET_FILE_POINTER;
    int64_t off = distance;
    if (high) off = ((int64_t)*high << 32) | (uint32_t)distance;
    const int whence = method == FILE_BEGIN ? SEEK_SET : method == FILE_CURRENT ? SEEK_CUR : SEEK_END;
    if (std::fseek(f, (long)off, whence) != 0) return INVALID_SET_FILE_POINTER;
    const int64_t pos = std::ftell(f);
    if (high) *high = (LONG)(pos >> 32);
    return (DWORD)pos;
}
