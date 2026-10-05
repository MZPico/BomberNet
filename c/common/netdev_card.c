/* Network device on a Unicard-compatible card (MZPico NET extension):
 * detection and the ten calls of core/netdev.h as card commands. */
#include <stdint.h>
#include "netdev.h"
#include "uc.h"
#include <string.h>

uint8_t net_device;
extern uint8_t net_slots;

/* REVD: a Unicard-compatible device answers status {02,06,04,00} and 4 data
 * bytes {major, minor, subtype, pc}; subtype 'M' (4Dh) is an MZPico. Then
 * INFO's NET bit marks the extension: no ERROR, OUTPUT set, command 95h and
 * at least 16 bytes to read. */
void net_detect(void) {
  uint8_t st[4], v[16];
  net_device = NETDEV_NONE;
  uc_cmd(cmdREVD);
  uc_status4(st);
  if (st[0] != 0x02 || st[1] != cmdREVD || st[2] != 0x04) return;
  uc_read(v, 4);
  net_device = (v[2] == 0x4d) ? NETDEV_MZPICO : NETDEV_UNICARD;
  uc_cmd(cmdX_INFO);
  uc_status4(st);
  if ((st[0] & (UC_ST_ERROR | UC_ST_OUTPUT)) != UC_ST_OUTPUT || st[1] != cmdX_INFO || st[2] < 16) return;
  uc_read(v, 16);
  if (v[3] & UC_INFO_NET) net_device = NETDEV_NET;
}

/* ---- command helpers ---- */

/* wait for a command to finish: IN_PROGRESS -> poll; then OUTPUT or ERROR */
static uint8_t net_wait(uint8_t st[4]) {
  uint16_t hi, lo;
  /* ~20 s at 4 status reads per iteration: a card whose relay link stalls
   * reports NO CONNECTION instead of freezing the machine */
  for (hi = 0; hi < 40; hi++) for (lo = 0; lo < 0x2000; lo++) {
    uc_status4(st);
    if (st[0] & UC_ST_ERROR) return st[2] ? st[2] : 0xfe;
    if (st[0] & UC_ST_INPROG) continue;
    if (st[0] & UC_ST_BUSY) return 10;         /* parameters incomplete */
    return 0;
  }
  return 9;
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

uint8_t net_send(uint16_t frame, const uint8_t keys[NET_BYTES]) {
  uint8_t st[4], i;
  uc_cmd(cmdN_SEND);
  uc_wword(frame);
  for (i = 0; i < 4; i++) uc_wr(keys[i]);                 /* fixed 4 bytes */
  return net_wait(st);
}

uint8_t net_poll(uint16_t frame, uint16_t *avail, uint8_t keys[NET_SLOTS * NET_BYTES]) {
  uint8_t st[4], r, v[2];
  uc_cmd(cmdN_POLL);
  uc_wword(frame);
  if ((r = net_wait(st)) != 0) return r;
  if (!(st[0] & UC_ST_OUTPUT)) return 0xff;
  uc_read(v, 2);
  *avail = v[0] | (v[1] << 8);
  r = (uint8_t)(net_slots * NET_BYTES);                 /* the device outputs slots * bytes */
  if (r == 0 || r > NET_SLOTS * NET_BYTES) r = NET_SLOTS * NET_BYTES;
  uc_read(keys, r);
  memset(keys + r, 0, NET_SLOTS * NET_BYTES - r);
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
