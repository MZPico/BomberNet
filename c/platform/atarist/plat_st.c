/* Atari ST: the platform layer (core/platform.h), docs/port-atari-st.md. A
 * TOS program (MiNTLib), run from the desktop or the AUTO folder; it runs
 * in supervisor mode, as ST games do, for the hardware registers.
 *
 * Screen: low resolution, 320 x 200 in 16 colours, 40 x 25 cells of 8 x 8.
 * The four bitplanes are interleaved word by word (a 16-pixel group is 4
 * words, one a plane), so a cell is a byte in each of 4 words two bytes
 * apart, and one MOVEP.L writes a row of it to all four planes (st_cell,
 * assembly): the glyph's byte four times over (st_rep), masked by the
 * colour's planes. The glyphs, colours and palette are the Amiga's tables
 * (the same 16-colour cells); the palette is converted to the ST's 3 bits
 * a gun (the STE's fourth bit in bit 3).
 *
 * Time: TOS's vertical-blank count (_frclock), a game frame being 3 TV
 * frames at 50 Hz, or 4,3,4,3,4 at 60 Hz. The test build times the frames
 * with the MFP's timer A (38400 Hz, 26 us). Keys, joysticks and the YM's
 * tones are step 1. */
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <mint/osbind.h>
#include "game.h"
#include "tables.h"

long _stksize = 65536;                    /* MiNTLib's startup gives the program this much stack */

#define FRCLOCK (*(volatile uint32_t *)0x466)          /* TV frames since the start */
#define SYNCMODE (*(volatile uint8_t *)0xffff820a)      /* bit 1: 50 Hz */
#define MFP_TADR (*(volatile uint8_t *)0xfffffa1f)      /* timer A's count */

static uint8_t *screen;
static long old_ssp;
static int16_t old_rez = -1;
static int16_t old_palette[16];
static uint32_t st_rep[256];              /* a glyph byte in all four bytes */
static uint32_t colour_mask[16];          /* a colour's planes: byte k (the most significant first) for plane k */

/* the MFP's timer A: an interrupt every 256 counts of 38400 Hz */
volatile uint32_t st_ta_ticks;
extern void st_timer_a(void);
__asm__("\n"
        "        .text\n"
        "        .globl  _st_timer_a\n"
        "_st_timer_a:\n"
        "        addq.l  #1,_st_ta_ticks\n"
        "        bclr    #5,0xfffffa0f\n"    /* the in-service bit: TOS runs the MFP with software end of interrupt */
        "        rte\n");
#define TIMER_HZ 38400u
#ifdef ST_TEST
static uint32_t st_time(void) {           /* in 1/38400 s */
  uint32_t t;
  uint8_t c;
  do { t = st_ta_ticks; c = MFP_TADR; } while (t != st_ta_ticks);
  return (t << 8) + (uint8_t)(0u - c);
}
#endif

#ifdef ST_TEST
#include "rec_data.h"                     /* a recording to replay (tools/build_st.py --test) */
#define TEST_FRAMES ((REC_LEN - 16) / 6)
static uint16_t rec_pos = 16, frames;
static uint32_t t_start, t_flush;
static struct {
  uint16_t bad, late, n;                  /* the first record whose hash differs (its offset), late frames, frames timed */
  uint32_t flush;                         /* timer ticks in the game flush, all frames */
  uint32_t work[TEST_FRAMES];             /* each game frame's work (flush .. next frame sync) */
  uint32_t fl[TEST_FRAMES];               /* of it the flush */
} test;
#endif

