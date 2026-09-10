/*
 * BOMBER - main flow: title screen, stage life cycle, HUD, frame loop.
 * Frame order is the same as the original main loop (1275h) because the
 * collision rules depend on what is already in the draw buffer.
 */
#include <stdint.h>
#include "game.h"
#include "data.h"
#include <string.h>

/* per stage: enemy count, enemy behaviour-cycle period; stage 5+ uses the last row */
static const uint8_t stage_table[5][2] = {
  {1, 0x10}, {2, 0x15}, {3, 0x1a}, {4, 0x1f}, {4, 0x24},
};

static void load_stage_params(void) {
  uint8_t s = stage < 6 ? stage : 5;
  enemies_left = stage_table[s - 1][0];
  enemy_period = stage_table[s - 1][1];
  if (game_mode == GAME_DM) enemies_left = 0;   /* arena: no monsters */
}

/* ---- HUD (row 24) ---- */
/* 3-4 players: "n ddddd<man>c " per player (10 chars), time only with 3 */
static void draw_hud_compact(void) {
  uint8_t *p = draw_at(0, HUD_ROW);
  uint8_t i;
  for (i = 0; i < player_count; i++) {
    player_t *pl = &players[i];
    if (pl->score > hi_score) hi_score = pl->score;
    p[0] = C_PLAYER_DIGIT(i);
    print_num4(p + 2, pl->score);
    p[7] = C_LIVES_ICON; p[8] = (game_mode == GAME_DM) ? pl->wins : pl->lives;
    p += 10;
  }
  if (player_count < 4) {
    p[0] = C_HUD_T;
    print_num5(p + 1, time_left);
    p[6] = C_SPACE;
    p[7] = C_ENEMY_ICON; p[8] = enemies_left;
  }
}

/* multiplayer HUD: "P1 000000 <man>3  P2 000000 <man>3  T0970 <enemy>1 S01" */
static void draw_hud_multi(void) {
  uint8_t *p = draw_at(0, HUD_ROW);
  uint8_t i;
  if (player_count > 2) { draw_hud_compact(); return; }
  for (i = 0; i < player_count; i++) {
    player_t *pl = &players[i];
    if (pl->score > hi_score) hi_score = pl->score;
    p[0] = C_HUD_P; p[1] = C_PLAYER_DIGIT(i);
    print_num5(p + 3, pl->score);
    p[10] = C_LIVES_ICON; p[11] = (game_mode == GAME_DM) ? pl->wins : pl->lives;
    p += 13;
  }
  p[0] = C_HUD_T;
  print_num5(p + 1, time_left);
  p[6] = C_SPACE;                     /* drop the fixed trailing 0 */
  p[7] = C_ENEMY_ICON; p[8] = enemies_left;
  p[10] = C_HUD_S; print_num2(p + 11, stage);
}

static void draw_hud(void) {
  uint8_t *p = draw_at(0, HUD_ROW);
  uint16_t score = players[0].score;
  if (player_count > 1) { draw_hud_multi(); return; }
  print_string(p, str_hud_score);
  print_string(p + 13, str_hud_bonus);
  print_string(p + 33, str_hud_stage);
  print_num2(p + 37, stage);
  if (score > hi_score) hi_score = score;
  print_num5(p + 6, score);
  print_num5(p + 19, time_left);
  p[24] = C_SPACE;                    /* blank the fixed 6th digit of the time */
}

static void draw_hud_icons(void) {
  uint8_t *p = draw_at(25, HUD_ROW);
  if (player_count > 1) return;
  p[0] = C_LIVES_ICON; p[1] = C_COLON; p[2] = players[0].lives;
  p[5] = C_ENEMY_ICON; p[6] = C_COLON; p[7] = enemies_left;
}

/* Every 20 frames: time -= 10. At zero the bricks and items vanish. */
static void time_tick(void) {
  if (tmr_time.counter != 0) return;
  if (time_left) { time_left -= 10; return; }
  if (timeout_flag) return;
  clear_map();
  map_walls();
  timeout_flag = 1;
  exit_present = 0;
  bonus_present = 0;
  bonus_revealed = 1;
  exit_revealed = 1;
  bonus_y = 0;
  exit_x = 0;
}

