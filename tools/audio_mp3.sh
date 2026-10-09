#!/bin/sh
# Converts every WAV in a directory to MP3 with LAME (no system lame here: `apt-get download lame libmp3lame0`,
# `dpkg -x` both into a private dir D and run with  LAME=D/usr/bin/lame LD_LIBRARY_PATH=D/usr/lib/x86_64-linux-gnu).
DIR=${1:-assets/audio}; LAME=${LAME:-lame}
for f in "$DIR"/*.wav; do "$LAME" --quiet -V2 --tt "$(basename "${f%.wav}")" --ta "Virgin Games - Sorcery+" "$f" "${f%.wav}.mp3"; done
