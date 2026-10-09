// Text format of a room (resources/rooms/roomNN.txt): the 40x18 tile map and the 64-byte room record.
#pragma once
#include <cstdint>
#include <string>
#include <vector>

struct RoomDef {
    int number = 0;
    uint8_t map[720] = {};        // tile ids, row by row (tile id = row*16+column in graphics/tiles1.png / tiles2.png)
    uint8_t rec[64] = {};         // room record copied to 6800 when the room is entered (doors, objects, enemies, ...)
};

std::string formatRoomText(const RoomDef& r);
bool parseRoomText(const std::string& text, RoomDef& out, std::string* err);
// The 512 byte disc sector the original stored: "DC ", room number, RLE tile map ("00 n" = n zeros, FF = end).
bool encodeRoomSector(const RoomDef& r, uint8_t* sector512, std::string* err);
bool decodeRoomSector(const uint8_t* sector512, const uint8_t* record64, RoomDef& out);
