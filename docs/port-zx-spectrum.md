# BomberNet on the ZX Spectrum 48K: feasibility and plan

Status 2026-09-29: steps 0 to 6 are done. The game plays locally and over the
network (Spectranet) on the Spectrum build, at the MZ's speed, and an MZ-800
and a Spectrum play in the same match. What remains needs hardware or
an emulator with a Spectranet (step 7); browser play is not planned (step 8).

Rule that shapes everything: every port simulates the MZ field, 40 x 24 logical
cells plus a status line, so that any machine can play any other over the relay.
A port only chooses how a logical cell is drawn.

## 1. Measurements taken from the MZ build (0.1.0)

| Item | Value | Source |
|---|---|---|
| Program code | 26.9 KB | `bomber.map`, CODE section |
| Constant data (tables, logo, strings) | 2.6 KB | rodata |
| Initialised data | 0.3 KB | data |
| Buffers (three 1000-byte screen layers and variables) | 3.4 KB | bss |
| Distinct glyphs drawn (game and title together) | 79 | `game_table`, `title_table` |
| Game frame | 60.8 ms (three TV frames) | emulator, cycle counter |
| Logic per frame, busy 4-player coop | 28 ms average | emulator |
| Screen flush per frame | 19.7 ms, constant | emulator |

The frame is 60 ms, not 20 ms. Consequences: the CPU budget per frame is three
times larger than a 50 Hz game would have, the network carries about 17 messages
per second per device, and the lobby of 0.1.0 shows the input delay too low by
a factor of three (it multiplies frames by 20 ms). That display is to be fixed
on the MZ first (step 0).

## 2. The machine

| Aspect | ZX Spectrum 48K | Effect on the port |
|---|---|---|
| CPU | Z80 at 3.5 MHz | Same speed as the MZ-700; logic cost carries over |
| Free RAM | 42,240 bytes (5B00h-FFFFh), of which 9,472 are contended (slower) | Tight, see the budget below |
| Screen | 256 x 192 bitmap, colour per 8 x 8 cell | 6 x 8 pixel cells: 40 x 6 = 240, 24 x 8 = 192 |
| Status line | No 25th row | Scores are written into the bottom wall row |
| Colour | Attribute grid (8 px) does not match the cell grid (6 px) | Walls and bricks one colour; figures set the attribute where they stand; some spill onto neighbours |
| Frame timing | 50 Hz interrupt | Three interrupts per game frame; simpler than the MZ timer |
| Keyboard | 8 half-rows, several keys at once | Two keyboard players possible |
| Joysticks | Kempston (port 1Fh), Sinclair (as keys) | Third and fourth local player |
| Sound | 1-bit beeper, CPU driven | Short blips inside the frame's slack only |
| Toolchain | z88dk `+zx`, same compiler as the MZ build | Core compiles unchanged; struct layout identical, so state hashes match |

### Memory budget (48K)

| Part | Bytes |
|---|---|
| Game code, MZ platform layer replaced by the ZX one | 27,000 |
| Tables, logo, strings | 2,600 |
| Glyphs, 79 x 8, shifted at draw time | 650 |
| Pre-shifted copies of the ~30 in-game figure glyphs (speed) | 1,500 |
| Buffers | 3,400 |
| Network device in software: state machine, JSON, WebSocket client | 4,000 |
| Network data: 32-frame input window, two line buffers | 1,000 |
| Stack, initialised data | 600 |
| **Total** | **40,750 of 42,240** |

It fits with about 1.5 KB to spare. Reserves if needed: the second compiler
back end of z88dk (usually 10 percent smaller code), a shorter title legend, a
smaller input window.

### Frame budget

| Part | MZ (measured) | ZX (estimate) |
|---|---|---|
| Logic | 28 ms | 28-33 ms (some code in contended RAM) |
| Screen: compare 1000 cells, draw the changed ones | 19.7 ms | 10 ms compare + 0.17 ms per changed cell; 80 cells = 24 ms |
| Network calls | under 1 ms (card) | 2-4 ms (socket poll, JSON) |
| **Frame** | **48 of 60 ms** | **about 55-60 of 60 ms** |

