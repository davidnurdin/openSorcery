// Player entity state machine of Sorcery+ (Z80 76DD-7B6A, 7B6B-7BE8) and the door transition (7940).
//
// Every game tick the main loop calls 76DD, which saves the player's old sprite image (8AAA) and jumps through the
// table at 935E to the handler of the player's state (low nibble of the entity's byte 0, see port_player.h).
// Handlers redraw the sprite with 8ACA / 8AC0 when they changed it.
//
// ---- Waiting for interrupts -------------------------------------------------------------------------------------
// The door transition is an animation script that waits for the frame tick (7BE9) between its steps.  It is written as a
// chain of small steps ("continuations", K_xxx): callThen() runs the wait / walking step and then the next step.  The waits
// suspend the game task (Machine::waitIrq), so the chain simply runs in the game task's stack.
// ---------------------------------------------------------------------------------------------------------------
#include "port_player.h"
#include <cstring>

using namespace player;

// ===============================================================================================================
// Small predicates on the room record
// ===============================================================================================================

// Lock types 1..6 of a door (low 6 bits of the door's flags byte).  Type 0 = unlocked.  Types 1-5 open when the
// player carries the matching item (IY+70 holds the carried item's id); type 6 never opens.  (Routines 7B86-7BAE.)
static const uint8_t LOCK_KEY_ITEM[5] = {0x3B, 0x2B, 0x29, 0x38, 0x44};

// 7B6B: carry set when the door record at `rec` can be passed.
bool player::lockOpen(Machine& m, uint16_t rec) {
    uint8_t lock = at(m, (uint16_t)(rec + 3)) & 0x3F;
    if (lock == 0) return true;
    if (lock > 5) return false;                  // type 6 (and the invalid 7+, which would crash the original)
    return iy(m, 0x46) == LOCK_KEY_ITEM[lock - 1];
}
NATIVE(0x7B6B, lockOpenEntry, "cf hl", 70) { setCarry(m, lockOpen(m, m.cpu.hl())); }

// 7BB0: filter applied to two bytes of the room record when the room is saved: 0xFF stays, otherwise bit 7 is cleared
// and values 8 and 9 are replaced by 0xFF (a collected/consumed object).
uint8_t player::dropRoomFlag(uint8_t v) {
    if (v == 0xFF) return v;
    v &= 0x7F;
    return (v & 0x7E) == 0x08 ? 0xFF : v;
}
NATIVE(0x7BB0, dropRoomFlagEntry, "a", 50) { m.cpu.a = dropRoomFlag(m.cpu.a); }

// 7B59: in room 0x3D the first left door slides up and down: its y follows the free-running counter at 9311.
void player::updateFloatingDoor(Machine& m) {
    if (at(m, mem::ROOM_NUMBER) != 0x3D) return;
    iy(m, REC_DOOR_L1) = ((at(m, 0x9311) & 0x30) << 1) | 0x10;
}
NATIVE(0x7B59, updateFloatingDoorEntry, "", 31) { updateFloatingDoor(m); }

// ===============================================================================================================
// Hurting the player (7850)
// ===============================================================================================================

// Packed-BCD decrement (what `sub 1; daa` does)
static uint8_t bcdDec(uint8_t v) { return (v & 0x0F) ? v - 1 : v - 7; }

// A hit removes 0x32/0x100 of an energy point; when the fraction underflows one whole BCD point is lost.
static void hurtPlayer(Machine& m) {
    uint8_t frac = iy(m, F_ENERGY_FRAC);
    iy(m, F_ENERGY_FRAC) = frac - 0x32;
    if (frac >= 0x32) return;                    // no borrow: still within the same energy point
    if (iy(m, F_ENERGY) == 0) return;
    iy(m, F_ENERGY) = bcdDec(iy(m, F_ENERGY));
    playSound(m, 0xA863);
    call(m, 0x91DD);                             // redraw the energy readout
}

// ===============================================================================================================
// State handlers.  Each is entered with IX = player entity.
// ===============================================================================================================

