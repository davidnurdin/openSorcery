#include "machine.h"
#include "native.h"
#include <algorithm>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <ucontext.h>

// Firmware ink (0..26) -> hardware colour number, and back.
static const uint8_t INK2HW[27] = {0x14, 0x04, 0x15, 0x1C, 0x18, 0x1D, 0x0C, 0x05, 0x0D, 0x16, 0x06, 0x17, 0x1E, 0x00,
                                   0x1F, 0x0E, 0x07, 0x0F, 0x12, 0x02, 0x13, 0x1A, 0x19, 0x1B, 0x0A, 0x03, 0x0B};
static uint8_t HW2INK[32];
static uint32_t HWRGB[32];
static const int LEVEL[3] = {0x00, 0x80, 0xFF};

// Far-call "addresses" handed out by KL FIND COMMAND for the hidden AMSDOS BIOS commands 0x81..0x89.
constexpr uint16_t FAR_BASE = 0xBF20;

// The cooperative game task.
struct Machine::Task {
    ucontext_t host{}, co{};
    std::vector<char> stack = std::vector<char>(1 << 20);
    std::function<void()> body;
    bool finished = false;
};

static Machine::Task* gTask = nullptr;
static void taskEntry() { gTask->body(); gTask->finished = true; }

Machine::Machine() {
    memset(ram, 0, sizeof ram);
    for (int i = 0; i < 27; i++) {
        HW2INK[INK2HW[i]] = i;
        int g = i / 9, r = (i / 3) % 3, b = i % 3;
        HWRGB[INK2HW[i]] = 0xFF000000u | LEVEL[b] << 16 | LEVEL[g] << 8 | LEVEL[r];
    }
    HW2INK[0x01] = 13; HWRGB[0x01] = HWRGB[0x00];     // duplicates in the hardware colour table
    HW2INK[0x08] = 7;  HWRGB[0x08] = HWRGB[0x05];
    HW2INK[0x09] = 25; HWRGB[0x09] = HWRGB[0x03];
    HW2INK[0x10] = 1;  HWRGB[0x10] = HWRGB[0x04];
    HW2INK[0x11] = 19; HWRGB[0x11] = HWRGB[0x02];
    memset(vid.hw, 0x14, sizeof vid.hw);
    vid0 = vid;
}

Machine::~Machine() { delete task; }

bool Machine::boot(const std::string& dir, std::string* err) {
    if (!res.load(dir, err)) return false;
    // the files the original loader put in memory before starting the game
    struct Item { const char* name; uint16_t addr; };
    static const Item items[] = {{"CONALP", 0x0100}, {"CODE", 0x7000}, {"TISCRN", 0x69C0}, {"SOUND", 0xA600}};
    memset(ram, 0, sizeof ram);
    for (auto& it : items) {
        const std::vector<uint8_t>* f = res.file(it.name);
        if (!f) { if (err) *err = std::string("missing resource ") + it.name; return false; }
        for (size_t i = 0; i < f->size(); i++) ram[(it.addr + i) & 0xFFFF] = (*f)[i];
    }
    ram[0x38] = 0xC3; ram[0x39] = 0x40; ram[0x3A] = 0xBF;      // firmware RST 38 vector: the game copies this word
    cpu = Z80();
    cpu.sp = 0xC000; cpu.im = 1; cpu.iff1 = cpu.iff2 = true;
    static const uint8_t defInk[17] = {1, 24, 20, 6, 26, 0, 2, 8, 10, 12, 14, 16, 18, 22, 1, 16, 1};   // firmware default palette
    for (int p = 0; p < 17; p++) { fwInk[p][0] = fwInk[p][1] = defInk[p]; vid.hw[0][p] = vid.hw[1][p] = INK2HW[defInk[p]]; }
    fwInk[14][1] = 24; fwInk[15][1] = 11; flashPhase = 0; flashCnt = 0;
    vid.mode = 1;
    vid0 = vid;
    events.clear(); tickers.clear(); flyEvents.clear();
    frameNo = 0; dead = false;
    return true;
}

