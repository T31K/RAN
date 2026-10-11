// Page side of the browser client: checks the browser, brings the game files into the Origin
// Private File System and starts the wasm client (ran.js) on the canvas.
//
// Game data (port/web/scripts/package_files.py) is a static site of gzip'd files named by their
// content hash, listed in manifest.json. Loading is prioritised:
//   1. the manifest (small, never cached) says which files this browser lacks or has outdated;
//   2. the files the game reads up to its server-select screen come as a few boot packs - the
//      game starts as soon as they are in;
//   3. the first map's files (where new characters land) download next, while the player logs in;
//   4. everything else streams in the background while they play.
// A file the game opens before it has arrived is fetched at once (gamefs.js awaits
// Module.ranEnsure), and opening a map moves that map's files to the front of the queue.
// Returning players download only the files whose hash changed.
'use strict';
Error.stackTraceLimit = 60;   // game crash stacks are deep

const cfg = Object.assign({
  data: 'data/',                                  // the packed game files (manifest.json, d/, p/)
  relay: 'wss://api.kaleidoscopical.com/ran/ws',  // WebSocket -> TCP relay to the game servers
}, window.RAN_CONFIG || {});
const params = new URLSearchParams(location.search);
// Served locally (serve_local.mjs) or from the backend itself: the relay is on the same host.
if (location.hostname === 'localhost' || location.hostname === '127.0.0.1' || location.pathname.startsWith('/ran/play'))
  cfg.relay = (location.protocol === 'https:' ? 'wss://' : 'ws://') + location.host + '/ran/ws';
if (params.get('relay')) cfg.relay = params.get('relay');
if (params.get('data')) cfg.data = params.get('data');

const $ = (id) => document.getElementById(id);
const fmtMB = (n) => (n / 1048576).toFixed(n < 10 * 1048576 ? 1 : 0) + ' MB';
const log = (t) => console.log('[loader] ' + t);

// ---- the loading window (index.html copies the game's Server Select window) -------------------

const ui = {
  step(id, state, detail) {
    const li = $('step-' + id);
    if (!li) return;
    li.dataset.state = state;
    if (detail !== undefined) li.querySelector('.detail').textContent = detail;
  },
  now(text) { $('now').textContent = text; log(text); },
  bar(frac) { $('fill').style.width = (Math.max(0, Math.min(1, frac)) * 100).toFixed(1) + '%'; },
  fail(text) {
    $('now').textContent = text;
    $('now').classList.add('error');
    $('overlay').hidden = false;
    log(text);
  },
  // The small note over the game while the rest downloads.
  background(text) {
    const el = $('bgload');
    el.hidden = !text;
    el.textContent = text || '';
  },
};

function checkBrowser() {
  const missing = [];
  if (typeof WebAssembly !== 'object' || typeof WebAssembly.Suspending !== 'function') missing.push('WebAssembly JSPI');
  if (!document.createElement('canvas').getContext('webgl2')) missing.push('WebGL 2');
  if (!navigator.storage || !navigator.storage.getDirectory) missing.push('file storage (OPFS)');
  if (typeof DecompressionStream !== 'function') missing.push('DecompressionStream');
  return missing;
}

// ---- game files --------------------------------------------------------------------------------

