// System queries for the native macOS build: DLL loading, process heap, OS/file version info,
// process memory (psapi), crash dumps (dbghelp), path splitting, and a few user32 odds and ends.
// Windows-only facilities report "not available" in the way the game already handles.
#pragma once
#include "win32/kernel.h"
#include "win32/files.h"
#include <mach/mach.h>
#include <sys/sysctl.h>

// ---- DLLs: the game probes for Windows DLLs (ddraw, d3d8, dinput, imm32, version, IMEs) and
// falls back when they are missing. No Windows DLL exists here.
inline HMODULE LoadLibrary(const char*) { SetLastError(126 /*ERROR_MOD_NOT_FOUND*/); return nullptr; }
#define LoadLibraryA LoadLibrary
inline HMODULE LoadLibraryW(const WCHAR*) { return LoadLibrary(nullptr); }
inline HMODULE LoadLibraryEx(const char* name, HANDLE, DWORD) { return LoadLibrary(name); }
inline void* GetProcAddress(HMODULE, const char*) { return nullptr; }
inline BOOL FreeLibrary(HMODULE) { return TRUE; }
inline HMODULE GetModuleHandle(const char* name) { return name ? nullptr : (HMODULE)(uintptr_t)1; }   // NULL = this process
#define GetModuleHandleA GetModuleHandle

// ---- Process heap.
inline HANDLE GetProcessHeap() { return (HANDLE)(uintptr_t)1; }
#define HEAP_ZERO_MEMORY 0x00000008
inline void* HeapAlloc(HANDLE, DWORD flags, SIZE_T n) { return (flags & HEAP_ZERO_MEMORY) ? std::calloc(1, n) : std::malloc(n); }
inline void* HeapReAlloc(HANDLE, DWORD, void* p, SIZE_T n) { return std::realloc(p, n); }
inline BOOL HeapFree(HANDLE, DWORD, void* p) { std::free(p); return TRUE; }
#define GMEM_FIXED    0x0000
#define GMEM_MOVEABLE 0x0002
#define GMEM_ZEROINIT 0x0040
#define GHND (GMEM_MOVEABLE | GMEM_ZEROINIT)
inline HGLOBAL GlobalAlloc(UINT flags, SIZE_T n) { return (flags & GMEM_ZEROINIT) ? std::calloc(1, n) : std::malloc(n); }
inline HGLOBAL GlobalFree(HGLOBAL p) { std::free(p); return nullptr; }
inline void* GlobalLock(HGLOBAL p) { return p; }
inline BOOL GlobalUnlock(HGLOBAL) { return TRUE; }

// ---- OS version: report the Windows version family the client code expects to run on.
typedef struct _OSVERSIONINFOA {
    DWORD dwOSVersionInfoSize, dwMajorVersion, dwMinorVersion, dwBuildNumber, dwPlatformId;
    char  szCSDVersion[128];
} OSVERSIONINFOA, OSVERSIONINFO, *LPOSVERSIONINFOA, *LPOSVERSIONINFO;
typedef struct _OSVERSIONINFOEXA {
    DWORD dwOSVersionInfoSize, dwMajorVersion, dwMinorVersion, dwBuildNumber, dwPlatformId;
    char  szCSDVersion[128];
    WORD  wServicePackMajor, wServicePackMinor, wSuiteMask;
    BYTE  wProductType, wReserved;
} OSVERSIONINFOEXA, OSVERSIONINFOEX, *LPOSVERSIONINFOEXA, *LPOSVERSIONINFOEX;
#define VER_PLATFORM_WIN32_NT 2
inline BOOL GetVersionEx(OSVERSIONINFOA* v)
{
    if (!v) return FALSE;
    const DWORD size = v->dwOSVersionInfoSize;
    std::memset(((BYTE*)v) + sizeof(DWORD), 0, (size > sizeof(DWORD) ? size : sizeof(OSVERSIONINFOA)) - sizeof(DWORD));
    v->dwMajorVersion = 10;
    v->dwMinorVersion = 0;
    v->dwBuildNumber = 19045;
    v->dwPlatformId = VER_PLATFORM_WIN32_NT;
    return TRUE;
}
#define GetVersionExA GetVersionEx

