# BomberNet on the 128K Spectrum with an ESP-01: plan and progress

Milan "Hood" Stava ported BomberNet to the eLeMeNt / MB03+ with an ESP-01
Wi-Fi module (128K Spectrum, release 19, PR #2). His port builds outside
the main build (SjASMPlus for its loader, Python packing scripts) and puts
its changes to the shared code behind `#ifdef ESP_FAST128`. It is brought
into main here piece by piece, in main's structure: one CMake build, the
shared code without machine-specific branches, the same tests for every
machine. The pieces already taken (credited to him in the commits):
resetting the hit spot and the network abort between matches, the input
delay of 1 frame on good links (in place of his own one-frame lead), the
lives in the player colours, the block-copy frame composition and BREAK to
leave a match.

## Steps

1. **A 128K Spectrum target** (`PLATFORM=zx128`): the game as on the 48K,
   the network driver in RAM bank 6. Done, see below.
2. **The ESP-01 driver** as a `tcp.h` in bank 6 (AT set-up and recovery,
   transparent mode, the receive ring), from his `tcp_esp_stream.c` and
   `tcp_bank128.c`; the driver chosen at build time.
3. **ESP-01 emulation** in `tools/zxemu.py`, from his tests, so that the
   lockstep and cross-play tests include ESP machines.
4. **128K extras**: his AY sound effects.
5. **His assembly versions of game routines**, where a profile shows they
   pay, as `PLAT_ASM_*` for every Z80 machine, each checked by the
   recordings.
6. **Hardware test** on an eLeMeNt, then a release.

## Step 1: the 128K target

The same split as in his port: the game from 24000 in the lower 48K (bank 0
at C000h), the network driver a program of its own at C000h in bank 6.

- `c/platform/zx/bank6.c` and the driver (`tcp_spectranet.c` for now) are
  linked at 49152 as a normal z88dk program whose start-up code never runs.
  `tools/zx128_entries.py` takes the addresses of `bank6_init`, the `tcp_*`
  functions and a signature from its map into `bank6_entries.h`, and
  checks that the module ends below its stack (the top 512 bytes of the
  bank).
- `c/platform/zx/tcp_bank6.c` is the game's `tcp.h`. It is linked first,
  so it stays below C000h. A call copies the arguments to a staging buffer
  at 5D00h-5DBFh (the BASIC loader's area, free once the game runs), pages
  bank 6 in, runs the function on bank 6's own stack, and pages bank 0 and
  the caller's stack back. BANKM (5B5Ch) is kept with port 7FFDh, as the
  ROM pages by it. Before the first call the signature is read from bank 6:
  on a machine without bank 6 the network is just absent.
- `tools/make_zx128_tap.py` writes the tape: `CLEAR 23999`, the game, a
  routine in the game that pages bank 6 in, the driver, a routine that
  pages bank 0 back and starts the game. With RAMTOP at 23999 BASIC has
  little room; a loader that pages with `POKE`/`OUT` itself (99 bytes)
  ended in "4 Out of memory" at the first `LOAD` on a 128K in FuseX, the
  46-byte one loads.
- `tools/zxemu.py` with `bank6=<file>` is a 128K Spectrum: the 128 ROM
  starts, the module goes into page 6, memory access at C000h and above
  follows the page selected. `tools/lockstep_zx.py` takes the builds of
  both machines (`A_BUILD`, `B_BUILD`); a build with a `bank6` file runs
  as a 128K.

Measured (the driver is still the Spectranet's 0.5 KB, so the 128K build
gains nothing yet; the point is the mechanism):

| | 48K | 128K |
|---|---|---|
| game: end of variables | FCE4h | FD31h (bridge, without the driver) |
| free above the game, lowest stack in a match | 465 bytes | 385 bytes |
| bank 6 | | 762 bytes used, 15110 free below its stack |

- 128K host against a 48K joiner and the reverse, local relay: 240 frames,
  15 hashed frames equal, no abort; BREAK between two 128K machines leaves
  the match on both. The recordings replay on the 128K build.
- FuseX `--machine 128`: the tape loads through the 128 BASIC tape loader,
  the game finds the driver in bank 6 and runs. (With the Spectranet, FuseX
  first stops in the Spectranext launcher, so FuseX was run without it;
  the Spectranet calls from bank 6 are covered by `tools/zxemu.py`.)
- The 128K tape is for 128K machines only: on a 48K the driver block
  overwrites the game.
