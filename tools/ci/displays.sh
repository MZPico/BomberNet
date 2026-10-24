#!/bin/sh
# The two invisible X displays the emulator tests use (:97, :98; AMIGA_DISPLAY, AMIGA_DISPLAY2).
for d in 97 98; do
  Xvfb :$d -screen 0 1024x768x24 -nolisten tcp > /dev/null 2>&1 &
done
for i in $(seq 50); do xdpyinfo -display :97 > /dev/null 2>&1 && xdpyinfo -display :98 > /dev/null 2>&1 && exit 0; sleep 0.2; done
echo "Xvfb did not start"; exit 1