/* ---- frames ---- */
static void frame_common(void) {
  tick_timers();                      /* also presents the previous frame */
  draw_hud();
  draw_hud_icons();
  composite_map();
}

static void frame(void) {
  frame_common();
  input_poll();
  update_bombs();
  draw_bombs();
  place_bombs();
  players_anim_step();
  enemy_ai();
  draw_bonus();
  draw_exit();
  draw_enemies();
  reveal_bonus();
  reveal_exit();
  draw_players();
  spawn_from_hit();
  check_pickups();
  time_tick();
  frame_no++;
  if (hash_period && (frame_no % hash_period) == 0) compute_state_hash();
}

/* animations only: no input, no AI */
static void frame_no_input(void) {
  frame_common();
  draw_bombs();
  draw_bonus();
  draw_exit();
  draw_enemies();
  draw_players();
  update_bombs();
}

static void frame_minimal(void) {
  frame_common();
  draw_bonus();
  draw_exit();
  draw_players();
}

static void idle_frames(uint8_t n) {
  while (n--) frame_no_input();
}

/* ---- title screen ---- */
static void title_init(void) {
  uint8_t i;
  static const uint8_t demo_x[4] = {0x0b, 0x13, 0x1a, 0x22};
  title_mode = 1;
  clear_buffers();
  clear_enemies();
  for (i = 0; i < 4; i++) {
    enemies[i].state = ENEMY_ALIVE;
    enemies[i].x = demo_x[i];
    enemies[i].y = 7;
    enemies[i].type = i;
  }
  clear_bombs();
}

/* ---- title menu: UP/DOWN pick a row, LEFT/RIGHT change its value ----
 * rows: MODE, PLAYERS, JOYSTICK, then one input row per player */
static uint8_t menu_item, menu_prev_keys;
static const char *const mode_names[2] = {"COOP      ", "DEATHMATCH"};
static const char *const joy_names[3] = {"NONE   ", "MZ-800 ", "MZ-1X03"};
static const char *const input_names[6] = {
  "", "CURSOR AND SPACE", "WASD AND E      ", "JOYSTICK 1      ", "JOYSTICK 2      ", "",
};
static const char *const kbd_a_cr_name = "CURSOR AND CR   ";

/* with two keyboard players, player A fires with CR (SPACE is next to WASD);
 * applied when the game starts, the title itself always listens to SPACE */
static uint8_t menu_fire_cr;
static void update_fire_key(void) {
  uint8_t i;
  menu_fire_cr = 0;
  for (i = 0; i < menu_players; i++)
    if (menu_inputs[i] == INPUT_KBD_B) menu_fire_cr = 1;
}

static uint8_t input_allowed(uint8_t in) {
  return in == INPUT_KBD_A || in == INPUT_KBD_B ||
         (joy_type != JOY_NONE && (in == INPUT_JOY1 || in == INPUT_JOY2));
}

static uint8_t input_used(uint8_t in, uint8_t except) {
  uint8_t i;
  for (i = 0; i < menu_players; i++)
    if (i != except && menu_inputs[i] == in) return 1;
  return 0;
}

/* next allowed and unused input for player i in direction dir */
static void input_cycle(uint8_t i, int8_t dir) {
  uint8_t in = menu_inputs[i], n;
  for (n = 0; n < 4; n++) {
    in = (uint8_t)((in - 1 + 4 + dir) % 4 + 1);      /* 1..4 */
    if (input_allowed(in) && !input_used(in, i)) { menu_inputs[i] = in; return; }
  }
}

/* keep the assignment valid after MODE/PLAYERS/JOYSTICK changes */
static void menu_validate(void) {
  uint8_t i, maxp = joy_type == JOY_NONE ? 2 : 4;
  if (menu_mode == GAME_DM && menu_players < 2) menu_players = 2;
  if (menu_players > maxp) menu_players = maxp;
  for (i = 0; i < menu_players; i++)
    if (!input_allowed(menu_inputs[i]) || input_used(menu_inputs[i], i)) input_cycle(i, 1);
  update_fire_key();
}

