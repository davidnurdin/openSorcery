#include "machine.h"
#include <cstdio>
#include <cstring>

// Firmware ink (0..26) -> hardware colour number, and back.
static const uint8_t INK2HW[27] = {0x14, 0x04, 0x15, 0x1C, 0x18, 0x1D, 0x0C, 0x05, 0x0D, 0x16, 0x06, 0x17, 0x1E, 0x00,
                                   0x1F, 0x0E, 0x07, 0x0F, 0x12, 0x02, 0x13, 0x1A, 0x19, 0x1B, 0x0A, 0x03, 0x0B};
static uint8_t HW2INK[32];
static uint32_t HWRGB[32];

static const int LEVEL[3] = {0x00, 0x80, 0xFF};

// Magic addresses used by the shims (inside the firmware area, never executed as code).
enum : uint16_t {
    FAR_BASE = 0xBF20,   // + AMSDOS command char (1..9)
    FW_IRQ = 0xBF40,
    SENTINEL = 0xBF7E,
};

Machine::Machine() : cpu(this) {
    memset(ram, 0, sizeof ram);
    for (int i = 0; i < 27; i++) {
        HW2INK[INK2HW[i]] = i;
        int g = i / 9, r = (i / 3) % 3, b = i % 3;
        HWRGB[INK2HW[i]] = 0xFF000000u | LEVEL[b] << 16 | LEVEL[g] << 8 | LEVEL[r];
    }
    // duplicates in the hardware table
    HW2INK[0x01] = 13; HWRGB[0x01] = HWRGB[0x00];
    HW2INK[0x08] = 7;  HWRGB[0x08] = HWRGB[0x05];
    HW2INK[0x09] = 25; HWRGB[0x09] = HWRGB[0x03];
    HW2INK[0x10] = 1;  HWRGB[0x10] = HWRGB[0x04];
    HW2INK[0x11] = 19; HWRGB[0x11] = HWRGB[0x02];
    memset(vid.hw, 0x14, sizeof vid.hw);
    vid0 = vid;
    installHooks();
}

void Machine::installHooks() {
    static const uint16_t list[] = {
        0x0008, 0x0018, 0x001E, 0xB906, 0xB909, 0xBB1E, 0xBB24, 0xBB5A, 0xBB75, 0xBC0E, 0xBC11, 0xBC32, 0xBC35, 0xBC38, 0xBC3E,
        0xBC77, 0xBC7A, 0xBC83, 0xBCA7, 0xBCAA, 0xBCAD, 0xBCB0, 0xBCB3, 0xBCB6, 0xBCB9, 0xBCBC, 0xBCBF, 0xBCCE, 0xBCD4, 0xBCD7, 0xBCE9, 0xBCEC,
        0xBCEF, 0xBCF5, 0xBD16, 0xBD1C, 0xBD25, FW_IRQ};
    for (uint16_t a : list) hooks[a] = 1;
}

static uint8_t decryptByte(uint8_t b) {   // loader: neg ; rrca ; sub 0x25
    uint8_t a = (uint8_t)(-b);
    a = (a >> 1) | (a << 7);
    return a - 0x25;
}

bool Machine::boot(const std::string& path, std::string* err) {
    if (!dsk.load(path, err)) return false;
    struct Item { const char* name; uint16_t addr; };
    static const Item items[] = {{"CONALP", 0x0100}, {"CODE", 0x7000}, {"TISCRN", 0x69C0}, {"SOUND.DAT", 0xA600}};
    memset(ram, 0, sizeof ram);
    for (auto& it : items) {
        const DskFile* f = dsk.find(it.name);
        if (!f || !f->hasHeader()) { if (err) *err = std::string("missing file ") + it.name; return false; }
        for (int i = 0; i < f->length(); i++) ram[(it.addr + i) & 0xFFFF] = decryptByte(f->body()[i]);
    }
    // firmware RAM vectors: RST 38 -> firmware interrupt handler
    ram[0x38] = 0xC3; ram[0x39] = FW_IRQ & 0xFF; ram[0x3A] = FW_IRQ >> 8;
    cpu.reset();
    cpu.pc = 0x7000; cpu.sp = 0xC000; cpu.im = 1; cpu.iff1 = cpu.iff2 = true;
    // firmware default palette
    static const uint8_t defInk[17] = {1, 24, 20, 6, 26, 0, 2, 8, 10, 12, 14, 16, 18, 22, 1, 16, 1};
    for (int p = 0; p < 17; p++) { fwInk[p][0] = fwInk[p][1] = defInk[p]; vid.hw[0][p] = vid.hw[1][p] = INK2HW[defInk[p]]; }
    fwInk[14][1] = 24; fwInk[15][1] = 11; flashPhase = 0; flashCnt = 0;
    vid.mode = 1;
    vid0 = vid;
    frameNo = 0; dead = false; irqPhase = 0;
    return true;
}

