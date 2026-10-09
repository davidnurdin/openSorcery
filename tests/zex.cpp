// CP/M harness running zexdoc/zexall against the Z80 core.
#include "../emu/z80.h"
#include <cstdio>
#include <cstdlib>
#include <vector>
struct Bus : Z80Bus {
    uint8_t m[65536] = {};
    uint8_t read(uint16_t a) override { return m[a]; }
    void write(uint16_t a, uint8_t v) override { m[a] = v; }
    uint8_t in(uint16_t) override { return 0xFF; }
    void out(uint16_t, uint8_t) override {}
};
int main(int argc, char** argv) {
    Bus bus; Z80 cpu(&bus);
    FILE* fp = fopen(argv[1], "rb"); if (!fp) return 1;
    size_t n = fread(&bus.m[0x100], 1, 0xF000, fp); fclose(fp);
    (void)n;
    bus.m[5] = 0xC9; bus.m[0] = 0x76; // BDOS stub at 5, warm boot halts
    cpu.pc = 0x100; cpu.sp = 0xFFF0;
    long long steps = 0;
    for (;;) {
        if (cpu.pc == 5) {
            if (cpu.c == 2) putchar(cpu.e);
            else if (cpu.c == 9) { for (uint16_t p = cpu.de(); bus.m[p] != '$'; p++) putchar(bus.m[p]); }
            fflush(stdout);
        }
        if (cpu.pc == 0 || cpu.halted) break;
        cpu.step(); steps++;
    }
    printf("\n[%lld instructions]\n", steps);
}