static void menu_change(int8_t dir) {
  switch (menu_item) {
  case 0: menu_mode ^= 1; break;
  case 1:
    if (dir > 0 && menu_players < 4) menu_players++;
    if (dir < 0 && menu_players > 1) menu_players--;
    break;
  case 2: joy_type = (uint8_t)((joy_type + 3 + dir) % 3); break;
  default: input_cycle(menu_item - 3, dir); break;
  }
  menu_validate();
}

#define MENU_X 3
#define MENU_W 34
#define MENU_Y 10
#define MENU_H 9            /* frame rows 9..17: 3 fixed rows + 4 player rows */

static void draw_title_box(void) {
  uint8_t r, c;
  for (r = 0; r < MENU_H; r++) {
    uint8_t *p = draw_at(MENU_X, MENU_Y + r);
    uint8_t edge = (r == 0 || r == MENU_H - 1);
    for (c = 0; c < MENU_W; c++) p[c] = edge ? T_BOX_H : C_SPACE;
    if (!edge) { p[0] = T_BOX_V; p[MENU_W - 1] = T_BOX_V; }
  }
  *draw_at(MENU_X, MENU_Y) = T_BOX_TL;
  *draw_at(MENU_X + MENU_W - 1, MENU_Y) = T_BOX_TR;
  *draw_at(MENU_X, MENU_Y + MENU_H - 1) = T_BOX_BL;
  *draw_at(MENU_X + MENU_W - 1, MENU_Y + MENU_H - 1) = T_BOX_BR;
}

static uint8_t title_ticks;

static void menu_row(uint8_t row, const char *label, uint8_t digit, const char *value, uint8_t selected) {
  uint8_t *p = draw_at(MENU_X + 2, MENU_Y + 1 + row);
  p[0] = selected ? T_ARR_RIGHT : C_SPACE;
  title_text(p + 2, label);
  if (digit) p[2 + 7] = digit;
  if (selected) title_text_hl(p + 13, value); else title_text(p + 13, value);
}

static void title_menu(void) {
  uint8_t k = mz_keys(), i, rows, *p;
  uint8_t edge = k & ~menu_prev_keys;
  char num[2];
  menu_prev_keys = k;
  rows = 3 + menu_players;
  if ((edge & KEY_UP) && menu_item > 0) { menu_item--; mz_tone(0x020a, 14); }
  if ((edge & KEY_DOWN) && menu_item < rows - 1) { menu_item++; mz_tone(0x020a, 14); }
  if (edge & KEY_LEFT) { menu_change(-1); mz_tone(0x030a, 14); }
  if (edge & KEY_RIGHT) { menu_change(1); mz_tone(0x030a, 14); }
  if (menu_item >= rows) menu_item = rows - 1;

  draw_title_box();
  menu_row(0, "MODE", 0, mode_names[menu_mode], menu_item == 0);
  num[0] = '0' + menu_players; num[1] = 0;
  menu_row(1, "PLAYERS", 0, num, menu_item == 1);
  menu_row(2, "JOYSTICK", 0, joy_names[joy_type], menu_item == 2);
  for (i = 0; i < menu_players; i++)
    menu_row(3 + i, "PLAYER", C_PLAYER_DIGIT(i),
             (menu_inputs[i] == INPUT_KBD_A && menu_fire_cr) ? kbd_a_cr_name : input_names[menu_inputs[i]],
             menu_item == 3 + i);

  p = draw_at(9, 19);
  p[0] = T_ARR_UP; p[1] = T_ARR_DOWN; title_text(p + 3, "SELECT");
  p[12] = T_ARR_LEFT; p[13] = T_ARR_RIGHT; title_text(p + 15, "CHANGE");
  title_text(draw_at(5, 20), "HI-SCORE");
  print_num5(draw_at(14, 20), hi_score);
  title_text(draw_at(22, 20), "SCORE");
  print_num5(draw_at(28, 20), players[0].score);
  title_ticks++;
  if (title_ticks & 0x10) title_text_hl(draw_at(8, 21), "PUSH SPACE TO START GAME");
  p = draw_at(2, 23);
  title_text(p, "COPYRIGHT  C  2026  MZPICO");
  p[10] = 0x17; p[12] = 0x18;               /* the original's "(" ")" glyphs */
}

