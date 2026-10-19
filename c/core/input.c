/* Input sources -> per-player key masks. The game logic only ever reads
 * players[i].keys, sampled once per frame here; that keeps the simulation
 * deterministic for replay and (later) network lockstep. */
#include <stdint.h>
#include "game.h"

/* A local source's keys for this game frame. Fire is a press, not a level:
 * it passes on the first frame it is down and not again until released, so
 * a press drops one bomb (held, the original dropped one every two cells).
 * Recordings and the network carry the keys after this, so replays and
 * lockstep see the same. */
uint8_t input_read(uint8_t source) {
  static uint8_t fire_was;                  /* bit per source: fire down last time */
  uint8_t k, bit;
  switch (source) {
  case INPUT_KBD_A: k = plat_keys_a(); break;
  case INPUT_KBD_B: k = plat_keys_b(); break;
  case INPUT_JOY1:  k = plat_joy(0); break;
  case INPUT_JOY2:  k = plat_joy(1); break;
  default:          return 0;        /* KBD_B and NET arrive in later phases */
  }
  bit = (uint8_t)(1 << source);
  if (k & KEY_SPACE) {
    if (fire_was & bit) k &= (uint8_t)~KEY_SPACE;
    fire_was |= bit;
  } else fire_was &= (uint8_t)~bit;
  return k;
}

void input_poll(void) {
  uint8_t i;
  if (net_active) { net_lockstep_poll(); return; }
  for (i = 0; i < MAX_PLAYERS; i++) {
    if (!players[i].active) { players[i].keys = 0; continue; }
    players[i].keys = replay_active ? replay_keys[i] : input_read(players[i].input);
  }
}

uint8_t players_alive(void) {
  uint8_t i, n = 0;
  for (i = 0; i < MAX_PLAYERS; i++)
    if (players[i].active && players[i].state < P_DYING) n++;
  return n;
}

uint8_t players_finished(void) {
  uint8_t i;
  for (i = 0; i < MAX_PLAYERS; i++)
    if (players[i].active && !players[i].life_lost) return 0;
  return 1;
}
