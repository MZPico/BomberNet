/*
 * ZX Spectrum 48K platform layer (z88dk sccz80, +zx target).
 *
 * Screen: a logical cell is 6 x 8 pixels, so the 40 x 24 field is 240 x 192
 * pixels, centred by 8 pixels (one attribute column) on each side. Four cells
 * make three bytes, so the screen is handled in groups of four cells: a group
 * is redrawn as a whole when one of its cells changed, which needs no masking.
 * The attribute squares (8 pixels) do not line up with the cells (6 pixels);
 * each of the three squares of a group takes the colour of one of the two
 * cells it covers: a figure before scenery, anything before a blank.
 *
 * The ROM interrupt routine stays in charge (IM 1): it counts the TV frames
 * the frame limiter needs. It uses IY, so nothing here touches IY.
 */
#include <stdint.h>
#include "game.h"
#include "tables.h"

/* menu texts */
/* Input sources. The two Sinclair sticks (Interface 2) are keys, so they
 * are always there: 6 7 8 9 0 is key set B, 1 2 3 4 5 is INPUT_JOY2. The
 * JOYSTICK row says what INPUT_JOY1 is: a Kempston or Fuller interface, or a
 * Cursor stick. A Cursor stick is keys 5 to 8 and 0, which both Sinclair
 * sets use as well, so it excludes them. */
const char *const plat_joy_names[PLAT_JOY_TYPES] = {"NONE    ", "KEMPSTON", "FULLER  ", "CURSOR  "};
const char *const plat_kbd_a_alt_name = "QAOP AND M      ";

const char *plat_input_name(uint8_t input) {
  switch (input) {
  case INPUT_KBD_A: return "QAOP AND SPACE  ";
  case INPUT_KBD_B: return "SINCLAIR 6 TO 0 ";
  case INPUT_JOY2:  return "SINCLAIR 1 TO 5 ";
  case INPUT_JOY1:
    return joy_type == JOY_KEMPSTON ? "KEMPSTON        " :
           joy_type == JOY_FULLER   ? "FULLER          " : "CURSOR 5 TO 8   ";
  }
  return "";
}

uint8_t plat_input_allowed(uint8_t input) {
  switch (input) {
  case INPUT_KBD_A: return 1;
  case INPUT_KBD_B:
  case INPUT_JOY2:  return joy_type != JOY_CURSOR;
  case INPUT_JOY1:  return joy_type != JOY_NONE;
  }
  return 0;
}

#define SCR_LEFT 1              /* first attribute column of the field (8 pixels in) */

/* attribute of each player's colour: green, yellow, white, blue (BRIGHT) */
static const uint8_t player_attrs[MAX_PLAYERS] = {0x44, 0x46, 0x47, 0x41};

/* The game never prints through the C library; the start-up code would link
 * its console driver anyway (1 KB), so it is redirected to nothing. */
void zx_no_console_stub(void) __naked {
  __asm
    PUBLIC zx_no_console
zx_no_console:
    ret
  __endasm;
}

/* ---- ports ---- */
uint8_t zx_bar_attr = 0x58;        /* status bar: black on the wall's colour, set in plat_init */
uint8_t zx_gcache[256];             /* rendered-group cache of flush_screen: 32 x (generation, 4 cells, address, spare) */
uint8_t zx_groups;                /* groups redrawn since it was last cleared (measurements) */

static uint8_t zx_in(uint16_t port) __z88dk_fastcall __naked {
  __asm
    ld   b,h
    ld   c,l
    in   a,(c)
    ld   l,a
    ld   h,0
    ret
  __endasm;
}

/* Keyboard half-rows, bit 0..4 (0 = pressed):
 *   FEFEh CAPS Z X C V      FDFEh A S D F G      FBFEh Q W E R T
 *   F7FEh 1 2 3 4 5         EFFEh 0 9 8 7 6      DFFEh P O I U Y
 *   BFFEh ENTER L K J H     7FFEh SPACE SYM M N B */
uint8_t plat_keys_a(void) {               /* Q up, A down, O left, P right, SPACE (or M) */
  uint8_t k = 0, r;
  if (!(zx_in(0xfbfe) & 0x01)) k |= KEY_UP;
  if (!(zx_in(0xfdfe) & 0x01)) k |= KEY_DOWN;
  r = zx_in(0xdffe);
  if (!(r & 0x02)) k |= KEY_LEFT;
  if (!(r & 0x01)) k |= KEY_RIGHT;
  r = zx_in(0x7ffe);
  if (!(r & (kbd_alt_fire ? 0x04 : 0x01))) k |= KEY_SPACE;
  return k;
}

