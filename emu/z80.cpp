#include "z80.h"

enum { FS = 0x80, FZ = 0x40, FY = 0x20, FH = 0x10, FX = 0x08, FP = 0x04, FN = 0x02, FC = 0x01 };

static uint8_t PAR[256];
static bool parInit = false;
static void initPar() {
    for (int i = 0; i < 256; i++) {
        int n = 0;
        for (int b = 0; b < 8; b++) n += (i >> b) & 1;
        PAR[i] = (n & 1) ? 0 : FP;
    }
    parInit = true;
}

static const uint8_t T_MAIN[256] = {
    4,10,7,6,4,4,7,4,4,11,7,6,4,4,7,4,
    8,10,7,6,4,4,7,4,12,11,7,6,4,4,7,4,
    7,10,16,6,4,4,7,4,7,11,16,6,4,4,7,4,
    7,10,13,6,11,11,10,4,7,11,13,6,4,4,7,4,
    4,4,4,4,4,4,7,4,4,4,4,4,4,4,7,4,
    4,4,4,4,4,4,7,4,4,4,4,4,4,4,7,4,
    4,4,4,4,4,4,7,4,4,4,4,4,4,4,7,4,
    7,7,7,7,7,7,4,7,4,4,4,4,4,4,7,4,
    4,4,4,4,4,4,7,4,4,4,4,4,4,4,7,4,
    4,4,4,4,4,4,7,4,4,4,4,4,4,4,7,4,
    4,4,4,4,4,4,7,4,4,4,4,4,4,4,7,4,
    4,4,4,4,4,4,7,4,4,4,4,4,4,4,7,4,
    5,10,10,10,10,11,7,11,5,10,10,4,10,17,7,11,
    5,10,10,11,10,11,7,11,5,4,10,11,10,4,7,11,
    5,10,10,19,10,11,7,11,5,4,10,4,10,4,7,11,
    5,10,10,4,10,11,7,11,5,6,10,4,10,4,7,11};

void Z80::reset() {
    if (!parInit) initPar();
    a = f = b = c = d = e = h = l = 0xFF;
    a_ = f_ = b_ = c_ = d_ = e_ = h_ = l_ = 0xFF;
    ix = iy = 0xFFFF; sp = 0xFFFF; pc = 0; i = r = 0;
    iff1 = iff2 = false; halted = false; im = 0; eiPending = false; cyc = 0;
}

int Z80::irq(uint8_t databus) {
    if (!iff1 || eiPending) return 0;
    iff1 = iff2 = false;
    halted = false;
    r = (r & 0x80) | ((r + 1) & 0x7F);
    if (im == 2) {
        push(pc);
        pc = rw((i << 8) | databus);
        return 19;
    }
    push(pc);
    pc = 0x38;
    return 13;
}

bool Z80::cond(int cc) const {
    switch (cc) {
    case 0: return !(f & FZ);
    case 1: return f & FZ;
    case 2: return !(f & FC);
    case 3: return f & FC;
    case 4: return !(f & FP);
    case 5: return f & FP;
    case 6: return !(f & FS);
    default: return f & FS;
    }
}

uint8_t Z80::getr(int n, uint16_t ea) {
    switch (n) {
    case 0: return b; case 1: return c; case 2: return d; case 3: return e;
    case 4: return h; case 5: return l; case 6: return rb(ea); default: return a;
    }
}
void Z80::setr(int n, uint8_t v, uint16_t ea) {
    switch (n) {
    case 0: b = v; break; case 1: c = v; break; case 2: d = v; break; case 3: e = v; break;
    case 4: h = v; break; case 5: l = v; break; case 6: wb(ea, v); break; default: a = v;
    }
}

void Z80::alu(int op, uint8_t v) {
    unsigned x = a, r, cy = f & FC;
    switch (op) {
    case 0: case 1:
        r = x + v + (op == 1 ? cy : 0);
        f = (r & (FS | FY | FX)) | ((r & 0xFF) ? 0 : FZ) | ((x ^ v ^ r) & FH) |
            (((x ^ v ^ 0x80) & (x ^ r) & 0x80) ? FP : 0) | ((r >> 8) & 1);
        a = r; break;
    case 2: case 3: case 7:
        r = x - v - (op == 3 ? cy : 0);
        f = (r & FS) | ((r & 0xFF) ? 0 : FZ) | ((x ^ v ^ r) & FH) |
            (((x ^ v) & (x ^ r) & 0x80) ? FP : 0) | FN | ((r >> 8) & 1) |
            ((op == 7) ? (v & (FY | FX)) : (r & (FY | FX)));
        if (op != 7) a = r;
        break;
    case 4: a &= v; f = (a & (FS | FY | FX)) | (a ? 0 : FZ) | FH | PAR[a]; break;
    case 5: a ^= v; f = (a & (FS | FY | FX)) | (a ? 0 : FZ) | PAR[a]; break;
    default: a |= v; f = (a & (FS | FY | FX)) | (a ? 0 : FZ) | PAR[a]; break;
    }
}

