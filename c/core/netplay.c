/* Lockstep network match over the network device (netdev.h). */
#include <stdint.h>
#include <string.h>
#include "game.h"

/* ---------------- lockstep match ---------------- */

uint8_t menu_net = NET_OFF;
uint8_t net_active, net_slot, net_slots, net_waiting, net_abort;
uint8_t net_delay = NET_DELAY_START;
char net_code[5] = "AAAA";
static uint16_t net_frame;
uint8_t net_table[MAX_PLAYERS] = {0, 4, 8, 12};   /* default: one player per device */
uint8_t net_total = 2;

/* ---- the input delay adjusted during the match ----
 * Each slot's input carries NET_BYTES bytes, one per local player; there are
 * at most three, so the last byte (ADAPT_BYTE) is never dealt to a player
 * and carries the delay's business instead:
 *  - a joiner: its last window's figures, ADAPT_VALID | waits << 4 |
 *    (margin + 1). The margin is the fewest frames the others' keys were
 *    early by (avail - frame when it first asks; -1 when it had to wait, up
 *    to 6), the waits how many of the window's frames it waited in (up to 7);
 *  - the host (slot 0): the room's delay, 1..NET_DELAY_MAX.
 * Every machine sends its own keys net_delay frames ahead and takes the
 * host's figure as soon as it sees it: one frame's keys sent twice to go up,
 * one send left out to go down. Every key carries its frame number, so the
 * lockstep stays exact whenever each machine switches. Older builds send 0
 * and ignore the byte; the host decides from the slots with figures (empty
 * slots send 0 too), and only when there are two or more: with an older
 * joiner in a room of two the delay stays as the lobby set it.
 * The host decides at the end of every window of ADAPT_WINDOW frames
 * (PLAT_NET_NO_ADAPT in plat_config.h: a machine short of memory reports
 * and follows as a joiner, but as the host keeps the lobby's delay):
 *  - up one frame when every machine waited in ADAPT_WAITS or more frames
 *    (a machine faster than the others waits a little every frame whatever
 *    the delay, so only everybody waiting means the delay is short), but
 *    never more than one frame above the lobby's figure: losses stall the
 *    game whatever the delay, and climbing on them only adds lag (a lossy
 *    test link took it to 8 frames, 480 ms);
 *  - down one frame when the smallest and the largest margin together are
 *    ADAPT_SLACK frames or more, in two windows running (with two machines
 *    their margins add up to 2 x delay - round trip, and one frame less
 *    takes 2 from that), and not within ADAPT_HOLD windows of going up. */
#define ADAPT_BYTE   (NET_BYTES - 1)
#define ADAPT_VALID  0x80
#define ADAPT_WINDOW 32               /* frames, about 2 s */
#define ADAPT_WAITS  6                /* of the window's 32 frames */
#define ADAPT_SLACK  2
#define ADAPT_HOLD   4
static uint8_t ahead;                 /* our keys went out up to net_frame + ahead (8-bit: small code on the Z80) */
static uint8_t w_min, w_waits;        /* this window: the smallest margin + 1, the frames waited in */
static uint8_t report;                /* our ADAPT_BYTE */
static uint8_t low_windows, hold;     /* host: windows running with room to go down; windows left before it may */
static uint8_t ceiling;               /* host: the lobby's delay + 1, the highest it goes */

/* the window's end: a joiner's figures out, the host's decision (statics and
 * no shifts by a variable: small code on the Z80) */