void Machine::setKey(int k, bool down) {
    if (k < 0 || k >= 80) return;
    if (down) keys[k >> 3] |= 1 << (k & 7); else keys[k >> 3] &= ~(1 << (k & 7));
}

void Machine::logVideo() { vlog.push_back({cycInFrame / CYC_LINE, vid}); }

void Machine::setInk(int pen, int i1, int i2) {
    if (pen < 0 || pen > 16) return;
    fwInk[pen][0] = i1 % 27; fwInk[pen][1] = i2 % 27;
    vid.hw[0][pen] = vid.hw[1][pen] = INK2HW[fwInk[pen][flashPhase]];
    logVideo();
}

void Machine::runSub(uint16_t addr) {
    Z80 saved = cpu;
    bool wasSub = inSub;
    inSub = true;
    cpu.iff1 = cpu.iff2 = false;
    cpu.push(SENTINEL);
    cpu.pc = addr;
    long guard = 0;
    while (cpu.pc != SENTINEL && !dead) {
        if ((cpu.pc >= 0xB900 || cpu.pc < 0x40) && cpu.pc != SENTINEL && callHook(cpu.pc)) {}
        else cycInFrame += cpu.step();
        if (++guard > 5000000) { die("runSub overrun"); break; }
    }
    cpu = saved;
    inSub = wasSub;
}

// Firmware interrupt service (chained from the game's own handler or reached through RST 38).
void Machine::fwIrq() {
    // frame flyback: VSYNC is active at the interrupt following the sync pulse
    bool tick50 = vsyncPending;   // the firmware's 1/50 s tick is locked to the frame flyback
    if (vsyncPending) {
        vsyncPending = false;
        auto fl = flyEvents;
        for (uint16_t blk : fl) {
            auto it = events.find(blk);
            if (it != events.end()) runSub(it->second.routine);
        }
    }
    // 50 Hz tick: every 6th interrupt
    if (tick50) {
        // firmware ink flash manager: when the flash period expires the whole firmware ink table is sent to the
        // hardware again (the game relies on this to restore its palette every frame: SCR SET FLASHING 1,1)
        if (++flashCnt >= vid.flashPeriod[flashPhase]) {
            flashCnt = 0; flashPhase ^= 1;
            for (int p = 0; p < 17; p++) vid.hw[0][p] = vid.hw[1][p] = INK2HW[fwInk[p][flashPhase]];
            logVideo();
        }
        std::vector<Ticker> tk = tickers;
        for (size_t i = 0; i < tickers.size(); i++) {
            if (--tickers[i].count <= 0) {
                tickers[i].count = tickers[i].refill ? tickers[i].refill : 0x7FFFFFFF;
                auto it = events.find(tickers[i].block + 6);
                if (it != events.end()) runSub(it->second.routine);
            }
        }
    }
}

void Machine::runFrame() {
    static const int irqLines[6] = {34, 86, 138, 190, 242, 294};
    static const int VSYNC_LINE = 240;
    vid0 = vid;
    vlog.clear();
    cycInFrame = 0;
    int ni = 0;
    bool vsyncDone = false;
    int nextSnd = 0;
    // frame flash timing
    while (cycInFrame < CYC_FRAME && !dead) {
        int line = cycInFrame / CYC_LINE;
        if (line >= nextSnd) { nextSnd += 156; snd.tick(); }
        if (!vsyncDone && line >= VSYNC_LINE) { vsyncDone = true; vsyncPending = true; }
        if (ni < 6 && line >= irqLines[ni]) { ni++; irqPending = true; }
        if (irqPending && !inSub) {
            int c = cpu.irq(0xFF);
            if (c) { irqPending = false; cycInFrame += c; continue; }
        }
        uint16_t pc = cpu.pc;
        if ((pc >= 0xB900 || pc < 0x40) && callHook(pc)) { cycInFrame += 8; continue; }
        if (profile) execCnt[pc]++;
        cycInFrame += cpu.step();
    }
    frameNo++;
}

