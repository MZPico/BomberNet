/*
 * Amstrad CPC platform layer (z88dk sccz80, +cpc target), Mode 1.
 *
 * The game takes the machine over in plat_init: interrupts go to a counter
 * of its own (at 38h, both ROMs paged out), the firmware is not called again.
 * Screen: 40 x 25 cells of 8 x 8 pixels at C000h, a cell is 2 bytes x 8
 * lines (the lines of a cell are 800h apart). Four inks: black, red, yellow,
 * cyan. A cell is drawn from its MZ glyph and a colour scheme (one ink or a
 * checkerboard of two, see tools/make_cpc_tables.py): each 4-pixel half of a
 * glyph line becomes a Mode 1 byte through the scheme's lookup table.
 */
#include <stdint.h>
#include "game.h"
#include "tables.h"

/* menu texts: the joystick port is on every CPC (a second stick through a
 * splitter); the JOYSTICK row switches the two sticks on as inputs */
const char *const plat_joy_names[PLAT_JOY_TYPES] = {"NONE    ", "CPC     "};
const char *const plat_kbd_a_alt_name = "CURSOR AND COPY ";

const char *plat_input_name(uint8_t input) {
  switch (input) {
  case INPUT_KBD_A: return "CURSOR AND SPACE";
  case INPUT_KBD_B: return "WASD AND E      ";
  case INPUT_JOY1:  return "JOYSTICK 1      ";
  case INPUT_JOY2:  return "JOYSTICK 2      ";
  }
  return "";
}

uint8_t plat_input_allowed(uint8_t input) {
  if (input == INPUT_JOY1 || input == INPUT_JOY2) return joy_type != JOY_NONE;
  return input == INPUT_KBD_A || input == INPUT_KBD_B;
}

/* The game never prints through the C library; the start-up code would link
 * its console driver anyway, so it is redirected to nothing. */
void cpc_no_console_stub(void) __naked {
  __asm
    PUBLIC cpc_no_console
cpc_no_console:
    ret
  __endasm;
}

volatile uint8_t cpc_ticks;             /* +1 per interrupt: 300 per second */
uint8_t cpc_scheme_ovr = 0xff;          /* draw with this scheme instead of the table's (0xff: none) */
uint8_t cd_x, cd_y, cd_code;            /* cpc_cell: the cell to draw */

/* ---- keyboard: one line of the 10 x 8 matrix (0 = pressed) ----
 * The matrix is read through the AY's port A (register 14) with the PPI:
 * select the register, switch PPI port A to input, put the line on port C
 * with the AY in read mode, read port A. */
static uint8_t cpc_line(uint8_t line) __z88dk_fastcall __naked {
  __asm
    ld   bc,0xf40e          ; AY register 14
    out  (c),c
    ld   bc,0xf6c0          ; select it
    out  (c),c
    ld   bc,0xf600
    out  (c),c
    ld   bc,0xf792          ; PPI port A in
    out  (c),c
    ld   a,l
    or   0x40               ; the line, AY read
    ld   b,0xf6
    ld   c,a
    out  (c),c
    ld   b,0xf4
    in   a,(c)
    ld   bc,0xf782          ; PPI port A out
    out  (c),c
    ld   bc,0xf600
    out  (c),c
    ld   l,a
    ld   h,0
    ret
  __endasm;
}

/* Matrix: line 0 UP RIGHT DOWN, line 1 LEFT COPY, line 5 bit 7 SPACE,
 * line 7 E(2) W(3) S(4) D(5), line 8 A(5), line 9 the joystick: up down
 * left right fire2 fire1; line 6 the second stick (keys 6 5 R T G F). */
uint8_t plat_keys_a(void) {               /* cursor keys and SPACE (or COPY) */
  uint8_t k = 0, r = cpc_line(0), l1 = cpc_line(1);
  if (!(r & 0x01)) k |= KEY_UP;
  if (!(r & 0x04)) k |= KEY_DOWN;
  if (!(r & 0x02)) k |= KEY_RIGHT;
  if (!(l1 & 0x01)) k |= KEY_LEFT;
  if (kbd_alt_fire ? !(l1 & 0x02) : !(cpc_line(5) & 0x80)) k |= KEY_SPACE;
  return k;
}

