// Sorcery+ sound: RAM layout / data tables of the music driver (MUCODE.BIN at 4F00) and of the sound effect table
// (SOUND.DAT at A600).  See docs/port_sound.md for the formats.
#pragma once
#include <cstdint>

namespace sound {

// ---- SOUND.DAT (loaded at A600) --------------------------------------------------------------------------
constexpr uint16_t AMPL_ENVELOPES = 0xA600;   // envelope n (1..15) lives at +16*n, 16 bytes each (SOUND AMPL ENVELOPE format)
constexpr uint16_t TONE_ENVELOPES = 0xA700;   // same for tone envelopes
constexpr uint16_t SFX_TABLE = 0xA800;        // 15 sound blocks of 9 bytes (SOUND QUEUE parameter blocks)
constexpr int SFX_COUNT = 15;
constexpr int SFX_SIZE = 9;

// The game selects an effect by passing the address of its block to 92BA ("play sound block at HL").
enum Sfx : uint16_t {
    SFX_SHOOT = 0xA800,          // spell fired (all 3 channels, falling sweep)
    SFX_COLLECT_RISE = 0xA809,   // rising sweep, played with SFX_CLICK when a room puzzle is solved
    SFX_CLICK = 0xA812,          // 10 ms noise tick
    SFX_ENEMY_EXPLODE = 0xA81B,  // noise burst, long decay (enemy destroyed)
    SFX_PLAYER_DEATH = 0xA824,   // low buzzing explosion (player dies, 8 debris sprites spawned)
    SFX_PICKUP = 0xA82D,         // object picked up (noise swish)
    SFX_BLIP = 0xA836,           // short tone blip on channel C
    SFX_BURST = 0xA83F,          // object bursts into 8 particles
    SFX_HIT = 0xA848,            // medium hit on channel B
    SFX_ZAP = 0xA851,            // all-channel zap
    SFX_THUD = 0xA85A,           // dull thud
    SFX_COUNTER = 0xA863,        // energy / lives counter changes (+1 or -1)
    SFX_GAME_OVER = 0xA86C,      // long noise decay
    SFX_LEVEL_DONE = 0xA875,     // two-tone jingle on channel B
    SFX_WHOOSH = 0xA87E,         // long slow noise
};

// ---- MUCODE.BIN (music driver, loaded at 4F00) ------------------------------------------------------------
constexpr uint16_t MU_STOP = 0x4F00;          // jp 4F38
constexpr uint16_t MU_INIT = 0x4F03;
constexpr uint16_t MU_STOP_IMPL = 0x4F38;
constexpr uint16_t MU_TICKER_BLOCK = 0x4F44;  // KL ticker block (13 bytes)
constexpr uint16_t MU_EVENT_BLOCK = 0x4F4A;   // KL event block, routine = MU_TICK
constexpr uint16_t MU_TICK = 0x4F51;          // called 50 times / s once the ticker has started (first call after 2 s)
constexpr uint16_t MU_POS = 0x4FE8;           // 3 x u16: current read position of channels A, B, C
constexpr uint16_t MU_SOUND_BLOCK = 0x5003;   // the sound block handed to SOUND QUEUE
constexpr uint16_t MU_AMPL_ENV = 0x500C;      // amplitude envelope 1 used by the music
constexpr uint16_t MU_NOTE_PERIODS = 0x5013;  // u16 tone periods, index = note - NOTE_FIRST
constexpr int NOTE_FIRST = 0x0C;
constexpr uint16_t MU_END_MARK = 0xFF;

struct Voice { uint16_t start; uint8_t volume; };
constexpr Voice VOICES[3] = {{0x50D3, 0x0F}, {0x5155, 0x0C}, {0x5297, 0x0F}};   // melody, arpeggio, echo melody

}  // namespace sound
