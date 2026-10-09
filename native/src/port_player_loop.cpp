// Main-loop helpers of Sorcery+: input decoding (74B8), demo abort test (74FB), HUD set-up (752D),
// room entry (75A4, 767F, 76CC) and the animation of the room-0x1C objects (7BF4).
//
// The main loop itself (7457-74B5) is port_game.cpp (roomLoop).  The pause routine 7509 is in port_player2.cpp.
#include "port_player.h"
#include <cstring>

using namespace player;

namespace {

// Terminates the current life: the original code does `pop hl; jp 8636` (drop the caller's return address and continue
// with the end-of-game flow); here an exception unwinds to the room loop (port_game.cpp).
[[noreturn]] void leaveToGameOver(Machine&) { throw LeaveGame{}; }

// Prints `text` with the glyph routine 8D1F starting at screen address `de`; returns the address after the text.
uint16_t printText(Machine& m, uint16_t de, const char* text) {
    for (const char* p = text; *p; p++) {
        m.cpu.a = (uint8_t)*p;
        m.cpu.setDE(de);
        call(m, 0x8D1F);                     // draws one glyph (mask in 9374), advances DE by 2
        de = m.cpu.de();
    }
    return de;
}

}  // namespace

// ---------------------------------------------------------------------------------------------------------------
// 74B8: decode this tick's input into IY+73.
//   * The player only reacts while in state ST_FLY, otherwise the input is 0.
//   * Live play: the interrupt routine has sampled the joystick/keys into IY+82.
//   * Demo mode: the input comes from a run-length coded script at (9A42): pairs {count, input bits}.
//     99B3 = remaining repeats, 99B2 = bits being replayed.  A pair with count 0 ends the demo.
// ---------------------------------------------------------------------------------------------------------------
NATIVE(0x74B8, readInput, "", 120) {
    iy(m, F_INPUT) = 0;
    if ((iy(m, 0x40) & 0x7F) != ST_FLY) return;
    iy(m, F_INPUT) = iy(m, F_RAW_INPUT);
    if (!(iy(m, F_DEMO) & 0x80)) return;

    uint8_t remaining = at(m, 0x99B3) - 1;
    if (remaining != 0) {                                    // keep repeating the current input
        at(m, 0x99B3) = remaining;
        iy(m, F_INPUT) = at(m, 0x99B2);
        return;
    }
    uint16_t script = rd16(m, 0x9A42);                       // fetch the next {count, bits}
    at(m, 0x99B3) = at(m, script);
    at(m, 0x99B2) = at(m, (uint16_t)(script + 1));
    wr16(m, 0x9A42, script + 2);
    iy(m, F_INPUT) = at(m, 0x99B2);
    if (at(m, 0x99B3) == 0) leaveToGameOver(m);              // end of the recorded demo
}

// 74FB: carry set when the demo must be aborted (fire 2 pressed while a demo is playing).
NATIVE(0x74FB, demoAbortRequested, "a cf", 37) {
    uint8_t demo = iy(m, F_DEMO) & 0x80;
    m.cpu.a = demo;
    setCarry(m, demo && (iy(m, F_RAW_INPUT) & IN_FIRE2));
}

// ---------------------------------------------------------------------------------------------------------------
// 752D: switch to mode 0, set the palette and draw the HUD panel (5 rows of 8 glyphs 0x60.. or 0x88.. in demo mode)
// plus the "ENERGIE" / "SCORE" captions.
// ---------------------------------------------------------------------------------------------------------------
void player::buildHud(Machine& m) {
    m.cpu.a = 0;
    call(m, 0xBC0E);                                         // SCR SET MODE 0 (clears the screen)
    m.cpu.setHL(0x92F8);                                     // 16 pen colours
    m.cpu.a = 1;
    call(m, 0x8F3C);

    at(m, 0x92DC) = 0x77;                                    // parameters of the HUD bar drawn by 91DD/91CA
    wr16(m, 0x92D7, 0xC680);
    wr16(m, 0x92D9, 0x2810);
    at(m, 0x92DB) = 0x12;
    at(m, mem::TEXT_MASK) = 0xFF;

    uint16_t de = 0xC680;
    uint8_t glyph = (iy(m, F_DEMO) & 0x80) ? 0x88 : 0x60;
    for (int row = 0; row < 5; row++) {
        for (int col = 0; col < 8; col++) {
            m.cpu.a = glyph++;
            m.cpu.setDE(de);
            call(m, 0x8D1F);
            de = m.cpu.de();
        }
        de += 0x40;                                          // next text row (80 bytes per row, 16 already used)
    }
    de = printText(m, 0x0780, "ENERGIE.....99% ");
    at(m, mem::TEXT_MASK) = 0x0F;
    printText(m, de, "   SCORE:000000");
}
NATIVE(0x752D, buildHudEntry, "", 1500) { buildHud(m); }

// ---------------------------------------------------------------------------------------------------------------
// 767F: stamp a 3x3 block of tiles (read from `tiles`) onto the tile map at the pixel position {y, x} of the object
// record at `rec`, and draw the object as a sprite with frame `frame`.  y == 0xFF means "no such object" (nothing is
// stamped or drawn).  Returns rec + 3, the position of the next 3-byte object.  The sprite goes through the scratch
// entity at DOOR_SPRITE; only its y, x, state and frame bytes are written.
// ---------------------------------------------------------------------------------------------------------------
static uint16_t stampObject(Machine& m, uint16_t rec, uint8_t frame, uint16_t tiles) {
    uint8_t y = at(m, rec), x = at(m, (uint16_t)(rec + 1));
    if (y != 0xFF) {
        uint16_t dst = tileAddr(y, x);
        for (int row = 0; row < 3; row++)
            for (int col = 0; col < 3; col++)
                at(m, (uint16_t)(dst + row * 40 + col)) = at(m, (uint16_t)(tiles + row * 3 + col));
    }
    Entity sprite{m, DOOR_SPRITE};
    sprite.y() = y;
    sprite.x() = x;
    if (y != 0xFF) {
        sprite.state() = 0;
        sprite.frame() = frame;
        sprite.draw();
    }
    return rec + 3;
}

