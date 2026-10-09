// Hand-written replacements of the Z80 text printer, room loader, tile blitter and UI helpers.
// Everything here is verified against the original with SORCERY_VERIFY=1 (see docs/port_text.md).
#include "gamemem.h"
#include "native.h"
#include "port_text.h"
#include <cstring>

using namespace mem;

namespace {

constexpr uint16_t IY = ROOMREC;                 // IY base of the game (flags/room record)
constexpr int MAP_CELLS = 40 * 18;               // tile map: 40 x 18 tiles
constexpr int GLYPH_BYTES = 16;                  // font glyph: 8 rows x 2 bytes
constexpr uint8_t FIRST_GLYPH = 0x20;            // codes below this are control codes
constexpr uint8_t CTL_SET_CURSOR = 0x1F;         // followed by 2 bytes: new cursor (low, high)
constexpr uint8_t CTL_SET_MASK = 0x1E;           // followed by 1 byte: new text pen mask
constexpr uint16_t SCREEN_MASK = 0x3FFF;         // screen offsets are kept without bits 14/15, hardware address = offset | C000
constexpr uint16_t FATAL_ERROR = 0x8DFB;         // original "rst 8" error stop

uint16_t rw(Machine& m, uint16_t a) { return m.ram[a] | m.ram[uint16_t(a + 1)] << 8; }
void ww(Machine& m, uint16_t a, uint16_t v) { m.ram[a] = uint8_t(v); m.ram[uint16_t(a + 1)] = v >> 8; }

// Firmware services live in machine.cpp; call one like `call addr` would.
void firmware(Machine& m, uint16_t addr) { m.call(addr); }

// The game must stop when the disc/room data is bad: run the original error stop (rst 8).
void fatalError(Machine& m) { m.call(FATAL_ERROR); }

// ------------------------------------------------------------------------------------------------
// Text printer.  Strings live in game memory; the last character of a string has bit 7 set.
// State (same registers as the original): hl = read pointer, de = cursor (screen offset, 2 bytes per glyph column,
// 80 bytes per character row), a/b mimic the leftovers of the original in case a caller looks at them.
// ------------------------------------------------------------------------------------------------
struct TextPrinter {
    Machine& m;
    uint16_t hl, de;
    uint8_t a, b;

    explicit TextPrinter(Machine& mach) : m(mach), hl(mach.cpu.hl()), de(mach.cpu.de()), a(mach.cpu.a), b(mach.cpu.b) {}
    void store() {
        m.cpu.setHL(hl);
        m.cpu.setDE(de);
        m.cpu.a = a;
        m.cpu.b = b;
    }

    // 8D1F: draw one 4x8-pixel glyph (2 screen bytes wide, 8 lines) at the cursor and advance it by 2 bytes.
    // Font bytes are ANDed with the pen mask at TEXT_MASK, so the mask selects the colour of the text.
    void drawGlyph(uint8_t ch) {
        const uint8_t mask = m.ram[TEXT_MASK];
        const uint16_t glyph = FONT + (ch - FIRST_GLYPH) * GLYPH_BYTES;
        uint8_t hi = (de >> 8) | 0xC0;            // screen RAM is at C000
        const uint8_t lo = de & 0xFF;
        // One glyph row per pixel line; the original stops when the address high byte overflows (so a cursor that
        // does not start on line 0 of a character cell gets a shorter glyph).
        for (int row = 0, carry = 0; !carry; row++) {
            const uint16_t dst = hi << 8 | lo;
            m.ram[dst] = m.ram[glyph + row * 2] & mask;
            m.ram[uint16_t(dst + 1)] = m.ram[glyph + row * 2 + 1] & mask;
            carry = hi + 8 > 0xFF;
            hi += 8;                              // next pixel line of the character cell (+0x800)
        }
        de = (de + 2) & SCREEN_MASK;
    }