uint8_t plat_keys_b(void) {               /* W A S D and E */
  uint8_t k = 0, r = cpc_line(7);
  if (!(r & 0x08)) k |= KEY_UP;
  if (!(r & 0x10)) k |= KEY_DOWN;
  if (!(r & 0x20)) k |= KEY_RIGHT;
  if (!(r & 0x04)) k |= KEY_SPACE;
  if (!(cpc_line(8) & 0x20)) k |= KEY_LEFT;
  return k;
}

uint8_t plat_joy(uint8_t n) {             /* both sticks: up, down, left, right, either fire */
  uint8_t k = 0, r;
  if (joy_type == JOY_NONE) return 0;
  r = cpc_line(n ? 6 : 9);
  if (!(r & 0x01)) k |= KEY_UP;
  if (!(r & 0x02)) k |= KEY_DOWN;
  if (!(r & 0x04)) k |= KEY_LEFT;
  if (!(r & 0x08)) k |= KEY_RIGHT;
  if ((r & 0x30) != 0x30) k |= KEY_SPACE;
  return k;
}

/* text entry: letters, DEL = delete, ESC = cancel */
uint8_t plat_key_char(void) {
  static const char letters[6][8] = {               /* lines 3 to 8, bits 0..7 */
    {0, 0, 0, 'P', 0, 0, 0, 0}, {0, 0, 'O', 'I', 'L', 'K', 'M', 0}, {0, 0, 'U', 'Y', 'H', 'J', 'N', 0},
    {0, 0, 'R', 'T', 'G', 'F', 'B', 'V'}, {0, 0, 'E', 'W', 'S', 'D', 'C', 'X'}, {0, 0, 0, 'Q', 0, 'A', 0, 'Z'},
  };
  uint8_t line, bit, r;
  if (!(cpc_line(8) & 0x04)) return 0x1b;
  if (!(cpc_line(9) & 0x80)) return 8;
  for (line = 0; line < 6; line++) {
    r = cpc_line(line + 3);
    for (bit = 0; bit < 8; bit++)
      if (!(r & (1 << bit)) && letters[line][bit]) return (uint8_t)letters[line][bit];
  }
  return 0;
}

/* ---- time ---- */
static uint8_t last_tick;

void plat_frame_sync(void) {               /* a game frame is three TV frames: 18 interrupts */
  while ((uint8_t)(cpc_ticks - last_tick) < 18) ;
  last_tick = cpc_ticks;
}

void plat_delay(void) {                    /* one TV frame */
  uint8_t t = cpc_ticks;
  while ((uint8_t)(cpc_ticks - t) < 6) ;
}

/* ---- sound: the MZ's tones on the AY's channel A ----
 * MZ: frequency = 1108400 / ratio, the game waits len * 3340 T-states of
 * 3.55 MHz (len * 0.94 ms) and stands still meanwhile, as here.
 * AY: period = 1 MHz / (16 * frequency) = ratio * 0.0564. */
static uint8_t ay_reg, ay_val;

static void ay_write(void) __naked {
  __asm
    ld   a,(_ay_reg)
    ld   b,0xf4
    out  (c),a
    ld   bc,0xf6c0          ; select the register
    out  (c),c
    ld   bc,0xf600
    out  (c),c
    ld   a,(_ay_val)
    ld   b,0xf4
    out  (c),a
    ld   bc,0xf680          ; write it
    out  (c),c
    ld   bc,0xf600
    out  (c),c
    ret
  __endasm;
}

static void ay(uint8_t reg, uint8_t val) { ay_reg = reg; ay_val = val; ay_write(); }

void plat_tone(uint16_t ratio, uint8_t len) {
  uint16_t period = (ratio >> 4) - (ratio >> 7) + (ratio >> 9);
  uint8_t t, wait = (uint8_t)(((uint16_t)len * 9) >> 5);
  if (!wait) wait = 1;
  ay(0, (uint8_t)period);
  ay(1, (uint8_t)(period >> 8) & 0x0f);
  ay(8, 13);
  t = cpc_ticks;
  while ((uint8_t)(cpc_ticks - t) < wait) ;
  ay(8, 0);
}

/* ---- the machine taken over ---- */
static void isr(void) __naked {
  __asm
    push af
    ld   a,(_cpc_ticks)
    inc  a
    ld   (_cpc_ticks),a
    pop  af
    ei
    ret
  __endasm;
}

