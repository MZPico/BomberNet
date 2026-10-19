/* Atari 800XL/130XE (64 KB): the platform layer (core/platform.h), screen
 * design D (docs/port-atari-xl.md). The OS stays on: its VBI keeps the
 * clock, the shadow registers and the keyboard.
 *
 * Screen: one 1000-byte screen memory, 40 x 25 screen codes.
 * - Game: a display list of 24 rows of ANTIC mode 4 (the field, 4 x 8
 *   pixels in four colours, tables.c: atari_field_font/_code) and one row
 *   of mode 2 (the status line, white text, atari_text_font/_code). A DLI
 *   at the end of row 22 moves the sprites to the status line (row 23, the
 *   wall, has no sprite data), one at the end of row 23 switches font and
 *   colours (atari.s); the deferred VBI moves the sprites back.
 * - Title and lobby: 25 rows of mode 2 with the title font.
 * Players: the four hardware sprites (single-line player/missile graphics,
 * 2 KB), in their colours; their shapes come from the codes the core drew
 * at their places (atari_sprite_shape), so their cells stay blank in the field.
 * On the status line the same sprites show each player's lives icon and
 * count in the player's colour (plat_hud_colour).
 * Input: two joysticks; the keyboard's arrow keys (- = + *, no CTRL needed)
 * with SHIFT as fire (the keyboard reports one key at a time, so SHIFT, a
 * line of its own, is the fire key that works while walking; SPACE fires
 * too, standing). Sound: POKEY voices 1-3,
 * tones that do not stop the game (as on the CPC and the 128K). */
#include <stdint.h>
#include <string.h>
#include "game.h"
#include "tables.h"

#define REG(a) (*(volatile uint8_t *)(a))
/* OS shadows */
#define RTCLOK  REG(0x14)                 /* +1 every vertical blank */
#define SDMCTL  REG(0x22f)
#define SDLSTL  (*(void *volatile *)0x230)
#define GPRIOR  REG(0x26f)
#define PCOLR0  REG(0x2c0)
#define COLOR0  REG(0x2c4)                /* COLOR0..COLOR4 at 2C4h-2C8h */
#define CHBAS   REG(0x2f4)
#define VDSLST  (*(void (*volatile *)(void))0x200)
/* hardware */
#define SIZEP0  REG(0xd008)
#define GRACTL  REG(0xd01d)
#define PALREG  REG(0xd014)
#define TRIG0   REG(0xd010)                /* TRIG1 one above */
#define PORTA   REG(0xd300)                /* the two sticks: 0 in bits 0-3, 1 in bits 4-7 */
#define AUDF1   REG(0xd200)               /* AUDFn at D200h + 2(n-1), AUDCn one above */
#define AUDCTL  REG(0xd208)
#define KBCODE  REG(0xd209)
#define IRQEN   REG(0xd20e)                /* write */
#define IRQST   REG(0xd20e)                /* read */
#define SKSTAT  REG(0xd20f)
#define PMBASE  REG(0xd407)
#define NMIEN   REG(0xd40e)

#define FIELD_TOP 24                      /* the first scanline of row 0 (two blank blocks of 8) */
#define HPOS(x) (uint8_t)(48 + (x) * 4)   /* a cell's left edge in colour clocks */

/* palette (hue << 4 | luminance), the MZ-700's colours as near as PAL has
 * them: field red, magenta, pale yellow, cyan; players green, strong
 * yellow, white, blue; and the MZ's eight inks (the title's icons) */
static const uint8_t field_colours[5] = {0x36, 0x4a, 0xee, 0x9a, 0x00};
static const uint8_t player_colours[4] = {0xba, 0xec, 0x0e, 0x78};
static const uint8_t ink_colours[8] = {0x00, 0x78, 0x36, 0x4a, 0xba, 0x9a, 0xec, 0x0e};

/* At fixed addresses at the top of RAM (the OS's screen is not needed),
 * so that their alignment wastes nothing; the C stack below them
 * (tools/build_atari.py places them and checks the program's end; atari.s
 * sets the stack): */
extern uint8_t atari_screen[SCREEN_CELLS];  /* $B400 */
extern uint8_t atari_pmg[2048];             /* $B800, sprites (single-line: 2 KB aligned); its
                                               first 0x300 bytes hold the network code's variables */
