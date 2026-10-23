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
 * Input: the keyboard processor (IKBD) sends make and break codes, and in
 * joystick mode a packet for each joystick change; TOS's handler of its
 * bytes (ikbdsys) is replaced while the game runs, keeping the state of
 * every key (several at once, as on the other machines) and of both
 * joysticks. The mouse is off meanwhile (port 0 is a joystick).
 *
 * Time: TOS's vertical-blank count (_frclock), a game frame being 3 TV
 * frames at 50 Hz, or 4,3,4,3,4 at 60 Hz. The MFP's timer A (38400 Hz,
 * an interrupt every 64 counts: 1.7 ms) ends a tone on time and times the
 * frames in the test build.
 *
 * Sound: the YM2149's channel A, a square wave at the MZ's pitch, its
 * volume put to 0 by timer A when the tone's length is up. */
#include <stdint.h>
#include <setjmp.h>
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
#define ACIA_CTRL (*(volatile uint8_t *)0xfffffc00)     /* the keyboard's ACIA: bit 0, a byte received */
#define ACIA_DATA (*(volatile uint8_t *)0xfffffc02)
#define YM_SELECT (*(volatile uint8_t *)0xffff8800)     /* the YM2149: the register (and reading it back) */
#define YM_WRITE  (*(volatile uint8_t *)0xffff8802)

static uint8_t *screen;
static long old_ssp;
static jmp_buf quit_jmp;
static int16_t old_rez = -1;
static int16_t old_palette[16];
static uint32_t st_rep[256];              /* a glyph byte in all four bytes */
static uint32_t colour_mask[16];          /* a colour's planes: byte k (the most significant first) for plane k */
static _KBDVECS *kbdv;
static long (*old_ikbdsys)(void);
static uint8_t old_mixer, have_ym;

/* the MFP's timer A: an interrupt every 64 counts of 38400 Hz; the tone's
 * countdown, at 0 the YM's channel A silent (its volume, register 8: the
 * register selected before is put back, as the main program may be
 * between selecting one and writing it) */
volatile uint32_t st_ta_ticks;
volatile uint16_t st_tone_q;
extern void st_timer_a(void);
__asm__("\n"
        "        .text\n"
        "        .globl  _st_timer_a\n"
        "_st_timer_a:\n"
        "        addq.l  #1,_st_ta_ticks\n"
        "        tst.w   _st_tone_q\n"
        "        beq.s   1f\n"
        "        subq.w  #1,_st_tone_q\n"
        "        bne.s   1f\n"
        "        move.w  sr,-(sp)\n"
        "        move.w  #0x2700,sr\n"
        "        move.b  #8,0xffff8800\n"
        "        move.b  #0,0xffff8802\n"
        "        move.w  (sp)+,sr\n"
        "1:      bclr    #5,0xfffffa0f\n"    /* the in-service bit: TOS runs the MFP with software end of interrupt */
        "        rte\n");
#define TIMER_HZ 38400u
#ifdef ST_TEST
static uint32_t st_time(void) {           /* in 1/38400 s */
  uint32_t t;
  uint8_t c;
  do { t = st_ta_ticks; c = MFP_TADR; } while (t != st_ta_ticks);
  return (t << 6) + (uint8_t)(64u - c);      /* the count runs 64 .. 1 */
}
#endif

#ifdef ST_PLAYTEST
static FILE *playlog;                     /* tools/st_play.py reads it: the players' places every frame */
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

/* ---- the keyboard and the joysticks: the IKBD's bytes ----
 * st_ikbd replaces TOS's ikbdsys, called from TOS's ACIA interrupt: it
 * takes every byte waiting and gives each to ikbd_byte. Codes 0xf6-0xff
 * begin a packet (the IKBD's status, the mouse, the clock, the
 * joysticks: so many bytes follow); a joystick's packet is its header
 * (0xfe port 0, 0xff port 1) and a byte: fire in bit 7, up 0, down 1,
 * left 2, right 3. Below them, a key's code, bit 7 set when released. */
