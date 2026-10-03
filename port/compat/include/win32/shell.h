// Shell folders and ShellExecute for the native macOS build. "My Documents", AppData and
// Local AppData all map to the per-user data directory (see win32/userdata.h).
#pragma once
#include "win32/files.h"
#include "win32/userdata.h"
#include <spawn.h>
#include <sys/wait.h>

extern char** environ;

#define CSIDL_DESKTOP          0x0000
#define CSIDL_PERSONAL         0x0005
#define CSIDL_MYDOCUMENTS      CSIDL_PERSONAL
#define CSIDL_APPDATA          0x001a
#define CSIDL_LOCAL_APPDATA    0x001c
#define CSIDL_COMMON_APPDATA   0x0023
#define CSIDL_FLAG_CREATE      0x8000
#define SHGFP_TYPE_CURRENT     0

#define SW_SHOWDEFAULT_SHELL   10

inline BOOL SHGetSpecialFolderPath(HWND, char* path, int csidl, BOOL /*create*/)
{
    if (!path) return FALSE;
    const int folder = csidl & 0xff;
    if (folder != CSIDL_PERSONAL && folder != CSIDL_APPDATA && folder != CSIDL_LOCAL_APPDATA &&
        folder != CSIDL_COMMON_APPDATA && folder != CSIDL_DESKTOP) { path[0] = 0; return FALSE; }
    const std::string dir = ran_compat::UserDataDir();   // created on demand
    std::strncpy(path, dir.c_str(), MAX_PATH - 1);
    path[MAX_PATH - 1] = 0;
    return TRUE;
}
#define SHGetSpecialFolderPathA SHGetSpecialFolderPath

inline HRESULT SHGetFolderPath(HWND hwnd, int csidl, HANDLE, DWORD, char* path)
{
    return SHGetSpecialFolderPath(hwnd, path, csidl, TRUE) ? S_OK : E_FAIL;
}
#define SHGetFolderPathA SHGetFolderPath

// ShellExecute(..., "open", url-or-file, ...): hand it to macOS `open`, which picks the default
// browser/app. Returns > 32 on success, like Windows.
inline HINSTANCE ShellExecute(HWND, const char* /*verb*/, const char* file, const char* /*params*/, const char*, int)
{
    if (!file || !*file) return (HINSTANCE)(intptr_t)2;
    const std::string target = std::strstr(file, "://") ? std::string(file) : ran_compat::ResolvePath(file);
    char* argv[] = {(char*)"/usr/bin/open", (char*)target.c_str(), nullptr};
    pid_t pid = 0;
    if (::posix_spawn(&pid, "/usr/bin/open", nullptr, nullptr, argv, environ) != 0) return (HINSTANCE)(intptr_t)2;
    int status = 0;
    ::waitpid(pid, &status, 0);
    return (HINSTANCE)(intptr_t)(WIFEXITED(status) && WEXITSTATUS(status) == 0 ? 42 : 2);
}
#define ShellExecuteA ShellExecute
