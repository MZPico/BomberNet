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

/* sio.s: one SIO command without the OS (its ROM is off) */
extern uint8_t sio_dev, sio_cmd, sio_dir, sio_div, sio_wait, sio_btime;
extern uint16_t sio_aux, sio_len;
extern void *sio_buf;
uint8_t atari_sio(void);                  /* 1: done, 128 and up: error */

static uint8_t open;

static uint8_t sio(uint8_t cmd, uint8_t dir, void *buf, uint16_t len, uint16_t aux, uint8_t wait) {
  sio_dev = DEV_N; sio_cmd = cmd; sio_dir = dir;
  sio_buf = buf; sio_len = len; sio_aux = aux;
  sio_div = DIV_STD; sio_wait = wait;      /* ticks of 20 ms for the device to complete */
  sio_btime = (uint8_t)(((DIV_STD + 7) * 23 >> 8) + 2);   /* a byte's time and a little: VCOUNT steps */
  return atari_sio();
}

static uint8_t st[4];                     /* waiting (2), connected, error */
static uint8_t status(void) { return sio('S', SIO_IN, st, 4, 0, 10); }

uint8_t tcp_present(void) {
  return status() < 128;                  /* 138: no device answered */
}

uint8_t tcp_open(const char *host, uint16_t port) {
  static char spec[256];
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

int16_t tcp_recv(uint8_t *buf, uint16_t max) {
  uint16_t n;
  if (!open || status() != 1) return -1;
  n = st[0] | (uint16_t)st[1] << 8;
  if (!n) return st[2] ? 0 : -1;          /* nothing waiting; closed when no longer connected */
  if (n > max) n = max;
  if (sio('R', SIO_IN, buf, n, n, 10) != 1) return -1;
  return (int16_t)n;
}

void tcp_close(void) {
  if (!open) return;
  sio('C', 0, 0, 0, 0, 10);
  open = 0;
}
