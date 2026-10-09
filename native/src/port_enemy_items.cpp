// Pickups, doors, bolts, debris and collision / energy events of a room (see port_enemy.cpp for the entities).
//
// Original routines (all called once per frame from the main loop at 745D-7495, in this order):
//   80C0  player action key: pick up / swap a floor object, or use the carried item to spawn something
//   81A2  update all entities (port_enemy.cpp)
//   854D  move the player's bolts      8540  move the debris
//   8470  / 84AB   walk against a locked "door" while carrying its key
//   85D8  energy pickup (heart)        8608  harmful object
//   7BF4  animation of the 8 decorations of room 0x1C      7C38  the ending sequence of room 0x1C
// plus 8099 (room entry: draw the floor objects), 858F (xor two pixels), 851B/850C (erase all bolts / debris),
// 85A7 (enemy touches player), 8011 (weapon / bolt hit test of an enemy), 7FD5/7FEA (enemy shots).
#include "native.h"
#include "port_enemy.h"

namespace enemy {

namespace {

constexpr uint16_t FLOOR_OBJECT_BASE = ROOMREC + rr::FLOOR_OBJECTS;   // 6834
constexpr int FLOOR_OBJECT_COUNT = 4;
constexpr int BOLT_COUNT = 8;
constexpr uint8_t SLOT_FREE = 0xFF;

// Hit box of sprites: |dy| < 0x12.., |dx| < 9..: (a - b + k) < n with 8 bit wrap-around, as in the original.
inline bool within(uint8_t a, uint8_t b, uint8_t k, uint8_t n) { return (uint8_t)(a - b + k) < n; }

// 858F  xor the 2-pixel mark of a bolt / debris particle (y = [ix], x = [ix+1]) onto the screen in two scan lines.
void xorMark(Machine& m, uint8_t y, uint8_t x) {
    m.cpu.b = y;
    m.cpu.c = x;
    m.call(0x8C30);                                 // screen address of pixel (x, y)
    const uint8_t mask = m.ram[PIXEL_MASK];
    uint16_t hl = m.cpu.hl();
    m.ram[hl] ^= mask;
    hl = (uint16_t)((hl & ~0x0800) | 0x1000);             // res 3,h / set 4,h : the second scan line
    m.ram[hl] ^= mask;
}

// 851B / 850C: erase every live particle of a table and mark all slots free.
void eraseTable(Machine& m, uint16_t table, uint8_t mask) {
    m.ram[PIXEL_MASK] = mask;
    for (int i = 0; i < BOLT_COUNT; i++) {
        const uint16_t s = table + 4 * i;
        if (m.ram[s] != SLOT_FREE) xorMark(m, m.ram[s], m.ram[s + 1]);
        m.ram[s] = SLOT_FREE;
    }
}

// 854D / 8540: move every live particle by its (dy, dx); it disappears when it leaves the 0x90 x 0xA0 play area.
void moveTable(Machine& m, uint16_t table, uint8_t mask) {
    m.ram[PIXEL_MASK] = mask;
    for (int i = 0; i < BOLT_COUNT; i++) {
        const uint16_t s = table + 4 * i;
        if (m.ram[s] == SLOT_FREE) continue;
        xorMark(m, m.ram[s], m.ram[s + 1]);
        const uint8_t ny = (uint8_t)(m.ram[s] + m.ram[s + 2]);
        if (ny >= 0x90) { m.ram[s] = SLOT_FREE; continue; }
        m.ram[s] = ny;
        const uint8_t nx = (uint8_t)(m.ram[s + 1] + m.ram[s + 3]);
        if (nx >= 0xA0) { m.ram[s] = SLOT_FREE; continue; }
        m.ram[s + 1] = nx;
        xorMark(m, ny, nx);
    }
}

// 84E1: the player has opened a door with the right item: show the key vanishing, remove the 3x3 tiles of the door.
void openDoor(Machine& m, uint8_t y, uint8_t x, uint8_t frame) {
    Entity e{m, SCRIPTED_ENTITY};
    e.state() = 0x80;
    e.y() = y;
    e.x() = x;
    e.frame() = frame;
    e.saveOld();
    e.erase();
    iyb(m, rr::P_HELD) = 0xFF;                            // the key is used up
    m.cpu.b = y;
    m.cpu.c = x;
    m.call(0x8C4C);                                 // tile map address of the door
    uint16_t tile = m.cpu.hl();
    for (int row = 0; row < 3; row++, tile += 40) m.ram[tile] = m.ram[tile + 1] = m.ram[tile + 2] = 0;
    playSound(m, 0xA836);
}

}  // namespace

// ------------------------------------------------------------------------------------------------------
// Helpers shared with the entity behaviours
// ------------------------------------------------------------------------------------------------------
void eraseBolts(Machine& m) { eraseTable(m, BOLTS, 0xCC); }
void eraseDebris(Machine& m) { eraseTable(m, DEBRIS, 0x33); }

// 85A7  an enemy / hostile object touches the player: sound, and the damage accumulator is charged `damage`;
// every time it underflows the player loses one energy point (BCD) and the energy bar is redrawn.
bool touchPlayer(Machine& m, const Entity& e, uint8_t damage) {
    if (!within(iyb(m, rr::P_Y), e.y(), 0x12, 0x25) || !within(iyb(m, rr::P_X), e.x(), 9, 0x13)) return false;
    playSound(m, 0xA863);
    const uint8_t hurt = iyb(m, rr::P_HURT);
    iyb(m, rr::P_HURT) = (uint8_t)(hurt - damage);
    if (hurt >= damage) return true;
    bool borrow;
    const uint8_t energy = bcdSub(iyb(m, rr::P_ENERGY), 1, &borrow);
    if (borrow) return true;                              // already 0
    iyb(m, rr::P_ENERGY) = energy;
    refreshHud(m);
    return true;
}

// 8011  was the enemy hit by the player's weapon swing (only if the carried item is `weaponItem`) or by a bolt?
// A hit turns the enemy into the exploding state, scores 1000 (weapon) / 500 (bolt) and draws it.
// Returns true when that happened: the enemy behaviour must then end without further processing.
bool weaponHit(Machine& m, const Entity& e, uint8_t weaponItem) {
    uint8_t scoreHi = 0;
    bool hit = false;
    if ((iyb(m, rr::P_ACTION) & 0x10) && within(iyb(m, rr::P_Y), e.y(), 0x12, 0x25) &&
        within(iyb(m, rr::P_X), e.x(), 9, 0x13) && weaponItem == iyb(m, rr::P_HELD)) {
        scoreHi = 0x10;
        iyb(m, rr::P_HELD) = 0xFF;                        // the weapon is used up
        e.a9() = 0x5C;                                    // frame of the remains
        hit = true;
    } else {
        for (int i = 0; i < BOLT_COUNT && !hit; i++) {    // bolts: the first one overlapping the enemy kills it
            const uint16_t s = BOLTS + 4 * i;
            if (m.ram[s] == SLOT_FREE) continue;
            if (within(m.ram[s], e.y(), 0x12, 0x25) && within(m.ram[s + 1], e.x(), 9, 0x13)) {
                scoreHi = 5;
                e.a9() = 0x5D;
                hit = true;
            }
        }
    }
    if (!hit) return false;
    e.state() = (uint8_t)((e.state() & 0x80) | 8);        // -> exploding
    iyb(m, rr::P_ACTION) &= ~0x10;
    iyb(m, rr::P_ACTION_DONE) |= 0x10;
    e.frame() = 0x4C;
    e.timer() = 0;
    addScore(m, (uint16_t)(scoreHi << 8));
    playSound(m, 0xA82D);
    e.draw();
    return true;
}

// 7FEA  burst of 8 debris particles at the enemy position (the particles' velocities are preset in RAM).
void burstDebris(Machine& m, const Entity& e) {
    eraseDebris(m);
    const uint8_t py = (uint8_t)((e.y() & 0xFE) + 9), px = (uint8_t)((e.x() & 0xFE) + 4);
    for (int i = 0; i < BOLT_COUNT; i++) { m.ram[DEBRIS + 4 * i] = py; m.ram[DEBRIS + 4 * i + 1] = px; }
    playSound(m, 0xA83F);
}

// 7FD5  after the game has been completed once, every enemy that calls this fires a debris burst now and then.
void randomShot(Machine& m, const Entity& e) {
    if (!(iyb(m, rr::P_MODE) & 0x80)) return;
    uint8_t& timer = iyb(m, rr::P_SHOT_TIMER);
    const uint8_t old = timer--;
    if (old >= 1) return;
    // The original loads the Z80 refresh register R here; we derive an equally arbitrary number.
    timer = m.random() | 0x80;
    burstDebris(m, e);
}

}  // namespace enemy

