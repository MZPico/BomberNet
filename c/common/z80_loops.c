/* Core loops in Z80 assembly, shared by the Z80 platforms (PLAT_ASM_COMPOSITE,
 * PLAT_ASM_HASH in plat_config.h). No hardware access; IY is not touched
 * (the Spectrum ROM interrupt needs it). */
#include <stdint.h>
#include "game.h"

/* composite_map: non-space map cells over the draw buffer, unrolled 8x */
void composite_map(void) __naked {
  __asm
    ld   hl,_map_layer
    ld   de,_draw_buf
    ld   c,125
cm_loop:
    ld   a,(hl)
    cp   0x20
    jr   z,cm_0
    ld   (de),a
cm_0:
    inc  hl
    inc  de
    ld   a,(hl)
    cp   0x20
    jr   z,cm_1
    ld   (de),a
cm_1:
    inc  hl
    inc  de
    ld   a,(hl)
    cp   0x20
    jr   z,cm_2
    ld   (de),a
cm_2:
    inc  hl
    inc  de
    ld   a,(hl)
    cp   0x20
    jr   z,cm_3
    ld   (de),a
cm_3:
    inc  hl
    inc  de
    ld   a,(hl)
    cp   0x20
    jr   z,cm_4
    ld   (de),a
cm_4:
    inc  hl
    inc  de
    ld   a,(hl)
    cp   0x20
    jr   z,cm_5
    ld   (de),a
cm_5:
    inc  hl
    inc  de
    ld   a,(hl)
    cp   0x20
    jr   z,cm_6
    ld   (de),a
cm_6:
    inc  hl
    inc  de
    ld   a,(hl)
    cp   0x20
    jr   z,cm_7
    ld   (de),a
cm_7:
    inc  hl
    inc  de
    dec  c
    jr   nz,cm_loop
    ret
  __endasm;
}

/* State hash byte loop (game.c): hh = rotl16(hh) ^ *p++ + 9E37h, hash_n
 * times. About 50 T per byte. */
void hash_run(void) __naked {
  __asm
    ld   hl,(_hash_ptr)
    ld   bc,(_hash_n)
    ld   de,(_hh)
hr_loop:
    ld   a,b
    or   c
    jr   z,hr_done
    dec  bc
    sla  e                  ; rotate left 16
    rl   d
    jr   nc,hr_nc
    inc  e                  ; bit 0 <- old bit 15 (bit 0 is clear after sla)
hr_nc:
    ld   a,(hl)
    inc  hl
    xor  e
    add  a,0x37             ; + 9E37h
    ld   e,a
    ld   a,d
    adc  a,0x9e
    ld   d,a
    jr   hr_loop
hr_done:
    ld   (_hh),de
    ret
  __endasm;
}
