/* tcp.h (through tcp_pc.c) over a WiFi modem on a PC's serial port (docs/port-pc.md): an
 * ESP8266 with Hayes firmware (Zimodem, WiFi232, RetroWiFi and the like)
 * that dials a TCP connection, ATDT host:port, and is then a plain byte
 * stream both ways until "+++" and ATH; the same commands as the Atari
 * ST's (c/platform/atarist/tcp_modem.c). Telnet translation off (ATNET0:
 * a telnet modem doubles every 0xff byte).
 *
 * The UART (8250 or 16550, its FIFO used when there is one) driven by its
 * interrupt both ways: what comes in goes to a ring of 4 KB, what goes out
 * from a ring of 1 KB, so the game neither loses bytes nor waits for the
 * line. The modem is looked for on COM1 and COM2, at 19200 bps and at
 * 115200 (the first that answers AT with OK); where nothing at all answers
 * there is no waiting, and the game plays locally. Times: the game's 1 kHz
 * timer (pc_ms). */
#include <stdint.h>
#include <stdio.h>
#include <string.h>
#include <conio.h>
#include <dos.h>
#include <i86.h>

uint32_t pc_ms(void);                     /* plat_pc.c: milliseconds */
extern uint8_t pc_modem_port;             /* plat_pc.c, the command line: 0 either, 1 COM1, 2 COM2 */
extern uint32_t pc_modem_baud;            /* plat_pc.c, the command line: 0 both speeds */
extern void (*pc_net_cleanup)(void);      /* plat_pc.c calls it at the end */
uint32_t pc_modem_tx, pc_modem_rx;        /* counters for the network test's log */

#define RX_SIZE 4096u                     /* powers of two: the indices wrap by a mask */
#define TX_SIZE 1024u
static volatile uint8_t rx_ring[RX_SIZE], tx_ring[TX_SIZE];
static volatile uint16_t rx_head, rx_tail, tx_head, tx_tail;   /* head: the next to write; tail: the next to read */
static volatile uint8_t tx_busy;          /* the transmitter's interrupt is on (it empties the ring) */
static uint16_t base;                     /* the UART's port; 0: none open */
static uint8_t irq, vector, fifo;
static void (__interrupt __far *old_vector)(void);
void modem_close(void);
static uint8_t online, have_modem, heard;  /* heard: any byte at all came in (since it was cleared) */
static uint8_t nc_at;                     /* online: how much of "\r\nNO CARRIER\r\n" the last bytes matched (it ends the call) */

static void __interrupt __far uart_isr(void) {
  uint8_t iir, n;
  while (!((iir = (uint8_t)inp(base + 2)) & 1)) {   /* a cause pending */
    switch (iir & 6) {
    case 4:                               /* bytes received: all of them into the ring */
      while (inp(base + 5) & 1) {
        uint8_t b = (uint8_t)inp(base);
        uint16_t h = (uint16_t)((rx_head + 1) & (RX_SIZE - 1));
        if (h != rx_tail) { rx_ring[rx_head] = b; rx_head = h; }
      }
      break;
    case 2:                               /* the transmitter empty: as many as it takes (16 with a FIFO) */
      for (n = fifo ? 16 : 1; n && tx_tail != tx_head; n--) {
        outp(base, tx_ring[tx_tail]);
        tx_tail = (uint16_t)((tx_tail + 1) & (TX_SIZE - 1));
      }
      if (tx_tail == tx_head) { outp(base + 1, 0x01); tx_busy = 0; }   /* nothing left: its interrupt off */
      break;
    case 6: (void)inp(base + 5); break;   /* a line status: read, ignored */
    default: (void)inp(base + 6); break;  /* a modem status: read, ignored */
    }
  }
  outp(0x20, 0x20);
}

static void uart_close(void) {
  if (!base) return;
  _disable();
  outp(base + 1, 0);                      /* its interrupts off, at the UART and at the controller */
  outp(base + 4, 0);
  outp(0x21, inp(0x21) | (1 << irq));
  _dos_setvect(vector, old_vector);
  _enable();
  base = 0;
}

