// Blocking parts of the player code of Sorcery+: pause, wait-for-tick, the ending sequence and the door entry point.
// Ported by hand from the Z80 code at 7509-752C, 7940, 7BE9 and 7C55-7D31.  See docs/port_player2.md.
// The game runs as a cooperative task: wherever the original spins on a flag set by the interrupt handler, this code calls
// Machine::waitIrq() (through flow::waitIrq()).
#include "port_player.h"
#include "port_flow.h"

using namespace player;

namespace {

// ---------------------------------------------------------------------------------------------------------------
// Constants
// ---------------------------------------------------------------------------------------------------------------
constexpr uint8_t KEY_SPACE = 0x2F;               // CPC key number of the pause key
constexpr uint8_t Z80_ZF = 0x40;                  // zero flag bit of F
constexpr uint8_t LOCK_PAUSED = 0x80;             // bit 7 of F_LOCK: the game is paused
constexpr uint8_t TICK_PENDING = 0x80;            // bit 7 of F_TICK: set by the interrupt handler every 50 Hz tick
constexpr uint8_t TICK_REARM = 0x07;              // value the main loop stores to re-arm the tick flag
constexpr uint8_t FRAME_FLAG = 0x20;              // bit 5 of F_TICK2: "frame tick", waited for by 7BE9

constexpr uint16_t COLLECTED = 0x95E4;            // magic items collected (8 = all); during the ending: objects released so far
constexpr uint16_t ENDING_COUNTDOWN = 0x95E5;     // ticks until the next object of room 0x1C starts to move
constexpr uint8_t ENDING_FIRST_DELAY = 0x46;      // 70 ticks before the first object
constexpr uint8_t ENDING_STEP_DELAY = 0x32;       // 50 ticks between the other objects
constexpr uint8_t ITEM_COUNT = 8;
constexpr uint8_t OBJECT_SIZE = 8;                // size of one entry of MOVING_OBJS
constexpr uint8_t OBJECT_FRAME_ACTIVE = 0x20;     // frame base of an object once released (low 2 bits keep the animation phase)
constexpr uint16_t SND_OBJECT_RELEASED = 0xA851;
constexpr uint16_t SND_ENERGY_FILL = 0xA875;
constexpr uint8_t ENERGY_FULL = 0x99;             // packed BCD 99 %
constexpr uint8_t DEATH_GAME_WON_BIT = 0x80;      // bit 7 of F_END: set when the player's death/drowning animation has finished
constexpr uint8_t ROOM_FILTER_KEEP = 0xC0;        // top two bits of a door's flags byte (destination selection)
constexpr uint8_t DOOR_NEVER_OPENS = 0x06;        // lock type 6
constexpr uint16_t BOSS_DOOR_L1_FLAGS = IY + REC_DOOR_L1 + 3;   // flags byte of the first left door record of room 0x1C
constexpr uint16_t BOSS_DOOR_R1_FLAGS = IY + REC_DOOR_R1 + 3;   // flags byte of the first right door record

constexpr uint16_t WAIT_TICK = 0x7BE9;

// Waits (in the task) for the 50 Hz tick counter: bit 7 of (iy+72) is set by the interrupt handler, the main loop re-arms it.
void waitMainTick(Machine& m) {
    while (!(iy(m, F_TICK) & TICK_PENDING)) flow::waitIrq();
    iy(m, F_TICK) = TICK_REARM;
}

// KM TEST KEY for the space bar.
bool spaceHeld(Machine& m) {
    m.cpu.a = KEY_SPACE;
    call(m, 0xBB1E);
    return !(m.cpu.f & Z80_ZF);
}

// ---------------------------------------------------------------------------------------------------------------
// 7509: pause.  Called every tick of the room loop (not in demo mode).  Space pauses the game: press and release it once
// to freeze, press and release again to continue.  While paused (iy+80) bit 7 is set.
// ---------------------------------------------------------------------------------------------------------------
NATIVE(0x7509, pauseCheck, "", 60) {
    if (!spaceHeld(m)) return;                     // not pressed: nothing to do, the caller continues at once
    iy(m, F_LOCK) |= LOCK_PAUSED;
    auto waitWhile = [&m](bool held) { while (spaceHeld(m) == held) flow::waitIrq(); };
    waitWhile(true);                               // key still down from the press that paused the game
    waitWhile(false);                              // paused: wait for the next press ...
    waitWhile(true);                               // ... and for its release
    iy(m, F_LOCK) &= ~LOCK_PAUSED;
}

// ---------------------------------------------------------------------------------------------------------------
// 7BE9: wait for the frame tick.  Spins on bit 5 of (iy+74), which the interrupt handler sets, then clears it.
// As a native it cannot spin: when the flag is not set yet it sends the Z80 to a `halt ; jp 7BE9` stub in low RAM, i.e. it
// is re-entered after the next interrupt.  The return address on the stack is left alone.
// ---------------------------------------------------------------------------------------------------------------
NATIVE(WAIT_TICK, waitFrameTick, "", 20) {
    while (!(iy(m, F_TICK2) & FRAME_FLAG)) flow::waitIrq();
    iy(m, F_TICK2) &= ~FRAME_FLAG;
}

// ---------------------------------------------------------------------------------------------------------------
// 7940: start of a door transition (not used any more: the state handlers
// call doorTransition() directly).  HL = door record, B = player frame, C = door frame.  The original returns with carry
// clear when the lock does not open; otherwise it drops its return address and the transition (continuations in
// port_player_state.cpp) ends with the `ret` to the caller's caller.
// ---------------------------------------------------------------------------------------------------------------
NATIVE(0x7940, doorEntry, "cf", 60) {
    const uint16_t rec = m.cpu.hl();
    if (!lockOpen(m, rec)) {
        setCarry(m, false);
        return;
    }
    doorTransition(m, rec, m.cpu.b, m.cpu.c);
}

// ---------------------------------------------------------------------------------------------------------------
// 7C55-7D31: the ending of the game.  Entered (through 7C38, port_enemy_items.cpp) when all 8 items are collected and the
// player stands at the altar of room 0x1C.  The game loop is suspended; this sequence runs its own three tick loops:
//   1. the 8 objects of room 0x1C come to life one after the other (70, then 50 ticks apart), the player is still
//      animated (the 77EA part of the fly state: input, hurt check);
//   2. the score and the energy readout run: every tick adds a point and fades the energy bar (926B) until the end flag
//      (iy+79 bit 7) is raised by the player code;
//   3. the energy is refilled to 99 % one point per tick.
// Afterwards the exits of room 0x1C are changed (the first left door loses its lock bits, the first right door gets lock
// type 6 = never opens), (iy+76) is decremented (end-game phase flag, bit 7) and the death flags are cleared.
// ---------------------------------------------------------------------------------------------------------------
void animatePlayerAndRoom(Machine& m) {
    Entity p{m, PLAYER};
    p.remember();
    m.cpu.ix = PLAYER;
    call(m, 0x77EA);                               // player: walking frame, hazards
}

NATIVE(0x7C55, endingSequence, "", 100) {
    call(m, 0x851B);                               // erase all bolts
    iy(m, F_LOCK) = 0xFF;
    iy(m, 0x58) = 0xFF;                            // "game was won once": initiation mode becomes selectable
    iy(m, F_INPUT) = 0;
    at(m, COLLECTED) = 0;
    at(m, ENDING_COUNTDOWN) = ENDING_FIRST_DELAY;

    // 1. release the 8 objects
    do {
        waitMainTick(m);
        if (--at(m, ENDING_COUNTDOWN) == 0) {
            at(m, ENDING_COUNTDOWN) = ENDING_STEP_DELAY;
            Entity obj{m, (uint16_t)(MOVING_OBJS + at(m, COLLECTED)++ * OBJECT_SIZE)};
            obj.remember();
            obj.frame() = (obj.frame() & 3) | OBJECT_FRAME_ACTIVE;
            obj.update();
            playSound(m, SND_OBJECT_RELEASED);
        }
        animatePlayerAndRoom(m);
        call(m, 0x7BF4);                           // animate the objects of the room
    } while (at(m, COLLECTED) != ITEM_COUNT);

    // 2. until the player code raises the end flag
    do {
        waitMainTick(m);
        m.cpu.setBC(0x0001);
        call(m, 0x91B2);                           // score
        call(m, 0x926B);                           // fade the energy bar
        playSound(m, SND_ENERGY_FILL);
        call(m, 0x7BF4);
        animatePlayerAndRoom(m);
    } while (!(iy(m, F_END) & DEATH_GAME_WON_BIT));

    // 3. refill the energy
    for (;;) {
        waitMainTick(m);
        const uint8_t energy = iy(m, F_ENERGY);
        if (energy == ENERGY_FULL) break;
        // packed BCD increment (`add a,1 ; daa`)
        iy(m, F_ENERGY) = (energy & 0x0F) == 9 ? (uint8_t)(energy + 7) : (uint8_t)(energy + 1);
        call(m, 0x7BF4);
        animatePlayerAndRoom(m);
        call(m, 0x91DD);                           // redraw the energy readout
    }

    // 4. change the exits and leave the end-game phase
    at(m, BOSS_DOOR_L1_FLAGS) &= ROOM_FILTER_KEEP;
    at(m, BOSS_DOOR_R1_FLAGS) = (at(m, BOSS_DOOR_R1_FLAGS) & ROOM_FILTER_KEEP) | DOOR_NEVER_OPENS;
    iy(m, F_MODE76)--;
    iy(m, F_END) = 0;
}


}  // namespace