void Machine::setKey(int k, bool down) {
    if (k < 0 || k >= 80) return;
    if (down) keys[k >> 3] |= 1 << (k & 7); else keys[k >> 3] &= ~(1 << (k & 7));
}

void Machine::logVideo() {
    // Raster effects are timed from the interrupt that triggers them (nominal time), not from the work done meanwhile.
    int64_t t = cycInFrame;
    vlog.push_back({(int)(t / CYC_LINE), vid});
}

void Machine::setInk(int pen, int i1, int i2) {
    if (pen < 0 || pen > 16) return;
    fwInk[pen][0] = i1 % 27; fwInk[pen][1] = i2 % 27;
    vid.hw[0][pen] = vid.hw[1][pen] = INK2HW[fwInk[pen][flashPhase]];
    logVideo();
}

void Machine::mcSetInks(uint16_t p) {
    for (int i = 0; i < 17; i++) {
        int pen = i == 0 ? 16 : i - 1;
        vid.hw[0][pen] = vid.hw[1][pen] = ram[(uint16_t)(p + i)] & 0x1F;
    }
    logVideo();
}

// ---------------------------------------------------------------------------------------------------------------------
// Calling routines of the original program
// ---------------------------------------------------------------------------------------------------------------------
void Machine::call(uint16_t addr) {
    if (const NativeDef* d = findNative(addr)) { d->fn(*this); return; }
    if (firmware(addr)) return;
    char t[96];
    snprintf(t, sizeof t, "call of the not yet ported routine %04X", addr);
    die(t);
}

// ---------------------------------------------------------------------------------------------------------------------
// Game task / scheduler
// ---------------------------------------------------------------------------------------------------------------------
void Machine::startGame(std::function<void()> body) {
    delete task;
    task = new Task;
    gTask = task;
    task->body = std::move(body);
    getcontext(&task->co);
    task->co.uc_stack.ss_sp = task->stack.data();
    task->co.uc_stack.ss_size = task->stack.size();
    task->co.uc_link = &task->host;
    makecontext(&task->co, (void (*)())taskEntry, 0);
    swapcontext(&task->host, &task->co);        // runs until the first waitIrq()
}

void Machine::waitIrq() {
    if (inIrq || !task) { die("waitIrq outside of the game task"); return; }
    swapcontext(&task->co, &task->host);
}

// The game's own interrupt handler (original 8D55): runs the routine of the current phase (6 phases per frame, selected by the
// table pointer at 92F5 and the phase counter 92F7), advances the phase, counts the tick down and chains to the firmware.
//   game table 92DD: 8D99 mode 0 | 8D9D joystick | 8DB9 energy-bar fade | 8DBC - | 8DBD status-area palette + mode 1 | 8DD3 -
//   menu table 92E9: 8DD4 joystick | 8DD6..8DD9 - | 8DDA tick flag
void Machine::gameIrq() {
    constexpr uint16_t PHASE = 0x92F7, TABLE = 0x92F5, IY = 0x6800;
    uint16_t handler = rw(rw(TABLE) + 2 * ram[PHASE]);
    auto joystick = [&] {
        if (ram[IY + 86] & 0x80) return;                    // (iy+86) bit 7: joystick polling disabled
        uint8_t a = keys[9] & 0x3F;                         // KM GET JOYSTICK
        ram[IY + 82] = a;
        if (a & 0x10) ram[IY + 82] = a ^ ram[IY + 75];      // fire is only reported once until released ((iy+75) = latch)
        else ram[IY + 75] = 0;
    };
    switch (handler) {
    case 0x8D99: mcSetMode(0); break;
    case 0x8D9D: case 0x8DD4: joystick(); break;
    case 0x8DB9: { Z80 saved = cpu; call(0x9257); cpu = saved; break; }    // energy-bar fade
    case 0x8DBC: case 0x8DD3: case 0x8DD6: case 0x8DD7: case 0x8DD8: case 0x8DD9: break;
    case 0x8DBD:
        ram[IY + 74] |= 0x20;                               // 50 Hz tick for the main loop
        cycInFrame += 780;                                  // the original burns ~2.6 scanlines before switching the screen area
        mcSetInks(0x936F);                                  // text-area palette
        mcSetMode(1);
        break;
    case 0x8DDA: ram[IY + 74] |= 0x20; break;
    default: { char t[64]; snprintf(t, sizeof t, "unknown interrupt phase routine %04X", handler); die(t); break; }
    }
    uint8_t ph = ram[PHASE] + 1;
    ram[PHASE] = ph < 6 ? ph : 0;
    ram[IY + 72]--;
    fwInterrupt();
}

