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
#ifdef ESP_FAST128
uint8_t esp_peer_legacy,esp_peer_fast;
extern uint8_t esp_input_delay, net_delay;
#endif

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

#ifdef ESP_FAST128
static char *put_u(char *p, uint16_t v) __naked {
 __asm
    pop af
    pop hl
    pop de
    push de
    push hl
    push af
    push ix
    ld ix,u11_pow
    ld bc,0x0400
u11_digit:
    push bc
    ld c,(ix+0)
    ld b,(ix+1)
    ld a,'0'-1
u11_sub:
    inc a
    or a
    sbc hl,bc
    jr nc,u11_sub
    add hl,bc
    pop bc
    cp '0'
    jr nz,u11_put
    bit 0,c
    jr z,u11_skip
u11_put:
    ld (de),a
    inc de
    ld c,1
u11_skip:
    inc ix
    inc ix
    djnz u11_digit
    ld a,l
    add a,'0'
    ld (de),a
    inc de
    ex de,hl
    pop ix
    ret
u11_pow: defw 10000,1000,100,10

 __endasm;
}
#else
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
#endif

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
#ifdef ESP_FAST128
static uint16_t avail_frame(void) __naked {
  __asm
    ld a,(_s_full)
    or a
    jr nz,af_mask
    ld a,(_s_slots)
    ld b,a
    xor a
    or b
    jr z,af_mask
    ld a,1
af_shift:
    add a,a
    djnz af_shift
    dec a
af_mask:
    ld c,a
    ld hl,(_s_base)
af_scan:
    ld a,h
    and l
    cp 255
    jr z,af_done
    ld a,l
    and 15
    ld e,a
    ld d,0
    push hl
    ld hl,_have
    add hl,de
    ld a,(hl)
    and c
    cp c
    jr nz,af_incomplete
    ld (hl),0
    pop hl
    inc hl
    jr af_scan
af_incomplete:
    pop hl
af_done:
    ld (_s_base),hl
    ld a,h
    or l
    jr z,af_none
    dec hl
    ret
af_none:
    ld hl,65535
    ret
  __endasm;
}
#else
static uint16_t avail_frame(void) {
  uint8_t full = s_full ? s_full : (uint8_t)((1 << s_slots) - 1);
  while (s_base != 0xffff && (have[(uint8_t)s_base & (WIN - 1)] & full) == full) {
    have[(uint8_t)s_base & (WIN - 1)] = 0;   /* the slot is free for frame base + WIN */
    s_base++;
  }
  return s_base ? s_base - 1 : 0xffff;
}
#endif

/* ---------------- the per-frame path, kept short for the Z80 ----------------
 * Every frame sends one input line and receives one per other device; the
 * generic parser above costs several milliseconds a line on a 3.5 MHz Z80.
 * Input lines are therefore built and read in one pass, on static data. */

