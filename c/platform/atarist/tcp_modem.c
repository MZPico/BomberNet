/* tcp.h over a WiFi modem on the ST's serial port (docs/port-atari-st.md):
 * an ESP8266 with Hayes firmware (Zimodem, WiFi232, RetroWiFi and the
 * like) that dials a TCP connection, ATDT host:port, and is then a plain
 * byte stream both ways until "+++" and ATH. The port runs at 19200 bps,
 * 8N1, no flow control; the modem is to be set to the same speed, and
 * telnet translation off (ATNET0 is sent: a telnet modem doubles 0xff
 * bytes, which the binary messages are full of).
 *
 * TOS's interrupt receives into the RS-232 input buffer (Iorec); it is
 * replaced by one of 4 KB while the game runs (TOS's own is 256 bytes:
 * a second of the relay's messages) and read directly, the program being
 * in supervisor mode. Bytes go out through the BIOS (Bconout, AUX:).
 * Timeouts count TOS's 200 Hz clock (_hz_200). Without a modem answering
 * AT the game plays locally. */
#include <stdint.h>
#include <stdio.h>
#include <string.h>
#include <mint/osbind.h>
#include "tcp.h"

#define HZ200 (*(volatile uint32_t *)0x4ba)
#define AUX 1
#define RX_SIZE 4096

static char rx_buf[RX_SIZE];
static _IOREC *rx;
static char *old_ibuf;
static short old_size, old_low, old_high;
static uint8_t online, have_modem, heard;   /* heard: any byte at all came in (since it was cleared) */
static char nc_tail[14];                  /* the last bytes received online: "\r\nNO CARRIER\r\n" ends the call */
extern void (*st_net_cleanup)(void);      /* plat_st.c calls it at the end */
uint32_t st_modem_tx, st_modem_rx;        /* counters for the network test's log */
uint32_t st_send_ticks, st_recv_calls, st_recv_bytes;   /* per frame: timer A's ticks (600 Hz) in tcp_send; tcp_recv's calls, bytes */
extern volatile uint32_t st_ta_ticks;

static int rx_byte(void) {                /* a byte waiting, or -1 */
  short h;
  if (rx->ibufhd == rx->ibuftl) return -1;
  h = (short)(rx->ibufhd + 1);
  if (h >= rx->ibufsiz) h = 0;
  rx->ibufhd = h;
  heard = 1;
  return (uint8_t)rx->ibuf[h];
}

static int rx_peek(void) {                /* the next byte waiting, left there, or -1 */
  short h;
  if (rx->ibufhd == rx->ibuftl) return -1;
  h = (short)(rx->ibufhd + 1);
  if (h >= rx->ibufsiz) h = 0;
  return (uint8_t)rx->ibuf[h];
}

static void tx(const char *s, uint16_t n) {
  while (n--) Bconout(AUX, (uint8_t)*s++);
}
static void tx_str(const char *s) { tx(s, (uint16_t)strlen(s)); }

static void quiet(uint16_t ticks) {       /* nothing sent for a while (the "+++" guard time), what comes in dropped */
  uint32_t t = HZ200;
  while (HZ200 - t < ticks) rx_byte();
}

/* a line from the modem within the timeout: 1 and the line (without its
 * CR LF), or 0. Blank lines are skipped. */
static uint8_t line(char *out, uint8_t max, uint16_t ticks) {
  uint32_t t = HZ200;
  uint8_t n = 0;
  while (HZ200 - t < ticks) {
    int c = rx_byte();
    if (c < 0) continue;
    if (c == '\r' || c == '\n') {
      if (n) { out[n] = 0; return 1; }
      continue;
    }
    if (n < max - 1) out[n++] = (char)c;
  }
  return 0;
}

/* a command, then its answer: 0 OK, 1 ERROR, 2 nothing (other lines, the
 * command's echo among them, skipped) */
static uint8_t command(const char *cmd, uint16_t ticks) {
  char l[48];
  uint32_t t = HZ200;
  tx_str(cmd); tx_str("\r");
  while (HZ200 - t < ticks) {
    if (!line(l, sizeof(l), (uint16_t)(ticks - (HZ200 - t)))) break;
    if (!strcmp(l, "OK")) return 0;
    if (!strcmp(l, "ERROR")) return 1;
  }
  return 2;
}

static void escape(void) {                /* online -> command mode: a second's quiet, "+++", a second's quiet */
  quiet(220);
  tx_str("+++");
  quiet(220);
}

