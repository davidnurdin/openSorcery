// Sprite engine: XOR drawing/erasing/moving of entities, tile collision probes, animated tiles,
// enemy-slot helpers and the score display.  Hand-written replacement for the Z80 routines 8AAA-8C2F,
// 8F5C-9007, 901E, 905B and 913E-91F7 (see docs/port_sprite.md).
#include "native.h"
#include "gamemem.h"
#include "port_sprite.h"
using namespace lift;

namespace {

// ------------------------------------------------------------------------------------------------
// Small helpers
// ------------------------------------------------------------------------------------------------
constexpr unsigned SPRITE_FRAME_BYTES = 144;   // 24 rows x 6 bytes
constexpr unsigned SPRITE_ROW_BYTES = 6;       // 12 mode-0 pixels
constexpr uint16_t TILE_COLUMNS = 40;          // tile map is 40 x 18 tiles of 4 x 8 pixels
constexpr uint8_t TILE_SOLID = 0x80;           // tile ids with bit 7 set block movement

inline uint8_t& R(Machine& m, uint16_t a) { return m.ram[a]; }
inline uint16_t rd16(Machine& m, uint16_t a) { return R(m, a) | (R(m, a + 1) << 8); }
inline void wr16(Machine& m, uint16_t a, uint16_t v) { R(m, a) = v & 0xFF; R(m, a + 1) = v >> 8; }

// CPC mode-0 screen address of pixel row y / column x (two pixels per byte).  Same as native 8C30.
inline uint16_t screenAddress(uint8_t y, uint8_t x) {
    return (uint16_t)(0xC000 + ((y & 7) << 11) + (y >> 3) * 80 + (x >> 1));
}
// Address of tile (x/4, y/8) in the tile map.  Same as native 8C4C.
inline uint16_t tileAddress(uint8_t y, uint8_t x) {
    return (uint16_t)(mem::TILEMAP + (y >> 3) * TILE_COLUMNS + (x >> 2));
}
// Next scan line of a CPC screen address (+0x800, and wrap into the next character row when it leaves the 16K block).
inline uint16_t nextScanline(uint16_t a) {
    uint16_t r = (uint16_t)((a & 0x00FF) | ((a + 0x0800) & 0xFF00));
    if ((unsigned)a + 0x0800 > 0xFFFF) r = (uint16_t)(r + 0xC050);
    return r;
}
inline uint16_t spriteFrameAddress(uint8_t frame) { return (uint16_t)(mem::SPRITES + frame * SPRITE_FRAME_BYTES); }

// ------------------------------------------------------------------------------------------------
// Entity: view over the 10 byte record in RAM
// ------------------------------------------------------------------------------------------------
class Entity {
public:
    Entity(Machine& m, uint16_t addr) : m_(m), addr_(addr) {}
    uint8_t& at(uint8_t off) { return m_.ram[(uint16_t)(addr_ + off)]; }
    uint8_t& state() { return at(mem::ENT_STATE); }
    uint8_t& y() { return at(mem::ENT_Y); }
    uint8_t& x() { return at(mem::ENT_X); }
    uint8_t& frame() { return at(mem::ENT_FRAME); }
    uint8_t& rows() { return at(mem::ENT_ROWS); }
    bool drawn() { return state() & mem::ENT_DRAWN; }
    uint16_t address() const { return addr_; }
private:
    Machine& m_;
    uint16_t addr_;
};

// ------------------------------------------------------------------------------------------------
// XOR blitters (originals at 8BAA and 8BD5, which were patched into the stepping loop at 8B65)
// ------------------------------------------------------------------------------------------------
// Sprite row for even x: 6 bytes XORed onto 6 screen bytes.
void xorRowAligned(Machine& m, uint16_t src, uint16_t dst) {
    for (unsigned i = 0; i < SPRITE_ROW_BYTES; i++) R(m, dst + i) ^= R(m, src + i);
}
// Sprite row for odd x: the row is shifted right by one mode-0 pixel, so it covers 7 screen bytes.
// A mode-0 byte holds two pixels: pixel 0 in the odd bits (mask AA), pixel 1 in the even bits (mask 55).
// Shifting by one pixel moves pixel 1 of byte i-1 into pixel 0 of byte i, and pixel 0 of byte i into pixel 1.
void xorRowShifted(Machine& m, uint16_t src, uint16_t dst) {
    uint8_t s[SPRITE_ROW_BYTES];
    for (unsigned i = 0; i < SPRITE_ROW_BYTES; i++) s[i] = R(m, src + i);
    R(m, dst) ^= (s[0] >> 1) & 0x55;
    for (unsigned i = 1; i < SPRITE_ROW_BYTES; i++) R(m, dst + i) ^= ((s[i - 1] << 1) & 0xAA) | ((s[i] >> 1) & 0x55);
    R(m, dst + SPRITE_ROW_BYTES) ^= (s[SPRITE_ROW_BYTES - 1] << 1) & 0xAA;
}

// One image on the screen: where it comes from and where it goes.
struct Image {
    uint16_t sprite;     // address of the first sprite row
    uint16_t screen;     // screen address of the first row
    bool shifted;        // odd x
    uint8_t rows;
};
Image imageOf(Machine& m, uint8_t y, uint8_t x, uint8_t frame, uint8_t rows) {
    (void)m;
    return Image{spriteFrameAddress(frame), screenAddress(y, x), (bool)(x & 1), rows};
}

// XORs the whole image onto the screen (draws it if it is not there, erases it if it is).
// `patch*` are the addresses of the original's self-modified operands, updated for bit-exactness.
void xorImage(Machine& m, Image img, uint16_t patchSprite, uint16_t patchScreen, uint16_t patchBlitter) {
    wr16(m, patchBlitter, img.shifted ? mem::LEGACY_BLIT_SHIFTED : mem::LEGACY_BLIT_ALIGNED);
    for (unsigned row = 0; row < img.rows; row++) {
        if (img.shifted) xorRowShifted(m, img.sprite, img.screen); else xorRowAligned(m, img.sprite, img.screen);
        img.sprite = (uint16_t)(img.sprite + SPRITE_ROW_BYTES);
        img.screen = nextScanline(img.screen);
    }
    wr16(m, patchSprite, img.sprite);
    wr16(m, patchScreen, img.screen);
}

// Erases the picture of the entity as it was before the move (snapshot at OLD_ENTITY), if one is on screen.
// Also clears the entity's "drawn" flag (the original 8AFC).
void eraseOldImage(Machine& m, Entity& e) {
    if (!(R(m, mem::OLD_ENTITY + mem::ENT_STATE) & mem::ENT_DRAWN)) return;
    e.state() &= ~mem::ENT_DRAWN;
    Image img = imageOf(m, R(m, mem::OLD_ENTITY + mem::ENT_Y), R(m, mem::OLD_ENTITY + mem::ENT_X),
                        R(m, mem::OLD_ENTITY + mem::ENT_FRAME), R(m, mem::OLD_ENTITY + mem::ENT_ROWS));
    xorImage(m, img, mem::SMC_OLD_SPRITE, mem::SMC_OLD_SCREEN, mem::SMC_OLD_BLITTER);
}

// Draws the entity at its current position and marks it drawn (the original 8B36).
void drawNewImage(Machine& m, Entity& e) {
    e.state() |= mem::ENT_DRAWN;
    Image img = imageOf(m, e.y(), e.x(), e.frame(), e.rows());
    xorImage(m, img, mem::SMC_NEW_SPRITE, mem::SMC_NEW_SCREEN, mem::SMC_NEW_BLITTER);
}

// What the original left behind: the row counters are consumed (0), bc = 0 and Z set.
void finishBlit(Machine& m) {
    R(m, mem::BLIT_ROWS_OLD) = 0;
    R(m, mem::BLIT_ROWS_NEW) = 0;
    m.cpu.a = 0;
    m.cpu.setBC(0);
    m.cpu.f = FZ | FP;
}

}  // namespace

