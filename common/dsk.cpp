#include "dsk.h"
#include <algorithm>
#include <cstdio>
#include <cstring>

bool DskFile::hasHeader() const {
    if (data.size() < 128) return false;
    unsigned sum = 0;
    for (int i = 0; i < 67; i++) sum += data[i];
    return (sum & 0xFFFF) == (unsigned)(data[67] | data[68] << 8);
}

bool DskImage::load(const std::string& path, std::string* err) {
    FILE* fp = fopen(path.c_str(), "rb");
    if (!fp) { if (err) *err = "cannot open " + path; return false; }
    fseek(fp, 0, SEEK_END);
    long n = ftell(fp);
    fseek(fp, 0, SEEK_SET);
    raw.resize(n);
    if (fread(raw.data(), 1, n, fp) != (size_t)n) { fclose(fp); if (err) *err = "short read"; return false; }
    fclose(fp);
    tracks.clear();
    cached = false;
    if (n < 0x100) { if (err) *err = "too small"; return false; }
    bool ext = memcmp(raw.data(), "EXTENDED", 8) == 0;
    bool std_ = memcmp(raw.data(), "MV - CPC", 8) == 0;
    if (!ext && !std_) { if (err) *err = "not a DSK image"; return false; }
    int nt = raw[0x30], ns = raw[0x31];
    size_t off = 0x100;
    for (int t = 0; t < nt; t++) {
        for (int s = 0; s < ns; s++) {
            size_t tsz = ext ? raw[0x34 + t * ns + s] * 256u : (raw[0x32] | raw[0x33] << 8);
            if (tsz == 0) continue;
            if (off + 0x100 > raw.size() || memcmp(&raw[off], "Track-Info", 10) != 0) {
                if (err) *err = "bad track header";
                return false;
            }
            int nsec = raw[off + 0x15];
            size_t p = off + 0x100;
            for (int i = 0; i < nsec; i++) {
                const uint8_t* e = &raw[off + 0x18 + i * 8];
                Sector sc;
                sc.c = e[0]; sc.h = e[1]; sc.r = e[2]; sc.n = e[3];
                sc.len = e[6] | e[7] << 8;
                if (!sc.len) sc.len = 128u << sc.n;
                sc.off = p;
                p += sc.len;
                tracks[t].push_back(sc);
            }
            off += tsz;
        }
    }
    return true;
}

bool DskImage::save(const std::string& path) const {
    FILE* fp = fopen(path.c_str(), "wb");
    if (!fp) return false;
    fwrite(raw.data(), 1, raw.size(), fp);
    fclose(fp);
    return true;
}

bool DskImage::readSector(int track, int id, uint8_t* out) const {
    auto it = tracks.find(track);
    if (it == tracks.end()) return false;
    for (auto& s : it->second)
        if (s.r == id) { memcpy(out, &raw[s.off], std::min<size_t>(s.len, 512)); return true; }
    return false;
}

bool DskImage::writeSector(int track, int id, const uint8_t* in) {
    auto it = tracks.find(track);
    if (it == tracks.end()) return false;
    for (auto& s : it->second)
        if (s.r == id) { memcpy(&raw[s.off], in, std::min<size_t>(s.len, 512)); cached = false; return true; }
    return false;
}

std::vector<uint8_t> DskImage::linearImage() const {
    std::vector<uint8_t> img;
    for (auto& t : tracks) {
        std::vector<Sector> secs = t.second;
        std::sort(secs.begin(), secs.end(), [](const Sector& a, const Sector& b) { return a.r < b.r; });
        for (auto& s : secs) img.insert(img.end(), raw.begin() + s.off, raw.begin() + s.off + s.len);
    }
    return img;
}

std::vector<DskFile> DskImage::files() const {
    if (cached) return cache;
    cache.clear();
    std::vector<uint8_t> img = linearImage();
    if (img.size() < 2048) return cache;
    std::map<std::string, DskFile> byName;
    std::vector<std::string> order;
    // collect extents per file, sorted by extent number
    struct Ext { int ex; int rc; std::vector<int> blocks; };
    std::map<std::string, std::vector<Ext>> exts;
    for (int i = 0; i < 2048; i += 32) {
        const uint8_t* e = &img[i];
        if (e[0] == 0xE5) continue;
        if (e[0] != 0) continue;   // user 0 only
        std::string nm, ex;
        for (int k = 1; k < 9; k++) nm += (char)(e[k] & 0x7F);
        for (int k = 9; k < 12; k++) ex += (char)(e[k] & 0x7F);
        while (!nm.empty() && nm.back() == ' ') nm.pop_back();
        while (!ex.empty() && ex.back() == ' ') ex.pop_back();
        if (nm.empty() || (unsigned char)nm[0] < 0x20) continue;   // "hidden" filler entries
        std::string full = ex.empty() ? nm : nm + "." + ex;
        Ext x; x.ex = e[12] | (e[14] & 0x3F) << 5; x.rc = e[15];
        for (int k = 16; k < 32; k++) if (e[k]) x.blocks.push_back(e[k]);
        if (!exts.count(full)) order.push_back(full);
        exts[full].push_back(x);
    }
    for (auto& nm : order) {
        auto& v = exts[nm];
        std::sort(v.begin(), v.end(), [](const Ext& a, const Ext& b) { return a.ex < b.ex; });
        DskFile f; f.name = nm;
        for (auto& x : v)
            for (int b : x.blocks) {
                size_t o = (size_t)b * 1024;
                if (o + 1024 <= img.size()) f.data.insert(f.data.end(), img.begin() + o, img.begin() + o + 1024);
            }
        cache.push_back(std::move(f));
    }
    cached = true;
    return cache;
}

const DskFile* DskImage::find(const std::string& name) const {
    files();
    std::string up = name;
    for (auto& c : up) c = toupper((unsigned char)c);
    for (auto& f : cache) if (f.name == up) return &f;
    for (auto& f : cache) {   // bare name: ignore extension
        size_t dot = f.name.find('.');
        if (dot != std::string::npos && f.name.substr(0, dot) == up) return &f;
    }
    return nullptr;
}
