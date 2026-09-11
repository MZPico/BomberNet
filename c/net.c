/* Network device detection (phase 5 step 2). The NET commands follow in
 * later steps; see docs/net-protocol.md. */
#include <stdint.h>
#include "game.h"
#include "uc.h"
#include <string.h>

uint8_t net_device;

/* REVD: a Unicard-compatible device answers status {02,06,04,00} and 4 data
 * bytes {major, minor, subtype, pc}; subtype 'M' (4Dh) is an MZPico. Then
 * INFO (16 bytes) tells whether the NET extension is present. With no device
 * the ports float and the status bytes do not match. */
void net_detect(void) {
  uint8_t st[4], v[16];
  net_device = NETDEV_NONE;
  uc_cmd(cmdREVD);
  uc_status4(st);
  if (st[0] != 0x02 || st[1] != cmdREVD || st[2] != 0x04) return;
  uc_read(v, 4);
  if (v[2] != 0x4d) { net_device = NETDEV_UNICARD; return; }
  net_device = NETDEV_MZPICO;
  uc_cmd(cmdX_INFO);
  uc_status4(st);
  if (!(st[0] & UC_ST_OUTPUT)) return;
  uc_read(v, 16);
  if (v[3] & UC_INFO_NET) net_device = NETDEV_NET;
}

/* ---- command helpers ---- */

/* wait for a command to finish: IN_PROGRESS -> poll; then OUTPUT or ERROR */
static uint8_t net_wait(uint8_t st[4]) {
  for (;;) {
    uc_status4(st);
    if (st[0] & UC_ST_ERROR) return st[2] ? st[2] : 0xfe;
    if (st[0] & UC_ST_INPROG) continue;
    if (st[0] & UC_ST_BUSY) return 10;         /* parameters incomplete */
    return 0;
  }
}

static void uc_wword(uint16_t v) { uc_wr((uint8_t)v); uc_wr((uint8_t)(v >> 8)); }

uint8_t net_status(net_status_t *s) {
  uint8_t st[4], r;
  uc_cmd(cmdN_STATUS);
  if ((r = net_wait(st)) != 0) return r;
  if (!(st[0] & UC_ST_OUTPUT)) return 0xff;
  uc_read((uint8_t *)s, 8);
  return 0;
}

uint8_t net_create(uint16_t build, uint8_t slots, const uint8_t *settings, uint8_t len, char code[5], uint8_t *slot) {
  uint8_t st[4], r, i;
  uc_cmd(cmdN_CREATE);
  uc_wword(NET_GAME_ID); uc_wword(build);
  uc_wr(slots); uc_wr(NET_BYTES); uc_wr(len);
  for (i = 0; i < 16; i++) uc_wr(i < len ? settings[i] : 0);   /* fixed 16 bytes */
  if ((r = net_wait(st)) != 0) return r;
  if (!(st[0] & UC_ST_OUTPUT)) return 0xff;
  for (i = 0; i < 4; i++) code[i] = (char)uc_rd();
  code[4] = 0;
  uc_rd();                                      /* 0x0D */
  *slot = uc_rd();
  return 0;
}

uint8_t net_join(uint16_t build, const char *code, uint8_t *slot, uint8_t *slots, uint8_t *settings, uint8_t *len) {
  uint8_t st[4], r, i, n, buf[16];
  uc_cmd(cmdN_JOIN);
  uc_wword(NET_GAME_ID); uc_wword(build);
  uc_wstr(code);
  if ((r = net_wait(st)) != 0) return r;
  if (!(st[0] & UC_ST_OUTPUT)) return 0xff;
  *slot = uc_rd();
  *slots = uc_rd();
  uc_rd();                                      /* bytes per slot: 1 for this game */
  n = uc_rd();
  uc_read(buf, 16);
  if (n > 16) n = 16;
  for (i = 0; i < n; i++) settings[i] = buf[i];
  *len = n;
  return 0;
}

uint8_t net_leave(void) {
  uint8_t st[4];
  uc_cmd(cmdN_LEAVE);
  return net_wait(st);
}

uint8_t net_ready(uint8_t ready, uint16_t *seed, uint16_t *start_frame) {
  uint8_t st[4], r, v[4];
  uc_cmd(cmdN_READY);
  uc_wr(ready);
  if ((r = net_wait(st)) != 0) return r;
  if (!(st[0] & UC_ST_OUTPUT)) return 0xff;
  uc_read(v, 4);
  *seed = v[0] | (v[1] << 8);
  *start_frame = v[2] | (v[3] << 8);
  return 0;
}

