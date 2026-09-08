; =====================================================================
;  BOMBER  (bomber.mzf, tape name " F1200")  -  Sharp MZ-700, Z80
;  Complete disassembly, reassembles byte-identical with pasmo:
;      pasmo --bin bomber.asm bomber.bin
;  Load/exec address 1200h, body length 2010h (1200h..320Fh).
;
;  Coordinates everywhere: B = row (Y, 0..24), C = column (X, 0..39).
;  Sprites are 2x2 characters; the board is 19x11 cells at screen
;  (1,1)..(38,22) inside a 0x88 wall; row 24 is the HUD.
;  Every character is a "logical code" translated to MZ-700 display
;  code + attribute by game_table / title_table in flush_screen.
; =====================================================================

; Monitor ROM (1Z-013A) entry points and work area
GETKY:	equ 0001Bh
MSTA:	equ 00044h
MSTP:	equ 00047h
RATIO:	equ 011A1h
VRAM:	equ 0D000h

	org 1200h

; ---- start @ 1200
; Cold entry (MZF exec address). Reset SP, clear buffers, zero score/hi-score, go to title.
start:
	di                          ; 1200
	ld sp,start                 ; 1201  stack grows down from the program start
	call clear_buffers          ; 1204
	ld hl,00000h                ; 1207
	ld (hi_score),hl            ; 120A  hi-score = 0
	ld (score),hl               ; 120D  score = 0
	jp title_screen             ; 1210

; ---- new_game @ 1213
; Start a new game: score=0, stage=1, lives=3.
new_game:
	ld hl,00000h                ; 1213
	ld (score),hl               ; 1216
	ld a,001h                   ; 1219  stage = 1
	ld (stage),a                ; 121B
	ld a,003h                   ; 121E  lives = 3
	ld (lives),a                ; 1220

; ---- stage_start @ 1223
; (Re)start the current stage: time=1000, clear flags, build map, spawn enemies.
stage_start:
	ld hl,003e8h                ; 1223  time = 1000
	ld (time_left),hl           ; 1226
	xor a                       ; 1229
	ld (timeout_flag),a         ; 122A
	ld (stage_cleared),a        ; 122D
	ld (bonus_present),a        ; 1230
	ld (exit_present),a         ; 1233
	ld (hit_spawned),a          ; 1236
	ld (hit_pending),a          ; 1239
	ld (bonus_revealed),a       ; 123C
	ld (exit_revealed),a        ; 123F
	ld (exit_touched),a         ; 1242
	ld (life_lost),a            ; 1245
	ld a,001h                   ; 1248
	ld (player_state),a         ; 124A
	call load_stage_params      ; 124D
	call clear_enemies          ; 1250
	call spawn_enemies          ; 1253
	call clear_bombs            ; 1256
	call clear_buffers          ; 1259
	call clear_map              ; 125C
	call draw_walls             ; 125F
	call generate_map           ; 1262
	call composite_map          ; 1265
	xor a                       ; 1268
	ld (bonus_present),a        ; 1269
	ld (exit_present),a         ; 126C
	ld hl,00918h                ; 126F  bytes 18 09 = "jr +9": game mode for put_vram_char
	ld (mode_patch),hl          ; 1272

; ---- main_loop @ 1275
; Main game loop - one iteration per frame.
main_loop:
	call tick_timers            ; 1275
	call draw_hud               ; 1278
	call draw_walls             ; 127B
	call draw_hud_icons         ; 127E
	call composite_map          ; 1281
	call update_bombs           ; 1284
	call draw_bombs             ; 1287
	call place_bomb             ; 128A
	call player_anim            ; 128D
	call enemy_ai               ; 1290
	call draw_bonus             ; 1293
	call draw_exit              ; 1296
	call draw_enemies           ; 1299
	call reveal_bonus           ; 129C
	call reveal_exit            ; 129F
	call draw_player            ; 12A2
	call spawn_from_hit         ; 12A5
	call check_pickups          ; 12A8
	call time_tick              ; 12AB
	ld a,(life_lost)            ; 12AE
	or a                        ; 12B1
	jp nz,player_dead           ; 12B2  death animation finished?
	ld a,(exit_touched)         ; 12B5
	or a                        ; 12B8
	jp nz,exit_taken            ; 12B9  exit tile touched?
	ld a,(stage_cleared)        ; 12BC
	or a                        ; 12BF
	jr z,main_loop              ; 12C0  stage not cleared -> next frame
	ld bc,00014h                ; 12C2  20 frames

; ---- stage_clear_anim @ 12C5
; Run 20 frames without input (enemy death animation finishes).
stage_clear_anim:
	push bc                     ; 12C5
	call frame_no_input         ; 12C6
	pop bc                      ; 12C9
	dec bc                      ; 12CA
	ld a,b                      ; 12CB
	or c                        ; 12CC
	jp nz,stage_clear_anim      ; 12CD
	ld hl,(time_left)           ; 12D0
	ld a,h                      ; 12D3
	or l                        ; 12D4
	jr z,next_stage             ; 12D5

; ---- time_bonus_loop @ 12D7
; Convert remaining time into score, 10 time = 1 point (x10 on display), beep each step.
time_bonus_loop:
	push hl                     ; 12D7
	call frame_minimal          ; 12D8
	ld hl,00a00h                ; 12DB  tone ratio 0x0A00
	ld (RATIO),hl               ; 12DE
	ld bc,0000ah                ; 12E1
	call beep                   ; 12E4
	pop hl                      ; 12E7
	ld bc,0000ah                ; 12E8
	or a                        ; 12EB  time -= 10
	sbc hl,bc                   ; 12EC
	ld (time_left),hl           ; 12EE
	ld de,(score)               ; 12F1  score += 1
	inc de                      ; 12F5
	ld (score),de               ; 12F6
	ld a,h                      ; 12FA
	or l                        ; 12FB
	jp nz,time_bonus_loop       ; 12FC
	call frame_minimal          ; 12FF
	call frame_minimal          ; 1302

; ---- next_stage @ 1305
; stage++, pause, restart at stage_start.
next_stage:
	ld a,(stage)                ; 1305
	inc a                       ; 1308
	ld (stage),a                ; 1309
	call delay                  ; 130C
	jp stage_start              ; 130F

; ---- exit_taken @ 1312
; Player touched the EXIT tile: 5 frames, then the SAME stage is regenerated.
exit_taken:
	ld bc,00005h                ; 1312
.et_loop:
	push bc                     ; 1315
	call frame_no_input         ; 1316
	pop bc                      ; 1319
	dec bc                      ; 131A
	ld a,b                      ; 131B
	or c                        ; 131C
	jr nz,.et_loop              ; 131D
	jp stage_start              ; 131F

; ---- player_dead @ 1322
; Death animation finished: lives--, game over when 0.
player_dead:
	call delay                  ; 1322
	ld a,(lives)                ; 1325
	dec a                       ; 1328
	or a                        ; 1329
	ld (lives),a                ; 132A
	jr z,game_over              ; 132D
	jp stage_start              ; 132F

; ---- load_stage_params @ 1332
; Load enemy count / enemy move period from stage_table (stage 5+ uses last entry).
load_stage_params:
	ld a,(stage)                ; 1332
	cp 006h                     ; 1335  stages 1..5 indexed, 6+ clamp
	jr c,.lsp_idx               ; 1337
	ld a,005h                   ; 1339
.lsp_idx:
	dec a                       ; 133B
	add a,a                     ; 133C
	ld hl,stage_table           ; 133D
	ld c,a                      ; 1340
	ld b,000h                   ; 1341
	add hl,bc                   ; 1343
	ld a,(hl)                   ; 1344
	ld (enemies_left),a         ; 1345
	inc hl                      ; 1348
	ld a,(hl)                   ; 1349
	ld (enemy_period),a         ; 134A
	ret                         ; 134D

; ---- stage_table @ 134E
; Per stage: enemy count, enemy behaviour-cycle period. Stage>=5 uses the last row.
stage_table:
	defb 01h,10h	; stage 1: enemies, period
	defb 02h,15h	; stage 2: enemies, period
	defb 03h,1Ah	; stage 3: enemies, period
	defb 04h,1Fh	; stage 4: enemies, period
	defb 04h,24h	; stage 5: enemies, period

; ---- game_over @ 1358
; Game over: 5 idle frames, then title screen.
game_over:
	ld bc,00005h                ; 1358
.go_loop:
	push bc                     ; 135B
	call frame_no_input         ; 135C
	pop bc                      ; 135F
	dec bc                      ; 1360
	ld a,b                      ; 1361
	or c                        ; 1362
	jr nz,.go_loop              ; 1363
	jp title_screen             ; 1365

; ---- frame_no_input @ 1368
; One game frame without player control or bomb keys (used for animations).
frame_no_input:
	call tick_timers            ; 1368
	call draw_hud               ; 136B
	call draw_walls             ; 136E
	call draw_hud_icons         ; 1371
	call composite_map          ; 1374
	call draw_bombs             ; 1377
	call draw_bonus             ; 137A
	call draw_exit              ; 137D
	call draw_enemies           ; 1380
	call draw_player            ; 1383
	call update_bombs           ; 1386
	ret                         ; 1389

; ---- frame_minimal @ 138A
; One frame updating only HUD/walls/map/player (used during time bonus).
frame_minimal:
	call tick_timers            ; 138A
	call draw_hud               ; 138D
	call draw_walls             ; 1390
	call draw_hud_icons         ; 1393
	call composite_map          ; 1396
	call draw_bonus             ; 1399
	call draw_exit              ; 139C
	call draw_player            ; 139F
	ret                         ; 13A2

; ---- title_screen @ 13A3
; Title screen: switch translation mode, draw logo, legend, demo sprites; wait for SPACE.
title_screen:
	ld hl,00000h                ; 13A3  bytes 00 00 = nop nop: title mode
	ld (mode_patch),hl          ; 13A6
	call clear_buffers          ; 13A9
	call title_init_enemies     ; 13AC
	call title_init_bombs       ; 13AF
title_loop:
	call GETKY                  ; 13B2  monitor GETKY: A = key or 0
	cp 020h                     ; 13B5  SPACE starts the game
	jp z,new_game               ; 13B7
	call tick_timers            ; 13BA
	ld bc,0*256+0               ; 13BD  Y=0,X=0
	call draw_addr              ; 13C0
	ld d,0f0h                   ; 13C3  240 chars = 6 rows
	ld e,089h                   ; 13C5  pillar char fills rows 0..5 first
.tl_fill:
	ld a,e                      ; 13C7
	ld (bc),a                   ; 13C8
	inc bc                      ; 13C9
	dec d                       ; 13CA
	jr nz,.tl_fill              ; 13CB
	ld bc,0*256+0               ; 13CD  Y=0,X=0
	call draw_addr              ; 13D0
	ld hl,title_logo            ; 13D3
.tl_logo:
	ld a,(hl)                   ; 13D6  copy logo until 0xFF
	inc a                       ; 13D7
	jr z,.tl_text               ; 13D8
	dec a                       ; 13DA
	ld (bc),a                   ; 13DB
	inc bc                      ; 13DC
	inc hl                      ; 13DD
	jr .tl_logo                 ; 13DE
.tl_text:
	ld bc,11*256+17             ; 13E0  Y=11,X=17
	call draw_addr              ; 13E3
	ld a,00ah                   ; 13E6  BONUS legend tile
	call put_tile               ; 13E8
	ld bc,11*256+25             ; 13EB  Y=11,X=25
	call draw_addr              ; 13EE
	ld a,00eh                   ; 13F1  EXIT legend tile
	call put_tile               ; 13F3
	ld bc,10*256+6              ; 13F6  Y=10,X=6
	call draw_addr              ; 13F9
	ld hl,str_legend1           ; 13FC
	call print_string           ; 13FF
	ld bc,11*256+6              ; 1402  Y=11,X=6
	call draw_addr              ; 1405
	ld hl,str_box_tl            ; 1408
	call print_string           ; 140B
	ld bc,12*256+6              ; 140E  Y=12,X=6
	call draw_addr              ; 1411
	ld hl,str_box_ml            ; 1414
	call print_string           ; 1417
	ld bc,13*256+2              ; 141A  Y=13,X=2
	call draw_addr              ; 141D
	ld hl,str_legend2           ; 1420
	call print_string           ; 1423
	ld bc,14*256+3              ; 1426  Y=14,X=3
	call draw_addr              ; 1429
	ld hl,str_box2_t            ; 142C
	call print_string           ; 142F
	ld bc,15*256+3              ; 1432  Y=15,X=3
	call draw_addr              ; 1435
	ld hl,str_box2_m            ; 1438
	call print_string           ; 143B
	ld bc,16*256+3              ; 143E  Y=16,X=3
	call draw_addr              ; 1441
	ld hl,str_box2_b            ; 1444
	call print_string           ; 1447
	ld bc,17*256+6              ; 144A  Y=17,X=6
	call draw_addr              ; 144D
	ld hl,str_box3_t            ; 1450
	call print_string           ; 1453
	ld bc,18*256+6              ; 1456  Y=18,X=6
	call draw_addr              ; 1459
	ld hl,str_box3_m            ; 145C
	call print_string           ; 145F
	ld bc,19*256+6              ; 1462  Y=19,X=6
	call draw_addr              ; 1465
	ld hl,str_box3_b            ; 1468
	call print_string           ; 146B
	ld bc,20*256+6              ; 146E  Y=20,X=6
	call draw_addr              ; 1471
	ld hl,str_down              ; 1474
	call print_string           ; 1477
	ld bc,22*256+8              ; 147A  Y=22,X=8
	call draw_addr              ; 147D
	ld hl,str_push_space        ; 1480
	call print_string           ; 1483
	ld bc,24*256+2              ; 1486  Y=24,X=2
	call draw_addr              ; 1489
	ld hl,str_copyright         ; 148C
	call print_string           ; 148F
	ld bc,7*256+4               ; 1492  Y=7,X=4
	call draw_addr              ; 1495
	ld hl,str_legend_row7       ; 1498
	call print_string           ; 149B
	ld bc,8*256+5               ; 149E  Y=8,X=5
	call draw_addr              ; 14A1
	ld hl,str_legend_row8       ; 14A4
	call print_string           ; 14A7
	ld bc,16*256+32             ; 14AA  Y=16,X=32
	call draw_addr              ; 14AD
	ld hl,(hi_score)            ; 14B0
	call print_num5             ; 14B3
	ld bc,18*256+32             ; 14B6  Y=18,X=32
	call draw_addr              ; 14B9
	ld hl,(score)               ; 14BC
	call print_num5             ; 14BF
	ld bc,7*256+2               ; 14C2  Y=7,X=2
	call draw_addr              ; 14C5
	ld a,08ch                   ; 14C8  BOMBER MAN legend sprite
	call put_tile               ; 14CA
	call draw_bombs             ; 14CD
	call draw_enemies           ; 14D0
	ld a,(tmr_player_anim)      ; 14D3  flip enemy/bomb animation when player timer wraps
	or a                        ; 14D6
	jr nz,.tl_next              ; 14D7
	ld a,(bomb_anim)            ; 14D9
	xor 002h                    ; 14DC
	ld (bomb_anim),a            ; 14DE
	ld a,(enemy_anim)           ; 14E1
	xor 002h                    ; 14E4
	ld (enemy_anim),a           ; 14E6
.tl_next:
	jp title_loop               ; 14E9

; ---- title_init_bombs @ 14EC
; Demo bomb for the legend at (32,11).
title_init_bombs:
	call clear_bombs            ; 14EC
	ld ix,bomb_table            ; 14EF
	ld (ix+000h),001h           ; 14F3
	ld (ix+001h),020h           ; 14F7
	ld (ix+002h),00bh           ; 14FB
	ret                         ; 14FF

; ---- title_init_enemies @ 1500
; Four demo enemies (types 0..3) for the legend row 7.
title_init_enemies:
	call clear_enemies          ; 1500
	ld ix,enemy_table           ; 1503
	ld (ix+000h),001h           ; 1507
	ld (ix+001h),00bh           ; 150B
	ld (ix+002h),007h           ; 150F
	ld (ix+003h),000h           ; 1513
	ld bc,00007h                ; 1517
	add ix,bc                   ; 151A
	ld (ix+000h),001h           ; 151C
	ld (ix+001h),013h           ; 1520
	ld (ix+002h),007h           ; 1524
	ld (ix+003h),001h           ; 1528
	add ix,bc                   ; 152C
	ld (ix+000h),001h           ; 152E
	ld (ix+001h),01ah           ; 1532
	ld (ix+002h),007h           ; 1536
	ld (ix+003h),002h           ; 153A
	add ix,bc                   ; 153E
	ld (ix+000h),001h           ; 1540
	ld (ix+001h),022h           ; 1544
	ld (ix+002h),007h           ; 1548
	ld (ix+003h),003h           ; 154C
	ret                         ; 1550

