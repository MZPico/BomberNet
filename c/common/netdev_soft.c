/* The network device in software, for machines without a network card of
 * their own (ZX Spectrum with a socket interface): the same room state,
 * input window and messages as the MZPico firmware (unicard_net.cpp), behind
 * the ten calls of core/netdev.h, over a WebSocket to the relay (ws.c). */
#include <stdint.h>
#include <string.h>
#include "platform.h"
#include "netdev.h"
#include "tcp.h"
#include "ws.h"

#ifndef NET_RELAY_HOST
#define NET_RELAY_HOST "api.mzpico.com"     /* plain ws:// host of mzpico.com */
#define NET_RELAY_PORT 80
#endif
const char *net_relay_host = NET_RELAY_HOST;
uint16_t net_relay_port = NET_RELAY_PORT;

uint8_t net_device;

#define WIN 16                   /* frames kept; a peer is at most the input delay (8) ahead */
#define MSGQ 4
#define E_BUILD 6
#define E_NOROOM 8
#define E_NOLINK 9
#define E_PARAM 10
#define E_FULL 11

static uint8_t s_state, s_slot, s_ready, s_err, s_slots, s_full, s_started;
uint8_t s_members;             /* public for tests */
uint8_t s_nbytes;              /* public for the assembly: bytes per slot, base of the window */
static uint16_t s_seed, s_start;
uint16_t s_base;
static char s_code[8];
static uint8_t s_settings[16], s_settings_len;
uint8_t have[WIN];
uint8_t data[WIN][NET_SLOTS][NET_BYTES];
static struct { uint8_t from, len, data[32]; } msgq[MSGQ];
static uint8_t mq_head, mq_tail;
static uint8_t pend, pend_done, pend_err;
char outbuf[WS_HDR + WS_LINE_MAX];              /* the frame header goes in front (fmt_input: outbuf + 8) */
#define out (outbuf + WS_HDR)

/* ---------------- text helpers ---------------- */

/* value after "name" (key given with its quotes); spaces and the colon are
 * skipped: the reference relay writes "a": 1, the production one "a":1 */
static const char *jfind(const char *l, const char *key) {
  const char *p = strstr(l, key);
  if (!p) return 0;
  p += strlen(key);
  while (*p == ' ' || *p == ':') p++;
  return p;
}

static uint16_t jint(const char *l, const char *key, uint16_t def) {  /* negative -> 0xffff */
  const char *p = jfind(l, key);
  uint16_t v = 0;
  if (!p) return def;
  if (*p == '-') return 0xffff;
  while (*p >= '0' && *p <= '9') v = v * 10 + (uint16_t)(*p++ - '0');
  return v;
}

static uint8_t jstr(const char *l, const char *key, char *dst, uint8_t n) {
  const char *p = jfind(l, key);
  uint8_t i = 0;
  if (p && *p == '"') {
    p++;
    while (*p && *p != '"' && i < n - 1) dst[i++] = *p++;
  }
  dst[i] = 0;
  return i;
}

static uint8_t hexval(char c) {
  if (c >= '0' && c <= '9') return (uint8_t)(c - '0');
  if (c >= 'a' && c <= 'f') return (uint8_t)(c - 'a' + 10);
  if (c >= 'A' && c <= 'F') return (uint8_t)(c - 'A' + 10);
  return 0;
}

static uint8_t hex_decode(const char *h, uint8_t *dst, uint8_t max) {
  uint8_t n = 0;
  while (h[0] && h[1] && n < max) { dst[n++] = (uint8_t)((hexval(h[0]) << 4) | hexval(h[1])); h += 2; }
  return n;
}

static char *put_s(char *p, const char *s) { while (*s) *p++ = *s++; return p; }

static char *put_u(char *p, uint16_t v) {
  static const uint16_t pw[4] = {10000, 1000, 100, 10};
  uint8_t i, d, any = 0;
  for (i = 0; i < 4; i++) {
    d = 0;
    while (v >= pw[i]) { v -= pw[i]; d++; }
    if (d || any) { *p++ = (char)('0' + d); any = 1; }
  }
  *p++ = (char)('0' + v);
  return p;
}

static char *put_hex(char *p, const uint8_t *b, uint8_t n) {
  static const char dg[] = "0123456789abcdef";
  while (n--) { *p++ = dg[*b >> 4]; *p++ = dg[*b & 15]; b++; }
  return p;
}

/* ---------------- state ---------------- */

static void frames_clear(void) {
  s_base = 0;
  memset(have, 0, sizeof(have));
  memset(data, 0, sizeof(data));
}

