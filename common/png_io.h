// PNG reading (indexed / RGB / RGBA, non interlaced, own inflate: no zlib needed) and indexed PNG writing.
#pragma once
#include <cstdint>
#include <string>
#include <vector>

struct Image {
    int w = 0, h = 0;
    std::vector<uint8_t> pix;      // w*h palette indices
    std::vector<uint32_t> pal;     // 0x00RRGGBB per index (as stored in the PNG, or as given for RGB images)
    uint8_t& at(int x, int y) { return pix[(size_t)y * w + x]; }
    uint8_t at(int x, int y) const { return pix[(size_t)y * w + x]; }
};

// Reads a PNG. Indexed images keep their indices (so editors that preserve the palette order work as expected); RGB/RGBA images are
// mapped to the nearest entry of `refPal` (indices into that palette). Returns false (and fills err) on failure.
bool readPng(const std::string& path, Image& out, const std::vector<uint32_t>& refPal, std::string* err = nullptr);
bool writeIndexedPng(const std::string& path, const Image& img);
