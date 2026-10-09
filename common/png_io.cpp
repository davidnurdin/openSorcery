#include "png_io.h"
#include <cstdio>
#include <cstring>

namespace {

// ---------------------------------------------------------------- inflate (RFC 1951)
struct Bits {
    const uint8_t* p; size_t n, pos = 0; uint32_t bitbuf = 0; int bitcnt = 0; bool err = false;
    int bit() {
        if (bitcnt == 0) { if (pos >= n) { err = true; return 0; } bitbuf = p[pos++]; bitcnt = 8; }
        int b = bitbuf & 1; bitbuf >>= 1; bitcnt--; return b;
    }
    uint32_t bits(int k) { uint32_t v = 0; for (int i = 0; i < k; i++) v |= (uint32_t)bit() << i; return v; }
};
struct Huff {
    uint16_t count[16], symbol[288];
    void build(const uint8_t* len, int n) {
        memset(count, 0, sizeof count);
        for (int i = 0; i < n; i++) count[len[i]]++;
        count[0] = 0;
        uint16_t offs[16]; offs[1] = 0;
        for (int i = 1; i < 15; i++) offs[i + 1] = offs[i] + count[i];
        for (int i = 0; i < n; i++) if (len[i]) symbol[offs[len[i]]++] = i;
    }
    int decode(Bits& b) const {
        int code = 0, first = 0, index = 0;
        for (int len = 1; len <= 15; len++) {
            code |= b.bit();
            int c = count[len];
            if (code - c < first) return symbol[index + (code - first)];
            index += c; first += c; first <<= 1; code <<= 1;
        }
        return -1;
    }
};
bool inflateBlock(Bits& b, std::vector<uint8_t>& out, const Huff& lit, const Huff& dist) {
    static const uint16_t lbase[] = {3,4,5,6,7,8,9,10,11,13,15,17,19,23,27,31,35,43,51,59,67,83,99,115,131,163,195,227,258};
    static const uint16_t lext[] = {0,0,0,0,0,0,0,0,1,1,1,1,2,2,2,2,3,3,3,3,4,4,4,4,5,5,5,5,0};
    static const uint16_t dbase[] = {1,2,3,4,5,7,9,13,17,25,33,49,65,97,129,193,257,385,513,769,1025,1537,2049,3073,4097,6145,8193,12289,16385,24577};
    static const uint16_t dext[] = {0,0,0,0,1,1,2,2,3,3,4,4,5,5,6,6,7,7,8,8,9,9,10,10,11,11,12,12,13,13};
    for (;;) {
        int s = lit.decode(b);
        if (s < 0 || b.err) return false;
        if (s < 256) out.push_back((uint8_t)s);
        else if (s == 256) return true;
        else {
            s -= 257; if (s >= 29) return false;
            int len = lbase[s] + b.bits(lext[s]);
            int d = dist.decode(b); if (d < 0 || d >= 30) return false;
            size_t distance = dbase[d] + b.bits(dext[d]);
            if (distance > out.size()) return false;
            for (int i = 0; i < len; i++) out.push_back(out[out.size() - distance]);
        }
    }
}
bool inflate(const uint8_t* data, size_t n, std::vector<uint8_t>& out) {
    if (n < 2) return false;
    Bits b{data + 2, n - 2};   // skip the zlib header
    int last;
    do {
        last = b.bit();
        int type = b.bits(2);
        if (type == 0) {
            b.bitcnt = 0;
            if (b.pos + 4 > b.n) return false;
            unsigned len = b.p[b.pos] | b.p[b.pos + 1] << 8; b.pos += 4;
            if (b.pos + len > b.n) return false;
            out.insert(out.end(), b.p + b.pos, b.p + b.pos + len); b.pos += len;
        } else if (type == 1) {
            uint8_t l[288]; for (int i = 0; i < 144; i++) l[i] = 8; for (int i = 144; i < 256; i++) l[i] = 9;
            for (int i = 256; i < 280; i++) l[i] = 7; for (int i = 280; i < 288; i++) l[i] = 8;
            Huff lit, dist; lit.build(l, 288);
            uint8_t dl[30]; for (int i = 0; i < 30; i++) dl[i] = 5; dist.build(dl, 30);
            if (!inflateBlock(b, out, lit, dist)) return false;
        } else if (type == 2) {
            int nlen = b.bits(5) + 257, ndist = b.bits(5) + 1, ncode = b.bits(4) + 4;
            static const uint8_t order[19] = {16,17,18,0,8,7,9,6,10,5,11,4,12,3,13,2,14,1,15};
            uint8_t lengths[320] = {};
            for (int i = 0; i < ncode; i++) lengths[order[i]] = (uint8_t)b.bits(3);
            Huff cl; cl.build(lengths, 19);
            uint8_t ll[320] = {}; int idx = 0;
            while (idx < nlen + ndist) {
                int sym = cl.decode(b); if (sym < 0 || b.err) return false;
                if (sym < 16) ll[idx++] = (uint8_t)sym;
                else {
                    int rep, val = 0;
                    if (sym == 16) { if (!idx) return false; val = ll[idx - 1]; rep = 3 + b.bits(2); }
                    else if (sym == 17) rep = 3 + b.bits(3);
                    else rep = 11 + b.bits(7);
                    if (idx + rep > nlen + ndist) return false;
                    while (rep--) ll[idx++] = (uint8_t)val;
                }
            }
            Huff lit, dist; lit.build(ll, nlen); dist.build(ll + nlen, ndist);
            if (!inflateBlock(b, out, lit, dist)) return false;
        } else return false;
        if (b.err) return false;
    } while (!last);
    return true;
}

uint32_t be32(const uint8_t* p) { return p[0] << 24 | p[1] << 16 | p[2] << 8 | p[3]; }
}  // namespace

