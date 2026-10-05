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
    /* network match: every player's keys arrive through the lockstep vector
     * (the local source is read by net_lockstep_poll), and the record must be
     * identical on every device because it is part of the state hash */
    p->input = net_active ? INPUT_NET : menu_inputs[i];
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
#ifdef ESP_FAST128
static void put_player_char(player_t *p,uint8_t *c,uint8_t code) __naked {
 __asm
    pop af
    pop bc
    pop de
    pop hl
    push hl
    push de
    push bc
    push af
    ld a,(de)
    ld b,a
    ld a,c
    ld (de),a
    ld a,b
    cp 0xc0
    ret c
    ld de,5
    add hl,de
    ld c,a
    ld a,(hl)
    cp 6
    ret nc
    ld (hl),6
    ld a,c
    cp 0xe0
    ret c
    ld de,-5
    add hl,de
    push hl
    call _player_killed
    pop bc
    ret
 __endasm;
}
#else
static void put_player_char(player_t *p, uint8_t *c, uint8_t code) {
  uint8_t old = *c;
  *c = code;
  if (old >= C_ENEMY_BASE && p->state < P_DYING) {
    p->state = P_DYING;
    if (old >= C_FIRE) player_killed(p);
  }
}

#endif

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
#ifdef ESP_FAST128
static void draw_player(player_t *p) __naked {
 __asm
    pop af
    pop hl
    push hl
    push af
    push ix
    push hl
    pop ix
    ld a,(ix+5)
    cp 6
    jr c,d14_alive
    sub 6
    add a,a
    ld c,a
    ld a,0x4e
    sub c
    jr d14_code
d14_alive:
    or a
    jr nz,d14_second
    ld (ix+5),1
    ld a,(ix+12)
    jr d14_code
d14_second:
    ld a,(ix+13)
d14_code:
    ld c,a
    ld l,(ix+4)
    ld h,0
    add hl,hl
    ld de,_row_off
    add hl,de
    ld e,(hl)
    inc hl
    ld d,(hl)
    ld hl,_draw_buf
    add hl,de
    ld e,(ix+3)
    ld d,0
    add hl,de
    ex de,hl
    call d14_char
    inc de
    inc c
    call d14_char
    ld hl,39
    add hl,de
    ex de,hl
    ld a,c
    add a,15
    ld c,a
    call d14_char
    inc de
    inc c
    call d14_char
    pop ix
    ret
d14_char:
    ld a,(de)
    ld h,a
    ld a,c
    ld (de),a
    ld a,h
    cp 0xc0
    ret c
    ld h,a
    ld a,(ix+5)
    cp 6
    ret nc
    ld (ix+5),6
    ld a,h
    cp 0xe0
    ret c
    push bc
    push de
    push ix
    call _player_killed
    pop hl
    pop de
    pop bc
    ret
 __endasm;
}
#else
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
#endif

/* The death frames (40h-5Fh) are green in the table; there is no room for
 * coloured copies, so the attribute plane is patched after every flush. */
#ifdef ESP_FAST128
static void players_death_colour_full(void) {
#else
void players_death_colour(void) {
#endif
  uint8_t i;
  for (i = 0; i < MAX_PLAYERS; i++) {
    player_t *p = &players[i];
    if (!p->active || p->state < P_DYING || p->life_lost) continue;
    plat_player_colour(p->x, p->y, i);
    plat_player_colour(p->x + 1, p->y, i);
    plat_player_colour(p->x, p->y + 1, i);
    plat_player_colour(p->x + 1, p->y + 1, i);
  }
}

#ifdef ESP_FAST128
void players_death_colour(void) __naked {
 __asm
    push ix
    ld ix,_players
    ld b,4
d14_colour_loop:
    ld a,(ix+0)
    or a
    jr z,d14_colour_next
    ld a,(ix+5)
    cp 6
    jr c,d14_colour_next
    ld a,(ix+8)
    or a
    jr z,d14_colour_full
d14_colour_next:
    ld de,16
    add ix,de
    djnz d14_colour_loop
    pop ix
    ret
d14_colour_full:
    pop ix
    jp _players_death_colour_full
 __endasm;
}
#endif

#ifdef ESP_FAST128
void draw_players(void) __naked {
 __asm
    push ix
    ld ix,_players
    ld b,4
d14_player_loop:
    ld a,(ix+0)
    or a
    jr z,d14_next
    ld a,(ix+8)
    or a
    jr nz,d14_next
    push bc
    push ix
    call _draw_player
    pop hl
    pop bc
d14_next:
    ld de,16
    add ix,de
    djnz d14_player_loop
    pop ix
    ret
 __endasm;
}
#else
void draw_players(void) {
  uint8_t i;
  for (i = 0; i < MAX_PLAYERS; i++)
    if (players[i].active && !players[i].life_lost) draw_player(&players[i]);
}
#endif

/* Cursor keys move by one char when none of the four chars ahead is a wall,
 * pillar or fresh brick (in the draw buffer). Players pass through each other. */
#ifdef ESP_FAST128
static void move_player(player_t *p) __naked {
 __asm
    pop af
    pop hl
    push hl
    push af
    push ix
    push hl
    pop ix
    ld a,(ix+2)
    ld d,(ix+4)
    ld e,(ix+3)
    bit 1,a
    jr z,m11_left
    inc d
    jr m11_check
m11_left:
    bit 3,a
    jr z,m11_right
    dec e
    jr m11_check
m11_right:
    bit 2,a
    jr z,m11_up
    inc e
    jr m11_check
m11_up:
    bit 0,a
    jr z,m11_done
    dec d
m11_check:
    ld l,d
    ld h,0
    add hl,hl
    ld bc,_row_off
    add hl,bc
    ld c,(hl)
    inc hl
    ld b,(hl)
    ld hl,_draw_buf
    add hl,bc
    ld c,e
    ld b,0
    add hl,bc
    call m11_block
    jr z,m11_done
    inc hl
    call m11_block
    jr z,m11_done
    ld bc,39
    add hl,bc
    call m11_block
    jr z,m11_done
    inc hl
    call m11_block
    jr z,m11_done
    ld (ix+3),e
    ld (ix+4),d
    ld a,(ix+6)
    srl a
    add a,2
    ld h,a
    ld l,10
    push hl
    ld hl,14
    push hl
    call _plat_tone
    pop bc
    pop bc
m11_done:
    pop ix
    ret
m11_block:
    ld a,(hl)
    cp 0x88
    ret z
    cp 0x89
    ret z
    cp 0x80
    ret

 __endasm;
}
#else
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
  plat_tone(((uint16_t)((p->anim >> 1) + 2) << 8) | 0x0a, 14);
}
#endif

