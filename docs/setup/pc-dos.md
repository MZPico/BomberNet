# BomberNet on the IBM PC (DOS)

**Runs on:** any PC from an 8088 up, DOS 3 or later or FreeDOS, 256 KB of
memory, a CGA, EGA or VGA card. Sound through the PC speaker, or an AdLib or
Sound Blaster's FM chip when one is found. An XT at 4.77 MHz keeps up on a
CGA or an EGA.

**Files:** `bombernet-0.3.0-pc-dos.exe`, the program (`BOMBER.EXE`), and
`bombernet-0.3.0-pc-dos.img`, a 360 KB floppy image with the program and its
`README.TXT` (a data disk, not a boot disk).

## Loading

Copy `BOMBER.EXE` anywhere and run `BOMBER`. `BOMBER /?` lists the options:

```
/RELAY=host[:port]   the relay (port 8765 if not given; else api.mzpico.com)
/CGA /EGA /VGA       the adapter's look (else the one found)
/COM1 /COM2          the WiFi modem's port (else both are tried)
/BAUD=n              the modem's speed (else 19200, then 115200)
/NOSOUND             no sound
/SPEAKER             the PC speaker (else an AdLib or Sound Blaster's FM)
/NOROLLBACK          no run-ahead in a network match
/NET=PKT /NET=MODEM  Ethernet or the modem only
/IP= /MASK= /GW= /DNS=  Ethernet's addresses (else DHCP's)
```

## Network play

Two ways; the game finds whichever is there.

- **Ethernet** with any network card that has a DOS packet driver. Load the
  driver first, then the game; it takes its address by DHCP, or from the
  `/IP= /MASK= /GW= /DNS=` options. For an NE2000 at 300h, IRQ 3, with
  Crynwr's free driver:

  ```
  NE2000 0x60 3 0x300
  BOMBER
  ```

- **A Wi-Fi modem** on COM1 or COM2: an ESP8266 with a Hayes firmware
  (Zimodem, WiFi232, RetroWiFi), joined to your Wi-Fi beforehand, at 19,200
  or 115,200 bps, with telnet translation off. The game finds it when no
  packet driver is loaded.

When nothing answers, NO CONNECTION shows after a few seconds. The title
screen always has the NETWORK row on the PC.

A relay of your own: `BOMBER /RELAY=192.168.1.10` (see the
[setup page](README.md#the-relay)).

## Keys

| | |
|---|---|
| Player 1 | cursor keys, SPACE fires (RETURN when a second keyboard player is set) |
| Player 2 on the keyboard | W A S D, E fires |
| Joysticks | two on the game port |
| Leave a match, cancel a room code | ESC |
| Back to DOS | Alt+X on the title screen |

## In DOSBox-X

For Ethernet, in the configuration `[ne2000]` `ne2000=true` and
`backend=slirp`, and load the packet driver in the autoexec as above. For a
modem, `[serial]` `serial1=modem`. On Windows use the MinGW build of
DOSBox-X: the Visual Studio build has no slirp and the game then freezes on
its first send. At 3000 cycles and up the game runs at full speed; at an
XT's 300 cycles DOSBox-X itself paces slowly in a match.