static void adapt_window(const uint8_t *k) {
  static uint8_t v, lo, hi, busy, n, s;
  v = (uint8_t)(ADAPT_VALID | w_min | (w_waits > 7 ? 0x70 : w_waits << 4));   /* our own figures */
  w_min = 7; w_waits = 0;
  if (net_slot) { report = v; return; }
#ifndef PLAT_NET_NO_ADAPT
  lo = 7; hi = 0; busy = 1; n = 0;
  for (s = 0; s < NET_SLOTS; s++, k += NET_BYTES) {   /* every slot with figures; slot 0 is ours */
    if (s) v = k[ADAPT_BYTE];
    if (!(v & ADAPT_VALID)) continue;               /* an empty slot, an older build, no window yet */
    n++;
    v &= 0x7f;                                      /* waits << 4 | margin + 1 */
    if ((v & 7) < lo) lo = v & 7;
    if ((v & 7) > hi) hi = v & 7;
    if (v < (ADAPT_WAITS << 4)) busy = 0;
  }
  if (n < 2) return;
  if (hold) hold--;
  if (busy) {                                       /* everybody waits: up */
    low_windows = 0;
    if (net_delay < ceiling) { net_delay++; hold = ADAPT_HOLD; }
  } else if ((uint8_t)(lo + hi) >= ADAPT_SLACK + 2 && !hold) {   /* room to spare: down, the second time running */
    if (++low_windows >= 2 && net_delay > NET_DELAY_MIN) { net_delay--; low_windows = 0; }
  } else low_windows = 0;
  report = net_delay;
#endif
}

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
  ahead = (uint8_t)(net_delay - 1);
  w_min = 7; w_waits = 0; low_windows = 0; hold = 1;   /* the first window is the start's */
#ifndef PLAT_NET_NO_ADAPT
  ceiling = net_delay < NET_DELAY_MAX ? (uint8_t)(net_delay + 1) : NET_DELAY_MAX;
#endif
  report = net_slot == 0 ? net_delay : 0;
}

/* Send the local players' keys for step N+net_delay (one byte per local
 * player; the last byte the delay's, above), wait for the vector of step N
 * and deal it out by the seat table. */
void net_lockstep_poll(void) {
  static uint16_t avail;                          /* statics: smaller code and stack on the Z80 (never re-entered) */
  static uint8_t keys[NET_SLOTS * NET_BYTES], local[NET_BYTES], i, tries;
  static net_status_t st;
  tries = 0;
  if (net_abort) { for (i = 0; i < MAX_PLAYERS; i++) players[i].keys = 0; return; }
  for (i = 0; i < ADAPT_BYTE; i++) local[i] = i < menu_local ? input_read(menu_inputs[i]) : 0;
  local[ADAPT_BYTE] = report;
  while (ahead < net_delay) {                     /* one frame, two going up, none going down */
    ahead++;                                      /* (not net_frame + ++ahead: sccz80 reads that as net_frame++ + ahead) */
    if (net_send(net_frame + ahead, local) != 0) { net_fail(); break; }
  }
  while (!net_abort) {
    if (net_poll(net_frame, &avail, keys) != 0) { net_fail(); break; }
    if (avail != 0xffff && avail >= net_frame) {
      if (!tries) {                               /* in time: frames early + 1 (the low bytes do: under 16 apart) */
        i = (uint8_t)((uint8_t)avail - (uint8_t)net_frame + 1);
        if (i < w_min) w_min = i;
      }
      break;
    }
    if (!tries) { w_min = 0; w_waits++; }          /* it waits */
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
  {                                               /* deal the vector out by the seat table */
    static player_t *p;
    static uint8_t t;
    for (i = 0, p = players; i < MAX_PLAYERS; i++, p++) {
      t = net_table[i];
      p->keys = (!net_abort && i < net_total && t != 0xff && p->active) ? keys[t] : 0;
    }
  }
  if (!net_abort) {
    net_waiting = 0;
    i = keys[ADAPT_BYTE];                         /* the host's delay (slot 0's last byte; 0 from an older host) */
    if (net_slot && i && i <= NET_DELAY_MAX) net_delay = i;
    if (((uint8_t)net_frame & (ADAPT_WINDOW - 1)) == ADAPT_WINDOW - 1) adapt_window(keys);
  }
  net_frame++;
  ahead--;
}

void net_match_end(void) {
  if (!net_active) return;
  net_active = 0;
  net_leave();
}
