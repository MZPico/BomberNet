/*
 * Host-side simulator: replaces mzio.c so the game logic can run on a PC.
 *
 *   cd c && cc -O1 -g -fsanitize=address,undefined -DHOST -Dmain=game_main \
 *        -I. host/sim.c video.c data.c game.c map.c enemy.c bomb.c player.c bomber.c -o build/sim
 *   build/sim [frames] [seed]
 *
 * A key bot presses SPACE on the title, then walks in random directions and
 * drops bombs. The screen is dumped as text at a few points and at the end.
 */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdint.h>
#include "game.h"
#include "data.h"

static unsigned long frames, budget = 20000, tones, vram_writes;

/* ---- record / replay (SIM_RECORD=file, SIM_REPLAY=file) ----
 * File: 16-byte header "BNR1", seed lo/hi, mode, players, inputs[4], hash_period,
 * pad; then one record per game frame (flush with title_mode == 0):
 * keys[4] fed to the frame that follows, then state_hash lo/hi as it stood at
 * that flush (= hash at the end of the previous frame). */
static FILE *rec_out, *rec_in;
static unsigned long rec_frames, mismatches;
static void record_replay_step(void);
static uint8_t vram[SCREEN_CELLS], vattr[SCREEN_CELLS];
static unsigned max_stage, deaths, exits, cleared, kills, bricks_burnt;
static uint8_t last_lives, last_stage, last_enemies;

/* ---- scripted deathmatch scenario (SIM_DM=1): P1 bombs P2 in the corner ---- */
static int scenario;
static uint8_t scn_keys(void) {
  if (title_mode) {
    if (frames == 2) return KEY_RIGHT;                    /* MODE -> deathmatch */
    return (frames >= 6 && frames < 9) ? KEY_SPACE : 0;
  }
  if (frames == 12) {                                     /* place P2 next to P1, open a lane down */
    unsigned r;
    players[1].x = 5; players[1].y = 1;
    for (r = 1; r < 12; r++) memset(map_layer + r * SCREEN_W + 1, C_SPACE, 4);
    return KEY_SPACE;
  }
  if (frames > 12 && frames < 40) return KEY_DOWN;
  if (frames > 230 && frames < 236) return KEY_SPACE;     /* leave the result box */
  return 0;
}

/* ---- key bot ---- */
static uint8_t bot_keys, bot_hold;

uint8_t mz_keys(void) {
  if (scenario) return scn_keys();
  if (title_mode) return (frames & 1) ? KEY_SPACE : 0;   /* press/release SPACE */
  if (bot_hold == 0) {
    uint8_t r = rand() & 15;
    bot_hold = 4 + (rand() & 31);
    bot_keys = (r < 4) ? (KEY_UP << r) : (r < 6 ? KEY_SPACE : 0);
    if (r >= 6 && r < 10) bot_keys = KEY_UP << (r - 6);
  }
  bot_hold--;
  return bot_keys;
}


uint8_t mz_keys_b(void) { return scenario ? 0 : (uint8_t)(rand() & 0x1f); }
void mz_tone(uint16_t ratio, uint8_t len) { (void)ratio; (void)len; tones++; }
void mz_delay(void) {}
uint8_t mz_joy(uint8_t n) { (void)n; return 0; }
uint8_t mz_joy800(uint8_t n) { (void)n; return 0; }
void mz_joy1x03_measure(void) {}
void mz_wait_vblank(void) {}
void frame_sync(void) {}
void mz_timer_init(void) {}
void mz_frame_sync(uint16_t t) { (void)t; }

