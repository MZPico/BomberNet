# BomberNet on the ZX Spectrum Next

**Runs on:** a ZX Spectrum Next, or a Next core on another FPGA board, with
its ESP Wi-Fi module, in its ZX Spectrum 128K mode. It is the same game and
the same tape as for the eLeMeNt ZX: the game tells the two machines apart
and drives the Next's own Wi-Fi.

**Not tried on a Next yet.** The network has been tested on an emulated
Next only, built from the Next's hardware documents, in matches against
every other machine. How NextZXOS loads the tape is still to be checked;
reports from real Nexts are welcome.

**File:** the 128K tape, `bombernet-<version>-zx-spectrum-128k-esp01.tap`.

## Loading

Copy the tape to the SD card and load it with the Next in 128K mode, as
any 128K tape: `LOAD ""` in 128 BASIC. The game uses the 128K's extra
memory (the network driver lives in a RAM bank) and the AY for its tones.

## Network play

The Next's ESP module must be on your Wi-Fi first: join it with the Next's
own Wi-Fi setup, as its manual describes. The module keeps the network,
and the game never asks for it. At start the game selects the ESP's
serial port, sets it to 115200 Bd (the module's AT firmware speed after a
reset) for whichever video mode the Next runs, probes the module, and
resets it if it does not answer.

Without a module answering, the title screen has no NETWORK row.

For a relay of your own, rewrite the tape's relay address with
`tools/set_relay.py` from the sources (see the [setup page](README.md#the-relay)).

## Keys

As on the 48K: Q A O P and SPACE (M when two share the keyboard), the
Sinclair keys, Kempston, Fuller or Cursor joysticks; BREAK leaves a match.

## In an emulator

Not yet. ZEsarUX can connect its emulated Next's serial port to a device on
the host, and CSpect emulates the ESP's AT commands itself; neither has
been tried with the game.
