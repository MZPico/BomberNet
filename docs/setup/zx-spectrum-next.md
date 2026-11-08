# BomberNet on the ZX Spectrum Next

**Runs on:** a ZX Spectrum Next, or a Next core on another FPGA board, with
its ESP Wi-Fi module, in its ZX Spectrum 128K mode. It is the same game and
the same tape as for the eLeMeNt ZX: the game tells the two machines apart
and drives the Next's own Wi-Fi.

**Not tried on a real Next yet.** The game has been tested on emulated
Nexts only: loaded under NextZXOS and played over the network in a whole
emulated Next, and matched against every other machine on a model built
from the Next's hardware documents. Reports from real Nexts are welcome.

**File:** the 128K tape, `bombernet-<version>-zx-spectrum-128k-esp01.tap`.

## Loading

Copy the tape to the SD card, open it in NextZXOS's Browser and choose
**1, 128K mode** in the TAP loader's menu. The game uses the 128K's extra
memory (the network driver lives in a RAM bank) and the AY for its tones.

## Network play

The Next's ESP module must be on your Wi-Fi first: join it with the Next's
own Wi-Fi setup, as its manual describes, so that the module keeps the
network and joins it again by itself after a reset (the AT firmware's
saved network and automatic connection, the usual setting). The game never
asks for the network, and when it resets an unresponsive module it relies
on that. At start the game selects the ESP's
serial port, sets it to 115200 Bd (the module's AT firmware speed after a
reset) for whichever video mode the Next runs, probes the module, and
resets it if it does not answer.

Without a module answering, the title screen shows the NETWORK row dimmed, as NONE.

For a relay of your own, rewrite the tape's relay address with
`tools/set_relay.py` from the sources (see the [setup page](README.md#the-relay)).

## Keys

As on the 48K: Q A O P and SPACE (M when two share the keyboard), the
Sinclair keys, Kempston, Fuller or Cursor joysticks; BREAK leaves a match.

## In an emulator

ZEsarUX runs it under NextZXOS and plays through the relay when its Next's
serial port is connected to a Wi-Fi module emulated on the host
(`tools/next_esp.py` from the sources, with ZEsarUX patched as
`docs/port-zx-next.md` describes). CSpect emulates the ESP's AT commands
itself; it has not been tried.