void Machine::fwInterrupt() {
    // frame flyback: VSYNC is active at the interrupt following the sync pulse
    bool tick50 = vsyncPending;   // the firmware's 1/50 s tick is locked to the frame flyback
    if (vsyncPending) {
        vsyncPending = false;
        auto fl = flyEvents;
        for (uint16_t blk : fl) {
            auto it = events.find(blk);
            if (it == events.end()) continue;
            if (it->second.routine == 0x8D87) {                 // the game's flyback event: resync the interrupt phase, tamper pen 13
                ram[0x92F7] = 1;
                fwInk[13][0] = (fwInk[13][0] + 1) % 27; fwInk[13][1] = (fwInk[13][1] + 1) % 27;
                vid.hw[0][13] = vid.hw[1][13] = INK2HW[fwInk[13][flashPhase]];
                logVideo();
            } else { Z80 s = cpu; call(it->second.routine); cpu = s; }
        }
    }
    if (tick50) {
        // firmware ink flash manager: when the flash period expires the whole firmware ink table is sent to the
        // hardware again (the game relies on this to restore its palette every frame: SCR SET FLASHING 1,1)
        if (++flashCnt >= vid.flashPeriod[flashPhase]) {
            flashCnt = 0; flashPhase ^= 1;
            for (int p = 0; p < 17; p++) vid.hw[0][p] = vid.hw[1][p] = INK2HW[fwInk[p][flashPhase]];
            logVideo();
        }
        for (size_t i = 0; i < tickers.size(); i++) {
            if (--tickers[i].count <= 0) {
                tickers[i].count = tickers[i].refill ? tickers[i].refill : 0x7FFFFFFF;
                auto it = events.find(tickers[i].block + 6);
                if (it != events.end()) { Z80 s = cpu; call(it->second.routine); cpu = s; }
            }
        }
    }
}

void Machine::deliverIrq(int line, bool game) {
    cycInFrame = (int64_t)line * CYC_LINE;
    inIrq = true;
    rng += (uint32_t)line * 2654435761u;           // stir the random source with the interrupt timing
    if (getenv("SORCERY_IRQLOG")) fprintf(stderr, "IRQ f%llu line%d game=%d phase=%d table=%04X\n", (unsigned long long)frameNo, line, (int)game, ram[0x92F7], rw(0x92F5));
    if (game) gameIrq();
    else fwInterrupt();
    inIrq = false;
}

void Machine::runFrame() {
    static const int irqLines[6] = {34, 86, 138, 190, 242, 294};
    static const int VSYNC_LINE = 240;
    vid0 = vid;
    vlog.clear();
    cycInFrame = 0;
    snd.tick();                                    // 100 Hz sound manager: at lines 0 and 156
    bool soundTick2 = false, vsync = false;
    for (int k = 0; k < 6 && !dead; k++) {
        int line = irqLines[k];
        if (!soundTick2 && line >= 156) { soundTick2 = true; snd.tick(); }
        if (!vsync && line >= VSYNC_LINE) { vsync = true; vsyncPending = true; }
        // the game's interrupt handler is active once it has installed its vector (IM 2, table at 5400, stub at 5555)
        bool game = cpu.im == 2 && ram[0x5555] == 0xC3 && rw(0x5556) == 0x8D55;
        if (cpu.iff1) deliverIrq(line, game);
        // let the game task run until it waits for the next interrupt
        if (task && !task->finished) {
            gTask = task;
            cycInFrame = (int64_t)line * CYC_LINE + 800;
            swapcontext(&task->host, &task->co);
        }
    }
    frameNo++;
}

