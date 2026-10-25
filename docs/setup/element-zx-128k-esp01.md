# BomberNet on the eLeMeNt ZX with an ESP-01

**Runs on:** an eLeMeNt ZX board in its ZX Spectrum 128K mode, with the ESP-01
Wi-Fi module on its serial port. This is Milan Stava's port of BomberNet,
brought into the common code; it was written for the eLeMeNt and the MB03+
interface, and only the eLeMeNt has been tried.

**File:** `bombernet-0.3.0-zx-spectrum-128k-esp01.tap`.

## Loading

In 128K mode, `LOAD ""` from the tape image with the board's loader. The
game uses the 128K's extra memory (the network driver lives in a RAM bank)
and the AY for its tones, so the match never stops for a beeper tone.

## Network play

The game drives the ESP-01 with AT commands (`AT+CIPSTART` opens the
connection to the relay). The module has to be joined to your Wi-Fi before
the game starts, with the AT firmware's `AT+CWJAP` from a terminal or the
board's own tools, as the eLeMeNt's documentation describes; the game does
not store Wi-Fi credentials. At start the game probes the module and
recovers it if it is in a bad state.

Without a module answering, the title screen has no NETWORK row.

For a relay of your own, rewrite the tape's relay address with
`tools/set_relay.py` from the sources (see the [setup page](README.md#the-relay)).

## Keys

As on the 48K: Q A O P and SPACE (M when two share the keyboard), the
Sinclair keys, Kempston, Fuller or Cursor joysticks; BREAK leaves a match.