// States 0 and 7 (76F6): wait for the counter in ix+4 to run out, then go to the next state.
static void stateAppearDelay(Machine& m) {
    Entity p{m, m.cpu.ix};
    if (--p.anim() == 0) p.state()++;
}

// State 1 (76FE): materialise: every 4th tick the frame counts down; at frame 0x4B the player becomes active (frame 0x20).
static void stateMaterialise(Machine& m) {
    Entity p{m, m.cpu.ix};
    if ((++p.anim() & 3) != 0) return;
    if (--p.frame() == 0x4B) {
        p.frame() = 0x20;
        p.state()++;
    }
    p.update();
}

// One of the four door-check points of state 2: the player is blocked by the wall, standing exactly in front of the
// door at `rec` (same y, and x differing by `dx`).  Returns true when a door transition has been started.
static bool tryDoor(Machine& m, const Entity& p, uint16_t rec, int dx, uint8_t playerFrame, uint8_t doorFrame) {
    if (p.y() != at(m, rec)) return false;
    if ((uint8_t)(p.x() + dx) != at(m, (uint16_t)(rec + 1))) return false;
    return doorTransition(m, rec, playerFrame, doorFrame);
}

// 77EA: the part of state 2 shared with the boss-room code (7C38 calls it directly): out of energy -> falling;
// otherwise choose the walking frame from the input direction, redraw, and check for damage (hazard tiles under the
// player, or an overlapping hazard entity).
static void stateFlyAnimateAndHurt(Machine& m) {
    Entity p{m, m.cpu.ix};
    if (iy(m, F_ENERGY) == 0) {                  // dead: start falling
        p.frame() = 0x20;
        p.state()++;
        p.anim() = 0;
        p.update();
        return;
    }
    p.anim()++;
    uint8_t in = iy(m, F_INPUT);
    uint8_t base = 0x20 - (in & IN_LEFT) + ((in >> 1) & 4);       // 0x1C facing left, 0x24 facing right, 0x20 neutral
    p.frame() = ((p.anim() >> 2) & 3) | base;
    p.update();
    if (p.x() >= 0x0E) updateFloatingDoor(m);

    // hazard tiles in the row below the sprite (3 tiles, 4 when x is not tile aligned)
    uint16_t row = tileAddr(p.y(), p.x()) + ((p.y() & 7) ? 0x78 : 0x50);
    int n = (p.x() & 3) ? 4 : 3;
    for (int i = 0; i < n; i++)
        if (at(m, (uint16_t)(row + i)) == TILE_HAZARD) { hurtPlayer(m); return; }

    // hazard entities: |dy| <= 0x12, |dx| <= 9
    uint8_t py = iy(m, 0x41), px = iy(m, 0x42);
    for (int i = 0; i < 8; i++) {
        uint16_t h = HAZARDS + 4 * i;
        if (at(m, h) == 0xFF) continue;
        if ((uint8_t)(at(m, h) - py + 0x12) >= 0x25) continue;
        if ((uint8_t)(at(m, (uint16_t)(h + 1)) - px + 0x09) >= 0x13) continue;
        hurtPlayer(m);
        return;
    }
}

