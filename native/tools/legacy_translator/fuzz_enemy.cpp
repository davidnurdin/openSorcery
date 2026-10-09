// Fuzz driver for the entity handlers of port_enemy.cpp (81A2, 81BD, 81DE-844D, 850C): boots the game, then repeatedly
// sets random plausible RAM state and calls each routine under the verifier.
//   g++ -O1 -std=c++17 -Inative/src -o fuzz_enemy native/tools/fuzz_enemy.cpp <all native/src/*.cpp except main.cpp> emu/z80.cpp common/dsk.cpp
//   SORCERY_VERIFY_ONLY="81DE ..." ./fuzz_enemy 20000   (run from the sorcery directory)
#include "native.h"
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <vector>
static uint32_t rs = 123456789u;
static uint32_t rnd() { rs ^= rs << 13; rs ^= rs >> 17; rs ^= rs << 5; return rs; }
int main(int argc, char** argv) {
    long iters = argc > 1 ? atol(argv[1]) : 20000;
    Machine m; std::string err;
    if (!m.boot("../Sorcery.dsk", &err)) { fprintf(stderr, "boot %s\n", err.c_str()); return 1; }
    for (long f = 0; f < 1500; f++) {
        if (f % 12 == 0) { uint32_t r = rnd(); if (f > 600) { m.setKey(72, r & 1); m.setKey(74, r>>2&1); m.setKey(77,(r>>4&3)==0);} else m.setKey(76, f == 396); }
        m.runFrame();
    }
    m.verify = true;
    std::vector<uint8_t> base(m.ram, m.ram + 65536); Z80 cpu0 = m.cpu;
    auto rb = [&](int n){ return (uint8_t)(rnd() % n); };
    std::vector<uint16_t> addrs;
    for (uint16_t a : {0x81DE,0x8205,0x824E,0x826B,0x8292,0x82BD,0x82F9,0x831B,0x8341,0x835E,0x836A,0x8374,0x838E,0x83A5,0x83C5,0x83D0,0x83DF,0x83EC,0x83FE,0x8416,0x842D,0x8440,0x844D,0x850C,0x81BD,0x81A2}) addrs.push_back(a);
    const uint16_t slots[] = {0x6802, 0x680C, 0x93E8, 0x93F2, 0x93FC, 0x9406, 0x9410, 0x941A, 0x9424, 0x942E, 0x9438, 0x9352};
    for (long it = 0; it < iters; it++) {
        memcpy(m.ram, base.data(), 65536); m.cpu = cpu0;
        uint16_t a = addrs[it % addrs.size()];
        auto randEntity = [&](uint16_t at, int type) {
            m.ram[at] = (rb(3) ? 0 : 0x80) | type;
            m.ram[at+1] = rb(6) ? rb(0x79) : rnd();
            m.ram[at+2] = rb(6) ? rb(0x95) : rnd();
            m.ram[at+3] = rb(8) ? 0x28 + rb(0x38) : rnd();
            m.ram[at+4] = rb(3) ? rnd() : (uint8_t)(0xFC + rb(4) - (rb(2) ? 0 : 0x10));
            m.ram[at+5] = rb(2) ? (rb(2) ? 0x17 : 0) : rb(30);
            for (int i = 6; i < 10; i++) m.ram[at+i] = rnd();
            if (rb(3) == 0) m.ram[at+1] = 0x6F + rb(3);
            if (rb(4) == 0) m.ram[at+4] = rb(2) ? 0xFF : 0x7F + rb(3);
            if (rb(4) == 0) m.ram[at+4] = (m.ram[at+4] & 0xFC) | 3;
            if (rb(5) == 0) m.ram[at+3] = 0x47 + rb(8);
            if (rb(6) == 0) m.ram[at+6] ^= 0x10;
            if (type == 0x2D && rb(2)) { uint16_t p = 0x9000 + rb(200); m.ram[at+8] = p; m.ram[at+9] = p >> 8; m.ram[p] = rb(3) ? rb(8) : 0x80; }
        };
        uint16_t ix = slots[rnd() % 12];
        int type = 0x20 + rb(0x17);
        if (a == 0x81BD || a == 0x81A2) type = rb(5) == 0 ? rb(10) : (rb(30) == 0 ? 0x7F : 0x20 + rb(0x17));
        else if (a != 0x850C) type = 0x20 + (a == 0x81DE ? 0 : 0);   // placeholder, set below
        // handler addresses -> state value
        static const uint16_t hs[] = {0x81DE,0x8205,0x824E,0x826B,0x8292,0x82BD,0x82F9,0x831B,0x8341,0x835E,0x836A,0x8374,0x838E,0x83A5,0x83C5,0x83D0,0x83DF,0x83EC,0x83FE,0x8416,0x842D,0x8440,0x844D};
        for (int i = 0; i < 23; i++) if (hs[i] == a) type = 0x20 + i;
        for (int s = 0; s < 8; s++) if (rb(2)) m.ram[0x93E8+10*s] = 0xFF;
        m.cpu.ix = ix;
        randEntity(ix, type);
        if (a == 0x81A2) for (int i = 0; i < 11; i++) { uint16_t s = i < 2 ? 0x6802 + 10*i : 0x93E8 + 10*(i-2); int t = rb(4) == 0 ? rb(10) : (rb(2) ? 0x7F : 0x20 + rb(0x17)); if (t == 0x7F || rb(3)) randEntity(s, t); if (t == 0x7F) m.ram[s] = rb(2) ? 0xFF : 0x7F; }
        if (rb(2)) memcpy(&m.ram[0x92C8], &m.ram[ix], 10); else for (int i = 0; i < 10; i++) m.ram[0x92C8+i] = rnd();
        // player and room record
        m.ram[0x6841] = rb(0x79); m.ram[0x6842] = rb(0x95); m.ram[0x6840] = rb(2) ? 0x02 : rnd();
        for (int i = 0; i < 32; i++) { m.ram[0x9464+i] = rb(3) ? 0xFF : rb(0x90); m.ram[0x9484+i] = rb(3) ? 0xFF : rb(0x90); }
        for (int i = 0x6C90; i < 0x6C90 + 720; i++) m.ram[i] = rb(5) == 0 ? 0x80 | rb(128) : rb(128);
        m.ram[0x945B] = rb(4);
        for (int r = 0; r < 4; r++) m.ram[0x6834+3*r+2] = rb(2) ? 0xFF : rb(70);
        m.ram[0x6857] = rb(2) ? 0x80 : 0;
        m.cpu.push(0x7000);
        uint64_t bad0 = m.vstats.count(a) ? m.vstats[a].bad : 0;
        uint8_t e0[10]; memcpy(e0, &m.ram[ix], 10);
        m.native(a);
        if (m.vstats[a].bad != bad0) { printf("BAD %04X ix=%04X ent:", a, ix); for (int i=0;i<10;i++) printf(" %02X", e0[i]); printf("\n"); }
    }
    m.verifyReport();
}
