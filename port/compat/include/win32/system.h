// System queries for the native macOS build: DLL loading, process heap, OS/file version info,
// process memory (psapi), crash dumps (dbghelp), path splitting, and a few user32 odds and ends.
// Windows-only facilities report "not available" in the way the game already handles.
#pragma once
#include "win32/kernel.h"
#include "win32/files.h"
#include "win32/user_types.h"
#include "win32/gdi_types.h"
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
// Other processes (the game's process-list diagnostics): not inspectable natively.
#define PROCESS_QUERY_INFORMATION 0x0400
#define PROCESS_VM_READ           0x0010
inline HANDLE OpenProcess(DWORD, BOOL, DWORD) { return NULL; }
inline DWORD GetModuleFileNameEx(HANDLE, HMODULE, char* buf, DWORD size) { if (buf && size) buf[0] = 0; return 0; }
#define GetModuleFileNameExA GetModuleFileNameEx
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
inline HWND SetFocus(HWND) { return nullptr; }     // ::SetFocus - keyboard focus belongs to the SDL window
inline HWND GetFocus() { return nullptr; }
inline HWND GetActiveWindow() { return nullptr; }
inline HWND GetForegroundWindow() { return nullptr; }
inline LRESULT SendMessage(HWND, UINT, WPARAM, LPARAM) { return 0; }
inline BOOL PostMessage(HWND, UINT, WPARAM, LPARAM) { return FALSE; }
#define SendMessageA SendMessage
#define PostMessageA PostMessage
inline DWORD GetWindowThreadProcessId(HWND, LPDWORD pid) { if (pid) *pid = (DWORD)::getpid(); return GetCurrentThreadId(); }
inline int SetMapMode(HDC, int) { return 1; /* MM_TEXT */ }
inline int GetObject(HANDLE, int, void* out) { (void)out; return 0; }
#define GetObjectA GetObject
inline BOOL DestroyCursor(HCURSOR) { return TRUE; }
inline BOOL LocalFileTimeToFileTime(const FILETIME* in, FILETIME* out) { if (in && out) *out = *in; return TRUE; }
#define DIB_RGB_COLORS 0
#define CLSCTX_INPROC_SERVER 0x1
#define ERROR_NOT_SUPPORTED 50u
#define EXCEPTION_BREAKPOINT 0x80000003u
typedef void (CALLBACK *LPTIMECALLBACK)(UINT, UINT, DWORD_PTR, DWORD_PTR, DWORD_PTR);
typedef struct tagSTICKYKEYS { UINT cbSize; DWORD dwFlags; } STICKYKEYS, *LPSTICKYKEYS;
typedef struct _STARTUPINFOA { DWORD cb; char* lpReserved; char* lpDesktop; char* lpTitle;
    DWORD dwX, dwY, dwXSize, dwYSize, dwXCountChars, dwYCountChars, dwFillAttribute, dwFlags;
    WORD wShowWindow, cbReserved2; BYTE* lpReserved2; HANDLE hStdInput, hStdOutput, hStdError; } STARTUPINFOA, STARTUPINFO, *LPSTARTUPINFO;
