/* The network over an Ultimate's own network (Ultimate 64, Ultimate-II+;
 * docs/port-c64.md): its command interface (UCI) at $DF1C-$DF1F, enabled
 * in the Ultimate's menu, and the network target ($03), as documented
 * (1541u-documentation.readthedocs.io, uci/) and as the firmware does it
 * (github.com/GideonZ/1541ultimate, software/io/network/network_target.cc):
 *
 *   $DF1C write: control (bit 0 push the command, 1 data accepted, 2
 *   abort, 3 clear the error); read: status (bit 7 data waits, 6 status
 *   waits, 5-4 the state: 0 idle, 1 busy, 2 last data, 3 more data; 3 the
 *   error). $DF1D write: the command's bytes; read: $C9, the interface's
 *   identification. $DF1E: the reply's data, $DF1F: its status text
 *   ("00,OK"), a byte a read.
 *
 * Commands: $03 $01 identify; $03 $07 port (low, high) host: open TCP, a
 * socket in reply; $03 $10 socket length (low, high): read, the count
 * (low, high) and the bytes in reply ("01" closed, "02" nothing waiting);
 * $03 $11 socket bytes: write; $03 $09 socket: close.
 *
 * The firmware gives each socket a 40 ms receive timeout: a read with
 * nothing waiting keeps the interface busy that long, and returns as soon
 * as data comes. While a connection is open the raster interrupt, every
 * quarter of a TV frame, takes the reply of the command going and pushes
 * the next (c64.s, uci_poll): a write with all that tcp_send queued, else
 * a read, so one is always waiting and a message is in the ring within
 * about 5 ms of reaching the Ultimate. The bytes read go to the WiC64
 * engine's ring (only one device is used), the writes queue in its first
 * buffer. Finding, opening and closing wait here, the pump off. */
#include <stdint.h>
#include <string.h>
#include "tcp_c64.h"

#define REG(a)  (*(volatile uint8_t *)(a))
#define CTRL    REG(0xdf1c)
#define STATUS  REG(0xdf1c)
#define CMD     REG(0xdf1d)
#define IDENT   REG(0xdf1d)
#define DATA    REG(0xdf1e)
#define STAT    REG(0xdf1f)
#define C_PUSH  0x01
#define C_ACC   0x02
#define C_ABORT 0x04
#define C_CLR   0x08
#define S_DATA  0x80
#define S_STAT  0x40
#define STATE   ((STATUS >> 4) & 3)
#define IDLE    0
#define BUSY    1

extern volatile uint8_t c64_ticks;        /* c64.s: +1 every TV frame */
extern volatile uint16_t eng_head, eng_tail;
extern uint8_t eng_rx[2048], eng_wbuf0[4 + 512];
#define RING 2048
#define WMAX 512

/* c64.s: the pump in the background */
extern volatile uint8_t uci_bg, uci_going, uci_sock, uci_failed;
extern volatile uint16_t uci_wq;
void uci_poll(void);
#define SEI() __asm__ volatile ("sei")
#define CLI() __asm__ volatile ("cli")

static uint8_t open;
static char st[4];                        /* the reply's status: its first characters */
static uint8_t reply[40];
static uint16_t reply_len;

/* until the interface leaves "busy": 1, or 0 after ticks TV frames */
static uint8_t wait_ready(uint8_t ticks) {
  uint8_t t = c64_ticks;
  while (STATE == BUSY) if ((uint8_t)(c64_ticks - t) > ticks) return 0;
  return 1;
}

static void push(const uint8_t *b, uint16_t n) {
  CTRL = C_CLR;
  while (n--) CMD = *b++;
  CTRL = C_PUSH;
}

/* the reply: up to max data bytes into buf (or, ring: into eng_rx after
 * the 2-byte count), the status's first characters into st; then accepted */
static void collect(uint8_t *buf, uint16_t max, uint8_t ring) {
  uint16_t n = 0, h = eng_head;
  uint8_t k = 0, b;
  for (;;) {
    while (STATUS & S_DATA) {
      b = DATA;
      if (ring) { if (n++ >= 2) { eng_rx[h] = b; h = (h + 1) & (RING - 1); } }
      else if (n < max) buf[n++] = b;
    }
    while (STATUS & S_STAT) { b = STAT; if (k < sizeof(st) - 1) st[k++] = (char)b; }
    if (STATE == 3) { CTRL = C_ACC; wait_ready(10); continue; }   /* more data: the next block */
    break;
  }
  st[k] = 0;
  CTRL = C_ACC;
  if (ring) eng_head = h;
  else reply_len = n;
}

static uint8_t ok(void) { return st[0] == '0' && st[1] == '0'; }

/* one command, waiting for it (finding, opening, closing) */
static uint8_t command(const uint8_t *b, uint16_t n, uint8_t ticks) {
  if (STATE != IDLE) { CTRL = C_ABORT; wait_ready(10); CTRL = C_ACC; }
  push(b, n);
  if (!wait_ready(ticks)) { CTRL = C_ABORT; return 0; }
  collect(reply, sizeof(reply), 0);
  return ok();
}

uint8_t uci_present(void) {
  static const uint8_t identify[2] = {0x03, 0x01};
  if (IDENT != 0xc9) return 0;            /* no Ultimate, or its command interface is off */
  return command(identify, 2, 10);
}

uint8_t uci_open(const char *host, uint16_t port) {
  static uint8_t c[64];
  uint8_t n = 4;
  if (open) uci_close();
  c[0] = 0x03; c[1] = 0x07; c[2] = (uint8_t)port; c[3] = (uint8_t)(port >> 8);
  while (*host && n < 62) c[n++] = (uint8_t)*host++;
  c[n++] = 0;
  if (!command(c, n, 250)) return 2;      /* up to 5 s to connect */
  if (reply_len < 1) return 3;
  SEI();
  uci_sock = reply[0];
  uci_failed = uci_going = 0; uci_wq = 0;
  eng_head = eng_tail = 0;
  uci_bg = 1;                             /* the pump starts its first read */
  CLI();
  open = 1;
  return 0;
}

uint8_t uci_send(const uint8_t *buf, uint16_t n) {
  uint8_t t = c64_ticks;
  if (!open) return 1;
  while (n) {
    uint16_t k;
    if (uci_failed) return 2;
    SEI();
    k = WMAX - uci_wq;
    if (k > n) k = n;
    memcpy(eng_wbuf0 + 3 + uci_wq, buf, k); uci_wq += k; buf += k; n -= k;
    uci_poll();                           /* the write goes now if the interface is free */
    CLI();
    if (n && (uint8_t)(c64_ticks - t) > 100) return 2;   /* the queue did not empty in 2 s */
  }
  return 0;
}

int16_t uci_recv(uint8_t *buf, uint16_t max) {
  uint16_t h, t = eng_tail, n;
  if (!open || uci_failed) return -1;
  SEI();
  h = eng_head;                           /* the interrupt writes it */
  CLI();
  if (h == t) return 0;
  n = (h - t) & (RING - 1);
  if (n > RING - t) n = RING - t;         /* up to the ring's end */
  if (n > max) n = max;
  memcpy(buf, eng_rx + t, n);
  eng_tail = (t + n) & (RING - 1);
  return (int16_t)n;
}

void uci_close(void) {
  uint8_t c[3];
  if (!open) return;
  uci_bg = 0;                             /* the pump stops; a command going ends */
  if (uci_going) { wait_ready(10); CTRL = C_ACC; uci_going = 0; }
  c[0] = 0x03; c[1] = 0x09; c[2] = uci_sock;
  command(c, 3, 10);
  open = 0;
}