static uint8_t sinclair(uint8_t r) {      /* bits: fire, up, down, right, left */
  uint8_t k = 0;
  if (!(r & 0x01)) k |= KEY_SPACE;
  if (!(r & 0x02)) k |= KEY_UP;
  if (!(r & 0x04)) k |= KEY_DOWN;
  if (!(r & 0x08)) k |= KEY_RIGHT;
  if (!(r & 0x10)) k |= KEY_LEFT;
  return k;
}

uint8_t plat_keys_b(void) {               /* 6 left, 7 right, 8 down, 9 up, 0 fire */
  return sinclair(zx_in(0xeffe));
}

uint8_t plat_joy(uint8_t n) {
  uint8_t r, k = 0;
  if (n) {                                /* Sinclair stick 2: 1 left, 2 right, 3 down, 4 up, 5 fire */
    if (joy_type == JOY_CURSOR) return 0;
    r = zx_in(0xf7fe);
    if (!(r & 0x01)) k |= KEY_LEFT;
    if (!(r & 0x02)) k |= KEY_RIGHT;
    if (!(r & 0x04)) k |= KEY_DOWN;
    if (!(r & 0x08)) k |= KEY_UP;
    if (!(r & 0x10)) k |= KEY_SPACE;
    return k;
  }
  switch (joy_type) {
  case JOY_KEMPSTON:                      /* port 1Fh, 1 = active: right, left, down, up, fire */
    r = zx_in(0x001f);
    /* no interface: the port floats. A real one keeps bits 5..7 low and
     * cannot report left with right or up with down. */
    if ((r & 0xe0) || (r & 0x03) == 0x03 || (r & 0x0c) == 0x0c) return 0;
    if (r & 0x01) k |= KEY_RIGHT;
    if (r & 0x02) k |= KEY_LEFT;
    if (r & 0x04) k |= KEY_DOWN;
    if (r & 0x08) k |= KEY_UP;
    if (r & 0x10) k |= KEY_SPACE;
    break;
  case JOY_FULLER:                        /* port 7Fh, 0 = active: up, down, left, right, bit 7 fire */
    r = zx_in(0x007f);
    if ((r & 0x03) == 0 || (r & 0x0c) == 0) return 0;      /* opposite directions: not a stick */
    if (!(r & 0x01)) k |= KEY_UP;
    if (!(r & 0x02)) k |= KEY_DOWN;
    if (!(r & 0x04)) k |= KEY_LEFT;
    if (!(r & 0x08)) k |= KEY_RIGHT;
    if (!(r & 0x80)) k |= KEY_SPACE;
    break;
  case JOY_CURSOR:                        /* 5 left, 6 down, 7 up, 8 right, 0 fire */
    if (!(zx_in(0xf7fe) & 0x10)) k |= KEY_LEFT;
    r = zx_in(0xeffe);
    if (!(r & 0x10)) k |= KEY_DOWN;
    if (!(r & 0x08)) k |= KEY_UP;
    if (!(r & 0x04)) k |= KEY_RIGHT;
    if (!(r & 0x01)) k |= KEY_SPACE;
    break;
  }
  return k;
}

/* text entry: letters, 0 = delete, CAPS SHIFT + SPACE (BREAK) = cancel */
uint8_t plat_key_char(void) {
  static const char rows[8][5] = {
    {0, 'Z', 'X', 'C', 'V'}, {'A', 'S', 'D', 'F', 'G'}, {'Q', 'W', 'E', 'R', 'T'}, {0, 0, 0, 0, 0},
    {8, 0, 0, 0, 0}, {'P', 'O', 'I', 'U', 'Y'}, {0, 'L', 'K', 'J', 'H'}, {0, 0, 'M', 'N', 'B'},
  };
  static const uint16_t ports[8] = {0xfefe, 0xfdfe, 0xfbfe, 0xf7fe, 0xeffe, 0xdffe, 0xbffe, 0x7ffe};
  uint8_t row, bit, r;
  if (!(zx_in(0xfefe) & 0x01) && !(zx_in(0x7ffe) & 0x01)) return 0x1b;
  for (row = 0; row < 8; row++) {
    r = zx_in(ports[row]);
    for (bit = 0; bit < 5; bit++)
      if (!(r & (1 << bit)) && rows[row][bit]) return (uint8_t)rows[row][bit];
  }
  return 0;
}