static void takeover(void) __naked {
  __asm
    di
    ld   hl,0x0038          ; IM 1: our counter (both ROMs paged out below)
    ld   (hl),0xc3
    inc  hl
    ld   de,_isr
    ld   (hl),e
    inc  hl
    ld   (hl),d
    im   1
    ld   bc,0x7f8d          ; gate array: Mode 1, lower and upper ROM off
    out  (c),c
    ld   hl,_cpc_palette
    ld   c,0                ; pens 0..3
tk_pal:
    out  (c),c
    ld   a,(hl)
    or   0x40
    out  (c),a
    inc  hl
    inc  c
    ld   a,c
    cp   4
    jr   nz,tk_pal
    ld   c,0x10             ; border: black
    out  (c),c
    ld   a,0x54
    out  (c),a
    ld   bc,0xbc0c          ; CRTC: screen at C000h, no offset
    out  (c),c
    ld   bc,0xbd30
    out  (c),c
    ld   bc,0xbc0d
    out  (c),c
    ld   bc,0xbd00
    out  (c),c
    ei
    ret
  __endasm;
}

void plat_init(void) {
  takeover();
  ay(7, 0x3e);                            /* mixer: tone A only */
  ay(8, 0);
}

/* ---- screen ---- */

/* cpc_cell: draw cell (cd_x, cd_y) showing logical code cd_code */
static void cpc_cell(void) __naked {
  __asm
    push ix
    ld   a,(_cd_y)          ; screen address: row table + 2 * column
    add  a,a
    ld   l,a
    ld   h,0
    ld   de,cc_rowtab
    add  hl,de
    ld   e,(hl)
    inc  hl
    ld   d,(hl)
    ld   a,(_cd_x)
    add  a,a
    add  a,e
    ld   e,a
    ld   a,d
    adc  a,0
    ld   d,a                ; DE = screen
    ld   a,(_cpc_scheme_ovr)
    cp   0xff
    jr   nz,cc_live
    ld   hl,_cpc_game_idx   ; fast: a cell rendered in advance, copied
    ld   a,(_title_mode)
    or   a
    jr   z,cc_t
    ld   hl,_cpc_title_idx
cc_t:
    ld   a,(_cd_code)
    ld   c,a
    ld   b,0
    add  hl,bc
    ld   l,(hl)             ; cell number -> 16 bytes
    ld   h,b
    add  hl,hl
    add  hl,hl
    add  hl,hl
    add  hl,hl
    ld   bc,_cpc_cells
    add  hl,bc
    REPT 8
    ldi                     ; left byte
    ld   a,(hl)             ; right byte
    ld   (de),a
    inc  hl
    dec  e                  ; back to the left byte (a cell starts at an even address)
    ld   a,d                ; next pixel line: +800h
    add  a,8
    ld   d,a
    ENDR
    pop  ix
    ret

cc_live:                    ; a player's colours over the table's: glyph and lookup
    ld   c,a                ; scheme
    ld   hl,_cpc_game_tab
    ld   a,(_cd_code)
    push bc
    ld   c,a
    ld   b,0
    add  hl,bc
    add  hl,bc
    pop  bc
    ld   b,(hl)             ; glyph
cc_s:
    ld   l,c                ; lookup rows of the scheme: lut + 32 * scheme, odd lines + 16
    ld   h,0
    add  hl,hl
    add  hl,hl
    add  hl,hl
    add  hl,hl
    add  hl,hl
    ld   a,b
    ld   bc,_cpc_lut
    add  hl,bc
    ld   (cc_r0),hl
    ld   bc,16
    add  hl,bc
    ld   (cc_r1),hl
    ld   l,a                ; glyph: 8 bytes
    ld   h,0
    add  hl,hl
    add  hl,hl
    add  hl,hl
    ld   bc,_cpc_glyphs
    add  hl,bc              ; HL = glyph
    ld   b,4
cc_pair:
    push bc
    ld   bc,(cc_r0)
    call cc_one
    ld   bc,(cc_r1)
    call cc_one
    pop  bc
    djnz cc_pair
    pop  ix
    ret

; one pixel line: HL = glyph line, DE = screen, BC = lookup row; HL + 1, DE + 800h
cc_one:
    ld   a,(hl)
    inc  hl
    push hl
    push af
    rrca
    rrca
    rrca
    rrca
    and  15                 ; left four pixels
    ld   l,a
    ld   h,0
    add  hl,bc
    ld   a,(hl)
    ld   (de),a
    inc  de
    pop  af
    and  15                 ; right four pixels
    ld   l,a
    ld   h,0
    add  hl,bc
    ld   a,(hl)
    ld   (de),a
    dec  de
    ld   a,d
    add  a,8
    ld   d,a
    pop  hl
    ret

cc_r0: defw 0
cc_r1: defw 0
cc_rowtab:
    defw 0xc000, 0xc050, 0xc0a0, 0xc0f0, 0xc140, 0xc190, 0xc1e0, 0xc230
    defw 0xc280, 0xc2d0, 0xc320, 0xc370, 0xc3c0, 0xc410, 0xc460, 0xc4b0
    defw 0xc500, 0xc550, 0xc5a0, 0xc5f0, 0xc640, 0xc690, 0xc6e0, 0xc730
    defw 0xc780
  __endasm;
}

