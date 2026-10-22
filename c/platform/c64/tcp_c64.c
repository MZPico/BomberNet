/* tcp.h on the C64: an Ultimate's own network (its command interface,
 * tcp_uci.c) when it answers, else a WiC64 (tcp_wic64.c). The Ultimate is
 * asked first: its identification register answers at once, while a
 * missing WiC64 is only known after a timeout. */
#include <stdint.h>
#include "tcp.h"
#include "tcp_c64.h"

static uint8_t dev;                       /* 1 the Ultimate, 2 a WiC64 */

uint8_t tcp_present(void) {
  dev = uci_present() ? 1 : wic64_present() ? 2 : 0;
  return dev != 0;
}

uint8_t tcp_open(const char *host, uint16_t port) {
  return dev == 1 ? uci_open(host, port) : wic64_open(host, port);
}

uint8_t tcp_send(const uint8_t *buf, uint16_t n) {
  return dev == 1 ? uci_send(buf, n) : wic64_send(buf, n);
}

int16_t tcp_recv(uint8_t *buf, uint16_t max) {
  return dev == 1 ? uci_recv(buf, max) : wic64_recv(buf, max);
}

void tcp_close(void) {
  if (dev == 1) uci_close(); else wic64_close();
}