bool Machine::callHook(uint16_t pc) {
    if (!hooks.count(pc)) return false;
    auto& c = cpu;
    auto setZ = [&](bool z) { c.f = z ? (c.f | 0x40) : (c.f & ~0x40); };
    auto setC = [&](bool v) { c.f = v ? (c.f | 1) : (c.f & ~1); };
    switch (pc) {
    case 0x0008: die("RST 8 (LOW JUMP) hit: firmware restart / fatal error"); return true;
    case 0x0018: {   // RST 3: KL FAR CALL, inline pointer to 3-byte far address
        uint16_t ret = c.pop();
        uint16_t ptr = c.rw(ret);
        uint16_t dest = c.rw(ptr);
        c.pc = ret + 2;
        if (dest >= FAR_BASE && dest < FAR_BASE + 16) farCall(dest - FAR_BASE);
        else die("far call to unknown address");
        return true; }
    case 0x001E:   // firmware 'PCHL' (jp (hl)): the game dispatches entity handlers with `call 001E`.
        // 92A5 uses it with HL=0 (flyback wait through the firmware jump block): treat that as one firmware interrupt service.
        if (c.hl() >= 0x0100) c.pc = c.hl(); else c.pc = 0x38;
        return true;
    case 0xB906: case 0xB909: doRet(); return true;           // KL L ROM ENABLE / DISABLE
    case 0xBB1E: {                                            // KM TEST KEY
        int k = c.a & 0x7F;
        bool pressed = keys[k >> 3] & (1 << (k & 7));
        setZ(!pressed); setC(false);
        doRet(); return true; }
    case 0xBB24: {                                            // KM GET JOYSTICK
        auto joy = [&](int) { return (uint8_t)(keys[9] & 0x3F); };   // row 9: up down left right fire2 fire1
        c.h = joy(0); c.l = 0; c.a = c.h;
        doRet(); return true; }
    case 0xBB5A: case 0xBB75: doRet(); return true;           // TXT OUTPUT / SET CURSOR (not rendered)
    case 0xBC0E: {                                            // SCR SET MODE
        vid.mode = c.a & 3;
        memset(&ram[0xC000], 0, 0x4000);
        logVideo();
        doRet(); return true; }
    case 0xBD1C: vid.mode = c.a & 3; logVideo(); doRet(); return true;   // MC SET MODE
    case 0xBC11: {                                            // SCR GET MODE
        c.a = vid.mode;
        setC(vid.mode == 0); setZ(vid.mode == 1);
        doRet(); return true; }
    case 0xBC32: setInk(c.a & 0x1F, c.b & 0x1F, c.c & 0x1F); doRet(); return true;   // SCR SET INK
    case 0xBC35: {                                            // SCR GET INK
        int p = c.a & 0x1F; if (p > 16) p = 16;
        c.b = fwInk[p][0]; c.c = fwInk[p][1];
        doRet(); return true; }
    case 0xBC38: setInk(16, c.b & 0x1F, c.c & 0x1F); doRet(); return true;           // SCR SET BORDER
    case 0xBC3E: vid.flashPeriod[0] = c.h ? c.h : 256; vid.flashPeriod[1] = c.l ? c.l : 256; doRet(); return true;
    case 0xBD25: {                                            // MC SET INKS: border + 16 pens
        uint16_t p = c.de();
        for (int i = 0; i < 17; i++) {
            int pen = i == 0 ? 16 : i - 1;
            vid.hw[0][pen] = vid.hw[1][pen] = ram[(p + i) & 0xFFFF] & 0x1F;
        }
        logVideo();
        doRet(); return true; }
    case 0xBCCE: setC(true); doRet(); return true;            // KL INIT BACK
    case 0xBCD4: {                                            // KL FIND COMMAND (AMSDOS hidden BIOS commands)
        uint8_t ch = ram[c.hl()] & 0x7F;
        if (ch >= 1 && ch <= 9) { c.setHL(FAR_BASE + ch); c.c = 7; setC(true); }
        else setC(false);
        doRet(); return true; }
    case 0xBCD7: {                                            // KL NEW FRAME FLY
        events[c.hl()] = {c.hl(), c.de(), c.b};
        flyEvents.push_back(c.hl());
        doRet(); return true; }
    case 0xBCEF:                                              // KL INIT EVENT
        events[c.hl()] = {c.hl(), c.de(), c.b};
        doRet(); return true;
    case 0xBCE9:                                              // KL ADD TICKER
        tickers.push_back({c.hl(), (int)c.de(), (int)c.bc()});
        doRet(); return true;
    case 0xBCEC:                                              // KL DEL TICKER
        for (size_t i = 0; i < tickers.size(); i++)
            if (tickers[i].block == c.hl()) { c.setDE(tickers[i].count); tickers.erase(tickers.begin() + i); setC(true); doRet(); return true; }
        setC(false); doRet(); return true;
    case 0xBCF5: doRet(); return true;                        // KL SYNC RESET
    // ---- sound manager ----
    case 0xBCA7: snd.reset(); doRet(); return true;                    // SOUND RESET
    case 0xBCAA: {                                                     // SOUND QUEUE
        uint8_t p[9];
        for (int i = 0; i < 9; i++) p[i] = ram[(c.hl() + i) & 0xFFFF];
        setC(snd.queue(p)); doRet(); return true; }
    case 0xBCAD: c.a = snd.check(c.a); doRet(); return true;           // SOUND CHECK
    case 0xBCB3: snd.release(c.a); doRet(); return true;               // SOUND RELEASE
    case 0xBCB6: snd.hold(); doRet(); return true;                     // SOUND HOLD
    case 0xBCB9: snd.cont(); doRet(); return true;                     // SOUND CONTINUE
    case 0xBCB0: doRet(); return true;                                 // SOUND ARM EVENT
    case 0xBCBC: case 0xBCBF: {                                        // SOUND AMPL / TONE ENVELOPE
        uint8_t d[16];
        for (int i = 0; i < 16; i++) d[i] = ram[(c.hl() + i) & 0xFFFF];
        bool ok = pc == 0xBCBC ? snd.setAmpl(c.a, d) : snd.setTone(c.a, d);
        if (ok) c.setHL(c.hl() + 16);
        setC(ok); doRet(); return true; }
    // ---- cassette / disc file access ----
    case 0xBC77: {                                            // CAS IN OPEN
        std::string nm;
        for (int i = 0; i < c.b; i++) nm += (char)ram[(c.hl() + i) & 0xFFFF];
        casFile = dsk.find(nm);
        if (!casFile || !casFile->hasHeader()) { setC(false); casFile = nullptr; doRet(); return true; }
        c.setBC(casFile->length());
        c.setDE(casFile->loadAddr());
        c.a = casFile->type();
        setC(true); setZ(false);
        doRet(); return true; }
    case 0xBC83: {                                            // CAS IN DIRECT
        if (!casFile) { setC(false); doRet(); return true; }
        uint16_t dst = c.hl();
        for (int i = 0; i < casFile->length(); i++) ram[(dst + i) & 0xFFFF] = casFile->body()[i];
        c.setHL(casFile->entry());
        setC(true);
        doRet(); return true; }
    case 0xBC7A: casFile = nullptr; setC(true); doRet(); return true;   // CAS IN CLOSE
    case 0xBD16: doRet(); return true;                        // MC START PROGRAM (loader only)
    case FW_IRQ:
        fwIrq();
        c.iff1 = c.iff2 = true;
        doRet();
        return true;
    default: return false;
    }
}

