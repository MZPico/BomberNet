/* Enemies: spawning, movement AI, drawing and death animation. */
#include <stdint.h>
#include "game.h"

/* per direction (left, right, up, down): the two chars ahead of the 2x2
 * sprite as (dy,dx) pairs, and the step (dy,dx) - original 1AF4h / 1B04h */
static const int8_t probe_offsets[4][4] = {
  { 0, -1,  1, -1},   /* left  */
  { 0,  2,  1,  2},   /* right */
  {-1,  0, -1,  1},   /* up    */
  { 2,  0,  2,  1},   /* down  */
};
static const int8_t dir_deltas[4][2] = {
  {0, -1}, {0, 1}, {-1, 0}, {1, 0},
};
/* the four board corners (x,y) */
static const uint8_t spawn_positions[4][2] = {
  {1, 1}, {37, 1}, {1, 21}, {37, 21},
};

void clear_enemies(void) {
  uint8_t i;
  for (i = 0; i < ENEMY_SLOTS; i++) enemies[i].state = ENEMY_FREE;
}

/* Activate enemies_left records at random corners (type is kept from
 * whatever the record held, as in the original). */
void spawn_enemies(void) {
  uint8_t i;
  for (i = 0; i < enemies_left && i < ENEMY_SLOTS; i++) {
    uint8_t c = rnd() & 3;
    enemies[i].x = spawn_positions[c][0];
    enemies[i].y = spawn_positions[c][1];
    enemies[i].state = ENEMY_ALIVE;
  }
}

/* An explosion hit the bonus/exit tile: once per stage, when the 2x2 at the
 * hit position is free, add four enemies there. */
void spawn_from_hit(void) {
  uint8_t i, n = 4;
  if (!hit_pending || hit_spawned) return;
  if (is_2x2_clear(map_at(hit_x, hit_y)) != C_SPACE) return;
  enemies_left += 4;
  for (i = 0; i < ENEMY_SLOTS && n; i++) {
    if (enemies[i].state != ENEMY_FREE) continue;
    enemies[i].state = ENEMY_ALIVE;
    enemies[i].x = hit_x;
    enemies[i].y = hit_y;
    n--;
  }
  hit_spawned = 1;
}

/* >= 0x80 when either char ahead is blocked in the draw buffer or the map */
static uint8_t enemy_probe(const enemy_t *e) {
  const int8_t *o = probe_offsets[e->dir];
  uint8_t c, x, y;
  x = e->x + o[1]; y = e->y + o[0];
  c = *draw_at(x, y);
  if (c >= 0x80) return c;
  c = *map_at(x, y);
  if (c >= 0x80) return c;
  x = e->x + o[3]; y = e->y + o[2];
  c = *draw_at(x, y);
  if (c >= 0x80) return c;
  return *map_at(x, y);
}

static void enemy_step(enemy_t *e) {
  e->y += dir_deltas[e->dir][0];
  e->x += dir_deltas[e->dir][1];
}

static void enemy_chase(enemy_t *e) {
  const player_t *p = &players[nearest_player(e->x, e->y)];
  if (e->x != p->x) {
    e->dir = (e->x > p->x) ? 0 : 1;
    if (enemy_probe(e) < 0x80) { enemy_step(e); return; }
  }
  if (e->y == p->y) return;
  e->dir = (e->y > p->y) ? 2 : 3;
  if (enemy_probe(e) < 0x80) enemy_step(e);
}

/* Runs when the enemy-move timer wraps (every 2nd frame). Each enemy counts
 * down; on expiry its type steps 3 -> 2 -> 1 -> 0 -> 3. Type 0 chases the
 * player; the others chase every 16th count, re-roll their direction every
 * 4th count, otherwise walk straight and re-roll once when blocked. */
void enemy_ai(void) {
  uint8_t i;
  if (tmr_enemy_move.counter != 0) return;
  enemy_anim ^= 2;
  for (i = 0; i < ENEMY_SLOTS; i++) {
    enemy_t *e = &enemies[i];
    uint8_t n;
    if (e->state != ENEMY_ALIVE) continue;
    if (e->countdown == 0) {
      e->type = (e->type == 0) ? 3 : e->type - 1;
      e->countdown = enemy_period;
    } else {
      e->countdown--;
    }
    if (e->type == 0) { enemy_chase(e); continue; }
    n = e->countdown & 0x0f;
    if (n == 0) { enemy_chase(e); continue; }
    if ((n & 3) == 0) {
      e->dir = rnd() & 3;
      if (enemy_probe(e) < 0x80) enemy_step(e);
      continue;
    }
    if (enemy_probe(e) < 0x80) { enemy_step(e); continue; }
    e->dir = rnd() & 3;
    if (enemy_probe(e) < 0x80) enemy_step(e);
  }
}

/* write one enemy char; fire under it starts the death animation */
static void put_enemy_char(enemy_t *e, uint8_t *p, uint8_t code) {
  if (*p >= C_FIRE) e->state = ENEMY_DYING;
  *p = code;
}

void draw_enemies(void) {
  uint8_t i;
  for (i = 0; i < ENEMY_SLOTS; i++) {
    enemy_t *e = &enemies[i];
    uint8_t *p;
    uint8_t code;
    if (e->state == ENEMY_FREE) continue;
    p = draw_at(e->x, e->y);
    if (e->state == ENEMY_ALIVE) {
      code = C_ENEMY_BASE + e->type * 4 + enemy_anim;
      put_enemy_char(e, p, code);
      put_enemy_char(e, p + 1, code + 1);
      put_enemy_char(e, p + SCREEN_W, code + 16);
      put_enemy_char(e, p + SCREEN_W + 1, code + 17);
      continue;
    }
    /* dying: states 2..9 -> tiles 0x22..0x30, past the sheet -> blank */
    code = e->state * 2 + 0x1e;
    if (code >= 0x30) fill_2x2(p, C_SPACE); else put_tile(p, code);
    tick_timer(&tmr_enemy_die);           /* ticked per dying enemy, as the original */
    if (tmr_enemy_die.counter != 0) continue;
    e->state++;
    if (e->state < 10) {
      mz_tone(((uint16_t)e->state << 8) | 0x32, 10);
      continue;
    }
    e->state = ENEMY_FREE;
    players[bomb_owner_near(e->x, e->y)].score += e->type * 4 + 2 + (rnd() & 3) + 1;
    if (players_alive()) {
      if (--enemies_left == 0) stage_cleared++;
    }
  }
}
