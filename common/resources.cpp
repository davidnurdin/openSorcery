#include "resources.h"
#include "roomtext.h"
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <sys/stat.h>

namespace res {
namespace {
uint32_t hwRgb(int hw) {   // CPC gate-array colour number -> 0xRRGGBB
    static const uint8_t ink2hw[27] = {0x14,0x04,0x15,0x1C,0x18,0x1D,0x0C,0x05,0x0D,0x16,0x06,0x17,0x1E,0x00,0x1F,0x0E,0x07,0x0F,0x12,0x02,0x13,0x1A,0x19,0x1B,0x0A,0x03,0x0B};
    static const int lv[3] = {0, 128, 255};
    hw &= 31;
    if (hw == 1) hw = 0; else if (hw == 8) hw = 5; else if (hw == 9) hw = 3; else if (hw == 0x10) hw = 4; else if (hw == 0x11) hw = 2;
    for (int i = 0; i < 27; i++) if (ink2hw[i] == hw) return lv[i / 9] << 8 | lv[(i / 3) % 3] << 16 | lv[i % 3];
    return 0;
}
int pen0(uint8_t b) { return (b >> 7 & 1) | (b >> 3 & 1) << 1 | (b >> 5 & 1) << 2 | (b >> 1 & 1) << 3; }          // mode 0, left pixel
int pen1(uint8_t b) { return (b >> 6 & 1) | (b >> 2 & 1) << 1 | (b >> 4 & 1) << 2 | (b & 1) << 3; }               // mode 0, right pixel
uint8_t enc0(int p0, int p1) {
    return (uint8_t)((p0 & 1) << 7 | (p0 >> 1 & 1) << 3 | (p0 >> 2 & 1) << 5 | (p0 >> 3 & 1) << 1 |
                     (p1 & 1) << 6 | (p1 >> 1 & 1) << 2 | (p1 >> 2 & 1) << 4 | (p1 >> 3 & 1));
}
int pen4(uint8_t b, int p) { return ((b >> (3 - p)) & 1) | ((b >> (7 - p)) & 1) << 1; }                           // mode 1, pixel p of 4
uint8_t enc1(const int* px) { uint8_t b = 0; for (int p = 0; p < 4; p++) b |= (uint8_t)(((px[p] & 1) << (3 - p)) | ((px[p] >> 1 & 1) << (7 - p))); return b; }

constexpr int SPR_COLS = 12, SPR_W = 12, SPR_H = 24, SPR_BYTES = 144;
}  // namespace

const std::vector<uint32_t>& palette(Kind k) {
    static std::vector<uint32_t> mode0, mode1;
    if (mode0.empty()) {
        static const int title[16] = {0x14, 0x0A, 0x0E, 0x0C, 0x13, 0x0B, 0x1F, 0x0F, 0x12, 0x06, 0x1C, 0x16, 0x03, 0x1F, 0x00, 0x1E};
        for (int h : title) {
            uint32_t c = hwRgb(h);
            for (bool dup = true; dup;) {   // keep the preview colours distinct, so that RGB edits map back to the right pen
                dup = false;
                for (uint32_t o : mode0) if (o == c) { dup = true; c = (c & ~0xFFu) | (((c & 0xFF) + 1) & 0xFF); break; }
            }
            mode0.push_back(c);
        }
        mode1 = {0x000000, 0xFFFF00, 0x00FFFF, 0xFFFFFF};
    }
    return k == Kind::Font ? mode1 : mode0;
}

// Layouts (pixel = pen number):
//  Sprites: frames of 12x24 px (24 rows x 6 bytes, mode 0), 12 frames per sheet row.
//  Tiles:   256 tiles of 4x8 px in a 16x16 grid; tile t byte j is stored at j*256+t (two bytes per pixel row).
//  Font:    glyphs of 8x8 px, 4 pens (mode 1), 16 per sheet row, 16 bytes per glyph (2 bytes per pixel row).
//  Screen:  160x200 px, mode 0, standard CPC screen memory interleave.
Image toImage(Kind k, const std::vector<uint8_t>& d) {
    Image im; im.pal = palette(k);
    auto get = [&](size_t i) { return i < d.size() ? d[i] : 0; };
    if (k == Kind::Sprites) {
        int frames = (int)d.size() / SPR_BYTES, rows = (frames + SPR_COLS - 1) / SPR_COLS;
        im.w = SPR_COLS * SPR_W; im.h = rows * SPR_H; im.pix.assign((size_t)im.w * im.h, 0);
        for (int f = 0; f < frames; f++)
            for (int y = 0; y < SPR_H; y++)
                for (int x = 0; x < 6; x++) {
                    uint8_t b = get(f * SPR_BYTES + y * 6 + x);
                    im.at(f % SPR_COLS * SPR_W + x * 2, f / SPR_COLS * SPR_H + y) = (uint8_t)pen0(b);
                    im.at(f % SPR_COLS * SPR_W + x * 2 + 1, f / SPR_COLS * SPR_H + y) = (uint8_t)pen1(b);
                }
    } else if (k == Kind::Tiles) {
        im.w = 64; im.h = 128; im.pix.assign((size_t)im.w * im.h, 0);
        for (int t = 0; t < 256; t++)
            for (int y = 0; y < 8; y++) {
                uint8_t a = get((2 * y) * 256 + t), b = get((2 * y + 1) * 256 + t);
                int ox = t % 16 * 4, oy = t / 16 * 8 + y;
                im.at(ox, oy) = (uint8_t)pen0(a); im.at(ox + 1, oy) = (uint8_t)pen1(a);
                im.at(ox + 2, oy) = (uint8_t)pen0(b); im.at(ox + 3, oy) = (uint8_t)pen1(b);
            }
    } else if (k == Kind::Font) {
        int glyphs = (int)d.size() / 16, rows = (glyphs + 15) / 16;
        im.w = 128; im.h = rows * 8; im.pix.assign((size_t)im.w * im.h, 0);
        for (int g = 0; g < glyphs; g++)
            for (int y = 0; y < 8; y++)
                for (int h = 0; h < 2; h++) {
                    uint8_t b = get(g * 16 + y * 2 + h);
                    for (int p = 0; p < 4; p++) im.at(g % 16 * 8 + h * 4 + p, g / 16 * 8 + y) = (uint8_t)pen4(b, p);
                }
    } else if (k == Kind::Screen) {
        im.w = 160; im.h = 200; im.pix.assign((size_t)im.w * im.h, 0);
        for (int y = 0; y < 200; y++)
            for (int x = 0; x < 80; x++) {
                uint8_t b = get((y >> 3) * 80 + (y & 7) * 0x800 + x);
                im.at(x * 2, y) = (uint8_t)pen0(b); im.at(x * 2 + 1, y) = (uint8_t)pen1(b);
            }
    }
    return im;
}

bool fromImage(Kind k, const Image& im, std::vector<uint8_t>& out, size_t length, std::string* err) {
    auto fail = [&](const char* m) { if (err) *err = m; return false; };
    out.assign(length, 0);
    auto put = [&](size_t i, uint8_t v) { if (i < out.size()) out[i] = v; };
    if (k == Kind::Sprites) {
        int frames = (int)length / SPR_BYTES;
        if (im.w != SPR_COLS * SPR_W || im.h < ((frames + SPR_COLS - 1) / SPR_COLS) * SPR_H) return fail("sprite sheet must be 144 pixels wide and 24 pixels high per row of 12 frames");
        for (int f = 0; f < frames; f++)
            for (int y = 0; y < SPR_H; y++)
                for (int x = 0; x < 6; x++)
                    put(f * SPR_BYTES + y * 6 + x, enc0(im.at(f % SPR_COLS * SPR_W + x * 2, f / SPR_COLS * SPR_H + y),
                                                       im.at(f % SPR_COLS * SPR_W + x * 2 + 1, f / SPR_COLS * SPR_H + y)));
    } else if (k == Kind::Tiles) {
        if (im.w != 64 || im.h != 128) return fail("tile sheet must be 64x128 pixels (16x16 tiles of 4x8)");
        for (int t = 0; t < 256; t++)
            for (int y = 0; y < 8; y++) {
                int ox = t % 16 * 4, oy = t / 16 * 8 + y;
                put((2 * y) * 256 + t, enc0(im.at(ox, oy), im.at(ox + 1, oy)));
                put((2 * y + 1) * 256 + t, enc0(im.at(ox + 2, oy), im.at(ox + 3, oy)));
            }
    } else if (k == Kind::Font) {
        int glyphs = (int)length / 16;
        if (im.w != 128 || im.h < ((glyphs + 15) / 16) * 8) return fail("font sheet must be 128 pixels wide, 8 pixels high per row of 16 glyphs");
        for (int g = 0; g < glyphs; g++)
            for (int y = 0; y < 8; y++)
                for (int h = 0; h < 2; h++) {
                    int px[4];
                    for (int p = 0; p < 4; p++) px[p] = im.at(g % 16 * 8 + h * 4 + p, g / 16 * 8 + y);
                    put(g * 16 + y * 2 + h, enc1(px));
                }
    } else if (k == Kind::Screen) {
        if (im.w != 160 || im.h != 200) return fail("screen picture must be 160x200 pixels");
        for (int y = 0; y < 200; y++)
            for (int x = 0; x < 80; x++)
                put((y >> 3) * 80 + (y & 7) * 0x800 + x, enc0(im.at(x * 2, y), im.at(x * 2 + 1, y)));
    }
    return true;
}
}  // namespace res

