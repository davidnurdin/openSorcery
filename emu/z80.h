// Z80 CPU core (documented + undocumented flag behaviour, cycle counted).
#pragma once
#include <cstdint>

struct Z80Bus {
    virtual ~Z80Bus() {}
    virtual uint8_t read(uint16_t a) = 0;
    virtual void write(uint16_t a, uint8_t v) = 0;
    virtual uint8_t in(uint16_t port) = 0;
    virtual void out(uint16_t port, uint8_t v) = 0;
};

class Z80 {
public:
    explicit Z80(Z80Bus* b) : bus(b) { reset(); }
    void reset();
    // Executes one instruction (or one halted NOP); returns T-states used.
    int step();
    // Maskable interrupt request. Returns T-states used (0 if not accepted).
    int irq(uint8_t databus = 0xFF);

    // registers
    uint8_t a, f, b, c, d, e, h, l;
    uint8_t a_, f_, b_, c_, d_, e_, h_, l_;
    uint16_t ix, iy, sp, pc;
    uint8_t i, r;
    bool iff1, iff2, halted;
    uint8_t im;
    bool eiPending;

    uint16_t bc() const { return b << 8 | c; }
    uint16_t de() const { return d << 8 | e; }
    uint16_t hl() const { return h << 8 | l; }
    uint16_t af() const { return a << 8 | f; }
    void setBC(uint16_t v) { b = v >> 8; c = v; }
    void setDE(uint16_t v) { d = v >> 8; e = v; }
    void setHL(uint16_t v) { h = v >> 8; l = v; }
    void setAF(uint16_t v) { a = v >> 8; f = v; }

    uint8_t rb(uint16_t x) { return bus->read(x); }
    void wb(uint16_t x, uint8_t v) { bus->write(x, v); }
    uint16_t rw(uint16_t x) { return rb(x) | rb(x + 1) << 8; }
    void ww(uint16_t x, uint16_t v) { wb(x, v); wb(x + 1, v >> 8); }
    void push(uint16_t v) { sp -= 2; ww(sp, v); }
    uint16_t pop() { uint16_t v = rw(sp); sp += 2; return v; }

private:
    Z80Bus* bus;
    int cyc;
    uint8_t fetch() { return rb(pc++); }
    uint16_t fetchw() { uint16_t v = rw(pc); pc += 2; return v; }
    uint8_t getr(int n, uint16_t ea = 0);
    void setr(int n, uint8_t v, uint16_t ea = 0);
    void alu(int op, uint8_t v);
    uint8_t inc8(uint8_t v);
    uint8_t dec8(uint8_t v);
    uint16_t add16(uint16_t x, uint16_t y);
    uint16_t adc16(uint16_t x, uint16_t y);
    uint16_t sbc16(uint16_t x, uint16_t y);
    uint8_t rot(int k, uint8_t v);
    bool cond(int cc) const;
    void execCB(uint16_t* idx);
    void execED();
    void execMain(uint8_t op, uint16_t* idx);
};
