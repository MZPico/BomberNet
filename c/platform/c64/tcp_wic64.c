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
 * The CPU waits throughout; CIA 2's interrupts are off (plat_init), so the
 * FLAG2 bit is polled, and reading the ICR clears it. */
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

#ifdef C64_NETTIME
/* the driver's cost (tools/crossplay.py reads it): cycles in tcp_send and
 * tcp_recv, of them the waits for a reply (in VICE a TV frame for a TCP
 * read or write: its alarm), commands and bytes over the user port */
uint32_t c64_cycles(void);
__attribute__((used)) struct { uint32_t total, waits; uint16_t commands, bytes; uint32_t t_out, t_in; } c64_net;   /* used: kept whole (LTO would split it) */
#define NT(x) x
#else
#define NT(x)
#endif

static uint8_t open, drained;
static uint8_t rx[2048];                  /* a read's bytes not yet taken (more: a stall of seconds, taken as lost) */
static uint16_t rx_pos, rx_len;

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
static uint8_t out(const uint8_t *p, uint16_t n) {
  uint8_t r;
  NT(uint32_t t = c64_cycles(); c64_net.bytes += n;)
  wic64_ptr = p; wic64_n = n; r = wic64_out();
  NT(c64_net.t_out += c64_cycles() - t;)
  return r;
}
static uint8_t in(uint8_t *p, uint16_t n) {
  uint8_t r;
  NT(uint32_t t = c64_cycles(); c64_net.bytes += n;)
  wic64_ptr = p; wic64_n = n; r = wic64_in();
  NT(c64_net.t_in += c64_cycles() - t;)
  return r;
}

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
  NT(uint32_t t0 = c64_cycles(); c64_net.commands++;)
  if (!wait_flag(wait)) return 0xff;      /* the reply is there */
  NT(c64_net.waits += c64_cycles() - t0;)
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
  open = 1; drained = 0; rx_pos = rx_len = 0;
  return 0;
}

uint8_t tcp_send(const uint8_t *buf, uint16_t n) {
  uint8_t st;
  NT(uint32_t t0 = c64_cycles();)
  if (!open) return 1;
  st = request(CMD_WRITE, buf, n, 50);
  NT(c64_net.total += c64_cycles() - t0;)
  if (st == 0xff || !take(0, 0) || st) return 2;
  return 0;
}

/* After a read that took all that was waiting, the next call says
 * "nothing" without asking (the WebSocket parser asks again at once). */
int16_t tcp_recv(uint8_t *buf, uint16_t max) {
  uint8_t st;
  uint16_t n;
  if (rx_pos < rx_len) {                  /* left from the last read */
    n = rx_len - rx_pos;
    if (n > max) n = max;
    memcpy(buf, rx + rx_pos, n); rx_pos += n;
    return (int16_t)n;
  }
  if (drained) { drained = 0; return 0; }
  if (!open) return -1;
  NT(uint32_t t0 = c64_cycles();)
  st = request(CMD_READ, 0, 0, 50);
  if (st == 0xff || st) return -1;
  if (reply_len > sizeof(rx)) return -1;  /* more than a read should bring */
  if (!take(rx, sizeof(rx))) return -1;
  NT(c64_net.total += c64_cycles() - t0;)
  if (!reply_len) return 0;
  rx_len = reply_len; rx_pos = 0;
  drained = 1;
  return tcp_recv(buf, max);
}

void tcp_close(void) {
  if (!open) return;
  if (request(CMD_CLOSE, 0, 0, 50) != 0xff) take(0, 0);
  open = 0;
}