typedef struct _PROCESS_INFORMATION { HANDLE hProcess, hThread; DWORD dwProcessId, dwThreadId; } PROCESS_INFORMATION, *LPPROCESS_INFORMATION;
inline BOOL CreateProcess(const char*, char*, void*, void*, BOOL, DWORD, void*, const char*, LPSTARTUPINFO, LPPROCESS_INFORMATION pi)
{
    if (pi) std::memset(pi, 0, sizeof(*pi));
    return FALSE;   // the game only launches Windows helpers (patcher/launcher)
}
#define CreateProcessA CreateProcess
typedef struct _PERF_DATA_BLOCK { WCHAR Signature[4]; DWORD LittleEndian, Version, Revision, TotalByteLength, HeaderLength, NumObjectTypes; } PERF_DATA_BLOCK, *PPERF_DATA_BLOCK;
#define SPI_GETSTICKYKEYS 0x003A
#define SPI_SETSTICKYKEYS 0x003B
inline BOOL SystemParametersInfo(UINT, UINT, void*, UINT) { return FALSE; }
#define SystemParametersInfoA SystemParametersInfo
typedef struct tagTOGGLEKEYS { UINT cbSize; DWORD dwFlags; } TOGGLEKEYS, *LPTOGGLEKEYS;
typedef struct _PERF_OBJECT_TYPE { DWORD TotalByteLength, DefinitionLength, HeaderLength, ObjectNameTitleIndex; } PERF_OBJECT_TYPE, *PPERF_OBJECT_TYPE;
#define SPI_GETTOGGLEKEYS 0x0034
#define SPI_SETTOGGLEKEYS 0x0035
#define TIME_ONESHOT  0x0000
#define TIME_PERIODIC 0x0001
inline UINT timeSetEvent(UINT, UINT, LPTIMECALLBACK, DWORD_PTR, UINT) { return 0; }   // multimedia timers: unused natively
inline UINT timeKillEvent(UINT) { return 0; }
inline BOOL SetFileTime(HANDLE, const FILETIME*, const FILETIME*, const FILETIME*) { return FALSE; }
inline void RaiseException(DWORD code, DWORD, DWORD, const ULONG_PTR*) { std::fprintf(stderr, "RaiseException 0x%08x\n", (unsigned)code); std::abort(); }
#define LOGPIXELSX 88
#define LOGPIXELSY 90
inline int GetDeviceCaps(HDC, int index) { return (index == LOGPIXELSX || index == LOGPIXELSY) ? 96 : 0; }
#define GWL_WNDPROC   (-4)
#define GWL_HINSTANCE (-6)
#define GWL_ID        (-12)
#define GWL_USERDATA  (-21)
inline LONG GetWindowLong(HWND, int) { return 0; }
inline LONG SetWindowLong(HWND, int, LONG) { return 0; }
#define GetWindowLongA GetWindowLong
#define SetWindowLongA SetWindowLong
inline int GetDIBits(HDC, HBITMAP, UINT, UINT, void*, void* /*BITMAPINFO*/, UINT) { return 0; }
inline HBITMAP CreateDIBSection(HDC, const void* /*BITMAPINFO*/, UINT, void** bits, HANDLE, DWORD) { if (bits) *bits = nullptr; return nullptr; }
inline HRESULT CoCreateInstance(REFCLSID, IUnknown*, DWORD, REFIID, void** out) { if (out) *out = nullptr; return E_NOINTERFACE; }
typedef struct tagFILTERKEYS { UINT cbSize; DWORD dwFlags, iWaitMSec, iDelayMSec, iRepeatMSec, iBounceMSec; } FILTERKEYS, *LPFILTERKEYS;
typedef struct _PERF_INSTANCE_DEFINITION { DWORD ByteLength, ParentObjectTitleIndex, ParentObjectInstance; LONG UniqueID; DWORD NameOffset, NameLength; } PERF_INSTANCE_DEFINITION, *PPERF_INSTANCE_DEFINITION;
typedef float* PFLOAT;
#define SPI_GETFILTERKEYS 0x0032
#define SPI_SETFILTERKEYS 0x0033
#define TRANSPARENT 1
#define OPAQUE      2
#define TIME_CALLBACK_FUNCTION    0x0000
#define TIME_CALLBACK_EVENT_SET   0x0010
#define TIME_CALLBACK_EVENT_PULSE 0x0020
inline int MulDiv(int a, int b, int c) { return c ? (int)(((long long)a * b + (c / 2)) / c) : -1; }
#define SM_CXCURSOR 13
#define SM_CYCURSOR 14
inline int GetSystemMetrics(int index)
{
    // Phase 2 reports the real screen through SDL; until then a common desktop size.
    switch (index) { case SM_CXSCREEN: return 1920; case SM_CYSCREEN: return 1080; case SM_CXCURSOR: case SM_CYCURSOR: return 32; default: return 0; }
}
#define GCLP_HCURSOR (-12)
#define GCL_HCURSOR  (-12)
inline ULONG_PTR SetClassLongPtr(HWND, int, LONG_PTR) { return 0; }
inline ULONG_PTR GetClassLongPtr(HWND, int) { return 0; }
#define SetClassLong SetClassLongPtr
#define GetClassLong GetClassLongPtr
inline HWND WindowFromPoint(POINT) { return nullptr; }
inline HCURSOR GetCursor() { return nullptr; }
inline int GetWindowText(HWND, char* buf, int max) { if (buf && max > 0) buf[0] = 0; return 0; }
#define GetWindowTextA GetWindowText
#define SMTO_NORMAL 0x0000
#define SMTO_ABORTIFHUNG 0x0002
inline LRESULT SendMessageTimeout(HWND, UINT, WPARAM, LPARAM, UINT, UINT, PDWORD_PTR result) { if (result) *result = 0; return 0; }
// The game was a 32-bit build where DWORD_PTR == DWORD, so it passes DWORD* here.
inline LRESULT SendMessageTimeout(HWND h, UINT m, WPARAM w, LPARAM l, UINT f, UINT t, DWORD* result)
{
    DWORD_PTR r = 0;
    const LRESULT ok = SendMessageTimeout(h, m, w, l, f, t, &r);
    if (result) *result = (DWORD)r;
    return ok;
}
#define PM_NOREMOVE 0x0000
#define PM_REMOVE   0x0001
namespace ran_compat {
    // Installed by the platform layer: pumps SDL events into the game and reports at most one
    // Win32-style message back (WM_QUIT when the app should exit). wait = block for an event
    // (GetMessage while the game is inactive), remove = PM_REMOVE.
    inline BOOL (*&MessageHook())(MSG* msg, BOOL wait, BOOL remove) { static BOOL (*hook)(MSG*, BOOL, BOOL) = nullptr; return hook; }
}
inline BOOL PeekMessage(MSG* msg, HWND, UINT, UINT, UINT flags)
{
    if (ran_compat::MessageHook()) return ran_compat::MessageHook()(msg, FALSE, (flags & PM_REMOVE) != 0);
    if (msg) std::memset(msg, 0, sizeof(*msg));
    return FALSE;
}
#define PeekMessageA PeekMessage
// Clipboard: Phase 2 maps it onto SDL_GetClipboardText / SDL_SetClipboardText.
#define CF_TEXT 1
inline BOOL OpenClipboard(HWND) { return FALSE; }
inline BOOL CloseClipboard() { return TRUE; }
inline BOOL EmptyClipboard() { return FALSE; }
inline HANDLE GetClipboardData(UINT) { return nullptr; }
inline HANDLE SetClipboardData(UINT, HANDLE) { return nullptr; }
inline BOOL IsClipboardFormatAvailable(UINT) { return FALSE; }
// UUIDs (rpc.h).
typedef GUID UUID;
#define RPC_S_UUID_LOCAL_ONLY 1824
inline long UuidCreate(UUID* u)
{
    if (!u) return 1;
    arc4random_buf(u, sizeof(*u));
    u->Data3 = (WORD)((u->Data3 & 0x0FFF) | 0x4000);                   // version 4
    u->Data4[0] = (BYTE)((u->Data4[0] & 0x3F) | 0x80);                 // RFC 4122 variant
    return 0;   // RPC_S_OK
}
typedef long RPC_STATUS;
typedef struct _PERF_COUNTER_BLOCK { DWORD ByteLength; } PERF_COUNTER_BLOCK, *PPERF_COUNTER_BLOCK;
#ifndef DECLSPEC_IMPORT
#define DECLSPEC_IMPORT
#endif
inline UINT WinExec(const char*, UINT) { return 2; }   // ERROR_FILE_NOT_FOUND: no Windows programs to start
inline int SetBkMode(HDC, int) { return 0; }
inline BOOL MoveWindow(HWND, int, int, int, int, BOOL) { return FALSE; }
inline BOOL ClipCursor(const RECT*) { return TRUE; }   // Phase 2: SDL mouse grab
inline BOOL GetClipCursor(RECT* r) { if (r) SetRectEmpty(r); return TRUE; }
inline BOOL IsIconic(HWND) { return FALSE; }
inline BOOL IsZoomed(HWND) { return FALSE; }
typedef void* HTASK;
#define CS_VREDRAW  0x0001
#define CS_HREDRAW  0x0002
#define CS_DBLCLKS  0x0008
#define CS_OWNDC    0x0020
inline BOOL UnregisterClass(const char*, HINSTANCE) { return TRUE; }
#define UnregisterClassA UnregisterClass
inline HWND FindWindow(const char*, const char*) { return nullptr; }   // no other game windows to find
#define FindWindowA FindWindow
#define EM_SETSEL       0x00B1
#define EM_LIMITTEXT    0x00C5
#define CB_GETITEMDATA  0x0150
#define CB_SETITEMDATA  0x0151
#define CB_ADDSTRING    0x0143
#define CB_RESETCONTENT 0x014B
inline HCURSOR SetCursor(HCURSOR) { return nullptr; }
inline int ShowCursor(BOOL show) { return show ? 0 : -1; }
inline BOOL ScreenToClient(HWND, POINT*) { return TRUE; }
inline BOOL ClientToScreen(HWND, POINT*) { return TRUE; }
inline UINT RegisterClipboardFormat(const char*) { return 0xC000; }
#define RegisterClipboardFormatA RegisterClipboardFormat

