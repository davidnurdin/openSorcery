# Architecture of the native port (`native/`)

The program is plain C++17 + SDL2. There is no Z80 emulation and no translated code: every routine of the original game
(CODE.BIN at 0x7000-0x92FF, MUCODE.BIN at 0x4F00) was rewritten by hand.

## Layout
* `machine.{h,cpp}` - the "hardware": 64 KB RAM image (the original keeps all its data at fixed addresses and the port keeps them),
  CPC video (gate-array palette + mode, recorded per scanline so the original's raster split - mode 0 playfield / mode 1 text area -
  renders correctly), joystick/keyboard, resources (`common/resources.*`: PNG/data/sector files, no .dsk), AY sound chip + firmware sound manager (`sound.*`), the firmware
  services the game used (`Machine::firmware`: palette, keys, disc, events...), the interrupt scheduler and the game task.
* **Interrupts / time.** The original is paced by 6 interrupts per frame. `Machine::runFrame` delivers them at the scanlines of a
  real CPC (`gameIrq` = the game's own handler: phase table, joystick, status-area palette + mode, energy-bar fade, tick counters;
  `fwInterrupt` = firmware: frame-flyback event, ink flash, tickers).
* **The game task.** The game logic is straight-line C++ running as a coroutine (ucontext) started with `Machine::startGame`.
  Wherever the original waited (`halt`, polling a flag set by the interrupt handler, delay loops) it calls `Machine::waitIrq()`
  (`flow::waitTick()`, `flow::timedLoop()` in `port_flow_task.cpp`), which hands control back until the next interrupt.
* `port_*.cpp` - the game, one file per area (see `docs/port_*.md`): `port_flow_*` boot/attract/title/hall of fame/game over,
  `port_game` the in-game loop, `port_player*`, `port_enemy*`, `port_handlers1`, `port_sprite` (XOR sprite engine, collision, animated
  tiles), `port_text` (text, rooms, tiles, disc), `port_sound` (music driver, effects), `port_screen`.
* Routines are registered with `NATIVE(0xADDR, name, "outs", cost) {...}` and called with `Machine::call(addr)`. The original
  address is the routine's identity (it is how the original tables, e.g. entity handlers at 0x937A, refer to them) and the register
  convention of the original is kept (`m.cpu.a`, `hl()`, ... in `regs.h`); `alu.h` has Z80-exact flag helpers for the few places where
  callers look at flags.

## How it was made (and verified)
1. Extract/decrypt the files from the DSK (`tools/extract_assets.py`, `tools/make_resources.cpp`), disassemble (`docs/z80_listing.txt`).
2. A Z80 emulator version (`emu/`) runs the original code with firmware shims.
3. A static Z80->C++ translator (`native/tools/legacy_translator`) produced a bit-identical native version of that code.
4. Every routine was rewritten by hand and checked against the translation: each call was executed natively and by the original on the same
   state and RAM/registers were compared (0 mismatches over many random-input runs and fuzzers), then the translation was deleted.
5. The final assembled program was tested with long random-input runs, the attract mode/demo, name entry and the victory sequence;
   it was not compared bit for bit with the emulator (timing at frame level differs slightly: natives take no CPU time).

## Known differences from the original
* One frame of wrong palette when a game starts; the screen "dissolve" (8E8C) is instant; firmware text output (the "TRICHEZ..."
  message of initiation mode) is not rendered; enemy shots use a xorshift random source (the original read the Z80 R register).
* Not tested at all: the real-time SDL window/audio on a desktop (only headless captures and a dummy-driver start were run).
