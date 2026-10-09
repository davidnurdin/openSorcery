#include "sound.h"
#include <algorithm>
#include <cmath>
#include <cstring>

// ---------------------------------------------------------------- AY-3-8912
// PSG clock 1 MHz (CPC). Tone/noise/envelope counters run on clock/8 ticks (125 kHz):
//   tone  frequency = 62500 / period       (square wave toggles every `period` ticks)
//   noise shift rate = 62500 / period      (LFSR shifts every 2*period ticks)
//   envelope step rate = 62500 / period    (16 steps per ramp)
static const double PSG_TICK_HZ = 125000.0;

void AY::reset() {
    r.fill(0); r[7] = 0xFF;
    pending.clear();
    for (int c = 0; c < 3; c++) { toneCnt[c] = 1; toneOut[c] = false; }
    noiseCnt = 1; lfsr = 1; noiseOut = false;
    envCnt = 1; envPos = 0; envVol = 0; envAttack = false; envHold = true; envRestart = false;
    tickFrac = 0; dc = 0;
}

void AY::write(int reg, uint8_t v) {
    reg &= 15;
    r[reg] = v;
    if (reg == 13) envRestart = true;
}

void AY::latch() {
    pending.push_back(r);
    if (pending.size() > 50) pending.pop_front();   // nobody is listening: keep the most recent half second
}

void AY::startEnvelope() {
    envAttack = r[13] & 4;
    envPos = 0; envHold = false;
    envVol = envAttack ? 0 : 15;
}

void AY::stepEnvelope() {
    if (envHold) return;
    if (++envPos < 16) { envVol = envAttack ? envPos : 15 - envPos; return; }
    // end of a ramp: apply the continue / alternate / hold bits of the shape
    int shape = r[13] & 15;
    int endLevel = envAttack ? 15 : 0;
    if (!(shape & 8)) { envHold = true; envVol = 0; return; }
    if (shape & 1) { envHold = true; envVol = (shape & 2) ? 15 - endLevel : endLevel; return; }
    if (shape & 2) envAttack = !envAttack;
    envPos = 0;
    envVol = envAttack ? 0 : 15;
}

double AY::tickPsg() {
    static double level[16];
    static bool init = false;
    if (!init) {   // measured YM2149/AY-3-8910 DAC levels
        static const double lv[16] = {0, 0.00999, 0.01445, 0.02105, 0.03070, 0.04555, 0.06449, 0.10736,
                                      0.12659, 0.20498, 0.29221, 0.37283, 0.49253, 0.63532, 0.80558, 1.0};
        memcpy(level, lv, sizeof level);
        init = true;
    }
    if (envRestart) { envRestart = false; startEnvelope(); }
    int mixer = r[7];
    for (int c = 0; c < 3; c++) {
        // While the tone is gated off (period 0 / mixer bit set) keep the divider re-armed, so that a tone switched on
        // starts with a rising edge (a one-tick sound like the game's 10 ms counter click then yields a pulse).
        if (mixer >> c & 1) { toneCnt[c] = 1; toneOut[c] = false; }
        if (--toneCnt[c] <= 0) {
            toneCnt[c] = std::max(1, r[c * 2] | (r[c * 2 + 1] & 15) << 8);
            toneOut[c] = !toneOut[c];
        }
    }
    if (--noiseCnt <= 0) {
        noiseCnt = 2 * std::max(1, r[6] & 31);
        uint32_t bit = (lfsr ^ (lfsr >> 3)) & 1;
        lfsr = (lfsr >> 1) | (bit << 16);
        noiseOut = lfsr & 1;
    }
    if (--envCnt <= 0) {
        envCnt = 2 * std::max(1, r[11] | r[12] << 8);
        stepEnvelope();
    }
    double acc = 0;
    for (int c = 0; c < 3; c++) {
        bool t = toneOut[c] || (mixer >> c & 1);
        bool n = noiseOut || (mixer >> (c + 3) & 1);
        if (t && n) acc += level[(r[8 + c] & 16) ? envVol : (r[8 + c] & 15)];
    }
    return acc;
}