// ---- Global user32/GDI functions on HWNDs that do not exist in the native build (Phase 2 owns
// the real window); they report an empty window.
typedef struct tagWINDOWPLACEMENT {
    UINT length, flags, showCmd;
    POINT ptMinPosition, ptMaxPosition;
    RECT rcNormalPosition;
} WINDOWPLACEMENT, *LPWINDOWPLACEMENT;
typedef struct _ICONINFO { BOOL fIcon; DWORD xHotspot, yHotspot; HBITMAP hbmMask, hbmColor; } ICONINFO, *PICONINFO;
typedef struct tagBITMAPINFOHEADER {
    DWORD biSize; LONG biWidth, biHeight; WORD biPlanes, biBitCount;
    DWORD biCompression, biSizeImage; LONG biXPelsPerMeter, biYPelsPerMeter; DWORD biClrUsed, biClrImportant;
} BITMAPINFOHEADER, *LPBITMAPINFOHEADER;
typedef struct tagRGBQUAD { BYTE rgbBlue, rgbGreen, rgbRed, rgbReserved; } RGBQUAD;
#pragma pack(push, 2)
typedef struct tagBITMAPFILEHEADER { WORD bfType; DWORD bfSize; WORD bfReserved1, bfReserved2; DWORD bfOffBits; } BITMAPFILEHEADER, *LPBITMAPFILEHEADER;
#pragma pack(pop)
static_assert(sizeof(BITMAPFILEHEADER) == 14, "BITMAPFILEHEADER is read straight from .bmp files");
// Keyboard layout / code page: the game is a Korean (CP949) client.
inline HKL GetKeyboardLayout(DWORD) { return (HKL)(uintptr_t)0x04120412; }   // ko-KR
inline UINT GetACP() { return 949; }
typedef struct tagBITMAPINFO { BITMAPINFOHEADER bmiHeader; RGBQUAD bmiColors[1]; } BITMAPINFO, *LPBITMAPINFO;
inline BOOL GetWindowRect(HWND, RECT* r) { if (r) SetRectEmpty(r); return FALSE; }
inline BOOL GetClientRect(HWND, RECT* r) { if (r) SetRectEmpty(r); return FALSE; }
inline HDC GetDC(HWND) { return nullptr; }
inline int ReleaseDC(HWND, HDC) { return 0; }
inline HWND GetDlgItem(HWND, int) { return nullptr; }
inline BOOL GetWindowPlacement(HWND, WINDOWPLACEMENT* wp) { if (wp) std::memset(wp, 0, sizeof(*wp)); return FALSE; }
inline BOOL SetWindowPlacement(HWND, const WINDOWPLACEMENT*) { return FALSE; }
inline HBRUSH CreateSolidBrush(COLORREF) { return nullptr; }
inline HDC CreateCompatibleDC(HDC) { return nullptr; }
inline BOOL DeleteDC(HDC) { return TRUE; }
inline BOOL DeleteObject(HGDIOBJ) { return TRUE; }
inline HGDIOBJ SelectObject(HDC, HGDIOBJ) { return nullptr; }
inline HCURSOR LoadCursor(HINSTANCE, const char*) { return nullptr; }
#define LoadCursorA LoadCursor
#define IDC_ARROW ((const char*)(uintptr_t)32512)
#define QS_POSTMESSAGE 0x0008
#define QS_ALLINPUT    0x04FF
inline DWORD GetQueueStatus(UINT) { return 0; }

