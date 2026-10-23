/* IBM PC (DOS): the platform layer (core/platform.h), docs/port-pc.md. A
 * real-mode DOS program (OpenWatcom, small model) for any PC with CGA,
 * EGA or VGA.
 *
 * Screen: mode 0Dh, 320 x 200 in 16 colours, 40 x 25 cells of 8 x 8; the
 * four bitplanes in parallel at A000:0000, 40 bytes a line. A cell is
 * written plane by plane through the Map Mask: the planes of its colour
 * get the glyph's 8 rows, the others zeros (16 memory writes, 2 port
 * writes). An earlier way, through Set/Reset and the latches (8 Bit Mask
 * writes), striped the black on a user's DOSBox-X: no latches now. The glyphs, colours and palette are the Amiga's
 * tables (the same 16-colour cells). A VGA shows the palette as it is
 * (its DAC); an EGA in a 200-line mode has only the CGA's 16 colours,
 * chosen for each of the game's by hand (ega_colours).
 *
 * A CGA: mode 4, 320 x 200 in 4 colours, the even lines at B800:0000 and
 * the odd ones at B800:2000, 80 bytes a line; a glyph's row is one word,
 * each bit two (cga_expand), masked to the colour. The title in palette 1
 * (cyan, magenta, white: the logo keeps its magenta), the game in palette
 * 0 (green, red, yellow: the field's red); the enemies all red, so that
 * red is danger and green and yellow are players: 1 green, 2 yellow, 3 and
 * 4 the same colours hollow (each block of the figure with its centre
 * cut out) and their status-line marks inverse.
 *
 * Time: the timer chip's channel 0 at about 1 kHz (the BIOS's own 18.2 Hz
 * tick still called on time), a game frame being 60 of its ticks; finer
 * times from its counter (1.19 MHz).
 *
 * Input: the keyboard's interrupt (INT 9) is the game's while it runs: the
 * make and break codes from port 60h (set 1, the same numbers as the Atari
 * ST's) keep the state of every key; the E0 prefix of an enhanced
 * keyboard's cursor keys is skipped. The game port (201h): two joysticks,
 * their axes timed against the centre found at the start.
 *
 * Sound: the speaker, which is the timer chip's channel 2: the MZ-800 has
 * the same chip, so a tone's divider is the MZ's scaled from its 1.1084 MHz
 * to the PC's 1.19318; the 1 kHz interrupt ends it on time. */
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
static volatile uint16_t tone_ms;         /* the tone's time left */
static uint8_t started;
static void (__interrupt __far *old_int9)(void);
static volatile uint8_t keydown[128];

static void __interrupt __far timer_isr(void) {
  ticks++;
  if (tone_ms && !--tone_ms) outp(0x61, inp(0x61) & 0xfc);   /* the speaker off */
  bios_acc += TICK_DIV;
  if (bios_acc < TICK_DIV) _chain_intr(old_int8);   /* carried past 65536: the BIOS's 18.2 Hz tick is due (it ends the interrupt) */
  outp(0x20, 0x20);
}

static uint32_t now(void) {               /* ticks: 32 bits read whole (a 16-bit CPU reads them in two) */
  uint32_t t;
  _disable(); t = ticks; _enable();
  return t;
}

/* the keyboard: a byte from port 60h, acknowledged (the XT's way, port 61h
 * bit 7, harmless on an AT), the interrupt ended; the BIOS never sees it */
static void __interrupt __far key_isr(void) {
  static uint8_t prefix;
  uint8_t b = inp(0x60), k = inp(0x61);
  outp(0x61, k | 0x80); outp(0x61, k);
  if (b == 0xe0 || b == 0xe1) prefix = 1;
  else {
    if (!(prefix && (b & 0x7f) == 0x2a)) keydown[b & 0x7f] = !(b & 0x80);   /* (an E0 2A: the enhanced keyboard's fake shift) */
    prefix = 0;
  }
  outp(0x20, 0x20);
}
#define K_ESC 0x01
#define K_BACKSPACE 0x0e
#define K_RETURN 0x1c
#define K_SPACE 0x39
#define K_UP 0x48
#define K_LEFT 0x4b
#define K_RIGHT 0x4d
#define K_DOWN 0x50
#define K_DELETE 0x53

