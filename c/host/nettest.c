/* Exercise the NET client against the host stub device (SIM_NET=1). */
#include <stdio.h>
#include <stdint.h>
#include <string.h>
#include "game.h"
#include "uc.h"
int nettest(void) {
  net_status_t s; char code[5]; uint8_t slot, slots, keys[4], r, sl; uint16_t seed, start, avail; uint8_t sbuf[16];
  net_detect();
  printf("net_device %u\n", net_device);
  r = net_status(&s); printf("status r=%u state=%u\n", r, s.state);
  r = net_create(0x0101, 2, (const uint8_t *)"\x01\x02", 2, code, &slot); printf("create r=%u code=%s slot=%u\n", r, code, slot);
  r = net_join(0x0101, "ZZZZ", &slot, &slots, sbuf, &sl); printf("join(unknown) r=%u\n", r);
  r = net_ready(1, &seed, &start); printf("ready r=%u seed=%04x start=%u\n", r, seed, start);
  r = net_send(3, 0x15); printf("send r=%u\n", r);
  r = net_poll(3, &avail, keys); printf("poll r=%u avail=%u keys=%02x %02x %02x %02x\n", r, avail, keys[0], keys[1], keys[2], keys[3]);
  r = net_poll(4, &avail, keys); printf("poll(4) r=%u avail=%u\n", r, avail);
  r = net_hash(3, 0xbeef); printf("hash r=%u\n", r);
  r = net_msg_recv(&slot, keys); printf("msg len=%u\n", r);
  r = net_leave(); printf("leave r=%u\n", r);
  r = net_status(&s); printf("status state=%u\n", s.state);
  return 0;
}
