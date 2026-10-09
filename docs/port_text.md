# Port notes: text, rooms, tiles, UI helpers (native/src/port_text.cpp, port_text.h)

All routines below are NATIVE() and verified (SORCERY_VERIFY, seeds 1-13, 3000-6000 frames): 0 mismatches.

## Text printer (8C77-8D1F)
* Strings: chars < 0x80, **last char has bit 7 set**. Cursor DE = screen *offset* (screen = C000 + offset, 80 bytes per char row,
  2 bytes per glyph = 4 mode-0 pixels). Offset 0x0640 = message line 1, 0x06E0 = message line 2, 0x07B2 score, 0x0798 energy.
* 8D1F glyph blitter: font at 0100, 16 bytes/glyph (8 rows x 2 bytes), each byte ANDed with the pen mask at 9374 (FF, 0F, F0 select
  colours). Draws rows until the address high byte overflows (so cursors not on line 0 of a cell draw fewer rows). DE += 2. Preserves all else.
* 8CFC print char / control code: >=0x20 glyph; 0x1F + 2 bytes (lo,hi) = set cursor; 0x1E + byte = set pen mask;
  0x00-0x1D = print phrase #(code+1) of the phrase table at 9A44 (nested strings may themselves contain codes).
* 8CE4 print string at HL; 8CDC print string number B (0-based, B strings skipped) of a table at HL; 8CF6 inline string
  (`call 8CF6 / db "TEXT"+80h`, return address moves behind the string; HL preserved). **8CF6 is NOT a native**: it is
  `ex (sp),hl / call 8CE4 / ex (sp),hl / ret`, already native through 8CE4, and cannot be verified (the verifier
  runs the original with a sentinel return address, i.e. it would read the string from there).
* 8CBD clear rectangle: zero bytes [DE..HL] (offsets) on all 8 pixel lines of that char row (nothing if HL<=DE).
* 8C77 message line 1: string (ROOMREC+0)+1 of table 9CB7 at 0640, rest of line to 067F cleared.
* 8C8E message line 2: ROOMREC+70 searched in the 18-byte key list at 9CA4 (sentinel written at 9CB6); if found, string #(index+1) of
  table 9AC8, or 9BB9 if ROOMREC+76 bit 7 (language/alternative table) at 06E0, line cleared to 071F.
* 91E8 print packed BCD byte (2 digits), HL++; 91DD energy = byte at 684D at 0798 mask F0; 91CA score = 3 BCD bytes at 92D4 (big endian) at 07B2
  mask 0F; 91B2 add BC (BCD) to the score (unless ROOMREC+87 bit 7) then print it.

## Rooms and tiles
* 8DE0 loadRoom: room n = byte 9310 (n < 0x4F else fatal `rst 8`). Disc sector: track 0x1F+n/9, id 0xC1+n%9, 512 bytes to 5600, then
  92A5 (wait flyback). Format: "DC ", n, RLE stream decoded to the 720-byte tile map at 6C90: FF = end, 00 k = k zeros (k=0 -> 256),
  other byte = literal tile id. Tile maps are 40x18 (tile = 4x8 px, 2 bytes x 8 lines); tile graphics planar at 5800 (byte j of tile t at 5800+j*256+t).
* 8EBC draw one tile; 8EA6 draw whole map (tile i -> C000+2i); 8E8C screen clear in a "dissolve" order (step 0x22D through the 16 KB
  screen, a delay loop after every byte - ~5.4M cycles, ~20 frames in the original; native version is instant, only timing differs).
* 8ED6 classifyTiles: converts the id map into a collision map in place: ids 0 and >0xDC -> 0; 1E/1F -> 81,81; 46/47 -> 01,01; 6E/6F -> 02,02
  (pairs overwrite the next cell too, as in the original; these are the three animated hazard tile groups); everything else -> 80 (solid).
  Room 3D special case: while 9667==0, if ROOMREC+70==2B then 9667=1, else tile F7 -> 01 first.
* 901E: builds the XOR delta frames of the animated tile groups (1E-25, 46-4D: 4 two-tile frames; 6E-71: 2 frames) at 6880..69BF
  (16 bytes per tile); the animation driver 905B XORs them onto the screen.
* 8F3C setPalette: HL = 16 inks, A = border ink; uses SCR SET INK / SET BORDER shims (via `m.hook`).

## Misc
* 91F8 loader: IX list of 5-byte entries (name len, name ptr, dest); loads from the DSK, writes length to 9255, decrypts (plain = ror1(-b)-0x25).
* 9257 (interrupt phase handler, entered via 8DB9) / 926B: energy bar fade. Every 24th call (counter 92DB; skipped if 6850 bit 7), AND the
  mask 92DC (77,BB,DD,EE rotating) into the bar byte at 92D7; after a byte is done advance, 16 bytes/line (92D9 low), lines left
  in the high byte; when exhausted set ROOMREC+79 bits 7 and 2 (out of energy). 926B is the step without the timing (called directly at 7CD3).
* 92A5: MC WAIT FLYBACK through low ROM `call 001E` (see below), then 92F7=0. 92BA: SOUND QUEUE with all registers/flags preserved.

## Not done / framework notes
* **8E36 (title screen draw) and 8E5B (tile reveal)** stay lifted: 8E5B has a `halt` per tile (720 tiles, one interrupt each = reveal animation).
  A native cannot wait for interrupts and callLifted would hang. Framework would need e.g. `Machine::waitInterrupt()`.
* 8F52-9006 (probe/collision helpers), 905B-9153 (tile animation driver, entity helpers) were outside my assigned ranges; untouched.
* Verify coverage is low (a few calls) for 8C77, 8C8E, 8DE0, 8ED6, 8F3C, 901E, 91B2, 91F8; the paths specific to room 3D, the end of the
  energy bar (death flags), and the direct entry 926B were never hit by the random runs (checked by reading only).
* Framework issue: `call 001E` in 92A5 executes RAM at 0x1E (zeros, i.e. NOPs until the RST 38 jump to the IRQ shim). Another agent patched
  ram[0x1E]=0xE9 locally; my native just calls `m.callLifted(0x001E)` so it follows whatever the framework does.
