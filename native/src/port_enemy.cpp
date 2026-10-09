// Entity update loop and the "scripted object" behaviours of a room.
//
// Original routines handled here:
//   81A2  update every entity (the two permanent ones of the room record, then the nine slots at 93E8)
//   81BD  update one entity: save its image (8AAA), look the behaviour up in the word table at 937A, run it
//   81DE-844D  behaviours 0x20-0x36: objects dropped by dying enemies, the death bursts of the player, the
//              reward effects of the collected magic items and the small state machines used by room scripts
//   850C  erase all debris particles
// The enemy behaviours 0-9 (7D32-7FCA) are ported elsewhere; 81BD reaches them through the framework like the
// original `call 001E` (= jp hl) did.
//
// An entity is 10 bytes {state, y, x, frame, timer, a5..a9}.  The low 7 bits of `state` select the behaviour,
// bit 7 is just kept as a flag by some scripts.  0xFF (or 0x7F) = unused slot.  Every behaviour is called once per
// frame by processEntity() with the *old* image saved by the sprite engine (8AAA) so that the final draw (8ACA)
// repaints only the difference.  A behaviour that returns without drawing keeps the sprite as it is.
// Entities live at: 6802 and 680C (two permanent slots inside the room record) and 93E8..9437 (eight slots).
// Each behaviour is entered with IX = the entity and returns with `ret` (the framework does it).
#include "native.h"
#include "port_enemy.h"

