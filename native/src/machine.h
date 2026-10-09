// The "hardware" of the port: RAM image of the original program, CPC video (gate array palette + mode raster log),
// keyboard/joystick, disc, sound chip, and the firmware services the game used (palette, keys, disc, sound queue ...).
// The game itself is C++ code running as a cooperative task (see port_flow_task.cpp); it waits for interrupts with waitIrq().
#pragma once
#include "../../common/resources.h"
#include "alu.h"
#include "regs.h"
#include "sound.h"
#include <cstdint>
#include <cstdio>
#include <functional>
#include <map>
#include <string>
#include <vector>

class Machine {
public:
    static constexpr int LINES = 312;              // scanlines per frame
    static constexpr int CYC_LINE = 256;           // T-states per scanline (4 MHz)
    static constexpr int CYC_FRAME = LINES * CYC_LINE;
    static constexpr int SCR_W = 640, SCR_H = 400; // visible bitmap (mode 2 x2 lines)
    static constexpr int BORDER_X = 32, BORDER_Y = 24;
    static constexpr int OUT_W = SCR_W + 2 * BORDER_X, OUT_H = SCR_H + 2 * BORDER_Y;

    Machine();
    ~Machine();
    bool boot(const std::string& resourceDir, std::string* err = nullptr);   // loads the resource folder, resets the machine
    void runFrame();                               // 1/50 s: six interrupts, the game task runs in between
    void render(uint32_t* rgba);                   // OUT_W x OUT_H, 0xAABBGGRR
    void setKey(int cpcKey, bool down);            // CPC key number 0..79 (72..77 = joystick 0)
    void audio(int16_t* out, int n, int rate) { snd.psg.mix(out, n, rate); }   // mono samples of the last frame
    bool crashed() const { return dead; }
    const std::string& deadReason() const { return deadMsg; }
    void die(const std::string& m) { dead = true; deadMsg = m; }

    uint8_t ram[65536];                            // Z80 address space of the original program
    Z80 cpu;                                       // parameter registers of the routines
    Resources res;                                 // graphics, data and disc sectors (no .dsk needed)
    SoundMgr snd;
    uint64_t frameNo = 0;
    // Random numbers (the original read the Z80 refresh register R): xorshift32, also stirred by the interrupts.
    uint8_t random() { rng ^= rng << 13; rng ^= rng >> 17; rng ^= rng << 5; return (uint8_t)(rng >> 11); }
    uint16_t rw(uint16_t a) const { return ram[a] | ram[(uint16_t)(a + 1)] << 8; }

    // ---- services for the game code ----
    void call(uint16_t addr);                      // run the routine of the original program at `addr` (hand-written or firmware)
    void waitIrq();                                // game task: sleep until the next interrupt has been serviced
    uint64_t now() const { return frameNo * CYC_FRAME + cycInFrame; }   // T-states since power on
    void mcSetMode(int mode) { vid.mode = mode & 3; logVideo(); }
    void mcSetInks(uint16_t table);                // 17 hardware colour numbers: border, pen 0..15
    void fwInterrupt();                            // firmware interrupt service (flyback events, ink flash, tickers)
    void rst18(uint16_t farEntry);                 // AMSDOS BIOS call through a far-call table entry
    struct Task;                                   // the game task (ucontext coroutine)
    void startGame(std::function<void()> body);    // starts the game task (runs until its first waitIrq)

    // ---- debugging ----
    int videoMode() const { return vid.mode; }
    int hwPen(int pen, int phase = 0) const { return vid.hw[phase][pen]; }
    void debugVideo(FILE* f) const;

private:
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
    void setInk(int pen, int ink1, int ink2);

    bool firmware(uint16_t addr);                  // firmware entry points (BBxx/BCxx/BDxx jump block)
    void gameIrq();                                // the game's own interrupt handler
    void farCall(int idx);
    void deliverIrq(int line, bool game);

    struct Event { uint16_t block, routine; uint8_t cls; };
    std::map<uint16_t, Event> events;     // by event block address
    struct Ticker { uint16_t block; int count, refill; };
    std::vector<Ticker> tickers;
    std::vector<uint16_t> flyEvents;
    bool vsyncPending = false;
    const std::vector<uint8_t>* casFile = nullptr;
    uint8_t keys[10] = {};
    bool dead = false;
    std::string deadMsg;
    int64_t cycInFrame = 0;
    Task* task = nullptr;
    bool inIrq = false;
    uint32_t rng = 2463534242u;
};