uint8_t Z80::inc8(uint8_t v) {
    uint8_t r = v + 1;
    f = (f & FC) | (r & (FS | FY | FX)) | (r ? 0 : FZ) | ((v & 15) == 15 ? FH : 0) | (v == 0x7F ? FP : 0);
    return r;
}
uint8_t Z80::dec8(uint8_t v) {
    uint8_t r = v - 1;
    f = (f & FC) | (r & (FS | FY | FX)) | (r ? 0 : FZ) | ((v & 15) == 0 ? FH : 0) | (v == 0x80 ? FP : 0) | FN;
    return r;
}
uint16_t Z80::add16(uint16_t x, uint16_t y) {
    uint32_t r = (uint32_t)x + y;
    f = (f & (FS | FZ | FP)) | ((r >> 8) & (FY | FX)) | (((x ^ y ^ r) >> 8) & FH) | ((r >> 16) & 1);
    return r;
}
uint16_t Z80::adc16(uint16_t x, uint16_t y) {
    uint32_t r = (uint32_t)x + y + (f & FC);
    uint16_t res = r;
    f = ((res >> 8) & (FS | FY | FX)) | (res ? 0 : FZ) | (((x ^ y ^ res) >> 8) & FH) |
        (((x ^ y ^ 0x8000) & (x ^ res) & 0x8000) ? FP : 0) | ((r >> 16) & 1);
    return res;
}
uint16_t Z80::sbc16(uint16_t x, uint16_t y) {
    uint32_t r = (uint32_t)x - y - (f & FC);
    uint16_t res = r;
    f = ((res >> 8) & (FS | FY | FX)) | (res ? 0 : FZ) | (((x ^ y ^ res) >> 8) & FH) |
        (((x ^ y) & (x ^ res) & 0x8000) ? FP : 0) | FN | ((r >> 16) & 1);
    return res;
}
uint8_t Z80::rot(int k, uint8_t v) {
    uint8_t c = f & FC, r, co;
    switch (k) {
    case 0: r = (v << 1) | (v >> 7); co = v >> 7; break;
    case 1: r = (v >> 1) | (v << 7); co = v & 1; break;
    case 2: r = (v << 1) | c; co = v >> 7; break;
    case 3: r = (v >> 1) | (c << 7); co = v & 1; break;
    case 4: r = v << 1; co = v >> 7; break;
    case 5: r = (v >> 1) | (v & 0x80); co = v & 1; break;
    case 6: r = (v << 1) | 1; co = v >> 7; break;
    default: r = v >> 1; co = v & 1; break;
    }
    f = (r & (FS | FY | FX)) | (r ? 0 : FZ) | PAR[r] | co;
    return r;
}

void Z80::execCB(uint16_t* idx) {
    uint16_t ea = 0;
    uint8_t op;
    if (idx) {
        ea = *idx + (int8_t)fetch();
        op = fetch();
        cyc += 15;
    } else {
        op = fetch();
        r = (r & 0x80) | ((r + 1) & 0x7F);
        cyc += 4;
    }
    int x = op >> 6, y = (op >> 3) & 7, z = op & 7;
    uint8_t v = idx ? rb(ea) : getr(z, hl());
    uint8_t res;
    if (x == 1) {
        uint8_t t = v & (1 << y);
        f = (f & FC) | FH | (t ? 0 : (FZ | FP)) | (t & FS) |
            ((idx || z == 6) ? ((idx ? ea : hl()) >> 8 & (FY | FX)) : (v & (FY | FX)));
        cyc += (idx || z == 6) ? 0 : 4;
        return;
    }
    if (x == 0) res = rot(y, v);
    else if (x == 2) res = v & ~(1 << y);
    else res = v | (1 << y);
    if (idx) {
        wb(ea, res);
        if (z != 6) setr(z, res);
    } else if (z == 6) {
        wb(hl(), res);
        cyc += 7;
    } else {
        setr(z, res);
        cyc += 4;
    }
}