#ifdef PC_PLAYTEST
static FILE *playlog;                     /* tools/pc_play.py reads it: the players' places every frame */
#endif
uint32_t pc_ms(void) { return now(); }    /* the network driver's clock */
void (*pc_net_cleanup)(void);             /* the network driver's: hung up, its UART's interrupt given back */
#ifdef PC_NETTEST
#include "netdev.h"
static FILE *netlog;                      /* C:\NET.LOG: tools/pc_match.py, tools/crossplay.py read it, the room's state every frame */
static char __far log_buf[16384];         /* formatted by hand, written every 32 frames (far: the data segment is 64 KB) */
static uint16_t log_len;
static void log_out(void) {
  static char chunk[512];
  uint16_t i = 0, n;
  while (i < log_len) {                   /* through a near buffer: fwrite takes near pointers in the small model */
    n = (uint16_t)(log_len - i > sizeof(chunk) ? sizeof(chunk) : log_len - i);
    _fmemcpy(chunk, log_buf + i, n);
    fwrite(chunk, 1, n, netlog);
    i += n;
  }
  fflush(netlog); _dos_commit(fileno(netlog));
  log_len = 0;
}
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

/* ---- menus and input ---- */
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
  uint8_t k = 0;
#ifdef PC_TEST
  return title_mode && (frames & 1) ? KEY_SPACE : 0;
#endif
#if defined(PC_NETTEST) && PC_NETTEST == 2
  {                                       /* joining: the room code from C:\CODE.TXT, once the test wrote it */
    static uint8_t poll, got;
    if (title_mode && !got && !(++poll & 15)) {
      FILE *f = fopen("C:\\CODE.TXT", "r");
      if (f) {                            /* (made before the start with a placeholder: DOSBox caches the folder's files) */
        char c[4];
        if (fread(c, 1, 4, f) == 4 && c[0] >= 'A' && c[0] <= 'Z' && c[1] >= 'A' && c[1] <= 'Z' && c[2] >= 'A' && c[2] <= 'Z'
            && c[3] >= 'A' && c[3] <= 'Z') { memcpy(net_code, c, 4); net_code[4] = 0; got = 1; }
        fclose(f);
      }
    }
  }
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

/* the game port: a write starts the four timers, each axis's bit stays 1
 * for a time that grows with the stick's position; counted in loops of
 * this code (the CPU's speed cancels out: the centre is counted the same
 * way at the start). Joystick A: axes bits 0-1, buttons 4-5; B: 2-3, 6-7. */
#define JOY_PORT 0x201
#define JOY_MAX 4000                      /* loops: an axis not back by then has no stick */
static uint16_t joy_centre[4];
static uint8_t joy_present;               /* bit n: joystick n answers */

static void joy_read(uint16_t *t) {       /* the four axes' counts (JOY_MAX for an axis that never ends) */
  uint16_t n;
  uint8_t pending = 0x0f, b, k;
  for (k = 0; k < 4; k++) t[k] = JOY_MAX;
  _disable();
  outp(JOY_PORT, 0);
  for (n = 0; n < JOY_MAX && pending; n++) {
    b = (uint8_t)(inp(JOY_PORT) & pending);
    if (b != pending) {
      for (k = 0; k < 4; k++) if ((pending & (1 << k)) && !(b & (1 << k))) t[k] = n;
      pending = b;
    }
  }
  _enable();
}

