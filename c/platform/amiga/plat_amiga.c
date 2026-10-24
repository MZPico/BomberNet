/* Amiga: the platform layer (core/platform.h), docs/port-amiga.md. An OS
 * program: it runs from Workbench or the Shell on a screen of its own and
 * leaves the OS running (a TCP/IP stack is an OS process).
 *
 * Screen: an Intuition screen of 320 x 200 pixels in 4 bitplanes (16
 * colours), 40 x 25 cells of 8 x 8: a cell is one of the MZ's glyphs
 * (tables.c) in one colour, its 8 bytes written to each plane where the
 * colour has the bit set (amiga_cell, assembly). A backdrop window on it
 * takes the keyboard's raw key events; the state of every key is kept, so
 * several keys at once work as on the other machines. The joysticks are
 * read from the hardware, as Amiga games do. Time: a vertical-blank
 * interrupt server counts TV frames (a frame is 3 PAL frames, or 4,3,4,3,4
 * NTSC ones) and switches a tone's DMA off when its time is up. Sound: one
 * Paula channel (allocated through audio.device) plays a square-wave
 * sample of the tone's length followed by silence, at the MZ's pitch. */
#include <stdint.h>
#include <setjmp.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <proto/exec.h>
#include <dos/dos.h>
#include <dos/dosextens.h>
#include <proto/intuition.h>
#include <proto/graphics.h>
#include <proto/timer.h>
#include <intuition/screens.h>
#include <intuition/intuition.h>
#include <graphics/gfxbase.h>
#include <hardware/custom.h>
#include <hardware/cia.h>
#include <hardware/dmabits.h>
#include <hardware/intbits.h>
#include <devices/timer.h>
#include <devices/audio.h>
#include <exec/interrupts.h>
#include <exec/memory.h>
#include "game.h"
#include "tables.h"

/* Our own stack: the Shell gives a program 4-16 KB, a Workbench icon 4 KB
 * unless it says more, and the game with its network needs more. The
 * core's main is compiled as bomber_main (tools/build_amiga.py); this main
 * swaps to a 64 KB stack around it. The core never returns (a game on an
 * 8-bit machine has nowhere to go); ESC on the title screen comes back
 * here by longjmp (plat_frame_sync), and main returns to the Shell or
 * Workbench, the atexit cleanup closing the screen. The state is static:
 * locals are not to be trusted across a change of stack. */
#define STACK_SIZE 65536
void bomber_main(void);
static struct StackSwapStruct sss;
static void *stack_mem;
static jmp_buf quit_jmp;
#undef main                               /* the build renames the core's main, not this one */
int main(void) {
  stack_mem = AllocMem(STACK_SIZE, MEMF_ANY);
  if (!stack_mem) return 20;
  sss.stk_Lower = stack_mem;
  sss.stk_Upper = (ULONG)stack_mem + STACK_SIZE;
  sss.stk_Pointer = (APTR)sss.stk_Upper;
  StackSwap(&sss);
  if (!setjmp(quit_jmp)) bomber_main();
  StackSwap(&sss);
  FreeMem(stack_mem, STACK_SIZE);
  return 0;
}

struct IntuitionBase *IntuitionBase;
struct GfxBase *GfxBase;
struct Device *TimerBase;
#define CUSTOM (*(volatile struct Custom *)0xdff000)
#define CIAA   (*(volatile struct CIA *)0xbfe001)
static struct Screen *screen;
static struct Window *window;
static struct MsgPort *timer_port, *audio_port;
static struct timerequest *timer_req;
static struct IOAudio *audio_io;
static uint8_t *plane[4];
static uint16_t bpr;                      /* bytes a row of a plane */
static uint32_t plane_step;               /* from one plane to the next (0: not evenly spaced) */
static int8_t *sample;                    /* the tone's sample, chip RAM */
static uint16_t *blank_pointer;           /* an invisible mouse pointer, chip RAM */
static uint8_t have_audio, audio_ch;
static uint16_t height;                   /* the screen's: 256 on PAL, 200 NTSC; the field in its middle */
#define SAMPLE_BYTES 2048