const Data = {
  base: null,
  manifest: null,
  files: [],            // [path, size, hash, csize]
  byPath: new Map(),    // lower-case path -> index
  have: new Map(),      // path -> hash of the copy in OPFS
  pending: new Map(),   // index -> Promise (download in flight)
  queue: [],            // indices still to fetch, in order
  queued: new Set(),
  root: null,
  dirs: new Map(),
  workers: 0,
  maxWorkers: 8,
  doneBytes: 0,
  totalBytes: 0,
  saveTimer: null,

  async dir(path) {
    if (!path) return this.root;
    let d = this.dirs.get(path);
    if (d) return d;
    const slash = path.lastIndexOf('/');
    const parent = await this.dir(slash < 0 ? '' : path.slice(0, slash));
    d = await parent.getDirectoryHandle(path.slice(slash + 1), { create: true });
    this.dirs.set(path, d);
    return d;
  },
  async write(path, bytes) {
    const slash = path.lastIndexOf('/');
    const d = await this.dir(slash < 0 ? '' : path.slice(0, slash));
    const w = await (await d.getFileHandle(path.slice(slash + 1), { create: true })).createWritable();
    await w.write(bytes);
    await w.close();
  },
  async readText(name) {
    try { return await (await (await this.root.getFileHandle(name)).getFile()).text(); } catch (e) { return null; }
  },
  // What is in OPFS, saved a few seconds after changes (a crash loses at most a re-download).
  save() {
    if (this.saveTimer) return;
    this.saveTimer = setTimeout(async () => {
      this.saveTimer = null;
      try {
        const w = await (await this.root.getFileHandle('.files.json', { create: true })).createWritable();
        await w.write(JSON.stringify(Object.fromEntries(this.have)));
        await w.close();
      } catch (e) { log('saving the file list: ' + e); }
    }, 3000);
  },

  async gunzip(bytes) {
    const s = new Blob([bytes]).stream().pipeThrough(new DecompressionStream('gzip'));
    return new Uint8Array(await new Response(s).arrayBuffer());
  },
  async fetchBytes(url) {
    for (let attempt = 0; ; attempt++) {
      try {
        const res = await fetch(url);
        if (!res.ok) throw new Error('HTTP ' + res.status);
        return new Uint8Array(await res.arrayBuffer());
      } catch (e) {
        if (attempt >= 4) throw new Error(url + ': ' + e.message);
        await new Promise((r) => setTimeout(r, 500 * 2 ** attempt));   // flaky network: retry
      }
    }
  },
  async store(i, bytes) {
    const [path, size, hash] = this.files[i];
    if (bytes.length !== size) throw new Error(`${path}: ${bytes.length} bytes, expected ${size}`);
    await this.write(path, bytes);
    this.have.set(path, hash);
    this.save();
  },
  missing(i) { const f = this.files[i]; return this.have.get(f[0]) !== f[2]; },

  // One file, now. Shared with any download of it already running.
  get(i) {
    if (!this.missing(i)) return Promise.resolve();
    let p = this.pending.get(i);
    if (p) return p;
    const f = this.files[i];
    p = this.fetchBytes(this.base + 'd/' + f[2])
      .then((gz) => { this.doneBytes += gz.length; return this.gunzip(gz); })
      .then((bytes) => this.store(i, bytes))
      .finally(() => this.pending.delete(i));
    this.pending.set(i, p);
    return p;
  },

  // A boot pack: one request for many files (each a gzip member at its offset).
  async getPack(pack, onBytes) {
    const need = pack.files.filter(([i]) => this.missing(i));
    if (!need.length) return;
    const blob = await this.fetchBytes(this.base + pack.name);
    onBytes(blob.length);
    await Promise.all(need.map(async ([i, offset]) => {
      if (!this.missing(i) || this.pending.has(i)) return this.pending.get(i);
      const p = this.gunzip(blob.subarray(offset, offset + this.files[i][3])).then((b) => this.store(i, b));
      this.pending.set(i, p);
      try { await p; } finally { this.pending.delete(i); }
    }));
  },

  // Background queue.
  enqueue(list, front) {
    const add = list.filter((i) => this.missing(i) && !this.queued.has(i));
    for (const i of add) this.queued.add(i);
    if (front) this.queue.unshift(...add);
    else this.queue.push(...add);
    this.pump();
  },
  pump() {
    while (this.workers < this.maxWorkers && this.queue.length) {
      const i = this.queue.shift();
      this.queued.delete(i);
      if (!this.missing(i)) continue;
      this.workers++;
      this.get(i).catch((e) => log('download failed: ' + e.message)).finally(() => {
        this.workers--;
        this.progress();
        this.pump();
      });
    }
    this.progress();
  },
  progress() {
    const left = this.queue.length + this.workers;
    if (!this.started) return;
    if (!left) { ui.background(''); return; }
    const pct = this.totalBytes ? Math.min(99, (this.doneBytes / this.totalBytes) * 100) : 0;
    ui.background(`Downloading the rest of the game in the background: ${pct.toFixed(0)}%`);
  },

  // gamefs.js: before the game reads a file. Unknown paths (files the game made) pass.
  ensure(path) {
    const i = this.byPath.get(path.toLowerCase());
    if (i === undefined || !this.missing(i)) return null;
    // Opening a map: its own files (same name stem) go to the front of the queue.
    const m = /^data\/map\/([^/]+?)\.wld\d*$/i.exec(path);
    if (m) this.promote(m[1].toLowerCase());
    return this.get(i);
  },
  promote(stem) {
    if (this.promoted === undefined) this.promoted = new Set();
    if (this.promoted.has(stem)) return;
    this.promoted.add(stem);
    const key = stem.replace(/_?\d+$/, '');   // w_school_02 -> w_school (its shadows, objects)
    const hits = [];
    this.files.forEach((f, i) => {
      const p = f[0].toLowerCase();
      if ((p.includes(stem) || (key.length > 3 && p.includes(key))) && this.missing(i)) hits.push(i);
    });
    if (hits.length) log(`map ${stem}: ${hits.length} files first`);
    this.enqueue(hits, true);
  },
  // gamefs.js: the game wrote a file - its copy is the one to keep.
  local(path) {
    const i = this.byPath.get(path.toLowerCase());
    if (i !== undefined) { this.have.set(this.files[i][0], this.files[i][2]); this.save(); }
  },

  async open() {
    this.base = new URL(cfg.data, location.href).href;
    const opfs = await navigator.storage.getDirectory();
    this.root = await opfs.getDirectoryHandle('game', { create: true });
    ui.step('check', 'active');
    ui.now('Checking for updates…');
    const [manifest, saved] = await Promise.all([
      fetch(this.base + 'manifest.json', { cache: 'no-store' }).then((r) => {
        if (!r.ok) throw new Error('manifest: HTTP ' + r.status);
        return r.json();
      }),
      this.readText('.files.json'),
    ]);
    this.manifest = manifest;
    this.files = manifest.files;
    this.files.forEach((f, i) => this.byPath.set(f[0].toLowerCase(), i));
    try { for (const [p, h] of Object.entries(JSON.parse(saved || '{}'))) this.have.set(p, h); } catch (e) {}
    const missing = this.files.filter((f, i) => this.missing(i));
    this.totalBytes = missing.reduce((n, f) => n + f[3], 0);
    log(`manifest ${manifest.version}: ${this.files.length} files, ${missing.length} to download (${fmtMB(this.totalBytes)})`);
    ui.step('check', 'done', missing.length ? `${fmtMB(this.totalBytes)} new` : 'up to date');
    if (missing.length && navigator.storage.persist) navigator.storage.persist().catch(() => {});
  },

  // Everything up to the server-select screen.
  async boot() {
    const packs = this.manifest.packs || [];
    const need = packs.filter((p) => p.files.some(([i]) => this.missing(i)));
    if (!need.length) { ui.step('ui', 'done', 'ready'); return; }
    const total = need.reduce((n, p) => n + p.bytes, 0);
    let got = 0;
    ui.step('ui', 'active');
    ui.now('Downloading the interface…');
    this.started = false;
    const tick = setInterval(() => ui.bar(got / total), 100);
    try {
      await Promise.all(need.map((p) => this.getPack(p, (n) => {
        got += n;
        this.doneBytes += n;
        ui.step('ui', 'active', `${fmtMB(got)} of ${fmtMB(total)}`);
      })));
    } finally { clearInterval(tick); }
    ui.bar(1);
    ui.step('ui', 'done', fmtMB(total));
  },

  // After the game started: the first map, then everything else.
  background() {
    this.started = true;
    this.maxWorkers = 6;
    const world = this.manifest.world || [];
    const rest = [];
    const inWorld = new Set(world);
    this.files.forEach((f, i) => { if (!inWorld.has(i)) rest.push(i); });
    // Maps last: each is large and only one is needed at a time (opening one promotes it).
    rest.sort((a, b) => (this.files[a][0].startsWith('data/map/') ? 1 : 0) - (this.files[b][0].startsWith('data/map/') ? 1 : 0));
    this.enqueue(world);
    this.enqueue(rest);
  },

  // gamefs (C++) builds the folder tree from this: every file of the game, downloaded or not,
  // plus the files the game created itself (from the last session's index).
  async index() {
    const index = new Map();
    for (const f of this.files) index.set(f[0], f[1]);
    const text = await this.readText('.index.txt');
    for (const line of (text || '').split('\n')) {
      const tab = line.indexOf('\t');
      if (tab <= 0) continue;
      const p = line.slice(0, tab);
      const i = this.byPath.get(p.toLowerCase());
      // A file the game made, or rewrote (its settings): the local size.
      if (i === undefined || (this.have.has(p) && !this.missing(i))) index.set(p, Number(line.slice(tab + 1)));
    }
    return index;
  },
};

