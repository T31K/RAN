// Win32 kernel stand-ins for the native macOS build: critical sections, threads, events,
// mutexes, waits, timers, interlocked ops, last-error and the MSVC CRT string helpers.
// Expects DXVK's native <windows.h> for the base types (DWORD, LONG, HANDLE, LARGE_INTEGER,
// SECURITY_ATTRIBUTES, WINAPI, WAIT_OBJECT_0, ZeroMemory).
//
// All waitable handles share one lock + condition variable. The game waits on handles in a
// handful of places (sound/network threads), so one lock keeps the Windows semantics exact
// (auto/manual-reset events, wait-any/wait-all) at no practical cost.
#pragma once
#include <windows.h>
#include "win32/extra_types.h"
#include <chrono>
#include <condition_variable>
#include <cstdarg>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <ctime>
#include <memory>
#include <mutex>
#include <strings.h>
#include <thread>
#include <unistd.h>

#ifndef INFINITE
#define INFINITE 0xFFFFFFFFu
#endif
#ifndef WAIT_OBJECT_0
#define WAIT_OBJECT_0 0
#endif
#ifndef WAIT_TIMEOUT
#define WAIT_TIMEOUT 258u
#endif
#ifndef WAIT_FAILED
#define WAIT_FAILED 0xFFFFFFFFu
#endif
#ifndef STILL_ACTIVE
#define STILL_ACTIVE 259u
#endif
#ifndef CREATE_SUSPENDED
#define CREATE_SUSPENDED 0x4u
#endif
#ifndef CopyMemory
#define CopyMemory(d, s, n) std::memcpy((d), (s), (n))
#endif
#ifndef MoveMemory
#define MoveMemory(d, s, n) std::memmove((d), (s), (n))
#endif
#ifndef FillMemory
#define FillMemory(d, n, v) std::memset((d), (v), (n))
#endif

typedef int errno_t;
#ifndef _TIME64_T_DEFINED
#define _TIME64_T_DEFINED
typedef int64_t __time64_t;
#endif
typedef DWORD (WINAPI *LPTHREAD_START_ROUTINE)(LPVOID);

namespace ran_compat {

    inline std::mutex& KernelLock() { static std::mutex m; return m; }
    inline std::condition_variable& KernelCond() { static std::condition_variable c; return c; }

    // A waitable kernel object. All members are called with KernelLock() held.
    struct KObject {
        virtual ~KObject() = default;
        virtual bool IsSignaled() const = 0;
        virtual void Acquire() {}        // consume the signal (auto-reset event, mutex ownership)
    };

    struct KHandle { std::shared_ptr<KObject> obj; };

    inline HANDLE MakeHandle(std::shared_ptr<KObject> obj) { return (HANDLE)new KHandle{std::move(obj)}; }
    inline KObject* Obj(HANDLE h) { return h ? ((KHandle*)h)->obj.get() : nullptr; }

    struct KEvent : KObject {
        bool manualReset = false;
        bool signaled = false;
        bool IsSignaled() const override { return signaled; }
        void Acquire() override { if (!manualReset) signaled = false; }
    };

    struct KMutex : KObject {
        std::thread::id owner;
        int count = 0;
        bool IsSignaled() const override { return count == 0 || owner == std::this_thread::get_id(); }
        void Acquire() override { owner = std::this_thread::get_id(); ++count; }
    };

    struct KThread : KObject {
        std::thread thread;
        bool finished = false;
        DWORD exitCode = STILL_ACTIVE;
        bool IsSignaled() const override { return finished; }
        ~KThread() override { if (thread.joinable()) thread.detach(); }
    };

    inline void Signal() { KernelCond().notify_all(); }

