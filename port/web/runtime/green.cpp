// See ran_green.h. The scheduler itself lives in green.js; this file keeps the per-thread C stacks.
#include "ran_green.h"
#include <emscripten.h>
#include <emscripten/stack.h>
#include <cstdio>
#include <cstdlib>
#include <exception>
#include <vector>

extern "C" {
// green.js
void ran_green_js_spawn(int id, uintptr_t stackTop);
void ran_green_js_yield(int kind, double ms, int thread);   // async (JSPI): 0 = yield, 1 = sleep, 2 = frame, 3 = block
void ran_green_js_wake();
double ran_green_js_now();
// emscripten stack helpers
void _emscripten_stack_restore(uintptr_t);
uintptr_t _emscripten_stack_alloc(size_t);
}

namespace {

struct Thread {
    ran_green::ThreadFn fn = nullptr;
    void* arg = nullptr;
    void* stack = nullptr;
    bool finished = false;
    unsigned code = 0;
};

// Thread stacks are heap blocks with no guard page: the lowest bytes hold a canary, checked
// at every switch, so an overflow is reported instead of silently corrupting the heap.
const uint64_t kCanary = 0x52414E5354414B21ull;   // "RANSTAK!"
const size_t kCanaryWords = 8;
void SetCanary(void* stack)
{
    uint64_t* p = (uint64_t*)stack;
    for (size_t i = 0; i < kCanaryWords; ++i) p[i] = kCanary;
}
void CheckCanary(int id, void* stack)
{
    if (!stack) return;
    const uint64_t* p = (const uint64_t*)stack;
    for (size_t i = 0; i < kCanaryWords; ++i)
        if (p[i] != kCanary) {
            std::fprintf(stderr, "[green] thread %d overflowed its stack\n", id);
            std::abort();
        }
}

std::vector<Thread>& Threads()
{
    static std::vector<Thread> t(1);   // [0] = main
    return t;
}
int g_current = 0;

bool Trace()
{
    static const int on = std::getenv("RAN_GREEN_TRACE") ? 1 : 0;
    return on != 0;
}

// Suspends the running thread. The C stack pointer is a single wasm global shared by every
// suspended stack, so it is saved here and restored the moment this thread resumes.
void Suspend(int kind, double ms)
{
    const int me = g_current;
    CheckCanary(me, Threads()[me].stack);
    const uintptr_t sp = emscripten_stack_get_current();
    ran_green_js_yield(kind, ms, me);
    _emscripten_stack_restore(sp);
    g_current = me;
}

} // namespace

#if defined(__has_feature)
#if __has_feature(address_sanitizer)
// AddressSanitizer builds (RAN_WEB_ASAN=1): report every error and keep going, so one run lists
// them all (the game has harmless over-reads that would otherwise stop at the first).
extern "C" const char* __asan_default_options() { return "halt_on_error=0:detect_leaks=0:detect_stack_use_after_return=0"; }
#endif
#endif

// Entry of a new green thread (a JSPI export: green.js sets the C stack pointer to the thread's
// own stack before calling it).
extern "C" EMSCRIPTEN_KEEPALIVE void ran_green_entry(int id)
{
    g_current = id;
    if (Trace()) std::fprintf(stderr, "[green] thread %d starts\n", id);
    const ran_green::ThreadFn fn = Threads()[id].fn;
    void* const arg = Threads()[id].arg;
    unsigned code = 0;
    // An exception leaving a thread function ends the process on Windows; here it is reported
    // and the thread ends, so the rest of the game keeps a consistent stack.
    try {
        code = fn(arg);
    } catch (const std::exception& e) {
        std::fprintf(stderr, "[green] thread %d: uncaught exception: %s\n", id, e.what());
    } catch (...) {
        std::fprintf(stderr, "[green] thread %d: uncaught exception\n", id);
    }
    CheckCanary(id, Threads()[id].stack);
    if (Trace()) std::fprintf(stderr, "[green] thread %d returns %u\n", id, code);
    Threads()[id].code = code;
    Threads()[id].finished = true;
    ran_green_js_wake();   // threads waiting for this one
}

namespace ran_green {

int Spawn(ThreadFn fn, void* arg, size_t stackSize)
{
    if (stackSize < (4u << 20)) stackSize = 4u << 20;
    Thread t;
    t.fn = fn;
    t.arg = arg;
    t.stack = std::aligned_alloc(16, stackSize);
    if (!t.stack) {
        std::fprintf(stderr, "[green] cannot allocate a %zu-byte thread stack\n", stackSize);
        return 0;
    }
    SetCanary(t.stack);
    Threads().push_back(t);
    const int id = (int)Threads().size() - 1;
    if (Trace()) std::fprintf(stderr, "[green] spawn %d (from thread %d)\n", id, g_current);
    ran_green_js_spawn(id, (uintptr_t)t.stack + stackSize);
    return id;
}

int Current() { return g_current; }

bool Finished(int id, unsigned* code)
{
    if (id <= 0 || id >= (int)Threads().size()) return true;
    if (code) *code = Threads()[id].code;
    return Threads()[id].finished;
}

void SetCurrent(int thread) { g_current = thread; }

void Yield() { Suspend(0, 0); }
void Sleep(unsigned ms) { Suspend(ms ? 1 : 0, ms); }
void WaitFrame() { Suspend(2, 0); }
void Block(double maxMs) { Suspend(3, maxMs); }
void WakeAll() { ran_green_js_wake(); }
double NowMs() { return ran_green_js_now(); }

} // namespace ran_green
