/* tcp.h over a WiC64 (Wi-Fi on the C64's user port; docs/port-c64.md),
 * its "revised" protocol as VICE 3.9 emulates it (userport_wic64.c):
 *
 *   The C64 sends with PA2 high and port B as output: each byte written to
 *   port B pulses PC2, and the WiC64 answers each with FLAG2 (CIA 2's ICR
 *   bit 4). A request is 'R', the command, the payload's length (low,
 *   high) and the payload.
 *   Then PA2 low and port B as input: FLAG2 says the reply is there; a
 *   first read of port B is a dummy (its PC2 pulse fetches the first
 *   byte), then each read is a byte, each after FLAG2: the status (0 is
 *   success), the payload's length (low, high), the payload.
 *
 * Commands: 0x21 TCP open ("host:port"), 0x22 TCP read (whatever is
 * waiting, at once: nothing when nothing is; the firmware's TcpClient::read,
 * up to 8 KB), 0x23 TCP write, 0x2e TCP close, 0x26 the firmware's version
 * (to see that a WiC64 is there). The official library
 * (github.com/WiC64-Team/wic64-library) does the same handshakes.
 * Opening, closing and finding the WiC64 wait for it here (FLAG2 polled;
 * reading the ICR clears it). Once a connection is open, writes and reads
 * run in the background (c64.s: FLAG2 as an NMI, a step of the command
 * each): the firmware's own pauses, about 13-18 ms a command, then overlap
 * the game's work instead of stopping it (docs/port-c64.md: 62 % speed
 * waiting, full speed in the background). The engine's counters
 * (eng_cmds, eng_flags) and C64_NETTIME's frame times show the cost. */
#include <stdint.h>
#include <string.h>
#include "tcp.h"

#define REG(a) (*(volatile uint8_t *)(a))
#define PA     REG(0xdd00)                /* bit 2: PA2 (bits 0-1: the VIC bank, kept) */
#define DDRA   REG(0xdd02)
#define PB     REG(0xdd01)
#define DDRB   REG(0xdd03)
#define ICR    REG(0xdd0d)
#define FLAG2  0x10

#define CMD_OPEN      0x21
#define CMD_READ      0x22
#define CMD_WRITE     0x23
#define CMD_CLOSE     0x2e
#define CMD_VERSION   0x26

extern volatile uint8_t c64_ticks;        /* c64.s: +1 every TV frame (20 ms PAL) */

/* the background engine (c64.s) */
extern volatile uint8_t eng_state, eng_open, eng_failed, eng_gap, eng_wantread, eng_fill, eng_tick;
extern volatile uint8_t eng_wlen[2], eng_wlenh[2];
extern volatile uint16_t eng_head, eng_tail, eng_cmds, eng_flags;
extern uint8_t eng_wbuf0[4 + 512], eng_wbuf1[4 + 512], eng_rx[2048];
void wic64_kick(void);
#define WMAX 512
#define SEI() __asm__ volatile ("sei")
#define CLI() __asm__ volatile ("cli")

static uint8_t open;

/* FLAG2 within ticks TV frames: 1, else 0 */
static uint8_t wait_flag(uint8_t ticks) {
  uint8_t t = c64_ticks, n;
  for (;;) {
    for (n = 0; n < 64; n++) if (ICR & FLAG2) return 1;
    if ((uint8_t)(c64_ticks - t) > ticks) return 0;
  }
}

/* c64.s: n bytes out of p, or into p, over the user port; 1 done, 0 no FLAG2 */
extern const uint8_t *wic64_ptr;
extern uint16_t wic64_n;
uint8_t wic64_out(void);
uint8_t wic64_in(void);
static uint8_t out(const uint8_t *p, uint16_t n) { wic64_ptr = p; wic64_n = n; return wic64_out(); }
static uint8_t in(uint8_t *p, uint16_t n) { wic64_ptr = p; wic64_n = n; return wic64_in(); }

static uint16_t reply_len;                /* the reply's payload, after request() */

/* one request; then the reply's status (0 success), its length in
 * reply_len, the payload still to be read by take(); 0xff: no answer.
 * wait: TV frames for the reply (a connection takes a while) */
