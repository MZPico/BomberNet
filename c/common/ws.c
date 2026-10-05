/* Minimal WebSocket client (RFC 6455) over tcp.h, as much as the relay needs:
 * one text message per line, unfragmented, ping answered, close honoured.
 *
 * Client frames must be masked; the mask key is 0, so the payload goes out
 * as it is (no XOR on a Z80). The handshake reply is read as a stream: only
 * the status line and the empty line that ends the headers matter (a proxy
 * may send 600 bytes of headers). The Sec-WebSocket-Accept is not checked. */
#include <stdint.h>
#include <string.h>
#include "platform.h"
#include "tcp.h"
#include "ws.h"

uint8_t ws_open_now, ws_lost;

static uint8_t rx[64];                     /* bytes from the socket not yet parsed */
static uint8_t rx_pos, rx_len;
static char lbuf[WS_HDR + WS_LINE_MAX];     /* received line, with room for a pong header */
#define line (lbuf + WS_HDR)
static uint8_t f_state, f_op;              /* frame parser: 0 opcode, 1 length, 2..3 ext length, 4 payload */
static uint16_t f_need, f_have;

/* the frame header is written into the WS_HDR bytes in front of the text */
#ifdef ESP_FAST128
static uint8_t send_frame(uint8_t op, char *p, uint16_t n) __naked {
 __asm
    pop af
    pop bc
    pop hl
    pop de
    push de
    push hl
    push bc
    push af
    ld a,b
    or a
    jr nz,w11_size
    ld a,c
    cp 161
    jr nc,w11_size
    dec hl
    ld (hl),0
    dec hl
    ld (hl),0
    dec hl
    ld (hl),0
    dec hl
    ld (hl),0
    ld a,c
    cp 126
    jr c,w11_short
    dec hl
    ld (hl),c
    dec hl
    ld (hl),b
    inc bc
    inc bc
    ld a,126
w11_short:
    or 128
    dec hl
    ld (hl),a
    ld a,e
    or 128
    dec hl
    ld (hl),a
    ld de,6
    ex de,hl
    add hl,bc
    push de
    push hl
    call _tcp_send
    pop bc
    pop bc
    ld a,l
    or a
    ret z
    ld a,1
    ld (_ws_lost),a
    xor a
    ld (_ws_open_now),a
    ld hl,9
    ret
w11_size:
    ld hl,11
    ret

 __endasm;
}
#else
static uint8_t send_frame(uint8_t op, char *p, uint16_t n) {
  uint8_t *h = (uint8_t *)p;
  if (n > WS_LINE_MAX) return 11;
  *--h = 0; *--h = 0; *--h = 0; *--h = 0;               /* mask key 0 */
  if (n < 126) *--h = 0x80 | (uint8_t)n;
  else { *--h = (uint8_t)n; *--h = (uint8_t)(n >> 8); *--h = 0x80 | 126; }
  *--h = 0x80 | op;
  if (tcp_send(h, (uint16_t)((uint8_t *)p - h) + n)) { ws_lost = 1; ws_open_now = 0; return 9; }
  return 0;
}
#endif

uint8_t ws_send(char *text) {
  if (!ws_open_now) return 9;
  return send_frame(0x1, text, (uint16_t)strlen(text));
}

static int16_t fill(void) {                 /* more bytes into rx; 0 = none, -1 = closed */
  int16_t n = tcp_recv(rx, sizeof(rx));
  if (n < 0) { ws_lost = 1; ws_open_now = 0; return -1; }
  rx_pos = 0; rx_len = (uint8_t)n;
  return n;
}

