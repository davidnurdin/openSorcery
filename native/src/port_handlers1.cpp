// Behaviours of the hostile creatures and the magic items of a room (entity states 0-9), plus the helpers they share.
//
// Original routines 7D32-8010, 85A7 and 8F52-8FD3.  The main loop (81A2) calls, for every entity, the routine
// whose address is stored at 0x937A + 2 * (state & 0x7F), with IX = the entity (10 bytes) and the entity's previous
// image saved by 8AAA.  A behaviour ends either with `jp 8ACA` (erase the old image, paint the new one) or with a
// plain `ret` (nothing to repaint).
//
//   state 0  emerge        the creature rises out of the ground, then takes the state stored in a9
//   state 1  chaser        flies straight at the player (dies to item 0x2D)
//   state 2  walker        walks towards the player along the floor (dies to item 0x41)
//   state 3  diver         swooping creature with a turn-around animation (dies to item 0x2C)
//   state 4  patrol        invulnerable flyer going back and forth between x = 0x34 and 0x44
//   state 5  follower      invulnerable, mirrors a room-record byte
//   state 6  collectible   one of the 8 magic items of the game
//   state 7  rise away     the collected item floats up and leaves the screen
//   state 8  exploding     death animation (frames 0x4C-0x4F), then state 9
//   state 9  remains       what is left of the creature, shown for 64 frames
//
// The verifier (SORCERY_VERIFY=1) checks every one of these natives against the original code.
#include "port_handlers1.h"
#include "native.h"

