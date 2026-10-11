// port/web/scripts/run_headless.mjs <dir> <page.html> [timeoutSec] [--shot out.png] [--until text]
// Serves <dir> over http (with the right wasm MIME type), opens the page in headless Chromium
// (Playwright, installed in ~/.cache/ran-web-test), prints the console, and exits when the page
// logs "[done]" (or --until text), on timeout, or on a page crash. Test harness only.
import http from 'node:http';
import fs from 'node:fs';
import path from 'node:path';
import { createRequire } from 'node:module';
const require = createRequire(process.env.HOME + '/.cache/ran-web-test/');
const { chromium } = require('playwright');

const args = process.argv.slice(2);
const dir = path.resolve(args[0]);
const page0 = args[1];
const timeout = Number(args[2] || 60) * 1000;
const opt = (name) => { const i = args.indexOf(name); return i >= 0 ? args[i + 1] : null; };
const shot = opt('--shot');
const until = opt('--until') || '[done]';
const profile = opt('--profile');   // persistent profile dir (keeps OPFS between runs)

const types = { '.html': 'text/html', '.js': 'text/javascript', '.mjs': 'text/javascript',
  '.wasm': 'application/wasm', '.json': 'application/json', '.data': 'application/octet-stream' };
// --devdata <client folder>: served at /devdata/ with a generated /devdata/.index.txt (the
// page's ?devdata=devdata/ mode: no download, no browser storage).
const devDir = opt('--devdata') ? path.resolve(opt('--devdata')) : null;
let devIndex = null;
if (devDir) {
  const skip = (rel) => /\.(exe|dll|pdb|bak|des|log)$/i.test(rel) || /(^|\/)\.DS_Store$/.test(rel) ||
    /^(GameGuard|cache|\$PLUGINSDIR|\$SYSDIR)\//.test(rel);
  const lines = [];
  const walk = (d, rel) => {
    for (const e of fs.readdirSync(d, { withFileTypes: true })) {
      const r = rel ? rel + '/' + e.name : e.name;
      if (e.isDirectory()) walk(path.join(d, e.name), r);
      else if (!skip(r)) lines.push(r + '\t' + fs.statSync(path.join(d, e.name)).size);
    }
  };
  walk(devDir, '');
  devIndex = lines.join('\n') + '\n';
}
const server = http.createServer((req, res) => {
  const pathname = decodeURIComponent(new URL(req.url, 'http://x').pathname);
  if (devDir && pathname.startsWith('/devdata/')) {
    const rel = pathname.slice('/devdata/'.length);
    if (rel === '.index.txt') { res.writeHead(200, { 'Content-Type': 'text/plain' }); res.end(devIndex); return; }
    const p = path.join(devDir, rel);
    if (!p.startsWith(devDir) || !fs.existsSync(p)) { res.writeHead(404); res.end(); return; }
    res.writeHead(200, { 'Content-Type': 'application/octet-stream', 'Content-Length': fs.statSync(p).size });
    fs.createReadStream(p).pipe(res);
    return;
  }
  const p = path.join(dir, pathname);
  if (!p.startsWith(dir) || !fs.existsSync(p) || fs.statSync(p).isDirectory()) { res.writeHead(404); res.end(); return; }
  res.writeHead(200, { 'Content-Type': types[path.extname(p)] || 'application/octet-stream',
                       'Content-Length': fs.statSync(p).size, 'Cache-Control': 'no-store' });
  fs.createReadStream(p).pipe(res);
});
// The WebSocket -> TCP relay (main-server routes/ran-web.js) on /ran/ws, dialling the real game
// servers - so a local run logs in like the deployed page.
const relayModule = process.env.RAN_RELAY_MODULE || (process.env.HOME + '/Projects/main/main-server/routes/ran-web.js');
if (fs.existsSync(relayModule)) require(relayModule).attachRanRelay(server);
// --port N: a fixed origin, so browser storage (OPFS) carries over between --profile runs.
await new Promise((r) => server.listen(Number(opt('--port') || 0), '127.0.0.1', r));
const port = server.address().port;
// --url <page>: test a deployed page (its own relay) instead of the local folder.
const url = opt('--url') || `http://127.0.0.1:${port}/${page0}${page0.includes('?') ? '&' : '?'}relay=ws://127.0.0.1:${port}/ran/ws`;

const launchArgs = ['--enable-features=WebAssemblyJSPI', '--use-angle=metal', '--enable-unsafe-swiftshader',
                    '--ignore-gpu-blocklist', '--enable-gpu'];
