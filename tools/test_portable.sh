#!/bin/sh
# The core's portability check: the PC simulator built natively and for a
# big-endian CPU (32-bit PowerPC, as a stand-in for the 68000 of the Atari ST
# and the Amiga), both replaying every recording in tests/recordings with
# identical state hashes. The big-endian build needs PLAT_HASH_PORTABLE (see
# hash_records in core/game.c); built without it, it fails at the first hash
# after a score changes (tests/recordings/coop2-score.bnr), which shows that
# the check would notice.
#
# Needs: cc, qemu-ppc-static (qemu-user-static), and Zig as a cross compiler:
#   build/venv/bin/pip install ziglang
set -e
cd "$(dirname "$0")/.."
SRC="platform/host/sim.c platform/host/nettest.c platform/mz/tables.c common/netdev_card.c core/video.c
     core/data.c core/game.c core/map.c core/enemy.c core/bomb.c core/player.c core/bomber.c core/input.c core/netplay.c"
INC="-DHOST -Icore -Icommon -Iplatform/host -Iplatform/mz"
mkdir -p build/portable
(cd c && cc -O1 $INC $SRC -o ../build/portable/sim_native)
(cd c && ../build/venv/bin/python -m ziglang cc -target powerpc-linux-musleabi -O1 -static $INC -DPLAT_HASH_PORTABLE \
     $SRC -o ../build/portable/sim_ppc)
fail=0
for r in tests/recordings/*.bnr; do
  for b in native ppc; do
    run=""; [ $b = ppc ] && run=qemu-ppc-static
    out=$(SIM_REPLAY=$r $run build/portable/sim_$b 2>&1 | grep -E "replay: end" | tail -1)
    case "$out" in *" 0 mismatches"*) ;; *) fail=1 ;; esac
    printf '%-36s %-6s %s\n' "$r" "$b" "${out:-no result}"
  done
done
[ $fail = 0 ] && echo "all recordings replay alike, little- and big-endian" || { echo "FAILED"; exit 1; }
