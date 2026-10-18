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
   `tcp_esp_fast.c`; the driver chosen at build time. Done, see below.
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

## Step 2: the ESP-01 driver

`PLATFORM=zx128` now builds with the ESP-01 driver in bank 6
(`-DZX128_NET=esp01`, the default); `-DZX128_NET=spectranet` keeps the
Spectranet one, which the test harness runs until the ESP-01 is emulated
(step 3).

- `c/platform/zx/tcp_esp01_at.c` is his `tcp_esp_fast.c` (the AT command
  layer: power, probe and recovery of the module, `AT+CIPSTART`, passive
  receive) and `tcp_esp01.c` his `tcp_esp_stream.c` (after the connection,
  the ESP's transparent mode with a 4096-byte receive ring in bank 6),
  unchanged but for their headers. The game side is the bridge of step 1;
  it asks for at most 192 bytes per receive, as his bridge did.
- Bank 6: 10972 bytes used, 4900 free below its stack (his release: 10902;
  the difference is the machine check below).
- His ring is meant to be 4096-aligned, but the assembler leaves `stream_rx`
  unaligned (D9FDh in his release maps, DA96h here). The code uses the 4096
  bytes from `stream_rx & FF00h`, which works because the bytes before
  `stream_rx` are the alignment padding; `tools/zx128_entries.py` now
  checks that on every build.
- Before the driver touches the UART, `bank6_present` (bank6.c) asks
  whether the machine is an eLeMeNt ZX or has an MB03+, the way the Wi-Fi
  BIOS 2.0 does (below). Without that, a 128K without the module waited 15
  seconds for the title in `tools/zxemu.py` (his recovery: 10 s trying to
  send `+++` to a UART that reads busy, then a power cycle of the module),
  as his release does; now 0.13 s, and the network is NONE.
- The recordings replay on the ESP-01 build; the Spectranet variant passes
  the 128K lockstep test.

### The hardware (eLeMeNt ZX Programmer's Reference 1.28, 2025-11-17)

From https://element.zxfiles.net/ (documents, "Serial and Wi-Fi
communication"):

- 143Bh receive, 133Bh transmit; reading 133Bh is the status: bit 0 a byte
  waiting, bit 1 still transmitting, bit 2 the receive FIFO full (2048
  bytes, data are lost when it is full), bit 3 256 bytes or more, bit 4
  1024 bytes or more. Bits 5-7 are not described.
- Writing 143Bh sets the speed (an index: 0 = 115200, the default after a
  reset, up to 23 = 11520000). The driver keeps whatever speed is set; the
  module and the UART must match (115200 in his hardware tests).
- ZXi register g_zxi_030 (select 7C3Bh, data 7D3Bh): write 01h erases the
  receive FIFO, as the driver's `es_drain` does.
- e_zxi_039 (select 783Bh, data 793Bh) chooses what the internal UART is
  wired to: 00 internal USB-UART (MB03+ mode), 01 MB03+, 02 ZX-Uno, 03
  ZX128-AY.
- The eLeMeNt and MB03+ Wi-Fi modules differ but their software is "100%
  compatible", and the specification follows the ZX Spectrum Next's (which
  later changed 133Bh).
- The Wi-Fi BIOS 2.0 by Busy and Hood (https://hood.speccy.cz/dwnld/
  Wifi_Bios_2.0.zip, the same source as `ports/esp01/WifiBios20_MB_EL.a80`
  in PR #2) has the rest: 703Bh/713Bh is the MB03+'s ZXi register pair,
  register 5 bit 0 the module's power (`turn_sub`, `turnon_wifi`), which
  serves the eLeMeNt's module too; register FFh tells the machines apart:
  read at 773Bh after 703Bh it is 3xh with an MB03+, read at 793Bh after
  783Bh it is E1h on an eLeMeNt ("E", its ID text).
- Set up on the eLeMeNt: Expansion pack B (e_zxi_039) on "UART MB03+"
  (from the documentation of Hood's Sarah FTP client: the Wi-Fi programs
  do not work otherwise). With an MB03+ and an eLeMeNt together both
  modules are on the same ports and the MB03+'s is the one used.
- The module takes at most 2048 bytes per send (Sarah); the bridge sends at
  most 192 at a time.
- The ESP must already be on the Wi-Fi network (Hood's Wi-Fi Setup program);
  the game does not set credentials.
- Emulator named in the documents: LnxSpectrum (https://www.ilnx.cz/lnxsp/),
  with the ZXi ports; whether it emulates the UART/ESP is not stated.
