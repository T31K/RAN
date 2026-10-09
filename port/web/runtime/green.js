// Scheduler for the cooperative threads of green.cpp (see green.h).
//
// Each suspended thread is a pending promise. Ready threads are resumed in batches: one batch
// per macrotask (MessageChannel - no 4 ms setTimeout clamp), so the browser gets the event loop
// between batches. Resolving a promise only queues the resumption as a microtask; every resumed
// thread then runs until its next suspension, all inside the same batch task.
addToLibrary({
  $RanGreen__deps: ['_emscripten_stack_restore'],
  $RanGreen: {
    ready: [],       // jobs for the next batch
    timers: [],      // {at, job}
    frames: [],      // jobs waiting for the next animation frame
    posted: false,
    timer: null,
    timerAt: 0,
    rafPending: false,
    port: null,
    post() {
      if (RanGreen.posted) return;
      RanGreen.posted = true;
      if (!RanGreen.port) {
        const ch = new MessageChannel();
        ch.port1.onmessage = () => RanGreen.pump();
        RanGreen.port = ch.port2;
        if (ch.port1.unref) ch.port1.unref();   // node: do not keep the process alive
      }
      RanGreen.port.postMessage(0);
    },
    armTimer() {
      if (!RanGreen.timers.length) return;
      let at = Infinity;
      for (const t of RanGreen.timers) at = Math.min(at, t.at);
      if (RanGreen.timer !== null) {
        if (RanGreen.timerAt <= at) return;
        clearTimeout(RanGreen.timer);
      }
      RanGreen.timerAt = at;
      RanGreen.timer = setTimeout(() => { RanGreen.timer = null; RanGreen.pump(); },
                                  Math.max(0, at - performance.now()));
    },
    pump() {
      RanGreen.posted = false;
      const now = performance.now();
      if (RanGreen.timers.length) {
        const due = [], later = [];
        for (const t of RanGreen.timers) (t.at <= now + 0.5 ? due : later).push(t);
        due.sort((a, b) => a.at - b.at);
        RanGreen.timers = later;
        for (const t of due) RanGreen.ready.push(t.job);
      }
      const batch = RanGreen.ready;
      RanGreen.ready = [];
      for (const job of batch) {
        try { job(); } catch (e) { err('[green] ' + (e && e.stack || e)); }
      }
      // Jobs queued by this batch (threads that yield again) run in the next one, after the
      // browser had the event loop.
      if (RanGreen.ready.length) RanGreen.post();
      RanGreen.armTimer();
    },
    enqueue(job) { RanGreen.ready.push(job); RanGreen.post(); },
  },

  ran_green_js_spawn__deps: ['$RanGreen', '_emscripten_stack_restore'],
  ran_green_js_spawn: (id, stackTop) => {
    if (typeof location !== 'undefined' && location.search.indexOf('greentrace') >= 0) out(`[green] spawn thread ${id}`);
    RanGreen.enqueue(() => {
      // The new JSPI stack starts on the thread's own C stack region.
      __emscripten_stack_restore(stackTop);
      const p = Module['_ran_green_entry'](id);
      if (p && p.then) p.then(() => { if (location.search.indexOf('greentrace') >= 0) out(`[green] thread ${id} ended`); },
                              (e) => err('[green] thread ' + id + ' died: ' + (e && e.stack || e)));
    });
  },

  ran_green_js_yield__deps: ['$RanGreen'],
  ran_green_js_yield__async: true,
  ran_green_js_yield: (kind, ms) => new Promise((resolve) => {
    if (kind === 1 && ms > 1) {
      RanGreen.timers.push({ at: performance.now() + ms, job: resolve });
      RanGreen.armTimer();
    } else if (kind === 2 && typeof requestAnimationFrame === 'function') {
      RanGreen.frames.push(resolve);
      if (!RanGreen.rafPending) {
        RanGreen.rafPending = true;
        requestAnimationFrame(() => {
          RanGreen.rafPending = false;
          const f = RanGreen.frames;
          RanGreen.frames = [];
          for (const r of f) RanGreen.ready.push(r);
          RanGreen.pump();
        });
      }
    } else if (kind === 2) {
      RanGreen.timers.push({ at: performance.now() + 16, job: resolve });
      RanGreen.armTimer();
    } else {
      RanGreen.enqueue(resolve);
    }
  }),

  ran_green_js_now: () => performance.now(),
});
