// Minimal PNG writer (stored deflate blocks, no dependencies).
#pragma once
#include <cstdint>
#include <cstdio>
#include <vector>
inline bool writePng(const char* path, const uint32_t* rgba, int w, int h) {
    static uint32_t crcT[256]; static bool init = false;
    if (!init) { for (uint32_t n = 0; n < 256; n++) { uint32_t c = n; for (int k = 0; k < 8; k++) c = c & 1 ? 0xEDB88320u ^ (c >> 1) : c >> 1; crcT[n] = c; } init = true; }
    auto crc = [&](const uint8_t* p, size_t n, uint32_t c = 0xFFFFFFFFu) { for (size_t i = 0; i < n; i++) c = crcT[(c ^ p[i]) & 255] ^ (c >> 8); return c; };
    std::vector<uint8_t> raw;
    for (int y = 0; y < h; y++) { raw.push_back(0); for (int x = 0; x < w; x++) { uint32_t p = rgba[y * w + x]; raw.push_back(p); raw.push_back(p >> 8); raw.push_back(p >> 16); } }
    std::vector<uint8_t> z = {0x78, 0x01};
    uint32_t a = 1, b = 0;
    for (uint8_t v : raw) { a = (a + v) % 65521; b = (b + a) % 65521; }
    for (size_t o = 0; o < raw.size(); o += 65535) {
        size_t n = raw.size() - o < 65535 ? raw.size() - o : 65535;
        z.push_back(o + n >= raw.size()); z.push_back(n); z.push_back(n >> 8); z.push_back(~n); z.push_back(~n >> 8);
        z.insert(z.end(), raw.begin() + o, raw.begin() + o + n);
    }
    uint32_t ad = b << 16 | a;
    z.push_back(ad >> 24); z.push_back(ad >> 16); z.push_back(ad >> 8); z.push_back(ad);
    FILE* f = fopen(path, "wb"); if (!f) return false;
    auto be = [&](uint32_t v) { uint8_t t[4] = {uint8_t(v >> 24), uint8_t(v >> 16), uint8_t(v >> 8), uint8_t(v)}; fwrite(t, 1, 4, f); };
    auto chunk = [&](const char* type, const std::vector<uint8_t>& d) {
        be(d.size()); std::vector<uint8_t> t(type, type + 4); t.insert(t.end(), d.begin(), d.end());
        fwrite(t.data(), 1, t.size(), f); be(~crc(t.data(), t.size()));
    };
    fwrite("\x89PNG\r\n\x1a\n", 1, 8, f);
    std::vector<uint8_t> ih = {uint8_t(w >> 24), uint8_t(w >> 16), uint8_t(w >> 8), uint8_t(w), uint8_t(h >> 24), uint8_t(h >> 16), uint8_t(h >> 8), uint8_t(h), 8, 2, 0, 0, 0};
    chunk("IHDR", ih); chunk("IDAT", z); chunk("IEND", {});
    fclose(f); return true;
}
