// Sorcery+ sound: native replacement of the music driver MUCODE (4F00-4FE7) and of the sound effect player (92BA).
//
// Music: three streams of (note, duration) byte pairs, one per AY channel, endlessly looped.  A KL ticker calls the
// "tick" routine 50 times a second; for every channel that has room in its SOUND queue it queues the next note.
// Effects: 92BA just hands a 9-byte sound block (see port_sound.h) to SOUND QUEUE.
#include "native.h"
#include "port_sound.h"
using namespace sound;

namespace {

uint16_t rd16(const Machine& m, uint16_t a) { return m.ram[a] | m.ram[(uint16_t)(a + 1)] << 8; }
void wr16(Machine& m, uint16_t a, uint16_t v) { m.ram[a] = v & 0xFF; m.ram[(uint16_t)(a + 1)] = v >> 8; }

SoundMgr& soundSink(Machine& m) { return m.snd; }

void firmware(Machine& m, uint16_t addr) { m.call(addr); }

// 4F38: stop the music: remove the ticker, reset the sound manager.
void stopMusic(Machine& m) {
    Z80& c = m.cpu;
    c.setHL(MU_TICKER_BLOCK);
    firmware(m, 0xBCEC);   // KL DEL TICKER
    firmware(m, 0xBCF5);   // KL SYNC RESET
    m.snd.reset();   // SOUND RESET
}

}  // namespace

NATIVE(0x4F00, musicStop, "", 64) { stopMusic(m); }
NATIVE(0x4F38, musicStopImpl, "", 54) { stopMusic(m); }

// 4F03: start the music: rewind the three streams, define the amplitude envelope, install the 50 Hz ticker.
NATIVE(0x4F03, musicStart, "", 216) {
    Z80& c = m.cpu;
    for (int ch = 0; ch < 3; ch++) wr16(m, MU_POS + 2 * ch, VOICES[ch].start);
    m.snd.setAmpl(1, &m.ram[MU_AMPL_ENV]);                 // SOUND AMPL ENVELOPE 1
    c.setHL(MU_EVENT_BLOCK); c.b = 0x81; c.c = 0x00; c.setDE(MU_TICK);
    m.call(0xBCEF);                                        // KL INIT EVENT (asynchronous, class 1)
    c.setHL(MU_TICKER_BLOCK); c.setDE(100); c.setBC(1);
    m.call(0xBCE9);                                        // KL ADD TICKER: first after 2 s, then every tick
    m.snd.reset();                                         // SOUND RESET
}

// 4F51: the 50 Hz tick of the music.  For each channel with free queue space, queue the next note of its stream.
// Stream format: note (MIDI-like, 0x0C = lowest), duration in 1/100 s; note 0xFF = restart the stream.
NATIVE(0x4F51, musicTick, "", 344) {
    SoundMgr& snd = soundSink(m);
    for (int ch = 0; ch < 3; ch++) {
        // The driver builds the SOUND QUEUE block in RAM; keep that visible (callers do not read it, the verifier does).
        m.ram[MU_SOUND_BLOCK] = 1 << ch;
        if ((snd.check(1 << ch) & 7) == 0) continue;           // queue full
        uint16_t pos = rd16(m, MU_POS + 2 * ch);
        uint8_t note = m.ram[pos];
        if (note == MU_END_MARK) { pos = VOICES[ch].start; note = m.ram[pos]; }   // loop
        uint8_t duration = m.ram[(uint16_t)(pos + 1)];
        wr16(m, MU_POS + 2 * ch, pos + 2);
        // Tone period from the note table (index wraps like the original 8-bit table arithmetic).
        uint8_t idx = (uint8_t)((uint8_t)(note - NOTE_FIRST) * 2);
        m.ram[MU_SOUND_BLOCK + 3] = m.ram[MU_NOTE_PERIODS + idx];
        m.ram[MU_SOUND_BLOCK + 4] = m.ram[MU_NOTE_PERIODS + idx + 1];
        m.ram[MU_SOUND_BLOCK + 6] = VOICES[ch].volume;
        m.ram[MU_SOUND_BLOCK + 7] = duration;
        snd.queue(&m.ram[MU_SOUND_BLOCK]);                      // SOUND QUEUE (ampl env 1, no tone env, no noise)
    }
}

// 92BA: play the sound effect whose 9-byte SOUND QUEUE block is at HL.  Preserves all registers.
NATIVE(0x92BA, playSound, "", 119) {
    uint8_t block[SFX_SIZE];
    uint16_t p = m.cpu.hl();
    for (int i = 0; i < SFX_SIZE; i++) block[i] = m.ram[(uint16_t)(p + i)];
    soundSink(m).queue(block);
}
