/* tcp.h on the PC: Ethernet through a packet driver (tcp_pkt.c) where one
 * is loaded, else a WiFi modem on a serial port (tcp_modem.c); /NET=PKT or
 * /NET=MODEM on the command line takes only the one. */
#include <stdint.h>
#include "tcp.h"

extern uint8_t pc_net;                    /* plat_pc.c: 0 either, 'P' the packet driver, 'M' the modem */
uint8_t pkt_present(void), pkt_open(const char *host, uint16_t port), pkt_send(const uint8_t *buf, uint16_t n);
int16_t pkt_recv(uint8_t *buf, uint16_t max);
void pkt_close(void);
uint8_t modem_present(void), modem_open(const char *host, uint16_t port), modem_send(const uint8_t *buf, uint16_t n);
int16_t modem_recv(uint8_t *buf, uint16_t max);
void modem_close(void);

static uint8_t pkt;                       /* 1: Ethernet */

uint8_t tcp_present(void) {
  if (pc_net != 'M' && pkt_present()) { pkt = 1; return 1; }
  return pc_net != 'P' && modem_present();
}
uint8_t tcp_open(const char *host, uint16_t port) { return pkt ? pkt_open(host, port) : modem_open(host, port); }
#ifdef PC_NETTEST                         /* the network test: the time in the calls (plat_pc.c logs it a frame) */
uint32_t pc_time_now(void);
extern uint32_t pc_net_counts;
extern uint16_t pc_net_calls;
uint8_t tcp_send(const uint8_t *buf, uint16_t n) {
  uint32_t t = pc_time_now();
  uint8_t r = pkt ? pkt_send(buf, n) : modem_send(buf, n);
  pc_net_counts += pc_time_now() - t;
  return r;
}
int16_t tcp_recv(uint8_t *buf, uint16_t max) {
  uint32_t t = pc_time_now();
  int16_t r = pkt ? pkt_recv(buf, max) : modem_recv(buf, max);
  pc_net_counts += pc_time_now() - t; pc_net_calls++;
  return r;
}
#else
uint8_t tcp_send(const uint8_t *buf, uint16_t n) { return pkt ? pkt_send(buf, n) : modem_send(buf, n); }
int16_t tcp_recv(uint8_t *buf, uint16_t max) { return pkt ? pkt_recv(buf, max) : modem_recv(buf, max); }
#endif
void tcp_close(void) { if (pkt) pkt_close(); else modem_close(); }
