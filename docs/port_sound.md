# Sorcery+ sound: music driver (MUCODE) and sound effects

Everything goes through the CPC firmware sound manager (SOUND QUEUE `BCAA`, envelopes `BCBC`/`BCBF`, implemented in
`native/src/sound.cpp`, 3 channels A=1 B=2 C=4, AY-3-8912 model). Time unit everywhere: 1/100 s.

## Sound block (SOUND QUEUE, 9 bytes at HL)
| off | meaning |
|---|---|
| 0 | status: bit0-2 channels (a sound for several channels is queued on each), bit3-5 rendezvous with A/B/C, bit6 hold, bit7 flush (clear queue, stop current) |
| 1 | amplitude envelope number (0 = none, 1..15) |
| 2 | tone envelope number |
| 3-4 | tone period (AY units, f = 62500/period; 0 = no tone) |
| 5 | noise period (0 = none, 1..31) |
| 6 | start volume 0..15 |
| 7-8 | duration, signed: >0 ticks; 0 = one pass of the amplitude envelope; <0 = envelope repeated -n times |

Envelopes are 16 bytes: byte 0 = number of sections (tone envelope: bit 7 = repeat forever), then up to 5 sections of
(step count, step size, pause). A step is taken immediately at the start of a section, then `pause` ticks wait. Amplitude steps add to the
volume (mod 16), tone steps add to the period (a count byte >= 240 in a tone section = absolute period). The game never uses AY hardware envelopes.

## Sound effects: `92BA` (SOUND.DAT, loaded to A600)
* `92BA`: `push af/bc/de/ix; call BCAA; pop ...` -- queue the sound block at HL. Natively `playSound` (`port_sound.cpp`).
* A600: amplitude envelopes n = 1..15 at A600 + 16n; A700: tone envelopes at A700 + 16n. Both are registered with the firmware at game start (7324-7346).
* A800-A886: 15 blocks of 9 bytes. The game plays a block with `ld hl,block ; call 92BA`; names in `port_sound.h` (guessed from the call sites):

| block | name | used when (call site) | channels / character |
|---|---|---|---|
| A800 | shoot | 796E, spell fired | ABC, tone env 1 falling, vol 15, one pass of ampl env 1 (0.4 s) |
| A809 | rise | 7B2B (+A812) | ABC, tone env 2 rising |
| A812 | click | 7B31 | ABC, noise 16, 10 ms |
| A81B | enemy_explode | 77E1, 81FC | ABC, noise 3, ampl env 2 (swell, decay) |
| A824 | player_death | 828B, with 8 debris sprites | B, tone env 3 (repeating), noise 16, env 3 x6 |
| A82D | pickup | 8060 | A, noise 31, ampl env 4 |
| A836 | blip | 8112, 8505 | C, tone env 4, 0.15 s |
| A83F | burst | 800A, 8 particles | A, noise 6, ampl env 5 |
| A848 | hit | 78AD, 839E, 8980 | B, tone env 5 (siren), ampl env 5 |
| A851 | zap | 7CA9 | ABC, noise 4, ampl env 6 x10 |
| A85A | thud | 7F64 | A, noise 8, ampl env 7 x8 |
| A863 | counter | 7863, 85FF, 862D, 85BD | C, 10 ms click, counter (energy/lives) changed |
| A86C | game_over | 8A12 | ABC, noise 24, ampl env 8 |
| A875 | level_done | 7CD6 | B, tone env 6, 20 ms |
| A87E | whoosh | 8245, 82F0 | ABC, noise 5, ampl env 9 (0.9 s) |

## Music driver MUCODE (4F00-531B, file MUCODE.BIN, loaded by the game at start of the title screen)
Entry points: `4F00` stop (`jp 4F38`), `4F03` start (called at 70D5 before the title), `4F51` the tick.
* `4F03` (`musicStart`): stream positions `4FE8/4FEA/4FEC` := 50D3, 5155, 5297; `SOUND AMPL ENVELOPE 1 := 500C` (2 sections: 1 step +0 pause 4, 5 steps -1 pause 20);
  `KL INIT EVENT` (block 4F4A, routine 4F51); `KL ADD TICKER` (block 4F44, first delay 100, then every tick = 50 Hz); `SOUND RESET`.
* `4F38` / `4F00` (`musicStop`): `KL DEL TICKER`, `KL SYNC RESET`, `SOUND RESET`. Called when a game starts (7169) and on exit (72A9).
* `4F51` (`musicTick`): for A (c=15), B (c=12), C (c=15): if `SOUND CHECK` shows a free queue slot, read `(note, duration)` from the stream (note FF = restart
  stream), look up the tone period `u16 @ 5013 + 2*(note-0x0C)`, fill the block at `5003` (status = channel, ampl env 1, no tone env, no noise, volume, duration = byte) and `SOUND QUEUE` it.
  The queue therefore runs a few notes ahead. (The `SOUND ARM EVENT` calls of the original are no-ops here.)
* Note numbers are MIDI numbers (0x0C = C0 = 16.35 Hz, 0x30 = 48, 0x54 = 84), the period table at 5013 has 0x61 entries (equal temperament, f = 62500/period). Durations are in 1/100 s
  (8, 16, 32, 48 = 1/16 ... 3/8 s; one 16 is a 0.16 s eighth note).
* Streams: A at 50D3 (64 notes, melody), B at 5155 (160 notes of 0.08 s, rapid arpeggio), C at 5297 (65 notes, the melody again, first note 8 instead of 16: a canon/echo 0.08 s early). Each stream is 12.80 s long and ends with `FF`; they loop together.
* There is exactly one tune (the title tune); in the game the music is switched off, only effects play.

## Verification
`native/tools/verify.sh 6000 1 2 3`: 4F00, 4F03, 4F38, 4F51 (246 calls/run) and 92BA (6-29 calls) with 0 mismatches. `AUDIO_VERIFY=1 tools/audio_export.bin` additionally
runs 1920 music ticks (three stream loops, including the `FF` wrap) under the verifier: 0 mismatches.
In verify mode the natives work on a scratch copy of the sound manager and skip the firmware calls (the original does the real ones afterwards).

## Export
`tools/audio_export.sh ../Sorcery.dsk assets/audio` (builds `tools/audio_export.cpp` on top of the Machine; calls the driver entry points `4F03`, `4F51`, `92BA`)
and `tools/audio_mp3.sh assets/audio` (LAME). See `assets/audio/README.md`.