// ---- starting the game -------------------------------------------------------------------------

let loginReady = null;
function startGame(index, devBase) {
  const canvas = $('canvas');
  canvas.addEventListener('contextmenu', (e) => e.preventDefault());
  ui.step('start', 'active');
  ui.now('Starting the game…');
  // The overlay stays until the game shows its own Server Select window (the login server's
  // connection opens then - ws.js raises the event). Its background is a still of the login
  // scene's first frame, and the game holds the scene on that frame (Module.ranSceneHeld) until
  // the overlay has faded out, so the switch is seamless; then the scene plays.
  // ?still=1 keeps the scene held with no overlay: for capturing that still.
  const still = params.get('still') === '1';
  loginReady = new Promise((resolve) => {
    window.addEventListener('ran-login-ready', resolve, { once: true });
    setTimeout(resolve, 90000);   // never keep the player behind the overlay
  });
  loginReady.then(() => {
    ui.step('connect', 'done', 'online');
    $('overlay').classList.add('leaving');
    setTimeout(() => {
      $('overlay').hidden = true;
      if (!still) Module.ranSceneHeld = false;
    }, still ? 0 : 400);
    canvas.focus();
    if (!devBase) Data.background();
  });
  window.Module = {
    canvas,
    ranSceneHeld: true,
    ranGameIndex: index,
    ranDevData: devBase || '',
    ranRelay: cfg.relay,
    ranEnsure: devBase ? null : (path) => Data.ensure(path),
    ranLocal: devBase ? null : (path) => Data.local(path),
    noInitialRun: true,
    print: (t) => console.log(t),
    printErr: (t) => console.warn(t),
    preRun: [() => {
      // RAN_* settings from the URL (e.g. ?RAN_INPUT_SCRIPT=...) for testing.
      for (const [k, v] of params) if (k.startsWith('RAN_')) Module.ENV[k] = v;
    }],
    onRuntimeInitialized: () => {
      ui.step('start', 'done', 'ready');
      ui.step('connect', 'active');
      ui.now('Connecting to the server…');
      guardPage();
      Promise.resolve(Module.callMain([])).catch((e) => {
        console.error('[loader] game stopped: ' + (e && e.stack || e));
        ui.fail('The game stopped: ' + e);
      });
    },
    onAbort: (what) => ui.fail('The game crashed: ' + what),
  };
  const s = document.createElement('script');
  s.src = 'ran.js';
  s.onerror = () => ui.fail('Could not load the game (ran.js).');
  document.body.appendChild(s);
}

