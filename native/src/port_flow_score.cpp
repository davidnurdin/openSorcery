// Hall of fame: the two score tables, the "enter your name" screen and the saving of the all-time table to the disc.
//
// Table format (both tables, 7 entries, 168 bytes):  { char name[21]; uint8_t bcd[3]; }  = 24 bytes per entry,
// sorted by score, best first.  Names are upper case ASCII padded with '.', scores are 6 packed BCD digits (most
// significant byte first, so that a byte wise comparison orders them).  The "today" table (9522) only lives in RAM; the
// "all times" table (98EF) is read from track 39, sector id C9 (the 512 byte sector holds the 168 bytes at its start) when
// the game starts and written back there whenever a score enters it.  The disc is accessed through the AMSDOS far calls
// 4 (read sector) / 5 (write sector) found at boot, registers: HL = buffer, D = track, E = drive, C = sector id.
#include "port_flow.h"
#include <cstdlib>
#include <cstring>

namespace flow {
using namespace ram;

namespace {
constexpr uint16_t CHAR_PRINT = 0x8CFC;          // text engine: print character A at the text position DE (DE advances)
constexpr uint16_t PRINT_BCD_BYTE = 0x91E8;      // prints the BCD byte at (HL) as 2 digits, HL++
constexpr uint16_t TEXT_NAME_ENTRY = 0x96B4;     // "COMPLIMENTS! VOTRE SCORE EST ELEVE! ENTREZ VOTRE NOM"
constexpr uint16_t POS_LETTERS = 0x0329, POS_LETTER_MARK = 0x0379, POS_NAME = 0x0427, POS_NAME_MARK = 0x0477;
constexpr int NUM_LETTERS = 30;
constexpr char LETTER_BACKSPACE = '<', LETTER_END = '>';
constexpr char MARKER = '^';
constexpr int JOY_LEFT = 0x04, JOY_RIGHT = 0x08, JOY_FIRE = 0x10;

void markLetter(Machine& m, char ch) {                // the cursor under the letter table
    m.cpu.setDE(POS_LETTER_MARK + 2 * at(NAME_LETTER));
    printChar(ch);
}

void markNamePos(Machine& m, char ch) {              // the cursor under the typed name
    m.cpu.setDE(POS_NAME_MARK + 2 * at(NAME_LEN));
    printChar(ch);
}

}  // namespace


// ---------------------------------------------------------------------------------------------------------------------
// 8866: print the 16 character name buffer at the name position
// ---------------------------------------------------------------------------------------------------------------------
void printName() {
    Machine& m = mach();
    m.cpu.setDE(POS_NAME);
    m.cpu.setHL(NAME_BUF);
    for (m.cpu.b = NAME_MAX; m.cpu.b != 0; --m.cpu.b) {
        m.cpu.a = at(m.cpu.hl());
        call(CHAR_PRINT);
        m.cpu.setHL(m.cpu.hl() + 1);
    }
}
NATIVE(0x8866, printNameNative, "a bc de hl", 100) { printName(); }

// 888D / 888E: DE += 2*A / DE += A (screen position of the n-th character of a row)
NATIVE(0x888D, charPosTimes2, "de", 34) { m.cpu.setDE(m.cpu.de() + (uint8_t)(2 * m.cpu.a)); }
NATIVE(0x888E, charPos, "de", 30) { m.cpu.setDE(m.cpu.de() + m.cpu.a); }

// ---------------------------------------------------------------------------------------------------------------------
// 8894: where does the current score (92D4..92D6) go in the table whose first entry is at `entry`?
// ---------------------------------------------------------------------------------------------------------------------
static HallRank hallRankFrom(Machine& m, uint16_t entry) {
    for (int rank = 0; rank < HALL_ENTRIES; ++rank, entry += HALL_ENTRY_SIZE) {
        for (int i = 0; i < 3; ++i) {
            uint8_t theirs = m.ram[entry + HALL_NAME_SIZE + i], ours = m.ram[SCORE + i];
            if (theirs < ours) return {true, rank, entry};     // we beat this entry: insert here
            if (theirs > ours) break;                           // better entry: look at the next one
        }                                                       // (equal scores: the older entry stays above)
    }
    return {false, HALL_ENTRIES, entry};
}
HallRank hallRank(uint16_t table) { return hallRankFrom(mach(), table); }

// in: IX = first entry  out: carry = found, C = rank, IX = entry
NATIVE(0x8894, hallRankNative, "cf bc ix", 290) {
    HallRank r = hallRankFrom(m, m.cpu.ix);
    m.cpu.c = (uint8_t)r.rank;
    m.cpu.ix = r.entry;
    m.cpu.f = (m.cpu.f & ~1) | (r.found ? 1 : 0);
}

// ---------------------------------------------------------------------------------------------------------------------
// 88BC: make room for a new entry at `rank` by moving the entries rank..5 one slot down (the last one drops out)
// ---------------------------------------------------------------------------------------------------------------------
void hallShiftDown(uint16_t tableEnd, int rank) {
    Machine& m = mach();
    int bytes = (HALL_ENTRIES - 1 - rank) * HALL_ENTRY_SIZE;
    uint16_t dst = tableEnd - 1, src = dst - HALL_ENTRY_SIZE;
    for (int i = 0; i < bytes; ++i) m.ram[(uint16_t)(dst - i)] = m.ram[(uint16_t)(src - i)];
}
// in: C = rank, HL = last byte of entry 5, DE = last byte of entry 6
NATIVE(0x88BC, hallShiftNative, "", 2750) {
    int left = 6 - m.cpu.c;
    if (left == 0) return;
    int bytes = left * HALL_ENTRY_SIZE;
    uint16_t src = m.cpu.hl(), dst = m.cpu.de();
    for (int i = 0; i < bytes; ++i) m.ram[(uint16_t)(dst - i)] = m.ram[(uint16_t)(src - i)];
    m.cpu.setBC(0);
    m.cpu.setHL(src - bytes);
    m.cpu.setDE(dst - bytes);
}

// ---------------------------------------------------------------------------------------------------------------------
// 88CC: the typed name is checked against a list of forbidden words (9EE0: {length, characters}..., 0).  A name that
// starts with one of them is replaced by the matching entry of the replacement list (9FEC; the first entry of that list is
// a dummy, so word n is replaced by string n+1); an empty name becomes "QUI EST LA?".
// ---------------------------------------------------------------------------------------------------------------------
static void copyTextToName(Machine& m, uint16_t src) {        // strings: bit 7 marks the last character
    uint16_t dst = NAME_BUF;
    for (;;) {
        uint8_t c = m.ram[src++];
        m.ram[dst++] = c & 0x7F;
        if (c & 0x80) break;
    }
}

void sanitizeName() {
    Machine& m = mach();
    uint8_t* r = m.ram;
    // name without the padding dots
    memset(&r[NAME_COMPACT], '.', NAME_MAX);
    int len = 0;
    for (int i = 0; i < NAME_MAX; ++i)
        if (r[NAME_BUF + i] != '.') r[NAME_COMPACT + len++] = r[NAME_BUF + i];
    r[NAME_COMPACT_LEN] = (uint8_t)len;
    // look for a forbidden word at the start of the name
    r[DICT_INDEX] = 0;
    uint16_t word = DICT;
    for (;; ++r[DICT_INDEX]) {
        int wlen = r[word];
        if (wlen == 0) break;                                   // end of the list
        ++word;
        bool match = len >= wlen && memcmp(&r[NAME_COMPACT], &r[word], wlen) == 0;
        word += wlen;
        if (!match) continue;
        memset(&r[NAME_BUF], '.', NAME_MAX);
        uint16_t text = DICT_REPLACEMENTS;
        for (int skip = r[DICT_INDEX] + 1; skip > 0; ) {         // skip DICT_INDEX+1 strings
            if (r[text++] & 0x80) --skip;
        }
        copyTextToName(m, text);
        return;
    }
    if (len == 0) copyTextToName(m, NAME_DEFAULT);
}
NATIVE(0x88CC, sanitizeNative, "", 10000) { sanitizeName(); }

// ---------------------------------------------------------------------------------------------------------------------
// 71D6: show a score table (7 rows) and wait about 4 seconds.  Returns true when fire was pressed.
// ---------------------------------------------------------------------------------------------------------------------
bool showHallTable(uint16_t table) {
    Machine& m = mach();
    uint16_t pos = 0x028D;                                       // first row; every row is two text lines lower
    uint16_t p = table;
    for (int row = 0; row < HALL_ENTRIES; ++row, p += HALL_ENTRY_SIZE, pos += 0xA0) {
        m.cpu.setDE(pos);
        setTextMask(0x0F);                                       // names in one pen ...
        for (int i = 0; i < HALL_NAME_SIZE; ++i) printChar((char)at(p + i));
        setTextMask(0xFF);                                       // ... scores in another
        m.cpu.setHL(p + HALL_NAME_SIZE);
        for (int i = 0; i < 3; ++i) call(PRINT_BCD_BYTE);
    }
    // The original waits with a loop that counts HL (= end of the table) + 3*65536 down while testing fire.  Every pass also
    // adds 0x29 to the random seed.
    uint32_t passes = (uint16_t)(table + HALL_BYTES) + 3u * 65536u;
    uint32_t done = timedLoop(passes, 79.0, true);
    at(SEED) = (uint8_t)(at(SEED) + 0x29 * done);
    return done < passes;
}

// ---------------------------------------------------------------------------------------------------------------------
// 8700..8863: the player's score entered the "today" table: let the player type a name with the joystick (move the
// cursor over the letter table, fire selects; '<' deletes, '>' ends), then insert the entry into the "today" table and,
// if good enough, into the all-time table, which is saved to the disc.
// ---------------------------------------------------------------------------------------------------------------------
void nameEntryAndSave() {
    Machine& m = mach();
    setMode(1);
    HallRank today = hallRank(HALL_TODAY);
    if (!today.found) return;

    hallShiftDown(HALL_TODAY + HALL_BYTES, today.rank);
    m.cpu.setHL(TEXT_NAME_ENTRY);
    call(0x8CE4);
    at(NAME_LETTER) = 0;
    at(NAME_LEN) = 0;
    memset(&m.ram[NAME_BUF], '.', NAME_MAX);

    // the table of selectable characters
    m.cpu.setDE(POS_LETTERS);
    for (int i = 0; i < NUM_LETTERS; ++i) printChar((char)at(LETTERS + i));

    bool done = false;
    while (!done) {
        // redraw cursor, name and name cursor
        markLetter(m, MARKER);
        printName();
        markNamePos(m, MARKER);
        for (bool again = true; again && !done; ) {
            for (int i = 0; i < 5; ++i) waitTick();              // the joystick is sampled five ticks apart
            uint8_t joy = at(JOY_STATE);
            if (joy & JOY_LEFT) {
                markLetter(m, ' ');
                at(NAME_LETTER) = at(NAME_LETTER) == 0 ? NUM_LETTERS - 1 : at(NAME_LETTER) - 1;
                again = false;
            } else if (joy & JOY_RIGHT) {
                markLetter(m, ' ');
                at(NAME_LETTER) = at(NAME_LETTER) + 1 >= NUM_LETTERS ? 0 : at(NAME_LETTER) + 1;
                again = false;
            } else if (joy & JOY_FIRE) {
                at(IY + 75) |= 0x10;                             // (flag set, never evaluated by anybody else)
                markNamePos(m, ' ');
                char ch = (char)at(LETTERS + at(NAME_LETTER));
                again = false;
                if (ch == LETTER_END) {
                    done = true;
                } else if (ch == LETTER_BACKSPACE) {
                    if (at(NAME_LEN) != 0) {
                        --at(NAME_LEN);
                        at(NAME_BUF + at(NAME_LEN)) = '.';
                    }
                } else {
                    at(NAME_BUF + at(NAME_LEN)) = (uint8_t)ch;
                    if (++at(NAME_LEN) >= NAME_MAX) done = true;
                }
            }
        }
    }

    sanitizeName();
    printName();
    for (int i = 0; i < 0x4B; ++i) waitTick();                   // show the (possibly replaced) name for a moment

    auto storeEntry = [&](uint16_t entry) {
        memcpy(&m.ram[entry], &m.ram[NAME_BUF], HALL_NAME_SIZE);
        memcpy(&m.ram[entry + HALL_NAME_SIZE], &m.ram[SCORE], 3);
    };
    storeEntry(today.entry);

    HallRank allTime = hallRank(HALL_ALL_TIME);
    if (!allTime.found) return;
    hallShiftDown(HALL_ALL_TIME + HALL_BYTES, allTime.rank);
    storeEntry(allTime.entry);

    // save the all-time table: it is the start of the sector on track 39
    memcpy(&m.ram[DISC_BUF], &m.ram[HALL_ALL_TIME], HALL_BYTES);
    m.cpu.setHL(DISC_BUF);
    m.cpu.d = HALL_TRACK;
    m.cpu.e = 0;
    m.cpu.c = HALL_SECTOR_ID;
    farCall(FAR_WRITE_SECTOR);
    return;
}

}  // namespace flow
