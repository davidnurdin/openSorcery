# Entity update loop and scripted-object handlers (`native/src/port_enemy.cpp`)

Native C++ for the Z80 routines 81A2, 81BD, 81DE-844D (types 0x20-0x36) and 850C. No translated code remains for them.

| addr | native name | what it does |
|---|---|---|
| 81A2 | updateEntities | updates the 2 permanent entities (6802, 680C) and the 9 slots at 93E8 (stride 10) |
| 81BD | updateEntity | unused slot (`state & 0x7F == 0x7F`): skip. Else save the entity (8AAA), look up the handler in the word table 937A, run it through `callLifted` (what `call 001E` = `jp hl` did), ix += 10, de = 10 |
| 81DE | fallingObject | type 0x20: falls 1 line/frame to y 0x70 or until blocked (then 9168 settles it as a floor object) |
| 8205 | landingBounce | 0x21: landing animation (frames 0x48..0x4C..0x47), then becomes 0x22 |
| 824E | floatUpToFloor | 0x22: rises 24 lines, then settles on the floor |
| 826B / 8292 | deathBurstEight / Four | 0x23 / 0x24: free the entity, erase all bolts, restart 8 (or 4) bolts at the player |
| 82BD | spawnRewardEffect | 0x25: takes the next position of table 945C (index 945B), +2000 points |
| 82F9 / 831B | rewardShrink / rewardSparkle | 0x26 / 0x27: animation, then settle as floor object |
| 8341 | vanishAnimation | 0x28: 4 frames then free |
| 835E, 836A, 8374, 838E | waitThenAdvance, showFrame39, finalFrameDebris, delayThenAlarm | 0x29-0x2C small delays / sound / debris burst |
| 83A5 | followYScript | 0x2D: y-delta script through the pointer in a8/a9, 0x80 ends |
| 83C5, 83D0 | timerThenRestore, frameThenRestore | 0x2E / 0x2F: return to the state saved in a6 |
| 83DF-842D | scriptStep30..34 | 0x30-0x34: set up the switch states and patch room bytes 06/0C/10 |
| 8440, 844D | advanceWithSound, fadeOutAnimation | 0x35 / 0x36 |
| 850C | eraseAllDebris | erase all 8 debris particles (shared helper with 851B) |

The behaviours 0-9 (7D32-7FCA) are not in this file; 81BD runs them through `callLifted`, so they work whether or not they are native.

## Regeneration note
`native/tools/regen.sh` only scans the first 40 entries of the handler table 937A (`w(0x937a,40)`), so handlers 0x28-0x36
(8341..844D) were never found as code and 850C is also a `db` block. I appended these addresses to `native/tools/extra_entries.txt`
(alternatively change 40 to 0x38 in regen.sh). Without them SORCERY_VERIFY compares against nothing (the original does nothing).

## Verification
* `native/tools/fuzz_enemy.cpp`: boots, then sets random plausible state (entity of the right type in any of the 12 slots, random player
  position, bolts/debris, tile map, floor records) and calls every routine under the verifier: 20000 calls (~1100 per routine), 0 mismatches.
  81BD / 81A2 are fuzzed with types 0-9 (lifted code), 0x20-0x36 and unused slots.
* Random play (`--rand-input 1..12`, 2500 frames): 81A2/81BD thousands of calls, 0 mismatches. The play-through rarely reaches 0x20+ handlers.
* Practical note: full verification (every native verifies nested natives, each running the original too) is exponentially slow
  after ~1200 frames; a private patch of `Machine::native` with an env var `SORCERY_VERIFY_ONLY="81BD 81DE ..."` (only listed natives
  verified, others just run) makes it manageable. It is not part of the deliverable.
