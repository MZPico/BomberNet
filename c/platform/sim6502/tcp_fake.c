/* tcp.h for the 6502 measurement platform: a relay inside the program, so
 * that the real network code (common/netdev_soft.c, ws.c) runs as in a match.
 * It answers the WebSocket handshake, "create" with a room of sim_slots
 * slots, "ready" with the start, and every input line of ours with the
 * input lines of the other slots for the same frame (their keys from
 * sim_peer_keys, the recording). Server frames are unmasked, as the relay's. */
#include <string.h>
#include "tcp.h"

uint8_t sim_slots = 2;
void sim_peer_keys(uint16_t frame, uint8_t slot, uint8_t keys[4]);

static uint8_t hs, in[200], out[700];
static uint16_t in_len, out_len, out_pos;

static void reply(const char *s) {
  uint16_t n = (uint16_t)strlen(s);
  if (out_pos == out_len) out_pos = out_len = 0;
  if (out_len + n + 4 > sizeof(out)) return;
  out[out_len++] = 0x81;
  if (n < 126) out[out_len++] = (uint8_t)n;
  else { out[out_len++] = 126; out[out_len++] = (uint8_t)(n >> 8); out[out_len++] = (uint8_t)n; }
  memcpy(out + out_len, s, n); out_len += n;
}

static char *put_num(char *p, uint16_t v) {
  char t[6]; uint8_t i = 0;
  do { t[i++] = (char)('0' + v % 10); v /= 10; } while (v);
  while (i) *p++ = t[--i];
  return p;
}

static void handle(const char *l, uint16_t n) {
  static char line[96];
  static const char hexd[] = "0123456789abcdef";
  const char *f;
  char *p;
  uint8_t s, k, keys[4];
  uint16_t frame = 0;
  if (n > 12 && !memcmp(l, "{\"op\":\"create\"", 14)) {
    strcpy(line, "{\"op\":\"room\",\"code\":\"SIMU\",\"slot\":0,\"slots\":");
    p = put_num(line + strlen(line), sim_slots);
    strcpy(p, ",\"bytes\":4}");
    reply(line);
  } else if (n > 12 && !memcmp(l, "{\"op\":\"ready\"", 13)) {
    strcpy(line, "{\"op\":\"start\",\"seed\":4660,\"frame\":0,\"mask\":");
    p = put_num(line + strlen(line), (uint16_t)((1 << sim_slots) - 1));
    strcpy(p, "}");
    reply(line);
  } else if (n > 12 && !memcmp(l, "{\"op\":\"input\"", 13)) {
    for (f = l; f < l + n - 8 && memcmp(f, "\"frame\":", 8); f++) ;
    for (f += 8; *f >= '0' && *f <= '9'; f++) frame = (uint16_t)(frame * 10 + (*f - '0'));
    for (s = 1; s < sim_slots; s++) {
      sim_peer_keys(frame, s, keys);
      strcpy(line, "{\"op\":\"input\",\"frame\":");
      p = put_num(line + strlen(line), frame);
      strcpy(p, ",\"slot\":"); p += 8;
      *p++ = (char)('0' + s);
      strcpy(p, ",\"data\":\""); p += 9;
      for (k = 0; k < 4; k++) { *p++ = hexd[keys[k] >> 4]; *p++ = hexd[keys[k] & 15]; }
      strcpy(p, "\"}");
      reply(line);
    }
  }
}

uint8_t tcp_present(void) { return 1; }
uint8_t tcp_open(const char *host, uint16_t port) { (void)host; (void)port; hs = 0; in_len = out_len = out_pos = 0; return 0; }
void tcp_close(void) {}

uint8_t tcp_send(const uint8_t *buf, uint16_t n) {
  uint16_t hdr, len;
  if (in_len + n > sizeof(in)) return 9;
  memcpy(in + in_len, buf, n); in_len += n;
  if (!hs) {
    if (in_len >= 4 && !memcmp(in + in_len - 4, "\r\n\r\n", 4)) {
      static const char ok[] = "HTTP/1.1 101 Switching Protocols\r\nUpgrade: websocket\r\nConnection: Upgrade\r\n\r\n";
      memcpy(out + out_len, ok, sizeof(ok) - 1); out_len += sizeof(ok) - 1;
      hs = 1; in_len = 0;
    }
    return 0;
  }
  while (in_len >= 2) {                         /* client frames: masked with the key 0 */
    len = in[1] & 0x7f; hdr = 2;
    if (len == 126) { if (in_len < 4) break; len = (uint16_t)(in[2] << 8 | in[3]); hdr = 4; }
    hdr += 4;
    if (in_len < hdr + len) break;
    handle((const char *)in + hdr, len);
    memmove(in, in + hdr + len, in_len - hdr - len); in_len -= hdr + len;
  }
  return 0;
}

int16_t tcp_recv(uint8_t *buf, uint16_t max) {
  uint16_t n = out_len - out_pos;
  if (n > max) n = max;
  memcpy(buf, out + out_pos, n); out_pos += n;
  return (int16_t)n;
}