using namespace enemy;

// 858F  (ix) = {y, x}: xor the mark of one bolt / debris particle
NATIVE(0x858F, xorParticle, "", 150) {
    xorMark(m, m.ram[m.cpu.ix], m.ram[(uint16_t)(m.cpu.ix + 1)]);
}

// 851B  erase all bolts (the player block's bolt table 9464)
NATIVE(0x851B, eraseAllBolts, "", 1000) { eraseBolts(m); }

// 854D  move the bolts / 8540  move the debris
NATIVE(0x854D, moveBolts, "", 850) { moveTable(m, BOLTS, 0xCC); }
NATIVE(0x8540, moveDebris, "", 850) { moveTable(m, DEBRIS, 0x33); }

// 8099  entering a room: paint the objects lying on the floor (the scratch entity at 9352 is used for drawing)
NATIVE(0x8099, drawFloorObjects, "", 700) {
    Entity e{m, SCRIPTED_ENTITY};
    for (int i = 0; i < FLOOR_OBJECT_COUNT; i++) {
        const uint16_t slot = FLOOR_OBJECT_BASE + 3 * i;
        e.y() = m.ram[slot];
        e.x() = m.ram[slot + 1];
        const uint8_t item = m.ram[slot + 2];
        e.frame() = item;
        e.state() = 0;
        e.frame() &= 0x7F;                                // bit 7 of the item: "already scored" mark, not part of the sprite number
        if (item != SLOT_FREE) e.paintNew();
    }
}