uint8_t ws_open(const char *host, uint16_t port, const char *path) {
  static const char k1[] = " HTTP/1.1\r\nHost: ";
  static const char k2[] = "\r\nUpgrade: websocket\r\nConnection: Upgrade\r\n"
                           "Sec-WebSocket-Key: dGhlIHNhbXBsZSBub25jZQ==\r\n"
                           "Sec-WebSocket-Version: 13\r\nUser-Agent: BomberNet\r\n\r\n";
  uint16_t t;
  uint8_t c, status = 0, n = 0;
  uint32_t tail = 0;
  ws_close();
  ws_lost = 0;
  if (tcp_open(host, port)) return 9;
  /* the request in pieces: TCP does not care, and no buffer is needed for it */
  if (tcp_send((const uint8_t *)"GET ", 4) || tcp_send((const uint8_t *)path, (uint16_t)strlen(path)) ||
      tcp_send((const uint8_t *)k1, sizeof(k1) - 1) || tcp_send((const uint8_t *)host, (uint16_t)strlen(host)) ||
      tcp_send((const uint8_t *)k2, sizeof(k2) - 1)) { tcp_close(); return 9; }
  rx_pos = rx_len = 0;
  for (t = 0; t < 500; t++) {              /* about 10 s */
    while (rx_pos < rx_len) {
      c = rx[rx_pos++];
      if (n < 12) {                        /* "HTTP/1.1 101" */
        if (n == 9 && c != '1') status = 1;
        if (n == 10 && c != '0') status = 1;
        if (n == 11 && c != '1') status = 1;
        n++;
      }
      tail = (tail << 8) | c;
      if (tail == 0x0d0a0d0aUL) {
        if (status || n < 12) { tcp_close(); return 9; }
        ws_open_now = 1;
        f_state = 0;
        return 0;
      }
    }
    if (fill() < 0) { tcp_close(); return 9; }
    if (rx_len == 0) plat_delay();
  }
  tcp_close();
  return 9;
}

