/* Z80 assembly versions of shared C, declared for the shared code that calls
 * them (the calling conventions are z88dk's). Each one exists in C as the
 * reference and is used only where plat_config.h asks for it; see
 * docs/portability.md. */
#ifndef CPU_H
#define CPU_H
#include <stdint.h>
uint8_t fast_input(const char *l) __z88dk_fastcall;   /* netline.c, CPU_ASM_NETLINE */
void fmt_input(void);
#endif
