# BomberNet on the ZX Spectrum

**Runs on:** a ZX Spectrum 48K or larger (a 128K in 48 BASIC mode too).

**File:** `bombernet-0.3.0-zx-spectrum-48k.tap`.

## Loading

`LOAD ""` and play the tape: a real cassette (mzpico.com's "Tape audio" does
not apply; convert the `.tap` with any tap-to-wav tool), a DivMMC, a
Spectranext's own loader, or any tape-image player. On a Spectranext choose
48 BASIC first.

The game loads at 24000 and keeps clear of the ROM's variables, so it runs
on a plain 48K.

## Network play

- **Spectranet:** the game uses the card's own TCP sockets and its DNS, so
  the Spectranet must be set up as for anything else on it: an address by
  DHCP or a static one, and a name server, in the Spectranet's own
  configuration (its ROM's setup utility). Nothing in the game to set.
- **Spectranext:** the same interface; set up as its manual describes.

Without a Spectranet, the title screen has no NETWORK row.

For a relay of your own, rewrite the tape's relay address with
`tools/set_relay.py` from the sources (see the [setup page](README.md#the-relay)).

## Keys

| | |
|---|---|
| Player 1 | Q A O P, SPACE fires (M when two share the keyboard) |
| Player 2 on the keyboard | the Sinclair joystick keys 6 7 8 9 0; a third set 1 2 3 4 5 |
| Joysticks | Kempston, Fuller, or Cursor (keys 5 to 8 and 0); JOYSTICK on the title screen picks the kind |
| Leave a match, cancel a room code | BREAK (CAPS SHIFT + SPACE) |

## In an emulator

FuseX (the Spectranext's emulator) runs the real Spectranet firmware and
plays through the relay; Fuse with its Spectranet emulation works with a
static address. Other emulators play locally.