void AY::mix(int16_t* out, int n, int rate) {
    size_t slices = pending.size();
    std::deque<Regs> pend;
    pend.swap(pending);
    for (int s = 0; s < n; s++) {
        if (slices) r = pend[std::min<size_t>(slices - 1, (size_t)s * slices / n)];
        tickFrac += PSG_TICK_HZ / rate;
        int nt = (int)tickFrac;
        tickFrac -= nt;
        double acc = 0;
        for (int i = 0; i < nt; i++) acc += tickPsg();
        double x = nt ? acc / nt : 0;
        dc += (x - dc) * 0.0008;   // remove the DC offset of the unipolar mix
        double v = (x - dc) * 9000.0;
        out[s] = (int16_t)std::max(-32000.0, std::min(32000.0, v));
    }
    if (slices) r = pend.back();
}

// --------------------------------------------------------------- sound manager
void SoundMgr::reset() {
    for (auto& c : ch) c = Chan();
    allHeld = false;   // (envelopes survive SOUND RESET: MUCODE defines envelope 1 and then resets)
    psg.reset();
}

bool SoundMgr::setAmpl(int n, const uint8_t* d) {
    if (n < 1 || n > 15) return false;
    memcpy(ampl[n].d, d, 16); ampl[n].valid = true; return true;
}
bool SoundMgr::setTone(int n, const uint8_t* d) {
    if (n < 1 || n > 15) return false;
    memcpy(tone[n].d, d, 16); tone[n].valid = true; return true;
}

bool SoundMgr::queue(const uint8_t* p) {
    Snd s;
    s.status = p[0]; s.ampl = p[1]; s.tone = p[2];
    s.period = p[3] | p[4] << 8; s.noise = p[5]; s.vol = p[6];
    s.dur = (int16_t)(p[7] | p[8] << 8);
    // a sound addressed to several channels is queued on each of them (all queues must have room)
    for (int c = 0; c < 3; c++)
        if ((s.status >> c & 1) && !(s.status & 0x80) && ch[c].q.size() >= 4 && (ch[c].active || ch[c].waiting)) return false;
    for (int c = 0; c < 3; c++) {
        if (!(s.status >> c & 1)) continue;
        Chan& k = ch[c];
        if (s.status & 0x80) { k.q.clear(); k.active = false; k.waiting = false; k.held = false; k.released = false; }   // flush
        k.q.push_back(s);
        tryStart(c);
    }
    return true;
}

int SoundMgr::check(int mask) {
    int c = (mask & 1) ? 0 : (mask & 2) ? 1 : (mask & 4) ? 2 : 0;
    Chan& k = ch[c];
    int st = 4 - (int)k.q.size();
    if (k.waiting || k.held) st |= (k.q.empty() ? 0 : (k.q.front().status & 0x38));
    if (k.held) st |= 0x40;
    if (k.active) st |= 0x80;
    return st & 0xFF;
}

void SoundMgr::release(int mask) {
    for (int c = 0; c < 3; c++) if (mask >> c & 1) { ch[c].held = false; ch[c].released = true; tryStart(c); }
}
void SoundMgr::hold() { allHeld = true; }
void SoundMgr::cont() { allHeld = false; }

void SoundMgr::start(int c, const Snd& s) {
    Chan& k = ch[c];
    k.cur = s; k.active = true; k.waiting = false; k.held = false; k.released = false;
    k.vol = s.vol & 15; k.period = s.period;
    k.a.start(s.ampl >= 1 && s.ampl <= 15 && ampl[s.ampl].valid && (ampl[s.ampl].d[0] & 0x7F) > 0);
    k.t.start(s.tone >= 1 && s.tone <= 15 && tone[s.tone].valid && (tone[s.tone].d[0] & 0x7F) > 0);
    k.remaining = s.dur > 0 ? s.dur : 0;
    k.repeats = s.dur < 0 ? -s.dur : 1;
}

