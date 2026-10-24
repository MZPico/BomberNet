/* The PC's network test only (tools/build_pc.py --net): the core's calls into
 * the network device timed. netdev_soft.c is compiled with its net_send,
 * net_poll, net_hash and net_status renamed nd_*; these wrap them. */
#include <stdint.h>
#include "netdev.h"

uint32_t pc_time_now(void);
uint32_t pc_nd_counts;                    /* timer counts in the calls since the last frame (plat_pc.c logs them) */
uint16_t pc_nd_polls;                     /* net_poll calls: more than one a frame is waiting for the others' keys */
uint8_t nd_net_send(uint16_t frame, const uint8_t keys[NET_BYTES]);
uint8_t nd_net_poll(uint16_t frame, uint16_t *avail, uint8_t keys[NET_SLOTS * NET_BYTES]);
uint8_t nd_net_hash(uint16_t frame, uint16_t hash);
uint8_t nd_net_status(net_status_t *st);

uint8_t net_send(uint16_t frame, const uint8_t keys[NET_BYTES]) {
  uint32_t t = pc_time_now(); uint8_t r = nd_net_send(frame, keys); pc_nd_counts += pc_time_now() - t; return r;
}
uint8_t net_poll(uint16_t frame, uint16_t *avail, uint8_t keys[NET_SLOTS * NET_BYTES]) {
  uint32_t t = pc_time_now(); uint8_t r = nd_net_poll(frame, avail, keys); pc_nd_counts += pc_time_now() - t; pc_nd_polls++; return r;
}
uint8_t net_hash(uint16_t frame, uint16_t hash) {
  uint32_t t = pc_time_now(); uint8_t r = nd_net_hash(frame, hash); pc_nd_counts += pc_time_now() - t; return r;
}
uint8_t net_status(net_status_t *st) {
  uint32_t t = pc_time_now(); uint8_t r = nd_net_status(st); pc_nd_counts += pc_time_now() - t; return r;
}