; ---- title_logo @ 1551
; 6 rows x 40 logical codes (0x2E..0x3C block graphics via title_table), 0xFF terminated.
title_logo:
	defb 2Eh,2Eh,2Eh,2Eh,2Eh,2Eh,2Eh,2Eh,2Eh,2Eh,2Eh,2Eh,2Eh,2Eh,2Eh,2Eh,2Eh,2Eh,2Eh,2Eh,2Eh,2Eh,2Eh,2Eh,2Eh,2Eh,2Eh,2Eh,2Eh,2Eh,2Eh,2Eh,2Eh,2Eh,2Eh,2Eh,2Eh,2Eh,2Eh,2Eh	; row 0
	defb 2Eh,3Ah,33h,33h,34h,38h,33h,33h,34h,3Ah,34h,38h,35h,3Ah,33h,33h,34h,3Ah,33h,33h,2Fh,3Ah,33h,33h,34h,2Eh,2Eh,3Ah,34h,38h,35h,20h,36h,39h,20h,3Ah,34h,20h,35h,2Eh	; row 1
	defb 2Eh,3Ah,3Ch,3Ch,2Fh,3Ah,20h,20h,35h,3Ah,3Ah,35h,35h,3Ah,3Ch,3Ch,2Fh,3Ah,3Ch,3Ch,20h,3Ah,3Ch,3Ch,2Fh,2Eh,2Eh,3Ah,3Ah,35h,35h,3Ah,3Ch,3Ch,35h,3Ah,32h,34h,35h,2Eh	; row 2
	defb 2Eh,3Ah,20h,20h,35h,3Ah,20h,20h,35h,3Ah,20h,20h,35h,3Ah,20h,20h,35h,3Ah,20h,20h,20h,3Ah,20h,39h,20h,2Eh,2Eh,3Ah,20h,20h,35h,3Ah,20h,20h,35h,3Ah,20h,32h,35h,2Eh	; row 3
	defb 2Eh,32h,33h,33h,20h,20h,33h,33h,20h,32h,20h,20h,2Fh,32h,33h,33h,20h,32h,33h,33h,2Fh,32h,20h,20h,2Fh,2Eh,2Eh,32h,20h,20h,2Fh,32h,20h,20h,2Fh,32h,20h,20h,2Fh,2Eh	; row 4
	defb 2Eh,2Eh,2Eh,2Eh,2Eh,2Eh,2Eh,2Eh,2Eh,2Eh,2Eh,2Eh,2Eh,2Eh,2Eh,2Eh,2Eh,2Eh,2Eh,2Eh,2Eh,2Eh,2Eh,2Eh,2Eh,2Eh,2Eh,2Eh,2Eh,2Eh,2Eh,2Eh,2Eh,2Eh,2Eh,2Eh,2Eh,2Eh,2Eh,2Eh	; row 5
	defb 0FFh	; end of logo

str_legend1:
	defb "UP        BONUS   EXIT   BOMB",0

str_box_tl:
	defb 10h,0Ch,11h,0

str_box_ml:
	defb 15h,22h,16h,0

str_legend2:
	defb "LEFT",12h,0Ch,13h,"RIGHT  1 PTS  NO PTS",0

str_box2_t:
	defb 10h,0Ch,11h,"   ",10h,0Ch,11h,0

str_box2_m:
	defb 15h,"$",16h,"   ",15h,"#",16h,0

str_box2_b:
	defb 12h,0Ch,13h,"   ",12h,0Ch,13h," SET BOMB  HI@SCORE ",0

str_box3_t:
	defb 10h,0Ch,11h,"    ",10h,0Ch,0Ch,0Ch,0Ch,0Ch,11h,0

str_box3_m:
	defb 15h,"!",16h,"    ",15h,"SPACE",16h,"      SCORE ",0

str_box3_b:
	defb 12h,0Ch,13h,"    ",12h,0Ch,0Ch,0Ch,0Ch,0Ch,13h,0

str_down:
	defb "DOWN",0

str_push_space:
	defb "PUSH SPACE TO START GAME",0

str_copyright:
	defb "COPYRIGHT ",17h,"C",18h," ",01h,09h,08h,03h,"  HUDSON SOFT INC",19h,0

str_legend_row7:
	defb "BOMBER   ",02h,"00@    ",01h,05h,"0@   ",01h,"00@    ",05h,"0@",0

str_legend_row8:
	defb "MAN      ",01h,06h,"0    ",01h,01h,"0     ",06h,"0     ",01h,"0",0

; ---- time_tick @ 1778
; Every 20 frames: time -= 10; at 0 all bricks and both items vanish (time out).
time_tick:
	ld a,(tmr_time)             ; 1778
	or a                        ; 177B
	ret nz                      ; 177C
	ld hl,(time_left)           ; 177D
	ld a,h                      ; 1780
	or l                        ; 1781
	jr z,.tt_zero               ; 1782
	or a                        ; 1784
	ld bc,0000ah                ; 1785  time -= 10
	sbc hl,bc                   ; 1788
	ld (time_left),hl           ; 178A
	ret                         ; 178D
.tt_zero:
	ld a,(timeout_flag)         ; 178E
	or a                        ; 1791
	ret nz                      ; 1792
	call clear_map              ; 1793  time out: wipe the map
	ld a,001h                   ; 1796
	ld (timeout_flag),a         ; 1798
	xor a                       ; 179B
	ld (exit_present),a         ; 179C
	ld (bonus_present),a        ; 179F
	ld a,001h                   ; 17A2  hide bonus and exit
	ld (bonus_revealed),a       ; 17A4
	ld (exit_revealed),a        ; 17A7
	xor a                       ; 17AA
	ld (bonus_y),a              ; 17AB
	ld (exit_x),a               ; 17AE
	ret                         ; 17B1

; ---- timeout_flag @ 17B2
; 1 once time reached 0 (player killed by time out).
timeout_flag:
	defb 00h

; ---- check_pickups @ 17B3
; Player on EXIT -> exit_taken; player on BONUS -> random 16..142 (x10) points.
check_pickups:
	ld a,(player_y)             ; 17B3
	ld b,a                      ; 17B6
	ld a,(player_x)             ; 17B7
	ld c,a                      ; 17BA
	ld a,(exit_y)               ; 17BB  player on the EXIT tile?
	cp b                        ; 17BE
	jr nz,.cp_bonus             ; 17BF
	ld a,(exit_x)               ; 17C1
	cp c                        ; 17C4
	jr nz,.cp_bonus             ; 17C5
	xor a                       ; 17C7
	ld (exit_present),a         ; 17C8
	ld a,001h                   ; 17CB
	ld (exit_touched),a         ; 17CD
	ret                         ; 17D0
.cp_bonus:
	ld a,(bonus_present)        ; 17D1
	or a                        ; 17D4
	ret z                       ; 17D5
	ld a,(bonus_y)              ; 17D6  player on the BONUS tile?
	cp b                        ; 17D9
	ret nz                      ; 17DA
	ld a,(bonus_x)              ; 17DB
	cp c                        ; 17DE
	ret nz                      ; 17DF
	xor a                       ; 17E0
	ld (bonus_present),a        ; 17E1
	ld hl,00100h                ; 17E4  tone ratio 0x0100
	ld (RATIO),hl               ; 17E7
	ld bc,00030h                ; 17EA
	call beep                   ; 17ED
	call random                 ; 17F0
	and 03fh                    ; 17F3  bonus = (rand & 0x3F)*2 | 0x10 = 16..142
	add a,a                     ; 17F5
	set 4,a                     ; 17F6
	ld c,a                      ; 17F8
	ld b,000h                   ; 17F9
	ld hl,(score)               ; 17FB
	add hl,bc                   ; 17FE
	ld (score),hl               ; 17FF
.cp_ret:
	ret                         ; 1802

; ---- spawn_from_hit @ 1803
; If an explosion hit the bonus/exit tile: once per stage add 4 enemies at that spot.
spawn_from_hit:
	ld a,(hit_pending)          ; 1803
	or a                        ; 1806
	ret z                       ; 1807
	ld a,(hit_spawned)          ; 1808
	or a                        ; 180B
	ret nz                      ; 180C
.sfh_go:
	ld a,(hit_y)                ; 180D
	ld b,a                      ; 1810
	ld a,(hit_x)                ; 1811
	ld c,a                      ; 1814
	call map_addr               ; 1815
	call is_2x2_clear           ; 1818
	cp 020h                     ; 181B
	ret nz                      ; 181D
	ld a,(enemies_left)         ; 181E  four more enemies
	add a,004h                  ; 1821
	ld (enemies_left),a         ; 1823
	ld ix,enemy_table           ; 1826  find free records
	ld d,004h                   ; 182A
.sfh_loop:
	ld a,(ix+000h)              ; 182C
	inc a                       ; 182F
	jr z,.sfh_done              ; 1830
	dec a                       ; 1832
	jr nz,.sfh_next             ; 1833
	ld (ix+000h),001h           ; 1835
	ld a,(hit_x)                ; 1839
	ld (ix+001h),a              ; 183C
	ld a,(hit_y)                ; 183F
	ld (ix+002h),a              ; 1842
	dec d                       ; 1845
	jr z,.sfh_done              ; 1846
.sfh_next:
	ld bc,00007h                ; 1848
	add ix,bc                   ; 184B
	jr .sfh_loop                ; 184D
.sfh_done:
	ld a,001h                   ; 184F
	ld (hit_spawned),a          ; 1851
	ret                         ; 1854

hit_x:
	defb 0Fh

; ---- hit_y @ 1856
; Position where an explosion hit the bonus/exit (enemy spawn point).
hit_y:
	defb 09h

; ---- clear_enemies @ 1857
; Mark all enemy records inactive.
clear_enemies:
	ld ix,enemy_table           ; 1857
.ce_loop:
	ld a,(ix+000h)              ; 185B
	inc a                       ; 185E
	ret z                       ; 185F
	ld (ix+000h),000h           ; 1860
	ld bc,00007h                ; 1864
	add ix,bc                   ; 1867
	jr .ce_loop                 ; 1869

; ---- spawn_enemies @ 186B
; Activate enemies_left enemies at random corner positions from spawn_positions.
spawn_enemies:
	ld a,(enemies_left)         ; 186B
	ld b,a                      ; 186E
	ld ix,enemy_table           ; 186F
.se_loop:
	ld hl,spawn_positions       ; 1873  random corner
	call random                 ; 1876
	and 003h                    ; 1879
	add a,a                     ; 187B
	add a,l                     ; 187C
	ld l,a                      ; 187D
	ld a,(hl)                   ; 187E
	ld (ix+001h),a              ; 187F
	inc hl                      ; 1882
	ld a,(hl)                   ; 1883
	ld (ix+002h),a              ; 1884
	ld (ix+000h),001h           ; 1887
	ld de,00007h                ; 188B
	add ix,de                   ; 188E
	dec b                       ; 1890
	jr nz,.se_loop              ; 1891
	ret                         ; 1893

; ---- spawn_positions @ 1894
; 4 x (X,Y): the four board corners.
spawn_positions:
	defb 01h,01h
	defb 25h,01h
	defb 01h,15h
	defb 25h,15h

; ---- reveal_bonus @ 189C
; When the brick over the BONUS is gone (2x2 clear in both layers) make it visible.
reveal_bonus:
	ld a,(bonus_revealed)       ; 189C
	or a                        ; 189F
	ret nz                      ; 18A0
	ld a,(bonus_y)              ; 18A1
	ld b,a                      ; 18A4
	ld a,(bonus_x)              ; 18A5
	ld c,a                      ; 18A8
	ld d,b                      ; 18A9
	ld e,c                      ; 18AA
	call map_addr               ; 18AB
	call is_2x2_clear           ; 18AE
	cp 020h                     ; 18B1
	ret nz                      ; 18B3
	ld b,d                      ; 18B4
	ld c,e                      ; 18B5
	call draw_addr              ; 18B6
	call is_2x2_clear           ; 18B9
	cp 020h                     ; 18BC
	ret nz                      ; 18BE
	ld a,001h                   ; 18BF
	ld (bonus_present),a        ; 18C1
	ld (bonus_revealed),a       ; 18C4
	ret                         ; 18C7

; ---- draw_bonus @ 18C8
; Draw BONUS tile (0x0A) into the draw buffer; explosion on it -> spawn enemies.
draw_bonus:
	ld a,(bonus_present)        ; 18C8
	or a                        ; 18CB
	ret z                       ; 18CC
	ld a,(bonus_y)              ; 18CD
	ld d,a                      ; 18D0
	ld a,(bonus_x)              ; 18D1
	ld e,a                      ; 18D4
	ld b,d                      ; 18D5
	ld c,e                      ; 18D6
	call draw_addr              ; 18D7
	ld a,00ah                   ; 18DA  tile 0x0A/0x0B/0x1A/0x1B
	call put_bonus_char         ; 18DC
	inc bc                      ; 18DF
	inc a                       ; 18E0
	call put_bonus_char         ; 18E1
	ld hl,00027h                ; 18E4
	add hl,bc                   ; 18E7
	ld b,h                      ; 18E8
	ld c,l                      ; 18E9
	add a,00fh                  ; 18EA
	call put_bonus_char         ; 18EC
	inc bc                      ; 18EF
	inc a                       ; 18F0
	call put_bonus_char         ; 18F1
	ret                         ; 18F4

; ---- put_bonus_char @ 18F5
; Write one char of the bonus tile; if the cell holds fire (>=0xE0) destroy bonus.
put_bonus_char:
	push af                     ; 18F5
	ld a,(bc)                   ; 18F6
	cp 0e0h                     ; 18F7  fire on the tile?
	jr c,.pbc_put               ; 18F9
	xor a                       ; 18FB
	ld (bonus_present),a        ; 18FC
	ld a,(bonus_x)              ; 18FF
	ld (hit_x),a                ; 1902
	ld a,(bonus_y)              ; 1905
	ld (hit_y),a                ; 1908
	ld a,001h                   ; 190B
	ld (hit_pending),a          ; 190D
.pbc_put:
	pop af                      ; 1910
	ld (bc),a                   ; 1911
	ret                         ; 1912

; ---- put_exit_char @ 1913
; Same for the EXIT tile.
put_exit_char:
	push af                     ; 1913
	ld a,(bc)                   ; 1914
	cp 0e0h                     ; 1915
	jr c,.pec_put               ; 1917
	xor a                       ; 1919
	ld (exit_present),a         ; 191A
	ld a,(exit_x)               ; 191D
	ld (hit_x),a                ; 1920
	ld a,(exit_y)               ; 1923
	ld (hit_y),a                ; 1926
	ld a,001h                   ; 1929
	ld (hit_pending),a          ; 192B
.pec_put:
	pop af                      ; 192E
	ld (bc),a                   ; 192F
	ret                         ; 1930

; ---- reveal_exit @ 1931
; When the brick over the EXIT is gone make it visible.
reveal_exit:
	ld a,(exit_revealed)        ; 1931
	or a                        ; 1934
	ret nz                      ; 1935
	ld a,(exit_y)               ; 1936
	ld b,a                      ; 1939
	ld a,(exit_x)               ; 193A
	ld c,a                      ; 193D
	ld d,b                      ; 193E
	ld e,c                      ; 193F
	call map_addr               ; 1940
	call is_2x2_clear           ; 1943
	cp 020h                     ; 1946
	ret nz                      ; 1948
	ld b,d                      ; 1949
	ld c,e                      ; 194A
	call draw_addr              ; 194B
	call is_2x2_clear           ; 194E
	cp 020h                     ; 1951
	ret nz                      ; 1953
	ld a,001h                   ; 1954
	ld (exit_present),a         ; 1956
	ld (exit_revealed),a        ; 1959

; ---- draw_exit @ 195C
; Draw EXIT tile (0x0E) into the draw buffer.
draw_exit:
	ld a,(exit_present)         ; 195C
	or a                        ; 195F
	ret z                       ; 1960
	ld a,(exit_y)               ; 1961
	ld b,a                      ; 1964
	ld a,(exit_x)               ; 1965
	ld c,a                      ; 1968
	call draw_addr              ; 1969
	ld a,00eh                   ; 196C  tile 0x0E/0x0F/0x1E/0x1F
	call put_exit_char          ; 196E
	inc bc                      ; 1971
	inc a                       ; 1972
	call put_exit_char          ; 1973
	ld hl,00027h                ; 1976
	add hl,bc                   ; 1979
	ld b,h                      ; 197A
	ld c,l                      ; 197B
	add a,00fh                  ; 197C
	call put_exit_char          ; 197E
	inc bc                      ; 1981
	inc a                       ; 1982
	call put_exit_char          ; 1983
	ret                         ; 1986

