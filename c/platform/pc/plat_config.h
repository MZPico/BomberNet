/* IBM PC, DOS (docs/port-pc.md): an 8086, 16-bit int, little-endian. */
#ifndef PLAT_CONFIG_H
#define PLAT_CONFIG_H
#define PLAT_ROWS 25            /* 25 rows of 8 lines: the 200-line screen */
#define PLAT_JOY_TYPES 2
#define PLAT_HASH_PORTABLE 1    /* the state hash field by field (core/game.c): Watcom pads structures */
#define NET_BINARY_INPUT 1      /* input lines as binary messages (the relay's bin=1) */
#endif
