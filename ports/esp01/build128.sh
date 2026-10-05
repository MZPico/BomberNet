#!/bin/sh
# Rebuild from C/inline assembly. Requires z88dk and SjASMPlus 1.20.3.
set -eu
cd "$(dirname "$0")/../.."
sh ports/esp01/build_game128.sh
python3 ports/esp01/bank_layout.py
sh ports/esp01/link_game128.sh
python3 ports/esp01/pack_game128.py --prepare
(cd build/esp01-128 && "${SJASMPLUS:-sjasmplus}" ../../ports/esp01/loader128.a80)
python3 ports/esp01/pack_game128.py
