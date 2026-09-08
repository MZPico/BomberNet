/* Board: outer wall, pillars, random brick layout. */
#include <stdint.h>
#include "game.h"

/* (y,x) screen cells that never get a brick: the neighbours of the four
 * corners, so enemies can leave their spawn points (original 21E5h). */
static const uint8_t reserved_cells[][2] = {
  {1, 3}, {3, 1}, {19, 1}, {21, 3}, {1, 35}, {3, 37}, {19, 37}, {21, 35},
};
#define RESERVED_COUNT 8
#define BRICK_COUNT 50

/* Outer wall (0x88) rows 0 and 23, columns 0 and 39; 9x5 pillars (0x89)
 * every 4 chars from (3,3). The original repainted them into the draw
 * buffer every frame; here they live in the map layer, so composite_map
 * (asm) carries them for free. Call after every clear_map(). */
void map_walls(void) {
  uint8_t i, j;
  uint8_t *top = map_at(0, 0), *bottom = map_at(0, 23);
  for (i = 0; i < SCREEN_W; i++) { top[i] = C_WALL; bottom[i] = C_WALL; }
  for (i = 0; i < 23; i++) {
    *map_at(0, i) = C_WALL;
    *map_at(39, i) = C_WALL;
  }
  for (j = 0; j < 5; j++)
    for (i = 0; i < 9; i++)
      fill_2x2(map_at(3 + i * 4, 3 + j * 4), C_PILLAR);
}

/* grid (0..18, 0..10) -> screen char position of the 2x2 cell */
void cell_to_screen(uint8_t *gx, uint8_t *gy) {
  *gx = *gx * 2 + 1;
  *gy = *gy * 2 + 1;
}

static void random_cell(uint8_t *gx, uint8_t *gy) {
  uint8_t v;
  do { v = rnd() & 0x1f; } while (v >= 19);
  *gx = v;
  do { v = rnd() & 0x1f; } while (v >= 11);
  *gy = v;
}

static uint8_t is_reserved(uint8_t x, uint8_t y) {
  uint8_t i;
  for (i = 0; i < RESERVED_COUNT; i++)
    if (reserved_cells[i][0] == y && reserved_cells[i][1] == x) return 1;
  return 0;
}

/* Player in the inner area (grid col 2..16, row 2..8) on a free cell, then
 * 50 bricks: not within one cell of the player, only on cells whose grid
 * column and row parity differ (pillars sit on odd/odd, odd/even and
 * even/odd stay open corridors), not on a reserved corner cell.
 * The first brick hides the BONUS, the last one the EXIT. */
static uint8_t pgx[MAX_PLAYERS], pgy[MAX_PLAYERS];   /* player grid cells */

/* 1 if grid cell (gx,gy) is within one cell of any active player */
static uint8_t near_player(uint8_t gx, uint8_t gy) {
  uint8_t i;
  for (i = 0; i < MAX_PLAYERS; i++) {
    uint8_t d;
    if (!players[i].active) continue;
    d = (gx > pgx[i]) ? gx - pgx[i] : pgx[i] - gx;
    if (d >= 2) continue;
    d = (gy > pgy[i]) ? gy - pgy[i] : pgy[i] - gy;
    if (d < 2) return 1;
  }
  return 0;
}

void generate_map(void) {
  uint8_t i, gx, gy, sx, sy, n;
  for (i = 0; i < MAX_PLAYERS; i++) {
    if (!players[i].active) continue;
    for (;;) {
      random_cell(&gx, &gy);
      if (gx < 2 || gx > 16 || gy < 2 || gy > 8) continue;
      sx = gx; sy = gy;
      cell_to_screen(&sx, &sy);
      if (*map_at(sx, sy) != C_SPACE) continue;
      if (i && near_player(gx, gy)) continue;    /* keep players apart */
      break;
    }
    pgx[i] = gx; pgy[i] = gy;
    players[i].x = sx;
    players[i].y = sy;
  }

  for (n = BRICK_COUNT; n; n--) {
    for (;;) {
      random_cell(&gx, &gy);
      if (near_player(gx, gy)) continue;
      if (((gx ^ gy) & 1) == 0) continue;
      sx = gx; sy = gy;
      cell_to_screen(&sx, &sy);
      if (is_reserved(sx, sy)) continue;
      break;
    }
    if (!bonus_present) {
      bonus_x = sx; bonus_y = sy; bonus_present = 1;
    }
    exit_x = sx; exit_y = sy; exit_present = 1;
    fill_2x2(map_at(sx, sy), C_BRICK);
  }
}
