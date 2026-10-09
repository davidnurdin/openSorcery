// Text printing, room loading, tile blitting and UI helpers of Sorcery+ (ports of Z80 routines 8C77-92BA).
// Shared RAM names of this area; see docs/port_text.md for the data formats.
#pragma once
#include <cstdint>

namespace mem {
// ---- text / UI ----
constexpr uint16_t TEXT_PHRASES = 0x9A44;     // table of control-code phrases (char codes 0x00-0x1D print string #code+1)
constexpr uint16_t MSG_TABLE_A = 0x9AC8;      // message-line-2 strings, first language/table (see printMessageLine2)
constexpr uint16_t MSG_TABLE_B = 0x9BB9;      // same messages, selected by bit 7 of ROOMREC+76
constexpr uint16_t MSG_KEYS = 0x9CA4;         // 18 values of ROOMREC+70 that have a message + 1 sentinel byte (9CB6)
constexpr uint16_t MSG_LINE1_TABLE = 0x9CB7;  // strings for message line 1
constexpr uint16_t SCORE_BCD = 0x92D4;        // 3 bytes, packed BCD, most significant first
constexpr uint16_t ENERGY_BCD = 0x684D;       // 1 byte, packed BCD (player entity +0x0D)
constexpr uint16_t FILE_LEN = 0x9255;         // length of the file the loader (91F8) read last
// ---- tile animation scratch / energy bar ----
constexpr uint16_t ANIM_FRAMES = 0x6880;      // XOR-delta frames of the 3 animated tile groups, 16 bytes per tile
constexpr uint16_t BAR_COUNTDOWN = 0x92DB;    // ticks until the next energy-bar fade step (period 24)
constexpr uint16_t BAR_PTR = 0x92D7;          // screen address of the byte currently being faded
constexpr uint16_t BAR_COUNT = 0x92D9;        // word: c = bytes left in this scan line of the bar, b = lines left
constexpr uint16_t BAR_MASK = 0x92DC;         // pen mask ANDed into the current byte, rotated right each step
constexpr uint16_t FW_FLAG = 0x92F7;          // interrupt phase counter
constexpr uint16_t SPECIAL_ROOM_FLAG = 0x9667;
}  // namespace mem
