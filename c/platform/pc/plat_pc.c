/* IBM PC (DOS): the platform layer (core/platform.h), docs/port-pc.md. A
 * real-mode DOS program (OpenWatcom, small model) for any PC with EGA or
 * VGA.
 *
 * Screen: mode 0Dh, 320 x 200 in 16 colours, 40 x 25 cells of 8 x 8; the
 * four bitplanes in parallel at A000:0000, 40 bytes a line. A cell is
 * drawn by the EGA's own logic: Set/Reset holds the colour for all four
 * planes, the latches hold zeros (loaded by reading a byte past the
 * screen that is never written), and each of the glyph's 8 rows is one
 * Bit Mask write and one memory write: the colour where the row has its
 * bits, black elsewhere. The glyphs, colours and palette are the Amiga's
 * tables (the same 16-colour cells). A VGA shows the palette as it is
 * (its DAC); an EGA in a 200-line mode has only the CGA's 16 colours,
 * chosen for each of the game's by hand (ega_colours).
 *
 * Time: the timer chip's channel 0 at about 1 kHz (the BIOS's own 18.2 Hz
 * tick still called on time), a game frame being 60 of its ticks; finer
 * times from its counter (1.19 MHz). Keys, joysticks and the speaker are
 * step 1. */
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <conio.h>
#include <dos.h>
#include <i86.h>
#include "game.h"
#include "tables.h"

#define PIT_HZ 1193182UL
#define TICK_DIV 1193u                    /* the timer at 1000.15 Hz */
#define GC_INDEX 0x3ce                    /* the EGA's graphics controller */
#define GC_DATA 0x3cf
#define SEQ_INDEX 0x3c4
#define SEQ_DATA 0x3c5

static volatile uint8_t far *vram;
static uint8_t col_map[16];               /* the game's colour -> the colour register drawn with (the nearest CGA colour on an EGA) */
static uint8_t old_mode;
static void (__interrupt __far *old_int8)(void);
static volatile uint32_t ticks;           /* timer interrupts since the start, ~1 ms each */
static volatile uint16_t bios_acc;        /* the BIOS's tick owed: 65536 counts of the timer's input */
static uint8_t started;

static void __interrupt __far timer_isr(void) {
  ticks++;
  bios_acc += TICK_DIV;
  if (bios_acc < TICK_DIV) _chain_intr(old_int8);   /* carried past 65536: the BIOS's 18.2 Hz tick is due (it ends the interrupt) */
  outp(0x20, 0x20);
}

#ifdef PC_TEST
static uint32_t pc_time(void) {           /* in the timer's counts (1.19 MHz, 0.84 us) */
  uint32_t t;
  uint16_t c;
  _disable();
  outp(0x43, 0x00);                       /* channel 0's count latched */
  c = inp(0x40); c |= (uint16_t)inp(0x40) << 8;
  t = ticks;
  outp(0x20, 0x0a);                       /* the interrupt controller's request register: */
  if ((inp(0x20) & 1) && c > TICK_DIV / 2) t++;   /* the count wrapped, its interrupt not taken yet */
  _enable();
  return t * TICK_DIV + (TICK_DIV - c);
}
#endif

#ifdef PC_TEST
#include "rec_data.h"                     /* a recording to replay (tools/build_pc.py --test) */
#define TEST_FRAMES ((REC_LEN - 16) / 6)
static uint16_t rec_pos = 16, frames;
static uint32_t t_start, t_flush;
static struct {
  uint16_t bad, late, n;                  /* the first record whose hash differs (its offset), late frames, frames timed */
  uint32_t flush;                         /* timer counts in the game flush, all frames */
  uint32_t work[TEST_FRAMES];             /* each game frame's work (flush .. next frame sync) */
  uint32_t fl[TEST_FRAMES];               /* of it the flush */
} __far test;                             /* far, as the recording: 24 KB for the longest */
#endif

/* ---- menus and input (step 1) ---- */
const char *const plat_joy_names[PLAT_JOY_TYPES] = {"NONE    ", "GAMEPORT"};
const char *const plat_kbd_a_alt_name = "CURSOR AND RET  ";
const char *plat_input_name(uint8_t input) {
  switch (input) {
  case INPUT_KBD_A: return "CURSOR AND SPACE";
  case INPUT_KBD_B: return "WASD AND E      ";
  case INPUT_JOY1:  return "JOYSTICK A      ";
  case INPUT_JOY2:  return "JOYSTICK B      ";
  }
  return "";
}
uint8_t plat_input_allowed(uint8_t input) {
  if (input == INPUT_JOY1 || input == INPUT_JOY2) return joy_type != JOY_NONE;
  return 1;
}