namespace enemy {

// ------------------------------------------------------------------------------------------------------
// Services of the original program (all of them outside this file's area -> called through the framework)
// ------------------------------------------------------------------------------------------------------
void Entity::saveOld() const { m.cpu.ix = addr; m.call(0x8AAA); }
void Entity::draw() const { m.cpu.ix = addr; m.call(0x8ACA); }
void Entity::erase() const { m.cpu.ix = addr; m.call(0x8AC0); }
void Entity::paintNew() const { m.cpu.ix = addr; m.call(0x8AB6); }

void playSound(Machine& m, uint16_t sndAddr) { m.cpu.setHL(sndAddr); m.call(0x92BA); }
void addScore(Machine& m, uint16_t bcd) { m.cpu.setBC(bcd); m.call(0x91B2); }
void refreshHud(Machine& m) { m.call(0x91DD); }

uint8_t bcdAdd(uint8_t a, uint8_t b) {
    unsigned s = a + b;
    uint8_t r = (uint8_t)s, adj = 0;
    if ((((a & 15) + (b & 15)) > 15) || (r & 15) > 9) adj |= 6;
    if (s > 255 || r > 0x99) adj |= 0x60;
    return (uint8_t)(r + adj);
}

uint8_t bcdSub(uint8_t a, uint8_t b, bool* borrow) {
    uint8_t r = (uint8_t)(a - b), adj = 0;
    bool br = a < b;
    if (((a & 15) < (b & 15)) || (r & 15) > 9) adj |= 6;
    if (br || r > 0x99) adj |= 0x60;
    if (borrow) *borrow = br;
    return (uint8_t)(r - adj);
}

// Tile collision queries of the sprite/room code (entity pointer in IX, carry set = blocked).
static bool blocked(Machine& m, const Entity& e, uint16_t routine) {
    m.cpu.ix = e.addr;
    m.call(routine);
    return m.cpu.f & 1;
}
static bool blockedDown(Machine& m, const Entity& e) { return blocked(m, e, 0x8FD3); }

// 9168: put the entity back as an object on the floor (or just drop it if all 4 floor slots are in use)
static void settleOnFloor(Machine& m, const Entity& e) { m.cpu.ix = e.addr; m.call(0x9168); }

// Behaviour indices (state & 0x7F)
enum Behaviour : uint8_t {
    // --- hostile / collectible things, set up by the room record ---
    B_EMERGE = 0x00,        // rises out of the ground (24 lines), then becomes the state saved in a9
    B_CHASER = 0x01,        // flies straight at the player in both axes (killed by item 0x2D)
    B_WALKER = 0x02,        // walks towards the player horizontally (killed by item 0x41)
    B_DIVER = 0x03,         // swooping enemy with a small turn-around state machine (killed by item 0x2C)
    B_PATROL = 0x04,        // invulnerable flyer patrolling x 0x34..0x44
    B_FOLLOWER = 0x05,      // invulnerable, x follows a room-record byte (6804)
    B_COLLECTIBLE = 0x06,   // magic item: collected when the player touches it
    B_RISE_AWAY = 0x07,     // a collected item floats up and leaves the screen
    B_EXPLODING = 0x08,     // death animation (4 frames), then B_REMAINS
    B_REMAINS = 0x09,       // leftovers shown for 64 frames, then the slot is freed
    // --- scripted objects ---
    B_FALL = 0x20,          // dropped object falling to the floor (y 0x70) or onto an obstacle
    B_BOUNCE = 0x21,        // landing animation, then B_FLOAT_UP
    B_FLOAT_UP = 0x22,      // rises 24 lines, then settles as a floor object
    B_BURST_FULL = 0x23,    // player death: all 8 bolts start from the player position
    B_BURST_QUAD = 0x24,    // same with 4 bolts
    B_SPAWN_FX = 0x25,      // spawns the next effect position from table 945C
    B_SPAWN_FX_2 = 0x26,    // frame countdown
    B_SPAWN_FX_3 = 0x27,    // animated, then settles as floor object
    B_VANISH = 0x28,        // 4 frame animation, then free
    B_WAIT = 0x29,          // wait 32 frames
    B_SHOW = 0x2A,          // show frame 0x39
    B_SHOW_BURST = 0x2B,    // last frame, free, throw debris
    B_DELAY_ALARM = 0x2C,   // wait 8 ticks then become B_VANISH
    B_SCRIPT_PATH = 0x2D,   // follows a table of y deltas (pointer in a8/a9) until 0x80
    B_TIMER_RESTORE = 0x2E, // counts timer up to 0, then becomes the state in a6
    B_FRAME_RESTORE = 0x2F, // frame := x, state := a6
    B_SET_30 = 0x30,
    B_SET_31 = 0x31,
    B_SET_32 = 0x32,
    B_SET_33 = 0x33,
    B_SET_34 = 0x34,
    B_ADVANCE = 0x35,       // next state, play sound
    B_FADE = 0x36,          // 4 frame animation, then free
};

// ------------------------------------------------------------------------------------------------------
// Shared helpers (original 8011 / 85A7 / 7FD5 / 7FEA / 850C / 851B), implemented in port_enemy_items.cpp
// (touchPlayer, eraseBolts, eraseDebris, randomShot, burstDebris, weaponHit are declared in port_enemy.h)
// ------------------------------------------------------------------------------------------------------

// ------------------------------------------------------------------------------------------------------
// Behaviours 0x20-0x36 (objects dropped by dying enemies and room scripts)
// ------------------------------------------------------------------------------------------------------

// 81DE  falling object: 1 line per frame down to y = 0x70, or until it hits something
NATIVE(0x81DE, fallingObject, "ix", 270) {
    Entity e{m, m.cpu.ix};
    if (e.y() != 0x70) {
        if (blockedDown(m, e)) { settleOnFloor(m, e); return; }
        e.y()++;
        e.draw();
        return;
    }
    e.state()++;
    e.a6() = 0;
    e.frame() = 0x48;
    playSound(m, 0xA81B);
    e.draw();
}

// 8205  landing animation: frame 0x48..0x4C and back to 0x47
NATIVE(0x8205, landingBounce, "ix", 130) {
    Entity e{m, m.cpu.ix};
    if (++e.timer() & 3) return;
    if (!(e.a6() & 0x10)) {
        e.frame()++;
        if (e.frame() != 0x4C) { e.draw(); return; }
        e.a6() |= 0x10;
    }
    e.frame()--;
    if (e.frame() != 0x47) { e.draw(); return; }
    e.a6() &= ~0x10;
    e.state()++;
    e.frame() = 0x28;
    e.a5() = 0;
    e.y() += 0x18;
    playSound(m, 0xA87E);
    e.draw();
}

// 824E  rises 24 lines (one per 4 frames), then settles as an object on the floor
NATIVE(0x824E, floatUpToFloor, "ix", 145) {
    Entity e{m, m.cpu.ix};
    if (++e.timer() & 3) return;
    e.y()--;
    e.a5()++;
    if (e.a5() != 0x18) { e.draw(); return; }
    settleOnFloor(m, e);
    e.draw();
}

// 828B tail of the two death bursts: sound only
static void burstSound(Machine& m) { playSound(m, 0xA824); }

// 826B  all 8 bolts start at the player's chest
NATIVE(0x826B, deathBurstEight, "ix", 830) {
    Entity e{m, m.cpu.ix};
    e.state() = 0xFF;
    eraseBolts(m);
    const uint8_t by = (uint8_t)(iyb(m, rr::P_Y) + 9), bx = (uint8_t)(iyb(m, rr::P_X) + 4);
    for (int i = 0; i < 8; i++) { m.ram[BOLTS + 4 * i] = by; m.ram[BOLTS + 4 * i + 1] = bx; }
    burstSound(m);
}

// 8292  same for every other bolt slot (4 bolts)
NATIVE(0x8292, deathBurstFour, "ix", 530) {
    Entity e{m, m.cpu.ix};
    e.state() = 0xFF;
    eraseBolts(m);
    const uint8_t by = (uint8_t)(iyb(m, rr::P_Y) + 9), bx = (uint8_t)(iyb(m, rr::P_X) + 4);
    for (int i = 0; i < 8; i += 2) { m.ram[BOLTS + 4 * i] = by; m.ram[BOLTS + 4 * i + 1] = bx; }   // slots 0, 2, 4, 6
    burstSound(m);
}

// 82BD  effect spawner: takes the next of the (up to 4) positions at 945C, remembers the index in 945B
NATIVE(0x82BD, spawnRewardEffect, "ix", 480) {
    Entity e{m, m.cpu.ix};
    const uint8_t i = m.ram[0x945B];
    m.ram[0x945B] = i + 1;
    if (i + 1 == 4) iyb(m, rr::PATCH_1D) = 0xC6;
    const uint16_t pos = 0x945C + 2 * i;
    e.a7() = e.y() = m.ram[pos];
    e.a8() = e.x() = m.ram[pos + 1];
    e.frame() = 0x4F;
    e.state()++;
    addScore(m, 0x2000);
    playSound(m, 0xA87E);
    e.draw();
}

// 82F9
NATIVE(0x82F9, rewardShrink, "ix", 120) {
    Entity e{m, m.cpu.ix};
    if (++e.timer() & 3) return;
    e.frame()--;
    if (e.frame() != 0x4B) { e.draw(); return; }
    e.state()++;
    e.frame() = 0x50;
    e.timer() = 0x80;
    e.draw();
}

// 831B
NATIVE(0x831B, rewardSparkle, "ix", 155) {
    Entity e{m, m.cpu.ix};
    if (++e.timer() & 3) return;
    e.frame() = (uint8_t)(((e.timer() >> 2) & 3) | 0x50);
    if (e.timer() != 0) { e.draw(); return; }
    settleOnFloor(m, e);
    e.frame() = 0x28;
    e.draw();
}

// 8341  4-frame animation then free the slot
NATIVE(0x8341, vanishAnimation, "ix", 180) {
    Entity e{m, m.cpu.ix};
    e.timer()++;
    e.frame() = (uint8_t)(((e.timer() >> 2) & 3) + 0x4C);
    if (!(e.timer() & 0x10)) { e.draw(); return; }
    e.state() = 0xFF;
    e.erase();
}

// 835E
NATIVE(0x835E, waitThenAdvance, "ix", 60) {
    Entity e{m, m.cpu.ix};
    e.timer()++;
    if (!(e.timer() & 0x20)) return;
    e.state()++;
}

// 836A
NATIVE(0x836A, showFrame39, "ix", 110) {
    Entity e{m, m.cpu.ix};
    e.frame() = 0x39;
    e.state()++;
    e.draw();
}

// 8374
NATIVE(0x8374, finalFrameDebris, "ix", 1180) {
    Entity e{m, m.cpu.ix};
    if (++e.timer() & 3) return;
    e.frame()++;
    e.state() = 0xFF;
    burstDebris(m, e);
    e.draw();
}

// 838E
NATIVE(0x838E, delayThenAlarm, "ix", 100) {
    Entity e{m, m.cpu.ix};
    e.timer()++;
    if (!(e.timer() & 8)) return;
    e.state() = 0xA8;                                     // = B_VANISH with bit 7 set
    e.timer() = 0;
    playSound(m, 0xA848);
}

// 83A5  follows a script of y deltas; 0x80 ends it
NATIVE(0x83A5, followYScript, "ix", 200) {
    Entity e{m, m.cpu.ix};
    uint16_t p = (uint16_t)(e.a8() | e.a9() << 8);
    const uint8_t v = m.ram[p++];
    e.a8() = (uint8_t)p;
    e.a9() = (uint8_t)(p >> 8);
    if (v == 0x80) { e.state() = 0xAF; return; }
    e.y() += v;
    e.draw();
}

// 83C5
NATIVE(0x83C5, timerThenRestore, "ix", 40) {
    Entity e{m, m.cpu.ix};
    if (++e.timer() != 0) return;
    e.state() = e.a6();
}

// 83D0
NATIVE(0x83D0, frameThenRestore, "ix", 145) {
    Entity e{m, m.cpu.ix};
    e.frame() = e.x();
    e.state() = e.a6();
    e.draw();
}

// 83DF
NATIVE(0x83DF, scriptStep30, "ix", 65) {
    Entity e{m, m.cpu.ix};
    e.a6() = 0xB1;
    e.timer() = 0x9C;
    e.state() = 0xAE;
}

// 83EC
NATIVE(0x83EC, scriptStep31, "ix", 150) {
    Entity e{m, m.cpu.ix};
    e.frame() = 0x54;
    e.a6()++;
    e.state() = 0xAE;
    e.timer() = 0xF0;
    e.draw();
}

// 83FE: also prints a message (text at 987C)
NATIVE(0x83FE, scriptStep32, "ix", 240) {
    Entity e{m, m.cpu.ix};
    e.frame() = 0x3C;
    e.state() = 0xAE;
    e.a6()++;
    e.timer() = 0xC8;
    m.cpu.setHL(0x987C);
    m.call(0x8CE4);
    e.draw();
}

// 8416: patches room record bytes 0x0C and 0x06
NATIVE(0x8416, scriptStep33, "ix", 165) {
    Entity e{m, m.cpu.ix};
    e.frame() = 0x54;
    iyb(m, rr::SWITCH_B) = 0xB4;
    e.a6() = 0xB5;
    e.state() = 0xAE;
    iyb(m, rr::SWITCH_A) = 0xC8;
    e.draw();
}

// 842D: patches room record byte 0x10
NATIVE(0x842D, scriptStep34, "ix", 145) {
    Entity e{m, m.cpu.ix};
    e.frame() = 0x55;
    e.a6() = 0xB5;
    e.state() = 0xAE;
    iyb(m, rr::SWITCH_C) = 0xC8;
    e.draw();
}

// 8440
NATIVE(0x8440, advanceWithSound, "ix", 120) {
    Entity e{m, m.cpu.ix};
    e.state()++;
    e.timer() = 0;
    playSound(m, 0xA82D);
}

// 844D
NATIVE(0x844D, fadeOutAnimation, "ix", 135) {
    Entity e{m, m.cpu.ix};
    if (++e.timer() & 3) return;
    e.frame() = (uint8_t)(((e.timer() >> 2) & 3) | 0x4C);
    if (!(e.timer() & 0x10)) { e.draw(); return; }
    e.state() = 0xFF;
    e.erase();
}

// 850C  erase all debris particles (same job as 851B for the bolts; 851B lives in port_enemy_items.cpp)
NATIVE(0x850C, eraseAllDebris, "", 1340) { eraseDebris(m); }

// Dispatch of one entity: remember its image, run the behaviour found in the word table at 937A and return.
// The behaviours 0-9 (7D32-7FCA) belong to another part of the port, so they are run through the framework.
void processEntity(Machine& m, uint16_t entityAddr) {
    Entity e{m, entityAddr};
    const uint8_t type = e.state() & 0x7F;
    if (type == 0x7F) return;                             // unused slot
    e.saveOld();                                          // 8AAA: keep a copy of the entity for the differential redraw
    const uint16_t handler = (uint16_t)(m.ram[HANDLER_TABLE + 2 * type] | m.ram[HANDLER_TABLE + 2 * type + 1] << 8);
    m.cpu.ix = entityAddr;
    m.call(handler);                                // 81D5: call 001E (= jp hl); the handler returns here
}

}  // namespace enemy

using namespace enemy;

// 81BD: process the entity at IX, then advance IX to the next one (entities are 10 bytes apart)
NATIVE(0x81BD, updateEntity, "ix de", 325) {
    Z80& c = m.cpu;
    const uint16_t addr = c.ix;
    processEntity(m, addr);
    c.ix = (uint16_t)(addr + ENTITY_SIZE);
    c.setDE(ENTITY_SIZE);
}

// 81A2: update the two permanent entities of the room record and the nine entity slots
NATIVE(0x81A2, updateEntities, "ix de", 3900) {
    processEntity(m, 0x6802);
    processEntity(m, 0x680C);
    for (int i = 0; i < 9; i++) processEntity(m, ENEMY_TABLE + i * ENTITY_SIZE);
    m.cpu.ix = ENEMY_TABLE + 9 * ENTITY_SIZE;
    m.cpu.setDE(ENTITY_SIZE);
    m.cpu.b = 0;
}