; ---- is_2x2_clear @ 1987
; A=0x20 if the 2x2 cell at draw-buffer address BC is all spaces, else the blocking char.
is_2x2_clear:
	ld a,(bc)                   ; 1987
	cp 020h                     ; 1988
	ret nz                      ; 198A
	inc bc                      ; 198B
	ld a,(bc)                   ; 198C
	cp 020h                     ; 198D
	ret nz                      ; 198F
	ld hl,00027h                ; 1990
	add hl,bc                   ; 1993
	ld b,h                      ; 1994
	ld c,l                      ; 1995
	ld a,(bc)                   ; 1996
	cp 020h                     ; 1997
	ret nz                      ; 1999
	inc bc                      ; 199A
	ld a,(bc)                   ; 199B
	ret                         ; 199C

; ---- tick_timer @ 199D
; HL -> [counter,period]: counter++ ; wraps to 0 when reaching period.
tick_timer:
	ld a,(hl)                   ; 199D
	inc a                       ; 199E  counter+1
	inc hl                      ; 199F
	cp (hl)                     ; 19A0  compare with period
	jr c,.tt_store              ; 19A1
	xor a                       ; 19A3
.tt_store:
	dec hl                      ; 19A4
	ld (hl),a                   ; 19A5
	ret                         ; 19A6

; ---- tick_timers @ 19A7
; Advance all frame timers, then flush the frame to VRAM.
tick_timers:
	ld hl,tmr_player_anim       ; 19A7
	call tick_timer             ; 19AA
	ld hl,tmr_enemy_move        ; 19AD
	call tick_timer             ; 19B0
	ld hl,tmr_unused            ; 19B3
	call tick_timer             ; 19B6
	ld hl,tmr_time              ; 19B9
	call tick_timer             ; 19BC
	call flush_screen           ; 19BF  present the frame
	ret                         ; 19C2

; ---- enemy_ai @ 19C3
; Move enemies: countdown -> cycle type 3..0; type 0 chases player every frame.
enemy_ai:
	ld a,(tmr_enemy_move)       ; 19C3
	or a                        ; 19C6
	ret nz                      ; 19C7
	ld a,(enemy_anim)           ; 19C8  toggle enemy anim frame
	xor 002h                    ; 19CB
	ld (enemy_anim),a           ; 19CD
	ld ix,enemy_table           ; 19D0
.ea_loop:
	ld a,(ix+000h)              ; 19D4
	inc a                       ; 19D7
	ret z                       ; 19D8
	dec a                       ; 19D9
	cp 001h                     ; 19DA  only living enemies move
	jp nz,.ea_next              ; 19DC
	ld a,(ix+004h)              ; 19DF  countdown--
	dec a                       ; 19E2
	ld d,a                      ; 19E3
	cp 0ffh                     ; 19E4
	jr nz,.ea_setcnt            ; 19E6
	ld a,(ix+003h)              ; 19E8  expired: type-- (3..0 cycle)
	dec a                       ; 19EB
	cp 0ffh                     ; 19EC
	jr nz,.ea_settype           ; 19EE
	ld a,003h                   ; 19F0
.ea_settype:
	ld (ix+003h),a              ; 19F2
	ld a,(enemy_period)         ; 19F5  reload countdown from stage param
	ld d,a                      ; 19F8
.ea_setcnt:
	ld a,d                      ; 19F9
	ld (ix+004h),a              ; 19FA
	ld a,(ix+003h)              ; 19FD
	or a                        ; 1A00
	jr z,.ea_move               ; 1A01
	ld a,(tmr_enemy_move)       ; 1A03  types 1..3 move every 4th tick
	and 003h                    ; 1A06
	jp nz,.ea_next              ; 1A08
.ea_move:
	ld a,(ix+003h)              ; 1A0B  type 0 -> chase
	or a                        ; 1A0E
	jr z,.ea_chase              ; 1A0F
	ld a,(ix+004h)              ; 1A11  low nibble of countdown
	and 00fh                    ; 1A14
	or a                        ; 1A16
	jr z,.ea_chase              ; 1A17
	and 003h                    ; 1A19  every 4th count pick a new direction
	or a                        ; 1A1B
	jr z,.ea_random             ; 1A1C
	call enemy_probe            ; 1A1E  blocked ahead?
	cp 080h                     ; 1A21
	jr nc,.ea_newdir            ; 1A23
	call enemy_step             ; 1A25
	jr .ea_next                 ; 1A28
.ea_newdir:
	call random                 ; 1A2A
	and 003h                    ; 1A2D
	ld (ix+005h),a              ; 1A2F
	call enemy_probe            ; 1A32
	cp 080h                     ; 1A35
	jr nc,.ea_next              ; 1A37
	call enemy_step             ; 1A39
	jr .ea_next                 ; 1A3C
.ea_random:
	call random                 ; 1A3E
	and 003h                    ; 1A41
	ld (ix+005h),a              ; 1A43
	call enemy_probe            ; 1A46
	cp 080h                     ; 1A49
	jr nc,.ea_next              ; 1A4B
	call enemy_step             ; 1A4D
	jr .ea_next                 ; 1A50
.ea_chase:
	ld a,(player_x)             ; 1A52  chase: align X first
	ld b,a                      ; 1A55
	ld a,(ix+001h)              ; 1A56
	cp b                        ; 1A59
	jr z,.ea_chase_y            ; 1A5A
	jr nc,.ea_left              ; 1A5C
	ld (ix+005h),001h           ; 1A5E  dir 1 = right
	jr .ea_tryx                 ; 1A62
.ea_left:
	ld (ix+005h),000h           ; 1A64  dir 0 = left
.ea_tryx:
	call enemy_probe            ; 1A68
	cp 080h                     ; 1A6B
	jr c,.ea_step               ; 1A6D
.ea_chase_y:
	ld a,(player_y)             ; 1A6F
	ld b,a                      ; 1A72
	ld a,(ix+002h)              ; 1A73
	cp b                        ; 1A76
	jr z,.ea_next               ; 1A77
	jr nc,.ea_up                ; 1A79
	ld (ix+005h),003h           ; 1A7B  dir 3 = down
	jr .ea_tryy                 ; 1A7F
.ea_up:
	ld (ix+005h),002h           ; 1A81  dir 2 = up
.ea_tryy:
	call enemy_probe            ; 1A85
	cp 080h                     ; 1A88
	jp nc,.ea_next              ; 1A8A
.ea_step:
	call enemy_step             ; 1A8D
.ea_next:
	ld bc,00007h                ; 1A90
	add ix,bc                   ; 1A93
	jp .ea_loop                 ; 1A95

; ---- enemy_step @ 1A98
; Move enemy IX one char in direction (IX+5) using dir_deltas.
enemy_step:
	ld a,(ix+005h)              ; 1A98
	add a,a                     ; 1A9B
	ld hl,dir_deltas            ; 1A9C
	ld b,000h                   ; 1A9F
	ld c,a                      ; 1AA1
	add hl,bc                   ; 1AA2
	ld a,(ix+002h)              ; 1AA3
	add a,(hl)                  ; 1AA6
	ld (ix+002h),a              ; 1AA7
	inc hl                      ; 1AAA
	ld a,(ix+001h)              ; 1AAB
	add a,(hl)                  ; 1AAE
	ld (ix+001h),a              ; 1AAF
	ret                         ; 1AB2

; ---- enemy_probe @ 1AB3
; A>=0x80 if the two chars ahead in direction (IX+5) are blocked (draw or map layer).
enemy_probe:
	ld hl,probe_offsets         ; 1AB3  table entry = dir*4
	ld a,(ix+005h)              ; 1AB6
	add a,a                     ; 1AB9
	add a,a                     ; 1ABA
	ld c,a                      ; 1ABB
	ld b,000h                   ; 1ABC
	add hl,bc                   ; 1ABE
	ld a,(ix+002h)              ; 1ABF
	add a,(hl)                  ; 1AC2
	ld b,a                      ; 1AC3
	inc hl                      ; 1AC4
	ld a,(ix+001h)              ; 1AC5
	add a,(hl)                  ; 1AC8
	ld c,a                      ; 1AC9
	push bc                     ; 1ACA
	call draw_addr              ; 1ACB
	ld a,(bc)                   ; 1ACE
	pop bc                      ; 1ACF
	cp 080h                     ; 1AD0  blocked in draw buffer
	ret nc                      ; 1AD2
	call map_addr               ; 1AD3
	ld a,(bc)                   ; 1AD6
	cp 080h                     ; 1AD7  blocked in map layer
	ret nc                      ; 1AD9
	inc hl                      ; 1ADA
	ld a,(ix+002h)              ; 1ADB
	add a,(hl)                  ; 1ADE
	ld b,a                      ; 1ADF
	inc hl                      ; 1AE0
	ld a,(ix+001h)              ; 1AE1
	add a,(hl)                  ; 1AE4
	ld c,a                      ; 1AE5
	push bc                     ; 1AE6
	call draw_addr              ; 1AE7
	ld a,(bc)                   ; 1AEA
	pop bc                      ; 1AEB
	cp 080h                     ; 1AEC
	ret nc                      ; 1AEE
	call map_addr               ; 1AEF
	ld a,(bc)                   ; 1AF2
	ret                         ; 1AF3

; ---- probe_offsets @ 1AF4
; Per direction (left,right,up,down): dY1,dX1,dY2,dX2 = the two chars ahead of the 2x2 sprite.
probe_offsets:
	defb 00h,0FFh,01h,0FFh	; 0: left
	defb 00h,02h,01h,02h	; 1: right
	defb 0FFh,00h,0FFh,01h	; 2: up
	defb 02h,00h,02h,01h	; 3: down

; ---- dir_deltas @ 1B04
; Per direction (left,right,up,down): dY,dX.
dir_deltas:
	defb 00h,0FFh	; 0: left
	defb 00h,01h	; 1: right
	defb 0FFh,00h	; 2: up
	defb 01h,00h	; 3: down

; ---- draw_enemies @ 1B0C
; Draw enemies; handle death animation (states 2..9), award points, count kills.
draw_enemies:
	ld ix,enemy_table           ; 1B0C
.de_loop:
	ld a,(ix+000h)              ; 1B10
	inc a                       ; 1B13
	ret z                       ; 1B14
	dec a                       ; 1B15
	jp z,.de_next               ; 1B16  state 0: free
	cp 001h                     ; 1B19  state 1: alive
	jp z,.de_alive              ; 1B1B
	ld a,(ix+001h)              ; 1B1E
	ld c,a                      ; 1B21
	ld a,(ix+002h)              ; 1B22
	ld b,a                      ; 1B25
	call draw_addr              ; 1B26
	ld a,(ix+000h)              ; 1B29
	add a,a                     ; 1B2C  dying: tile = state*2 + 0x1E (0x22..0x30)
	add a,01eh                  ; 1B2D
	cp 030h                     ; 1B2F  past the last frame -> blank
	jr c,.de_dying              ; 1B31
	ld a,020h                   ; 1B33
	call fill_2x2               ; 1B35
	jr .de_dying2               ; 1B38
.de_dying:
	call put_tile               ; 1B3A
.de_dying2:
	push hl                     ; 1B3D
	ld hl,tmr_enemy_die         ; 1B3E  death animation timer
	call tick_timer             ; 1B41
	pop hl                      ; 1B44
	ld a,(tmr_enemy_die)        ; 1B45
	or a                        ; 1B48
	jp nz,.de_next              ; 1B49
	ld a,(ix+000h)              ; 1B4C
	inc a                       ; 1B4F
	cp 00ah                     ; 1B50  state 10 -> record freed
	jr z,.de_killed             ; 1B52
	ld (ix+000h),a              ; 1B54
	ld h,a                      ; 1B57
	ld l,032h                   ; 1B58  tone ratio state<<8 | 0x32
	ld (RATIO),hl               ; 1B5A
	ld bc,0000ah                ; 1B5D
	call beep                   ; 1B60
	jr .de_next                 ; 1B63
.de_killed:
	xor a                       ; 1B65
	ld (ix+000h),a              ; 1B66
	ld a,(ix+003h)              ; 1B69  points = type*4+2 + rand(1..4)
	add a,a                     ; 1B6C
	add a,a                     ; 1B6D
	inc a                       ; 1B6E
	inc a                       ; 1B6F
	ld b,a                      ; 1B70
	call random                 ; 1B71
	and 003h                    ; 1B74
	inc a                       ; 1B76
	add a,b                     ; 1B77
	ld c,a                      ; 1B78
	ld b,000h                   ; 1B79
	ld hl,(score)               ; 1B7B
	add hl,bc                   ; 1B7E
	ld (score),hl               ; 1B7F
	ld a,(player_state)         ; 1B82  player alive?
	cp 006h                     ; 1B85
	jr nc,.de_next              ; 1B87
	ld a,(enemies_left)         ; 1B89  enemies_left--
	dec a                       ; 1B8C
	ld (enemies_left),a         ; 1B8D
	jr nz,.de_next              ; 1B90
	ld a,(stage_cleared)        ; 1B92  last one: stage cleared
	inc a                       ; 1B95
	ld (stage_cleared),a        ; 1B96
	jr .de_next                 ; 1B99
.de_alive:
	ld a,(ix+001h)              ; 1B9B
	ld c,a                      ; 1B9E
	ld a,(ix+002h)              ; 1B9F
	ld b,a                      ; 1BA2
	call draw_addr              ; 1BA3
	ld a,(ix+003h)              ; 1BA6  tile = type*4 + 0xC0 + anim
	add a,a                     ; 1BA9
	add a,a                     ; 1BAA
	add a,0c0h                  ; 1BAB
	ld hl,enemy_anim            ; 1BAD
	add a,(hl)                  ; 1BB0
	call put_enemy_char         ; 1BB1
	inc a                       ; 1BB4
	inc bc                      ; 1BB5
	call put_enemy_char         ; 1BB6
	ld hl,00027h                ; 1BB9
	add hl,bc                   ; 1BBC
	ld b,h                      ; 1BBD
	ld c,l                      ; 1BBE
	add a,00fh                  ; 1BBF
	call put_enemy_char         ; 1BC1
	inc bc                      ; 1BC4
	inc a                       ; 1BC5
	call put_enemy_char         ; 1BC6
.de_next:
	ld bc,00007h                ; 1BC9
	add ix,bc                   ; 1BCC
	jp .de_loop                 ; 1BCE

; ---- put_enemy_char @ 1BD1
; Write one enemy char; if the cell holds fire (>=0xE0) start dying (state 2).
put_enemy_char:
	push af                     ; 1BD1
	ld a,(bc)                   ; 1BD2
	cp 0e0h                     ; 1BD3
	jr c,.pen_put               ; 1BD5
	ld (ix+000h),002h           ; 1BD7
.pen_put:
	pop af                      ; 1BDB
	ld (bc),a                   ; 1BDC
	ret                         ; 1BDD

; ---- enemy_table @ 1BDE
; 7-byte records: state(0 free,1 alive,2..9 dying), X, Y, type 0..3, countdown, dir, unused. 0xFF ends.
enemy_table:
	defb 01h,0Bh,07h,00h,04h,01h,00h	; enemy 0
	defb 01h,13h,07h,01h,07h,03h,00h	; enemy 1
	defb 01h,1Ah,07h,02h,00h,01h,00h	; enemy 2
	defb 01h,22h,07h,03h,03h,00h,00h	; enemy 3
	defb 00h,15h,11h,00h,08h,03h,00h	; enemy 4
	defb 00h,00h,00h,01h,09h,00h,00h	; enemy 5
	defb 00h,00h,00h,03h,05h,00h,00h	; enemy 6
	defb 00h,00h,00h,02h,03h,00h,00h	; enemy 7
	defb 0FFh,0FFh,0FFh,0FFh	; terminator (4 x 0xFF)

; ---- enemy_anim @ 1C1A
; Enemy animation frame offset (0/2).
enemy_anim:
	defb 00h

; ---- place_bomb @ 1C1B
; SPACE pressed and player alive: put a bomb (tile 0x60) in a free 2x2 map cell.
place_bomb:
	ld a,(player_state)         ; 1C1B  player alive?
	cp 006h                     ; 1C1E
	ret nc                      ; 1C20
	call GETKY                  ; 1C21  SPACE?
	cp 020h                     ; 1C24
	ret nz                      ; 1C26
	ld ix,bomb_table            ; 1C27