/* the vertical-blank server (the OS calls it with A1 = is_Data; a server
 * returns with the Z flag set): +1 a TV frame, and the tone's countdown:
 * at 0 the channel's DMA goes off */
volatile uint32_t amiga_ticks;
volatile uint16_t amiga_tone_q, amiga_tone_off;
extern void amiga_vb_server(void);
asm("\n"
    "        .text\n"
    "        .globl  _amiga_vb_server\n"
    "_amiga_vb_server:\n"
    "        addq.l  #1,_amiga_ticks\n"
    "        tst.w   _amiga_tone_q\n"
    "        beq.s   1f\n"
    "        subq.w  #1,_amiga_tone_q\n"
    "        bne.s   1f\n"
    "        move.w  _amiga_tone_off,0xdff096\n"
    "1:      moveq   #0,d0\n"
    "        rts\n");
static struct Interrupt vb_int;

/* the E clock (timer.device): 709379 Hz on PAL, 715909 NTSC */
static uint32_t eclock_hz;
#ifdef AMIGA_NETTEST
static uint32_t eclock_now(void) { struct EClockVal ev; ReadEClock(&ev); return ev.ev_lo; }
#endif
#ifdef AMIGA_TEST
static uint32_t eclock(void) {
  struct EClockVal ev;
  ReadEClock(&ev);
  return ev.ev_lo;
}
#endif

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
#ifdef AMIGA_PLAYTEST
static FILE *playlog;                     /* tools/amiga_play.py reads it: the players' places every frame */
#endif
#ifdef AMIGA_NETTEST
#include "netdev.h"
static FILE *netlog;                      /* tools/crossplay.py reads it: the room's state every frame */
static char log_buf[16384];
static uint16_t log_len;
static void log_out(void) { if (log_len) { fwrite(log_buf, 1, log_len, netlog); fflush(netlog); log_len = 0; } }
static void log_s(const char *t) { while (*t) log_buf[log_len++] = *t++; }
static void log_dec(uint32_t v) {
  char d[10]; uint8_t n = 0;
  do { d[n++] = (char)('0' + v % 10); v /= 10; } while (v);
  while (n) log_buf[log_len++] = d[--n];
}
static void log_hex(uint8_t v) {
  static const char digit[] = "0123456789abcdef";
  log_buf[log_len++] = digit[v >> 4]; log_buf[log_len++] = digit[v & 15];
}
#endif

/* ---- the keyboard: raw key codes, their state kept ---- */
static uint8_t keydown[128];
static void poll_keys(void) {
  struct IntuiMessage *m;
  if (!window) return;
  while ((m = (struct IntuiMessage *)GetMsg(window->UserPort)) != NULL) {
    if (m->Class == IDCMP_RAWKEY) keydown[m->Code & 0x7f] = !(m->Code & 0x80);
    ReplyMsg((struct Message *)m);
  }
}
#define K_SPACE 0x40
#define K_BACKSPACE 0x41
#define K_RETURN 0x44
#define K_ESC 0x45
#define K_DEL 0x46
#define K_UP 0x4c
#define K_DOWN 0x4d
#define K_RIGHT 0x4e
#define K_LEFT 0x4f

/* ---- menus and input ---- */
const char *const plat_joy_names[PLAT_JOY_TYPES] = {"NONE    ", "AMIGA   "};
const char *const plat_kbd_a_alt_name = "CURSOR AND RET  ";
const char *plat_input_name(uint8_t input) {
  switch (input) {
  case INPUT_KBD_A: return "CURSOR AND SPACE";
  case INPUT_KBD_B: return "WASD AND E      ";
  case INPUT_JOY1:  return "JOYSTICK PORT 2 ";
  case INPUT_JOY2:  return "JOYSTICK PORT 1 ";
  }
  return "";
}
uint8_t plat_input_allowed(uint8_t input) {
  if (input == INPUT_JOY1 || input == INPUT_JOY2) return joy_type != JOY_NONE;
  return 1;
}