// State 2 (771C): flying.  Input bits move the sprite 2 pixels per tick unless the collision probe says "blocked"
// (carry): 8FB7 up, 8F5C left, 8F82 right, 8FDD down.  Without UP the player sinks (gravity).  Pushing against a wall
// exactly in front of a door starts the door transition.  Landing on river tiles drowns.
static void stateFly(Machine& m) {
    Entity p{m, m.cpu.ix};
    uint8_t in = iy(m, F_INPUT);

    auto blocked = [&](uint16_t probe) { p.run(probe); return carry(m); };

    if ((in & IN_UP) && !blocked(0x8FB7)) p.y() -= 2;

    if (in & IN_LEFT) {
        if (!blocked(0x8F5C)) p.x() -= 2;
        else if (tryDoor(m, p, IY + REC_DOOR_L1, -0x0C, 0x1C, 0x31) || tryDoor(m, p, IY + REC_DOOR_L2, -0x0C, 0x1C, 0x31))
            return;
    }
    if (in & IN_RIGHT) {
        if (!blocked(0x8F82)) p.x() += 2;
        else if (tryDoor(m, p, IY + REC_DOOR_R1, +0x0C, 0x24, 0x35) || tryDoor(m, p, IY + REC_DOOR_R2, +0x0C, 0x24, 0x35))
            return;
    }

    if (!(in & IN_UP)) {
        if (!blocked(0x8FDD)) {
            p.y() += 2;                                          // gravity
        } else {
            // landed: drown when every tile under the sprite is river
            uint16_t tile = m.cpu.hl();                          // last tile examined by the probe
            int n = (m.cpu.c & 3) ? 4 : 3;
            if (n == 4) tile++;
            bool allRiver = true;
            for (int i = 0; i < n; i++) allRiver &= at(m, (uint16_t)(tile - i)) == TILE_RIVER;
            if (allRiver) {
                p.frame() = 0x48;
                p.anim() = 0;
                p.state() = (p.state() & 0x80) | ST_DROWN_SPLASH;
                playSound(m, 0xA81B);
                p.update();
                return;
            }
        }
    }
    stateFlyAnimateAndHurt(m);
}

// State 3 (7894): out of energy: fall to the floor, then start the death animation.
static void stateFall(Machine& m) {
    Entity p{m, m.cpu.ix};
    p.run(0x8FDD);
    if (!carry(m)) {
        p.y() += 2;
    } else {
        p.frame() = 0x4C;
        p.anim() = 0;
        p.state()++;
        playSound(m, 0xA848);
    }
    p.update();
}

// State 4 (78B6): death animation, frames 0x4C..0x4F; afterwards the game ends ("out of energy").
static void stateDying(Machine& m) {
    Entity p{m, m.cpu.ix};
    if ((++p.anim() & 3) != 0) return;
    if (++p.frame() != 0x50) { p.update(); return; }
    iy(m, F_END) |= 0x80 | 0x02;
    p.erase();
}

// State 5 (78D5): splash animation: frames 0x48 -> 0x4C -> back to 0x47 (ix bit 4 = "going down"), then the sinking
// state with frame 0x5E and a 32-step counter in ix+7.
static void stateDrownSplash(Machine& m) {
    Entity p{m, m.cpu.ix};
    if ((++p.anim() & 3) != 0) return;
    if (!(p.state() & 0x10)) {
        if (++p.frame() != 0x4C) { p.update(); return; }
        p.state() |= 0x10;
    }
    if (--p.frame() != 0x47) { p.update(); return; }
    p.state() &= ~0x10;
    p.frame() = 0x5E;
    p.state()++;
    p.aux() = 0xE0;
    p.y() += 2;
    p.update();
}

// State 6 (7916): sinking: frames 0x5E/0x5F alternate while ix+7 counts up to 0 (8 ticks per count... 32 steps), then
// the game ends ("drowned in the river").
static void stateDrownSink(Machine& m) {
    Entity p{m, m.cpu.ix};
    if ((++p.anim() & 3) != 0) return;
    p.frame() ^= 1;
    if (++p.aux() != 0) { p.update(); return; }
    p.state() &= 0xF0;
    iy(m, F_END) |= 0x80 | 0x01;
    p.erase();
}

// Handler table (935E) indexed by state.  State 7 shares the countdown handler with state 0.
static void (*const HANDLERS[8])(Machine&) = {
    stateAppearDelay, stateMaterialise, stateFly, stateFall, stateDying, stateDrownSplash, stateDrownSink, stateAppearDelay,
};

// Handlers are also registered at their original addresses so that code still jumping through the original table works.
NATIVE(0x76F6, h76F6, "ix", 35) { stateAppearDelay(m); }
NATIVE(0x76FE, h76FE, "ix", 150) { stateMaterialise(m); }
NATIVE(0x771C, h771C, "ix", 900) { stateFly(m); }
NATIVE(0x77EA, h77EA, "ix", 900) { stateFlyAnimateAndHurt(m); }
NATIVE(0x7894, h7894, "ix", 150) { stateFall(m); }
NATIVE(0x78B6, h78B6, "ix", 100) { stateDying(m); }
NATIVE(0x78D5, h78D5, "ix", 100) { stateDrownSplash(m); }
NATIVE(0x7916, h7916, "ix", 100) { stateDrownSink(m); }

