#!/bin/sh
# Reproduce the hardware-tested release using its preserved link images.
set -eu
cd "$(dirname "$0")/../.."
mkdir -p build/esp01-128
unzip -oq ports/esp01/tests/alpha19-reference.zip -d .
python3 ports/esp01/tests/link_alpha19_overlay128.py "${SJASMPLUS:-sjasmplus}"
python3 ports/esp01/pack_game128.py --prepare
(cd build/esp01-128 && "${SJASMPLUS:-sjasmplus}" ../../ports/esp01/loader128.a80)
python3 ports/esp01/pack_game128.py
cmp build/esp01-128/bombernet_esp01_128_alpha19.tap runable/19Bomberman.tap
