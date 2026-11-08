# BomberNet on the ZX Spectrum Next: plan and progress

The Next runs the 128K build (`PLATFORM=zx128`, docs/port-zx128-esp01.md)
in its 128K mode: the same game, the same network driver in RAM bank 6. Its
ESP Wi-Fi module sits behind a UART whose ports, 133Bh and 143Bh, the
eLeMeNt ZX's copied, so Milan Stava's ESP-01 driver talks to it unchanged.
What differs is around the UART, and it is a few branches in that driver.

## Steps

1. **The driver on a Next**: tell the Next apart, select its ESP's UART at
   115200 Bd, its module reset and FIFO reset. Done, see below.
2. **The Next in the test emulator**: `tools/zxemu.py` with
   `esp01='next'`; the lockstep and cross-play tests with Next machines,
   in CI. Done, see below.
3. **A real emulator**: the tape loaded under NextZXOS and matches played
   on a whole emulated Next (ZEsarUX). Done, see below.
4. **Hardware test** on a Next, then the release (the 128K tape under a
   Next name as well, a catalog card, a setup guide).
5. Optional: the Next's 28 MHz (nextreg 07h) gives it the time for the
   run-ahead of docs/rollback-plan.md, the only Z80 machine that has it.
   The 128K tape has no memory left for it (below), so it would be a Next
   build of its own (z88dk's `+zxn`, a `.nex` file).

## Step 1: the driver

From the core's own documents (gitlab.com/SpectrumNext/
ZX_Spectrum_Next_FPGA, `cores/zxnext/ports.txt` and `nextreg.txt`):

- **Which machine.** `bank6.c` asks for the eLeMeNt or the MB03+ first, as
  before, then for a Next: nextreg 7Fh (a user register, through 243Bh and
  253Bh) written 5Ah and A5h must read back each. A 128K, +2 or +3 decodes
  neither port (A1 = 1, A0 = 1); on a Next the register's value is put
  back. A ZX Printer decodes both (A2 = 0) and its motor starts on 5Ah: on
  a machine that is not a Next the check ends by writing 04h, motor off. The
  machine ID (nextreg 00h) is not used: 0Ah on a Next, other values on the
  Next cores of other FPGA boards and in emulators.
- **The UART.** 153Bh bit 6 = 0 selects the ESP's UART (the other is the
  Pi's), with bit 4 the prescaler's top bits 16:14 = 0; 163Bh = 18h, 8 bits,
  no parity, one stop bit, no flow control; nextreg 83h bit 4 keeps the
  UART's ports decoded. 115200 Bd: the prescaler is the system clock / the
  baud rate, the clock from the video timing (nextreg 11h: 28.0 to 33.0 MHz
  for VGA 0 to 6, 27 MHz for HDMI), so 243 to 286, or 234; 143Bh takes its
  bits 6:0, then bits 13:7 with bit 7 set. The eLeMeNt's driver keeps the
  speed it finds; on the Next it is set, as the module's AT firmware starts
  at 115200 after a reset.
