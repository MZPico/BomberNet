/* The core on a 6502 (llvm-mos simulator): replays a recording compiled in
 * (rec_data.h, from tools/sim6502.py), checks the state hash of every game
 * frame against it and times the frames in CPU cycles: from the start of the
 * flush to the next frame sync, the work a frame does. The screen flush is
 * C (or with SIM_ASM 6502 assembly, flush.s): each changed cell's code and
 * colour into screen and colour memory, as a C64 (screen RAM + colour RAM) or
 * an Atari would take them. With SIM_NET every game frame also does a
 * network frame's work (net_send of our keys, the other slots' input lines
 * through the WebSocket parser, net_poll) against the relay in tcp_fake.c. */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "game.h"
#include "tables.h"
#include "rec_data.h"                 /* rec[]: BNR1 header and 6-byte records; REC_LEN */

#ifdef SIM_NET
#include "netdev.h"
extern uint8_t sim_slots;
#ifndef SIM_SLOTS
#define SIM_SLOTS 2
#endif
#define getenv_slots SIM_SLOTS
#endif

#define MAX_FRAMES 1200
uint8_t sim_vram[SCREEN_CELLS], sim_vattr[SCREEN_CELLS], sim_codes[256], sim_attrs[256];   /* public for flush.s */
void flush_cells(void);
#define vram sim_vram
#define vattr sim_vattr
static uint16_t pos = 16, frames, game_frames, mismatches, tones;
static uint32_t t_flush, work[MAX_FRAMES];
static uint16_t nwork;


const char *const plat_joy_names[PLAT_JOY_TYPES] = {"NONE   ", "MZ-800 ", "MZ-1X03"};
const char *const plat_kbd_a_alt_name = "CURSOR AND CR   ";
const char *plat_input_name(uint8_t input) { (void)input; return "KEYS            "; }
uint8_t plat_input_allowed(uint8_t input) { (void)input; return 1; }
uint8_t plat_keys_a(void) { return title_mode && (frames & 1) ? KEY_SPACE : 0; }   /* the title: SPACE */
uint8_t plat_keys_b(void) { return 0; }
uint8_t plat_key_char(void) { return 0; }
uint8_t plat_break(void) { return 0; }
uint8_t plat_joy(uint8_t n) { (void)n; return 0; }
void plat_tone(uint16_t ratio, uint8_t len) { (void)ratio; (void)len; tones++; }
void plat_delay(void) {}
void plat_player_colour(uint8_t x, uint8_t y, uint8_t player) { vattr[y * SCREEN_W + x] = (uint8_t)(player + 1); }
void plat_hud_colour(uint8_t x, uint8_t n, uint8_t player, uint8_t blank) {
  (void)blank;
  for (; n; n--, x++) vattr[HUD_ROW * SCREEN_W + x] = (uint8_t)(player + 1);
}

static int cmp(const void *a, const void *b) {
  uint32_t x = *(const uint32_t *)a, y = *(const uint32_t *)b;
  return x < y ? -1 : x > y;
}

static void finish(void) {
  uint16_t i, over15 = 0, over094 = 0;
  qsort(work, nwork, sizeof(work[0]), cmp);
  for (i = 0; i < nwork; i++) {
    if (work[i] > 90000UL) over15++;          /* 60 ms at 1.5 MHz (an Atari in a text mode) */
    if (work[i] > 56400UL) over094++;         /* 60 ms at 0.94 MHz (a C64) */
  }
  printf("RESULT frames %u mismatches %u tones %u\n", game_frames, mismatches, tones);
  printf("CYCLES median %lu p90 %lu p99 %lu max %lu\n", (unsigned long)work[nwork / 2],
         (unsigned long)work[nwork * 9 / 10], (unsigned long)work[nwork * 99 / 100], (unsigned long)work[nwork - 1]);
  printf("OVER at 1.5 MHz %u, at 0.94 MHz %u, of %u\n", over15, over094, nwork);
  exit(mismatches ? 1 : 0);
}

