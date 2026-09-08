#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <input.h>
#include <string.h>
#include <ctype.h>

#include "console.h"


uint8_t vram_codes[256] = {
  0x00, 0x01, 0x02, 0x03, 0x04, 0x05, 0x06, 0x07, 0x08, 0x09, 0x0a, 0x0b, 0x0c, 0x0d, 0x0e, 0x0f,
  0x10, 0x11, 0x12, 0x13, 0x14, 0x15, 0x16, 0x17, 0x18, 0x19, 0x1a, 0x1b, 0x1c, 0x1d, 0x1e, 0x1f,
  0x00, 0x61, 0x62, 0x63, 0x64, 0x65, 0x66, 0x67, 0x68, 0x69, 0x6b, 0x6a, 0x2f, 0x2a, 0x2e, 0x2d,
  0x20, 0x21, 0x22, 0x23, 0x24, 0x25, 0x26, 0x27, 0x28, 0x29, 0x4f, 0x2c, 0x51, 0x2b, 0x57, 0x49,
  0x55, 0x01, 0x02, 0x03, 0x04, 0x05, 0x06, 0x07, 0x08, 0x09, 0x0a, 0x0b, 0x0c, 0x0d, 0x0e, 0x0f,
  0x10, 0x11, 0x12, 0x13, 0x14, 0x15, 0x16, 0x17, 0x18, 0x19, 0x1a, 0x52, 0x59, 0x54, 0xbe, 0x7c,
  0xa4, 0x81, 0x82, 0x83, 0x84, 0x85, 0x86, 0x87, 0x88, 0x89, 0x8a, 0x8b, 0x8c, 0x8d, 0x8e, 0x8f,
  0x90, 0x91, 0x92, 0x93, 0x94, 0x95, 0x96, 0x97, 0x98, 0x99, 0x9a, 0xac, 0x79, 0x40, 0xa5, 0x0f,
  0x80, 0x81, 0x82, 0x83, 0x84, 0x85, 0x86, 0x87, 0x88, 0x89, 0x8a, 0x8b, 0x8c, 0x8d, 0x8e, 0x8f,
  0x90, 0x91, 0x92, 0x93, 0x94, 0x95, 0x96, 0x97, 0x98, 0x99, 0x9a, 0x9b, 0x9c, 0x9d, 0x9e, 0x9f,
  0xa0, 0xa1, 0xa2, 0xa3, 0xa4, 0xa5, 0xa6, 0xa7, 0xa8, 0xa9, 0xaa, 0xab, 0xac, 0xad, 0xae, 0xaf,
  0xb0, 0xb1, 0xb2, 0xb3, 0xb4, 0xb5, 0xb6, 0xb7, 0xb8, 0xb9, 0xba, 0xbb, 0xbc, 0xbd, 0xbe, 0xbf,
  0xc0, 0xc1, 0xc2, 0xc3, 0xc4, 0xc5, 0xc6, 0xc7, 0xc8, 0xc9, 0xca, 0xcb, 0xcc, 0xcd, 0xce, 0xcf,
  0xd0, 0xd1, 0xd2, 0xd3, 0xd4, 0xd5, 0xd6, 0xd7, 0xd8, 0xd9, 0xda, 0xdb, 0xdc, 0xdd, 0xde, 0xdf,
  0xe0, 0xe1, 0xe2, 0xe3, 0xe4, 0xe5, 0xe6, 0xe7, 0xe8, 0xe9, 0xea, 0xeb, 0xec, 0xed, 0xee, 0xef,
  0xf0, 0xf1, 0xf2, 0xf3, 0xf4, 0xf5, 0xf6, 0xf7, 0xf8, 0xf9, 0x6a, 0xfb, 0x6c, 0x4d, 0x4e, 0xff,
};

uint8_t fk_latched;        // current debounced F-key (0 = none)
uint8_t fk_release_count;  // consecutive zero samples seen
uint8_t fk_press_lockout;  // ignore cross-talk for a few polls after press
uint8_t fk_autorepeat_trigger;
uint8_t fk_autorepeat_speed;
uint8_t fk_curr_key;


/* multiplication by 40
 * to be called from assembly code
 * inputs: 
 *    e - a number to multiply
 * outputs:
 *   hl - result
 * destroys:
 *   de
 *   hl
 *   f
 */
void _multiply40(void) __naked {
  __asm
    ld d, 0
    ld h, e
    sla e
    sla e
    sla e
    rl d
    sla e
    rl d
    sla e
    rl d

    ld l, h
    ld h, 0
    sla l
    sla l
    sla l

    add hl, de
    ret
  __endasm;
}

void put_str_xy(uint8_t x, uint8_t y, const char *s) __naked {
    __asm
        push iy
        ld iy, 4
        add iy, sp

        ld e, (iy+2)    ; line number
        call __multiply40
        ld e, (iy+4)
        ld d, 0xd0
        add hl, de
        ld de, hl

        ld l, (iy+0)
        ld h, (iy+1)
insert_loop_psx:
        ld a, (hl)
        or a
        jr z, done_psx
        push hl
        ld c, a
        ld hl, _vram_codes
        ld b, 0
        add hl, bc
        ld a, (hl)
        ld (de), a
        inc de
        pop hl
        inc hl
        jr insert_loop_psx
done_psx:
        pop iy
        ret
    __endasm;
}