/* ---- time ---- */
#define FRAMES_LO ((volatile uint8_t *)0x5c78)     /* ROM frame counter, +1 every 20 ms */
static uint8_t last_tick;

void plat_frame_sync(void) {                       /* a game frame is three TV frames */
  while ((uint8_t)(*FRAMES_LO - last_tick) < 3) ;
  last_tick = *FRAMES_LO;
}

void plat_delay(void) {
  uint8_t t = *FRAMES_LO;
  while (*FRAMES_LO == t) ;
}

/* ---- sound: the MZ's tones on the beeper ----
 * The MZ starts its tone generator with a divider ("ratio") of the 8253 clock,
 * 1.1084 MHz on the MZ-800, waits in a loop of len * 256 DJNZ (len * 3340
 * T-states) and stops it. The game stands still meanwhile, there as here.
 *   frequency   = 1108400 / ratio
 *   half period = 3500000 / (2 * frequency) = ratio * 1.579 T-states
 *   toggles     = len * 3340 / half period  = len * 2115 / ratio
 * The wait loop below takes 26 T-states per count. */
static uint16_t tone_half, tone_count;

static void beep(void) __naked {
  __asm
    ld   hl,(_tone_count)
    ld   e,0                ; port value: border black, speaker bit 4
bp_loop:
    ld   a,e
    xor  0x10
    ld   e,a
    out  (0xfe),a
    ld   bc,(_tone_half)
bp_wait:
    dec  bc
    ld   a,b
    or   c
    jr   nz,bp_wait
    dec  hl
    ld   a,h
    or   l
    jr   nz,bp_loop
    xor  a
    out  (0xfe),a
    ret
  __endasm;
}

void plat_tone(uint16_t ratio, uint8_t len) {
  uint16_t half;
  if (ratio < 0x0100) ratio = 0x0100;
  half = (ratio >> 4) - (ratio >> 9);              /* ratio * 1.579 / 26 */
  tone_half = half > 3 ? half - 2 : 1;             /* less the T-states around the wait */
  tone_count = ((uint16_t)len * 529) / (ratio >> 2);
  if (tone_count < 2) tone_count = 2;
  beep();
}

/* ---- screen ---- */
static void zx_clear(void) __naked {
  __asm
    xor  a
    out  (0xfe),a           ; black border
    ld   hl,0x4000
    ld   de,0x4001
    ld   bc,0x17ff
    ld   (hl),0
    ldir                    ; pixels off
    ld   hl,0x5800
    ld   de,0x5801
    ld   bc,0x02ff
    ld   (hl),0x47          ; bright white on black
    ldir
    ret
  __endasm;
}

void plat_init(void) {
  zx_clear();
  /* paper = the ink of the wall with its brightness, ink black */
  zx_bar_attr = (uint8_t)((zx_game_tab[2 * C_WALL + 1] & 0x40) | ((zx_game_tab[2 * C_WALL + 1] & 0x07) << 3));
}

/* after the flush: the attribute squares under a cell in the player's colour
 * (the death frames are green in the table) */
void plat_player_colour(uint8_t x, uint8_t y, uint8_t player) {
  uint16_t px = (uint16_t)x * 6;
  uint8_t a0 = (uint8_t)(px >> 3), a1 = (uint8_t)((px + 5) >> 3);
  uint8_t *p;
  if (y >= PLAT_ROWS) return;
  p = (uint8_t *)0x5800 + (uint16_t)y * 32 + SCR_LEFT + a0;
  p[0] = player_attrs[player];
  if (a1 != a0) p[1] = player_attrs[player];
}

/* flush_screen: draw_buf -> screen, groups of four cells; clears draw_buf.
 * Only a group or two change per frame, so the scan is what counts: it keeps
 * its pointers in registers and works out screen addresses only for a group
 * that is redrawn. */
