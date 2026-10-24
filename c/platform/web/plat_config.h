/* The browser (docs/port-web.md): WebAssembly from Emscripten, 32-bit int,
 * little-endian. */
#ifndef PLAT_CONFIG_H
#define PLAT_CONFIG_H
#define PLAT_ROWS 25            /* 25 rows of 8 lines: a 320 x 200 canvas */
#define PLAT_JOY_TYPES 2        /* none, a gamepad (the Gamepad API) */
#define PLAT_HASH_PORTABLE 1    /* the state hash field by field (core/game.c) */
#define NET_BINARY_INPUT 1      /* input lines as binary messages (the relay's bin=1) */
#endif