void put_str_attr_xy(uint8_t x, uint8_t y, const char *s, uint8_t attr) __naked {
    __asm
        push iy
        ld iy, 4
        add iy, sp

        ld e, (iy+4)    ; line number
        call __multiply40
        ld e, (iy+6)
        ld d, 0xd0
        add hl, de
        ld de, hl

        ld l, (iy+2)
        ld h, (iy+3)
insert_loop_psa:
        ld a, (hl)
        or a
        jr z, done_psa

        push hl
        ld c, a
        ld hl, _vram_codes
        ld b, 0
        add hl, bc
        ld a, (hl)
        ld (de), a

        push de
        ld a, 8
        add a, d
        ld d, a
        ld a, (iy+0)
        ld (de), a
        pop de

        inc de
        pop hl
        inc hl
        jr insert_loop_psa
done_psa:
        pop iy
        ret
    __endasm;
}

void put_char_attr_xy(uint8_t x, uint8_t y, char c, uint8_t attr) __naked {
    __asm
        push iy
        ld iy, 4
        add iy, sp

        ld e, (iy+4)    ; y
        call __multiply40
        ld d, 0xD0
        ld e, (iy+6)    ; x
        add hl, de

        ld c, (iy+2)    ; char
        ld b, 0
        ld de, hl
        ld hl, _vram_codes
        add hl, bc
        ld a, (hl)
        ld hl, de

        ld (hl), a
        add hl, 0x0800
        ld a, (iy+0)    ; attr
        ld (hl), a

        pop iy
        ret
    __endasm;
}

void put_multi_char_xy(uint8_t x, uint8_t y, uint8_t c, uint8_t cnt) __naked {
    __asm
        push iy
        ld iy, 4
        add iy, sp

        ld e, (iy+4)    ; y
        call __multiply40
        ld e, (iy+6)    ; x
        ld d, 0xD0      ; d8000 - start of attribute vram
        add hl, de

        ld de, hl
        inc de          ; setup for ldir

        ld c, (iy+2)
        ld b, 0
        push hl
        ld hl, _vram_codes
        add hl, bc
        ld a, (hl)
        pop hl
        ld (hl), a      ; 1st attr
        ld c, (iy+0)    ; cnt
        dec c
        ld b, 0
        ldir            ; copy from the 1st attr on
        pop iy
        ret
    __endasm;
}

void put_multi_attr_xy(uint8_t x, uint8_t y, uint8_t attr, uint8_t cnt) __naked {
    __asm
        push iy
        ld iy, 4
        add iy, sp

        ld e, (iy+4)    ; y
        call __multiply40
        ld e, (iy+6)    ; x
        ld d, 0xD8      ; d8000 - start of attribute vram
        add hl, de

        ld c, (iy+0)    ; cnt
        ld a, c
        or a
        jr z, pmay_finish

        ld de, hl
        inc de          ; setup for ldir

        ld b, (iy+2)
        ld (hl), b      ; 1st attr
        dec c
        jr z, pmay_finish
        ld b, 0
        ldir            ; copy from the 1st attr on
pmay_finish:
        pop iy
        ret
    __endasm;
}

void clrscr(void) __naked {
    __asm
        ld hl, 0xd000
        ld de, hl
        inc de
        xor a
        ld (hl), a
        ld bc, 0x0800-1
        ldir
        ld a, 0x71
        ld (hl), a
        ld bc, 0x800-1
        ldir
        ret
    __endasm;
}

void scroll_down(uint8_t first, uint8_t number) __naked {
  __asm
    push iy
    ld iy, 4
    add iy, sp

    ;ld hl, 0xd000 + 22*40 -1
    ;ld de, 0xd000 + 23*40 -1
    ;ld bc, 20*40

    ; Load number into a, store in b
    ld a, (iy+0)
    ld b, a        ; b = number

    ; Load first into e
    ld e, (iy+2)

    ; Compute a = first + number - 1
    add a, e       ; a = first + number
    dec a          ; a = first + number - 1
    ld e, a
    call __multiply40
    ; hl = (first + number - 1) * 40
    ld de, 0xd000
    add hl, de     ; hl = source = 0xd000 + offset
    dec hl         ; hl = hl - 1

    ; de = destination = hl + 40
    push hl
    pop de
    ld bc, 40
    add hl, bc     ; hl += 40 → hl = dest + 1
    ex de, hl      ; de = dest = src + 40, hl = src

    ; Compute bc = number * 40
    push hl
    push de
    ld e, (iy+0)
    call __multiply40
    ; hl = number * 40
    ld b, h
    ld c, l
    pop de
    pop hl

scroll_down_loop:
    ld a, (hl)
    ld (de), a
    dec hl
    dec de
    dec bc
    ld a,b
    or c
    jr nz, scroll_down_loop

    pop iy
    ret
  __endasm;
}

