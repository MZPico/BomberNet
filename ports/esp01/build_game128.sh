#!/bin/sh
set -eu
cd "$(dirname "$0")/../.."
mkdir -p build/esp01-128
flags='+zx -lndos -pragma-define:REGISTER_SP=65535 -pragma-define:CLIB_EXIT_STACK_SIZE=0 -pragma-define:CRT_ENABLE_STDIO=0 -pragma-define:CLIB_OPEN_MAX=0 -pragma-redirect:fputc_cons=zx_no_console -m'
zcc $flags -zorg=49152 -Iports/esp01 -o build/esp01-128/esp_bank ports/esp01/bank_module.c ports/esp01/tcp_esp_stream.c