/* Every 2nd frame: toggle the animation frame; alive -> move; dying -> step
 * the death animation (every 4th call) and raise life_lost at its end. */
#ifdef ESP_FAST128
void players_anim_step(void) __naked {
 __asm
    ld a,(_tmr_player_anim)
    or a
    ret nz
    push ix
    ld ix,_players
    ld b,4
p15_anim:
    ld a,(ix+0)
    or a
    jr z,p15_anim_next
    ld a,(ix+6)
    xor 2
    ld (ix+6),a
    ld a,(ix+5)
    cp 6
    jr c,p15_move
    cp 13
    jr nz,p15_die
    ld (ix+8),1
    jr p15_anim_next
p15_die:
    ld a,(ix+7)
    inc a
    ld (ix+7),a
    cp 4
    jr c,p15_anim_next
    ld (ix+7),0
    inc (ix+5)
    ld h,(ix+5)
    ld l,0
    push bc
    push hl
    ld hl,32
    push hl
    call _plat_tone
    pop hl
    pop hl
    pop bc
    jr p15_anim_next
p15_move:
    push bc
    push ix
    call _move_player
    pop hl
    pop bc
p15_anim_next:
    ld de,16
    add ix,de
    djnz p15_anim
    pop ix
    ret
 __endasm;
}
#else
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
    plat_tone((uint16_t)p->state << 8, 32);
  }
}
#endif

/* On the EXIT tile: the stage is regenerated (no points). On the BONUS
 * tile: random 16..142 points (x10 on screen) and the bonus disappears. */
#ifdef ESP_FAST128
void check_pickups(void) __naked {
 __asm
    push ix
    ld ix,_players
    ld b,4
p15_pick:
    ld a,(ix+0)
    or a
    jr z,p15_pick_next
    ld a,(ix+5)
    cp 6
    jr nc,p15_pick_next
    ld a,(_exit_y)
    cp (ix+4)
    jr nz,p15_bonus
    ld a,(_exit_x)
    cp (ix+3)
    jr nz,p15_bonus
    xor a
    ld (_exit_present),a
    inc a
    ld (_exit_touched),a
    pop ix
    ret
p15_bonus:
    ld a,(_bonus_present)
    or a
    jr z,p15_pick_next
    ld a,(_bonus_y)
    cp (ix+4)
    jr nz,p15_pick_next
    ld a,(_bonus_x)
    cp (ix+3)
    jr nz,p15_pick_next
    xor a
    ld (_bonus_present),a
    push bc
    ld hl,0x0100
    push hl
    ld hl,0x30
    push hl
    call _plat_tone
    pop hl
    pop hl
    call _rnd
    ld a,l
    and 63
    add a,a
    or 16
    ld e,a
    ld d,0
    ld l,(ix+10)
    ld h,(ix+11)
    add hl,de
    ld (ix+10),l
    ld (ix+11),h
    pop bc
p15_pick_next:
    ld de,16
    add ix,de
    djnz p15_pick
    pop ix
    ret
 __endasm;
}
#else
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
    plat_tone(0x0100, 0x30);
    p->score += ((rnd() & 0x3f) << 1) | 0x10;
  }
}
#endif

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
