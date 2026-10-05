/* Lockstep network match over the network device (netdev.h). */
#include <stdint.h>
#include <string.h>
#include "game.h"

/* ---------------- lockstep match ---------------- */

#ifdef ESP_FAST128
uint8_t esp_input_delay;
extern void esp_choose_delay(void);
#endif
uint8_t menu_net = NET_OFF;
uint8_t net_active, net_slot, net_slots, net_waiting, net_abort;
uint8_t net_delay = NET_DELAY_MIN + 1;
char net_code[5] = "AAAA";
static uint16_t net_frame;
uint8_t net_table[MAX_PLAYERS] = {0, 4, 8, 12};   /* default: one player per device */
uint8_t net_total = 2;

/* a failed NET command: ask the device why (desync / dropped) */
static void net_fail(void) {
  net_status_t st;
  net_abort = 9;
  if (net_status(&st) == 0 && (st.state == NETST_DESYNC || st.state == NETST_DROPPED)) net_abort = st.state;
}

#ifdef ESP_FAST128
void net_match_start(void) __naked {
 __asm
    call _esp_choose_delay
    xor a
    ld (_net_waiting),a
    ld (_net_abort),a
    ld hl,0
    ld (_net_frame),hl
    inc a
    ld (_net_active),a
    ld b,0
start11_loop:
    ld a,(_esp_input_delay)
    cp b
    ret z
    push bc
    ld l,b
    ld h,0
    push hl
    ld hl,start11_none
    push hl
    call _net_send
    pop bc
    pop bc
    pop bc
    inc b
    jr start11_loop
start11_none: defs 4,0

 __endasm;
}
#else
void net_match_start(void) {
  uint8_t f;
  static const uint8_t none[NET_BYTES] = {0, 0, 0, 0};
  net_frame = 0;
  net_waiting = 0;
  net_abort = 0;
  net_active = 1;
  for (f = 0; f < net_delay; f++) net_send(f, none);   /* nobody moves in the first steps */
}
#endif

/* Send the local players' keys for step N+net_delay (one byte per local
 * player), wait for the vector of step N and deal it out by the seat table. */
