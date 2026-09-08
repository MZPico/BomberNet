/* Screen layers and cell helpers (portable C). */
#include <stdint.h>
#include "game.h"

uint8_t draw_buf[SCREEN_CELLS];
uint8_t map_layer[SCREEN_CELLS];
uint8_t shadow_vram[SCREEN_CELLS];
uint8_t title_mode;

const uint16_t row_off[SCREEN_H] = {
    0,  40,  80, 120, 160, 200, 240, 280, 320, 360, 400, 440, 480,
  520, 560, 600, 640, 680, 720, 760, 800, 840, 880, 920, 960,
};

#ifdef HOST
#include <assert.h>
uint8_t *draw_at(uint8_t x, uint8_t y) {
  assert(x < SCREEN_W && y < SCREEN_H);
  return draw_buf + row_off[y] + x;
}

uint8_t *map_at(uint8_t x, uint8_t y) {
  assert(x < SCREEN_W && y < SCREEN_H);
  return map_layer + row_off[y] + x;
}
#endif

/* 2x2 tile from a 16-wide tile sheet: code, code+1 / code+16, code+17 */
void put_tile(uint8_t *p, uint8_t code) {
  p[0] = code;
  p[1] = code + 1;
  p[SCREEN_W] = code + 16;
  p[SCREEN_W + 1] = code + 17;
}

void fill_2x2(uint8_t *p, uint8_t code) {
  p[0] = code;
  p[1] = code;
  p[SCREEN_W] = code;
  p[SCREEN_W + 1] = code;
}

uint8_t is_2x2_clear(const uint8_t *p) {
  if (p[0] != C_SPACE) return p[0];
  if (p[1] != C_SPACE) return p[1];
  if (p[SCREEN_W] != C_SPACE) return p[SCREEN_W];
  return p[SCREEN_W + 1];
}

void print_string(uint8_t *p, const char *s) {
  while (*s) *p++ = (uint8_t)*s++;
}

/* digits are logical codes 0..9; no division (sccz80 calls a slow helper) */
void print_num2(uint8_t *p, uint8_t v) {
  uint8_t q = 0;
  while (v >= 10) { v -= 10; q++; }
  p[0] = q;
  p[1] = v;
}

static const uint16_t pow10[4] = {10000, 1000, 100, 10};

void print_num5(uint8_t *p, uint16_t v) {
  uint8_t i;
  for (i = 0; i < 4; i++) {
    uint16_t d = pow10[i];
    uint8_t q = 0;
    while (v >= d) { v -= d; q++; }
    *p++ = q;
  }
  *p++ = (uint8_t)v;
  *p = 0;                       /* the original always shows a trailing 0 */
}

/* game-mode glyph for each letter A..Z (0 = not available) */
static const uint8_t hud_letters[26] = {
  0x95, 0x15, 0x11, 0x97, 0x14, 0x00, 0x31, 0x98, 0x99, 0x00, 0x8f, 0x93, 0x94,
  0x17, 0x12, 0x92, 0x00, 0x13, 0x10, 0x30, 0x18, 0x9e, 0x8e, 0x9f, 0x96, 0x00,
};

void hud_text(uint8_t *p, const char *s) {
  while (*s) {
    uint8_t c = (uint8_t)*s++;
    if (c >= 'A' && c <= 'Z') c = hud_letters[c - 'A'];
    else if (c >= '0' && c <= '9') c -= '0';
    else c = C_SPACE;
    *p++ = c;
  }
}

void print_num4(uint8_t *p, uint16_t v) {
  uint8_t i;
  if (v > 9999) v = 9999;
  for (i = 1; i < 4; i++) {
    uint16_t d = pow10[i];
    uint8_t q = 0;
    while (v >= d) { v -= d; q++; }
    *p++ = q;
  }
  *p++ = (uint8_t)v;
  *p = 0;
}

/* title mode: letters and space are direct, digits are codes 0..9, '-' is
 * 40h; anything else would hit the logo graphics, so it becomes a space */
void title_text(uint8_t *p, const char *s) {
  while (*s) {
    uint8_t c = (uint8_t)*s++;
    if (c >= '0' && c <= '9') c -= '0';
    else if (c == '-') c = 0x40;
    else if (!((c >= 'A' && c <= 'Z') || c == ' ')) c = C_SPACE;
    *p++ = c;
  }
}

/* yellow text in title mode: letters 60h-79h, digits 7Ah-7Fh / 25h-28h, dash 29h */
void title_text_hl(uint8_t *p, const char *s) {
  while (*s) {
    uint8_t c = (uint8_t)*s++;
    if (c >= 'A' && c <= 'Z') c = 0x60 + (c - 'A');
    else if (c >= '0' && c <= '5') c = 0x7a + (c - '0');
    else if (c >= '6' && c <= '9') c = 0x25 + (c - '6');
    else if (c == '-') c = 0x29;
    else c = C_SPACE;
    *p++ = c;
  }
}

void clear_map(void) {
  uint16_t i;
  for (i = 0; i < SCREEN_CELLS; i++) map_layer[i] = C_SPACE;
}

/* draw buffer to spaces, shadow to 0xff so the next flush repaints everything */
void clear_buffers(void) {
  uint16_t i;
  for (i = 0; i < SCREEN_CELLS; i++) {
    draw_buf[i] = C_SPACE;
    shadow_vram[i] = 0xff;
  }
}