const std::vector<Resources::Entry>& Resources::entries() {
    using res::Kind;
    static const std::vector<Entry> e = {
        {"CONALP", "graphics/font.png", Kind::Font, 0x900},
        {"CODE", "data/CODE.bin", Kind::Raw, 0x314A},
        {"TISCRN", "data/TISCRN.bin", Kind::Raw, 0x2D0},
        {"SOUND", "data/SOUND.dat", Kind::Raw, 0x287},
        {"CONSET1", "graphics/tiles1.png", Kind::Tiles, 0x1000},
        {"CONSET2", "graphics/tiles2.png", Kind::Tiles, 0x1000},
        {"SPRITES1", "graphics/sprites1.png", Kind::Sprites, 0x3600},
        {"SPRITES2", "graphics/sprites2.png", Kind::Sprites, 0x3600},
        {"SMESSC", "data/SMESSC.bin", Kind::Raw, 0x114A},
        {"MUCODE", "data/MUCODE.bin", Kind::Raw, 0x41B},
        {"STRANGE", "graphics/title.png", Kind::Screen, 0x3FFF},
    };
    return e;
}

static bool exists(const std::string& p) { struct stat s; return stat(p.c_str(), &s) == 0; }

std::string Resources::find(const std::string& explicitDir, const char* argv0) {
    std::vector<std::string> cand;
    if (!explicitDir.empty()) cand.push_back(explicitDir);
    if (const char* e = getenv("SORCERY_RES")) cand.push_back(e);
    cand.push_back("resources");
    std::string exe = argv0 ? argv0 : "";
    size_t sl = exe.rfind('/');
    std::string base = sl == std::string::npos ? "." : exe.substr(0, sl);
    cand.push_back(base + "/resources");
    cand.push_back(base + "/../resources");
    cand.push_back("sorcery/resources");
    cand.push_back("public/sorcery/resources");
    for (auto& c : cand) if (exists(c + "/data/CODE.bin")) return c;
    return "";
}

