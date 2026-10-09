// Named RAM locations of the original program (Z80 address space). Add names as you reverse-engineer them,
// in your own section, with a comment on meaning/units.
#pragma once
#include <cstdint>

namespace mem {
// ---- common ----
constexpr uint16_t FONT = 0x0100;          // CONALP: 2 bytes x 8 rows per glyph, mode-1 pixels (glyph = char-0x20)
constexpr uint16_t SPRITES = 0x0A00;       // 144 bytes per frame: 24 rows x 6 bytes (12 mode-0 pixels), XOR-drawn
constexpr uint16_t CODE = 0x7000;
constexpr uint16_t LOCDATA = 0x4000;       // 64-byte record per room, copied to ROOMREC when entering a room
constexpr uint16_t TILESET = 0x5800;       // 256 tiles x 16 bytes, stored planar: byte j of tile t at TILESET + j*256 + t (4x8 px, mode 0)
constexpr uint16_t ROOMREC = 0x6800;       // current room record == IY base of the game
constexpr uint16_t PLAYER = 0x6840;        // player entity (IX in many routines): +0 state/flags(bit7=drawn), +1 y, +2 x, +3 frame, +4 anim counter
constexpr uint16_t TILEMAP = 0x6C90;       // 40x18 tile ids of the current room (720 bytes)
constexpr uint16_t SCREEN = 0xC000;        // CPC screen RAM
constexpr uint16_t ROOM_NUMBER = 0x9310;
constexpr uint16_t TEXT_MASK = 0x9374;     // pen mask ANDed with font bytes by the text printer (8D1F)
}  // namespace mem