#define pmg atari_pmg
#define PLAYER_MEM(p) (pmg + 0x400 + ((uint16_t)(p) << 8))
#if defined(ATARI_TEST) || defined(NETTEST_TIME)
/* time in units of 2 scanlines (128 us; 60 ms = 469) */
uint16_t atari_now(void) { uint8_t t, v; do { t = RTCLOK; v = REG(0xd40b); } while (t != RTCLOK);
  return t * 156u + (v >= 124 ? v - 124 : v + 32); }   /* RTCLOK counts at VCOUNT 124 (the VBI) */
uint16_t atari_since(uint16_t t) { uint16_t n = atari_now(); return n >= t ? n - t : n + 39936u - t; }   /* RTCLOK wraps */
#define now atari_now
#define since atari_since
static void show3(uint16_t at, uint16_t n) {
  atari_screen[at] = atari_text_code[n / 100 % 10];
  atari_screen[at + 1] = atari_text_code[n / 10 % 10];
  atari_screen[at + 2] = atari_text_code[n % 10];
}
#endif
#ifdef ATARI_TEST
uint16_t test_bad;                        /* the first record whose hash differs (its offset) */
uint16_t test_late;                       /* game frames that took longer than 60 ms */
static uint16_t t_start, t_n, t_max;      /* frame work */
static uint32_t t_flush, t_work;
#endif
#ifdef NETTEST_TIME
extern uint16_t atari_sio_time, atari_sio_fails;   /* tcp_fujinet.c: SIO time since the last frame, failures */
static uint16_t nt_sum, nt_max, nt_n;
#endif
uint8_t atari_text_page, atari_title_page, atari_hpos[4], atari_hud_hpos[4];
void (*atari_dli_vec)(void);              /* the screen's first DLI (the VBI sets it) */
static uint8_t shown_y[4] = {0xff, 0xff, 0xff, 0xff}, hud_set, hud_shown, mode = 0xff;
static uint8_t hud_cells[4][2];           /* the codes each status-line sprite shows (0xff: none) */
static uint8_t icon_cells[5][4];          /* the codes each title icon sprite shows (0xff: none) */

/* display lists (they must not cross a 1 KB boundary) */
static uint8_t dl_game[3 + 3 + 23 + 3 + 3] ATARI_ALIGN(64), dl_title[3 + 3 + 24 + 3] ATARI_ALIGN(64);
void atari_dli(void);
void atari_dli_title(void);
void atari_vbi(void);
void atari_flush_game(void);

static void build_lists(void) {
  uint8_t *p = dl_game, i;
  uint16_t s = (uint16_t)atari_screen;
  *p++ = 0x70; *p++ = 0x70;                                   /* 16 blank scanlines */
  *p++ = 0x44; *p++ = (uint8_t)s; *p++ = (uint8_t)(s >> 8);   /* mode 4 + LMS: row 0 */
  for (i = 1; i < 22; i++) *p++ = 0x04;
  *p++ = 0x84; *p++ = 0x84;                                   /* rows 22 and 23, DLIs: the sprites, then the status line */
  s += 960;
  *p++ = 0x42; *p++ = (uint8_t)s; *p++ = (uint8_t)(s >> 8);   /* mode 2 + LMS: the status line */
  *p++ = 0x41; *p++ = (uint8_t)(uint16_t)dl_game; *p++ = (uint8_t)((uint16_t)dl_game >> 8);
  p = dl_title; s = (uint16_t)atari_screen;
  *p++ = 0x70; *p++ = 0x70;
  *p++ = 0x44; *p++ = (uint8_t)s; *p++ = (uint8_t)(s >> 8);   /* the logo: mode 4, in colour */
  for (i = 1; i < 5; i++) *p++ = 0x04;
  *p++ = 0x84;                                                /* row 5, DLI: text from here */
  for (i = 6; i < 25; i++) *p++ = 0x02;
  *p++ = 0x41; *p++ = (uint8_t)(uint16_t)dl_title; *p++ = (uint8_t)((uint16_t)dl_title >> 8);
}

