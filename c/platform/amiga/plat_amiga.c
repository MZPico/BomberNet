/* Amiga: the platform layer (core/platform.h), step 0 of
 * docs/port-amiga.md: the screen, the frame clock, and the test mode that
 * replays a recording and times its frames. An OS program: it runs from
 * Workbench or the Shell and leaves the OS running (a TCP/IP stack is an
 * OS process).
 *
 * Screen: an Intuition screen of 320 x 200 pixels in 4 bitplanes (16
 * colours), 40 x 25 cells of 8 x 8: a cell is one of the MZ's glyphs
 * (tables.c) in one colour, its 8 bytes written to each plane where the
 * colour has the bit set. Time: a vertical-blank interrupt server counts
 * TV frames; a frame is 3 PAL frames, or 4,3,4,3,4 NTSC ones. */
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <proto/exec.h>
#include <proto/intuition.h>
#include <proto/graphics.h>
#include <proto/timer.h>
#include <intuition/screens.h>
#include <graphics/gfxbase.h>
#include <hardware/intbits.h>
#include <devices/timer.h>
#include <exec/interrupts.h>
#include "game.h"
#include "tables.h"

struct IntuitionBase *IntuitionBase;
struct GfxBase *GfxBase;
struct Device *TimerBase;
static struct Screen *screen;
static struct MsgPort *timer_port;
static struct timerequest *timer_req;
static uint8_t *plane[4];
static uint16_t bpr;                      /* bytes a row of a plane */

/* the vertical-blank server: +1 a TV frame (a Z flag set on return, as a
 * server must; the OS calls it with A1 = is_Data) */
volatile uint32_t amiga_ticks;
extern void amiga_vb_server(void);
asm("\n"
    "        .text\n"
    "        .globl  _amiga_vb_server\n"
    "_amiga_vb_server:\n"
    "        addq.l  #1,_amiga_ticks\n"
    "        moveq   #0,d0\n"
    "        rts\n");
static struct Interrupt vb_int;

/* the E clock (timer.device): 709379 Hz on PAL, 715909 NTSC */
static uint32_t eclock_hz;
static uint32_t eclock(void) {
  struct EClockVal ev;
  ReadEClock(&ev);
  return ev.ev_lo;
}

#ifdef AMIGA_TEST
#include "rec_data.h"                     /* a recording to replay (tools/build_amiga.py --test) */
#define TEST_FRAMES ((REC_LEN - 16) / 6)
static uint16_t rec_pos = 16, frames;
static uint32_t t_start, t_flush;
static struct {
  uint16_t bad, late, n;                  /* the first record whose hash differs (its offset), late frames, frames timed */
  uint32_t flush;                         /* E-clock ticks in the game flush, all frames */
  uint32_t work[TEST_FRAMES];             /* each game frame's work (flush .. next frame sync), E-clock ticks */
  uint32_t fl[TEST_FRAMES];               /* of it the flush */
} test;
#endif