void Z80::execED() {
    uint8_t op = fetch();
    r = (r & 0x80) | ((r + 1) & 0x7F);
    int x = op >> 6, y = (op >> 3) & 7, z = op & 7, p = y >> 1, q = y & 1;
    cyc += 8;
    if (x == 1) {
        switch (z) {
        case 0: {
            uint8_t v = bus->in(bc());
            f = (f & FC) | (v & (FS | FY | FX)) | (v ? 0 : FZ) | PAR[v];
            if (y != 6) setr(y, v);
            cyc += 4; break; }
        case 1: bus->out(bc(), y == 6 ? 0 : getr(y)); cyc += 4; break;
        case 2: {
            uint16_t rp = p == 0 ? bc() : p == 1 ? de() : p == 2 ? hl() : sp;
            setHL(q ? adc16(hl(), rp) : sbc16(hl(), rp));
            cyc += 7; break; }
        case 3: {
            uint16_t ad = fetchw();
            if (!q) ww(ad, p == 0 ? bc() : p == 1 ? de() : p == 2 ? hl() : sp);
            else {
                uint16_t v = rw(ad);
                if (p == 0) setBC(v); else if (p == 1) setDE(v); else if (p == 2) setHL(v); else sp = v;
            }
            cyc += 12; break; }
        case 4: { uint8_t o = a; a = 0; alu(2, o); break; }
        case 5: pc = pop(); iff1 = iff2; cyc += 6; break;
        case 6: im = (y & 3) == 2 ? 1 : (y & 3) == 3 ? 2 : 0; break;
        default:
            switch (y) {
            case 0: i = a; cyc += 1; break;
            case 1: r = a; cyc += 1; break;
            case 2: a = i; f = (f & FC) | (a & (FS | FY | FX)) | (a ? 0 : FZ) | (iff2 ? FP : 0); cyc += 1; break;
            case 3: a = r; f = (f & FC) | (a & (FS | FY | FX)) | (a ? 0 : FZ) | (iff2 ? FP : 0); cyc += 1; break;
            case 4: { uint8_t v = rb(hl()); wb(hl(), (a << 4) | (v >> 4)); a = (a & 0xF0) | (v & 15);
                f = (f & FC) | (a & (FS | FY | FX)) | (a ? 0 : FZ) | PAR[a]; cyc += 10; break; }
            case 5: { uint8_t v = rb(hl()); wb(hl(), (v << 4) | (a & 15)); a = (a & 0xF0) | (v >> 4);
                f = (f & FC) | (a & (FS | FY | FX)) | (a ? 0 : FZ) | PAR[a]; cyc += 10; break; }
            default: break;
            }
        }
        return;
    }
    if (x == 2 && y >= 4) {
        bool dec = y & 1, rep = y >= 6;
        int dd = dec ? -1 : 1;
        switch (z) {
        case 0: {
            uint8_t v = rb(hl()); wb(de(), v);
            setHL(hl() + dd); setDE(de() + dd); setBC(bc() - 1);
            uint8_t n = v + a;
            f = (f & (FS | FZ | FC)) | (n & FX) | ((n << 4) & FY) | (bc() ? FP : 0);
            cyc += 8;
            if (rep && bc()) { pc -= 2; cyc += 5; }
            break; }
        case 1: {
            uint8_t v = rb(hl());
            uint8_t res = a - v;
            uint8_t hf = ((a ^ v ^ res) & FH);
            setHL(hl() + dd); setBC(bc() - 1);
            uint8_t n = res - (hf ? 1 : 0);
            f = (f & FC) | (res & FS) | (res ? 0 : FZ) | hf | (bc() ? FP : 0) | FN | (n & FX) | ((n << 4) & FY);
            cyc += 8;
            if (rep && bc() && res) { pc -= 2; cyc += 5; }
            break; }
        case 2: {
            uint8_t v = bus->in(bc()); wb(hl(), v); b--;
            setHL(hl() + dd);
            f = (b & (FS | FY | FX)) | (b ? 0 : FZ) | FN;
            cyc += 8;
            if (rep && b) { pc -= 2; cyc += 5; }
            break; }
        default: {
            uint8_t v = rb(hl()); b--; bus->out(bc(), v);
            setHL(hl() + dd);
            f = (b & (FS | FY | FX)) | (b ? 0 : FZ) | FN;
            cyc += 8;
            if (rep && b) { pc -= 2; cyc += 5; }
            break; }
        }
    }
}