/* switch between the game's screen and the title's (on the flush that changes title_mode) */
static void set_mode(uint8_t title) {
  uint8_t i;
  mode = title;
  memset(atari_screen, 0, SCREEN_CELLS);
  memset(shadow_vram, 0xff, SCREEN_CELLS);                    /* everything is drawn again */
  memset(pmg + 0x300, 0, 0x500);                              /* no sprites */
  for (i = 0; i < 4; i++) { shown_y[i] = 0xff; atari_hpos[i] = atari_hud_hpos[i] = 0; hud_cells[i][0] = 0xff; }
  hud_shown = 0;
  for (i = 0; i < 5; i++) icon_cells[i][0] = 0xff;
  if (title) {                                                /* the logo red and magenta, the rest white on black (DLI) */
    CHBAS = (uint8_t)((uint16_t)atari_logo_font >> 8);
    COLOR0 = field_colours[0]; REG(0x2c5) = field_colours[1]; REG(0x2c6) = 0x0e; REG(0x2c7) = 0; REG(0x2c8) = 0;
    GPRIOR = 0x11;                                            /* players over the playfield, the missiles a fifth player (PF3) */
    atari_dli_vec = atari_dli_title;
    SDLSTL = dl_title;
  } else {
    CHBAS = (uint8_t)((uint16_t)atari_field_font >> 8);
    for (i = 0; i < 5; i++) REG(0x2c4 + i) = field_colours[i];
    for (i = 0; i < 4; i++) REG(0x2c0 + i) = player_colours[i];
    for (i = 0; i < 4; i++) REG(0xd004 + i) = 0;              /* HPOSM: the missiles away */
    GPRIOR = 0x01;
    atari_dli_vec = atari_dli;
#ifdef ATARI_TEST
    if (test_bad) REG(0x2c8) = 0x26;
#endif
    SDLSTL = dl_game;
  }
}

/* ---- menus and input ---- */
const char *const plat_joy_names[PLAT_JOY_TYPES] = {"NONE    ", "ATARI   "};
const char *const plat_kbd_a_alt_name = "ARROWS AND SHIFT";

const char *plat_input_name(uint8_t input) {
  switch (input) {
  case INPUT_KBD_A: return "ARROWS AND SHIFT";
  case INPUT_JOY1:  return "JOYSTICK 1      ";
  case INPUT_JOY2:  return "JOYSTICK 2      ";
  }
  return "";
}

uint8_t plat_input_allowed(uint8_t input) {         /* one keyboard player: it reports one key at a time */
  if (input == INPUT_JOY1 || input == INPUT_JOY2) return joy_type != JOY_NONE;
  return input == INPUT_KBD_A;
}

#ifdef ATARI_TEST
#include "rec_data.h"                     /* a recording to replay (tools/atari_test.py) */
static uint16_t rec_pos = 16, frames;
#endif

/* The keyboard (POKEY) tracks one key at a time: while an arrow is held,
 * SPACE is not seen at all on a real machine (emulators let it replace the
 * arrow, and then report no key on its release). SHIFT has a line of its
 * own, so it is the fire key that works while walking; SPACE fires too,
 * standing. Fire is a press, not a level: one bomb each time (held, the
 * core would drop one every two cells), and a press counts only after
 * fire was up for two frames, and fire is down only after 2 vertical
 * blanks (40 ms) and not if it began as a new direction key came:
 * Altirra's keyboard mapping can drop SHIFT for a moment, or assert it
 * while one arrow gives way to another (atari.s), which made a bomb at a
 * turn. */
extern volatile uint8_t fire_tv, fire_bad;
#ifdef FIREDEBUG
static uint8_t fire_count;                /* fires so far, SHIFT's line, KBCODE: on the status line */
#endif
static uint8_t fire_up = 2, keys_frame = 0xff, keys_now, flushes;   /* flushes: +1 a frame (flush_screen) */
uint8_t plat_keys_a(void) {
  uint8_t k = 0, c, f;
#ifdef ATARI_TEST
  return title_mode && (frames & 1) ? KEY_SPACE : 0;
#endif
  if (keys_frame == flushes) return keys_now;         /* once a frame: every caller sees the same press */
  if (!(SKSTAT & 0x04)) {                             /* a key is down */
    c = KBCODE & 0x3f;
    if (c == 0x0e) k = KEY_UP;
    else if (c == 0x0f) k = KEY_DOWN;
    else if (c == 0x06) k = KEY_LEFT;
    else if (c == 0x07) k = KEY_RIGHT;
  }
  if (fire_tv >= 2 && !fire_bad) k |= KEY_SPACE;      /* SHIFT or SPACE, down for 2 vertical blanks, not
                                                         begun with a turn (atari.s) */
  f = k & KEY_SPACE;
  if (f) {
    if (fire_up < 2) k &= (uint8_t)~KEY_SPACE;        /* still the same press, or one up for only a frame */
    fire_up = 0;
  } else if (fire_up < 2) fire_up++;
#ifdef FIREDEBUG
  if (k & KEY_SPACE) fire_count++;                    /* shown over P1's score */
  atari_screen[963] = atari_text_code[fire_count % 10];
  atari_screen[964] = atari_text_code[(SKSTAT >> 3) & 1];
  atari_screen[965] = atari_text_code[(KBCODE & 0x3f) / 10]; atari_screen[966] = atari_text_code[(KBCODE & 0x3f) % 10];
#endif
  keys_frame = flushes; keys_now = k;
  return k;
}

