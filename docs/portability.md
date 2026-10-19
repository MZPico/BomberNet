# Portability: one C game, many machines

BomberNet runs on Z80 machines today (Sharp MZ-800, ZX Spectrum 48K and
128K, Amstrad CPC). Ports to other processors are planned: the 6502 (Atari
XL/XE, Commodore 64) and the 68000 (Atari ST, Amiga). These rules keep the
game itself in one C source for all of them, and keep each machine's
speed-ups from turning into copies of the game.

## The rules

1. **The core is portable C.** `c/core` (the game) and `c/common` (the
   network device in software, the WebSocket client) are a conservative
   C99 subset that sccz80 (z88dk), cc65, llvm-mos, Oscar64 and m68k gcc all
   handle: fixed-width types from `<stdint.h>`, no assembly, no
   compiler-specific keywords. Integer width does not matter: the PC
   simulator (32-bit `int`) and the Z80 builds (16-bit `int`) replay the
   same recordings with the same hashes.
2. **Machine code lives in two places only.**
   - `c/platform/<machine>/`: what is the machine's own (screen, keys,
     sound, timing, the network hardware behind `tcp.h`).
   - `c/cpu/<processor>/`: faster versions of shared C for one processor
     (`c/cpu/z80/loops.c`, `netline.c`; a 6502 port would add `c/cpu/6502/`).
     Each one keeps its C version in the shared code as the reference, and a
     machine uses it only when its `plat_config.h` asks for it
     (`PLAT_ASM_COMPOSITE`, `PLAT_ASM_HASH`, `PLAT_ASM_TEXT`,
     `CPU_ASM_NETLINE`). Their declarations, with the compiler's calling
     conventions, go in `c/cpu/<processor>/cpu.h`, not in the shared headers.
3. **Only plain loops become assembly, never game rules.** Copying the
   playfield, hashing bytes, printing digits, building and reading the
   network's input lines: these do not change when the game changes. Bombs,
   players and enemies stay in C, as their rules (the original detects
   collisions while drawing) would otherwise exist twice per processor.
4. **Optimise in C first.** A change in C helps every machine, the slow
   6502 ones most (a C64 runs at 1 MHz). Measure (`tools/lockstep_zx.py`
   with `PROF=`), change the C, measure again; assembly only for what is
   still too slow and allowed by rule 3.
5. **Byte order and layout are never left to chance.** What machines
   compare (the state hash) or exchange (recordings, network messages) is
   defined byte by byte: messages are text, recordings are little-endian by
   construction, and the hash takes the records in a canonical order with
   16-bit values low byte first. On little-endian machines without padding
   (the Z80, the 6502, the PC) the records are hashed as they lie in memory,
   which compile-time size checks in `core/game.c` guarantee; a machine
   that stores them otherwise (the 68000: big-endian, structures padded to
   even sizes) sets `PLAT_HASH_PORTABLE`.
6. **Test it on a big-endian CPU.** `tools/test_portable.sh` builds the PC
   simulator natively and for 32-bit PowerPC (big-endian, as a stand-in for
   the 68000) and replays `tests/recordings` on both. Without
   `PLAT_HASH_PORTABLE` the PowerPC build fails at the first hash after a
   score changes (`coop2-score.bnr`): the check notices.

## What a new machine needs

- `c/platform/<machine>/`: `plat_config.h`, the functions of
  `core/platform.h` (screen flush from the 40 x 25 grid of logical cells,
  keys, joysticks, tones, frame timing), the tables from logical cells to
  the machine's characters or pixels (generated, like
  `tools/make_zx_tables.py`), and a `tcp.h` for its network hardware.
- The screen is a grid of logical cells, so a machine with a character
  mode (C64, Atari 8-bit) maps cells to characters and colours; a bitmap
  machine (ST, Amiga, like the CPC) renders cells from pre-drawn glyphs.
- Network hardware is behind `tcp.h` (connect, send, receive, close); the
  WebSocket client and the room protocol are shared. Candidates: FujiNet
  (Atari 8-bit), WiC64 or an Ultimate cartridge (C64), bsdsocket.library
  (Amiga), a network adapter with STiNG (ST).
- A frame is 60 ms on every machine (three PAL TV frames); in a network
  match all machines keep that pace, so a new machine must do a busy
  4-player frame in under 60 ms. On the Spectrum (3.5 MHz Z80) it is about
  50 ms; a 1-2 MHz 6502 needs a good compiler and rule 4.
- The build: a CMake platform section, with the compiler's toolchain.
