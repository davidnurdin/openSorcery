// Sorcery (Virgin Games, 1985) - CPC 6128 port. Front-end: SDL2 window, or --headless capture mode.
#include "machine.h"
#include "../common/png.h"
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <string>
#include <vector>
#ifdef USE_SDL
#include <SDL2/SDL.h>
#endif

struct KeyEvt { long frame; int key; bool down; };

int main(int argc, char** argv) {
    std::string dsk = "public/Sorcery.dsk";
    unsigned watchAddr = 0, watchMask = 0; long dbgFrame = -1; bool headless = false, verbose = false, hashTrace = false;
    long frames = 0;
    std::vector<std::pair<long, std::string>> shots;
    std::vector<KeyEvt> script;
    std::string profOut, ramOut;
    unsigned randSeed = 0; bool useRand = false;
    for (int i = 1; i < argc; i++) {
        std::string a = argv[i];
        if (a == "--dsk" && i + 1 < argc) dsk = argv[++i];
        else if (a == "--headless") headless = true;
        else if (a == "-v") verbose = true;
        else if (a == "--watch" && i + 1 < argc) { sscanf(argv[++i], "%x:%x", &watchAddr, &watchMask); }   // test aid: report frames where (ram[addr] & mask) != 0
        else if (a == "--hash") hashTrace = true;
        else if (a == "--vlog" && i + 1 < argc) dbgFrame = atol(argv[++i]);
        else if (a == "--profile" && i + 1 < argc) profOut = argv[++i];
        else if (a == "--dump-ram" && i + 1 < argc) ramOut = argv[++i];
        else if (a == "--rand-input" && i + 1 < argc) { randSeed = atoi(argv[++i]); useRand = true; }
        else if (a == "--frames" && i + 1 < argc) frames = atol(argv[++i]);
        else if (a == "--shot" && i + 1 < argc) { // frame:path
            std::string s = argv[++i]; size_t c = s.find(':');
            shots.push_back({atol(s.substr(0, c).c_str()), s.substr(c + 1)});
        } else if (a == "--key" && i + 1 < argc) { // frame:cpckey:1|0
            long f; int k, d; if (sscanf(argv[++i], "%ld:%d:%d", &f, &k, &d) == 3) script.push_back({f, k, d != 0});
        } else { fprintf(stderr, "usage: %s [--dsk file] [--headless --frames N --shot F:out.png --key F:K:1]\n", argv[0]); return 1; }
    }
    Machine m;
    std::string err;
    if (!m.boot(dsk, &err)) { fprintf(stderr, "boot failed: %s\n", err.c_str()); return 1; }
    std::vector<uint32_t> fb(Machine::OUT_W * Machine::OUT_H);
    if (headless) {
        if (!profOut.empty()) m.enableProfile();
        uint32_t rs = randSeed * 2654435761u + 12345;
        auto rnd = [&]() { rs ^= rs << 13; rs ^= rs >> 17; rs ^= rs << 5; return rs; };
        for (long f = 0; f < frames; f++) {
            if (useRand && f % 12 == 0) {   // random joystick every 12 frames (fire pulses after the attract mode)
                uint32_t r = rnd();
                if (f > 600) {
                    m.setKey(72, r & 1); m.setKey(73, r >> 1 & 1 && !(r & 1)); m.setKey(74, r >> 2 & 1); m.setKey(75, r >> 3 & 1 && !(r >> 2 & 1));
                    m.setKey(77, (r >> 4 & 3) == 0); m.setKey(76, (r >> 8 & 15) == 0);
                } else m.setKey(76, f == 396);
            }
            for (auto& k : script) if (k.frame == f) m.setKey(k.key, k.down);
            m.runFrame();
            if (dbgFrame == f) m.debugVideo(stdout);
            if (watchMask && (m.ram[watchAddr & 0xFFFF] & watchMask)) { static long last = -100; if (f - last > 50) printf("watch hit at frame %ld (room %02X)\n", f, m.ram[0x9310]); last = f; }
            if (hashTrace) {
                uint32_t h = 2166136261u;
                for (int i = 0; i < 65536; i++) h = (h ^ m.ram[i]) * 16777619u;
                fprintf(stdout, "%ld %04X %04X %04X %04X %04X %04X %08X\n", f, m.cpu.pc, m.cpu.sp, m.cpu.af(), m.cpu.bc(), m.cpu.de(), m.cpu.hl(), h);
            }
            if (verbose && f % 25 == 0) fprintf(stderr, "frame %ld pc=%04X sp=%04X iff=%d im=%d halted=%d\n", f, m.cpu.pc, m.cpu.sp, m.cpu.iff1, m.cpu.im, m.cpu.halted);
            if (m.crashed()) { fprintf(stderr, "crashed at frame %ld: %s (pc=%04X)\n", f, m.deadReason().c_str(), m.cpu.pc); break; }
            for (auto& s : shots) if (s.first == f) { m.render(fb.data()); writePng(s.second.c_str(), fb.data(), Machine::OUT_W, Machine::OUT_H); }
        }
        if (!profOut.empty()) {
            FILE* pf = fopen(profOut.c_str(), "w");
            for (int a = 0; a < 65536; a++) if (m.execCnt[a]) fprintf(pf, "X %04X %u\n", a, m.execCnt[a]);
            for (int a = 0; a < 65536; a++) if (m.writeCnt[a]) fprintf(pf, "W %04X %u\n", a, m.writeCnt[a]);
            fclose(pf);
        }
        if (verbose) { fprintf(stderr, "mode=%d pal:", m.videoMode()); for (int p = 0; p < 17; p++) fprintf(stderr, " %02X", m.hwPen(p)); fprintf(stderr, "\n"); }
        if (!ramOut.empty()) { FILE* rf = fopen(ramOut.c_str(), "wb"); fwrite(m.ram, 1, 65536, rf); fclose(rf); }
        return 0;
    }
#ifdef USE_SDL
    if (SDL_Init(SDL_INIT_VIDEO | SDL_INIT_AUDIO | SDL_INIT_GAMECONTROLLER) != 0) { fprintf(stderr, "SDL_Init: %s\n", SDL_GetError()); return 1; }
    SDL_Window* win = SDL_CreateWindow("Sorcery+ (CPC 6128) - C++/SDL2 port", SDL_WINDOWPOS_CENTERED, SDL_WINDOWPOS_CENTERED,
                                       Machine::OUT_W * 2, Machine::OUT_H * 2, SDL_WINDOW_RESIZABLE);
    SDL_Renderer* ren = SDL_CreateRenderer(win, -1, SDL_RENDERER_ACCELERATED | SDL_RENDERER_PRESENTVSYNC);
    if (!ren) ren = SDL_CreateRenderer(win, -1, SDL_RENDERER_SOFTWARE);
    SDL_RenderSetLogicalSize(ren, Machine::OUT_W, Machine::OUT_H);
    SDL_SetHint(SDL_HINT_RENDER_SCALE_QUALITY, "nearest");
    SDL_Texture* tex = SDL_CreateTexture(ren, SDL_PIXELFORMAT_ABGR8888, SDL_TEXTUREACCESS_STREAMING, Machine::OUT_W, Machine::OUT_H);
    SDL_AudioSpec want{}, have{};
    want.freq = 44100; want.format = AUDIO_S16SYS; want.channels = 1; want.samples = 1024;
    SDL_AudioDeviceID adev = SDL_OpenAudioDevice(nullptr, 0, &want, &have, 0);
    if (adev) SDL_PauseAudioDevice(adev, 0);
    SDL_GameController* pad = nullptr;
    for (int i = 0; i < SDL_NumJoysticks(); i++) if (SDL_IsGameController(i)) { pad = SDL_GameControllerOpen(i); break; }

    // keyboard -> CPC keys (row 9 = joystick 0: up down left right fire2 fire1)
    auto mapKey = [](SDL_Scancode sc) -> int {
        switch (sc) {
        case SDL_SCANCODE_UP: case SDL_SCANCODE_W: return 72;
        case SDL_SCANCODE_DOWN: case SDL_SCANCODE_S: return 73;
        case SDL_SCANCODE_LEFT: case SDL_SCANCODE_A: return 74;
        case SDL_SCANCODE_RIGHT: case SDL_SCANCODE_D: return 75;
        case SDL_SCANCODE_X: case SDL_SCANCODE_LALT: case SDL_SCANCODE_RALT: case SDL_SCANCODE_Z: case SDL_SCANCODE_LCTRL:
        case SDL_SCANCODE_RCTRL: case SDL_SCANCODE_RETURN: return 76;   // every fire key = joystick fire (bit 4: shoot / start)
        case SDL_SCANCODE_SPACE: return 47;     // pause
        default: return -1;
        }
    };
    const int samplesPerFrame = have.freq / 50;
    std::vector<int16_t> abuf(samplesPerFrame);
    uint64_t freq = SDL_GetPerformanceFrequency(), next = SDL_GetPerformanceCounter();
    bool quit = false, full = false;
    while (!quit) {
        SDL_Event e;
        while (SDL_PollEvent(&e)) {
            if (e.type == SDL_QUIT) quit = true;
            else if (e.type == SDL_KEYDOWN || e.type == SDL_KEYUP) {
                bool down = e.type == SDL_KEYDOWN;
                if (down && !e.key.repeat) {
                    if (e.key.keysym.scancode == SDL_SCANCODE_ESCAPE) quit = true;
                    else if (e.key.keysym.scancode == SDL_SCANCODE_F11) { full = !full; SDL_SetWindowFullscreen(win, full ? SDL_WINDOW_FULLSCREEN_DESKTOP : 0); }
                    else if (e.key.keysym.scancode == SDL_SCANCODE_F5) { m.render(fb.data()); writePng("sorcery_shot.png", fb.data(), Machine::OUT_W, Machine::OUT_H); }
                    else if (e.key.keysym.scancode == SDL_SCANCODE_F1) m.boot(dsk);
                }
                int k = mapKey(e.key.keysym.scancode);
                if (k >= 0) m.setKey(k, down);
            } else if (e.type == SDL_CONTROLLERBUTTONDOWN || e.type == SDL_CONTROLLERBUTTONUP) {
                bool down = e.type == SDL_CONTROLLERBUTTONDOWN;
                switch (e.cbutton.button) {
                case SDL_CONTROLLER_BUTTON_DPAD_UP: m.setKey(72, down); break;
                case SDL_CONTROLLER_BUTTON_DPAD_DOWN: m.setKey(73, down); break;
                case SDL_CONTROLLER_BUTTON_DPAD_LEFT: m.setKey(74, down); break;
                case SDL_CONTROLLER_BUTTON_DPAD_RIGHT: m.setKey(75, down); break;
                case SDL_CONTROLLER_BUTTON_A: case SDL_CONTROLLER_BUTTON_B: case SDL_CONTROLLER_BUTTON_X: m.setKey(76, down); break;
                case SDL_CONTROLLER_BUTTON_START: m.setKey(47, down); break;
                default: break;
                }
            }
        }
        m.runFrame();
        if (m.crashed()) { fprintf(stderr, "emulation stopped: %s (pc=%04X)\n", m.deadReason().c_str(), m.cpu.pc); break; }
        if (adev) {
            m.audio(abuf.data(), samplesPerFrame, have.freq);
            if (SDL_GetQueuedAudioSize(adev) < (Uint32)samplesPerFrame * 2 * 6) SDL_QueueAudio(adev, abuf.data(), samplesPerFrame * 2);
        }
        m.render(fb.data());
        SDL_UpdateTexture(tex, nullptr, fb.data(), Machine::OUT_W * 4);
        SDL_RenderClear(ren);
        SDL_RenderCopy(ren, tex, nullptr, nullptr);
        SDL_RenderPresent(ren);
        next += freq / 50;
        uint64_t now = SDL_GetPerformanceCounter();
        if (next > now) SDL_Delay((Uint32)((next - now) * 1000 / freq));
        else if (now - next > freq / 5) next = now;
    }
    if (pad) SDL_GameControllerClose(pad);
    if (adev) SDL_CloseAudioDevice(adev);
    SDL_DestroyTexture(tex); SDL_DestroyRenderer(ren); SDL_DestroyWindow(win); SDL_Quit();
    return 0;
#else
    fprintf(stderr, "built without SDL; use --headless\n");
    return 1;
#endif
}