uint8_t plat_keys_b(void) { return 0; }

uint8_t plat_joy(uint8_t n) {
  uint8_t s = n ? PORTA >> 4 : PORTA, k = 0;
  if (!(s & 1)) k |= KEY_UP;
  if (!(s & 2)) k |= KEY_DOWN;
  if (!(s & 4)) k |= KEY_LEFT;
  if (!(s & 8)) k |= KEY_RIGHT;
  if (!REG(0xd010 + n)) k |= KEY_SPACE;
  return k;
}

/* the OS's key buffer: letters for the room code, DELETE, ESC */
static const char keyletter[64] = {
  'L', 'J', 0, 0, 0, 'K', 0, 0, 'O', 0, 'P', 'U', 0, 'I', 0, 0,
  'V', 0, 'C', 0, 0, 'B', 'X', 'Z', 0, 0, 0, 0, 0, 0, 0, 0,
  0, 0, 0, 'N', 0, 'M', 0, 0, 'R', 0, 'E', 'Y', 0, 'T', 'W', 'Q',
  0, 0, 0, 0, 0, 0, 0, 0, 'F', 'H', 'D', 0, 0, 'G', 'S', 'A'};
/* POKEY latches a key press and BREAK as interrupt requests (IRQST bits 6
 * and 7 low); interrupts stay masked, so they are polled and acknowledged
 * here (atari_irqen: IRQEN's bits outside SIO, sio.s) */
extern uint8_t atari_irqen;
static uint8_t irq_taken(uint8_t bit) {
  if (IRQST & bit) return 0;
  IRQEN = (uint8_t)(atari_irqen & ~bit);
  IRQEN = atari_irqen;
  return 1;
}

uint8_t plat_key_char(void) {
  uint8_t c;
  if (!irq_taken(0x40)) return 0;
  c = KBCODE & 0x3f;
  if (c == 0x34) return 8;                            /* BACK SPACE */
  if (c == 0x1c) return 0x1b;                         /* ESC */
  return (uint8_t)keyletter[c];
}

uint8_t plat_break(void) { return irq_taken(0x80); }

/* ---- time ---- */
static uint8_t last_tick, ntsc, ntsc_step;
static void tones_due(void);

void plat_frame_sync(void) {                          /* 60 ms: 3 PAL frames, or 4,3,4,3,4 NTSC ones (3.6) */
  uint8_t need = 3;
  if (ntsc) { need = (ntsc_step & 1) ? 3 : 4; if (++ntsc_step == 5) ntsc_step = 0; }
#ifdef ATARI_TEST
  if (t_start && rec_pos + 6 <= REC_LEN && (uint8_t)(RTCLOK - last_tick) > need) test_late++;
  if (t_start && rec_pos + 6 <= REC_LEN) {
    uint16_t w = since(t_start);
    t_work += w; t_n++;
    if (w > t_max) t_max = w;
  }
  t_start = 0;
#endif
  while ((uint8_t)(RTCLOK - last_tick) < need) tones_due();
  tones_due();
  last_tick = RTCLOK;
}

void plat_delay(void) {
  uint8_t t = RTCLOK;
  while (RTCLOK == t) tones_due();
}

/* ---- sound: the MZ's tones on POKEY, the game running on ----
 * frequency = 1108400 / ratio; POKEY (64 kHz clock): f = 63921 / (2 (AUDF + 1)),
 * so AUDF = ratio * 0.02883 - 1. Voices 1 and 2 (3 and 4 are the serial
 * port's clock, sio.s): 14 (steps, menus) voice 1, the rest voice 2. */
