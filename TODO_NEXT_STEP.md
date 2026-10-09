# TODO / next step (noted, to be decided later)

## Idea: a "real developer" C++ rewrite (version C)
The current native port (`native/`) is a *functional port*: it reproduces the behaviour of the original Z80 program routine by routine, but
keeps the original's structure (64 KB RAM image at the original addresses, registers as calling convention, interrupt phases, XOR sprite
drawing into CPC screen memory, routines identified by their original addresses). A developer would not have written it like that, and I
(Claude) do not know how the original author structured the game - only what it does.

A from-scratch rewrite written the way a developer would do it:
* fixed time-step game loop, classes `Player`, `Enemy`, `Projectile`, `Room`, `Renderer`, `Input`, `Audio`, an entity system instead of address tables;
* data loaded from files by loaders (rooms/sprites/sounds from `resources/`), no 64 KB memory image;
* modern rendering (sprites with transparency / textures) while keeping the CPC look if wanted (palette per room, mode 0 / mode 1 areas);
* game logic independent of the hardware (no raster split by interrupts, no register calling convention).

### What already exists to build on
* The observed rules: movement (2 px steps), collisions (tile probes one pixel further), player states (appear, fly, fall, die, drown, door
  transition), enemy behaviours (`docs/port_handlers1.md`, `docs/port_enemy*.md`), doors/locks, objects/pickups, score/energy/BCD, victory and
  ending sequences, hall of fame, demo script format, initiation ("TRICHEZ") mode - see `docs/port_*.md`.
* Extracted data: `resources/` (sprites/tiles/font/title as editable indexed PNG, 79 rooms as text, data files), `assets/audio` (WAV/MP3),
  `assets/png` (previews).
* Reference for comparisons: the current native version (`sorcery-native`) and the emulator version (`sorcery-emu`, bit-exact original code).

### Plan (steps with a checkpoint each)
1. Decide the required fidelity: "looks like the game" vs "identical frame by frame"; keep exact CPC rendering or simplify; reuse the current
   sound manager (`native/src/sound.*`) or rewrite it.
2. Data layer: loaders for resources, room/record structures with named fields (the unknown record bytes 0x34-0x3F need to be understood).
3. Core: game loop, input, renderer (tiles + sprites + HUD + text), audio.
4. Gameplay: player, enemies, projectiles, pickups, doors, energy/score, death and game over, name entry / hall of fame.
5. Flow: attract mode, title, demo, victory, ending.
6. Compare every step against the current native version (screenshots / state traces with the same scripted input) and list deliberate differences.

### Estimate (very rough, no guarantee)
* playable with the main gameplay: about half of a working session like the one that produced the current port;
* faithful on rare cases (ending sequence, all enemy types, door edge cases) and compared with the reference: at least as much again;
* parallel agents speed it up but hit session limits before (two agents were interrupted last time).
* The cost depends mostly on the fidelity chosen in step 1.

### Known weak points of the current port to remember
* Not compared frame by frame with the emulator after the last restructuring (natives take no simulated CPU time).
* Not tested: real-time SDL window/audio on a desktop; the ending sequence (8 objects) in the final build; editing PNG/room files in the game window.
* Collision probe natives left/right/up do not restore HL like the original (no caller found that needs it; `blockedDown` was a bug of this kind: the witch did not drown).
* Rooms: 12 bytes of the record (0x34-0x3F) not understood; one frame of wrong palette at game start; firmware text "TRICHEZ..." not rendered.
