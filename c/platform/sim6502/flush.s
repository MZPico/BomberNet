; flush_screen of the 6502 measurement platform, as a C64 or an Atari would
; have it: every cell of draw_buf that differs from shadow_vram goes to the
; screen (its code, sim_codes[]) and the colour memory (sim_attrs[]); draw_buf
; is cleared. Four quarters of 250 cells, absolute,x; the cell code indexes
; the 256-byte tables with y.
        .section .text.flush_cells,"ax",@progbits
        .global flush_cells
flush_cells:
        ldx #0
1:      ldy draw_buf,x
        lda #0x20
        sta draw_buf,x
        tya
        cmp shadow_vram,x
        beq 2f
        sta shadow_vram,x
        lda sim_codes,y
        sta sim_vram,x
        lda sim_attrs,y
        sta sim_vattr,x
2:      ldy draw_buf+250,x
        lda #0x20
        sta draw_buf+250,x
        tya
        cmp shadow_vram+250,x
        beq 3f
        sta shadow_vram+250,x
        lda sim_codes,y
        sta sim_vram+250,x
        lda sim_attrs,y
        sta sim_vattr+250,x
3:      ldy draw_buf+500,x
        lda #0x20
        sta draw_buf+500,x
        tya
        cmp shadow_vram+500,x
        beq 4f
        sta shadow_vram+500,x
        lda sim_codes,y
        sta sim_vram+500,x
        lda sim_attrs,y
        sta sim_vattr+500,x
4:      ldy draw_buf+750,x
        lda #0x20
        sta draw_buf+750,x
        tya
        cmp shadow_vram+750,x
        beq 5f
        sta shadow_vram+750,x
        lda sim_codes,y
        sta sim_vram+750,x
        lda sim_attrs,y
        sta sim_vattr+750,x
5:      inx
        cpx #250
        beq 6f
        jmp 1b
6:      rts
