/* Commodore 64: the platform layer (core/platform.h), step 0 of
 * docs/port-c64.md: the screen, the frame clock, tones, the joysticks, and
 * the test mode that replays a recording and times its frames.
 *
 * Screen: the text mode, as the MZ-700's: 40 x 25 cells, the MZ characters
 * (tables.c), one colour a cell from colour RAM on black. The screen
 * ($E800) and the character set ($F000) are in the RAM under the KERNAL ROM
 * (VIC bank 3), written only. The players are characters too, in their own
 * colours (plat_player_colour), as on the MZ.
 * Time: a raster interrupt once a TV frame (c64.s); a frame is 3 PAL
 * frames, or 4,3,4,3,4 NTSC ones. The KERNAL stays mapped; its CIA 1 timer
 * interrupt (keyboard scan, clock) is off.
 * Input: the keyboard (CIA 1's matrix, scanned here: the KERNAL's scan is
 * off) with two key sets, and the two joysticks. Key set A is the cursor
 * keys, SHIFT with them for up and left, and SPACE (RETURN when two players
 * share the keyboard); key set B is W A S D and E. Joystick 1's lines are
 * the matrix's columns: what it holds down is left out of the scan.
 * Joystick 2's lines are its rows, which a scan cannot tell apart (as in
 * most C64 games). RUN/STOP cancels, INST/DEL deletes, RESTORE does nothing. */
#include <stdint.h>
#include <string.h>
#include "game.h"
#include "tables.h"

#define REG(a) (*(volatile uint8_t *)(a))
#define SCREEN ((uint8_t *)0xe800)
#define CHARSET ((uint8_t *)0xf000)
#define COLOUR ((volatile uint8_t *)0xd800)
#define CIA1_PRA REG(0xdc00)
#define CIA1_PRB REG(0xdc01)

extern volatile uint8_t c64_ticks;        /* c64.s: +1 every TV frame */
void c64_irq(void);
void c64_nmi(void);
void c64_flush_game(void);

/* the MZ players' colours (green, yellow, white, blue; the blue light, to be
 * seen on black) */
static const uint8_t player_colours[MAX_PLAYERS] = {5, 7, 1, 14};

/* ---- the cycle clock: CIA 2's timer A counts cycles, timer B its underflows ---- */
#if defined(C64_TEST) || defined(C64_NETTIME)
uint32_t c64_cycles(void);
#define cycles c64_cycles
uint32_t c64_cycles(void) {
  uint8_t bl, bh, al, ah;
  do { bl = REG(0xdd06); bh = REG(0xdd07); ah = REG(0xdd05); al = REG(0xdd04); }
  while (bl != REG(0xdd06) || ah != REG(0xdd05));
  return ~((uint32_t)bh << 24 | (uint32_t)bl << 16 | (uint16_t)(ah << 8) | al);
}
#endif

#ifdef C64_TEST
#include "rec_data.h"                     /* a recording to replay (tools/build_c64.py --test) */
#define TEST_FRAMES ((REC_LEN - 16) / 6)
static uint16_t rec_pos = 16, frames;
static uint32_t t_start, t_flush_start;
/* read by tools/c64_test.py through VICE's monitor once done is 1 */
struct {
  uint8_t done, ntsc;
  uint16_t bad;                           /* the first record whose hash differs (its offset), 0: none */
  uint16_t late;                          /* game frames longer than 60 ms */
  uint16_t n;                             /* game frames timed */
  uint32_t flush;                         /* cycles in the game flush, all frames */
  uint16_t work[TEST_FRAMES];             /* each game frame's work in cycles (flush .. next frame sync) */
} c64_test;
#endif

/* ---- menus and input ---- */
const char *const plat_joy_names[PLAT_JOY_TYPES] = {"NONE    ", "C64     "};
const char *const plat_kbd_a_alt_name = "CURSOR AND RET  ";

const char *plat_input_name(uint8_t input) {
  switch (input) {
  case INPUT_KBD_A: return "CURSOR AND SPACE";
  case INPUT_KBD_B: return "WASD AND E      ";
  case INPUT_JOY1:  return "JOYSTICK 1      ";
  case INPUT_JOY2:  return "JOYSTICK 2      ";
  }
  return "";
}

