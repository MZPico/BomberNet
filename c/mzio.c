/*
 * Sharp MZ-700 (and MZ-800 in MZ-700 mode) hardware layer, inline Z80 asm.
 * Same environment as the MZPico Manager (z88dk sccz80, +mz target).
 *
 *   mz_keys        direct 8255 keyboard matrix scan (several keys at once)
 *   mz_tone        monitor MSTA (0044h) / MSTP (0047h) with RATIO at 11A1h
 *   flush_screen   diff draw_buf against shadow_vram, translate changed cells
 *                  through game_table / title_table into VRAM D000h / D800h
 *   composite_map  copy non-space map_layer cells over draw_buf
 */
#include <stdint.h>
#include "game.h"
#include "data.h"

/* Keyboard matrix (from the 1Z-013 key table, bit 7 first per row):
 *   strobe F6h: \ ^ - SPACE 0 9 . ,          -> SPACE = bit 4
 *   strobe F7h: INST DEL UP DOWN RIGHT LEFT ? /  -> UP bit5 DOWN bit4 RIGHT bit3 LEFT bit2
 * Lines read 0 when pressed. */
uint8_t mz_keys(void) __naked {
  __asm
    ld   a,0xf7
    ld   (0xe000),a
    nop
    nop
    ld   a,(0xe001)
    ld   b,a
    nop
    ld   a,(0xe001)
    or   b
    cpl                     ; 1 = pressed
    and  0x3c
    rrca
    rrca                    ; UP->bit3 DOWN->bit2 RIGHT->bit1 LEFT->bit0
    ld   c,a
    xor  a
    bit  3,c
    jr   z,mk_1
    or   0x01               ; KEY_UP
mk_1:
    bit  2,c
    jr   z,mk_2
    or   0x02               ; KEY_DOWN
mk_2:
    bit  1,c
    jr   z,mk_3
    or   0x04               ; KEY_RIGHT
mk_3:
    bit  0,c
    jr   z,mk_4
    or   0x08               ; KEY_LEFT
mk_4:
    ld   c,a
    ld   a,0xf6
    ld   (0xe000),a
    nop
    nop
    ld   a,(0xe001)
    ld   b,a
    nop
    ld   a,(0xe001)
    or   b
    bit  4,a
    ld   a,c
    jr   nz,mk_5
    or   0x10               ; KEY_SPACE
mk_5:
    ld   l,a
    ld   h,0
    ret
  __endasm;
}

/* Keyboard set B: strobe F2h row: Q R S T U V W X (bit 7..0) -> W bit1 = up,
 * S bit5 = down; strobe F4h row: A B C D E F G H -> A bit7 = left, D bit4 =
 * right, E bit3 = fire. */
uint8_t mz_keys_b(void) __naked {
  __asm
    ld   a,0xf2
    ld   (0xe000),a
    nop
    nop
    ld   a,(0xe001)
    ld   b,a
    nop
    ld   a,(0xe001)
    or   b
    cpl
    ld   c,0
    bit  1,a
    jr   z,kb_1
    set  0,c                ; KEY_UP
kb_1:
    bit  5,a
    jr   z,kb_2
    set  1,c                ; KEY_DOWN
kb_2:
    ld   a,0xf4
    ld   (0xe000),a
    nop
    nop
    ld   a,(0xe001)
    ld   b,a
    nop
    ld   a,(0xe001)
    or   b
    cpl
    bit  4,a
    jr   z,kb_3
    set  2,c                ; KEY_RIGHT
kb_3:
    bit  7,a
    jr   z,kb_4
    set  3,c                ; KEY_LEFT
kb_4:
    bit  3,a
    jr   z,kb_5
    set  4,c                ; KEY_SPACE (fire)
kb_5:
    ld   l,c
    ld   h,0
    ret
  __endasm;
}

/* ---- joysticks ----
 * MZ-800 / MZ-1500: ports F0h (joy 1) and F1h (joy 2), active low:
 * bit0 up, bit1 down, bit2 left, bit3 right, bit4 fire. The lines are only
 * driven while 8255 port A bit 5 (joy 1) / bit 6 (joy 2) is low; keyboard
 * strobes leave them high, so lower the bit around the read. */
uint8_t mz_joy800(uint8_t n) __naked {
  __asm
    push iy
    ld   iy,4
    add  iy,sp
    ld   a,(iy+0)
    or   a
    jr   nz,j8_two
    ld   a,0xdf             ; PA bit5 low: joy 1
    ld   (0xe000),a
    nop
    nop
    in   a,(0xf0)
    jr   j8_got
j8_two:
    ld   a,0xbf             ; PA bit6 low: joy 2
    ld   (0xe000),a
    nop
    nop
    in   a,(0xf1)
j8_got:
    ld   b,a
    ld   a,0xff
    ld   (0xe000),a         ; lines back to idle
    ld   a,b
    cpl                     ; 1 = active
    ld   c,a
    xor  a
    bit  0,c
    jr   z,j8_1
    or   0x01               ; KEY_UP
j8_1:
    bit  1,c
    jr   z,j8_2
    or   0x02               ; KEY_DOWN
j8_2:
    bit  3,c
    jr   z,j8_3
    or   0x04               ; KEY_RIGHT
j8_3:
    bit  2,c
    jr   z,j8_4
    or   0x08               ; KEY_LEFT
j8_4:
    bit  4,c
    jr   z,j8_5
    or   0x10               ; KEY_SPACE (fire 1)
j8_5:
    ld   l,a
    ld   h,0
    pop  iy
    ret
  __endasm;
}