// ---- File version resources (version.lib): there are none in a Mach-O binary.
typedef struct tagVS_FIXEDFILEINFO {
    DWORD dwSignature, dwStrucVersion, dwFileVersionMS, dwFileVersionLS, dwProductVersionMS, dwProductVersionLS;
    DWORD dwFileFlagsMask, dwFileFlags, dwFileOS, dwFileType, dwFileSubtype, dwFileDateMS, dwFileDateLS;
} VS_FIXEDFILEINFO;
inline DWORD GetFileVersionInfoSize(const char*, LPDWORD handle) { if (handle) *handle = 0; return 0; }
inline BOOL GetFileVersionInfo(const char*, DWORD, DWORD, void*) { return FALSE; }
inline BOOL VerQueryValue(const void*, const char*, void** out, UINT* len) { if (out) *out = nullptr; if (len) *len = 0; return FALSE; }
#define GetFileVersionInfoSizeA GetFileVersionInfoSize
#define GetFileVersionInfoA GetFileVersionInfo
#define VerQueryValueA VerQueryValue

// ---- Process memory (psapi): real numbers from the Mach task.
typedef struct _PROCESS_MEMORY_COUNTERS {
    DWORD cb, PageFaultCount;
    SIZE_T PeakWorkingSetSize, WorkingSetSize, QuotaPeakPagedPoolUsage, QuotaPagedPoolUsage;
    SIZE_T QuotaPeakNonPagedPoolUsage, QuotaNonPagedPoolUsage, PagefileUsage, PeakPagefileUsage;
} PROCESS_MEMORY_COUNTERS, *PPROCESS_MEMORY_COUNTERS;
inline HANDLE GetCurrentProcess() { return (HANDLE)(intptr_t)-1; }
inline DWORD GetCurrentProcessId() { return (DWORD)::getpid(); }
inline BOOL GetProcessMemoryInfo(HANDLE, PPROCESS_MEMORY_COUNTERS pmc, DWORD cb)
{
    if (!pmc || cb < sizeof(*pmc)) return FALSE;
    std::memset(pmc, 0, sizeof(*pmc));
    pmc->cb = sizeof(*pmc);
    mach_task_basic_info info;
    mach_msg_type_number_t count = MACH_TASK_BASIC_INFO_COUNT;
    if (task_info(mach_task_self(), MACH_TASK_BASIC_INFO, (task_info_t)&info, &count) != KERN_SUCCESS) return FALSE;
    pmc->WorkingSetSize = (SIZE_T)info.resident_size;
    pmc->PeakWorkingSetSize = (SIZE_T)info.resident_size_max;
    pmc->PagefileUsage = (SIZE_T)info.virtual_size;
    return TRUE;
}
inline BOOL GlobalMemoryStatusEx(LPMEMORYSTATUSEX m)
{
    if (!m) return FALSE;
    uint64_t total = 0;
    size_t len = sizeof(total);
    ::sysctlbyname("hw.memsize", &total, &len, nullptr, 0);
    m->ullTotalPhys = total;
    m->ullAvailPhys = total / 2;   // rough; only used for diagnostics
    m->ullTotalVirtual = m->ullAvailVirtual = total;
    m->dwMemoryLoad = 50;
    return TRUE;
}

// ---- Crash dumps (dbghelp): crash reporting is replaced natively (plan Phase 4).
typedef enum _MINIDUMP_TYPE { MiniDumpNormal = 0, MiniDumpWithDataSegs = 1, MiniDumpWithFullMemory = 2 } MINIDUMP_TYPE;
typedef struct _MINIDUMP_EXCEPTION_INFORMATION { DWORD ThreadId; void* ExceptionPointers; BOOL ClientPointers; } MINIDUMP_EXCEPTION_INFORMATION, *PMINIDUMP_EXCEPTION_INFORMATION;
inline BOOL MiniDumpWriteDump(HANDLE, DWORD, HANDLE, MINIDUMP_TYPE, const void*, const void*, const void*) { return FALSE; }

