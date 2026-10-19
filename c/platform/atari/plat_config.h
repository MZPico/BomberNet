/* Atari 800XL/130XE (64 KB), the screen of design D (docs/port-atari-xl.md). */
#ifndef PLAT_CONFIG_H
#define PLAT_CONFIG_H
#define PLAT_ROWS 25            /* 24 field rows in ANTIC mode 4, the status line in mode 2 */
#define PLAT_JOY_TYPES 2
#define JOY_ATARI 1             /* the two joystick ports */
#define PLAT_ASM_COMPOSITE 1    /* cpu/6502/loops.s */
#define NET_BINARY_INPUT 1      /* input lines as binary messages (the relay's bin=1): ~7 bytes on FujiNet's bus, not ~50 */
#endif