// ================================================================================================
// Entity drawing
// ================================================================================================

// ix = entity.  Snapshot its 10 bytes to OLD_ENTITY, to be erased later by eraseEntity/moveEntity.
NATIVE(0x8AAA, saveEntity, "bc de hl", 300) {
    Z80& c = m.cpu;
    for (unsigned i = 0; i < mem::ENTITY_SIZE; i++) R(m, mem::OLD_ENTITY + i) = R(m, c.ix + i);
    c.setHL(c.ix + mem::ENTITY_SIZE);
    c.setDE(mem::OLD_ENTITY + mem::ENTITY_SIZE);
    c.setBC(0);
}

// ix = entity.  Draws the entity at its current position (nothing is erased).
NATIVE(0x8AB6, drawEntity, "a bc zf cf", 9500) {
    Entity e(m, m.cpu.ix);
    drawNewImage(m, e);
    finishBlit(m);
}

// ix = entity.  Erases the image recorded by saveEntity (if it was drawn) and marks the entity not drawn.
NATIVE(0x8AC0, eraseEntity, "a bc zf cf", 9580) {
    Entity e(m, m.cpu.ix);
    eraseOldImage(m, e);
    finishBlit(m);
}

// ix = entity.  Moves the picture: erases the old image (saveEntity snapshot) and draws the new one.
// The original interleaved the rows of both images (with the vertical offset between them) to cut flicker on
// the real beam; since both are XORs onto the same memory the end result is simply "xor old, xor new".
NATIVE(0x8ACA, moveEntity, "a bc zf cf", 17760) {
    Entity e(m, m.cpu.ix);
    eraseOldImage(m, e);
    drawNewImage(m, e);
    finishBlit(m);
}