    // 8CFC: print one character or execute a control code (the stream continues at hl).
    void printChar(uint8_t ch) {
        if (ch >= FIRST_GLYPH) { drawGlyph(ch); return; }       // (a is preserved by the original)
        if (ch == CTL_SET_CURSOR) {
            uint8_t e = m.ram[hl++];
            uint8_t d = m.ram[hl++];
            de = d << 8 | e;
        } else if (ch == CTL_SET_MASK) {
            a = m.ram[hl++];
            m.ram[TEXT_MASK] = a;
        } else {
            // Codes 0x00..0x1D print a phrase from the phrase table (e.g. shared item names).
            const uint16_t saved = hl;
            a = ch + 1;
            b = a;
            hl = TEXT_PHRASES;
            printSelectedString();
            hl = saved;
        }
    }

    // 8CE4: print the string at hl up to and including the character with bit 7 set.
    void printString() {
        for (;;) {
            const uint8_t ch = m.ram[hl++];
            a = ch & 0x7F;
            if (ch & 0x80) { printChar(a); return; }
            a = ch;
            printChar(ch);
        }
    }

    // 8CDC: hl points to a table of strings; skip b strings, then print the next one.
    void printSelectedString() {
        do {
            while (!(m.ram[hl++] & 0x80)) {}
        } while (--b);
        printString();
    }
};

// 8CBD: clear the text rectangle from cursor `start` up to offset `end` (inclusive) on all 8 pixel lines of the character row.
// Leaves the registers like the original does.
void clearRect(Machine& m, uint16_t end, uint16_t start) {
    Z80& c = m.cpu;
    const uint16_t diff = end - start;
    c.setHL(diff);
    if (end < start || diff == 0) return;             // nothing (or nonsense) to clear
    c.setBC(diff);
    uint16_t line = start | 0xC000;
    uint16_t last = line;
    uint8_t hi = line >> 8;
    for (;;) {
        last = hi << 8 | (line & 0xFF);
        for (uint32_t i = 0; i <= diff; i++) m.ram[uint16_t(last + i)] = 0;
        hi += 8;
        c.a = hi;
        if (hi < 8) break;                              // carry: 8 lines done
    }
    c.setDE(uint16_t(last + 1 + diff));
    c.setHL(hi << 8 | (line & 0xFF));
}

}  // namespace

// ---------------------------------------------------------------- text printer entry points
// 8D1F: a = glyph code (>= 0x20), de = cursor. Preserves every register except de.
NATIVE(0x8D1F, drawGlyph, "a f de", 880) {
    TextPrinter t(m);
    t.drawGlyph(m.cpu.a);
    m.cpu.setDE(t.de);
}

// 8CFC: a = character or control code; hl = stream pointer for codes that take arguments.
NATIVE(0x8CFC, printChar, "a bc de hl", 1000) {
    TextPrinter t(m);
    t.printChar(m.cpu.a);
    t.store();
}

// 8CE4: print the string at hl (last char has bit 7) at cursor de.
NATIVE(0x8CE4, printString, "a bc de hl", 9300) {
    TextPrinter t(m);
    t.printString();
    t.store();
}

// 8CDC: hl = string table, b = index of the string to print (strings are 0-based; b strings are skipped).
NATIVE(0x8CDC, printTableString, "a bc de hl", 19000) {
    TextPrinter t(m);
    t.printSelectedString();
    t.store();
}

// 8CF6 (inline string call: `call 8CF6` followed by the characters of the string, last one with bit 7 set) is deliberately
// not a NATIVE: the original is just `ex (sp),hl / call 8CE4 / ex (sp),hl / ret`, i.e. it prints the string that starts at
// the return address with printString above and moves the return address behind the string.  It cannot be verified
// because the verifier runs the original with a sentinel as return address (the "string" would be read from there).

// 8CBD: clear rectangle, hl = end offset, de = start offset (see clearRect).
NATIVE(0x8CBD, clearTextRect, "a bc de hl", 5000) { clearRect(m, m.cpu.hl(), m.cpu.de()); }

// 8C77: top message line (screen offset 0640..067F): string number (ROOMREC+0)+1 of table 9CB7, rest of the line cleared.
NATIVE(0x8C77, printMessageLine1, "a bc de hl", 34000) {
    TextPrinter t(m);
    t.b = uint8_t(m.ram[IY] + 1);
    t.hl = MSG_LINE1_TABLE;
    t.de = 0x0640;
    m.ram[TEXT_MASK] = 0xFF;
    t.a = 0xFF;
    t.printSelectedString();
    t.store();
    clearRect(m, 0x067F, t.de);
}