/* ---- menus and input (step 1) ---- */
const char *const plat_joy_names[PLAT_JOY_TYPES] = {"NONE    ", "ATARI   "};
const char *const plat_kbd_a_alt_name = "CURSOR AND RET  ";
const char *plat_input_name(uint8_t input) {
  switch (input) {
  case INPUT_KBD_A: return "CURSOR AND SPACE";
  case INPUT_KBD_B: return "WASD AND E      ";
  case INPUT_JOY1:  return "JOYSTICK PORT 1 ";
  case INPUT_JOY2:  return "JOYSTICK PORT 0 ";
  }
  return "";
}
uint8_t plat_input_allowed(uint8_t input) {
  if (input == INPUT_JOY1 || input == INPUT_JOY2) return joy_type != JOY_NONE;
  return 1;
}

uint8_t plat_keys_a(void) {
#ifdef ST_TEST
  return title_mode && (frames & 1) ? KEY_SPACE : 0;
#else
  return 0;
#endif
}
uint8_t plat_keys_b(void) { return 0; }
uint8_t plat_joy(uint8_t n) { (void)n; return 0; }
uint8_t plat_key_char(void) { return 0; }
uint8_t plat_break(void) { return 0; }

/* ---- time ---- */
static uint32_t last_tick;
static uint8_t hz60, hz60_step;

void plat_frame_sync(void) {                    /* 60 ms: 3 TV frames at 50 Hz, or 4,3,4,3,4 at 60 Hz (3.6) */
  uint8_t need = 3;
  if (hz60) { need = (hz60_step & 1) ? 3 : 4; if (++hz60_step == 5) hz60_step = 0; }
#ifdef ST_TEST
  if (t_start && test.n < TEST_FRAMES) {
    test.work[test.n++] = st_time() - t_start;
    if (FRCLOCK - last_tick > need) test.late++;
  }
  t_start = 0;
#endif
  while (FRCLOCK - last_tick < need) Vsync();
  last_tick = FRCLOCK;
}

void plat_delay(void) { Vsync(); }

void plat_tone(uint16_t ratio, uint8_t len) { (void)ratio; (void)len; }   /* the YM2149: step 1 */

/* ---- screen ---- */
static uint8_t mode = 0xff;

/* one cell: for each of its 8 rows the glyph's byte in all four bytes,
 * masked to the colour's planes, to the four planes' bytes 2 apart; the
 * rows 160 bytes apart */
extern void st_cell(uint8_t *dst, const uint8_t *glyph, uint32_t mask);
__asm__("\n"
        "        .text\n"
        "        .globl  _st_cell\n"
        "_st_cell:\n"                     /* 4(sp) dst, 8(sp) glyph, 12(sp) the colour's mask */
        "        move.l  a2,-(sp)\n"
        "        move.l  8(sp),a0\n"
        "        move.l  12(sp),a1\n"
        "        move.l  16(sp),d1\n"
        "        lea     _st_rep,a2\n"
        "        moveq   #0,d0\n"
        "        move.b  (a1)+,d0\n"
        "        lsl.w   #2,d0\n"
        "        move.l  0(a2,d0.w),d0\n"
        "        and.l   d1,d0\n"
        "        movep.l d0,0(a0)\n"
        "        moveq   #0,d0\n"
        "        move.b  (a1)+,d0\n"
        "        lsl.w   #2,d0\n"
        "        move.l  0(a2,d0.w),d0\n"
        "        and.l   d1,d0\n"
        "        movep.l d0,160(a0)\n"
        "        moveq   #0,d0\n"
        "        move.b  (a1)+,d0\n"
        "        lsl.w   #2,d0\n"
        "        move.l  0(a2,d0.w),d0\n"
        "        and.l   d1,d0\n"
        "        movep.l d0,320(a0)\n"
        "        moveq   #0,d0\n"
        "        move.b  (a1)+,d0\n"
        "        lsl.w   #2,d0\n"
        "        move.l  0(a2,d0.w),d0\n"
        "        and.l   d1,d0\n"
        "        movep.l d0,480(a0)\n"
        "        moveq   #0,d0\n"
        "        move.b  (a1)+,d0\n"
        "        lsl.w   #2,d0\n"
        "        move.l  0(a2,d0.w),d0\n"
        "        and.l   d1,d0\n"
        "        movep.l d0,640(a0)\n"
        "        moveq   #0,d0\n"
        "        move.b  (a1)+,d0\n"
        "        lsl.w   #2,d0\n"
        "        move.l  0(a2,d0.w),d0\n"
        "        and.l   d1,d0\n"
        "        movep.l d0,800(a0)\n"
        "        moveq   #0,d0\n"
        "        move.b  (a1)+,d0\n"
        "        lsl.w   #2,d0\n"
        "        move.l  0(a2,d0.w),d0\n"
        "        and.l   d1,d0\n"
        "        movep.l d0,960(a0)\n"
        "        moveq   #0,d0\n"
        "        move.b  (a1)+,d0\n"
        "        lsl.w   #2,d0\n"
        "        move.l  0(a2,d0.w),d0\n"
        "        and.l   d1,d0\n"
        "        movep.l d0,1120(a0)\n"
        "        move.l  (sp)+,a2\n"
        "        rts\n");

