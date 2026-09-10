/* Input sources -> per-player key masks. The game logic only ever reads
 * players[i].keys, sampled once per frame here; that keeps the simulation
 * deterministic for replay and (later) network lockstep. */
#include <stdint.h>
#include "game.h"

uint8_t input_read(uint8_t source) {
  switch (source) {
  case INPUT_KBD_A: return mz_keys();
  case INPUT_KBD_B: return mz_keys_b();
  case INPUT_JOY1:  return mz_joy(0);
  case INPUT_JOY2:  return mz_joy(1);
  default:          return 0;        /* KBD_B and NET arrive in later phases */
  }
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