// 8C8E: second message line (0x06E0..0x071F).  Looks ROOMREC+70 up in the 18-entry key list at 9CA4; if it is there the
// matching message is printed (in the table chosen by bit 7 of ROOMREC+76) and the rest of the line cleared.
NATIVE(0x8C8E, printMessageLine2, "hl", 24000) {
    Z80& c = m.cpu;
    const uint8_t key = m.ram[IY + 70];
    m.ram[MSG_KEYS + 18] = key;                       // sentinel: the search always ends
    int idx = 0;
    while (m.ram[MSG_KEYS + idx] != key) idx++;
    if (idx == 18) {                                  // only the sentinel matched: no message for this value
        c.setHL(MSG_KEYS + 19);
        c.setBC(0);
        return;
    }
    c.setBC(0x13 - (idx + 1));
    TextPrinter t(m);
    t.hl = (m.ram[IY + 76] & 0x80) ? MSG_TABLE_B : MSG_TABLE_A;
    t.de = 0x06E0;
    t.b = uint8_t(idx + 1);
    m.ram[TEXT_MASK] = 0xFF;
    t.a = 0xFF;
    t.printSelectedString();
    t.store();
    clearRect(m, 0x071F, t.de);
}

// ---------------------------------------------------------------- BCD number printing
namespace {
// 91E8: print the packed-BCD byte at hl as two digits and advance hl.
void printBcdByte(TextPrinter& t) {
    const uint8_t v = t.m.ram[t.hl];
    t.printChar('0' + (v >> 4));
    t.printChar('0' + (v & 0x0F));
    t.hl++;
    t.a = '0';
}
}  // namespace

// 91E8: hl = packed BCD byte, de = cursor.
NATIVE(0x91E8, printBcd, "a de hl", 1900) {
    TextPrinter t(m);
    printBcdByte(t);
    t.store();
}

// 91DD: print the energy (one BCD byte at 684D) at 0798 in the "high nibble" pen colour (mask F0).
NATIVE(0x91DD, printEnergy, "a de hl", 1950) {
    TextPrinter t(m);
    t.de = 0x0798;
    t.hl = ENERGY_BCD;
    m.ram[TEXT_MASK] = 0xF0;
    printBcdByte(t);
    t.store();
}

// 91CA: print the 6-digit score (3 BCD bytes at 92D4) at 07B2 with pen mask 0F.
NATIVE(0x91CA, printScore, "a de hl", 5800) {
    TextPrinter t(m);
    t.hl = SCORE_BCD;
    t.de = 0x07B2;
    m.ram[TEXT_MASK] = 0x0F;
    for (int i = 0; i < 3; i++) printBcdByte(t);
    t.store();
}

namespace {
// One packed-BCD byte addition with the Z80 `add/adc` + `daa` semantics.
uint8_t bcdAddByte(uint8_t x, uint8_t y, unsigned& carry) {
    const unsigned sum = x + y + carry;
    const bool halfCarry = ((x & 0x0F) + (y & 0x0F) + carry) > 0x0F;
    uint8_t r = sum & 0xFF;
    bool cy = sum > 0xFF;
    unsigned adjust = 0;
    if (halfCarry || (r & 0x0F) > 9) adjust |= 0x06;
    if (cy || r > 0x99) { adjust |= 0x60; cy = true; }
    carry = cy;
    return uint8_t(r + adjust);
}
}  // namespace

// 91B2: add the packed BCD value bc (4 digits) to the score unless the "no score" flag (ROOMREC+87 bit 7) is set,
// then fall into printScore.
NATIVE(0x91B2, addScore, "a de hl", 5900) {
    Z80& c = m.cpu;
    if (!(m.ram[IY + 87] & 0x80)) {
        unsigned carry = 0;
        m.ram[SCORE_BCD + 2] = bcdAddByte(m.ram[SCORE_BCD + 2], c.c, carry);
        m.ram[SCORE_BCD + 1] = bcdAddByte(m.ram[SCORE_BCD + 1], c.b, carry);
        m.ram[SCORE_BCD + 0] = bcdAddByte(m.ram[SCORE_BCD + 0], 0, carry);
    }
    TextPrinter t(m);
    t.hl = SCORE_BCD;
    t.de = 0x07B2;
    m.ram[TEXT_MASK] = 0x0F;
    for (int i = 0; i < 3; i++) printBcdByte(t);
    t.store();
}