#ifdef ESP_FAST128
const char *ws_poll(void) __naked {
 __asm
    ld a,(_ws_open_now)
    or a
    jp z,w9_none
w9_again:
    ld a,(_rx_pos)
    ld c,a
    ld a,(_rx_len)
    sub c
    jr nz,w9_available
    call _fill
    ld a,h
    or a
    jp nz,w9_none
    ld a,l
    or a
    jp z,w9_none
w9_available:
    ld a,(_f_state)
    cp 4
    jp z,w9_payload
    ld a,(_rx_pos)
    ld e,a
    inc a
    ld (_rx_pos),a
    ld d,0
    ld hl,_rx
    add hl,de
    ld c,(hl)
    ld a,(_f_state)
    or a
    jr z,w9_opcode
    dec a
    jr z,w9_length
    dec a
    jr z,w9_high
    ld hl,(_f_need)
    ld l,c
    jr w9_need
w9_opcode:
    ld a,c
    and 0xf0
    cp 0x80
    jp nz,w9_close
    ld a,c
    and 15
    ld (_f_op),a
    ld a,1
    ld (_f_state),a
    jr w9_again
w9_length:
    ld a,c
    and 127
    cp 127
    jp z,w9_close
    ld hl,0
    ld (_f_have),hl
    cp 126
    jr nz,w9_short
    ld a,2
    ld (_f_state),a
    jr w9_again
w9_short:
    ld l,a
    jr w9_need
w9_high:
    ld h,c
    ld l,0
    ld (_f_need),hl
    ld a,3
    ld (_f_state),a
    jr w9_again
w9_need:
    ld (_f_need),hl
    ld a,4
    ld (_f_state),a
    ld a,h
    or l
    jp z,w9_done
    jp w9_again
w9_payload:
    ld hl,(_f_need)
    ld de,(_f_have)
    or a
    sbc hl,de
    ld a,(_rx_pos)
    ld c,a
    ld a,(_rx_len)
    sub c
    ld c,a
    ld b,0
    ld a,h
    or a
    jr nz,w9_n
    ld a,l
    cp c
    jr nc,w9_n
    ld c,a
w9_n:
    push bc
    ld hl,(_f_have)
    ld a,h
    or a
    jr nz,w9_discard
    ld a,l
    cp 159
    jr nc,w9_discard
    ld a,159
    sub l
    cp c
    jr nc,w9_copy
    ld c,a
w9_copy:
    ld de,_lbuf+8
    add hl,de
    ex de,hl
    ld a,(_rx_pos)
    ld l,a
    ld h,0
    ld bc,_rx
    add hl,bc
    pop bc
    push bc
    ld a,e
    ; recover bounded copy count from remaining line room
    ld a,(_f_have)
    ld b,a
    ld a,159
    sub b
    cp c
    jr nc,w9_copy_n
    ld c,a
w9_copy_n:
    ld b,0
    ldir
w9_discard:
    pop bc
    ld a,(_rx_pos)
    add a,c
    ld (_rx_pos),a
    ld hl,(_f_have)
    add hl,bc
    ld (_f_have),hl
    ld de,(_f_need)
    or a
    sbc hl,de
    jp nz,w9_again
w9_done:
    xor a
    ld (_f_state),a
    ld hl,(_f_have)
    ld a,h
    or a
    jr nz,w9_trunc
    ld a,l
    cp 159
    jr c,w9_term
w9_trunc:
    ld hl,159
w9_term:
    ld de,_lbuf+8
    add hl,de
    ld (hl),0
    ld a,(_f_op)
    cp 1
    jr z,w9_text
    cp 8
    jr z,w9_close
    cp 9
    jp nz,w9_again
    ld hl,10
    push hl
    ld hl,_lbuf+8
    push hl
    ld hl,(_f_have)
    ld a,h
    or a
    jr nz,w9_pingmax
    ld a,l
    cp 160
    jr c,w9_pinglen
w9_pingmax:
    ld hl,159
w9_pinglen:
    push hl
    call _send_frame
    pop bc
    pop bc
    pop bc
    jp w9_again
w9_close:
    ld a,1
    ld (_ws_lost),a
    call _ws_close
w9_none:
    ld hl,0
    ret
w9_text:
    ld hl,_lbuf+8
    ret
 __endasm;
}
#else
const char *ws_poll(void) {
  uint8_t c;
  uint16_t n, room;
  if (!ws_open_now) return 0;
  for (;;) {
    if (rx_pos >= rx_len && fill() <= 0) return 0;
    while (rx_pos < rx_len) {
      if (f_state == 4) {                  /* payload: as much as there is, in one copy */
        n = rx_len - rx_pos;
        if (n > f_need - f_have) n = f_need - f_have;
        room = f_have < WS_LINE_MAX - 1 ? WS_LINE_MAX - 1 - f_have : 0;
        memcpy(line + f_have, rx + rx_pos, n < room ? n : room);
        f_have += n;
        rx_pos += (uint8_t)n;
        if (f_have < f_need) continue;
        goto done;
      }
      c = rx[rx_pos++];
      switch (f_state) {
      case 0:
#ifdef ESP_STREAM
        /* Transparent ESP status text/EOF is not a valid relay frame. */
        if ((c & 0xf0) != 0x80) { ws_lost=1; ws_close(); return 0; }
#endif
        f_op = c & 0x0f; f_state = 1; break;
      case 1:
        c &= 0x7f;                         /* server frames are not masked */
        if (c == 127) { ws_lost = 1; ws_close(); return 0; }   /* 64-bit length: never from the relay */
        f_have = 0;
        if (c == 126) { f_state = 2; break; }
        f_need = c;
        f_state = 4;
        if (f_need) break;
        goto done;
      case 2: f_need = (uint16_t)c << 8; f_state = 3; break;
      case 3: f_need |= c; f_state = 4; if (f_need) break; goto done;
      default:
      done:
        f_state = 0;
        line[f_have < WS_LINE_MAX - 1 ? f_have : WS_LINE_MAX - 1] = 0;
        if (f_op == 0x1) return line;
        if (f_op == 0x9) send_frame(0xa, line, f_have < WS_LINE_MAX ? f_have : WS_LINE_MAX - 1);   /* ping -> pong, same (binary) payload */
        if (f_op == 0x8) { ws_lost = 1; ws_close(); return 0; }
        break;
      }
    }
  }
}

#endif

void ws_close(void) {
  if (ws_open_now) { line[0] = 0; send_frame(0x8, line, 0); }
  if (ws_open_now || ws_lost) tcp_close();
  ws_open_now = 0;
  rx_pos = rx_len = 0;
  f_state = 0;
}