/* at every flush of a game frame: compare the hash, hand out the next keys */
static void replay_step(void) {
  uint16_t h;
  if (pos + 6 > REC_LEN) finish();
  h = rec[pos + 4] | (uint16_t)(rec[pos + 5] << 8);
#ifndef HASH_OFF
  if (h != state_hash) {
    if (mismatches < 5) printf("MISMATCH at game frame %u: file %04x, here %04x\n", game_frames, h, state_hash);
    mismatches++;
  }
#else
  (void)h;
#endif
  memcpy(replay_keys, rec + pos, 4);
#ifdef SIM_NET
  {                                           /* a network frame's work: our keys out, the others' in */
    static uint8_t mine[NET_BYTES], kv[NET_SLOTS * NET_BYTES];
    uint16_t av;
    mine[0] = rec[pos]; mine[1] = rec[pos + 1];
    net_send(game_frames + 1, mine);
    net_poll(game_frames, &av, kv);
    if (hash_period && (frame_no % hash_period) == 0) net_hash(frame_no, state_hash);
  }
#endif
  pos += 6;
  game_frames++;
}

#ifdef SIM_NET
void sim_peer_keys(uint16_t frame, uint8_t slot, uint8_t keys[4]) {   /* slot s plays the recording's players 2s, 2s+1 */
  uint16_t p = 16 + (uint16_t)frame * 6;
  memset(keys, 0, 4);
  if (p + 6 <= REC_LEN && slot < 2) { keys[0] = rec[p + 2 * slot]; keys[1] = rec[p + 2 * slot + 1]; }
}
#endif

void flush_screen(void) {
  uint16_t i;
  const uint8_t *tab = title_mode ? title_table : game_table;
  frames++;
  if (!title_mode) { t_flush = clock(); replay_step(); }
#ifdef FLUSH_NONE                             /* measurement: the frame without showing it */
  memset(draw_buf, C_SPACE, SCREEN_CELLS); (void)i; (void)tab;
  return;
#endif
#ifdef SIM_ASM
  if (!title_mode) { flush_cells(); (void)i; (void)tab; return; }
#endif
  for (i = 0; i < SCREEN_CELLS; i++) {
    uint8_t a = draw_buf[i];
    draw_buf[i] = C_SPACE;
    if (a == shadow_vram[i]) continue;
    shadow_vram[i] = a;
    vram[i] = tab[2 * a];
    vattr[i] = tab[2 * a + 1];
  }
}

void plat_frame_sync(void) {                  /* no waiting: the end of a frame's work */
  if (t_flush && nwork < MAX_FRAMES && game_frames > 30) work[nwork++] = clock() - t_flush;   /* past the first stage start */
  t_flush = 0;
}

void plat_init(void) {                        /* the recording's settings, as the PC simulator takes them */
  match_seed = rec[4] | (uint16_t)(rec[5] << 8);
  menu_mode = rec[6]; menu_players = rec[7];
  memcpy(menu_inputs, rec + 8, 4);
#ifdef HASH_OFF
  hash_period = 0;                            /* a standalone game does not hash; no checking then */
#else
  hash_period = rec[12];
#endif
  if (menu_players > 2) joy_type = JOY_800;
  replay_active = 1;
  {
    uint16_t c;
    for (c = 0; c < 256; c++) { sim_codes[c] = game_table[2 * c]; sim_attrs[c] = game_table[2 * c + 1]; }
  }
#ifdef SIM_NET
  {                                           /* a room through the real network code */
    static char code[5];
    uint8_t slot, st[2] = {0, 0};
    uint16_t seed, start;
    if (getenv_slots) sim_slots = getenv_slots;
    net_detect();
    if (net_create(BUILD_ID, sim_slots, st, 2, code, &slot) || net_ready(1, &seed, &start) || seed == 0xffff)
      printf("SIM_NET: no room\n");
    else printf("SIM_NET: room %s, %u slots, start %u\n", code, sim_slots, start);
  }
#endif
}
