; C64: the game screen's flush and the frame clock (docs/port-c64.md).
; The screen is at $E800 and the character set at $F000, in the RAM under
; the KERNAL ROM (VIC bank 3): the CPU only writes there, and writes go to
; the RAM. Colour RAM at $D800.

        .set SCREEN, 0xe800
        .set COLOUR, 0xd800

; c64_flush_game: every cell of draw_buf that differs from shadow_vram goes
; to the screen (c64_game_code) and the colour RAM (c64_game_colour). Rows
; 0-23 in eight sections of 120 cells, absolute,x, not cleared (every game
; frame begins with composite_frame, which overwrites them: core/platform.h);
; the status line is cleared. An unchanged cell costs 11 cycles, a changed
; one 29.
        .macro  cells off
        lda draw_buf+\off,x
        cmp shadow_vram+\off,x
        beq 1f
        sta shadow_vram+\off,x
        tay
        lda c64_game_code,y
        sta SCREEN+\off,x
        lda c64_game_colour,y
        sta COLOUR+\off,x
1:
        .endm

        .section .text.c64_flush_game,"ax",@progbits
        .global c64_flush_game
c64_flush_game:
        ldx #119
2:      cells 0
        cells 120
        cells 240
        cells 360
        cells 480
        cells 600
        cells 720
        cells 840
        dex
        bmi 3f
        jmp 2b
3:      ldx #39                 ; the status line, cleared
4:      ldy draw_buf+960,x
        lda #0x20
        sta draw_buf+960,x
        tya
        cmp shadow_vram+960,x
        beq 5f
        sta shadow_vram+960,x
        lda c64_game_code,y
        sta SCREEN+960,x
        lda c64_game_colour,y
        sta COLOUR+960,x
5:      dex
        bpl 4b
        rts

; The interrupts: the KERNAL's entry saves the registers and jumps through
; $0314 here; $EA81 restores them. The raster interrupt comes four times a
; TV frame (lines 10, 88, 166, 244: about 5 ms apart on PAL): each counts
; down the tones (c64_tone_q, plat_c64.c) and switches a finished one off,
; whatever the game is doing; at line 244, below the text, it counts
; c64_ticks. CIA 1's timer B ends the pause between two WiC64 commands;
; both start the next one (wic64_kick). CIA 1's timer A interrupt (the
; KERNAL's keyboard scan and clock) is off.
        .section .text.c64_irq,"ax",@progbits
        .global c64_irq
c64_irq:
        lda 0xdc0d              ; CIA 1 (reading acknowledges): timer B?
        and #0x02
        beq 1f
        lda #0
        sta eng_gap
        jsr wic64_kick
1:      lda 0xd019
        and #0x01
        beq 2f
        sta 0xd019              ; acknowledge the raster interrupt
        lda #0x10               ; the tones: a quarter of a TV frame less; at 0, gate off
        ldx c64_tone_q
        beq 3f
        dec c64_tone_q
        bne 3f
        sta 0xd404              ; voice 1
3:      ldx c64_tone_q+1
        beq 4f
        dec c64_tone_q+1
        bne 4f
        sta 0xd40b              ; voice 2
4:      lda irq_quarter         ; the next line
        clc
        adc #1
        and #3
        sta irq_quarter
        tax
        lda irq_lines,x
        sta 0xd012
        cpx #0
        bne 2f                  ; this one was line 244: a TV frame
        inc c64_ticks
        jsr wic64_kick          ; and once a TV frame, whatever was missed
2:      jmp 0xea81

        .section .rodata.irq_lines,"a",@progbits
irq_lines:
        .byte 10, 88, 166, 244

        .section .bss.irq_quarter,"aw",@nobits
irq_quarter:
        .zero 1
        .global c64_tone_q
c64_tone_q:
        .zero 2                 ; the tones' remaining quarters (voices 1, 2)

; WiC64 bytes over the user port (tcp_wic64.c): wic64_out sends wic64_n
; bytes from (wic64_ptr), each answered by FLAG2; wic64_in receives
; wic64_n bytes to (wic64_ptr), each after FLAG2. A = 1 done, 0 when a
; FLAG2 did not come within about 0.8 s (65536 tries). About 25 cycles a
; byte besides the WiC64's own time.
        .section .text.wic64_out,"ax",@progbits
        .global wic64_out
wic64_out:
        ldy #0
1:      lda wic64_n
        ora wic64_n+1
        beq 8f
        lda (wic64_ptr),y
        sta 0xdd01              ; the byte; PC2 pulses
        jsr wait_flag2
        beq 9f
        inc wic64_ptr
        bne 2f
        inc wic64_ptr+1