.pb_loop:
	ld a,(ix+000h)              ; 1C2B  find a free bomb record
	inc a                       ; 1C2E
	ret z                       ; 1C2F
	dec a                       ; 1C30
	or a                        ; 1C31
	jr nz,.pb_next              ; 1C32
	ld a,(player_y)             ; 1C34  bomb at player position
	ld b,a                      ; 1C37
	ld (ix+002h),a              ; 1C38
	ld a,(player_x)             ; 1C3B
	ld (ix+001h),a              ; 1C3E
	ld c,a                      ; 1C41
	call map_addr               ; 1C42
	ld d,b                      ; 1C45
	ld e,c                      ; 1C46
	call is_space               ; 1C47  2x2 must be empty in the map layer
	ret nz                      ; 1C4A
	inc bc                      ; 1C4B
	call is_space               ; 1C4C
	ret nz                      ; 1C4F
	ld hl,00027h                ; 1C50
	add hl,bc                   ; 1C53
	ld b,h                      ; 1C54
	ld c,l                      ; 1C55
	call is_space               ; 1C56
	ret nz                      ; 1C59
	inc bc                      ; 1C5A
	call is_space               ; 1C5B
	ret nz                      ; 1C5E
	ld b,d                      ; 1C5F
	ld c,e                      ; 1C60
	ld a,060h                   ; 1C61  bomb tile 0x60
	call put_tile               ; 1C63
	ld a,001h                   ; 1C66
	ld (ix+000h),a              ; 1C68
	xor a                       ; 1C6B  timer=0, player_state=0 (restart standing anim)
	ld (ix+003h),a              ; 1C6C
	ld (player_state),a         ; 1C6F
	ret                         ; 1C72
.pb_next:
	ld bc,00004h                ; 1C73
	add ix,bc                   ; 1C76
	jp .pb_loop                 ; 1C78

; ---- is_space @ 1C7B
; Z if map char at BC is a space.
is_space:
	ld a,(bc)                   ; 1C7B
	cp 020h                     ; 1C7C
	ret                         ; 1C7E

; ---- bomb_table @ 1C7F
; 4-byte records: state(0 free,1..4 ticking,5..13 exploding), X, Y, timer. 0xFF ends.
bomb_table:
	defb 01h,20h,0Bh,00h	; bomb 0
	defb 00h,10h,05h,00h	; bomb 1
	defb 00h,0Dh,09h,00h	; bomb 2
	defb 00h,09h,0Dh,00h	; bomb 3
	defb 00h,21h,10h,00h	; bomb 4
	defb 0FFh	; terminator

; ---- bomb_anim @ 1C94
; Bomb animation frame offset (0/2).
bomb_anim:
	defb 02h

; ---- clear_bombs @ 1C95
; Mark all bomb records free.
clear_bombs:
	ld ix,bomb_table            ; 1C95
.cb_loop:
	ld a,(ix+000h)              ; 1C99
	inc a                       ; 1C9C
	ret z                       ; 1C9D
	ld (ix+000h),000h           ; 1C9E
	ld bc,00004h                ; 1CA2
	add ix,bc                   ; 1CA5
	jr .cb_loop                 ; 1CA7

; ---- update_bombs @ 1CA9
; Advance bomb states: 1..4 tick (7 frames each), 5..13 explosion phases, 14 -> free.
update_bombs:
	ld a,(bomb_anim)            ; 1CA9
	xor 002h                    ; 1CAC
	ld (bomb_anim),a            ; 1CAE
	ld ix,bomb_table            ; 1CB1
.ub_loop:
	ld a,(ix+000h)              ; 1CB5
	inc a                       ; 1CB8
	ret z                       ; 1CB9
	dec a                       ; 1CBA
	jr z,.ub_next               ; 1CBB
	cp 005h                     ; 1CBD  states >= 5 (exploding) advance every frame
	jr nc,.ub_advance           ; 1CBF
	ld a,(ix+003h)              ; 1CC1  ticking: every 7 frames
	inc a                       ; 1CC4
	ld (ix+003h),a              ; 1CC5
	cp 007h                     ; 1CC8
	jr nz,.ub_next              ; 1CCA
.ub_advance:
	xor a                       ; 1CCC
	ld (ix+003h),a              ; 1CCD
	ld a,(ix+000h)              ; 1CD0
	inc a                       ; 1CD3
	cp 00eh                     ; 1CD4  state 14 -> free
	jr z,.ub_free               ; 1CD6
	ld (ix+000h),a              ; 1CD8
	cp 005h                     ; 1CDB  entering state 5: explosion sound
	jr c,.ub_next               ; 1CDD
	ld a,(player_state)         ; 1CDF
	cp 006h                     ; 1CE2
	jr nc,.ub_next              ; 1CE4
	ld a,(ix+000h)              ; 1CE6
	ld h,a                      ; 1CE9  tone ratio state<<8 | 0x0A
	ld l,00ah                   ; 1CEA
	ld (RATIO),hl               ; 1CEC
	ld bc,0000ch                ; 1CEF
	call beep                   ; 1CF2
	jr .ub_next                 ; 1CF5
.ub_free:
	xor a                       ; 1CF7
	ld (ix+000h),a              ; 1CF8
.ub_next:
	ld bc,00004h                ; 1CFB
	add ix,bc                   ; 1CFE
	jr .ub_loop                 ; 1D00

; ---- blast_pattern @ 1D02
; 4 arms x 8 (dY,dX) pairs: left, right, up, down; each arm = 4 chars top row then 4 chars bottom row. 0x80 ends.
blast_pattern:
	defb 00h,0FFh,00h,0FEh,00h,0FDh,00h,0FCh,01h,0FFh,01h,0FEh,01h,0FDh,01h,0FCh	; left
	defb 00h,02h,00h,03h,00h,04h,00h,05h,01h,02h,01h,03h,01h,04h,01h,05h	; right
	defb 0FFh,00h,0FEh,00h,0FDh,00h,0FCh,00h,0FFh,01h,0FEh,01h,0FDh,01h,0FCh,01h	; up
	defb 02h,00h,03h,00h,04h,00h,05h,00h,02h,01h,03h,01h,04h,01h,05h,01h	; down
	defb 80h,80h,80h,80h	; end (only the first 0x80 is needed)

; ---- draw_bombs @ 1D46
; Draw ticking bombs (0x60..0x6C) and explosions (center 0xE0.., arms via blast_pattern).
draw_bombs:
	ld ix,bomb_table            ; 1D46
.db_loop:
	ld a,(ix+000h)              ; 1D4A  state 0: skip
	inc a                       ; 1D4D
	ret z                       ; 1D4E
	dec a                       ; 1D4F
	jr z,.db_next               ; 1D50
	cp 00dh                     ; 1D52  state 13: erase
	jr z,.db_erase              ; 1D54
	cp 005h                     ; 1D56  states 5..12: explode
	jr c,.db_ticking            ; 1D58
	add a,a                     ; 1D5A  center tile = state*2 + 0xD6 (0xE0..0xEE)
	add a,0d6h                  ; 1D5B
	exx                         ; 1D5D
	ld d,a                      ; 1D5E
	exx                         ; 1D5F
	jr .db_explode              ; 1D60
.db_ticking:
	dec a                       ; 1D62  ticking: tile = (state-1)*4 + 0x60 + anim
	add a,a                     ; 1D63
	add a,a                     ; 1D64
	ld d,a                      ; 1D65
	ld a,(bomb_anim)            ; 1D66
	add a,d                     ; 1D69
	add a,060h                  ; 1D6A
	ld d,a                      ; 1D6C
	exx                         ; 1D6D
	ld d,a                      ; 1D6E
	exx                         ; 1D6F
	ld a,(ix+001h)              ; 1D70
	ld c,a                      ; 1D73
	exx                         ; 1D74
	ld c,a                      ; 1D75
	exx                         ; 1D76
	ld a,(ix+002h)              ; 1D77
	ld b,a                      ; 1D7A
	exx                         ; 1D7B
	ld b,a                      ; 1D7C
	exx                         ; 1D7D
	call map_addr               ; 1D7E
	call put_bomb_char          ; 1D81
	inc bc                      ; 1D84
	inc d                       ; 1D85
	call put_bomb_char          ; 1D86
	ld hl,00027h                ; 1D89
	add hl,bc                   ; 1D8C
	ld b,h                      ; 1D8D
	ld c,l                      ; 1D8E
	ld a,00fh                   ; 1D8F
	add a,d                     ; 1D91
	ld d,a                      ; 1D92
	call put_bomb_char          ; 1D93
	inc bc                      ; 1D96
	inc d                       ; 1D97
	call put_bomb_char          ; 1D98
	exx                         ; 1D9B
	call draw_addr              ; 1D9C
	ld a,d                      ; 1D9F
	call put_tile               ; 1DA0
	exx                         ; 1DA3
.db_next:
	ld bc,00004h                ; 1DA4
	add ix,bc                   ; 1DA7
	jr .db_loop                 ; 1DA9

; ---- put_bomb_char @ 1DAB
; Write one bomb char; if the cell holds fire -> chain: state 4, timer 6.
put_bomb_char:
	ld a,(bc)                   ; 1DAB
	cp 0e0h                     ; 1DAC
	ret c                       ; 1DAE
	ld (ix+000h),004h           ; 1DAF  chain reaction: explode next frame
	ld (ix+003h),006h           ; 1DB3
	ret                         ; 1DB7
.db_erase:
	ld a,(ix+001h)              ; 1DB8
	ld c,a                      ; 1DBB
	ld a,(ix+002h)              ; 1DBC
	ld b,a                      ; 1DBF
	call map_addr               ; 1DC0
	ld a,020h                   ; 1DC3  remove bomb from map layer
	call fill_2x2               ; 1DC5
	jr .db_next                 ; 1DC8
.db_explode:
	ld a,(ix+001h)              ; 1DCA
	ld c,a                      ; 1DCD
	ld a,(ix+002h)              ; 1DCE
	ld b,a                      ; 1DD1
	call map_addr               ; 1DD2
	exx                         ; 1DD5
	ld a,d                      ; 1DD6
	exx                         ; 1DD7
	call put_tile               ; 1DD8  center 2x2
	exx                         ; 1DDB
	ld a,d                      ; 1DDC
	exx                         ; 1DDD
	inc a                       ; 1DDE  arm fire code E' = center+2, or space in the last phase
	inc a                       ; 1DDF
	cp 0f0h                     ; 1DE0
	jr c,.db_arms               ; 1DE2
	ld a,020h                   ; 1DE4
.db_arms:
	exx                         ; 1DE6
	ld e,a                      ; 1DE7
	exx                         ; 1DE8
	ld hl,blast_pattern         ; 1DE9
.db_arm_loop:
	ld a,(hl)                   ; 1DEC  0x80 ends the pattern (0x00 is a valid offset)
	or a                        ; 1DED
	jr z,.db_arm_first          ; 1DEE
	add a,a                     ; 1DF0
	or a                        ; 1DF1
	jr z,.db_next               ; 1DF2
.db_arm_first:
	ld a,(hl)                   ; 1DF4
	inc hl                      ; 1DF5
	add a,(ix+002h)             ; 1DF6
	ld b,a                      ; 1DF9
	ld a,(hl)                   ; 1DFA
	inc hl                      ; 1DFB
	add a,(ix+001h)             ; 1DFC
	ld c,a                      ; 1DFF
	call blast_cell_near        ; 1E00
	cp 088h                     ; 1E03  solid wall: skip the remaining 3 chars of this row
	jr nz,.db_arm_second        ; 1E05
	jr .db_arm_skip3            ; 1E07

; ---- dead_code_1 @ 1E09
; Unreachable leftover: cp 89h / jr nz,+8.
dead_code_1:
	defb 0FEh,89h,20h,08h
.db_arm_skip3:
	inc hl                      ; 1E0D
	inc hl                      ; 1E0E
	inc hl                      ; 1E0F
	inc hl                      ; 1E10
	inc hl                      ; 1E11
	inc hl                      ; 1E12
	jr .db_arm_loop             ; 1E13
.db_arm_second:
	ld a,(hl)                   ; 1E15
	inc hl                      ; 1E16
	add a,(ix+002h)             ; 1E17
	ld b,a                      ; 1E1A
	ld a,(hl)                   ; 1E1B
	inc hl                      ; 1E1C
	add a,(ix+001h)             ; 1E1D
	ld c,a                      ; 1E20
	call blast_cell_near        ; 1E21
	cp 080h                     ; 1E24  brick/wall: skip the remaining 2 chars
	jr c,.db_arm_third          ; 1E26
	cp 08ah                     ; 1E28
	jr nc,.db_arm_third         ; 1E2A
	inc hl                      ; 1E2C
	inc hl                      ; 1E2D
	inc hl                      ; 1E2E
	inc hl                      ; 1E2F
	jr .db_arm_loop             ; 1E30
.db_arm_third:
	ld a,(hl)                   ; 1E32
	inc hl                      ; 1E33
	add a,(ix+002h)             ; 1E34
	ld b,a                      ; 1E37
	ld a,(hl)                   ; 1E38
	inc hl                      ; 1E39
	add a,(ix+001h)             ; 1E3A
	ld c,a                      ; 1E3D
	call blast_cell_far         ; 1E3E
	ex af,af'                   ; 1E41
	cp 080h                     ; 1E42  brick: skip the last char
	jr c,.db_arm_fourth         ; 1E44
	cp 08ah                     ; 1E46
	jr nc,.db_arm_fourth        ; 1E48
	ex af,af'                   ; 1E4A
	inc hl                      ; 1E4B
	inc hl                      ; 1E4C
	jr .db_arm_loop             ; 1E4D
.db_arm_fourth:
	ex af,af'                   ; 1E4F
	ld a,(hl)                   ; 1E50
	inc hl                      ; 1E51
	add a,(ix+002h)             ; 1E52
	ld b,a                      ; 1E55
	ld a,(hl)                   ; 1E56
	inc hl                      ; 1E57
	add a,(ix+001h)             ; 1E58
	ld c,a                      ; 1E5B
	call blast_cell_far         ; 1E5C
	jr .db_arm_loop             ; 1E5F

; ---- blast_cell_near @ 1E61
; Blast one char (Y=B,X=C): walls stop; bricks 0x80..0x87 burn one step; else write fire E'.
blast_cell_near:
	push bc                     ; 1E61
	call draw_addr              ; 1E62
	ld a,(bc)                   ; 1E65  draw buffer: walls/pillars stop the blast
	pop bc                      ; 1E66
	cp 088h                     ; 1E67
	ret z                       ; 1E69
	cp 089h                     ; 1E6A
	ret z                       ; 1E6C
	call map_addr               ; 1E6D
	ld a,(bc)                   ; 1E70  map layer
	cp 088h                     ; 1E71
	ret z                       ; 1E73
	cp 080h                     ; 1E74  0x80..0x89 = brick (or bomb/wall)
	jr c,.bcn_fire              ; 1E76
	cp 08ah                     ; 1E78
	jr nc,.bcn_fire             ; 1E7A
	jr .bcn_burn                ; 1E7C
.bcn_fire:
	exx                         ; 1E7E
	ld a,e                      ; 1E7F
	exx                         ; 1E80
	jr .bcn_put                 ; 1E81
.bcn_burn:
	inc a                       ; 1E83  burn brick one step, 0x88 -> removed
	cp 088h                     ; 1E84
	jr nz,.bcn_put              ; 1E86
	ld a,020h                   ; 1E88
.bcn_put:
	ld (bc),a                   ; 1E8A
	ret                         ; 1E8B

; ---- blast_cell_far @ 1E8C
; Blast one char beyond a brick check: bricks stop it, empty gets fire E'.
blast_cell_far:
	push bc                     ; 1E8C
	call draw_addr              ; 1E8D
	ld a,(bc)                   ; 1E90
	ex af,af'                   ; 1E91
	ld a,(bc)                   ; 1E92
	ex af,af'                   ; 1E93
	pop bc                      ; 1E94
	cp 088h                     ; 1E95
	ret z                       ; 1E97
	cp 089h                     ; 1E98
	ret z                       ; 1E9A
	call map_addr               ; 1E9B
	ld a,(bc)                   ; 1E9E
	cp 08ah                     ; 1E9F
	jr nc,.bcf_fire             ; 1EA1
	cp 080h                     ; 1EA3
	jr c,.bcf_fire              ; 1EA5
	ret                         ; 1EA7
.bcf_fire:
	exx                         ; 1EA8
	ld a,e                      ; 1EA9
	exx                         ; 1EAA
	ld (bc),a                   ; 1EAB
	ret                         ; 1EAC

; ---- put_tile @ 1EAD
; Write 2x2 tile A,A+1 / A+16,A+17 at draw/map address BC (16-wide tile sheet).
put_tile:
	ld (bc),a                   ; 1EAD
	inc bc                      ; 1EAE
	inc a                       ; 1EAF
	ld (bc),a                   ; 1EB0
	ld hl,00027h                ; 1EB1
	add hl,bc                   ; 1EB4
	ld b,h                      ; 1EB5
	ld c,l                      ; 1EB6
	add a,00fh                  ; 1EB7
	ld (bc),a                   ; 1EB9
	inc bc                      ; 1EBA
	inc a                       ; 1EBB
	ld (bc),a                   ; 1EBC
	ret                         ; 1EBD

