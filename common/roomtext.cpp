#include "roomtext.h"
#include <cctype>
#include <cstdio>
#include <cstring>
#include <sstream>

namespace {
struct Field { int off, len; const char* note; };
// What is known about the 64 byte record (see docs/port_player.md, docs/port_enemy2.md)
const Field FIELDS[] = {
    {0x00, 2, "text number of the room name (line 1 of the status area), ?"},
    {0x02, 10, "entity 1: state (FF = none), y, x, frame, ... - the 2 permanent entities of the room"},
    {0x0C, 10, "entity 2"},
    {0x16, 4, "door in the left wall 1: y, x, destination room, flags (bits 0-5 lock type 0-6, bit 6/7 = which door/side at the destination)"},
    {0x1A, 4, "door in the left wall 2"},
    {0x1E, 4, "door in the right wall 1"},
    {0x22, 4, "door in the right wall 2"},
    {0x26, 3, "object A: y, x, ? (FF = none)"},
    {0x29, 3, "object B"},
    {0x2C, 3, "object C"},
    {0x2F, 3, "object D"},
    {0x32, 2, "pickup: y (FF = none), x"},
    {0x34, 12, "other room data (not yet understood)"},
};
}  // namespace

std::string formatRoomText(const RoomDef& r) {
    std::ostringstream o;
    char b[256];
    o << "; Sorcery+ room " << r.number << "\n"
      << "; map: 40 x 18 tiles, hex tile ids (tile id = row*16+column of the tile sheets in graphics/tiles1.png / tiles2.png), '..' = 00 = empty.\n"
      << "; The tile classes used by the game: ids 00 and above DC = empty, 1E/1F, 46/47, 6E/6F = animated hazards, all other ids are solid.\n"
      << "room " << r.number << "\nmap\n";
    for (int y = 0; y < 18; y++) {
        for (int x = 0; x < 40; x++) {
            uint8_t t = r.map[y * 40 + x];
            if (t == 0) o << ".."; else { snprintf(b, sizeof b, "%02x", t); o << b; }
            o << (x == 39 ? "\n" : " ");
        }
    }
    o << "end\n; room record: 64 bytes in hex, grouped by field (the text after ';' is only a note)\nrecord\n";
    for (const Field& f : FIELDS) {
        snprintf(b, sizeof b, "%02x:", f.off);
        o << b;
        for (int i = 0; i < f.len; i++) { snprintf(b, sizeof b, " %02x", r.rec[f.off + i]); o << b; }
        for (int i = f.len; i < 12; i++) o << "   ";
        o << "   ; " << f.note << "\n";
    }
    o << "end\n";
    return o.str();
}

static bool isHex(char c) { return isxdigit((unsigned char)c) != 0; }

bool parseRoomText(const std::string& text, RoomDef& out, std::string* err) {
    auto fail = [&](const std::string& m) { if (err) *err = m; return false; };
    std::istringstream in(text);
    std::string line, section;
    int nmap = 0, nrec = 0;
    bool haveRoom = false;
    out = RoomDef();
    int lineNo = 0;
    while (std::getline(in, line)) {
        lineNo++;
        size_t sc = line.find(';');
        if (sc != std::string::npos) line.erase(sc);
        std::istringstream ls(line);
        std::string tok;
        std::vector<std::string> toks;
        while (ls >> tok) toks.push_back(tok);
        if (toks.empty()) continue;
        if (toks[0] == "room" && toks.size() == 2) { out.number = atoi(toks[1].c_str()); haveRoom = true; continue; }
        if (toks.size() == 1 && (toks[0] == "map" || toks[0] == "record")) { section = toks[0]; continue; }
        if (toks.size() == 1 && toks[0] == "end") { section.clear(); continue; }
        if (section.empty()) return fail("line " + std::to_string(lineNo) + ": unexpected text outside map/record");
        for (std::string t : toks) {
            if (section == "record" && t.back() == ':') continue;             // field label
            if (section == "map") {
                int v;
                if (t == ".." || t == "--") v = 0;
                else if (t.size() == 2 && isHex(t[0]) && isHex(t[1])) v = (int)strtol(t.c_str(), nullptr, 16);
                else return fail("line " + std::to_string(lineNo) + ": bad tile '" + t + "' (use two hex digits or '..')");
                if (nmap >= 720) return fail("map has more than 720 tiles");
                if (v == 0xFF) return fail("tile id FF is reserved (end marker of the original format)");
                out.map[nmap++] = (uint8_t)v;
            } else {
                if (t.size() != 2 || !isHex(t[0]) || !isHex(t[1])) return fail("line " + std::to_string(lineNo) + ": bad byte '" + t + "'");
                if (nrec >= 64) return fail("record has more than 64 bytes");
                out.rec[nrec++] = (uint8_t)strtol(t.c_str(), nullptr, 16);
            }
        }
    }
    if (!haveRoom) return fail("missing 'room N' line");
    if (nmap != 720) return fail("map must have 40 x 18 = 720 tiles, found " + std::to_string(nmap));
    if (nrec != 64) return fail("record must have 64 bytes, found " + std::to_string(nrec));
    return true;
}

bool encodeRoomSector(const RoomDef& r, uint8_t* s, std::string* err) {
    memset(s, 0, 512);
    s[0] = 'D'; s[1] = 'C'; s[2] = ' '; s[3] = (uint8_t)r.number;
    int p = 4;
    for (int i = 0; i < 720;) {
        if (r.map[i] != 0) { if (p >= 511) goto toolong; s[p++] = r.map[i++]; continue; }
        int n = 0;
        while (i + n < 720 && r.map[i + n] == 0 && n < 256) n++;
        if (p + 2 >= 511) goto toolong;
        s[p++] = 0; s[p++] = (uint8_t)(n == 256 ? 0 : n);
        i += n;
    }
    if (p >= 512) goto toolong;
    s[p] = 0xFF;
    return true;
toolong:
    if (err) *err = "room " + std::to_string(r.number) + ": the map is too complex for one 512 byte sector (too many different neighbouring tiles)";
    return false;
}

bool decodeRoomSector(const uint8_t* s, const uint8_t* rec, RoomDef& out) {
    out = RoomDef();
    if (s[0] != 'D' || s[1] != 'C' || s[2] != ' ') return false;
    out.number = s[3];
    int i = 4, n = 0;
    while (i < 512 && s[i] != 0xFF) {
        if (s[i] == 0) { i++; int c = s[i] ? s[i] : 256; while (c-- && n < 720) out.map[n++] = 0; }
        else if (n < 720) out.map[n++] = s[i];
        i++;
    }
    memcpy(out.rec, rec, 64);
    return n == 720;
}
