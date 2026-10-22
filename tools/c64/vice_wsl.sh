#!/bin/sh
# BomberNet on a C64 in VICE with a window, under WSL (WSLg shows it on the
# Windows desktop, with sound): the patched VICE (tools/c64/vice-3.9-bombernet.patch)
# built with its SDL2 interface into ~/src/vice/dist-sdl (docs/port-c64.md).
#
#   tools/c64/vice_wsl.sh [uci|wic64|both] [program]
#     uci    the Ultimate's network (its command interface, imitated: UCI_EMU)
#     wic64  a WiC64 on the user port, with the real firmware's timing (WIC64_FW_TIMING)
#     both   both (the game takes the Ultimate's)
#   program: a .prg or .d64 (default build/c64/bomber.prg)
# F12 opens VICE's menu (joystick keys, settings).
cd "$(dirname "$0")/../.." || exit 1
NET=${1:-uci}
PRG=$(realpath "${2:-build/c64/bomber.prg}")
VICE=${VICE_SDL:-$HOME/src/vice/dist-sdl/bin/x64sc}
OPTS=""
case "$NET" in
  uci) export UCI_EMU=1 ;;
  wic64) export WIC64_FW_TIMING=1; OPTS="-userportdevice 23" ;;
  both) export UCI_EMU=1 WIC64_FW_TIMING=1; OPTS="-userportdevice 23" ;;
esac
mkdir -p "$HOME/.local/state/vice"
exec "$VICE" +logcolorize -pal $OPTS -autostart "$PRG"