bool Resources::load(const std::string& dir, std::string* err) {
    root = dir;
    files.clear();
    roomSectors.assign(79, std::vector<uint8_t>(512, 0));
    std::vector<uint8_t> loc(0x1400, 0xFF);                                   // LOCDATA: 80 records of 64 bytes, the last one unused
    for (int n = 0; n < 79; n++) {
        char nm[64]; snprintf(nm, sizeof nm, "/rooms/room%02d.txt", n);
        FILE* f = fopen((dir + nm).c_str(), "rb");
        if (!f) { if (err) *err = std::string("missing resource ") + dir + nm; return false; }
        std::string text; char buf[4096]; size_t k;
        while ((k = fread(buf, 1, sizeof buf, f)) > 0) text.append(buf, k);
        fclose(f);
        RoomDef r; std::string msg;
        if (!parseRoomText(text, r, &msg)) { if (err) *err = dir + nm + ": " + msg; return false; }
        if (r.number != n) { if (err) *err = dir + nm + ": 'room' number does not match the file name"; return false; }
        if (!encodeRoomSector(r, roomSectors[n].data(), &msg)) { if (err) *err = dir + nm + ": " + msg; return false; }
        memcpy(&loc[n * 64], r.rec, 64);
    }
    files["LOCDATA"] = std::move(loc);
    for (const Entry& e : entries()) {
        std::string path = dir + "/" + e.path;
        std::vector<uint8_t> bytes;
        if (e.kind == res::Kind::Raw) {
            FILE* f = fopen(path.c_str(), "rb");
            if (!f) { if (err) *err = "missing resource " + path; return false; }
            uint8_t buf[65536]; size_t n;
            while ((n = fread(buf, 1, sizeof buf, f)) > 0) bytes.insert(bytes.end(), buf, buf + n);
            fclose(f);
            if (bytes.size() < e.length) { if (err) *err = path + ": file too short"; return false; }
            bytes.resize(e.length);
        } else {
            Image im; std::string msg;
            if (!readPng(path, im, res::palette(e.kind), &msg) || !res::fromImage(e.kind, im, bytes, e.length, &msg)) { if (err) *err = path + ": " + msg; return false; }
        }
        files[e.game] = std::move(bytes);
    }
    return true;
}

