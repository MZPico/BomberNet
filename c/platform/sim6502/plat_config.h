/* 6502 measurement platform (llvm-mos simulator, tools/sim6502.py): no
 * machine, only the core, a screen flush in C into a code + colour screen
 * as a C64 or an Atari would have it, and a recording to replay. */
#ifndef PLAT_CONFIG_H
#define PLAT_CONFIG_H
#define PLAT_ROWS 25
#define PLAT_JOY_TYPES 3
#define JOY_800  1
#define JOY_1X03 2
#ifdef SIM_ASM
#define PLAT_ASM_COMPOSITE 1    /* cpu/6502/loops.s */
#endif
#endif