namespace handlers1 {

// ------------------------------------------------------------------------------------------------------
// Services of the sprite engine / HUD / sound, which live outside this file
// ------------------------------------------------------------------------------------------------------
void Ent::draw() const { m.cpu.ix = addr; m.call(0x8ACA); }
void Ent::erase() const { m.cpu.ix = addr; m.call(0x8AC0); }
uint8_t& roomByte(Machine& m, int off) { return m.ram[(uint16_t)(ROOMREC + off)]; }
void playSound(Machine& m, uint16_t soundAddr) { m.cpu.setHL(soundAddr); m.call(0x92BA); }
void addScore(Machine& m, uint16_t bcd) { m.cpu.setBC(bcd); m.call(0x91B2); }

namespace {

constexpr uint8_t SLOT_FREE = 0xFF;      // state / y of an unused entity or particle slot
constexpr uint8_t PLAYING = 0x02;        // low nibble of the player state while the game is being played

// Sounds (addresses of the sound command blocks)
constexpr uint16_t SND_HIT = 0xA863;         // something hurts the player
constexpr uint16_t SND_ENEMY_DIES = 0xA82D;
constexpr uint16_t SND_ITEM = 0xA85A;        // magic item collected
constexpr uint16_t SND_SHOT = 0xA83F;        // enemy fires

// Entity states
constexpr uint8_t ST_EXPLODING = 8;
constexpr uint8_t ST_MASK = 0x7F;

// Frames
constexpr uint8_t FRAME_EXPLOSION_FIRST = 0x4C;   // 4 frames 4C..4F
constexpr uint8_t FRAME_EXPLOSION_END = 0x50;
constexpr uint8_t FRAME_REMAINS_BY_WEAPON = 0x5C;
constexpr uint8_t FRAME_REMAINS_BY_BOLT = 0x5D;
constexpr uint8_t FRAME_ITEM_TAKEN = 0x50;        // 4 frames 50..53, also used for the rising item
constexpr uint8_t FRAME_ITEM_SPIN = 0x54;         // 4 frames 54..57
constexpr uint8_t FRAME_FOLLOWER = 0x58;          // 4 frames 58..5B
constexpr uint8_t FRAME_PATROL = 0x3C;            // 4 frames 3C..3F

// Items the player must hold to kill each kind of creature with a weapon swing
constexpr uint8_t WEAPON_VS_CHASER = 0x2D;
constexpr uint8_t WEAPON_VS_WALKER = 0x41;
constexpr uint8_t WEAPON_VS_DIVER = 0x2C;

// Damage charged to the accumulator when a creature touches the player (an energy point is lost per underflow)
constexpr uint8_t DAMAGE_CHASER = 0x4B, DAMAGE_WALKER = 0x3C, DAMAGE_DIVER = 0x32, DAMAGE_INVULNERABLE = 0xFF;

// Hit box test with 8 bit wrap-around, as the original does it:  (a - b + bias) < size
inline bool within(uint8_t a, uint8_t b, uint8_t bias, uint8_t size) { return (uint8_t)(a - b + bias) < size; }
// Do two sprites overlap?  (y: closer than 0x12 lines, x: closer than 9 pixels - the player is 24 lines high)
inline bool hitBox(uint8_t ay, uint8_t ax, uint8_t by, uint8_t bx) {
    return within(ay, by, 0x12, 0x25) && within(ax, bx, 9, 0x13);
}
inline bool overlapsPlayer(Machine& m, uint8_t y, uint8_t x) {
    return hitBox(roomByte(m, rr::P_Y), roomByte(m, rr::P_X), y, x);
}

// BCD "sub 1 ; daa" on an energy value; carry set (borrow) when it was 0.
uint8_t bcdDecrement(uint8_t v, bool* borrow) {
    *borrow = v == 0;
    const uint8_t r = (uint8_t)(v - 1);
    uint8_t adjust = 0;
    if ((v & 15) == 0 || (r & 15) > 9) adjust |= 0x06;    // decimal adjust after the subtraction
    if (r > 0x99) adjust |= 0x60;
    return (uint8_t)(r - adjust);
}

// ---- tile map probes -----------------------------------------------------------------------------------------
inline uint16_t tileAt(uint8_t y, uint8_t x) { return (uint16_t)((y & 0xF8) * 5 + (x >> 2) + TILE_MAP); }
inline bool solid(Machine& m, uint16_t a) { return m.ram[a] & 0x80; }

// Test the three tiles starting at `first` and stepping by `step` (a column of a creature 24 lines high, or the
// three tiles of a row 12 pixels wide).  `extra`: also test one more tile beyond them (creature not tile aligned).
bool solidRun(Machine& m, uint16_t first, uint16_t step, bool extra) {
    for (int i = 0; i < 3; i++)
        if (solid(m, (uint16_t)(first + i * step))) return true;
    return extra && solid(m, (uint16_t)(first + 3 * step));
}

constexpr uint16_t TILE_ROW = 40;                        // tiles per row of the map

}  // namespace

// 8F52  a solid tile (or the screen edge) directly left of the creature?  Only tile-aligned x is tested (x % 4 == 0).
bool probeLeft(Machine& m, const Ent& e) {
    const uint8_t y = e.y(), x = e.x();
    if (x == 0) return true;
    if (x & 3) return false;
    return solidRun(m, (uint16_t)(tileAt(y, x) - 1), TILE_ROW, (y & 7) != 0);
}

// 8F78  ... directly right (the creature is 12 pixels = 3 tiles wide; the screen is 160 pixels wide)
bool probeRight(Machine& m, const Ent& e) {
    const uint8_t y = e.y(), x = e.x();
    if (x == 0x94) return true;
    if (x & 3) return false;
    return solidRun(m, (uint16_t)(tileAt(y, x) + 3), TILE_ROW, (y & 7) != 0);
}

// 8FAD  ... directly above (only when y is a multiple of 8)
bool probeUp(Machine& m, const Ent& e) {
    const uint8_t y = e.y(), x = e.x();
    if (y == 0) return true;
    if (y & 7) return false;
    return solidRun(m, (uint16_t)(tileAt(y, x) - TILE_ROW), 1, (x & 3) != 0);
}

// 8FD3  ... directly below (the creature is 24 lines = 3 tile rows high; the field is 144 lines high)
bool probeDown(Machine& m, const Ent& e) {
    const uint8_t y = e.y(), x = e.x();
    if (y == 0x78) return true;
    if (y & 7) return false;
    return solidRun(m, (uint16_t)(tileAt(y, x) + 3 * TILE_ROW), 1, (x & 3) != 0);
}

// 85A7  A hostile creature at e overlaps the player: play the hurt sound and charge `damage` to the accumulator.
// Every underflow of the accumulator costs the player one energy point (BCD), which redraws the energy display.
bool touchPlayer(Machine& m, const Ent& e, uint8_t damage) {
    if (!overlapsPlayer(m, e.y(), e.x())) return false;
    playSound(m, SND_HIT);
    const uint8_t hurt = roomByte(m, rr::P_HURT);
    roomByte(m, rr::P_HURT) = (uint8_t)(hurt - damage);
    if (hurt >= damage) return true;                      // no underflow yet
    bool borrow;
    const uint8_t energy = bcdDecrement(roomByte(m, rr::P_ENERGY), &borrow);
    if (borrow) return true;                              // no energy left to lose
    roomByte(m, rr::P_ENERGY) = energy;
    m.call(0x91DD);                                 // redraw the energy display
    return true;
}

// 8011  Was the creature hit by the weapon swing of the player (action key while holding `weaponItem`), or by one of
// the player's bolts?  A hit starts the explosion, scores 1000 (weapon, which is used up) or 500 (bolt), and draws.
// Returns true when that happened: the behaviour must then end at once (the original discards its own return
// address and finishes through 8ACA).
bool weaponHit(Machine& m, const Ent& e, uint8_t weaponItem) {
    uint8_t scoreHigh;                                    // BCD points / 100
    if ((roomByte(m, rr::P_ACTION) & 0x10) && overlapsPlayer(m, e.y(), e.x()) &&
        roomByte(m, rr::P_HELD) == weaponItem) {
        scoreHigh = 0x10;
        roomByte(m, rr::P_HELD) = 0xFF;                   // the weapon is used up
        e.next() = FRAME_REMAINS_BY_WEAPON;
    } else {
        int bolt = 0;
        for (; bolt < PARTICLE_COUNT; bolt++) {           // the first bolt overlapping the creature kills it
            const uint16_t s = BOLTS + 4 * bolt;
            if (m.ram[s] != SLOT_FREE && hitBox(m.ram[s], m.ram[s + 1], e.y(), e.x())) break;
        }
        if (bolt == PARTICLE_COUNT) return false;
        scoreHigh = 0x05;
        e.next() = FRAME_REMAINS_BY_BOLT;
    }
    e.state() = (uint8_t)((e.state() & 0x80) | ST_EXPLODING);
    roomByte(m, rr::P_ACTION) &= ~0x10;
    roomByte(m, rr::P_ACTION_DONE) |= 0x10;
    e.frame() = FRAME_EXPLOSION_FIRST;
    e.timer() = 0;
    addScore(m, (uint16_t)(scoreHigh << 8));
    playSound(m, SND_ENEMY_DIES);
    e.draw();
    return true;
}

// 7FEA  The creature fires: all 8 enemy shots start at its centre (their velocities are preset in the table).
void burstDebris(Machine& m, const Ent& e) {
    m.call(0x850C);                                 // remove the shots still flying
    const uint8_t y = (uint8_t)((e.y() & 0xFE) + 9), x = (uint8_t)((e.x() & 0xFE) + 4);
    for (int i = 0; i < PARTICLE_COUNT; i++) {
        m.ram[DEBRIS + 4 * i] = y;
        m.ram[DEBRIS + 4 * i + 1] = x;
    }
    playSound(m, SND_SHOT);
}

// 7FD5  After the game has been completed once (bit 7 of the mode byte), creatures shoot now and then: a countdown
// runs per call and when it underflows a burst is fired and the countdown restarts at a pseudo random 0x80-0xFF.
void maybeShoot(Machine& m, const Ent& e) {
    if (!(roomByte(m, rr::P_MODE) & 0x80)) return;
    const uint8_t before = roomByte(m, rr::P_SHOT_TIMER)--;
    if (before >= 1) return;
    roomByte(m, rr::P_SHOT_TIMER) = m.random() | 0x80;      // the original used the Z80 refresh register R as random source
    burstDebris(m, e);
}

}  // namespace handlers1
using namespace handlers1;

