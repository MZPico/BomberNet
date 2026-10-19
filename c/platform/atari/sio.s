; SIO without the OS (its ROM is off while the game runs): one command to a
; device on the serial bus, polled, with interrupts masked. The protocol as
; the OS's SIO and FujiNet's firmware (lib/bus/sio) have it:
;   COMMAND line low; device, command, aux1, aux2, checksum; COMMAND high;
;   the device answers 'A' (or 'N');
;   out: the data and its checksum, 'A' again; then 'C' (or 'E') when done;
;   in:  'C' (or 'E'), then the data and its checksum.
; The checksum is the sum with the carries added back in. The baud rate is
; POKEY channels 3+4 joined at 1.79 MHz: divisor sio_div, 0x28 for the
; standard 19,200 baud, smaller for FujiNet's high speed.
;
; In: sio_dev, sio_cmd, sio_aux (16 bits), sio_buf, sio_len, sio_dir (0x40
; in, 0x80 out, 0 none), sio_div, sio_wait (ticks of 20 ms to wait for 'C').
; Out: A = 1 done, 0x8a timeout, 0x8b NAK, 0x8c framing or overrun, 0x8f
; checksum, 0x90 the device's error ('E').

SEROUT = 0xd20d
SERIN  = 0xd20d                 ; read (SEROUT: write)
SKCTL  = 0xd20f
SKSTAT = 0xd20f
SKRES  = 0xd20a
IRQEN  = 0xd20e
IRQST  = 0xd20e
AUDF3  = 0xd204
AUDC3  = 0xd205
AUDF4  = 0xd206
AUDC4  = 0xd207
AUDCTL = 0xd208
STIMER = 0xd209                 ; write
PBCTL  = 0xd303
RTCLOK = 0x14
SENDWAIT = 50                   ; ticks a send may stall (an emulated bus pauses while it forwards)
GETWAIT = 10                    ; ticks to wait for a byte (likewise)
T0STEPS = 7                     ; 768-896 us (delay): FujiNet's idle loop empties its
                                ; serial input until it sees COMMAND, so the spec's t0
T1STEPS = 2                     ; 128-256 us: FujiNet waits for COMMAND high anyway
T3STEPS = 2                     ; 128-256 us: after its ACK FujiNet reads our data
                                ; frame from its UART buffer

        .section .bss.sio,"aw",@nobits
        .global sio_dev, sio_cmd, sio_aux, sio_buf, sio_len, sio_dir, sio_div, sio_wait, sio_btime, atari_irqen
sio_dev:  .zero 1
sio_cmd:  .zero 1
sio_aux:  .zero 2
sio_buf:  .zero 2
sio_len:  .zero 2
sio_dir:  .zero 1
sio_div:  .zero 1
sio_wait: .zero 1
sio_btime: .zero 1              ; a byte's time in VCOUNT steps (bytetime)
atari_irqen: .zero 1            ; IRQEN's bits outside SIO (keyboard, BREAK)
        .global sio_last, sio_busy, sio_acked
sio_last: .zero 1               ; the last call's result
sio_busy: .zero 1               ; 1 while a command runs
sio_acked: .zero 1              ; 1 once the device accepted the command frame
frame:    .zero 5
sum:      .zero 1
left:     .zero 2
limit:    .zero 1               ; RTCLOK value at which a wait gives up

ptr = 0x30                      ; zero page of the OS's own SIO (the OS is off)

        .section .text.atari_sio,"ax",@progbits
        .global atari_sio
atari_sio:
        lda #0
        sta sio_acked
        lda #1
        sta sio_busy            ; a short vertical blank meanwhile (atari.s)
        lda sio_div
        sta AUDF3
        lda #0
        sta AUDF4
        lda #0x28               ; channel 3 at 1.79 MHz, joined with 4 (kept so:
        sta AUDCTL              ; voices 1 and 2, the tones, are not affected)
        sta STIMER              ; the timers restarted at the new rate, as the OS
                                ; does: a stale count garbles the first bits
        lda #0xa0               ; pure tone, volume 0: no noise from the bus
        sta AUDC3
        sta AUDC4
        ; the command frame
        lda sio_dev
        sta frame
        lda sio_cmd
        sta frame+1
        lda sio_aux
        sta frame+2
        lda sio_aux+1
        sta frame+3
        lda #mos16lo(frame)
        sta ptr
        lda #mos16hi(frame)
        sta ptr+1
        lda #4
        sta left
        lda #0
        sta left+1
        jsr checksum
        sta frame+4
        sta SKRES               ; clear old serial errors
        lda #0x34               ; COMMAND low
        sta PBCTL
        ldx #T0STEPS
        jsr delay               ; t0
        lda #5
        sta left
        jsr send
        bcc 9f
        jmp fail