void Z80::execMain(uint8_t op, uint16_t* idx) {
    int x = op >> 6, y = (op >> 3) & 7, z = op & 7, p = y >> 1, q = y & 1;
    // helpers for HL / IX / IY and (HL)/(IX+d)
    auto HL = [&]() -> uint16_t { return idx ? *idx : hl(); };
    auto setHLv = [&](uint16_t v) { if (idx) *idx = v; else setHL(v); };
    auto R8 = [&](int n, uint16_t ea) -> uint8_t {
        if (idx && n == 4) return *idx >> 8;
        if (idx && n == 5) return *idx & 0xFF;
        return getr(n, ea);
    };
    auto W8 = [&](int n, uint8_t v, uint16_t ea) {
        if (idx && n == 4) *idx = (*idx & 0xFF) | (v << 8);
        else if (idx && n == 5) *idx = (*idx & 0xFF00) | v;
        else setr(n, v, ea);
    };
    auto EA = [&]() -> uint16_t { return idx ? (uint16_t)(*idx + (int8_t)fetch()) : hl(); };

    if (x == 1) {
        if (op == 0x76) { halted = true; return; }
        if (y == 6) { uint16_t ea = EA(); wb(ea, getr(z)); if (idx) cyc += 8; return; }
        if (z == 6) { uint16_t ea = EA(); setr(y, rb(ea)); if (idx) cyc += 8; return; }
        W8(y, R8(z, 0), 0); return;
    }
    if (x == 2) {
        if (z == 6) { uint16_t ea = EA(); alu(y, rb(ea)); if (idx) cyc += 8; }
        else alu(y, R8(z, 0));
        return;
    }
    if (x == 0) {
        switch (z) {
        case 0:
            if (y == 0) return;
            if (y == 1) { uint8_t t = a; a = a_; a_ = t; t = f; f = f_; f_ = t; return; }
            { int8_t d = fetch();
              if (y == 2) { b--; if (b) { pc += d; cyc += 5; } }
              else if (y == 3) { pc += d; cyc += 5; }
              else if (cond(y - 4)) { pc += d; cyc += 5; } }
            return;
        case 1:
            if (!q) {
                uint16_t v = fetchw();
                if (p == 0) setBC(v); else if (p == 1) setDE(v); else if (p == 2) setHLv(v); else sp = v;
            } else {
                uint16_t rp = p == 0 ? bc() : p == 1 ? de() : p == 2 ? HL() : sp;
                setHLv(add16(HL(), rp));
            }
            return;
        case 2:
            if (!q) {
                if (p == 0) wb(bc(), a); else if (p == 1) wb(de(), a);
                else if (p == 2) ww(fetchw(), HL()); else wb(fetchw(), a);
            } else {
                if (p == 0) a = rb(bc()); else if (p == 1) a = rb(de());
                else if (p == 2) setHLv(rw(fetchw())); else a = rb(fetchw());
            }
            return;
        case 3: {
            int dd = q ? -1 : 1;
            if (p == 0) setBC(bc() + dd); else if (p == 1) setDE(de() + dd);
            else if (p == 2) setHLv(HL() + dd); else sp += dd;
            return; }
        case 4: case 5: {
            if (y == 6) {
                uint16_t ea = EA(); uint8_t v = rb(ea);
                wb(ea, z == 4 ? inc8(v) : dec8(v)); if (idx) cyc += 8;
            } else {
                uint8_t v = R8(y, 0);
                W8(y, z == 4 ? inc8(v) : dec8(v), 0);
            }
            return; }
        case 6:
            if (y == 6) { uint16_t ea = EA(); wb(ea, fetch()); if (idx) cyc += 5; }
            else W8(y, fetch(), 0);
            return;
        default:
            switch (y) {
            case 0: { uint8_t c0 = a >> 7; a = (a << 1) | c0; f = (f & (FS | FZ | FP)) | (a & (FY | FX)) | c0; break; }
            case 1: { uint8_t c0 = a & 1; a = (a >> 1) | (c0 << 7); f = (f & (FS | FZ | FP)) | (a & (FY | FX)) | c0; break; }
            case 2: { uint8_t c0 = a >> 7; a = (a << 1) | (f & FC); f = (f & (FS | FZ | FP)) | (a & (FY | FX)) | c0; break; }
            case 3: { uint8_t c0 = a & 1; a = (a >> 1) | ((f & FC) << 7); f = (f & (FS | FZ | FP)) | (a & (FY | FX)) | c0; break; }
            case 4: {
                uint8_t adj = 0, cy = f & FC;
                if ((f & FH) || (a & 15) > 9) adj |= 6;
                if (cy || a > 0x99) { adj |= 0x60; cy = 1; }
                uint8_t o = a, res;
                uint8_t hf;
                if (f & FN) { res = a - adj; hf = ((f & FH) && (o & 15) < 6) ? FH : 0; }
                else { res = a + adj; hf = (o & 15) > 9 ? FH : 0; }
                a = res;
                f = (res & (FS | FY | FX)) | (res ? 0 : FZ) | hf | PAR[res] | (f & FN) | cy;
                break; }
            case 5: a = ~a; f = (f & (FS | FZ | FP | FC)) | (a & (FY | FX)) | FH | FN; break;
            case 6: f = (f & (FS | FZ | FP)) | (a & (FY | FX)) | FC; break;
            default: f = (f & (FS | FZ | FP)) | (a & (FY | FX)) | ((f & FC) ? FH : 0) | ((f & FC) ^ FC); break;
            }
            return;
        }
    }
    // x == 3
    switch (z) {
    case 0: if (cond(y)) { pc = pop(); cyc += 6; } return;
    case 1:
        if (!q) {
            uint16_t v = pop();
            if (p == 0) setBC(v); else if (p == 1) setDE(v); else if (p == 2) setHLv(v); else setAF(v);
        } else {
            if (p == 0) pc = pop();
            else if (p == 1) {
                uint8_t t;
                t = b; b = b_; b_ = t; t = c; c = c_; c_ = t; t = d; d = d_; d_ = t;
                t = e; e = e_; e_ = t; t = h; h = h_; h_ = t; t = l; l = l_; l_ = t;
            } else if (p == 2) pc = HL();
            else sp = HL();
        }
        return;
    case 2: { uint16_t ad = fetchw(); if (cond(y)) pc = ad; return; }
    case 3:
        switch (y) {
        case 0: pc = fetchw(); return;
        case 2: { uint8_t n = fetch(); bus->out((a << 8) | n, a); return; }
        case 3: { uint8_t n = fetch(); a = bus->in((a << 8) | n); return; }
        case 4: { uint16_t v = rw(sp); ww(sp, HL()); setHLv(v); return; }
        case 5: { uint8_t t = d; d = h; h = t; t = e; e = l; l = t; return; }
        case 6: iff1 = iff2 = false; return;
        case 7: iff1 = iff2 = true; eiPending = true; return;
        default: return;
        }
    case 4: { uint16_t ad = fetchw(); if (cond(y)) { push(pc); pc = ad; cyc += 7; } return; }
    case 5:
        if (!q) {
            uint16_t v = p == 0 ? bc() : p == 1 ? de() : p == 2 ? HL() : af();
            push(v);
        } else { uint16_t ad = fetchw(); push(pc); pc = ad; }
        return;
    case 6: alu(y, fetch()); return;
    default: push(pc); pc = y * 8; return;
    }
}

int Z80::step() {
    if (!parInit) initPar();
    eiPending = false;
    cyc = 0;
    if (halted) { r = (r & 0x80) | ((r + 1) & 0x7F); return 4; }
    r = (r & 0x80) | ((r + 1) & 0x7F);
    uint8_t op = fetch();
    cyc = T_MAIN[op];
    switch (op) {
    case 0xCB: execCB(nullptr); break;
    case 0xED: execED(); break;
    case 0xDD: case 0xFD: {
        uint16_t* idx = (op == 0xDD) ? &ix : &iy;
        uint8_t o2 = fetch();
        r = (r & 0x80) | ((r + 1) & 0x7F);
        cyc += 4;
        if (o2 == 0xCB) { cyc = 0; execCB(idx); }
        else if (o2 == 0xDD || o2 == 0xFD || o2 == 0xED) { pc--; cyc = 4; }
        else { cyc = T_MAIN[o2] + 4; execMain(o2, idx); }
        break; }
    default: execMain(op, nullptr); break;
    }
    return cyc;
}
