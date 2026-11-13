// grooves.h - Sabbath Kit's built-in doom / stoner / sludge grooves.
//
// SPDX-License-Identifier: MIT
//
// Each groove is written as a drum grid, one line per instrument (GM note), one string per bar.
// A bar string's length sets its resolution: 16 = sixteenths, 12 = eighth-note triplets (12/8 feel),
// 8 = eighths, 24 = sixteenth triplets.
//   X = accent (124)   x = normal (100)   o = soft (76)   g = ghost (44)   . = rest
//   F = flam (a soft grace note just before an accent)
// Every groove has MAIN bars (looped) and up to three alternative FILL bars, played in turn on the last bar
// of every 4 or 8 when Fill is on; the next downbeat then gets a crash.
//
// Used by SabbathKit.cpp (playback) and gonzi/tools/export_grooves.cpp (MIDI files for the MPC).
// Parsing allocates: do it once, outside the audio thread.

#ifndef SABBATH_GROOVES_H
#define SABBATH_GROOVES_H

#include <algorithm>
#include <cstdint>
#include <cstring>
#include <string>
#include <vector>

namespace sabbath {

const int kTicksPerBeat = 96;            // 4 beats per bar
const int kTicksPerBar  = 4 * kTicksPerBeat;

struct Line { int note; const char* bars[4]; };            // up to 4 bars, nullptr = unused
struct GrooveDef
{
    const char* name;
    int mainBars;
    Line main[10];                                          // note 0 terminates
    Line fill[8];
};

// GM notes used: 36 kick, 38 snare, 40 snare edge, 37 cross-stick, 42 closed hat, 44 pedal hat,
// 46 open hat, 48 tom1 (14" rack), 45 tom2 (16" floor), 41 tom3 (18" floor), 49 crash A20,
// 57 crash K17, 55 crash Paiste 2002, 51 ride, 53 ride bell, 59 ride shank (wash), 54 tambourine.

// Fill sets: each line holds up to three alternative fill bars (variants 0, 1, 2), played in turn.
#define FILLS_STRAIGHT { \
    { 42, { "x.x.x.x.........", "x..x........", nullptr } }, \
    { 36, { "X.....x.........", "X.......x..x", "X...x...X...x..." } }, \
    { 38, { "........Xxox....", "......X..X..", "F.....F...F.XxXX" } }, \
    { 48, { "............xx..", ".......x....", nullptr } }, \
    { 45, { "..............x.", "..........x.", nullptr } }, \
    { 41, { "...............X", nullptr, "..xx....xx......" } }, {0,{}} }
#define FILLS_TRIPLET { \
    { 42, { "x.ox.o......", "x.ox.o......", nullptr } }, \
    { 36, { "X.....X.....", "X....x..x..x", "X.....x.....X.....x....." } }, \
    { 38, { "...X..xxx...", "...X..X..X..", "......X...........ooxxXX" } }, \
    { 48, { ".........xx.", ".......x....", nullptr } }, \
    { 45, { nullptr, "..........x.", nullptr } }, \
    { 41, { "...........X", nullptr, "............x..x........" } }, {0,{}} }
#define FILLS_SLOW { \
    { 55, { "X...........", nullptr, nullptr } }, \
    { 36, { "X.....x.x.x.", "X.....x...x.x...", "X.......x.x.x.x." } }, \
    { 38, { "...F........", "........F.....F.", "........o.x.X.X." } }, \
    { 45, { "......X.....", nullptr, nullptr } }, \
    { 41, { "........X.X.", "............x...", "........o.x.X.X." } }, {0,{}} }
#define FILLS_TRIBAL { \
    { 36, { "X...X...X...X...", "X..X..X..X..", "X...x...X...x..." } }, \
    { 38, { nullptr, nullptr, "F.......F...XXXX" } }, \
    { 48, { "xx..xx..x.......", "xxx.........", nullptr } }, \
    { 45, { "..xx..xx.x......", "...xxx......", nullptr } }, \
    { 41, { "..........xxXXXX", "......xxxXXX", "..x.x.x...x....." } }, {0,{}} }

