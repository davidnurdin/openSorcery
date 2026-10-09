#include "native.h"
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <vector>
static uint32_t rs = 987654321u;
static uint32_t rnd() { rs ^= rs << 13; rs ^= rs >> 17; rs ^= rs << 5; return rs; }
int main(int argc, char** argv) {
    long iters = argc > 1 ? atol(argv[1]) : 20000;
    Machine m; std::string err;
    if (!m.boot("../Sorcery.dsk", &err)) { fprintf(stderr, "boot %s\n", err.c_str()); return 1; }
    for (long f = 0; f < 2500; f++) {
        if (f % 12 == 0) { uint32_t r = rnd(); if (f > 600) { m.setKey(72, r & 1); m.setKey(74, r>>2&1); m.setKey(77,(r>>4&3)==0);} else m.setKey(76, f == 396); }
        m.runFrame();
    }
    m.verify = true;
    std::vector<uint8_t> base(m.ram, m.ram + 65536); Z80 cpu0 = m.cpu;
    const uint16_t addrs[] = {0x8AAA,0x8AB6,0x8AC0,0x8ACA,0x8F5C,0x8F82,0x8FB7,0x8FDD,0x905B,0x913E,0x9168,0x91B2,0x91DD,0x91E8,0x901E};
    auto rb = [&](int n){ return (uint8_t)(rnd() % n); };
    for (long it = 0; it < iters; it++) {
        memcpy(m.ram, base.data(), 65536); m.cpu = cpu0;
        uint16_t a = addrs[it % (sizeof addrs / sizeof *addrs)];
        auto randEntity = [&](uint16_t at) {
            m.ram[at] = rnd() & 1 ? 0x80 | rb(16) : rb(16);
            m.ram[at+1] = rb(8) ? rb(0x79) : rnd();
            m.ram[at+2] = rb(8) ? rb(0x95) : rnd();
            m.ram[at+3] = rb(77);
            m.ram[at+4] = rnd();
            m.ram[at+5] = rb(3) ? 24 : rb(30);
            for (int i = 6; i < 10; i++) m.ram[at+i] = rnd();
        };
        uint16_t ixs[] = {0x6840, 0x9352, 0x93E8, 0x93F2, 0x93FC, 0x9406, 0x9410, 0x941A, 0x9424, 0x942E};
        uint16_t ix = ixs[rnd() % 10];
        m.cpu.ix = ix;
        randEntity(ix);
        if (a >= 0x8AAA && a <= 0x8ACA) { randEntity(0x92C8); if (rb(2)) memcpy(&m.ram[0x92C8], &m.ram[ix], 10); if (rb(3)==0) m.ram[0x92C9]=m.ram[ix+1]+rb(5)-2; }
        if (a >= 0x8F5C && a <= 0x8FDD) {
            for (int i = 0; i < 720; i++) m.ram[0x6C90+i] = rb(5) == 0 ? 0x80 | rb(128) : rb(128);
            if (rb(2)) { m.ram[ix+1] &= 0xF8; } if (rb(2)) m.ram[ix+2] &= 0xFC; if (rb(4)==0) m.ram[ix+2] = (m.ram[ix+2] & 0xFC) | rb(4);
        }
        if (a == 0x905B) {
            m.ram[0x9375] = rb(7); m.ram[0x9376] = rb(4) * 0x20;
            uint8_t ids[] = {0x81, 0x01, 0x02, 0x03, 0x41};
            for (int i = 0; i < 720; i++) m.ram[0x6C90+i] = rb(3) == 0 ? ids[rb(5)] : rb(256);
            if (rb(4)==0) m.ram[0x6C90+719] = ids[rb(3)]; if (rb(4)==0) m.ram[0x6C90+718] = ids[rb(3)];
            for (int i = 0x6880; i < 0x69C0; i++) m.ram[i] = rnd();
            m.ram[0x6826] = rb(3)==0 ? 0xFF : rb(0x79); m.ram[0x6827] = rb(0x95); m.ram[0x6829] = rb(3)==0 ? 0xFF : rb(0x79); m.ram[0x682A] = rb(0x95);
        }
        if (a == 0x913E || a == 0x9168) {
            for (int s = 0; s < 8; s++) if (rb(2)) m.ram[0x93E8+10*s] = 0xFF;
            if (rb(4)==0) for (int s = 0; s < 8; s++) m.ram[0x93E8+10*s] = 0xFF;
            for (int r = 0; r < 4; r++) m.ram[0x6834+3*r+2] = rb(2) ? 0xFF : rb(70);
            if (rb(4)==0) for (int r = 0; r < 4; r++) m.ram[0x6834+3*r+2] = 0xFF;
            if (rb(4)==0) for (int r = 0; r < 4; r++) m.ram[0x6834+3*r+2] = 0;
            if (a == 0x9168 && rb(2)) m.ram[ix] = rb(16);
        }
        if (a == 0x91B2) {
            m.ram[0x6857] = rb(2) ? 0x80 : 0;
            auto bcd = [&]{ return rb(4) ? (uint8_t)(rb(10) << 4 | rb(10)) : (uint8_t)rnd(); };
            m.ram[0x92D4]=bcd(); m.ram[0x92D5]=bcd(); m.ram[0x92D6]=bcd();
            m.cpu.b = bcd(); m.cpu.c = bcd();
            if (rb(3)==0) { m.ram[0x92D6]=0x99; m.ram[0x92D5]=0x99; }
        }
        if (a == 0x91E8) { m.cpu.setHL(0x92D4 + rb(3)); m.cpu.setDE(0x0700 + rb(255)); m.ram[0x9374] = rb(2) ? 0xF0 : 0x0F; m.cpu.a = rnd(); }
        if (a == 0x901E) for (int i = 0x5800; i < 0x6800; i++) m.ram[i] = rnd();
        m.cpu.push(0x7000);
        uint8_t e0[10], o0[10]; memcpy(e0, &m.ram[ix], 10); memcpy(o0, &m.ram[0x92C8], 10);
        uint64_t bad0 = m.vstats.count(a) ? m.vstats[a].bad : 0;
        m.native(a);
        if (m.vstats[a].bad != bad0) { printf("BAD %04X ix=%04X ent:", a, ix); for (int i=0;i<10;i++) printf(" %02X", e0[i]); printf(" old:"); for (int i=0;i<10;i++) printf(" %02X", o0[i]); printf("\n"); }
    }
    m.verifyReport();
}