static char glyph(uint8_t c) {
  if (c == C_SPACE) return ' ';
  if (c < 10) return '0' + c;
  if (c == C_WALL) return '#';
  if (c == C_PILLAR) return '+';
  if (c >= 0x80 && c < 0x88) return '%';
  if (c >= C_FIRE) return '*';
  if (c >= 0x60 && c < 0x70) return 'o';
  if (c >= 0xc0 && c < 0xe0) return 'E';
  if (c >= 0x8a && c < 0x8e) return 'P';
  if (c >= 0x9a && c < 0x9e) return 'P';
  if (c == 0x90) return 'L';
  if (c == 0x91) return 'e';
  if (c >= 0xa0 && c < 0xc0) return 'P';
  if ((c >= 0x0a && c < 0x0c) || (c >= 0x1a && c < 0x1c)) return 'B';
  if ((c >= 0x0e && c < 0x10) || (c >= 0x1e && c < 0x20)) return 'X';
  if (c >= 0x41 && c < 0x5b) return (char)c;
  if (c >= 0x10 && c < 0x1a) return 'h';
  if (c == C_COLON) return ':';
  if (c == C_BOX_H) return '-';
  if (c == C_BOX_V) return '|';
  if (c == C_BOX_TL || c == C_BOX_TR || c == C_BOX_BL || c == C_BOX_BR) return '+';
  if (c >= 0x92 && c <= 0x99) return "PLMAYDHI"[c - 0x92];
  if (c == 0x8e) return 'W';
  if (c == 0x8f) return 'K';
  if (c >= 0x22 && c < 0x40) return '~';
  if (c >= 0x40 && c < 0x60) return 'p';
  return '?';
}

static void dump(const char *why) {
  unsigned y, x;
  printf("---- %s: frame %lu, score %u, stage %u, lives %u, enemies %u, time %u\n",
         why, frames, players[0].score, stage, players[0].lives, enemies_left, time_left);
  for (y = 0; y < SCREEN_H; y++) {
    putchar('|');
    for (x = 0; x < SCREEN_W; x++) putchar(glyph(shadow_vram[y * SCREEN_W + x]));
    puts("|");
  }
}

static void finish(void) {
  dump("end");
  printf("frames=%lu tones=%lu vram_writes=%lu max_stage=%u cleared=%u exits=%u deaths=%u kills=%u\n",
         frames, tones, vram_writes, max_stage, cleared, exits, deaths, kills);
  exit(0);
}

void flush_screen(void) {
  unsigned i;
  frames++;
  if (!title_mode && (rec_out || rec_in)) record_replay_step();
  for (i = 0; i < SCREEN_CELLS; i++) {
    uint8_t a = draw_buf[i];
    const uint8_t *t;
    draw_buf[i] = C_SPACE;
    if (a == shadow_vram[i]) continue;
    t = (title_mode ? title_table : game_table) + a * 2;
    shadow_vram[i] = a;
    vram[i] = t[0];
    vattr[i] = t[1];
    vram_writes++;
  }
  if (stage > max_stage) max_stage = stage;
  if (players[0].lives < last_lives) deaths++;
  if (exit_touched && !title_mode) exits++;
  if (stage_cleared && stage != last_stage) cleared++;
  if (enemies_left < last_enemies) kills += last_enemies - enemies_left;
  last_lives = players[0].lives; last_stage = stage; last_enemies = enemies_left;
  if (scenario) {
    static int shown;
    if (!shown && shadow_vram[9 * SCREEN_W + 8] == C_BOX_TL) { shown = 1; dump("result box"); }
    if (frames == 240) dump("after the box");
  } else {
    if (frames == 3) dump("title");
    if (frames == 40) dump("first stage frame");
    if (frames == 400) dump("in play");
  }
  if (frames >= budget) {
    if (rec_out) { fclose(rec_out); printf("recorded %lu game frames\n", rec_frames); }
    if (rec_in) printf("replay: %lu frames compared, %lu mismatches\n", rec_frames, mismatches);
    finish();
  }
}

void mz_set_attr(uint8_t x, uint8_t y, uint8_t attr) { vattr[y * SCREEN_W + x] = attr; }