// ======================================================================================================
// The behaviour routines.  Each one is entered with IX = entity and returns to the entity update loop.
// ======================================================================================================
namespace {

Ent entityAtIx(Machine& m) { return Ent{m, m.cpu.ix}; }

// The creatures below only act while the player is in the normal playing state (not dying / changing room);
// otherwise they merely stay painted.
bool playerIsPlaying(Machine& m) { return (roomByte(m, rr::P_STATE) & 0x0F) == PLAYING; }

// Animation frame = the frame's upper bits with the low two bits taken from `phase`.
uint8_t withLow2Bits(uint8_t frame, uint8_t phase) { return (uint8_t)((frame & ~3) | (phase & 3)); }

}  // namespace

// 7D32  A creature appearing out of the ground: one more line of its sprite is revealed every other frame (the top
// moves up by one, a5 counts the lines). After 24 lines it becomes a normal creature (state taken from a9).
NATIVE(0x7D32, emerge, "ix iy", 120) {
    Ent e = entityAtIx(m);
    if (--e.timer() != 0) return;
    e.timer() = 1;
    e.y()--;
    e.rows()++;
    if (e.rows() == 24) e.state() = e.next();
    e.draw();
}

// 7D51  The chaser flies diagonally straight at the player, one pixel per frame in each axis, unless a wall blocks
// that axis.  The sprite animates with timer bits 2-3.
NATIVE(0x7D51, chaser, "ix iy", 900) {
    Ent e = entityAtIx(m);
    if (!playerIsPlaying(m)) { e.draw(); return; }
    if (weaponHit(m, e, WEAPON_VS_CHASER)) return;
    touchPlayer(m, e, DAMAGE_CHASER);
    maybeShoot(m, e);
    const uint8_t px = roomByte(m, rr::P_X), py = roomByte(m, rr::P_Y);
    if (px < e.x() && !probeLeft(m, e)) e.x()--;
    if (e.x() < px && !probeRight(m, e)) e.x()++;
    if (py < e.y() && !probeUp(m, e)) e.y()--;
    if (e.y() < py && !probeDown(m, e)) e.y()++;
    e.timer()++;
    e.frame() = withLow2Bits(e.frame(), e.timer() >> 2);
    e.draw();
}