static void store_input(uint16_t frame, uint8_t slot, const char *hex) {
  uint8_t i;
  if (slot >= NET_SLOTS) return;
  if (frame < s_base || frame - s_base >= WIN) return;
  i = (uint8_t)frame & (WIN - 1);
  hex_decode(hex, data[i][slot], NET_BYTES);
  have[i] |= (uint8_t)(1 << slot);
}

/* highest frame up to which every frame is complete, 0xffff = none */
static uint16_t avail_frame(void) {
  uint8_t full = s_full ? s_full : (uint8_t)((1 << s_slots) - 1);
  while (s_base != 0xffff && (have[(uint8_t)s_base & (WIN - 1)] & full) == full) {
    have[(uint8_t)s_base & (WIN - 1)] = 0;   /* the slot is free for frame base + WIN */
    s_base++;
  }
  return s_base ? s_base - 1 : 0xffff;
}

/* ---------------- the per-frame path, kept short for the Z80 ----------------
 * Every frame sends one input line and receives one per other device; the
 * generic parser above costs several milliseconds a line on a 3.5 MHz Z80.
 * Input lines are therefore built and read in one pass, on static data. */

#ifndef CPU_ASM_NETLINE                    /* cpu/z80/netline.c on the Z80 machines */
static const char *fp;
static uint16_t fv;

static uint8_t fnum(void) {                /* next unsigned number after fp -> fv; 0 = none */
  while (*fp && (uint8_t)(*fp - '0') > 9) fp++;
  if (!*fp) return 0;
  fv = 0;
  while ((uint8_t)(*fp - '0') <= 9) fv = (uint16_t)((fv << 3) + (fv << 1) + (uint8_t)(*fp++ - '0'));
  return 1;
}

/* {"op":"input","frame":F,"slot":S,"data":"hex"} (both relays keep this
 * order; spaces allowed). 1 = handled, 0 = let the generic parser try. */
static uint8_t fast_input(const char *l) {
  uint16_t frame;
  uint8_t slot, i, *d, hi;
  fp = l;
  while (*fp && *fp != ':') fp++;           /* after "op" */
  while (*fp == ':' || *fp == ' ' || *fp == '"') fp++;
  if (*fp != 'i') return 0;
  if (!fnum()) return 0;
  frame = fv;
  if (!fnum() || fv >= NET_SLOTS) return 0;
  slot = (uint8_t)fv;
  while (*fp && *fp != ':') fp++;           /* "data" */
  while (*fp == ':' || *fp == ' ' || *fp == '"') fp++;
  if (frame < s_base || frame - s_base >= WIN) return 1;
  i = (uint8_t)frame & (WIN - 1);
  d = data[i][slot];
  for (hi = 0; hi < NET_BYTES && fp[0] && fp[1] && fp[0] != '"'; hi++, fp += 2)
    d[hi] = (uint8_t)((hexval(fp[0]) << 4) | hexval(fp[1]));
  have[i] |= (uint8_t)(1 << slot);
  return 1;
}

/* the input line for frame fi_frame from fi_keys into out */
static uint16_t fi_frame;
static const uint8_t *fi_keys;
static void fmt_input(void) {
  static const char dg[] = "0123456789abcdef";
  char *q;
  uint8_t k;
  strcpy(out, "{\"op\":\"input\",\"frame\":");
  q = put_u(out + strlen(out), fi_frame);
  memcpy(q, ",\"data\":\"", 9); q += 9;
  for (k = 0; k < s_nbytes; k++) { *q++ = dg[fi_keys[k] >> 4]; *q++ = dg[fi_keys[k] & 15]; }
  *q++ = '"'; *q++ = '}'; *q = 0;
}
#else
#include "cpu.h"
uint16_t fi_frame;            /* public: the assembly refers to them */
const uint8_t *fi_keys;
#endif

static void link_lost(void) {
  if (s_state >= NETST_INROOM && s_state != NETST_DROPPED) s_state = NETST_DROPPED;
  s_started = 0;
  if (pend) { pend_err = E_NOLINK; pend_done = 1; }
}