uint8_t plat_keys_a(void) {
  uint8_t k = 0;
#ifdef AMIGA_TEST
  return title_mode && (frames & 1) ? KEY_SPACE : 0;
#endif
#if defined(AMIGA_NETTEST) && AMIGA_NETTEST == 2
  {                                       /* joining: the room code from DH0:code.txt, once the test wrote it */
    static uint8_t poll, got;
    if (title_mode && !got && !(++poll & 15)) {
      FILE *f = fopen("DH0:code.txt", "r");
      if (f) { if (fread(net_code, 1, 4, f) == 4) { net_code[4] = 0; got = 1; } fclose(f); }
    }
  }
#endif
  poll_keys();
  if (keydown[K_UP]) k |= KEY_UP;
  if (keydown[K_DOWN]) k |= KEY_DOWN;
  if (keydown[K_LEFT]) k |= KEY_LEFT;
  if (keydown[K_RIGHT]) k |= KEY_RIGHT;
  if (kbd_alt_fire ? keydown[K_RETURN] : keydown[K_SPACE]) k |= KEY_SPACE;
  return k;
}

uint8_t plat_keys_b(void) {
  uint8_t k = 0;
  poll_keys();
  if (keydown[0x11]) k |= KEY_UP;           /* W */
  if (keydown[0x21]) k |= KEY_DOWN;         /* S */
  if (keydown[0x20]) k |= KEY_LEFT;         /* A */
  if (keydown[0x22]) k |= KEY_RIGHT;        /* D */
  if (keydown[0x12]) k |= KEY_SPACE;        /* E */
  return k;
}

/* port 2 (n 0, the joystick port) and port 1 (n 1, the mouse's): JOYxDAT
 * has right in bit 1, left in bit 9, down as bit 1 xor bit 0, up as bit 9
 * xor bit 8; the fire buttons are CIA-A's PRA bits 7 and 6, low when
 * pressed */
uint8_t plat_joy(uint8_t n) {
  uint16_t j = n ? CUSTOM.joy0dat : CUSTOM.joy1dat, x = j ^ (j >> 1);
  uint8_t k = 0;
  if (x & 0x0100) k |= KEY_UP;
  if (x & 0x0001) k |= KEY_DOWN;
  if (j & 0x0200) k |= KEY_LEFT;
  if (j & 0x0002) k |= KEY_RIGHT;
  if (!(CIAA.ciapra & (n ? 0x40 : 0x80))) k |= KEY_SPACE;
  return k;
}

/* text entry: letters, BACKSPACE or DEL deletes, ESC cancels */
static const char letters[3][11] = {"QWERTYUIOP", "ASDFGHJKL", "ZXCVBNM"};
static uint8_t typing;                    /* plat_key_char was asked this frame: a room code or the lobby, where ESC cancels */
uint8_t plat_key_char(void) {
  uint8_t r, c;
  typing = 1;
  poll_keys();
  if (keydown[K_ESC]) return 0x1b;
  if (keydown[K_BACKSPACE] || keydown[K_DEL]) return 8;
  for (r = 0; r < 3; r++)
    for (c = 0; letters[r][c]; c++)
      if (keydown[(r == 0 ? 0x10 : r == 1 ? 0x20 : 0x31) + c]) return (uint8_t)letters[r][c];
  return 0;
}

uint8_t plat_break(void) { poll_keys(); return keydown[K_ESC]; }

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
#ifdef AMIGA_TEST
  if (!title_mode) t_start = eclock();         /* the frame's work: from here to the next sync, the next flush included */
#endif
  {                                       /* ESC on the title screen leaves the game: a new press (not one held from */
    static uint8_t esc_was;               /* cancelling a room code), on a frame nothing asked for typed keys */
    uint8_t esc;
    poll_keys();
    esc = keydown[K_ESC];
    if (title_mode && esc && !esc_was && !typing) longjmp(quit_jmp, 1);
    esc_was = esc;
    typing = 0;
  }
}

void plat_delay(void) { WaitTOF(); }

