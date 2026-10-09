// Game resources of the native port, read from a folder (no .dsk needed):
//   data/      CODE.bin MUCODE.bin SOUND.dat SMESSC.bin TISCRN.bin   plain (unscrambled) data of the original files
//   graphics/  sprites1.png sprites2.png tiles1.png tiles2.png font.png title.png  indexed PNGs, pixel value = CPC pen number (editable)
//   rooms/     room00.txt .. room78.txt                                           text files: tile map + room record (see docs / the files)
//   data/cutscene.bin, data/halloffame.bin                                         the two other disc sectors used by the game
//   halloffame.sav (written by the game)                                           the saved all-time hall of fame (overrides data/halloffame.bin)
#pragma once
#include "png_io.h"
#include <cstdint>
#include <map>
#include <string>
#include <vector>

namespace res {
enum class Kind { Raw, Sprites, Tiles, Font, Screen };
// CPC pixel formats <-> indexed images (see resources.cpp for the layouts)
Image toImage(Kind k, const std::vector<uint8_t>& data);
bool fromImage(Kind k, const Image& img, std::vector<uint8_t>& out, size_t length, std::string* err);
const std::vector<uint32_t>& palette(Kind k);   // preview palette of the PNGs
}

class Resources {
public:
    bool load(const std::string& dir, std::string* err);
    const std::vector<uint8_t>* file(const std::string& gameName) const;   // plain bytes of a game file ("CODE", "SPRITES1", "STRANGE.SCN" ...)
    bool readSector(int track, int id, uint8_t* out512);
    bool writeSector(int track, int id, const uint8_t* in512);
    const std::string& dir() const { return root; }
    // Looks for the resource folder: explicit path, $SORCERY_RES, ./resources, next to the executable, ../resources ...
    static std::string find(const std::string& explicitDir, const char* argv0);

    struct Entry { const char* game; const char* path; res::Kind kind; size_t length; };
    static const std::vector<Entry>& entries();
private:
    std::string root;
    std::map<std::string, std::vector<uint8_t>> files;   // plain game files (LOCDATA is built from the room texts)
    std::vector<std::vector<uint8_t>> roomSectors;       // 79 encoded room sectors
    std::string sectorPath(int track, int id) const;
};