uint8_t plat_joy(uint8_t n) {             /* n 0: joystick A, 1: B */
  uint16_t t[4];
  uint8_t k = 0, b, ax = (uint8_t)(n * 2);
  if (!(joy_present & (1 << n))) return 0;
  joy_read(t);
  if (t[ax] < joy_centre[ax] / 2) k |= KEY_LEFT;
  else if (t[ax] > joy_centre[ax] + joy_centre[ax] / 2) k |= KEY_RIGHT;
  if (t[ax + 1] < joy_centre[ax + 1] / 2) k |= KEY_UP;
  else if (t[ax + 1] > joy_centre[ax + 1] + joy_centre[ax + 1] / 2) k |= KEY_DOWN;
  b = (uint8_t)~inp(JOY_PORT);            /* the buttons, low while pressed */
  if (b & (n ? 0xc0 : 0x30)) k |= KEY_SPACE;
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
static uint32_t next_frame;

void plat_frame_sync(void) {                    /* 60 ms */
#ifdef PC_TEST
  if (t_start && test.n < TEST_FRAMES) {
    test.work[test.n++] = pc_time() - t_start;
    if ((int32_t)(now() - next_frame) > 0) test.late++;
  }
  t_start = 0;
#endif
  while ((int32_t)(now() - next_frame) < 0) ;
  next_frame += FRAME_MS;
  if ((int32_t)(now() - next_frame) > FRAME_MS) next_frame = now();   /* far behind (a pause): no rush to catch up */
  {                                       /* ESC on the title screen ends the program: a new press (not one held from */
    static uint8_t esc_was;               /* cancelling a room code), on a frame nothing asked for typed keys */
    uint8_t esc = keydown[K_ESC];
    if (title_mode && esc && !esc_was && !typing) exit(0);
    esc_was = esc;
    typing = 0;
  }
}

void plat_delay(void) {                         /* a TV frame's worth: 20 ms */
  uint32_t t = now();
  while (now() - t < 20) ;
}

/* frequency = 1108400 / ratio Hz on the MZ, whose 8253 counts 1108400 Hz:
 * the PC's counts 1193182, so the divider is ratio * 1193182 / 1108400.
 * The length: len units of about 0.93 ms (len * 238 / 256 ms). */
void plat_tone(uint16_t ratio, uint8_t len) {
  uint32_t d;
#ifdef PC_NO_TONES
  return;
#endif
  if (!ratio) return;
  d = (uint32_t)ratio * 1193182UL / 1108400UL;
  if (d > 0xffff) d = 0xffff;
  _disable();
  outp(0x43, 0xb6);                             /* channel 2, both bytes, square wave */
  outp(0x42, (uint8_t)d); outp(0x42, (uint8_t)(d >> 8));
  outp(0x61, inp(0x61) | 3);                     /* its gate and the speaker on */
  tone_ms = (uint16_t)(((uint16_t)len * 238) >> 8) + 1;
  _enable();
}

/* ---- screen ---- */
static uint8_t mode = 0xff;

static uint8_t cga;                       /* the adapter is a CGA: mode 4 */
static uint16_t cga_expand[256];          /* a glyph row: each bit as a 2-bit pixel, the two bytes in memory order */
static const uint8_t cga_game_map[16] = {0, 1, 2, 2, 1, 1, 3, 3,   1, 3, 1, 3,   3, 3, 1, 1};   /* palette 0: green, red, yellow */
static const uint8_t cga_title_map[16] = {0, 1, 2, 2, 1, 1, 3, 3,  3, 1, 2, 3,   3, 3, 1, 1};   /* palette 1: cyan, magenta, white */

static void cga_cell(uint8_t x, uint8_t y, uint8_t g, uint8_t col) {
  const uint8_t *s = amiga_glyphs + ((uint16_t)g << 3);
  uint16_t mask = (uint16_t)(0x5555u * (title_mode ? cga_title_map : cga_game_map)[col & 15]);
  uint8_t marked = !title_mode && (col == 10 || col == 11);   /* players 3 and 4 */
  uint8_t hollow = marked && y < HUD_ROW, invert = marked && y == HUD_ROW;
  volatile uint16_t far *even = (volatile uint16_t far *)(vram + (uint16_t)y * 320 + x * 2);
  volatile uint16_t far *odd = (volatile uint16_t far *)(vram + 0x2000 + (uint16_t)y * 320 + x * 2);
  uint8_t l, b;
  for (l = 0; l < 8; l++) {
    b = s[l];
    if (hollow && (l == 2 || l == 6)) b &= (uint8_t)~0x22;   /* the blocks' centres: rows 2 and 6, columns 2 and 6 */
    if (invert) b = (uint8_t)~b;
    if (l & 1) { *odd = cga_expand[b] & mask; odd += 40; }
    else { *even = cga_expand[b] & mask; even += 40; }
  }
}

static void cell(uint8_t x, uint8_t y, uint8_t g, uint8_t col) {
  volatile uint8_t far *d = vram + (uint16_t)y * 320 + x;
  const uint8_t *s = amiga_glyphs + ((uint16_t)g << 3);
  uint8_t l;
  if (cga) { cga_cell(x, y, g, col); return; }
  col = col_map[col & 15];
  outp(SEQ_DATA, col);                          /* the colour's planes: the glyph (the Map Mask is selected: ega_ready) */
  for (l = 0; l < 8; l++) d[l * 40] = s[l];
  if (col != 15) {                              /* the other planes: zeros */
    outp(SEQ_DATA, (uint8_t)(~col & 15));
    for (l = 0; l < 8; l++) d[l * 40] = 0;
  }
}

static void set_mode(uint8_t title) {           /* everything is drawn again */
  mode = title;
  memset(shadow_vram, 0xff, SCREEN_CELLS);
  if (cga) {                                    /* the CGA's palette: 1 for the title, 0 for the game; bright, black behind */
    union REGS r;
    r.h.ah = 0x0b; r.h.bh = 0; r.h.bl = 0x10; int86(0x10, &r, &r);
    r.h.ah = 0x0b; r.h.bh = 1; r.h.bl = title ? 1 : 0; int86(0x10, &r, &r);
  }
}

static void ega_ready(void) {             /* the EGA's writes as cell() needs them, the Map Mask selected (nothing on a CGA): */
  if (cga) return;                        /* the CPU's byte as it is (no Set/Reset, every bit, replace, write mode 0) */
  outp(GC_INDEX, 1); outp(GC_DATA, 0);
  outp(GC_INDEX, 3); outp(GC_DATA, 0);
  outp(GC_INDEX, 5); outp(GC_DATA, 0);
  outp(GC_INDEX, 8); outp(GC_DATA, 0xff);
  outp(SEQ_INDEX, 2);
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
  ega_ready();                                  /* the latches: zeros */
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
        cell((uint8_t)(i % SCREEN_W), (uint8_t)(i / SCREEN_W), amiga_game_glyph[a],
             cga && a >= C_ENEMY_BASE && a < C_FIRE ? 2 : amiga_game_colour[a]);   /* a CGA: every enemy red */
      }
    }
    for (i = HUD_ROW * SCREEN_W; i < SCREEN_CELLS; i++) {
      a = draw_buf[i]; draw_buf[i] = C_SPACE;
      if (a == shadow_vram[i]) continue;
      shadow_vram[i] = a;
      cell((uint8_t)(i - HUD_ROW * SCREEN_W), HUD_ROW, amiga_game_glyph[a], amiga_game_colour[a]);
    }
  }
