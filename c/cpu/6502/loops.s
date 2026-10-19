; Core loops in 6502 assembly (llvm-mos), for the 6502 machines that ask for
; them in plat_config.h (PLAT_ASM_COMPOSITE); the C in core/video.c is the
; reference, see docs/portability.md. The screen is 1000 cells, done in four
; quarters of 250 with absolute,x addressing.

; composite_frame: the start of a frame, rows 0-23 of draw_buf blank: the
; playfield (960 cells) is a copy of the map, the status row a merge.
        .section .text.composite_frame,"ax",@progbits
        .global composite_frame
composite_frame:
        ldx #0
1:      lda map_layer,x
        sta draw_buf,x
        lda map_layer+240,x
        sta draw_buf+240,x
        lda map_layer+480,x
        sta draw_buf+480,x
        lda map_layer+720,x
        sta draw_buf+720,x
        inx
        cpx #240
        bne 1b
        ldx #39                 ; row 24: non-space map cells over the status line
2:      lda map_layer+960,x
        cmp #0x20
        beq 3f
        sta draw_buf+960,x
3:      dex
        bpl 2b
        rts

; composite_map: non-space map cells over draw_buf, all 1000
        .section .text.composite_map,"ax",@progbits
        .global composite_map
composite_map:
        ldx #0
1:      lda map_layer,x
        cmp #0x20
        beq 2f
        sta draw_buf,x
2:      lda map_layer+250,x
        cmp #0x20
        beq 3f
        sta draw_buf+250,x
3:      lda map_layer+500,x
        cmp #0x20
        beq 4f
        sta draw_buf+500,x
4:      lda map_layer+750,x
        cmp #0x20
        beq 5f
        sta draw_buf+750,x
5:      inx
        cpx #250
        bne 1b
        rts