// ================================================================================================
// Tile collision probes.  Used by the player/enemy movement code: carry set = path blocked.
// A tile blocks when bit 7 of its id is set.  Sprites are 12 pixels (3 tiles) wide and 24 pixels (3 tiles) tall,
// but one tile more has to be tested when the sprite is not aligned with the tile grid.
// ================================================================================================
namespace {
bool tileSolid(Machine& m, uint16_t a) { return R(m, a) & TILE_SOLID; }

// Tests the vertical strip of tiles starting at `top` (3 tiles, 4 if y is not row aligned).
bool columnBlocked(Machine& m, uint16_t top, uint8_t y) {
    bool blocked = tileSolid(m, top) || tileSolid(m, top + TILE_COLUMNS) || tileSolid(m, top + 2 * TILE_COLUMNS);
    if (blocked) return true;
    return (y & 7) && tileSolid(m, top + 3 * TILE_COLUMNS);
}
// Tests the horizontal strip starting at `left` (3 tiles, 4 if x is not column aligned).
bool rowBlocked(Machine& m, uint16_t left, uint8_t x) {
    bool blocked = tileSolid(m, left) || tileSolid(m, left + 1) || tileSolid(m, left + 2);
    if (blocked) return true;
    return (x & 3) && tileSolid(m, left + 3);
}

// Is the column left of a sprite at (y,x) solid?  Only matters when x is on a tile boundary.
bool leftBlocked(Machine& m, uint8_t y, uint8_t x) {
    if (x == 0) return true;                       // screen edge
    if (x & 3) return false;
    return columnBlocked(m, tileAddress(y, x) - 1, y);
}
bool rightBlocked(Machine& m, uint8_t y, uint8_t x) {
    if (x == 0x94) return true;                    // 148 + 12 pixels = right screen edge
    if (x & 3) return false;
    return columnBlocked(m, tileAddress(y, x) + 3, y);
}
bool upBlocked(Machine& m, uint8_t y, uint8_t x) {
    if (y == 0) return true;
    if (y & 7) return false;
    return rowBlocked(m, tileAddress(y, x) - TILE_COLUMNS, x);
}
bool downBlocked(Machine& m, uint8_t y, uint8_t x) {
    if (y == 0x78) return true;                    // 120 + 24 rows = bottom of the play field
    if (y & 7) return false;
    return rowBlocked(m, tileAddress(y, x) + 3 * TILE_COLUMNS, x);
}
void setCarry(Machine& m, bool v) { m.cpu.f = v ? 1 : 0; }
}  // namespace

