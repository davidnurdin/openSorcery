// Screen / tile-map address helpers (original routines at 8C30, 8C4C, 8C64).
#include "native.h"
using namespace lift;

// (b = pixel row, c = pixel column in mode-0 pixels)  ->  hl = CPC screen RAM address.
// Callers do not look at the flags the original leaves behind.
NATIVE(0x8C30, screenAddr, "a hl", 140) {
    Z80& c = m.cpu;
    uint8_t x = c.c >> 1;                                   // 2 pixels per byte
    c.a = ((c.b << 3) & 0x38) | 0xC0;                       // line within the character cell -> bits 11-13
    c.setHL((c.b & 0xF8) * 10 + ((c.a << 8) | x));         // 80 bytes per character row
}

// (b = pixel row, c = pixel column) -> hl = address of the tile in the 40x18 tile map at 6C90
NATIVE(0x8C4C, tileMapAddr, "a hl", 120) {
    Z80& c = m.cpu;
    c.a = c.b & 0xF8;
    c.setHL(c.a * 5 + (c.c >> 2) + 0x6C90);                 // row*40 + col
}

// a = sprite frame number -> hl = address of its 144 bytes (24 rows x 6 bytes) at 0A00
NATIVE(0x8C64, spriteAddr, "hl de", 130) {
    Z80& c = m.cpu;
    c.setDE(0x0A00);
    c.setHL(c.a * 144 + 0x0A00);
}