uint8_t plat_keys_a(void) {
#ifdef PC_TEST
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
static uint32_t next_frame;

void plat_frame_sync(void) {                    /* 60 ms */
#ifdef PC_TEST
  if (t_start && test.n < TEST_FRAMES) {
    test.work[test.n++] = pc_time() - t_start;
    if ((int32_t)(ticks - next_frame) > 0) test.late++;
  }
  t_start = 0;
#endif
  while ((int32_t)(ticks - next_frame) < 0) ;
  next_frame += FRAME_MS;
  if ((int32_t)(ticks - next_frame) > FRAME_MS) next_frame = ticks;   /* far behind (a pause): no rush to catch up */
}

void plat_delay(void) {                         /* a TV frame's worth: 20 ms */
  uint32_t t = ticks;
  while (ticks - t < 20) ;
}

void plat_tone(uint16_t ratio, uint8_t len) { (void)ratio; (void)len; }   /* the speaker: step 1 */

/* ---- screen ---- */
static uint8_t mode = 0xff;
static uint8_t cur_colour = 0xff;

static void cell(uint8_t x, uint8_t y, uint8_t g, uint8_t col) {
  volatile uint8_t far *d = vram + (uint16_t)y * 320 + x;
  const uint8_t *s = amiga_glyphs + ((uint16_t)g << 3);
  uint8_t l;
  col = col_map[col & 15];
  if (col != cur_colour) {                      /* Set/Reset: the colour, then Bit Mask selected again */
    outp(GC_INDEX, 0); outp(GC_DATA, col);
    outp(GC_INDEX, 8);
    cur_colour = col;
  }
  for (l = 0; l < 8; l++, d += 40) { outp(GC_DATA, *s++); *d = 0; }
}

static void set_mode(uint8_t title) {           /* everything is drawn again */
  mode = title;
  memset(shadow_vram, 0xff, SCREEN_CELLS);
}

void flush_screen(void) {
  uint16_t i;
  uint8_t a;
#ifdef PC_TEST
  frames++;
  if (!title_mode && rec_pos + 6 <= REC_LEN) {   /* the hash checked */
    if (hash_period && (rec[rec_pos + 4] | (uint16_t)(rec[rec_pos + 5] << 8)) != state_hash && !test.bad)
      test.bad = rec_pos;
    _fmemcpy(replay_keys, rec + rec_pos, 4); rec_pos += 6;   /* rec is far: the near memcpy would read DS:offset */
  }
  if (!title_mode) t_start = t_flush = pc_time();
#endif
  if (mode != title_mode) set_mode(title_mode);
  (void)vram[0xffff];                           /* the latches: zeros (that byte is never written) */
  cur_colour = 0xff;
  outp(GC_INDEX, 8);
  if (title_mode) {
    for (i = 0; i < SCREEN_CELLS; i++) {
      a = draw_buf[i]; draw_buf[i] = C_SPACE;
      if (a == shadow_vram[i]) continue;
      shadow_vram[i] = a;
      cell((uint8_t)(i % SCREEN_W), (uint8_t)(i / SCREEN_W), amiga_title_glyph[a], amiga_title_colour[a]);
    }
  } else {
    /* rows 0-23, two cells a compare (every game frame begins with
     * composite_frame, which overwrites them: no clearing); then the
     * status line, cleared */
    const uint16_t *d2 = (const uint16_t *)draw_buf, *s2 = (const uint16_t *)shadow_vram;
    uint16_t w;
    for (w = 0; w < (HUD_ROW * SCREEN_W) / 2; w++) {
      if (d2[w] == s2[w]) continue;
      for (i = (uint16_t)(w * 2); i < (uint16_t)(w * 2 + 2); i++) {
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
#ifdef PC_TEST
  if (!title_mode) {
    if (rec_pos + 6 <= REC_LEN) { uint32_t d = pc_time() - t_flush; test.flush += d; if (test.n < TEST_FRAMES) test.fl[test.n] = d; }
    else {                                      /* the recording replayed: the figures out, done */
      FILE *f = fopen(PC_RESULT, "w");
      if (f) {
        fprintf(f, "frames %u bad %u late %u hz %lu flush %lu\n", test.n, test.bad, test.late, (unsigned long)PIT_HZ,
                (unsigned long)test.flush);
        for (i = 0; i < test.n; i++) fprintf(f, "%lu %lu\n", (unsigned long)test.work[i], (unsigned long)test.fl[i]);
        fclose(f);
      }
      exit(0);
    }
  }
#endif
}

void plat_player_colour(uint8_t x, uint8_t y, uint8_t player) {
  (void)vram[0xffff];
  cur_colour = 0xff;
  outp(GC_INDEX, 8);
  cell(x, y, amiga_game_glyph[shadow_vram[(uint16_t)y * SCREEN_W + x]], (uint8_t)(8 + player));
}

void plat_hud_colour(uint8_t x, uint8_t n, uint8_t player, uint8_t blank) {
  (void)blank;
  (void)vram[0xffff];
  cur_colour = 0xff;
  outp(GC_INDEX, 8);
  for (; n; n--, x++) cell(x, HUD_ROW, amiga_game_glyph[shadow_vram[HUD_ROW * SCREEN_W + x]], (uint8_t)(8 + player));
}

/* ---- start and end ---- */
static void cleanup(void) {
  union REGS r;
  if (!started) return;
  _disable();
  outp(0x43, 0x36); outp(0x40, 0); outp(0x40, 0);   /* the timer back to 18.2 Hz */
  _dos_setvect(0x08, old_int8);
  _enable();
  outp(GC_INDEX, 1); outp(GC_DATA, 0);           /* Enable Set/Reset off, Bit Mask all: as the BIOS left them */
  outp(GC_INDEX, 8); outp(GC_DATA, 0xff);
  r.h.ah = 0; r.h.al = old_mode;                 /* the screen mode back */
  int86(0x10, &r, &r);
  started = 0;
}

/* an EGA in a 200-line mode has the CGA's 16 colours; the game's are
 * chosen by hand, not by nearness (which made players 3 and 4 both grey):
 * the field in the dark shades (the bricks dark red, the border dark
 * magenta), the players in bright ones of their own (green, brown for
 * orange, light blue, light red) */
static const uint8_t ega_colours[16] = {
  0,  1,  4,  5,  2,  3, 14, 15,          /* the MZ's black, blue, red, magenta, green, cyan, yellow, white */
  10, 6,  9, 12,                          /* players 1-4: light green, orange, light blue, light red */
  7,  7,  6,  2                           /* the spares: greys, brown, dark green */
};

void plat_init(void) {
  union REGS r;
  uint8_t i;
  r.h.ah = 0x0f; int86(0x10, &r, &r); old_mode = r.h.al;
  r.h.ah = 0x12; r.h.bl = 0x10; r.h.bh = 0xff; int86(0x10, &r, &r);   /* EGA or better? (BH 0 or 1 if so) */
  if (r.h.bh > 1) { printf("BomberNet needs an EGA or VGA card.\n"); exit(1); }
  r.h.ah = 0; r.h.al = 0x0d; int86(0x10, &r, &r);
  vram = (volatile uint8_t far *)MK_FP(0xa000, 0);
  r.x.ax = 0x1a00; int86(0x10, &r, &r);
  if (r.h.al == 0x1a) {                          /* a VGA: the game's colours exactly, in the DAC entries mode 0Dh shows */
    for (i = 0; i < 16; i++) {                   /* (0-7 and 16-23), 6 bits a gun */
      uint16_t c = amiga_palette[i];
      r.x.ax = 0x1010; r.x.bx = (uint16_t)(i < 8 ? i : i + 8);
      r.h.dh = (uint8_t)(((c >> 8) & 15) * 63 / 15); r.h.ch = (uint8_t)(((c >> 4) & 15) * 63 / 15); r.h.cl = (uint8_t)((c & 15) * 63 / 15);
      int86(0x10, &r, &r);
      col_map[i] = i;
    }
  } else for (i = 0; i < 16; i++) col_map[i] = ega_colours[i];   /* an EGA: its 16, chosen by hand */
  outp(SEQ_INDEX, 2); outp(SEQ_DATA, 0x0f);      /* all four planes written */
  outp(GC_INDEX, 3); outp(GC_DATA, 0);           /* no rotation, replace */
  outp(GC_INDEX, 5); outp(GC_DATA, 0);           /* write mode 0 */
  outp(GC_INDEX, 1); outp(GC_DATA, 0x0f);        /* Set/Reset for all four planes */
  outp(GC_INDEX, 0); outp(GC_DATA, 0);
  outp(GC_INDEX, 8); outp(GC_DATA, 0xff);
  vram[0xffff] = 0;                              /* the latches' zero (Set/Reset 0, all bits) */
  old_int8 = _dos_getvect(0x08);
  _disable();
  _dos_setvect(0x08, timer_isr);
  outp(0x43, 0x36); outp(0x40, TICK_DIV & 0xff); outp(0x40, TICK_DIV >> 8);
  _enable();
  started = 1;
  atexit(cleanup);
  joy_type = 1;
  set_mode(1);
#ifdef PC_TEST
  match_seed = rec[4] | (uint16_t)(rec[5] << 8);
  menu_mode = rec[6]; menu_players = rec[7];
  _fmemcpy(menu_inputs, rec + 8, 4);
#ifdef TEST_HASH_OFF
  hash_period = 0;
#else
  hash_period = rec[12];
#endif
  replay_active = 1;
#endif
  next_frame = ticks + FRAME_MS;
}