static void handle_line(const char *l) {
  char op[10], hex[34];
  uint16_t v;
  if (fast_input(l)) return;
  jstr(l, "\"op\"", op, sizeof(op));
  if (!strcmp(op, "input")) {                          /* the frequent one first */
    jstr(l, "\"data\"", hex, sizeof(hex));
    store_input(jint(l, "\"frame\"", 0), (uint8_t)jint(l, "\"slot\"", 0xff), hex);
  } else if (!strcmp(op, "room")) {
    v = jint(l, "\"slot\"", 0xffff);
    jstr(l, "\"code\"", s_code, sizeof(s_code));
    s_slots = (uint8_t)jint(l, "\"slots\"", s_slots);
    s_nbytes = (uint8_t)jint(l, "\"bytes\"", s_nbytes);
    if (s_nbytes > NET_BYTES) s_nbytes = NET_BYTES;
    s_settings_len = jstr(l, "\"settings\"", hex, sizeof(hex)) ? hex_decode(hex, s_settings, 16) : 0;
    if (v >= NET_SLOTS || (jfind(l, "\"spectator\"") && *jfind(l, "\"spectator\"") == 't')) { s_slot = 0xff; s_state = NETST_SPECTATOR; }
    else { s_slot = (uint8_t)v; s_state = NETST_INROOM; }
    s_members = 1; s_ready = 0; s_started = 0; s_full = 0;
    frames_clear();
    if (pend) pend_done = 1;
  } else if (!strcmp(op, "members")) {
    s_members = (uint8_t)jint(l, "\"count\"", s_members);
    s_ready = (uint8_t)jint(l, "\"ready\"", s_ready);
  } else if (!strcmp(op, "start")) {
    s_seed = jint(l, "\"seed\"", 0);
    s_start = jint(l, "\"frame\"", 0);
    s_full = (uint8_t)jint(l, "\"mask\"", (uint16_t)((1 << s_slots) - 1));
    s_started = 1;
    if (s_state != NETST_SPECTATOR) s_state = NETST_RUNNING;
    frames_clear();
    s_base = s_start;
  } else if (!strcmp(op, "desync")) {
    s_state = NETST_DESYNC;
  } else if (!strcmp(op, "dropped")) {
    s_state = NETST_DROPPED;
    s_started = 0;
  } else if (!strcmp(op, "msg")) {
    if ((uint8_t)(mq_head - mq_tail) < MSGQ) {
      uint8_t i = mq_head & (MSGQ - 1);
      char mh[66];
      jstr(l, "\"data\"", mh, sizeof(mh));
      msgq[i].from = (uint8_t)jint(l, "\"from\"", 0xff);
      msgq[i].len = hex_decode(mh, msgq[i].data, 32);
      mq_head++;
    }
  } else if (!strcmp(op, "error")) {
    s_err = (uint8_t)jint(l, "\"code\"", E_PARAM);
    if (pend) { pend_err = s_err; pend_done = 1; }
  }
}

static void pump(void) {
  const char *l;
  while ((l = ws_poll()) != 0) handle_line(l);
  if (ws_lost) { ws_lost = 0; link_lost(); }
}

static uint8_t send_line(void) { return ws_send(out); }   /* out has WS_HDR bytes in front */

/* open the room's socket, send the first line (already in out), wait for the reply */
static uint8_t room_request(const char *path) {
  uint16_t t;
  if (!net_device) return E_NOLINK;
  pend = 1; pend_done = 0; pend_err = 0;
  s_state = NETST_READY;
  if (ws_open(net_relay_host, net_relay_port, path) || send_line()) { pend = 0; return E_NOLINK; }
  for (t = 0; t < 500 && !pend_done; t++) {           /* about 10 s */
    pump();
    if (!pend_done) plat_delay();
  }
  pend = 0;
  if (!pend_done) pend_err = E_NOLINK;
  if (pend_err) { ws_close(); s_state = NETST_READY; s_err = pend_err; return pend_err; }
  return 0;
}

/* ---------------- the ten calls ---------------- */

void net_detect(void) {
  net_device = tcp_present() ? NETDEV_NET : NETDEV_NONE;
  s_state = net_device ? NETST_READY : NETST_NOLINK;
}

uint8_t net_status(net_status_t *st) {
  uint16_t av;
  pump();
  av = (s_state == NETST_RUNNING || s_state == NETST_SPECTATOR) ? avail_frame() : 0xffff;
  st->state = net_device ? (s_state == NETST_NOLINK ? NETST_READY : s_state) : NETST_NOLINK;
  st->slot = s_slot; st->members = s_members; st->ready_mask = s_ready; st->rtt = 0;
  st->buffered = (av == 0xffff || av + 1 <= s_start) ? 0 : (uint8_t)(av + 1 - s_start);
  st->msgs = (uint8_t)(mq_head - mq_tail);
  st->last_error = s_err;
  return 0;
}

uint8_t net_create(uint16_t build, uint8_t slots, const uint8_t *settings, uint8_t len, char code[5], uint8_t *slot) {
  static char path[32];
  char *p;
  uint8_t r;
  if (slots < 1 || slots > NET_SLOTS) return E_PARAM;
  if (len > 16) len = 16;
  p = put_u(put_s(path, "/net?game="), NET_GAME_ID); put_s(p, "&create=1")[0] = 0;
  p = put_u(put_s(out, "{\"op\":\"create\",\"game\":"), NET_GAME_ID);
  p = put_u(put_s(p, ",\"build\":"), build);
  p = put_u(put_s(p, ",\"slots\":"), slots);
  p = put_u(put_s(p, ",\"bytes\":"), NET_BYTES);
  p = put_hex(put_s(p, ",\"settings\":\""), settings, len);
  put_s(p, "\"}")[0] = 0;
  if ((r = room_request(path)) != 0) return r;
  memcpy(code, s_code, 4); code[4] = 0;
  *slot = s_slot;
  return 0;
}

