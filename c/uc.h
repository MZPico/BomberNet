/* Unicard-compatible repository client (MZPico device on ports 50h/51h).
 * Transport primitives copied from the MZPico manager's mz-comm.c. */
#ifndef UC_H
#define UC_H
#include <stdint.h>
#define UC_CMD_PORT  0x50
#define UC_DATA_PORT 0x51
#define cmdSTSR      0x03
#define cmdSTORNO    0x04
#define cmdREVD      0x06
#define cmdX_INFO    0x95
/* MZPico NET extensions, docs/net-protocol.md */
#define cmdN_STATUS  0xa0
#define cmdN_CREATE  0xa1
#define cmdN_JOIN    0xa2
#define cmdN_LEAVE   0xa3
#define cmdN_READY   0xa4
#define cmdN_SEND    0xa5
#define cmdN_POLL    0xa6
#define cmdN_HASH    0xa7
#define cmdN_MSG     0xa8
#define cmdN_RECV    0xa9
#define UC_ST_BUSY   0x01
#define UC_ST_OUTPUT 0x02
#define UC_ST_INPROG 0x40
#define UC_ST_ERROR  0x80
#define UC_INFO_NET  0x08     /* INFO feature bit: NET commands available */

void uc_cmd(uint8_t command);
void uc_wr(uint8_t data);
uint8_t uc_rd(void);
void uc_status4(uint8_t *status);        /* STSR, then the 4 status bytes */
void uc_read(uint8_t *dst, uint16_t n);  /* n data-port bytes via INIR */
void uc_wstr(const char *s);             /* string parameter, 0x0D terminated */

/* ---- net.c ---- */
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
#define NET_BYTES    1
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
uint8_t net_send(uint16_t frame, uint8_t keys);
uint8_t net_poll(uint16_t frame, uint16_t *avail, uint8_t keys[NET_SLOTS]);
uint8_t net_hash(uint16_t frame, uint16_t hash);
uint8_t net_msg_send(uint8_t to, const uint8_t *data, uint8_t len);
uint8_t net_msg_recv(uint8_t *from, uint8_t *data);                       /* returns len (0 = none) */
#endif
