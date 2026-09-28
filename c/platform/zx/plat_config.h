/* ZX Spectrum 48K. */
#ifndef PLAT_CONFIG_H
#define PLAT_CONFIG_H
#define PLAT_ROWS 24            /* the status line is shown on top of the bottom wall */
#define PLAT_JOY_TYPES 4        /* what INPUT_JOY1 is; the Sinclair sticks are key sets */
#define JOY_KEMPSTON 1          /* port 1Fh */
#define JOY_FULLER   2          /* port 7Fh */
#define JOY_CURSOR   3          /* Cursor / Protek / AGF: keys 5 6 7 8 and 0 */
#define PLAT_ASM_COMPOSITE 1    /* common/z80_loops.c */
#define PLAT_ASM_HASH 1
#endif
