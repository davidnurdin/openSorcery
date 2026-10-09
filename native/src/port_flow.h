// Game flow (boot, attract mode, title, hall of fame, game start / game over, name entry) - shared declarations.
//
// The original code of this area is *sequential and blocking*: it spins on counters that the interrupt handler (8D55)
// changes, or burns CPU time in empty loops.  Here it is straight-line C++ running as the game task (Machine::startGame):
//
//     flow::waitIrq()          sleep until the next interrupt has been serviced (the original `halt`)
//     flow::waitTick()         wait for the 50 Hz game tick (original 7BE9 busy loop on bit 5 of (iy+74))
//     flow::timedLoop(...)     model of an empty delay loop of the original (see its comment)
#pragma once
#include "native.h"
#include "gamemem.h"
#include <cstdint>
#include <functional>
#include <vector>

namespace flow {

// ------------------------------------------------------------------------------------------------------------------
// RAM map of the area (Z80 addresses).  (iy+n) is 0x6800+n; IY is constantly 0x6800 in this part of the game.
// ------------------------------------------------------------------------------------------------------------------
namespace ram {
constexpr uint16_t IY = 0x6800;
// (iy+n) flag bytes
constexpr uint16_t TICK_COUNTER = IY + 72;     // decremented by every interrupt; bit 7 = sprite engine tick (8A97 waits for it)
constexpr uint16_t JOY_EDGE = IY + 73;
constexpr uint16_t IRQ_FLAGS = IY + 74;        // bit 5 = 50 Hz tick set by the interrupt handler (waited for by 7BE9)
constexpr uint16_t DEATH_CAUSE = IY + 79;      // bit 0/1/2 = killed by (text 97E8/980D/9832), bit 6 = game won, bit 7 = dead
constexpr uint16_t PLAYER_FLAGS = IY + 80;
constexpr uint16_t DEMO_FLAG = IY + 81;        // bit 7 = demo game (attract mode) running
constexpr uint16_t JOY_STATE = IY + 82;        // joystick as read by the handler: bit 2 left, bit 3 right, bit 4 fire
constexpr uint16_t DEMO_ALT = IY + 85;         // toggles between the two demo scripts
constexpr uint16_t LOOP_MODE = IY + 86;        // bit 7: key held at boot -> attract screens loop forever (no demo, no music)
constexpr uint16_t CHEAT_ON = IY + 87;         // bit 7: initiation (practice) mode selected for this game
constexpr uint16_t CHEAT_AVAIL = IY + 88;      // bit 7: game was won once -> initiation mode may be selected

constexpr uint16_t IRQ_TABLE_PTR = 0x92F5;     // table of the 6 interrupt phase handlers in use
constexpr uint16_t IRQ_PHASE = 0x92F7;
constexpr uint16_t IRQ_TABLE_MENU = 0x92E9;    // phases used in menus / attract / game over
constexpr uint16_t IRQ_TABLE_GAME = 0x92DD;    // phases used while playing
constexpr uint16_t FW_IRQ_VECTOR = 0x9350;     // address the interrupt handler chains to (firmware IRQ routine)
constexpr uint16_t PAL_MENU = 0x9308;          // 16 inks + border
constexpr uint16_t PAL_PICTURE = 0x930C;
constexpr uint16_t PAL_SCENE = 0x92F8;
constexpr uint16_t ROOM = 0x9310;              // current room number
constexpr uint16_t SEED = 0x9311;              // pseudo random seed, stirred by delay loops and key timing
constexpr uint16_t SCORE = 0x92D4;             // 3 bytes BCD, most significant first
constexpr uint16_t SAVED_COUNT = 0x95E4;       // sorcerers rescued
constexpr uint16_t FILE_LIST_TITLE = 0x94A5;   // resource lists understood by 91F8: {len, name ptr, load addr}...
constexpr uint16_t FILE_LIST_GAME = 0x94B5;
constexpr uint16_t FILE_LIST_PICTURE = 0x94D0;
constexpr uint16_t FILE_LIST_GAME_ALT = 0x94D6;
constexpr uint16_t SCROLL_LIST = 0x95CC;       // pointer into the table of title scroller messages (words, offsets from 0A00)
constexpr uint16_t SCROLL_TEXT = 0x95CA;       // pointer to the next character of the current message
constexpr uint16_t SCROLL_GLYPH = 0x95CE;      // 8 bytes: bit pattern of the character being scrolled in
constexpr uint16_t SCROLL_GLYPH_PTR = 0x95D6;
constexpr uint16_t TILE_MAP = 0x6C90;
// hall of fame
constexpr uint16_t HALL_TODAY = 0x9522;        // "greatest sorcerers of today", 7 entries, lost at power off
constexpr uint16_t HALL_ALL_TIME = 0x98EF;     // "greatest sorcerer of all times", 7 entries, saved on disc
constexpr uint16_t DISC_BUF = 0x5600;          // sector buffer used for the saved table
constexpr uint16_t NAME_COMPACT_LEN = 0x98A4;
constexpr uint16_t NAME_LETTER = 0x98A5;       // selected entry of LETTERS
constexpr uint16_t NAME_LEN = 0x98A6;          // characters typed so far
constexpr uint16_t NAME_BUF = 0x98A7;          // 16 characters, '.' padded
constexpr uint16_t NAME_DEFAULT = 0x98BC;      // text used when nothing (or a forbidden word) was typed ("QUI EST LA?")
constexpr uint16_t NAME_COMPACT = 0x9894;      // name without the padding dots
constexpr uint16_t LETTERS = 0x98C7;           // 30 selectable characters: '<' '>' '!' 'A'..'Z' '.'
constexpr uint16_t DICT_INDEX = 0x9EDF;
constexpr uint16_t DICT = 0x9EE0;              // forbidden names: {len, chars...} ... 0
constexpr uint16_t DICT_REPLACEMENTS = 0x9FEC; // replacement texts, bit 7 marks the last character, same order as DICT
// AMSDOS far-call table (3 bytes each: address, ROM) built at boot from RSX commands 0x81..0x89
constexpr uint16_t FAR_TABLE = 0x9997;
constexpr uint16_t FAR_SET_MESSAGE = 0x9997;
constexpr uint16_t FAR_SELECT_FORMAT = 0x999D;
constexpr uint16_t FAR_READ_SECTOR = 0x99A0;
constexpr uint16_t FAR_WRITE_SECTOR = 0x99A3;
}  // namespace ram

constexpr int HALL_ENTRIES = 7;
constexpr int HALL_ENTRY_SIZE = 24;            // 21 name characters ('.' padded) + 3 BCD score bytes
constexpr int HALL_NAME_SIZE = 21;
constexpr int NAME_MAX = 16;
constexpr int HALL_BYTES = HALL_ENTRIES * HALL_ENTRY_SIZE;   // 168 = 0xA8
constexpr uint8_t HALL_TRACK = 39, HALL_SECTOR_ID = 0xC9;    // where the all-time table is stored on the disc

// ------------------------------------------------------------------------------------------------------------------
// Task / timing API (port_flow_task.cpp)
// ------------------------------------------------------------------------------------------------------------------

Machine& mach();                               // the machine the task is running on
void bind(Machine& m);                         // makes mach() valid
void waitIrq();
void waitTick();
uint64_t now();                                // T-states since power on (frame based clock)
bool fireHeld();                               // joystick fire / (iy+82) bit 4

// Models `iterations` passes of a busy loop that costs `cyclesPerIteration` T-states per pass.  `body(i)` is run once per
// pass (it must be cheap) - the passes that "fit" into the time elapsed since the last interrupt are run in one go, then the
// task sleeps.  With stopOnFire the loop ends as soon as the joystick fire bit is seen (like the original `bit 4,(iy+82)` test).
// Returns the number of passes executed.
uint32_t timedLoop(uint32_t iterations, double cyclesPerIteration, bool stopOnFire, const std::function<void(uint32_t)>& body = nullptr);


// ------------------------------------------------------------------------------------------------------------------
// Helpers shared by the flow files
// ------------------------------------------------------------------------------------------------------------------
inline uint8_t& at(uint16_t a) { return mach().ram[a]; }
inline uint16_t word(uint16_t a) { return mach().ram[a] | mach().ram[(uint16_t)(a + 1)] << 8; }
inline void setWord(uint16_t a, uint16_t v) { mach().ram[a] = v & 0xFF; mach().ram[(uint16_t)(a + 1)] = v >> 8; }
inline void call(uint16_t addr) { mach().call(addr); }

void printAt(uint16_t screenPos, const char* text);   // print ASCII text at a text position (8CFC per character)
void printChar(char c);                               // print one character at the current position (DE)
void setTextMask(uint8_t mask);
void setMode(int mode);                               // SCR SET MODE (also clears the screen)
void setPalette(uint16_t table, int a = 0);           // 8F3C
void farCall(uint16_t farEntry);                      // AMSDOS call through the far-call table (registers in/out as the firmware)

// hall of fame (port_flow_score.cpp)
struct HallRank {
    bool found;                                       // the score beats an entry of the table
    int rank;                                         // 0..6 (7 when not found)
    uint16_t entry;                                   // address of that entry
};
bool showHallTable(uint16_t table);                   // 71D6: true if fire was pressed
HallRank hallRank(uint16_t table);                    // 8894
void hallShiftDown(uint16_t tableEnd, int rank);      // 88BC
void sanitizeName();                                  // 88CC
void printName();                                     // 8866
void nameEntryAndSave();                              // 8700..8863

enum class Start { Game, Demo };
struct GameEnd { bool victory = false; bool fire = false; };
GameEnd roomLoop(bool demo);                          // 7457: the in-game loop; returns when the life/demo/game is over
void gameMain(Machine& m);                            // the whole program: boot, attract mode, games (never returns)
Start attractMode();                                  // 70C4: attract loop until a game or the demo starts
bool playGame(bool demo);                             // 72A9..: one game; for a demo returns true when fire was pressed
void victory();                                       // 8955
void gameOver();                                      // 8636

}  // namespace flow