    inline DWORD Wait(DWORD count, const HANDLE* handles, bool waitAll, DWORD ms)
    {
        std::unique_lock<std::mutex> lock(KernelLock());
        const auto deadline = std::chrono::steady_clock::now() + std::chrono::milliseconds(ms);
        for (;;) {
            if (waitAll) {
                bool all = true;
                for (DWORD i = 0; i < count; ++i) all = all && Obj(handles[i]) && Obj(handles[i])->IsSignaled();
                if (all) {
                    for (DWORD i = 0; i < count; ++i) Obj(handles[i])->Acquire();
                    return WAIT_OBJECT_0;
                }
            } else {
                for (DWORD i = 0; i < count; ++i) {
                    KObject* o = Obj(handles[i]);
                    if (o && o->IsSignaled()) { o->Acquire(); return WAIT_OBJECT_0 + i; }
                }
            }
            if (ms == 0) return WAIT_TIMEOUT;
            if (ms == INFINITE) KernelCond().wait(lock);
            else if (KernelCond().wait_until(lock, deadline) == std::cv_status::timeout) {
                // One last check after the timeout so a signal racing the deadline is not lost.
                ms = 0;
            }
        }
    }

    inline std::chrono::steady_clock::time_point ProcessStart()
    {
        static const auto t0 = std::chrono::steady_clock::now();
        return t0;
    }

    inline DWORD& LastError() { thread_local DWORD e = 0; return e; }
}

// Overlapped I/O descriptor (the client's sockets are event-driven; this only needs to exist).
typedef struct _OVERLAPPED {
    ULONG_PTR Internal;
    ULONG_PTR InternalHigh;
    union {
        struct { DWORD Offset; DWORD OffsetHigh; };
        void* Pointer;
    };
    HANDLE hEvent;
} OVERLAPPED, *LPOVERLAPPED;

typedef struct _MEMORYSTATUSEX {   // MEMORYSTATUS itself comes from DXVK's windows_base.h
    DWORD dwLength, dwMemoryLoad;
    DWORDLONG ullTotalPhys, ullAvailPhys, ullTotalPageFile, ullAvailPageFile;
    DWORDLONG ullTotalVirtual, ullAvailVirtual, ullAvailExtendedVirtual;
} MEMORYSTATUSEX, *LPMEMORYSTATUSEX;

// ---- Critical sections (recursive, like Windows) ----
typedef struct RAN_CRITICAL_SECTION { std::recursive_mutex* m; } CRITICAL_SECTION, *LPCRITICAL_SECTION;

inline void InitializeCriticalSection(LPCRITICAL_SECTION cs) { cs->m = new std::recursive_mutex(); }
inline BOOL InitializeCriticalSectionAndSpinCount(LPCRITICAL_SECTION cs, DWORD) { InitializeCriticalSection(cs); return TRUE; }
inline void DeleteCriticalSection(LPCRITICAL_SECTION cs) { delete cs->m; cs->m = nullptr; }
inline void EnterCriticalSection(LPCRITICAL_SECTION cs) { cs->m->lock(); }
inline void LeaveCriticalSection(LPCRITICAL_SECTION cs) { cs->m->unlock(); }
inline BOOL TryEnterCriticalSection(LPCRITICAL_SECTION cs) { return cs->m->try_lock() ? TRUE : FALSE; }

// ---- Handles, events, mutexes, waits ----
inline BOOL CloseHandle(HANDLE h)
{
    if (!h) return FALSE;
    std::lock_guard<std::mutex> lock(ran_compat::KernelLock());
    delete (ran_compat::KHandle*)h;
    return TRUE;
}

inline HANDLE CreateEvent(SECURITY_ATTRIBUTES*, BOOL manualReset, BOOL initialState, const char*)
{
    auto e = std::make_shared<ran_compat::KEvent>();
    e->manualReset = manualReset != FALSE;
    e->signaled = initialState != FALSE;
    return ran_compat::MakeHandle(e);
}
#define CreateEventA CreateEvent

inline BOOL SetEvent(HANDLE h)
{
    std::lock_guard<std::mutex> lock(ran_compat::KernelLock());
    auto* e = (ran_compat::KEvent*)ran_compat::Obj(h);
    if (!e) return FALSE;
    e->signaled = true;
    ran_compat::Signal();
    return TRUE;
}