/* ---- menus and input (step 1 fills these in) ---- */
const char *const plat_joy_names[PLAT_JOY_TYPES] = {"NONE    ", "AMIGA   "};
const char *const plat_kbd_a_alt_name = "CURSOR AND RET  ";
const char *plat_input_name(uint8_t input) {
  switch (input) {
  case INPUT_KBD_A: return "CURSOR AND SPACE";
  case INPUT_KBD_B: return "WASD AND E      ";
  case INPUT_JOY1:  return "JOYSTICK 2      ";
  case INPUT_JOY2:  return "JOYSTICK 1      ";
  }
  return "";
}
uint8_t plat_input_allowed(uint8_t input) {
  if (input == INPUT_JOY1 || input == INPUT_JOY2) return joy_type != JOY_NONE;
  return 1;
}
uint8_t plat_keys_a(void) {
#ifdef AMIGA_TEST
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
static uint8_t ntsc, ntsc_step;

void plat_frame_sync(void) {                    /* 60 ms: 3 PAL frames, or 4,3,4,3,4 NTSC ones (3.6) */
  uint8_t need = 3;
  if (ntsc) { need = (ntsc_step & 1) ? 3 : 4; if (++ntsc_step == 5) ntsc_step = 0; }
#ifdef AMIGA_TEST
  if (t_start && test.n < TEST_FRAMES) {
    test.work[test.n++] = eclock() - t_start;
    if (amiga_ticks - last_tick > need) test.late++;
  }
  t_start = 0;
#endif
  while (amiga_ticks - last_tick < need) WaitTOF();
  last_tick = amiga_ticks;
}

void plat_delay(void) { WaitTOF(); }

void plat_tone(uint16_t ratio, uint8_t len) { (void)ratio; (void)len; }   /* step 1: Paula */

/* ---- screen ---- */
static uint8_t mode = 0xff;

/* one cell: glyph g in colour col at column x, row y. The 8 bytes of the
 * glyph to each plane where the colour has the bit set, zero where not: in
 * assembly, gcc's byte loops cost 380 ms for a stage's 1000 cells. */
extern void amiga_cell(uint8_t *dst, const uint8_t *glyph, uint16_t colour, uint16_t stride, uint32_t plane_step);
asm("\n"
    "        .text\n"
    "        .globl  _amiga_cell\n"
    "_amiga_cell:\n"                    /* 4(sp) dst in plane 0, 8(sp) glyph, 14(sp) colour, 18(sp) stride, 20(sp) plane_step */
    "        movem.l d2-d5/a2,-(sp)\n"
    "        move.l  24(sp),a0\n"
    "        move.l  28(sp),a1\n"
    "        move.w  34(sp),d0\n"
    "        move.w  38(sp),d1\n"
    "        move.l  40(sp),d5\n"
    "        moveq   #3,d4\n"            /* 4 planes */
    "1:      move.l  a0,a2\n"
    "        lsr.w   #1,d0\n"
    "        bcc.s   3f\n"
    "        move.b  (a1),(a2)\n"        /* the glyph's rows */
    "        move.b  1(a1),0(a2,d1.w)\n"
    "        add.w   d1,a2\n"
    "        move.b  2(a1),0(a2,d1.w)\n"
    "        add.w   d1,a2\n"
    "        move.b  3(a1),0(a2,d1.w)\n"
    "        add.w   d1,a2\n"
    "        move.b  4(a1),0(a2,d1.w)\n"
    "        add.w   d1,a2\n"
    "        move.b  5(a1),0(a2,d1.w)\n"
    "        add.w   d1,a2\n"
    "        move.b  6(a1),0(a2,d1.w)\n"
    "        add.w   d1,a2\n"
    "        move.b  7(a1),0(a2,d1.w)\n"
    "        bra.s   4f\n"
    "3:      clr.b   (a2)\n"             /* zeroes */
    "        clr.b   0(a2,d1.w)\n"
    "        add.w   d1,a2\n"
    "        clr.b   0(a2,d1.w)\n"
    "        add.w   d1,a2\n"
    "        clr.b   0(a2,d1.w)\n"
    "        add.w   d1,a2\n"
    "        clr.b   0(a2,d1.w)\n"
    "        add.w   d1,a2\n"
    "        clr.b   0(a2,d1.w)\n"
    "        add.w   d1,a2\n"
    "        clr.b   0(a2,d1.w)\n"
    "        add.w   d1,a2\n"
    "        clr.b   0(a2,d1.w)\n"
    "4:      add.l   d5,a0\n"            /* the next plane */
    "        dbf     d4,1b\n"
    "        movem.l (sp)+,d2-d5/a2\n"
    "        rts\n");
static uint32_t plane_step;               /* from one plane to the next (Intuition allocates them in a row) */

static void cell(uint8_t x, uint8_t y, uint8_t g, uint8_t col) {
  if (plane_step)
    amiga_cell(plane[0] + (uint32_t)y * 8 * bpr + x, amiga_glyphs + ((uint16_t)g << 3), col, bpr, plane_step);
  else {                                  /* planes not evenly spaced: the C way */
    const uint8_t *s = amiga_glyphs + ((uint16_t)g << 3);
    uint32_t off = (uint32_t)y * 8 * bpr + x;
    uint8_t p, l;
    for (p = 0; p < 4; p++) {
      uint8_t *d = plane[p] + off;
      if (col & (1 << p)) for (l = 0; l < 8; l++, d += bpr) *d = s[l];
      else for (l = 0; l < 8; l++, d += bpr) *d = 0;
    }
  }
}

static void set_mode(uint8_t title) {           /* everything is drawn again */
  mode = title;
  memset(shadow_vram, 0xff, SCREEN_CELLS);
}

void flush_screen(void) {
  uint8_t x, y, *d = draw_buf, *s = shadow_vram, a;
  const uint8_t *glyph, *colour;
#ifdef AMIGA_TEST
  frames++;
  if (!title_mode && rec_pos + 6 <= REC_LEN) {   /* the hash checked */
    if (hash_period && (rec[rec_pos + 4] | (uint16_t)(rec[rec_pos + 5] << 8)) != state_hash && !test.bad)
      test.bad = rec_pos;
    memcpy(replay_keys, rec + rec_pos, 4); rec_pos += 6;
  }
  if (!title_mode) t_start = t_flush = eclock();
#endif
  if (mode != title_mode) set_mode(title_mode);
  glyph = title_mode ? amiga_title_glyph : amiga_game_glyph;
  colour = title_mode ? amiga_title_colour : amiga_game_colour;
  for (y = 0; y < SCREEN_H; y++)
    for (x = 0; x < SCREEN_W; x++, d++, s++) {
      a = *d;
      if (title_mode || y == HUD_ROW) *d = C_SPACE;   /* in a game rows 0-23 are overwritten by composite_frame */
      if (a == *s) continue;
      *s = a;
      cell(x, y, glyph[a], colour[a]);
    }
#ifdef AMIGA_TEST
  if (!title_mode) {
    if (rec_pos + 6 <= REC_LEN) { uint32_t d = eclock() - t_flush; test.flush += d; if (test.n < TEST_FRAMES) test.fl[test.n] = d; }
    else if (rec_pos + 6 > REC_LEN) {           /* the recording replayed: the figures out, done */
      FILE *f = fopen(AMIGA_RESULT, "w");
      uint16_t i;
      if (f) {
        fprintf(f, "frames %u bad %u late %u hz %lu flush %lu ntsc %u planestep %lu\n", test.n, test.bad, test.late,
                (unsigned long)eclock_hz, (unsigned long)test.flush, ntsc, (unsigned long)plane_step);
        for (i = 0; i < test.n; i++) fprintf(f, "%lu %lu\n", (unsigned long)test.work[i], (unsigned long)test.fl[i]);
        fclose(f);
      }
      exit(0);
    }
  }
#endif
}

void plat_player_colour(uint8_t x, uint8_t y, uint8_t player) {
  uint16_t i = (uint16_t)y * SCREEN_W + x;
  cell(x, y, amiga_game_glyph[shadow_vram[i]], (uint8_t)(8 + player));
}

void plat_hud_colour(uint8_t x, uint8_t n, uint8_t player, uint8_t blank) {
  (void)blank;
  for (; n; n--, x++) cell(x, HUD_ROW, amiga_game_glyph[shadow_vram[HUD_ROW * SCREEN_W + x]], (uint8_t)(8 + player));
}

/* ---- start and end ---- */
static void cleanup(void) {
  if (vb_int.is_Code) RemIntServer(INTB_VERTB, &vb_int);
  if (screen) CloseScreen(screen);
  if (timer_req) { CloseDevice((struct IORequest *)timer_req); DeleteIORequest((struct IORequest *)timer_req); }
  if (timer_port) DeleteMsgPort(timer_port);
  if (GfxBase) CloseLibrary((struct Library *)GfxBase);
  if (IntuitionBase) CloseLibrary((struct Library *)IntuitionBase);
}

void plat_init(void) {
  uint8_t i;
  struct EClockVal ev;
  atexit(cleanup);
  IntuitionBase = (struct IntuitionBase *)OpenLibrary("intuition.library", 37);
  GfxBase = (struct GfxBase *)OpenLibrary("graphics.library", 37);
  if (!IntuitionBase || !GfxBase) exit(20);
  timer_port = CreateMsgPort();
  timer_req = (struct timerequest *)CreateIORequest(timer_port, sizeof(struct timerequest));
  if (!timer_req || OpenDevice("timer.device", UNIT_ECLOCK, (struct IORequest *)timer_req, 0)) exit(20);
  TimerBase = timer_req->tr_node.io_Device;
  eclock_hz = ReadEClock(&ev);
  ntsc = (GfxBase->DisplayFlags & PAL) == 0;
  screen = OpenScreenTags(NULL, SA_Width, 320, SA_Height, 200, SA_Depth, 4, SA_Quiet, TRUE, SA_ShowTitle, FALSE,
                          SA_Title, (ULONG)"BomberNet", TAG_DONE);
  if (!screen) exit(20);
  LoadRGB4(&screen->ViewPort, (UWORD *)amiga_palette, 16);
  bpr = screen->BitMap.BytesPerRow;
  for (i = 0; i < 4; i++) { plane[i] = (uint8_t *)screen->BitMap.Planes[i]; memset(plane[i], 0, (size_t)bpr * 200); }
  plane_step = (uint32_t)(plane[1] - plane[0]);
  if (plane[2] - plane[1] != (long)plane_step || plane[3] - plane[2] != (long)plane_step) plane_step = 0;
  vb_int.is_Node.ln_Type = NT_INTERRUPT;
  vb_int.is_Node.ln_Pri = 10;
  vb_int.is_Node.ln_Name = "BomberNet";
  vb_int.is_Code = (void (*)())amiga_vb_server;
  AddIntServer(INTB_VERTB, &vb_int);
  joy_type = 1;
  set_mode(1);
#ifdef AMIGA_TEST
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
  last_tick = amiga_ticks;
}
