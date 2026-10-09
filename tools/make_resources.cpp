// Builds the resource folder of the native port from the original disk image:
//   make_resources Sorcery.dsk resources/
// Files are unscrambled; graphics become indexed PNGs (pixel = CPC pen number). The result is checked by loading it back
// and comparing it byte for byte with the unscrambled disk files.
#include "../common/dsk.h"
#include "../common/resources.h"
#include "../common/roomtext.h"
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <string>
#include <sys/stat.h>

static uint8_t decryptByte(uint8_t b) { uint8_t a = (uint8_t)(-b); a = (a >> 1) | (a << 7); return a - 0x25; }
static void mk(const std::string& d) { mkdir(d.c_str(), 0755); }
static bool writeFile(const std::string& p, const std::vector<uint8_t>& v) { FILE* f = fopen(p.c_str(), "wb"); if (!f) return false; fwrite(v.data(), 1, v.size(), f); fclose(f); return true; }

int main(int argc, char** argv) {
    if (argc < 3) { fprintf(stderr, "usage: %s Sorcery.dsk out_dir\n", argv[0]); return 1; }
    DskImage dsk; std::string err;
    if (!dsk.load(argv[1], &err)) { fprintf(stderr, "%s\n", err.c_str()); return 1; }
    std::string out = argv[2];
    mk(out); mk(out + "/data"); mk(out + "/graphics"); mk(out + "/rooms");
    struct Map { const char* dskName; const char* game; };
    static const Map maps[] = {{"CONALP.BIN", "CONALP"}, {"CODE.BIN", "CODE"}, {"TISCRN.BIN", "TISCRN"}, {"SOUND.DAT", "SOUND"},
                               {"CONSET1.BIN", "CONSET1"}, {"CONSET2.BIN", "CONSET2"}, {"SPRITES1.BIN", "SPRITES1"}, {"SPRITES2.BIN", "SPRITES2"},
                               {"SMESSC.BIN", "SMESSC"}, {"MUCODE.BIN", "MUCODE"}, {"LOCDATA.BIN", "LOCDATA"}, {"STRANGE.SCN", "STRANGE"}};
    std::map<std::string, std::vector<uint8_t>> plain;
    for (const Map& m : maps) {
        const DskFile* f = dsk.find(m.dskName);
        if (!f || !f->hasHeader()) { fprintf(stderr, "missing %s\n", m.dskName); return 1; }
        std::vector<uint8_t> v(f->length());
        for (int i = 0; i < f->length(); i++) v[i] = decryptByte(f->body()[i]);
        plain[m.game] = v;
    }
    for (const Resources::Entry& e : Resources::entries()) {
        const std::vector<uint8_t>& v = plain[e.game];
        std::string path = out + "/" + e.path;
        if (e.kind == res::Kind::Raw) { if (!writeFile(path, v)) { fprintf(stderr, "cannot write %s\n", path.c_str()); return 1; } }
        else if (!writeIndexedPng(path, res::toImage(e.kind, v))) { fprintf(stderr, "cannot write %s\n", path.c_str()); return 1; }
    }
    // rooms: text files; the 80 records of LOCDATA go with them
    const std::vector<uint8_t>& loc = plain["LOCDATA"];
    for (int n = 0; n < 79; n++) {
        uint8_t buf[512];
        if (!dsk.readSector(31 + n / 9, 0xC1 + n % 9, buf)) { fprintf(stderr, "room %d missing\n", n); return 1; }
        RoomDef r;
        if (!decodeRoomSector(buf, &loc[n * 64], r) || r.number != n) { fprintf(stderr, "room %d: bad sector\n", n); return 1; }
        char name[64]; snprintf(name, sizeof name, "/rooms/room%02d.txt", n);
        std::string txt = formatRoomText(r);
        writeFile(out + name, std::vector<uint8_t>(txt.begin(), txt.end()));
    }
    for (int n = 79; n < 81; n++) {                       // cutscene sprites (T39 C8) and hall of fame (T39 C9)
        uint8_t buf[512];
        dsk.readSector(31 + n / 9, 0xC1 + n % 9, buf);
        writeFile(out + (n == 79 ? "/data/cutscene.bin" : "/data/halloffame.bin"), std::vector<uint8_t>(buf, buf + 512));
    }
    // check
    Resources r;
    if (!r.load(out, &err)) { fprintf(stderr, "reload failed: %s\n", err.c_str()); return 1; }
    int bad = 0;
    for (const Map& m : maps) {
        const std::vector<uint8_t>* v = r.file(m.game);
        if (std::string(m.game) == "LOCDATA") { if (!v || memcmp(v->data(), plain["LOCDATA"].data(), 79 * 64)) { fprintf(stderr, "MISMATCH LOCDATA\n"); bad++; } continue; }
        if (!v || *v != plain[m.game]) { fprintf(stderr, "MISMATCH %s\n", m.game); bad++; }
    }
    uint8_t a[512], b[512];
    for (int n = 0; n < 81; n++) {
        int t = 31 + n / 9, id = 0xC1 + n % 9;
        dsk.readSector(t, id, a);
        if (!r.readSector(t, id, b)) { fprintf(stderr, "MISMATCH sector %d/%02X\n", t, id); bad++; continue; }
        if (n < 79) { RoomDef x, y; if (!decodeRoomSector(a, &plain["LOCDATA"][n * 64], x) || !decodeRoomSector(b, &plain["LOCDATA"][n * 64], y) || memcmp(x.map, y.map, 720)) { fprintf(stderr, "MISMATCH room %d\n", n); bad++; } }
        else if (memcmp(a, b, 512)) { fprintf(stderr, "MISMATCH sector %d/%02X\n", t, id); bad++; }
    }
    if (bad) printf("round trip FAILED (%d)\n", bad);
    else printf("resources written to %s, round trip OK\n", out.c_str());
    return bad ? 1 : 0;
}