static void bot_fill_keys(void) {
  unsigned i;
  for (i = 0; i < MAX_PLAYERS; i++) {
    static uint8_t hold[MAX_PLAYERS], cur[MAX_PLAYERS];
    if (!players[i].active) { replay_keys[i] = 0; continue; }
    if (hold[i] == 0) {
      uint8_t r = rand() & 15;
      hold[i] = 4 + (rand() & 31);
      cur[i] = (r < 4) ? (KEY_UP << r) : (r < 6 ? KEY_SPACE : 0);
      if (r >= 6 && r < 10) cur[i] = KEY_UP << (r - 6);
    }
    hold[i]--;
    replay_keys[i] = cur[i];
  }
}

/* called at every flush of a game frame */
static void record_replay_step(void) {
  uint8_t rec[6];
  if (rec_out) {
    bot_fill_keys();
    memcpy(rec, replay_keys, 4);
    rec[4] = (uint8_t)state_hash; rec[5] = (uint8_t)(state_hash >> 8);
    fwrite(rec, 1, 6, rec_out);
    rec_frames++;
  } else if (rec_in) {
    uint16_t h;
    if (fread(rec, 1, 6, rec_in) != 6) {
      printf("replay: end of file after %lu frames, %lu mismatches\n", rec_frames, mismatches);
      exit(mismatches ? 1 : 0);
    }
    h = rec[4] | (rec[5] << 8);
    if (h != state_hash) {
      if (mismatches < 10)
        printf("replay: HASH MISMATCH at game frame %lu (frame_no %u): file %04x, here %04x\n",
               rec_frames, frame_no, h, state_hash);
      mismatches++;
    }
    memcpy(replay_keys, rec, 4);
    rec_frames++;
  }
}

void composite_map(void) {
  unsigned i;
  for (i = 0; i < SCREEN_CELLS; i++)
    if (map_layer[i] != C_SPACE) draw_buf[i] = map_layer[i];
}

void game_main(void);

static void setup_from_env(void) {
  const char *v;
  if ((v = getenv("SIM_MODE"))) menu_mode = (uint8_t)atoi(v);
  if ((v = getenv("SIM_PLAYERS"))) menu_players = (uint8_t)atoi(v);
  if ((v = getenv("SIM_SEED"))) match_seed = (uint16_t)strtoul(v, 0, 0);
  if ((v = getenv("SIM_HASH"))) hash_period = (uint8_t)atoi(v);
  if (menu_players > 2) joy_type = JOY_800;
}

int main(int argc, char **argv) {
  const char *v;
  if (argc > 1) budget = strtoul(argv[1], 0, 10);
  scenario = getenv("SIM_DM") != 0;
  srand(argc > 2 ? atoi(argv[2]) : 1);
  setup_from_env();
  if ((v = getenv("SIM_RECORD"))) {
    uint8_t hdr[16] = {'B', 'N', 'R', '1'};
    rec_out = fopen(v, "wb");
    if (!rec_out) { perror(v); return 2; }
    if (!hash_period) hash_period = 1;
    hdr[4] = (uint8_t)match_seed; hdr[5] = (uint8_t)(match_seed >> 8);
    hdr[6] = menu_mode; hdr[7] = menu_players;
    memcpy(hdr + 8, menu_inputs, 4); hdr[12] = hash_period;
    fwrite(hdr, 1, 16, rec_out);
    replay_active = 1;
  } else if ((v = getenv("SIM_REPLAY"))) {
    uint8_t hdr[16];
    rec_in = fopen(v, "rb");
    if (!rec_in || fread(hdr, 1, 16, rec_in) != 16 || memcmp(hdr, "BNR1", 4)) { fprintf(stderr, "bad replay %s\n", v); return 2; }
    match_seed = hdr[4] | (hdr[5] << 8); menu_mode = hdr[6]; menu_players = hdr[7];
    memcpy(menu_inputs, hdr + 8, 4); hash_period = hdr[12];
    if (menu_players > 2) joy_type = JOY_800;
    replay_active = 1;
  }
  game_main();
  return 0;
}