static uint8_t tone_on, tone_end[2];
static void tones_due(void) {
  uint8_t v;
  if (!tone_on) return;
  for (v = 0; v < 2; v++)
    if ((tone_on & (1 << v)) && (int8_t)(RTCLOK - tone_end[v]) >= 0) {
      REG(0xd201 + 2 * v) = 0;
      tone_on &= (uint8_t)~(1 << v);
    }
}

void plat_tone(uint16_t ratio, uint8_t len) {
  uint8_t v = len == 14 ? 0 : 1;
  uint16_t f = (ratio >> 5) - (ratio >> 9) - (ratio >> 11);    /* ratio * 0.0288 */
  REG(0xd200 + 2 * v) = (uint8_t)(f ? f - 1 : 0);
  REG(0xd201 + 2 * v) = 0xa8;                                  /* pure tone, volume 8 */
  tone_end[v] = (uint8_t)(RTCLOK + 1 + (((uint16_t)len * 3) >> 6));
  tone_on |= (uint8_t)(1 << v);
}

/* ---- screen ---- */
#define SPRITE(c) (atari_sprite_shape + (atari_sprite_index[c] << 3))
static uint8_t shown_cells[4][4];                     /* the codes each sprite shows now */
static void sprites_field(void) {                     /* each player's shape where the core drew it */
  uint8_t p, r, *m, *sc, c0, c1, c2, c3;
  const uint8_t *a, *b;
  for (p = 0; p < MAX_PLAYERS; p++) {
    player_t *pl = &players[p];
    uint8_t *cell;
    m = PLAYER_MEM(p) + FIELD_TOP;
    if (!pl->active || pl->life_lost || pl->y >= HUD_ROW - 1) {
      if (shown_y[p] != 0xff) { memset(m + (shown_y[p] << 3), 0, 16); shown_y[p] = 0xff; }
      continue;
    }
    cell = draw_at(pl->x, pl->y);
    c0 = cell[0]; c1 = cell[1]; c2 = cell[SCREEN_W]; c3 = cell[SCREEN_W + 1];
    sc = shown_cells[p];
    atari_hpos[p] = HPOS(pl->x);
    if (shown_y[p] == pl->y && sc[0] == c0 && sc[1] == c1 && sc[2] == c2 && sc[3] == c3) continue;
    if (shown_y[p] != 0xff) memset(m + (shown_y[p] << 3), 0, 16);
    sc[0] = c0; sc[1] = c1; sc[2] = c2; sc[3] = c3;
    shown_y[p] = pl->y;
    m += pl->y << 3;
    a = SPRITE(c0); b = SPRITE(c1);
    for (r = 0; r < 8; r++) m[r] = (uint8_t)(a[r] << 4 | b[r]);
    a = SPRITE(c2); b = SPRITE(c3);
    for (r = 0; r < 8; r++) m[8 + r] = (uint8_t)(a[r] << 4 | b[r]);
  }
}

/* two text cells (8 x 8 hi-res pixels each) as one sprite row of 8: two pixels become one */
static void halve(uint8_t *m, const uint8_t *a, const uint8_t *b) {
  uint8_t r, k, l, h, bit;
  for (r = 0; r < 8; r++) {
    h = 0; bit = 0x80;
    for (k = 0, l = a[r]; k < 4; k++, l <<= 2, bit >>= 1) if (l & 0xc0) h |= bit;
    for (k = 0, l = b[r]; k < 4; k++, l <<= 2, bit >>= 1) if (l & 0xc0) h |= bit;
    m[r] = h;
  }
}

/* The title's legend (rows 7-8): the player and the four enemies in their
 * MZ colours, by the four sprites and the missiles as a fifth (PF3), their
 * shapes converted as in the game (narrow dots); the text cells under them
 * are left blank. */
#define ICON_ROW 7
#define ICON(c) (atari_icon_shape + (atari_icon_index[c] << 3))
static const uint8_t icon_x[5] = {2, 0x0b, 0x13, 0x1a, 0x22};
static const uint8_t icon_col[SCREEN_W] = {      /* the columns of icon_x and the one after */
  0, 0, 1, 1, 0, 0, 0, 0, 0, 0, 0, 1, 1, 0, 0, 0, 0, 0, 0, 1, 1, 0, 0, 0, 0, 0, 1, 1, 0, 0,
  0, 0, 0, 0, 1, 1, 0, 0, 0, 0};