NATIVE(0x767F, stampObjectEntry, "hl bc de", 600) {
    uint16_t bc = m.cpu.bc(), de = m.cpu.de();
    uint16_t next = stampObject(m, m.cpu.hl(), m.cpu.c, de);
    m.cpu.setBC(bc);                                         // the original preserves BC and DE
    m.cpu.setDE(de);
    m.cpu.setHL(next);
}

// 76CC: the three extra objects of room 0x3D (records at 932A..) use the door tiles too.
static void stampRoom3DExtras(Machine& m) {
    uint16_t rec = 0x932A;
    for (int i = 0; i < 3; i++) rec = stampObject(m, rec, 0x31, 0x9318);
}
NATIVE(0x76CC, stampRoom3DExtrasEntry, "hl bc de", 2000) {
    stampRoom3DExtras(m);
    m.cpu.c = 0x31;
    m.cpu.setDE(0x9318);
    m.cpu.setHL(0x932A + 9);
}

// ---------------------------------------------------------------------------------------------------------------
// 75A4: enter room `room`.
//   * copy its 64-byte record from the room table (LOCDATA) to the current-room record at 6800,
//   * let the loaders build the tile map (8DE0, 8EA6, 8ED6),
//   * stamp the doors and objects of the record onto the map and draw their sprites,
//   * reset the respawn table, enemy table and the room's pickup entity, redraw the room.
// ---------------------------------------------------------------------------------------------------------------
void player::enterRoom(Machine& m, uint8_t room) {
    at(m, mem::ROOM_NUMBER) = room;
    std::memcpy(&m.ram[mem::ROOMREC], &m.ram[mem::LOCDATA + room * 64], 64);
    // The loaders depend on the registers the original `ldir` leaves behind (found by verification): HL = end of the
    // source record, DE = end of the destination, BC = 0.
    m.cpu.setHL(mem::LOCDATA + room * 64 + 64);
    m.cpu.setDE(mem::PLAYER);
    m.cpu.setBC(0);
    call(m, 0x8DE0);
    call(m, 0x8EA6);
    call(m, 0x8ED6);

    uint16_t rec = IY + REC_DOOR_L1;
    rec = stampObject(m, rec, 0x31, 0x9318) + 1;             // two left doors (4-byte records)
    rec = stampObject(m, rec, 0x31, 0x9318) + 1;
    rec = stampObject(m, rec, 0x35, 0x9318) + 1;             // two right doors
    rec = stampObject(m, rec, 0x35, 0x9318) + 1;
    rec = stampObject(m, rec, 0x36, 0x9315);                 // 3-byte objects
    rec = stampObject(m, rec, 0x36, 0x9315);
    rec = stampObject(m, rec, 0x43, 0x9321);
    rec = stampObject(m, rec, 0x42, 0x9312);
    if (room == 0x3D) stampRoom3DExtras(m);

    for (int i = 0; i < 16; i++) at(m, (uint16_t)(RESPAWN + 4 * i)) = 0xFF;
    at(m, 0x9376) = 0x60;
    at(m, 0x9375) = 0;
    call(m, 0x8099);

    std::memset(&m.ram[ENEMIES], 0xFF, 0x5A);                // 9 enemy slots + the pickup slot
    if (iy(m, REC_PICKUP_Y) != 0xFF) {                       // room has a pickup: entity 9438
        Entity pickup{m, 0x9438};
        pickup.state() = 0x06;
        pickup.y() = iy(m, REC_PICKUP_Y);
        pickup.x() = iy(m, REC_PICKUP_X);
        pickup.frame() = 0x54;
        pickup.anim() = 0x00;
        at(m, 0x943D) = 0x18;
    }
    std::memset(&m.ram[MOVING_OBJS], 0xFF, 0x40);

    call(m, 0x8C77);                                         // draw the room
    call(m, 0x8C8E);
    call(m, 0x91DD);
    call(m, 0x91CA);

    if (room == 0x1C) {                                      // room 0x1C: restore the saved moving objects
        unsigned n = (at(m, 0x95E4) * 8) & 0xFF;
        if (n) std::memcpy(&m.ram[MOVING_OBJS], &m.ram[0x9627], n);
    }
}
NATIVE(0x75A4, enterRoomEntry, "", 4000) { enterRoom(m, m.cpu.a); }

// ---------------------------------------------------------------------------------------------------------------
// 7BF4: in room 0x1C the objects at 95E7 (pairs of 8-byte entities, one pair per tick, round robin through
// 95E6) animate by cycling the low 2 bits of their frame number.
// ---------------------------------------------------------------------------------------------------------------
NATIVE(0x7BF4, animateBossRoom, "", 40) {
    if (at(m, mem::ROOM_NUMBER) != 0x1C) return;
    uint8_t group = (at(m, 0x95E6) + 1) & 3;
    at(m, 0x95E6) = group;
    uint16_t obj = MOVING_OBJS + group * 16;
    for (int i = 0; i < 2; i++, obj += 8) {
        if (at(m, obj) == 0xFF) continue;
        Entity e{m, obj};
        e.remember();
        e.frame() = (e.frame() & ~3) | ((e.frame() + 1) & 3);
        e.update();
    }
}