// Starts the sound at the head of the queue if the channel is idle (handling hold and rendezvous).
void SoundMgr::tryStart(int c) {
    Chan& k = ch[c];
    if (k.active || k.q.empty()) return;
    const Snd& s = k.q.front();
    if ((s.status & 0x40) && !k.released) { k.held = true; return; }
    if (k.held) return;
    int rv = (s.status >> 3) & 7;
    if (rv) {
        k.waiting = true;
        // every channel we rendezvous with must be waiting for us
        for (int o = 0; o < 3; o++) {
            if (!(rv >> o & 1)) continue;
            Chan& ok = ch[o];
            if (!ok.waiting || ok.q.empty() || !((ok.q.front().status >> 3 & 7) >> c & 1)) return;
        }
        for (int o = 0; o < 3; o++) {
            if (!(rv >> o & 1)) continue;
            Snd t = ch[o].q.front(); ch[o].q.erase(ch[o].q.begin());
            start(o, t);
        }
    }
    Snd t = k.q.front(); k.q.erase(k.q.begin());
    start(c, t);
}

// Advances one envelope by one tick. `apply(sectionBytes)` performs one step; returns when the envelope is exhausted.
// Section = (step count, step size, pause); the first step of a section is taken immediately, then `pause` ticks wait.
template <class Apply>
static void runEnv(const uint8_t* d, bool repeat, int& sec, int& step, int& wait, bool& done, Apply apply) {
    if (done) return;
    if (wait > 0 && --wait > 0) return;
    int count = std::min(d[0] & 0x7F, 5);
    for (int guard = 0; guard < 16; guard++) {
        if (sec >= count) {
            if (!repeat) { done = true; return; }
            sec = step = 0;
            continue;
        }
        const uint8_t* s = d + 1 + 3 * sec;
        if (step >= s[0] || s[0] == 0) { sec++; step = 0; continue; }
        apply(s);
        step++;
        wait = std::max<int>(s[2], 1);
        return;
    }
}

void SoundMgr::stepAmpl(Chan& k) {
    const uint8_t* d = ampl[k.cur.ampl].d;
    runEnv(d, false, k.a.sec, k.a.step, k.a.wait, k.a.done, [&](const uint8_t* s) {
        if (s[0] & 0x80) return;   // hardware envelope section: not supported
        k.vol = (k.vol + (int8_t)s[1]) & 15;
    });
    // negative duration: run the whole envelope -dur times (the volume carries over between passes)
    if (k.a.done && k.cur.ampl && k.cur.dur < 0 && k.repeats > 1) { k.repeats--; k.a.start(true); }
}

void SoundMgr::stepTone(Chan& k) {
    const uint8_t* d = tone[k.cur.tone].d;
    runEnv(d, d[0] & 0x80, k.t.sec, k.t.step, k.t.wait, k.t.done, [&](const uint8_t* s) {
        if (s[0] >= 240) { k.period = ((s[0] & 15) << 8) | s[1]; return; }   // absolute period section
        k.period = (k.period + (int8_t)s[1]) & 0xFFF;
    });
}

void SoundMgr::tick() {
    if (!allHeld) {
        for (int c = 0; c < 3; c++) {
            Chan& k = ch[c];
            if (!k.active) { tryStart(c); continue; }
            stepAmpl(k); stepTone(k);
        }
    }
    output();
    psg.latch();
    if (allHeld) return;
    // end of sounds is decided after the sound has been heard for its last tick
    for (int c = 0; c < 3; c++) {
        Chan& k = ch[c];
        if (!k.active) continue;
        bool end;
        if (k.cur.dur > 0) end = --k.remaining <= 0;
        else end = k.a.done;       // 0 or negative: until the (repeated) amplitude envelope has run out
        if (end) { k.active = false; tryStart(c); }
    }
}

void SoundMgr::output() {
    int mixer = 0x3F;
    for (int c = 0; c < 3; c++) {
        Chan& k = ch[c];
        int vol = 0, per = 0;
        if (k.active && !allHeld) {
            vol = k.vol & 15; per = k.period;
            if (per) mixer &= ~(1 << c);
            if (k.cur.noise) { mixer &= ~(8 << c); psg.write(6, k.cur.noise & 31); }
        }
        psg.write(c * 2, per & 0xFF); psg.write(c * 2 + 1, (per >> 8) & 15);
        psg.write(8 + c, vol);
    }
    psg.write(7, mixer);
}
