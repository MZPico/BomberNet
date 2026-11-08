/* Amiga (docs/port-amiga.md): a 68000, big-endian, structures padded. */
#ifndef PLAT_CONFIG_H
#define PLAT_CONFIG_H
#define PLAT_ROWS 25            /* 25 rows of 8 lines in a 200-line screen */
#define PLAT_JOY_TYPES 2
#define PLAT_HASH_PORTABLE 1    /* the state hash field by field (core/game.c): the 68000 is big-endian */
#define PLAT_RUNAHEAD 1         /* a frame ahead of the agreed one in a match (docs/rollback-plan.md) */
#define NET_BINARY_INPUT 1      /* input lines as binary messages (the relay's bin=1) */
#define NET_PLATFORM "amiga"   /* the relay's statistics: &pf= (docs/net-protocol.md) */
extern volatile uint32_t amiga_ticks;
#define PLAT_CLOCK16 amiga_ticks   /* +1 every TV frame (plat_amiga.c): the match's silence (core/netplay.c) */
extern uint8_t ntsc;            /* 60 Hz (plat_amiga.c) */
#define PLAT_CLOCK16_10S (ntsc ? 600 : 500)
#endif
