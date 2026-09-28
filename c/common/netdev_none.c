/* Network device for a build without any: the game runs offline. */
#include <stdint.h>
#include "netdev.h"

uint8_t net_device = NETDEV_NONE;

void net_detect(void) { net_device = NETDEV_NONE; }
uint8_t net_status(net_status_t *st) { st->state = NETST_NOLINK; return 9; }
uint8_t net_create(uint16_t build, uint8_t slots, const uint8_t *settings, uint8_t len, char code[5], uint8_t *slot) {
  (void)build; (void)slots; (void)settings; (void)len; (void)code; (void)slot; return 9;
}
uint8_t net_join(uint16_t build, const char *code, uint8_t *slot, uint8_t *slots, uint8_t *settings, uint8_t *len) {
  (void)build; (void)code; (void)slot; (void)slots; (void)settings; (void)len; return 9;
}
uint8_t net_leave(void) { return 0; }
uint8_t net_ready(uint8_t ready, uint16_t *seed, uint16_t *start_frame) { (void)ready; (void)seed; (void)start_frame; return 9; }
uint8_t net_send(uint16_t frame, const uint8_t keys[NET_BYTES]) { (void)frame; (void)keys; return 9; }
uint8_t net_poll(uint16_t frame, uint16_t *avail, uint8_t keys[NET_SLOTS * NET_BYTES]) { (void)frame; (void)avail; (void)keys; return 9; }
uint8_t net_hash(uint16_t frame, uint16_t hash) { (void)frame; (void)hash; return 9; }
uint8_t net_msg_send(uint8_t to, const uint8_t *data, uint8_t len) { (void)to; (void)data; (void)len; return 9; }
uint8_t net_msg_recv(uint8_t *from, uint8_t *data) { (void)from; (void)data; return 0; }
