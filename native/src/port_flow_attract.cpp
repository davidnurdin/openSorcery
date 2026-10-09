// Boot, attract mode (hall of fame pages, title picture with scroller, demo game), start of a game.
//
// Screen flow of the original (addresses of the Z80 routines in brackets):
//
//   boot [7000]: find the AMSDOS BIOS commands, read the all-time hall of fame from the disc, install the interrupt
//        handler (IM 2, vector table at 5400, handler 8D55), wait about a second, remember whether a key was held.
//   attract [70C4]: load the attract resources, then loop forever over
//        page 1  "LES PLUS GRANDS SORCIERS DE NOS JOURS" = today's table              (about 4 s, fire starts the game)
//        page 2  "LE GRAND SORCIER DES TEMPS"            = all-time table              (about 4 s)
//        page 3  title picture is revealed [8E36], one message of the title scroller runs along the bottom [7218]
//        every 4th round: a full screen picture dissolves in [7174..71AD]
//        then a demo game (recorded joystick input) is played [71B2]; when it ends the loop starts again
//   fire at any time -> start of a real game [72A9], which hands over to the room loop at 7457.
//
// Everything here runs as a task (see port_flow_task.cpp): waitTick()/timedLoop() are the places where the original spins.
#include "port_flow.h"
#include <algorithm>
#include <cstring>

