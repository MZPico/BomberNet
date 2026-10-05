#!/usr/bin/env python3
"""Self-test of tools/cpcemu.py with real Z80 code (assembled with pasmo):
the 300 Hz interrupt, VSYNC, the keyboard scan through PPI and AY, Mode 1
palette and screenshot, and an M4 session the way M4 programs do it (find
the ROM by name, DNS, socket, connect, send, poll, receive) against the
local relay (relay/relay.py on 8765: an HTTP request answered by uvicorn).

  tools/cpcemu_selftest.py [screenshot.png]
"""
import os, subprocess, sys, tempfile
sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
from cpcemu import CPC, KEYS

ASM = r'''
        org 4000h
; ---- interrupts: count at 5000h ----
start:  di
        im 1
        ld sp,0C000h
        ld bc,7F8Dh             ; RMR: mode 1, both ROMs off
        out (c),c
        ld hl,0
        ld (5000h),hl
        ei
        ld hl,0                 ; VSYNC rising edges -> 5002h
        ld (5002h),hl
        ld d,0                  ; previous VSYNC bit
vloop:  ld b,0F5h
        in a,(c)
        and 1
        ld e,a
        xor d
        and e                   ; 1 on a rising edge
        jr z,vsame
        ld hl,(5002h)
        inc hl
        ld (5002h),hl
vsame:  ld d,e
        ld hl,(5000h)           ; until 300 interrupts (one second)
        ld bc,300
        or a
        sbc hl,bc
        jr c,vloop
        ld a,1
        ld (5004h),a            ; first part done
        jr part2

; ---- keyboard: line 5 (SPACE bit 7) and line 9 (joystick) -> 5010h, 5011h ----
part2:  ld a,5
        call kbline
        ld (5010h),a
        ld a,9
        call kbline
        ld (5011h),a
; ---- palette and screen: pens 0..3 black, red, yellow, cyan; stripes ----
        ld bc,7F00h
        ld hl,pal
        ld e,4
pl:     out (c),c               ; select pen
        ld a,(hl)
        out (c),a               ; its colour
        inc hl
        inc c
        dec e
        jr nz,pl
        ld hl,0C000h            ; bytes: pen 0, 1, 2, 3 x 4 pixels, repeating
        ld de,stripes
fill:   ld a,(de)
        ld (hl),a
        inc hl
        inc e
        ld a,e
        and 3
        ld e,a
        ld a,stripes AND 0FFh
        add a,e
        ld e,a
        ld a,h
        or a
        jr nz,fill
; ---- M4: find the ROM, DNS, socket, connect, send, poll, recv ----
        ld d,127
romlp:  ld bc,0DF00h
        out (c),d
        ld bc,7F85h             ; RMR: mode 1, upper ROM on, lower off
        out (c),c
        ld a,(0C004h)
        ld l,a
        ld a,(0C005h)
        ld h,a
        ld a,(hl)
        cp 'M'
        jr nz,romnx
        inc hl
        ld a,(hl)
        cp '4'
        jr z,romok
romnx:  dec d
        jr nz,romlp
        jp fail
romok:  ld a,d
        ld (5020h),a
        ld hl,(0FF02h)          ; response buffer
        ld (5022h),hl
        ld hl,(0FF06h)          ; socket table
        ld (5024h),hl
        ld hl,cmdlookup
        call sendcmd
dnswt:  ld ix,(5024h)
        ld a,(ix+0)
        cp 5
        jr z,dnswt
        ld a,(ix+4)             ; resolved IP into the connect packet (least significant first)
        ld (cip),a
        ld a,(ix+5)
        ld (cip+1),a
        ld a,(ix+6)
        ld (cip+2),a
        ld a,(ix+7)
        ld (cip+3),a
        ld hl,cmdsocket
        call sendcmd
        ld iy,(5022h)
        ld a,(iy+3)
        ld (5026h),a            ; socket number
        ld (csock),a
        ld (ssock),a
        ld (rsock),a
        add a,a
        add a,a
        add a,a
        add a,a
        ld e,a
        ld d,0
        ld hl,(5024h)
        add hl,de
        ld (5028h),hl           ; this socket's entry
        ld hl,cmdconnect
        call sendcmd
cwait:  ld ix,(5028h)
        ld a,(ix+0)
        cp 1
        jr z,cwait
        ld (5027h),a            ; status after connect
        or a
        jp nz,fail
        ld hl,cmdsend
        call sendcmd
rwait:  ld ix,(5028h)
        ld a,(ix+2)
        or (ix+3)
        jr z,rwait
        ld hl,cmdrecv
        call sendcmd
        ld iy,(5022h)
        ld c,(iy+4)
        ld b,(iy+5)
        ld (502Ah),bc           ; bytes received
        push iy
        pop hl
        ld de,6
        add hl,de
        ld de,5100h
        ldir                    ; data -> 5100h
fail:   ld bc,7F8Dh             ; ROM off again
        out (c),c
        ld a,1
        ld (502Fh),a            ; done
done:   jr done

sendcmd: ld bc,0FE00h           ; as in the M4 examples: length, command, parameters
        ld d,(hl)
        inc d
sendlp: inc b
        outi
        dec d
        jr nz,sendlp
        ld bc,0FC00h
        out (c),c
        ret

kbline: ld bc,0F40Eh            ; AY register 14
        out (c),c
        ld bc,0F6C0h            ; select it
        out (c),c
        ld bc,0F600h
        out (c),c
        ld bc,0F792h            ; port A in
        out (c),c
        ld b,0F6h
        or 40h                  ; line, AY read
        ld c,a
        out (c),c
        ld b,0F4h
        in a,(c)
        ld bc,0F782h            ; port A out
        out (c),c
        ld bc,0F600h
        out (c),c
        ret

pal:    db 54h, 4Ch, 4Ah, 53h   ; black, bright red, bright yellow, bright cyan
        org 4400h
stripes: db 00h, 0F0h, 0Fh, 0FFh
cmdlookup: db 12
        dw 4336h
        db "localhost",0
cmdsocket: db 5
        dw 4331h
        db 0,0,6
cmdconnect: db 9
        dw 4332h
csock:  db 0
cip:    db 0,0,0,0
        dw 8765
cmdsend: db 5+req_end-req
        dw 4334h
ssock:  db 0
        dw req_end-req
req:    db "GET / HTTP/1.0",13,10,13,10
req_end:
cmdrecv: db 5
        dw 4335h
rsock:  db 0
        dw 200
'''