uint8_t plat_input_allowed(uint8_t input) {
  if (input == INPUT_JOY1 || input == INPUT_JOY2) return joy_type != JOY_NONE;
  return 1;
}

/* The matrix, rows 0-7 (a 0 bit of CIA 1's port A selects one) by columns
 * 0-7 (port B, 0 bits: keys down). The keys used:
 *   row 0: INST/DEL 0, RETURN 1, CRSR right 2, CRSR down 7
 *   row 1: W 1, A 2, Z 4, S 5, E 6, left SHIFT 7
 *   row 2: R 1, D 2, C 4, F 5, T 6, X 7     row 3: Y 1, G 2, B 4, H 5, U 6, V 7
 *   row 4: I 1, J 2, M 4, K 5, O 6, N 7     row 5: P 1, L 2
 *   row 6: right SHIFT 4                    row 7: SPACE 4, Q 6, RUN/STOP 7 */
static uint8_t kb[8];                           /* keys down: 1 bits */
static void scan(void) {
  uint8_t r, sel = 0xfe, joy1;
  CIA1_PRA = 0xff;
  joy1 = (uint8_t)~CIA1_PRB;                    /* joystick 1 alone: its columns */
  for (r = 0; r < 8; r++, sel = (uint8_t)(sel << 1 | 1)) {
    CIA1_PRA = sel;
    kb[r] = (uint8_t)(~CIA1_PRB & ~joy1);
  }
  CIA1_PRA = 0xff;
}
#define DOWN(r, c) (kb[r] & (1 << (c)))

uint8_t plat_keys_a(void) {
  uint8_t k = 0, shift;
#ifdef C64_TEST
  return title_mode && (frames & 1) ? KEY_SPACE : 0;
#endif
  scan();
  shift = DOWN(1, 7) || DOWN(6, 4);
  if (DOWN(0, 2)) k |= shift ? KEY_LEFT : KEY_RIGHT;
  if (DOWN(0, 7)) k |= shift ? KEY_UP : KEY_DOWN;
  if (kbd_alt_fire ? DOWN(0, 1) : DOWN(7, 4)) k |= KEY_SPACE;
  return k;
}

uint8_t plat_keys_b(void) {
  uint8_t k = 0;
  scan();
  if (DOWN(1, 1)) k |= KEY_UP;
  if (DOWN(1, 5)) k |= KEY_DOWN;
  if (DOWN(1, 2)) k |= KEY_LEFT;
  if (DOWN(2, 2)) k |= KEY_RIGHT;
  if (DOWN(1, 6)) k |= KEY_SPACE;
  return k;
}

uint8_t plat_joy(uint8_t n) {               /* port 1 (n 0) and port 2: up, down, left, right, fire low */
  uint8_t s, k = 0;
  CIA1_PRA = 0xff;
  s = n ? CIA1_PRA : CIA1_PRB;
  if (!(s & 1)) k |= KEY_UP;
  if (!(s & 2)) k |= KEY_DOWN;
  if (!(s & 4)) k |= KEY_LEFT;
  if (!(s & 8)) k |= KEY_RIGHT;
  if (!(s & 16)) k |= KEY_SPACE;
  return k;
}

/* text entry: letters, INST/DEL deletes, RUN/STOP cancels */
static const char letters[6][8] = {             /* rows 1 to 5 and 7, by column */
  {0, 'W', 'A', 0, 'Z', 'S', 'E', 0}, {0, 'R', 'D', 0, 'C', 'F', 'T', 'X'}, {0, 'Y', 'G', 0, 'B', 'H', 'U', 'V'},
  {0, 'I', 'J', 0, 'M', 'K', 'O', 'N'}, {0, 'P', 'L', 0, 0, 0, 0, 0}, {0, 0, 0, 0, 0, 0, 'Q', 0}};
uint8_t plat_key_char(void) {
  uint8_t r, c, row;
  scan();
  if (DOWN(7, 7)) return 0x1b;
  if (DOWN(0, 0)) return 8;
  for (r = 0; r < 6; r++) {
    row = kb[r == 5 ? 7 : r + 1];
    for (c = 0; c < 8; c++)
      if ((row & (1 << c)) && letters[r][c]) return (uint8_t)letters[r][c];
  }
  return 0;
}

