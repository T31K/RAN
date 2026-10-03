// Behaviour tests for the Win32 kernel stand-ins: critical sections, threads, events,
// mutexes, waits, timers and the CRT string helpers.
#include <windows.h>
#include "win32/kernel.h"
#include <atomic>
#include <cstdio>
#include <cstring>

static int g_failed = 0;
#define CHECK(cond) do { if (!(cond)) { std::printf("FAIL %s:%d  %s\n", __FILE__, __LINE__, #cond); ++g_failed; } } while (0)

static CRITICAL_SECTION g_cs;
static int g_counter = 0;

static DWORD WINAPI AddThousand(LPVOID)
{
    for (int i = 0; i < 1000; ++i) {
        EnterCriticalSection(&g_cs);
        EnterCriticalSection(&g_cs);   // recursive, as on Windows
        ++g_counter;
        LeaveCriticalSection(&g_cs);
        LeaveCriticalSection(&g_cs);
    }
    return 7;
}

static unsigned __stdcall BeginThreadProc(void* arg)
{
    *(int*)arg = 42;
    return 0;
}

static HANDLE g_event;
static std::atomic<bool> g_eventSeen{false};
static DWORD WINAPI WaitForEvent(LPVOID)
{
    if (WaitForSingleObject(g_event, 2000) == WAIT_OBJECT_0) g_eventSeen = true;
    return 0;
}

int main()
{
    // Critical section + CreateThread + join via WaitForSingleObject + exit code.
    InitializeCriticalSection(&g_cs);
    HANDLE th[4];
    for (auto& h : th) h = CreateThread(nullptr, 0, AddThousand, nullptr, 0, nullptr);
    for (auto& h : th) CHECK(WaitForSingleObject(h, INFINITE) == WAIT_OBJECT_0);
    DWORD code = 0;
    CHECK(GetExitCodeThread(th[0], &code) && code == 7);
    for (auto& h : th) CHECK(CloseHandle(h));
    CHECK(g_counter == 4000);
    CHECK(TryEnterCriticalSection(&g_cs));
    LeaveCriticalSection(&g_cs);
    DeleteCriticalSection(&g_cs);

    // _beginthreadex returns a HANDLE-compatible uintptr_t.
    int out = 0;
    HANDLE bt = (HANDLE)_beginthreadex(nullptr, 0, BeginThreadProc, &out, 0, nullptr);
    CHECK(bt != nullptr);
    WaitForSingleObject(bt, INFINITE);
    CloseHandle(bt);
    CHECK(out == 42);

    // Auto-reset event wakes a waiting thread; timeout path.
    g_event = CreateEvent(nullptr, FALSE, FALSE, nullptr);
    CHECK(WaitForSingleObject(g_event, 10) == WAIT_TIMEOUT);
    HANDLE waiter = CreateThread(nullptr, 0, WaitForEvent, nullptr, 0, nullptr);
    Sleep(20);
    CHECK(SetEvent(g_event));
    WaitForSingleObject(waiter, INFINITE);
    CloseHandle(waiter);
    CHECK(g_eventSeen);
    CHECK(WaitForSingleObject(g_event, 0) == WAIT_TIMEOUT);   // auto-reset consumed it

    // Manual-reset event stays signalled until ResetEvent.
    HANDLE manual = CreateEvent(nullptr, TRUE, TRUE, nullptr);
    CHECK(WaitForSingleObject(manual, 0) == WAIT_OBJECT_0);
    CHECK(WaitForSingleObject(manual, 0) == WAIT_OBJECT_0);
    ResetEvent(manual);
    CHECK(WaitForSingleObject(manual, 0) == WAIT_TIMEOUT);

    // WaitForMultipleObjects (wait-any) reports the index of the signalled handle.
    HANDLE a = CreateEvent(nullptr, TRUE, FALSE, nullptr);
    HANDLE b = CreateEvent(nullptr, TRUE, FALSE, nullptr);
    SetEvent(b);
    HANDLE both[2] = {a, b};
    CHECK(WaitForMultipleObjects(2, both, FALSE, 100) == WAIT_OBJECT_0 + 1);
    CHECK(WaitForMultipleObjects(2, both, TRUE, 10) == WAIT_TIMEOUT);
    CloseHandle(a); CloseHandle(b); CloseHandle(manual); CloseHandle(g_event);

    // Mutex.
    HANDLE m = CreateMutex(nullptr, FALSE, nullptr);
    CHECK(WaitForSingleObject(m, 0) == WAIT_OBJECT_0);
    CHECK(ReleaseMutex(m));
    CloseHandle(m);

    // Timers advance and agree with Sleep.
    const DWORD t0 = GetTickCount();
    const DWORD tg0 = timeGetTime();
    LARGE_INTEGER f, c0, c1;
    CHECK(QueryPerformanceFrequency(&f) && f.QuadPart > 0);
    QueryPerformanceCounter(&c0);
    Sleep(30);
    QueryPerformanceCounter(&c1);
    const DWORD dt = GetTickCount() - t0;
    CHECK(dt >= 25 && dt < 500);
    CHECK(timeGetTime() - tg0 >= 25);
    const double secs = double(c1.QuadPart - c0.QuadPart) / double(f.QuadPart);
    CHECK(secs >= 0.025 && secs < 0.5);

    // Interlocked + last error.
    LONG v = 1;
    CHECK(InterlockedExchange(&v, 5) == 1 && v == 5);
    CHECK(InterlockedIncrement(&v) == 6);
    CHECK(InterlockedDecrement(&v) == 5);
    SetLastError(123);
    CHECK(GetLastError() == 123);

    // CRT string helpers.
    CHECK(_stricmp("HeLLo", "hello") == 0);
    CHECK(_strnicmp("ABCx", "abcY", 3) == 0);
    char buf[8];
    CHECK(strcpy_s(buf, sizeof(buf), "abc") == 0 && std::strcmp(buf, "abc") == 0);
    CHECK(strcat_s(buf, sizeof(buf), "de") == 0 && std::strcmp(buf, "abcde") == 0);
    CHECK(sprintf_s(buf, sizeof(buf), "%d", 1234) == 4 && std::strcmp(buf, "1234") == 0);
    char small[4];
    _snprintf(small, sizeof(small), "%s", "toolong");
    CHECK(std::strncmp(small, "too", 3) == 0);
    CHECK(_atoi64("9000000000") == 9000000000LL);
    char z[4] = {1, 2, 3, 4};
    ZeroMemory(z, sizeof(z));
    CHECK(z[0] == 0 && z[3] == 0);

    if (g_failed == 0) std::printf("PASS win32_kernel_test\n");
    return g_failed == 0 ? 0 : 1;
}
