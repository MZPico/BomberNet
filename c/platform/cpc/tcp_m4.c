/* tcp.h over the M4 board (Amstrad CPC), as the M4's own ROM and examples
 * drive it (github.com/M4Duke: m4rom/M4ROM.s, M4examples/tcp.s, lookup.s):
 *
 * - a command is a packet written to port FE00h byte by byte (length,
 *   command word, parameters) and started by an OUT to FC00h;
 * - the answer is in the M4's ROM, paged in at C000h for the moment of
 *   reading: the pointer table at FF00h gives the response buffer (FF02h:
 *   byte 3 result, 4..5 size, 6.. data) and the socket table (FF06h: 16
 *   bytes per socket; status 0 idle, 1 connect in progress, 2 send in
 *   progress, 3 closed by the peer, 5 lookup in progress on entry 0, F0h..
 *   error; bytes 2..3 received and waiting; 4..7 the IP, least significant
 *   byte first);
 * - the ROM is found by the first name of its RSX table, "M4 BOARD".
 * Writes go to the RAM under a paged-in ROM (the screen), so paging it in
 * costs nothing but the time; the interrupt counter lives below C000h. */
#include <stdint.h>
#include <string.h>
#include "tcp.h"

extern volatile uint8_t cpc_ticks;

#define C_NETSOCKET  0x4331
#define C_NETCONNECT 0x4332
#define C_NETCLOSE   0x4333
#define C_NETSEND    0x4334
#define C_NETRECV    0x4335
#define C_NETHOSTIP  0x4336

static uint8_t m4_rom = 0xff;                   /* ROM number of the M4, 0xff: none */
static uint16_t m4_resp, m4_socks;              /* response buffer, socket table */
static uint8_t sock = 0xff;
uint8_t m4_pkt[12];                             /* command header (public for the assembly) */
uint8_t m4_hlen;
const uint8_t *m4_data;                         /* bytes sent after the header */
uint16_t m4_dlen;

/* page the M4 ROM in at C000h (and out again: Mode 1, both ROMs off) */
static void m4_on(void) __naked {
  __asm
    ld   a,(_m4_rom)
    ld   bc,0xdf00
    out  (c),a
    ld   bc,0x7f85          ; Mode 1, upper ROM on, lower ROM off
    out  (c),c
    ret
  __endasm;
}

static void m4_off(void) __naked {
  __asm
    ld   bc,0x7f8d          ; Mode 1, both ROMs off
    out  (c),c
    ret
  __endasm;
}

/* send m4_pkt (m4_hlen bytes), then m4_dlen bytes from m4_data, and start it */
static void m4_out(void) __naked {
  __asm
    ld   hl,_m4_pkt
    ld   a,(_m4_hlen)
    ld   d,a
    ld   bc,0xfe00
mo_h:
    inc  b                  ; OUTI puts B - 1 on the bus: FEh
    outi
    dec  d
    jr   nz,mo_h
    ld   hl,(_m4_data)
    ld   a,(_m4_dlen)       ; at most 250 bytes (tcp_send splits longer ones)
    or   a
    jr   z,mo_ack
    ld   d,a
mo_d:
    inc  b                  ; OUTI: 36 T-states a byte with the loop
    outi
    dec  d
    jr   nz,mo_d
mo_ack:
    ld   bc,0xfc00
    out  (c),c
    ret
  __endasm;
}

static void cmd(uint16_t c, uint8_t hlen, const uint8_t *data, uint16_t dlen) {
  m4_pkt[0] = (uint8_t)(hlen - 1 + dlen);       /* bytes after the length byte (ignored by C_NETSEND) */
  m4_pkt[1] = (uint8_t)c; m4_pkt[2] = (uint8_t)(c >> 8);
  m4_hlen = hlen; m4_data = data; m4_dlen = dlen;
  m4_out();
}

static uint8_t resp8(uint8_t off) {
  uint8_t v;
  m4_on(); v = *(volatile uint8_t *)(m4_resp + off); m4_off();
  return v;
}

/* the socket's entry: status and bytes waiting */
static uint8_t s_status;
static uint16_t s_waiting;
static void entry(uint8_t n) {
  volatile uint8_t *e = (volatile uint8_t *)(m4_socks + (uint16_t)n * 16);
  m4_on();
  s_status = e[0]; s_waiting = e[2] | (uint16_t)e[3] << 8;
  m4_off();
}