uint8_t mz_joy(uint8_t n) {
  if (joy_type == JOY_800) return mz_joy800(n);
  if (joy_type == JOY_1X03) return joy_state[n & 1];
  return 0;
}

/* VBLK is 8255 port C bit 7 (E002h): high during display, low in vblank. */
void mz_wait_vblank(void) __naked {
  __asm
wv_disp:
    ld   a,(0xe002)
    bit  7,a
    jr   z,wv_disp          ; still in vblank: wait for display
    ld   a,(0xe008)
    cpl
    ld   (j13_sw),a         ; MZ-1X03 switches (1 = pressed) while VBLK is high
wv_vbl:
    ld   a,(0xe002)
    bit  7,a
    jr   nz,wv_vbl          ; display: wait for the falling edge
    ret
  __endasm;
}

/* MZ-1X03 on E008h bits 1..4 (active low). Call right after the VBLK
 * falling edge: switches were sampled during display, then each axis holds
 * its bit low for 68 + 28*pos T-states (pos 0..255, 128 = centre). The bit
 * is sampled 64 times, about 105 T-states apart (full scale ~7200 T), so the
 * count of low samples is ~pos/3.75: < 22 = left/up, > 46 = right/down.
 * bit1 = X stick 1, bit2 = Y stick 1, bit3 = X stick 2, bit4 = Y stick 2;
 * in display the same bits are SW1/SW2 of stick 1 and SW1/SW2 of stick 2. */
void mz_joy1x03_measure(void) __naked {
  __asm
    push ix
    ld   a,(j13_sw)         ; switches sampled by the display-phase probe below
    ld   ixl,a
    ld   b,64
    ld   c,0                ; low count X1
    ld   d,0                ; Y1
    ld   e,0                ; X2
    ld   h,0                ; Y2
j13_loop:
    ld   a,(0xe008)
    bit  1,a
    jr   nz,j13_a
    inc  c
j13_a:
    bit  2,a
    jr   nz,j13_b
    inc  d
j13_b:
    bit  3,a
    jr   nz,j13_c
    inc  e
j13_c:
    bit  4,a
    jr   nz,j13_d
    inc  h
j13_d:
    djnz j13_loop
    ; stick 1
    ld   a,ixl
    and  0x02               ; SW1 stick 1 -> fire
    ld   l,0
    jr   z,j13_s1f
    ld   l,0x10
j13_s1f:
    ld   a,c
    cp   22
    jr   nc,j13_s1r
    set  3,l                ; KEY_LEFT
    jr   j13_s1y
j13_s1r:
    cp   47
    jr   c,j13_s1y
    set  2,l                ; KEY_RIGHT
j13_s1y:
    ld   a,d
    cp   22
    jr   nc,j13_s1d
    set  0,l                ; KEY_UP
    jr   j13_s1done
j13_s1d:
    cp   47
    jr   c,j13_s1done
    set  1,l                ; KEY_DOWN
j13_s1done:
    ld   a,l
    ld   (_joy_state),a
    ; stick 2
    ld   a,ixl
    and  0x08               ; SW1 stick 2 -> fire
    ld   l,0
    jr   z,j13_s2f
    ld   l,0x10
j13_s2f:
    ld   a,e
    cp   22
    jr   nc,j13_s2r
    set  3,l
    jr   j13_s2y
j13_s2r:
    cp   47
    jr   c,j13_s2y
    set  2,l
j13_s2y:
    ld   a,h
    cp   22
    jr   nc,j13_s2d
    set  0,l
    jr   j13_s2done
j13_s2d:
    cp   47
    jr   c,j13_s2done
    set  1,l
j13_s2done:
    ld   a,l
    ld   (_joy_state+1),a
    pop  ix
    ret
j13_sw:
    defb 0
  __endasm;
}

/* mz_tone(ratio, len): RATIO=ratio, MSTA, busy loop len*256, MSTP. */
void mz_tone(uint16_t ratio, uint8_t len) __naked {
  __asm
    push iy
    ld   iy,4
    add  iy,sp
    ld   l,(iy+2)
    ld   h,(iy+3)
    ld   (0x11a1),hl        ; RATIO
    ld   c,(iy+0)
    ld   b,0
    call 0x0044             ; MSTA
mt_loop:
    djnz mt_loop
    dec  c
    jr   nz,mt_loop
    call 0x0047             ; MSTP
    pop  iy
    ret
  __endasm;
}