// 76DD: per-tick update of the player entity.
NATIVE(0x76DD, updatePlayer, "ix", 400) {
    Entity p{m, PLAYER};
    p.remember();
    uint8_t state = p.state() & 0x0F;
    if (state < 8) HANDLERS[state](m);
}

// ===============================================================================================================
// Walking animation step (7BBF) - waits for the tick, used by the door transition
// ===============================================================================================================
static void walkStepStart(Machine& m);
static void walkStepResume(Machine& m);
static const Continuation K_WALK{0x7BC3, walkStepResume};

// One tick of the player's walking animation: the frame comes from the facing base in 936E and the animation counter.
// Repeats until the counter is a multiple of 4 (so one call lasts up to 4 ticks).
static void walkStepStart(Machine& m) {
    Entity p{m, PLAYER};
    p.remember();
    p.frame() = at(m, 0x936E) | ((p.anim() >> 2) & 3);
    p.anim()++;
    callThen(m, 0x7BE9, K_WALK);                 // wait for the frame tick
}
static void walkStepResume(Machine& m) {
    Entity p{m, PLAYER};
    p.update();
    call(m, 0x905B);
    m.cpu.ix = PLAYER;
    if (p.anim() & 3) walkStepStart(m);
}
NATIVE(0x7BBF, walkStep, "ix", 300) { walkStepStart(m); }
NATIVE(0x7BC3, walkStepResumeEntry, "ix", 0) { walkStepResume(m); }

// ===============================================================================================================
// Door transition (7940)
// ===============================================================================================================
// Called when the player pushes against a wall exactly at a door.  `rec` is the 4-byte door record
// {y, x, destination room, flags}.  If the lock allows it (7B6B) the whole transition runs as an animation:
//   A  the player walks into the door while the door sprite opens (3 frames);
//   B  the player walks on until he is inside the doorway (x +-4 from the door);
//   C  all enemies are retired, the old room's record is saved back into the room table (consumed locks stay open),
//      the special end-game cutscene runs when leaving room 0x1C with F_MODE76 bit 7, the new room is entered;
//   D  the player appears at the destination door (flags bit 7: right/left side, bit 6: first/second door) and walks
//      out of it, E the door closes.
// In demo mode the destination room and flags are read from the demo script instead (so demos are reproducible).
// The routine returns true when the transition ran ("the caller must return immediately", the original pops its own
// return address).  Control is passed on through the continuations below.

namespace {
struct Pending {                                 // destination of the running transition
    uint8_t room;
    uint8_t flags;
} pending;
}  // namespace

static void trA_walked(Machine&);
static void trA_ticked(Machine&);
static void trB_walked(Machine&);
static void trD_walked(Machine&);
static void trE_walked(Machine&);
static void trE_ticked(Machine&);
static const Continuation K_A_WALKED{0x7982, trA_walked};
static const Continuation K_A_TICKED{0x7991, trA_ticked};
static const Continuation K_B_WALKED{0x79A0, trB_walked};
static const Continuation K_D_WALKED{0x7B14, trD_walked};
static const Continuation K_E_WALKED{0x7B3E, trE_walked};
static const Continuation K_E_TICKED{0x7B4D, trE_ticked};

// Runs the routine at `target` (a wait for the frame tick, a walking step ...) and then the next piece of the transition.
void player::callThen(Machine& m, uint16_t target, const Continuation& k) {
    m.call(target);
    k.fn(m);
}

