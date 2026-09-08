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
static uint8_t vram[SCREEN_CELLS], vattr[SCREEN_CELLS];
static unsigned max_stage, deaths, exits, cleared, kills, bricks_burnt;
static uint8_t last_lives, last_stage, last_enemies;

/* ---- scripted deathmatch scenario (SIM_DM=1): P1 bombs P2 in the corner ---- */
static int scenario;
static uint8_t scn_keys(void) {
  if (title_mode) {
    if (frames == 2) return KEY_DOWN;                     /* MODE -> deathmatch */
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
  for (i = 0; i < SCREEN_CELLS; i++) {
    uint8_t a = draw_buf[i];
    const uint8_t *t;
    draw_buf[i] = C_SPACE;
    if (a == shadow_vram[i]) continue;
    t = (title_mode && a < 0x5b) ? title_table + a * 2 : game_table + a * 2;
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
  if (frames >= budget) finish();
}

void composite_map(void) {
  unsigned i;
  for (i = 0; i < SCREEN_CELLS; i++)
    if (map_layer[i] != C_SPACE) draw_buf[i] = map_layer[i];
}

void game_main(void);

int main(int argc, char **argv) {
  if (argc > 1) budget = strtoul(argv[1], 0, 10);
  scenario = getenv("SIM_DM") != 0;
  srand(argc > 2 ? atoi(argv[2]) : 1);
  game_main();
  return 0;
}