// 7DBE  The walker follows the player horizontally along the floor.  Frames 0x10-0x13 walk left, 0x18-0x1B right; the
// legs animate every 4th frame and the creature only moves during the second half of the cycle.
NATIVE(0x7DBE, walker, "ix iy", 900) {
    constexpr uint8_t LEFT_FIRST = 0x10, LEFT_LAST = 0x13, RIGHT_FIRST = 0x18, RIGHT_END = 0x1C;
    Ent e = entityAtIx(m);
    if (!playerIsPlaying(m)) { e.draw(); return; }
    if (weaponHit(m, e, WEAPON_VS_WALKER)) return;
    touchPlayer(m, e, DAMAGE_WALKER);
    maybeShoot(m, e);
    e.timer()++;
    if (roomByte(m, rr::P_X) < e.x()) {                   // the player is on the left
        if ((e.timer() & 3) == 0) {
            e.frame()--;
            if (e.frame() < LEFT_FIRST) e.frame() = LEFT_LAST;
        }
        if (e.frame() < 0x15 && !probeLeft(m, e)) e.x()--;
    } else {
        if ((e.timer() & 3) == 0) {
            e.frame()++;
            if (e.frame() >= RIGHT_END) e.frame() = RIGHT_FIRST;
        }
        if (e.frame() >= 0x17 && !probeRight(m, e)) e.x()++;
    }
    e.draw();
}