uint8_t net_send(uint16_t frame, uint8_t keys) {
  uint8_t st[4];
  uc_cmd(cmdN_SEND);
  uc_wword(frame);
  uc_wr(keys); uc_wr(0); uc_wr(0); uc_wr(0);               /* fixed 4 bytes */
  return net_wait(st);
}

uint8_t net_poll(uint16_t frame, uint16_t *avail, uint8_t keys[NET_SLOTS]) {
  uint8_t st[4], r, v[2];
  uc_cmd(cmdN_POLL);
  uc_wword(frame);
  if ((r = net_wait(st)) != 0) return r;
  if (!(st[0] & UC_ST_OUTPUT)) return 0xff;
  uc_read(v, 2);
  *avail = v[0] | (v[1] << 8);
  uc_read(keys, NET_SLOTS * NET_BYTES);
  return 0;
}

uint8_t net_hash(uint16_t frame, uint16_t hash) {
  uint8_t st[4];
  uc_cmd(cmdN_HASH);
  uc_wword(frame);
  uc_wword(hash);
  return net_wait(st);
}

uint8_t net_msg_send(uint8_t to, const uint8_t *data, uint8_t len) {
  uint8_t st[4], i;
  uc_cmd(cmdN_MSG);
  uc_wr(to); uc_wr(len);
  for (i = 0; i < 32; i++) uc_wr(i < len ? data[i] : 0);     /* fixed 32 bytes */
  return net_wait(st);
}

uint8_t net_msg_recv(uint8_t *from, uint8_t *data) {
  uint8_t st[4], n, buf[32];
  uc_cmd(cmdN_RECV);
  if (net_wait(st) != 0 || !(st[0] & UC_ST_OUTPUT)) return 0;
  *from = uc_rd();
  n = uc_rd();
  uc_read(buf, 32);
  if (*from == 0xff) return 0;
  if (n > 32) n = 32;
  memcpy(data, buf, n);
  return n;
}

/* ---------------- lockstep match ---------------- */

uint8_t menu_net = NET_OFF;
uint8_t net_active, net_slot, net_slots, net_waiting, net_abort;
char net_code[5] = "AAAA";
static uint16_t net_frame;
static uint8_t net_local_input;

/* a failed NET command: ask the device why (desync / dropped) */
static void net_fail(void) {
  net_status_t st;
  net_abort = 9;
  if (net_status(&st) == 0 && (st.state == NETST_DESYNC || st.state == NETST_DROPPED)) net_abort = st.state;
}

void net_match_start(uint8_t local_input) {
  uint8_t f;
  net_local_input = local_input;
  net_frame = 0;
  net_waiting = 0;
  net_abort = 0;
  net_active = 1;
  for (f = 0; f < NET_DELAY; f++) net_send(f, 0);   /* nobody moves in the first steps */
}

/* Send the local keys for step N+NET_DELAY, wait for the vector of step N. */
void net_lockstep_poll(void) {
  uint16_t avail;
  uint8_t keys[NET_SLOTS], i, tries = 0;
  net_status_t st;
  if (net_abort) { for (i = 0; i < MAX_PLAYERS; i++) players[i].keys = 0; return; }
  if (net_send(net_frame + NET_DELAY, input_read(net_local_input)) != 0) net_fail();
  while (!net_abort) {
    if (net_poll(net_frame, &avail, keys) != 0) { net_fail(); break; }
    if (avail != 0xffff && avail >= net_frame) break;
    if (++tries > 4) {
      net_waiting = 1;
      if ((tries & 15) == 0 && net_status(&st) == 0 &&
          (st.state == NETST_DESYNC || st.state == NETST_DROPPED || st.state == NETST_NOLINK))
        net_abort = st.state ? st.state : 9;
    }
  }
  for (i = 0; i < MAX_PLAYERS; i++)
    players[i].keys = (!net_abort && i < net_slots && players[i].active) ? keys[i] : 0;
  if (!net_abort) net_waiting = 0;
  net_frame++;
}

void net_match_end(void) {
  if (!net_active) return;
  net_active = 0;
  net_leave();
}
