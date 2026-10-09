// Cooperative ("green") threads for the browser build.
//
// The browser gives the game one thread: the page's main thread, where WebGL, WebSockets,
// input and audio live. The game is written for real threads that block (Sleep, waits on
// events, critical sections). JSPI (WebAssembly JavaScript Promise Integration) lets a wasm
// call stack suspend on a JS promise and resume later, so every game thread is its own
// suspendable wasm stack and blocking calls become "yield to the scheduler":
//
//  - ran_green::Spawn starts a thread: a new JSPI stack entered through ran_green_entry,
//    running on its own region of the C (linear memory) stack.
//  - ran_green::Yield / Sleep suspend the calling thread. The JS scheduler (green.js) resumes
//    ready threads in batches, one batch per macrotask, so the browser processes input,
//    network and rendering between batches.
//  - ran_green::WaitFrame suspends until the next animation frame (Present).
//
// No thread runs while another is between two yields, so there is no preemption and plain data
// is race-free between suspension points. Blocking code must only run on green threads (main()
// counts as one): JS-initiated calls into wasm (event callbacks) must never yield.
#pragma once
#include <cstddef>
#include <cstdint>

extern "C" {
uintptr_t emscripten_stack_get_current(void);   // emscripten/stack.h
void _emscripten_stack_restore(uintptr_t sp);   // emscripten's stackRestore
}

namespace ran_green {

typedef unsigned (*ThreadFn)(void*);

// Starts a thread; returns its id (> 0). The thread first runs at the next scheduler batch.
int Spawn(ThreadFn fn, void* arg, size_t stackSize = 0);
// Id of the running thread (0 = main).
int Current();
// Has thread `id` returned? (Its exit code in *code.)
bool Finished(int id, unsigned* code = nullptr);
// Yield to other ready threads (and, once per batch, to the browser).
void Yield();
// Sleep at least `ms` milliseconds (0 = Yield).
void Sleep(unsigned ms);
// Suspend until the next requestAnimationFrame.
void WaitFrame();
// Milliseconds since start (performance.now based).
double NowMs();

// Every other async (suspending) JS import must be called inside an AsyncScope: other threads
// run while the caller is suspended, and they move the C stack pointer (one wasm global) and
// the current-thread id. The scope saves both and restores them when the call returns.
//     { ran_green::AsyncScope s; rc = some_async_js_import(...); }
// (Inline on purpose: the restore must run in the caller's frame, before anything else uses the
// stack pointer.)
void SetCurrent(int thread);
class AsyncScope {
public:
    AsyncScope() : m_thread(Current()), m_sp(emscripten_stack_get_current()) {}
    ~AsyncScope()
    {
        _emscripten_stack_restore(m_sp);
        SetCurrent(m_thread);
    }
    AsyncScope(const AsyncScope&) = delete;
    AsyncScope& operator=(const AsyncScope&) = delete;
private:
    int m_thread;
    uintptr_t m_sp;
};

} // namespace ran_green
