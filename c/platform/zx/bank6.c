/* The 128K Spectrum's RAM bank 6: a program of its own at C000h with the
 * network driver (tcp.h), called by the game through tcp_bank6.c. It is
 * linked as a normal z88dk program, but its start-up code never runs: the
 * game calls bank6_init once (the variables are zeroed), then the tcp_*
 * functions, whose addresses tools/zx128_entries.py takes from the map.
 * Everything here runs with bank 6 paged in at C000h and the stack at its
 * top, so it must not touch the game's memory above C000h; the game passes
 * data through its staging buffer below C000h. */
#include <stdint.h>

const char bank6_signature[4] = {'B', 'N', 'B', '6'};   /* checked by the game before the first call */

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
