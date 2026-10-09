// CPC firmware sound manager (SOUND QUEUE / envelopes) driving an AY-3-8912 model.
//
// The firmware semantics modelled here (all times in 1/100 s, i.e. one SoundMgr::tick()):
//  * three channels (A=1, B=2, C=4), each with a queue of 4 pending sounds plus the sound being played;
//  * a sound is a 9-byte block: status, amplitude envelope no., tone envelope no., tone period (16 bit),
//    noise period, start volume, duration (16 bit, signed);
//  * duration > 0: play that many ticks; 0: play one pass of the amplitude envelope; < 0: play the
//    amplitude envelope -duration times (the volume is not reset between repeats);
//  * amplitude envelope sections (count, step size, pause) change the volume, tone envelope sections change the tone
//    period; bit 7 of the tone envelope's section count makes it repeat endlessly;
//  * status bits: 1/2/4 channels, 8/16/32 rendezvous with A/B/C, 0x40 hold, 0x80 flush.
// Hardware (AY) envelopes in amplitude envelopes are not implemented (the game does not use them), the AY
// model itself has a hardware envelope generator for completeness.
#pragma once
#include <array>
#include <cstdint>
#include <deque>
#include <mutex>
#include <vector>

class AY {
public:
    void reset();
    void write(int reg, uint8_t v);
    // Remembers the current register set as one "time slice" (SoundMgr calls this every 1/100 s). mix() spreads the
    // pending slices evenly over the samples it generates, so register changes between video frames are not lost.
    void latch();
    // Generates mono signed 16-bit samples at `rate` Hz (PSG clock = 1 MHz).
    void mix(int16_t* out, int n, int rate);
private:
    typedef std::array<uint8_t, 16> Regs;
    void startEnvelope();
    void stepEnvelope();
    double tickPsg();        // advances the chip by one clock/8 tick and returns the instantaneous (unfiltered) output
    Regs r{};
    std::deque<Regs> pending;
    int toneCnt[3] = {1, 1, 1};
    bool toneOut[3] = {false, false, false};
    int noiseCnt = 1;
    uint32_t lfsr = 1;
    bool noiseOut = false;
    int envCnt = 1, envPos = 0, envVol = 0;
    bool envAttack = false, envHold = false, envRestart = false;
    double tickFrac = 0, dc = 0;
};

class SoundMgr {
public:
    SoundMgr() { reset(); }
    void reset();
    bool queue(const uint8_t* p);            // SOUND QUEUE (8/9 byte block); false if a queue is full
    int check(int chanMask);                 // SOUND CHECK status byte
    bool setAmpl(int n, const uint8_t* d);   // SOUND AMPL ENVELOPE (16 byte block)
    bool setTone(int n, const uint8_t* d);   // SOUND TONE ENVELOPE
    void release(int mask);                  // SOUND RELEASE
    void hold();                             // SOUND HOLD
    void cont();                             // SOUND CONTINUE
    void tick();                             // one centisecond (100 Hz)
    AY psg;

    struct Snd { uint8_t status, ampl, tone; uint16_t period; uint8_t noise, vol; int16_t dur; };
private:
    struct Env { uint8_t d[16] = {}; bool valid = false; };
    Env ampl[16], tone[16];
    // Run-time state of one envelope (amplitude or tone) of the playing sound.
    struct EnvRun {
        int sec = 0, step = 0, wait = 0;
        bool done = true;
        void start(bool active) { sec = step = wait = 0; done = !active; }
    };
    struct Chan {
        std::vector<Snd> q;
        bool active = false, waiting = false, held = false, released = false;
        Snd cur{};
        int remaining = 0, repeats = 0;
        int vol = 0, period = 0;
        EnvRun a, t;
    } ch[3];
    bool allHeld = false;
    void start(int c, const Snd& s);
    void stepAmpl(Chan& c);
    void stepTone(Chan& c);
    void tryStart(int c);
    void output();
};