// ---------------------------------------------------------------- tiles and the screen
namespace {
// 8EBC: copy tile `id` (planar: byte j at TILESET + j*256 + id) to 2 bytes x 8 lines of screen at `dst`.
void blitTile(Machine& m, uint8_t id, uint16_t dst) {
    uint16_t src = TILESET + id;
    for (int row = 0; row < 8; row++) {
        m.ram[dst] = m.ram[src];
        m.ram[uint16_t(dst + 1)] = m.ram[src + 0x100];
        src += 0x200;
        dst += 0x800;                         // next pixel line
    }
}
}  // namespace

// 8EBC: hl = address of a tile id in the tile map, de = screen address (bits 14/15 set).  Preserves bc/de/hl.
NATIVE(0x8EBC, drawTile, "bc de hl", 680) { blitTile(m, m.ram[m.cpu.hl()], m.cpu.de()); }

// 8EA6: draw the whole 40x18 tile map to the screen (2 bytes per tile, 80 bytes per row, 8 pixel lines per tile row).
NATIVE(0x8EA6, drawTileMap, "bc de hl", 529000) {
    for (int i = 0; i < MAP_CELLS; i++) blitTile(m, m.ram[TILEMAP + i], SCREEN + 2 * i);
    m.cpu.setHL(TILEMAP + MAP_CELLS);
    m.cpu.setDE(SCREEN + 2 * MAP_CELLS);
    m.cpu.setBC(0);
}

// 8E8C: clear the screen in a "dissolve" pattern: bytes are visited in steps of 0x22D through the 16 KB screen (the step
// is odd, so every byte is hit once), with a delay loop after each one (~20 frames in the original, here instant).
NATIVE(0x8E8C, dissolveClearScreen, "a bc de hl", 5420000) {
    uint16_t p = SCREEN;
    do {
        m.ram[p] = 0;
        p = uint16_t(p + 0x022D) | 0xC000;
    } while (p != SCREEN);
    m.cpu.setHL(SCREEN);
    m.cpu.setDE(0x022D);
    m.cpu.setBC(0x00C0);
    m.cpu.a = 0;
}

// 8F3C: a = border ink, hl = 16 palette entries (hardware inks, no flashing).  Sets pens 0-15 and the border.
NATIVE(0x8F3C, setPalette, "a bc hl", 1600) {
    Z80& c = m.cpu;
    const uint8_t border = c.a;
    uint16_t p = c.hl();
    for (int pen = 0; pen < 16; pen++) {
        c.a = pen;
        c.b = c.c = m.ram[p++];
        firmware(m, 0xBC32);                      // SCR SET INK
    }
    c.setHL(p);
    c.a = 0;
    c.b = c.c = border;
    firmware(m, 0xBC38);                          // SCR SET BORDER
}

// 8ED6: convert the freshly loaded tile-id map (6C90) into a collision map in place.  Tile 0 / ids above 0xDC stay 0
// (empty), tiles 1E/1F, 46/47 and 6E/6F are animated hazards stored as pairs 81/81, 01/01, 02/02 and everything else is
// solid (0x80).  Room 3D has a one-off rewrite of tile F7 depending on 9667.
NATIVE(0x8ED6, classifyTiles, "a bc hl", 67500) {
    Z80& c = m.cpu;
    if (m.ram[ROOM_NUMBER] == 0x3D && m.ram[SPECIAL_ROOM_FLAG] == 0) {
        if (m.ram[IY + 70] == 0x2B) {
            m.ram[SPECIAL_ROOM_FLAG] = 1;
        } else {
            for (int i = 0; i < MAP_CELLS; i++)
                if (m.ram[TILEMAP + i] == 0xF7) m.ram[TILEMAP + i] = 0x01;
        }
    }
    constexpr uint8_t SOLID = 0x80;
    uint16_t hl = TILEMAP;
    uint16_t bc = MAP_CELLS;
    do {
        const uint8_t tile = m.ram[hl];
        m.ram[hl] = 0;
        if (uint8_t(tile - 1) < 0xDC) {               // tile 1..0xDC
            uint8_t pair = 0;
            if (tile == 0x1E || tile == 0x1F) pair = 0x81;
            else if (tile == 0x46 || tile == 0x47) pair = 0x01;
            else if (tile == 0x6E || tile == 0x6F) pair = 0x02;
            if (pair) {                               // the two halves of an animated tile take two map cells
                m.ram[hl++] = pair;
                bc--;
                m.ram[hl] = pair;
            } else {
                m.ram[hl] = SOLID;
            }
        }
        hl++;
        bc--;
    } while (bc != 0);
    c.setHL(hl);
    c.setBC(bc);
    c.a = 0;
}

