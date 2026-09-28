/* Sharp MZ-700 / MZ-800 (MZ-700 mode). */
#ifndef PLAT_CONFIG_H
#define PLAT_CONFIG_H
#define PLAT_ROWS 25            /* text rows: the status line has its own row */
#define PLAT_JOY_TYPES 3
#define JOY_800  1              /* MZ-800/MZ-1500 digital sticks on ports F0h/F1h */
#define JOY_1X03 2              /* MZ-700 (and MZ-1500) analogue MZ-1X03 on E008h, timed at VBLK */
#define PLAT_ASM_COMPOSITE 1    /* composite_map and hash_run are assembly (plat_mz.c) */
#define PLAT_ASM_HASH 1
#endif