; ---- fill_2x2 @ 1EBE
; Write char A into all four chars of the 2x2 cell at BC.
fill_2x2:
	ld (bc),a                   ; 1EBE
	inc bc                      ; 1EBF
	ld (bc),a                   ; 1EC0
	ld hl,00027h                ; 1EC1
	add hl,bc                   ; 1EC4
	ld b,h                      ; 1EC5
	ld c,l                      ; 1EC6
	ld (bc),a                   ; 1EC7
	inc bc                      ; 1EC8
	ld (bc),a                   ; 1EC9
	ret                         ; 1ECA

; ---- tmr_player_anim @ 1ECB
; Pairs [counter,period]. player anim, explosion step, enemy death anim, enemy move, (unused), time tick.
tmr_player_anim:
	defb 00h,02h	; counter, period
tmr_explode:
	defb 00h,04h	; counter, period
tmr_enemy_die:
	defb 00h,04h	; counter, period
tmr_enemy_move:
	defb 00h,02h	; counter, period
tmr_unused:
	defb 02h,05h	; counter, period
tmr_time:
	defb 02h,14h	; counter, period

; ---- draw_player @ 1ED7
; Draw player: state 0/1 standing, 2..5 walking (unused), 6..13 dying.
draw_player:
	ld a,(player_state)         ; 1ED7
	cp 006h                     ; 1EDA  dying?
	jr nc,.dp_dying             ; 1EDC
	cp 002h                     ; 1EDE  walking?
	jr nc,.dp_walk              ; 1EE0
	or a                        ; 1EE2  state 0 -> 1, tile 0x8A
	jr nz,.dp_stand             ; 1EE3
	inc a                       ; 1EE5
	ld (player_state),a         ; 1EE6
	ld e,08ah                   ; 1EE9
	jr .dp_put                  ; 1EEB
.dp_stand:
	ld e,08ch                   ; 1EED  tile 0x8C
	jr .dp_put                  ; 1EEF
.dp_walk:
	dec a                       ; 1EF1  tile = (state-2)*4 + 0xA0 + anim
	dec a                       ; 1EF2
	add a,a                     ; 1EF3
	add a,a                     ; 1EF4
	add a,0a0h                  ; 1EF5
	ld e,a                      ; 1EF7
	ld a,(player_anim_frame)    ; 1EF8
	add a,e                     ; 1EFB
	ld e,a                      ; 1EFC
	jr .dp_put                  ; 1EFD
.dp_dying:
	sub 006h                    ; 1EFF  tile = 0x4E - (state-6)*2
	add a,a                     ; 1F01
	ld e,a                      ; 1F02
	ld a,04eh                   ; 1F03
	sub e                       ; 1F05
	ld e,a                      ; 1F06
.dp_put:
	ld a,(player_y)             ; 1F07
	ld b,a                      ; 1F0A
	ld a,(player_x)             ; 1F0B
	ld c,a                      ; 1F0E
	call draw_addr              ; 1F0F
	call put_player_char        ; 1F12
	call put_player_char        ; 1F15
	ld hl,00026h                ; 1F18
	add hl,bc                   ; 1F1B
	ld b,h                      ; 1F1C
	ld c,l                      ; 1F1D
	ld a,e                      ; 1F1E
	add a,00eh                  ; 1F1F
	ld e,a                      ; 1F21
	call put_player_char        ; 1F22
	call put_player_char        ; 1F25
	ret                         ; 1F28

; ---- put_player_char @ 1F29
; Write one player char; if the cell held an enemy or fire (>=0xC0) start dying.
put_player_char:
	ld a,(bc)                   ; 1F29
	ld d,a                      ; 1F2A
	ld a,e                      ; 1F2B
	ld (bc),a                   ; 1F2C
	inc bc                      ; 1F2D
	inc e                       ; 1F2E
	ld a,d                      ; 1F2F
	cp 0c0h                     ; 1F30  enemy or fire under the player?
	ret c                       ; 1F32
	ld a,(player_state)         ; 1F33
	cp 006h                     ; 1F36
	ret nc                      ; 1F38
	ld a,006h                   ; 1F39  start dying
	ld (player_state),a         ; 1F3B
	ret                         ; 1F3E

; ---- move_deltas @ 1F3F
; Per key (down,left,right,up): dX,dY.
move_deltas:
	defb 00h,01h	; down
	defb 0FFh,00h	; left
	defb 01h,00h	; right
	defb 00h,0FFh	; up

; ---- player_anim @ 1F47
; Toggle player animation frame; drive death animation; set life_lost at the end.
player_anim:
	ld a,(tmr_player_anim)      ; 1F47
	or a                        ; 1F4A
	ret nz                      ; 1F4B
	ld a,(player_anim_frame)    ; 1F4C  toggle player anim frame
	xor 002h                    ; 1F4F
	ld (player_anim_frame),a    ; 1F51
	ld a,(player_state)         ; 1F54
	cp 006h                     ; 1F57
	jr c,move_player            ; 1F59
	cp 00dh                     ; 1F5B  state 13: animation over
	jr z,.pa_lost               ; 1F5D
	ld hl,tmr_explode           ; 1F5F
	call tick_timer             ; 1F62
	ld a,(tmr_explode)          ; 1F65
	or a                        ; 1F68
	ret nz                      ; 1F69
	ld a,(player_state)         ; 1F6A  death animation step
	inc a                       ; 1F6D
	ld (player_state),a         ; 1F6E
	ld h,a                      ; 1F71
	ld l,000h                   ; 1F72
	ld (RATIO),hl               ; 1F74  tone ratio state<<8
	ld bc,00020h                ; 1F77
	call beep                   ; 1F7A
	ret                         ; 1F7D
.pa_lost:
	ld a,001h                   ; 1F7E
	ld (life_lost),a            ; 1F80
	ret                         ; 1F83

; ---- life_lost @ 1F84
; 1 when the player death animation has finished.
life_lost:
	defb 01h

; ---- move_player @ 1F85
; Cursor keys (0x11..0x14) move the player by one char if the 2x2 ahead is free.
move_player:
	call GETKY                  ; 1F85  monitor GETKY
	ld b,002h                   ; 1F88
	ld c,a                      ; 1F8A
	cp 011h                     ; 1F8B  0x11 = cursor down
	jr z,.mp_dir                ; 1F8D
	inc b                       ; 1F8F
	ld a,c                      ; 1F90
	cp 014h                     ; 1F91  0x14 = cursor left
	jr z,.mp_dir                ; 1F93
	inc b                       ; 1F95
	ld a,c                      ; 1F96
	cp 013h                     ; 1F97  0x13 = cursor right
	jr z,.mp_dir                ; 1F99
	inc b                       ; 1F9B
	ld a,c                      ; 1F9C
	cp 012h                     ; 1F9D  0x12 = cursor up
	jr z,.mp_dir                ; 1F9F
	ret                         ; 1FA1
.mp_dir:
	ld hl,move_deltas           ; 1FA2  table index (dir-2)*2
	ld a,b                      ; 1FA5
	ld e,a                      ; 1FA6
	dec a                       ; 1FA7
	dec a                       ; 1FA8
	add a,a                     ; 1FA9
	ld b,000h                   ; 1FAA
	ld c,a                      ; 1FAC
	add hl,bc                   ; 1FAD
	ld a,(player_x)             ; 1FAE
	add a,(hl)                  ; 1FB1
	ld c,a                      ; 1FB2
	ld a,(player_y)             ; 1FB3
	inc hl                      ; 1FB6
	add a,(hl)                  ; 1FB7
	ld b,a                      ; 1FB8
	push bc                     ; 1FB9
	call draw_addr              ; 1FBA
	ld d,000h                   ; 1FBD  D = number of blocking chars
	call count_block            ; 1FBF
	call count_block            ; 1FC2
	ld hl,00026h                ; 1FC5
	add hl,bc                   ; 1FC8
	ld b,h                      ; 1FC9
	ld c,l                      ; 1FCA
	call count_block            ; 1FCB
	call count_block            ; 1FCE
	ld a,d                      ; 1FD1
	or a                        ; 1FD2
	pop bc                      ; 1FD3
	ret nz                      ; 1FD4
	ld a,b                      ; 1FD5
	ld (player_y),a             ; 1FD6
	ld a,c                      ; 1FD9
	ld (player_x),a             ; 1FDA
	ld a,e                      ; 1FDD  DEAD STORE: direction never reaches player_state
	ld a,(player_anim_frame)    ; 1FDE  step sound: ratio (anim/2+2)<<8 | 0x0A
	srl a                       ; 1FE1
	inc a                       ; 1FE3
	inc a                       ; 1FE4
	ld l,00ah                   ; 1FE5
	ld h,a                      ; 1FE7
	ld (RATIO),hl               ; 1FE8
	ld bc,0000eh                ; 1FEB
	call beep                   ; 1FEE
	ret                         ; 1FF1

; ---- count_block @ 1FF2
; D++ if map char at BC is a wall/pillar/brick; BC++.
count_block:
	ld a,(bc)                   ; 1FF2
	inc bc                      ; 1FF3
	cp 088h                     ; 1FF4
	jr nz,.cb_chk89             ; 1FF6
	inc d                       ; 1FF8
.cb_chk89:
	cp 089h                     ; 1FF9
	jr nz,.cb_chk80             ; 1FFB
	inc d                       ; 1FFD
.cb_chk80:
	cp 080h                     ; 1FFE
	ret nz                      ; 2000
	inc d                       ; 2001
	ret                         ; 2002

; ---- draw_hud @ 2003
; Row 24: SCORE, BONUS(time), STG, and update hi-score.
draw_hud:
	ld bc,24*256+0              ; 2003  row 24 col 0
	call draw_addr              ; 2006
	ld hl,str_hud_score         ; 2009
	call print_string           ; 200C
	ld bc,24*256+13             ; 200F  row 24 col 13
	call draw_addr              ; 2012
	ld hl,str_hud_bonus         ; 2015
	call print_string           ; 2018
	ld b,018h                   ; 201B  row 24 col 33
	ld c,021h                   ; 201D
	call draw_addr              ; 201F
	ld hl,str_hud_stage         ; 2022
	call print_string           ; 2025
	ld b,018h                   ; 2028  row 24 col 37: stage number
	ld c,025h                   ; 202A
	call draw_addr              ; 202C
	ld a,(stage)                ; 202F
	call print_num2             ; 2032
	ld bc,(score)               ; 2035  hi_score = max(hi_score, score)
	ld hl,(hi_score)            ; 2039
	sbc hl,bc                   ; 203C
	jr nc,.dh_score             ; 203E
	ld (hi_score),bc            ; 2040
.dh_score:
	ld bc,24*256+6              ; 2044  row 24 col 6
	call draw_addr              ; 2047
	ld hl,(score)               ; 204A
	call print_num5             ; 204D
	ld bc,24*256+19             ; 2050  row 24 col 19
	call draw_addr              ; 2053
	ld hl,(time_left)           ; 2056
	call print_num5             ; 2059
	dec bc                      ; 205C  blank the 6th digit of the time
	ld a,020h                   ; 205D
	ld (bc),a                   ; 205F
	ret                         ; 2060

; ---- draw_walls @ 2061
; Outer wall (0x88) and 9x5 pillars (0x89) into the draw buffer.
draw_walls:
	ld a,088h                   ; 2061  top row 0x88
	ld d,028h                   ; 2063
	ld bc,0*256+0               ; 2065  Y=0,X=0
	call draw_addr              ; 2068
.dw_top:
	ld (bc),a                   ; 206B
	inc bc                      ; 206C
	dec d                       ; 206D
	jr nz,.dw_top               ; 206E
	ld d,028h                   ; 2070
	ld bc,23*256+0              ; 2072  row 23
	call draw_addr              ; 2075
.dw_bottom:
	ld (bc),a                   ; 2078
	inc bc                      ; 2079
	dec d                       ; 207A
	jr nz,.dw_bottom            ; 207B
	ld d,017h                   ; 207D  left column
	ld bc,0*256+0               ; 207F  Y=0,X=0
	call draw_addr              ; 2082
	ld a,088h                   ; 2085
	ld hl,00028h                ; 2087
.dw_left:
	ld (bc),a                   ; 208A
	add hl,bc                   ; 208B
	ld b,h                      ; 208C
	ld c,l                      ; 208D
	ld hl,00028h                ; 208E
	dec d                       ; 2091
	jr nz,.dw_left              ; 2092
	ld d,017h                   ; 2094
	ld bc,0*256+39              ; 2096  right column (39)
	call draw_addr              ; 2099
	ld a,088h                   ; 209C
	ld hl,00028h                ; 209E
.dw_right:
	ld (bc),a                   ; 20A1
	add hl,bc                   ; 20A2
	ld b,h                      ; 20A3
	ld c,l                      ; 20A4
	ld hl,00028h                ; 20A5
	dec d                       ; 20A8
	jr nz,.dw_right             ; 20A9
	ld a,003h                   ; 20AB  pillars start at (3,3)
	ld b,a                      ; 20AD
	ld c,a                      ; 20AE
	call draw_addr              ; 20AF
	ld d,005h                   ; 20B2
.dw_prow:
	ld e,009h                   ; 20B4
.dw_pcol:
	call put_pillar             ; 20B6
	or a                        ; 20B9
	ld h,b                      ; 20BA
	ld l,c                      ; 20BB
	ld bc,00025h                ; 20BC  next pillar 4 chars right
	sbc hl,bc                   ; 20BF
	ld b,h                      ; 20C1
	ld c,l                      ; 20C2
	dec e                       ; 20C3
	jr nz,.dw_pcol              ; 20C4
	ld hl,0007ch                ; 20C6  next pillar row 4 rows down
	add hl,bc                   ; 20C9
	ld b,h                      ; 20CA
	ld c,l                      ; 20CB
	dec d                       ; 20CC
	jr nz,.dw_prow              ; 20CD
	ret                         ; 20CF

; ---- put_pillar @ 20D0
; 2x2 of 0x89 at BC, BC advances to the lower-right char.
put_pillar:
	ld a,089h                   ; 20D0
	ld (bc),a                   ; 20D2
	inc bc                      ; 20D3
	ld (bc),a                   ; 20D4
	ld hl,00027h                ; 20D5
	add hl,bc                   ; 20D8
	ld b,h                      ; 20D9
	ld c,l                      ; 20DA
	ld (bc),a                   ; 20DB
	inc bc                      ; 20DC
	ld (bc),a                   ; 20DD
	ret                         ; 20DE

; ---- draw_hud_icons @ 20DF
; Row 24 col 25: lives icon and count; col 30: enemy icon and enemies_left.
draw_hud_icons:
	ld bc,24*256+25             ; 20DF  row 24 col 25
	call draw_addr              ; 20E2
	ld a,090h                   ; 20E5  lives icon
	ld (bc),a                   ; 20E7
	inc bc                      ; 20E8
	ld a,021h                   ; 20E9
	ld (bc),a                   ; 20EB
	inc bc                      ; 20EC
	ld a,(lives)                ; 20ED
	ld (bc),a                   ; 20F0
	inc bc                      ; 20F1
	inc bc                      ; 20F2
	ld a,091h                   ; 20F3  enemy icon
	ld (bc),a                   ; 20F5
	inc bc                      ; 20F6
	ld a,021h                   ; 20F7
	ld (bc),a                   ; 20F9
	inc bc                      ; 20FA
	ld a,(enemies_left)         ; 20FB
	ld (bc),a                   ; 20FE
	ret                         ; 20FF

; ---- str_hud_score @ 2100
; Logical codes 0x10..0x19 are HUD glyphs (SCORE / BONUS letters) in game_table.
str_hud_score:
	defb 10h,11h,12h,13h,14h,"!",0

str_hud_bonus:
	defb 15h,16h,17h,18h,19h,"!",0

str_hud_stage:
	defb 10h,"01!",0

; ---- random_cell @ 2113
; C=random grid column 0..18, B=random grid row 0..10.
random_cell:
	call random                 ; 2113
	and 01fh                    ; 2116
	cp 013h                     ; 2118  column 0..18
	jr nc,random_cell           ; 211A
	ld c,a                      ; 211C
.rc_row:
	call random                 ; 211D
	and 01fh                    ; 2120
	cp 00bh                     ; 2122  row 0..10
	jr nc,.rc_row               ; 2124
	ld b,a                      ; 2126
	ret                         ; 2127

; ---- cell_to_screen @ 2128
; Grid (B,C) -> screen (B*2+1, C*2+1).
cell_to_screen:
	ld a,b                      ; 2128
	add a,a                     ; 2129
	add a,001h                  ; 212A
	ld b,a                      ; 212C
	ld a,c                      ; 212D
	add a,a                     ; 212E
	add a,001h                  ; 212F
	ld c,a                      ; 2131
	ret                         ; 2132

tmp_cx:
	defb 0Eh

; ---- tmp_cy @ 2134
; Player grid position during map generation.
tmp_cy:
	defb 02h

