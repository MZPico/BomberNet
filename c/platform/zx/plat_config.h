/* ZX Spectrum 48K. */
#ifndef PLAT_CONFIG_H
#define PLAT_CONFIG_H
#define PLAT_NET_NO_ADAPT 1     /* as the host it keeps the lobby's delay (core/netplay.c): no room for the rest */
#define PLAT_ROWS 24            /* the status line is shown on top of the bottom wall */
#define PLAT_JOY_TYPES 4        /* what INPUT_JOY1 is; the Sinclair sticks are key sets */
#define JOY_KEMPSTON 1          /* port 1Fh */
#define JOY_FULLER   2          /* port 7Fh */
#define JOY_CURSOR   3          /* Cursor / Protek / AGF: keys 5 6 7 8 and 0 */
#define PLAT_ASM_COMPOSITE 1    /* cpu/z80/loops.c */
#define PLAT_ASM_HASH 1
#define CPU_ASM_NETLINE 1       /* the input lines of common/netdev_soft.c in cpu/z80/netline.c
                                   (unused with NET_BINARY_INPUT) */
#define NET_BINARY_INPUT 1      /* input lines as binary messages (the relay's bin=1): about 7 bytes, not 50 */
#define PLAT_ASM_TEXT 1         /* print_string, hud_text, title_text* in cpu/z80/loops.c */
#endif