static void title_frame(void) {
  uint8_t i;
  tick_timers();
  title_menu();
  for (i = 0; i < 240; i++) draw_buf[i] = title_logo[i];
  print_string(draw_at(2, 24), str_copyright);
  print_string(draw_at(4, 7), str_legend_row7);
  print_string(draw_at(5, 8), str_legend_row8);
  put_tile(draw_at(2, 7), C_PLAYER_B);
  draw_enemies();
  if (tmr_player_anim.counter == 0) {
    bomb_anim ^= 2;
    enemy_anim ^= 2;
  }
}

/* returns when SPACE is pressed */
static void title_screen(void) {
  title_init();
  kbd_fire_cr = 0;                    /* the title starts on SPACE */
  update_fire_key();
  while (mz_keys() & KEY_SPACE) title_frame();   /* release a held SPACE first */
  do { title_frame(); } while (!(mz_keys() & KEY_SPACE));
}

/* ---- stage life cycle ---- */
static void stage_start(void) {
  time_left = 1000;
  timeout_flag = 0;
  stage_cleared = 0;
  bonus_present = exit_present = 0;
  hit_spawned = hit_pending = 0;
  bonus_revealed = exit_revealed = 0;
  exit_touched = 0;
  load_stage_params();
  clear_enemies();
  spawn_enemies();
  clear_bombs();
  clear_buffers();
  clear_map();
  map_walls();
  generate_map();
  players_stage_reset();
  composite_map();
  bonus_present = exit_present = 0;   /* hidden until their brick burns (original 1268h) */
  if (game_mode == GAME_DM) {         /* no items in the arena */
    bonus_revealed = exit_revealed = 1;
    exit_x = exit_y = 0;
  }
  if (hash_period) compute_state_hash();
  title_mode = 0;
}

static void time_bonus(void) {
  while (time_left) {
    uint8_t i;
    frame_minimal();
    mz_tone(0x0a00, 10);
    time_left -= 10;
    for (i = 0; i < MAX_PLAYERS; i++)
      if (players[i].active && players[i].state < P_DYING) players[i].score++;
  }
  frame_minimal();
  frame_minimal();
}

/* one stage; returns 0 = stage cleared, 1 = exit taken (replay), 2 = player died */
static uint8_t play_stage(void) {
  for (;;) {
    frame();
    if (players_finished()) return 2;
    if (exit_touched) return 1;
    if (stage_cleared) return 0;
  }
}

/* all active players are dead: each loses a life; the match ends when
 * nobody has lives left */
static uint8_t lose_lives(void) {
  uint8_t i, remaining = 0;
  for (i = 0; i < MAX_PLAYERS; i++) {
    player_t *p = &players[i];
    if (!p->active) continue;
    if (p->lives) p->lives--;
    if (p->lives) remaining++;
  }
  return remaining;
}

/* ---- deathmatch ---- */

/* 1 while more than one player is alive or someone is still dying */
static uint8_t round_running(void) {
  uint8_t i, alive = 0, dying = 0;
  for (i = 0; i < MAX_PLAYERS; i++) {
    player_t *p = &players[i];
    if (!p->active) continue;
    if (p->state < P_DYING) alive++;
    else if (!p->life_lost) dying++;
  }
  return alive > 1 || dying;
}

/* winner index, or 0xff for a draw: last one standing, else most kills */
static uint8_t round_winner(void) {
  uint8_t i, best = 0xff, best_kills = 0, tie = 0, alive = 0, last = 0;
  for (i = 0; i < MAX_PLAYERS; i++) {
    player_t *p = &players[i];
    if (!p->active) continue;
    if (p->state < P_DYING) { alive++; last = i; }
    if (p->kills > best_kills) { best_kills = p->kills; best = i; tie = 0; }
    else if (p->kills == best_kills && best != 0xff) tie = 1;
  }
  if (alive == 1) return last;
  return tie ? 0xff : best;
}

