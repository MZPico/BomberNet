/* Amstrad CPC 464/664/6128, Mode 1. */
#ifndef PLAT_CONFIG_H
#define PLAT_CONFIG_H
#define PLAT_ROWS 25            /* 40 x 25 cells of 8 x 8 pixels: the status line has its own row */
#define PLAT_JOY_TYPES 2
#define JOY_CPC 1               /* the joystick port (matrix line 9), a second stick on line 6 through a splitter */
#define PLAT_ASM_COMPOSITE 1    /* common/z80_loops.c */
#define PLAT_ASM_HASH 1
#define PLAT_ASM_TEXT 1         /* print_string, hud_text, title_text* in common/z80_loops.c */
#endif
