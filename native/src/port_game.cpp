// The in-game loop of Sorcery+ (original 7457-74B5), the frame synchronisation 8A97 and the title picture reveal (8E36, 8E5B).
//
// The game logic is paced by the interrupt handler: it counts the interrupts down in (iy+72); when bit 7 gets set the main
// loop runs one tick of all subsystems (about 37.5 Hz, every 8th interrupt).
#include "port_flow.h"
#include "port_player.h"

namespace flow {
using namespace ram;

namespace {
constexpr uint16_t IY_ = 0x6800;
constexpr uint16_t CARRIED_ITEM_SHOWN = 0x9CB6;     // item id for which the message line 2 was last drawn
constexpr uint16_t ITEM_CARRIED = IY_ + 70;         // item the player carries
constexpr uint16_t FREE_RUN_SEED = 0x9311;          // stirred every tick (random source of the original)
constexpr uint16_t SORCERERS_FREED = 0x945B;        // 4 = all sorcerers freed
constexpr uint16_t FIRST_ENTITY_SLOT = 0x93E8;      // 0xFF in the first slot: the last "freeing" effect has finished
constexpr uint8_t TICK_REARM = 0x07;

void waitMainTick() {
    while (!(at(TICK_COUNTER) & 0x80)) waitIrq();
    at(TICK_COUNTER) = TICK_REARM;
}
}  // namespace

// 7457/745D: runs until the player's life is over (death, demo aborted with fire, victory).
GameEnd roomLoop(bool demo) {
    Machine& m = mach();
    GameEnd end;
    at(PLAYER_FLAGS) = at(DEMO_FLAG);                       // 7457: (iy+80) = (iy+81)
    try {
        for (;;) {
            waitMainTick();
            if (!(at(DEMO_FLAG) & 0x80)) call(0x7509);      // pause (space), not in a demo
            call(0x74B8);                                   // joystick -> (iy+73)
            call(0x76DD);                                   // the player
            call(0x80C0);                                   // weapon / spell fired by the player
            call(0x81A2);                                   // enemies and other entities
            call(0x854D);                                   // bolts
            call(0x8540);                                   // debris
            call(0x905B);                                   // animated tiles, collectibles
            call(0x8470);                                   // door A
            call(0x84AB);                                   // door B
            call(0x85D8);                                   // heart pickup
            call(0x8608);                                   // hazards touching the player
            call(0x7BF4);                                   // room 0x1C decorations
            call(0x7C38);                                   // ending check
            if (at(SORCERERS_FREED) == 4 && at(FIRST_ENTITY_SLOT) == 0xFF) { end.victory = true; return end; }   // 8955
            at(FREE_RUN_SEED) = (uint8_t)(at(FREE_RUN_SEED) + 0x29);
            if (at(CARRIED_ITEM_SHOWN) != at(ITEM_CARRIED)) call(0x8C8E);   // message line 2 follows the carried item
            call(0x74FB);                                   // demo: has fire been pressed?
            if (m.cpu.f & 1) { end.fire = true; return end; }
            if (at(DEATH_CAUSE) & 0x80) return end;         // the player is dead
        }
    } catch (const LeaveGame&) {
        return end;
    }
}

// 8A97: wait for the tick, then keep the room animated (entities, debris, animated tiles); used by the animation sequences.
NATIVE(0x8A97, frameSync, "", 100) {
    waitMainTick();
    m.call(0x81A2);
    m.call(0x8540);
    m.call(0x905B);
}

// 8E5B: reveal the 40x18 tile map one tile per interrupt in a scrambled order (step 0xAD through the 720 positions).
NATIVE(0x8E5B, revealTiles, "", 100) {
    constexpr int TILES = 0x2D0, STEP = 0xAD;
    int idx = 0;
    for (int n = 0; n < TILES; ++n) {
        m.cpu.setHL(mem::TILEMAP + idx);
        m.cpu.setDE(0xC000 | (idx * 2));
        m.call(0x8EBC);                                     // draw one tile
        idx = (idx + STEP) % TILES;
        waitIrq();
    }
}

// 8E36: the title picture (TISCRN = tile map 720 bytes) is revealed with the scrambled tile order.
NATIVE(0x8E36, revealTitle, "", 100) {
    m.cpu.a = 0;
    m.call(0xBC0E);                                         // mode 0, screen cleared
    m.cpu.setHL(PAL_SCENE);
    m.cpu.a = 0;
    m.call(0x8F3C);                                         // palette
    for (int i = 0; i < 0x2D0; ++i) m.ram[mem::TILEMAP + i] = m.ram[0x69C0 + i];
    m.call(0x8E5B);
    m.call(0x8ED6);                                         // classify tiles (animation markers, solid)
    at(IY_ + 38) = 0xFF;
    at(IY_ + 41) = 0xFF;
}

}  // namespace flow
