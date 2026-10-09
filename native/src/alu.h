// Flag-exact 8/16 bit ALU helpers (Z80 semantics) for routines whose callers look at the flags.
#pragma once
#include "regs.h"
#include <cstdint>

namespace lift {
enum { FS = 0x80, FZ = 0x40, FY = 0x20, FH = 0x10, FX = 0x08, FP = 0x04, FN = 0x02, FC = 0x01 };
extern uint8_t PAR[256];


inline void alu(Z80& c, int op, uint8_t v) {
    unsigned x = c.a, r, cy = c.f & FC;
    switch (op) {
    case 0: case 1:
        r = x + v + (op == 1 ? cy : 0);
        c.f = (r & (FS | FY | FX)) | ((r & 0xFF) ? 0 : FZ) | ((x ^ v ^ r) & FH) |
              (((x ^ v ^ 0x80) & (x ^ r) & 0x80) ? FP : 0) | ((r >> 8) & 1);
        c.a = r; break;
    case 2: case 3: case 7:
        r = x - v - (op == 3 ? cy : 0);
        c.f = (r & FS) | ((r & 0xFF) ? 0 : FZ) | ((x ^ v ^ r) & FH) | (((x ^ v) & (x ^ r) & 0x80) ? FP : 0) | FN |
              ((r >> 8) & 1) | ((op == 7) ? (v & (FY | FX)) : (r & (FY | FX)));
        if (op != 7) c.a = r;
        break;
    case 4: c.a &= v; c.f = (c.a & (FS | FY | FX)) | (c.a ? 0 : FZ) | FH | PAR[c.a]; break;
    case 5: c.a ^= v; c.f = (c.a & (FS | FY | FX)) | (c.a ? 0 : FZ) | PAR[c.a]; break;
    default: c.a |= v; c.f = (c.a & (FS | FY | FX)) | (c.a ? 0 : FZ) | PAR[c.a]; break;
    }
}
inline uint8_t inc8(Z80& c, uint8_t v) {
    uint8_t r = v + 1;
    c.f = (c.f & FC) | (r & (FS | FY | FX)) | (r ? 0 : FZ) | ((v & 15) == 15 ? FH : 0) | (v == 0x7F ? FP : 0);
    return r;
}
inline uint8_t dec8(Z80& c, uint8_t v) {
    uint8_t r = v - 1;
    c.f = (c.f & FC) | (r & (FS | FY | FX)) | (r ? 0 : FZ) | ((v & 15) == 0 ? FH : 0) | (v == 0x80 ? FP : 0) | FN;
    return r;
}
inline uint16_t add16(Z80& c, uint16_t x, uint16_t y) {
    uint32_t r = (uint32_t)x + y;
    c.f = (c.f & (FS | FZ | FP)) | ((r >> 8) & (FY | FX)) | (((x ^ y ^ r) >> 8) & FH) | ((r >> 16) & 1);
    return r;
}
inline uint16_t adc16(Z80& c, uint16_t x, uint16_t y) {
    uint32_t r = (uint32_t)x + y + (c.f & FC);
    uint16_t res = r;
    c.f = ((res >> 8) & (FS | FY | FX)) | (res ? 0 : FZ) | (((x ^ y ^ res) >> 8) & FH) |
          (((x ^ y ^ 0x8000) & (x ^ res) & 0x8000) ? FP : 0) | ((r >> 16) & 1);
    return res;
}
inline uint16_t sbc16(Z80& c, uint16_t x, uint16_t y) {
    uint32_t r = (uint32_t)x - y - (c.f & FC);
    uint16_t res = r;
    c.f = ((res >> 8) & (FS | FY | FX)) | (res ? 0 : FZ) | (((x ^ y ^ res) >> 8) & FH) |
          (((x ^ y) & (x ^ res) & 0x8000) ? FP : 0) | FN | ((r >> 16) & 1);
    return res;
}
inline uint8_t rot(Z80& c, int k, uint8_t v) {
    uint8_t cy = c.f & FC, r, co;
    switch (k) {
    case 0: r = (v << 1) | (v >> 7); co = v >> 7; break;
    case 1: r = (v >> 1) | (v << 7); co = v & 1; break;
    case 2: r = (v << 1) | cy; co = v >> 7; break;
    case 3: r = (v >> 1) | (cy << 7); co = v & 1; break;
    case 4: r = v << 1; co = v >> 7; break;
    case 5: r = (v >> 1) | (v & 0x80); co = v & 1; break;
    case 6: r = (v << 1) | 1; co = v >> 7; break;
    default: r = v >> 1; co = v & 1; break;
    }
    c.f = (r & (FS | FY | FX)) | (r ? 0 : FZ) | PAR[r] | co;
    return r;
}
// BIT n,v ; `xy` = byte providing the undocumented Y/X flags
inline void bit(Z80& c, int n, uint8_t v, uint8_t xy) {
    uint8_t t = v & (1 << n);
    c.f = (c.f & FC) | FH | (t ? 0 : (FZ | FP)) | (t & FS) | (xy & (FY | FX));
}
inline void daa(Z80& c) {
    uint8_t adj = 0, cy = c.f & FC;
    if ((c.f & FH) || (c.a & 15) > 9) adj |= 6;
    if (cy || c.a > 0x99) { adj |= 0x60; cy = 1; }
    uint8_t o = c.a, res, hf;
    if (c.f & FN) { res = c.a - adj; hf = ((c.f & FH) && (o & 15) < 6) ? FH : 0; }
    else { res = c.a + adj; hf = (o & 15) > 9 ? FH : 0; }
    c.a = res;
    c.f = (res & (FS | FY | FX)) | (res ? 0 : FZ) | hf | PAR[res] | (c.f & FN) | cy;
}
}  // namespace lift
