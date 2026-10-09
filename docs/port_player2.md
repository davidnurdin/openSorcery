# Port of the blocking player code (pause, tick wait, ending, door entry)

File: `native/src/port_player2.cpp` (no header needed).  Z80 ranges: `7509-752C`, `7940`, `7BE9`, `7C55-7D31`.

## What was already native, what is new

The earlier player port (`port_player_state.cpp`) already turned the whole door/teleport/death sequence (7940-7B58, the walking
step 7BBF, the state handlers 76F6-7916) into native code, using "continuations" (`callThen(7BE9 / 7BBF, K_xxx)`: the next piece of
the animation is registered at an otherwise dead address, so every wait happens inside the original `7BE9` loop).  All live labels
of 7940-7B6A are therefore native; only the translated *copy* of the code is still in `game_gen.cpp` (the generator keeps it
as the reference for the verifier, it can no longer be reached).  New in this phase:

| address | native | what |
|---|---|---|
| `7509` | `pauseCheck` | pause: space freezes the game (task: press/release, press/release). `(iy+80)` bit 7 is set while paused |
| `7BE9` | `waitFrameTick` | wait for bit 5 of `(iy+74)`: now a native that sends the Z80 to a `halt ; jp 7BE9` stub at `00E8` while the flag is clear |
| `7940` | `doorEntry` | entry point for translated callers (none left): lock test, drops the caller's return address, starts `doorTransition()` |
| `7C55` | `endingEntry` | the ending sequence (entered through the native `7C38`, which pushes 7C55), straight-line C++ in a flow task |

So after this change **no execution path reaches translated code in 7509-752C, 7940-7B6A, 7BE9, 7C55-7D31**.

## Design

Same cooperative task as the flow port (`port_flow.h`): `flow::startTask(m, body)`, `flow::waitIrq()`, end with a `Handoff` whose
`pc` is the return address of the original `call` (popped by the entry native; "jump style").  All four natives are registered only
when `flow::flowEnabled()` (not under `SORCERY_VERIFY`, not with `SORCERY_FLOW_ORIG=1`); in those modes the translated code runs, so
the original can still be used as the reference.

* `7509`: a pressed space (KM TEST KEY 0x2F via `call(0xBB1E)`) starts a task; otherwise the native returns at once (it is called every
  tick).  The task waits one interrupt between key polls.
* `7BE9`: a plain native cannot spin, so when the flag is clear it pushes the stub address on the Z80 stack (the framework's `ret`
  enters the stub `halt ; jp 7BE9`), i.e. it is re-entered after the next interrupt.  This also serves the continuations of
  `port_player_state.cpp` and any translated `call 7BE9`.
* `7C55`: phases (see the comment in the source): 1) release the 8 objects of room 0x1C (70, then 50 ticks apart; `95E4/95E5`),
  2) score +1 and energy bar fade per tick until the player code raises `(iy+79)` bit 7, 3) refill energy to 99 BCD one point per
  tick, 4) door flags of room 0x1C changed (first left door: lock bits cleared; first right door: lock type 6), `(iy+76)--` (end-game
  phase flag, bit 7), `(iy+79)=0`.  Every tick wait is `while(!(iy72&0x80)) waitIrq(); iy72=7`.

## Verification

Behaviour (not per-call) verification: the screen of every frame is hashed (`SORCERY_SHOTHASH=1`, a private addition to `main.cpp`,
plus `--poke frame:addr:val`, both only in my private copy) and the new build is compared frame by frame with the build before this
change (flow port enabled in both), 3000-7000 frames per scenario:

* door: player poked next to the right door of the start room, RIGHT held: transition, new room, door closing - **0 differing frames**.
* death by energy: `(iy+77)=0` -> fall, death animation, game over, name entry flow - **0 differing frames** (3000 frames).
* ending: a door destination is poked to room 0x1C, then `95E4=8` and the player position (0x18,0x48): all three phases and the
  following game (7000 frames) - **0 differing frames**; the ending completes (`(iy+76)=FF`, door flags changed).
* pause: space pressed/released twice around frame 800 / 900: the paused screens are identical; after resuming the new version lags
  by one frame for a few frames (the task polls the key after the next interrupt, the original spins on it), differing frames: 29 of 1100,
  then identical again.
* random joystick input (`--rand-input 1..6`, 4000 frames): 1-7 single isolated frames differ out of 4000.  Disabling only the
  `7509` native makes them vanish: cause is the flat 60 T-state charge of a native versus the ~30 T-states of the tiny original
  that is called every tick (a frame boundary race), not a logic difference.  `7BE9` alone gives 0 differences.

Not verified per call (impossible: they block).  Not tested: pause + ending combined, SDL front end.

## Notes / framework suggestions

* `00E8-00EB` in low RAM now holds the wait stub (next to the flow stubs at `00F0`).
* `Machine::native()` charging a flat 60 T-states (instead of the routine's cost) is what causes the rare one-frame differences; an
  option to charge the declared `cost` for small hot natives would remove them.
* The old translated code of 7940-7B58 / 7C55-7D31 cannot be removed from `game_gen.cpp` by `regen.sh` (it keeps overridden code as
  the verification reference); it is unreachable.
* To also remove the continuation machinery of `port_player_state.cpp` (`callThen`, `K_xxx`), the door transition could be rewritten
  as one straight-line flow task like `endingSequence()` (that file belongs to the other agent; not touched).