9:
        jsr rxmode              ; the device may answer at once
        ldx #T1STEPS
        jsr delay               ; t1
        lda #0x3c               ; COMMAND high
        sta PBCTL
        lda #GETWAIT            ; the answer
        jsr get
        bcc 9f
        jmp fail
9:
        cmp #'A'
        beq 9f
        jmp nak
9:
        lda #1                  ; accepted: what follows may have taken effect
        sta sio_acked
        lda sio_dir
        bpl 9f
        jmp out
9:      bne 9f
        jmp done
9:
        ; in: 'C', the data, its checksum
        jsr complete
        bcc 9f
        jmp fail
9:
        jsr setbuf
        jsr recv
        bcc 9f
        jmp fail
9:
        jsr setbuf
        jsr checksum
        sta sum
        lda #GETWAIT
        jsr get
        bcc 9f
        jmp fail
9:
        cmp sum
        beq 9f
        jmp badsum
9:
        lda #1
        jmp leave
out:    ldx #T3STEPS
        jsr delay               ; t3: the device ready for our data frame
        jsr setbuf
        jsr checksum            ; the data's checksum after it
        sta sum
        jsr setbuf
        jsr send
        bcc 9f
        jmp fail
9:
        lda #mos16lo(sum)
        sta ptr
        lda #mos16hi(sum)
        sta ptr+1
        lda #1
        sta left
        lda #0
        sta left+1
        jsr send
        bcc 9f
        jmp fail
9:
        jsr rxmode
        lda #GETWAIT
        jsr get
        bcc 9f
        jmp fail
9:
        cmp #'A'
        beq 9f
        jmp nak
9:
done:   jsr complete
        bcc 9f
        jmp fail
9:
        lda #1
        jmp leave
nak:    lda #0x8b
        jmp leave
badsum: lda #0x8f
        jmp leave
fail:   ; A: the error
leave:  sta sio_last             ; for a look in a debugger
        pha
        lda #0
        sta sio_busy
        lda #0x3c
        sta PBCTL
        lda atari_irqen
        sta IRQEN
        lda #3                  ; SKCTL: keyboard scan and debounce only
        sta SKCTL
        pla
        rts

; 'C' within sio_wait ticks; 'E': the device's error (its data is not read)
complete:
        lda sio_wait
        jsr get
        bcs 1f
        cmp #'C'
        beq 2f
        lda #0x90
        sec
1:      rts
2:      clc
        rts

setbuf: lda sio_buf
        sta ptr
        lda sio_buf+1
        sta ptr+1
        lda sio_len
        sta left
        lda sio_len+1
        sta left+1
        rts

; A = the sum of `left` bytes at ptr, carries added back in
checksum:
        ldy #0
        lda #0
        ldx left+1
        beq 3f
1:      clc                     ; whole pages
2:      adc (ptr),y
        adc #0
        iny
        bne 2b
        inc ptr+1
        dex
        bne 1b
3:      ldx left
        beq 5f
        clc
4:      adc (ptr),y
        adc #0
        iny
        dex
        bne 4b
5:      rts

