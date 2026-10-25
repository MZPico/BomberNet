# BomberNet on the Amstrad CPC

**Runs on:** a CPC 464, 664 or 6128 (64 KB is enough), in Mode 1.

**Files:** `bombernet-0.3.0-amstrad-cpc.dsk`, a disc image with the program
`BOMBER` on it, and `bombernet-0.3.0-amstrad-cpc.cpc`, the program by itself
(an AMSDOS binary).

## Loading

- From the disc image, on a real drive, a Gotek or an emulator: `RUN"BOMBER`.
- From the M4's SD card: copy the `.cpc` file as `BOMBER.CPC` (or any name)
  into a folder on the card, `|CD` to it and `RUN"BOMBER.CPC`.
- Any other mass storage that runs AMSDOS binaries works the same.

## Network play

**An M4 board.** The game uses the M4's TCP sockets through its ROM, so the
M4 must be joined to your Wi-Fi first, as its manual describes (its
configuration on the SD card, or its web page). Nothing in the game to set.

Without an M4, the title screen has no NETWORK row.

For a relay of your own, rewrite the `.dsk` or the `.cpc` file's relay
address with `tools/set_relay.py` from the sources (see the
[setup page](README.md#the-relay)).

## Keys

| | |
|---|---|
| Player 1 | cursor keys, SPACE fires (COPY when a second keyboard player is set) |
| Player 2 on the keyboard | W A S D, E fires |
| Joysticks | two: the second through the first joystick's pass-through connector |
| Leave a match, cancel a room code | ESC |

## In an emulator

CPCemu emulates the M4 with real network connections and plays through the
relay. WinAPE, Caprice, Retro Virtual Machine and the others play locally.