/* ---- sound: the MZ's tones on Paula ----
 * frequency = 1108400 / ratio Hz; a wave of 4 samples, so the period
 * (3546895 Hz over the sample rate) is ratio * 4 / 5. The sample holds the
 * tone for its length (len units of about 0.93 ms) and silence after it;
 * the vertical-blank server stops the DMA a frame or two later, before
 * the sample loops. */
void plat_tone(uint16_t ratio, uint8_t len) {
  uint16_t period, n, i;
  uint32_t bytes;
#ifdef AMIGA_NO_TONES
  return;
#endif
  if (!have_audio || !ratio) return;
  period = (uint16_t)((uint32_t)ratio * 4 / 5);
  if (period < 124) period = 124;
  bytes = (uint32_t)len * 3546895UL / 1075 / period;
  if (bytes > SAMPLE_BYTES - 64) bytes = SAMPLE_BYTES - 64;
  n = (uint16_t)bytes;
  amiga_tone_q = 0;
  CUSTOM.dmacon = (uint16_t)(1 << audio_ch);
  for (i = 0; i < n; i++) sample[i] = (i & 2) ? -100 : 100;
  for (; i < SAMPLE_BYTES; i++) sample[i] = 0;
  CUSTOM.aud[audio_ch].ac_ptr = (UWORD *)sample;
  CUSTOM.aud[audio_ch].ac_len = SAMPLE_BYTES / 2;
  CUSTOM.aud[audio_ch].ac_per = period;
  CUSTOM.aud[audio_ch].ac_vol = 40;
  CUSTOM.dmacon = (uint16_t)(DMAF_SETCLR | (1 << audio_ch));
  amiga_tone_q = (uint16_t)((((uint16_t)len * 3) >> 6) + 2);
}

/* ---- screen ---- */
static uint8_t mode = 0xff;

/* one cell: the glyph's 8 bytes to each plane where the colour has the
 * bit set, zero where not; the rows 40 bytes apart (a 320-pixel screen) */
extern void amiga_cell(uint8_t *dst, const uint8_t *glyph, uint16_t colour, uint32_t plane_step);
asm("\n"
    "        .text\n"
    "        .globl  _amiga_cell\n"
    "_amiga_cell:\n"                    /* 4(sp) dst in plane 0, 8(sp) glyph, 14(sp) colour, 16(sp) plane step */
    "        move.l  d2,-(sp)\n"
    "        move.l  8(sp),a0\n"
    "        move.l  12(sp),a1\n"
    "        move.w  18(sp),d0\n"
    "        move.l  20(sp),d1\n"
    "        moveq   #3,d2\n"            /* 4 planes */
    "1:      lsr.w   #1,d0\n"
    "        bcc.s   3f\n"
    "        move.b  (a1),(a0)\n"
    "        move.b  1(a1),40(a0)\n"
    "        move.b  2(a1),80(a0)\n"
    "        move.b  3(a1),120(a0)\n"
    "        move.b  4(a1),160(a0)\n"
    "        move.b  5(a1),200(a0)\n"
    "        move.b  6(a1),240(a0)\n"
    "        move.b  7(a1),280(a0)\n"
    "        bra.s   4f\n"
    "3:      clr.b   (a0)\n"
    "        clr.b   40(a0)\n"
    "        clr.b   80(a0)\n"
    "        clr.b   120(a0)\n"
    "        clr.b   160(a0)\n"
    "        clr.b   200(a0)\n"
    "        clr.b   240(a0)\n"
    "        clr.b   280(a0)\n"
    "4:      add.l   d1,a0\n"            /* the next plane */
    "        dbf     d2,1b\n"
    "        move.l  (sp)+,d2\n"
    "        rts\n");

