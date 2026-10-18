/* The 128K Spectrum's RAM bank 6: a program of its own at C000h with the
 * network driver (tcp.h), called by the game through tcp_bank6.c. It is
 * linked as a normal z88dk program, but its start-up code never runs: the
 * game calls bank6_init once (the variables are zeroed), then the tcp_*
 * functions, whose addresses tools/zx128_entries.py takes from the map.
 * Everything here runs with bank 6 paged in at C000h and the stack at its
 * top, so it must not touch the game's memory above C000h; the game passes
 * data through its staging buffer below C000h. */
#include <stdint.h>
#include "tcp.h"

const char bank6_signature[4] = {'B', 'N', 'B', '6'};   /* checked by the game before the first call */

#ifdef BANK6_ESP01
/* Is there an eLeMeNt ZX or an MB03+ (the machines with the ESP-01 UART)?
 * As the Wi-Fi BIOS 2.0 by Busy and Hood tells them: ZXi register FFh, the
 * eLeMeNt's at 783Bh/793Bh reads E1h (its ID text), the MB03+'s at
 * 703Bh/773Bh reads 3xh. Without either the driver would spend 15 s in its
 * recovery of a module that is not there. The ports have A1 = 1: not the
 * 128K's paging port. -> L = 1 when one is there */
static uint8_t esp01_machine(void) __naked {
  __asm
    ld   bc,0x783b          ; eLeMeNt: register FFh, the ID
    ld   a,0xff
    out  (c),a
    inc  b                  ; 793Bh
    in   a,(c)
    and  0xf0
    cp   0xe0
    jr   z,em_yes
    ld   bc,0x703b          ; MB03+: register FFh
    ld   a,0xff
    out  (c),a
    ld   b,0x77             ; 773Bh
    in   a,(c)
    and  0xf0
    cp   0x30
    jr   z,em_yes
    ld   hl,0
    ret
em_yes:
    ld   hl,1
    ret
  __endasm;
}
#endif

/* the game's tcp_present, through bank6_entries.h */
uint8_t bank6_present(void) {
#ifdef BANK6_ESP01
  if (!esp01_machine()) return 0;
#endif
  return tcp_present();
}

void bank6_init(void) __naked {
  __asm
    EXTERN __BSS_head, __BSS_END_tail
    ld   hl,__BSS_head
    ld   de,__BSS_head+1
    ld   bc,__BSS_END_tail-__BSS_head-1
    ld   (hl),0
    ldir
    ld   hl,0
    ret
  __endasm;
}

void bank6_no_console(void) __naked {          /* the library's console output: none */
  __asm
    PUBLIC zx_no_console
zx_no_console:
    ret
  __endasm;
}

int main(void) { return 0; }                   /* the start-up code needs one; never called */