; ---- generate_map @ 2135
; Place player in the inner area, then 50 bricks (0x80); first brick hides BONUS, last hides EXIT.
generate_map:
	call random_cell            ; 2135
	ld a,c                      ; 2138
	ld (tmp_cx),a               ; 2139
	cp 002h                     ; 213C  player column 2..16
	jr c,generate_map           ; 213E
	cp 011h                     ; 2140
	jr nc,generate_map          ; 2142
	ld a,b                      ; 2144
	ld (tmp_cy),a               ; 2145
	cp 002h                     ; 2148  player row 2..8
	jr c,generate_map           ; 214A
	cp 009h                     ; 214C
	jr nc,generate_map          ; 214E
	call cell_to_screen         ; 2150
	push bc                     ; 2153
	call draw_addr              ; 2154
	ld a,(bc)                   ; 2157  cell must be free
	cp 020h                     ; 2158
	pop bc                      ; 215A
	jr nz,generate_map          ; 215B
	ld a,c                      ; 215D
	ld (player_x),a             ; 215E
	ld a,b                      ; 2161
	ld (player_y),a             ; 2162
	ld a,032h                   ; 2165  50 bricks
.gm_brick:
	ex af,af'                   ; 2167
.gm_retry:
	call random_cell            ; 2168
	ld d,b                      ; 216B
	ld e,c                      ; 216C
	ld a,(tmp_cx)               ; 216D  reject cells within 1 of the player
	sub e                       ; 2170
	jr nc,.gm_dx                ; 2171
	neg                         ; 2173
.gm_dx:
	cp 002h                     ; 2175
	jr nc,.gm_parity            ; 2177
	ld a,(tmp_cy)               ; 2179
	sub d                       ; 217C
	jr nc,.gm_dy                ; 217D
	neg                         ; 217F
.gm_dy:
	cp 002h                     ; 2181
	jr nc,.gm_parity            ; 2183
	jr .gm_retry                ; 2185
.gm_parity:
	ld a,c                      ; 2187  reject cells where column and row parity match (pillars and corridors)
	bit 0,a                     ; 2188
	jr z,.gm_parity2            ; 218A
	ld a,b                      ; 218C
	bit 0,a                     ; 218D
	jr nz,.gm_retry             ; 218F
	jr .gm_place                ; 2191
.gm_parity2:
	ld a,b                      ; 2193
	bit 0,a                     ; 2194
	jr z,.gm_retry              ; 2196
.gm_place:
	call cell_to_screen         ; 2198
	ld hl,reserved_cells        ; 219B  reject reserved corner cells
.gm_resv_loop:
	ld a,(hl)                   ; 219E
	inc a                       ; 219F
	jr z,.gm_items              ; 21A0
	dec a                       ; 21A2
	cp b                        ; 21A3
	jr nz,.gm_resv_skip         ; 21A4
	inc hl                      ; 21A6
	ld a,(hl)                   ; 21A7
	cp c                        ; 21A8
	jr z,.gm_retry              ; 21A9
	jr .gm_resv_next            ; 21AB
.gm_resv_skip:
	inc hl                      ; 21AD
.gm_resv_next:
	inc hl                      ; 21AE
	jr .gm_resv_loop            ; 21AF
.gm_items:
	ld a,(bonus_present)        ; 21B1  first brick hides the BONUS
	or a                        ; 21B4
	jr nz,.gm_exit              ; 21B5
	ld a,b                      ; 21B7
	ld (bonus_y),a              ; 21B8
	ld a,c                      ; 21BB
	ld (bonus_x),a              ; 21BC
	ld a,001h                   ; 21BF
	ld (bonus_present),a        ; 21C1
.gm_exit:
	ld a,b                      ; 21C4  last brick hides the EXIT
	ld (exit_y),a               ; 21C5
	ld a,c                      ; 21C8
	ld (exit_x),a               ; 21C9
	ld a,001h                   ; 21CC
	ld (exit_present),a         ; 21CE
	call map_addr               ; 21D1
	ld a,080h                   ; 21D4  brick 0x80
	call fill_2x2               ; 21D6
	ex af,af'                   ; 21D9  brick counter
	dec a                       ; 21DA
	jp nz,.gm_brick             ; 21DB
	ret                         ; 21DE

bonus_x:
	defb 07h

bonus_y:
	defb 05h

bonus_present:
	defb 00h

exit_x:
	defb 11h

exit_y:
	defb 03h

exit_present:
	defb 00h

; ---- reserved_cells @ 21E5
; (Y,X) screen cells that never get a brick: the neighbours of the four corners. 0xFF ends.
reserved_cells:
	defb 01h,03h
	defb 03h,01h
	defb 13h,01h
	defb 15h,03h
	defb 01h,23h
	defb 03h,25h
	defb 13h,25h
	defb 15h,23h
	defb 0FFh

; ---- dead_code_2 @ 21F6
; Unreachable leftover: call map_addr / ld a,20h / call fill_2x2 / ret.
dead_code_2:
	defb 0CDh,97h,26h,3Eh,20h,0CDh,0BEh,1Eh,0C9h

; ---- random @ 21FF
; A = 8-bit pseudo random (16-bit LFSR-ish mixed with R register). Preserves BC,DE,HL.
random:
	push hl                     ; 21FF
	push de                     ; 2200
	push bc                     ; 2201
	ld hl,(rand_seed)           ; 2202
	ld a,h                      ; 2205
	rla                         ; 2206
	rla                         ; 2207
	xor l                       ; 2208
	rra                         ; 2209
	push af                     ; 220A
	ld a,h                      ; 220B
	xor l                       ; 220C
	ld h,a                      ; 220D
	ld a,r                      ; 220E  refresh register adds entropy
	xor l                       ; 2210
	ld l,a                      ; 2211
	pop af                      ; 2212
	rl l                        ; 2213
	rl h                        ; 2215
	ld (rand_seed),hl           ; 2217
	ld a,l                      ; 221A
	pop bc                      ; 221B
	pop de                      ; 221C
	pop hl                      ; 221D
	ret                         ; 221E

rand_seed:
	defw 0BC6Eh

; ---- print_num2 @ 2221
; Write A (0..99) as two digit chars at BC.
print_num2:
	ld h,000h                   ; 2221
	ld l,a                      ; 2223
	ld de,0000ah                ; 2224
	call div_digit              ; 2227
	ld a,l                      ; 222A
	ld (bc),a                   ; 222B
	ret                         ; 222C

; ---- print_num5 @ 222D
; Write HL as 5 digits followed by a fixed 0 digit (scores show x10).
print_num5:
	ld de,02710h                ; 222D
	call div_digit              ; 2230
	ld de,003e8h                ; 2233
	call div_digit              ; 2236
	ld de,00064h                ; 2239
	call div_digit              ; 223C
	ld de,0000ah                ; 223F
	call div_digit              ; 2242
	ld a,l                      ; 2245  units
	ld (bc),a                   ; 2246
	inc bc                      ; 2247
	xor a                       ; 2248  fixed trailing 0
	ld (bc),a                   ; 2249
	inc bc                      ; 224A
	ret                         ; 224B

; ---- div_digit @ 224C
; HL/DE -> digit written at BC (BC++), remainder in HL.
div_digit:
	xor a                       ; 224C
.dd_loop:
	or a                        ; 224D
	sbc hl,de                   ; 224E
	jr c,.dd_done               ; 2250
	inc a                       ; 2252
	jr .dd_loop                 ; 2253
.dd_done:
	add hl,de                   ; 2255
	ld (bc),a                   ; 2256
	inc bc                      ; 2257
	ret                         ; 2258

; ---- print_string @ 2259
; Copy 0-terminated string HL to buffer address BC.
print_string:
	ld a,(hl)                   ; 2259
	inc hl                      ; 225A
	or a                        ; 225B
	ret z                       ; 225C
	ld (bc),a                   ; 225D
	inc bc                      ; 225E
	jr print_string             ; 225F

; ---- draw_addr @ 2261
; BC = draw_buffer + B*40 + C  (B=row/Y, C=column/X).
draw_addr:
	push hl                     ; 2261
	push de                     ; 2262
	ld l,b                      ; 2263  HL = B*40
	ld h,000h                   ; 2264
	ld e,l                      ; 2266
	ld d,h                      ; 2267
	add hl,hl                   ; 2268
	add hl,hl                   ; 2269
	add hl,de                   ; 226A
	add hl,hl                   ; 226B
	add hl,hl                   ; 226C
	add hl,hl                   ; 226D
	ld b,000h                   ; 226E  + C
	add hl,bc                   ; 2270
	ld bc,draw_buffer           ; 2271
	add hl,bc                   ; 2274
	ld b,h                      ; 2275
	ld c,l                      ; 2276
	pop de                      ; 2277
	pop hl                      ; 2278
	ret                         ; 2279

; ---- clear_map @ 227A
; Fill map_layer with spaces.
clear_map:
	ld hl,map_layer             ; 227A
	ld a,020h                   ; 227D
	ld b,028h                   ; 227F
.cm_row:
	ld c,019h                   ; 2281
.cm_col:
	ld (hl),a                   ; 2283
	inc hl                      ; 2284
	dec c                       ; 2285
	jr nz,.cm_col               ; 2286
	dec b                       ; 2288
	jr nz,.cm_row               ; 2289
	ret                         ; 228B

; ---- map_layer @ 228C
; 40x25 logical codes: walls are NOT here; bricks 0x80..0x87, bombs, fire, items. Snapshot of RAM at save time.
map_layer:
	defb 20h,20h,20h,20h,20h,20h,20h,20h,20h,20h,20h,20h,20h,20h,20h,20h,20h,20h,20h,20h,20h,20h,20h,20h,20h,20h,20h,20h,20h,20h,20h,20h,20h,20h,20h,20h,20h,20h,20h,20h	; row 0
	defb 20h,20h,20h,20h,20h,20h,20h,80h,80h,20h,20h,80h,80h,20h,20h,80h,80h,20h,20h,80h,80h,20h,20h,20h,20h,20h,20h,20h,20h,20h,20h,20h,20h,20h,20h,20h,20h,20h,20h,20h	; row 1
	defb 20h,20h,20h,20h,20h,20h,20h,80h,80h,20h,20h,80h,80h,20h,20h,80h,80h,20h,20h,80h,80h,20h,20h,20h,20h,20h,20h,20h,20h,20h,20h,20h,20h,20h,20h,20h,20h,20h,20h,20h	; row 2
	defb 20h,20h,20h,20h,20h,20h,20h,20h,20h,20h,20h,20h,20h,20h,20h,20h,20h,20h,80h,20h,20h,20h,20h,20h,20h,80h,80h,20h,20h,20h,20h,20h,20h,20h,20h,20h,20h,20h,20h,20h	; row 3
	defb 20h,20h,20h,20h,20h,20h,20h,20h,20h,20h,20h,20h,20h,20h,20h,20h,20h,20h,80h,20h,20h,20h,20h,20h,20h,80h,80h,20h,20h,20h,20h,20h,20h,20h,20h,20h,20h,20h,20h,20h	; row 4
	defb 20h,20h,20h,20h,20h,20h,20h,80h,80h,20h,20h,20h,20h,20h,20h,20h,20h,20h,20h,20h,20h,20h,20h,20h,20h,20h,20h,20h,20h,20h,20h,20h,20h,20h,20h,80h,80h,20h,20h,20h	; row 5
	defb 20h,20h,20h,20h,20h,20h,20h,80h,80h,20h,20h,20h,20h,20h,20h,20h,20h,20h,20h,20h,20h,20h,20h,20h,20h,20h,20h,20h,20h,20h,20h,20h,20h,20h,20h,80h,80h,20h,20h,20h	; row 6
	defb 20h,20h,20h,20h,20h,20h,20h,20h,20h,20h,20h,20h,20h,80h,80h,20h,20h,20h,20h,20h,20h,80h,80h,20h,20h,80h,80h,20h,20h,20h,20h,20h,20h,80h,80h,20h,20h,80h,80h,20h	; row 7
	defb 20h,20h,20h,20h,20h,20h,20h,20h,20h,20h,20h,20h,20h,80h,80h,20h,20h,20h,20h,20h,20h,80h,80h,20h,20h,80h,80h,20h,20h,20h,20h,20h,20h,80h,80h,20h,20h,80h,80h,20h	; row 8
	defb 20h,20h,20h,80h,80h,20h,20h,20h,20h,20h,20h,20h,20h,20h,20h,80h,80h,20h,20h,80h,80h,20h,20h,80h,80h,20h,20h,80h,80h,20h,20h,80h,80h,20h,20h,80h,80h,20h,20h,20h	; row 9
	defb 20h,20h,20h,80h,80h,20h,20h,20h,20h,20h,20h,20h,20h,20h,20h,80h,80h,20h,20h,80h,80h,20h,20h,80h,80h,20h,20h,80h,80h,20h,20h,80h,80h,20h,20h,80h,80h,20h,20h,20h	; row 10
	defb 20h,20h,20h,20h,20h,20h,20h,20h,20h,80h,80h,20h,20h,20h,20h,20h,20h,20h,20h,20h,20h,80h,80h,20h,20h,20h,20h,20h,20h,20h,20h,20h,20h,80h,80h,20h,20h,80h,80h,20h	; row 11
	defb 20h,20h,20h,20h,20h,20h,20h,20h,20h,80h,80h,20h,20h,20h,20h,20h,20h,20h,20h,20h,20h,80h,80h,20h,20h,20h,20h,20h,20h,20h,20h,20h,20h,80h,80h,20h,20h,80h,80h,20h	; row 12
	defb 20h,20h,20h,20h,20h,20h,20h,80h,80h,20h,20h,80h,80h,20h,20h,20h,20h,20h,20h,20h,20h,20h,20h,20h,20h,20h,20h,20h,20h,20h,20h,80h,80h,20h,20h,20h,20h,20h,20h,20h	; row 13
	defb 20h,20h,20h,20h,20h,20h,20h,80h,80h,20h,20h,80h,80h,20h,20h,20h,20h,20h,20h,20h,20h,20h,20h,20h,20h,20h,20h,20h,20h,20h,20h,80h,80h,20h,20h,20h,20h,20h,20h,20h	; row 14
	defb 20h,80h,80h,20h,20h,20h,20h,20h,20h,80h,80h,20h,20h,20h,20h,20h,20h,20h,20h,20h,20h,20h,20h,20h,20h,80h,80h,20h,20h,80h,80h,20h,20h,20h,20h,20h,20h,80h,80h,20h	; row 15
	defb 20h,80h,80h,20h,20h,20h,20h,20h,20h,80h,80h,20h,20h,20h,20h,20h,20h,20h,20h,20h,20h,20h,20h,20h,20h,80h,80h,20h,20h,80h,80h,20h,20h,20h,20h,20h,20h,80h,80h,20h	; row 16
	defb 20h,20h,20h,20h,20h,20h,20h,80h,80h,20h,20h,20h,20h,20h,20h,80h,80h,20h,20h,80h,80h,20h,20h,20h,20h,20h,20h,20h,20h,20h,20h,20h,20h,20h,20h,80h,80h,20h,20h,20h	; row 17
	defb 20h,20h,20h,20h,20h,20h,20h,80h,80h,20h,20h,20h,20h,20h,20h,80h,80h,20h,20h,80h,80h,20h,20h,20h,20h,20h,20h,20h,20h,20h,20h,20h,20h,20h,20h,80h,80h,20h,20h,20h	; row 18
	defb 20h,20h,20h,20h,20h,20h,20h,20h,20h,20h,20h,20h,20h,80h,80h,20h,20h,20h,20h,20h,20h,20h,20h,20h,20h,80h,80h,20h,20h,80h,80h,20h,20h,20h,20h,20h,20h,20h,20h,20h	; row 19
	defb 20h,20h,20h,20h,20h,20h,20h,20h,20h,20h,20h,20h,20h,80h,80h,20h,20h,20h,20h,20h,20h,20h,20h,20h,20h,80h,80h,20h,20h,80h,80h,20h,20h,20h,20h,20h,20h,20h,20h,20h	; row 20
	defb 20h,20h,20h,20h,20h,20h,20h,20h,20h,20h,20h,20h,20h,20h,20h,80h,80h,20h,20h,20h,20h,20h,20h,20h,20h,20h,20h,80h,80h,20h,20h,20h,20h,20h,20h,20h,20h,20h,20h,20h	; row 21
	defb 20h,20h,20h,20h,20h,20h,20h,20h,20h,20h,20h,20h,20h,20h,20h,80h,80h,20h,20h,20h,20h,20h,20h,20h,20h,20h,20h,80h,80h,20h,20h,20h,20h,20h,20h,20h,20h,20h,20h,20h	; row 22
	defb 20h,20h,20h,20h,20h,20h,20h,20h,20h,20h,20h,20h,20h,20h,20h,20h,20h,20h,20h,20h,20h,20h,20h,20h,20h,20h,20h,20h,20h,20h,20h,20h,20h,20h,20h,20h,20h,20h,20h,20h	; row 23
	defb 20h,20h,20h,20h,20h,20h,20h,20h,20h,20h,20h,20h,20h,20h,20h,20h,20h,20h,20h,20h,20h,20h,20h,20h,20h,20h,20h,20h,20h,20h,20h,20h,20h,20h,20h,20h,20h,20h,20h,20h	; row 24

