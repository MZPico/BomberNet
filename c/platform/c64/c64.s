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

; The frame clock: a raster interrupt once a TV frame (line 251, below the
; text) counts c64_ticks. The KERNAL's interrupt entry saves the registers
; and jumps through $0314 here; $EA81 restores them. CIA 1's timer
; interrupt (the KERNAL's keyboard scan and clock) is off.
        .section .text.c64_irq,"ax",@progbits
        .global c64_irq
c64_irq:
        lda #1
        sta 0xd019              ; acknowledge the raster interrupt
        inc c64_ticks
        jmp 0xea81

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

; RESTORE's NMI (through $0318): nothing
        .section .text.c64_nmi,"ax",@progbits
        .global c64_nmi
c64_nmi:
        rti

        .section .bss.c64_ticks,"aw",@nobits
        .global c64_ticks
c64_ticks:
        .zero 1