bool Machine::firmware(uint16_t pc) {
    auto& c = cpu;
    auto setZ = [&](bool z) { c.f = z ? (c.f | 0x40) : (c.f & ~0x40); };
    auto setC = [&](bool v) { c.f = v ? (c.f | 1) : (c.f & ~1); };
    switch (pc) {
    case 0x001E:   // firmware PCHL (jp (hl)): the game dispatches entity handlers with `call 001E`; HL=0 = flyback wait of 92A5
        if (c.hl() >= 0x0100) call(c.hl()); else fwInterrupt();
        return true;
    case 0xB906: case 0xB909: return true;           // KL L ROM ENABLE / DISABLE
    case 0xBB1E: {                                            // KM TEST KEY
        int k = c.a & 0x7F;
        bool pressed = keys[k >> 3] & (1 << (k & 7));
        setZ(!pressed); setC(false);
        return true; }
    case 0xBB24: {                                            // KM GET JOYSTICK
        auto joy = [&](int) { return (uint8_t)(keys[9] & 0x3F); };   // row 9: up down left right fire2 fire1
        c.h = joy(0); c.l = 0; c.a = c.h;
        return true; }
    case 0xBB5A: case 0xBB75: return true;           // TXT OUTPUT / SET CURSOR (not rendered)
    case 0xBC0E: {                                            // SCR SET MODE
        vid.mode = c.a & 3;
        memset(&ram[0xC000], 0, 0x4000);
        logVideo();
        return true; }
    case 0xBD1C: vid.mode = c.a & 3; logVideo(); return true;   // MC SET MODE
    case 0xBC11: {                                            // SCR GET MODE
        c.a = vid.mode;
        setC(vid.mode == 0); setZ(vid.mode == 1);
        return true; }
    case 0xBC32: setInk(c.a & 0x1F, c.b & 0x1F, c.c & 0x1F); return true;   // SCR SET INK
    case 0xBC35: {                                            // SCR GET INK
        int p = c.a & 0x1F; if (p > 16) p = 16;
        c.b = fwInk[p][0]; c.c = fwInk[p][1];
        return true; }
    case 0xBC38: setInk(16, c.b & 0x1F, c.c & 0x1F); return true;           // SCR SET BORDER
    case 0xBC3E: vid.flashPeriod[0] = c.h ? c.h : 256; vid.flashPeriod[1] = c.l ? c.l : 256; return true;
    case 0xBD25: mcSetInks(c.de()); return true;     // MC SET INKS: border + 16 pens
    case 0xBCCE: setC(true); return true;            // KL INIT BACK
    case 0xBCD4: {                                            // KL FIND COMMAND (AMSDOS hidden BIOS commands)
        uint8_t ch = ram[c.hl()] & 0x7F;
        if (ch >= 1 && ch <= 9) { c.setHL(FAR_BASE + ch); c.c = 7; setC(true); }
        else setC(false);
        return true; }
    case 0xBCD7: {                                            // KL NEW FRAME FLY
        events[c.hl()] = {c.hl(), c.de(), c.b};
        flyEvents.push_back(c.hl());
        return true; }
    case 0xBCEF:                                              // KL INIT EVENT
        events[c.hl()] = {c.hl(), c.de(), c.b};
        return true;
    case 0xBCE9:                                              // KL ADD TICKER
        tickers.push_back({c.hl(), (int)c.de(), (int)c.bc()});
        return true;
    case 0xBCEC:                                              // KL DEL TICKER
        for (size_t i = 0; i < tickers.size(); i++)
            if (tickers[i].block == c.hl()) { c.setDE(tickers[i].count); tickers.erase(tickers.begin() + i); setC(true); return true; }
        setC(false); return true;
    case 0xBCF5: return true;                        // KL SYNC RESET
    // ---- sound manager ----
    case 0xBCA7: snd.reset(); return true;                    // SOUND RESET
    case 0xBCAA: {                                                     // SOUND QUEUE
        uint8_t p[9];
        for (int i = 0; i < 9; i++) p[i] = ram[(c.hl() + i) & 0xFFFF];
        setC(snd.queue(p)); return true; }
    case 0xBCAD: c.a = snd.check(c.a); return true;           // SOUND CHECK
    case 0xBCB3: snd.release(c.a); return true;               // SOUND RELEASE
    case 0xBCB6: snd.hold(); return true;                     // SOUND HOLD
    case 0xBCB9: snd.cont(); return true;                     // SOUND CONTINUE
    case 0xBCB0: return true;                                 // SOUND ARM EVENT
    case 0xBCBC: case 0xBCBF: {                                        // SOUND AMPL / TONE ENVELOPE
        uint8_t d[16];
        for (int i = 0; i < 16; i++) d[i] = ram[(c.hl() + i) & 0xFFFF];
        bool ok = pc == 0xBCBC ? snd.setAmpl(c.a, d) : snd.setTone(c.a, d);
        if (ok) c.setHL(c.hl() + 16);
        setC(ok); return true; }
    // ---- cassette / disc file access ----
    case 0xBC77: {                                            // CAS IN OPEN
        std::string nm;
        for (int i = 0; i < c.b; i++) nm += (char)ram[(c.hl() + i) & 0xFFFF];
        casFile = res.file(nm);
        if (!casFile) { setC(false); return true; }
        c.setBC((uint16_t)casFile->size());
        c.setDE(0x1800);
        c.a = 2;
        setC(true); setZ(false);
        return true; }
    case 0xBC83: {                                            // CAS IN DIRECT
        if (!casFile) { setC(false); return true; }
        uint16_t dst = c.hl();
        for (size_t i = 0; i < casFile->size(); i++) ram[(dst + i) & 0xFFFF] = (*casFile)[i];
        c.setHL(0);
        setC(true);
        return true; }
    case 0xBC7A: casFile = nullptr; setC(true); return true;   // CAS IN CLOSE
    case 0xBD16: return true;                        // MC START PROGRAM (loader only)
    default: return false;
    }
}