Feasible, with little slack in busy frames. The cell drawing routine and the
compare loop are the two pieces that must be hand-written assembly. Lockstep
runs at the pace of the slowest device, so an overrun slows a match but does
not break it.

## 3. Network options

The MZ game talks to a card that does the networking. The Spectrum has no such
card, so the device logic (room state, input window, JSON, transport) runs on
the Spectrum itself, behind the same ten-command interface the core already
uses. Two transports are candidates.

| | A. Spectranet socket interface | B. ESP8266 modem on a UART |
|---|---|---|
| Hardware | Spectranet (Ethernet, W5100) and Spectranext (WiFi, 2025, same programming interface) | Spectrum Next (built in), ZX-Uno and similar FPGA machines; on real Spectrums only via the sound chip, which the 48K does not have |
| Works on a real 48K | Yes | No |
| Availability | Original Spectranet scarce; Spectranext sold by four retailers | Every Next has it |
| Programming model | BSD-style sockets, library lives in the interface's own paged memory | AT command dialogue over serial, all parsing in the program |
| z88dk support | Socket library ships with the interface's SDK | Present for the Next |
| RAM cost in the program | Low: call stubs only | Higher: command builder, reply parser, receive state machine |
| Cost per message | One send call; receive by polling a socket | Command, wait for prompt, payload, wait for confirmation: 10-20 ms at 115200 baud |
| Fits 17 messages per second | Comfortably | Yes, with little room for more players |
| Relay transport | Plain TCP; the WebSocket handshake and framing are written once in the program (about 600 bytes) | Same, on top of the modem's TCP |
| TLS | Not needed (the relay's card host is plain HTTP); Spectranext adds it | Modem firmware can do it |
| Emulation, desktop | Fuse (Spectranet, static IP only), FuseX (Spectranext, with a debugger) | CSpect via plugin, ZEsarUX with a physical module |
| Emulation, browser | A JSSpeccy fork with Spectranet exists | None found |
| Reach | Small, enthusiast, but the group that buys a network card wants network games | Largest installed base of networked Spectrums |

**Choice: A first.** It is the only one that runs on the target machine, it has
the better programming model and the better test tools. B is added later as a
second transport for the Next; only the lowest layer (open, send line, receive
line) differs, about 1 KB of code.

### Browser play: not planned

Decided 2026-09-29: the Spectrum version is for physical machines with a
Spectranet or Spectranext, and for desktop emulators that emulate one
(FuseX, Fuse). Online play in the browser stays with the original game on
the emulated MZ-800 on mzpico.com; a second browser version would add
nothing for players. A Spectrum and a browser player still meet in the same
rooms, since both use the same relay.

## 4. Architecture changes in the core

1. **Directory split**: `core/` (logic, menus, lobby, lockstep), `platform/mz`,
   `platform/zx`, `platform/host`. The MZ build must stay byte-for-byte
   equivalent in behaviour; the replay tool proves it.
2. **Status line as a platform decision**: own row (MZ) or inside the bottom
   wall (ZX and other 24-row machines).
3. **Network device interface**: today `net.c` does port I/O to the card. It
   becomes a set of ten functions with two implementations: the card (MZ,
   emulators) and the software device (ZX with Spectranet, later ESP). The
   software device is a C version of the firmware's `unicard_net.cpp`.
4. **Logical codes to glyphs**: the translation tables stay; on the ZX they
   yield a glyph number and an attribute instead of a character-ROM code.

## 5. Plan

| Step | Work | Result | Effort |
|---|---|---|---|
| 0 | MZ: lobby shows the delay in real milliseconds (60 per frame) | 0.1.1, done | 1 hour |
| 1 | Core and platform split, status-line hook, network device interface; MZ build verified by replay and lockstep tests | Same game, portable tree; done | 3 days |
| 2 | ZX platform, local play: z88dk build to `.tap`, 6 x 8 cell drawing, 79 glyphs redrawn, attribute rule, keyboard and Kempston, frame sync, beeper | Playable local game, 1-4 players; done (tested in a scripted emulator, tape file loads through the ROM loader) | 8 days |
| 3 | Determinism across platforms: scripted Spectrum emulator run replaying an MZ recording, hashes compared | Proof that both machines simulate identically; done: 4 recordings, 1,796 frames, no mismatch | 2 days |
| 4 | Software network device and WebSocket client on Spectranet sockets; against the reference relay, then production | Spectrum against Spectrum; done: in a scripted emulator with the Spectranet emulated at its programming interface, local relay and production, 2 and 3 seats | 6 days |
| 5 | Cross-platform match: MZ emulator against Spectrum emulator, automated | The goal of the port; done: either machine hosts, 2 and 3 seats, 300 frames with equal hashes | 2 days |
| 6 | Fit and speed on 48K: memory map, contended RAM placement, assembly for the hot loops | Holds 60 ms frames on a 48K; done: title at game pace, stage starts 400 -> 260 ms, 890 bytes spare | 4 days |
| 7 | Real hardware: Spectranext on a 48K | Validated release | needs a unit and a tester |
| 8 | ~~Browser play: Spectrum emulator with the card-style device on the play page~~ | Dropped: browser play stays with the emulated MZ-800 | - |
| 9 | ESP modem transport for the Next | Next owners without a card | 4 days |

Steps 0 to 6 are about five weeks of work and need no hardware. Step 7 is the
first point where a purchase or a volunteer is needed.

## Measured after step 2

| Item | Estimate | Measured |
|---|---|---|
| Program with buffers, no network code | 35.5 KB | 33.3 KB (24000 to 57298), about 8 KB free |
| Logic per frame, busy 4-player game | 28-33 ms | 25.7 ms average, 33 ms worst |
| Screen routine | 24 ms | 18.7 ms fixed plus 1.5 ms per redrawn group; 21 ms average |
| Groups redrawn per frame | up to 80 cells | 1.5 on average, 6 at most |
| Frame | 55-60 ms | about 46 ms, every game frame is exactly three TV frames |

What turned out differently from the plan:

- Almost nothing changes on screen from one frame to the next, so the cost of
  the screen routine is the comparison of the 960 cells, not the drawing. The
  first version took 48 ms; keeping the pointers in registers and moving the
  routine out of contended RAM brought it to 19 ms.
- Link order matters on the 48K: the first 8 KB of the program are in
  contended RAM. Menus and lobby are linked first, the frame loop last.
- The status line is written on the bottom wall: the row becomes a bar in the
  wall's colour with black text, and each player's number is in the player's
  colour. Mixing text cells with wall cells looked
  cluttered.
- Joysticks: Kempston, Fuller and Cursor are selectable, the two Sinclair
  sticks are always there as key sets. Which inputs a joystick kind allows is
  now the platform's decision (`plat_input_allowed`).
- Sound follows the MZ in pitch and length; the MZ blocks during a tone too,
  so the frame budget is the same.
- The ROM interrupt routine is kept (IM 1). It counts the TV frames for the
  frame limiter and costs a keyboard scan every 20 ms. An own handler would
  save about 1 ms per frame and is left for step 6 if needed.
- The MZ emulator ran tests at real-time speed and waited for an audio sync
  that never comes when headless: 7.5 s per game frame. `tools/emu.py` now
  switches it to maximum speed: 0.08 s per game frame, a 500-frame replay in
  about a minute.
- The emulator used for tests is the Python package `zx` driven by
  `tools/zxemu.py`, not Fuse: it is scriptable, headless, and runs a
  500-frame replay in seconds. Fuse or real hardware remain for a final look.

## Measured after step 4

| Item | Result |
|---|---|
| Program with network code and buffers | ends at 64,436; stack peak about 330 bytes; about 760 bytes free |
| Send one input line (build, frame, socket call) | about 1.8 ms |
| Receive and store one input line | about 2.7 ms |
| 4 devices, worst case | about 10 ms of network work per frame on top of about 46 ms |

What turned out differently from the plan:

- sccz80 made the network code about 8 KB, not 5, and the program ran past
  64 KB. The C library's console driver, linked by the start-up code but
  never used, is redirected to nothing (1 KB); the input window is 16 frames
  (a peer is at most 8 ahead); the WebSocket frame header is written in
  front of the line instead of copying the line into a frame buffer.
- Per character work in C is slow on the Z80: the first version spent 31 ms
  per frame on one input line each way. The WebSocket payload is now copied
  in one block, and the input line is read and written by two assembly
  routines; the generic JSON reader remains for the lobby lines.
- Client frames use the mask key 0, so the payload needs no XOR.
- The reference relay writes `"a": 1` and Cloudflare writes `"a":1`; the
  reader skips spaces after keys.
- The server's keep-alive pings carry binary payload: the pong echoes it by
  its length, not by strlen.
- Test infrastructure: the emulator package keeps one keyboard for all
  machines in a process (fixed in `tools/zxemu.py`), and two machines that
  wait for each other over the network must be stepped side by side, never
  run one at a time.
- Not verified on hardware: the Spectranet calls follow the register
  conventions of its ROM sources and are exercised against an emulation of
  that interface, not a real card. Worth checking in FuseX (Spectranext's
  emulator) or on a unit before a release.

## Measured after step 5

`tools/crossplay.py` runs mz800emu (MZPico card emulated, JSON lines over TCP
to the relay) and the Spectrum (Spectranet emulated, WebSocket) against the
same local relay, one TV frame each in turn.

| Match | Result |
|---|---|
| MZ hosts, Spectrum joins, 300 frames | 18 hashed frames equal, no abort |
| Spectrum hosts, MZ joins | equal hashes, no abort |
| Spectrum hosts with 2 local players, MZ joins with 1 | equal hashes, no abort |
| End of a match | the 1000-byte fields of both machines are identical |

Both lobbies measured an input delay of 2 frames (120 ms) against the local
relay.

Found on the way, for step 6: the Spectrum's title screen runs at 6 TV
frames per frame (120 ms), twice a game frame, and its first two frames take
about 20 TV frames each while the whole screen is drawn. Menus react half as
fast as on the MZ; the game itself is not affected.

## Measured after step 6

| Item | Before | After |
|---|---|---|
| Title frame | 6 TV frames (120 ms) | 3 TV frames (60 ms), as the game |
| First title frame after entering it | 20 + 19 TV frames | 4 + 13 TV frames |
| First frame of a stage or deathmatch round | 20 TV frames (400 ms) | 11 TV frames (220 to 260 ms) |
| Program end, stack spare | 64,436, about 760 bytes | 64,306, about 890 bytes |

What was done:

- The title spent its time in C loops that fill the menu box, copy the logo
  and convert text character by character. Block fills and copies
  (memset/memcpy) and the five text routines in Z80 assembly
  (`common/z80_loops.c`, also used by the MZ) fixed it and made the program
  smaller.
- After `clear_buffers` the Spectrum wipes its display in one pass (pixels
  and attributes pushed through the stack pointer) and marks the cells as
  blank, so only non-blank cells are drawn.
- A playfield has about 20 distinct groups of four cells. Groups drawn in
  the current flush are cached (32 entries by a hash of the four cells, a
  generation byte instead of clearing); a repeat is a copy of 24 pixel bytes
  and 3 attributes. On a real field 178 of 230 groups hit the cache.
- The status row is never cached: its colours differ from the same cells
  elsewhere.
- Checked: title, game and status line screens identical to the previous
  build after the same frames, on the Spectrum and on the MZ; all replays
  and the three network pairings pass.

Not done, not needed so far: an own interrupt routine instead of the ROM's
(about 1 ms per frame), a faster glyph renderer (the rest of the 260 ms).
Busy frames can reach 60 ms and more, but only while a sound plays: sounds
block the game, as they do on the MZ.

## 6. Risks

| Risk | Severity | Mitigation |
|---|---|---|
| 48K memory overrun | Medium | Budget above has 1.5 KB spare; smaller back end, shorter title, 128K-only features kept out |
| Busy frames exceed 60 ms | Medium | Assembly for drawing and compare; lockstep tolerates slowdowns |
| Colour spill looks poor | Low to medium | Decide the attribute rule on mock-ups before writing the routine |
| Spectranet paging and interrupts interfere | Medium | No socket calls from the interrupt; frame counter only |
| Socket receive polling is slower than expected | Medium | Measure in step 4 before building on it |
| No Spectranext unit for validation | Medium | FuseX emulates it; find a tester in that community |
| Rights (derivative of Hudson Soft's Bomber Man) | Unchanged | Same position as the MZ version; more visibility on a bigger platform |
