// Shared definitions of the "other moving things" part of Sorcery+ : enemies, dropped objects, bolts (spells),
// pickups, doors and the collision / energy / score events.  See docs/port_enemy.md.
#pragma once
#include "machine.h"
#include <cstdint>

namespace enemy {

// ---------------------------------------------------------------------------------------------------------
// RAM map (Z80 address space).  IY == ROOMREC (0x6800) all the time in the original program.
// ---------------------------------------------------------------------------------------------------------
constexpr uint16_t ROOMREC = 0x6800;
// Offsets inside the room record / "player block" (what the original addresses as (iy+N)).
namespace rr {
constexpr int SWITCH_A = 0x06;          // room bytes patched by the scripted effects 0x33 / 0x32 (door tile data)
constexpr int SWITCH_B = 0x0C;
constexpr int SWITCH_C = 0x10;
constexpr int PATCH_1D = 0x1D;          // byte set to 0xC6 when the 4th spawn of effect 0x25 appears
constexpr int HEART_Y = 0x26;           // energy pickup of the room: y (0xFF = none), x
constexpr int HEART_X = 0x27;
constexpr int HAZARD_Y = 0x29;          // harmful object of the room: y (0xFF = none), x
constexpr int HAZARD_X = 0x2A;
constexpr int DOOR_A_Y = 0x2C;          // locked "door" 1: y, x, key item that opens it (0xFF = none / already open)
constexpr int DOOR_A_X = 0x2D;
constexpr int DOOR_A_KEY = 0x2E;
constexpr int DOOR_B_Y = 0x2F;          // locked "door" 2 (y is stored 8 lines below the sprite position)
constexpr int DOOR_B_X = 0x30;
constexpr int DOOR_B_KEY = 0x31;
constexpr int COLLECTIBLE = 0x32;       // 0xFF when the room's collectible (magic item) has been taken
constexpr int FLOOR_OBJECTS = 0x34;     // 4 x {y, x, item}: objects lying on the floor (item 0xFF = free slot)
// ---- player block (PLAYER == 0x6840) ----
constexpr int P_STATE = 0x40;           // low nibble 2 == normal "in play" state
constexpr int P_Y = 0x41;
constexpr int P_X = 0x42;
constexpr int P_HELD = 0x46;            // item carried by the player (sprite frame number, 0xFF = nothing)
constexpr int P_VSYNC = 0x48;           // bit 7 set by the frame interrupt; main loop waits for it, then stores 7
constexpr int P_ACTION = 0x49;          // bit 4: action key (pick up / use / weapon swing) requested this frame
constexpr int P_ACTION_DONE = 0x4B;     // bit 4 set when an action has been consumed
constexpr int P_MODE = 0x4C;            // bit 7: game completed once (enemies shoot, ending not replayed)
constexpr int P_ENERGY = 0x4D;          // energy, BCD (0x99 max)
constexpr int P_HURT = 0x4E;            // damage accumulator: every borrow costs one energy point
constexpr int P_FLAGS = 0x4F;
constexpr int P_DEMO = 0x57;            // bit 7: attract mode, no score
constexpr int P_SHOT_TIMER = 0x53;      // countdown until a post-game enemy fires its next burst
}  // namespace rr

constexpr uint16_t ROOM_NUMBER = 0x9310;
constexpr uint16_t COLLECTED = 0x95E4;       // number of magic items collected so far (8 = complete)
constexpr uint16_t SCRIPTED_ENTITY = 0x9352; // scratch entity used for pickups / doors / floor objects
constexpr uint16_t ENEMY_TABLE = 0x93E8;     // 9 entities x 10 bytes (only the first 8 are allocated by 0x913E)
constexpr uint16_t ITEM_USE_TABLE = 0x9442;  // 6-byte records {item, state, room, aux9, aux7, aux8}, terminated by 0xFF
constexpr uint16_t BOLTS = 0x9464;           // 8 x {y, x, dy, dx}: the player's bolts (y == 0xFF: free)
constexpr uint16_t DEBRIS = 0x9484;          // 8 x {y, x, dy, dx}: sparks / explosion debris
constexpr uint16_t PIXEL_MASK = 0x94A4;      // 0xCC for bolts, 0x33 for debris (pixel pair xored on screen)
constexpr uint16_t HANDLER_TABLE = 0x937A;   // word table of behaviour routines indexed by (state & 0x7F)
constexpr uint16_t ENTITY_SIZE = 10;

// An entity (10 bytes in RAM) as seen by the behaviour routines.
struct Entity {
    Machine& m;
    uint16_t addr;
    uint8_t& state() const { return m.ram[addr]; }     // bit 7: free flag, bits 0-6: behaviour index (0x7F/0xFF = unused)
    uint8_t& y() const { return m.ram[addr + 1]; }
    uint8_t& x() const { return m.ram[addr + 2]; }
    uint8_t& frame() const { return m.ram[addr + 3]; } // sprite frame number
    uint8_t& timer() const { return m.ram[addr + 4]; } // animation / delay counter
    uint8_t& a5() const { return m.ram[addr + 5]; }    // behaviour specific bytes
    uint8_t& a6() const { return m.ram[addr + 6]; }
    uint8_t& a7() const { return m.ram[addr + 7]; }
    uint8_t& a8() const { return m.ram[addr + 8]; }
    uint8_t& a9() const { return m.ram[addr + 9]; }
    // sprite engine services (original routines at 8AAA / 8ACA / 8AC0 / 8AB6)
    void saveOld() const;    // remember the current state so the next draw() only repaints the difference
    void draw() const;       // erase old image, paint the new one
    void erase() const;      // erase old image only
    void paintNew() const;   // paint without erasing
};

inline uint8_t& iyb(Machine& m, int off) { return m.ram[ROOMREC + off]; }

// Packed BCD helpers with the exact semantics of "add a,v / sub v ; daa" (valid for any input byte).
uint8_t bcdAdd(uint8_t a, uint8_t b);
uint8_t bcdSub(uint8_t a, uint8_t b, bool* borrow = nullptr);

// ---- small services of the original program ----
void playSound(Machine& m, uint16_t sndAddr);   // 92BA
void addScore(Machine& m, uint16_t bcd);        // 91B2 (bc = BCD amount)
void refreshHud(Machine& m);                    // 91DD (energy bar)

// ---- shared with port_enemy_items.cpp ----
void eraseBolts(Machine& m);                    // 851B
void eraseDebris(Machine& m);                   // 850C
bool touchPlayer(Machine& m, const Entity& e, uint8_t damage);      // 85A7
bool weaponHit(Machine& m, const Entity& e, uint8_t weaponItem);    // 8011 (true: the caller must return)
void randomShot(Machine& m, const Entity& e);                       // 7FD5
void burstDebris(Machine& m, const Entity& e);                      // 7FEA
void processEntity(Machine& m, uint16_t entityAddr);                // 81BD

}  // namespace enemy