/* framed message box centred on the arena: columns 8..31, rows 9..14 */
#define BOX_X 8
#define BOX_W 24
#define BOX_Y 9
#define BOX_H 6

static void draw_box(void) {
  uint8_t r, c;
  for (r = 0; r < BOX_H; r++) {
    uint8_t *p = draw_at(BOX_X, BOX_Y + r);
    uint8_t edge = (r == 0 || r == BOX_H - 1);
    for (c = 0; c < BOX_W; c++) p[c] = edge ? C_BOX_H : C_SPACE;
    if (!edge) { p[0] = C_BOX_V; p[BOX_W - 1] = C_BOX_V; }
  }
  *draw_at(BOX_X, BOX_Y) = C_BOX_TL;
  *draw_at(BOX_X + BOX_W - 1, BOX_Y) = C_BOX_TR;
  *draw_at(BOX_X, BOX_Y + BOX_H - 1) = C_BOX_BL;
  *draw_at(BOX_X + BOX_W - 1, BOX_Y + BOX_H - 1) = C_BOX_BR;
}

static void box_line(uint8_t row, const char *s) {
  uint8_t len = (uint8_t)strlen(s), i;
  uint8_t *p = draw_at(BOX_X + (BOX_W - len) / 2, row);
  hud_text(p, s);
  for (i = 0; i < len; i++)                 /* '1'..'4' after "PLAYER " in colour */
    if (i >= 7 && s[i] >= '1' && s[i] <= '4' && s[i - 1] == ' ' && s[0] == 'P') p[i] = C_PLAYER_DIGIT(s[i] - '1');
}

/* framed message in the middle of the arena until fire is pressed (min 40 frames) */
static void show_message(const char *l1, const char *l2) {
  uint8_t n = 40, i, any;
  for (;;) {
    frame_minimal();
    draw_box();
    box_line(BOX_Y + 2, l1);
    box_line(BOX_Y + 3, l2);
    input_poll();
    any = 0;
    for (i = 0; i < MAX_PLAYERS; i++)
      if (players[i].active && (players[i].keys & KEY_SPACE)) any = 1;
    if (n) n--;
    else if (any) return;
  }
}

static void run_deathmatch(void) {
  char line[24];
  uint8_t w, i;
  players_setup(menu_players);
  stage = 1;
  for (;;) {
    stage_start();
    do { frame(); } while (round_running() && !timeout_flag);
    idle_frames(10);
    w = round_winner();
    if (w == 0xff) {
      show_message("DRAW", "PRESS FIRE");
    } else {
      players[w].wins++;
      strcpy(line, "PLAYER 1 WINS");
      line[7] = '1' + w;
      if (players[w].wins >= DM_ROUNDS_TO_WIN) {
        show_message(line, "THE MATCH");
        return;
      }
      show_message(line, "THE ROUND");
    }
    for (i = 0; i < MAX_PLAYERS; i++) players[i].keys = 0;
    stage++;
  }
}

static void run_game(void) {
  game_mode = menu_mode;
  kbd_fire_cr = menu_fire_cr;
  rng_seed(match_seed);
  frame_no = 0;
  tmr_player_anim.counter = tmr_enemy_die.counter = tmr_enemy_move.counter = 0;
  tmr_time.counter = 2;
  enemy_anim = bomb_anim = 0;
  if (game_mode == GAME_DM) { run_deathmatch(); return; }
  players_setup(menu_players);
  stage = 1;
  for (;;) {
    stage_start();
    switch (play_stage()) {
    case 0:
      idle_frames(20);
      time_bonus();
      stage++;
      mz_delay();
      break;
    case 1:
      idle_frames(5);
      break;
    default:
      mz_delay();
      if (lose_lives() == 0) {
        idle_frames(5);
        return;                       /* game over */
      }
      break;
    }
  }
}

#ifdef HOST
void game_main(void)
#else
void main(void)
#endif
{
  mz_timer_init();
  clear_buffers();
  hi_score = 0;
  players_setup(1);
  for (;;) {
    title_screen();
    run_game();
  }
}
