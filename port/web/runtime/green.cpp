// See ran_green.h. The scheduler itself lives in green.js; this file keeps the per-thread C stacks.
#include "ran_green.h"
#include <emscripten.h>
#include <emscripten/stack.h>
#include <cstdio>
#include <cstdlib>
#include <vector>

extern "C" {
// green.js
void ran_green_js_spawn(int id, uintptr_t stackTop);
void ran_green_js_yield(int kind, double ms);   // async (JSPI): 0 = yield, 1 = sleep, 2 = frame
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

std::vector<Thread>& Threads()
{
    static std::vector<Thread> t(1);   // [0] = main
    return t;
}
int g_current = 0;

// Suspends the running thread. The C stack pointer is a single wasm global shared by every
// suspended stack, so it is saved here and restored the moment this thread resumes.
void Suspend(int kind, double ms)
{
    const int me = g_current;
    const uintptr_t sp = emscripten_stack_get_current();
    ran_green_js_yield(kind, ms);
    _emscripten_stack_restore(sp);
    g_current = me;
}

} // namespace

// Entry of a new green thread (a JSPI export: green.js sets the C stack pointer to the thread's
// own stack before calling it).
extern "C" EMSCRIPTEN_KEEPALIVE void ran_green_entry(int id)
{
    g_current = id;
    Thread& t = Threads()[id];
    const unsigned code = t.fn(t.arg);
    Threads()[id].code = code;
    Threads()[id].finished = true;
}

namespace ran_green {

int Spawn(ThreadFn fn, void* arg, size_t stackSize)
{
    if (stackSize < (1u << 20)) stackSize = 1u << 20;
    Thread t;
    t.fn = fn;
    t.arg = arg;
    t.stack = std::aligned_alloc(16, stackSize);
    if (!t.stack) return 0;
    Threads().push_back(t);
    const int id = (int)Threads().size() - 1;
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

void Yield() { Suspend(0, 0); }
void Sleep(unsigned ms) { Suspend(ms ? 1 : 0, ms); }
void WaitFrame() { Suspend(2, 0); }
double NowMs() { return ran_green_js_now(); }

} // namespace ran_green
