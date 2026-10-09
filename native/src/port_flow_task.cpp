// Timing helpers of the game task: waiting for interrupts / the 50 Hz tick, busy-loop models, small wrappers.
//
// The original program blocks all the time (halt, polling counters that the interrupt handler changes, empty delay loops).
// Here the game is straight-line C++ running as a cooperative task (Machine::startGame); whenever the original would
// spin, the task calls waitIrq(), which hands control back to the machine until the next interrupt has been serviced.
#include "port_flow.h"
#include <cstdio>
#include <cstdlib>
#include <cstring>

namespace flow {
namespace {
Machine* gMachine = nullptr;
constexpr double BUSY_FRACTION = 0.969;     // share of the CPU time the main program gets (the rest is spent in the handler)
}  // namespace

Machine& mach() { return *gMachine; }
void bind(Machine& m) { gMachine = &m; }

void waitIrq() { gMachine->waitIrq(); }

// 7BE9: wait for the 50 Hz tick flag set by the interrupt handler, and clear it.
void waitTick() {
    while (!(at(ram::IRQ_FLAGS) & 0x20)) waitIrq();
    at(ram::IRQ_FLAGS) &= ~0x20;
}

uint64_t now() { return gMachine->now(); }

bool fireHeld() { return at(ram::JOY_STATE) & 0x10; }

// Models a busy loop of the original: `iterations` passes of `cyclesPerIteration` T-states each.
uint32_t timedLoop(uint32_t iterations, double cyclesPerIteration, bool stopOnFire, const std::function<void(uint32_t)>& body) {
    const uint64_t start = now();
    uint32_t done = 0;
    for (;;) {
        if (stopOnFire && fireHeld()) break;
        if (done >= iterations) break;
        waitIrq();
        uint64_t t = now();
        double executed = t > start ? (double)(t - start) * BUSY_FRACTION / cyclesPerIteration : 0.0;
        uint32_t target = executed >= iterations ? iterations : (uint32_t)executed;
        for (; done < target; ++done)
            if (body) body(done);
    }
    return done;
}

void printChar(char ch) {
    mach().cpu.a = (uint8_t)ch;
    call(0x8CFC);
}

void printAt(uint16_t screenPos, const char* text) {
    mach().cpu.setDE(screenPos);
    for (; *text; ++text) printChar(*text);
}

void setTextMask(uint8_t mask) { at(mem::TEXT_MASK) = mask; }

void setMode(int mode) {
    mach().cpu.a = (uint8_t)mode;
    call(0xBC0E);
}

void setPalette(uint16_t table, int border) {
    mach().cpu.setHL(table);
    mach().cpu.a = (uint8_t)border;
    call(0x8F3C);
}

void farCall(uint16_t farEntry) { mach().rst18(farEntry); }

}  // namespace flow