; send `left` bytes (1 or more) from ptr, and wait until the last has gone
; out. As the OS: the requests enabled, the first byte at once, then each
; "output needed" acknowledged before the next byte is written (POKEY only
; latches a request that is enabled when it happens, and one acknowledged
; after the write may be the new byte's). Gives up after SENDWAIT ticks
; without progress (POKEY sends with or without a listener, so only a fault
; gets there): carry set, A = 0x8a.
send:   lda #0x23               ; SKCTL: transmit at channel 4's rate
        sta SKCTL
        lda atari_irqen         ; no old request; then "output needed" latched
        sta IRQEN               ; before the first byte, which moves on at once
        ora #0x10
        sta IRQEN
        ldy #0
        lda (ptr),y
        sta SEROUT
        jsr next
1:      lda left
        ora left+1
        beq 4f
        lda RTCLOK
        clc
        adc #SENDWAIT
        sta limit
2:      lda IRQST
        and #0x10
        beq 3f
        lda RTCLOK
        cmp limit
        bne 2b
        jmp 6f
3:      lda atari_irqen         ; acknowledge "output needed", then the byte
        sta IRQEN
        ora #0x10
        sta IRQEN
        lda (ptr),y
        sta SEROUT
        jsr next
        jmp 1b
4:      lda RTCLOK              ; the last byte into the shift register ("needed"
        clc                     ; once more), then its time on the line: not
        adc #SENDWAIT           ; "output done", which is not latched
        sta limit
5:      lda IRQST
        and #0x10
        beq 8f
        lda RTCLOK
        cmp limit
        bne 5b
        jmp 6f
8:      jsr bytetime
        jmp 7f
6:      lda atari_irqen
        sta IRQEN
        lda #0x8a
        sec
        rts
7:      lda atari_irqen
        sta IRQEN
        clc
        rts

; one byte sent: ptr,y on, left down
next:   iny
        bne 1f
        inc ptr+1
1:      lda left
        bne 2f
        dec left+1
2:      dec left
        rts

; receive `left` bytes to ptr, tight: at 57,600 baud a byte comes every
; 174 us, and a DLI waiting for its scan line may take 60 of them. About
; 45 cycles a byte: the timeout looked at every 256 polls (GETWAIT ticks
; for the whole block), POKEY's latched errors (framing, overrun) once at
; the end. Carry set and A = the error.
recv:   lda RTCLOK
        clc
        adc #GETWAIT
        sta limit
        ldy #0
1:      lda left
        ora left+1
        beq 6f
        ldx #0
2:      lda IRQST
        and #0x20
        beq 3f
        dex
        bne 2b
        lda RTCLOK
        cmp limit
        bne 2b
        lda #0x8a
        sec
        rts
3:      lda atari_irqen         ; acknowledge "input ready", ready for the next
        sta IRQEN
        ora #0x20
        sta IRQEN
        lda SERIN
        sta (ptr),y
        iny
        bne 4f
        inc ptr+1
4:      lda left
        bne 5f
        dec left+1
5:      dec left
        jmp 1b
6:      lda SKSTAT              ; framing error, overrun: bits 7, 6 low
        and #0xc0
        cmp #0xc0
        bne 7f
        clc
        rts
7:      sta SKRES
        lda #0x8c
        sec
        rts

; one byte within A ticks: A = the byte, carry clear; carry set and A =
; 0x8a (timeout) or 0x8c (framing, overrun). Keeps Y.
get:    clc
        adc RTCLOK
        sta limit
1:      lda IRQST
        and #0x20
        beq 2f
        lda RTCLOK
        cmp limit
        bne 1b
        lda #0x8a
        sec
        rts
2:      lda SKSTAT              ; framing error, overrun: bits 7, 6 low
        and #0xc0
        cmp #0xc0
        bne 3f
        lda atari_irqen         ; acknowledge "input ready", ready for the next
        sta IRQEN
        ora #0x20
        sta IRQEN
        lda SERIN
        clc
        rts
3:      sta SKRES
        lda #0x8c
        sec
        rts

; receive from here on: SKCTL at channel 4's rate, "input ready" latched
rxmode: lda #0x13
        sta SKCTL
        lda atari_irqen
        ora #0x20
        sta IRQEN
        rts

; a byte's time on the line, a little more, by VCOUNT (+1 every 2 scan
; lines, 128 us), not by the CPU's speed (ANTIC's DMA varies it): sio_btime
; changes of VCOUNT (tcp_fujinet.c: (sio_div + 7) * 23 / 256 + 2). The
; device answers 850 us after the byte at the earliest: receiving must be
; on by then.
VCOUNT = 0xd40b
bytetime:
        ldx sio_btime
1:      lda VCOUNT
2:      cmp VCOUNT
        beq 2b
        dex
        bne 1b
        rts

; X changes of VCOUNT (128 us each), whatever ANTIC's DMA does to the
; CPU's speed: the command frame's t0 (COMMAND low to the first byte) and
; t1 (the last byte to COMMAND high), t3 (ACK to our data frame). The SIO
; spec asks 750, 650 and 1000 us; FujiNet needs the first (see T0STEPS),
; not the others, and the gaps are much of a short command's time
delay:
1:      lda VCOUNT
2:      cmp VCOUNT
        beq 2b
        dex
        bne 1b
        rts
