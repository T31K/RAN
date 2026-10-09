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

} // namespace ran_green