; ---- composite_map @ 2674
; Copy every non-space map_layer char over the draw buffer.
composite_map:
	ld de,003e8h                ; 2674
	ld bc,0*256+0               ; 2677  Y=0,X=0
	call map_addr               ; 267A
	exx                         ; 267D
	ld bc,0*256+0               ; 267E  Y=0,X=0
	call draw_addr              ; 2681
	exx                         ; 2684
.cmp_loop:
	ld a,(bc)                   ; 2685
	cp 020h                     ; 2686
	jr z,.cmp_skip              ; 2688
	exx                         ; 268A
	ld (bc),a                   ; 268B
	exx                         ; 268C
.cmp_skip:
	inc bc                      ; 268D
	exx                         ; 268E
	inc bc                      ; 268F
	exx                         ; 2690
	dec de                      ; 2691
	ld a,d                      ; 2692
	or e                        ; 2693
	jr nz,.cmp_loop             ; 2694
	ret                         ; 2696

; ---- map_addr @ 2697
; BC = map_layer + B*40 + C.
map_addr:
	push hl                     ; 2697
	push de                     ; 2698
	ld l,b                      ; 2699  HL = B*40
	ld h,000h                   ; 269A
	ld e,l                      ; 269C
	ld d,h                      ; 269D
	add hl,hl                   ; 269E
	add hl,hl                   ; 269F
	add hl,de                   ; 26A0
	add hl,hl                   ; 26A1
	add hl,hl                   ; 26A2
	add hl,hl                   ; 26A3
	ld b,000h                   ; 26A4
	add hl,bc                   ; 26A6
	ld bc,map_layer             ; 26A7
	add hl,bc                   ; 26AA
	ld b,h                      ; 26AB
	ld c,l                      ; 26AC
	pop de                      ; 26AD
	pop hl                      ; 26AE
	ret                         ; 26AF

; ---- delay @ 26B0
; Busy wait (0x5000 iterations).
delay:
	ld hl,05000h                ; 26B0
.dl_loop:
	dec hl                      ; 26B3
	ld a,h                      ; 26B4
	or l                        ; 26B5
	jr nz,.dl_loop              ; 26B6
	ret                         ; 26B8

; ---- beep @ 26B9
; Play tone (ratio already stored at RATIO) for B*C loop counts.
beep:
	call MSTA                   ; 26B9  monitor MSTA: start sound
.bp_loop:
	djnz .bp_loop               ; 26BC
	dec c                       ; 26BE
	jr nz,.bp_loop              ; 26BF
	call MSTP                   ; 26C1  monitor MSTP: stop sound
	ret                         ; 26C4

unused_26c5:
	defb 01h

player_x:
	defb 10h

player_y:
	defb 05h

; ---- player_state @ 26C8
; 0 stand,1 standing(anim),2..5 walking(never set),6..13 dying.
player_state:
	defb 0Dh

player_anim_frame:
	defb 00h

score:
	defw 00000h

hi_score:
	defw 00172h

time_left:
	defw 00334h

lives:
	defb 00h

stage:
	defb 01h

; ---- hit_pending @ 26D2
; 1 = explosion hit bonus/exit, enemies to spawn.
hit_pending:
	defb 00h

; ---- hit_spawned @ 26D3
; 1 = spawn already happened this stage.
hit_spawned:
	defb 00h

enemies_left:
	defb 01h

bonus_revealed:
	defb 00h

exit_revealed:
	defb 00h

exit_touched:
	defb 00h

enemy_period:
	defb 10h

; ---- stage_cleared @ 26D9
; non-zero when the last enemy died.
stage_cleared:
	defb 00h

; ---- flush_screen @ 26DA
; Diff draw buffer against shadow, write changed chars+attributes to VRAM, clear draw buffer.
flush_screen:
	push bc                     ; 26DA
	push de                     ; 26DB
	push hl                     ; 26DC
	ld hl,VRAM                  ; 26DD  HL' = VRAM pointer
	exx                         ; 26E0
	ld de,draw_buffer           ; 26E1
	ld hl,shadow_vram           ; 26E4
	ld bc,00000h                ; 26E7
	ld a,020h                   ; 26EA  A' = space (draw buffer is consumed)
	ex af,af'                   ; 26EC
	ld c,019h                   ; 26ED
.fs_row:
	ld b,028h                   ; 26EF
.fs_col:
	ld a,(de)                   ; 26F1
	ex af,af'                   ; 26F2
	ld (de),a                   ; 26F3
	ex af,af'                   ; 26F4
	cp (hl)                     ; 26F5  changed?
	ld (hl),a                   ; 26F6
	call nz,put_vram_char       ; 26F7
	inc hl                      ; 26FA
	inc de                      ; 26FB
	exx                         ; 26FC
	inc hl                      ; 26FD
	exx                         ; 26FE
	djnz .fs_col                ; 26FF
	dec c                       ; 2701
	jr nz,.fs_row               ; 2702
	pop hl                      ; 2704
	pop de                      ; 2705
	pop bc                      ; 2706
	ret                         ; 2707

; ---- put_vram_char @ 2708
; Translate logical code A through game_table/title_table and store char + attribute.
put_vram_char:
	push bc                     ; 2708
	push de                     ; 2709
	push hl                     ; 270A
	exx                         ; 270B
	push hl                     ; 270C
	exx                         ; 270D
	ld l,a                      ; 270E  HL = code*2
	ld h,000h                   ; 270F
	add hl,hl                   ; 2711

; ---- mode_patch @ 2712
; SELF-MODIFIED: 00 00 (nop nop) = title mode, 18 09 (jr +9) = game mode.
mode_patch:
	nop                         ; 2712
	nop                         ; 2713
	cp 05ah                     ; 2714  title mode: codes < 0x5A use title_table
	jr nc,.pvc_game             ; 2716
	ld de,title_table           ; 2718
	jr .pvc_lookup              ; 271B
.pvc_game:
	ld de,game_table            ; 271D
.pvc_lookup:
	add hl,de                   ; 2720
	pop de                      ; 2721
	ex de,hl                    ; 2722
	ld a,(de)                   ; 2723
	ld (hl),a                   ; 2724
	inc de                      ; 2725
	ld bc,00800h                ; 2726  attribute plane is VRAM+0x800
	add hl,bc                   ; 2729
	ld a,(de)                   ; 272A
	ld (hl),a                   ; 272B
	pop hl                      ; 272C
	pop de                      ; 272D
	pop bc                      ; 272E
	ret                         ; 272F

; ---- clear_buffers @ 2730
; draw_buffer <- spaces (1024), shadow_vram <- 0xFF (1024) forcing a full redraw.
clear_buffers:
	ld hl,draw_buffer           ; 2730
	ld bc,00004h                ; 2733
	ld a,020h                   ; 2736
.clb_loop1:
	ld (hl),a                   ; 2738
	inc hl                      ; 2739
	djnz .clb_loop1             ; 273A
	dec c                       ; 273C
	jr nz,.clb_loop1            ; 273D
	ld hl,shadow_vram           ; 273F
	ld a,0ffh                   ; 2742
	ld bc,00004h                ; 2744
.clb_loop2:
	ld (hl),a                   ; 2747
	inc hl                      ; 2748
	djnz .clb_loop2             ; 2749
	dec c                       ; 274B
	jr nz,.clb_loop2            ; 274C
	ret                         ; 274E

; ---- draw_buffer @ 274F
; 40x25 logical codes, rebuilt every frame (flush_screen clears it). 1024 bytes reserved.
draw_buffer:
	defb 20h,20h,20h,20h,20h,20h,20h,20h,20h,20h,20h,20h,20h,20h,20h,20h,20h,20h,20h,20h,20h,20h,20h,20h,20h,20h,20h,20h,20h,20h,20h,20h,20h,20h,20h,20h,20h,20h,20h,20h	; row 0
	defb 20h,20h,20h,20h,20h,20h,20h,20h,20h,20h,20h,20h,20h,20h,20h,20h,20h,20h,20h,20h,20h,20h,20h,20h,20h,20h,20h,20h,20h,20h,20h,20h,20h,20h,20h,20h,20h,20h,20h,20h	; row 1
	defb 20h,20h,20h,20h,20h,20h,20h,20h,20h,20h,20h,20h,20h,20h,20h,20h,20h,20h,20h,20h,20h,20h,20h,20h,20h,20h,20h,20h,20h,20h,20h,20h,20h,20h,20h,20h,20h,20h,20h,20h	; row 2
	defb 20h,20h,20h,20h,20h,20h,20h,20h,20h,20h,20h,20h,20h,20h,20h,20h,20h,20h,20h,20h,20h,20h,20h,20h,20h,20h,20h,20h,20h,20h,20h,20h,20h,20h,20h,20h,20h,20h,20h,20h	; row 3
	defb 20h,20h,20h,20h,20h,20h,20h,20h,20h,20h,20h,20h,20h,20h,20h,20h,20h,20h,20h,20h,20h,20h,20h,20h,20h,20h,20h,20h,20h,20h,20h,20h,20h,20h,20h,20h,20h,20h,20h,20h	; row 4
	defb 20h,20h,20h,20h,20h,20h,20h,20h,20h,20h,20h,20h,20h,20h,20h,20h,20h,20h,20h,20h,20h,20h,20h,20h,20h,20h,20h,20h,20h,20h,20h,20h,20h,20h,20h,20h,20h,20h,20h,20h	; row 5
	defb 20h,20h,20h,20h,20h,20h,20h,20h,20h,20h,20h,20h,20h,20h,20h,20h,20h,20h,20h,20h,20h,20h,20h,20h,20h,20h,20h,20h,20h,20h,20h,20h,20h,20h,20h,20h,20h,20h,20h,20h	; row 6
	defb 20h,20h,20h,20h,20h,20h,20h,20h,20h,20h,20h,20h,20h,20h,20h,20h,20h,20h,20h,20h,20h,20h,20h,20h,20h,20h,20h,20h,20h,20h,20h,20h,20h,20h,20h,20h,20h,20h,20h,20h	; row 7
	defb 20h,20h,20h,20h,20h,20h,20h,20h,20h,20h,20h,20h,20h,20h,20h,20h,20h,20h,20h,20h,20h,20h,20h,20h,20h,20h,20h,20h,20h,20h,20h,20h,20h,20h,20h,20h,20h,20h,20h,20h	; row 8
	defb 20h,20h,20h,20h,20h,20h,20h,20h,20h,20h,20h,20h,20h,20h,20h,20h,20h,20h,20h,20h,20h,20h,20h,20h,20h,20h,20h,20h,20h,20h,20h,20h,20h,20h,20h,20h,20h,20h,20h,20h	; row 9
	defb 20h,20h,20h,20h,20h,20h,20h,20h,20h,20h,20h,20h,20h,20h,20h,20h,20h,20h,20h,20h,20h,20h,20h,20h,20h,20h,20h,20h,20h,20h,20h,20h,20h,20h,20h,20h,20h,20h,20h,20h	; row 10
	defb 20h,20h,20h,20h,20h,20h,20h,20h,20h,20h,20h,20h,20h,20h,20h,20h,20h,20h,20h,20h,20h,20h,20h,20h,20h,20h,20h,20h,20h,20h,20h,20h,20h,20h,20h,20h,20h,20h,20h,20h	; row 11
	defb 20h,20h,20h,20h,20h,20h,20h,20h,20h,20h,20h,20h,20h,20h,20h,20h,20h,20h,20h,20h,20h,20h,20h,20h,20h,20h,20h,20h,20h,20h,20h,20h,20h,20h,20h,20h,20h,20h,20h,20h	; row 12
	defb 20h,20h,20h,20h,20h,20h,20h,20h,20h,20h,20h,20h,20h,20h,20h,20h,20h,20h,20h,20h,20h,20h,20h,20h,20h,20h,20h,20h,20h,20h,20h,20h,20h,20h,20h,20h,20h,20h,20h,20h	; row 13
	defb 20h,20h,20h,20h,20h,20h,20h,20h,20h,20h,20h,20h,20h,20h,20h,20h,20h,20h,20h,20h,20h,20h,20h,20h,20h,20h,20h,20h,20h,20h,20h,20h,20h,20h,20h,20h,20h,20h,20h,20h	; row 14
	defb 20h,20h,20h,15h,24h,16h,20h,20h,20h,15h,23h,16h,20h,20h,20h,20h,20h,20h,20h,20h,20h,20h,20h,20h,20h,20h,20h,20h,20h,20h,20h,20h,20h,20h,20h,20h,20h,20h,20h,20h	; row 15
	defb 20h,20h,20h,12h,0Ch,13h,20h,20h,20h,12h,0Ch,13h,20h,53h,45h,54h,20h,42h,4Fh,4Dh,42h,20h,20h,48h,49h,40h,53h,43h,4Fh,52h,45h,20h,00h,00h,03h,07h,00h,00h,20h,20h	; row 16
	defb 20h,20h,20h,20h,20h,20h,10h,0Ch,11h,20h,20h,20h,20h,10h,0Ch,0Ch,0Ch,0Ch,0Ch,11h,20h,20h,20h,20h,20h,20h,20h,20h,20h,20h,20h,20h,20h,20h,20h,20h,20h,20h,20h,20h	; row 17
	defb 20h,20h,20h,20h,20h,20h,15h,21h,16h,20h,20h,20h,20h,15h,53h,50h,41h,43h,45h,16h,20h,20h,20h,20h,20h,20h,53h,43h,4Fh,52h,45h,20h,00h,00h,00h,00h,00h,00h,20h,20h	; row 18
	defb 20h,20h,20h,20h,20h,20h,12h,0Ch,13h,20h,20h,20h,20h,12h,0Ch,0Ch,0Ch,0Ch,0Ch,13h,20h,20h,20h,20h,20h,20h,20h,20h,20h,20h,20h,20h,20h,20h,20h,20h,20h,20h,20h,20h	; row 19
	defb 20h,20h,20h,20h,20h,20h,44h,4Fh,57h,4Eh,20h,20h,20h,20h,20h,20h,20h,20h,20h,20h,20h,20h,20h,20h,20h,20h,20h,20h,20h,20h,20h,20h,20h,20h,20h,20h,20h,20h,20h,20h	; row 20
	defb 20h,20h,20h,20h,20h,20h,20h,20h,20h,20h,20h,20h,20h,20h,20h,20h,20h,20h,20h,20h,20h,20h,20h,20h,20h,20h,20h,20h,20h,20h,20h,20h,20h,20h,20h,20h,20h,20h,20h,20h	; row 21
	defb 20h,20h,20h,20h,20h,20h,20h,20h,50h,55h,53h,48h,20h,53h,50h,41h,43h,45h,20h,54h,4Fh,20h,53h,54h,41h,52h,54h,20h,47h,41h,4Dh,45h,20h,20h,20h,20h,20h,20h,20h,20h	; row 22
	defb 20h,20h,20h,20h,20h,20h,20h,20h,20h,20h,20h,20h,20h,20h,20h,20h,20h,20h,20h,20h,20h,20h,20h,20h,20h,20h,20h,20h,20h,20h,20h,20h,20h,20h,20h,20h,20h,20h,20h,20h	; row 23
	defb 20h,20h,43h,4Fh,50h,59h,52h,49h,47h,48h,54h,20h,17h,43h,18h,20h,01h,09h,08h,03h,20h,20h,48h,55h,44h,53h,4Fh,4Eh,20h,53h,4Fh,46h,54h,20h,49h,4Eh,43h,19h,20h,20h	; row 24
	defb 20h,20h,20h,20h,20h,20h,20h,20h,20h,20h,20h,20h,20h,20h,20h,20h,20h,20h,20h,20h,20h,20h,20h,20h	; padding to 1024

