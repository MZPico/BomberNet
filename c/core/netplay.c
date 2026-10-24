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
 *    (margin + 1). The margin is the second fewest frames the others' keys
 *    were early by (avail - frame when it first asks; -1 when it had to
 *    wait, up to 6): one late frame in a window is left out, two count. The
 *    waits are how many of the window's frames it waited in (up to 7);
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
 * and follows as a joiner, but as the host keeps the lobby's delay;
 * PLAT_NET_FOLLOW_ONLY: it only follows, and sends no figures, so a room
 * with it keeps the lobby's delay; no machine needs it now):
 *  - up one frame when every machine waited in ADAPT_WAITS or more frames
 *    (a machine faster than the others waits a little every frame whatever
 *    the delay, so only everybody waiting means the delay is short), but
 *    never above the lobby's figure, which counts the polling at both ends
 *    and is pessimistic already: so a match is never slower than the lobby
 *    made it. Losses, and a machine too slow for the pace (a browser that
 *    has just started), make everybody wait whatever the delay, and
 *    climbing on them only adds lag (a lossy test link took it to 8 frames,
 *    480 ms; the play page's slow start from 5 to 6);
 *  - down one frame when the smallest and the largest margin together are
 *    ADAPT_SLACK frames or more, in two windows running (with two machines
 *    their margins add up to 2 x delay - round trip, and one frame less
 *    takes 2 from that; a machine that waited counts -1: counted as 0, a
 *    PC - PC test went down to 1 and both waited at once), and not within
 *    ADAPT_HOLD windows of going up. */
#define ADAPT_BYTE   (NET_BYTES - 1)
#define ADAPT_VALID  0x80
#define ADAPT_WINDOW 32               /* frames, about 2 s */
#define ADAPT_WAITS  6                /* of the window's 32 frames */
#define ADAPT_SLACK  2
#define ADAPT_HOLD   4
#ifdef PLAT_RUNAHEAD
static void ra_start(void);
#endif
static uint8_t ahead;                 /* our keys went out up to net_frame + ahead (8-bit: small code on the Z80) */
static uint8_t w_min, w_min2, w_waits;   /* this window: the smallest margin + 1, the second smallest, the frames waited in */
static uint8_t report;                /* our ADAPT_BYTE */
static uint8_t low_windows, hold;     /* host: windows running with room to go down; windows left before it may */
static uint8_t ceiling;               /* host: the lobby's delay, the highest it goes */
#ifdef NET_ADAPT_TRACE
uint8_t net_adapt_seen[NET_SLOTS];    /* tests: the host's last figures per slot (its own in slot 0) */
#endif

/* the window's end: a joiner's figures out, the host's decision (statics and
 * no shifts by a variable: small code on the Z80) */
#ifndef PLAT_NET_FOLLOW_ONLY
static void adapt_window(const uint8_t *k) {
  static uint8_t v, lo, hi, busy, n, s;
  v = (uint8_t)(ADAPT_VALID | w_min2 | (w_waits > 7 ? 0x70 : w_waits << 4));   /* our own figures: one late frame left out */
  w_min = w_min2 = 7; w_waits = 0;
  if (net_slot) { report = v; return; }
#ifndef PLAT_NET_NO_ADAPT
  lo = 7; hi = 0; busy = 1; n = 0;
  for (s = 0; s < NET_SLOTS; s++, k += NET_BYTES) {   /* every slot with figures; slot 0 is ours */
    if (s) v = k[ADAPT_BYTE];
#ifdef NET_ADAPT_TRACE
    net_adapt_seen[s] = v;
#endif
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
#endif

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
  w_min = w_min2 = 7; w_waits = 0; low_windows = 0; hold = 1;   /* the first window is the start's */
#ifndef PLAT_NET_NO_ADAPT
  ceiling = net_delay;
#endif
  report = net_slot == 0 ? net_delay : 0;
#ifdef PLAT_RUNAHEAD
  ra_start();
#endif
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
#ifndef PLAT_NET_FOLLOW_ONLY
      if (!tries) {                               /* in time: frames early + 1 (the low bytes do: under 16 apart) */
        i = (uint8_t)((uint8_t)avail - (uint8_t)net_frame + 1);
        if (i < w_min) { w_min2 = w_min; w_min = i; }
        else if (i < w_min2) w_min2 = i;
      }
#endif
      break;
    }
#ifndef PLAT_NET_FOLLOW_ONLY
    if (!tries) { w_min2 = w_min; w_min = 0; w_waits++; }   /* it waits */
#endif
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
#ifndef PLAT_NET_FOLLOW_ONLY
    if (((uint8_t)net_frame & (ADAPT_WINDOW - 1)) == ADAPT_WINDOW - 1) adapt_window(keys);
#endif
  }
  net_frame++;
  ahead--;
}