2:      lda wic64_n
        bne 3f
        dec wic64_n+1
3:      dec wic64_n
        jmp 1b
8:      lda #1
9:      rts

        .section .text.wic64_in,"ax",@progbits
        .global wic64_in
wic64_in:
        ldy #0
1:      lda wic64_n
        ora wic64_n+1
        beq 8f
        jsr wait_flag2
        beq 9f
        lda 0xdd01              ; the byte; PC2 pulses (the WiC64 puts the next one)
        sta (wic64_ptr),y
        inc wic64_ptr
        bne 2f
        inc wic64_ptr+1
2:      lda wic64_n
        bne 3f
        dec wic64_n+1
3:      dec wic64_n
        jmp 1b
8:      lda #1
9:      rts

; FLAG2 (CIA 2's ICR bit 4; reading the ICR clears it): A = 1, or 0 after
; 65536 tries. Y is kept.
        .section .text.wait_flag2,"ax",@progbits
wait_flag2:
        lda #0x10
        bit 0xdd0d
        bne 7f                  ; at once: the usual case
        ldx #0
        stx wic64_tmo
1:      bit 0xdd0d
        bne 7f
        dex
        bne 1b
        dec wic64_tmo
        bne 1b
        lda #0
        rts
7:      lda #1
        rts

        .section .zp.bss.wic64_ptr,"zaw",@nobits
        .global wic64_ptr
wic64_ptr:
        .zero 2
        .section .bss.wic64_n,"aw",@nobits
        .global wic64_n
wic64_n:
        .zero 2
wic64_tmo:
        .zero 1

;---------------------------------------------------------------------------
; The WiC64 in the background (tcp_wic64.c, docs/port-c64.md): while a TCP
; connection is open, CIA 2's FLAG2 is an NMI, and each one moves the
; command in progress a step, as the official library's loops do: send the
; next byte (each sent byte is answered by FLAG2); after the last one's,
; port B in and PA2 low; then the reply is there (FLAG2): the dummy read;
; then the status and the length, then the payload, a byte each FLAG2.
; Writes: tcp_send appends to one of two buffers, and a write command sends
; all that was queued. Reads: when tcp_recv asks (eng_wantread), or by
; themselves 8 TV frames after the last one (the game does not poll the
; network between stages, for seconds; stock VICE then answers a read
; nobody asked for and drops its bytes), into the ring eng_rx: the NMI
; moves eng_head when a read ends, tcp_recv moves eng_tail. A command starts 2 ms after the last one ended
; (CIA 1 timer B, c64_irq): VICE's WiC64 sends one more FLAG2 after a
; reply's last byte, which must not answer the next command's first byte.

        .set E_IDLE, 0
        .set E_OUT, 1
        .set E_READY, 2
        .set E_HDR, 3
        .set E_DATA, 4
        .set WMAX, 512           ; a write buffer's payload
        .set RXMAX, 2048         ; the ring (a read is started with 1 KB free; one that does not fit: lost)

; NMI (the KERNAL's entry $FE43: SEI, JMP ($0318)): FLAG2, or RESTORE
; (nothing). The WiC64 answers a byte within microseconds, so the next
; FLAG2 comes while this handler still runs: that nested NMI only notes it
; (nmi_pend), and the first one takes the next step before it leaves.
        .section .text.c64_nmi,"ax",@progbits
        .global c64_nmi
c64_nmi:
        pha
        txa
        pha
        tya
        pha
        lda 0xdd0d              ; acknowledge (the NMI line goes high again); FLAG2?
        and #0x10
        beq nmi_end
        inc eng_flags           ; FLAG2s, for the timing build
        bne 1f
        inc eng_flags+1
1:      lda nmi_busy
        beq 2f
        inc nmi_pend            ; nested: the first handler takes this step
        jmp nmi_end
2:      inc nmi_busy
nmi_step:
        ldx eng_state
        bne 6f
        jmp nmi_next            ; idle: a FLAG2 after a reply's end
6:      dex
        bne 7f
        jmp st_out
7:      dex
        bne 8f
        jmp st_ready
8:      dex
        bne 5f
        jmp st_hdr
5:      lda 0xdd01              ; E_DATA: a payload byte
        ldx eng_discard
        bne 2f
        ldy #0
        sta (wic64_ptr),y
        inc wic64_ptr
        bne 2f
        inc wic64_ptr+1
        lda wic64_ptr+1
        cmp #>(eng_rx + RXMAX)
        bne 2f
        lda #>eng_rx            ; the ring's end: its start
        sta wic64_ptr+1
2:      lda eng_left
        bne 3f
        dec eng_left+1
3:      dec eng_left
        lda eng_left
        ora eng_left+1
        bne nmi_next
        jmp job_done
nmi_next:                       ; another FLAG2 meanwhile?
        lda nmi_pend
        beq 4f
        dec nmi_pend
        jmp nmi_step
4:      dec nmi_busy
        lda nmi_pend            ; one just now, before nmi_busy was down
        beq nmi_end
        inc nmi_busy
        dec nmi_pend
        jmp nmi_step
nmi_end:
        pla
        tay
        pla
        tax
        pla
        rti

st_out:                         ; the last byte sent was taken
        lda eng_left
        ora eng_left+1
        beq 4f
        ldy #0
        lda (wic64_ptr),y
        sta 0xdd01              ; the next one (PC2 pulses)
        inc wic64_ptr
        bne 5f
        inc wic64_ptr+1
5:      lda eng_left
        bne 6f
        dec eng_left+1
6:      dec eng_left
        jmp nmi_next
4:      lda #0                  ; all sent: the WiC64 answers
        sta 0xdd03              ; port B in
        lda 0xdd00
        and #0xfb
        sta 0xdd00              ; PA2 low
        lda #E_READY
        sta eng_state
        jmp nmi_next

st_ready:                       ; the reply is there
        lda 0xdd01              ; the dummy read: it fetches the first byte
        lda #0
        sta eng_hdri
        lda #E_HDR
        sta eng_state
        jmp nmi_next

st_hdr:                         ; the status, the length (low, high)
        lda 0xdd01
        ldx eng_hdri
        sta eng_hdr,x
        inx
        stx eng_hdri
        cpx #3
        bne nmi_next
        lda eng_hdr+1
        sta eng_left
        lda eng_hdr+2
        sta eng_left+1
        ora eng_left
        beq job_done            ; no payload
        lda #1
        sta eng_discard
        lda eng_job
        beq 7f                  ; a write's payload: dropped
        jsr rx_free             ; free bytes (high) in A, with a page to spare for tcp_recv's moving tail
        sec
        sbc #1
        bcc 9f
        cmp eng_left+1
        beq 9f
        bcs 8f
9:      lda #1                  ; a read that does not fit: lost
        sta eng_failed
        jmp 7f
8:      lda #0
        sta eng_discard
        lda #1
        sta eng_got
        lda eng_head            ; eng_rx is page-aligned
        sta wic64_ptr
        lda eng_head+1
        clc
        adc #>eng_rx
        sta wic64_ptr+1
7:      lda #E_DATA
        sta eng_state
        jmp nmi_next

job_done:
        lda eng_hdr
        beq 1f
        lda #1                  ; an error: the connection is lost
        sta eng_failed
1:      lda eng_got
        beq 2f
        lda #0                  ; a read's bytes are in the ring: the head moves past them
        sta eng_got
        lda wic64_ptr+1
        sec
        sbc #>eng_rx
        sta eng_head+1
        lda wic64_ptr
        sta eng_head
2:      lda #E_IDLE
        sta eng_state
        lda #1
        sta eng_gap
        lda #<1970              ; 2 ms (PAL), then c64_irq starts the next command
        sta 0xdc06
        lda #>1970
        sta 0xdc07
        lda #0x19               ; timer B: start, one-shot, load
        sta 0xdc0f
        jmp nmi_next

; wic64_kick: start the next command if the WiC64 is free: a write when
; bytes are queued, else a read when asked for and the last one's bytes
; were taken. From c64_irq and from tcp_wic64.c (interrupts off inside).
        .section .text.wic64_kick,"ax",@progbits
        .global wic64_kick
wic64_kick:
        php
        sei
        lda eng_state
        ora eng_gap
        bne 9f
        lda eng_open
        beq 9f
        ldx eng_fill
        lda eng_wlen,x
        ora eng_wlenh,x
        bne kick_write
        lda eng_wantread
        bne 1f
        lda c64_ticks           ; not asked for: after 8 TV frames
        sec
        sbc eng_lastread
        cmp #8
        bcc 9f
1:      jsr rx_free
        cmp #4                  ; 1 KB free
        bcc 9f
        lda c64_ticks
        sta eng_lastread
        lda #0                  ; a read
        sta eng_wantread
        lda #1
        sta eng_job
        lda #<eng_readreq
        sta wic64_ptr
        lda #>eng_readreq
        sta wic64_ptr+1
        lda #4
        sta eng_left
        lda #0
        sta eng_left+1
        jmp kick_start
9:      plp
        rts

; the ring's free bytes, high byte in A (tail - head - 1, 11 bits); X kept
rx_free:
        lda eng_tail
        clc                     ; - head - 1
        sbc eng_head
        lda eng_tail+1
        sbc eng_head+1
        and #(RXMAX >> 8) - 1
        rts

kick_write:                     ; all that was queued in buffer x, its header in front
        lda #0
        sta eng_job
        lda eng_wlen,x
        sta eng_whdr+2
        clc
        adc #4
        sta eng_left
        lda eng_wlenh,x
        sta eng_whdr+3
        adc #0
        sta eng_left+1
        txa
        bne 1f
        lda #<eng_wbuf0
        sta wic64_ptr
        lda #>eng_wbuf0
        sta wic64_ptr+1
        jmp 2f
1:      lda #<eng_wbuf1
        sta wic64_ptr
        lda #>eng_wbuf1
        sta wic64_ptr+1
2:      ldy #3                  ; 'R', 0x23 (TCP write), the length
3:      lda eng_whdr,y
        sta (wic64_ptr),y
        dey
        bpl 3b
        txa                     ; the other buffer takes tcp_send's bytes now
        eor #1
        tax
        stx eng_fill
        lda #0
        sta eng_wlen,x
        sta eng_wlenh,x

kick_start:                     ; wic64_ptr, eng_left, eng_job set: the first byte
        inc eng_cmds
        bne 4f
        inc eng_cmds+1
4:      lda c64_ticks
        sta eng_tick
        lda 0xdd0d              ; no FLAG2 from before
        lda #0xff
        sta 0xdd03              ; port B out
        lda 0xdd00
        ora #0x04
        sta 0xdd00              ; PA2 high: the C64 sends
        ldy #0
        lda (wic64_ptr),y
        tax
        inc wic64_ptr
        bne 5f
        inc wic64_ptr+1
5:      lda eng_left
        bne 6f
        dec eng_left+1
6:      dec eng_left
        lda #E_OUT
        sta eng_state
        stx 0xdd01              ; the first byte: its FLAG2 is an NMI
        plp
        rts

        .section .rodata.eng_readreq,"a",@progbits
eng_readreq:
        .byte 0x52, 0x22, 0, 0  ; 'R', TCP read, no payload

        .section .data.eng_whdr,"aw",@progbits
eng_whdr:
        .byte 0x52, 0x23, 0, 0  ; 'R', TCP write, the length

        .section .bss.eng,"aw",@nobits
        .global eng_state, eng_open, eng_failed, eng_gap, eng_wantread, eng_head, eng_tail, eng_rx
        .global eng_fill, eng_wlen, eng_wlenh, eng_wbuf0, eng_wbuf1, eng_tick, eng_cmds, eng_flags
eng_state:      .zero 1
eng_open:       .zero 1         ; the engine runs (tcp_open .. tcp_close)
eng_failed:     .zero 1         ; an error or a reply too big: the connection is lost
eng_gap:        .zero 1         ; the pause after a command
eng_job:        .zero 1         ; 0 write, 1 read
eng_left:       .zero 2
eng_hdr:        .zero 3
eng_hdri:       .zero 1
eng_discard:    .zero 1
eng_wantread:   .zero 1
eng_got:        .zero 1         ; the read in progress stores its bytes
eng_head:       .zero 2         ; the ring: written up to here (the NMI)
eng_tail:       .zero 2         ; taken up to here (tcp_recv)
eng_lastread:   .zero 1         ; c64_ticks at the last read's start
eng_fill:       .zero 1         ; the buffer tcp_send fills
eng_wlen:       .zero 2         ; its bytes (low; buffers 0, 1)
eng_wlenh:      .zero 2         ; (high)
eng_tick:       .zero 1         ; c64_ticks when the command started
eng_cmds:       .zero 2
eng_flags:      .zero 2
nmi_busy:       .zero 1         ; the NMI handler runs
nmi_pend:       .zero 1         ; FLAG2s that came meanwhile
eng_wbuf0:      .zero 4 + WMAX
eng_wbuf1:      .zero 4 + WMAX
        .balign 256
eng_rx:         .zero RXMAX

        .section .bss.c64_ticks,"aw",@nobits
        .global c64_ticks
c64_ticks:
        .zero 1