// ix = entity.  Carry = it cannot go one more pixel left (tested at x and x-1, as it moves two pixels at a time).
NATIVE(0x8F5C, blockedLeft, "cf bc", 195) {
    Z80& c = m.cpu; Entity e(m, c.ix);
    uint8_t y = e.y(), x = e.x();
    bool b = leftBlocked(m, y, x);
    if (!b) { x--; b = leftBlocked(m, y, x); }
    c.b = y; c.c = x; setCarry(m, b);
}
NATIVE(0x8F82, blockedRight, "cf bc", 290) {
    Z80& c = m.cpu; Entity e(m, c.ix);
    uint8_t y = e.y(), x = e.x();
    bool b = rightBlocked(m, y, x);
    if (!b) { x++; b = rightBlocked(m, y, x); }
    c.b = y; c.c = x; setCarry(m, b);
}
NATIVE(0x8FB7, blockedUp, "cf bc", 220) {
    Z80& c = m.cpu; Entity e(m, c.ix);
    uint8_t y = e.y(), x = e.x();
    bool b = upBlocked(m, y, x);
    if (!b) { y--; b = upBlocked(m, y, x); }
    c.b = y; c.c = x; setCarry(m, b);
}
// Like the original, leaves HL at the third tile of the row below the sprite when it looked at the tiles (the player's landing
// code uses it to see whether the player landed on river tiles): HL = address of that row + 2.
NATIVE(0x8FDD, blockedDown, "cf bc hl", 245) {
    Z80& c = m.cpu; Entity e(m, c.ix);
    uint8_t y = e.y(), x = e.x();
    uint16_t hl = c.hl();
    auto probe = [&](uint8_t yy) {
        if (yy != 0x78 && (yy & 7) == 0) hl = (uint16_t)(tileAddress(yy, x) + 3 * TILE_COLUMNS + 2);
        return downBlocked(m, yy, x);
    };
    bool b = probe(y);
    if (!b) { y++; b = probe(y); }
    c.b = y; c.c = x; c.setHL(hl); setCarry(m, b);
}

// ================================================================================================
// Animated tiles
// ================================================================================================
namespace {
// XOR a 5x4 byte image (a collectible item) onto the screen at pixel row y, column x.  y = 0xFF: no item.
void xorItem(Machine& m, uint8_t y, uint8_t x) {
    if (y == 0xFF) return;
    uint16_t scr = screenAddress(y, x);
    for (unsigned row = 0; row < 4; row++) {
        for (unsigned i = 0; i < 5; i++) R(m, scr + i) ^= R(m, mem::ITEM_SPRITE + row * 5 + i);
        scr = (uint16_t)(scr + 0x0800);          // (no wrap into the next character row: the original does not either)
    }
}

// Draws one 8x8 pixel (two tile) XOR delta at screen address `scr`: 8 scan lines of 4 bytes.
// Delta layout: left tile = 16 bytes (2 per line), right tile = next 16 bytes.
void xorDelta(Machine& m, uint16_t src, uint16_t scr) {
    for (unsigned line = 0; line < 8; line++) {
        uint16_t s = (uint16_t)(src + 2 * line);     // the original only advances the low byte; deltas never cross a page
        R(m, scr) ^= R(m, s);
        R(m, scr + 1) ^= R(m, s + 1);
        R(m, scr + 2) ^= R(m, s + 16);
        R(m, scr + 3) ^= R(m, s + 17);
        scr = (uint16_t)(scr + 0x0800);
    }
}

// Applies the delta at `src` to every occurrence of tile `tile` in the tile map.
void animateTileKind(Machine& m, uint16_t src, uint8_t tile) {
    wr16(m, mem::ANIM_SRC, src);
    R(m, mem::ANIM_TILE) = tile;
    constexpr unsigned CELLS = TILE_COLUMNS * 18;
    unsigned pos = 0;                                // next cell to examine
    while (true) {
        bool found = false;
        while (pos < CELLS) {                        // `cpir`
            uint8_t v = R(m, mem::TILEMAP + pos++);
            if (v == tile) { found = true; break; }
        }
        // `ret po`: also gives up when the match was the very last cell.
        if (!found || pos == CELLS) return;
        unsigned cell = pos - 1;
        xorDelta(m, src, (uint16_t)(0xC000 + 2 * cell));
        pos++;                                       // the cell to the right belongs to the same 8 pixel wide object
        if (pos >= CELLS) return;
    }
}
}  // namespace

