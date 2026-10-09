// OPFS access for gamefs.cpp. The page's loader (loader.js) unpacked the game into the OPFS
// folder "game" and passes its index (Module.ranGameIndex: Map path -> size). This file reads
// and writes single files there and keeps the index (OPFS "game/.index.txt") current when the
// game creates or deletes files.
addToLibrary({
  $RanFs: {
    dirs: new Map(),     // "a/b" -> FileSystemDirectoryHandle
    root: null,
    saveTimer: null,
    async dir(path, create) {
      if (!RanFs.root) RanFs.root = await (await navigator.storage.getDirectory()).getDirectoryHandle('game', { create: true });
      if (!path) return RanFs.root;
      let d = RanFs.dirs.get(path);
      if (d) return d;
      const slash = path.lastIndexOf('/');
      const parent = await RanFs.dir(slash < 0 ? '' : path.slice(0, slash), create);
      d = await parent.getDirectoryHandle(path.slice(slash + 1), { create });
      RanFs.dirs.set(path, d);
      return d;
    },
    async file(path, create) {
      const slash = path.lastIndexOf('/');
      const d = await RanFs.dir(slash < 0 ? '' : path.slice(0, slash), create);
      return d.getFileHandle(path.slice(slash + 1), { create });
    },
    saveIndex() {
      if (RanFs.saveTimer) return;
      RanFs.saveTimer = setTimeout(async () => {
        RanFs.saveTimer = null;
        try {
          let text = '';
          for (const [p, s] of Module['ranGameIndex']) text += p + '\t' + s + '\n';
          const w = await (await RanFs.file('.index.txt', true)).createWritable();
          await w.write(text);
          await w.close();
        } catch (e) { err('[gamefs] index: ' + e); }
      }, 2000);
    },
  },

  ran_gamefs_index__deps: ['$RanFs', '$stringToNewUTF8'],
  ran_gamefs_index: () => {
    const idx = Module['ranGameIndex'];
    if (!idx) return 0;
    let text = '';
    for (const [p, s] of idx) text += p + '\t' + s + '\n';
    return stringToNewUTF8(text);
  },

  ran_gamefs_load__deps: ['$RanFs', '$UTF8ToString'],
  ran_gamefs_load__async: true,
  ran_gamefs_load: async (pathPtr, dst, size) => {
    const path = UTF8ToString(pathPtr);
    const trace = typeof location !== 'undefined' && location.search.indexOf('fstrace') >= 0;
    const t0 = performance.now();
    if (trace) out(`[gamefs] load ${path} (${size} bytes)...`);
    try {
      let buf;
      if (Module['ranDevData']) {
        // Development: files straight from a client folder served over HTTP (no OPFS copy).
        const res = await fetch(Module['ranDevData'] + path.split('/').map(encodeURIComponent).join('/'));
        if (!res.ok) throw new Error('HTTP ' + res.status);
        buf = new Uint8Array(await res.arrayBuffer());
      } else {
        const f = await (await RanFs.file(path, false)).getFile();
        buf = new Uint8Array(await f.arrayBuffer());
      }
      if (buf.length < size) { err(`[gamefs] ${path}: ${buf.length} bytes, expected ${size}`); return 1; }
      HEAPU8.set(buf.subarray(0, size), dst);   // HEAPU8 is re-read here: memory may have grown
      if (trace) out(`[gamefs] loaded ${path} in ${(performance.now() - t0).toFixed(0)} ms`);
      return 0;
    } catch (e) {
      err(`[gamefs] load ${path}: ${e}`);
      return 1;
    }
  },

  ran_gamefs_store__deps: ['$RanFs', '$UTF8ToString'],
  ran_gamefs_store__async: true,
  ran_gamefs_store: async (pathPtr, src, len) => {
    const path = UTF8ToString(pathPtr);
    const bytes = HEAPU8.slice(src, src + len);   // copy before the first await
    if (Module['ranDevData']) return 2;           // development: writes stay in memory
    try {
      const w = await (await RanFs.file(path, true)).createWritable();
      await w.write(bytes);
      await w.close();
      const idx = Module['ranGameIndex'];
      if (idx && idx.get(path) !== len) { idx.set(path, len); RanFs.saveIndex(); }
      return 0;
    } catch (e) {
      err(`[gamefs] store ${path}: ${e}`);
      return 1;
    }
  },

  ran_gamefs_remove__deps: ['$RanFs', '$UTF8ToString'],
  ran_gamefs_remove: (pathPtr) => {
    const path = UTF8ToString(pathPtr);
    const idx = Module['ranGameIndex'];
    if (Module['ranDevData']) { if (idx) idx.delete(path); return; }
    if (idx && idx.delete(path)) RanFs.saveIndex();
    const slash = path.lastIndexOf('/');
    RanFs.dir(slash < 0 ? '' : path.slice(0, slash), false)
      .then((d) => d.removeEntry(path.slice(slash + 1)))
      .catch(() => {});
  },
});
