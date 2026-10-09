# Sprite engine port (`native/src/port_sprite.cpp`, `port_sprite.h`)

Hand-written C++ for the Z80 routines 8AAA-8C2F, 8F5C-9007, 901E, 905B, 913E-91F7 (all verified against the
original with `SORCERY_VERIFY=1`, plus a fuzz harness for rare paths, see "Verification").

## Entity (sprite object), 10 bytes, `IX` points to it
| off | meaning |
|---|---|
| +0 | state; bit 7 = "an XOR image of this entity is on screen"; low nibble = behaviour state |
| +1 | y (top pixel row, 0..0x78 for a 24-row sprite on the 144-row field) |
| +2 | x (left pixel, mode-0 pixels, 0..0x94); **bit 0 selects the shifted blitter** |
| +3 | sprite frame (144 bytes each at 0A00: 24 rows x 6 bytes = 12 pixels) |
| +4 | animation counter |
| +5 | height in rows (24 for a full frame; smaller for small sprites) |
| +7,+8,+9 | (enemy slots) alternative y/x/frame; bit 7 of +9 chooses between (+1,+2,+3) and (+7,+8,+9) when a remnant is made |

Instances: player at 6840, 8 enemy/object slots at 93E8 (state 0xFF = free), working entities at 9352.
`OLD_ENTITY` (92C8, 10 bytes) is a snapshot of an entity before it is moved (`saveEntity`), i.e. what is on screen.

## Drawing (XOR, so the same call draws and erases)
* `8AAA saveEntity`  copy entity (ix) to 92C8.
* `8AB6 drawEntity`  draw the image at the current position, set drawn flag.
* `8AC0 eraseEntity` erase the snapshot image if it was drawn (bit 7 of 92C8), clear the drawn flag of the entity.
* `8ACA moveEntity`  erase the snapshot image, draw the new image, set the drawn flag.
* Blitters: even x = plain 6-byte XOR per row (orig 8BAA). Odd x = the row shifted right by one mode-0 pixel
  (orig 8BD5, was undisassembled data in the listing): 7 destination bytes, byte i gets
  `((s[i-1]<<1)&AA) | ((s[i]>>1)&55)` (pixel 0 lives in the odd bits, pixel 1 in the even bits).
* The original steps old and new image row-interleaved with the vertical offset between them (8AD0-8B96, the
  self-modifying loop at 8B65) to avoid flicker while racing the beam. Everything is an XOR on the same memory so the
  result is identical to "xor old image, xor new image"; the native version does exactly that.
  (Only if the destination of the blit overlaps the sprite *source* data - possible only for y outside the play
  field, e.g. y >= 0xF0 where the screen address wraps around 64K - the order would matter. Never happens in play.)
* The self-modified operands (8B6A/8B6D/8B70 old image, 8B7F/8B82/8B85 new image) are still written, with the
  values the original leaves there, purely so that the RAM comparison of the verifier stays exact. They are dead state.
  `finishBlit` also leaves the original's exit state: 92D2 = 92D3 = 0, bc = 0, Z set. Not reproduced: hl/de scratch.

## Tile collision (carry set = blocked), input ix = entity
`8F5C` left, `8F82` right, `8FB7` up, `8FDD` down. A tile with id bit 7 set is solid. Each probe is done at the
current position and one pixel further (entities move 2 px/step so any crossing of a tile boundary is seen);
a probe only looks at the tile map when the coordinate is on a tile boundary (x&3==0 / y&7==0). Left: x==0 is blocked
(screen edge); right: x==0x94 (148+12=160); up: y==0; down: y==0x78 (120+24=144). Left/right test the 3 tiles of the
neighbouring column (+1 row if y is not 8-aligned), up/down the 3 tiles of the neighbouring row (+1 if x not 4-aligned).
Output also `bc = (y, x')` where x'/y' is decremented/incremented when the first probe passed.
The unlabeled `8F52/8F78/8FAD/8FD3` single-probe wrappers (db in the listing) are never called.