- **The status bits** of 133Bh mean what the driver reads them as: bit 0 a
  byte waiting, bit 1 the transmit buffer full (64 bytes), bit 2 the
  receive buffer overflowed (cleared by the read; the driver's error 5).
  The receive buffer holds 512 bytes, the eLeMeNt's 2048.
- **The module's reset.** Where the eLeMeNt switches the module's power
  (ZXi register 5), the Next holds nextreg 02h bit 7, which resets the ESP
  and the expansion bus, and the driver's `es_init` releases it.
- **The FIFO reset.** 163Bh bit 7 empties both FIFOs (the eLeMeNt's ZXi
  register 30h).

The game's program is the same (but for the name, below); bank 6 grows by
259 bytes (11231, about 4.6 KB free below its stack). The eLeMeNt's paths
are unchanged.

## Step 2: the test emulator

`tools/zxemu.py` class `NextEsp`, from the same documents: the nextregs
above (00h, 02h, 11h, 7Fh, 83h), the UART's four ports, the status bits,
a 512-byte receive buffer that counts what it loses, a 64-byte transmit
buffer. The module hears the Spectrum only with the ESP's UART selected,
its ports on, 8-bit frames and a prescaler within 2 % of 115200 Bd at the
video timing's clock (`NEXT_TIMING`, default 0); else what is sent is lost
and counted. The eLeMeNt's ZXi ports read FFh, as on a Next.

- `tools/lockstep_zx.py`: `A_NEXT=1`, `B_NEXT=1` make that machine a Next;
  a byte lost in an ESP's receive buffer now fails the test.
- `tools/crossplay.py`: the machine `next`.
- CI (test.yml): two Nexts with four players on the dm4 keys and the stack
  watermark, two Nexts on HDMI timing, a Next against a 48K Spectranet, an
  eLeMeNt against a Next.

Results, each with 0 mismatches, no abort and no byte lost in an ESP's
receive buffer; the local relay unless noted:

| Match | Frames | Notes |
|---|---|---|
| Next against Next, 4 players (2 + 2) on the dm4 keys | 240 | 139 bytes of stack left above the program |
| Next against Next, HDMI timing (27 MHz clock, prescaler 234) | 240 | |
| Next host, 48K Spectranet joiner, and the reverse | 240 each | |
| Next host, eLeMeNt joiner, and the reverse | 240 each | |
| Next against Next, BREAK after 60 | | both leave; the module back to commands, the Next hosts a new room |
| Next against CPC, both ways (`tools/crossplay.py`) | 160 each | playfields identical |
| Next against MZ-800, both ways | 160 each | playfields identical |
| Next host, 48K joiner, production relay | 200 | the module resolves and connects to api.mzpico.com itself |

The emulation is written from the documents, not from a real Next: the
loading, the module's real timing and replies, and the 512-byte receive
buffer at the relay's real pace are the hardware test's to check (step 4).

## Step 3: a whole emulated Next

ZEsarUX (built from source in `~/src/zesarux`, git 4d86ac8) emulates the
Next with its SD card and NextZXOS, and can bridge its UART to a host
character device; `tools/ci/zesarux-uartbridge.patch` adds the option
`--uartbridge <device>` (otherwise a menu setting). `tools/next_esp.py`
puts an ESP's AT firmware on a pseudo-terminal, connected to the local
relay. `tools/next_zesarux.py host|join` boots the Next from ZEsarUX's SD
image (NextZXOS 2.06H, 2020) with the tape added, through the video mode
test and the welcome into the Browser, opens the tape with the TAP
loader's "128K mode" as a player would, and plays against the 48K of
`tools/zxemu.py`: the Next hosts, or types the 48K's room code.

- The tape loads in 128K mode from the Browser; the title's NETWORK row
  reads OFF and can be changed (not dimmed as NONE): the game found the
  Next, set its UART and the module answered.
- Next hosting, 48K joining: 600 frames, no abort (the game compares the
  hashes every 16 frames; a difference would end it with DESYNC).
- 48K hosting, Next joining by its keyboard: 600 frames, no abort.
- The relay sees `pf=zx-spectrum-next`.

ZEsarUX's UART status has only bit 0 (a byte waiting), its transmit never
busy, and the nextreg 02h reset does not reach the module: those parts are
covered by `tools/zxemu.py` above, not here.

## The machine's name for the relay

The relay's statistics take `&pf=<id>` (docs/net-protocol.md). The 128K
tape is the same file on an eLeMeNt and a Next, so the name is chosen at
run time: bank 6's `bank6_present` writes `&pf=element-zx-128k` or
`&pf=zx-spectrum-next` into 5DAAh-5DBFh, above the staging buffer (which
is now 170 bytes, 5D00h-5DA9h), and the game copies it from there into the
room's URL (`NET_PLATFORM_AT` in plat_config.h). The game program grows by
1 byte (FE76h, 42 bytes under its limit); doing the same in the game
itself cost 57 to 60 bytes, more than it had.
