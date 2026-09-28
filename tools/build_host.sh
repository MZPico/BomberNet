#!/bin/sh
# Host simulator: the whole game on a PC (key bot, recording/replay, network card stub).
set -e
cd "$(dirname "$0")/../c"
mkdir -p build
cc -O1 -g -fsanitize=address,undefined -DHOST -Icore -Icommon -Iplatform/host -Iplatform/mz \
   platform/host/sim.c platform/host/nettest.c platform/mz/tables.c common/netdev_card.c \
   core/video.c core/data.c core/game.c core/map.c core/enemy.c core/bomb.c core/player.c \
   core/bomber.c core/input.c core/netplay.c -o build/sim "$@"
echo "built c/build/sim"
