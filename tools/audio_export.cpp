// Offline audio exporter for Sorcery+: renders every tune and sound effect to a 44.1 kHz mono 16-bit WAV file.
//
// It boots the native Machine (so SOUND.DAT is in RAM at A600), loads the music driver MUCODE at 4F00 and then drives the
// real driver entry points: 4F03 (start music), the 50 Hz tick routine 4F51 and the effect player 92BA, exactly as the
// game and the firmware ticker do.  The firmware sound manager (SoundMgr + AY model) is stepped every 1/100 s and its
// output is recorded.
//
//   audio_export <Sorcery.dsk> <out_dir>      (set AUDIO_VERIFY=1 to cross-check natives against the Z80 code)
#include "../native/src/machine.h"
#include "../native/src/port_sound.h"
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <string>
#include <vector>

static const int RATE = 44100;
static const int SAMPLES_PER_TICK = RATE / 100;   // 441

static void writeWav(const std::string& path, const std::vector<int16_t>& s) {
    FILE* f = fopen(path.c_str(), "wb");
    if (!f) { perror(path.c_str()); exit(1); }
    uint32_t dataLen = (uint32_t)s.size() * 2, v;
    auto w32 = [&](uint32_t x) { fwrite(&x, 4, 1, f); };
    auto w16 = [&](uint16_t x) { fwrite(&x, 2, 1, f); };
    fwrite("RIFF", 1, 4, f); w32(36 + dataLen); fwrite("WAVEfmt ", 1, 8, f);
    w32(16); w16(1); w16(1); w32(RATE); w32(RATE * 2); w16(2); w16(16);
    fwrite("data", 1, 4, f); w32(dataLen);
    fwrite(s.data(), 2, s.size(), f);
    (void)v;
    fclose(f);
}

// The loader's file decryption (see docs/NOTES.md): plain = ror1(-b) - 0x25.
static uint8_t decrypt(uint8_t b) { uint8_t a = (uint8_t)-b; a = (a >> 1) | (a << 7); return a - 0x25; }

static void loadEnvelopes(Machine& m) {   // what the game does at 7324: all 15 amplitude and tone envelopes of SOUND.DAT
    for (int n = 1; n <= 15; n++) {
        m.snd.setAmpl(n, &m.ram[sound::AMPL_ENVELOPES + 16 * n]);
        m.snd.setTone(n, &m.ram[sound::TONE_ENVELOPES + 16 * n]);
    }
}

static bool idle(Machine& m) {
    for (int c = 0; c < 3; c++) if ((m.snd.check(1 << c) & 0x80) || (m.snd.check(1 << c) & 7) != 4) return false;
    return true;
}

static void renderTick(Machine& m, std::vector<int16_t>& out) {
    m.snd.tick();
    size_t at = out.size();
    out.resize(at + SAMPLES_PER_TICK);
    m.audio(&out[at], SAMPLES_PER_TICK, RATE);
}

int main(int argc, char** argv) {
    if (argc < 2) { fprintf(stderr, "usage: %s out_dir  (run from the folder containing resources/)\n", argv[0]); return 1; }
    std::string dir = argv[1];
    Machine m;
    std::string err;
    std::string resDir = Resources::find("", argv[0]);
    if (!m.boot(resDir, &err)) { fprintf(stderr, "%s\n", err.c_str()); return 1; }
    const std::vector<uint8_t>* mu = m.res.file("MUCODE");
    if (!mu) { fprintf(stderr, "MUCODE not found\n"); return 1; }
    for (size_t i = 0; i < mu->size(); i++) m.ram[(0x4F00 + i) & 0xFFFF] = (*mu)[i];

    // ---- sound effects ----
    struct Fx { uint16_t addr; const char* name; };
    static const Fx fx[] = {
        {sound::SFX_SHOOT, "sfx_shoot"}, {sound::SFX_COLLECT_RISE, "sfx_rise"}, {sound::SFX_CLICK, "sfx_click"},
        {sound::SFX_ENEMY_EXPLODE, "sfx_enemy_explode"}, {sound::SFX_PLAYER_DEATH, "sfx_player_death"},
        {sound::SFX_PICKUP, "sfx_pickup"}, {sound::SFX_BLIP, "sfx_blip"}, {sound::SFX_BURST, "sfx_burst"},
        {sound::SFX_HIT, "sfx_hit"}, {sound::SFX_ZAP, "sfx_zap"}, {sound::SFX_THUD, "sfx_thud"},
        {sound::SFX_COUNTER, "sfx_counter"}, {sound::SFX_GAME_OVER, "sfx_game_over"},
        {sound::SFX_LEVEL_DONE, "sfx_level_done"}, {sound::SFX_WHOOSH, "sfx_whoosh"}};
    for (const Fx& f : fx) {
        m.snd.reset();
        loadEnvelopes(m);
        std::vector<int16_t> pcm(SAMPLES_PER_TICK * 5, 0);   // 50 ms of leading silence
        m.cpu.setHL(f.addr);
        m.call(0x92BA);                                  // the game's "play effect at HL"
        int ticks = 0;
        while (!idle(m) && ticks++ < 1500) renderTick(m, pcm);
        for (int i = 0; i < 20; i++) renderTick(m, pcm);       // 200 ms tail
        writeWav(dir + "/" + f.name + ".wav", pcm);
        printf("%s.wav  block %04X  %.2f s\n", f.name, f.addr, pcm.size() / (double)RATE);
    }

    // ---- music: the title tune, 3 loops (the streams loop every 12.8 s) ----
    {
        m.snd.reset();
        loadEnvelopes(m);
        m.call(sound::MU_INIT);                          // music init: redefines amplitude envelope 1, installs the ticker
        std::vector<int16_t> pcm;
        const int loops = 3, ticks = 1280 * loops;
        for (int t = 0; t < ticks; t++) {
            if (t % 2 == 0) m.call(sound::MU_TICK);      // the KL ticker: 50 Hz
            renderTick(m, pcm);
        }
        for (int i = 0; i < 100; i++) renderTick(m, pcm);      // release tail
        writeWav(dir + "/music_title.wav", pcm);
        printf("music_title.wav  %.2f s\n", pcm.size() / (double)RATE);
        m.call(sound::MU_STOP);
    }
    return 0;
}