inline BOOL ResetEvent(HANDLE h)
{
    std::lock_guard<std::mutex> lock(ran_compat::KernelLock());
    auto* e = (ran_compat::KEvent*)ran_compat::Obj(h);
    if (!e) return FALSE;
    e->signaled = false;
    return TRUE;
}

inline HANDLE CreateMutex(SECURITY_ATTRIBUTES*, BOOL initialOwner, const char*)
{
    auto m = std::make_shared<ran_compat::KMutex>();
    if (initialOwner) m->Acquire();
    return ran_compat::MakeHandle(m);
}
#define CreateMutexA CreateMutex

inline BOOL ReleaseMutex(HANDLE h)
{
    std::lock_guard<std::mutex> lock(ran_compat::KernelLock());
    auto* m = (ran_compat::KMutex*)ran_compat::Obj(h);
    if (!m || m->count == 0 || m->owner != std::this_thread::get_id()) return FALSE;
    if (--m->count == 0) m->owner = std::thread::id();
    ran_compat::Signal();
    return TRUE;
}

inline DWORD WaitForSingleObject(HANDLE h, DWORD ms) { return ran_compat::Wait(1, &h, false, ms); }
inline DWORD WaitForMultipleObjects(DWORD n, const HANDLE* hs, BOOL waitAll, DWORD ms) { return ran_compat::Wait(n, hs, waitAll != FALSE, ms); }

// ---- Threads ----
inline HANDLE CreateThread(SECURITY_ATTRIBUTES*, SIZE_T, LPTHREAD_START_ROUTINE fn, LPVOID arg, DWORD, LPDWORD threadId)
{
    auto t = std::make_shared<ran_compat::KThread>();
    std::shared_ptr<ran_compat::KThread> keep = t;   // the running thread keeps its object alive
    t->thread = std::thread([fn, arg, keep]() {
        const DWORD code = fn(arg);
        std::lock_guard<std::mutex> lock(ran_compat::KernelLock());
        keep->exitCode = code;
        keep->finished = true;
        ran_compat::Signal();
    });
    if (threadId) *threadId = (DWORD)std::hash<std::thread::id>()(t->thread.get_id());
    return ran_compat::MakeHandle(t);
}

inline uintptr_t _beginthreadex(void*, unsigned, unsigned (__stdcall *fn)(void*), void* arg, unsigned, unsigned* threadId)
{
    struct Thunk { unsigned (__stdcall *fn)(void*); void* arg; };
    auto* thunk = new Thunk{fn, arg};
    DWORD id = 0;
    HANDLE h = CreateThread(nullptr, 0, [](LPVOID p) -> DWORD {
        Thunk t = *(Thunk*)p;
        delete (Thunk*)p;
        return (DWORD)t.fn(t.arg);
    }, thunk, 0, &id);
    if (threadId) *threadId = id;
    return (uintptr_t)h;
}

inline BOOL GetExitCodeThread(HANDLE h, LPDWORD code)
{
    std::lock_guard<std::mutex> lock(ran_compat::KernelLock());
    auto* t = (ran_compat::KThread*)ran_compat::Obj(h);
    if (!t || !code) return FALSE;
    *code = t->finished ? t->exitCode : STILL_ACTIVE;
    return TRUE;
}

inline DWORD GetCurrentThreadId() { return (DWORD)std::hash<std::thread::id>()(std::this_thread::get_id()); }
inline void Sleep(DWORD ms) { std::this_thread::sleep_for(std::chrono::milliseconds(ms)); }
inline DWORD SleepEx(DWORD ms, BOOL) { Sleep(ms); return 0; }

// ---- Timers ----
inline DWORD GetTickCount()
{
    using namespace std::chrono;
    return (DWORD)duration_cast<milliseconds>(steady_clock::now() - ran_compat::ProcessStart()).count();
}
inline ULONGLONG GetTickCount64()
{
    using namespace std::chrono;
    return (ULONGLONG)duration_cast<milliseconds>(steady_clock::now() - ran_compat::ProcessStart()).count();
}
inline DWORD timeGetTime() { return GetTickCount(); }
inline DWORD timeBeginPeriod(UINT) { return 0; }
inline DWORD timeEndPeriod(UINT) { return 0; }
inline BOOL QueryPerformanceFrequency(LARGE_INTEGER* f) { f->QuadPart = 1000000000LL; return TRUE; }
inline BOOL QueryPerformanceCounter(LARGE_INTEGER* c)
{
    using namespace std::chrono;
    c->QuadPart = (LONGLONG)duration_cast<nanoseconds>(steady_clock::now() - ran_compat::ProcessStart()).count();
    return TRUE;
}