// AMSDOS disc BIOS (hidden RSX commands 0x81..0x89)
void Machine::farCall(int idx) {
    auto& c = cpu;
    auto ok = [&](bool v) { c.f = v ? (c.f | 1) : (c.f & ~1); if (v) c.a = 0; };
    switch (idx) {
    case 1: case 2: case 3: case 9: ok(true); break;        // set message / setup disc / select format / retry count
    case 4: {                                                // READ SECTOR: HL=buf D=track E=drive C=sector id
        uint8_t buf[512];
        if (res.readSector(c.d, c.c, buf)) { for (int i = 0; i < 512; i++) ram[(c.hl() + i) & 0xFFFF] = buf[i]; ok(true); }
        else ok(false);
        break; }
    case 5: {                                                // WRITE SECTOR
        uint8_t buf[512];
        for (int i = 0; i < 512; i++) buf[i] = ram[(c.hl() + i) & 0xFFFF];
        ok(res.writeSector(c.d, c.c, buf));
        break; }
    case 7: ok(true); break;                                 // MOVE TRACK
    case 6: case 8: ok(true); break;
    default: die("unknown AMSDOS BIOS call"); break;
    }
}


void Machine::rst18(uint16_t farEntry) {
    uint16_t dest = rw(farEntry);
    if (dest >= FAR_BASE && dest < FAR_BASE + 16) farCall(dest - FAR_BASE);
    else die("far call to unknown address");
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

