/* Players: setup, drawing, movement, death animation, bonus and exit tiles. */
#include <stdint.h>
#include "game.h"

/* per key (down, left, right, up): dx, dy */
static const int8_t move_deltas[4][2] = {
  {0, 1}, {-1, 0}, {1, 0}, {0, -1},
};

/* standing frames per player: player 0 keeps the original 0x8A/0x8C tiles,
 * players 1..3 use the former walking rows 0xA0..0xBF (see extract_data.py) */
static const uint8_t player_tiles[MAX_PLAYERS][2] = {
  {C_PLAYER_A, C_PLAYER_B}, {0xa0, 0xa2}, {0xa4, 0xa6}, {0xa8, 0xaa},
};

void players_setup(uint8_t count) {
  uint8_t i;
  player_count = count;
  for (i = 0; i < MAX_PLAYERS; i++) {
    player_t *p = &players[i];
    p->active = i < count;
    p->input = menu_inputs[i];
    p->tile_a = player_tiles[i][0];
    p->tile_b = player_tiles[i][1];
    p->score = 0;
    p->lives = 3;
    p->keys = 0;
    p->kills = 0;
    p->wins = 0;
    p->anim = 0;
    p->x = p->y = 0;
    p->state = P_STAND1;
    p->death_tick = 0;
    p->life_lost = 0;
  }
}

/* called by stage_start after generate_map placed the positions */
void players_stage_reset(void) {
  uint8_t i;
  for (i = 0; i < MAX_PLAYERS; i++) {
    player_t *p = &players[i];
    p->state = P_STAND1;
    p->death_tick = 0;
    p->life_lost = 0;
    p->kills = 0;
    if (game_mode == GAME_COOP && p->active && p->lives == 0) p->active = 0;   /* out of the match */
  }
}

/* write one player char; an enemy or fire under it starts dying */
static void put_player_char(player_t *p, uint8_t *c, uint8_t code) {
  uint8_t old = *c;
  *c = code;
  if (old >= C_ENEMY_BASE && p->state < P_DYING) {
    p->state = P_DYING;
    if (old >= C_FIRE) player_killed(p);
  }
}

/* Deathmatch scoring: the owner of the closest exploding bomb gets the kill
 * (+10, shown as 100); blowing yourself up costs 5. */
void player_killed(player_t *p) {
  uint8_t k;
  if (game_mode != GAME_DM) return;
  k = bomb_owner_near(p->x, p->y);
  if (&players[k] == p) {
    if (p->score >= 5) p->score -= 5;
    return;
  }
  players[k].kills++;
  players[k].score += 10;
}

/* state 0/1 standing, 6..13 dying */
static void draw_player(player_t *p) {
  uint8_t code;
  uint8_t *c;
  if (p->state >= P_DYING) {
    code = 0x4e - (p->state - P_DYING) * 2;
  } else if (p->state == P_STAND0) {
    p->state = P_STAND1;
    code = p->tile_a;
  } else {
    code = p->tile_b;
  }
  c = draw_at(p->x, p->y);
  put_player_char(p, c, code);
  put_player_char(p, c + 1, code + 1);
  put_player_char(p, c + SCREEN_W, code + 16);
  put_player_char(p, c + SCREEN_W + 1, code + 17);
}

/* The death frames (40h-5Fh) are green in the table; there is no room for
 * coloured copies, so the attribute plane is patched after every flush. */
void players_death_colour(void) {
  uint8_t i;
  for (i = 0; i < MAX_PLAYERS; i++) {
    player_t *p = &players[i];
    uint8_t a;
    if (!p->active || p->state < P_DYING || p->life_lost) continue;
    a = player_attrs[i];
    mz_set_attr(p->x, p->y, a);
    mz_set_attr(p->x + 1, p->y, a);
    mz_set_attr(p->x, p->y + 1, a);
    mz_set_attr(p->x + 1, p->y + 1, a);
  }
}

void draw_players(void) {
  uint8_t i;
  for (i = 0; i < MAX_PLAYERS; i++)
    if (players[i].active && !players[i].life_lost) draw_player(&players[i]);
}

/* Cursor keys move by one char when none of the four chars ahead is a wall,
 * pillar or fresh brick (in the draw buffer). Players pass through each other. */