static volatile uint8_t keydown[128], joy_state[2];
static uint8_t pkt_left, pkt_type;
void ikbd_byte(uint8_t b);
void ikbd_byte(uint8_t b) {
  static const uint8_t length[10] = {7, 5, 2, 2, 2, 2, 6, 2, 1, 1};   /* the bytes after the headers 0xf6..0xff */
  if (pkt_left) {
    pkt_left--;
    if (pkt_type >= 0xfe) joy_state[pkt_type - 0xfe] = b;
    return;
  }
  if (b >= 0xf6) { pkt_type = b; pkt_left = length[b - 0xf6]; return; }
  keydown[b & 0x7f] = !(b & 0x80);
}
extern long st_ikbd(void);
__asm__("\n"
        "        .text\n"
        "        .globl  _st_ikbd\n"
        "_st_ikbd:\n"
        "        movem.l d0-d2/a0-a2,-(sp)\n"
        "1:      btst    #0,0xfffffc00\n"    /* a byte received */
        "        beq.s   2f\n"
        "        moveq   #0,d0\n"
        "        move.b  0xfffffc02,d0\n"
        "        move.l  d0,-(sp)\n"
        "        jsr     _ikbd_byte\n"
        "        addq.l  #4,sp\n"
        "        bra.s   1b\n"
        "2:      movem.l (sp)+,d0-d2/a0-a2\n"
        "        rts\n");
#define K_ESC 0x01
#define K_BACKSPACE 0x0e
#define K_RETURN 0x1c
#define K_SPACE 0x39
#define K_UP 0x48
#define K_LEFT 0x4b
#define K_RIGHT 0x4d
#define K_DOWN 0x50
#define K_DELETE 0x53

/* ---- menus and input ---- */
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
  uint8_t k = 0;
#ifdef ST_TEST
  return title_mode && (frames & 1) ? KEY_SPACE : 0;
#endif
  if (keydown[K_UP]) k |= KEY_UP;
  if (keydown[K_DOWN]) k |= KEY_DOWN;
  if (keydown[K_LEFT]) k |= KEY_LEFT;
  if (keydown[K_RIGHT]) k |= KEY_RIGHT;
  if (kbd_alt_fire ? keydown[K_RETURN] : keydown[K_SPACE]) k |= KEY_SPACE;
  return k;
}

uint8_t plat_keys_b(void) {
  uint8_t k = 0;
  if (keydown[0x11]) k |= KEY_UP;           /* W */
  if (keydown[0x1f]) k |= KEY_DOWN;         /* S */
  if (keydown[0x1e]) k |= KEY_LEFT;         /* A */
  if (keydown[0x20]) k |= KEY_RIGHT;        /* D */
  if (keydown[0x12]) k |= KEY_SPACE;        /* E */
  return k;
}

/* joystick 0 (n 0) is the one in port 1, the joystick port; joystick 1 the mouse's port 0 */
uint8_t plat_joy(uint8_t n) {
  uint8_t j = joy_state[n ? 0 : 1], k = 0;
  if (j & 1) k |= KEY_UP;
  if (j & 2) k |= KEY_DOWN;
  if (j & 4) k |= KEY_LEFT;
  if (j & 8) k |= KEY_RIGHT;
  if (j & 0x80) k |= KEY_SPACE;
  return k;
}

/* text entry: letters, BACKSPACE or DELETE deletes, ESC cancels */
static const char letters[3][11] = {"QWERTYUIOP", "ASDFGHJKL", "ZXCVBNM"};
static uint8_t typing;                    /* plat_key_char was asked this frame: a room code or the lobby, where ESC cancels */
uint8_t plat_key_char(void) {
  uint8_t r, c;
  typing = 1;
  if (keydown[K_ESC]) return 0x1b;
  if (keydown[K_BACKSPACE] || keydown[K_DELETE]) return 8;
  for (r = 0; r < 3; r++)
    for (c = 0; letters[r][c]; c++)
      if (keydown[(r == 0 ? 0x10 : r == 1 ? 0x1e : 0x2c) + c]) return (uint8_t)letters[r][c];
  return 0;
}

uint8_t plat_break(void) { return keydown[K_ESC]; }

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
  {                                       /* ESC on the title screen leaves the game: a new press (not one held from */
    static uint8_t esc_was;               /* cancelling a room code), on a frame nothing asked for typed keys */
    uint8_t esc = keydown[K_ESC];
    if (title_mode && esc && !esc_was && !typing) longjmp(quit_jmp, 1);
    esc_was = esc;
    typing = 0;
  }
}

void plat_delay(void) { Vsync(); }

/* the YM's register written with the interrupts off: timer A's interrupt writes one too */
static void ym(uint8_t reg, uint8_t v) {
  uint16_t sr;
  __asm__ volatile("move.w sr,%0\n\tmove.w #0x2700,sr" : "=d"(sr));
  YM_SELECT = reg; YM_WRITE = v;
  __asm__ volatile("move.w %0,sr" :: "d"(sr));
}

/* frequency = 1108400 / ratio Hz; the YM's 2 MHz over 16 a period count:
 * the period is ratio * 125000 / 1108400. The length: len units of about
 * 0.93 ms, timer A's interrupts 1.67 ms apart (len * 0.559) */
