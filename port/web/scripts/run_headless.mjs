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
await new Promise((r) => server.listen(0, '127.0.0.1', r));
const port = server.address().port;
const url = `http://127.0.0.1:${port}/${page0}${page0.includes('?') ? '&' : '?'}relay=ws://127.0.0.1:${port}/ran/ws`;

const launchArgs = ['--enable-features=WebAssemblyJSPI', '--use-angle=metal', '--enable-unsafe-swiftshader',
                    '--ignore-gpu-blocklist', '--enable-gpu'];
let browser, context;
if (profile) {
  context = await chromium.launchPersistentContext(profile, { headless: true, args: launchArgs, viewport: { width: 1024, height: 768 } });
} else {
  browser = await chromium.launch({ headless: true, args: launchArgs });
  context = await browser.newContext({ viewport: { width: 1024, height: 768 } });
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
if (actions.length) {
  await new Promise((resolve) => {
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
      } else if (a.op === 'shot') {
        await page.screenshot({ path: a.args[0] });
        console.log('[harness] screenshot ' + a.args[0]);
      }
      console.log(`[harness] ${a.at}s ${a.op} ${a.args.join(' ')}`);
    } catch (e) { console.log('[harness] action failed: ' + e); }
  }
}
