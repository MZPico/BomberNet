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

        .section .bss.c64_ticks,"aw",@nobits
        .global c64_ticks
c64_ticks:
        .zero 1
