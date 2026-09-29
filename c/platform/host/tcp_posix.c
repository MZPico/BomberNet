/* tcp.h on the PC, for tests of the software network device.
 * NET_TCP_PRESENT=0 in the environment simulates a machine without a network. */
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <poll.h>
#include <netdb.h>
#include <sys/socket.h>
#include "tcp.h"

static int fd = -1;

uint8_t tcp_present(void) {
  const char *v = getenv("NET_TCP_PRESENT");
  return !(v && *v == '0');
}

uint8_t tcp_open(const char *host, uint16_t port) {
  struct addrinfo hints, *res;
  char ps[8];
  tcp_close();
  memset(&hints, 0, sizeof(hints));
  hints.ai_family = AF_INET;
  hints.ai_socktype = SOCK_STREAM;
  snprintf(ps, sizeof(ps), "%u", port);
  if (getaddrinfo(host, ps, &hints, &res)) return 0xef;           /* as the Spectranet: host not found */
  fd = socket(res->ai_family, res->ai_socktype, 0);
  if (fd < 0 || connect(fd, res->ai_addr, res->ai_addrlen)) { freeaddrinfo(res); tcp_close(); return 0xfa; }
  freeaddrinfo(res);
  return 0;
}

uint8_t tcp_send(const uint8_t *buf, uint16_t n) {
  if (fd < 0) return 0xfd;
  while (n) {
    ssize_t w = send(fd, buf, n, MSG_NOSIGNAL);
    if (w <= 0) return 0xfc;
    buf += w; n -= (uint16_t)w;
  }
  return 0;
}

int16_t tcp_recv(uint8_t *buf, uint16_t max) {
  struct pollfd p;
  ssize_t r;
  if (fd < 0) return -1;
  p.fd = fd; p.events = POLLIN; p.revents = 0;
  if (poll(&p, 1, 2) <= 0) return 0;              /* a moment, like a slow machine */
  r = recv(fd, buf, max, 0);
  return r <= 0 ? -1 : (int16_t)r;
}

void tcp_close(void) {
  if (fd >= 0) close(fd);
  fd = -1;
}
