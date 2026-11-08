/* Commodore 64 (docs/port-c64.md). */
#ifndef PLAT_CONFIG_H
#define PLAT_CONFIG_H
#define PLAT_ROWS 25            /* the text mode's 25 rows */
#define PLAT_JOY_TYPES 2
#define PLAT_ASM_COMPOSITE 1    /* cpu/6502/loops.s */
#define NET_BINARY_INPUT 1      /* input lines as binary messages (the relay's bin=1) */
#define NET_PLATFORM "c64"   /* the relay's statistics: &pf= (docs/net-protocol.md) */
extern volatile uint8_t c64_ticks;
#define PLAT_CLOCK c64_ticks    /* +1 every TV frame (c64.s): the match's silence (core/netplay.c) */
extern uint8_t ntsc;            /* 60 Hz (plat_c64.c) */
#define PLAT_CLOCK_QUARTER (ntsc ? 15 : 12)
#endif
