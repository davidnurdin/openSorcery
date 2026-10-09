# Sorcery+ audio

Rendered offline (44.1 kHz, mono, 16 bit) by `tools/audio_export.sh` using the native firmware sound manager + AY-3-8912 model,
driven through the game's own entry points (`4F03`/`4F51` music, `92BA` effects). MP3 (LAME -V2) next to each WAV.
Effect names are guessed from the call sites; the address is the 9-byte sound block in SOUND.DAT (A800-A886). Details: `docs/port_sound.md`.

| file | source | length |
|---|---|---|
| music_title.wav / .mp3 | the title tune (3 channels), 3 loops of 12.8 s + tail | 39.4 s |
| sfx_shoot | A800 spell fired | 0.66 s |
| sfx_rise | A809 rising sweep | 0.66 s |
| sfx_click | A812 10 ms noise tick | 0.26 s |
| sfx_enemy_explode | A81B | 1.42 s |
| sfx_player_death | A824 | 1.87 s |
| sfx_pickup | A82D | 0.96 s |
| sfx_blip | A836 | 0.40 s |
| sfx_burst | A83F | 1.31 s |
| sfx_hit | A848 | 1.31 s |
| sfx_zap | A851 | 1.25 s |
| sfx_thud | A85A | 2.01 s |
| sfx_counter | A863 (a 10 ms click) | 0.26 s |
| sfx_game_over | A86C | 1.97 s |
| sfx_level_done | A875 | 0.27 s |
| sfx_whoosh | A87E | 1.16 s |

(Files include 50 ms of leading and 200 ms of trailing silence for the effects.)