IRQ = bytes([0xF5, 0xE5,                      # push af; push hl
             0x2A, 0x00, 0x50, 0x23, 0x22, 0x00, 0x50,   # ld hl,(5000h); inc hl; ld (5000h),hl
             0xE1, 0xF1, 0xFB, 0xC9])          # pop hl; pop af; ei; ret


def main():
    with tempfile.TemporaryDirectory() as d:
        open(f'{d}/t.asm', 'w').write(ASM)
        r = subprocess.run(['pasmo', f'{d}/t.asm', f'{d}/t.bin'], capture_output=True, text=True)
        if r.returncode: sys.exit(r.stderr or r.stdout)
        code = open(f'{d}/t.bin', 'rb').read()
    c = CPC(m4=True)
    c.poke(0x4000, code)
    c.poke(0x38, IRQ)
    c.pc = 0x4000
    c.press('SPACE', 'J0_LEFT', 'J0_FIRE1')
    ok = True
    t0 = c.now()
    while not c.read8(0x5004): c.run_ticks(4000)
    secs, vs = (c.now() - t0) / 4_000_000, c.read16(0x5002)
    print(f'300 interrupts took {secs:.3f} s of CPC time (1 expected); VSYNC edges in it: {vs} (50 expected)')
    ok &= 0.99 <= secs <= 1.01 and 49 <= vs <= 51
    print(f'keyboard line 5: {c.read8(0x5010):08b} (SPACE = bit 7 low), line 9: {c.read8(0x5011):08b} (left = bit 2, fire 1 = bit 5 low)')
    ok &= c.read8(0x5010) == 0x7F and c.read8(0x5011) == 0xDB
    for _ in range(400):
        if c.read8(0x502F): break
        c.run_ticks(40_000)
    n = c.read16(0x502A)
    data = c.read(0x5100, n)
    print(f'M4 ROM found at {c.read8(0x5020)}, response at {c.read16(0x5022):04x}, sockets at {c.read16(0x5024):04x}; '
          f'socket {c.read8(0x5026)}, connect status {c.read8(0x5027)}, received {n} bytes: {data[:40]!r}')
    ok &= c.read8(0x5020) == 6 and data.startswith(b'HTTP/1.')
    png = sys.argv[1] if len(sys.argv) > 1 else None
    if png: c.screenshot(png)
    rows = c.screen_rgb()
    print('screen row 0, first 16 pixels:', [rows[0][x] for x in range(0, 16, 4)])
    ok &= [rows[0][x] for x in (0, 4, 8, 12)] == [(0, 0, 0), (255, 0, 0), (255, 255, 0), (0, 255, 255)]
    print('OK' if ok else 'FAILED')
    sys.exit(0 if ok else 1)


if __name__ == '__main__':
    main()
