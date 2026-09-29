/* Test of the software network device on the PC: two of these, one creating
 * a room and one joining it, play N lockstep frames through the relay and
 * check each other's inputs and messages.
 *   softnet_test host N          prints "CODE xxxx", then runs
 *   softnet_test join N CODE
 * The relay comes from NET_RELAY=host:port (default 127.0.0.1:8765). */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include "netdev.h"

extern const char *net_relay_host;
extern uint16_t net_relay_port;
void plat_delay(void) { usleep(20000); }

static int fail(const char *what, int r) { printf("FAIL %s (%d)\n", what, r); return 1; }

int main(int argc, char **argv) {
  static char host[64];
  const char *relay = getenv("NET_RELAY");
  int is_host = argc > 1 && !strcmp(argv[1], "host"), n = argc > 2 ? atoi(argv[2]) : 40, f, t;
  uint8_t slot, slots = 2, len = 0, from, m[32], settings[16] = {1, 2}, keys[16], mine[4], r, got_msg = 0;
  char code[5];
  uint16_t seed, start, avail;
  net_status_t st;
  strcpy(host, relay ? relay : "127.0.0.1:8765");
  if (strchr(host, ':')) { net_relay_port = (uint16_t)atoi(strchr(host, ':') + 1); *strchr(host, ':') = 0; }
  net_relay_host = host;
  net_detect();
  if (net_device != NETDEV_NET) return fail("detect", net_device);
  if (is_host) {
    if ((r = net_create(0x0604, 2, settings, 2, code, &slot))) return fail("create", r);
    printf("CODE %s slot %u\n", code, slot); fflush(stdout);
  } else {
    if (argc < 4) return fail("usage", 0);
    if ((r = net_join(0x0604, argv[3], &slot, &slots, settings, &len))) return fail("join", r);
    printf("JOINED slot %u slots %u settings %u:%02x%02x\n", slot, slots, len, settings[0], settings[1]); fflush(stdout);
    if (len != 2 || settings[0] != 1 || settings[1] != 2) return fail("settings", len);
  }
  for (t = 0; t < 500; t++) { net_status(&st); if (st.members == 2) break; plat_delay(); }
  if (st.members != 2) return fail("members", st.members);
  m[0] = 'H'; m[1] = slot;
  net_msg_send(0xff, m, 2);
  for (t = 0; t < 500 && !got_msg; t++) {
    if (net_msg_recv(&from, m) == 2 && m[0] == 'H' && m[1] == from) got_msg = 1;
    else plat_delay();
  }
  if (!got_msg) return fail("message", 0);
  for (t = 0; t < 500; t++) {
    if ((r = net_ready(1, &seed, &start))) return fail("ready", r);
    if (seed != 0xffff) break;
    plat_delay();
  }
  if (seed == 0xffff) return fail("start", 0);
  printf("START seed %04x frame %u\n", seed, start); fflush(stdout);
  for (f = 0; f < 3; f++) { mine[0] = 0; mine[1] = mine[2] = mine[3] = 0; net_send((uint16_t)f, mine); }
  for (f = 0; f < n; f++) {
    mine[0] = (uint8_t)(f * 7 + slot); mine[1] = (uint8_t)(0x40 + slot); mine[2] = mine[3] = 0;
    if ((r = net_send((uint16_t)(f + 3), mine))) return fail("send", r);
    for (t = 0; t < 500; t++) {
      if ((r = net_poll((uint16_t)f, &avail, keys))) return fail("poll", r);
      if (avail != 0xffff && avail >= f) break;
      plat_delay();
    }
    if (t == 500) return fail("frame wait", f);
    if (f >= 3) {
      uint8_t other = (uint8_t)(1 - slot), e0 = (uint8_t)((f - 3) * 7 + other);
      if (keys[other * 4] != e0 || keys[other * 4 + 1] != 0x40 + other || keys[slot * 4] != (uint8_t)((f - 3) * 7 + slot))
        { printf("frame %d keys %02x %02x | %02x %02x\n", f, keys[0], keys[1], keys[4], keys[5]); return fail("keys", f); }
    }
    if ((f & 15) == 15) net_hash((uint16_t)f, (uint16_t)(0x1234 + f));
  }
  net_status(&st);
  if (st.state != NETST_RUNNING) return fail("state", st.state);
  for (t = 0; t < 50; t++) { net_status(&st); plat_delay(); }   /* let the other side finish */
  net_leave();
  printf("OK %d frames\n", n);
  return 0;
}
