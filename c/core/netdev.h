/* Network device interface: what the core needs from "the network", as ten
 * calls (docs/net-protocol.md). Two kinds of implementation:
 *   - a card that does the networking itself (common/netdev_card.c over the
 *     Unicard-style port transport): MZPico, emulators, the host stub;
 *   - a software device running on the machine (planned: ZX Spectrum with a
 *     socket interface), same calls, same semantics. */
#ifndef NETDEV_H
#define NETDEV_H
#include <stdint.h>

#define NETDEV_NONE    0
#define NETDEV_UNICARD 1      /* a Unicard: files only */
#define NETDEV_MZPICO  2      /* MZPico without NET support */
#define NETDEV_NET     3      /* MZPico with NET */
extern uint8_t net_device;
void net_detect(void);

/* NET client API (docs/net-protocol.md). All return 0 on success, else the
 * device error code (6 build mismatch, 7 room unknown/full, 8 not in room,
 * 9 not linked, 10 bad parameter, 11 buffer full, 0xff no output). */
#define NET_GAME_ID  0x424e      /* 'BN' */
#define NET_SLOTS    4
#define NET_BYTES    4          /* input bytes per slot: one per local player of that device */
typedef struct {
  uint8_t state;        /* NETST_* */
  uint8_t slot, members, ready_mask, rtt, buffered, msgs, last_error;
} net_status_t;
#define NETST_NOLINK 0
#define NETST_READY  1
#define NETST_INROOM 2
#define NETST_RUNNING 3
#define NETST_DESYNC 4
#define NETST_DROPPED 5
#define NETST_SPECTATOR 6
uint8_t net_status(net_status_t *st);
uint8_t net_create(uint16_t build, uint8_t slots, const uint8_t *settings, uint8_t len, char code[5], uint8_t *slot);
uint8_t net_join(uint16_t build, const char *code, uint8_t *slot, uint8_t *slots, uint8_t *settings, uint8_t *len);
uint8_t net_leave(void);
uint8_t net_ready(uint8_t ready, uint16_t *seed, uint16_t *start_frame);   /* 0xffff while waiting */
uint8_t net_send(uint16_t frame, const uint8_t keys[NET_BYTES]);
uint8_t net_poll(uint16_t frame, uint16_t *avail, uint8_t keys[NET_SLOTS * NET_BYTES]);
uint8_t net_hash(uint16_t frame, uint16_t hash);
uint8_t net_msg_send(uint8_t to, const uint8_t *data, uint8_t len);
uint8_t net_msg_recv(uint8_t *from, uint8_t *data);                       /* returns len (0 = none) */
#endif
