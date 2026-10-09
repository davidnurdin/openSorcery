#!/bin/sh
# Builds tools/audio_export and renders all audio.
#   tools/audio_export.sh [out_dir]
set -e
ROOT=$(cd "$(dirname "$0")/.." && pwd); OUT=${1:-$ROOT/assets/audio}
mkdir -p "$OUT"
g++ -O2 -std=c++17 -o "$ROOT/tools/audio_export.bin" "$ROOT/tools/audio_export.cpp" \
    "$ROOT"/native/src/machine.cpp "$ROOT"/native/src/sound.cpp "$ROOT"/native/src/alu.cpp "$ROOT"/native/src/native.cpp \
    "$ROOT"/native/src/port_*.cpp "$ROOT"/common/resources.cpp "$ROOT"/common/png_io.cpp "$ROOT"/common/roomtext.cpp
(cd "$ROOT" && "$ROOT/tools/audio_export.bin" "$OUT")