// 7E2D  The diver flies horizontally two pixels per frame towards the player's column and drifts one line per frame
// towards the player's height.  Frames: 4-7 fly left, 0x0C-0x0F fly right, 8-0x0B the turn-around (8 = facing right
// ... 0x0B = facing left).  Flag bits in a6: bit 0 = facing/turning right, bit 1 = turn-around animation running.
NATIVE(0x7E2D, diver, "ix iy", 1100) {
    enum : uint8_t { FACING_RIGHT = 1, TURNING = 2 };
    constexpr uint8_t TURN_FIRST = 0x08, TURN_LAST = 0x0B, FLY_LEFT = 0x04, FLY_RIGHT = 0x0C;
    Ent e = entityAtIx(m);
    if (!playerIsPlaying(m)) { e.draw(); return; }
    if (weaponHit(m, e, WEAPON_VS_DIVER)) return;
    touchPlayer(m, e, DAMAGE_DIVER);
    maybeShoot(m, e);
    uint8_t flags = e.flags(), y = e.y(), x = e.x(), frame = e.frame(), t = e.timer();
    t++;
    const uint8_t px = roomByte(m, rr::P_X), py = roomByte(m, rr::P_Y);
    if (flags & TURNING) {                                // step through the turn frames every other tick
        if (!(t & 1)) {
            frame += (flags & FACING_RIGHT) ? -1 : 1;
            if ((uint8_t)(frame - TURN_FIRST) >= 4) flags &= ~TURNING;   // left the turn frames: done
        }
    } else if (x == px) {                                 // exactly above/below the player: hover, pick a facing
        flags = (flags & ~3) | (frame < 0x0A ? TURNING : (TURNING | FACING_RIGHT));
        frame = (flags & FACING_RIGHT) ? TURN_LAST : TURN_FIRST;
    } else if (x > px) {                                  // the player is on the left
        if (frame < TURN_FIRST) {                         // already facing left: fly
            x -= 2;
            frame = ((t >> 2) & 3) | FLY_LEFT;
        } else {                                          // facing right: turn to the left
            frame = TURN_LAST;
            flags |= TURNING | FACING_RIGHT;
        }
    } else {                                              // the player is on the right
        if (frame < FLY_RIGHT) {                          // not yet facing right: turn
            frame = TURN_FIRST;
            flags = (flags | TURNING) & ~FACING_RIGHT;
        } else {
            x += 2;
            frame = ((t >> 2) & 3) | FLY_RIGHT;
        }
    }
    if (py != y) y += (py < y) ? -1 : 1;                  // drift towards the player's height
    e.flags() = flags; e.y() = y; e.x() = x; e.frame() = frame; e.timer() = t;
    e.draw();
}

// 7EDB  The patrolling flyer (invulnerable, hurts on touch) moves one pixel per frame between x = 0x34 and 0x44.
// Bit 7 of a9 is its direction (set = left).
NATIVE(0x7EDB, patrol, "ix iy", 250) {
    constexpr uint8_t MIN_X = 0x34, END_X = 0x45, LEFT = 0x80;
    Ent e = entityAtIx(m);
    touchPlayer(m, e, DAMAGE_INVULNERABLE);
    e.timer()++;
    e.frame() = (uint8_t)(((e.timer() >> 2) & 3) | FRAME_PATROL);
    auto step = [&] { return (uint8_t)(e.x() + ((e.next() & LEFT) ? -1 : 1)); };
    uint8_t nx = step();
    if (nx < MIN_X || nx >= END_X) {                      // end of the beat: turn round
        e.next() ^= LEFT;
        nx = step();
    }
    e.x() = nx;
    e.draw();
}

// 7F18  The follower (invulnerable, hurts on touch) hangs at x = room byte 4 + 0x1C, i.e. it moves with something else
// in the room, and just animates.
NATIVE(0x7F18, follower, "ix iy", 250) {
    Ent e = entityAtIx(m);
    touchPlayer(m, e, DAMAGE_INVULNERABLE);
    e.timer()++;
    e.frame() = (uint8_t)(((e.timer() >> 2) & 3) | FRAME_FOLLOWER);
    e.x() = (uint8_t)(roomByte(m, rr::FOLLOW_X) + 0x1C);
    e.draw();
}

