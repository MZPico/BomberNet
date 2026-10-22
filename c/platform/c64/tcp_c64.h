/* The C64's two network devices behind tcp.h (tcp_c64.c): a WiC64 on the
 * user port (tcp_wic64.c), or an Ultimate's own network (Ultimate 64,
 * Ultimate-II+: its command interface, tcp_uci.c). */
#ifndef TCP_C64_H
#define TCP_C64_H
#include <stdint.h>
uint8_t wic64_present(void);
uint8_t wic64_open(const char *host, uint16_t port);
uint8_t wic64_send(const uint8_t *buf, uint16_t n);
int16_t wic64_recv(uint8_t *buf, uint16_t max);
void wic64_close(void);
uint8_t uci_present(void);
uint8_t uci_open(const char *host, uint16_t port);
uint8_t uci_send(const uint8_t *buf, uint16_t n);
int16_t uci_recv(uint8_t *buf, uint16_t max);
void uci_close(void);
#endif
