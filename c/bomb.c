/* Bombs: placement, ticking, explosion and blast propagation. */
#include <stdint.h>
#include "game.h"
#include "data.h"

void clear_bombs(void) {
  uint8_t i;
  for (i = 0; i < BOMB_SLOTS; i++) {
    bomb_t *b = &bombs[i];
    b->state = BOMB_FREE; b->x = b->y = b->timer = b->owner = 0;
  }
}

/* Fire key while alive: first free record, at the player position, only if
 * the 2x2 map cell is empty. The bomb tile goes into the map layer. */
static void place_bomb(uint8_t owner) {
  player_t *p = &players[owner];
  uint8_t i;
  uint8_t *m;
  if (p->state >= P_DYING) return;
  if (!(p->keys & KEY_SPACE)) return;
  for (i = 0; i < BOMB_SLOTS; i++) {
    bomb_t *b = &bombs[i];
    if (b->state != BOMB_FREE) continue;
    b->x = p->x;
    b->y = p->y;
    m = map_at(p->x, p->y);
    if (is_2x2_clear(m) != C_SPACE) return;
    put_tile(m, C_BOMB_TILE);
    b->state = BOMB_TICK1;
    b->timer = 0;
    b->owner = owner;
    p->state = P_STAND0;
    return;
  }
}

void place_bombs(void) {
  uint8_t i;
  for (i = 0; i < MAX_PLAYERS; i++)
    if (players[i].active) place_bomb(i);
}

/* Fire tiles carry no owner; credit goes to the closest exploding bomb. */
uint8_t bomb_owner_near(uint8_t x, uint8_t y) {
  uint8_t i, best = 0, best_d = 0xff;
  for (i = 0; i < BOMB_SLOTS; i++) {
    bomb_t *b = &bombs[i];
    uint8_t d;
    if (b->state < BOMB_EXPLODE || b->state > 12) continue;
    d = (b->x > x ? b->x - x : x - b->x) + (b->y > y ? b->y - y : y - b->y);
    if (d < best_d) { best_d = d; best = b->owner; }
  }
  return best;
}

/* states 1..4 advance every 7 frames, 5..13 every frame, 14 -> free */
void update_bombs(void) {
  uint8_t i;
  bomb_anim ^= 2;
  for (i = 0; i < BOMB_SLOTS; i++) {
    bomb_t *b = &bombs[i];
    if (b->state == BOMB_FREE) continue;
    if (b->state < BOMB_EXPLODE) {
      if (++b->timer != 7) continue;
    }
    b->timer = 0;
    if (++b->state == BOMB_END) { b->state = BOMB_FREE; continue; }
    if (b->state >= BOMB_EXPLODE && players_alive())
      mz_tone(((uint16_t)b->state << 8) | 0x0a, 12);
  }
}

/* fire code for the arms of the explosion being drawn */
static uint8_t fire_code;

/* Blast one char: walls and pillars stop it; a brick burns one stage
 * (0x87 -> removed); anything else becomes fire. Returns the new content. */
static uint8_t blast_cell_near(uint8_t x, uint8_t y) {
  uint8_t *m = map_at(x, y);
  uint8_t c = *m;
  if (c == C_WALL || c == C_PILLAR) return c;
  if (c >= 0x80 && c < 0x8a) {
    c++;
    if (c == C_WALL) c = C_SPACE;
  } else {
    c = fire_code;
  }
  *m = c;
  return c;
}

/* Same, but a brick is left alone (the blast stops in front of it). */
static uint8_t blast_cell_far(uint8_t x, uint8_t y) {
  uint8_t *m = map_at(x, y);
  uint8_t c = *m;
  if (c >= 0x80 && c < 0x8a) return c;
  *m = fire_code;
  return fire_code;
}

#define IS_BLOCK(c) ((c) >= 0x80 && (c) < 0x8a)

/* Four arms of two rows of four chars (blast_pattern). Per row: a solid wall
 * on the first char skips the row; a brick after the 1st or 3rd char stops it. */
static void blast_arms(const bomb_t *b) {
  const int8_t *p = (const int8_t *)blast_pattern;
  const int8_t *end = p + 64;
  while (p < end) {
    uint8_t c = blast_cell_near(b->x + p[1], b->y + p[0]);
    if (c == C_WALL) { p += 8; continue; }
    c = blast_cell_near(b->x + p[3], b->y + p[2]);
    if (IS_BLOCK(c)) { p += 8; continue; }
    c = blast_cell_far(b->x + p[5], b->y + p[4]);
    if (IS_BLOCK(c)) { p += 8; continue; }
    blast_cell_far(b->x + p[7], b->y + p[6]);
    p += 8;
  }
}

/* write one ticking-bomb char into the map; fire there triggers a chain */
static void put_bomb_char(bomb_t *b, uint8_t *m, uint8_t code) {
  if (*m >= C_FIRE) { b->state = 4; b->timer = 6; }
  *m = code;
}

void draw_bombs(void) {
  uint8_t i;
  for (i = 0; i < BOMB_SLOTS; i++) {
    bomb_t *b = &bombs[i];
    uint8_t *m, code;
    if (b->state == BOMB_FREE) continue;
    m = map_at(b->x, b->y);
    if (b->state == BOMB_ERASE) { fill_2x2(m, C_SPACE); continue; }
    if (b->state >= BOMB_EXPLODE) {
      code = b->state * 2 + 0xd6;            /* 0xe0 .. 0xee centre */
      put_tile(m, code);
      code += 2;
      fire_code = (code >= 0xf0) ? C_SPACE : code;
      blast_arms(b);
      continue;
    }
    /* ticking: frames 0x60,0x64,0x68,0x6c (+anim) in both layers */
    code = (b->state - 1) * 4 + C_BOMB_TILE + bomb_anim;
    put_bomb_char(b, m, code);
    put_bomb_char(b, m + 1, code + 1);
    put_bomb_char(b, m + SCREEN_W, code + 16);
    put_bomb_char(b, m + SCREEN_W + 1, code + 17);
    put_tile(draw_at(b->x, b->y), code);
  }
}
