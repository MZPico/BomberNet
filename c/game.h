/*
 * BOMBER - C port of the Sharp MZ-700 game (see ../bomber.asm for the original).
 * Shared types, constants and globals.
 *
 * Conventions kept from the original:
 *   - every screen cell holds a "logical code" (see README, Logical code map),
 *     translated to display code + attribute only when flushed to VRAM;
 *   - coordinates are (x = column 0..39, y = row 0..24); sprites are 2x2 chars;
 *   - two layers: map_layer (persistent stage) and draw_buf (rebuilt every frame).
 */
#ifndef GAME_H
#define GAME_H

#include <stdint.h>

#define SCREEN_W 40
#define SCREEN_H 25
#define SCREEN_CELLS (SCREEN_W * SCREEN_H)
#define HUD_ROW 24

/* logical codes */
#define C_SPACE      0x20
#define C_COLON      0x21
#define C_BONUS_TILE 0x0a
#define C_EXIT_TILE  0x0e
#define C_BOMB_TILE  0x60
#define C_BRICK      0x80   /* 0x80..0x87 burning stages, 0x88 => gone */
#define C_WALL       0x88
#define C_PILLAR     0x89
#define C_PLAYER_A   0x8a
#define C_PLAYER_B   0x8c
#define C_LIVES_ICON 0x90
#define C_ENEMY_ICON 0x91
#define C_HUD_P      0x92   /* HUD letters P L M A Y D H I at 92h..99h (game_table) */
#define C_HUD_L      0x93
#define C_HUD_S      0x10   /* original HUD glyphs: S C O R E B O N U S, T=30h G=31h */
#define C_HUD_T      0x30
#define C_BOX_H      0x0c   /* message frame: horizontal, vertical, corners */
#define C_BOX_V      0x0d
#define C_BOX_TL     0x1c
#define C_BOX_TR     0x1d
#define C_PLAYER_DIGIT(i) (player_digit_codes[i])   /* coloured '1'..'4' */
#define C_BOX_BL     0xac
#define C_BOX_BR     0xad
#define C_ENEMY_BASE 0xc0   /* + type*4 + anim */
#define C_FIRE       0xe0   /* >= 0xe0 is fire */

/* enemy record */
#define ENEMY_SLOTS 8
#define ENEMY_FREE  0
#define ENEMY_ALIVE 1
#define ENEMY_DYING 2       /* 2..9 animation, 10 => freed */

typedef struct {
  uint8_t state;
  uint8_t x, y;
  uint8_t type;       /* 0..3, sprite colour and behaviour */
  uint8_t countdown;  /* reloaded from enemy_period */
  uint8_t dir;        /* 0 left, 1 right, 2 up, 3 down */
} enemy_t;

/* bomb record */
#define BOMB_SLOTS   8
#define BOMB_FREE    0
#define BOMB_TICK1   1      /* 1..4 ticking */
#define BOMB_EXPLODE 5      /* 5..12 explosion phases */
#define BOMB_ERASE   13
#define BOMB_END     14

typedef struct {
  uint8_t state;
  uint8_t x, y;
  uint8_t timer;
  uint8_t owner;      /* player index, for kill credit */
} bomb_t;

/* frame timers: counter wraps to 0 on reaching period */
typedef struct {
  uint8_t counter;
  uint8_t period;
} ftimer_t;

/* player states */
#define P_STAND0 0
#define P_STAND1 1
#define P_WALK   2          /* 2..5 unused walking frames (kept for features) */
#define P_DYING  6          /* 6..13 */
#define P_DEAD   13

/* input sources */
#define INPUT_NONE  0
#define INPUT_KBD_A 1       /* cursor keys + SPACE */
#define INPUT_KBD_B 2       /* second key set (phase 1) */
#define INPUT_JOY1  3
#define INPUT_JOY2  4
#define INPUT_NET   5

#define MAX_PLAYERS 4

/* game modes */
#define GAME_COOP 0
#define GAME_DM   1         /* deathmatch: no enemies, last one standing */
#define DM_ROUNDS_TO_WIN 3

typedef struct {
  uint8_t active;      /* takes part in the match */
  uint8_t input;       /* INPUT_* */
  uint8_t keys;        /* key mask sampled this frame (the only input the logic sees) */
  uint8_t x, y;
  uint8_t state;       /* P_* */
  uint8_t anim;        /* 0/2 */
  uint8_t death_tick;  /* per-player death animation counter */
  uint8_t life_lost;   /* death animation finished */
  uint8_t lives;
  uint16_t score;
  uint8_t tile_a, tile_b;   /* standing frames: logical tile codes */
  uint8_t kills;       /* deathmatch: kills this round */
  uint8_t wins;        /* deathmatch: rounds won */
} player_t;

