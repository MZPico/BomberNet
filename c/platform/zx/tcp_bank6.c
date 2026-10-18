/* tcp.h on the 128K Spectrum: the driver lives in RAM bank 6 (bank6.c and
 * the driver linked with it, at C000h), this side pages it in and calls it.
 *
 * This file is linked first, so all of it is below C000h: the code and the
 * page and stack state stay visible while bank 6 replaces bank 0 at C000h.
 * Data in and out goes through the staging buffer at 5D00h-5DBFh, as the
 * game's own buffers and its stack may be above C000h: the BASIC loader's
 * area below the game, free once the game runs (it never returns). A
 * call runs on bank 6's own stack (from its top down) and returns to the
 * caller's page and stack; BANKM (5B5Ch) is kept with the port, as the
 * ROM expects. The entry addresses come from bank 6's map (bank6_entries.h,
 * written by tools/zx128_entries.py). */
#include <stdint.h>
#include <string.h>
#include "tcp.h"
#include "bank6_entries.h"

#define STAGE_SIZE 192
#define b6_stage ((uint8_t *)0x5d00)
static uint8_t b6_state;                       /* 0 not checked, 1 ready, 2 no bank 6 driver */

/* entry(a1, a2) in bank 6: the arguments as the C function there expects them */
static uint16_t bank_call(uint16_t entry, uint16_t a1, uint16_t a2) __naked {
  (void)entry; (void)a1; (void)a2;
  __asm
    ld   hl,2
    add  hl,sp
    ld   e,(hl)
    inc  hl
    ld   d,(hl)             ; a2
    inc  hl
    ld   c,(hl)
    inc  hl
    ld   b,(hl)             ; a1
    inc  hl
    ld   a,(hl)
    inc  hl
    ld   h,(hl)
    ld   l,a                ; entry
    ld   (b6_a1),bc
    ld   (b6_a2),de
    ld   (b6_entry),hl
    push ix
    push iy
    di
    ld   (b6_sp),sp
    ld   a,(0x5b5c)         ; BANKM
    ld   (b6_page),a
    and  0xf8
    or   6
    ld   bc,0x7ffd
    ld   (0x5b5c),a
    out  (c),a
    ld   sp,0               ; the stack of bank 6, from FFFFh down
    ei
    ld   hl,(b6_a1)
    push hl
    ld   hl,(b6_a2)
    push hl
    ld   hl,b6_return
    push hl
    ld   hl,(b6_entry)
    jp   (hl)
b6_return:                  ; HL = the result
    pop  bc
    pop  bc
    di
    ld   a,(b6_page)
    ld   bc,0x7ffd
    ld   (0x5b5c),a
    out  (c),a
    ld   sp,(b6_sp)
    ei
    pop  iy
    pop  ix
    ret

; Is the driver in bank 6? Its signature, read with bank 6 paged in.
; On a 48K machine (or a 128K one locked to 48K) the port does nothing and
; C000h shows the game. -> HL = 1 when it is there
b6_check:
    di
    ld   a,(0x5b5c)
    ld   (b6_page),a
    and  0xf8
    or   6
    ld   bc,0x7ffd
    out  (c),a
    ld   hl,BANK6_SIGNATURE
    ld   de,b6_sign
    ld   b,4
b6_cmp:
    ld   a,(de)
    cp   (hl)
    jr   nz,b6_cmp_end
    inc  hl
    inc  de
    djnz b6_cmp
b6_cmp_end:
    ld   a,(b6_page)
    ld   c,0xfd
    ld   d,b                ; 0: all four matched
    ld   b,0x7f
    out  (c),a
    ei
    ld   hl,0
    ld   a,d
    or   a
    ret  nz
    inc  l
    ret

; For the tape loader (tools/make_zx128_tap.py), by RANDOMIZE USR once the
; game is loaded: zx128_page6 pages bank 6 in and returns to BASIC, which
; then loads the driver there; zx128_start pages bank 0 back and starts the
; game. BANKM goes with the port: the ROM pages by it.
    PUBLIC zx128_page6, zx128_start
zx128_page6:
    ld   a,0x16             ; ROM 1, bank 6
    ld   bc,0x7ffd
    di
    ld   (0x5b5c),a
    out  (c),a
    ei
    ret
zx128_start:
    di
    ld   a,0x10             ; ROM 1, bank 0
    ld   bc,0x7ffd
    ld   (0x5b5c),a
    out  (c),a
    jp   24000

b6_sign:  defm "BNB6"
b6_a1:    defw 0
b6_a2:    defw 0
b6_entry: defw 0
b6_sp:    defw 0
b6_page:  defb 0
  __endasm;
}

static uint8_t b6_present(void) __naked {
  __asm
    jp   b6_check
  __endasm;
}

static uint8_t ready(void) {
  if (!b6_state) {
    b6_state = 2;
    if (b6_present()) { bank_call(BANK6_INIT, 0, 0); b6_state = 1; }
  }
  return b6_state == 1;
}

uint8_t tcp_present(void) {
  return ready() && (uint8_t)bank_call(BANK6_TCP_PRESENT, 0, 0);
}

uint8_t tcp_open(const char *host, uint16_t port) {
  if (!ready()) return 0xfd;
  strcpy((char *)b6_stage, host);                  /* the relay's name: short */
  return (uint8_t)bank_call(BANK6_TCP_OPEN, (uint16_t)b6_stage, port);
}

static uint16_t k;                               /* static: smaller code with sccz80 */
static uint8_t r;
uint8_t tcp_send(const uint8_t *buf, uint16_t n) {
  if (!ready()) return 0xfd;
  for (; n; buf += k, n -= k) {
    k = n > STAGE_SIZE ? STAGE_SIZE : n;
    memcpy(b6_stage, buf, k);
    if ((r = (uint8_t)bank_call(BANK6_TCP_SEND, (uint16_t)b6_stage, k)) != 0) return r;
  }
  return 0;
}

int16_t tcp_recv(uint8_t *buf, uint16_t max) {
  if (!ready()) return -1;
  k = (uint16_t)bank_call(BANK6_TCP_RECV, (uint16_t)b6_stage, max > STAGE_SIZE ? STAGE_SIZE : max);
  if ((int16_t)k > 0) memcpy(buf, b6_stage, k);
  return (int16_t)k;
}

void tcp_close(void) {
  if (b6_state == 1) bank_call(BANK6_TCP_CLOSE, 0, 0);
}
