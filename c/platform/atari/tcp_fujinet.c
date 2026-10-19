/* tcp.h over FujiNet (Atari 8-bit), its network device N1: on the SIO bus
 * (device 71h), through our own SIO routine (sio.s; the OS is off). The
 * commands, as the
 * firmware's NDevice handles them (fujinet-firmware lib/device/NDevice):
 *   'O' open:   aux1 12 (read and write), aux2 0 (no translation),
 *               256 bytes out: "N:TCP://host:port/", 0-terminated
 *   'S' status: 4 bytes in: bytes waiting (16 bits), connected, error
 *   'R' read:   aux1/aux2 = bytes, that many in
 *   'W' write:  aux1/aux2 = bytes, that many out
 *   'C' close
 * SIO busy-waits: a command costs its bytes at the baud rate plus the
 * handshake. */
#include <stdint.h>
#include <string.h>
#include "tcp.h"

#define DEV_N 0x71
#define SIO_IN  0x40
#define SIO_OUT 0x80
#define DIV_STD 0x28                      /* POKEY divisor for 19,200 baud */
#define CMD_HSIO '?'                      /* FujiNet's high-speed index (the POKEY divisor), then that speed */

/* sio.s: one SIO command without the OS (its ROM is off) */
extern uint8_t sio_dev, sio_cmd, sio_dir, sio_div, sio_wait, sio_btime, sio_acked;
extern uint16_t sio_aux, sio_len;
extern void *sio_buf;
uint8_t atari_sio(void);                  /* 1: done, 128 and up: error */

static uint8_t open, div = DIV_STD;
#ifdef NETTEST_TIME
uint16_t atari_sio_time;                  /* time in SIO since plat_atari.c last took it, 128 us units */
uint16_t atari_sio_fails;                 /* commands that failed in the end */
uint8_t atari_sio_failcmd, atari_sio_failr, atari_sio_failack;   /* the last of them */
uint16_t atari_now(void);
uint16_t atari_since(uint16_t t);
#endif

#ifdef SIO_BENCH
uint16_t atari_sio_calls;                 /* SIO commands sent, repeats included */
#endif
static uint8_t sio1(void) {
#ifdef SIO_BENCH
  atari_sio_calls++;
#endif
  sio_div = div;
  sio_btime = (uint8_t)(((div + 7) * 23 >> 8) + 2);   /* a byte's time and a little: VCOUNT steps */
  return atari_sio();
}

/* One command. A NAK (to the command or to our data frame: not done) is
 * tried once more. If the device did not answer the command frame at all,
 * or garbled, it is tried again, then at the other speed: FujiNet goes back
 * to 19,200 baud after bad command frames, and starts there after a reset.
 * Once accepted, a read or a write is not repeated for other failures (it
 * would take more bytes off the stream, or send its bytes twice); a status
 * or the speed query may be. */
static uint8_t div_hs = DIV_STD;          /* the high speed, once FujiNet named it */
static uint8_t sio(uint8_t cmd, uint8_t dir, void *buf, uint16_t len, uint16_t aux, uint8_t wait) {
  uint8_t r;
#ifdef NETTEST_TIME
  uint16_t t = atari_now();
#endif
  sio_dev = DEV_N; sio_cmd = cmd; sio_dir = dir;
  sio_buf = buf; sio_len = len; sio_aux = aux;
  sio_wait = wait;                         /* ticks of 20 ms for the device to complete */
  uint8_t again = cmd == 'S' || cmd == CMD_HSIO;      /* these may run twice */
  r = sio1();
  if (r == 0x8b || (r != 1 && r != 0x90 && (!sio_acked || again))) r = sio1();   /* not done: once more */
  if (r != 1 && r != 0x8b && r != 0x90 && (!sio_acked || again) && div_hs != DIV_STD) {
    div = div == DIV_STD ? div_hs : DIV_STD;          /* then the other speed */
    r = sio1();
  }
#ifdef NETTEST_TIME
  atari_sio_time += atari_since(t);
  if (r != 1) { atari_sio_fails++; atari_sio_failcmd = cmd; atari_sio_failr = r; atari_sio_failack = sio_acked; }
#endif
  return r;
}

/* ask FujiNet for its high speed and use it (it answers at 19,200, then switches) */
static void hsio(void) {
#ifndef NET_NO_HSIO
  uint8_t idx;
  div = DIV_STD;
  if (sio(CMD_HSIO, SIO_IN, &idx, 1, 0, 10) == 1 && idx < DIV_STD) div = div_hs = idx;
#endif
}

static uint8_t st[4];                     /* waiting (2), connected, error */
static uint8_t status(void) { return sio('S', SIO_IN, st, 4, 0, 10); }

uint8_t tcp_present(void) {
  uint8_t n = 3;                          /* a FujiNet that has just started may miss one */
  while (status() >= 128)                 /* 138: no device answered */
    if (!--n) return 0;
  hsio();
  return 1;
}

uint8_t tcp_open(const char *host, uint16_t port) {
  static char spec[256] __attribute__((section(".bss.lowfree")));   /* the OS's page 4-6 (hiram.ld) */
  char *p = spec, d[6];
  uint8_t n = 0;
  memset(spec, 0, sizeof(spec));
  memcpy(p, "N:TCP://", 8); p += 8;
  while (*host && p < spec + 240) *p++ = *host++;
  *p++ = ':';
  do d[n++] = (char)('0' + port % 10); while (port /= 10);
  while (n) *p++ = d[--n];
  *p = '/';
  if (open) tcp_close();
  if (div == DIV_STD) hsio();             /* back at 19,200 (a reset, errors): fast again */
  if (sio('O', SIO_OUT, spec, 256, 12, 250) != 1) return 2;     /* FujiNet connects before it completes */
  open = 1;
  if (status() != 1 || !st[2]) { tcp_close(); return 3; }
  return 0;
}

uint8_t tcp_send(const uint8_t *buf, uint16_t n) {
  if (!open) return 1;
  while (n) {
    uint16_t k = n > 256 ? 256 : n;
    if (sio('W', SIO_OUT, (void *)buf, k, k, 10) != 1) return 2;
    buf += k; n -= k;
  }
  return 0;
}

/* After a read that took all that was waiting, the next call says "nothing"
 * without asking: the WebSocket parser asks again at once, and a status
 * command costs about 4 ms of the frame. Data that comes meanwhile is read
 * on the call after. */
static uint8_t drained;
int16_t tcp_recv(uint8_t *buf, uint16_t max) {
  uint16_t n, waiting;
  if (drained) { drained = 0; return 0; }
  if (!open || status() != 1) return -1;
  waiting = n = st[0] | (uint16_t)st[1] << 8;
  if (!n) return st[2] ? 0 : -1;          /* nothing waiting; closed when no longer connected */
  if (n > max) n = max;
  if (sio('R', SIO_IN, buf, n, n, 10) != 1) return -1;
  drained = n == waiting;
  return (int16_t)n;
}

void tcp_close(void) {
  if (!open) return;
  sio('C', 0, 0, 0, 0, 10);
  open = 0;
}

#ifdef SIO_BENCH
/* the timing benchmark (sio_bench.c, tools/atari/fakefn.atdevice): the fake
 * FujiNet makes n bytes wait after each write ('I', its own command) */
void tcp_bench_in(uint16_t n) { sio('I', 0, 0, 0, n, 10); }
#endif