/* ---- globals (game.c) ---- */
extern const uint8_t player_digit_codes[MAX_PLAYERS];
extern const uint8_t player_attrs[MAX_PLAYERS];   /* VRAM attribute of each player's colour */
extern player_t players[MAX_PLAYERS];
extern uint8_t player_count;
extern uint8_t menu_players;              /* chosen on the title screen */
extern uint8_t menu_inputs[MAX_PLAYERS];  /* INPUT_* per player, chosen on the title screen */
extern uint8_t menu_mode, game_mode;      /* GAME_COOP / GAME_DM */
/* joystick types */
#define JOY_NONE 0
#define JOY_800  1          /* MZ-800/MZ-1500 digital sticks on ports F0h/F1h */
#define JOY_1X03 2          /* MZ-700 (and MZ-1500) analogue MZ-1X03 on E008h, timed at VBLK */
extern uint8_t joy_type;
extern uint8_t joy_state[2];              /* MZ-1X03: key masks measured in the frame sync */
extern uint16_t hi_score, time_left;
extern uint8_t stage;
extern uint8_t enemies_left, enemy_period;
extern uint8_t stage_cleared, exit_touched, timeout_flag;
extern uint8_t hit_pending, hit_spawned, hit_x, hit_y;
extern uint8_t bonus_x, bonus_y, bonus_present, bonus_revealed;
extern uint8_t exit_x, exit_y, exit_present, exit_revealed;
extern uint8_t enemy_anim, bomb_anim;
extern enemy_t enemies[ENEMY_SLOTS];
extern bomb_t bombs[BOMB_SLOTS];
extern ftimer_t tmr_player_anim, tmr_enemy_die, tmr_enemy_move, tmr_time;

/* ---- video.c ---- */
extern uint8_t draw_buf[SCREEN_CELLS];
extern uint8_t map_layer[SCREEN_CELLS];
extern uint8_t shadow_vram[SCREEN_CELLS];
extern uint8_t title_mode;   /* 1: flush translates through title_table (all 256 codes) */

extern const uint16_t row_off[SCREEN_H];   /* y * 40 */
#ifdef HOST
uint8_t *draw_at(uint8_t x, uint8_t y);       /* bounds-checked on the host */
uint8_t *map_at(uint8_t x, uint8_t y);
#else
#define draw_at(x, y) (draw_buf + row_off[(y)] + (x))
#define map_at(x, y)  (map_layer + row_off[(y)] + (x))
#endif
void put_tile(uint8_t *p, uint8_t code);      /* code, code+1 / code+16, code+17 */
void fill_2x2(uint8_t *p, uint8_t code);
uint8_t is_2x2_clear(const uint8_t *p);       /* returns 0x20 or the blocking code */
void print_string(uint8_t *p, const char *s);
void print_num2(uint8_t *p, uint8_t v);
void print_num5(uint8_t *p, uint16_t v);       /* 5 digits + fixed trailing 0 */
void print_num4(uint8_t *p, uint16_t v);       /* 4 digits + fixed trailing 0 (compact HUD) */
void hud_text(uint8_t *p, const char *s);      /* ASCII A-Z/0-9/space -> game-mode glyphs */
void title_text(uint8_t *p, const char *s);    /* ASCII -> title-mode codes (digits, '-', letters) */
void title_text_hl(uint8_t *p, const char *s); /* same, letters in yellow (hl_letters) */
/* title-mode box glyphs (title_table) */
#define T_BOX_H  0x0c
#define T_BOX_TL 0x10
#define T_BOX_TR 0x11
#define T_BOX_BL 0x12
#define T_BOX_BR 0x13
#define T_BOX_V  0x15
#define T_ARR_DOWN 0x21
#define T_ARR_UP   0x22
#define T_ARR_RIGHT 0x23
#define T_ARR_LEFT  0x24
void clear_map(void);
void clear_buffers(void);

/* ---- mzio.c / host ---- */
#define KEY_UP    0x01
#define KEY_DOWN  0x02
#define KEY_RIGHT 0x04
#define KEY_LEFT  0x08
#define KEY_SPACE 0x10
extern uint8_t kbd_fire_cr;                   /* 1: keyboard set A fires with CR instead of SPACE */
uint8_t mz_keys(void);                        /* keyboard set A: cursor keys + SPACE (or CR) */
uint8_t mz_keys_b(void);                      /* keyboard set B: W A S D + E */
uint8_t mz_joy(uint8_t n);                    /* joystick 0/1 as a key mask (type from joy_type) */
uint8_t mz_joy800(uint8_t n);                 /* raw read of port F0h/F1h as a key mask */
void mz_joy1x03_measure(void);                /* at the VBLK edge: fill joy_state[] for both sticks */
void mz_wait_vblank(void);                    /* wait for the next VBLK falling edge (8255 PC7) */