bool player::doorTransition(Machine& m, uint16_t rec, uint8_t playerFrame, uint8_t doorFrame) {
    if (!lockOpen(m, rec)) return false;
    iy(m, F_LOCK) = 0xFF;

    Entity door{m, DOOR_SPRITE};
    door.state() = 0x80;
    door.y() = at(m, rec);
    door.x() = at(m, (uint16_t)(rec + 1));
    pending.room = at(m, (uint16_t)(rec + 2));
    pending.flags = at(m, (uint16_t)(rec + 3)) & 0xC0;
    at(m, (uint16_t)(rec + 3)) = pending.flags;  // the key is used up: the lock bits are cleared for good
    door.frame() = doorFrame;
    at(m, 0x936E) = playerFrame;                 // facing base used by the walking steps (0x1C left, 0x24 right)
    iy(m, 0x43) = playerFrame;                   // player's frame
    playSound(m, 0xA800);
    Entity pl{m, PLAYER};
    pl.update();
    callThen(m, 0x7BBF, K_A_WALKED);
    return true;
}

// 7940 as a routine, for the original code that may still call it: on success the return address of the caller is
// discarded (the caller's caller continues).  Not registered: it can not be verified (it never returns to the
// verification harness), the handlers above call doorTransition() directly.

// Phase A: door opens while the player walks.
static void trA_walked(Machine& m) {
    Entity door{m, DOOR_SPRITE};
    door.remember();
    door.frame()--;
    callThen(m, 0x7BE9, K_A_TICKED);
}
static void trA_ticked(Machine& m) {
    Entity door{m, DOOR_SPRITE};
    door.update();
    if (((door.frame() - 0x2E) & 3) != 0) callThen(m, 0x7BBF, K_A_WALKED);   // door still opening
    else callThen(m, 0x7BBF, K_B_WALKED);
}

// Direction of the player's walk: -2 when facing left (936E < 0x20), +2 when facing right
static int walkDirection(Machine& m) { return at(m, 0x936E) < 0x20 ? -1 : +1; }

// Disc read of the cutscene sprites: track 0x27, sector id 0xC8 -> RAM 5600 (originally done through a far call to
// the AMSDOS BIOS: `rst 0x18`).
static void readCutsceneSector(Machine& m) {
    uint8_t buf[512];
    if (m.res.readSector(0x27, 0xC8, buf)) std::memcpy(&m.ram[0x5600], buf, 512);
}

// End-game cutscene shown when leaving room 0x1C while F_MODE76 bit 7 is set (7A11-7A75).
static void endGameCutscene(Machine& m) {
    at(m, 0x4EA5) = (at(m, 0x4EA5) & 0xC0) | 0x06;     // music driver: switch tune/speed
    m.cpu.im = 1;                                      // firmware interrupt mode while the screen is rebuilt
    m.cpu.a = 0;
    call(m, 0xBC0E);                                   // mode 0, clear
    m.cpu.setBC(0);
    call(m, 0xBC38);                                   // border = black
    readCutsceneSector(m);
    std::memmove(&m.ram[mem::SPRITES], &m.ram[0x5600], 0x120);   // two sprite frames replace frames 0 and 1
    Entity s{m, DOOR_SPRITE};
    s.state() = 0; s.y() = 0x50; s.x() = 0x3C; s.frame() = 0;
    s.draw();
    s.x() = 0x58;
    s.frame()++;
    s.draw();
    m.cpu.ix = 0x94C5;
    call(m, 0x91F8);
    call(m, 0x901E);
    m.cpu.a = 0;
    call(m, 0xBC0E);
    m.cpu.im = 2;
    buildHud(m);
}