static uint8_t request(uint8_t cmd, const uint8_t *data, uint16_t n, uint8_t wait) {
  uint8_t h[4];
  h[0] = 'R'; h[1] = cmd; h[2] = (uint8_t)n; h[3] = (uint8_t)(n >> 8);
  (void)ICR;                              /* no FLAG2 from before */
  DDRB = 0xff;
  PA |= 0x04;                             /* the C64 sends */
  if (!out(h, 4) || !out(data, n)) return 0xff;
  DDRB = 0x00;
  PA &= (uint8_t)~0x04;                   /* the WiC64 sends */
  if (!wait_flag(wait)) return 0xff;      /* the reply is there */
  (void)PB;                               /* the dummy read: it fetches the first byte */
  if (!in(h, 3)) return 0xff;             /* the status, the length */
  reply_len = h[1] | (uint16_t)h[2] << 8;
  return h[0];
}

/* the reply's payload: up to max bytes into buf, the rest read and dropped; 0 if the WiC64 stopped */
static uint8_t take(uint8_t *buf, uint16_t max) {
  uint8_t b;
  uint16_t n = reply_len;
  if (n <= max) return in(buf, n);
  if (!in(buf, max)) return 0;
  for (n -= max; n; n--) if (!in(&b, 1)) return 0;
  return 1;
}

uint8_t tcp_present(void) {
  uint8_t v[4];
  DDRA |= 0x04;                           /* PA2 an output */
  if (request(CMD_VERSION, 0, 0, 10) != 0) return 0;
  return take(v, sizeof(v));
}

uint8_t tcp_open(const char *host, uint16_t port) {
  static char spec[64];
  char *p = spec, d[6];
  uint8_t n = 0, st;
  if (open) tcp_close();
  while (*host && p < spec + 56) *p++ = *host++;
  *p++ = ':';
  do d[n++] = (char)('0' + port % 10); while (port /= 10);
  while (n) *p++ = d[--n];
  st = request(CMD_OPEN, (const uint8_t *)spec, (uint16_t)(p - spec), 250);   /* up to 5 s to connect */
  if (st == 0xff || !take(0, 0)) return 2;
  if (st) return 3;
  open = 1;
  SEI();
  eng_state = eng_gap = eng_failed = eng_wantread = eng_fill = 0;
  eng_head = eng_tail = 0;
  eng_wlen[0] = eng_wlen[1] = eng_wlenh[0] = eng_wlenh[1] = 0;
  eng_open = 1;
  (void)ICR;
  ICR = 0x90;                               /* FLAG2 is an NMI now */
  CLI();
  return 0;
}

static uint8_t lost(void) {                 /* an error, or a command stuck for 2 s */
  if (eng_failed) return 1;
  if (eng_state && (uint8_t)(c64_ticks - eng_tick) > 100) eng_failed = 1;
  return eng_failed;
}

/* queued: the next write command sends it, with whatever else was queued */
uint8_t tcp_send(const uint8_t *buf, uint16_t n) {
  if (!open) return 1;
  while (n) {
    uint16_t len, k;
    uint8_t f;
    if (lost()) return 2;
    SEI();
    f = eng_fill;
    len = eng_wlen[f] | (uint16_t)eng_wlenh[f] << 8;
    k = WMAX - len;
    if (k > n) k = n;
    if (k) {
      memcpy((f ? eng_wbuf1 : eng_wbuf0) + 4 + len, buf, k);
      len += k; eng_wlen[f] = (uint8_t)len; eng_wlenh[f] = (uint8_t)(len >> 8);
      buf += k; n -= k;
    }
    CLI();
    wic64_kick();                           /* full: wait until it goes */
  }
  return 0;
}

/* what the reads brought (the ring eng_rx); else ask for a read (the
 * engine does it soon) and say "nothing" for now */
int16_t tcp_recv(uint8_t *buf, uint16_t max) {
  uint16_t h, t, n;
  if (lost()) return -1;
  if (!open) return -1;
  do h = eng_head; while (h != eng_head);   /* the NMI writes it a byte at a time */
  t = eng_tail;
  if (h != t) {
    n = (h - t) & 2047;
    if (n > 2048 - t) n = 2048 - t;         /* up to the ring's end */
    if (n > max) n = max;
    memcpy(buf, eng_rx + t, n);
    eng_tail = (t + n) & 2047;
    return (int16_t)n;
  }
  eng_wantread = 1;                         /* (still) wanted: start it if the WiC64 is free */
  wic64_kick();
  return 0;
}

void tcp_close(void) {
  uint8_t t;
  if (!open) return;
  eng_open = 0;                             /* no more commands; the one going ends */
  for (t = c64_ticks; eng_state && (uint8_t)(c64_ticks - t) < 100; ) ;
  ICR = 0x10;                               /* FLAG2 polled again */
  eng_state = 0;
  if (request(CMD_CLOSE, 0, 0, 50) != 0xff) take(0, 0);
  open = 0;
}