// 901E: pre-compute the XOR-delta frames of the three animated tile groups into 6880..69BF.  Each group is a cycle of
// two-tile frames (1E-25: 4 frames, 46-4D: 4 frames, 6E-71: 2 frames); the delta from frame k to frame k+1 (wrapping to
// frame 0) is stored, 16 bytes per tile.  The animation driver (905B) XORs one delta onto the screen per step.
NATIVE(0x901E, buildAnimFrames, "bc de hl", 24700) {
    struct Group { uint8_t first; int frames; };
    static const Group groups[3] = {{0x1E, 4}, {0x46, 4}, {0x6E, 2}};
    uint16_t dst = ANIM_FRAMES;
    uint8_t cur = 0, next = 0;
    for (const Group& g : groups) {
        for (int k = 0; k < g.frames; k++) {
            cur = g.first + 2 * k;
            next = (k == g.frames - 1) ? g.first : cur + 2;
            for (int t = 0; t < 2; t++)
                for (int plane = 0; plane < 16; plane++)
                    m.ram[dst++] = m.ram[TILESET + plane * 256 + next + t] ^ m.ram[TILESET + plane * 256 + cur + t];
        }
    }
    m.cpu.setBC(dst);
    m.cpu.setDE(TILESET + 0x6E + 2);     // the original leaves de/hl just behind the last pair
    m.cpu.setHL(TILESET + 0x70 + 2);
}

// ---------------------------------------------------------------- disc access
namespace {
// 92A5: wait for the next frame flyback (the original disables interrupts and calls the firmware MC WAIT FLYBACK).
// Used after every disc access.
void waitForFlyback(Machine& m) {
    m.waitIrq();                          // the flyback wait: let the next interrupt (and the flyback event) happen
    m.ram[FW_FLAG] = 0;                   // the interrupt phase counter restarts
}
}  // namespace

NATIVE(0x92A5, waitFlyback, "a hl", 220) { waitForFlyback(m); m.cpu.a = 0; }

// 8DE0: load the room whose number is at 9310 from its sector (track 1F + n/9, sector id C1 + n%9, 512 bytes -> 5600) and
// RLE-decode it into the tile map at 6C90.  Sector layout: "DC ", room number, then the RLE stream:
//   FF = end, 00 n = n zero tiles (n=0 means 256), any other byte = one literal tile id.
NATIVE(0x8DE0, loadRoom, "a de hl", 31500) {
    Z80& c = m.cpu;
    constexpr uint16_t SECTOR_BUF = 0x5600;
    constexpr int ROOM_COUNT = 0x4F;
    const uint8_t room = m.ram[ROOM_NUMBER];
    if (room >= ROOM_COUNT) { fatalError(m); return; }
    const int track = 0x1F + room / 9;
    const int sectorId = 0xC1 + room % 9;
    uint8_t buf[512];
    if (!m.res.readSector(track, sectorId, buf)) { fatalError(m); return; }
    memcpy(&m.ram[SECTOR_BUF], buf, sizeof buf);
    waitForFlyback(m);
    const uint8_t* p = &m.ram[SECTOR_BUF];
    if (p[0] != 'D' || p[1] != 'C' || p[2] != ' ' || p[3] != room) { fatalError(m); return; }
    uint16_t src = SECTOR_BUF + 4;
    uint16_t dst = TILEMAP;
    for (;;) {
        const uint8_t v = m.ram[src];
        if (v == 0xFF) break;
        if (v == 0) {
            src++;
            int n = m.ram[src];
            if (n == 0) n = 256;
            while (n--) m.ram[dst++] = 0;
        } else {
            m.ram[dst++] = v;
        }
        src++;
    }
    c.setHL(dst);
    c.setDE(src);
    c.a = 0;
}

