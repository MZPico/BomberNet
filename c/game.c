/* Global game state, random generator, frame timers. */
#include <stdint.h>
#include "game.h"

const uint8_t player_digit_codes[MAX_PLAYERS] = {0xae, 0xaf, 0xbc, 0xbd};
const uint8_t player_attrs[MAX_PLAYERS] = {0x40, 0x60, 0x70, 0x10};   /* green, yellow, white, blue */
player_t players[MAX_PLAYERS];
uint8_t player_count;
uint8_t menu_players = 1;
uint8_t menu_inputs[MAX_PLAYERS] = {INPUT_KBD_A, INPUT_KBD_B, INPUT_JOY1, INPUT_JOY2};
uint8_t menu_mode = GAME_COOP, game_mode = GAME_COOP;
uint8_t joy_type = JOY_NONE;
uint8_t kbd_fire_cr;
uint8_t joy_state[2];
uint16_t hi_score, time_left;
uint8_t stage;
uint8_t enemies_left, enemy_period;
uint8_t stage_cleared, exit_touched, timeout_flag;
uint8_t hit_pending, hit_spawned, hit_x, hit_y;
uint8_t bonus_x, bonus_y, bonus_present, bonus_revealed;
uint8_t exit_x, exit_y, exit_present, exit_revealed;
uint8_t enemy_anim, bomb_anim;
enemy_t enemies[ENEMY_SLOTS];
bomb_t bombs[BOMB_SLOTS];

/* [counter, period]; periods as in the original (1ECBh) */
ftimer_t tmr_player_anim = {0, 2};
ftimer_t tmr_enemy_die   = {0, 4};
ftimer_t tmr_enemy_move  = {0, 2};
ftimer_t tmr_time        = {2, 20};

static uint16_t rand_seed = 0xbc6e;
uint16_t match_seed = 0xbc6e;
uint16_t frame_no;
uint16_t state_hash;
uint8_t hash_period;
uint8_t replay_active;
uint8_t replay_keys[MAX_PLAYERS];

void rng_seed(uint16_t seed) { rand_seed = seed ? seed : 1; }

static uint16_t hh;
static void h8(uint8_t b) { hh = (uint16_t)(((hh << 1) | (hh >> 15)) ^ b) + 0x9e37; }
static void h16(uint16_t v) { h8((uint8_t)v); h8((uint8_t)(v >> 8)); }
static void hbytes(const uint8_t *p, uint16_t n) { while (n--) h8(*p++); }

void compute_state_hash(void) {
  hh = 0x5a5a;
  hbytes((const uint8_t *)players, sizeof(players));
  hbytes((const uint8_t *)bombs, sizeof(bombs));
  hbytes((const uint8_t *)enemies, sizeof(enemies));
  hbytes(map_layer, SCREEN_CELLS);
  h16(rand_seed); h16(time_left); h16(frame_no);
  h8(stage); h8(enemies_left); h8(enemy_period); h8(stage_cleared); h8(exit_touched);
  h8(timeout_flag); h8(hit_pending); h8(hit_spawned); h8(hit_x); h8(hit_y);
  h8(bonus_x); h8(bonus_y); h8(bonus_present); h8(bonus_revealed);
  h8(exit_x); h8(exit_y); h8(exit_present); h8(exit_revealed);
  h8(enemy_anim); h8(bomb_anim); h8(game_mode); h8(player_count);
  h8(tmr_player_anim.counter); h8(tmr_enemy_die.counter); h8(tmr_enemy_move.counter); h8(tmr_time.counter);
  state_hash = hh;
}

/* 16-bit xorshift (full period). The original mixed its seed with the Z80
 * R register; that does not exist on the host build, so a proper generator
 * is used instead. */
uint8_t rnd(void) {
  uint16_t v = rand_seed;
  v ^= v << 7;
  v ^= v >> 9;
  v ^= v << 8;
  rand_seed = v;
  return (uint8_t)v;
}

void tick_timer(ftimer_t *t) {
  uint8_t c = t->counter + 1;
  t->counter = (c >= t->period) ? 0 : c;
}

/* Advance all frame timers, then present the frame. */
/* tmr_explode and tmr_enemy_die are ticked by their users only (as in the
 * original 19A7h); ticking them here too made a single dying enemy never
 * reach the zero count that advances its animation. */
void tick_timers(void) {
  tick_timer(&tmr_player_anim);
  tick_timer(&tmr_enemy_move);
  tick_timer(&tmr_time);
  frame_sync();
  flush_screen();
  players_death_colour();
}

#ifndef HOST
/* Frame limiter. With an MZ-1X03 the frame is aligned to every 3rd vblank
 * (60 ms) because the stick pulses can only be timed from the VBLK edge. */
void frame_sync(void) {
  if (joy_type == JOY_1X03) {
    mz_frame_sync(FRAME_TICKS_VBLK);
    mz_wait_vblank();
    mz_joy1x03_measure();
  } else {
    mz_frame_sync(FRAME_TICKS);
  }
}
#endif
