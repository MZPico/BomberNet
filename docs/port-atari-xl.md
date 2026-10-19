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
   from, then the cell tables from the MZ characters. Done: design D
   (`tools/make_atari_tables.py`).
3. **The platform layer**: display list, character set, flush, keyboard and
   two joysticks, POKEY tones, frame timing, memory map; replays identical,
   then screenshots and play in atari800. Done, see below.
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

## Step 3: the platform layer

`c/platform/atari/` (`plat_atari.c`, `atari.s`, the generated `tables.c`),
built by `tools/build_atari.py` (llvm-mos `mos-atari8-dos-clang -Os`, an
`.xex` for DOS or an emulator, 27 KB, BASIC off). Memory: the program
from $2000 to about $95A0; the screen at $AC00 and the sprites at $B000,
fixed, under the C stack (MEMTOP $BC1F down by 1 KB), so that their
alignment wastes nothing; the build checks that the program ends below
the screen.

Colours: the MZ-700's, as near as the PAL palette has them (measured in
atari800): red $36, magenta $4A (hue 4: the MZ's magenta is pink, hue 5
is violet), pale yellow $EE, cyan $9A; players green $BA, yellow $EC,
white $0E, blue $78.

- Screen, design D: one 40 x 25 screen memory. In a game a display list of
  24 rows of ANTIC mode 4 (the field) and one of mode 2 (the status line);
  a DLI at the end of row 22 moves the four sprites to the status line (row
  23, the wall, has no sprite data, so nothing shows half-way), one at the
  end of row 23 switches font and colours. The title and lobby: the logo
  (rows 0-5) in mode 4, red and magenta as on the MZ, the rest mode 2 text
  (a DLI); the legend's five icons in their MZ colours by the four sprites
  and the missiles as a fifth. Mode 2 cannot colour the menu's text.
- Players: the four hardware sprites (single-line PMG), their shapes taken
  from the codes the core drew at their place, rebuilt only when these
  change. On the status line the same sprites colour each player's lives
  icon and count (`plat_hud_colour`). The player's number before the score
  stays white: it is not next to the icon, and four sprites have no room
  for a second piece each.
- The vertical blank is ours (VVBLKI), not the OS's: the clock, the
  hardware registers from the OS's shadows, the key debounce delay and
  SIO's timer 1, a tenth of the OS's two stages. The OS stays on for the
  keyboard IRQ, BREAK and (step 4) SIO.
- Input: two joysticks (PORTA, TRIG0/1); the keyboard as one player, the
  arrow keys without CTRL, fire SPACE or SHIFT. The keyboard reports one
  key at a time: SPACE pressed while an arrow is held replaces it and stays
  reported after its release. So the direction is the last arrow pressed,
  held while any key is down, and SPACE fires for two frames at each press,
  counted by a keyboard IRQ chained before the OS's; SHIFT is a line of its
  own and fires while held. (atari800 reports no key at all once any key
  is released, so there the walk stops; real machines and Altirra keep
  it.) Text entry from the OS's key code (CH).
- Sound: POKEY voices 1-3, tones that do not stop the game, a voice per kind
  as on the CPC.
- Timing: 3 TV frames (PAL), 4,3,4,3,4 on NTSC (60 ms on average).
- `--test recording.bnr` (`ATARI_TEST`): the recording compiled in and
  replayed from the title on, the state hash compared every game frame. At
  the end the background turns green (identical, no late frame), yellow
  (identical, some frames late) or red (a hash differed), and the status
  line shows, in units of 128 us (two scanlines, 60 ms = 469): late
  frames, average frame work, its flush, the longest frame. `--frames n`
  takes only the first n frames (500 frames do not fit beside the program
  any more; 400 do).
  `CFLAGS_ATARI=-DTEST_HASH_OFF` times a standalone game (no hash, nothing
  checked).

Correct: dm4, coop4, dm2, coop1, coop2s and dm2s replay with identical
hashes in atari800 (800XL, PAL); coop2-score does not fit in memory with the
program.

The CPU is slower than the 1.5 MHz assumed in steps 0-1: ANTIC's character
modes take 40 font bytes every scanline and the names on each row's first,
so about 1.1 MHz is left. The first build took 62 ms a frame (coop4). Three
changes brought it to 41:

- the flush no longer clears the playfield in a game: `composite_frame`
  overwrites rows 0-23 at the start of every game frame anyway (the
  contract in `core/platform.h` says so now; the Z80 machines can drop
  their clearing too);
- our own vertical blank instead of the OS's;
- sprite shapes rebuilt only when they change.

Frame work in atari800 (PAL), average (longest):

| | 4 players (coop4) | 2 players (dm2) |
|---|---|---|
| hash every 16 frames, as in a network match | 40.6 ms (52) | 37.2 ms (24) |
| standalone, no hash | 34.7 ms | 33.7 ms |

The flush is 14 ms of it. Late frames: only the round and stage starts
(map generation), as on the Z80 machines. About 20 ms of the 60 are left
for FujiNet in step 4. (The recordings that hash every frame, coop1, coop2s
and dm2s, take 75-90 ms: a test setting, not a game.)
