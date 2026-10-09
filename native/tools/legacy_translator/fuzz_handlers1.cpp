// Fuzz harness for the entity behaviours of port_handlers1.cpp: boots the game, then calls each native under the
// verifier on random plausible state (entity, player, bolts, tile map ...).
// build:  g++ -O1 -std=c++17 -Inative/src -o fuzz_handlers1 native/tools/fuzz_handlers1.cpp \
//         $(ls native/src/*.cpp | grep -v main.cpp) emu/z80.cpp common/dsk.cpp
// run (from the directory holding ../Sorcery.dsk):  ./fuzz_handlers1 [iterations]
#include "native.h"
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <vector>
static uint32_t rs = 24681357u;
static uint32_t rnd() { rs ^= rs << 13; rs ^= rs >> 17; rs ^= rs << 5; return rs; }
int main(int argc, char** argv) {
    long iters = argc > 1 ? atol(argv[1]) : 20000;
    Machine m; std::string err;
    if (!m.boot("../Sorcery.dsk", &err)) { fprintf(stderr, "boot %s\n", err.c_str()); return 1; }
    for (long f = 0; f < 2500; f++) {
        if (f % 12 == 0) { uint32_t r = rnd(); if (f > 600) { m.setKey(72, r & 1); m.setKey(74, r >> 2 & 1); m.setKey(77, (r >> 4 & 3) == 0); } else m.setKey(76, f == 396); }
        m.runFrame();
    }
    m.verify = true;
    std::vector<uint8_t> base(m.ram, m.ram + 65536); Z80 cpu0 = m.cpu;
    const uint16_t addrs[] = {0x7D32, 0x7D51, 0x7DBE, 0x7E2D, 0x7EDB, 0x7F18, 0x7F37, 0x7F84, 0x7FA6, 0x7FCA,
                              0x7FD5, 0x7FEA, 0x85A7, 0x8F52, 0x8F78, 0x8FAD, 0x8FD3};
    auto rb = [&](int n) { return (uint8_t)(rnd() % n); };
    for (long it = 0; it < iters; it++) {
        memcpy(m.ram, base.data(), 65536); m.cpu = cpu0;
        const uint16_t a = addrs[it % (sizeof addrs / sizeof *addrs)];
        const uint16_t ixs[] = {0x93E8, 0x93F2, 0x93FC, 0x9406, 0x9410, 0x941A, 0x9424, 0x942E, 0x6802, 0x680C};
        const uint16_t ix = ixs[rnd() % 10];
        m.cpu.ix = ix;
        // entity near the player most of the time so that every overlap test is taken in both directions
        const uint8_t py = rb(8) ? rb(0x79) : rnd(), px = rb(8) ? rb(0x95) : rnd();
        m.ram[0x6841] = py; m.ram[0x6842] = px;
        m.ram[ix] = rb(2) ? 0x80 | rb(16) : rb(16);
        m.ram[ix + 1] = rb(2) ? (uint8_t)(py + rb(60) - 30) : (rb(8) ? rb(0x79) : rnd());
        m.ram[ix + 2] = rb(2) ? (uint8_t)(px + rb(30) - 15) : (rb(8) ? rb(0x95) : rnd());
        if (rb(8) == 0) m.ram[ix + 2] = px;
        m.ram[ix + 3] = rb(0x60);
        if (a == 0x7DBE && rb(2)) m.ram[ix + 3] = 0x0E + rb(14);   // walker frames
        if (a == 0x7E2D && rb(2)) m.ram[ix + 3] = 4 + rb(12);      // diver frames
        m.ram[ix + 4] = rb(4) == 0 ? rb(4) : rnd();
        if (rb(8) == 0) m.ram[ix + 4] = 0xFF;
        if (a == 0x7D32 && rb(2)) { m.ram[ix + 4] = 1 + rb(2); m.ram[ix + 5] = rb(2) ? 23 : rb(30); }
        else m.ram[ix + 5] = rb(3) ? 24 : rb(30);
        for (int i = 6; i < 10; i++) m.ram[ix + i] = rnd();
        if (a == 0x7EDB) m.ram[ix + 2] = rb(2) ? 0x32 + rb(16) : rnd();
        if (a == 0x7F84) m.ram[ix + 1] = rb(2) ? 0xC8 + rb(8) : rnd();
        if (a == 0x7FCA && rb(2)) m.ram[ix + 4] = 0xFF;
        if (a == 0x7FA6) { m.ram[ix + 4] = rb(2) ? 3 : rnd(); m.ram[ix + 3] = rb(2) ? 0x4F : 0x4C + rb(4); }
        m.ram[0x6840] = rb(4) ? 0x02 | (rb(16) << 4 & 0x10) : rnd();   // player state: mostly "playing"
        m.ram[0x6846] = rb(3) ? 0xFF : (rb(2) ? 0x2D : (rb(2) ? 0x41 : 0x2C));          // held item
        m.ram[0x6849] = rb(2) ? 0x10 : rnd();
        m.ram[0x684C] = rb(2) ? 0x80 : rb(0x80);
        m.ram[0x684D] = rb(3) ? (uint8_t)(rb(10) << 4 | rb(10)) : (uint8_t)(rb(2) ? 0 : 0x10 * rb(10));
        m.ram[0x684E] = rb(2) ? rb(8) : rnd();
        m.ram[0x6853] = rb(3) ? 0 : (rb(2) ? 1 : rnd());
        m.ram[0x6804] = rnd();
        for (int b = 0; b < 8; b++) {                                    // bolts
            if (rb(3)) { m.ram[0x9464 + 4 * b] = 0xFF; continue; }
            m.ram[0x9464 + 4 * b] = (uint8_t)(m.ram[ix + 1] + rb(60) - 30);
            m.ram[0x9464 + 4 * b + 1] = (uint8_t)(m.ram[ix + 2] + rb(30) - 15);
        }
        for (int i = 0; i < 720; i++) m.ram[0x6C90 + i] = rb(6) == 0 ? 0x80 | rb(128) : rb(128);
        if (rb(3) == 0) { m.ram[ix + 1] &= 0xF8; m.ram[ix + 2] &= 0xFC; }
        else { if (rb(2)) m.ram[ix + 1] &= 0xF8; if (rb(2)) m.ram[ix + 2] &= 0xFC; }
        m.cpu.c = rb(4) == 0 ? 0xFF : rb(0x60);
        m.ram[0x92C8] = m.ram[ix];                                   // what the dispatcher's saveEntity (8AAA) leaves
        memcpy(&m.ram[0x92C8], &m.ram[ix], 10);
        m.cpu.push(0x7000);
        uint8_t e0[10]; memcpy(e0, &m.ram[ix], 10);
        const uint64_t bad0 = m.vstats.count(a) ? m.vstats[a].bad : 0;
        m.native(a);
        if (m.vstats[a].bad != bad0) { printf("BAD %04X ix=%04X ent:", a, ix); for (int i = 0; i < 10; i++) printf(" %02X", e0[i]); printf("\n"); }
    }
    m.verifyReport();
}
