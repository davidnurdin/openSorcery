#!/bin/sh
# Runs the native build in verification mode (every NATIVE() routine is checked against the original code).
# usage: native/tools/verify.sh [frames] [seeds...]      (needs ./sorcery-native-headless, built with `make sorcery-native-headless`)
ROOT=$(cd "$(dirname "$0")/../.." && pwd); F=${1:-6000}; shift; SEEDS=${*:-"1 2 3"}
for s in $SEEDS; do
  echo "== seed $s, $F frames"
  SORCERY_VERIFY=1 "$ROOT/sorcery-native-headless" --dsk "$ROOT/../Sorcery.dsk" --headless --frames $F --rand-input $s 2>&1 | grep -E "^verify|VERIFY|crashed|stopped"
done