#ifdef PLAT_RUNAHEAD
/* ---- run-ahead (docs/rollback-plan.md): this machine shows a frame ahead
 * of the agreed one. net_frame is the next agreed frame to simulate; the
 * screen shows `shown`. Every step: our keys out for net_frame + net_delay
 * as the lockstep sends them; back to the snapshot of the agreed state
 * when the live one is a guess; the agreed frames that have arrived, up
 * to the one to show (quiet but the last); then the rest guessed: our own
 * keys as sent, the others' last known direction, no fire. A guess is
 * never acted on: a guessed frame that ends the round is dropped and the
 * agreed state kept. The lead is allowed by the frame's measured idle
 * (plat_frame_idle): one more guessed frame when RUNAHEAD_IDLE_MIN ms were
 * left, one fewer after a late frame, then RUNAHEAD_HOLD frames before
 * trying again; never more than the delay (the keys are known that far),
 * nor than net_runahead_cap (a switch). An XT at 4.77 MHz never leaves 0. */
uint8_t net_runahead_cap = NET_DELAY_MAX;
#define RUNAHEAD_IDLE_MIN 12
#define RUNAHEAD_HOLD 32
#define OWN_WIN 16                            /* our input lines kept by frame & 15 (the device gives a frame back only complete) */
static uint16_t sent_to, shown;
static uint8_t own_keys[OWN_WIN][NET_BYTES], last_vec[NET_SLOTS * NET_BYTES];
static uint8_t lead_max, ra_hold;
static uint8_t draw_keep[SCREEN_CELLS];       /* the picture before a guessed frame, kept when that frame is dropped */

static void ra_deal(const uint8_t *keys) {    /* the vector out by the seat table, as the lockstep does */
  uint8_t i, t;
  player_t *p;
  for (i = 0, p = players; i < MAX_PLAYERS; i++, p++) {
    t = net_table[i];
    p->keys = (!net_abort && i < net_total && t != 0xff && p->active) ? keys[t] : 0;
  }
}

static void ra_start(void) {
  sent_to = (uint16_t)(net_delay - 1);
  shown = 0xffff;
  lead_max = ra_hold = 0;
  memset(own_keys, 0, sizeof(own_keys));
  memset(last_vec, 0, sizeof(last_vec));
  sim_quiet = sim_guessed = 0;
}

uint8_t net_runahead_lead(void) { return (uint8_t)(shown + 1 - net_frame); }

