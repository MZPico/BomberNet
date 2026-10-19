; Atari XL/XE: the display list interrupt, the deferred VBI and the screen
; flush (llvm-mos). See plat_atari.c for the layout.

; Two DLIs. At the end of field row 22: the four player sprites move to
; the players' lives on the status line (row 23 is the bottom wall, where no
; sprite has data, so moving them there shows nothing half-way).
        .section .text.atari_dli,"ax",@progbits
        .global atari_dli
atari_dli:
        pha
        lda atari_hud_hpos+0
        sta 0xd000              ; HPOSP0..3
        lda atari_hud_hpos+1
        sta 0xd001
        lda atari_hud_hpos+2
        sta 0xd002
        lda atari_hud_hpos+3
        sta 0xd003
        lda #mos16lo(atari_dli2)
        sta 0x200               ; VDSLST: the next DLI
        lda #mos16hi(atari_dli2)
        sta 0x201
        pla
        rti

; At the end of row 23: the status line is ANTIC mode 2 text, white on black.
atari_dli2:
        pha
        lda atari_text_page
        sta 0xd40a              ; WSYNC
        sta 0xd409              ; CHBASE
        lda #0x00
        sta 0xd018              ; COLPF2: the text's background
        lda #0x0e
        sta 0xd017              ; COLPF1: the text's luminance
        lda #mos16lo(atari_dli)
        sta 0x200
        lda #mos16hi(atari_dli)
        sta 0x201
        pla
        rti

; The vertical blank, in place of the OS's (VVBLKI): the OS's two stages
; cost about 1,000-2,000 cycles a TV frame, this a tenth of that. It does
; what the game needs of them: the clock, the hardware registers from the
; OS's shadows (set by plat_atari.c), the key debounce delay, SIO's timeout
; timer 1 (for the network), and the sprites back to the players' places in
; the field.
        .section .text.atari_vbi,"ax",@progbits
        .global atari_vbi
atari_vbi:
        inc 0x14                ; RTCLOK
        bne 1f
        inc 0x13
        bne 1f
        inc 0x12
1:      lda 0x230               ; SDLSTL -> DLISTL/H
        sta 0xd402
        lda 0x231
        sta 0xd403
        lda 0x22f               ; SDMCTL -> DMACTL
        sta 0xd400
        lda 0x2f4               ; CHBAS -> CHBASE
        sta 0xd409
        lda 0x26f               ; GPRIOR -> PRIOR
        sta 0xd01b
        ldx #8                  ; PCOLR0-3, COLOR0-4 -> COLPM0-3, COLPF0-3, COLBK
2:      lda 0x2c0,x
        sta 0xd012,x
        dex
        bpl 2b
        lda atari_hpos+0        ; the sprites in the field
        sta 0xd000
        lda atari_hpos+1
        sta 0xd001
        lda atari_hpos+2
        sta 0xd002
        lda atari_hpos+3
        sta 0xd003
        lda #mos16lo(atari_dli) ; the first DLI next
        sta 0x200
        lda #mos16hi(atari_dli)
        sta 0x201
        lda 0x2f1               ; KEYDEL: the keyboard IRQ ignores the same key until 0
        beq 3f
        dec 0x2f1
3:      lda 0x218               ; CDTMV1, SIO's timeout: count down, at 0 call CDTMA1
        bne 4f
        lda 0x219
        beq 6f
        dec 0x219
4:      dec 0x218
        lda 0x218
        ora 0x219
        bne 6f
        jsr 5f
6:      jmp 0xe462              ; XITVBV
5:      jmp (0x226)

; flush in game mode: every changed cell of draw_buf to the screen, the
; field (960 cells, not cleared: composite_frame overwrites them, see
; platform.h) through atari_field_code, the status line through
; atari_text_code, cleared. Eight sections of 120, absolute,x: 11 cycles an
; unchanged cell.
        .section .text.atari_flush_game,"ax",@progbits
        .global atari_flush_game
atari_flush_game:
        ldx #119
10:      lda draw_buf+0,x
        cmp shadow_vram+0,x
        beq 11f
        sta shadow_vram+0,x
        tay
        lda atari_field_code,y
        sta atari_screen+0,x
11:
12:      lda draw_buf+120,x
        cmp shadow_vram+120,x
        beq 13f
        sta shadow_vram+120,x
        tay
        lda atari_field_code,y
        sta atari_screen+120,x
13:
14:      lda draw_buf+240,x
        cmp shadow_vram+240,x
        beq 15f
        sta shadow_vram+240,x
        tay
        lda atari_field_code,y
        sta atari_screen+240,x
15:
16:      lda draw_buf+360,x
        cmp shadow_vram+360,x
        beq 17f
        sta shadow_vram+360,x
        tay
        lda atari_field_code,y
        sta atari_screen+360,x
17:
18:      lda draw_buf+480,x
        cmp shadow_vram+480,x
        beq 19f
        sta shadow_vram+480,x
        tay
        lda atari_field_code,y
        sta atari_screen+480,x
19:
20:      lda draw_buf+600,x
        cmp shadow_vram+600,x
        beq 21f
        sta shadow_vram+600,x
        tay
        lda atari_field_code,y
        sta atari_screen+600,x
21:
22:      lda draw_buf+720,x
        cmp shadow_vram+720,x
        beq 23f
        sta shadow_vram+720,x
        tay
        lda atari_field_code,y
        sta atari_screen+720,x
23:
24:      lda draw_buf+840,x
        cmp shadow_vram+840,x
        beq 25f
        sta shadow_vram+840,x
        tay
        lda atari_field_code,y
        sta atari_screen+840,x
25:
        dex
        bmi 30f
        jmp 10b
30:     ldx #39                 ; the status line
31:     ldy draw_buf+960,x
        lda #0x20
        sta draw_buf+960,x
        tya
        cmp shadow_vram+960,x
        beq 32f
        sta shadow_vram+960,x
        lda atari_text_code,y
        sta atari_screen+960,x
32:     dex
        bpl 31b
        rts
