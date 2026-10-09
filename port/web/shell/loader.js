// Page side of the browser client: checks the browser, makes sure the game data is unpacked in
// the Origin Private File System (downloading it on the first visit or after an update), then
// starts the wasm client (ran.js) on the canvas.
'use strict';
Error.stackTraceLimit = 60;   // game crash stacks are deep

const cfg = Object.assign({
  manifest: 'game.json',                       // data manifest: {version, bytes, parts:[...]}
  relay: 'wss://api.kaleidoscopical.com/ran/ws', // WebSocket -> TCP relay to the game servers
}, window.RAN_CONFIG || {});
const params = new URLSearchParams(location.search);
// Served locally (serve_local.mjs) or from the backend itself: the relay is on the same host.
if (location.hostname === 'localhost' || location.hostname === '127.0.0.1' || location.pathname.startsWith('/ran/play'))
  cfg.relay = (location.protocol === 'https:' ? 'wss://' : 'ws://') + location.host + '/ran/ws';
if (params.get('relay')) cfg.relay = params.get('relay');
if (params.get('manifest')) cfg.manifest = params.get('manifest');

const $ = (id) => document.getElementById(id);
const status = (text) => { $('status').textContent = text; console.log('[loader] ' + text); };
const fail = (text) => { $('status').textContent = text; $('status').classList.add('error'); console.log('[loader] ' + text); };
const fmtMB = (n) => (n / 1048576).toFixed(0) + ' MB';

function checkBrowser() {
  const missing = [];
  if (typeof WebAssembly !== 'object' || typeof WebAssembly.Suspending !== 'function') missing.push('WebAssembly JSPI');
  if (!document.createElement('canvas').getContext('webgl2')) missing.push('WebGL 2');
  if (!navigator.storage || !navigator.storage.getDirectory) missing.push('file storage (OPFS)');
  if (typeof DecompressionStream !== 'function') missing.push('DecompressionStream');
  return missing;
}

async function readText(dir, name) {
  try { return await (await (await dir.getFileHandle(name)).getFile()).text(); } catch (e) { return null; }
}

async function ensureData() {
  status('Checking game data…');
  const manifestUrl = new URL(cfg.manifest, location.href);
  const manifest = await (await fetch(manifestUrl, { cache: 'no-store' })).json();
  const opfs = await navigator.storage.getDirectory();
  const game = await opfs.getDirectoryHandle('game', { create: true });
  const have = await readText(game, '.version');
  console.log(`[loader] game data: have ${have}, current ${manifest.version}`);
  if (have !== manifest.version) {
    if (navigator.storage.persist) navigator.storage.persist().catch(() => {});
    const parts = manifest.parts.map((p) => new URL(p, manifestUrl).href);
    $('bar').hidden = false;
    status(`Downloading game data (${fmtMB(manifest.bytes)}, first visit only)…`);
    await new Promise((resolve, reject) => {
      const w = new Worker('unpack-worker.js');
      w.onmessage = (e) => {
        const m = e.data;
        if (m.error) { reject(new Error(m.error)); return; }
        if (m.progress !== undefined) {
          const pct = Math.min(100, (m.progress / m.total) * 100);
          $('fill').style.width = pct.toFixed(1) + '%';
          status(`Downloading game data: ${fmtMB(m.progress)} of ${fmtMB(m.total)} (${pct.toFixed(0)}%)`);
        }
        if (m.done) { w.terminate(); resolve(); }
      };
      w.onerror = (e) => reject(new Error(e.message));
      w.postMessage({ parts, version: manifest.version, bytes: manifest.bytes });
    });
    $('bar').hidden = true;
  }
  const text = await readText(game, '.index.txt');
  const index = new Map();
  for (const line of (text || '').split('\n')) {
    const tab = line.indexOf('\t');
    if (tab > 0) index.set(line.slice(0, tab), Number(line.slice(tab + 1)));
  }
  return index;
}

function startGame(index, devBase) {
  status('Starting…');
  const canvas = $('canvas');
  canvas.addEventListener('contextmenu', (e) => e.preventDefault());
  window.Module = {
    canvas,
    ranGameIndex: index,
    ranDevData: devBase || '',
    ranRelay: cfg.relay,
    noInitialRun: true,
    print: (t) => console.log(t),
    printErr: (t) => console.warn(t),
    preRun: [() => {
      // RAN_* settings from the URL (e.g. ?RAN_INPUT_SCRIPT=...) for testing.
      for (const [k, v] of params) if (k.startsWith('RAN_')) Module.ENV[k] = v;
    }],
    onRuntimeInitialized: () => {
      $('overlay').hidden = true;
      canvas.focus();
      Promise.resolve(Module.callMain([])).catch((e) => {
        console.error('[loader] game stopped: ' + (e && e.stack || e));
        fail('The game stopped: ' + e);
      });
    },
    onAbort: (what) => fail('The game crashed: ' + what),
  };
  const s = document.createElement('script');
  s.src = 'ran.js';
  s.onerror = () => fail('Could not load ran.js');
  document.body.appendChild(s);
}

// Development: ?devdata=<url/> reads the game files from a served client folder (its file list
// at <url>/.index.txt) instead of the unpacked archive.
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
    fail('This browser lacks ' + missing.join(', ') + '. Use a current Chrome or Edge.');
    return;
  }
  try {
    const dev = params.get('devdata');
    if (dev) {
      const base = new URL(dev, location.href).href.replace(/\/?$/, '/');
      startGame(await devData(base), base);
      return;
    }
    startGame(await ensureData());
  } catch (e) {
    fail('Could not prepare the game: ' + (e && e.message || e));
  }
})();
