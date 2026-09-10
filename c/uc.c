/* Unicard transport primitives (z88dk, same code as the MZPico manager). */
#include <stdint.h>
#include "uc.h"

void uc_cmd(uint8_t command) __naked {
  __asm
    push iy
    ld iy, 4
    add iy, sp
    ld a, (iy+0)
    out (UC_CMD_PORT), a
    pop iy
    ret
  __endasm;
}

void uc_wr(uint8_t data) __naked {
  __asm
    push iy
    ld iy, 4
    add iy, sp
    ld a, (iy+0)
    out (UC_DATA_PORT), a
    pop iy
    ret
  __endasm;
}

uint8_t uc_rd(void) __naked {
  __asm
    in a, (UC_DATA_PORT)
    ld h, 0
    ld l, a
    ret
  __endasm;
}

/* STSR rewinds the status pointer; it parks after the 4th byte */
void uc_status4(uint8_t *status) __naked {
  __asm
    push iy
    ld iy, 4
    add iy, sp
    ld l, (iy+0)
    ld h, (iy+1)
    ld a, cmdSTSR
    out (UC_CMD_PORT), a
    ld c, UC_CMD_PORT
    ld b, 4
    inir
    pop iy
    ret
  __endasm;
}

/* INIR in 256-byte blocks; the device holds EXWAIT until a byte is ready */
void uc_read(uint8_t *dst, uint16_t n) __naked {
  __asm
    push iy
    ld iy, 4
    add iy, sp
    ld e, (iy+0)
    ld d, (iy+1)
    ld l, (iy+2)
    ld h, (iy+3)
    ld c, UC_DATA_PORT
_ucr_blocks:
    ld b, 0
    ld a, d
    or a
    jr z, _ucr_rest
    inir
    dec d
    jr _ucr_blocks
_ucr_rest:
    ld b, e
    or b
    jr z, _ucr_done
    inir
_ucr_done:
    pop iy
    ret
  __endasm;
}

void uc_wstr(const char *s) {
  while (*s) uc_wr((uint8_t)*s++);
  uc_wr(0x0d);
}
