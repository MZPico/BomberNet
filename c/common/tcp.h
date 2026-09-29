/* One TCP connection, non-blocking receive. Implemented per machine:
 * platform/zx/tcp_spectranet.c (Spectranet socket calls), and on the PC
 * platform/host/tcp_posix.c for tests. */
#ifndef TCP_H
#define TCP_H
#include <stdint.h>
uint8_t tcp_present(void);                         /* 1: a network interface is fitted */
uint8_t tcp_open(const char *host, uint16_t port); /* 0, else an error code (resolve, connect) */
uint8_t tcp_send(const uint8_t *buf, uint16_t n);  /* 0, else an error code */
int16_t tcp_recv(uint8_t *buf, uint16_t max);      /* bytes read, 0 = nothing waiting, -1 = closed */
void tcp_close(void);
#endif
