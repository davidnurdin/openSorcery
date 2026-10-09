// Shared definitions for the first group of entity behaviour routines (0x7D32-0x8010 plus the small helpers they
// use: 85A7, 8F52-8FD3).  See docs/port_handlers1.md.
#pragma once
#include "machine.h"
#include <cstdint>

namespace handlers1 {

// ---- RAM map (Z80 address space) ----------------------------------------------------------------------------
constexpr uint16_t ROOMREC = 0x6800;           // IY == ROOMREC for the whole program
constexpr uint16_t TILE_MAP = 0x6C90;          // 40 x 18 tile ids; a tile with bit 7 set is solid
constexpr uint16_t BOLTS = 0x9464;             // 8 x {y, x, dy, dx}, y == 0xFF: free slot
constexpr uint16_t DEBRIS = 0x9484;            // 8 x {y, x, dy, dx}: enemy shots
constexpr int PARTICLE_COUNT = 8;
constexpr uint16_t COLLECTED = 0x95E4;         // number of magic items collected so far

// Offsets into the room record / player block (what the original addresses as (iy+N)).
namespace rr {
constexpr int FOLLOW_X = 0x04;                 // byte at 6804, used as horizontal position by the "follower"
constexpr int COLLECTIBLE = 0x32;              // 0xFF once the room's magic item has been taken
constexpr int P_STATE = 0x40;                  // low nibble 2 == normal "in play" state
constexpr int P_Y = 0x41;
constexpr int P_X = 0x42;
constexpr int P_HELD = 0x46;                   // item carried by the player (0xFF = nothing)
constexpr int P_ACTION = 0x49;                 // bit 4: action key / weapon swing requested
constexpr int P_ACTION_DONE = 0x4B;            // bit 4: an action has been consumed
constexpr int P_MODE = 0x4C;                   // bit 7: game completed once -> enemies shoot
constexpr int P_ENERGY = 0x4D;                 // BCD
constexpr int P_HURT = 0x4E;                   // damage accumulator
constexpr int P_SHOT_TIMER = 0x53;             // countdown to the next enemy shot (post-game mode)
}  // namespace rr

// Entity: 10 bytes {state, y, x, frame, timer, a5..a9} (see docs/port_sprite.md).
struct Ent {
    Machine& m;
    uint16_t addr;
    uint8_t& state() const { return m.ram[addr]; }   // bit 7: image on screen / flag, bits 0-6: behaviour index
    uint8_t& y() const { return m.ram[(uint16_t)(addr + 1)]; }
    uint8_t& x() const { return m.ram[(uint16_t)(addr + 2)]; }
    uint8_t& frame() const { return m.ram[(uint16_t)(addr + 3)]; }
    uint8_t& timer() const { return m.ram[(uint16_t)(addr + 4)]; }
    uint8_t& rows() const { return m.ram[(uint16_t)(addr + 5)]; }   // a5: rows revealed / behaviour byte
    uint8_t& flags() const { return m.ram[(uint16_t)(addr + 6)]; }  // a6
    uint8_t& next() const { return m.ram[(uint16_t)(addr + 9)]; }   // a9: state to switch to / remains frame / direction
    void draw() const;      // 8ACA: erase old image, paint new one
    void erase() const;     // 8AC0: erase old image only
};

uint8_t& roomByte(Machine& m, int off);          // (iy+off)

// ---- services shared by the behaviours (also usable by other handler groups) ----
bool touchPlayer(Machine& m, const Ent& e, uint8_t damage);          // 85A7
bool weaponHit(Machine& m, const Ent& e, uint8_t weaponItem);        // 8011 (true: behaviour must end)
void maybeShoot(Machine& m, const Ent& e);                           // 7FD5
void burstDebris(Machine& m, const Ent& e);                          // 7FEA
void playSound(Machine& m, uint16_t soundAddr);                      // 92BA
void addScore(Machine& m, uint16_t bcd);                             // 91B2

// Single-probe tile collision (8F52 / 8F78 / 8FAD / 8FD3): true = a solid tile is in the way.
bool probeLeft(Machine& m, const Ent& e);
bool probeRight(Machine& m, const Ent& e);
bool probeUp(Machine& m, const Ent& e);
bool probeDown(Machine& m, const Ent& e);

}  // namespace handlers1
