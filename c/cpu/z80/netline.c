/* The per-frame input lines of common/netdev_soft.c in Z80 assembly
 * (CPU_ASM_NETLINE in plat_config.h): fast_input reads one received input
 * line into the input window, fmt_input writes ours. The C versions in
 * netdev_soft.c are the reference; this does the same on the variables
 * named there (fi_frame, fi_keys, data, have, s_base, s_nbytes, outbuf).
 * See docs/portability.md. */
#include <stdint.h>
#include "game.h"
#include "cpu.h"


uint8_t fast_input(const char *l) __z88dk_fastcall __naked {
  __asm
    EXTERN _s_base, _data, _have, _s_nbytes, _outbuf, _fi_frame, _fi_keys                     ; HL = the line (nothing may come before the assembly)
fi_c1:
    ld   a,(hl)             ; to the colon after op
    or   a
    jp   z,fi_no
    inc  hl
    cp   ':'
    jr   nz,fi_c1
    call fi_sep
    ld   a,(hl)
    cp   'i'                ; input
    jp   nz,fi_no
    call fi_num             ; frame -> DE
    jp   c,fi_no
    push de
    call fi_num             ; slot -> DE
    pop  bc                 ; BC = frame
    jp   c,fi_no
    ld   a,d
    or   a
    jp   nz,fi_no
    ld   a,e
    cp   4                  ; NET_SLOTS
    jp   nc,fi_no
    ld   (fi_slot),a
fi_c2:
    ld   a,(hl)             ; to the colon after data
    or   a
    jp   z,fi_no
    inc  hl
    cp   ':'
    jr   nz,fi_c2
    call fi_sep
    push hl                 ; HL = hex digits
    ld   hl,(_s_base)       ; frame in [base, base + 16) ?
    ld   a,c
    sub  l
    ld   e,a
    ld   a,b
    sbc  a,h
    jr   c,fi_old           ; before the window: already consumed
    or   a
    jr   nz,fi_old
    ld   a,e
    cp   16                 ; WIN
    jr   nc,fi_old
    ld   a,c
    and  15                 ; index
    ld   (fi_idx),a
    ld   l,a
    ld   h,0
    add  hl,hl
    add  hl,hl
    add  hl,hl
    add  hl,hl              ; index * 16
    ld   a,(fi_slot)
    add  a,a
    add  a,a                ; slot * 4
    ld   e,a
    ld   d,0
    add  hl,de
    ld   de,_data
    add  hl,de
    ex   de,hl              ; DE = destination
    pop  hl
    ld   b,4                ; NET_BYTES
fi_hex:
    ld   a,(hl)
    or   a
    jr   z,fi_mark
    cp   0x22               ; quote
    jr   z,fi_mark
    call fi_nib
    add  a,a
    add  a,a
    add  a,a
    add  a,a
    ld   c,a
    inc  hl
    ld   a,(hl)
    or   a
    jr   z,fi_mark
    call fi_nib
    or   c
    ld   (de),a
    inc  de
    inc  hl
    djnz fi_hex
fi_mark:
    ld   a,(fi_idx)         ; have[index] |= 1 << slot
    ld   e,a
    ld   d,0
    ld   hl,_have
    add  hl,de
    ld   a,(fi_slot)
    ld   b,a
    ld   a,1
    inc  b
fi_sh:
    dec  b
    jr   z,fi_or
    add  a,a
    jr   fi_sh
fi_or:
    or   (hl)
    ld   (hl),a
    ld   hl,1
    ret
fi_old:
    pop  hl
    ld   hl,1               ; handled, outside the window
    ret
fi_no:
    ld   hl,0
    ret

fi_sep:                     ; skip colons, quotes and spaces
    ld   a,(hl)
    cp   ':'
    jr   z,fi_s1
    cp   ' '
    jr   z,fi_s1
    cp   0x22               ; quote
    ret  nz
fi_s1:
    inc  hl
    jr   fi_sep

fi_num:                     ; next number after HL -> DE, carry = none
    ld   a,(hl)
    or   a
    scf
    ret  z
    sub  '0'
    cp   10
    jr   c,fi_n1
    inc  hl
    jr   fi_num
fi_n1:
    ld   de,0
fi_n2:
    ld   a,(hl)
    sub  '0'
    cp   10
    jr   nc,fi_n3
    push hl
    ld   h,d
    ld   l,e
    add  hl,hl
    add  hl,hl
    add  hl,de
    add  hl,hl              ; * 10
    ld   e,a
    ld   d,0
    add  hl,de
    ex   de,hl
    pop  hl
    inc  hl
    jr   fi_n2
fi_n3:
    or   a                  ; carry clear
    ret

fi_nib:                     ; hex digit in A -> 0..15
    cp   'A'
    jr   c,fi_d
    or   0x20
    sub  'a' - 10
    ret
fi_d:
    sub  '0'
    ret

fi_slot: defb 0
fi_idx:  defb 0
  __endasm;
}

/* the input line for frame fi_frame from fi_keys into out */
void fmt_input(void) __naked {
  __asm
    ld   hl,fm_pre
    ld   de,_outbuf+8       ; out = outbuf + WS_HDR
    ld   bc,22
    ldir                    ; {op:input,frame:
    ld   hl,(_fi_frame)
    ld   c,0                ; no digit written yet
    ld   ix,fm_pow
    ld   b,4
fm_dig:
    push bc
    ld   c,(ix+0)
    ld   b,(ix+1)
    ld   a,'0' - 1
fm_sub:
    inc  a
    or   a
    sbc  hl,bc
    jr   nc,fm_sub
    add  hl,bc
    pop  bc
    cp   '0'
    jr   nz,fm_put
    bit  0,c
    jr   z,fm_skip
fm_put:
    ld   (de),a
    inc  de
    ld   c,1
fm_skip:
    inc  ix
    inc  ix
    djnz fm_dig
    ld   a,l
    add  a,'0'
    ld   (de),a
    inc  de
    ld   hl,fm_mid
    ld   bc,9
    ldir                    ; ,data:
    ld   hl,(_fi_keys)
    ld   a,(_s_nbytes)
    ld   b,a
fm_hex:
    ld   a,(hl)
    rrca
    rrca
    rrca
    rrca
    call fm_nib
    ld   a,(hl)
    call fm_nib
    inc  hl
    djnz fm_hex
    ld   a,0x22             ; quote
    ld   (de),a
    inc  de
    ld   a,'}'
    ld   (de),a
    inc  de
    xor  a
    ld   (de),a
    ret
fm_nib:
    and  15
    add  a,'0'
    cp   '9' + 1
    jr   c,fm_n1
    add  a,'a' - '9' - 1
fm_n1:
    ld   (de),a
    inc  de
    ret
fm_pre: defb 123, 34, 111, 112, 34, 58, 34, 105, 110, 112, 117, 116, 34, 44, 34, 102, 114, 97, 109, 101, 34, 58   ; {op:input,frame:
fm_mid: defb 44, 34, 100, 97, 116, 97, 34, 58, 34   ; ,data:
fm_pow: defw 10000, 1000, 100, 10
  __endasm;
}
