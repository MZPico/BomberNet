/* Game-only WebSocket transport: AT setup/recovery, raw UART while connected.
 * EOF is identified by the WebSocket close/invalid-frame parser, not raw TCP.
 * 4096-byte banked RX ring plus the hardware 2048-byte FIFO, no RTS/CTS.
 */
#define tcp_present command_present
#define tcp_open command_open
#define tcp_send command_send
#define tcp_recv command_recv
#define tcp_close command_close
#define tcp_error command_error
#define tcp_peer_closed command_peer_closed
#define tcp_read_commands command_read_commands
#include "tcp_esp_fast.c"
#undef tcp_present
#undef tcp_open
#undef tcp_send
#undef tcp_recv
#undef tcp_close
#undef tcp_error
#undef tcp_peer_closed
#undef tcp_read_commands

static uint8_t stream_active;
static uint8_t stream_start(void) __naked {
 __asm
    ld hl,stream_receive_mode
    call es_command
    or a
    jr nz,stream_start_fail
    ld hl,stream_mode
    call es_command
    or a
    jr nz,stream_start_fail
    ld a,1
    ld (es_promptwait),a
    ld hl,stream_begin
    call es_command
    or a
    jr nz,stream_start_fail
    ld hl,stream_rx & 0xff00
    ld (stream_head),hl
    ld (stream_tail),hl
    ld hl,0
    ld (stream_count),hl
    xor a
    ld (es_promptwait),a
    ld (es_busy),a
    inc a
    ld (_stream_active),a
    ld hl,0
    ret
stream_start_fail:
    ld hl,1
    ret
 __endasm;
}
uint8_t tcp_present(void) {
    if(stream_active) return 1;
    return command_present();
}
uint8_t tcp_open(const char *host,uint16_t port) {
    uint8_t r=command_open(host,port);
    if(r) return r;
    r=stream_start();
    if(r) command_close();
    return r;
}
uint8_t tcp_send(const uint8_t *buf,uint16_t n) __naked {
 __asm
    pop bc
    pop de
    pop hl
    push hl
    push de
    push bc
    ld (es_sendptr),hl
    ld (es_sendlen),de
    ld a,(_stream_active)
    or a
    jr z,stream_send_fail
    call es_deadline
stream_send_more:
    call stream_pump
    ld a,(es_error)
    or a
    jr nz,stream_send_fail
    ld hl,(es_sendptr)
    ld de,(es_sendlen)
    ld a,32
    ld (stream_budget),a
stream_send_loop:
    ld a,d
    or e
    jr z,stream_send_ok
    ld bc,0x133b
    in a,(c)
    bit 1,a
    jr nz,stream_send_counter
    ld a,(hl)
    out (c),a
    inc hl
    dec de
stream_send_counter:
    ld a,(stream_budget)
    dec a
    ld (stream_budget),a
    jr nz,stream_send_loop
    ld (es_sendptr),hl
    ld (es_sendlen),de
    ld hl,(es_tick)
    ex de,hl
    ld hl,(0x5c78)
    or a
    sbc hl,de
    ld de,500
    or a
    sbc hl,de
    jr c,stream_send_more
    ld a,2
    call es_fault
stream_send_fail:
    ld hl,1
    ret
stream_send_ok:
    ld hl,0
    ret
 __endasm;
}
int16_t tcp_recv(uint8_t *buf,uint16_t max) __naked {
 __asm
    pop bc
    pop de
    pop hl
    push hl
    push de
    push bc
    ld (es_dest),hl
    ld (es_max),de
    call stream_pump
    ld a,(es_error)
    or a
    jr nz,stream_recv_fail
    ld a,(_stream_active)
    or a
    jr z,stream_recv_fail
    ld hl,(stream_count)
    ld de,(es_max)
    or a
    sbc hl,de
    jr c,stream_recv_all
    ld (stream_count),hl
    jr stream_recv_n
stream_recv_all:
    ld de,(stream_count)
    ld hl,0
    ld (stream_count),hl
stream_recv_n:
    ld (es_max),de
    ld a,d
    or e
    jr z,stream_recv_done
    ld b,d
    ld c,e                  ; bridge limits max to 192
    ld hl,(stream_tail)
    ld de,(es_dest)
    ld a,h
    cp (stream_rx+4096)/256-1
    jr nz,stream_contiguous
    ld a,l
    add a,c
    jr nc,stream_contiguous
    ; Split at the ring boundary; never invoke LDIR with a zero count.
    push bc
    ld a,l
    neg
    ld c,a
    ldir
    ld h,stream_rx/256
    pop bc
    ld a,(stream_tail)
    add a,c
    ld c,a
    or a
    jr z,stream_copied
stream_contiguous:
    ldir
    ld a,h
    cp (stream_rx+4096)/256
    jr nz,stream_copied
    ld h,stream_rx/256
stream_copied:
    ld (stream_tail),hl
stream_recv_done:
    ld hl,(es_max)
    ret
stream_recv_fail:
    ld hl,65535
    ret
 __endasm;
}
void tcp_close(void) __naked {
 __asm
    ld a,(_stream_active)
    or a
    jr z,stream_close_command
    ; Conservative one-second guards, using ROM ticks regardless of turbo.
    ld de,50
    call es_pause
    call es_deadline
    ld b,3
stream_escape:
    push bc
    ld a,'+'
    call es_write
    pop bc
    jr nc,stream_escape_sent
    push bc
    ld hl,(es_tick)
    ex de,hl
    ld hl,(0x5c78)
    or a
    sbc hl,de
    ld de,500
    or a
    sbc hl,de
    pop bc
    jr c,stream_escape
    ld a,2
    call es_fault
    xor a
    ld (_stream_active),a
    jp _command_close
stream_escape_sent:
    djnz stream_escape
    ld de,50
    call es_pause
    xor a
    ld (_stream_active),a
    call es_clear
    call es_drain
    ld hl,stream_normal
    call es_command
stream_close_command:
    jp _command_close
 __endasm;
}
uint8_t tcp_error(void) {return command_error();}
uint8_t tcp_peer_closed(void) {return command_peer_closed();}
uint16_t tcp_read_commands(void) {return 0;}
static void stream_engine(void) __naked {
 __asm
stream_pump:
    ld a,(_stream_active)
    or a
    ret z
    ld a,(es_error)
    or a
    ret nz
    ld hl,(stream_head)
    ld de,(stream_count)
    ld bc,0xc004
stream_pump_loop:
    ld a,d
    cp 16
    jr nc,stream_pump_done
    push bc
    push hl
    call es_read
    pop hl
    pop bc
    jr c,stream_pump_empty
    ld c,4
    ld (hl),a
    inc hl
    ld a,h
    cp (stream_rx+4096)/256
    jr nz,stream_enqueue
    ld h,stream_rx/256
stream_enqueue:
    inc de
    djnz stream_pump_loop
stream_pump_done:
    ld (stream_head),hl
    ld (stream_count),de
    ret
stream_pump_empty:
    ld a,(es_error)
    or a
    jr nz,stream_pump_done
    dec c
    jr nz,stream_pump_loop
    jr stream_pump_done
stream_receive_mode: defm "AT+CIPRECVMODE=0"
    defb 13,10,0
stream_mode: defm "AT+CIPMODE=1"
    defb 13,10,0
stream_begin: defm "AT+CIPSEND"
    defb 13,10,0
stream_normal: defm "AT+CIPMODE=0"
    defb 13,10,0
    SECTION bss_compiler
stream_head: defs 2
stream_tail: defs 2
stream_count: defs 2
stream_budget: defs 1
    ALIGN 4096
stream_rx: defs 4096
    SECTION code_compiler
 __endasm;
}
