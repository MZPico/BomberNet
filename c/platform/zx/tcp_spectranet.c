/* tcp.h over the Spectranet socket calls (Spectranet and Spectranext).
 * The calls go through IXCALL (3FFDh): the interface pages its ROM in, runs
 * the routine at IX and pages it out again. Registers in and out as in the
 * Spectranet ROM sources (rom/w5100_*.asm, rom/dns.asm); errors set carry
 * with the code in A. Parameters pass through globals to keep the call
 * stub simple. */
#include <stdint.h>
#include "tcp.h"

#define SN_SOCKET        0x3e00      /* C = type -> A = fd */
#define SN_CLOSE         0x3e03      /* A = fd */
#define SN_CONNECT       0x3e0f      /* A = fd, DE = 4-byte IP, BC = port */
#define SN_SEND          0x3e12      /* A = fd, DE = data, BC = length -> BC = sent */
#define SN_RECV          0x3e15      /* A = fd, DE = buffer, BC = size -> BC = received (blocks) */
#define SN_POLLFD        0x3e24      /* A = fd -> Z = not ready, C = reason: bit 2 data, bit 1 closed */
#define SN_GETHOSTBYNAME 0x3e27      /* HL = name, DE = 4-byte result */
#define SOCK_STREAM 1

uint8_t sn_a;
uint16_t sn_bc, sn_de, sn_hl;

/* call a Spectranet routine; returns the flags (bit 0 carry, bit 6 zero) */
static uint8_t sn_call(uint16_t fn) __z88dk_fastcall __naked {
  __asm
    push ix
    push hl
    pop  ix                 ; routine address
    ld   a,(_sn_a)
    ld   bc,(_sn_bc)
    ld   de,(_sn_de)
    ld   hl,(_sn_hl)
    call 0x3ffd             ; IXCALL
    ld   (_sn_a),a
    ld   (_sn_bc),bc
    push af
    pop  hl                 ; L = flags
    ld   h,0
    pop  ix
    ret
  __endasm;
}

#define CARRY(f) ((f) & 0x01)
#define ZERO(f)  ((f) & 0x40)

static uint8_t fd = 0xff;
static uint8_t ip[4];

/* The Spectranet control register (033Bh) mirrors the border colour in its
 * low 3 bits; without the interface the port reads the floating bus. The
 * border is black all the time, so it is set to cyan for the test only.
 * A Spectranet with its disable switch on answers the port too, but its
 * firmware never started and its traps (IXCALL) are off: a call would run
 * the Spectrum ROM instead. So the interface is paged in by the control
 * register (bit 0) and the API jump table the firmware sets up at start
 * (3E00h: SOCKET .. GETHOSTBYNAME, 14 JP instructions) is checked. */
static uint8_t detect(void) __naked {
  __asm
    ei
    halt
    ld   a,5
    out  (0xfe),a
    ld   bc,0x033b
    in   a,(c)
    and  0x07
    ld   l,a
    xor  a
    out  (0xfe),a
    ld   a,l
    sub  5
    jr   nz,dt_no
    di
    ld   a,1
    out  (c),a              ; page the Spectranet in
    ld   hl,0x3e00
    ld   e,14
dt_tbl:
    ld   a,(hl)
    cp   0xc3               ; JP
    jr   nz,dt_out          ; Z clear: no working firmware
    inc  hl
    inc  hl
    inc  hl
    dec  e
    jr   nz,dt_tbl          ; Z set after the last entry
dt_out:
    ld   a,0                ; (the flags stay)
    out  (c),a              ; and out again
    ei
    ld   l,1
    jr   z,dt_yes
dt_no:
    ld   l,0
dt_yes:
    ld   h,0
    ret
  __endasm;
}

uint8_t tcp_present(void) { return detect(); }

uint8_t tcp_open(const char *host, uint16_t port) {
  uint8_t f;
  tcp_close();
  sn_hl = (uint16_t)host; sn_de = (uint16_t)ip;
  f = sn_call(SN_GETHOSTBYNAME);
  if (CARRY(f)) return sn_a;
  sn_bc = SOCK_STREAM;
  f = sn_call(SN_SOCKET);
  if (CARRY(f)) return sn_a;
  fd = sn_a;
  sn_de = (uint16_t)ip; sn_bc = port;
  f = sn_call(SN_CONNECT);
  if (CARRY(f)) { f = sn_a; tcp_close(); return f; }
  return 0;
}

uint8_t tcp_send(const uint8_t *buf, uint16_t n) {
  if (fd == 0xff) return 0xfd;
  sn_a = fd; sn_de = (uint16_t)buf; sn_bc = n;
  if (CARRY(sn_call(SN_SEND))) return sn_a;
  return 0;
}

int16_t tcp_recv(uint8_t *buf, uint16_t max) {
  uint8_t f;
  if (fd == 0xff) return -1;
  sn_a = fd;
  f = sn_call(SN_POLLFD);
  if (CARRY(f)) return -1;
  if (ZERO(f)) return 0;                           /* nothing to read */
  if (sn_bc & 0x04) {                              /* data waiting */
    sn_a = fd; sn_de = (uint16_t)buf; sn_bc = max;
    if (CARRY(sn_call(SN_RECV))) return -1;
    return (int16_t)sn_bc;
  }
  return (sn_bc & 0x02) ? -1 : 0;                  /* closed by the peer */
}

void tcp_close(void) {
  if (fd == 0xff) return;
  sn_a = fd;
  sn_call(SN_CLOSE);
  fd = 0xff;
}
