#!/bin/sh
# A test that runs emulators in real time, given one more chance: tools/ci/retry.sh command args...
# (a busy CI machine can starve an emulator for a moment; a real failure fails twice)
"$@" && exit 0
echo "::warning::failed once, retrying: $*"
sleep 5
exec "$@"
