/* Lockstep network match over the network device (netdev.h). */
#include <stdint.h>
#include <string.h>
#include "game.h"

/* ---------------- lockstep match ---------------- */

uint8_t menu_net = NET_OFF;
uint8_t net_active, net_slot, net_slots, net_waiting, net_abort;
uint8_t net_delay = NET_DELAY_MIN + 1;
char net_code[5] = "AAAA";
static uint16_t net_frame;
uint8_t net_table[MAX_PLAYERS] = {0, 4, 8, 12};   /* default: one player per device */
uint8_t net_total = 2;

/* a failed NET command: ask the device why (desync / dropped) */
static void net_fail(void) {
  net_status_t st;
  net_abort = 9;
  if (net_status(&st) == 0 && (st.state == NETST_DESYNC || st.state == NETST_DROPPED)) net_abort = st.state;
}

void net_match_start(void) {
  uint8_t f;
  static const uint8_t none[NET_BYTES] = {0, 0, 0, 0};
  net_frame = 0;
  net_waiting = 0;
  net_abort = 0;
  net_active = 1;
  for (f = 0; f < net_delay; f++) net_send(f, none);   /* nobody moves in the first steps */
}

/* Send the local players' keys for step N+net_delay (one byte per local
 * player), wait for the vector of step N and deal it out by the seat table. */
void net_lockstep_poll(void) {
  uint16_t avail;
  uint8_t keys[NET_SLOTS * NET_BYTES], local[NET_BYTES], i, tries = 0;
  net_status_t st;
  if (net_abort) { for (i = 0; i < MAX_PLAYERS; i++) players[i].keys = 0; return; }
  for (i = 0; i < NET_BYTES; i++) local[i] = i < menu_local ? input_read(menu_inputs[i]) : 0;
  if (net_send(net_frame + net_delay, local) != 0) net_fail();
  while (!net_abort) {
    if (net_poll(net_frame, &avail, keys) != 0) { net_fail(); break; }
    if (avail != 0xffff && avail >= net_frame) break;
    if (++tries > 4) {
      net_waiting = 1;
      if ((tries & 15) == 0) {
        if (net_status(&st) == 0 &&
            (st.state == NETST_DESYNC || st.state == NETST_DROPPED || st.state == NETST_NOLINK))
          net_abort = st.state ? st.state : 9;
        else if (plat_key_char() == 0x1b) net_abort = NET_ABORT_BREAK;   /* BREAK: give up waiting */
      }
    }
  }
  for (i = 0; i < MAX_PLAYERS; i++)
    players[i].keys = (!net_abort && i < net_total && net_table[i] != 0xff && players[i].active) ? keys[net_table[i]] : 0;
  if (!net_abort) net_waiting = 0;
  net_frame++;
}

void net_match_end(void) {
  if (!net_active) return;
  net_active = 0;
  net_leave();
}
