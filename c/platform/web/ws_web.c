/* ws.h in the browser (docs/port-web.md): the page's own WebSocket, no TCP
 * under it. The relay's address: the page's choice (Module.bombernetRelay,
 * e.g. ?relay=ws://127.0.0.1:8765), else on an https page the same origin
 * (wss://<host>/net: mzpico.com and its staging serve the relay there), else
 * ws://host:port as the game asks. Messages wait in the page's queue until
 * ws_poll takes them; when none waits, ws_poll yields to the browser
 * (emscripten_sleep): the core's waits poll in a loop, and without a turn
 * the browser would never deliver. */
#include <stdint.h>
#include <string.h>
#include <emscripten.h>
#include "ws.h"

uint8_t ws_open_now, ws_lost, ws_bin, ws_len;
static char line[WS_LINE_MAX];

EM_JS(void, js_ws_open, (const char *host, int port, const char *path), {
  const h = UTF8ToString(host), p = UTF8ToString(path);
  let base = Module.bombernetRelay;
  if (!base) base = (typeof location !== 'undefined' && location.protocol === 'https:') ? 'wss://' + location.host : 'ws://' + h + ':' + port;
  const st = Module.bombernetWs = { state: 0, queue: [] };
  try {
    const ws = new WebSocket(base + p);
    ws.binaryType = 'arraybuffer';
    st.ws = ws;
    ws.onopen = () => { if (Module.bombernetWs === st) st.state = 1; };
    ws.onmessage = (e) => { if (Module.bombernetWs === st) st.queue.push(e.data); };
    ws.onclose = ws.onerror = () => { if (Module.bombernetWs === st) st.state = 2; };
  } catch (e) { st.state = 2; }
});

EM_JS(int, js_ws_state, (void), { const st = Module.bombernetWs; return st ? st.state : 2; });

EM_JS(int, js_ws_send, (const uint8_t *p, int n, int binary), {
  const st = Module.bombernetWs;
  if (!st || st.state !== 1) return 9;
  try { st.ws.send(binary ? HEAPU8.slice(p, p + n) : UTF8ToString(p, n)); } catch (e) { return 9; }
  return 0;
});

/* the next message into out (at most max bytes): its length, -1 none; *bin 1 for a binary one */
EM_JS(int, js_ws_pop, (uint8_t *out, int max, uint8_t *bin), {
  const st = Module.bombernetWs;
  if (!st || !st.queue.length) return -1;
  const m = st.queue.shift();
  let b;
  if (typeof m === 'string') { b = new TextEncoder().encode(m); HEAPU8[bin] = 0; }
  else { b = new Uint8Array(m); HEAPU8[bin] = 1; }
  const n = Math.min(b.length, max);
  HEAPU8.set(b.subarray(0, n), out);
  return n;
});

EM_JS(void, js_ws_close, (void), {
  const st = Module.bombernetWs;
  Module.bombernetWs = null;
  if (st && st.ws) try { st.ws.close(); } catch (e) {}
});

uint8_t ws_open(const char *host, uint16_t port, const char *path) {
  uint16_t t;
  ws_close();
  ws_lost = 0;
  js_ws_open(host, port, path);
  for (t = 0; t < 500 && js_ws_state() == 0; t++) emscripten_sleep(20);   /* about 10 s */
  if (js_ws_state() != 1) { js_ws_close(); return 9; }
  ws_open_now = 1;
  return 0;
}

uint8_t ws_send(char *text) {
  uint16_t n = (uint16_t)strlen(text);
  if (n > WS_LINE_MAX) return 11;
  if (!ws_open_now || js_ws_send((const uint8_t *)text, n, 0)) { ws_lost = 1; ws_open_now = 0; return 9; }
  return 0;
}

uint8_t ws_send_bin(uint8_t *data, uint8_t n) {
  if (!ws_open_now || js_ws_send(data, n, 1)) { ws_lost = 1; ws_open_now = 0; return 9; }
  return 0;
}

const char *ws_poll(void) {
  uint8_t bin;
  int n;
  if (!ws_open_now) return 0;
  n = js_ws_pop((uint8_t *)line, WS_LINE_MAX - 1, &bin);
  if (n < 0) {
    if (js_ws_state() == 2) { ws_lost = 1; ws_open_now = 0; return 0; }   /* closed, and everything taken */
    emscripten_sleep(0);                  /* nothing: the browser's turn */
    return 0;
  }
  line[n] = 0;
  ws_bin = bin;
  ws_len = (uint8_t)n;
  return line;
}

void ws_close(void) {
  js_ws_close();
  ws_open_now = 0;
}

/* tcp.h's question for netdev_soft.c: a browser always has a network */
uint8_t tcp_present(void) { return 1; }
