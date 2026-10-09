// Computes per-instruction T-state costs from the reference interpreter.
// in : memory image (65536 bytes), list "addr len" ; out: "addr base extra" (extra = additional cost when branch/repeat taken)
#include "../../emu/z80.h"
#include <cstdio>
#include <cstring>
#include <algorithm>
struct Bus : Z80Bus {
    uint8_t m[65536];
    uint8_t read(uint16_t a) override { return m[a]; }
    void write(uint16_t a, uint8_t v) override { m[a] = v; }
    uint8_t in(uint16_t) override { return 0xFF; }
    void out(uint16_t, uint8_t) override {}
};
int main(int argc, char** argv) {
    static uint8_t img[65536];
    FILE* f = fopen(argv[1], "rb"); fread(img, 1, 65536, f); fclose(f);
    FILE* l = fopen(argv[2], "r"); FILE* o = fopen(argv[3], "w");
    unsigned addr, len;
    Bus bus;
    while (fscanf(l, "%x %u", &addr, &len) == 2) {
        int lo = 1 << 30, hi = 0;
        for (int fl = 0; fl < 2; fl++)
            for (int bcv : {0x0101, 0x0202, 0x0001, 0x0002}) {
                memset(bus.m, 0, sizeof bus.m);
                for (unsigned i = 0; i < len; i++) bus.m[0x1000 + i] = img[addr + i];
                Z80 c(&bus);
                c.pc = 0x1000; c.sp = 0x3000; c.ix = c.iy = 0x2000; c.setHL(0x2100); c.setDE(0x2200); c.setBC(bcv);
                c.a = 0x55; c.f = fl ? 0xFF : 0x00;
                // for LDIR etc. keep bc small & non-zero
                int t = c.step();
                lo = std::min(lo, t); hi = std::max(hi, t);
            }
        fprintf(o, "%04X %d %d\n", addr, lo, hi - lo);
    }
    fclose(o);
}