uint8_t plat_break(void) {                      /* RUN/STOP, quickly: row 7 alone */
  uint8_t v;
  CIA1_PRA = 0x7f;
  v = CIA1_PRB;
  CIA1_PRA = 0xff;
  return !(v & 0x80);
}

/* ---- time ---- */
static uint8_t last_tick, ntsc, ntsc_step;
static void tones_due(void);

#ifdef C64_NETTIME
/* frames in a match (tools/crossplay.py reads them): the work from one
 * frame sync to the next (the network's waits included), the wait in it,
 * frames that came late */
__attribute__((used)) struct { uint32_t busy, idle; uint16_t n, late; } c64_frames;
static uint32_t f_end;
#endif

void plat_frame_sync(void) {                    /* 60 ms: 3 PAL frames, or 4,3,4,3,4 NTSC ones (3.6) */
  uint8_t need = 3;
#ifdef C64_NETTIME
  uint32_t f_in = cycles();
  if (f_end && !title_mode) { c64_frames.busy += f_in - f_end; c64_frames.n++;
                              if ((uint8_t)(c64_ticks - last_tick) > need) c64_frames.late++; }
#endif
  if (ntsc) { need = (ntsc_step & 1) ? 3 : 4; if (++ntsc_step == 5) ntsc_step = 0; }
#ifdef C64_TEST
  if (t_start && c64_test.n < TEST_FRAMES) {
    uint32_t w = cycles() - t_start;
    c64_test.work[c64_test.n++] = w > 0xffff ? 0xffff : (uint16_t)w;
    if ((uint8_t)(c64_ticks - last_tick) > need) c64_test.late++;
  }
  t_start = 0;
#endif
  while ((uint8_t)(c64_ticks - last_tick) < need) tones_due();
  tones_due();
  last_tick = c64_ticks;
#ifdef C64_NETTIME
  f_end = cycles();
  if (!title_mode) c64_frames.idle += f_end - f_in;
#endif
}

void plat_delay(void) {
  uint8_t t = c64_ticks;
  while (c64_ticks == t) tones_due();
}

/* ---- sound: the MZ's tones on the SID, the game running on ----
 * frequency = 1108400 / ratio Hz; the SID's register is f * 2^24 / 985248
 * (PAL), so 18874368 / ratio. Voice 1 for the steps and menus (length 14),
 * voice 2 for the rest; triangle waves. */
static uint8_t tone_on, tone_end[2];
static void tones_due(void) {
  uint8_t v;
  if (!tone_on) return;
  for (v = 0; v < 2; v++)
    if ((tone_on & (1 << v)) && (int8_t)(c64_ticks - tone_end[v]) >= 0) {
      REG(0xd404 + 7 * v) = 0x10;                     /* gate off */
      tone_on &= (uint8_t)~(1 << v);
    }
}

void plat_tone(uint16_t ratio, uint8_t len) {
  uint8_t v = len == 14 ? 0 : 1;
  uint16_t f = ratio ? (uint16_t)(18874368UL / ratio) : 0;
  REG(0xd400 + 7 * v) = (uint8_t)f;
  REG(0xd401 + 7 * v) = (uint8_t)(f >> 8);
  REG(0xd404 + 7 * v) = 0x11;                         /* triangle, gate on */
  tone_end[v] = (uint8_t)(c64_ticks + 1 + (((uint16_t)len * 3) >> 6));
  tone_on |= (uint8_t)(1 << v);
}

/* ---- screen ---- */
static uint8_t mode = 0xff;

static void set_mode(uint8_t title) {           /* everything is drawn again */
  mode = title;
  memset(shadow_vram, 0xff, SCREEN_CELLS);
}

void flush_screen(void) {
  uint16_t i;
#ifdef C64_TEST
  frames++;
  if (!title_mode && rec_pos + 6 <= REC_LEN) {   /* the hash checked: a mismatch turns the border red */
    if (hash_period && (rec[rec_pos + 4] | (uint16_t)(rec[rec_pos + 5] << 8)) != state_hash && !c64_test.bad)
      c64_test.bad = rec_pos;
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
      SCREEN[i] = c64_title_code[a];
      COLOUR[i] = c64_title_colour[a];
    }
    return;
  }
