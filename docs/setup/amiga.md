# BomberNet on the Amiga

**Runs on:** any Amiga with Kickstart 2.04 or later and 512 KB of chip RAM;
1 MB in all is comfortable. OCS, ECS and AGA alike, PAL or NTSC; a 68000 at
7 MHz is enough. It is an ordinary OS program: it opens a screen, and gives
everything back when it ends.

**Files:** `bombernet-0.3.0-amiga.adf`, a bootable floppy image with the
game and an icon, and `bombernet-0.3.0-amiga`, the program by itself.

## Loading

- Boot the `.adf` (a floppy written from it, a Gotek, or an emulator): its
  startup-sequence starts the game.
- Or copy the program anywhere and start it from Workbench (its icon is on
  the floppy, `bomber.info`) or a Shell.

## Network play

The game uses `bsdsocket.library`, the socket interface every Amiga TCP/IP
stack provides. Start your stack before the game: Roadshow, AmiTCP, Miami,
Genesis, over any Ethernet or Wi-Fi hardware they support. The game needs
no settings of its own.

Without a stack running, the title screen has no NETWORK row.

For a relay of your own, rewrite the `.adf` or the program's relay address
with `tools/set_relay.py` from the sources (see the
[setup page](README.md#the-relay)).

## Keys

| | |
|---|---|
| Player 1 | cursor keys, SPACE fires (RETURN when a second keyboard player is set) |
| Player 2 on the keyboard | W A S D, E fires |
| Joysticks | both ports; the title screen names them by port (the mouse port is port 1) |
| Leave a match, cancel a room code | ESC |

## In an emulator

WinUAE and FS-UAE implement `bsdsocket.library` themselves with the host's
network: enable "bsdsocket.library emulation" (WinUAE: Settings, Host,
Miscellaneous; FS-UAE: `bsdsocket_library = 1`) and the game plays through
the relay with no stack installed. An A500 configuration is fine; a real
Kickstart is not needed, FS-UAE's bundled AROS ROM works.
