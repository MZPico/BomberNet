/* Core loops in Z80 assembly, for the Z80 machines that ask for them in
 * plat_config.h (PLAT_ASM_COMPOSITE, PLAT_ASM_HASH, PLAT_ASM_TEXT). The C in
 * core/ is the reference; see docs/portability.md. No hardware access; IY is
 * not touched (the Spectrum ROM interrupt needs it). */
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

/* composite_frame: composite_map at the start of a frame, when the flush has
 * emptied draw_buf and only the status row is drawn: the 960 playfield cells
 * are copied (16 T a byte instead of 38), the status row is merged (the idea:
 * Milan Stava's ESP-01 port) */
void composite_frame(void) __naked {
  __asm
    ld   hl,_map_layer
    ld   de,_draw_buf
    ld   bc,960
cf_copy:
    ldi
    ldi
    ldi
    ldi
    ldi
    ldi
    ldi
    ldi
    ldi
    ldi
    ldi
    ldi
    ldi
    ldi
    ldi
    ldi
    jp   pe,cf_copy         ; 960 = 60 x 16: BC reaches 0 at the end of a pass
    ld   b,40
cf_hud:
    ld   a,(hl)
    cp   0x20
    jr   z,cf_0
    ld   (de),a
cf_0:
    inc  hl
    inc  de
    djnz cf_hud
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

#ifdef PLAT_ASM_TEXT
/* Text into the draw buffer, character by character through a mapping; they
 * run every title and lobby frame. Arguments (p, s): sccz80 pushes p first,
 * so at entry the stack holds the return address, s, p. */
extern const uint8_t hud_letters[26];

/* print_num5 / print_num4 / print_num2 (core/video.c has them in C): the
 * digits as logical codes 0..9 by subtracting powers of ten. In C, sccz80
 * made 176 + 152 + 64 bytes of them and 0.7 ms a number on the Spectrum. */
void print_num5(uint8_t *p, uint16_t v) __naked {   /* 5 digits + the fixed trailing 0 */
  (void)p; (void)v;
  __asm
    call pn_args
    ld   bc,-10000
    call pn_digit
pn_four:
    ld   bc,-1000
    call pn_digit
    ld   bc,-100
    call pn_digit
    ld   bc,-10
    call pn_digit
    ld   a,l                ; the units
    ld   (de),a
    inc  de
    xor  a                  ; the original always shows a trailing 0
    ld   (de),a
    ret
pn_args:                    ; (return, ret to the caller, v, p) -> HL = v, DE = p
    ld   hl,4
    add  hl,sp
    ld   e,(hl)
    inc  hl
    ld   d,(hl)
    inc  hl
    ld   a,(hl)
    inc  hl
    ld   h,(hl)
    ld   l,a
    ex   de,hl
    ret
pn_digit:                   ; HL = the value, BC = -power -> (DE++) = digit, HL = the rest
    ld   a,-1
pn_sub:
    inc  a
    add  hl,bc
    jr   c,pn_sub
    sbc  hl,bc              ; one too many (carry is clear)
    ld   (de),a
    inc  de
    ret
  __endasm;
}

void print_num4(uint8_t *p, uint16_t v) __naked {   /* 4 digits (9999 at most) + the trailing 0 */
  (void)p; (void)v;
  __asm
    call pn_args
    push hl
    ld   bc,-10000
    add  hl,bc
    pop  hl
    jr   nc,pn_four
    ld   hl,9999
    jr   pn_four
  __endasm;
}

void print_num2(uint8_t *p, uint8_t v) __naked {
  (void)p; (void)v;
  __asm
    call pn_args            ; L = v, DE = p
    ld   a,l
    ld   b,-1
pn_tens:
    inc  b
    sub  10
    jr   nc,pn_tens
    add  a,10
    ex   de,hl
    ld   (hl),b
    inc  hl
    ld   (hl),a
    ret
  __endasm;
}

void print_string(uint8_t *p, const char *s) __naked {
  __asm
    pop  bc
    pop  hl
    pop  de
    push de
    push hl
    push bc
ps_l:
    ld   a,(hl)
    or   a
    ret  z
    ld   (de),a
    inc  hl
    inc  de
    jr   ps_l
  __endasm;
}

/* title mode: digits -> 0..9, '-' -> 40h, A-Z and space as they are, else space */
void title_text(uint8_t *p, const char *s) __naked {
  __asm
    pop  bc
    pop  hl
    pop  de
    push de
    push hl
    push bc
tt_l:
    ld   a,(hl)
    or   a
    ret  z
    inc  hl
    cp   '0'
    jr   c,tt_n
    cp   '9' + 1
    jr   nc,tt_n
    sub  '0'
    jr   tt_put
tt_n:
    cp   '-'
    jr   nz,tt_a
    ld   a,0x40
    jr   tt_put
tt_a:
    cp   ' '
    jr   z,tt_put
    cp   'A'
    jr   c,tt_sp
    cp   'Z' + 1
    jr   c,tt_put
tt_sp:
    ld   a,0x20
tt_put:
    ld   (de),a
    inc  de
    jr   tt_l
  __endasm;
}

/* title mode, yellow: A-Z -> 60h.., 0-5 -> 7Ah.., 6-9 -> 25h.., '-' -> 29h */
void title_text_hl(uint8_t *p, const char *s) __naked {
  __asm
    pop  bc
    pop  hl
    pop  de
    push de
    push hl
    push bc
th_l:
    ld   a,(hl)
    or   a
    ret  z
    inc  hl
    cp   'A'
    jr   c,th_d
    cp   'Z' + 1
    jr   nc,th_sp
    add  a,0x60 - 'A'
    jr   th_put
th_d:
    cp   '-'
    jr   nz,th_d1
    ld   a,0x29
    jr   th_put
th_d1:
    cp   '0'
    jr   c,th_sp
    cp   '5' + 1
    jr   nc,th_d2
    add  a,0x7a - '0'
    jr   th_put
th_d2:
    cp   '9' + 1
    jr   nc,th_sp
    add  a,0x25 - '6'
    jr   th_put
th_sp:
    ld   a,0x20
th_put:
    ld   (de),a
    inc  de
    jr   th_l
  __endasm;
}

/* greyed out: A-Z -> E0h.., else space */
void title_text_dim(uint8_t *p, const char *s) __naked {
  __asm
    pop  bc
    pop  hl
    pop  de
    push de
    push hl
    push bc
td_l:
    ld   a,(hl)
    or   a
    ret  z
    inc  hl
    cp   'A'
    jr   c,td_sp
    cp   'Z' + 1
    jr   nc,td_sp
    add  a,0xe0 - 'A'
    jr   td_put
td_sp:
    ld   a,0x20
td_put:
    ld   (de),a
    inc  de
    jr   td_l
  __endasm;
}

/* game mode: A-Z through hud_letters, digits -> 0..9, else space */
void hud_text(uint8_t *p, const char *s) __naked {
  __asm
    pop  bc
    pop  hl
    pop  de
    push de
    push hl
    push bc
hx_l:
    ld   a,(hl)
    or   a
    ret  z
    inc  hl
    cp   '0'
    jr   c,hx_sp
    cp   '9' + 1
    jr   nc,hx_a
    sub  '0'
    jr   hx_put
hx_a:
    cp   'A'
    jr   c,hx_sp
    cp   'Z' + 1
    jr   nc,hx_sp
    push hl
    sub  'A'
    ld   c,a
    ld   b,0
    ld   hl,_hud_letters
    add  hl,bc
    ld   a,(hl)
    pop  hl
    jr   hx_put
hx_sp:
    ld   a,0x20
hx_put:
    ld   (de),a
    inc  de
    jr   hx_l
  __endasm;
}
#endif