// ---- kernel32 odds and ends.
#define MAX_COMPUTERNAME_LENGTH 15
inline BOOL GetComputerName(char* buf, LPDWORD size)
{
    if (!buf || !size || *size == 0) return FALSE;
    char host[256] = {};
    ::gethostname(host, sizeof(host) - 1);
    std::strncpy(buf, host, *size - 1);
    buf[*size - 1] = 0;
    *size = (DWORD)std::strlen(buf);
    return TRUE;
}
#define GetComputerNameA GetComputerName
inline DWORD GetVersion() { return 0x47BB0A00u; }   // 10.0 build 18363-style: major 10, minor 0
inline void GlobalMemoryStatus(MEMORYSTATUS* m)   // full layout via the DXVK windows_base.h patch
{
    if (!m) return;
    MEMORYSTATUSEX ex = {};
    GlobalMemoryStatusEx(&ex);
    m->dwMemoryLoad = ex.dwMemoryLoad;
    m->dwTotalPhys = (SIZE_T)ex.ullTotalPhys;
    m->dwAvailPhys = (SIZE_T)ex.ullAvailPhys;
    m->dwTotalPageFile = m->dwAvailPageFile = 0;
    m->dwTotalVirtual = (SIZE_T)ex.ullTotalVirtual;
    m->dwAvailVirtual = (SIZE_T)ex.ullAvailVirtual;
}
inline DWORD GetFullPathName(const char* path, DWORD size, char* buf, char** filePart)
{
    std::string p = ran_compat::ResolvePath(path);
    char real[4096];
    if (::realpath(p.c_str(), real)) p = real;
    else if (!p.empty() && p[0] != '/') { char cwd[4096]; if (::getcwd(cwd, sizeof(cwd))) p = std::string(cwd) + "/" + p; }
    if (!buf || size <= p.size()) return (DWORD)p.size() + 1;
    std::memcpy(buf, p.c_str(), p.size() + 1);
    if (filePart) { char* slash = std::strrchr(buf, '/'); *filePart = slash ? slash + 1 : buf; }
    return (DWORD)p.size();
}
#define GetFullPathNameA GetFullPathName
inline BOOL GetFileTime(HANDLE, FILETIME* c, FILETIME* a, FILETIME* w)
{
    if (c) std::memset(c, 0, sizeof(*c));
    if (a) std::memset(a, 0, sizeof(*a));
    if (w) std::memset(w, 0, sizeof(*w));
    return FALSE;
}
inline errno_t _strdate_s(char* buf, size_t size)
{
    const time_t t = ::time(nullptr);
    struct tm tmv;
    ::localtime_r(&t, &tmv);
    return std::strftime(buf, size, "%m/%d/%y", &tmv) ? 0 : 34;
}
template <size_t N> inline errno_t _strdate_s(char (&buf)[N]) { return _strdate_s(buf, N); }
// wsprintf/wvsprintf: Windows caps the output at 1024 characters.
inline int wvsprintf(char* buf, const char* fmt, va_list ap) { return std::vsnprintf(buf, 1024, fmt, ap); }
inline int wsprintf(char* buf, const char* fmt, ...)
{
    va_list ap;
    va_start(ap, fmt);
    const int n = wvsprintf(buf, fmt, ap);
    va_end(ap);
    return n;
}
#define wvsprintfA wvsprintf
#define wsprintfA wsprintf
inline errno_t _strtime_s(char* buf, size_t size)
{
    const time_t t = ::time(nullptr);
    struct tm tmv;
    ::localtime_r(&t, &tmv);
    return std::strftime(buf, size, "%H:%M:%S", &tmv) ? 0 : 34;
}
template <size_t N> inline errno_t _strtime_s(char (&buf)[N]) { return _strtime_s(buf, N); }
inline DWORD MsgWaitForMultipleObjects(DWORD n, const HANDLE* hs, BOOL all, DWORD ms, DWORD) { return n ? WaitForMultipleObjects(n, hs, all, ms) : (Sleep(ms == INFINITE ? 0 : ms), WAIT_TIMEOUT); }
inline BOOL DosDateTimeToFileTime(WORD, WORD, FILETIME* ft) { if (ft) std::memset(ft, 0, sizeof(*ft)); return FALSE; }
typedef union _ULARGE_INTEGER { struct { DWORD LowPart, HighPart; }; ULONGLONG QuadPart; } ULARGE_INTEGER, *PULARGE_INTEGER;
#define ERROR_READ_FAULT 30u
#define ERROR_CRC        23u

