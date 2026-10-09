// End of a game: the victory sequence (8955), and the game over screen (8636) which leads to the name entry.
#include "port_flow.h"
#include <cstring>

namespace flow {
using namespace ram;

namespace {

constexpr uint16_t ENTITY = 0x93E8;               // scratch sprite entity used by the victory animation: +0 state/frame, +1 y, +2 x, +3 anim, +4 counter
constexpr uint16_t ESCORT_POSITIONS = 0x945C;     // 4 x {y, x}: where the four rescued sorcerers appear
constexpr uint16_t ESCORT_COUNT = 0x945B;
constexpr uint16_t SOUND_FANFARE_1 = 0xA848, SOUND_FANFARE_2 = 0xA86C;   // sound queue blocks (92BA queues one)
constexpr uint16_t TEXT_VICTORY = 0x9857;
constexpr uint16_t DEATH_TEXTS[3] = {0x97E8, 0x980D, 0x9832};            // matching bits 0..2 of DEATH_CAUSE
constexpr uint16_t TEXT_GAME_OVER = 0x9681;       // "VOTRE SCORE EST ... ET VOUS AVEZ SAUVE n SORCIERS"
constexpr uint16_t TEXT_ALL_SAVED_LOST = 0x96F6, TEXT_ALL_SAVED_WON = 0x977D;

// 8A97 is the sprite engine's main step: it waits for the engine tick and moves everything.  Run it until the entity's
// state byte shows that it has finished (all bits except bit 7 set).
void stepUntilFinished(uint16_t stateByte) {
    do {
        call(0x8A97);
    } while ((at(stateByte) | 0x80) != 0xFF);
}

void fireSound(uint16_t block) {                 // 92BA: SOUND QUEUE
    mach().cpu.setHL(block);
    call(0x92BA);
}

}  // namespace

// ---------------------------------------------------------------------------------------------------------------------
// 8955: all four pieces collected and the final sprite finished -> the victory animation, then the game over screen.
// ---------------------------------------------------------------------------------------------------------------------
void victory() {
    Machine& m = mach();
    Z80& c = m.cpu;
    at(PLAYER_FLAGS) |= 0x80;
    memcpy(&m.ram[ENTITY], &m.ram[mem::PLAYER], 10);               // the entity starts as a copy of the player
    c.ix = ENTITY;
    at(ENTITY + 0) = 0xA8;
    at(ENTITY + 9) = 0xFF;
    at(ENTITY + 4) = 0;
    fireSound(SOUND_FANFARE_1);
    stepUntilFinished(ENTITY);

    // the four sorcerers appear one after the other at their places ...
    for (int phase = 0; phase < 2; ++phase) {
        for (int i = 0; i < 4; ++i) {
            at(ESCORT_COUNT) = (uint8_t)i;
            c.ix = ENTITY;
            at(ENTITY + 1) = at(ESCORT_POSITIONS + 2 * i);
            at(ENTITY + 2) = at(ESCORT_POSITIONS + 2 * i + 1);
            at(ENTITY + 0) = phase == 0 ? 0xA9 : 0xAC;
            at(ENTITY + 3) = phase == 0 ? 0x28 : 0x3A;
            at(ENTITY + 4) = 0;
            stepUntilFinished(ENTITY);
        }
    }

    // ... then everything flashes white
    for (int pen = 0; pen < 16; ++pen) {
        c.a = (uint8_t)pen;
        c.b = c.c = 0x1A;
        call(0xBC32);                                             // SCR SET INK
    }
    call(0x8A97);
    fireSound(SOUND_FANFARE_2);

    // the final scene: an empty room with two sprites (iy+0.. and iy+10..), a text, and the score bonus
    memset(&m.ram[mem::TILEMAP], 0, 0x2D0);
    call(0x8EA6);                                                 // draw the (empty) tile map
    at(IY + 2) = 0x2D;
    at(IY + 12) = 0x2D;
    at(IY + 4) = 0x3C;
    at(IY + 14) = 0x58;
    at(IY + 10) = at(IY + 20) = 0x7C;
    at(IY + 11) = at(IY + 21) = 0x9E;                             // both use the animation script at 9E7C
    at(IY + 5) = 0x54;
    at(IY + 15) = 0x55;
    at(IY + 8) = 0xB0;
    at(IY + 18) = 0xFF;
    waitTick();
    waitTick();
    setPalette(PAL_SCENE, 1);
    c.setHL(TEXT_VICTORY);
    call(0x8CE4);
    c.setDE(0x0690);
    c.setHL(0x07FF);
    call(0x8CBD);                                                 // clear the message line
    stepUntilFinished(IY + 2);

    at(DEATH_CAUSE) |= 0x88;
    c.a = at(SCORE);
    lift::alu(c, 0, 2);                                           // ADD A,2 ; DAA : +20000 points
    lift::daa(c);
    at(SCORE) = c.a;
    at(DEATH_CAUSE) |= 0x40;                                      // "won"
    gameOver();
}

// ---------------------------------------------------------------------------------------------------------------------
// 8636: the player is dead (or has won): show why, then the final score, then the name entry if the score is high enough.
// ---------------------------------------------------------------------------------------------------------------------
void gameOver() {
    if (at(DEMO_FLAG) & 0x80) return;                             // 8636: a demo just returns to the attract code
    Machine& m = mach();
    Z80& c = m.cpu;
    for (int bit = 0; bit < 3; ++bit) {
        if (at(DEATH_CAUSE) & (1 << bit)) {
            c.setHL(DEATH_TEXTS[bit]);
            call(0x8CE4);
        }
    }
    timedLoop(5 * 65536, 26.0, false);                            // about 2 seconds to read the message

    call(0x8E8C);                                                 // clear the screen
    setWord(IRQ_TABLE_PTR, IRQ_TABLE_MENU);
    setPalette(PAL_MENU, 0);
    setMode(1);
    c.setHL(TEXT_GAME_OVER);
    call(0x8CE4);
    c.setDE(0x03F0);
    printChar((char)((at(SAVED_COUNT) & 0x0F) | 0x30));           // number of sorcerers saved
    c.setDE(0x0402);
    if (at(SAVED_COUNT) != 1) printChar('S');
    printChar('.');
    c.setDE(0x0351);
    c.setHL(SCORE);
    for (int i = 0; i < 3; ++i) call(0x91E8);                     // the score, 6 digits

    // The original now waits with a loop that counts HL down b times; HL is whatever the code left in it.
    uint32_t hl = (uint16_t)(SCORE + 3);
    int b = 0x0A;
    if (at(SAVED_COUNT) == 8) {                                   // all eight sorcerers: congratulations, initiation mode
        bool won = at(DEATH_CAUSE) & 0x40;
        c.setDE(0x0550);
        hl = TEXT_ALL_SAVED_LOST;
        if (!won) { c.setHL(hl); call(0x8CE4); hl = c.hl(); }
        hl = TEXT_ALL_SAVED_WON;
        if (won) { c.setHL(hl); call(0x8CE4); hl = c.hl(); }
        setTextMask(0xF0);
        printAt(0x0000, "MODE D'INITIATION MAINTENANT DISPONIBLE.");
        b = 0x14;
    }
    timedLoop(hl + (b - 1) * 65536u, 26.0, false);
    nameEntryAndSave();
}

}  // namespace flow
