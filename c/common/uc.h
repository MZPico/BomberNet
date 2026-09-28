/* Unicard-compatible card transport: command and data port, 4-byte status.
 * The primitives are implemented per platform (platform/mz/uc_mz.c: ports
 * 50h/51h; the host simulator stubs them). common/netdev_card.c builds the
 * network device interface (core/netdev.h) on top of them. */
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
#endif