static const GrooveDef kGrooves[] = {
    { "Iron Ride", 4,   // half-time doom: ride eighths, foot hat on 2 and 4, kick pushes before the next bar
      { { 51, { "X.o.x.o.x.o.x.o.", "X.o.x.o.x.o.x.o.", "X.o.x.o.x.o.x.o.", "X.o.x.o.x.o.x.o." } },
        { 44, { "....o.......o...", "....o.......o...", "....o.......o...", "....o.......o..." } },
        { 36, { "X.....x...x.....", "X.....x......xx.", "X.....x...x.....", "X..x..x.....x.x." } },
        { 38, { "........X.......", "........X.......", "........X......g", "........X....g.." } },
        { 49, { "X...............", nullptr, nullptr, nullptr } }, {0,{}} },
      FILLS_STRAIGHT },

    { "Wizard Wash", 4, // the big Paiste on every beat, slow and huge
      { { 55, { "X...x...X...x...", "X...x...X...x...", "X...x...X...x...", "X...x...X...x..." } },
        { 36, { "X.....x...x.....", "X.....x.x.......", "X.....x...x.....", "X..x..x...x...x." } },
        { 38, { "........X.......", "........X.......", "........X.......", "........X...X..." } }, {0,{}} },
      FILLS_SLOW },

    { "Funeral", 4,     // funeral-doom crawl: almost nothing, each hit matters
      { { 49, { "X...............", nullptr, nullptr, nullptr } },
        { 51, { "....o...x...o...", "x...o...x...o...", "x...o...x...o...", "x...o...x...o..." } },
        { 36, { "X...............", "X.............x.", "X.........x.....", "X.....x.......x." } },
        { 38, { "........X.......", "........X.......", "........F.......", "........X......." } },
        { 41, { nullptr, "............o...", nullptr, "............x..." } }, {0,{}} },
      FILLS_SLOW },

    { "Stoner Shuffle", 4, // 12/8 shuffle, accented hats, ghosted snare
      { { 42, { "X.oX.oX.oX.o", "X.oX.oX.oX..", "X.oX.oX.oX.o", "X.oX.oX.oX.." } },
        { 46, { nullptr, "...........o", nullptr, "...........o" } },
        { 36, { "X....xX.....", "X....xX..x..", "X....xX.....", "X.x..xX....x" } },
        { 38, { "...X.....X..", "...X.....X.g", "...X.g...X..", "...X.....X.g" } }, {0,{}} },
      FILLS_TRIPLET },

    { "Desert Rock", 4, // driving straight eighths, open hat lifting into the next bar
      { { 42, { "X.x.X.x.X.x.X.x.", "X.x.X.x.X.x.X...", "X.x.X.x.X.x.X.x.", "X.x.X.x.X.x.X..." } },
        { 46, { nullptr, "..............x.", nullptr, "..............x." } },
        { 36, { "X.x.....X.x.....", "X.x....xX.x.....", "X.x.....X.x.....", "X.x....xX.x...x." } },
        { 38, { "....X.......X...", "....X..g....X...", "....X.......X..g", "....X..g....X.x." } }, {0,{}} },
      FILLS_STRAIGHT },

    { "Sabbath Gallop", 4, // galloping floor tom, foot hat on the beat
      { { 41, { "x.xx.xx.xx.x", "x.xx.xx.xx.x", "x.xx.xx.xx.x", "x.xx.xx.xx.x" } },
        { 44, { "o..o..o..o..", "o..o..o..o..", "o..o..o..o..", "o..o..o..o.." } },
        { 36, { "X.....X.....", "X.....X..x..", "X.....X.....", "X..x..X..x.." } },
        { 38, { "...X.....X..", "...X.....X..", "...X.....X..", "...X.....X.x" } },
        { 49, { "X...........", nullptr, nullptr, nullptr } },
        { 57, { nullptr, nullptr, "X...........", nullptr } }, {0,{}} },
      FILLS_TRIPLET },

    { "Sludge Toms", 4, // tribal floor toms, snare only on 3
      { { 45, { "x...x.....x.....", "x...x.....x...x.", "x...x.....x.....", "x...x.....x.x.x." } },
        { 41, { "..x...x.x...x.x.", "..x...x.x...x...", "..x...x.x...x.x.", "..x...x.x......." } },
        { 44, { "....o.......o...", "....o.......o...", "....o.......o...", "....o.......o..." } },
        { 36, { "X.......X.......", "X.......X......x", "X.......X.......", "X......xX......." } },
        { 38, { "........X.......", "........X.......", "........X.......", "........X......." } },
        { 57, { "X...............", nullptr, nullptr, nullptr } }, {0,{}} },
      FILLS_TRIBAL },

    { "Sludge Crush", 4, // half-time, double kicks, Paiste on 1 and 3, hats muttering in between
      { { 55, { "X.......x.......", "x.......x.......", "X.......x.......", "x.......x......." } },
        { 42, { "..o.o.o...o.o.o.", "..o.o.o...o.o.o.", "..o.o.o...o.o.o.", "..o.o.o...o.o.o." } },
        { 36, { "XX....x.xX......", "XX....x.xX....x.", "XX....x.xX......", "XX.x..x.xX..x.x." } },
        { 38, { "........X.......", "........X.......", "........X.....g.", "........X......." } }, {0,{}} },
      FILLS_SLOW },

    { "Slow Blues Doom", 4, // 12/8 ride triplets, heavy backbeat, ghost notes
      { { 51, { "XooxooXooxoo", "XooxooXooxoo", "XooxooXooxoo", "XooxooXooxoo" } },
        { 44, { "...o.....o..", "...o.....o..", "...o.....o..", "...o.....o.." } },
        { 36, { "X.....X..x..", "X.....X.....", "X....xX..x..", "X.....X.x..x" } },
        { 38, { "...X.....X..", "...X.g...X..", "...X.....X..", "...X.g...X.g" } }, {0,{}} },
      FILLS_TRIPLET },

    { "Tribal Ritual", 4, // toms, kick and tambourine, no backbeat
      { { 48, { "x...x.....x.x...", "x...x.....x.x...", "x...x.....x.x...", "x...x.....x.x..." } },
        { 45, { "..x...x.x.......", "..x...x.x....x..", "..x...x.x.......", "..x...x.x....x.." } },
        { 41, { "X.......X...x.x.", "X.......X.....xx", "X.......X...x.x.", "X.......X...xxXX" } },
        { 36, { "X.......X.......", "X.......X.......", "X.......X.......", "X.......X.....x." } },
        { 54, { "....x.......x...", "....x.......x...", "....x.......x...", "....x.......x..." } }, {0,{}} },
      FILLS_TRIBAL },

    { "Levee Stomp", 4, // heavy half-time shuffle, triplet kick pushes
      { { 42, { "x.ox.ox.ox.o", "x.ox.ox.ox.o", "x.ox.ox.ox.o", "x.ox.ox.ox.o" } },
        { 36, { "X.x........x", "X.x....x.x..", "X.x........x", "X.x..x.x.x.." } },
        { 38, { "......X.....", "......X.....", "......X.....", "......X....g" } }, {0,{}} },
      FILLS_TRIPLET },

    { "Fuzz Pocket", 4, // stoner pocket: syncopated kick, ghost notes, tambourine
      { { 42, { "X.x.X.x.X.x.X.x.", "X.x.X.x.X.x.X.x.", "X.x.X.x.X.x.X.x.", "X.x.X.x.X.x.X.x." } },
        { 54, { "....x.......x...", "....x.......x...", "....x.......x...", "....x.......x..." } },
        { 36, { "X..x..x.X.....x.", "X..x..x.X..x....", "X..x..x.X.....x.", "X..x..x.X..x..x." } },
        { 38, { "....X..g....X...", "....X..g.g..X..g", "....X..g....X...", "....X..g.g..X.x." } }, {0,{}} },
      FILLS_STRAIGHT },
};