static void title_icons(void) {
  uint8_t k, r, c[4], *hc, shape[16];
  const uint8_t *a, *b;
  for (k = 0; k < 5; k++) {
    uint16_t at = ICON_ROW * SCREEN_W + icon_x[k];
    const uint8_t *cell = shadow_vram + at;
    uint8_t col;
    c[0] = cell[0]; c[1] = cell[1]; c[2] = cell[SCREEN_W]; c[3] = cell[SCREEN_W + 1];
    hc = icon_cells[k];
    if (hc[0] == c[0] && hc[1] == c[1] && hc[2] == c[2] && hc[3] == c[3]) continue;
    memcpy(hc, c, 4);
    a = ICON(c[0]); b = ICON(c[1]);                   /* converted as the game's cells: tables.c */
    for (r = 0; r < 8; r++) shape[r] = (uint8_t)(a[r] << 4 | b[r]);
    a = ICON(c[2]); b = ICON(c[3]);
    for (r = 0; r < 8; r++) shape[8 + r] = (uint8_t)(a[r] << 4 | b[r]);
    col = ink_colours[atari_title_ink[c[0] != C_SPACE ? c[0] : c[2]]];
    if (k < 4) {
      memcpy(PLAYER_MEM(k) + FIELD_TOP + ICON_ROW * 8, shape, 16);
      atari_hpos[k] = HPOS(icon_x[k]);
      REG(0x2c0 + k) = col;
    } else {                                          /* missiles 0-3 side by side, two pixels each */
      uint8_t *m = pmg + 0x300 + FIELD_TOP + ICON_ROW * 8;
      for (r = 0; r < 16; r++) {
        uint8_t b = shape[r];
        m[r] = (uint8_t)((b >> 6) | ((b >> 2) & 0x0c) | ((b << 2) & 0x30) | (b << 6));
      }
      for (r = 0; r < 4; r++) REG(0xd004 + r) = (uint8_t)(HPOS(icon_x[k]) + 2 * r);
      REG(0x2c7) = col;                               /* COLOR3: the fifth player */
    }
  }
}

void flush_screen(void) {
  uint16_t i;
  flushes++;
#ifdef ATARI_TEST
  frames++;
  if (!title_mode && rec_pos + 6 <= REC_LEN) {     /* the hash checked: a mismatch turns the border red */
    if (hash_period && (rec[rec_pos + 4] | (uint16_t)(rec[rec_pos + 5] << 8)) != state_hash && !test_bad) test_bad = rec_pos;
    memcpy(replay_keys, rec + rec_pos, 4); rec_pos += 6;
  }
#endif
  if (mode != title_mode) set_mode(title_mode);
  if (title_mode) {
    for (i = 0; i < SCREEN_CELLS; i++) {
      uint8_t a = draw_buf[i];
      draw_buf[i] = C_SPACE;
      if (a == shadow_vram[i]) continue;
      shadow_vram[i] = a;
      if ((uint16_t)(i - ICON_ROW * SCREEN_W) < 2 * SCREEN_W && icon_col[(uint8_t)(i - ICON_ROW * SCREEN_W) % SCREEN_W])
        continue;                                     /* a legend icon's cell: its sprite draws it (no white flash) */
      atari_screen[i] = i < 240 ? atari_logo_code[a] : atari_title_code[a];
    }
    title_icons();
#ifdef NETTEST_HOST
    { extern uint8_t sio_last; atari_screen[960] = atari_title_code['A' + (sio_last >> 4)];   /* nibbles as A-P */
      atari_screen[961] = atari_title_code['A' + (sio_last & 15)]; }
#endif
    return;
  }
#ifdef ATARI_TEST
  t_start = now();
#endif
  sprites_field();
  atari_flush_game();
#ifdef NETTEST_TIME
  if (atari_sio_time > nt_max) nt_max = atari_sio_time;   /* SIO time a frame: average, longest of 16 */
  nt_sum += atari_sio_time; atari_sio_time = 0;
  if (++nt_n == 16) {
    show3(960 + 3, nt_sum >> 4); show3(960 + 16, nt_max);   /* over the scores: average, longest */
    show3(960 + 6, atari_sio_fails);
    { extern uint8_t atari_sio_failcmd, atari_sio_failr;   /* the last failure */
      show3(960 + 19, atari_sio_failcmd); show3(960 + 22, atari_sio_failr); }
    nt_sum = nt_max = nt_n = 0;
  }
#endif
  if (hud_shown && !hud_set) {                        /* no lives shown this frame: away with them */
    for (i = 0; i < 4; i++) { memset(PLAYER_MEM(i) + FIELD_TOP + 192, 0, 8); atari_hud_hpos[i] = 0; hud_cells[i][0] = 0xff; }
    hud_shown = 0;
  }
  hud_set = 0;
#ifdef ATARI_TEST
  if (rec_pos + 6 <= REC_LEN) t_flush += since(t_start);
  if (test_bad) REG(0x2c8) = 0x26;                    /* red: a hash differed */
  else if (rec_pos + 6 > REC_LEN)                     /* the whole recording replayed identically: */
  {
    REG(0x2c8) = test_late ? 0xe8 : 0xb4;             /* green, or yellow when frames were late */
    memset(atari_screen + 960 + 20, 0, 20);           /* late frames, average work, its flush, the longest */
    memset(atari_hud_hpos, 0, 4);
    show3(960 + 24, test_late); show3(960 + 28, (uint16_t)(t_work / t_n));
    show3(960 + 32, (uint16_t)(t_flush / t_n)); show3(960 + 36, t_max);
  }
#endif
}