// ---- Interlocked, last error, debug output ----
inline LONG InterlockedExchange(LONG volatile* target, LONG value) { return __atomic_exchange_n(target, value, __ATOMIC_SEQ_CST); }
inline LONG InterlockedIncrement(LONG volatile* target) { return __atomic_add_fetch(target, 1, __ATOMIC_SEQ_CST); }
inline LONG InterlockedDecrement(LONG volatile* target) { return __atomic_sub_fetch(target, 1, __ATOMIC_SEQ_CST); }
inline LONG InterlockedCompareExchange(LONG volatile* target, LONG exchange, LONG comparand)
{
    __atomic_compare_exchange_n(target, &comparand, exchange, false, __ATOMIC_SEQ_CST, __ATOMIC_SEQ_CST);
    return comparand;
}
inline DWORD GetLastError() { return ran_compat::LastError(); }
inline void SetLastError(DWORD e) { ran_compat::LastError() = e; }
inline void OutputDebugString(const char* s) { std::fputs(s, stderr); }
#define OutputDebugStringA OutputDebugString

// ---- MSVC CRT string helpers ----
inline int _stricmp(const char* a, const char* b) { return strcasecmp(a, b); }
inline int _strnicmp(const char* a, const char* b, size_t n) { return strncasecmp(a, b, n); }
#define stricmp _stricmp
#define strnicmp _strnicmp
#define _snprintf std::snprintf
#define _vsnprintf std::vsnprintf
inline long long _atoi64(const char* s) { return std::strtoll(s, nullptr, 10); }

inline errno_t strcpy_s(char* dst, size_t size, const char* src)
{
    if (!dst || size == 0) return 22;   // EINVAL
    const size_t n = std::strlen(src);
    if (n >= size) { std::memcpy(dst, src, size - 1); dst[size - 1] = 0; return 34; }   // ERANGE, truncated
    std::memcpy(dst, src, n + 1);
    return 0;
}
inline errno_t strncpy_s(char* dst, size_t size, const char* src, size_t count)
{
    if (!dst || size == 0) return 22;
    size_t n = std::strlen(src);
    if (n > count) n = count;
    if (n >= size) n = size - 1;
    std::memcpy(dst, src, n);
    dst[n] = 0;
    return 0;
}
inline errno_t strcat_s(char* dst, size_t size, const char* src)
{
    const size_t used = strnlen(dst, size);
    if (used >= size) return 22;
    return strcpy_s(dst + used, size - used, src);
}
inline int vsprintf_s(char* dst, size_t size, const char* fmt, va_list ap)
{
    const int n = std::vsnprintf(dst, size, fmt, ap);
    return (n < 0 || (size_t)n >= size) ? -1 : n;
}
inline int sprintf_s(char* dst, size_t size, const char* fmt, ...)
{
    va_list ap;
    va_start(ap, fmt);
    const int n = vsprintf_s(dst, size, fmt, ap);
    va_end(ap);
    return n;
}
// MSVC's localtime_s takes (tm*, const time_t*) - the reverse of C11's Annex K order.
inline errno_t localtime_s(struct tm* out, const time_t* t) { return ::localtime_r(t, out) ? 0 : 22; }
inline errno_t gmtime_s(struct tm* out, const time_t* t) { return ::gmtime_r(t, out) ? 0 : 22; }
inline errno_t _localtime64_s(struct tm* out, const __time64_t* t) { const time_t tt = (time_t)*t; return localtime_s(out, &tt); }
inline char* strtok_s(char* s, const char* delim, char** ctx) { return ::strtok_r(s, delim, ctx); }
// sscanf_s only differs for %s/%c/%[ (extra size argument). The game uses it with %d/%lf only
// (tinyxml), so plain sscanf is exact; revisit if a %s caller appears.
#define sscanf_s std::sscanf
inline int _snprintf_s(char* dst, size_t size, size_t count, const char* fmt, ...)
{
    va_list ap;
    va_start(ap, fmt);
    const size_t limit = count < size ? count + 1 : size;
    const int n = std::vsnprintf(dst, limit, fmt, ap);
    va_end(ap);
    return (n < 0 || (size_t)n >= limit) ? -1 : n;
}
inline int lstrcmpiA(const char* a, const char* b) { return strcasecmp(a, b); }
inline int lstrcmpA(const char* a, const char* b) { return std::strcmp(a, b); }
#define lstrcmpi lstrcmpiA
#define lstrcmp  lstrcmpA
inline void* SecureZeroMemory(void* p, size_t n) { volatile unsigned char* v = (volatile unsigned char*)p; while (n--) *v++ = 0; return p; }

