// port/web/scripts/serve_local.mjs [port] [client-folder]
// Local play/dev server: the built page (port/build/web/out), the client folder at /devdata/
// (no download, no browser storage) and the WebSocket relay on /ran/ws to the real game servers.
// Open http://localhost:<port>/ (redirects to the dev-mode URL).
import http from 'node:http';
import fs from 'node:fs';
import path from 'node:path';
import { createRequire } from 'node:module';
import { fileURLToPath } from 'node:url';
const require = createRequire(import.meta.url);

const root = path.resolve(path.dirname(fileURLToPath(import.meta.url)), '../../..');
const out = path.join(root, 'port/build/web/out');
const port = Number(process.argv[2] || 8080);
const devDir = path.resolve(process.argv[3] || (process.env.HOME + '/Projects/RAN/client'));

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
const devIndex = lines.join('\n') + '\n';

const types = { '.html': 'text/html', '.js': 'text/javascript', '.wasm': 'application/wasm', '.json': 'application/json' };
const send = (res, p, type) => {
  if (!p || !fs.existsSync(p) || fs.statSync(p).isDirectory()) { res.writeHead(404); res.end(); return; }
  res.writeHead(200, { 'Content-Type': type, 'Content-Length': fs.statSync(p).size, 'Cache-Control': 'no-store' });
  fs.createReadStream(p).pipe(res);
};
const server = http.createServer((req, res) => {
  const u = new URL(req.url, 'http://x');
  const pathname = decodeURIComponent(u.pathname);
  if (pathname === '/') { res.writeHead(302, { Location: '/index.html?devdata=devdata/' }); res.end(); return; }
  if (pathname.startsWith('/devdata/')) {
    const rel = pathname.slice('/devdata/'.length);
    if (rel === '.index.txt') { res.writeHead(200, { 'Content-Type': 'text/plain' }); res.end(devIndex); return; }
    const p = path.join(devDir, rel);
    return send(res, p.startsWith(devDir) ? p : null, 'application/octet-stream');
  }
  const p = path.join(out, pathname);
  send(res, p.startsWith(out) ? p : null, types[path.extname(p)] || 'application/octet-stream');
});
require(process.env.HOME + '/Projects/main/main-server/routes/ran-web.js').attachRanRelay(server);
server.listen(port, () => console.log(`RAN web: http://localhost:${port}/  (${lines.length} game files from ${devDir})`));