// 80C0  the action key was pressed (bit 4 of the player's action byte)
NATIVE(0x80C0, playerAction, "", 80) {
    if (!(iyb(m, rr::P_ACTION) & 0x10)) return;
    Entity e{m, SCRIPTED_ENTITY};
    const uint8_t py = iyb(m, rr::P_Y), px = iyb(m, rr::P_X);

    // 1. pick up (swap with the carried item) an object lying within reach
    for (int i = 0; i < FLOOR_OBJECT_COUNT; i++) {
        const uint16_t slot = FLOOR_OBJECT_BASE + 3 * i;
        if (m.ram[slot + 2] == SLOT_FREE) continue;
        if (!within(m.ram[slot], py, 0x12, 0x25) || !within(m.ram[slot + 1], px, 9, 0x13)) continue;
        if (m.ram[ROOM_NUMBER] == 0x46) continue;         // nothing can be picked up in room 0x46
        iyb(m, rr::P_ACTION) &= ~0x10;
        iyb(m, rr::P_ACTION_DONE) |= 0x10;
        e.y() = m.ram[slot];
        e.x() = m.ram[slot + 1];
        e.frame() = m.ram[slot + 2];
        uint8_t& slotItem = m.ram[slot + 2];
        const bool firstTime = !(slotItem & 0x80);        // bit 7 marks an object that has been handled before
        slotItem = iyb(m, rr::P_HELD) | 0x80;             // the carried item is left on the floor
        if (firstTime) addScore(m, 0x0050);
        playSound(m, 0xA836);
        e.state() = 0x80;
        e.frame() &= 0x7F;
        e.saveOld();
        const uint8_t picked = e.frame();
        const uint8_t dropped = iyb(m, rr::P_HELD);
        iyb(m, rr::P_HELD) = picked;
        e.frame() = dropped;                              // the scratch entity now shows what was put down
        if (dropped == 0xFF) e.erase(); else e.draw();
        return;
    }

    // 2. use the carried item: records {item, state, room, a9, a7, a8} of the item table; 0xFF in the room field = anywhere
    for (uint16_t rec = ITEM_USE_TABLE; m.ram[rec] != 0xFF; rec += 6) {
        if (m.ram[rec] != iyb(m, rr::P_HELD)) continue;
        const uint8_t room = m.ram[rec + 2];
        if (room != m.ram[ROOM_NUMBER] && room != 0xFF) continue;
        m.call(0x913E);                             // find a free enemy slot (ix, carry = found)
        if (!(m.cpu.f & 1)) return;
        Entity n{m, m.cpu.ix};
        n.frame() = m.ram[rec];
        n.state() = m.ram[rec + 1];
        n.a9() = m.ram[rec + 3];
        n.a7() = m.ram[rec + 4];
        n.a8() = m.ram[rec + 5];
        n.timer() = 0;
        n.a5() = 0x18;
        n.a6() = 0;
        iyb(m, rr::P_HELD) = 0xFF;                        // the item is consumed
        iyb(m, rr::P_ACTION) &= ~0x10;
        iyb(m, rr::P_ACTION_DONE) |= 0x10;
        n.y() = py;
        n.x() = px;
        return;
    }
}