static void cell(uint8_t x, uint8_t y, uint8_t g, uint8_t col) {
  st_cell(screen + (uint16_t)y * 1280 + ((x >> 1) << 3) + (x & 1), amiga_glyphs + ((uint16_t)g << 3), colour_mask[col & 15]);
}

static void set_mode(uint8_t title) {           /* everything is drawn again */
  mode = title;
  memset(shadow_vram, 0xff, SCREEN_CELLS);
}

void flush_screen(void) {
  uint16_t i;
  uint8_t a;
#ifdef ST_TEST
  frames++;
  if (!title_mode && rec_pos + 6 <= REC_LEN) {   /* the hash checked */
    if (hash_period && (rec[rec_pos + 4] | (uint16_t)(rec[rec_pos + 5] << 8)) != state_hash && !test.bad)
      test.bad = rec_pos;
    memcpy(replay_keys, rec + rec_pos, 4); rec_pos += 6;
  }
  if (!title_mode) t_start = t_flush = st_time();
#endif
  if (mode != title_mode) set_mode(title_mode);
  if (title_mode) {
    for (i = 0; i < SCREEN_CELLS; i++) {
      a = draw_buf[i]; draw_buf[i] = C_SPACE;
      if (a == shadow_vram[i]) continue;
      shadow_vram[i] = a;
      cell((uint8_t)(i % SCREEN_W), (uint8_t)(i / SCREEN_W), amiga_title_glyph[a], amiga_title_colour[a]);
    }
  } else {
    /* rows 0-23, four cells a compare (every game frame begins with
     * composite_frame, which overwrites them: no clearing); then the
     * status line, cleared */
    const uint32_t *d4 = (const uint32_t *)draw_buf, *s4 = (const uint32_t *)shadow_vram;
    uint16_t w;
    for (w = 0; w < (HUD_ROW * SCREEN_W) / 4; w++) {
      if (d4[w] == s4[w]) continue;
      for (i = (uint16_t)(w * 4); i < (uint16_t)(w * 4 + 4); i++) {
        a = draw_buf[i];
        if (a == shadow_vram[i]) continue;
        shadow_vram[i] = a;
        cell((uint8_t)(i % SCREEN_W), (uint8_t)(i / SCREEN_W), amiga_game_glyph[a], amiga_game_colour[a]);
      }
    }
    for (i = HUD_ROW * SCREEN_W; i < SCREEN_CELLS; i++) {
      a = draw_buf[i]; draw_buf[i] = C_SPACE;
      if (a == shadow_vram[i]) continue;
      shadow_vram[i] = a;
      cell((uint8_t)(i - HUD_ROW * SCREEN_W), HUD_ROW, amiga_game_glyph[a], amiga_game_colour[a]);
    }
  }
#ifdef ST_TEST
  if (!title_mode) {
    if (rec_pos + 6 <= REC_LEN) { uint32_t d = st_time() - t_flush; test.flush += d; if (test.n < TEST_FRAMES) test.fl[test.n] = d; }
    else {                                      /* the recording replayed: the figures out, done */
      FILE *f = fopen(ST_RESULT, "w");
      if (f) {
        fprintf(f, "frames %u bad %u late %u hz %lu flush %lu hz60 %u\n", test.n, test.bad, test.late,
                (unsigned long)TIMER_HZ, (unsigned long)test.flush, hz60);
        for (i = 0; i < test.n; i++) fprintf(f, "%lu %lu\n", (unsigned long)test.work[i], (unsigned long)test.fl[i]);
        fclose(f);
      }
      exit(0);
    }
  }
#endif
}