uint8_t net_runahead_step(void) {
  static uint8_t keys[NET_SLOTS * NET_BYTES], local[NET_BYTES];
  static net_status_t st;
  uint16_t avail, target, f;
  uint8_t i, s, tries, noted, cap;
  if (net_abort) {                                /* the match is over: the agreed state on the screen */
    if (sim_guessed) { snap_restore(); sim_guessed = 0; }
    for (i = 0; i < MAX_PLAYERS; i++) players[i].keys = 0;
    return 1;
  }
  if (plat_frame_idle == 0) { if (lead_max) lead_max--; ra_hold = RUNAHEAD_HOLD; }
  else if (ra_hold) ra_hold--;
  else if (plat_frame_idle >= RUNAHEAD_IDLE_MIN && lead_max < NET_DELAY_MAX) lead_max++;
  for (i = 0; i < ADAPT_BYTE; i++) local[i] = i < menu_local ? input_read(menu_inputs[i]) : 0;
  local[ADAPT_BYTE] = report;
  while (sent_to < (uint16_t)(net_frame + net_delay)) {   /* one frame's keys twice to go up, none to go down */
    sent_to++;
    memcpy(own_keys[sent_to & (OWN_WIN - 1)], local, NET_BYTES);
    if (net_send(sent_to, local) != 0) { net_fail(); break; }
  }
  target = (uint16_t)(shown + 1);
  if (sim_guessed) { snap_restore(); sim_guessed = 0; }
  tries = 0; noted = 0;
  cap = lead_max < net_runahead_cap ? lead_max : net_runahead_cap;
  if (cap > net_delay) cap = net_delay;
  while (!net_abort && net_frame <= target) {           /* the agreed frames */
    if (net_poll(net_frame, &avail, keys) != 0) { net_fail(); break; }
    if (avail != 0xffff && avail >= net_frame) {
      if (!noted) {                               /* in time: frames early + 1, for the delay's adjustment */
        noted = 1;
        i = (uint8_t)((uint8_t)avail - (uint8_t)net_frame + 1);
        if (i < w_min) { w_min2 = w_min; w_min = i; }
        else if (i < w_min2) w_min2 = i;
      }
      ra_deal(keys);
      sim_quiet = (net_frame != target);
      sim_frame_begin();
      frame_logic(0);
      memcpy(last_vec, keys, sizeof(last_vec));
      i = keys[ADAPT_BYTE];                       /* the host's delay */
      if (net_slot && i && i <= NET_DELAY_MAX) net_delay = i;
      if (((uint8_t)net_frame & (ADAPT_WINDOW - 1)) == ADAPT_WINDOW - 1) adapt_window(keys);
      net_frame++;
      continue;
    }
    if (!noted) { noted = 1; w_min2 = w_min; w_min = 0; w_waits++; }   /* late: counted as a wait, guessed or not */
    if ((uint16_t)(target - net_frame + 1) <= cap) break;            /* the rest guessed */
    if (++tries > 4) {                            /* waits, as the lockstep */
      net_waiting = 1;
      if ((tries & 15) == 0) {
        if (net_status(&st) == 0 &&
            (st.state == NETST_DESYNC || st.state == NETST_DROPPED || st.state == NETST_NOLINK))
          net_abort = st.state ? st.state : 9;
        else if (plat_key_char() == 0x1b) net_abort = NET_ABORT_BREAK;
      }
    }
  }
  sim_quiet = 0;
  if (net_abort) { for (i = 0; i < MAX_PLAYERS; i++) players[i].keys = 0; return 1; }
  net_waiting = 0;
  if (net_frame <= target) {                      /* the guessed frames */
    snap_save(); sim_guessed = 1;
    for (f = net_frame; f <= target; f++) {
      for (s = 0; s < NET_SLOTS; s++)
        for (i = 0; i < NET_BYTES; i++)
          keys[s * NET_BYTES + i] = s == net_slot ? own_keys[f & (OWN_WIN - 1)][i]
                                                  : (uint8_t)(last_vec[s * NET_BYTES + i] & (uint8_t)~KEY_SPACE);
      memcpy(draw_keep, draw_buf, SCREEN_CELLS);
      ra_deal(keys);
      sim_quiet = (f != target);
      sim_frame_begin();
      frame_logic(1);
      if (frame_ends()) {                         /* not from a guess: the agreed state back, the previous picture kept */
        snap_restore(); sim_guessed = 0;
        memcpy(draw_buf, draw_keep, SCREEN_CELLS);
        target = (uint16_t)(net_frame - 1);
        break;
      }
    }
    sim_quiet = 0;
  }
  shown = target;
  return 1;
}
#endif

void net_match_end(void) {
  if (!net_active) return;
  net_active = 0;
  net_leave();
}