static uint8_t uart_open(uint16_t port, uint8_t line_irq, uint32_t bps) {
  uint16_t div = (uint16_t)(115200UL / bps);
  uart_close();
  outp(port + 7, 0x5a);                   /* a UART there? (its scratch register; an 8250 without one fails, rare) */
  if (inp(port + 7) != 0x5a) return 0;
  base = port; irq = line_irq; vector = (uint8_t)(8 + line_irq);
  rx_head = rx_tail = tx_head = tx_tail = 0; tx_busy = 0;
  _disable();
  outp(base + 1, 0);
  outp(base + 3, 0x80);                   /* the divisor */
  outp(base, div & 0xff); outp(base + 1, div >> 8);
  outp(base + 3, 0x03);                   /* 8N1 */
  outp(base + 2, 0xc7);                   /* the FIFO on, emptied, an interrupt at 14 bytes (nothing on an 8250) */
  fifo = (inp(base + 2) & 0xc0) == 0xc0;
  outp(base + 4, 0x0b);                   /* DTR, RTS, OUT2 (the interrupt line) */
  while (inp(base + 5) & 1) (void)inp(base);
  (void)inp(base + 2); (void)inp(base + 6);
  old_vector = _dos_getvect(vector);
  _dos_setvect(vector, uart_isr);
  outp(0x21, inp(0x21) & ~(1 << irq));
  outp(base + 1, 0x01);                   /* the receiver's interrupt on */
  _enable();
  return 1;
}

static int rx_byte(void) {                /* a byte waiting, or -1 */
  uint8_t b;
  if (rx_tail == rx_head) return -1;
  b = rx_ring[rx_tail];
  rx_tail = (uint16_t)((rx_tail + 1) & (RX_SIZE - 1));
  heard = 1;
  return b;
}

static int rx_peek(void) { return rx_tail == rx_head ? -1 : rx_ring[rx_tail]; }

static void tx(const char *s, uint16_t n) {
  while (n--) {
    uint16_t h = (uint16_t)((tx_head + 1) & (TX_SIZE - 1));
    while (h == tx_tail) ;                /* the ring full: the interrupt is emptying it */
    tx_ring[tx_head] = (uint8_t)*s++;
    _disable();
    tx_head = h;
    if (!tx_busy) { tx_busy = 1; outp(base + 1, 0x03); }   /* the transmitter's interrupt on: it fires at once, being empty */
    _enable();
  }
}
static void tx_str(const char *s) { tx(s, (uint16_t)strlen(s)); }

static void quiet(uint16_t ms) {          /* nothing sent for a while (the "+++" guard time), what comes in dropped */
  uint32_t t = pc_ms();
  while (pc_ms() - t < ms) rx_byte();
}

/* a line from the modem within the timeout: 1 and the line (without its
 * CR LF), or 0. Blank lines are skipped. */
