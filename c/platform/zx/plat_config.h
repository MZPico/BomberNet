/* ZX Spectrum 48K. */
#ifndef PLAT_CONFIG_H
#define PLAT_CONFIG_H
#define PLAT_ROWS 24            /* the status line is shown on top of the bottom wall */
#define PLAT_JOY_TYPES 2
#define JOY_KEMPSTON 1          /* Kempston on port 1Fh (stick 1) and Sinclair keys 1-5 (stick 2) */
#define PLAT_ASM_COMPOSITE 1    /* common/z80_loops.c */
#define PLAT_ASM_HASH 1
#endif