/* after the flush: a cell in the player's colours (the shared death frames) */
void plat_player_colour(uint8_t x, uint8_t y, uint8_t player) {
  if (y >= SCREEN_H) return;
  cd_x = x; cd_y = y; cd_code = shadow_vram[row_off[y] + x];
  cpc_scheme_ovr = cpc_player_schemes[player];
  cpc_cell();
  cpc_scheme_ovr = 0xff;
}

/* flush_screen: draw_buf -> screen, changed cells only; clears draw_buf.
 * After clear_buffers the screen is wiped in one pass and only the cells
 * that are not blank are drawn. */
void flush_screen(void) __naked {
  __asm
    ld   a,(_screen_cleared)
    or   a
    jr   z,fs_scan
    xor  a
    ld   (_screen_cleared),a
    di
    ld   (fs_sp),sp
    ld   sp,0               ; C000h..FFFFh: pushed from the top down
    ld   hl,0
    ld   b,0
fs_wipe:
    push hl
    push hl
    push hl
    push hl
    push hl
    push hl
    push hl
    push hl
    push hl
    push hl
    push hl
    push hl
    push hl
    push hl
    push hl
    push hl
    push hl
    push hl
    push hl
    push hl
    push hl
    push hl
    push hl
    push hl
    push hl
    push hl
    push hl
    push hl
    push hl
    push hl
    push hl
    push hl
    djnz fs_wipe
    ld   sp,(fs_sp)
    ei
    ld   hl,_shadow_vram    ; the screen now shows blanks
    ld   de,_shadow_vram+1
    ld   bc,999
    ld   (hl),0x20
    ldir
fs_scan:                    ; 40 compares per row, unrolled: an unchanged cell costs 36 T-states
    ld   de,_draw_buf
    ld   hl,_shadow_vram
    xor  a
    ld   (_cd_y),a
fs_row:
    ld   (fs_rowst),hl
    REPT 40
    ld   a,(de)
    cp   (hl)
    call nz,fs_chg
    inc  hl
    inc  de
    ENDR
    ld   a,(_cd_y)
    inc  a
    ld   (_cd_y),a
    cp   25
    jp   nz,fs_row

    di                      ; the draw buffer starts every frame empty
    ld   (fs_sp),sp
    ld   sp,_draw_buf+1000
    ld   hl,0x2020
    ld   b,125
fs_clr:
    push hl
    push hl
    push hl
    push hl
    djnz fs_clr
    ld   sp,(fs_sp)
    ei
    ret

fs_chg:                     ; A = new code, HL = shadow, DE = draw buffer
    ld   (hl),a             ; shadow = new code
    ld   (_cd_code),a
    push hl
    push de
    ld   de,(fs_rowst)
    or   a
    sbc  hl,de
    ld   a,l
    ld   (_cd_x),a          ; column = offset in the row
    ld   a,(_cd_y)
    push af
    call _cpc_cell
    pop  af
    ld   (_cd_y),a
    pop  de
    pop  hl
    ret

fs_rowst: defw 0
fs_sp: defw 0
  __endasm;
}
