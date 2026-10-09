# Port of the game flow (boot, attract mode, title, hall of fame, game start/over, name entry)

Files: `native/src/port_flow.h`, `port_flow_task.cpp`, `port_flow_attract.cpp`, `port_flow_score.cpp`, `port_flow_gameover.cpp`.
Z80 range replaced: `7000-7456`, `8636-8A96` (8A97 itself is the sprite engine's and is only *called*).

## Design: blocking code as a cooperative task

The original flow waits for the interrupt handler everywhere (`halt`, `bit 7,(iy+72)`, `bit 5,(iy+74)`, empty counting loops,
key polling).  A `NATIVE()` function cannot wait (it must return before the machine emulates any more time), and
`Machine::callLifted()` cannot either (no interrupts are delivered inside it, and the T-states a long call uses are dropped at
the end of the frame).  So the flow is written as straight-line C++ that runs on its own stack (ucontext coroutine,
`port_flow_task.cpp`):

* `waitIrq()`: the task suspends; the Z80 executes a 4 byte stub at `00F0` (`halt ; jp 7003`), the game's interrupt handler runs,
  then `7003` (a native, "RESUME") resumes the task.
* `callTimed(addr)` / `call(addr)`: the task suspends; the Z80 `call`s the *original* routine in real time (interrupts keep
  running, music plays, the raster palette splits keep working); its `ret` lands on RESUME.  All text/palette/disc/sprite
  routines of the flow are called this way, so loading a file (about 9 frames of decrypting) takes the same time as before.
  Outside of the task (leaf natives checked by the verifier) `call()` is `callLifted`.
* `waitTick()` = original `7BE9` (wait for bit 5 of `(iy+74)`), `timedLoop(n, cyclesPerPass, stopOnFire, body)` models the empty
  delay loops (cost per pass measured with the emulator: 79.0 for the 7203 loop, 26 for the `dec hl` loops, 363 for the dissolve).
  Fire (bit 4 of `(iy+82)`) aborts it like the original `bit 4,(iy+82)`; the 9311 seed is advanced by the number of passes done.
* When the flow ends it returns a `Handoff` (values to push on the Z80 stack + pc): e.g. start of a game = push `7457`, jump to
  `75A4` with A = room, i.e. exactly `call 75A4` falling into the room loop.  A demo game additionally pushes `71B9` (the return
  address of the original `call 72A9`): `8636` does `ret nz` in demo mode and the native at `71B9` continues the attract loop
  (carry = fire pressed = start a real game).
* Entry natives (all "jump style": they never consume the stack's return address, they push continuations on top):
  `7000` boot+attract, `7003` resume, `71B9` after demo, `8636` game over (demo: plain `ret`), `8955` victory check
  (every frame from the room loop; the original `pop hl` drops the room loop when the victory starts).
* Time is measured in interrupts (`Machine::cycInFrame` is private): `now() = (6*frame + (92F7+3)%6) * 13312` T-states.
  `BUSY_FRACTION = 0.969` is the share of the CPU the main program gets (rest = handler).
* The flow natives are **not registered when `SORCERY_VERIFY` is set** (the verifier would run the blocking original after the
  native) nor with `SORCERY_FLOW_ORIG=1` (run the original flow code, for comparisons).  The regen marker comments
  (`// NATIVE(0x7000 ...`) in `port_flow_task.cpp` are needed because `regen.sh` greps for the text `NATIVE(0x`.

Requirements for the other areas: `8636`, `8955`, `71B9`, `7000` must be reached through translated `jp`/`call` (the player's room loop
does `call 8955` / `jp 8636`; a native room loop must tail-dispatch to them, not use `callLifted`).  `8A97` is called with
`callTimed` (it waits for the engine tick itself); the sprite-engine port must therefore keep that wait (e.g. not turn it into a
spin inside a native).  RAM `00F0-00FF` is used for the two stubs (never used by the game).

## Screen flow (Z80 addresses)

1. **Boot 7000**: KL L ROM DISABLE, find the AMSDOS hidden RSX commands `81..89` (names at 98E5) and store `{addr, rom}` in the
   far-call table `9997` (3 bytes each; used with `rst 18h`): `9997` set message (A=FF: no disc messages), `999D` select format
   (A=C1), `99A0` read sector, `99A3` write sector.  Reads the all-time hall of fame (track 39, sector id C9, drive 0 -> 5600, copied
   to 98EF, 168 bytes; failure -> `jp 8DFB`).  Installs the interrupt: IM 2, `I=54`, vector table `5400-5500` filled with `55`,
   `5555: jp 8D55`, event `8D87` on frame flyback, `9350` = firmware IRQ routine; clears `6800-687F`, `(iy+85)=FF`, `95CC=0A00`, `halt` x256
   (~1 s).  If key 60 (CPC key number 0x3C) is held: `(iy+86)=FF` ("loop mode": the attract pages repeat forever, no demo, no music) and the
   screen is filled with FF.
2. **Attract 70C4**: IM 1, mode 1, load resources `94A5`, `901E`, music init `4F03`, irq table `92E9`, IM 2, then loop `70E0`:
   page 1 "LES PLUS GRANDS SORCIERS DE NOS JOURS" (today's table `9522`, 7 rows, ~4 s), page 2 "LE GRAND SORCIER DES TEMPS" (all-time
   table `98EF`), `8E36` reveals the title picture (720 steps, one per interrupt), `7218` scrolls one message of the title scroller
   (messages: word offsets at `0A00`, pointer `95CC`; every character is 8 bit patterns shifted in 2 columns per tick, 4 ticks per char,
   scrolling the line at `C690` left, `905B` animates the picture), then (every 4th round, i.e. when the low byte of `95CC` is 8) a picture
   (`94D0` list, loaded to 0A00) dissolves in byte by byte in the order `pos += 0x22D` over the 16K screen, then `71B2`: demo game
   (`(iy+81)=FF`, `72A9`).  Fire (joystick fire2 = CPC key 76 = bit 4 of `(iy+82)`) at any time -> `72A9`.
3. **Start of game 72A9**: music off; message line wiped in mode 0; if `(iy+88)` bit 7 (game was won once, set by 7C5C) and key 0x3E is held
   -> initiation (practice) mode ("TRICHEZ..." via firmware text output, which this machine does not render): `(iy+87)=FF`.
   `72FB`: load game resources (`94B5`; `94D6` in initiation mode and in every second demo), define the sound envelopes
   (`A610..` volume, `A710..` tone), clear `6800-684F`.  Start place: demo = recorded script (`99B4`/`9A0F`: room, x, y, then
   `{duration, joystick}` pairs read through `9A42`), initiation = room 0x3A at (0x10,0x58) with `(iy+76)=FF` and four code bytes (`4EA5, 4C99, 4DDD, 4D25`)
   patched to `C6`, normal = `seed & 7` (minus 5 if >= 5) selects one of five start places (`9668`, 3 bytes: room, x, y) and swaps the
   two spawn points of up to two rooms (`9677`, 2 room numbers per place, FF = none; record offset `+26h`).  Then status panel `752D`, irq table
   `92DD`, IM 2, `call 75A4` (room A), room loop `7457`.
4. **Game over 8636**: death cause texts (bits 0..2 of `(iy+79)` -> `97E8/980D/9832`), ~2 s pause, clear, "VOTRE SCORE EST ... ET VOUS AVEZ SAUVE n
   SORCIER(S)" (`9681`), score (`92D4..92D6`, BCD) and a pause (loop counts HL down b times: `(92D7, b=10)`; with all 8 sorcerers: text `96F6`/`977D`
   + "MODE D'INITIATION MAINTENANT DISPONIBLE", `b=20`, HL = end of that text).  Then `8700`: if the score beats an entry of today's table
   -> name entry, else attract.
5. **Name entry**: letters row (`98C7`: `<` backspace, `>` end, `!`, A-Z, `.`) at text pos `0329`; `^` marks the selected letter (`0379+2n`) and the
   write position (`0477+2n`); name buffer `98A7` (16 chars, `.` padded) printed at `0427`.  Joystick sampled every 5 ticks: left/right = move
   (wrap), fire = select.  `88CC` filters the name: it must not *start with* one of the forbidden words in the dictionary `9EE0` (`{len, chars}`..., 0,
   ~40 names such as brand names and rude words); the replacement is string n+1 of `9FEC` (bit 7 = last char; the first string is a dummy), e.g.
   "GROS.BARBE"; an empty name becomes "QUI EST LA?" (`98BC`).  The entry is stored in today's table; if it also beats the all-time table it is
   inserted there (`88BC` shifts the lower entries down) and the table is written back with AMSDOS write sector (track 39, sector id C9).
6. **Victory 8955** (called every frame; starts when `(945B)==4` and `(93E8)==FF`): the entity copy of the player at `93E8` plays animation `A8`, then four
   sorcerers (`A9`, then `AC`) appear at the positions `945C`, all pens flash white (SCR SET INK x16), fanfare (`A848`, `A86C`), empty tile map, "VOUS AVEZ
   VAINCU LE NECROMANCIEN" scene (`9857`), `+20000` points, `(iy+79) |= 0xC8`, then the game over screen (`8636`).

## High score table format

7 entries of 24 bytes = 168 bytes (0xA8): `char name[21]` (upper case, padded with `.`) + `uint8_t score[3]` (6 packed BCD digits, most significant byte
first).  Sorted best first; equal scores insert below the old entry.  Two copies: today (`9522`, RAM only) and all-time (`98EF`, loaded from
and saved to the first 168 bytes of the sector with id C9 on track 39 via AMSDOS commands read sector = 4 / write sector = 5).  The scratch buffer is `5600`.
Default contents: NICK 020000, ANGELA 019000 ... HUGH 014000 (all-time); today's table is initialised with DAVE etc.

## RAM fields (see `port_flow.h` for all names)

`(iy+72)` tick counter (decremented by every interrupt, bit 7 = sprite tick), `(iy+74)` bit 5 = 50 Hz tick, `(iy+79)` death cause / won, `(iy+81)` demo,
`(iy+82)` joystick (bit 2 left, 3 right, 4 fire), `(iy+85)` demo script toggle, `(iy+86)` loop mode, `(iy+87)` initiation mode, `(iy+88)` initiation
available, `9310` room, `9311` random seed (stirred by +0x29 per delay pass, +0x4B per scroller character, +0x29 per room-loop pass), `92D4` score,
`95E4` sorcerers saved, `945B` victory counter, `92F5/92F7` interrupt phase table pointer / phase.

## Verification

* Leaf routines `8866 888D 888E 8894 88BC 88CC` are normal `NATIVE`s, verified with the framework verifier (they only run at the end of a game):
  `SORCERY_VERIFY=1 SORCERY_FLOW_FUZZ=1 ./sorcery-native-headless --dsk ../Sorcery.dsk --headless --frames 2600 --key 100:76:1 --key 112:76:0 --poke ...`
  - the fuzz option (in `port_flow_score.cpp`) runs 400 random names / score tables / registers as nested calls on the first call of one of them: 0 mismatches.
  (The standard `verify.sh` never reaches them; game over needs a death.  `--poke` is a local option of a private copy of `main.cpp`; the same effect:
  set `(iy+79)=0x81` (684F) and the score `92D4..` in a debugger/test harness.)
* Everything else is compared end-to-end against the original code: run the same input with `SORCERY_FLOW_ORIG=1` (original flow) and without, hash
  `Machine::render()` every frame and compare the sequences.  Results (3000-6000 frames, scripted keys): the boot, hall of fame pages, title,
  scroller, demo, start of a game with a given seed, cheat start, loop mode (key 60), game over with name entry (typing "A" and ending), saving to disc,
  victory sequence and game over after it all produce the *same screens in the same order*; static screens match to the frame, transitions differ by 0-3
  frames (the original spins by cycle count, the port by interrupt count), and from the room loop on the frames are bit identical (offset 0) once the
  game is running with the same random seed.
* Not bit exact: the random seed `9311` (start place of a normal game), because it counts delay-loop passes at the moment fire is pressed, and the port
  knows the time only to 1/6 frame.  Not tested: disc errors (`jp 8DFB`), SDL front-end.

## Discoveries

* Key 60 (0x3C) at boot -> "loop mode" (attract pages forever); `(iy+88)` + key 0x3E at game start -> initiation mode (after winning).
* The delay between the hall of fame pages is a pure CPU-time loop (hl = end of table + 3*65536 passes of 79 T-states); seed `9311` is stirred by it.
* High score table lives on track 39, sector C9; written through AMSDOS RSX `0x85` (write sector), not through the CP/M file system.
* Framework suggestions: (1) make `Machine::cycInFrame` (or a `clock()`) public; (2) `runFrame` drops the T-states a long native/`callLifted` used
  past the frame end - interrupts are lost/bunched; (3) a first-class `Machine::callTimed` would let other areas port waiting code too.
