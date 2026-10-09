// Downloads the game archive (a gzip'd ustar tar, possibly split into parts) and unpacks it into
// the Origin Private File System folder "game", streaming: nothing is held in memory beyond the
// current chunk. Writes "game/.index.txt" (path TAB size per line) and "game/.version" last, so
// an interrupted unpack is simply redone on the next visit.
//
// in:  { parts: [url...], version, bytes }   out: {progress, done, error}
'use strict';

let root;
const dirs = new Map();

async function dirFor(path) {
  const slash = path.lastIndexOf('/');
  if (slash < 0) return root;
  const dpath = path.slice(0, slash);
  let d = dirs.get(dpath);
  if (d) return d;
  d = root;
  let acc = '';
  for (const part of dpath.split('/')) {
    acc = acc ? acc + '/' + part : part;
    const cached = dirs.get(acc);
    if (cached) { d = cached; continue; }
    d = await d.getDirectoryHandle(part, { create: true });
    dirs.set(acc, d);
  }
  return d;
}

// Concatenates the parts into one byte stream, reporting downloaded bytes.
function partsStream(parts, onBytes) {
  let i = 0;
  let reader = null;
  return new ReadableStream({
    async pull(controller) {
      for (;;) {
        if (!reader) {
          if (i >= parts.length) { controller.close(); return; }
          const res = await fetch(parts[i++], { cache: 'no-store' });
          if (!res.ok) throw new Error(`download failed: ${res.status} ${res.url}`);
          reader = res.body.getReader();
        }
        const { value, done } = await reader.read();
        if (done) { reader = null; continue; }
        onBytes(value.length);
        controller.enqueue(value);
        return;
      }
    },
  });
}

function cstr(b, off, len) {
  let s = '';
  for (let i = 0; i < len && b[off + i]; i++) s += String.fromCharCode(b[off + i]);
  return s;
}

async function unpack(msg) {
  root = await (await navigator.storage.getDirectory()).getDirectoryHandle('game', { create: true });
  try { await root.removeEntry('.version'); } catch (e) {}
  let downloaded = 0, lastPost = 0;
  const post = (force) => {
    const now = performance.now();
    if (force || now - lastPost > 200) { lastPost = now; postMessage({ progress: downloaded, total: msg.bytes }); }
  };
  const stream = partsStream(msg.parts, (n) => { downloaded += n; post(false); })
    .pipeThrough(new DecompressionStream('gzip'));
  const reader = stream.getReader();

  let buf = new Uint8Array(0);
  let eof = false;
  async function need(n) {   // ensure at least n bytes buffered (or EOF)
    while (buf.length < n && !eof) {
      const { value, done } = await reader.read();
      if (done) { eof = true; break; }
      const nb = new Uint8Array(buf.length + value.length);
      nb.set(buf);
      nb.set(value, buf.length);
      buf = nb;
    }
    return buf.length >= n;
  }
  function take(n) { const out = buf.subarray(0, n); buf = buf.subarray(n); return out; }

  const index = [];
  let longName = null;
  for (;;) {
    if (!(await need(512))) break;
    const h = take(512).slice();
    if (h.every((x) => x === 0)) break;   // end-of-archive block
    const size = parseInt(cstr(h, 124, 12).trim() || '0', 8);
    const type = String.fromCharCode(h[156] || 48);
    let name = cstr(h, 0, 100);
    const prefix = cstr(h, 345, 155);
    if (prefix) name = prefix + '/' + name;
    if (longName) { name = longName; longName = null; }
    const padded = Math.ceil(size / 512) * 512;
    if (type === 'L') {   // GNU long name: the data block is the next entry's name
      await need(padded);
      longName = cstr(take(padded), 0, size);
      continue;
    }
    name = name.replace(/^\.\//, '');
    if (type !== '0' && type !== '\0' && type !== '7') {   // directories etc.: skip data
      let left = padded;
      while (left > 0) { await need(Math.min(left, 1 << 20)); left -= take(Math.min(left, buf.length)).length; }
      continue;
    }
    if (!name || name.split('/').some((p) => p === '..')) throw new Error('bad path in archive: ' + name);
    const dir = await dirFor(name);
    const fh = await dir.getFileHandle(name.slice(name.lastIndexOf('/') + 1), { create: true });
    const access = await fh.createSyncAccessHandle();
    access.truncate(0);
    let left = size, at = 0;
    while (left > 0) {
      await need(Math.min(left, 1 << 20));
      const chunk = take(Math.min(left, buf.length));
      access.write(chunk, { at });
      at += chunk.length;
      left -= chunk.length;
    }
    access.flush();
    access.close();
    if (padded > size) { await need(padded - size); take(padded - size); }
    index.push(name + '\t' + size);
  }
  // index and version last: a complete unpack is marked by .version
  const writeText = async (name, text) => {
    const access = await (await root.getFileHandle(name, { create: true })).createSyncAccessHandle();
    const bytes = new TextEncoder().encode(text);
    access.truncate(0);
    access.write(bytes, { at: 0 });
    access.flush();
    access.close();
  };
  await writeText('.index.txt', index.join('\n') + '\n');
  await writeText('.version', msg.version);
  post(true);
  postMessage({ done: true, files: index.length });
}

onmessage = (e) => {
  unpack(e.data).catch((err) => postMessage({ error: String(err && err.message || err) }));
};