// ---- Process snapshots (tlhelp32): there is no Windows process list to walk.
#define TH32CS_SNAPPROCESS 0x00000002
#define TH32CS_SNAPMODULE  0x00000008
#define TH32CS_SNAPHEAPLIST 0x00000001
#define TH32CS_SNAPTHREAD  0x00000004
#define TH32CS_SNAPALL     (TH32CS_SNAPHEAPLIST | TH32CS_SNAPPROCESS | TH32CS_SNAPTHREAD | TH32CS_SNAPMODULE)
typedef struct tagPROCESSENTRY32 {
    DWORD dwSize, cntUsage, th32ProcessID; ULONG_PTR th32DefaultHeapID; DWORD th32ModuleID, cntThreads, th32ParentProcessID;
    LONG pcPriClassBase; DWORD dwFlags; char szExeFile[MAX_PATH];
} PROCESSENTRY32, *LPPROCESSENTRY32;
typedef struct tagMODULEENTRY32 {
    DWORD dwSize, th32ModuleID, th32ProcessID, GlblcntUsage, ProccntUsage; BYTE* modBaseAddr; DWORD modBaseSize;
    HMODULE hModule; char szModule[256]; char szExePath[MAX_PATH];
} MODULEENTRY32, *LPMODULEENTRY32;
inline HANDLE CreateToolhelp32Snapshot(DWORD, DWORD) { return INVALID_HANDLE_VALUE; }
inline BOOL Process32First(HANDLE, LPPROCESSENTRY32) { return FALSE; }
inline BOOL Process32Next(HANDLE, LPPROCESSENTRY32) { return FALSE; }
inline BOOL Module32First(HANDLE, LPMODULEENTRY32) { return FALSE; }
inline BOOL Module32Next(HANDLE, LPMODULEENTRY32) { return FALSE; }

