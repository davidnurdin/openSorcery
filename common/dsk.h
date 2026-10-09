// Extended/standard CPCEMU .DSK image + CP/M directory + AMSDOS header handling.
#pragma once
#include <cstdint>
#include <map>
#include <string>
#include <vector>

struct DskFile {
    std::string name;            // "CODE.BIN" (upper case, 8.3, no padding)
    std::vector<uint8_t> data;   // raw bytes including the 128 byte AMSDOS header
    bool hasHeader() const;
    uint8_t type() const { return data.size() > 18 ? data[18] : 0; }
    uint16_t loadAddr() const { return data[21] | data[22] << 8; }
    uint16_t length() const { return data[24] | data[25] << 8; }   // logical length from header
    uint16_t entry() const { return data[26] | data[27] << 8; }
    const uint8_t* body() const { return data.data() + 128; }
};

class DskImage {
public:
    bool load(const std::string& path, std::string* err = nullptr);
    bool save(const std::string& path) const;
    // Physical sector access by (track, sector ID).
    bool readSector(int track, int sectorId, uint8_t* out512) const;
    bool writeSector(int track, int sectorId, const uint8_t* in512);
    // CP/M files (data format, 1 KB blocks, directory in blocks 0-1).
    std::vector<DskFile> files() const;
    const DskFile* find(const std::string& name) const;   // exact "NAME.EXT" or bare "NAME"
    std::vector<uint8_t> linearImage() const;             // all sectors in track/ID order
private:
    struct Sector { int c, h, r, n; size_t off; size_t len; };
    std::vector<uint8_t> raw;
    std::map<int, std::vector<Sector>> tracks;
    mutable std::vector<DskFile> cache;
    mutable bool cached = false;
};