#ifdef ESP_FAST128
void net_lockstep_poll(void) __naked {
 __asm
    push ix
    ld a,(_net_abort)
    or a
    jr z,l11_begin
    ld ix,_players
    ld de,16
    ld b,4
l11_abort_keys:
    ld (ix+2),0
    add ix,de
    djnz l11_abort_keys
    pop ix
    ret
l11_begin:
    ld hl,(0x5c78)
    ld (l11_start),hl
    xor a
    ld (l11_tries),a
    ld hl,_menu_inputs
    ld de,l11_local
    ld bc,0x0400
l11_read:
    ld a,(_menu_local)
    cp c
    ld a,0
    jr z,l11_store
    jr c,l11_store
    push bc
    push de
    push hl
    ld l,(hl)
    ld h,0
    push hl
    call _input_read
    pop bc
    ld a,l
    pop hl
    pop de
    pop bc
l11_store:
    ld (de),a
    inc de
    inc hl
    inc c
    djnz l11_read
    ld a,(_esp_input_delay)
    ld e,a
    ld d,0
    ld hl,(_net_frame)
    add hl,de
    push hl
    ld hl,l11_local
    push hl
    call _net_send
    pop bc
    pop bc
    ld a,l
    or a
    call nz,_net_fail
l11_poll:
    ld a,(_net_abort)
    or a
    jp nz,l11_keys
    ld hl,(_net_frame)
    push hl
    ld hl,l11_avail
    push hl
    ld hl,l11_keysbuf
    push hl
    call _net_poll
    pop bc
    pop bc
    pop bc
    ld a,l
    or a
    jr z,l11_available
    call _net_fail
    jr l11_keys
l11_available:
    ld hl,(l11_avail)
    ld a,h
    and l
    inc a
    jr z,l11_wait
    ld de,(_net_frame)
    or a
    sbc hl,de
    jr nc,l11_keys
l11_wait:
    ld hl,l11_tries
    inc (hl)
    ld a,(hl)
    cp 5
    jr c,l11_poll
    ld a,1
    ld (_net_waiting),a
    ld a,(hl)
    and 15
    jr nz,l11_poll
    ld hl,(0x5c78)
    ld de,(l11_start)
    or a
    sbc hl,de
    ld de,500
    or a
    sbc hl,de
    jr c,l11_status
    ld a,9
    jr l11_failed
l11_status:
    ld hl,l11_st
    push hl
    call _net_status
    pop bc
    ld a,l
    or a
    jr nz,l11_break
    ld a,(l11_st)
    or a
    jr nz,l11_connected
    ld a,9
    jr l11_failed
l11_connected:
    cp 4
    jr z,l11_failed
    cp 5
    jr z,l11_failed
l11_break:
    call _plat_key_char
    ld a,l
    cp 0x1b
    jp nz,l11_poll
    ld a,7
l11_failed:
    ld (_net_abort),a
l11_keys:
    ld ix,_players
    ld hl,_net_table
    ld bc,0x0400
l11_assign:
    ld (ix+2),0
    ld a,(_net_abort)
    or a
    jr nz,l11_next
    ld a,(_net_total)
    cp c
    jr z,l11_next
    jr c,l11_next
    ld a,(ix+0)
    or a
    jr z,l11_next
    ld a,(hl)
    cp 16
    jr nc,l11_next
    push hl
    ld e,a
    ld d,0
    ld hl,l11_keysbuf
    add hl,de
    ld a,(hl)
    ld (ix+2),a
    pop hl
l11_next:
    inc hl
    ld de,16
    add ix,de
    inc c
    djnz l11_assign
    ld a,(_net_abort)
    or a
    jr nz,l11_end
    ld (_net_waiting),a
l11_end:
    ld hl,(_net_frame)
    inc hl
    ld (_net_frame),hl
    pop ix
    ret
l11_start: defw 0
l11_tries: defb 0
l11_local: defs 4,0
l11_keysbuf: defs 16,0
l11_avail: defw 0
l11_st: defs 8,0

 __endasm;
}
#else
void net_lockstep_poll(void) {
  uint16_t avail;
  uint8_t keys[NET_SLOTS * NET_BYTES], local[NET_BYTES], i, tries = 0;
  net_status_t st;
#ifdef ESP_STREAM
  uint16_t tick_start=*(volatile uint16_t *)0x5c78;
#endif
  if (net_abort) { for (i = 0; i < MAX_PLAYERS; i++) players[i].keys = 0; return; }
  for (i = 0; i < NET_BYTES; i++) local[i] = i < menu_local ? input_read(menu_inputs[i]) : 0;
  if (net_send(net_frame + net_delay, local) != 0) net_fail();
  while (!net_abort) {
    if (net_poll(net_frame, &avail, keys) != 0) { net_fail(); break; }
    if (avail != 0xffff && avail >= net_frame) break;
    if (++tries > 4) {
      net_waiting = 1;
      if ((tries & 15) == 0) {
#ifdef ESP_STREAM
        if ((uint16_t)(*(volatile uint16_t *)0x5c78-tick_start)>=500) { net_abort=9; break; }
#endif
        if (net_status(&st) == 0 &&
            (st.state == NETST_DESYNC || st.state == NETST_DROPPED || st.state == NETST_NOLINK))
          net_abort = st.state ? st.state : 9;
        else if (plat_key_char() == 0x1b) net_abort = NET_ABORT_BREAK;   /* BREAK: give up waiting */
      }
    }
  }
  for (i = 0; i < MAX_PLAYERS; i++)
    players[i].keys = (!net_abort && i < net_total && net_table[i] != 0xff && players[i].active) ? keys[net_table[i]] : 0;
  if (!net_abort) net_waiting = 0;
  net_frame++;
}
#endif

void net_match_end(void) {
  if (!net_active) return;
  net_active = 0;
  net_leave();
}