static void cleanup(void) {
  if (online) { escape(); command("ATH", 400); online = 0; }
  if (rx) {
    uint16_t sr;
    __asm__ volatile("move.w sr,%0\n\tmove.w #0x2700,sr" : "=d"(sr));
    rx->ibuf = old_ibuf; rx->ibufsiz = old_size; rx->ibuflow = old_low; rx->ibufhi = old_high;
    rx->ibufhd = rx->ibuftl = 0;
    __asm__ volatile("move.w %0,sr" :: "d"(sr));
    rx = NULL;
  }
}

uint8_t tcp_present(void) {
  if (!rx) {
    uint16_t sr;
    Rsconf(BAUD_19200, FLOW_NONE, -1, -1, -1, -1);
    rx = Iorec(IO_SERIAL);
    __asm__ volatile("move.w sr,%0\n\tmove.w #0x2700,sr" : "=d"(sr));
    old_ibuf = rx->ibuf; old_size = rx->ibufsiz; old_low = rx->ibuflow; old_high = rx->ibufhi;
    rx->ibuf = rx_buf; rx->ibufsiz = RX_SIZE; rx->ibuflow = RX_SIZE / 4; rx->ibufhi = RX_SIZE * 3 / 4;
    rx->ibufhd = rx->ibuftl = 0;
    __asm__ volatile("move.w %0,sr" :: "d"(sr));
    st_net_cleanup = cleanup;
    tx_str("\r");
    quiet(20);
    heard = 0;
    if (command("AT", 100) == 0) have_modem = 1;
    else if (heard) {                   /* something answered, not OK: perhaps a modem still online from before (its */
      escape();                         /* far end got our AT): out of it, and again. Silence: no modem, no waiting */
      command("ATH", 200);
      have_modem = command("AT", 100) == 0;
    }
    if (have_modem) {
      command("ATE0", 100);             /* no echo */
      command("ATNET0", 100);           /* no telnet (an ERROR where the firmware has no such setting) */
    }
  }
  return have_modem;
}

uint8_t tcp_open(const char *host, uint16_t port) {
  char cmd[72], l[48];
  uint32_t t;
  if (!have_modem) return 1;
  tcp_close();
  snprintf(cmd, sizeof(cmd), "ATDT%s:%u\r", host, (unsigned)port);
  tx_str(cmd);
  t = HZ200;
  while (HZ200 - t < 4000) {              /* 20 s */
    if (!line(l, sizeof(l), 200)) continue;
    if (!strncmp(l, "CONNECT", 7)) {   /* the line ends with CR LF, and line() stopped at the CR: */
      uint32_t w = HZ200;               /* the LF is no data (it put the HTTP reply a byte out) */
      while (HZ200 - w < 10 && rx_peek() < 0) ;
      if (rx_peek() == '\n') rx_byte();
      online = 1; memset(nc_tail, 0, sizeof(nc_tail));
      return 0;
    }
    if (!strcmp(l, "NO CARRIER") || !strcmp(l, "ERROR") || !strcmp(l, "BUSY") || !strcmp(l, "NO ANSWER")) return 4;
  }
  return 4;
}

uint8_t tcp_send(const uint8_t *buf, uint16_t n) {
  if (!online) return 1;
  st_modem_tx += n;
  { uint32_t t = st_ta_ticks; tx((const char *)buf, n); st_send_ticks += st_ta_ticks - t; }
  return 0;
}

int16_t tcp_recv(uint8_t *buf, uint16_t max) {
  static const char nc[14] = "\r\nNO CARRIER\r\n";
  int16_t n = 0;
  int c;
  if (!online) return -1;
  st_recv_calls++;
  while (n < (int16_t)max && (c = rx_byte()) >= 0) {
    buf[n++] = (uint8_t)c;
    memmove(nc_tail, nc_tail + 1, sizeof(nc_tail) - 1);
    nc_tail[sizeof(nc_tail) - 1] = (char)c;
    if (!memcmp(nc_tail, nc, sizeof(nc))) { online = 0; return -1; }   /* the far end closed: the modem is back in command mode */
  }
  st_modem_rx += (uint16_t)n; st_recv_bytes += (uint16_t)n;
  return n;
}

void tcp_close(void) {
  if (!online) return;
  escape();
  command("ATH", 400);
  online = 0;
}
