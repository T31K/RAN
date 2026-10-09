// Game sockets as WebSockets (see the browser section of compat win32/winsock.h).
//
// The page sets Module.ranRelay to the WebSocket->TCP relay base URL; a socket connecting to
// a.b.c.d:port opens `${ranRelay}?host=a.b.c.d&port=port` (binary frames both ways, each frame
// a raw slice of the TCP stream). Everything here runs on the page's event loop; the C side only
// polls state and copies bytes, so no wasm code is re-entered from WebSocket callbacks.
addToLibrary({
  $RanWs: {
    // (library objects are serialised at build time: anything read from the page goes in a function)
    trace: null,
    tracing() {
      if (RanWs.trace === null) RanWs.trace = typeof location !== 'undefined' && location.search.indexOf('nettrace') >= 0;
      return RanWs.trace;
    },
    next: 100,
    socks: new Map(),   // id -> {ws, state, chunks: [Uint8Array], head, avail}
  },

  ran_ws_socket__deps: ['$RanWs'],
  ran_ws_socket: () => {
    const id = RanWs.next++;
    RanWs.socks.set(id, { ws: null, state: 0, chunks: [], head: 0, avail: 0 });
    return id;
  },

  ran_ws_connect__deps: ['$RanWs'],
  ran_ws_connect: (id, ipNet, port) => {
    const s = RanWs.socks.get(id);
    if (!s || s.state !== 0) return -1;
    const host = [ipNet & 255, (ipNet >>> 8) & 255, (ipNet >>> 16) & 255, (ipNet >>> 24) & 255].join('.');
    const base = Module['ranRelay'] || ((location.protocol === 'https:' ? 'wss://' : 'ws://') + location.host + '/ws');
    let ws;
    try {
      ws = new WebSocket(`${base}?host=${host}&port=${port}`);
    } catch (e) {
      err(`[net] ${host}:${port} ${e}`);
      s.state = 4;
      return 0;
    }
    ws.binaryType = 'arraybuffer';
    s.ws = ws;
    s.state = 1;
    out(`[net] connecting ${host}:${port}`);
    ws.onopen = () => { s.state = 2; out(`[net] connected ${host}:${port}`); };
    ws.onmessage = (ev) => {
      const b = new Uint8Array(ev.data);
      if (!b.length) return;
      if (RanWs.tracing()) out(`[net] ${host}:${port} <-${b.length} bytes: ${Array.from(b.subarray(0, 16), (x) => x.toString(16).padStart(2, '0')).join(' ')}`);
      s.chunks.push(b);
      s.avail += b.length;
    };
    ws.onerror = () => { if (s.state < 2) s.state = 4; };
    ws.onclose = (ev) => {
      out(`[net] closed ${host}:${port} (${ev.code})`);
      if (s.state === 1) s.state = 4;
      else if (s.state === 2) s.state = 3;
    };
    return 0;
  },

  ran_ws_state__deps: ['$RanWs'],
  ran_ws_state: (id) => { const s = RanWs.socks.get(id); return s ? s.state : 4; },

  ran_ws_available__deps: ['$RanWs'],
  ran_ws_available: (id) => { const s = RanWs.socks.get(id); return s ? s.avail : 0; },

  ran_ws_send__deps: ['$RanWs'],
  ran_ws_send: (id, buf, len) => {
    const s = RanWs.socks.get(id);
    if (!s || s.state !== 2) return -1;
    const bytes = HEAPU8.slice(buf, buf + len);
    if (RanWs.tracing()) out(`[net] socket ${id} ->${len} bytes: ${Array.from(bytes.subarray(0, 16), (x) => x.toString(16).padStart(2, '0')).join(' ')}`);
    s.ws.send(bytes);
    return len;
  },

  ran_ws_recv__deps: ['$RanWs'],
  ran_ws_recv: (id, buf, len) => {
    const s = RanWs.socks.get(id);
    if (!s) return 0;
    let n = 0;
    while (n < len && s.chunks.length) {
      const c = s.chunks[0];
      const take = Math.min(len - n, c.length - s.head);
      HEAPU8.set(c.subarray(s.head, s.head + take), buf + n);
      n += take;
      s.head += take;
      if (s.head >= c.length) { s.chunks.shift(); s.head = 0; }
    }
    s.avail -= n;
    return n;
  },

  ran_ws_close__deps: ['$RanWs'],
  ran_ws_close: (id) => {
    const s = RanWs.socks.get(id);
    if (!s) return;
    if (s.ws) { s.ws.onclose = s.ws.onmessage = s.ws.onerror = null; try { s.ws.close(); } catch (e) {} }
    RanWs.socks.delete(id);
  },
});