static void cell(uint8_t x, uint8_t y, uint8_t g, uint8_t col) {
  uint32_t off = (uint32_t)y * 8 * bpr + x;
  if (plane_step && bpr == 40) amiga_cell(plane[0] + off, amiga_glyphs + ((uint16_t)g << 3), col, plane_step);
  else {                                  /* planes not as expected: the C way */
    const uint8_t *s = amiga_glyphs + ((uint16_t)g << 3);
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
  uint16_t i;
  uint8_t a;
#ifdef AMIGA_TEST
  frames++;
  if (!title_mode && rec_pos + 6 <= REC_LEN) {   /* the hash checked */
    if (hash_period && (rec[rec_pos + 4] | (uint16_t)(rec[rec_pos + 5] << 8)) != state_hash && !test.bad)
      test.bad = rec_pos;
    memcpy(replay_keys, rec + rec_pos, 4); rec_pos += 6;
  }
  if (!title_mode) t_flush = eclock();
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
#ifdef AMIGA_PLAYTEST
  if (playlog) {
    static uint8_t every;
    fprintf(playlog, "%u %u %u %u %u %u %u %u %u %u %u %u %u %u %u\n", frame_no, title_mode, players[0].x, players[0].y, players[0].state,
            players[1].x, players[1].y, players[1].state, players[2].x, players[2].y, players[2].state,
            players[3].x, players[3].y, players[3].state, (unsigned)(kbd_alt_fire | have_audio << 1 | audio_ch << 2));
    if (!(++every & 7)) fflush(playlog);
  }
#endif
#ifdef AMIGA_NETTEST
  if (netlog) {                           /* the state tools/crossplay.py reads from the other machines' memory: */
    static uint8_t every;                 /* formatted by hand into RAM, written out every 32 frames (written with */
    static uint32_t last_log;             /* fprintf and flushed every frame on FS-UAE's drive it took 157 ms a frame) */
    uint32_t log_in = eclock_now();
    uint16_t i;
    if (log_len > sizeof(log_buf) - 2300) log_out();
    log_s("frame "); log_dec(frame_no); log_s(" title "); log_dec(title_mode);
    log_s(" code "); { uint8_t k; for (k = 0; k < 4; k++) log_buf[log_len++] = net_code[k] ? net_code[k] : ' '; }
    log_s(" active "); log_dec(net_active); log_s(" abort "); log_dec(net_abort); log_s(" slot "); log_dec(net_slot);
    log_s(" delay "); log_dec(net_delay); log_s(" table ");
    { uint8_t k; for (k = 0; k < 4; k++) log_hex(net_table[k]); }
    log_s(" hash "); log_hex((uint8_t)(state_hash >> 8)); log_hex((uint8_t)state_hash); log_s(" map ");
#ifndef AMIGA_NETTEST_NOMAP
    if (title_mode || frame_no % 16 == 1)  /* the map: on the hashed frames' lines (what the tools compare) */
      for (i = 0; i < SCREEN_CELLS; i++) log_hex(map_layer[i]);
    else
#endif
    log_s("none");
    { extern uint32_t amiga_send_waits, amiga_recv_calls, amiga_recv_bytes;   /* after the 20 fields the tools read */
      log_s(" tick "); log_dec(amiga_ticks); log_s(" log "); log_dec(last_log); log_s(" sendwaits "); log_dec(amiga_send_waits);
      log_s(" recvcalls "); log_dec(amiga_recv_calls); log_s(" recvbytes "); log_dec(amiga_recv_bytes); log_s("\n");
      amiga_send_waits = amiga_recv_calls = amiga_recv_bytes = 0; }
    if (!(++every & 31)) log_out();
    last_log = eclock_now() - log_in;
  }
#endif
#ifdef AMIGA_TEST
  if (!title_mode) {
    if (rec_pos + 6 <= REC_LEN) { uint32_t d = eclock() - t_flush; test.flush += d; if (test.n < TEST_FRAMES) test.fl[test.n] = d; }
    else {                                      /* the recording replayed: the figures out, done */
      FILE *f = fopen(AMIGA_RESULT, "w");
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
  cell(x, y, amiga_game_glyph[shadow_vram[(uint16_t)y * SCREEN_W + x]], (uint8_t)(8 + player));
}

void plat_hud_colour(uint8_t x, uint8_t n, uint8_t player, uint8_t blank) {
  (void)blank;
  for (; n; n--, x++) cell(x, HUD_ROW, amiga_game_glyph[shadow_vram[HUD_ROW * SCREEN_W + x]], (uint8_t)(8 + player));
}

#ifdef AMIGA_NETTEST
/* A trap handler for the test builds: a CPU exception (an address error,
 * an illegal instruction) shows its number and the faulting PC as hex on
 * the screen's top row (written into the bitplanes: no OS calls here) and
 * halts, for a screenshot. The 68000's frame after the trap number: for a
 * bus or address error (2, 3) a function word, the address, the
 * instruction, then SR and PC (PC at 14); for the others SR and PC (PC at
 * 6). */
void crash_show(uint32_t trap, uint32_t pc0, uint32_t pc1, uint32_t *usp);   /* called from amiga_trap (assembly) */
void crash_show(uint32_t trap, uint32_t pc0, uint32_t pc1, uint32_t *usp) {
  uint32_t v[20];                          /* in binary: the title font has no glyph for every hex digit */
  uint8_t k, i;
  v[0] = trap; v[1] = pc0; v[2] = pc1;
  for (k = 3; k < 20; k++) v[k] = usp[k - 3];   /* the user stack from its pointer: return addresses among them */
  for (k = 0; k < 20; k++)
    for (i = 0; i < 32; i++) cell(i, k, amiga_title_glyph[(uint8_t)('0' + ((v[k] >> (31 - i)) & 1))], 7);
}
extern void amiga_trap(void);
asm("\n"
    "        .text\n"
    "        .globl  _amiga_trap\n"
    "_amiga_trap:\n"
    "        move.l  usp,a0\n"
    "        move.l  a0,-(sp)\n"           /* the user stack pointer */
    "        move.l  14+4(sp),-(sp)\n"     /* PC if a bus or address error */
    "        move.l  6+8(sp),-(sp)\n"      /* PC if another exception */
    "        move.l  12(sp),-(sp)\n"       /* the trap number */
    "        jsr     _crash_show\n"
    "1:      bra.s   1b\n");
#endif

/* ---- start and end ---- */
static void cleanup(void) {
  if (vb_int.is_Code) RemIntServer(INTB_VERTB, &vb_int);
  if (have_audio) { CUSTOM.dmacon = (uint16_t)(1 << audio_ch); CloseDevice((struct IORequest *)audio_io); }
  if (audio_io) DeleteIORequest((struct IORequest *)audio_io);
  if (audio_port) DeleteMsgPort(audio_port);
  if (sample) FreeMem(sample, SAMPLE_BYTES);
  if (window) { ClearPointer(window); CloseWindow(window); }
  if (blank_pointer) FreeMem(blank_pointer, 12);
  if (screen) CloseScreen(screen);
  if (timer_req) { CloseDevice((struct IORequest *)timer_req); DeleteIORequest((struct IORequest *)timer_req); }
  if (timer_port) DeleteMsgPort(timer_port);
  if (GfxBase) CloseLibrary((struct Library *)GfxBase);
  if (IntuitionBase) CloseLibrary((struct Library *)IntuitionBase);
#ifdef AMIGA_PLAYTEST
  if (playlog) fclose(playlog);
#endif
#ifdef AMIGA_NETTEST
  if (netlog) { log_out(); fclose(netlog); }
#endif
}

void plat_init(void) {
  static UBYTE channels[4] = {1, 2, 4, 8};  /* any one channel */
  uint8_t i;
  struct EClockVal ev;
  atexit(cleanup);
  ((struct Process *)FindTask(NULL))->pr_WindowPtr = (APTR)-1;   /* no requesters from DOS or libraries: errors come back as codes */
  IntuitionBase = (struct IntuitionBase *)OpenLibrary("intuition.library", 37);
  GfxBase = (struct GfxBase *)OpenLibrary("graphics.library", 37);
  if (!IntuitionBase || !GfxBase) exit(20);
  timer_port = CreateMsgPort();
  timer_req = (struct timerequest *)CreateIORequest(timer_port, sizeof(struct timerequest));
  if (!timer_req || OpenDevice("timer.device", UNIT_ECLOCK, (struct IORequest *)timer_req, 0)) exit(20);
  TimerBase = timer_req->tr_node.io_Device;
  eclock_hz = ReadEClock(&ev);
  ntsc = (GfxBase->DisplayFlags & PAL) == 0;
  height = ntsc ? 200 : 256;                     /* the whole display: nothing shows through below the field */
  screen = OpenScreenTags(NULL, SA_Width, 320, SA_Height, height, SA_Depth, 4, SA_Quiet, TRUE, SA_ShowTitle, FALSE,
                          SA_Title, (ULONG)"BomberNet", TAG_DONE);
  if (!screen) exit(20);
  LoadRGB4(&screen->ViewPort, (UWORD *)amiga_palette, 16);
  bpr = screen->BitMap.BytesPerRow;
  for (i = 0; i < 4; i++) {
    memset(screen->BitMap.Planes[i], 0, (size_t)bpr * height);
    plane[i] = (uint8_t *)screen->BitMap.Planes[i] + (uint32_t)((height - 200) / 2) * bpr;   /* the field centred */
  }
  plane_step = (uint32_t)(plane[1] - plane[0]);
  if (plane[2] - plane[1] != (long)plane_step || plane[3] - plane[2] != (long)plane_step) plane_step = 0;
  window = OpenWindowTags(NULL, WA_CustomScreen, (ULONG)screen, WA_Left, 0, WA_Top, 0, WA_Width, 320, WA_Height, height,
                          WA_Backdrop, TRUE, WA_Borderless, TRUE, WA_Activate, TRUE, WA_RMBTrap, TRUE,
                          WA_NoCareRefresh, TRUE, WA_IDCMP, IDCMP_RAWKEY, TAG_DONE);
  if (!window) exit(20);
  blank_pointer = AllocMem(12, MEMF_CHIP | MEMF_CLEAR);
  if (blank_pointer) SetPointer(window, blank_pointer, 1, 16, 0, 0);
  /* sound: a channel from audio.device, then its registers */
  audio_port = CreateMsgPort();
  audio_io = (struct IOAudio *)CreateIORequest(audio_port, sizeof(struct IOAudio));
  sample = AllocMem(SAMPLE_BYTES, MEMF_CHIP | MEMF_CLEAR);
  if (audio_io && sample) {
    audio_io->ioa_Request.io_Message.mn_Node.ln_Pri = 80;
    audio_io->ioa_Request.io_Command = ADCMD_ALLOCATE;
    audio_io->ioa_Data = channels;
    audio_io->ioa_Length = sizeof(channels);
    audio_io->ioa_AllocKey = 0;
    if (OpenDevice("audio.device", 0, (struct IORequest *)audio_io, 0) == 0) {
      uint8_t mask = (uint8_t)(uint32_t)audio_io->ioa_Request.io_Unit;
      for (audio_ch = 0; audio_ch < 3 && !(mask & (1 << audio_ch)); audio_ch++) ;
      amiga_tone_off = (uint16_t)(1 << audio_ch);
      have_audio = 1;
    }
  }
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
#ifdef AMIGA_NETTEST                     /* 1: host a room of AMIGA_NETTEST_PLAYERS on SPACE; 2: join */
  netlog = fopen("DH0:net.log", "a");           /* appended: what was logged before a crash and reboot survives */
  if (netlog) {
    fprintf(netlog, "start\n");                 /* the marker between runs */
    fflush(netlog);
    FindTask(NULL)->tc_TrapCode = (APTR)amiga_trap;
  }
  menu_mode = 1; menu_players = AMIGA_NETTEST_PLAYERS; menu_net = AMIGA_NETTEST; menu_local = AMIGA_NETTEST_LOCAL;
#endif
#ifdef AMIGA_PLAYTEST
  playlog = fopen("DH0:play.log", "w");
  menu_mode = 1; menu_players = 4;              /* a deathmatch of 4: key set A, key set B, the two joystick ports */
  menu_inputs[0] = INPUT_KBD_A; menu_inputs[1] = INPUT_KBD_B; menu_inputs[2] = INPUT_JOY1; menu_inputs[3] = INPUT_JOY2;
#endif
  last_tick = amiga_ticks;
}
