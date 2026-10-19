# BomberNet on the Atari XL/XE: plan and progress

The first port to a processor other than the Z80, chosen before the C64
(see `docs/portability.md` for the rules it follows):

- CPU: 6502 at 1.77 MHz, about 1.5 MHz left in a text mode after ANTIC's
  display DMA; the C64 has 0.94 MHz.
- Network: FujiNet, one widespread Wi-Fi device, TCP through its `N:` device;
  it can be emulated (FujiNet-PC with Altirra or atari800).
- A strong scene in Czechia and Poland.

## Steps

0. **Feasibility on the 6502**: the core built with llvm-mos, recordings
   replayed in its simulator, frames timed. Done, see below.
1. **6502 helpers** (`c/cpu/6502/`) and the screen flush in assembly;
   measure again, with the network code (`netdev_soft.c`, `ws.c`) against a
   relay inside the program. Done, see below.
2. **The screen**: 40 x 25 cells (a display list for the 25th row), colours
   per cell against hardware sprites for the players; mock-ups to choose
   from, then the cell tables from the MZ characters.
3. **The platform layer**: display list, character set, flush, keyboard and
   two joysticks, POKEY tones, frame timing (PAL: 3 TV frames; NTSC: a POKEY
   timer, 59.9 ms), memory map; replays identical, then screenshots and play
   in atari800 / Altirra.
4. **The network**: `tcp.h` over FujiNet `N:TCP`, emulated in a test harness;
   its cost per frame (the serial bus), lockstep and cross-play tests,
   production relay, FujiNet-PC.
5. **Hardware test, release.** Then the C64 on the same 6502 work.

## Step 0: the core on a 6502

`tools/sim6502.py <recording>` builds the core with llvm-mos (SDK 23.3.0,
`mos-sim-clang -O2`) for its 6502 simulator (`mos-sim`) with the measurement
platform `c/platform/sim6502/`: the recording is compiled in, every game
frame's state hash is compared with it, and each frame's work (from the
start of the flush to the next frame sync) is counted in CPU cycles
(`clock()`, the simulator's cycle counter). The flush is C: each changed
cell's code and colour into screen and colour memory, as a C64 or an Atari
would take them. Options: `--hash-off` (a standalone game), `--profile`
(cycles per function and game frame), `CFLAGS6502=-DFLUSH_NONE` (the frame
without the flush).

Correctness: the core compiles unchanged with a third compiler and replays
every recording with identical hashes: coop1, coop2s, coop4, dm2, dm2s, dm4
and 1500 frames of coop2-score, 0 mismatches. The program (core, MZ tables,
a recording, no network) is 41-49 KB.

Time per game frame, standalone, 4 players (dm4; coop4 and 1 player alike):

| | cycles | Atari (1.5 MHz) | C64 (0.94 MHz) |
|---|---|---|---|
| all, C flush | 110,000 | 74 ms | 118 ms |
| without the flush | 82,000 | 55 ms | 87 ms |

Where the cycles go (profile, 4 players, without the flush):

| | cycles per frame |
|---|---|
| `memcpy`: the playfield copy, 960 bytes (46 cycles a byte) | 44,600 |
| `memset`: the measurement's clearing of the draw buffer, 1000 bytes | 31,300 |
| all the game logic together | about 10,000 |

The flush in C adds about 60,000 (60 cycles a cell). So the game logic is
cheap on the 6502 (llvm-mos compiles it well); the frame is the library's
generic byte loops (16-bit counters, about 30-46 cycles a byte) and the C
flush over all 1000 cells. Writing the copy as a C loop with an 8-bit index
does not help: LLVM turns it back into a `memcpy` call (57 ms, slower).
llvm-mos's `memcpy` and `memset` are weak symbols, so a port can supply its
own: page-unrolled 6502 loops of about 10 cycles a byte, a per-CPU helper
under the rules, with no change to the core.

Estimate with the helpers (step 1 measures it): copy about 10,000, an
assembly flush of about 12-15 cycles a cell 15,000, logic 10,000:
about 37,000 cycles, 25 ms on an Atari and 40 ms on a C64 for a standalone
4-player frame. A network match adds the hash (measured: about 5,700) and
the network code and device, so the Atari has headroom and the C64 will be
tight, which confirms the Atari first. A core change that tracks changed
cells instead of copying and comparing the whole screen every frame would
help every machine further, but is not needed for the Atari.

## Step 1: 6502 assembly where the cycles were, the network code measured

- `c/cpu/6502/loops.s`: `composite_frame` (the playfield copy, about 11
  cycles a byte) and `composite_map`, under `PLAT_ASM_COMPOSITE` as on the
  Z80; the C in `core/video.c` stays the reference. Not a replacement of
  the library's `memcpy`: that would tie us to llvm-mos's calling
  convention for library routines, these take no arguments.
- `c/platform/sim6502/flush.s`: the measurement platform's flush in
  assembly, as a real port's would be: four quarters of 250 cells with
  absolute,x, the cell tables split into 256-byte code and colour halves
  (`sim_codes`, `sim_attrs`), about 25 cycles a cell.
- `c/platform/sim6502/tcp_fake.c`: a relay inside the program behind
  `tcp.h` (the WebSocket handshake, `create`, `ready`, and for each of our
  input lines the other slots' lines for that frame). With `SIM_NET` the
  platform opens a room through the real `net_detect`/`net_create`/
  `net_ready` and does a network frame's work every game frame: `net_send`
  of our keys, the other machines' lines through the WebSocket parser,
  `net_poll`, `net_hash`.
- `tools/sim6502.py` with `CFLAGS6502="-DSIM_ASM"` (the assembly),
  `-DSIM_NET -DSIM_SLOTS=n` (the network with n machines), `-Os` for size.

Correct: with the assembly, dm4, coop4, dm2s and 1500 frames of coop2-score
replay with 0 mismatches.

Busy 4-player deathmatch (dm4), median (90th percentile) of a frame's work:

| | Atari (~1.5 MHz) | C64 (0.94 MHz) |
|---|---|---|
| standalone, `-O2` | 27.6 ms (29.7) | 44.1 ms (47.5) |
| standalone, `-Os` | 29.4 ms (32.0) | 46.9 ms (51.1) |
| network, 2 machines, `-Os`, hash on | 33.6 ms (35.5; 99th 40.2) | 53.7 ms (56.6; 99th 64.2) |
| network, 4 machines, `-Os`, hash on | 34.0 ms (35.9; 99th 40.6) | 54.3 ms (57.3; 99th 64.8) |

The network figures hold the state hash (about 3.8 ms on the Atari) and
the network code, which is only about 1 ms on the 6502 (on the Z80, with
sccz80 and the Spectranet calls, 8 ms); not yet the device: FujiNet's
serial bus is step 4. The Atari has about 25 ms of a 60 ms frame left for
it, the C64 about 5.

Size (llvm-mos, per source file): the core and the network code are about
46 KB of code at `-O2` (`netdev_soft.c` 14.5 KB), 30 KB at `-Os`, plus
about 3 KB of tables and constants and 4 KB of variables: about 37 KB at
`-Os`, comparable with the Z80 builds (40 KB). That fits an Atari XL/XE
with BASIC off ($0700-$BFFF, less the screen, character set and display
list); the RAM under the OS ROM is there if needed. The build will be
`-Os`, 2 ms slower than `-O2`.