uint8_t net_join(uint16_t build, const char *code, uint8_t *slot, uint8_t *slots, uint8_t *settings, uint8_t *len) {
  static char path[40];
  char *p;
  uint8_t r;
  p = put_u(put_s(path, "/net?game="), NET_GAME_ID); put_s(put_s(p, "&code="), code)[0] = 0;
  p = put_u(put_s(out, "{\"op\":\"join\",\"game\":"), NET_GAME_ID);
  p = put_u(put_s(p, ",\"build\":"), build);
  put_s(put_s(put_s(p, ",\"code\":\""), code), "\"}")[0] = 0;
  if ((r = room_request(path)) != 0) return r;
  *slot = s_slot; *slots = s_slots;
  memcpy(settings, s_settings, s_settings_len);
  *len = s_settings_len;
  return 0;
}

uint8_t net_leave(void) {
  if (s_state < NETST_INROOM) return E_NOROOM;
  strcpy(out, "{\"op\":\"leave\"}");
  send_line();
  ws_close();
  s_state = net_device ? NETST_READY : NETST_NOLINK;
  s_started = 0; s_members = 0; s_ready = 0; s_full = 0;
  return 0;
}

uint8_t net_ready(uint8_t ready, uint16_t *seed, uint16_t *start_frame) {
  pump();
  if (s_state != NETST_INROOM && s_state != NETST_RUNNING) return E_NOROOM;
  strcpy(out, ready ? "{\"op\":\"ready\",\"ready\":1}" : "{\"op\":\"ready\",\"ready\":0}");
  send_line();
  pump();
  if (s_started) { *seed = s_seed; *start_frame = s_start; }
  else { *seed = 0xffff; *start_frame = 0xffff; }
  return 0;
}

uint8_t net_send(uint16_t frame, const uint8_t keys[NET_BYTES]) {
  uint8_t i;
  if (s_state != NETST_RUNNING) return E_NOROOM;
  if (frame >= s_base && frame - s_base < WIN) {      /* our own input: kept as it is */
    i = (uint8_t)frame & (WIN - 1);
    memcpy(data[i][s_slot], keys, NET_BYTES);
    have[i] |= (uint8_t)(1 << s_slot);
  }
  fi_frame = frame; fi_keys = keys;
  fmt_input();
  return send_line() ? E_FULL : 0;
}

uint8_t net_poll(uint16_t frame, uint16_t *avail, uint8_t keys[NET_SLOTS * NET_BYTES]) {
  uint16_t av;
  uint8_t s, i;
  pump();
  if (s_state != NETST_RUNNING && s_state != NETST_SPECTATOR) return E_NOROOM;
  av = avail_frame();
  *avail = av;
  memset(keys, 0, NET_SLOTS * NET_BYTES);
  if (av != 0xffff && frame <= av && (uint16_t)(frame + WIN) > s_base) {
    i = (uint8_t)frame & (WIN - 1);
    for (s = 0; s < s_slots && s < NET_SLOTS; s++) memcpy(keys + s * s_nbytes, data[i][s], s_nbytes);
  }
  return 0;
}

uint8_t net_hash(uint16_t frame, uint16_t hash) {
  char *p;
  if (s_state != NETST_RUNNING) return E_NOROOM;
  p = put_u(put_s(out, "{\"op\":\"hash\",\"frame\":"), frame);
  put_s(put_u(put_s(p, ",\"hash\":"), hash), "}")[0] = 0;
  send_line();
  return 0;
}

uint8_t net_msg_send(uint8_t to, const uint8_t *d, uint8_t len) {
  char *p;
  if (s_state < NETST_INROOM) return E_NOROOM;
  if (len > 32) len = 32;
  p = put_s(out, "{\"op\":\"msg\",\"to\":");
  p = to == 0xff ? put_s(p, "-1") : put_u(p, to);
  put_s(put_hex(put_s(p, ",\"data\":\""), d, len), "\"}")[0] = 0;
  send_line();
  return 0;
}

uint8_t net_msg_recv(uint8_t *from, uint8_t *d) {
  uint8_t i, n;
  pump();
  if (mq_head == mq_tail) return 0;
  i = mq_tail & (MSGQ - 1);
  *from = msgq[i].from;
  n = msgq[i].len;
  memcpy(d, msgq[i].data, n);
  mq_tail++;
  return n;
}
