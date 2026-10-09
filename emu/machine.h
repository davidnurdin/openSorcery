// Amstrad CPC 6128 "just enough" machine: Z80 + RAM + gate-array video + firmware/AMSDOS shims.
#pragma once
#include "../common/dsk.h"
#include "sound.h"
#include "z80.h"
#include <cstdint>
#include <cstdio>
#include <functional>
#include <map>
#include <string>
#include <vector>

class Machine : public Z80Bus {
public:
    static constexpr int LINES = 312;              // scanlines per frame
    static constexpr int CYC_LINE = 256;           // T-states per scanline (4 MHz)
    static constexpr int CYC_FRAME = LINES * CYC_LINE;
    static constexpr int SCR_W = 640, SCR_H = 400; // visible bitmap (mode 2 x2 lines)
    static constexpr int BORDER_X = 32, BORDER_Y = 24;
    static constexpr int OUT_W = SCR_W + 2 * BORDER_X, OUT_H = SCR_H + 2 * BORDER_Y;

    Machine();
    bool boot(const std::string& dskPath, std::string* err = nullptr);
    void runFrame();                               // emulate 1/50 s
    void render(uint32_t* rgba);                   // OUT_W x OUT_H, 0xAABBGGRR
    void setKey(int cpcKey, bool down);
    int videoMode() const { return vid.mode; }
    void debugVideo(FILE* f) const;
    int hwPen(int pen, int phase = 0) const { return vid.hw[phase][pen]; }            // CPC key number 0..79
    bool crashed() const { return dead; }
    const std::string& deadReason() const { return deadMsg; }
    uint64_t frameNo = 0;
    // profiling (headless): per-address executed-instruction counts and RAM write counts
    bool profile = false;
    std::vector<uint32_t> execCnt, writeCnt;
    void enableProfile() { profile = true; execCnt.assign(65536, 0); writeCnt.assign(65536, 0); }
    uint8_t ram[65536];
    Z80 cpu;
    DskImage dsk;

    SoundMgr snd;
    // Mixes `n` mono samples of the last emulated frame.
    void audio(int16_t* out, int n, int rate) { snd.psg.mix(out, n, rate); }

    // Z80Bus
    uint8_t read(uint16_t a) override { return ram[a]; }
    void write(uint16_t a, uint8_t v) override { ram[a] = v; if (profile) writeCnt[a]++; }
    uint8_t in(uint16_t) override { return 0xFF; }
    void out(uint16_t, uint8_t) override {}

private:
    // ---- video state ----
    struct Video {
        int mode = 1;
        uint8_t hw[2][17];       // hardware colour (0..31) for the two flash phases, pen 16 = border
        int flashPeriod[2] = {10, 10};
    } vid, vid0;
    struct VEvent { int line; Video v; };
    std::vector<VEvent> vlog;
    int flashCnt = 0, flashPhase = 0;
    uint8_t fwInk[17][2] = {};   // firmware ink table (ink numbers 0..26: ink1, ink2 per pen)
    void logVideo();

    // ---- firmware shims ----
    std::map<uint16_t, int> hooks;
    void installHooks();
    bool callHook(uint16_t pc);
    void doRet() { cpu.pc = cpu.pop(); }
    void fwIrq();
    void runSub(uint16_t addr);
    void farCall(int idx);
    void setInk(int pen, int ink1, int ink2);

    // events (tickers / frame flyback)
    struct Event { uint16_t block, routine; uint8_t cls; };
    std::map<uint16_t, Event> events;     // by event block address
    struct Ticker { uint16_t block; int count, refill; };
    std::vector<Ticker> tickers;
    std::vector<uint16_t> flyEvents;
    int irqPhase = 0;
    bool vsyncPending = false;

    // ---- CAS ----
    const DskFile* casFile = nullptr;

    // ---- keyboard ----
    uint8_t keys[10] = {};

    bool dead = false;
    std::string deadMsg;
    int cycInFrame = 0;
    bool irqPending = false;
    void die(const std::string& m) { dead = true; deadMsg = m; }
    bool inSub = false;
};