void plat_player_colour(uint8_t x, uint8_t y, uint8_t player) { (void)x; (void)y; (void)player; }   /* sprites have it */

/* after the flush: a player's lives icon and count (two cells) by the player's sprite, in its colour */
void plat_hud_colour(uint8_t x, uint8_t n, uint8_t player, uint8_t blank) {
  uint8_t c0 = shadow_vram[960 + x], c1 = shadow_vram[960 + x + 1], *hc = hud_cells[player];
  (void)n; (void)blank;
#ifdef ATARI_TEST
  if (rec_pos + 6 > REC_LEN) return;                  /* the figures are on the status line now */
#endif
  atari_hud_hpos[player] = HPOS(x);
  hud_set = hud_shown = 1;
  if (hc[0] == c0 && hc[1] == c1) return;
  hc[0] = c0; hc[1] = c1;
  halve(PLAYER_MEM(player) + FIELD_TOP + 192, atari_text_font + (atari_text_code[c0] << 3),
        atari_text_font + (atari_text_code[c1] << 3));
}

void plat_init(void) {
  uint8_t i;
  ntsc = (PALREG & 0x0e) != 0;
  build_lists();
  atari_text_page = (uint8_t)((uint16_t)atari_text_font >> 8);
  atari_title_page = (uint8_t)((uint16_t)atari_title_font >> 8);
  memset(pmg, 0, sizeof(pmg));                        /* with the network code's variables (hiram.ld) */
  memset((void *)0x400, 0, 0x300);                    /* the OS's pages 4-6: more of them */
  PMBASE = (uint8_t)((uint16_t)pmg >> 8);
  for (i = 0; i < 4; i++) { REG(0x2c0 + i) = player_colours[i]; REG(0xd008 + i) = 0; }
  GPRIOR = 0x01;                                      /* players over the playfield */
  GRACTL = 0x03;
  SDMCTL = 0x3e;                                      /* normal playfield, players and missiles, single-line */
  AUDCTL = 0;
  REG(0xd20f) = 3;                                    /* SKCTL: keyboard scanning on */
  NMIEN = 0;
  VDSLST = atari_dli;                                 /* the vertical blank: atari_nmi (atari.s) */
  NMIEN = 0xc0;
  joy_type = JOY_ATARI;
  irq_taken(0x40); irq_taken(0x80);                   /* no key or BREAK from before */
#ifdef NETTEST_HOST
  menu_net = NET_HOST; menu_players = 2;              /* test: SPACE hosts a 2-player room */
#endif
#if defined(NETTEST_HOST) || defined(NET_WAIT_FUJINET)
  { uint8_t t = RTCLOK; while ((uint8_t)(RTCLOK - t) < 150) ; }   /* FujiNet-PC restarts on the emulator's reset */
#endif
  set_mode(1);
#ifdef ATARI_TEST
  match_seed = rec[4] | (uint16_t)(rec[5] << 8);
  menu_mode = rec[6]; menu_players = rec[7];
  memcpy(menu_inputs, rec + 8, 4);
#ifdef TEST_HASH_OFF
  hash_period = 0;                                    /* a standalone game: no hash, nothing checked */
#else
  hash_period = rec[12];
#endif
  replay_active = 1;
#endif
  last_tick = RTCLOK;
}