// --viewport 1512x900 --dpr 2: a page size and device pixel ratio like a Retina MacBook's.
const [vw, vh] = (opt('--viewport') || '1024x768').split('x').map(Number);
const ctxOpts = { viewport: { width: vw, height: vh }, deviceScaleFactor: Number(opt('--dpr') || 1) };
let browser, context;
if (profile) {
  context = await chromium.launchPersistentContext(profile, { headless: true, args: launchArgs, ...ctxOpts });
} else {
  browser = await chromium.launch({ headless: true, args: launchArgs });
  context = await browser.newContext(ctxOpts);
}
const page = await context.newPage();
let finished = false;
const finish = async (code) => {
  if (finished) return; finished = true;
  if (shot) { try { await page.screenshot({ path: shot }); console.log('[harness] screenshot ' + shot); } catch (e) { console.log('[harness] screenshot failed ' + e); } }
  await context.close(); if (browser) await browser.close(); server.close(); process.exit(code);
};
page.on('console', (m) => { const t = m.text(); console.log(t); if (t.includes(until)) finish(0); });
page.on('pageerror', (e) => console.log('[pageerror] ' + (e.stack || e)));
page.on('crash', () => { console.log('[harness] page crashed'); finish(2); });
setTimeout(() => { console.log('[harness] timeout'); finish(1); }, timeout);
await page.goto(url);

// --actions "12:click 488 373; 20:type T31K; 21:key Enter; 30:shot /tmp/a.png" - real browser
// input at the given seconds after the game's first frame (a [d3d9] device line).
const actions = (opt('--actions') || '').split(';').map((s) => s.trim()).filter(Boolean).map((s) => {
  const colon = s.indexOf(':');
  const [op, ...rest] = s.slice(colon + 1).trim().split(' ');
  return { at: Number(s.slice(0, colon)), op, args: rest };
});
// --from-start: action times count from page load (loading-screen tests), not the first frame.
if (actions.length) {
  if (!args.includes('--from-start')) await new Promise((resolve) => {
    const check = (m) => { if (m.text().includes('[d3d9] WebGL2 device')) { page.off('console', check); resolve(); } };
    page.on('console', check);
  });
  const t0 = Date.now();
  for (const a of actions.sort((x, y) => x.at - y.at)) {
    const wait = t0 + a.at * 1000 - Date.now();
    if (wait > 0) await new Promise((r) => setTimeout(r, wait));
    try {
      if (a.op === 'click' || a.op === 'rclick') {
        await page.mouse.move(Number(a.args[0]), Number(a.args[1]));
        await page.mouse.down({ button: a.op === 'rclick' ? 'right' : 'left' });
        await new Promise((r) => setTimeout(r, 120));
        await page.mouse.up({ button: a.op === 'rclick' ? 'right' : 'left' });
      } else if (a.op === 'move') {
        await page.mouse.move(Number(a.args[0]), Number(a.args[1]));
      } else if (a.op === 'type') {
        await page.keyboard.type(a.args.join(' '), { delay: 60 });
      } else if (a.op === 'key') {
        await page.keyboard.press(a.args[0]);
      } else if (a.op === 'profile') {
        // "profile <seconds> <out.cpuprofile>": a V8 CPU profile (wasm functions by name) -
        // open in Chrome DevTools > Performance, or summarize with profile_summary.mjs.
        const cdp = await context.newCDPSession(page);
        await cdp.send('Profiler.enable');
        await cdp.send('Profiler.setSamplingInterval', { interval: 200 });
        await cdp.send('Profiler.start');
        await new Promise((r) => setTimeout(r, Number(a.args[0]) * 1000));
        const { profile: prof } = await cdp.send('Profiler.stop');
        fs.writeFileSync(a.args[1], JSON.stringify(prof));
        console.log('[harness] cpu profile ' + a.args[1]);
      } else if (a.op === 'export') {
        // "export <opfs folder> <local folder>": copies files the game wrote into browser
        // storage (e.g. game/cache, its font caches) out to disk.
        const files = await page.evaluate(async (dirPath) => {
          let d = await navigator.storage.getDirectory();
          for (const part of dirPath.split('/')) d = await d.getDirectoryHandle(part);
          const out = [];
          const walk = async (dir, rel) => {
            for await (const [name, h] of dir.entries()) {
              if (h.kind === 'directory') await walk(h, rel + name + '/');
              else {
                const buf = new Uint8Array(await (await h.getFile()).arrayBuffer());
                let s = '';
                for (let i = 0; i < buf.length; i += 32768) s += String.fromCharCode.apply(null, buf.subarray(i, i + 32768));
                out.push([rel + name, btoa(s)]);
              }
            }
          };
          await walk(d, '');
          return out;
        }, a.args[0]);
        for (const [rel, b64] of files) {
          const p = path.join(a.args[1], rel);
          fs.mkdirSync(path.dirname(p), { recursive: true });
          fs.writeFileSync(p, Buffer.from(b64, 'base64'));
        }
        console.log(`[harness] exported ${files.length} files from ${a.args[0]} to ${a.args[1]}`);
      } else if (a.op === 'shot') {
        await page.screenshot({ path: a.args[0] });
        console.log('[harness] screenshot ' + a.args[0]);
      }
      console.log(`[harness] ${a.at}s ${a.op} ${a.args.join(' ')}`);
    } catch (e) { console.log('[harness] action failed: ' + e); }
  }
}