void flush_screen(void) __naked {
  __asm
    push ix
    ; the status line (row 24), when there is one, is written on the bottom
    ; wall (row 23): the row becomes a bar in the wall's colour with the text
    ; on it
    xor  a
    ld   (fz_bar),a
    ld   hl,_draw_buf+960
    ld   b,40
    ld   a,0x20
fz_any:
    cp   (hl)
    jr   nz,fz_status
    inc  hl
    djnz fz_any
    jr   fz_scan
fz_status:
    ld   a,1
    ld   (fz_bar),a
    ld   hl,_draw_buf+960
    ld   de,_draw_buf+920
    ld   bc,40
    ldir

fz_scan:
    ld   hl,_zx_game_tab
    ld   a,(_title_mode)
    or   a
    jr   z,fz_t
    ld   hl,_zx_title_tab
fz_t:
    ld   (fz_tab),hl

    ; After clear_buffers every cell is to be drawn. Most are blank: wipe the
    ; display in one pass instead, in the colour of a blank cell, and draw
    ; only what is not blank (the status row is always drawn: its bar colour).
    ld   a,(_screen_cleared)
    or   a
    jr   z,fz_scan2
    xor  a
    ld   (_screen_cleared),a
    ld   de,0x40                ; 2 * space: the attribute of a blank cell
    add  hl,de
    inc  hl
    ld   a,(hl)
    and  0x7f
    ld   (fz_blank),a
    di
    ld   (fz_sp),sp
    ld   sp,0x5800              ; pixels off, pushed from the top down
    ld   hl,0
    ld   b,0                    ; 256 x 12 pushes = 6144 bytes
fz_wipe:
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
    djnz fz_wipe
    ld   sp,0x5b00              ; attributes: 768 bytes
    ld   a,(fz_blank)
    ld   h,a
    ld   l,a
    ld   b,64
fz_wipa:
    push hl
    push hl
    push hl
    push hl
    push hl
    push hl
    djnz fz_wipa
    ld   sp,(fz_sp)
    ei
    ld   hl,_shadow_vram        ; the display now shows blanks
    ld   de,_shadow_vram+1
    ld   bc,919
    ld   (hl),0x20
    ldir                        ; rows 0..22; row 23 stays unknown, it is redrawn
fz_scan2:
    ld   hl,fz_gen              ; a new generation: the cache is empty
    inc  (hl)
    jr   nz,fz_gok
    inc  (hl)                   ; generation 0 is never used: clear on wrap-around
    ld   hl,_zx_gcache
    ld   de,_zx_gcache+1
    ld   bc,255
    ld   (hl),0
    ldir
fz_gok:
    ld   de,_draw_buf
    ld   hl,_shadow_vram
    ld   c,24               ; rows left
fz_row:
    ld   b,10               ; groups left
fz_grp:
    ld   a,(de)
    cp   (hl)
    jr   nz,fz_chg0
    inc  hl
    inc  de
    ld   a,(de)
    cp   (hl)
    jr   nz,fz_chg1
    inc  hl
    inc  de
    ld   a,(de)
    cp   (hl)
    jr   nz,fz_chg2
    inc  hl
    inc  de
    ld   a,(de)
    cp   (hl)
    jr   nz,fz_chg3
    inc  hl
    inc  de
fz_next:
    dec  b
    jp   nz,fz_grp
    dec  c
    jp   nz,fz_row

    di                      ; the draw buffer starts every frame empty:
    ld   (fz_sp),sp         ; filled through the stack pointer, 2 ms
    ld   sp,_draw_buf+1000
    ld   hl,0x2020
    ld   b,125
fz_clr:
    push hl
    push hl
    push hl
    push hl
    djnz fz_clr
    ld   sp,(fz_sp)
    ei
    pop  ix
    ret

fz_chg3:
    dec  hl
    dec  de
fz_chg2:
    dec  hl
    dec  de
fz_chg1:
    dec  hl
    dec  de
fz_chg0:                    ; HL = shadow, DE = draw buffer, at the start of the group
    push bc
    ld   (fz_src),de
    ld   (fz_shd),hl
    ld   a,10
    sub  b
    ld   b,a
    add  a,a
    add  a,b
    ld   (fz_g3),a          ; 3 * group: byte and attribute column in the row
    ld   a,24
    sub  c                  ; row
    ld   (fz_crow),a
    ld   l,a
    sub  23
    ld   a,0
    jr   nz,fz_not23
    inc  a
fz_not23:
    ld   (fz_row23),a
    ld   h,0
    add  hl,hl
    push hl
    ld   de,fz_rowtab
    add  hl,de
    ld   e,(hl)
    inc  hl
    ld   d,(hl)
    ld   a,(fz_g3)
    add  a,e
    ld   e,a
    ld   (fz_scr),de
    pop  hl                 ; row * 2
    add  hl,hl
    add  hl,hl
    add  hl,hl
    add  hl,hl              ; row * 32
    ld   a,(fz_g3)
    inc  a                  ; the field starts at attribute column 1
    add  a,l
    ld   l,a
    ld   a,h
    adc  a,0x58
    ld   h,a
    ld   (fz_attr),hl
    call fz_group
    ld   hl,(fz_shd)
    ld   de,(fz_src)
    ld   bc,4
    add  hl,bc
    ex   de,hl
    add  hl,bc
    ex   de,hl
    pop  bc
    jp   fz_next

; one group: four cells at (fz_src) -> shadow, 3 bytes x 8 lines, 3 attributes
fz_group:
    ld   hl,_zx_groups
    inc  (hl)
    ld   hl,(fz_src)
    ld   de,(fz_shd)
    ld   bc,4
    ldir
    ; A playfield has some 20 distinct groups of four cells and repeats them
    ; everywhere. Groups already drawn in this flush are cached by their four
    ; cells (32 entries: generation, 4 cells, screen address); a repeat is a
    ; copy of 24 bytes and 3 attributes. The status row is never cached: its
    ; colours differ from the same cells elsewhere.
    ld   a,(fz_crow)
    cp   23
    jp   z,fz_draw
    ld   hl,(fz_src)            ; hash: c0 ^ c1 ^ c2 rotated 5 ^ c3 rotated 1, 5 bits
    ld   a,(hl)                 ; (chosen on real playfields: fewest collisions)
    inc  hl
    xor  (hl)
    inc  hl
    ld   c,(hl)
    rrc  c
    rrc  c
    rrc  c
    xor  c
    inc  hl
    ld   c,(hl)
    rlc  c
    xor  c
    and  31
    ld   l,a
    ld   h,0
    add  hl,hl
    add  hl,hl
    add  hl,hl
    ld   de,_zx_gcache
    add  hl,de
    ld   (fz_ent),hl
    ld   a,(fz_gen)
    cp   (hl)
    jr   nz,fz_miss
    inc  hl
    ld   de,(fz_src)
    ld   b,4
fz_hcmp:
    ld   a,(de)
    cp   (hl)
    jr   nz,fz_miss
    inc  hl
    inc  de
    djnz fz_hcmp
    ld   e,(hl)                 ; hit: HL = entry + 5, the screen address of the first copy
    inc  hl
    ld   d,(hl)
    ex   de,hl                  ; HL = source, DE = destination
    ld   de,(fz_scr)
    push hl
    ld   a,8
fz_cpy:
    ldi
    ldi
    ldi
    dec  l
    dec  l
    dec  l
    inc  h
    dec  e
    dec  e
    dec  e
    inc  d
    dec  a
    jr   nz,fz_cpy
    pop  hl                     ; attributes: 58h + third, same low byte
    ld   a,h
    rrca
    rrca
    rrca
    and  3
    or   0x58
    ld   h,a
    ld   de,(fz_attr)
    ldi
    ldi
    ldi
    ret
fz_miss:                        ; remember where this one is drawn
    ld   hl,(fz_ent)
    ld   a,(fz_gen)
    ld   (hl),a
    inc  hl
    ex   de,hl
    ld   hl,(fz_src)
    ld   bc,4
    ldir
    ex   de,hl
    ld   de,(fz_scr)
    ld   (hl),e
    inc  hl
    ld   (hl),d
fz_draw:
    ld   hl,(fz_src)
    ld   de,fz_work
    ld   ix,fz_info
    ld   b,4
fz_res:
    push bc
    ld   a,(hl)
    inc  hl
    push hl
    ld   l,a
    ld   h,0
    add  hl,hl
    ld   bc,(fz_tab)
    add  hl,bc
    ld   a,(hl)             ; glyph number
    inc  hl
    ld   c,(hl)             ; attribute, bit 7 = figure
    ld   (ix+0),a
    ld   (ix+1),c
    inc  ix
    inc  ix
    ld   l,a
    ld   h,0
    add  hl,hl
    add  hl,hl
    add  hl,hl
    ld   bc,_zx_glyphs
    add  hl,bc
    ld   bc,8
    ldir
    pop  hl
    pop  bc
    djnz fz_res

    ld   hl,(fz_scr)
    ld   ix,fz_work
    ld   b,8
fz_line:
    ld   a,(ix+8)           ; byte 0 = cell 0, two pixels of cell 1
    rlca
    rlca
    and  0x03
    or   (ix+0)
    ld   (hl),a
    inc  l
    ld   a,(ix+16)          ; byte 1 = four pixels of cell 1, four of cell 2
    rrca
    rrca
    rrca
    rrca
    and  0x0f
    ld   c,a
    ld   a,(ix+8)
    add  a,a
    add  a,a
    and  0xf0
    or   c
    ld   (hl),a
    inc  l
    ld   a,(ix+24)          ; byte 2 = two pixels of cell 2, cell 3
    rrca
    rrca
    and  0x3f
    ld   c,a
    ld   a,(ix+16)
    rlca
    rlca
    rlca
    rlca
    and  0xc0
    or   c
    ld   (hl),a
    dec  l
    dec  l
    inc  h                  ; next pixel line of the character row
    inc  ix
    djnz fz_line

    ld   ix,fz_info         ; attributes: squares 0, 1, 2
    ld   hl,(fz_attr)
    ld   b,(ix+0)
    ld   c,(ix+1)
    ld   d,(ix+2)
    ld   e,(ix+3)
    call fz_pick            ; square 0: cell 0, else cell 1
    ld   (hl),a
    inc  hl
    ld   b,(ix+2)
    ld   c,(ix+3)
    ld   d,(ix+4)
    ld   e,(ix+5)
    call fz_pick            ; square 1: cell 1, else cell 2
    ld   (hl),a
    inc  hl
    ld   b,(ix+6)
    ld   c,(ix+7)
    ld   d,(ix+4)
    ld   e,(ix+5)
    call fz_pick            ; square 2: cell 3, else cell 2
    ld   (hl),a
    ld   a,(fz_bar)         ; the status bar: black text on the wall's colour,
    or   a                  ; a player's digit in the player's colour
    ret  z
    ld   a,(fz_row23)
    or   a
    ret  z
    ld   ix,fz_info
    ld   hl,(fz_attr)
    ld   c,(ix+1)           ; square 0: cells 0 and 1
    ld   e,(ix+3)
    call fz_bartab
    ld   (hl),a
    inc  hl
    ld   c,(ix+3)           ; square 1: cells 1 and 2
    ld   e,(ix+5)
    call fz_bartab
    ld   (hl),a
    inc  hl
    ld   c,(ix+7)           ; square 2: cells 3 and 2
    ld   e,(ix+5)
    call fz_bartab
    ld   (hl),a
    ret

; C, E = attributes of the two cells of a square -> A = attribute on the bar
fz_bartab:
    ld   a,c
    bit  7,a
    jr   nz,fz_bartab1
    ld   a,e
    bit  7,a
    jr   nz,fz_bartab1
    ld   a,(_zx_bar_attr)
    ret
fz_bartab1:                  ; the digit keeps its ink, on the bar's colour
    and  0x07
    ld   b,a
    ld   a,(_zx_bar_attr)
    and  0xf8
    or   b
    ret

; B,C = glyph and attribute of the main cell, D,E = of the other -> A = attribute
fz_pick:
    ld   a,b
    or   a
    jr   z,fz_other         ; main is blank
    ld   a,d
    or   a
    jr   z,fz_main          ; other is blank
    bit  7,e
    jr   z,fz_main          ; other is scenery
    bit  7,c
    jr   nz,fz_main         ; both figures
fz_other:
    ld   a,e
    and  0x7f
    ret
fz_main:
    ld   a,c
    and  0x7f
    ret

fz_tab:  defw 0
fz_src:  defw 0
fz_shd:  defw 0
fz_scr:  defw 0
fz_attr: defw 0
fz_sp:   defw 0
fz_blank: defb 0
fz_crow: defb 0
fz_gen:  defb 0
fz_ent:  defw 0
fz_g3:   defb 0
fz_bar:  defb 0
fz_row23: defb 0
fz_info: defs 8
fz_work: defs 32
fz_rowtab:                  ; address of pixel line 0, byte 1 of each character row
    defw 0x4001, 0x4021, 0x4041, 0x4061, 0x4081, 0x40a1, 0x40c1, 0x40e1
    defw 0x4801, 0x4821, 0x4841, 0x4861, 0x4881, 0x48a1, 0x48c1, 0x48e1
    defw 0x5001, 0x5021, 0x5041, 0x5061, 0x5081, 0x50a1, 0x50c1, 0x50e1
  __endasm;
}