// ---- More GDI/user32 that only the Windows window paths call.
#define MM_TEXT 1
#define BI_RGB  0L
#define CB_GETCURSEL 0x0147
typedef void* HHOOK;
inline int FillRect(HDC, const RECT*, HBRUSH) { return 0; }
inline BOOL GetIconInfo(HICON, PICONINFO info) { if (info) std::memset(info, 0, sizeof(*info)); return FALSE; }
inline HCURSOR LoadCursorFromFile(const char*) { return nullptr; }
#define LoadCursorFromFileA LoadCursorFromFile
// Gamma ramps (the game's brightness option): Phase 3 maps this onto the native renderer.
inline BOOL GetDeviceGammaRamp(HDC, void*) { return FALSE; }
inline BOOL SetDeviceGammaRamp(HDC, void*) { return FALSE; }
inline char* lstrcpy(char* d, const char* s) { return std::strcpy(d, s); }
inline char* lstrcpyn(char* d, const char* s, int n) { if (n <= 0) return d; std::strncpy(d, s, (size_t)n - 1); d[n - 1] = 0; return d; }
#define lstrcpyA lstrcpy
#define lstrcpynA lstrcpyn

// ---- Window class / creation / message loop of the Windows shell (CD3DApplication). The
// native build never creates a Win32 window: the Phase 2 SDL3 main owns the window and calls
// the engine's frame functions directly, so these report "no window" and "no message".
typedef struct tagWNDCLASSA {
    UINT style; WNDPROC lpfnWndProc; int cbClsExtra, cbWndExtra; HINSTANCE hInstance;
    HICON hIcon; HCURSOR hCursor; HBRUSH hbrBackground; const char* lpszMenuName; const char* lpszClassName;
} WNDCLASSA, WNDCLASS, *LPWNDCLASS;
typedef WORD ATOM;
inline ATOM RegisterClass(const WNDCLASS*) { return 1; }
#define RegisterClassA RegisterClass
#ifndef CW_USEDEFAULT
#define CW_USEDEFAULT ((int)0x80000000)
#endif
inline HWND CreateWindowEx(DWORD, const char*, const char*, DWORD, int, int, int, int, HWND, HMENU, HINSTANCE, void*) { return nullptr; }
inline HWND CreateWindow(const char* cls, const char* title, DWORD style, int x, int y, int w, int h, HWND parent, HMENU menu, HINSTANCE inst, void* param)
{
    return CreateWindowEx(0, cls, title, style, x, y, w, h, parent, menu, inst, param);
}
#define CreateWindowA CreateWindow
#define CreateWindowExA CreateWindowEx
inline BOOL AdjustWindowRectEx(RECT*, DWORD, BOOL, DWORD) { return TRUE; }   // no frame: client == window
inline BOOL AdjustWindowRect(RECT* rc, DWORD style, BOOL menu) { return AdjustWindowRectEx(rc, style, menu, 0); }
inline LRESULT DefWindowProc(HWND, UINT, WPARAM, LPARAM) { return 0; }
#define DefWindowProcA DefWindowProc
inline BOOL DestroyWindow(HWND) { return TRUE; }
inline HMENU GetMenu(HWND) { return nullptr; }
inline BOOL DestroyMenu(HMENU) { return TRUE; }
inline BOOL SetMenu(HWND, HMENU) { return FALSE; }
inline BOOL GetMessage(MSG* msg, HWND, UINT, UINT)
{
    if (ran_compat::MessageHook()) {
        ran_compat::MessageHook()(msg, TRUE, TRUE);
        return msg && msg->message != WM_QUIT;
    }
    if (msg) { std::memset(msg, 0, sizeof(*msg)); msg->message = WM_QUIT; }
    return FALSE;
}
#define GetMessageA GetMessage
inline BOOL TranslateMessage(const MSG*) { return FALSE; }
inline LRESULT DispatchMessage(const MSG*) { return 0; }
#define DispatchMessageA DispatchMessage
namespace ran_compat {
    // Set by the Phase 2 main loop: PostQuitMessage asks it to leave.
    inline void (*&QuitHook())(int) { static void (*hook)(int) = nullptr; return hook; }
}
inline void PostQuitMessage(int code) { if (ran_compat::QuitHook()) ran_compat::QuitHook()(code); }
inline BOOL SetWindowPos(HWND, HWND, int, int, int, int, UINT) { return FALSE; }
#define WHITE_BRUSH 0
#define BLACK_BRUSH 4
inline HGDIOBJ GetStockObject(int) { return nullptr; }
#define HTCLIENT          1
#define SC_SIZE           0xF000
#define SC_MOVE           0xF010
#define SC_MINIMIZE       0xF020
#define SC_MAXIMIZE       0xF030
#define SC_CLOSE          0xF060
#define SC_KEYMENU        0xF100
#define SC_MONITORPOWER   0xF170
#define SIZE_RESTORED     0
#define SIZE_MINIMIZED    1
#define SIZE_MAXIMIZED    2
#define SIZE_MAXSHOW      3
#define SIZE_MAXHIDE      4
#define WM_CONTEXTMENU    0x007B
#define WM_ENTERMENULOOP  0x0211
#define WM_EXITMENULOOP   0x0212