// 7F37  A magic item (one of the 8 to collect).  It spins (4 frames, changing every 4th call) until the player
// overlaps it: then it counts as collected (+2000 points), the room record is marked and it floats away (state 7).
NATIVE(0x7F37, collectible, "ix iy", 250) {
    constexpr uint16_t SCORE_ITEM = 0x2000;
    Ent e = entityAtIx(m);
    if (within(roomByte(m, rr::P_Y), e.y(), 0x17, 0x2E) && within(roomByte(m, rr::P_X), e.x(), 0x0B, 0x17)) {
        e.state()++;
        m.ram[COLLECTED]++;
        e.frame() = FRAME_ITEM_TAKEN;
        roomByte(m, rr::COLLECTIBLE) = 0xFF;
        addScore(m, SCORE_ITEM);
        playSound(m, SND_ITEM);
        e.draw();
        return;
    }
    e.timer()++;
    if (e.timer() & 3) return;
    e.frame() = (uint8_t)(((e.frame() + 1) & 3) | FRAME_ITEM_SPIN);
    e.draw();
}

// 7F84  The collected item rises two lines per frame (spinning) and is freed when it has wrapped past the top (y >= 0xC8).
NATIVE(0x7F84, riseAway, "ix iy", 200) {
    Ent e = entityAtIx(m);
    e.y() -= 2;
    e.frame() = (uint8_t)((((e.y() >> 1) ^ 3) & 3) | FRAME_ITEM_TAKEN);
    if (e.y() < 0xC8) { e.draw(); return; }
    e.state() = SLOT_FREE;
    e.erase();
}

// 7FA6  Death animation: frames 4C-4F, one every 4th call; then the entity becomes "remains" (state 9) showing the
// frame stored in a9 for 64 more calls (the timer starts at 0xC0 and counts up to 0).
NATIVE(0x7FA6, exploding, "ix iy", 150) {
    Ent e = entityAtIx(m);
    e.timer()++;
    if (e.timer() & 3) return;
    e.frame()++;
    if (e.frame() == FRAME_EXPLOSION_END) {
        e.state()++;
        e.timer() = 0xC0;
        e.frame() = e.next();
    }
    e.draw();
}

// 7FCA  Remains: when the timer wraps to 0 the picture is erased and the slot is freed.
NATIVE(0x7FCA, remains, "ix iy", 80) {
    Ent e = entityAtIx(m);
    if (++e.timer() != 0) return;
    e.state() = SLOT_FREE;
    e.erase();
}

// ------------------------------------------------------------------------------------------------------
// Helpers callable from other routines of the original program, so they are registered at their own addresses.
// ------------------------------------------------------------------------------------------------------

// 7FD5 / 7FEA  (IX = entity)
NATIVE(0x7FD5, shootNowAndThen, "", 40) { maybeShoot(m, entityAtIx(m)); }
NATIVE(0x7FEA, shootBurst, "", 400) { burstDebris(m, entityAtIx(m)); }

// 85A7  (IX = entity, C = damage)
NATIVE(0x85A7, creatureTouchesPlayer, "", 60) { touchPlayer(m, entityAtIx(m), m.cpu.c); }

// 8F52 / 8F78 / 8FAD / 8FD3  (IX = entity)  carry set = blocked
static void setBlocked(Machine& m, bool blocked) { m.cpu.f = (uint8_t)((m.cpu.f & ~1) | (blocked ? 1 : 0)); }
NATIVE(0x8F52, blockedLeftHere, "cf", 90) { setBlocked(m, probeLeft(m, entityAtIx(m))); }
NATIVE(0x8F78, blockedRightHere, "cf", 90) { setBlocked(m, probeRight(m, entityAtIx(m))); }
NATIVE(0x8FAD, blockedUpHere, "cf", 90) { setBlocked(m, probeUp(m, entityAtIx(m))); }
NATIVE(0x8FD3, blockedDownHere, "cf", 90) { setBlocked(m, probeDown(m, entityAtIx(m))); }