void plat_tone(uint16_t ratio, uint8_t len) {
  uint32_t period;
#ifdef ST_NO_TONES
  return;
#endif
  if (!have_ym || !ratio) return;
  period = (uint32_t)ratio * 125000UL / 1108400UL;
  if (period < 1) period = 1;
  if (period > 4095) period = 4095;
  st_tone_q = 0;
  ym(0, (uint8_t)period);
  ym(1, (uint8_t)(period >> 8));
  ym(8, 12);
  st_tone_q = (uint16_t)((((uint16_t)len * 143) >> 8) + 1);
}

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
#ifdef ST_PLAYTEST
  if (playlog) {
    static uint8_t every;
    fprintf(playlog, "%u %u %u %u %u %u %u %u %u %u %u %u %u %u %u\n", frame_no, title_mode, players[0].x, players[0].y, players[0].state,
            players[1].x, players[1].y, players[1].state, players[2].x, players[2].y, players[2].state,
            players[3].x, players[3].y, players[3].state, (unsigned)(kbd_alt_fire | have_ym << 1));
    if (!(++every & 7)) fflush(playlog);
  }
#endif
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
      longjmp(quit_jmp, 1);
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
static uint8_t started;                   /* plat_init done: the timer, the YM, the keyboard taken */
static void cleanup(void) {
  static const char mouse_on[1] = {0x08};       /* the IKBD's relative mouse mode: the mouse back on port 0 */
  if (!started) return;
  *(volatile uint8_t *)0xfffffa19 = 0;          /* timer A stopped (its control register), its interrupt off */
  Jdisint(13);
  if (have_ym) { ym(8, 0); ym(7, old_mixer); }
  if (old_ikbdsys) {
    Ikbdws(0, (void *)mouse_on);
    kbdv->ikbdsys = old_ikbdsys;
    __asm__ volatile("dc.w 0xa009" ::: "d0", "d1", "d2", "a0", "a1", "a2");   /* Line-A: the mouse pointer shown */
  }
#ifdef ST_PLAYTEST
  if (playlog) fclose(playlog);
#endif
  if (old_rez >= 0) Setscreen((void *)-1, (void *)-1, old_rez);
  Setpalette(old_palette);
  Vsync();
}

/* The core's main is compiled as bomber_main (tools/build_st.py) and
 * never returns (a game on an 8-bit machine has nowhere to go); this main
 * enters supervisor mode around it. ESC on the title screen (and the end of
 * a test replay) comes back here by longjmp: the cleanup, then supervisor
 * mode left at the depth it was entered, as TOS requires (leaving it from
 * deeper, in an atexit handler, crashed on the way out). */
void bomber_main(void);
#undef main                               /* the build renames the core's main, not this one */
int main(void) {
  if (Getrez() == 2) { (void)Cconws("BomberNet needs a colour monitor (low resolution).\r\n"); return 1; }
  old_ssp = Super(0L);
  if (!setjmp(quit_jmp)) bomber_main();
  cleanup();
  Super(old_ssp);
  return 0;
}

void plat_init(void) {
  static int16_t palette[16];
  uint16_t i;
  int16_t rez = Getrez();
  started = 1;
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
  {                                             /* the YM: channel A's tone on, its volume 0; the port bits (6-7, the floppy's) kept */
    uint16_t sr;
    __asm__ volatile("move.w sr,%0\n\tmove.w #0x2700,sr" : "=d"(sr));
    YM_SELECT = 7; old_mixer = YM_SELECT;
    __asm__ volatile("move.w %0,sr" :: "d"(sr));
    ym(8, 0);
    ym(7, (uint8_t)((old_mixer & 0xc0) | 0x3e));
    have_ym = 1;
  }
  Xbtimer(0, 5, 64, st_timer_a);                /* timer A: /64 (38400 Hz), 64 counts an interrupt (600 Hz) */
  {                                             /* the keyboard and the joysticks: the IKBD's bytes to us; joystick reports */
    static const char joy_on[1] = {0x14};       /* (the mouse off: port 0 is a joystick) */
    kbdv = Kbdvbase();
    old_ikbdsys = kbdv->ikbdsys;
    kbdv->ikbdsys = st_ikbd;
    Ikbdws(0, (void *)joy_on);
  }
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
#ifdef ST_PLAYTEST
  playlog = fopen("C:\\PLAY.LOG", "w");
  menu_mode = 1; menu_players = 4;              /* a deathmatch of 4: key set A, key set B, the two joystick ports */
  menu_inputs[0] = INPUT_KBD_A; menu_inputs[1] = INPUT_KBD_B; menu_inputs[2] = INPUT_JOY1; menu_inputs[3] = INPUT_JOY2;
#endif
  last_tick = FRCLOCK;
}
