/* Minimal WebSocket client for the relay: text frames of one line each. */
#ifndef WS_H
#define WS_H
#include <stdint.h>
#define WS_LINE_MAX 160
#define WS_HDR 8                                   /* room a sender leaves in front of its text */
uint8_t ws_open(const char *host, uint16_t port, const char *path);  /* 0, else 9 (no link) */
uint8_t ws_send(char *text);                       /* text with WS_HDR bytes of room in front; 0, else 11 or 9 */
const char *ws_poll(void);                         /* next received line or 0; valid until the next call */
void ws_close(void);
extern uint8_t ws_open_now;                        /* connection up */
extern uint8_t ws_lost;                            /* set when the peer closed or the link failed */
#endif