void mz_delay(void) __naked {
  __asm
    ld   hl,0x5000
md_loop:
    dec  hl
    ld   a,h
    or   l
    jr   nz,md_loop
    ret
  __endasm;
}

/* ---- frame limiter on 8253 counter 1 (memory mapped at E005h/E007h) ----
 * Counter 1 is clocked at 15611 Hz. It is programmed as a free-running
 * 16-bit down counter (mode 2, reload 0 = 65536), so elapsed time is a plain
 * 16-bit difference and does not depend on the reload value or on the mode
 * the monitor left it in. (The monitor's clock, fed from this counter, is
 * disturbed; a game does not need it.) */
void mz_timer_init(void) __naked {
  __asm
    ld   a,0x74             ; counter 1, LSB+MSB, mode 2, binary
    ld   (0xe007),a
    xor  a
    ld   (0xe005),a
    ld   (0xe005),a         ; reload 0 = 65536
    ld   hl,0
    ld   (fsync_prev),hl
    ret
  __endasm;
}

/* mz_frame_sync(ticks): elapsed = (prev - now) & 0xffff (down counter); spin until >= ticks */
void mz_frame_sync(uint16_t ticks) __naked {
  __asm
    push iy
    ld   iy,4
    add  iy,sp
    ld   c,(iy+0)
    ld   b,(iy+1)
    ld   (fsync_ticks),bc
fsync_loop:
    ld   a,0x40             ; latch counter 1
    ld   (0xe007),a
    ld   a,(0xe005)
    ld   e,a
    ld   a,(0xe005)
    ld   d,a                ; DE = now
    ld   hl,(fsync_prev)
    or   a
    sbc  hl,de              ; HL = prev - now (mod 65536)
    ld   bc,(fsync_ticks)
    or   a
    sbc  hl,bc
    jr   c,fsync_loop       ; not yet
    ld   (fsync_prev),de
    pop  iy
    ret
fsync_prev:
    defw 0
fsync_ticks:
    defw 0
  __endasm;
}

/* flush_screen: for each of the 1000 cells
 *   a = draw_buf[i]; draw_buf[i] = ' ';
 *   if (a != shadow[i]) { shadow[i] = a; VRAM[i] = table[a].code; VRAM[i+800h] = table[a].attr; }
 * VRAM address = shadow address + fs_delta (constant), so the hot loop
 * touches only DE (draw), HL (shadow), A and B. Unrolled 8x (1000 = 125 * 8).
 */
void flush_screen(void) __naked {
  __asm
    push ix
    ld   hl,0xd000
    ld   de,_shadow_vram
    or   a
    sbc  hl,de
    ld   (fs_delta),hl
    ld   de,_draw_buf
    ld   hl,_shadow_vram
    ld   b,0x20             ; draw buffer cells are reset to space
    ld   c,125
fs_loop:
    ld   a,(de)
    cp   (hl)
    call nz,fs_put
    ld   a,b
    ld   (de),a
    inc  hl
    inc  de
    ld   a,(de)
    cp   (hl)
    call nz,fs_put
    ld   a,b
    ld   (de),a
    inc  hl
    inc  de
    ld   a,(de)
    cp   (hl)
    call nz,fs_put
    ld   a,b
    ld   (de),a
    inc  hl
    inc  de
    ld   a,(de)
    cp   (hl)
    call nz,fs_put
    ld   a,b
    ld   (de),a
    inc  hl
    inc  de
    ld   a,(de)
    cp   (hl)
    call nz,fs_put
    ld   a,b
    ld   (de),a
    inc  hl
    inc  de
    ld   a,(de)
    cp   (hl)
    call nz,fs_put
    ld   a,b
    ld   (de),a
    inc  hl
    inc  de
    ld   a,(de)
    cp   (hl)
    call nz,fs_put
    ld   a,b
    ld   (de),a
    inc  hl
    inc  de
    ld   a,(de)
    cp   (hl)
    call nz,fs_put
    ld   a,b
    ld   (de),a
    inc  hl
    inc  de
    dec  c
    jr   nz,fs_loop
    pop  ix
    ret

fs_put:                     ; A = logical code, HL = shadow cell (still old)
    ld   (hl),a
    push bc
    push de
    push hl
    ld   e,a
    ld   d,0
    ld   hl,_game_table
    ld   a,(_title_mode)
    or   a
    jr   z,fs_lookup
    ld   hl,_title_table
fs_lookup:
    add  hl,de
    add  hl,de
    push hl
    pop  ix                 ; IX -> (code, attr)
    pop  hl
    push hl
    ld   de,(fs_delta)
    add  hl,de              ; HL = VRAM char address
    ld   a,(ix+0)
    ld   (hl),a
    ld   a,(ix+1)
    ld   de,0x0800
    add  hl,de
    ld   (hl),a             ; attribute plane
    pop  hl
    pop  de
    pop  bc
    ret
fs_delta:
    defw 0
  __endasm;
}

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