; ---- shadow_vram @ 2B4F
; Logical codes currently shown on screen (for the diff). 1024 bytes reserved.
shadow_vram:
	defb 2Eh,2Eh,2Eh,2Eh,2Eh,2Eh,2Eh,2Eh,2Eh,2Eh,2Eh,2Eh,2Eh,2Eh,2Eh,2Eh,2Eh,2Eh,2Eh,2Eh,2Eh,2Eh,2Eh,2Eh,2Eh,2Eh,2Eh,2Eh,2Eh,2Eh,2Eh,2Eh,2Eh,2Eh,2Eh,2Eh,2Eh,2Eh,2Eh,2Eh	; row 0
	defb 2Eh,3Ah,33h,33h,34h,38h,33h,33h,34h,3Ah,34h,38h,35h,3Ah,33h,33h,34h,3Ah,33h,33h,2Fh,3Ah,33h,33h,34h,2Eh,2Eh,3Ah,34h,38h,35h,20h,36h,39h,20h,3Ah,34h,20h,35h,2Eh	; row 1
	defb 2Eh,3Ah,3Ch,3Ch,2Fh,3Ah,20h,20h,35h,3Ah,3Ah,35h,35h,3Ah,3Ch,3Ch,2Fh,3Ah,3Ch,3Ch,20h,3Ah,3Ch,3Ch,2Fh,2Eh,2Eh,3Ah,3Ah,35h,35h,3Ah,3Ch,3Ch,35h,3Ah,32h,34h,35h,2Eh	; row 2
	defb 2Eh,3Ah,20h,20h,35h,3Ah,20h,20h,35h,3Ah,20h,20h,35h,3Ah,20h,20h,35h,3Ah,20h,20h,20h,3Ah,20h,39h,20h,2Eh,2Eh,3Ah,20h,20h,35h,3Ah,20h,20h,35h,3Ah,20h,32h,35h,2Eh	; row 3
	defb 2Eh,32h,33h,33h,20h,20h,33h,33h,20h,32h,20h,20h,2Fh,32h,33h,33h,20h,32h,33h,33h,2Fh,32h,20h,20h,2Fh,2Eh,2Eh,32h,20h,20h,2Fh,32h,20h,20h,2Fh,32h,20h,20h,2Fh,2Eh	; row 4
	defb 2Eh,2Eh,2Eh,2Eh,2Eh,2Eh,2Eh,2Eh,2Eh,2Eh,2Eh,2Eh,2Eh,2Eh,2Eh,2Eh,2Eh,2Eh,2Eh,2Eh,2Eh,2Eh,2Eh,2Eh,2Eh,2Eh,2Eh,2Eh,2Eh,2Eh,2Eh,2Eh,2Eh,2Eh,2Eh,2Eh,2Eh,2Eh,2Eh,2Eh	; row 5
	defb 20h,20h,20h,20h,20h,20h,20h,20h,20h,20h,20h,20h,20h,20h,20h,20h,20h,20h,20h,20h,20h,20h,20h,20h,20h,20h,20h,20h,20h,20h,20h,20h,20h,20h,20h,20h,20h,20h,20h,20h	; row 6
	defb 20h,20h,8Ch,8Dh,42h,4Fh,4Dh,42h,45h,52h,20h,0C0h,0C1h,02h,30h,30h,40h,20h,20h,0C4h,0C5h,01h,05h,30h,40h,20h,0C8h,0C9h,01h,30h,30h,40h,20h,20h,0CCh,0CDh,05h,30h,40h,20h	; row 7
	defb 20h,20h,9Ch,9Dh,20h,4Dh,41h,4Eh,20h,20h,20h,0D0h,0D1h,20h,01h,06h,30h,20h,20h,0D4h,0D5h,01h,01h,30h,20h,20h,0D8h,0D9h,20h,06h,30h,20h,20h,20h,0DCh,0DDh,01h,30h,20h,20h	; row 8
	defb 20h,20h,20h,20h,20h,20h,20h,20h,20h,20h,20h,20h,20h,20h,20h,20h,20h,20h,20h,20h,20h,20h,20h,20h,20h,20h,20h,20h,20h,20h,20h,20h,20h,20h,20h,20h,20h,20h,20h,20h	; row 9
	defb 20h,20h,20h,20h,20h,20h,55h,50h,20h,20h,20h,20h,20h,20h,20h,20h,42h,4Fh,4Eh,55h,53h,20h,20h,20h,45h,58h,49h,54h,20h,20h,20h,42h,4Fh,4Dh,42h,20h,20h,20h,20h,20h	; row 10
	defb 20h,20h,20h,20h,20h,20h,10h,0Ch,11h,20h,20h,20h,20h,20h,20h,20h,20h,0Ah,0Bh,20h,20h,20h,20h,20h,20h,0Eh,0Fh,20h,20h,20h,20h,20h,62h,63h,20h,20h,20h,20h,20h,20h	; row 11
	defb 20h,20h,20h,20h,20h,20h,15h,22h,16h,20h,20h,20h,20h,20h,20h,20h,20h,1Ah,1Bh,20h,20h,20h,20h,20h,20h,1Eh,1Fh,20h,20h,20h,20h,20h,72h,73h,20h,20h,20h,20h,20h,20h	; row 12
	defb 20h,20h,4Ch,45h,46h,54h,12h,0Ch,13h,52h,49h,47h,48h,54h,20h,20h,31h,20h,50h,54h,53h,20h,20h,4Eh,4Fh,20h,50h,54h,53h,20h,20h,20h,20h,20h,20h,20h,20h,20h,20h,20h	; row 13
	defb 20h,20h,20h,10h,0Ch,11h,20h,20h,20h,10h,0Ch,11h,20h,20h,20h,20h,20h,20h,20h,20h,20h,20h,20h,20h,20h,20h,20h,20h,20h,20h,20h,20h,20h,20h,20h,20h,20h,20h,20h,20h	; row 14
	defb 20h,20h,20h,15h,24h,16h,20h,20h,20h,15h,23h,16h,20h,20h,20h,20h,20h,20h,20h,20h,20h,20h,20h,20h,20h,20h,20h,20h,20h,20h,20h,20h,20h,20h,20h,20h,20h,20h,20h,20h	; row 15
	defb 20h,20h,20h,12h,0Ch,13h,20h,20h,20h,12h,0Ch,13h,20h,53h,45h,54h,20h,42h,4Fh,4Dh,42h,20h,20h,48h,49h,40h,53h,43h,4Fh,52h,45h,20h,00h,00h,03h,07h,00h,00h,20h,20h	; row 16
	defb 20h,20h,20h,20h,20h,20h,10h,0Ch,11h,20h,20h,20h,20h,10h,0Ch,0Ch,0Ch,0Ch,0Ch,11h,20h,20h,20h,20h,20h,20h,20h,20h,20h,20h,20h,20h,20h,20h,20h,20h,20h,20h,20h,20h	; row 17
	defb 20h,20h,20h,20h,20h,20h,15h,21h,16h,20h,20h,20h,20h,15h,53h,50h,41h,43h,45h,16h,20h,20h,20h,20h,20h,20h,53h,43h,4Fh,52h,45h,20h,00h,00h,00h,00h,00h,00h,20h,20h	; row 18
	defb 20h,20h,20h,20h,20h,20h,12h,0Ch,13h,20h,20h,20h,20h,12h,0Ch,0Ch,0Ch,0Ch,0Ch,13h,20h,20h,20h,20h,20h,20h,20h,20h,20h,20h,20h,20h,20h,20h,20h,20h,20h,20h,20h,20h	; row 19
	defb 20h,20h,20h,20h,20h,20h,44h,4Fh,57h,4Eh,20h,20h,20h,20h,20h,20h,20h,20h,20h,20h,20h,20h,20h,20h,20h,20h,20h,20h,20h,20h,20h,20h,20h,20h,20h,20h,20h,20h,20h,20h	; row 20
	defb 20h,20h,20h,20h,20h,20h,20h,20h,20h,20h,20h,20h,20h,20h,20h,20h,20h,20h,20h,20h,20h,20h,20h,20h,20h,20h,20h,20h,20h,20h,20h,20h,20h,20h,20h,20h,20h,20h,20h,20h	; row 21
	defb 20h,20h,20h,20h,20h,20h,20h,20h,50h,55h,53h,48h,20h,53h,50h,41h,43h,45h,20h,54h,4Fh,20h,53h,54h,41h,52h,54h,20h,47h,41h,4Dh,45h,20h,20h,20h,20h,20h,20h,20h,20h	; row 22
	defb 20h,20h,20h,20h,20h,20h,20h,20h,20h,20h,20h,20h,20h,20h,20h,20h,20h,20h,20h,20h,20h,20h,20h,20h,20h,20h,20h,20h,20h,20h,20h,20h,20h,20h,20h,20h,20h,20h,20h,20h	; row 23
	defb 20h,20h,43h,4Fh,50h,59h,52h,49h,47h,48h,54h,20h,17h,43h,18h,20h,01h,09h,08h,03h,20h,20h,48h,55h,44h,53h,4Fh,4Eh,20h,53h,4Fh,46h,54h,20h,49h,4Eh,43h,19h,20h,20h	; row 24
	defb 0FFh,0FFh,0FFh,0FFh,0FFh,0FFh,0FFh,0FFh,0FFh,0FFh,0FFh,0FFh,0FFh,0FFh,0FFh,0FFh,0FFh,0FFh,0FFh,0FFh,0FFh,0FFh,0FFh,0FFh	; padding to 1024

; ---- game_table @ 2F4F
; 256 x (display code, attribute) for game mode.
game_table:
	defb 20h,70h,21h,70h,22h,70h,23h,70h,24h,70h,25h,70h,26h,70h,27h,70h	; codes 00h-07h
	defb 28h,70h,29h,70h,44h,60h,44h,60h,00h,70h,00h,70h,4Eh,20h,4Dh,20h	; codes 08h-0Fh
	defb 13h,70h,03h,70h,0Fh,70h,12h,70h,05h,70h,02h,70h,0Fh,70h,0Eh,70h	; codes 10h-17h
	defb 15h,70h,13h,70h,44h,60h,44h,60h,00h,70h,00h,70h,43h,20h,43h,20h	; codes 18h-1Fh
	defb 00h,70h,4Fh,70h,0FEh,20h,0FDh,20h,0FEh,20h,0FCh,20h,0FCh,20h,0FDh,20h	; codes 20h-27h
	defb 0F6h,20h,0F9h,20h,0F6h,20h,0F8h,20h,0F8h,20h,00h,70h,00h,70h,00h,70h	; codes 28h-2Fh
	defb 14h,70h,07h,70h,0FAh,20h,0F5h,20h,0F8h,20h,0F5h,20h,0FAh,20h,0F4h,20h	; codes 30h-37h
	defb 0F2h,20h,0F5h,20h,0F8h,20h,0F1h,20h,0F8h,20h,0F3h,20h,0F6h,20h,0F8h,20h	; codes 38h-3Fh
	defb 00h,40h,00h,40h,0F8h,40h,00h,40h,0F6h,40h,00h,40h,0F6h,40h,0F9h,40h	; codes 40h-47h
	defb 0FCh,40h,0FDh,40h,0FEh,40h,0FCh,40h,0FEh,40h,0FDh,40h,0FEh,40h,0FDh,40h	; codes 48h-4Fh
	defb 0F6h,40h,0F8h,40h,0F8h,40h,0F3h,40h,0F8h,40h,0F1h,40h,0F1h,40h,0F5h,40h	; codes 50h-57h
	defb 0FAh,40h,0F4h,40h,0F8h,40h,0F5h,40h,0FAh,40h,0F5h,40h,0FAh,40h,0F7h,40h	; codes 58h-5Fh
	defb 0FEh,10h,0FDh,10h,0F8h,10h,0F4h,10h,0FEh,10h,0FDh,10h,0F8h,10h,0F4h,10h	; codes 60h-67h
	defb 0FEh,10h,0FDh,10h,0F8h,10h,0F4h,10h,0FEh,20h,0FDh,20h,0F8h,20h,0F4h,20h	; codes 68h-6Fh
	defb 0FBh,10h,0F7h,10h,0F2h,10h,0F1h,10h,0FBh,10h,0F7h,10h,0F2h,10h,0F1h,10h	; codes 70h-77h
	defb 0FBh,10h,0F7h,10h,0F2h,10h,0F1h,10h,0FBh,20h,0F7h,20h,0F2h,20h,0F1h,20h	; codes 78h-7Fh
	defb 63h,20h,0F7h,20h,0FDh,20h,0F6h,20h,0F9h,20h,0F6h,20h,0F9h,20h,0F8h,20h	; codes 80h-87h
	defb 0D0h,30h,0D0h,20h,0FEh,10h,0FDh,10h,0FEh,40h,0FDh,40h,0FEh,40h,0FDh,40h	; codes 88h-8Fh
	defb 0CAh,40h,0CEh,20h,00h,70h,00h,70h,00h,70h,00h,70h,00h,70h,00h,70h	; codes 90h-97h
	defb 00h,70h,00h,70h,0F6h,10h,0F9h,10h,0F6h,40h,0F9h,40h,0F6h,40h,0F9h,40h	; codes 98h-9Fh
	defb 0FEh,40h,0FDh,40h,0FEh,40h,0FDh,40h,0FEh,40h,0FDh,40h,0FEh,40h,0FDh,40h	; codes A0h-A7h
	defb 0FEh,40h,0FDh,40h,0FEh,40h,0FDh,40h,0FEh,40h,0FDh,40h,0FEh,40h,0FDh,40h	; codes A8h-AFh
	defb 0F6h,40h,0F9h,40h,0F6h,40h,0F9h,40h,0F6h,40h,0F9h,40h,0F6h,40h,0F9h,40h	; codes B0h-B7h
	defb 0F6h,40h,0F9h,40h,0F6h,40h,0F9h,40h,0F6h,40h,0F9h,40h,0F6h,40h,0F9h,40h	; codes B8h-BFh
	defb 0FAh,20h,0FDh,20h,0FEh,20h,0F5h,20h,0FAh,30h,0FDh,30h,0FEh,30h,0F5h,30h	; codes C0h-C7h
	defb 0FAh,50h,0FDh,50h,0FEh,50h,0F5h,50h,0FAh,60h,0FDh,60h,0FEh,60h,0F5h,60h	; codes C8h-CFh
	defb 0FBh,20h,0F5h,20h,0FAh,20h,0F7h,20h,0FBh,30h,0F5h,30h,0FAh,30h,0F7h,30h	; codes D0h-D7h
	defb 0FBh,50h,0F5h,50h,0FAh,50h,0F7h,50h,0FBh,60h,0F5h,60h,0FAh,60h,0F7h,60h	; codes D8h-DFh
	defb 4Eh,60h,4Dh,60h,0F7h,70h,0FDh,70h,0FDh,70h,0F7h,70h,0F3h,70h,0F6h,70h	; codes E0h-E7h
	defb 0F5h,70h,0F9h,70h,0F1h,70h,0F2h,70h,0F4h,70h,0F8h,70h,0F1h,70h,0F2h,70h	; codes E8h-EFh
	defb 42h,60h,56h,60h,0FEh,70h,0FBh,70h,0FBh,70h,0FEh,70h,0FAh,70h,0F3h,70h	; codes F0h-F7h
	defb 0FCh,70h,0F6h,70h,0F4h,70h,0F8h,70h,0F8h,70h,0F4h,70h,0F4h,70h,0F1h,70h	; codes F8h-FFh

; ---- title_table @ 314F
; 90 x (display code, attribute) used for codes < 0x5A in title mode.
title_table:
	defb 20h,70h,21h,70h,22h,70h,23h,70h,24h,70h,25h,70h,26h,70h,27h,70h	; codes 00h-07h
	defb 28h,70h,29h,70h,44h,60h,44h,60h,78h,70h,00h,70h,4Eh,20h,4Dh,20h	; codes 08h-0Fh
	defb 5Ch,70h,5Dh,70h,1Ch,70h,1Dh,70h,00h,70h,79h,70h,79h,70h,68h,70h	; codes 10h-17h
	defb 69h,70h,2Eh,70h,44h,60h,44h,60h,34h,70h,00h,70h,43h,20h,43h,20h	; codes 18h-1Fh
	defb 00h,70h,0C1h,70h,0C2h,70h,0C3h,70h,0C4h,70h,00h,00h,00h,00h,00h,00h	; codes 20h-27h
	defb 00h,00h,00h,00h,00h,00h,00h,00h,00h,00h,00h,00h,0D0h,20h,0F1h,30h	; codes 28h-2Fh
	defb 20h,70h,49h,70h,0F2h,30h,0F3h,30h,0F4h,30h,0F5h,30h,0F6h,30h,0F7h,30h	; codes 30h-37h
	defb 0F8h,30h,0F9h,30h,0FAh,30h,0FBh,30h,0FCh,30h,0FDh,30h,0FEh,30h,0FFh,30h	; codes 38h-3Fh
	defb 2Ah,70h,01h,70h,02h,70h,03h,70h,04h,70h,05h,70h,06h,70h,07h,70h	; codes 40h-47h
	defb 08h,70h,09h,70h,0Ah,70h,0Bh,70h,0Ch,70h,0Dh,70h,0Eh,70h,0Fh,70h	; codes 48h-4Fh
	defb 10h,70h,11h,70h,12h,70h,13h,70h,14h,70h,15h,70h,16h,70h,17h,70h	; codes 50h-57h
	defb 18h,70h,19h,70h	; codes 58h-59h
	defs 13,0	; unused tail, end of the MZF body (3210h)

program_end:
