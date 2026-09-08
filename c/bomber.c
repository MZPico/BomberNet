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
    p[0] = i + 1;
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
    p[0] = C_HUD_P; p[1] = i + 1;
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
  bombs[0].state = BOMB_TICK1;
  bombs[0].x = 0x20;
  bombs[0].y = 0x0b;
}

/* menu line on the title: player count (LEFT/RIGHT) and the P2 keys */
static uint8_t menu_prev_keys, menu_prev_keys_b;

static void title_menu(void) {
  uint8_t k = mz_keys(), *p;
  uint8_t edge = k & ~menu_prev_keys;
  menu_prev_keys = k;
  uint8_t kb = mz_keys_b(), maxp;
  uint8_t edge_b = kb & ~menu_prev_keys_b;
  menu_prev_keys_b = kb;
  if (edge_b & KEY_UP) joy_type = (joy_type + 1) % 3;
  if (edge_b & KEY_DOWN) joy_type = (joy_type + 2) % 3;
  maxp = joy_type == JOY_NONE ? 2 : 4;
  if ((edge & KEY_RIGHT) && menu_players < maxp) menu_players++;
  if ((edge & KEY_LEFT) && menu_players > 1) menu_players--;
  if (menu_players > maxp) menu_players = maxp;
  if (edge & (KEY_UP | KEY_DOWN)) menu_mode ^= 1;
  if (menu_mode == GAME_DM && menu_players < 2) menu_players = 2;
  /* title mode: letters and 00h..09h digits are text, ':' '<' '>' and ASCII
   * digits would be logo block graphics; 23h/24h are the right/left arrows */
  p = draw_at(2, 21);
  print_string(p, "PLAYERS ");
  p[8] = 0x24; p[10] = menu_players; p[12] = 0x23;
  print_string(p + 15, menu_mode == GAME_DM ? "MODE  DEATHMATCH" : "MODE  COOP      ");
  p[15 + 4] = 0x22; p[15 + 5] = 0x21;         /* up/down arrows */
  p = draw_at(2, 23);
  print_string(p, "P  CURSOR AND SPACE  P  WASD AND E");
  p[1] = 1; p[22] = 2;                      /* digit codes, not ASCII */
  p = draw_at(2, 20);
  print_string(p, joy_type == JOY_800 ? "JOYSTICK MZ 800  " :
                  joy_type == JOY_1X03 ? "JOYSTICK MZ 1X03 " : "JOYSTICK NONE    ");
  p[17] = 0x22; p[18] = 0x21;              /* W/S arrows */
  print_string(p + 20, "P  P  JOY");
  p[21] = 3; p[24] = 4;
}

static void title_frame(void) {
  uint8_t i;
  tick_timers();
  title_menu();
  for (i = 0; i < 240; i++) draw_buf[i] = title_logo[i];
  put_tile(draw_at(17, 11), C_BONUS_TILE);
  put_tile(draw_at(25, 11), C_EXIT_TILE);
  print_string(draw_at(6, 10), str_legend1);
  print_string(draw_at(6, 11), str_box_tl);
  print_string(draw_at(6, 12), str_box_ml);
  print_string(draw_at(2, 13), str_legend2);
  print_string(draw_at(3, 14), str_box2_t);
  print_string(draw_at(3, 15), str_box2_m);
  print_string(draw_at(3, 16), str_box2_b);
  print_string(draw_at(6, 17), str_box3_t);
  print_string(draw_at(6, 18), str_box3_m);
  print_string(draw_at(6, 19), str_box3_b);
  print_string(draw_at(6, 20), "    ");  /* row 20 now holds the joystick line */
  print_string(draw_at(8, 22), str_push_space);
  print_string(draw_at(2, 24), str_copyright);
  print_string(draw_at(4, 7), str_legend_row7);
  print_string(draw_at(5, 8), str_legend_row8);
  print_num5(draw_at(32, 16), hi_score);
  print_num5(draw_at(32, 18), players[0].score);
  put_tile(draw_at(2, 7), C_PLAYER_B);
  draw_bombs();
  draw_enemies();
  if (tmr_player_anim.counter == 0) {
    bomb_anim ^= 2;
    enemy_anim ^= 2;
  }
}

/* returns when SPACE is pressed */
static void title_screen(void) {
  title_init();
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
  uint8_t len = (uint8_t)strlen(s);
  hud_text(draw_at(BOX_X + (BOX_W - len) / 2, row), s);
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
