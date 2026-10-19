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

; The title's DLI, at the end of the logo (row 5): the rest is ANTIC mode 2
; text, white on black.
        .section .text.atari_dli_title,"ax",@progbits
        .global atari_dli_title
atari_dli_title:
        pha
        lda atari_title_page
        sta 0xd40a              ; WSYNC
        sta 0xd409              ; CHBASE
        lda #0x00
        sta 0xd018              ; COLPF2
        lda #0x0e
        sta 0xd017              ; COLPF1
        pla
        rti

; The vertical blank, in place of the OS's: the OS's two stages cost about
; 1,000-2,000 cycles a TV frame, this a tenth of that. It does what the game
; needs of them: the clock, the hardware registers from the OS's shadows
; (set by plat_atari.c), and the sprites back to the players' places in the
; field.
        .section .text.atari_vbi,"ax",@progbits
        .global atari_vbi
atari_vbi:
        inc 0x14                ; RTCLOK
        bne 1f
        inc 0x13
        bne 1f
        inc 0x12
1:      lda 0x2f4               ; CHBAS -> CHBASE, COLOR1/2 -> COLPF1/2: the DLIs
        sta 0xd409              ; change them for the text rows
        lda 0x2c5
        sta 0xd017
        lda 0x2c6
        sta 0xd018
        lda sio_busy            ; during SIO no more than that, the clock and the
        bne 7f                  ; sprites: bytes come every 174 us at 57,600 baud (sio.s)
        lda 0x230               ; SDLSTL -> DLISTL/H
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
7:      lda atari_hpos+0        ; the sprites in the field
        sta 0xd000
        lda atari_hpos+1
        sta 0xd001
        lda atari_hpos+2
        sta 0xd002
        lda atari_hpos+3
        sta 0xd003
        lda atari_dli_vec       ; the screen's first DLI next
        sta 0x200
        lda atari_dli_vec+1
        sta 0x201
        pla                     ; Y, X, A were pushed (atari_nmi)
        tay
        pla
        tax
        pla
        rti

; The OS is off while the game runs: its ROM is switched out (the RAM
; under it, 0xc000-0xcfff and 0xd800-0xfff9, holds fonts and the network
; code: see hiram.ld and tools/build_atari.py) and nothing of it is called.
; The CPU takes its vectors from RAM then:
; - NMI: ours, as the OS's: a DLI through VDSLST, else the vertical blank;
; - IRQ: none, interrupts stay masked; the keyboard, BREAK and the serial
;   port are polled (plat_atari.c, atari_sio).
        .section .text.atari_nmi,"ax",@progbits
        .global atari_nmi
atari_nmi:
        bit 0xd40f              ; NMIST: a DLI?
        bpl 1f
        jmp (0x200)
1:      pha
        txa
        pha
        tya
        pha
        sta 0xd40f              ; NMIRES
        jmp atari_vbi
atari_rti:
        rti

; At start (before the C runtime clears its variables): interrupts masked,
; the ROM off, the RAM vectors; POKEY's keyboard and BREAK requests latched
; for polling. Inline in the start-up sequence (no RTS).
        .section .init.150,"ax",@progbits
        sei
        lda #0
        sta 0xd40e              ; NMIEN: none while the ROM goes
        lda 0xd301
        and #0xfe
        sta 0xd301
        lda #mos16lo(atari_nmi)
        sta 0xfffa
        lda #mos16hi(atari_nmi)
        sta 0xfffb
        lda #mos16lo(atari_rti)
        sta 0xfffe
        sta 0xfffc
        lda #mos16hi(atari_rti)
        sta 0xffff
        sta 0xfffd
        lda #0xc0               ; IRQEN: BREAK and keyboard
        sta atari_irqen
        sta 0xd20e
        lda #0x40               ; the vertical blank again (plat_init adds the DLIs)
        sta 0xd40e
        lda #0x00               ; the C stack from below the screen (0xb400), not MEMTOP
        sta __rc0
        lda #0xb4
        sta __rc1

; While the program loads: a segment of the code that lives under the ROM,
; loaded into a buffer (4 bytes: destination, length; then the bytes), and
; this routine run by the loader (INITAD) to copy it there. The OS stays
; quiet meanwhile: no interrupts, the ROM back on at the end.
        .section .text.atari_hicopy,"ax",@progbits
        .global atari_hicopy
atari_hicopy:
        php
        sei
        lda 0xd40e
        pha
        lda #0
        sta 0xd40e
        lda 0xd301
        pha
        and #0xfe
        sta 0xd301
        lda 0xb400              ; HIBUF (tools/build_atari.py): destination, length, bytes
        sta 0x80                ; zero page __rc0..3 (the C runtime's, free while loading)
        lda 0xb401
        sta 0x81
        lda #0x04
        sta 0x82
        lda #0xb4
        sta 0x83
        ldx 0xb403              ; whole pages
        ldy #0
1:      cpx #0
        beq 3f
2:      lda (0x82),y
        sta (0x80),y
        iny
        bne 2b
        inc 0x81
        inc 0x83
        dex
        jmp 1b
3:      ldx 0xb402              ; the rest
        beq 5f
4:      lda (0x82),y
        sta (0x80),y
        iny
        dex
        bne 4b
5:      pla
        sta 0xd301
        pla
        sta 0xd40e
        plp
        rts


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


