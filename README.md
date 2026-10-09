# openSorcery - Sorcery+ (Virgin Games, 1985, Amstrad CPC 6128) - C++/SDL2 ports

Source: `../Sorcery.dsk` (French disk version, public domain). Two versions are built from it:

| | version A `emu/` | version B `native/` |
|---|---|---|
| idea | Z80 emulator core + CPC firmware/AMSDOS shims run the original code | the game logic rewritten as C++ (no Z80, no translated code) |
| binary | `sorcery-emu` (`-headless`) | `sorcery-native` (`-headless`) |
| status | complete, playable | complete: boot, attract/title/hall of fame, demo, game, enemies, doors, ending, victory, game over, name entry, music + effects |

Build: `make emu`, `make native` (needs SDL2 dev files, or `SDL_INC=... SDL_LIB=...`). `sorcery-emu` reads the original `Sorcery.dsk` (`--dsk path`, default `public/Sorcery.dsk`);
`sorcery-native` needs **no .dsk**: it reads the `resources/` folder (`--res DIR`), see `resources/README.md` (editable PNG graphics, rooms as text). Keys: arrows/WASD move, X/Alt/Z/Ctrl/Enter = fire (shoots the weapon/spell; starts a game from the title screen), Space pause, F5 screenshot, F11 fullscreen, Esc quit.

Directories: `common/` DSK reader, PNG writer - `emu/` version A - `native/` version B (old translator tools: `native/tools/legacy_translator`) -
`resources/` data + editable PNG graphics of the native version - `tools/` (`make_resources.cpp` builds `resources/` from the .dsk; Python extraction, listing, contact sheets) - `docs/` notes, Z80 listing, porting guide - `assets/` extracted PNG/raw/audio - `tests/` zexdoc harness for the Z80 core.

Assets: `python3 tools/extract_assets.py ../Sorcery.dsk assets` (decrypted files in `assets/raw`, PNGs in `assets/png`: title, tiles, font, sprites, 79 rooms).

## Version B status
All routines of the original program were rewritten by hand and verified one by one against a bit-exact translation during the port
(0 mismatches); the final program ran long random-input games (60000 frames x 8 seeds), the attract mode with the demo, name entry and the
victory sequence without problems. Limits and known differences: `docs/PORTING.md`. Per-area notes: `docs/port_*.md`.
Audio exports: `assets/audio` (WAV + MP3; not listened to by the author). The real-time SDL window and audio were not tried on a desktop.