// 8470  door 1: the player stands 8 pixels to the side of the door record, on its line, carrying its key
NATIVE(0x8470, tryDoorA, "", 40) {
    const uint8_t y = iyb(m, rr::DOOR_A_Y);
    if (y == 0xFF || y != iyb(m, rr::P_Y)) return;
    if ((uint8_t)(iyb(m, rr::DOOR_A_X) - iyb(m, rr::P_X) + 8) & 0xEF) return;
    if (iyb(m, rr::DOOR_A_KEY) != iyb(m, rr::P_HELD)) return;
    const uint8_t x = iyb(m, rr::DOOR_A_X);
    iyb(m, rr::DOOR_A_Y) = 0xFF;
    openDoor(m, y, x, 0x43);
}

// 84AB  door 2 (its record holds y + 8)
NATIVE(0x84AB, tryDoorB, "", 40) {
    const uint8_t y = iyb(m, rr::DOOR_B_Y);
    if (y == 0xFF || (uint8_t)(y - 8) != iyb(m, rr::P_Y)) return;
    if (iyb(m, rr::DOOR_B_X) != iyb(m, rr::P_X)) return;
    if (iyb(m, rr::DOOR_B_KEY) != iyb(m, rr::P_HELD)) return;
    const uint8_t x = iyb(m, rr::DOOR_B_X);
    iyb(m, rr::DOOR_B_Y) = 0xFF;
    openDoor(m, y, x, 0x42);
}

// 85D8  energy pickup (heart): stand on it -> +1 energy (BCD, max 0x99)
NATIVE(0x85D8, heartPickup, "", 40) {
    const uint8_t y = iyb(m, rr::HEART_Y);
    if (y == 0xFF || (uint8_t)(y - 0x10) != iyb(m, rr::P_Y)) return;
    if (!within(iyb(m, rr::HEART_X), iyb(m, rr::P_X), 4, 9)) return;
    iyb(m, rr::P_HURT) = 0xFF;
    const uint8_t energy = iyb(m, rr::P_ENERGY);
    if (energy == 0x99) return;
    iyb(m, rr::P_ENERGY) = bcdAdd(energy, 1);
    playSound(m, 0xA863);
    refreshHud(m);
}

// 8608  harmful object (same geometry): -1 energy
NATIVE(0x8608, hazardTouch, "", 40) {
    const uint8_t y = iyb(m, rr::HAZARD_Y);
    if (y == 0xFF || (uint8_t)(y - 0x10) != iyb(m, rr::P_Y)) return;
    if (!within(iyb(m, rr::HAZARD_X), iyb(m, rr::P_X), 4, 9)) return;
    iyb(m, rr::P_HURT) = 0xFF;
    bool borrow;
    const uint8_t energy = bcdSub(iyb(m, rr::P_ENERGY), 1, &borrow);
    if (borrow) return;
    iyb(m, rr::P_ENERGY) = energy;
    playSound(m, 0xA863);
    refreshHud(m);
}

// 7C38  the ending of the game.  It is entered when all 8 magic items are collected, in room 0x1C at (0x18, 0x48),
// and only if the game has not been completed before; the sequence itself is endingSequence() in port_player2.cpp.
NATIVE(0x7C38, endingCheck, "", 51) {
    if (iyb(m, rr::P_MODE) & 0x80) return;
    if (m.ram[COLLECTED] != 8) return;
    if (m.ram[ROOM_NUMBER] != 0x1C) return;
    if (iyb(m, rr::P_Y) != 0x18 || iyb(m, rr::P_X) != 0x48) return;
    m.call(0x7C55);                                       // the ending sequence
}