## Animated tiles
* `901E buildAnimationDeltas`: on room load builds XOR deltas between consecutive animation frames (each frame =
  2 tiles side by side, tile ids 1E,20,22,24 -> 6880; 46,48,4A,4C -> 6900; 6E,70 -> 6980; 32 bytes per delta: 16 bytes left
  tile, 16 bytes right tile, 2 bytes per scan line). Deltas are cyclic (last wraps to first).
* `905B animateTiles` (called every frame from 8A97 and elsewhere): 5-step cycle on the counter 9375 (4..0).
  Step 4: advance phase (9376 += 0x20 mod 0x80) and XOR the two collectible items (5x4 byte image at 9333, positions
  6826/6827 and 6829/682A, y = 0xFF = absent). Steps 3,2,1: for every cell of the tile map (cpir scan; the cell right of a
  hit is skipped; a hit in the very last cell is ignored - quirk of `ret po`) holding tile 02 / 01 / 81 XOR the delta
  of the current phase onto the screen at 0xC000 + 2*cell (8 scan lines x 4 bytes). Step 0: nothing.
  Work variables 9377 (delta address) and 9379 (searched tile) are kept as the original leaves them.

## Slots
* `913E findFreeEnemySlot`: ix = first slot of the 8 at 93E8 with state 0xFF, carry set; none: carry clear.
* `9153` (helper of 9168; also registered but only reachable from lifted 9168): hl = first free 3-byte remnant record at 6834 (frame byte 0xFF), carry.
* `9168 enemyToRemnant`: frees enemy slot ix (state = 0xFF) and leaves its picture as a static remnant {y,x,frame}
  (y' = y + rows - 24 so the bottom edge is preserved) if a record is free.

## HUD
`91B2 addScore` (bc = BCD points added to 3-byte BCD score at 92D4 unless bit 7 of 6857 set; reprints the 6 digits at text
position 07B2 with pen mask 0F), `91DD printItemCount` (BCD at 684D, position 0798, mask F0), `91E8 printBcdPair`
(prints 2 BCD digits from (hl) through the text printer 8CFC, via `rld` nibble rotation, then hl++). 8CFC is called with `callLifted`.

## Not ported
* `8A97` (wait for frame flag `bit 7,(iy+72)`; clear it, call 81A2, 8540, then `jp 905B`): it contains a busy wait for
  an interrupt-set flag, which a NATIVE function cannot do (needs a framework "yield/wait" primitive). It stays lifted; it
  jumps into the native 905B. 8C30/8C4C/8C64 (screen/tile/sprite addresses) are in port_screen.cpp; the C++ in port_sprite.cpp
  uses local inline copies.
* The old 8AFC/8B36/8B65/8B96/8BAA/8BD5 internals no longer exist as routines.

## Verification
`native/tools/verify.sh` (seeds 1-6, 6000-20000 frames): 0 mismatches. Coverage per 6000 frames: saveEntity ~12000, moveEntity ~3900,
drawEntity ~25, eraseEntity 1-2, blocked* 800-2000 each, animateTiles ~3900, buildAnimationDeltas 2, addScore 1-2, printBcdPair ~55.
Rarely or never reached with random input (enemyToRemnant real path, findFreeEnemySlot, addScore carries, all branches of
animateTiles incl. last-cell hits) were verified with a fuzz harness that boots the game, then sets random plausible
RAM state (entities, tile maps, slot tables, BCD) and calls each native under the verifier: 20000+ calls per routine, 0 mismatches
except moveEntity with y >= 0xF0 (see the overlap caveat above).

## Bug found after the port (fixed)
8FDD (blockedDown) must leave HL at the third tile of the row below the sprite: the player's landing code (stateFly) reads it to decide
whether the player landed on river tiles. The first native version did not set HL, so the witch never drowned.
