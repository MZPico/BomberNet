/* tcp.h over bsdsocket.library (docs/port-amiga.md): the socket API every
 * Amiga TCP/IP stack provides (Roadshow, AmiTCP, Miami, Genesis), over
 * any hardware, and which FS-UAE and WinUAE implement themselves. One
 * socket, non-blocking: the stack receives in the background, so a
 * message is taken by the first poll after it arrives. Without a stack
 * (the library does not open) the game plays locally. */
#include <stdint.h>
#include <stdlib.h>
#include <string.h>
#include <errno.h>
#include <proto/exec.h>
#include <proto/dos.h>
#include <proto/bsdsocket.h>
#include <sys/socket.h>
#include <sys/ioctl.h>
#include <netinet/in.h>
#include <netdb.h>
#include "tcp.h"

struct Library *SocketBase;
static LONG sock = -1;

static void socket_cleanup(void) {
  if (sock >= 0) CloseSocket(sock);
  sock = -1;
  if (SocketBase) CloseLibrary(SocketBase);
  SocketBase = NULL;
}

uint8_t tcp_present(void) {
  if (!SocketBase) {
    SocketBase = OpenLibrary("bsdsocket.library", 3);
    if (SocketBase) atexit(socket_cleanup);
  }
  return SocketBase != NULL;
}

uint8_t tcp_open(const char *host, uint16_t port) {
  struct hostent *h;
  struct sockaddr_in a;
  LONG one = 1;
  if (!SocketBase) return 1;
  tcp_close();
  h = gethostbyname((STRPTR)host);          /* a name or a dotted address */
  if (!h || h->h_addrtype != AF_INET) return 2;
  memset(&a, 0, sizeof(a));
  a.sin_len = sizeof(a);
  a.sin_family = AF_INET;
  a.sin_port = htons(port);
  memcpy(&a.sin_addr, h->h_addr_list[0], sizeof(a.sin_addr));
  sock = socket(AF_INET, SOCK_STREAM, 0);
  if (sock < 0) return 3;
  if (connect(sock, (struct sockaddr *)&a, sizeof(a)) < 0) { CloseSocket(sock); sock = -1; return 4; }
  IoctlSocket(sock, FIONBIO, (char *)&one);  /* non-blocking from here: the game polls */
  return 0;
}

uint8_t tcp_send(const uint8_t *buf, uint16_t n) {
  uint16_t tries = 0;
  if (sock < 0) return 1;
  while (n) {
    LONG k;
    k = send(sock, (APTR)buf, n, 0);
    if (k > 0) { buf += k; n = (uint16_t)(n - k); tries = 0; continue; }
    if (k < 0 && Errno() != EWOULDBLOCK) return 2;
    if (++tries > 250) return 2;             /* the stack's buffer full for 5 s */
    Delay(1);                                /* 20 ms: let the stack send */
  }
  return 0;
}

int16_t tcp_recv(uint8_t *buf, uint16_t max) {
  LONG n;
  if (sock < 0) return -1;
  n = recv(sock, (APTR)buf, max, 0);
  if (n > 0) return (int16_t)n;
  if (n == 0) return -1;                     /* closed by the other side */
  return Errno() == EWOULDBLOCK ? 0 : -1;
}

void tcp_close(void) {
  if (sock >= 0) CloseSocket(sock);
  sock = -1;
}