/* wait while the entry's status is `busy`, at most `ticks` interrupts (300 a second) */
static uint8_t wait_entry(uint8_t n, uint8_t busy, uint16_t ticks) {
  uint8_t t = cpc_ticks, now;
  for (;;) {
    entry(n);
    if (s_status != busy) return s_status;
    now = cpc_ticks;
    if ((uint8_t)(now - t) >= 1) {
      if (ticks <= (uint8_t)(now - t)) return 0xff;
      ticks -= (uint8_t)(now - t);
      t = now;
    }
  }
}

uint8_t tcp_present(void) {
  uint8_t n;
  static const uint8_t name[8] = {'M', '4', ' ', 'B', 'O', 'A', 'R', 'D' | 0x80};
  for (n = 127; n != 0xff; n--) {
    uint16_t p;
    uint8_t ok;
    m4_rom = n;
    m4_on();
    p = *(volatile uint16_t *)0xC004;
    ok = p >= 0xC000 && p < 0xFFF8 && !memcmp((const void *)p, name, 8);
    if (ok) { m4_resp = *(volatile uint16_t *)0xFF02; m4_socks = *(volatile uint16_t *)0xFF06; }
    m4_off();
    if (ok) return 1;
  }
  m4_rom = 0xff;
  return 0;
}

/* a dotted IPv4 address -> the M4's order (least significant byte first); 1 if it was one */
static uint8_t parse_ip(const char *s, uint8_t *ip) {
  uint8_t i, v;
  for (i = 0; i < 4; i++) {
    if (*s < '0' || *s > '9') return 0;
    v = 0;
    while (*s >= '0' && *s <= '9') v = (uint8_t)(v * 10 + (*s++ - '0'));
    ip[3 - i] = v;
    if (i < 3 && *s++ != '.') return 0;
  }
  return *s == 0;
}

uint8_t tcp_open(const char *host, uint16_t port) {
  uint8_t ip[4], i, st;
  static const uint8_t sockparm[3] = {0, 0, 6};                /* domain, type, TCP */
  tcp_close();
  if (m4_rom == 0xff) return 0xfd;
  if (!parse_ip(host, ip)) {                                    /* DNS: entry 0 */
    cmd(C_NETHOSTIP, 3, (const uint8_t *)host, (uint16_t)strlen(host) + 1);
    if (resp8(3) != 1) return 0xf1;
    if (wait_entry(0, 5, 3000) != 0) return 0xf1;
    m4_on();
    for (i = 0; i < 4; i++) ip[i] = *(volatile uint8_t *)(m4_socks + 4 + i);
    m4_off();
  }
  cmd(C_NETSOCKET, 3, sockparm, 3);
  sock = resp8(3);
  if (sock == 0xff) return 0xf2;
  m4_pkt[3] = sock;
  memcpy(m4_pkt + 4, ip, 4);
  m4_pkt[8] = (uint8_t)port; m4_pkt[9] = (uint8_t)(port >> 8);
  cmd(C_NETCONNECT, 10, 0, 0);
  if (resp8(3) == 0xff) { tcp_close(); return 0xf3; }
  st = wait_entry(sock, 1, 3000);
  if (st != 0) { tcp_close(); return 0xf3; }
  return 0;
}

uint8_t tcp_send(const uint8_t *buf, uint16_t n) {
  uint16_t k;
  if (sock == 0xff) return 0xfd;
  while (n) {
    k = n > 250 ? 250 : n;                                      /* the length byte of a packet */
    if (wait_entry(sock, 2, 1500) != 0) return 0xf4;            /* the previous send is done */
    m4_pkt[3] = sock; m4_pkt[4] = (uint8_t)k; m4_pkt[5] = (uint8_t)(k >> 8);
    cmd(C_NETSEND, 6, buf, k);
    buf += k; n -= k;
  }
  return 0;
}

int16_t tcp_recv(uint8_t *buf, uint16_t max) {
  uint16_t n;
  if (sock == 0xff) return -1;
  entry(sock);
  if (s_status >= 0xf0) return -1;
  if (!s_waiting) return s_status == 3 ? -1 : 0;
  if (max > s_waiting) max = s_waiting;
  m4_pkt[3] = sock; m4_pkt[4] = (uint8_t)max; m4_pkt[5] = (uint8_t)(max >> 8);
  cmd(C_NETRECV, 6, 0, 0);
  m4_on();
  n = 0;
  if (*(volatile uint8_t *)(m4_resp + 3) == 0) {
    n = *(volatile uint16_t *)(m4_resp + 4);
    if (n > max) n = max;
    memcpy(buf, (const void *)(m4_resp + 6), n);
  }
  m4_off();
  return (int16_t)n;
}

void tcp_close(void) {
  if (sock == 0xff) return;
  m4_pkt[3] = sock;
  cmd(C_NETCLOSE, 4, 0, 0);
  sock = 0xff;
}
