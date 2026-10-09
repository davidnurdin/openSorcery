// Registry of hand-written C++ replacements for routines of the original Z80 program.
//
//   NATIVE(0x8C4C, tileAddr, "hl", 40) { ... }     // addr, C++ name, output registers, approx. cost in T-states
//
// On entry the registers/RAM hold the state the original Z80 routine would see; the function leaves them as the original
// would (the `outs` string documents which output registers callers rely on). Routines are invoked with Machine::call(addr).
#pragma once
#include "machine.h"

struct NativeDef {
    uint16_t addr;
    const char* name;
    void (*fn)(Machine&);
    const char* outs;
    int cost;
};
void registerNative(const NativeDef& d);
const NativeDef* findNative(uint16_t addr);
const std::vector<NativeDef>& allNatives();

#define NATIVE(addr, fname, outs, cost)                                                   \
    static void fname(Machine& m);                                                        \
    static struct fname##_reg { fname##_reg() { registerNative({addr, #fname, fname, outs, cost}); } } fname##_inst; \
    static void fname(Machine& m)

// Routine kept for reference but not registered.
#define NATIVE_OFF(addr, fname, outs, cost) [[maybe_unused]] static void fname(Machine& m)