void scroll_up(uint8_t first, uint8_t number) __naked {
  __asm
    push iy
    ld iy, 4
    add iy, sp

    ;ld hl, 0xd000 + 3*40
    ;ld de, 0xd000 + 2*40
    ;ld bc, 20*40

    ; Load number into a, store in b
    ld b, (iy+0)

    ; Load first into e
    ld e, (iy+2)

    dec e
    ; Compute de = 0xD000 + first * 40
    call __multiply40
    ld de, 0xd000
    add hl, de     ; hl = dest
    push hl
    pop de

    ; hl = de + 40 = src
    ld bc, 40
    add hl, bc     ; hl = source

    ; Compute bc = number * 40
    push hl
    push de
    ld e, (iy+0)
    call __multiply40
    ld b, h
    ld c, l
    pop de
    pop hl

scroll_up_loop:
    ld a, (hl)
    ld (de), a
    inc hl
    inc de
    dec bc
    ld a, b
    or c
    jr nz, scroll_up_loop

    pop iy
    ret
  __endasm;
}

void border(uint8_t color) __naked {
  __asm
    push iy
    ld iy, 4
    add iy, sp

    ld bc, 0x06cf
    ld a, (iy+0)
    out (c), a

    pop iy
    ret
  __endasm;
}

void beep(void) __naked {
  __asm
    jp 0x0577
  __endasm;
}

uint8_t scan_fkeys(void) {
  __asm
    ld   a,0xf9
    ld  (0xe000),a
    nop
    nop
    ld   a,(0xe001)
    ld   b,a
    nop
    ld   a,(0xe001)
    and  b
    cpl
    ld l, a
    ld h, 0
  __endasm;
}

// Tunables (in "polls")
#define FK_RELEASE_STABLE  10   // require this many consecutive zero reads to confirm release
#define FK_PRESS_LOCKOUT   10   // after first press, ignore cross-talk for this many polls

// Map F-key bitfield to 1..5 (highest bit wins)
uint8_t map_fmask(uint8_t m) {
  if      (m & 0x80) return 1;
  else if (m & 0x40) return 2;
  else if (m & 0x20) return 3;
  else if (m & 0x10) return 4;
  else if (m & 0x08) return 5;
  else               return 0;
}

uint8_t inkey(void) {
  // --- F-key debouncer state (persists across calls) ---

  // --- read and debounce F-keys (eager press, delayed release) ---
  uint8_t fmask = scan_fkeys();
  uint8_t raw_fk = map_fmask(fmask);

  if (raw_fk) {
    if (fk_latched == 0) {
      // First detection: accept immediately
      fk_latched       = raw_fk;
      fk_release_count = 0;
      fk_press_lockout = FK_PRESS_LOCKOUT;
    } else {
      // Already holding a key: keep it (ignore flaps to other F-keys)
      fk_release_count = 0;
      if (fk_press_lockout) fk_press_lockout--;  // run down lockout
    }
  } else {
    // No F-key bits set: only release after consecutive zeros and after lockout
    if (fk_latched) {
      if (fk_release_count < FK_RELEASE_STABLE) fk_release_count++;
      if (fk_release_count >= FK_RELEASE_STABLE && fk_press_lockout == 0) {
        fk_latched       = 0;    // confirmed release
        fk_release_count = 0;
      }
    } else {
      fk_release_count = 0;
    }
    if (fk_press_lockout) fk_press_lockout--;    // run down lockout
  }

  // Debounced F-key result
  uint8_t c = fk_latched;

  // If no debounced F-key is held, fall back to normal keys
  if (!c)
    c = getk();  // MUST be non-blocking

  // --- your autorepeat logic unchanged ---
  if (c != 0 && fk_curr_key == c) {
    if (fk_autorepeat_trigger <= 50) {
      fk_autorepeat_trigger++;
      fk_autorepeat_speed = 0;
      return 0;
    } else {
      fk_autorepeat_speed++;
      if (fk_autorepeat_speed <= 5)
        return 0;
      else
        fk_autorepeat_speed = 0;
      // fallthrough to return c
    }
  } else {
    fk_autorepeat_trigger = 0;
    fk_autorepeat_speed   = 0;
  }

  fk_curr_key = c;
  return c;
}

void loading_screen(const char* name) {
  put_str_xy(16, 9, "LOADING");
  int ln = strlen(name);
  put_str_xy(20 - ln / 2, 11, name);
}

void get_uppercase_extension(const char* filename, char* extension) {
  const char* dot = strrchr(filename, '.');
  if (!dot || dot == filename) {
    extension[0] = '\0';
    return;
  }

  dot++;
  while (*dot) {
    *extension++ = toupper((unsigned char)*dot);
    dot++;
  }
  *extension = '\0';
}

void console_init(void) {
  fk_latched = 0;
  fk_release_count = 0;
  fk_press_lockout = 0;
  fk_autorepeat_trigger = 0;
  fk_autorepeat_speed = 0;
  fk_curr_key = 0;
}