const int kNumGrooves = (int)(sizeof(kGrooves) / sizeof(kGrooves[0]));

struct Event { uint16_t tick; uint8_t note; uint8_t vel; };
struct ParsedGroove
{
    std::string name;
    std::vector<std::vector<Event>> bars;   // main bars
    std::vector<std::vector<Event>> fills;  // 1 to 3 alternatives
};

inline int charVel(char c)
{
    switch (c) { case 'X': return 124; case 'x': return 100; case 'o': return 76; case 'g': return 44; default: return 0; }
}

inline void parseBar(int note, const char* s, std::vector<Event>& out)
{
    if (s == nullptr) return;
    const int n = (int)std::strlen(s);
    if (n == 0) return;
    for (int i = 0; i < n; ++i)
    {
        if (s[i] == 'F')
        {
            const int t = i * kTicksPerBar / n;
            if (t >= 4) out.push_back({ (uint16_t)(t - 4), (uint8_t)note, (uint8_t)52 });
            out.push_back({ (uint16_t)t, (uint8_t)note, (uint8_t)124 });
            continue;
        }
        const int v = charVel(s[i]);
        if (v > 0) out.push_back({ (uint16_t)(i * kTicksPerBar / n), (uint8_t)note, (uint8_t)v });
    }
}

inline void sortBar(std::vector<Event>& b)
{
    std::stable_sort(b.begin(), b.end(), [](const Event& a, const Event& c) { return a.tick < c.tick; });
}

inline std::vector<ParsedGroove> parseAll()
{
    std::vector<ParsedGroove> all;
    for (int g = 0; g < kNumGrooves; ++g)
    {
        const GrooveDef& d = kGrooves[g];
        ParsedGroove p; p.name = d.name;
        p.bars.resize((size_t)d.mainBars);
        for (int l = 0; l < 10 && d.main[l].note != 0; ++l)
            for (int b = 0; b < d.mainBars; ++b) parseBar(d.main[l].note, d.main[l].bars[b], p.bars[(size_t)b]);
        for (int v = 0; v < 3; ++v)
        {
            std::vector<Event> f;
            for (int l = 0; l < 8 && d.fill[l].note != 0; ++l) parseBar(d.fill[l].note, d.fill[l].bars[v], f);
            if (!f.empty()) { sortBar(f); p.fills.push_back(f); }
        }
        for (auto& b : p.bars) sortBar(b);
        all.push_back(p);
    }
    return all;
}

} // namespace sabbath

#endif
