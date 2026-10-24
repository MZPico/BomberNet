#!/bin/sh
# The local reference relay on 127.0.0.1:8765 (WebSocket) and 8766 (TCP), log in build/relay.log.
cd "$(dirname "$0")/../.." || exit 1
[ -x build/venv/bin/uvicorn ] || { python3 -m venv build/venv && build/venv/bin/pip install -q -r relay/requirements.txt; }
(cd relay && ../build/venv/bin/uvicorn relay:app --host 127.0.0.1 --port 8765 > ../build/relay.log 2>&1 &)
for i in $(seq 50); do nc -z 127.0.0.1 8765 && exit 0; sleep 0.2; done
echo "the relay did not start"; tail -20 build/relay.log; exit 1