#ifndef __Z80
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
/* JSON object key order is not part of the wire protocol. */
static uint8_t fast_input(const char *l) {
  char op[10], hex[NET_BYTES * 2 + 1];
  uint16_t frame, slot;
  if (!jstr(l, "\"op\"", op, sizeof(op)) || strcmp(op, "input")) return 0;
  if (!jfind(l, "\"frame\"") || !jfind(l, "\"slot\"") ||
      !jstr(l, "\"data\"", hex, sizeof(hex))) return 0;
  frame = jint(l, "\"frame\"", 0);
  slot = jint(l, "\"slot\"", 0xffff);
  if (slot >= NET_SLOTS) return 0;
  store_input(frame, (uint8_t)slot, hex);
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
/* The same two in Z80 assembly: they run every frame, once per device. */
uint16_t fi_frame;            /* public: the assembly below refers to them */
const uint8_t *fi_keys;

static uint8_t fast_input(const char *l) __z88dk_fastcall __naked {
  __asm                     ; HL = the line (nothing may come before the assembly)
#ifdef ESP_FAST128
    push hl
    ld de,g12_prefix
    call g12_match
    jr c,g12_fallback
    call g12_number
    jr c,g12_fallback
    ld de,g12_slot
    call g12_match
    jr c,g12_fallback
    call g12_number
    jr c,g12_fallback
    ld de,g12_data
    call g12_match
    jr c,g12_fallback
    pop hl
    jr g12_fast
g12_fallback:
    pop hl
    ld hl,0
    ret
g12_number:
    ld a,(hl)
    cp ' '
    jr nz,g12_num_first
    inc hl
    jr g12_number
g12_num_first:
    sub '0'
    cp 10
    ccf
    ret c
g12_num_loop:
    inc hl
    ld a,(hl)
    sub '0'
    cp 10
    jr c,g12_num_loop
    or a
    ret
g12_match:
    ld a,(de)
    or a
    ret z
    ld a,(hl)
    cp ' '
    jr nz,g12_compare
    inc hl
    jr g12_match
g12_compare:
    ld a,(de)
    cp (hl)
    scf
    ret nz
    inc de
    inc hl
    jr g12_match
g12_prefix: defb '{',34,"op",34,':',34,"input",34,',',34,"frame",34,':',0
g12_slot: defb ',',34,"slot",34,':',0
g12_data: defb ',',34,"data",34,':',34,0
#endif
g12_fast:
fi_c1:
    ld   a,(hl)             ; to the colon after op
    or   a
    jp   z,fi_no
    inc  hl
    cp   ':'
    jr   nz,fi_c1
    call fi_sep
    ld   a,(hl)
    cp   'i'                ; input
    jp   nz,fi_no
    call fi_num             ; frame -> DE
    jp   c,fi_no
    push de
    call fi_num             ; slot -> DE
    pop  bc                 ; BC = frame
    jp   c,fi_no
    ld   a,d
    or   a
    jp   nz,fi_no
    ld   a,e
    cp   4                  ; NET_SLOTS
    jp   nc,fi_no
    ld   (fi_slot),a
fi_c2:
    ld   a,(hl)             ; to the colon after data
    or   a
    jp   z,fi_no
    inc  hl
    cp   ':'
    jr   nz,fi_c2
    call fi_sep
    push hl                 ; HL = hex digits
    ld   hl,(_s_base)       ; frame in [base, base + 16) ?
    ld   a,c
    sub  l
    ld   e,a
    ld   a,b
    sbc  a,h
    jr   c,fi_old           ; before the window: already consumed
    or   a
    jr   nz,fi_old
    ld   a,e
    cp   16                 ; WIN
    jr   nc,fi_old
    ld   a,c
    and  15                 ; index
    ld   (fi_idx),a
    ld   l,a
    ld   h,0
    add  hl,hl
    add  hl,hl
    add  hl,hl
    add  hl,hl              ; index * 16
    ld   a,(fi_slot)
    add  a,a
    add  a,a                ; slot * 4
    ld   e,a
    ld   d,0
    add  hl,de
    ld   de,_data
    add  hl,de
    ex   de,hl              ; DE = destination
    pop  hl
    ld   b,4                ; NET_BYTES
fi_hex:
    ld   a,(hl)
    or   a
    jr   z,fi_mark
    cp   0x22               ; quote
    jr   z,fi_mark
    call fi_nib
    add  a,a
    add  a,a
    add  a,a
    add  a,a
    ld   c,a
    inc  hl
    ld   a,(hl)
    or   a
    jr   z,fi_mark
    call fi_nib
    or   c
    ld   (de),a
    inc  de
    inc  hl
    djnz fi_hex
fi_mark:
    ld   a,(fi_idx)         ; have[index] |= 1 << slot
    ld   e,a
    ld   d,0
    ld   hl,_have
    add  hl,de
    ld   a,(fi_slot)
    ld   b,a
    ld   a,1
    inc  b
fi_sh:
    dec  b
    jr   z,fi_or
    add  a,a
    jr   fi_sh
fi_or:
    or   (hl)
    ld   (hl),a
    ld   hl,1
    ret
fi_old:
    pop  hl
    ld   hl,1               ; handled, outside the window
    ret
fi_no:
    ld   hl,0
    ret

fi_sep:                     ; skip colons, quotes and spaces
    ld   a,(hl)
    cp   ':'
    jr   z,fi_s1
    cp   ' '
    jr   z,fi_s1
    cp   0x22               ; quote
    ret  nz
fi_s1:
    inc  hl
    jr   fi_sep

fi_num:                     ; next number after HL -> DE, carry = none
    ld   a,(hl)
    or   a
    scf
    ret  z
    sub  '0'
    cp   10
    jr   c,fi_n1
    inc  hl
    jr   fi_num
fi_n1:
    ld   de,0
fi_n2:
    ld   a,(hl)
    sub  '0'
    cp   10
    jr   nc,fi_n3
    push hl
    ld   h,d
    ld   l,e
    add  hl,hl
    add  hl,hl
    add  hl,de
    add  hl,hl              ; * 10
    ld   e,a
    ld   d,0
    add  hl,de
    ex   de,hl
    pop  hl
    inc  hl
    jr   fi_n2
fi_n3:
    or   a                  ; carry clear
    ret

fi_nib:                     ; hex digit in A -> 0..15
    cp   'A'
    jr   c,fi_d
    or   0x20
    sub  'a' - 10
    ret
fi_d:
    sub  '0'
    ret

fi_slot: defb 0
fi_idx:  defb 0
  __endasm;
}

/* the input line for frame fi_frame from fi_keys into out */
static void fmt_input(void) __naked {
  __asm
    ld   hl,fm_pre
    ld   de,_outbuf+8       ; out = outbuf + WS_HDR
    ld   bc,22
    ldir                    ; {op:input,frame:
    ld   hl,(_fi_frame)
    ld   c,0                ; no digit written yet
    ld   ix,fm_pow
    ld   b,4
fm_dig:
    push bc
    ld   c,(ix+0)
    ld   b,(ix+1)
    ld   a,'0' - 1
fm_sub:
    inc  a
    or   a
    sbc  hl,bc
    jr   nc,fm_sub
    add  hl,bc
    pop  bc
    cp   '0'
    jr   nz,fm_put
    bit  0,c
    jr   z,fm_skip
fm_put:
    ld   (de),a
    inc  de
    ld   c,1
fm_skip:
    inc  ix
    inc  ix
    djnz fm_dig
    ld   a,l
    add  a,'0'
    ld   (de),a
    inc  de
    ld   hl,fm_mid
    ld   bc,9
    ldir                    ; ,data:
    ld   hl,(_fi_keys)
    ld   a,(_s_nbytes)
    ld   b,a
fm_hex:
    ld   a,(hl)
    rrca
    rrca
    rrca
    rrca
    call fm_nib
    ld   a,(hl)
    call fm_nib
    inc  hl
    djnz fm_hex
    ld   a,0x22             ; quote
    ld   (de),a
    inc  de
    ld   a,'}'
    ld   (de),a
    inc  de
    xor  a
    ld   (de),a
    ret
fm_nib:
    and  15
    add  a,'0'
    cp   '9' + 1
    jr   c,fm_n1
    add  a,'a' - '9' - 1
fm_n1:
    ld   (de),a
    inc  de
    ret
fm_pre: defb 123, 34, 111, 112, 34, 58, 34, 105, 110, 112, 117, 116, 34, 44, 34, 102, 114, 97, 109, 101, 34, 58   ; {op:input,frame:
fm_mid: defb 44, 34, 100, 97, 116, 97, 34, 58, 34   ; ,data:
fm_pow: defw 10000, 1000, 100, 10
  __endasm;
}
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
#ifdef ESP_FAST128
    mq_head=mq_tail=0; s_err=0;
#endif
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

#ifdef ESP_FAST128
/* Progress received inputs during the platform's otherwise idle frame wait. */
void net_background(void) { pump(); }
#endif
static uint8_t send_line(void) { return ws_send(out); }   /* out has WS_HDR bytes in front */

/* open the room's socket, send the first line (already in out), wait for the reply */
static uint8_t room_request(const char *path) {
  uint16_t t;
#ifdef ESP_FAST128
  esp_peer_legacy=esp_peer_fast=0;
#endif
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

#ifdef ESP01_COMPACT48
/* HOST/JOIN are synchronous and never need their paths at the same time. */
static char path[40];
#endif
uint8_t net_create(uint16_t build, uint8_t slots, const uint8_t *settings, uint8_t len, char code[5], uint8_t *slot) {
#ifndef ESP01_COMPACT48
  static char path[32];
#endif
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
#ifndef ESP01_COMPACT48
  static char path[40];
#endif
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
#ifdef ESP_FAST128
  mq_head=mq_tail=0; s_err=0;
#endif
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

#ifdef ESP_FAST128
uint8_t net_send(uint16_t frame,const uint8_t keys[NET_BYTES]) __naked {
 __asm
    pop af
    pop hl
    pop de
    push de
    push hl
    push af
    ld (_fi_keys),hl
    ld (_fi_frame),de
    ld a,(_s_state)
    cp 3
    jr nz,n9_send_error
    ex de,hl
    ld de,(_s_base)
    or a
    sbc hl,de
    jr c,n9_format
    ld a,h
    or a
    jr nz,n9_format
    ld a,l
    cp 16
    jr nc,n9_format
    ld a,(_fi_frame)
    and 15
    ld e,a
    ld d,0
    ld hl,_have
    add hl,de
    ld a,(_s_slot)
    ld b,a
    ld a,1
    inc b
n9_bit:
    dec b
    jr z,n9_have
    add a,a
    jr n9_bit
n9_have:
    or (hl)
    ld (hl),a
    ld a,e
    add a,a
    add a,a
    add a,a
    add a,a
    ld e,a
    ld hl,_data
    add hl,de
    ld a,(_s_slot)
    add a,a
    add a,a
    ld e,a
    add hl,de
    ex de,hl
    ld hl,(_fi_keys)
    ld bc,4
    ldir
n9_format:
    push ix
    call _fmt_input
    call _send_line
    pop ix
    ld a,l
    ld hl,0
    or a
    ret z
    ld l,11
    ret
n9_send_error:
    ld hl,8
    ret
 __endasm;
}
#else
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

#endif

#ifdef ESP_FAST128
uint8_t net_poll(uint16_t frame,uint16_t *avail,uint8_t keys[NET_SLOTS*NET_BYTES]) __naked {
 __asm
    pop af
    pop hl
    pop de
    pop bc
    push bc
    push de
    push hl
    push af
    push ix
    push hl
    pop ix
    push bc
    push de
    call _pump
    ld a,(_s_state)
    cp 3
    jr z,n9_poll_state
    cp 6
    jr z,n9_poll_state
    pop de
    pop bc
    pop ix
    ld hl,8
    ret
n9_poll_state:
    call _avail_frame
    pop de
    ld a,l
    ld (de),a
    inc de
    ld a,h
    ld (de),a
    push hl
    push ix
    pop hl
    ld (hl),0
    ld d,h
    ld e,l
    inc de
    ld bc,15
    ldir
    pop hl
    pop bc
    ld a,h
    and l
    inc a
    jr z,n9_poll_none
    or a
    sbc hl,bc
    jr c,n9_poll_none
    ld h,b
    ld l,c
    ld de,16
    add hl,de
    ld de,(_s_base)
    or a
    sbc hl,de
    jr c,n9_poll_none
    jr z,n9_poll_none
    ld a,c
    and 15
    add a,a
    add a,a
    add a,a
    add a,a
    ld l,a
    ld h,0
    ld de,_data
    add hl,de
    push ix
    pop de
    ld a,(_s_nbytes)
    or a
    jr z,n9_poll_none
    cp 5
    jr nc,n9_poll_none
    ld a,(_s_slots)
    cp 5
    jr c,n9_slots
    ld a,4
n9_slots:
    or a
    jr z,n9_poll_none
    ld b,a
n9_slot:
    push bc
    ld a,(_s_nbytes)
    ld c,a
    ld b,0
    push bc
    ldir
    pop bc
    ld a,4
    sub c
    ld c,a
    add hl,bc
    pop bc
    djnz n9_slot
n9_poll_none:
    pop ix
    ld hl,0
    ret
 __endasm;
}
#else
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

#endif

uint8_t net_hash(uint16_t frame, uint16_t hash) {
  char *p;
  if (s_state != NETST_RUNNING) return E_NOROOM;
  p = put_u(put_s(out, "{\"op\":\"hash\",\"frame\":"), frame);
  put_s(put_u(put_s(p, ",\"hash\":"), hash), "}")[0] = 0;
  send_line();
  return 0;
}

#ifdef ESP_FAST128
static uint8_t net_msg_send_general(uint8_t to, const uint8_t *d, uint8_t len) {
  char *p;
  if (s_state < NETST_INROOM) return E_NOROOM;
  if (len > 32) len = 32;
  p = put_s(out, "{\"op\":\"msg\",\"to\":");
  p = to == 0xff ? put_s(p, "-1") : put_u(p, to);
  put_s(put_hex(put_s(p, ",\"data\":\""), d, len), "\"}")[0] = 0;
  send_line();
  return 0;
}
uint8_t net_msg_send(uint8_t to, const uint8_t *d, uint8_t len) __naked {
 __asm
    push ix
    ld ix,0
    add ix,sp
    ld c,(ix+4)
    ld b,0
    ld l,(ix+6)
    ld h,(ix+7)
    ld a,c
    cp 2
    jr nz,ms11_call
    ld a,(hl)
    cp 1
    jr z,ms11_tag
    cp 2
    jr z,ms11_tag
    cp 4
    jr nz,ms11_call
ms11_tag:
    ld (ms11_buf),a
    inc hl
    ld a,(hl)
    ld (ms11_buf+1),a
    ld hl,ms11_buf
    ld c,4
ms11_call:
    ld e,(ix+8)
    ld d,0
    push de
    push hl
    push bc
    call _net_msg_send_general
    pop bc
    pop bc
    pop bc
    pop ix
    ret
ms11_buf: defb 0,0,0x45,11

 __endasm;
}
#else
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
#endif

#ifdef ESP_FAST128
static uint8_t net_msg_recv_general(uint8_t *from, uint8_t *d) {
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
uint8_t net_msg_recv(uint8_t *from, uint8_t *d) __naked {
 __asm
    push ix
    ld ix,0
    add ix,sp
    ld l,(ix+6)
    ld h,(ix+7)
    push hl
    ld l,(ix+4)
    ld h,(ix+5)
    push hl
    call _net_msg_recv_general
    pop bc
    pop bc
    push hl
    ld a,l
    cp 2
    jr c,mr11_done
    ld c,a
    ld l,(ix+6)
    ld h,(ix+7)
    ld a,(hl)
    cp 4
    jr nc,mr11_done
    ld b,a
    inc b
    xor a
    scf
mr11_mask:
    rla
    djnz mr11_mask
    ld b,a
    ld l,(ix+4)
    ld h,(ix+5)
    ld a,(hl)
    cp 1
    jr z,mr11_type
    cp 2
    jr z,mr11_type
    cp 4
    jr nz,mr11_done
mr11_type:
    ld a,c
    cp 2
    jr z,mr11_legacy
    cp 4
    jr nz,mr11_done
    inc hl
    inc hl
    ld a,(hl)
    cp 0x45
    jr nz,mr11_done
    inc hl
    ld a,(hl)
    cp 11
    jr nz,mr11_done
    ld hl,_esp_peer_fast
    jr mr11_save
mr11_legacy:
    ld hl,_esp_peer_legacy
mr11_save:
    ld a,(hl)
    or b
    ld (hl),a
mr11_done:
    pop hl
    pop ix
    ret
 __endasm;
}
#else
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
#endif



#ifdef ESP_FAST128
/* A slot's input delay is independent: prime exactly the frames it will not
 * send live. Short-link two-frame rooms use one local frame; retain measured
 * lookahead on slower links. Never reduce delay after priming. */
void esp_choose_delay(void) __naked {
 __asm
    ld a,(_net_delay)
    cp 2
    jr nz,d15_store
    dec a
d15_store:
    ld (_esp_input_delay),a
    ret
 __endasm;
}

#endif