// ---- Path splitting (MSVC <stdlib.h>): both separators, like the Windows CRT.
inline void _splitpath(const char* path, char* drive, char* dir, char* fname, char* ext)
{
    const char* p = path ? path : "";
    if (drive) drive[0] = 0;
    if (p[0] && p[1] == ':') { if (drive) { drive[0] = p[0]; drive[1] = ':'; drive[2] = 0; } p += 2; }
    const char* lastSep = nullptr;
    for (const char* c = p; *c; ++c) if (*c == '\\' || *c == '/') lastSep = c;
    const char* name = lastSep ? lastSep + 1 : p;
    if (dir) { const size_t n = (size_t)(name - p); std::memcpy(dir, p, n); dir[n] = 0; }
    const char* dot = std::strrchr(name, '.');
    if (fname) { const size_t n = dot ? (size_t)(dot - name) : std::strlen(name); std::memcpy(fname, name, n); fname[n] = 0; }
    if (ext) { if (dot) std::strcpy(ext, dot); else ext[0] = 0; }
}
inline errno_t _splitpath_s(const char* path, char* drive, size_t, char* dir, size_t, char* fname, size_t, char* ext, size_t)
{
    _splitpath(path, drive, dir, fname, ext);
    return 0;
}
inline void _makepath(char* out, const char* drive, const char* dir, const char* fname, const char* ext)
{
    out[0] = 0;
    if (drive && *drive) { std::strcat(out, drive); if (drive[1] != ':') std::strcat(out, ":"); }
    if (dir && *dir) {
        std::strcat(out, dir);
        const char last = dir[std::strlen(dir) - 1];
        if (last != '\\' && last != '/') std::strcat(out, "\\");
    }
    if (fname) std::strcat(out, fname);
    if (ext && *ext) { if (*ext != '.') std::strcat(out, "."); std::strcat(out, ext); }
}
inline errno_t _makepath_s(char* out, size_t, const char* drive, const char* dir, const char* fname, const char* ext)
{
    _makepath(out, drive, dir, fname, ext);
    return 0;
}

// ---- user32 odds and ends owned by the Phase 2 platform layer.
#define KEYEVENTF_EXTENDEDKEY 0x0001
#define KEYEVENTF_KEYUP       0x0002
inline void keybd_event(BYTE, BYTE, DWORD, ULONG_PTR) {}
namespace ran_compat {
    inline BOOL (*&CursorPosHook())(POINT*) { static BOOL (*hook)(POINT*) = nullptr; return hook; }
}
inline BOOL GetCursorPos(POINT* p)
{
    if (ran_compat::CursorPosHook()) return ran_compat::CursorPosHook()(p);
    if (p) p->x = p->y = 0;
    return FALSE;
}
inline BOOL SetCursorPos(int, int) { return FALSE; }
inline HCURSOR SetCursor(HCURSOR) { return nullptr; }
inline int ShowCursor(BOOL show) { return show ? 0 : -1; }
inline BOOL ScreenToClient(HWND, POINT*) { return TRUE; }
inline BOOL ClientToScreen(HWND, POINT*) { return TRUE; }
inline UINT RegisterClipboardFormat(const char*) { return 0xC000; }
#define RegisterClipboardFormatA RegisterClipboardFormat

// ---- Misc constants.
#ifndef INVALID_FILE_SIZE
#define INVALID_FILE_SIZE 0xFFFFFFFFu
#endif
#ifndef SEVERITY_ERROR
#define SEVERITY_ERROR 1
#define SEVERITY_SUCCESS 0
#endif
#ifndef MAKE_HRESULT
#define MAKE_HRESULT(sev, fac, code) ((HRESULT)(((unsigned long)(sev) << 31) | ((unsigned long)(fac) << 16) | ((unsigned long)(code))))
#endif
#ifndef RPC_S_OK
#define RPC_S_OK 0
#endif
#ifndef CO_E_NOTINITIALIZED
#define CO_E_NOTINITIALIZED ((HRESULT)0x800401F0L)
#endif
inline HRESULT CoInitialize(void*) { return S_OK; }
inline HRESULT CoInitializeEx(void*, DWORD) { return S_OK; }
inline void CoUninitialize() {}