namespace flow {
using namespace ram;

namespace {


constexpr uint16_t PRINT_AT_SCREEN_BOTTOM = 0x0690;
constexpr uint8_t KEY_HELD_AT_BOOT = 0x3C;      // CPC key number: loop the attract pages, no music/demo
constexpr uint8_t KEY_INITIATION = 0x3E;        // CPC key number: select initiation (practice) mode

bool keyPressed(uint8_t cpcKey) {               // KM TEST KEY
    mach().cpu.a = cpcKey;
    call(0xBB1E);
    return !(mach().cpu.f & 0x40);
}

// ---------------------------------------------------------------------------------------------------------------------
// 7218: the title scroller.  The message list at 0A00 holds word offsets (0xFFFF = wrap) to zero terminated texts; the
// pointer to the next message is kept in SCROLL_LIST.  Every character is turned into an 8 line bit pattern and shifted in
// from the right, two pixel columns per step, scrolling the line at screen row 21 left by one byte per tick (4 ticks per
// character).  Returns true if fire was pressed.
// ---------------------------------------------------------------------------------------------------------------------
constexpr uint16_t SCROLL_LINE = 0xC690;         // first byte of the 80 byte line (scanline 0 of the text row)
constexpr uint16_t SCROLL_LINE_STRIDE = 0x0800;  // next scanline of the same character row

void buildGlyphBits(uint8_t ch) {
    // Font glyph = 2 bytes per row; the scroller uses the high nibble of the first and the low nibble of the second byte.
    uint16_t glyph = (uint16_t)(ch * 16 - 0x100);
    for (int row = 0; row < 8; ++row)
        at(SCROLL_GLYPH + row) = (at(glyph + 2 * row) & 0xF0) | (at(glyph + 2 * row + 1) & 0x0F);
}

void scrollOneCharacter() {
    for (int step = 0; step < 4; ++step) {
        waitTick();
        for (int row = 0; row < 8; ++row) {
            uint16_t line = SCROLL_LINE + row * SCROLL_LINE_STRIDE;
            memmove(&mach().ram[line], &mach().ram[line + 1], 0x4F);     // scroll left by one byte
            uint8_t& bits = at(SCROLL_GLYPH + row);
            at(line + 0x4F) = bits & 0xC0;                               // feed in the next two pixel columns
            bits = (uint8_t)((bits << 2) | (bits >> 7));                 // two `rl`: shifts left by 2, bit 0 gets old bit 7
        }
        setWord(SCROLL_GLYPH_PTR, SCROLL_GLYPH + 8);
        call(0x905B);                                                    // animate the title picture
    }
}

bool scrollTitleMessage() {
    for (;;) {
        uint16_t offset = word(word(SCROLL_LIST));
        setWord(SCROLL_LIST, word(SCROLL_LIST) + 2);
        setWord(SCROLL_TEXT, 0x0A00 + offset);
        if (offset != 0xFFFF) break;
        setWord(SCROLL_LIST, 0x0A00);                                    // end of list: start again
    }
    for (;;) {
        at(SEED) += 0x4B;
        if (fireHeld()) return true;
        uint16_t p = word(SCROLL_TEXT);
        uint8_t ch = at(p);
        setWord(SCROLL_TEXT, p + 1);
        if (ch == 0) return false;                                       // end of message
        buildGlyphBits(ch);
        scrollOneCharacter();
    }
}

// 7174..71B1: every fourth round a picture dissolves in byte by byte in a scrambled order (step 0x22D through the 16K
// of screen memory).  Returns true if fire was pressed.
bool showPictureDissolve() {
    Machine& m = mach();
    m.cpu.ix = FILE_LIST_PICTURE;
    call(0x91F8);
    call(0x8E8C);                                                        // clear the screen
    setMode(1);
    setPalette(PAL_PICTURE, 0);
    constexpr uint32_t BYTES = 0x4000;
    uint16_t pos = 0xC000;
    uint32_t done = timedLoop(BYTES, 363.0, true, [&](uint32_t) {
        m.ram[pos] = m.ram[(uint16_t)(pos - 0xB600)];                    // the picture was loaded to 0A00
        pos = (uint16_t)((pos + 0x022D) | 0xC000);
    });
    return done < BYTES;
}

// 7324..7345: sound envelopes (16 volume envelopes from A610, 16 tone envelopes from A710) for the game
void defineSoundEnvelopes() {
    Machine& m = mach();
    for (uint16_t firmwareCall : {0xBCBC, 0xBCBF}) {
        uint16_t table = firmwareCall == 0xBCBC ? 0xA610 : 0xA710;
        for (;;) {
            m.cpu.setHL(table);
            m.cpu.a = (uint8_t)((table & 0xFF) >> 4 | (table & 0xFF) << 4);   // envelope number = high nibble swapped
            call(firmwareCall);
            if (!(m.cpu.f & 1)) break;                                   // firmware refused: stop
            table = m.cpu.hl();
            if ((table & 0xFF) == 0) break;
        }
    }
}

// 73C... the player starts in one of five places; two spawn points in the start room are exchanged as well
void chooseRandomStart() {
    int idx = at(SEED) & 7;
    if (idx >= 5) idx -= 5;
    at(SEED) = (uint8_t)idx;
    const uint16_t place = 0x9668 + 3 * idx;
    at(ROOM) = at(place);
    at(IY + 65) = at(place + 1);
    at(IY + 66) = at(place + 2);
    // two room numbers per start place whose spawn data is modified
    for (int k = 0; k < 2; ++k) {
        uint8_t room = at(0x9677 + 2 * idx + k);
        if (room == 0xFF) continue;
        uint16_t p = 0x4000 + 0x26 + room * 64;                          // spawn table inside the room record
        std::swap(at(p), at(p + 3));
        std::swap(at(p + 1), at(p + 4));
        at(p + 5) = 2;
        at(p + 2) = 0;
    }
}

// 73D1: "initiation mode" (practice): fixed start room, and four bytes of the room code are patched (0xC6 = ADD A,n)
void initiationStart() {
    at(ROOM) = 0x3A;
    at(IY + 65) = 0x10;
    at(IY + 66) = 0x58;
    at(IY + 76) = 0xFF;
    for (uint16_t a : {0x4EA5, 0x4C99, 0x4DDD, 0x4D25}) at(a) = 0xC6;
}

// 73F2: the demo game plays a recorded script: room, x, y, then {duration, joystick} pairs read by the player code
void demoStart() {
    uint16_t script = (at(DEMO_ALT) & 0x80) ? 0x9A0F : 0x99B4;
    at(ROOM) = at(script);
    at(IY + 65) = at(script + 1);
    at(IY + 66) = at(script + 2);
    setWord(0x9A42, script + 3);
    at(0x99B3) = 1;
}

}  // namespace

bool playGame(bool demo) {
    Machine& m = mach();
    Z80& c = m.cpu;
    call(0x4F00);                                                        // music off
    call(0xBC11);                                                        // SCR GET MODE: carry = mode 0
    if (c.f & 1) printAt(PRINT_AT_SCREEN_BOTTOM, "                                        ");
    at(CHEAT_ON) = 0;
    if ((at(CHEAT_AVAIL) & 0x80) && keyPressed(KEY_INITIATION)) {
        // The game was won before and the key is held: initiation mode.  The firmware text output is not rendered here.
        at(CHEAT_ON) = 0xFF;
        c.setHL(0x0117);
        call(0xBB75);
        for (char ch : {'T', 'R', 'I', 'C', 'H', 'E', 'Z', '.', '.', '.'}) { c.a = (uint8_t)ch; call(0xBB5A); }
        for (int i = 0; i < 50; ++i) waitTick();
    }

    // load the game resources (the cheat / second demo use the alternative list)
    c.im = 1;
    uint16_t list = FILE_LIST_GAME;
    if (at(DEMO_FLAG) & 0x80) {
        at(DEMO_ALT) ^= 0xFF;
        if (at(DEMO_ALT) != 0) list = FILE_LIST_GAME_ALT;
    } else if (at(CHEAT_ON) & 0x80) {
        list = FILE_LIST_GAME_ALT;
    }
    c.ix = list;
    call(0x91F8);
    defineSoundEnvelopes();
    setTextMask(0xFF);
    call(0x901E);
    memset(&m.ram[0x6800], 0, 0x50);                                     // reset the game flags (iy+0..79)

    if (at(DEMO_FLAG) & 0x80) demoStart();
    else if (at(CHEAT_ON) & 0x80) initiationStart();
    else chooseRandomStart();

    // common initialisation
    at(SAVED_COUNT) = 0;
    at(0x945B) = 0;
    at(0x9667) = 0;
    at(SCORE) = at(SCORE + 1) = at(SCORE + 2) = 0;
    at(IY + 67) = 0x4F;
    at(IY + 68) = 0x14;
    at(IY + 69) = 0x18;
    --at(IY + 70);
    at(IY + 77) = 0x99;
    --at(IY + 78);
    at(0x9357) = 0x18;
    call(0x8E8C);                                                        // clear the screen
    call(0x752D);                                                        // status panel
    setWord(IRQ_TABLE_PTR, IRQ_TABLE_GAME);
    c.im = 2;

    c.a = at(ROOM);
    call(0x75A4);                                                        // enter the first room
    GameEnd end = roomLoop(demo);                                        // 7457: the game itself
    if (end.victory) victory();                                          // victory screens, then game over (not in a demo)
    else if (!demo) gameOver();                                          // final screens, name entry
    return demo && end.fire;                                             // a demo ends back in the attract code
}


// ---------------------------------------------------------------------------------------------------------------------
// 70C4: attract loop
// ---------------------------------------------------------------------------------------------------------------------
Start attractMode() {
    Machine& m = mach();
    m.cpu.im = 1;
    setMode(1);
    m.cpu.ix = FILE_LIST_TITLE;
    call(0x91F8);
    call(0x901E);
    call(0x4F03);                                                        // start the title music
    setWord(IRQ_TABLE_PTR, IRQ_TABLE_MENU);
    m.cpu.im = 2;

    for (;;) {                                                           // 70E0
        setMode(1);
        setPalette(PAL_MENU, 0);
        setTextMask(0xF0);
        printAt(0x00F3, "LES PLUS GRANDS SORCIERS DE NOS JOURS");
        if (showHallTable(HALL_TODAY)) return Start::Game;

        setMode(1);
        setTextMask(0xF0);
        printAt(0x00FE, "LE GRAND SORCIER DES TEMPS");
        if (showHallTable(HALL_ALL_TIME)) return Start::Game;

        call(0x8E36);                                                    // reveal the title picture (waits for interrupts)
        if (scrollTitleMessage()) return Start::Game;
        if (at(LOOP_MODE) & 0x80) continue;                              // key held at boot: only the pages, forever

        call(0x4F00);                                                    // music off
        if ((word(SCROLL_LIST) & 0xFF) == 0x08 && showPictureDissolve()) return Start::Game;

        at(DEMO_FLAG) = 0xFF;                                            // 71B2: play the demo
        return Start::Demo;
    }
}

// ---------------------------------------------------------------------------------------------------------------------
// 7000: program entry: boot, then attract mode and games forever
// ---------------------------------------------------------------------------------------------------------------------
void gameMain(Machine& m) {
    bind(m);
    Z80& c = m.cpu;
    call(0xB909);                                                        // KL L ROM DISABLE
    c.setDE(0xAB00);
    c.setHL(0xB100);
    c.c = 7;
    call(0xBCCE);                                                        // KL INIT BACK

    // Look up the AMSDOS hidden RSX commands (names 0x81..0x89) and store {address, rom} for each: the far-call table
    uint16_t table = FAR_TABLE;
    for (uint16_t name = 0x98E5; at(name) != 0; ++name, table += 3) {
        c.setHL(name);
        call(0xBCD4);                                                    // KL FIND COMMAND
        at(table) = c.l;
        at(table + 1) = c.h;
        at(table + 2) = c.c;
    }
    c.a = 0xFF;
    farCall(FAR_SET_MESSAGE);                                            // no disc error messages
    c.a = 0xC1;
    c.e = 0;
    farCall(FAR_SELECT_FORMAT);                                          // data format: first sector id C1
    c.setHL(DISC_BUF);
    c.d = HALL_TRACK;
    c.e = 0;
    c.c = HALL_SECTOR_ID;
    farCall(FAR_READ_SECTOR);                                            // the all-time hall of fame
    if (!(c.f & 1)) { m.die("disc error: cannot read the hall of fame"); return; }
    memcpy(&m.ram[HALL_ALL_TIME], &m.ram[DISC_BUF], HALL_BYTES);

    c.iy = IY;
    setWord(IRQ_TABLE_PTR, IRQ_TABLE_MENU);
    at(IRQ_PHASE) = 0;
    c.setHL(0x9347);                                                     // frame flyback event: the game's 8D87
    c.b = 0x81;
    c.c = 0;
    c.setDE(0x8D87);
    call(0xBCD7);
    setWord(FW_IRQ_VECTOR, word(0x0039));                                // the firmware's own interrupt routine

    // interrupt mode 2: vector table 5400..5500 filled with 55, so every vector is 5555 = jp 8D55 (the game's handler)
    at(0x5555) = 0xC3;
    setWord(0x5556, 0x8D55);
    memset(&m.ram[0x5400], 0x55, 0x101);
    c.im = 2;
    c.i = 0x54;
    c.iff1 = c.iff2 = true;

    c.setHL(0x0101);
    call(0xBC3E);                                                        // SCR SET FLASHING
    memset(&m.ram[0x6800], 0, 0x80);                                     // game variables
    at(DEMO_ALT) = 0xFF;
    setWord(SCROLL_LIST, 0x0A00);
    for (int i = 0; i < 256; ++i) waitIrq();                             // `halt` x 256: about a second

    if (keyPressed(KEY_HELD_AT_BOOT)) {
        at(LOOP_MODE) = 0xFF;
        // fill the screen with 0xFF: a Z80 LDIR (21 T-states per byte), which takes a few frames
        timedLoop(0x4000, 21.0, false, [&](uint32_t i) { m.ram[mem::SCREEN + i] = 0xFF; });
    }

    for (;;) {
        Start s = attractMode();
        if (s == Start::Demo) {                                          // 71B9: the demo is over
            bool fire = playGame(true);
            at(DEMO_FLAG) = 0;
            if (!fire) continue;                                         // back to the attract pages
            setWord(IRQ_TABLE_PTR, IRQ_TABLE_MENU);                      // fire during the demo starts a real game
            m.cpu.a = 0;
            call(0xBD1C);                                                // MC SET MODE 0
            m.cpu.setDE(0x05A0);
            m.cpu.setHL(0x07FF);
            call(0x8CBD);                                                // clear the lower part of the screen
        }
        playGame(false);                                                 // returns after game over and name entry
    }
}

}  // namespace flow
