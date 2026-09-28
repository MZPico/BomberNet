# BomberNet on the ZX Spectrum 48K: feasibility and plan

Status: analysis, 2026-09-28. Nothing here is implemented yet.

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

### Browser play

A browser cannot open raw TCP, so an emulated Spectranet cannot reach the relay
directly. For the play page the same approach as on the MZ is used instead: the
emulator gets a small card-style device on two I/O ports, and the page bridges
it to the relay's WebSocket. The Spectrum program detects which one is present.
That keeps the existing bridge and relay untouched.

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
| 1 | Core and platform split, status-line hook, network device interface; MZ build verified by replay and lockstep tests | Same game, portable tree | 3 days |
| 2 | ZX platform, local play: z88dk build to `.tap`, 6 x 8 cell drawing, 79 glyphs redrawn, attribute rule, keyboard and Kempston, frame sync, beeper | Playable local game in Fuse, 1-4 players | 8 days |
| 3 | Determinism across platforms: scripted Spectrum emulator run replaying an MZ recording, hashes compared | Proof that both machines simulate identically | 2 days |
| 4 | Software network device and WebSocket client on Spectranet sockets; against the reference relay, then production | Spectrum against Spectrum in Fuse | 6 days |
| 5 | Cross-platform match: MZ emulator against Spectrum emulator, automated | The goal of the port | 2 days |
| 6 | Fit and speed on 48K: memory map, contended RAM placement, assembly for the hot loops | Holds 60 ms frames on a 48K | 4 days |
| 7 | Real hardware: Spectranext on a 48K | Validated release | needs a unit and a tester |
| 8 | Browser play: Spectrum emulator with the card-style device on the play page | Spectrum title on the site | 5 days |
| 9 | ESP modem transport for the Next | Next owners without a card | 4 days |

Steps 0 to 6 are about five weeks of work and need no hardware. Step 7 is the
first point where a purchase or a volunteer is needed.

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