// Called once per frame.  A 5-step cycle (counter 4..0): step 4 advances the animation phase and flashes the
// collectible items, steps 3/2/1 apply the XOR delta of one kind of animated tile (0x02, 0x01, 0x81) everywhere.
NATIVE(0x905B, animateTiles, "", 12000) {
    uint8_t tick = R(m, mem::ANIM_TICK);
    R(m, mem::ANIM_TICK) = tick - 1;
    if (tick == 0) {
        R(m, mem::ANIM_TICK) = 4;
        R(m, mem::ANIM_PHASE) = (R(m, mem::ANIM_PHASE) + 0x20) & 0x60;
    }
    uint8_t phase = R(m, mem::ANIM_PHASE);
    switch (R(m, mem::ANIM_TICK)) {
    case 1: animateTileKind(m, mem::ANIM_DELTAS_A + phase, 0x81); break;
    case 2: animateTileKind(m, mem::ANIM_DELTAS_B + phase, 0x01); break;
    case 3: animateTileKind(m, mem::ANIM_DELTAS_C + (phase & 0x20), 0x02); break;
    case 4:
        xorItem(m, R(m, mem::ITEM1_Y), R(m, mem::ITEM1_X));
        xorItem(m, R(m, mem::ITEM2_Y), R(m, mem::ITEM2_X));
        break;
    default: break;
    }
}

// ------------------------------------------------------------------------------------------------
// Building the animation deltas (called when a room is entered)
// ------------------------------------------------------------------------------------------------
namespace {
// dst[0..31] = tiles (a, a+1) XOR tiles (b, b+1).  Tile graphics are planar: byte j of tile t is at TILESET + j*256 + t.
void tilePairDelta(Machine& m, uint16_t dst, uint8_t a, uint8_t b) {
    for (unsigned t = 0; t < 2; t++)
        for (unsigned j = 0; j < 16; j++)
            R(m, dst + t * 16 + j) = R(m, mem::TILESET + j * 256 + (uint8_t)(a + t)) ^ R(m, mem::TILESET + j * 256 + (uint8_t)(b + t));
}
// A looping animation of `frames` double-width tiles starting at tile `first` (every 2nd tile id is a new frame):
// delta[i] = frame[i+1] XOR frame[i]; the last one wraps to frame 0.  Returns the end of the delta buffer.
uint16_t buildDeltas(Machine& m, uint16_t dst, uint8_t first, unsigned frames) {
    for (unsigned i = 0; i < frames; i++) {
        uint8_t cur = first + 2 * i, next = first + 2 * ((i + 1) % frames);
        tilePairDelta(m, dst, next, cur);
        dst += 32;
    }
    return dst;
}
}  // namespace

// Precomputes the XOR deltas between consecutive animation tiles (frames 0x1E.. -> 6880, 0x46.. -> 6900, 0x6E.. -> 6980).
NATIVE(0x901E, buildAnimationDeltas, "bc", 24730) {
    uint16_t end = buildDeltas(m, mem::ANIM_DELTAS_A, 0x1E, 4);
    end = buildDeltas(m, mem::ANIM_DELTAS_B, 0x46, 4);
    end = buildDeltas(m, mem::ANIM_DELTAS_C, 0x6E, 2);
    m.cpu.setBC(end);
}

// ================================================================================================
// Enemy slots / remnants
// ================================================================================================

// -> ix = first free enemy slot, carry set; carry clear (ix past the table) when all 8 are in use.
NATIVE(0x913E, findFreeEnemySlot, "a cf ix", 70) {
    Z80& c = m.cpu;
    c.ix = mem::ENEMY_SLOTS;
    c.a = 0;
    for (unsigned i = 0; i < mem::ENEMY_SLOT_COUNT; i++) {
        if (R(m, c.ix) == 0xFF) { setCarry(m, true); return; }
        c.ix += mem::ENTITY_SIZE;
    }
    setCarry(m, false);
}

namespace {
// Address of the first free remnant record, or 0 when none is free.
uint16_t findFreeRemnant(Machine& m) {
    for (unsigned i = 0; i < mem::REMNANT_COUNT; i++) {
        uint16_t rec = mem::REMNANTS + 3 * i;
        if (R(m, rec + 2) == 0xFF) return rec;
    }
    return 0;
}
}  // namespace

// -> hl = first free remnant record {y,x,frame}, carry set; carry clear (hl past the table) when all are used.
NATIVE(0x9153, findFreeRemnantRecord, "a cf hl", 165) {
    Z80& c = m.cpu;
    uint16_t rec = findFreeRemnant(m);
    c.a = 0;
    c.setHL(rec ? rec : mem::REMNANTS + 3 * mem::REMNANT_COUNT);
    setCarry(m, rec != 0);
}