bool readPng(const std::string& path, Image& img, const std::vector<uint32_t>& refPal, std::string* err) {
    auto fail = [&](const std::string& m) { if (err) *err = path + ": " + m; return false; };
    FILE* f = fopen(path.c_str(), "rb");
    if (!f) return fail("cannot open");
    std::vector<uint8_t> d;
    uint8_t buf[65536]; size_t n;
    while ((n = fread(buf, 1, sizeof buf, f)) > 0) d.insert(d.end(), buf, buf + n);
    fclose(f);
    if (d.size() < 8 || memcmp(d.data(), "\x89PNG\r\n\x1a\n", 8) != 0) return fail("not a PNG");
    size_t p = 8;
    int w = 0, h = 0, depth = 0, ctype = 0, interlace = 0;
    std::vector<uint32_t> plte;
    std::vector<uint8_t> idat;
    while (p + 8 <= d.size()) {
        uint32_t len = be32(&d[p]); const uint8_t* t = &d[p + 4]; const uint8_t* c = &d[p + 8];
        if (p + 12 + len > d.size()) return fail("truncated");
        if (!memcmp(t, "IHDR", 4)) { w = be32(c); h = be32(c + 4); depth = c[8]; ctype = c[9]; interlace = c[12]; }
        else if (!memcmp(t, "PLTE", 4)) for (uint32_t i = 0; i + 2 < len; i += 3) plte.push_back(c[i] << 16 | c[i + 1] << 8 | c[i + 2]);
        else if (!memcmp(t, "IDAT", 4)) idat.insert(idat.end(), c, c + len);
        else if (!memcmp(t, "IEND", 4)) break;
        p += 12 + len;
    }
    if (!w || !h) return fail("no IHDR");
    if (interlace) return fail("interlaced PNG not supported");
    int channels = ctype == 3 ? 1 : ctype == 2 ? 3 : ctype == 6 ? 4 : ctype == 0 ? 1 : ctype == 4 ? 2 : 0;
    if (!channels || (ctype != 3 && depth != 8 && ctype != 0)) return fail("unsupported PNG format (use indexed, RGB or RGBA 8 bit)");
    std::vector<uint8_t> raw;
    if (!inflate(idat.data(), idat.size(), raw)) return fail("bad compressed data");
    int bpp = (channels * depth + 7) / 8;
    size_t stride = ((size_t)w * channels * depth + 7) / 8;
    if (raw.size() < (stride + 1) * h) return fail("short image data");
    std::vector<uint8_t> prev(stride, 0), cur(stride);
    img.w = w; img.h = h; img.pix.assign((size_t)w * h, 0);
    img.pal = (ctype == 3) ? plte : refPal;
    auto nearest = [&](uint32_t rgb) {
        int best = 0; long bd = 1L << 40;
        for (size_t i = 0; i < refPal.size(); i++) {
            int dr = (int)(rgb >> 16 & 255) - (int)(refPal[i] >> 16 & 255), dg = (int)(rgb >> 8 & 255) - (int)(refPal[i] >> 8 & 255),
                db = (int)(rgb & 255) - (int)(refPal[i] & 255);
            long dd = (long)dr * dr + (long)dg * dg + (long)db * db;
            if (dd < bd) { bd = dd; best = (int)i; }
        }
        return best;
    };
    for (int y = 0; y < h; y++) {
        const uint8_t* line = &raw[(stride + 1) * y];
        int ft = line[0];
        for (size_t i = 0; i < stride; i++) {
            int a = i >= (size_t)bpp ? cur[i - bpp] : 0, b = prev[i], c = i >= (size_t)bpp ? prev[i - bpp] : 0, x = line[1 + i], v;
            switch (ft) {
            case 0: v = x; break;
            case 1: v = x + a; break;
            case 2: v = x + b; break;
            case 3: v = x + ((a + b) >> 1); break;
            case 4: { int pa = abs(b - c), pb = abs(a - c), pc = abs(a + b - 2 * c); int pr = (pa <= pb && pa <= pc) ? a : (pb <= pc ? b : c); v = x + pr; break; }
            default: return fail("bad filter");
            }
            cur[i] = (uint8_t)v;
        }
        for (int x = 0; x < w; x++) {
            int v;
            if (ctype == 3 || ctype == 0) {
                if (depth == 8) v = cur[x];
                else { int per = 8 / depth, shift = 8 - depth * (x % per + 1); v = (cur[x / per] >> shift) & ((1 << depth) - 1); }
                if (ctype == 0) v = nearest(v * 0x010101);
            } else {
                const uint8_t* px = &cur[(size_t)x * channels];
                uint32_t rgb = ctype == 4 ? px[0] * 0x010101 : (px[0] << 16 | px[1] << 8 | px[2]);
                v = nearest(rgb);
            }
            img.pix[(size_t)y * w + x] = (uint8_t)v;
        }
        prev = cur;
    }
    return true;
}