// Phase B ends and phase C runs without waiting, then phase D starts.
static void trB_walked(Machine& m) {
    Entity p{m, PLAYER};
    Entity door{m, DOOR_SPRITE};
    p.remember();
    int dir = walkDirection(m);
    p.x() += 2 * dir;
    p.update();
    uint8_t target = p.x() + 4 * dir;            // inside the doorway
    if (door.x() != target) { callThen(m, 0x7BBF, K_B_WALKED); return; }

    // ---- C: leave the room ----
    for (int i = 0; i < 8; i++) {
        m.cpu.ix = ENEMIES + 10 * i;
        call(m, 0x9168);                         // retire enemy (moves it to the respawn table)
    }
    uint8_t room = at(m, mem::ROOM_NUMBER);
    iy(m, REC_FILTER_A) = dropRoomFlag(iy(m, REC_FILTER_A));
    iy(m, REC_FILTER_B) = dropRoomFlag(iy(m, REC_FILTER_B));
    if (room == 0x3D) iy(m, REC_DOOR_L1) = 0x10;
    std::memcpy(&m.ram[mem::LOCDATA + room * 64], &m.ram[mem::ROOMREC], 64);   // save the room state

    if ((iy(m, F_MODE76) & 0x80) && room == 0x1C) endGameCutscene(m);

    bool demo = iy(m, F_DEMO) & 0x80;
    uint8_t newRoom = pending.room;
    if (demo) { uint16_t s = rd16(m, 0x9A42); newRoom = at(m, s); wr16(m, 0x9A42, s + 1); }
    enterRoom(m, newRoom);
    uint8_t flags = pending.flags;
    if (demo) {
        at(m, 0x99B3) = 1;
        uint16_t s = rd16(m, 0x9A42);
        flags = at(m, s);
        wr16(m, 0x9A42, s + 1);
    }

    // ---- D: appear at the destination door ----
    uint16_t dest;
    uint8_t doorFrameBase;
    uint8_t exitX;
    if (!(flags & 0x80)) {                       // arrive at a left-wall door, walking right
        dest = IY + ((flags & 0x40) ? REC_DOOR_L2 : REC_DOOR_L1);
        iy(m, 0x41) = at(m, dest);
        iy(m, 0x42) = at(m, (uint16_t)(dest + 1)) + 4;
        exitX = iy(m, 0x42) + 8;
        at(m, 0x936E) = 0x24;
        doorFrameBase = 0x31;
    } else {                                     // arrive at a right-wall door, walking left
        dest = IY + ((flags & 0x40) ? REC_DOOR_R2 : REC_DOOR_R1);
        iy(m, 0x41) = at(m, dest);
        iy(m, 0x42) = at(m, (uint16_t)(dest + 1)) - 4;
        exitX = iy(m, 0x42) - 8;
        at(m, 0x936E) = 0x1C;
        doorFrameBase = 0x35;
    }
    p.state() &= 0x7F;                           // the player sprite is not on screen yet
    p.aux() = exitX;
    door.state() = 0x80;
    door.x() = at(m, (uint16_t)(dest + 1));
    door.y() = at(m, dest);
    door.frame() = doorFrameBase;
    door.remember();
    door.frame() -= 3;                           // door shown open
    door.update();
    callThen(m, 0x7BBF, K_D_WALKED);
}

// Phase D: the player walks out of the open door until x == ix+7.
static void trD_walked(Machine& m) {
    Entity p{m, PLAYER};
    p.remember();
    p.x() += 2 * walkDirection(m);
    p.update();
    if (p.x() != p.aux()) { callThen(m, 0x7BBF, K_D_WALKED); return; }
    playSound(m, 0xA809);
    playSound(m, 0xA812);
    callThen(m, 0x7BBF, K_E_WALKED);
}

// Phase E: the door closes (3 frames), then the transition is over.
static void trE_walked(Machine& m) {
    Entity door{m, DOOR_SPRITE};
    door.remember();
    door.frame()++;
    callThen(m, 0x7BE9, K_E_TICKED);
}
static void trE_ticked(Machine& m) {
    Entity door{m, DOOR_SPRITE};
    door.update();
    if (((door.frame() - 0x31) & 3) != 0) { callThen(m, 0x7BBF, K_E_WALKED); return; }
    iy(m, F_LOCK) = iy(m, F_DEMO);
    updateFloatingDoor(m);
}

NATIVE(0x7982, trA_walkedEntry, "", 0) { trA_walked(m); }
NATIVE(0x7991, trA_tickedEntry, "", 0) { trA_ticked(m); }
NATIVE(0x79A0, trB_walkedEntry, "", 0) { trB_walked(m); }
NATIVE(0x7B14, trD_walkedEntry, "", 0) { trD_walked(m); }
NATIVE(0x7B3E, trE_walkedEntry, "", 0) { trE_walked(m); }
NATIVE(0x7B4D, trE_tickedEntry, "", 0) { trE_ticked(m); }
