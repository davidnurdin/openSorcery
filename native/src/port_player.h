// Player entity, input, HUD and room-entry code of Sorcery+ (ported by hand from the Z80 code at 7457-7C37).
// Shared declarations of port_player_loop.cpp (input / HUD / room entry) and port_player_state.cpp (player state machine,
// door transition).  See docs/port_player.md for the overview.
#pragma once
#include "native.h"

// Thrown to abandon the room loop and go to the end-of-game flow (the original does `pop hl ; jp 8636`).
struct LeaveGame {};
#include "gamemem.h"

namespace player {

// ---------------------------------------------------------------------------------------------------------------
// RAM layout.  IY (0x6800) points at the 64-byte record of the current room, followed directly by the player entity
// (0x6840) and a block of game flags.  Offsets below are relative to IY, as in the original code (iy+nn).
// ---------------------------------------------------------------------------------------------------------------
constexpr uint16_t IY = mem::ROOMREC;                 // 0x6800
constexpr uint16_t PLAYER = mem::PLAYER;              // 0x6840 == IY+64
constexpr uint16_t DOOR_SPRITE = 0x9352;              // scratch entity used for the door sprite / one-off sprites
constexpr uint16_t ENEMIES = 0x93E8;                  // 9 entities x 10 bytes (the last one, 9438, is the room's pickup)
constexpr uint16_t RESPAWN = 0x9464;                  // 16 entries x 4 bytes, byte 0 = 0xFF when unused
constexpr uint16_t HAZARDS = 0x9484;                  // 8 entries x 4 bytes {y, x, ?, ?}: things that hurt the player
constexpr uint16_t MOVING_OBJS = 0x95E7;              // 8 entries x 8 bytes in room 0x1C (see animateBossRoom)

// Room record offsets (IY+n).  Door records are 4 bytes {y, x, destination room, flags}; the flags byte holds the
// lock type in bits 0-5 and, in bits 6-7, "which door of the destination" (bit 6) / "which side" (bit 7).
enum RoomRec : uint8_t {
    REC_FILTER_A = 0x02,        // two bytes that are filtered by dropRoomFlag() when the room is saved
    REC_FILTER_B = 0x0C,
    REC_DOOR_L1 = 0x16,         // left-wall doors (player flies into them with LEFT)
    REC_DOOR_L2 = 0x1A,
    REC_DOOR_R1 = 0x1E,         // right-wall doors
    REC_DOOR_R2 = 0x22,
    REC_OBJ_A = 0x26,           // 3-byte {y, x, ?} objects stamped on the tile map (sprite frame 0x36)
    REC_OBJ_B = 0x29,
    REC_OBJ_C = 0x2C,           // sprite frame 0x43
    REC_OBJ_D = 0x2F,           // sprite frame 0x42
    REC_PICKUP_Y = 0x32,        // y of the room's pickup entity (0xFF = none); x follows
    REC_PICKUP_X = 0x33,
};

// Game flag block (IY+n)
enum Flags : uint8_t {
    F_TICK = 0x48,              // bit 7 set by the 50 Hz interrupt, cleared (set to 7) by the main loop
    F_INPUT = 0x49,             // decoded input bits of this tick (see Input)
    F_TICK2 = 0x4A,             // bit 5 = "frame tick" used by the waitTick routine 7BE9
    F_MODE76 = 0x4C,            // bit 7: end-game phase flag (final cutscene when leaving room 0x1C)
    F_ENERGY = 0x4D,            // energy, packed BCD (starts at 0x99, shown as a percentage)
    F_ENERGY_FRAC = 0x4E,       // fraction of one energy point; a hit removes 0x32/0x100 of it
    F_END = 0x4F,               // end-of-game reasons: bit0 drowned, bit1 out of energy, bit2 out of time; bit7 = leave loop
    F_LOCK = 0x50,              // 0xFF while a transition runs; bit 7 also set while paused
    F_DEMO = 0x51,              // bit 7: demo (attract) mode, input comes from the recorded script at 9A42
    F_RAW_INPUT = 0x52,         // joystick/keys sampled by the interrupt (same bit layout as Input; bit 4 = fire2)
};

// Bits of F_INPUT / F_RAW_INPUT (CPC joystick 0 layout)
enum Input : uint8_t {
    IN_UP = 0x01, IN_DOWN = 0x02, IN_LEFT = 0x04, IN_RIGHT = 0x08, IN_FIRE2 = 0x10, IN_FIRE1 = 0x20,
};

// Player states (low nibble of the entity's byte 0; bit 7 = "sprite is drawn").
enum State : uint8_t {
    ST_APPEAR_DELAY = 0,        // counts ix+4 down, then ST_MATERIALISE
    ST_MATERIALISE = 1,         // spawn animation (frame counts down to 0x4B)
    ST_FLY = 2,                 // normal play: fly, hit hazards, take doors
    ST_FALL = 3,                // out of energy: fall until the floor
    ST_DYING = 4,               // death animation, then game over (END bit 1)
    ST_DROWN_SPLASH = 5,        // landed on river tiles (0x81): splash animation
    ST_DROWN_SINK = 6,          // sinking, then game over (END bit 0)
};

constexpr uint8_t TILE_HAZARD = 0x02;       // touching it costs energy
constexpr uint8_t TILE_RIVER = 0x81;        // solid tile: landing on it drowns the player

// ---------------------------------------------------------------------------------------------------------------
// Tiny helpers over the Z80 address space
// ---------------------------------------------------------------------------------------------------------------
inline uint8_t& at(Machine& m, uint16_t a) { return m.ram[a]; }
inline uint8_t& iy(Machine& m, unsigned off) { return m.ram[(uint16_t)(IY + off)]; }
inline uint16_t rd16(Machine& m, uint16_t a) { return m.ram[a] | m.ram[(uint16_t)(a + 1)] << 8; }
inline void wr16(Machine& m, uint16_t a, uint16_t v) { m.ram[a] = (uint8_t)v; m.ram[(uint16_t)(a + 1)] = v >> 8; }
inline bool carry(const Machine& m) { return m.cpu.f & 1; }
inline void setCarry(Machine& m, bool c) { m.cpu.f = (m.cpu.f & ~1) | (c ? 1 : 0); }

// Address of a tile in the 40x18 tile map for pixel row b / pixel column c (same as the routine at 8C4C).
inline uint16_t tileAddr(uint8_t y, uint8_t x) { return (uint16_t)(mem::TILEMAP + (y & 0xF8) * 5 + (x >> 2)); }

// Calls a Z80 routine (original code, or a native replacement) and returns.
inline void call(Machine& m, uint16_t addr) { m.call(addr); }

// Sprite entity: 10 bytes {state, y, x, frame, anim counter, height, ?, aux, ?, ?}.  The sprite engine routines take the
// entity in IX: 8AAA "remember the old image", 8ACA "redraw the changed sprite", 8AB6 "draw", 8AC0 "erase".
struct Entity {
    Machine& m;
    uint16_t base;
    uint8_t& state() const { return m.ram[base]; }
    uint8_t& y() const { return m.ram[(uint16_t)(base + 1)]; }
    uint8_t& x() const { return m.ram[(uint16_t)(base + 2)]; }
    uint8_t& frame() const { return m.ram[(uint16_t)(base + 3)]; }
    uint8_t& anim() const { return m.ram[(uint16_t)(base + 4)]; }
    uint8_t& aux() const { return m.ram[(uint16_t)(base + 7)]; }
    void run(uint16_t routine) const { m.cpu.ix = base; call(m, routine); }
    void remember() const { run(0x8AAA); }
    void update() const { run(0x8ACA); }
    void draw() const { run(0x8AB6); }
    void erase() const { run(0x8AC0); }
};

inline void playSound(Machine& m, uint16_t effect) { m.cpu.setHL(effect); call(m, 0x92BA); }

// ---------------------------------------------------------------------------------------------------------------
// Routines shared between the two translation units
// ---------------------------------------------------------------------------------------------------------------
void enterRoom(Machine& m, uint8_t room);                       // 75A4
void buildHud(Machine& m);                                      // 752D
void updateFloatingDoor(Machine& m);                            // 7B59
uint8_t dropRoomFlag(uint8_t v);                                // 7BB0
bool lockOpen(Machine& m, uint16_t doorRec);                    // 7B6B
bool doorTransition(Machine& m, uint16_t doorRec, uint8_t playerFrame, uint8_t doorFrame);   // 7940

// Calls Z80 routine `target`, then continues in the C++ function `k` (see port_player_state.cpp, "continuations").
struct Continuation {
    uint16_t addr;                  // address registered with NATIVE(): the "return address" of the call
    void (*fn)(Machine&);
};
void callThen(Machine& m, uint16_t target, const Continuation& k);

}  // namespace player