// ix = enemy slot.  A slot in use is freed (state 0xFF); its last picture is kept as a static "remnant" record
// (y adjusted so that the feet stay put for sprites shorter than 24 rows) if one is free.
NATIVE(0x9168, enemyToRemnant, "hl", 60) {
    Z80& c = m.cpu;
    Entity e(m, c.ix);
    if (e.state() == 0xFF) return;
    uint16_t rec = findFreeRemnant(m);
    e.state() = 0xFF;
    if (!rec) { c.setHL(mem::REMNANTS + 3 * mem::REMNANT_COUNT); return; }
    uint8_t dropRows = (uint8_t)(0x18 - e.rows());
    if (e.at(mem::ENT_ALT_FRAME) & 0x80) {
        R(m, rec) = e.y() - dropRows; R(m, rec + 1) = e.x(); R(m, rec + 2) = e.frame();
    } else {
        R(m, rec) = e.at(mem::ENT_ALT_Y) - dropRows; R(m, rec + 1) = e.at(mem::ENT_ALT_X); R(m, rec + 2) = e.at(mem::ENT_ALT_FRAME);
    }
    c.setHL(rec + 2);
}

// ================================================================================================
// Score / HUD digits
// ================================================================================================
namespace {
struct Bcd { uint8_t value; bool carry; };
// ADC + DAA on two packed BCD bytes.
Bcd bcdAdd(uint8_t a, uint8_t b, bool carryIn) {
    unsigned sum = a + b + carryIn;
    bool carry = sum > 0xFF, half = ((a & 15) + (b & 15) + carryIn) > 15;
    uint8_t r = (uint8_t)sum, adjust = 0;
    if (half || (r & 15) > 9) adjust |= 0x06;
    if (carry || r > 0x99) { adjust |= 0x60; carry = true; }
    return Bcd{(uint8_t)(r + adjust), carry};
}
// Prints the two BCD digits of byte (hl) with the text printer (8CFC prints the char in a at screen cell de and advances),
// then advances hl.  The original rotates the nibbles through (hl) and a with `rld` and restores the byte.
void printBcdByte(Machine& m) {
    Z80& c = m.cpu;
    auto rld = [&] {
        uint8_t& mem = R(m, c.hl());
        uint8_t t = mem;
        mem = (uint8_t)((t << 4) | (c.a & 0x0F));
        c.a = (c.a & 0xF0) | (t >> 4);
    };
    c.a = '0';
    rld();
    m.call(0x8CFC);
    rld();
    m.call(0x8CFC);
    rld();
    c.setHL(c.hl() + 1);
}
}  // namespace

// hl = BCD byte, de = text position, 9374 = pen mask: prints two digits.
NATIVE(0x91E8, printBcdPair, "a hl de", 1915) { printBcdByte(m); }

// Prints the collectible counter (BCD at 684D) at text position 0798 in the "F0" pen colour.
NATIVE(0x91DD, printItemCount, "a hl de", 1955) {
    m.cpu.setDE(0x0798);
    m.cpu.setHL(mem::ROOMREC + 0x4D);
    R(m, mem::TEXT_MASK) = 0xF0;
    printBcdByte(m);
}

// bc = points in BCD (b = hundreds/thousands pair, c = ones/tens pair).  Adds them to the score unless score
// counting is disabled (bit 7 of 6857), then redraws the 6 digit score at text position 07B2.
NATIVE(0x91B2, addScore, "a hl de", 5950) {
    Z80& c = m.cpu;
    if (!(R(m, mem::ROOMREC + 0x57) & 0x80)) {
        Bcd lo = bcdAdd(R(m, mem::SCORE + 2), c.c, false);
        Bcd mid = bcdAdd(R(m, mem::SCORE + 1), c.b, lo.carry);
        Bcd hi = bcdAdd(R(m, mem::SCORE), 0, mid.carry);
        R(m, mem::SCORE + 2) = lo.value; R(m, mem::SCORE + 1) = mid.value; R(m, mem::SCORE) = hi.value;
    }
    c.setHL(mem::SCORE);
    c.setDE(0x07B2);
    R(m, mem::TEXT_MASK) = 0x0F;
    for (int i = 0; i < 3; i++) printBcdByte(m);
}
