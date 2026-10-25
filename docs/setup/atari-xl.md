# BomberNet on the Atari XL/XE

**Runs on:** an Atari 800XL, 65XE, 130XE or any XL/XE with 64 KB, PAL or NTSC.

**File:** `bombernet-0.3.0-atari-xl.xex`.

## Loading

The `.xex` is a standard executable: boot it from any loader (a FujiNet's
own loader from its SD card or a TNFS host, an SIO2SD, a disk with a
binary loader, an emulator's "run program"). The game switches the OS ROM
out while it runs, to use the memory under it, and handles the screen,
keyboard and the serial bus itself; a reset brings the OS back.

## Network play

**A FujiNet.** The game talks to the FujiNet's `N:` network device over the
serial bus, at the FujiNet's high speed. The FujiNet must be joined to your
Wi-Fi, which its CONFIG program does once; nothing in the game to set.

Without a FujiNet answering, the title screen has no NETWORK row.

**Not tried on real hardware yet.** The network has been tested only in an
emulated chain (Altirra, FujiNet-PC and the NetSIO hub), which runs far
slower than real time. From a model of the FujiNet firmware's timing a
match should run 5 to 18 % slower than on the other machines, and as every
machine in a room keeps the same pace, the whole room then runs at the
Atari's. Reports from a real FujiNet are very welcome: how it plays, and
whether the pace holds.

For a relay of your own, rewrite the `.xex` file's relay address with
`tools/set_relay.py` from the sources (see the [setup page](README.md#the-relay)).

## Keys

| | |
|---|---|
| Player 1 | the arrow keys, SHIFT fires while walking (the keyboard reports one key at a time; SPACE fires too, standing still) |
| Joysticks | ports 1 and 2 |
| Leave a match, cancel a room code | ESC |

## In an emulator

atari800 (with its built-in AltirraOS) and Altirra play it locally. Network
play in an emulator needs FujiNet-PC and the NetSIO hub with Altirra, which
is a developer's setup and too slow for a real match.
