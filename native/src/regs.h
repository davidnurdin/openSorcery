// CPU register file used as the calling convention of the hand-written routines.
// The routines of the original Z80 program take their parameters in registers; the C++ port keeps that convention
// (a, hl, ...) so that routines written one by one can be chained.  There is no instruction execution any more.
#pragma once
#include <cstdint>

class Z80 {
public:
    uint8_t a = 0, f = 0, b = 0, c = 0, d = 0, e = 0, h = 0, l = 0;
    uint8_t a_ = 0, f_ = 0, b_ = 0, c_ = 0, d_ = 0, e_ = 0, h_ = 0, l_ = 0;
    uint16_t ix = 0, iy = 0, sp = 0, pc = 0;
    uint8_t i = 0, r = 0;
    bool iff1 = false, iff2 = false, halted = false, eiPending = false;
    uint8_t im = 0;

    uint16_t bc() const { return b << 8 | c; }
    uint16_t de() const { return d << 8 | e; }
    uint16_t hl() const { return h << 8 | l; }
    uint16_t af() const { return a << 8 | f; }
    void setBC(uint16_t v) { b = v >> 8; c = v; }
    void setDE(uint16_t v) { d = v >> 8; e = v; }
    void setHL(uint16_t v) { h = v >> 8; l = v; }
    void setAF(uint16_t v) { a = v >> 8; f = v; }
};
