# BomberNet on the Atari ST

**Runs on:** an ST, STE, Mega ST or Mega STE with 1 MB and a colour monitor
or TV (low resolution), with TOS or EmuTOS. 512 KB is not enough yet.

**Files:** `bombernet-0.3.0-atari-st.st`, a bootable floppy image (a
single-sided 360 KB disk, which every ST drive reads), and
`bombernet-0.3.0-atari-st.prg`, the program by itself.

## Loading

- Boot the `.st` (a floppy written from it, a Gotek, or an emulator): TOS
  runs the game from the disk's AUTO folder.
- Or copy `BOMBER.PRG` anywhere and start it from the desktop; it also runs
  from a hard disk's AUTO folder.

## Network play

**A Wi-Fi modem on the serial (modem) port:** an ESP8266 with a Hayes
command firmware (Zimodem, WiFi232, RetroWiFi and the like), already joined
to your Wi-Fi as its manual describes. The game talks to it at 19,200 bps,
8N1, no flow control: `AT`, `ATE0` and `ATNET0` at start (no echo, no telnet
translation), then `ATDT relay:port` to connect. Set the modem to 19,200 bps
if it is not there already; nothing else in the game to set.

Without a modem answering, the title screen has no NETWORK row.

For a relay of your own, rewrite the `.st` or the `.prg` file's relay
address with `tools/set_relay.py` from the sources (see the
[setup page](README.md#the-relay)).

## Keys

| | |
|---|---|
| Player 1 | cursor keys, SPACE fires (RETURN when a second keyboard player is set) |
| Player 2 on the keyboard | W A S D, E fires |
| Joysticks | both ports; the title screen names them by port (the mouse port is port 0) |
| Leave a match, cancel a room code | ESC |

## In an emulator

Hatari plays it locally, with EmuTOS or a TOS image. Hatari has no network
device of its own; network play needs the real modem on a real ST.