// AMSDOS disc BIOS (reached through hidden RSX commands 0x81..0x89 + far call)
void Machine::farCall(int idx) {
    auto& c = cpu;
    auto ok = [&](bool v) { c.f = v ? (c.f | 1) : (c.f & ~1); if (v) c.a = 0; };
    switch (idx) {
    case 1: case 2: case 3: case 9: ok(true); break;        // set message / setup disc / select format / retry count
    case 4: {                                                // READ SECTOR: HL=buf D=track E=drive C=sector id
        uint8_t buf[512];
        if (dsk.readSector(c.d, c.c, buf)) { for (int i = 0; i < 512; i++) ram[(c.hl() + i) & 0xFFFF] = buf[i]; ok(true); }
        else ok(false);
        break; }
    case 5: {                                                // WRITE SECTOR
        uint8_t buf[512];
        for (int i = 0; i < 512; i++) buf[i] = ram[(c.hl() + i) & 0xFFFF];
        ok(dsk.writeSector(c.d, c.c, buf));
        break; }
    case 7: ok(true); break;                                 // MOVE TRACK
    case 6: case 8: ok(true); break;
    default: die("unknown AMSDOS BIOS call"); break;
    }
}

void Machine::render(uint32_t* out) {
    // Reconstruct the video state of every scanline of the last frame.
    static Video lineV[LINES];
    Video cur = vid0;
    size_t ei = 0;
    for (int l = 0; l < LINES; l++) {
        while (ei < vlog.size() && vlog[ei].line <= l) cur = vlog[ei++].v;
        lineV[l] = cur;
    }
    auto colour = [&](const Video& v, int pen) -> uint32_t {
        uint8_t hw = v.hw[0][pen];
        return HWRGB[hw & 0x1F];
    };
    for (int r = 0; r < OUT_H; r++) {
        int line;
        int yy = r - BORDER_Y;
        if (yy < 0) line = LINES - (BORDER_Y - r + 1) / 2;
        else if (yy >= SCR_H) line = 200 + (yy - SCR_H) / 2;
        else line = yy / 2;
        const Video& v = lineV[line];
        uint32_t border = colour(v, 16);
        uint32_t* row = out + (size_t)r * OUT_W;
        if (yy < 0 || yy >= SCR_H) { for (int x = 0; x < OUT_W; x++) row[x] = border; continue; }
        for (int x = 0; x < BORDER_X; x++) row[x] = row[OUT_W - 1 - x] = border;
        int y = yy / 2;
        const uint8_t* src = &ram[0xC000 + (y >> 3) * 80 + (y & 7) * 0x800];
        uint32_t* d = row + BORDER_X;
        switch (v.mode) {
        case 0:
            for (int i = 0; i < 80; i++) {
                uint8_t b = src[i];
                int p0 = (b >> 7 & 1) | (b >> 3 & 1) << 1 | (b >> 5 & 1) << 2 | (b >> 1 & 1) << 3;
                int p1 = (b >> 6 & 1) | (b >> 2 & 1) << 1 | (b >> 4 & 1) << 2 | (b & 1) << 3;
                uint32_t c0 = colour(v, p0), c1 = colour(v, p1);
                for (int k = 0; k < 4; k++) { *d++ = c0; }
                for (int k = 0; k < 4; k++) { *d++ = c1; }
            }
            break;
        case 1:
            for (int i = 0; i < 80; i++) {
                uint8_t b = src[i];
                for (int p = 0; p < 4; p++) {
                    int pen = ((b >> (3 - p)) & 1) | ((b >> (7 - p)) & 1) << 1;
                    uint32_t c0 = colour(v, pen);
                    *d++ = c0; *d++ = c0;
                }
            }
            break;
        default:
            for (int i = 0; i < 80; i++) {
                uint8_t b = src[i];
                for (int p = 0; p < 8; p++) *d++ = colour(v, (b >> (7 - p)) & 1);
            }
            break;
        }
    }
}

void Machine::debugVideo(FILE* f) const {
    fprintf(f, "frame start: mode=%d pal0:", vid0.mode);
    for (int p = 0; p < 17; p++) fprintf(f, " %02X", vid0.hw[0][p]);
    fprintf(f, "\n");
    Video prev = vid0;
    for (auto& e : vlog) {
        fprintf(f, "line %3d mode=%d", e.line, e.v.mode);
        for (int p = 0; p < 17; p++) if (e.v.hw[0][p] != prev.hw[0][p]) fprintf(f, " pen%d:%02X->%02X", p, prev.hw[0][p], e.v.hw[0][p]);
        fprintf(f, "\n");
        prev = e.v;
    }
}