#ifdef PC_PLAYTEST
  if (playlog) {
    static uint8_t every;
    fprintf(playlog, "%u %u %u %u %u %u %u %u %u %u %u %u %u %u %u\n", frame_no, title_mode, players[0].x, players[0].y, players[0].state,
            players[1].x, players[1].y, players[1].state, players[2].x, players[2].y, players[2].state,
            players[3].x, players[3].y, players[3].state, (unsigned)(kbd_alt_fire | joy_present << 1));
    if (!(++every & 7)) { fflush(playlog); _dos_commit(fileno(playlog)); }
  }
#endif
#ifdef PC_NETTEST
  if (netlog) {                           /* the state the tools read from the other machines' memory */
    static uint8_t every;
    extern uint32_t pc_modem_tx, pc_modem_rx;
    if (log_len > sizeof(log_buf) - 2300) log_out();
    log_s("frame "); log_dec(frame_no); log_s(" title "); log_dec(title_mode);
    log_s(" code "); { uint8_t k; for (k = 0; k < 4; k++) log_buf[log_len++] = net_code[k] ? net_code[k] : ' '; }
    log_s(" active "); log_dec(net_active); log_s(" abort "); log_dec(net_abort); log_s(" slot "); log_dec(net_slot);
    log_s(" delay "); log_dec(net_delay); log_s(" table ");
    { uint8_t k; for (k = 0; k < 4; k++) log_hex(net_table[k]); }
    log_s(" hash "); log_hex((uint8_t)(state_hash >> 8)); log_hex((uint8_t)state_hash); log_s(" map ");
    if (title_mode || frame_no % 16 == 1)  /* the map: on the hashed frames' lines (what the tools compare) */
      for (i = 0; i < SCREEN_CELLS; i++) log_hex(map_layer[i]);
    else log_s("none");
    log_s(" ms "); log_dec(now()); log_s(" tx "); log_dec(pc_modem_tx); log_s(" rx "); log_dec(pc_modem_rx); log_s("\n");
    if (!(++every & 31)) log_out();
  }