const std::vector<uint8_t>* Resources::file(const std::string& name) const {
    std::string n;
    for (char c : name) n += (char)toupper((unsigned char)c);
    size_t dot = n.find('.');
    if (dot != std::string::npos) n = n.substr(0, dot);
    auto it = files.find(n);
    return it == files.end() ? nullptr : &it->second;
}

std::string Resources::sectorPath(int track, int id) const {
    if (track == 39 && id == 0xC8) return root + "/data/cutscene.bin";
    if (track == 39 && id == 0xC9) return root + "/data/halloffame.bin";
    return "";
}

bool Resources::readSector(int track, int id, uint8_t* out) {
    int room = (track - 31) * 9 + (id - 0xC1);
    if (track >= 31 && track <= 39 && id >= 0xC1 && id <= 0xC9 && room >= 0 && room < 79) {   // a room: built from its text file
        memcpy(out, roomSectors[room].data(), 512);
        return true;
    }
    std::string p = sectorPath(track, id);
    if (p.empty()) return false;
    if (track == 39 && id == 0xC9 && exists(root + "/halloffame.sav")) p = root + "/halloffame.sav";
    FILE* f = fopen(p.c_str(), "rb");
    if (!f) return false;
    memset(out, 0, 512);
    size_t n = fread(out, 1, 512, f);
    fclose(f);
    return n > 0;
}

bool Resources::writeSector(int track, int id, const uint8_t* in) {
    if (!(track == 39 && id == 0xC9)) return false;          // only the hall of fame is ever written
    FILE* f = fopen((root + "/halloffame.sav").c_str(), "wb");
    if (!f) return true;                                      // read-only folder: the game goes on, the table is just not kept
    fwrite(in, 1, 512, f);
    fclose(f);
    return true;
}