static uint8_t line(char *out, uint8_t max, uint16_t ms) {
  uint32_t t = pc_ms();
  uint8_t n = 0;
  while (pc_ms() - t < ms) {
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
static uint8_t command(const char *cmd, uint16_t ms) {
  char l[48];
  uint32_t t = pc_ms(), e;
  tx_str(cmd); tx_str("\r");
  while ((e = pc_ms() - t) < ms) {
    if (!line(l, sizeof(l), (uint16_t)(ms - e))) break;
    if (!strcmp(l, "OK")) return 0;
    if (!strcmp(l, "ERROR")) return 1;
  }
  return 2;
}

static void escape(void) {                /* online -> command mode: a second's quiet, "+++", a second's quiet */
  quiet(1100);
  tx_str("+++");
  quiet(1100);
}

static void cleanup(void) {
  if (online) { escape(); command("ATH", 2000); online = 0; }
  { uint32_t t = pc_ms(); while (tx_busy && pc_ms() - t < 200) ; }   /* the last bytes out */
  uart_close();
}

/* the modem on one port at one speed: OK to AT; something else answering
 * may be a modem still online from before (its far end got our AT): out
 * of it, and again; silence: none, no waiting */
static uint8_t probe(uint16_t port, uint8_t line_irq, uint32_t bps) {
  if (!uart_open(port, line_irq, bps)) return 0;
  tx_str("\r");
  quiet(100);
  heard = 0;
  if (command("AT", 300) == 0) return 1;
  if (!heard) return 0;
  escape();
  command("ATH", 1000);
  return command("AT", 500) == 0;
}

uint8_t modem_present(void) {
  static uint8_t done;
  static const uint16_t ports[2] = {0x3f8, 0x2f8};
  static const uint8_t irqs[2] = {4, 3};
  static const uint32_t speeds[2] = {19200UL, 115200UL};
  uint8_t p, s;
  if (done) return have_modem;
  done = 1;
  pc_net_cleanup = cleanup;
  for (p = 0; p < 2 && !have_modem; p++) {
    if (pc_modem_port && p != pc_modem_port - 1) continue;
    if (pc_modem_baud) have_modem = probe(ports[p], irqs[p], pc_modem_baud);
    else for (s = 0; s < 2 && !have_modem; s++) have_modem = probe(ports[p], irqs[p], speeds[s]);
  }
  if (!have_modem) { uart_close(); return 0; }
  command("ATE0", 500);                   /* no echo */
  command("ATNET0", 500);                 /* no telnet (an ERROR where the firmware has no such setting) */
  return 1;
}

uint8_t modem_open(const char *host, uint16_t port) {
  char cmd[72], l[48];
  uint32_t t;
  if (!have_modem) return 1;
  modem_close();
  sprintf(cmd, "ATDT%s:%u\r", host, (unsigned)port);
  tx_str(cmd);
  t = pc_ms();
  while (pc_ms() - t < 20000UL) {
    if (!line(l, sizeof(l), 1000)) continue;
    if (!strncmp(l, "CONNECT", 7)) {   /* the line ends with CR LF, and line() stopped at the CR: */
      uint32_t w = pc_ms();             /* the LF is no data (it would put the HTTP reply a byte out) */
      while (pc_ms() - w < 50 && rx_peek() < 0) ;
      if (rx_peek() == '\n') rx_byte();
      online = 1; nc_at = 0;
      return 0;
    }
    if (!strcmp(l, "NO CARRIER") || !strcmp(l, "ERROR") || !strcmp(l, "BUSY") || !strcmp(l, "NO ANSWER")) return 4;
  }
  return 4;
}

uint8_t modem_send(const uint8_t *buf, uint16_t n) {
  if (!online) return 1;
  pc_modem_tx += n;
  tx((const char *)buf, n);
  return 0;
}

/* The bytes straight from the ring, each matched against "NO CARRIER" by
 * one compare (a matcher's state, not a 14-byte window shifted and compared
 * at every byte: that cost an XT most of the 10 ms the network took a frame) */
int16_t modem_recv(uint8_t *buf, uint16_t max) {
  static const char nc[15] = "\r\nNO CARRIER\r\n";
  uint16_t n = 0, t = rx_tail;
  uint8_t c, at = nc_at;
  if (!online) return -1;
  while (n < max && t != rx_head) {
    c = rx_ring[t];
    t = (uint16_t)((t + 1) & (RX_SIZE - 1));
    buf[n++] = c;
    if (c == (uint8_t)nc[at]) {
      if (++at == 14) { rx_tail = t; online = 0; return -1; }   /* the far end closed: the modem is back in command mode */
    } else at = (uint8_t)(c == '\r');   /* the pattern's only repeat is its first byte */
  }
  rx_tail = t; nc_at = at;
  if (n) heard = 1;
  pc_modem_rx += n;
  return (int16_t)n;
}

void modem_close(void) {
  if (!online) return;
  escape();
  command("ATH", 2000);
  online = 0;
}