/* ---- input.c ---- */
void input_poll(void);                        /* sample every active player's source into .keys */
uint8_t players_alive(void);                  /* active players with state < P_DYING */
uint8_t players_finished(void);               /* 1 when no active player is still alive or dying */
void mz_tone(uint16_t ratio, uint8_t len);    /* monitor MSTA/MSTP, len*256 loops */
void mz_delay(void);
/* frame limiter: 8253 counter 1 runs at 15611 Hz; the original game frame
 * measured 58.6 ms (208k cycles) in play, i.e. 915 ticks */
#define FRAME_TICKS 915
#define FRAME_TICKS_VBLK 880                  /* then wait for the vblank edge: frame = 3 vblanks */
void mz_timer_init(void);                     /* program counter 1, take first stamp */
void mz_frame_sync(uint16_t ticks);           /* wait until ticks passed since last sync */
void frame_sync(void);                        /* limiter + joystick sampling (game.c) */
void flush_screen(void);                      /* draw_buf -> VRAM diff, clears draw_buf */
void mz_set_attr(uint8_t x, uint8_t y, uint8_t attr);   /* direct write to the attribute plane */
void composite_map(void);                     /* non-space map cells over draw_buf */

/* ---- determinism (game.c) ----
 * The simulation is a pure function of (match_seed, per-frame input vector).
 * state_hash covers every variable the logic depends on; it is recomputed at
 * the end of each frame when hash_period != 0 and frame_no % hash_period == 0.
 * With replay_active the input vector comes from replay_keys[] instead of the
 * hardware (host replay, emulator harness, and later the network). */
extern uint16_t match_seed;               /* seeds the RNG at run_game */
extern uint16_t frame_no;                 /* game frames since run_game */
extern uint16_t state_hash;
extern uint8_t hash_period;               /* 0 = never hash */
extern uint8_t replay_active;
extern uint8_t replay_keys[MAX_PLAYERS];
void rng_seed(uint16_t seed);
void compute_state_hash(void);

/* ---- network match (net.c, phase 6) ----
 * Lockstep: every input_poll is one step; the local player's keys are sent
 * for step N+NET_DELAY and the step's input vector is awaited from the
 * device, then copied into players[].keys like a replay. */
#define BUILD_ID  0x0601          /* bump on any change of the simulation or protocol */
#define NET_OFF   0
#define NET_HOST  1
#define NET_JOIN  2
#define NET_DELAY 2
extern uint8_t menu_net;          /* NET_OFF / NET_HOST / NET_JOIN (title) */
extern uint8_t net_active;        /* a lockstep match is running */
extern uint8_t net_slot, net_slots, net_waiting;
extern uint8_t net_abort;         /* 0 none, else NETST_DESYNC / NETST_DROPPED / 9 link lost */
extern char net_code[5];
uint8_t input_read(uint8_t source);
void net_match_start(uint8_t local_input);   /* prime the first NET_DELAY steps */
void net_lockstep_poll(void);                /* one step: fills players[].keys */
void net_match_end(void);

/* ---- util ---- */
uint8_t rnd(void);
void tick_timer(ftimer_t *t);
void tick_timers(void);

/* ---- map.c ---- */
void map_walls(void);                          /* outer wall + pillars into map_layer */
void generate_map(void);                       /* also places the active players */
void cell_to_screen(uint8_t *gx, uint8_t *gy);

/* ---- enemy.c ---- */
void clear_enemies(void);
void spawn_enemies(void);
void enemy_ai(void);
void draw_enemies(void);
void spawn_from_hit(void);

/* ---- bomb.c ---- */
void clear_bombs(void);
void place_bombs(void);
uint8_t bomb_owner_near(uint8_t x, uint8_t y);  /* owner of the closest exploding bomb */
void update_bombs(void);
void draw_bombs(void);

/* ---- player.c ---- */
void players_setup(uint8_t count);            /* activate players 0..count-1 with default inputs */
void players_stage_reset(void);               /* standing, alive, at their start positions */
void player_killed(player_t *p);              /* deathmatch credit when a player starts dying */
void players_death_colour(void);              /* after flush: dying sprites keep the player colour */
void draw_players(void);
void players_anim_step(void);
void check_pickups(void);
uint8_t nearest_player(uint8_t x, uint8_t y);  /* index of the closest alive player */
void reveal_bonus(void);
void reveal_exit(void);
void draw_bonus(void);
void draw_exit(void);

#endif