#ifndef ERROR_SUCCESS
#define ERROR_SUCCESS       0u
#endif
#ifndef ERROR_OUTOFMEMORY
#define ERROR_OUTOFMEMORY   14u
#endif
#ifndef E_OUTOFMEMORY
#define E_OUTOFMEMORY       ((HRESULT)0x8007000EL)
#endif
#ifndef E_INVALIDARG
#define E_INVALIDARG        ((HRESULT)0x80070057L)
#endif

// CP949 lead-byte test (the game's only DBCS code page; see win32/codepage.h).
inline BOOL IsDBCSLeadByteEx(UINT, BYTE b) { return (b >= 0x81 && b <= 0xFE) ? TRUE : FALSE; }
inline BOOL IsDBCSLeadByte(BYTE b) { return IsDBCSLeadByteEx(0, b); }

// Keyboard/mouse state and message boxes are owned by the SDL3 platform layer (Phase 2).
// Until it is linked in, input reads as "nothing pressed" and message boxes go to stderr.
namespace ran_compat {
    inline short (*&KeyStateHook())(int) { static short (*hook)(int) = nullptr; return hook; }
    inline int (*&MessageBoxHook())(void*, const char*, const char*, unsigned) { static int (*hook)(void*, const char*, const char*, unsigned) = nullptr; return hook; }
}
inline short GetKeyState(int vk) { return ran_compat::KeyStateHook() ? ran_compat::KeyStateHook()(vk) : 0; }
inline short GetAsyncKeyState(int vk) { return GetKeyState(vk); }
inline int MessageBoxA(HWND hwnd, const char* text, const char* caption, UINT type)
{
    if (ran_compat::MessageBoxHook()) return ran_compat::MessageBoxHook()(hwnd, text, caption, type);
    std::fprintf(stderr, "[MessageBox] %s: %s\n", caption ? caption : "", text ? text : "");
    return 1;   // IDOK
}
#define MessageBox MessageBoxA

// MSVC's array overloads (size deduced from the destination array).
template <size_t N> inline errno_t strcpy_s(char (&dst)[N], const char* src) { return strcpy_s(dst, N, src); }
template <size_t N> inline errno_t strcat_s(char (&dst)[N], const char* src) { return strcat_s(dst, N, src); }
template <size_t N> inline errno_t strncpy_s(char (&dst)[N], const char* src, size_t count) { return strncpy_s(dst, N, src, count); }
template <size_t N> inline int _snprintf_s(char (&dst)[N], size_t count, const char* fmt, ...)
{
    va_list ap;
    va_start(ap, fmt);
    const size_t limit = count < N ? count + 1 : N;
    const int n = std::vsnprintf(dst, limit, fmt, ap);
    va_end(ap);
    return (n < 0 || (size_t)n >= limit) ? -1 : n;
}
template <size_t N> inline int sprintf_s(char (&dst)[N], const char* fmt, ...)
{
    va_list ap;
    va_start(ap, fmt);
    const int n = vsprintf_s(dst, N, fmt, ap);
    va_end(ap);
    return n;
}
