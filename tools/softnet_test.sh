#!/bin/sh
# Two instances of the software network device through a relay (default: the
# local reference relay, WebSocket port 8765). NET_RELAY=api.mzpico.com:80 for
# production.
cd "$(dirname "$0")/../c"
N=${1:-60}
out=$(mktemp)
build/softnet_test host $N > "$out" 2>&1 &
hp=$!
for i in $(seq 1 100); do grep -q "^CODE" "$out" && break; sleep 0.1; done
code=$(awk '/^CODE/ {print $2}' "$out")
[ -n "$code" ] || { echo "host did not create a room:"; cat "$out"; kill $hp 2>/dev/null; exit 1; }
jout=$(mktemp)
build/softnet_test join $N "$code" > "$jout" 2>&1
jr=$?
wait $hp; hr=$?
sed 's/^/join: /' "$jout"; sed 's/^/host: /' "$out"; rm -f "$out" "$jout"
[ $jr -eq 0 ] && [ $hr -eq 0 ]