// Reload shortcuts do nothing while playing, and leaving the page (Cmd/Ctrl+W, the close or
// reload button) asks first: browsers let a page block reload keys but never the close key.
function guardPage() {
  window.addEventListener('keydown', (e) => {
    const k = e.key.toLowerCase();
    if (k === 'f5' || ((e.metaKey || e.ctrlKey) && (k === 'r' || k === 'w'))) e.preventDefault();
  }, { capture: true });
  window.addEventListener('beforeunload', (e) => { e.preventDefault(); e.returnValue = ''; });
  // In fullscreen the Keyboard Lock API hands the page the browser's own shortcuts too, so
  // Cmd/Ctrl+W reaches the handler above and does nothing.
  document.addEventListener('fullscreenchange', () => {
    if (!navigator.keyboard || !navigator.keyboard.lock) return;
    if (document.fullscreenElement) navigator.keyboard.lock().catch(() => {});
    else navigator.keyboard.unlock();
  });
}

// Development: ?devdata=<url/> reads the game files from a served client folder (its file list
// at <url>/.index.txt) instead of the packed site.
async function devData(base) {
  const text = await (await fetch(base + '.index.txt', { cache: 'no-store' })).text();
  const index = new Map();
  for (const line of text.split('\n')) {
    const tab = line.indexOf('\t');
    if (tab > 0) index.set(line.slice(0, tab), Number(line.slice(tab + 1)));
  }
  return index;
}

(async () => {
  const missing = checkBrowser();
  if (missing.length) {
    ui.fail('This browser lacks ' + missing.join(', ') + '. Use a current Chrome or Edge.');
    return;
  }
  try {
    const dev = params.get('devdata');
    if (dev) {
      const base = new URL(dev, location.href).href.replace(/\/?$/, '/');
      ui.step('check', 'done', 'dev data');
      ui.step('ui', 'done', 'dev data');
      startGame(await devData(base), base);
      return;
    }
    await Data.open();
    await Data.boot();
    startGame(await Data.index());
  } catch (e) {
    ui.fail('Could not prepare the game: ' + (e && e.message || e));
  }
})();
