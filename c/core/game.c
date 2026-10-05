/* Global game state, random generator, frame timers. */
#include <stdint.h>
#include "game.h"

const uint8_t player_digit_codes[MAX_PLAYERS] = {0xae, 0xaf, 0xbc, 0xbd};
player_t players[MAX_PLAYERS];
uint8_t player_count;
uint8_t menu_players = 1;
uint8_t menu_local = 1;
uint8_t menu_inputs[MAX_PLAYERS] = {INPUT_KBD_A, INPUT_KBD_B, INPUT_JOY1, INPUT_JOY2};
uint8_t menu_mode = GAME_COOP, game_mode = GAME_COOP;
uint8_t joy_type = JOY_NONE;
uint8_t kbd_alt_fire;
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

/* Canonical inactive hit coordinates at match seeding (included in hashes). */
void rng_seed(uint16_t seed) {
  rand_seed = seed ? seed : 1;
#ifdef ESP_FAST128
  hit_x = hit_y = 0;
#endif
}

/* hh = rotl16(hh) ^ byte + 9E37h per byte. The byte loop is assembly on the
 * Z80 (hash_run in the platform layer, over hash_ptr/hash_n): the C version at ~200 T
 * per byte cost three frames per hash and showed as a hitch every 16 frames. */
uint16_t hh;
const uint8_t *hash_ptr;
uint16_t hash_n;
#ifdef ESP_FAST128
static void h8(uint8_t b) __naked {
 __asm
    pop bc
    pop hl
    push hl
    push bc
    ld a,l
    ld de,(_hh)
    sla e
    rl d
    jr nc,h11_nocarry
    inc e
h11_nocarry:
    xor e
    add a,0x37
    ld e,a
    ld a,d
    adc a,0x9e
    ld d,a
    ld (_hh),de
    ret

 __endasm;
}
#else
static void h8(uint8_t b) { hh = (uint16_t)(((hh << 1) | (hh >> 15)) ^ b) + 0x9e37; }
#endif
static void h16(uint16_t v) { h8((uint8_t)v); h8((uint8_t)(v >> 8)); }
static void hbytes(const uint8_t *p, uint16_t n) {
#ifdef PLAT_ASM_HASH
  hash_ptr = p; hash_n = n; hash_run();
#else
  while (n--) h8(*p++);
#endif
}

/* The map (1000 bytes) is not hashed in one go: hash_frame_step hashes a
 * few rows every frame of the period into map_acc, and the hash frame folds
 * the accumulator in with the small records. Same schedule on every device,
 * so the hashes still compare; any divergence shows within one period. */
static uint16_t map_acc = 0x5a5a;
static void hash_scalars(void);

static void hash_records(void) {
  hbytes((const uint8_t *)players, sizeof(players));
  hbytes((const uint8_t *)bombs, sizeof(bombs));
  hbytes((const uint8_t *)enemies, sizeof(enemies));
}

/* every frame after frame_no++ (hash_period != 0): the period's slice */
#ifdef ESP_FAST128
void hash_frame_step_general(void)
#else
void hash_frame_step(void)
#endif
{
  uint8_t k = frame_no % hash_period, r0, r1;
  if (hash_period < 2) { compute_state_hash(); return; }
  if (k == 0) {                       /* hash frame: records + accumulated map */
    hh = 0x5a5a;
    hash_records();
    h16(map_acc);
    hash_scalars();
    state_hash = hh;
    map_acc = 0x5a5a;
    return;
  }
  r0 = (uint8_t)(((k - 1) * SCREEN_H) / (hash_period - 1));
  r1 = (uint8_t)((k * SCREEN_H) / (hash_period - 1));
  if (r1 > r0) {
    hh = map_acc;
    hbytes(map_layer + r0 * SCREEN_W, (uint16_t)(r1 - r0) * SCREEN_W);
    map_acc = hh;
  }
}

#ifdef ESP_FAST128
/* Common 16-frame schedule, using exactly the original 25-row slices. */
void hash_frame_step(void) __naked {
 __asm
    ld a,(_hash_period)
    cp 16
    jp nz,_hash_frame_step_general
    ld a,(_frame_no)
    and 15
    jp z,_hash_frame_step_general
    dec a
    ld e,a
    add a,a
    add a,e
    ld e,a
    ld d,0
    ld hl,hf_slices
    add hl,de
    ld e,(hl)
    inc hl
    ld d,(hl)
    inc hl
    ld a,(hl)
    ld hl,0
    ld l,a
    ld (_hash_n),hl
    ld (_hash_ptr),de
    ld hl,(_map_acc)
    ld (_hh),hl
    call _hash_run
    ld hl,(_hh)
    ld (_map_acc),hl
    ret
hf_slices:
    defw _map_layer+0
    defb 40
    defw _map_layer+40
    defb 80
    defw _map_layer+120
    defb 80
    defw _map_layer+200
    defb 40
    defw _map_layer+240
    defb 80
    defw _map_layer+320
    defb 80
    defw _map_layer+400
    defb 40
    defw _map_layer+440
    defb 80
    defw _map_layer+520
    defb 80
    defw _map_layer+600
    defb 40
    defw _map_layer+640
    defb 80
    defw _map_layer+720
    defb 80
    defw _map_layer+800
    defb 40
    defw _map_layer+840
    defb 80
    defw _map_layer+920
    defb 80
  __endasm;
}
#endif

/* full hash of the state right now (match start, hash_period 1) */
void compute_state_hash(void) {
  hh = 0x5a5a;
  hash_records();
  hbytes(map_layer, SCREEN_CELLS);
  hash_scalars();
  state_hash = hh;
  map_acc = 0x5a5a;
}

static void hash_scalars(void) {
  h16(rand_seed); h16(time_left); h16(frame_no);
  h8(stage); h8(enemies_left); h8(enemy_period); h8(stage_cleared); h8(exit_touched);
  h8(timeout_flag); h8(hit_pending); h8(hit_spawned); h8(hit_x); h8(hit_y);
  h8(bonus_x); h8(bonus_y); h8(bonus_present); h8(bonus_revealed);
  h8(exit_x); h8(exit_y); h8(exit_present); h8(exit_revealed);
  h8(enemy_anim); h8(bomb_anim); h8(game_mode); h8(player_count);
  h8(tmr_player_anim.counter); h8(tmr_enemy_die.counter); h8(tmr_enemy_move.counter); h8(tmr_time.counter);
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

#ifdef ESP_FAST128
void tick_timer(ftimer_t *t) __naked {
 __asm
    pop af
    pop hl
    push hl
    push af
    ld a,(hl)
    inc a
    inc hl
    cp (hl)
    jr c,t14_store
    xor a
t14_store:
    dec hl
    ld (hl),a
    ret
 __endasm;
}
#else
void tick_timer(ftimer_t *t) {
  uint8_t c = t->counter + 1;
  t->counter = (c >= t->period) ? 0 : c;
}
#endif

/* Advance all frame timers, then present the frame. */
/* tmr_explode and tmr_enemy_die are ticked by their users only (as in the
 * original 19A7h); ticking them here too made a single dying enemy never
 * reach the zero count that advances its animation. */
void tick_timers(void) {
  tick_timer(&tmr_player_anim);
  tick_timer(&tmr_enemy_move);
  tick_timer(&tmr_time);
  plat_frame_sync();
#ifdef ESP_FAST128
  extern uint8_t esp_defer_present;
  if (!net_active || !esp_defer_present) {
#endif
  flush_screen();
  players_death_colour();
#ifdef ESP_FAST128
  }
#endif
}