static void move_player(player_t *p) {
  uint8_t k = p->keys, d, x, y;
  const uint8_t *c;
  if (k & KEY_DOWN) d = 0;
  else if (k & KEY_LEFT) d = 1;
  else if (k & KEY_RIGHT) d = 2;
  else if (k & KEY_UP) d = 3;
  else return;
  x = p->x + move_deltas[d][0];
  y = p->y + move_deltas[d][1];
  c = draw_at(x, y);
  #define BLOCKS(v) ((v) == C_WALL || (v) == C_PILLAR || (v) == C_BRICK)
  if (BLOCKS(c[0]) || BLOCKS(c[1]) || BLOCKS(c[SCREEN_W]) || BLOCKS(c[SCREEN_W + 1])) return;
  p->x = x;
  p->y = y;
  /* hook for walking animation (the original lost the direction here) */
  mz_tone(((uint16_t)((p->anim >> 1) + 2) << 8) | 0x0a, 14);
}

/* Every 2nd frame: toggle the animation frame; alive -> move; dying -> step
 * the death animation (every 4th call) and raise life_lost at its end. */
void players_anim_step(void) {
  uint8_t i;
  if (tmr_player_anim.counter != 0) return;
  for (i = 0; i < MAX_PLAYERS; i++) {
    player_t *p = &players[i];
    if (!p->active) continue;
    p->anim ^= 2;
    if (p->state < P_DYING) { move_player(p); continue; }
    if (p->state == P_DEAD) { p->life_lost = 1; continue; }
    if (++p->death_tick < 4) continue;
    p->death_tick = 0;
    p->state++;
    mz_tone((uint16_t)p->state << 8, 32);
  }
}

/* On the EXIT tile: the stage is regenerated (no points). On the BONUS
 * tile: random 16..142 points (x10 on screen) and the bonus disappears. */
void check_pickups(void) {
  uint8_t i;
  for (i = 0; i < MAX_PLAYERS; i++) {
    player_t *p = &players[i];
    if (!p->active || p->state >= P_DYING) continue;
    if (exit_y == p->y && exit_x == p->x) {
      exit_present = 0;
      exit_touched = 1;
      return;
    }
    if (!bonus_present) continue;
    if (bonus_y != p->y || bonus_x != p->x) continue;
    bonus_present = 0;
    mz_tone(0x0100, 0x30);
    p->score += ((rnd() & 0x3f) << 1) | 0x10;
  }
}

/* closest alive player to (x,y); falls back to player 0 */
uint8_t nearest_player(uint8_t x, uint8_t y) {
  uint8_t i, best = 0;
  uint16_t best_d = 0xffff;
  for (i = 0; i < MAX_PLAYERS; i++) {
    player_t *p = &players[i];
    uint16_t d;
    if (!p->active || p->state >= P_DYING) continue;
    d = (p->x > x ? p->x - x : x - p->x) + (p->y > y ? p->y - y : y - p->y);
    if (d < best_d) { best_d = d; best = i; }
  }
  return best;
}

/* The item becomes visible once its brick is gone: both layers clear. */
void reveal_bonus(void) {
  if (bonus_revealed) return;
  if (is_2x2_clear(map_at(bonus_x, bonus_y)) != C_SPACE) return;
  if (is_2x2_clear(draw_at(bonus_x, bonus_y)) != C_SPACE) return;
  bonus_present = 1;
  bonus_revealed = 1;
}

void reveal_exit(void) {
  if (exit_revealed) return;
  if (is_2x2_clear(map_at(exit_x, exit_y)) != C_SPACE) return;
  if (is_2x2_clear(draw_at(exit_x, exit_y)) != C_SPACE) return;
  exit_present = 1;
  exit_revealed = 1;
  draw_exit();                        /* the original falls through into draw_exit */
}

/* fire on the item destroys it and queues an enemy spawn at that spot */
static void put_item_char(uint8_t *c, uint8_t code, uint8_t *present, uint8_t x, uint8_t y) {
  if (*c >= C_FIRE) {
    *present = 0;
    hit_x = x;
    hit_y = y;
    hit_pending = 1;
  }
  *c = code;
}

static void draw_item(uint8_t code, uint8_t *present, uint8_t x, uint8_t y) {
  uint8_t *c;
  if (!*present) return;
  c = draw_at(x, y);
  put_item_char(c, code, present, x, y);
  put_item_char(c + 1, code + 1, present, x, y);
  put_item_char(c + SCREEN_W, code + 16, present, x, y);
  put_item_char(c + SCREEN_W + 1, code + 17, present, x, y);
}

void draw_bonus(void) { draw_item(C_BONUS_TILE, &bonus_present, bonus_x, bonus_y); }
void draw_exit(void)  { draw_item(C_EXIT_TILE, &exit_present, exit_x, exit_y); }
