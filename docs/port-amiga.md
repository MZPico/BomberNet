# BomberNet on the Amiga: plan and progress

The first 68000 port (`docs/portability.md` has the rules; the core's
big-endian hashing, `PLAT_HASH_PORTABLE`, is already tested on PowerPC).

- CPU: 68000 at 7.09 MHz (PAL; 7.16 NTSC), 16/32-bit: the busiest frame
  is expected far under the 60 ms the Z80 and 6502 machines nearly fill.
- Screen: a bitmap, 320 x 256 on PAL (320 x 200 NTSC), 4 bitplanes = 16
  colours: 40 x 25 cells of 8 x 8 pixels drawn from pre-rendered glyphs, as
  the CPC port does, any colour a cell.
- Network: `bsdsocket.library`, the one socket API every Amiga TCP/IP
  stack provides (Roadshow, AmiTCP, Miami, Genesis) over any hardware
  (Plipbox, X-Surf, A2065, PCMCIA Ethernet and Wi-Fi on the A600/A1200);
  FS-UAE and WinUAE implement it themselves with the host's network.
- Requirements: Kickstart 2.04 or later (the stacks need it), 512 KB of
  chip RAM and 512 KB more recommended, any Amiga (OCS, ECS, AGA).

## Steps

0. **Measure first**: the toolchain; the core built for the 68000; the
   screen (an Intuition screen, the cell renderer); a recording replayed in
   FS-UAE with the hashes checked and the frames timed. Results written to
   a host directory mounted as an Amiga drive.
1. **The platform layer**: keyboard (two key sets, the room code's
   letters), joysticks, Paula tones, PAL/NTSC frame timing, memory; the
   title and lobby; play in FS-UAE.
2. **The network**: `tcp.h` over `bsdsocket.library`; the lobby's delay;
   cross-play with every machine through the local relay (FS-UAE's own
   `bsdsocket` emulation), then the production relay.
3. **Release**: an executable plus an ADF; `set_relay.py` on the executable
   (a plain file: the field is contiguous) and the ADF; hardware testers;
   the README and the catalog.

## Design

**An OS program, not a takeover.** The game runs from Workbench or the
Shell on a screen of its own and leaves the OS running: the TCP/IP stack
is an OS process and `bsdsocket.library` needs multitasking. So:
- the screen is an Intuition custom screen (320 x 200 of 4 bitplanes; on
  PAL 320 x 256 with the field in the top 200 lines or centred) with a
  borderless backdrop window for input events;
- the frame clock is a vertical-blank interrupt server (`AddIntServer`,
  `INTB_VERTB`) counting TV frames: a frame is 3 PAL or 4,3,4,3,4 NTSC
  ones, as on the other machines; the game waits for it with a busy loop
  or `WaitTOF`;
- the keyboard comes as raw key events (IDCMP `RAWKEY`) from the window:
  the matrix state is kept from the down/up events, so several keys at
  once work as on the other machines (cursor keys and SPACE; W A S D and
  E; letters, DEL and ESC for the room code);
- the joysticks are read from the hardware (`JOY0DAT`/`JOY1DAT`, the CIA
  fire bits), as most Amiga games do, port 2 first;
- the tones go through Paula: one channel (allocated through
  `audio.device` once, then the registers), a looped square-wave sample
  at the tone's period, switched off by the vertical-blank server after
  the tone's length, as on the C64 (the MZ's lengths);
- memory: the screen's bitmap in chip RAM (allocated by Intuition), the
  program anywhere; no fixed addresses.

**The screen.** Each logical cell is one of the MZ's 8 x 8 glyphs in one
of 16 colours: the glyph's 8 bytes go to the four bitplanes (a byte to
each plane where the colour has the bit set, zero where not), 32 byte
writes a cell, by the CPU: the flush of a few changed cells a frame costs
nothing measurable, a stage's full redraw about 32 KB of writes, a few
milliseconds. The blitter is not needed. The palette: black, the MZ's
seven inks, the four players' own colours (as on the C64), and spares.
Tables from `tools/make_amiga_tables.py` as for the other machines.

**The network.** `tcp.h` over sockets: `gethostbyname` at `tcp_open`,
`socket`/`connect`, the socket set non-blocking (`IoctlSocket FIONBIO`),
`send`, `recv` (0 bytes with `EWOULDBLOCK` is "nothing"; a closed
connection is -1), `CloseSocket`. The stack receives in the background,
so a message is taken by the first poll after it arrives (the Atari's and
the C64's fixed behaviour). The WebSocket client and the room protocol
are shared, with binary input lines. Without a stack running (no
`bsdsocket.library`), the game plays locally.

**The toolchain.** Either bebbo's gcc for AmigaOS (`m68k-amigaos-gcc`,
gcc 6, built from source) or vbcc with the NDK (binary releases). Step 0
takes whichever builds here within the hour; the core compiles with
sccz80, llvm-mos and gcc already, so either should do.

**The test bench.** FS-UAE on the invisible X display, a host directory
mounted as a hard drive (`DH1:`) for recordings and results, its
`bsdsocket.library` emulation for the network, screenshots by `x11grab` as
with Altirra. FS-UAE has no remote monitor like VICE's: test builds are
driven by compiled-in recordings (the hashes checked, the frames timed in
the vertical-blank count and the CIA timers) and by settings preset in
the build (hosting a room on SPACE, as the Atari's `NETTEST_HOST`), and
write their figures to `DH1:`. A Kickstart ROM is needed: 3.1 ideally;
FS-UAE's free AROS replacement ROM is tried first.

**Distribution.** The executable (runs from any disk or hard drive, on
Workbench by icon or from the Shell) and a bootable ADF with it and a
`startup-sequence` for floppy-only Amigas; with a TCP/IP stack already
running for a network match.

## Risks and open questions

- Kickstart 1.3 machines (an unexpanded A500 of 1987-1990) have no
  `bsdsocket` stack: they play locally only. Stated, not solved.
- The vertical-blank server and the hardware joystick reads are the usual
  Amiga practice but not strictly OS-clean; a stack that needs the CPU
  (Roadshow is light) shares it with the game: the frame time with the
  network is measured in step 2.
- The AROS ROM may lack something the test bench needs (Intuition
  screens, `bsdsocket` hooks): then a real Kickstart image is needed.
- Toolchain build time and quirks (bebbo's gcc is a large build).
- The Atari ST could follow on the same toolchain family and renderer,
  if a network device worth supporting appears.
