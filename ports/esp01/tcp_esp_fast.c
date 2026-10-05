/* Fast banked sccz80 TCP backend. Same passive protocol as the C reference.
 * Receive chunks are 192 bytes. No next read while the local chunk is held.
 * All UART polling is bounded; synchronous operations have 10s deadlines.
 */
#include "tcp.h"
uint8_t tcp_present(void) __naked {
 __asm
    call es_init
    ld a,(es_opened)
    or a
    jr nz,es_present_probe
    call es_clear
    call es_drain
es_present_probe:
    ld hl,es_at
    call es_probe
    or a
    jr z,es_present_ok
    ; Exit a previous transparent session without losing volatile Wi-Fi state.
    call es_escape_unknown
    or a
    jr nz,es_present_power
    ld hl,es_at
    call es_probe
    or a
    jr z,es_present_ok
es_present_power:
    ; A fixed-length payload state may still require module power recovery.
    ; Cycle only module power, keeping baud and persistent Wi-Fi credentials.
    ld bc,0x703b
    ld a,5
    out (c),a
    inc b
    in a,(c)
    res 0,a
    out (c),a
    ld de,50
    call es_pause
    call es_init
    ld de,150
    call es_pause
    call es_clear
    call es_drain
    ld hl,es_at
    call es_command
    ld hl,0
    or a
    ret nz
es_present_ok:
    ld hl,0
    inc l
    ret
 __endasm;
}
uint8_t tcp_open(const char *host,uint16_t port) __naked {
 __asm
    pop bc
    pop de
    pop hl
    push hl
    push de
    push bc
    ld (es_host),hl
    ld (es_port),de
    call _tcp_close
    ; Also close a socket left by another program or an earlier failed open.
    call es_clear
    call es_drain
    ; A previous application may have left CIPMUX=1 sockets open.
    ld hl,es_closeall
    call es_command
    call es_clear
    call es_drain
    ld hl,es_close
    call es_command
    ; ERROR means there was no socket; it is harmless here.
    ; Contiguous flags are reset together; buffers are length framed.
    ld hl,es_busy
    ld de,es_busy+1
    ld bc,13
    ld (hl),0
    ldir
    ld hl,0
    ld (es_reads),hl
    call _tcp_present
    ld a,l
    or a
    jr z,es_open_fail
    ld hl,es_setup
    ld b,5
es_setup_loop:
    push bc
    push hl
    call es_command
    pop hl
    pop bc
    or a
    jr nz,es_open_fail
es_setup_next:
    ld a,(hl)
    inc hl
    or a
    jr nz,es_setup_next
    djnz es_setup_loop
    ld hl,es_connect
    ld de,es_cmd
    call es_copy
    ld hl,(es_host)
    ld b,90
es_host_copy:
    ld a,(hl)
    or a
    jr z,es_host_end
    cp 32
    jr c,es_open_param
    cp 34
    jr z,es_open_param
    cp 92
    jr z,es_open_param
    ld (de),a
    inc de
    inc hl
    djnz es_host_copy
    jr es_open_param
es_host_end:
    ld a,34
    ld (de),a
    inc de
    ld a,','
    ld (de),a
    inc de
    ld hl,(es_port)
    ld a,h
    or l
    jr z,es_open_param
    call es_number
    ld hl,es_crlf
    call es_copy
    ld hl,es_cmd
    call es_command
    or a
    jr nz,es_open_fail
    ld a,(es_closed)
    or a
    jr nz,es_open_fail
    inc a
    ld (es_opened),a
    ld hl,0
    ret
es_open_param:
    ld hl,3
    ret
es_open_fail:
    ld hl,1
    ret
 __endasm;
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
    ld a,d
    or e
    jp z,es_send_ok
    ld hl,(es_sendlen)
    ld de,2049
    or a
    sbc hl,de
    jr nc,es_send_fail
    call es_wait
    or a
    jr nz,es_send_fail
    ld a,(es_opened)
    or a
    jr z,es_send_fail
    ld a,(es_closed)
    or a
    jr nz,es_send_fail
    ld hl,es_send
    ld de,es_cmd
    call es_copy
    ld hl,(es_sendlen)
    call es_number
    ld hl,es_crlf
    call es_copy
    ld a,1
    ld (es_promptwait),a
    ld hl,es_cmd
    call es_command
    or a
    jr nz,es_send_fail
    ld a,2
    ld (es_promptwait),a
    ld a,1
    ld (es_busy),a
    call es_deadline
es_send_loop:
    call es_pump
    ld a,(es_closed)
    or a
    jr nz,es_send_fail
    ld a,(es_error)
    or a
    jr nz,es_send_fail
    ; Drain notifications between bounded 32-byte TX bursts.
    ld b,32
es_send_burst:
    ld hl,(es_sendptr)
    ld a,(hl)
    push bc
    call es_write
    pop bc
    jr c,es_send_loop
    ld hl,(es_sendptr)
    inc hl
    ld (es_sendptr),hl
    ld hl,(es_sendlen)
    dec hl
    ld (es_sendlen),hl
    ld a,h
    or l
    jr z,es_send_complete
    djnz es_send_burst
    jr es_send_loop
es_send_complete:
    ; Payload is fully transmitted. Keep SEND OK pending so the game can
    ; compute while the ESP completes it. The next send waits before issuing
    ; another command; recv/present/close continue the same bounded parser.
    ; Late SEND FAIL is reported by recv (and by the next send).

es_send_ok:
    ld hl,0
    ret
es_send_fail:
    ld hl,1
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
    call es_pump
es_recv_check:
    ld a,(es_error)
    or a
    jp nz,es_recv_fail
    ld a,(es_rxpos)
    ld c,a
    ld a,(es_rxlen)
    sub c
    jr z,es_recv_empty
    ld hl,(es_max)
    ld b,a
    ld a,h
    or a
    jr nz,es_recv_count
    ld a,l
    cp b
    jr nc,es_recv_count
    ld b,a
es_recv_count:
    ld a,b
    or a
    jr z,es_recv_zero
    ld e,c
    ld d,0
    ld hl,es_rx
    add hl,de
    ld de,(es_dest)
    ld c,b
    ld b,0
    push bc
    ldir
    pop hl
    ld a,(es_rxpos)
    add a,l
    ld (es_rxpos),a
    ld b,a
    ld a,(es_rxlen)
    cp b
    ret nz
    xor a
    ld (es_rxpos),a
    ld (es_rxlen),a
    ret
es_recv_empty:
    ld a,(es_opened)
    or a
    jr z,es_recv_fail
    ld a,(es_busy)
    or a
    jr nz,es_recv_zero
    ld a,(es_pending)
    or a
    jr nz,es_recv_query
    ld a,(es_closed)
    or a
    jr nz,es_recv_fail
es_recv_zero:
    ld hl,0
    ret
es_recv_query:
    ld hl,(es_max)
    ld a,h
    or l
    jr z,es_recv_zero
    ld a,(es_ready)
    or a
    jr nz,es_recv_data
    ld a,1
    ld (es_query),a
    xor a
    ld (es_query_seen),a
    ld hl,es_length
    call es_start
    ; Start TX immediately and consume an already available reply without
    ; repeated round trips through the C game/bridge polling loop.
    call es_pump
    call es_pump
    jp es_recv_check
es_recv_data:
    xor a
    ld (es_ready),a
    ld hl,es_receive
    call es_start
    ld hl,(es_reads)
    inc hl
    ld (es_reads),hl
    call es_pump
    call es_pump
    jp es_recv_check
es_recv_fail:
    ld hl,65535
    ret
 __endasm;
}
void tcp_close(void) __naked {
 __asm
    ; Parser resets also clear es_opened. Always close the physical socket,
    ; including after transparent-mode cleanup; ERROR for no socket is harmless.
    ; Finish an outstanding passive read before issuing another command.
    call es_wait
    call es_clear
    ld hl,es_close
    call es_command
es_close_end:
    call es_clear
    call es_drain
    ret
 __endasm;
}
uint8_t tcp_error(void) __naked {
 __asm
    ld a,(es_error)
    ld l,a
    ld h,0
    ret
 __endasm;
}
uint8_t tcp_peer_closed(void) __naked {
 __asm
    ld a,(es_closed)
    ld l,a
    ld h,0
    ret
 __endasm;
}
uint16_t tcp_read_commands(void) __naked {
 __asm
    ld hl,(es_reads)
    ret
 __endasm;
}
/* Helpers are kept in one naked block to avoid compiler prologues. */
static void esp_engine(void) __naked {
 __asm
; BIOS 2.0 turn_sub/turnon_wifi1 and byte UART semantics, only required services.
es_init:
    ld bc,0x703b
    ld a,5
    out (c),a
    inc b
    in a,(c)
    set 0,a
    out (c),a
    ret
 ; Reset parser/transaction state, preserving host and port arguments.
es_clear:
    ld hl,es_busy
    ld de,es_busy+1
    ld bc,13
    ld (hl),0
    ldir
    ld hl,0
    ld (es_txptr),hl
    ld (es_available),hl
    ret
; Discard old replies only when no transaction is active. Bounded 2048 bytes.
es_drain:
    ; BIOS 2.0 hardware FIFO reset. Also clears an old overflow condition.
    ld bc,0x7c3b
    ld a,0x30
    out (c),a
    inc b
    ld a,1
    out (c),a
    ld de,2048
    ld l,4
es_drain_loop:
    ld bc,0x133b
    in a,(c)
    bit 0,a
    jr nz,es_drain_byte
    dec l
    ret z
    jr es_drain_loop
es_drain_byte:
    ld l,4
    inc b
    in a,(c)
    dec de
    ld a,d
    or e
    jr nz,es_drain_loop
    ret
es_escape_unknown:
    ld de,50
    call es_pause
    call es_deadline
    ld b,3
es_unknown_plus:
    push bc
    ld a,'+'
    call es_write
    pop bc
    jr nc,es_unknown_sent
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
    jr c,es_unknown_plus
    ld a,2
    ret
es_unknown_sent:
    djnz es_unknown_plus
    ld de,50
    call es_pause
    call es_clear
    call es_drain
    ; Terminate a stray command line too; do not wait for its response.
    ld hl,es_crlf
    call es_start
es_unknown_crlf:
    call es_pump
    ld a,(es_error)
    or a
    ret nz
    ld hl,(es_txptr)
    ld a,h
    or l
    jr nz,es_unknown_crlf
    ld de,1
    call es_pause
    call es_clear
    call es_drain
    xor a
    ret
; Wait for DE ROM ticks; independent of CPU turbo, interrupts stay enabled.
es_pause:
    ld hl,(0x5c78)
    push hl
es_pause_loop:
    pop bc
    push bc
    ld hl,(0x5c78)
    or a
    sbc hl,bc
    or a
    sbc hl,de
    jr c,es_pause_loop
    pop bc
    ret
; Read byte -> A, carry when no byte or FIFO full (error 5).
es_read:
    ld bc,0x133b
    in a,(c)
    bit 2,a
    jr z,es_read_space
    ld a,5
    call es_fault
    scf
    ret
es_read_space:
    rra
    ccf
    ret c
    inc b
    in a,(c)
    or a
    ret
; Write A, carry if TX occupied; no waits, no FIFO clears.
es_write:
    ld e,a
    ld bc,0x133b
    in a,(c)
    rra
    rra
    ret c
    out (c),e
    ret
; AT discovery needs only 300 ms; retain ten seconds for real network commands.
es_probe:
    call es_start
    ld hl,(es_tick)
    ld de,485
    or a
    sbc hl,de
    ld (es_tick),hl
    jp es_wait
es_command:
    push hl
    call es_wait
    pop hl
    or a
    ret nz
    call es_start
es_wait:
    call es_pump
    ld a,(es_error)
    or a
    ret nz
    ld a,(es_busy)
    or a
    jr nz,es_wait
    ret
es_start:
    ld (es_txptr),hl
    ld a,1
    ld (es_busy),a
es_deadline:
    ld hl,(0x5c78)
    ld (es_tick),hl
    ret
es_pump:
    ld a,(es_error)
    or a
    ret nz
    ; B bounds bytes, C bounds consecutive empty status probes. No waits.
    ld bc,0x8004
es_pump_rx:
    push bc
    call es_read
    jr c,es_pump_empty
    call es_consume
    pop bc
    ld c,4
    djnz es_pump_rx
    jr es_pump_tx
es_pump_empty:
    pop bc
    ld a,(es_error)
    or a
    ret nz
    dec c
    jr nz,es_pump_rx
es_pump_tx:
    ld b,32
es_command_burst:
    ld hl,(es_txptr)
    ld a,h
    or l
    jr z,es_pump_time
    ld a,(hl)
    or a
    jr z,es_tx_done
    push bc
    push hl
    call es_write
    pop hl
    pop bc
    jr c,es_pump_time
    inc hl
    ld (es_txptr),hl
    djnz es_command_burst
    jr es_pump_time
es_tx_done:
    ld hl,0
    ld (es_txptr),hl
es_pump_time:
    ld a,(es_busy)
    or a
    ret z
    ld hl,(es_tick)
    ex de,hl
    ld hl,(0x5c78)
    or a
    sbc hl,de
    ld de,500
    or a
    sbc hl,de
    ret c
    ld a,2
es_fault:
    ld (es_error),a
    xor a
    ld (es_busy),a
    ld hl,0
    ld (es_txptr),hl
    ret
es_consume:
    ld c,a
    ld a,(es_remain)
    or a
    jr z,es_text
    dec a
    ld (es_remain),a
    ld a,(es_rxlen)
    cp 192
    jp nc,es_bad
    ld e,a
    ld d,0
    inc a
    ld (es_rxlen),a
    ld hl,es_rx
    add hl,de
    ld (hl),c
    ret
es_text:
    ld a,(es_discard)
    or a
    jr z,es_text_byte
    ld a,c
    cp 10
    ret nz
    xor a
    ld (es_discard),a
    ret
es_text_byte:
    ld a,c
    cp 13
    ret z
    ld a,(es_llen)
    ld e,a
    ld d,0
    ld hl,es_line
    add hl,de
    ld a,c
    cp 10
    jp z,es_line_end
    cp '>'
    jr nz,es_header
    ld a,e
    or a
    jr nz,es_header
    ld a,(es_promptwait)
    cp 1
    ret nz
    xor a
    ld (es_busy),a
    ld (es_promptwait),a
    ret
es_header:
    ld a,e
    cp 14
    jr c,es_append
    ld a,(es_line+12)
    cp ','
    ld a,c
    jr nz,es_modern
    cp ':'
    jr es_delimiter
es_modern:
    cp ','
es_delimiter:
    jr nz,es_append
    push hl
    ld hl,es_line
    ld de,es_data_prefix
    call es_equal
    pop hl
    jr nz,es_append
    ld (hl),0
    ld hl,es_line+13
    call es_parse
    jp c,es_bad
    ld a,d
    or a
    jp nz,es_bad
    ld a,e
    cp 193
    jp nc,es_bad
    ld (es_remain),a
    ld hl,(es_available)
    or a
    sbc hl,de
    jr nc,es_remaining_valid
    ld hl,0
es_remaining_valid:
    ld (es_available),hl
    ld a,h
    or l
    ld a,0
    jr z,es_data_empty
    inc a
es_data_empty:
    ld (es_ready),a
    ; New bytes can arrive after the notification while a chunk is read.
    ; Probe once after exhausting the known count instead of losing that tail.
    ld a,(es_remain)
    or a
    ld a,0
    jr z,es_no_tail
    inc a
es_no_tail:
    ld (es_pending),a
    xor a
    ld (es_llen),a
    ret
es_append:
    ld a,(es_llen)
    cp 47
    jr nc,es_long
    ld (hl),c
    inc a
    ld (es_llen),a
    ret
es_long:
    ld a,1
    ld (es_discard),a
    xor a
    ld (es_llen),a
    ret
es_bad:
    ld a,4
    jp es_fault
es_line_end:
    ld (hl),0
    xor a
    ld (es_llen),a
    ld hl,es_line
    ld de,es_ok
    call es_equal
    jr nz,es_not_ok
    ld a,(es_promptwait)
    or a
    ret nz
    ld a,(es_query)
    or a
    jr z,es_done
    ld a,(es_query_seen)
    or a
    jp z,es_bad
es_done:
    xor a
    ld (es_query),a
    xor a
    ld (es_busy),a
    ld (es_promptwait),a
    ret
es_not_ok:
    ld hl,es_line
    ld de,es_send_ok_text
    call es_equal
    jr z,es_done
    ld hl,es_line
    ld de,es_closed_text
    call es_equal
    jr z,es_mark_closed
    ld hl,es_line
    ld de,es_wifi
    call es_equal
    jr nz,es_not_closed
es_mark_closed:
    ld a,1
    ld (es_closed),a
    ret
es_not_closed:
    ld hl,es_line
    ld de,es_ipd
    call es_equal
    jr nz,es_not_ipd
    call es_parse
    ret c
    ld (es_available),de
    ld a,d
    or e
    ret z
    ld a,1
    ld (es_pending),a
    ld (es_ready),a
    ret
es_not_ipd:
    ld hl,es_line
    ld de,es_len_prefix
    call es_equal
    jr nz,es_check_error
    call es_parse
    jp c,es_bad
    ld (es_available),de
    ld a,d
    or e
    ld a,0
    jr z,es_no_available
    inc a
es_no_available:
    ld (es_pending),a
    ld (es_ready),a
    ld a,1
    ld (es_query_seen),a
    ret
es_check_error:
    ld a,(es_line)
    cp 'E'
    jr z,es_at_error
    cp 'F'
    jr z,es_at_error
    cp 'b'
    jr z,es_at_error
    ld hl,es_line
    ld de,es_send_fail_text
    call es_equal
    ret nz
es_at_error:
    ld a,1
    jp es_fault
; Compare a NUL-terminated prefix DE with HL; Z on match, HL after prefix.
es_equal:
    ld a,(de)
    or a
    ret z
    cp (hl)
    ret nz
    inc de
    inc hl
    jr es_equal
; Decimal at HL -> DE. Carry on no digits or overflow. Stop at NUL/comma.
es_parse:
    ld de,0
    ld b,0
es_parse_loop:
    ld a,(hl)
    sub '0'
    cp 10
    jr nc,es_parse_end
    ld c,a
    push hl
    ld h,d
    ld l,e
    add hl,hl
    jr c,es_parse_bad
    add hl,hl
    jr c,es_parse_bad
    add hl,de
    jr c,es_parse_bad
    add hl,hl
    jr c,es_parse_bad
    ld e,c
    ld d,0
    add hl,de
    jr c,es_parse_bad
    ex de,hl
    pop hl
    inc hl
    inc b
    jr es_parse_loop
es_parse_bad:
    pop hl
    scf
    ret
es_parse_end:
    ld a,b
    or a
    scf
    ret z
    ld a,(hl)
    or a
    ret z
    cp ','
    scf
    ret nz
    or a
    ret
; Append string HL to DE, leave DE at its terminating NUL.
es_copy:
    ld a,(hl)
    ld (de),a
    or a
    ret z
    inc hl
    inc de
    jr es_copy
; Append decimal HL to DE, preserving DE across repeated subtraction.
es_number:
    push ix
    ld ix,es_powers
    ld b,5
    ld c,0
es_num_digit:
    push bc
    ld c,(ix+0)
    ld b,(ix+1)
    ld a,'0'-1
es_num_sub:
    inc a
    or a
    sbc hl,bc
    jr nc,es_num_sub
    add hl,bc
    pop bc
    cp '0'
    jr nz,es_num_put
    ld (de),a
    ld a,c
    or a
    jr nz,es_num_zero
    ld a,b
    cp 1
    jr nz,es_num_skip
es_num_zero:
    ld a,'0'
es_num_put:
    ld (de),a
    inc de
    ld c,1
es_num_skip:
    inc ix
    inc ix
    djnz es_num_digit
    xor a
    ld (de),a
    pop ix
    ret
es_powers: defw 10000,1000,100,10,1
es_at: defm "AT"
    defb 13,10,0
es_setup: defm "ATE0"
    defb 13,10,0
    defm "AT+CIPMUX=0"
    defb 13,10,0
    defm "AT+CIPMODE=0"
    defb 13,10,0
    defm "AT+CIPDINFO=0"
    defb 13,10,0
    defm "AT+CIPRECVMODE=1"
    defb 13,10,0
es_connect: defm "AT+CIPSTART="
    defb 34
    defm "TCP"
    defb 34,',',34,0
es_send: defm "AT+CIPSEND="
    defb 0
es_length: defm "AT+CIPRECVLEN?"
    defb 13,10,0
es_receive: defm "AT+CIPRECVDATA=192"
    defb 13,10,0
es_closeall: defm "AT+CIPCLOSE=5"
    defb 13,10,0
es_close: defm "AT+CIPCLOSE"
    defb 13,10,0
es_crlf: defb 13,10,0
es_data_prefix: defm "+CIPRECVDATA"
    defb 0
es_len_prefix: defm "+CIPRECVLEN:"
    defb 0
es_ipd: defm "+IPD,"
    defb 0
es_closed_text: defm "CLOSED"
    defb 0
es_wifi: defm "WIFI DISCONNECT"
    defb 0
es_ok: defm "OK"
    defb 0
es_send_ok_text: defm "SEND OK"
    defb 0
es_send_fail_text: defm "SEND FAIL"
    defb 0
    SECTION bss_compiler
es_host: defs 2
es_port: defs 2
es_sendptr: defs 2
es_sendlen: defs 2
es_dest: defs 2
es_max: defs 2
es_txptr: defs 2
es_tick: defs 2
es_reads: defs 2
es_available: defs 2
es_busy: defs 1
es_error: defs 1
es_closed: defs 1
es_opened: defs 1
es_pending: defs 1
es_ready: defs 1
es_query: defs 1
es_query_seen: defs 1
es_promptwait: defs 1
es_llen: defs 1
es_discard: defs 1
es_remain: defs 1
es_rxlen: defs 1
es_rxpos: defs 1
; All driver state and buffers reside in bank 6.
es_cmd: defs 128
es_line: defs 48
es_rx: defs 192
    SECTION code_compiler
 __endasm;
}