void plat_player_colour(uint8_t x, uint8_t y, uint8_t player) {
  cell(x, y, amiga_game_glyph[shadow_vram[(uint16_t)y * SCREEN_W + x]], (uint8_t)(8 + player));
}

void plat_hud_colour(uint8_t x, uint8_t n, uint8_t player, uint8_t blank) {
  (void)blank;
  for (; n; n--, x++) cell(x, HUD_ROW, amiga_game_glyph[shadow_vram[HUD_ROW * SCREEN_W + x]], (uint8_t)(8 + player));
}

/* ---- start and end ---- */
static void cleanup(void) {
  *(volatile uint8_t *)0xfffffa19 = 0;          /* timer A stopped (its control register), its interrupt off */
  Jdisint(13);
  if (old_rez >= 0) Setscreen((void *)-1, (void *)-1, old_rez);
  Setpalette(old_palette);
  Vsync();
  if (old_ssp) Super(old_ssp);
}

void plat_init(void) {
  static int16_t palette[16];
  uint16_t i;
  int16_t rez = Getrez();
  if (rez == 2) { (void)Cconws("BomberNet needs a colour monitor (low resolution).\r\n"); exit(1); }
  old_ssp = Super(0L);
  atexit(cleanup);
  for (i = 0; i < 16; i++) old_palette[i] = Setcolor(i, -1);
  if (rez != 0) { old_rez = rez; Setscreen((void *)-1, (void *)-1, 0); }
  screen = (uint8_t *)Physbase();
  memset(screen, 0, 32000);
  for (i = 0; i < 16; i++) {                    /* the Amiga's 4 bits a gun: the top 3 in bits 0-2, the lowest in bit 3 (STE) */
    uint16_t c = amiga_palette[i], v = 0, s;
    for (s = 0; s < 12; s += 4) { uint16_t g = (c >> s) & 15; v |= (uint16_t)(((g >> 1) | ((g & 1) << 3)) << s); }
    palette[i] = (int16_t)v;
  }
  Setpalette(palette);
  for (i = 0; i < 256; i++) st_rep[i] = (uint32_t)i * 0x01010101u;
  for (i = 0; i < 16; i++) {
    uint8_t k;
    colour_mask[i] = 0;
    for (k = 0; k < 4; k++) if (i & (1 << k)) colour_mask[i] |= 0xffUL << (24 - 8 * k);
  }
  __asm__ volatile("dc.w 0xa00a" ::: "d0", "d1", "d2", "a0", "a1", "a2");   /* Line-A: the mouse pointer hidden */
  hz60 = !(SYNCMODE & 2);
  Xbtimer(0, 5, 0, st_timer_a);                 /* timer A: /64 (38400 Hz), 256 counts an interrupt */
  joy_type = 1;
  set_mode(1);
#ifdef ST_TEST
  match_seed = rec[4] | (uint16_t)(rec[5] << 8);
  menu_mode = rec[6]; menu_players = rec[7];
  memcpy(menu_inputs, rec + 8, 4);
#ifdef TEST_HASH_OFF
  hash_period = 0;
#else
  hash_period = rec[12];
#endif
  replay_active = 1;
#endif
  last_tick = FRCLOCK;
}
