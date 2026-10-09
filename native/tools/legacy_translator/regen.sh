#!/bin/sh
# Regenerates native/src/game_gen.cpp from the DSK:   native/tools/regen.sh path/to/Sorcery.dsk
set -e
D=$(dirname "$0"); ROOT="$D/../.."; DSK=${1:-$ROOT/../Sorcery.dsk}; T=$(mktemp -d)
python3 -I "$ROOT/tools/extract_assets.py" "$DSK" $T/assets >/dev/null
python3 -I - "$T" <<'PY'
import sys
t=sys.argv[1]
m=bytearray(65536)
def put(f,a):
    d=open(f'{t}/assets/raw/{f}','rb').read(); m[a:a+len(d)]=d
put('CODE.BIN',0x7000); put('MUCODE.BIN',0x4F00)
open(f'{t}/mem.bin','wb').write(m)
def w(a,n): return [m[a+2*i]|m[a+2*i+1]<<8 for i in range(n)]
e=[0x7000,0x8d55,0x8baa,0x8bd5,0x4f00,0x4f03,0x4f38,0x4f51]+w(0x92e9,6)+w(0x92dd,6)+w(0x935e,8)+[x for x in w(0x937a,0x38) if 0x7000<=x<0x9300]
open(f'{t}/entries.txt','w').write('\n'.join('%04X'%a for a in sorted(set(e))))
PY
cat "$D/extra_entries.txt" >> $T/entries.txt
python3 -I "$D/lift.py" scan $T/mem.bin $T/entries.txt $T/addrs.txt
g++ -O1 -o $T/cyc "$D/cyc.cpp" "$ROOT/emu/z80.cpp"
$T/cyc $T/mem.bin $T/addrs.txt $T/cyc.txt
grep -ho "NATIVE(0x[0-9A-Fa-f]*" "$ROOT"/native/src/port_*.cpp 2>/dev/null | sed "s/NATIVE(0x//" > $T/ovr.txt || true
touch $T/ovr.txt
python3 -I "$D/lift.py" gen $T/mem.bin $T/addrs.txt $T/cyc.txt "$ROOT/native/src/game_gen.cpp" $T/ovr.txt
