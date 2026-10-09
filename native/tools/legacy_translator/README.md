# Legacy: the Z80 -> C++ translator used during the port

The native version was first obtained by statically translating the original Z80 code to C++ (`lift.py`, `cyc.cpp`, `regen.sh`),
then every routine was rewritten by hand (see `docs/port_*.md`) and verified against the translation (`verify.sh`, `fuzz_*.cpp`)
before the translation was removed. These tools no longer build against the current tree (the translation, the Z80 core and
the verification mode of `Machine` are gone); they are kept for reference. The stage where both existed is in git history only if
you kept one - the translated program was bit-identical to the emulator (`emu/`).