#ifdef C64_TEST
  t_start = t_flush_start = cycles();
#endif
  c64_flush_game();
#ifdef C64_TEST
  if (rec_pos + 6 <= REC_LEN) c64_test.flush += cycles() - t_flush_start;
  if (c64_test.bad) REG(0xd020) = 2;              /* red: a hash differed */
  else if (rec_pos + 6 > REC_LEN && !c64_test.done) {
    REG(0xd020) = c64_test.late ? 7 : 5;          /* green, or yellow when frames were late */
    c64_test.done = 1;
  }
#endif
}

void plat_player_colour(uint8_t x, uint8_t y, uint8_t player) {
  COLOUR[y * SCREEN_W + x] = player_colours[player];
}

void plat_hud_colour(uint8_t x, uint8_t n, uint8_t player, uint8_t blank) {
  (void)blank;
  for (; n; n--, x++) COLOUR[HUD_ROW * SCREEN_W + x] = player_colours[player];
}

/* PAL has 312 lines (the raster counter reaches 311), NTSC 262 or 263 */
static uint8_t is_ntsc(void) {
  uint8_t m = 0;
  while (!(REG(0xd011) & 0x80)) ;                 /* to the lines from 256 on */
  while (REG(0xd011) & 0x80) if (REG(0xd012) > m) m = REG(0xd012);
  return m < 0x30;
}

void plat_init(void) {
  __asm__ volatile ("sei");
  REG(0xdc0d) = 0x7f; (void)REG(0xdc0d);         /* CIA 1: no timer interrupt (the KERNAL's) */
  REG(0xdd0d) = 0x7f; (void)REG(0xdd0d);         /* CIA 2: no NMI */
  ntsc = is_ntsc();
  /* the cycle clock: timer A counts cycles from $FFFF, timer B its underflows */
  REG(0xdd04) = REG(0xdd05) = REG(0xdd06) = REG(0xdd07) = 0xff;
  REG(0xdd0e) = 0x11;                            /* timer A: start, load, continuous, cycles */
  REG(0xdd0f) = 0x51;                            /* timer B: start, load, counts timer A's underflows */
  /* the screen: VIC bank 3 ($C000-$FFFF), screen $E800, characters $F000 */
  memcpy(CHARSET, c64_font, sizeof(c64_font));
  memset(SCREEN, 0, SCREEN_CELLS);
  REG(0xdd02) |= 3;
  REG(0xdd00) &= 0xfc;
  REG(0xd018) = 0xac;
  REG(0xd020) = 0; REG(0xd021) = 0;              /* black border and background */
  REG(0xd015) = 0;                               /* no sprites */
  /* the SID: full volume, both voices on at once, no release */
  memset((void *)0xd400, 0, 0x18);
  REG(0xd405) = REG(0xd40c) = 0x00;              /* attack 0, decay 0 */
  REG(0xd406) = REG(0xd40d) = 0xf0;              /* sustain 15, release 0 */
  REG(0xd418) = 0x0f;
  /* the frame clock: a raster interrupt at line 251 */
  *(void (**)(void))0x314 = c64_irq;
  *(void (**)(void))0x318 = c64_nmi;             /* RESTORE: nothing (the KERNAL would stop the program) */
  REG(0xd012) = 251;
  REG(0xd011) &= 0x7f;
  REG(0xd019) = 1;
  REG(0xd01a) = 1;
  CIA1_PRA = 0xff; REG(0xdc02) = 0xff; REG(0xdc03) = 0;   /* keyboard rows out, columns in */
  joy_type = 1;
  set_mode(1);
#ifdef C64_TEST
  c64_test.ntsc = ntsc;
  match_seed = rec[4] | (uint16_t)(rec[5] << 8);
  menu_mode = rec[6]; menu_players = rec[7];
  memcpy(menu_inputs, rec + 8, 4);
#ifdef TEST_HASH_OFF
  hash_period = 0;                               /* a standalone game: no hash, nothing checked */
#else
  hash_period = rec[12];
#endif
  replay_active = 1;
#endif
  __asm__ volatile ("cli");
  last_tick = c64_ticks;
}
