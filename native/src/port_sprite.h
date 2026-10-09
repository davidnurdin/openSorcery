// Names for the sprite engine (port_sprite.cpp): entity layout, scratch RAM and animated-tile buffers.
// Addresses are in the Z80 address space of the original program.
#pragma once
#include <cstdint>

namespace mem {
// ---- entity ("sprite object") layout: 10 bytes, IX points to it ----
// The player lives at PLAYER (6840), the 8 enemy/object slots at ENEMY_SLOTS (93E8), a few more at 9352 etc.
constexpr uint8_t ENT_STATE = 0;     // bit 7 = "drawn on screen" (an XOR image exists), low bits = behaviour state
constexpr uint8_t ENT_Y = 1;         // top pixel row (0..0x78 for a 24 row sprite on the 144 row play field)
constexpr uint8_t ENT_X = 2;         // left pixel column in mode-0 pixels (0..0x94); bit 0 selects the shifted blitter
constexpr uint8_t ENT_FRAME = 3;     // sprite frame number (144 bytes each at SPRITES)
constexpr uint8_t ENT_ANIM = 4;      // animation counter
constexpr uint8_t ENT_ROWS = 5;      // height of the sprite in pixel rows (24 for full frames)
constexpr uint8_t ENT_ALT_Y = 7;     // (enemy slots) alternative y / x / frame used when leaving a remnant, see enemyToRemnant
constexpr uint8_t ENT_ALT_X = 8;
constexpr uint8_t ENT_ALT_FRAME = 9; // bit 7 selects which of the two sets is used
constexpr unsigned ENTITY_SIZE = 10;
constexpr uint8_t ENT_DRAWN = 0x80;

constexpr uint16_t ENEMY_SLOTS = 0x93E8;   // 8 entities; state byte 0xFF = free slot
constexpr unsigned ENEMY_SLOT_COUNT = 8;
constexpr uint16_t REMNANTS = 0x6834;      // 4 records of 3 bytes {y, x, frame}; frame 0xFF = free
constexpr unsigned REMNANT_COUNT = 4;

// ---- scratch state of the XOR blitter ----
constexpr uint16_t OLD_ENTITY = 0x92C8;    // copy (10 bytes) of an entity before it moves: what is currently on screen
constexpr uint16_t BLIT_ROWS_OLD = 0x92D2; // work counters of the original row stepping loop (always 0 between calls)
constexpr uint16_t BLIT_ROWS_NEW = 0x92D3;
// The original blitter patched its own code ("self-modifying"): operands of `ld de,sprite`, `ld hl,screen`, `call blitter`
// at these addresses. They are dead state now, but kept bit-identical so the verifier (which compares all RAM) agrees.
constexpr uint16_t SMC_OLD_SPRITE = 0x8B6A, SMC_OLD_SCREEN = 0x8B6D, SMC_OLD_BLITTER = 0x8B70;
constexpr uint16_t SMC_NEW_SPRITE = 0x8B7F, SMC_NEW_SCREEN = 0x8B82, SMC_NEW_BLITTER = 0x8B85;
constexpr uint16_t LEGACY_BLIT_ALIGNED = 0x8BAA, LEGACY_BLIT_SHIFTED = 0x8BD5;

// ---- animated tiles (905B) ----
constexpr uint16_t ANIM_TICK = 0x9375;     // counts 4..0, one step per call of animateTiles
constexpr uint16_t ANIM_PHASE = 0x9376;    // animation frame * 0x20 (0,0x20,0x40,0x60)
constexpr uint16_t ANIM_SRC = 0x9377;      // address of the XOR-delta graphics used for the current step
constexpr uint16_t ANIM_TILE = 0x9379;     // tile id searched in the tile map during the current step
constexpr uint16_t ANIM_DELTAS_A = 0x6880; // 4 deltas x 32 bytes (2 tiles), animation for tile 0x81
constexpr uint16_t ANIM_DELTAS_B = 0x6900; // 4 deltas, tile 0x01
constexpr uint16_t ANIM_DELTAS_C = 0x6980; // 2 deltas, tile 0x02
constexpr uint16_t ITEM1_Y = 0x6826, ITEM1_X = 0x6827;   // two collectible items on the room: y = 0xFF when absent
constexpr uint16_t ITEM2_Y = 0x6829, ITEM2_X = 0x682A;
constexpr uint16_t ITEM_SPRITE = 0x9333;   // 5x4 byte image of the collectible items

// ---- HUD ----
constexpr uint16_t SCORE = 0x92D4;         // 3 bytes BCD, most significant first
}  // namespace mem