bool writeIndexedPng(const std::string& path, const Image& img) {
    static uint32_t crcT[256]; static bool init = false;
    if (!init) { for (uint32_t n = 0; n < 256; n++) { uint32_t c = n; for (int k = 0; k < 8; k++) c = c & 1 ? 0xEDB88320u ^ (c >> 1) : c >> 1; crcT[n] = c; } init = true; }
    auto crc = [&](const uint8_t* p, size_t n) { uint32_t c = 0xFFFFFFFFu; for (size_t i = 0; i < n; i++) c = crcT[(c ^ p[i]) & 255] ^ (c >> 8); return ~c; };
    std::vector<uint8_t> raw;
    for (int y = 0; y < img.h; y++) { raw.push_back(0); raw.insert(raw.end(), &img.pix[(size_t)y * img.w], &img.pix[(size_t)y * img.w] + img.w); }
    std::vector<uint8_t> z = {0x78, 0x01};
    uint32_t a = 1, b = 0;
    for (uint8_t v : raw) { a = (a + v) % 65521; b = (b + a) % 65521; }
    for (size_t o = 0; o < raw.size() || o == 0; o += 65535) {
        size_t n = raw.size() - o < 65535 ? raw.size() - o : 65535;
        z.push_back(o + n >= raw.size()); z.push_back(n); z.push_back(n >> 8); z.push_back(~n); z.push_back(~n >> 8);
        z.insert(z.end(), raw.begin() + o, raw.begin() + o + n);
        if (raw.empty()) break;
    }
    uint32_t ad = b << 16 | a;
    z.push_back(ad >> 24); z.push_back(ad >> 16); z.push_back(ad >> 8); z.push_back(ad);
    FILE* f = fopen(path.c_str(), "wb"); if (!f) return false;
    auto be = [&](uint32_t v) { uint8_t t[4] = {uint8_t(v >> 24), uint8_t(v >> 16), uint8_t(v >> 8), uint8_t(v)}; fwrite(t, 1, 4, f); };
    auto chunk = [&](const char* type, const std::vector<uint8_t>& dd) {
        be(dd.size()); std::vector<uint8_t> t(type, type + 4); t.insert(t.end(), dd.begin(), dd.end());
        fwrite(t.data(), 1, t.size(), f); be(crc(t.data(), t.size()));
    };
    fwrite("\x89PNG\r\n\x1a\n", 1, 8, f);
    std::vector<uint8_t> ih = {uint8_t(img.w >> 24), uint8_t(img.w >> 16), uint8_t(img.w >> 8), uint8_t(img.w),
                               uint8_t(img.h >> 24), uint8_t(img.h >> 16), uint8_t(img.h >> 8), uint8_t(img.h), 8, 3, 0, 0, 0};
    chunk("IHDR", ih);
    std::vector<uint8_t> pl;
    for (uint32_t c : img.pal) { pl.push_back(c >> 16); pl.push_back(c >> 8); pl.push_back(c); }
    chunk("PLTE", pl);
    chunk("IDAT", z);
    chunk("IEND", {});
    fclose(f);
    return true;
}