// ---- Edit/list/combo box messages (sent to MFC controls that do not exist natively).
#define EM_SCROLL         0x00B5
#define EM_LINESCROLL     0x00B6
#define EM_SCROLLCARET    0x00B7
#define EM_REPLACESEL     0x00C2
#define SB_LINEDOWN       1
#define SB_PAGEDOWN       3
#define SMTO_BLOCK        0x0001
#define LB_ERR            (-1)
#define CB_ERR            (-1)
#define CB_SETCURSEL      0x014E
#define CB_FINDSTRING     0x014C
#define CB_FINDSTRINGEXACT 0x0158

// ---- Embedded resources: a Mach-O binary has no .rsrc section; game data lives in files.
#ifndef E_UNEXPECTED
#define E_UNEXPECTED ((HRESULT)0x8000FFFFL)
#endif
typedef void* HRSRC;
inline HRSRC FindResource(HMODULE, const char*, const char*) { return nullptr; }
#define FindResourceA FindResource
inline HGLOBAL LoadResource(HMODULE, HRSRC) { return nullptr; }
inline void* LockResource(HGLOBAL) { return nullptr; }
inline DWORD SizeofResource(HMODULE, HRSRC) { return 0; }
#define CREATE_NO_WINDOW 0x08000000

// ---- Misc constants.
#ifndef NO_ERROR
#define NO_ERROR 0L
#endif
#ifndef FACILITY_WIN32
#define FACILITY_ITF   4
#define FACILITY_WIN32 7
#endif
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