// 91F8: load the files listed at ix (5-byte entries: name length, name pointer, destination; length 0 ends) from the
// resource folder (the original unscrambled them in place with  plain = ror1(-b) - 0x25;  the resources are already plain).
NATIVE(0x91F8, loadFiles, "a bc de hl ix", 545000) {
    Z80& c = m.cpu;
    uint16_t ix = c.ix;
    uint16_t hl = c.hl();
    uint16_t bc = c.bc();
    while (m.ram[ix] != 0) {
        const int nameLen = m.ram[ix];
        const uint16_t namePtr = rw(m, ix + 1);
        const uint16_t dest = rw(m, ix + 3);
        std::string name;
        for (int i = 0; i < nameLen; i++) name += char(m.ram[uint16_t(namePtr + i)]);
        const std::vector<uint8_t>* f = m.res.file(name);
        if (!f) { fatalError(m); return; }
        const uint16_t len = (uint16_t)f->size();
        for (int i = 0; i < len; i++) m.ram[uint16_t(dest + i)] = (*f)[i];
        ww(m, FILE_LEN, len);
        hl = dest + len;
        bc = 0;
        ix += 5;
    }
    c.ix = ix;
    c.setHL(hl);
    c.setBC(bc);
    c.setDE(0x2400);
    c.a = 0;
    c.f |= 1;
}

// ---------------------------------------------------------------- energy bar fade (interrupt phase handler) and sound queue
namespace {
// 926B: fade one step of the energy bar: AND the pen mask into the byte at BAR_PTR (4 successive masks 77,BB,DD,EE clear one
// byte pixel plane by plane); when a byte is finished advance to the next; at the end of the bar flag the player's death.
void fadeBarStep(Machine& m) {
    uint16_t p = rw(m, BAR_PTR) | 0xC000;
    uint8_t mask = m.ram[BAR_MASK];
    m.ram[p] &= mask;
    const bool carry = mask & 1;
    m.ram[BAR_MASK] = uint8_t(mask >> 1 | mask << 7);
    if (carry) return;                                  // more mask steps for this byte
    p++;
    uint16_t bc = rw(m, BAR_COUNT);
    uint8_t cnt = bc & 0xFF, rows = bc >> 8;
    if (--cnt == 0) {                                   // end of the 16-byte line: go to the next pixel line
        cnt = 0x10;
        uint32_t sum = uint32_t(p) + 0x07F0;
        p = uint16_t(sum);
        if (sum > 0xFFFF) p = uint16_t(p + 0xC050);     // wrapped past the screen: next character row
        if (--rows == 0) m.ram[IY + 79] |= 0x84;        // bar empty: set the "out of energy" flags
    }
    ww(m, BAR_PTR, p);
    ww(m, BAR_COUNT, rows << 8 | cnt);
}
}  // namespace

// 926B: see fadeBarStep.
NATIVE(0x926B, fadeBarStepEntry, "", 600) { fadeBarStep(m); }

// 9257: called from the interrupt phase table; every 24th call (unless ROOMREC+0x50 bit 7 is set) fade the energy bar.
NATIVE(0x9257, fadeBar, "", 72) {
    if (m.ram[ROOMREC + 0x50] & 0x80) return;
    if (m.ram[BAR_COUNTDOWN]-- != 0) return;
    m.ram[BAR_COUNTDOWN] = 0x17;
    fadeBarStep(m);
}

// 92BA: queue the 9-byte firmware sound command at hl (SOUND QUEUE); all registers and flags are preserved.
NATIVE(0x92BA, queueSound, "a f bc de hl ix", 120) {
    const uint8_t savedF = m.cpu.f;
    firmware(m, 0xBCAA);
    m.cpu.f = savedF;
}
