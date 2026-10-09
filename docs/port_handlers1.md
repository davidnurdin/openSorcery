# Entity behaviour routines, group 1 (`native/src/port_handlers1.cpp`, `port_handlers1.h`)

The per-frame entity loop (81A2/81BD) calls, for every entity (10 bytes, `IX` = entity), the routine whose address is in the
word table at `0x937A + 2 * (state & 0x7F)` after `8AAA` saved the entity's old image. A behaviour ends with `jp 8ACA`
(erase old image / paint new one) or a plain `ret`. This group covers states 0-9 (code 7D32-7FD2) and the helpers they use.

| addr | native | state | behaviour |
|---|---|---|---|
| 7D32 | `emerge` | 0 | creature rises out of the ground (1 line per 2 calls, a5 counts to 24), then state := a9 |
| 7D51 | `chaser` | 1 | flies diagonally at the player, killed by item 0x2D, damage 0x4B |
| 7DBE | `walker` | 2 | walks on the floor towards the player (frames 10-13 left, 18-1B right), killed by 0x41, damage 0x3C |
| 7E2D | `diver` | 3 | swooping flyer with turn-around frames 8-0B (a6 bit0 = right, bit1 = turning), killed by 0x2C, damage 0x32 |
| 7EDB | `patrol` | 4 | invulnerable, x oscillates 0x34..0x44 (a9 bit 7 = left) |
| 7F18 | `follower` | 5 | invulnerable, x = byte 6804 + 0x1C |
| 7F37 | `collectible` | 6 | magic item: spins; on overlap +2000 points, `COLLECTED++`, room record mark, state 7 |
| 7F84 | `riseAway` | 7 | the item floats up 2 lines/frame, freed at y >= 0xC8 |
| 7FA6 | `exploding` | 8 | frames 4C-4F every 4th call, then state 9 with frame a9, timer 0xC0 |
| 7FCA | `remains` | 9 | timer counts up to 0 (64 calls), then the slot is freed |

Helpers (also registered at their own address because other code calls them):

| addr | native | meaning |
|---|---|---|
| 7FD5 | `shootNowAndThen` | once the game was completed (mode bit 7) a countdown (iy+0x53) triggers `7FEA`, reload `R | 0x80` (the Z80 refresh register; in the lifted program it is just the stored value) |
| 7FEA | `shootBurst` | clears the enemy shots (850C) and starts all 8 at the entity centre |
| 85A7 | `creatureTouchesPlayer` | C = damage: on overlap sound, accumulator (iy+0x4E) -= C, on underflow BCD energy -1 and HUD redraw |
| 8F52 / 8F78 / 8FAD / 8FD3 | `blockedLeftHere` ... | single probe tile test at the current position (carry = blocked); these were `db` in the old listing |

C++ API in `port_handlers1.h` (namespace `handlers1`): `Ent`, `touchPlayer`, `weaponHit`, `maybeShoot`, `burstDebris`, `probe*`,
usable by other behaviour groups.

## 8011 (weapon / bolt hit test) is not a NATIVE
`8011` is `call`ed by chaser/walker/diver (the only callers). On a hit it executes `pop hl` (drops its return address) and finishes through
`jp 8ACA`, i.e. it *ends the calling behaviour*. A NATIVE cannot be verified like that (the original would run on into the
dispatcher beyond the verifier's sentinel), so it is implemented as `handlers1::weaponHit()` returning true ("behaviour must end") and
called directly by the three behaviours. Nothing calls the translated 8011 any more (it only stays in `game_gen.cpp` as unreachable code
from the generic address switch).

## Not in this group
Entity states 0x20-0x36 (code 81DE-846F, includes 8374 which is the only other user of 7FEA), the entity loop 81A2/81BD, and the
ending sequence 7C55-7D31. Table entries 0x28 and above (state >= 0x28) are beyond the 40 entries `regen.sh` follows, so those
routines are not discovered by the lifter (they run under the interpreter fallback).

## Verification
* `SORCERY_VERIFY=1 ./sorcery-native-headless --dsk ../Sorcery.dsk --headless --frames N --rand-input S`: 0 mismatches (see the numbers
  in the final report; random play reaches states 0-3 and the helpers constantly, rarely 4-9).
* `native/tools/fuzz_handlers1.cpp` boots the game then calls each native under the verifier 100000+ times with random plausible state
  (entity near the player, random tile map, bolts, held item, action key, mode bit, timers on wrap boundaries): every routine
  has >= 1000 calls and 0 mismatches. Build line in the file header.
