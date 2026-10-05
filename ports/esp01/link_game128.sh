#!/bin/sh
set -eu
cd "$(dirname "$0")/../.."
core='c/core/bomber.c c/core/netplay.c c/core/data.c c/core/map.c c/core/game.c c/core/input.c c/core/video.c c/core/enemy.c c/core/bomb.c c/core/player.c'
common='c/platform/zx/plat_zx.c c/platform/zx/tables.c c/common/netdev_soft.c c/common/ws.c c/common/z80_loops.c'
flags='+zx -O3 -SO3 -DESP_FAST128 -DESP_STREAM -lndos -zorg=24000 -pragma-define:REGISTER_SP=65535 -pragma-define:CLIB_EXIT_STACK_SIZE=0 -pragma-define:CRT_ENABLE_STDIO=0 -pragma-define:CLIB_OPEN_MAX=0 -pragma-redirect:fputc_cons=zx_no_console -m -Ic/core -Ic/common -Ic/platform/zx -Iports/esp01 -Ibuild/esp01-128'
# The bridge must precede every core/platform source (fixed RAM below C000).
zcc $flags -o build/esp01-128/bomber ports/esp01/tcp_bank128.c $core $common
