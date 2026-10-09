# Version B - native C++ port

Pure C++17/SDL2 rewrite of the game logic; no Z80 emulation, no translated code. See `docs/PORTING.md` for the architecture,
`docs/port_*.md` for the per-area notes. Build with `make native` from the project root. Runs from the `resources/` folder (no .dsk): `./sorcery-native [--res DIR]`.

Test aids of the headless binary (`--res DIR` instead of the old `--dsk`): `--headless --frames N --shot F:file.png --rand-input SEED --key F:KEY:1 --poke F:ADDR:VAL --vlog F --hash`.