#endif
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
  ega_ready();
  cell(x, y, amiga_game_glyph[shadow_vram[(uint16_t)y * SCREEN_W + x]], (uint8_t)(8 + player));
}

void plat_hud_colour(uint8_t x, uint8_t n, uint8_t player, uint8_t blank) {
  (void)blank;
  ega_ready();
  for (; n; n--, x++) cell(x, HUD_ROW, amiga_game_glyph[shadow_vram[HUD_ROW * SCREEN_W + x]], (uint8_t)(8 + player));
}

/* ---- start and end ---- */
static void cleanup(void) {
  union REGS r;
  if (!started) return;
  if (pc_net_cleanup) pc_net_cleanup();          /* hung up, the UART's interrupt given back */
  _disable();
  outp(0x43, 0x36); outp(0x40, 0); outp(0x40, 0);   /* the timer back to 18.2 Hz */
  _dos_setvect(0x08, old_int8);
  _dos_setvect(0x09, old_int9);
  outp(0x61, inp(0x61) & 0xfc);                  /* the speaker off */
  _enable();
#ifdef PC_PLAYTEST
  if (playlog) fclose(playlog);
#endif
#ifdef PC_NETTEST
  if (netlog) { log_out(); fclose(netlog); netlog = NULL; }
#endif
  if (!cga) { outp(SEQ_INDEX, 2); outp(SEQ_DATA, 0x0f); }   /* all four planes written again, as the BIOS left them */
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
  if (r.h.bh > 1) {                              /* no: a CGA, unless the BIOS's equipment word says mono (MDA, Hercules) */
    int86(0x11, &r, &r);
    if ((r.x.ax & 0x30) == 0x30) { printf("BomberNet needs a CGA, EGA or VGA card.\n"); exit(1); }
    cga = 1;
  }
  if (cga) {
    uint16_t b;
    r.x.ax = 0x0004; int86(0x10, &r, &r);       /* 320 x 200, 4 colours */
    vram = (volatile uint8_t far *)MK_FP(0xb800, 0);
    for (b = 0; b < 256; b++) {
      uint16_t w = 0;
      uint8_t k;
      for (k = 0; k < 8; k++)                    /* pixel k: the first four in the first byte, the leftmost highest */
        if (b & (0x80 >> k)) w |= (uint16_t)(3u << ((k < 4 ? 0 : 8) + 6 - 2 * (k & 3)));
      cga_expand[b] = w;
    }
  } else {
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
  ega_ready();
  }
  {                                              /* the game port: which sticks answer, and their centres */
    uint16_t t[4];
    uint8_t k;
    joy_read(t);
    for (k = 0; k < 4; k++) joy_centre[k] = t[k];
    if (t[0] < JOY_MAX && t[1] < JOY_MAX) joy_present |= 1;
    if (t[2] < JOY_MAX && t[3] < JOY_MAX) joy_present |= 2;
  }
  old_int8 = _dos_getvect(0x08);
  old_int9 = _dos_getvect(0x09);
  _disable();
  _dos_setvect(0x08, timer_isr);
  _dos_setvect(0x09, key_isr);
  outp(0x43, 0x36); outp(0x40, TICK_DIV & 0xff); outp(0x40, TICK_DIV >> 8);
  _enable();
  started = 1;
  atexit(cleanup);
  joy_type = joy_present ? 1 : JOY_NONE;
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
#ifdef PC_NETTEST                         /* 1: host a room of PC_NETTEST_PLAYERS on SPACE; 2: join */
  netlog = fopen("C:\\NET.LOG", "a");
  if (netlog) { log_s("start\n"); log_out(); }
  menu_mode = 1; menu_players = PC_NETTEST_PLAYERS; menu_net = PC_NETTEST; menu_local = PC_NETTEST_LOCAL;
#endif
#ifdef PC_PLAYTEST
  playlog = fopen("C:\\PLAY.LOG", "w");
  menu_mode = 1; menu_players = 4;              /* a deathmatch of 4: key set A, key set B, the two joysticks */
  menu_inputs[0] = INPUT_KBD_A; menu_inputs[1] = INPUT_KBD_B; menu_inputs[2] = INPUT_JOY1; menu_inputs[3] = INPUT_JOY2;
#endif
  next_frame = now() + FRAME_MS;
}
