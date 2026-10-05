/* Driver bank: link a normal classic CRT, but enter the API directly.
 * Only the BSS reset below runs at first use; main/CRT start are never called.
 */
#include <stdint.h>
void esp_bank_reset(void) __naked {
  __asm
    EXTERN __BSS_head, __BSS_END_tail
    ld hl,__BSS_head
    ld de,__BSS_head+1
    ld bc,__BSS_END_tail-__BSS_head-1
    ld (hl),0
    ldir
    ld hl,0
    ret
  __endasm;
}
void bank_no_console_stub(void) __naked {
  __asm
    PUBLIC zx_no_console
zx_no_console:
    ret
  __endasm;
}
int main(void) {return 0;}
