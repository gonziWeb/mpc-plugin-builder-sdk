// grooves.h - Sabbath Kit's built-in doom / stoner / sludge grooves.
//
// SPDX-License-Identifier: MIT
//
// Each groove is written as a drum grid, one line per instrument (GM note), one string per bar.
// A bar string's length sets its resolution: 16 = sixteenths, 12 = eighth-note triplets (12/8 feel),
// 8 = eighths, 24 = sixteenth triplets.
//   X = accent (124)   x = normal (100)   o = soft (76)   g = ghost (44)   . = rest
// Every groove has MAIN bars (looped) and one FILL bar (played on the last bar of every 4 or 8
// when Fill is on; the next downbeat then gets a crash).
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

static const GrooveDef kGrooves[] = {
    { "Iron Ride", 2,   // half-time doom, ride on quarters (70s British doom)
      { { 51, { "X...x...x...x...", "X...x...x...x..." } },
        { 36, { "X.....x.......x.", "X.....x...x....." } },
        { 38, { "........X.......", "........X......." } },
        { 49, { "X...............", nullptr } } , {0,{}} },
      { { 36, { "X..............." } }, { 38, { "........x.x.xxXX" } }, { 45, { "....x.x........." } },
        { 51, { "X...x..........." } }, {0,{}} } },

    { "Wizard Wash", 2, // crash/wash on every quarter, slow and huge
      { { 55, { "X...x...x...x...", "X...x...x...x..." } },
        { 36, { "X.....x...x.....", "X.....x.....x..." } },
        { 38, { "........X.......", "........X......." } }, {0,{}} },
      { { 36, { "X.......X......." } }, { 38, { "X...X...X.X.XXXX" } }, { 55, { "X..............." } }, {0,{}} } },

    { "Funeral", 2,     // one hit per beat at most: funeral doom crawl
      { { 49, { "X...............", "................" } },
        { 51, { "....o.......o...", "o...o...o...o..." } },
        { 36, { "X...............", "X.............x." } },
        { 38, { "........X.......", "........X......." } },
        { 41, { "................", "............o..." } }, {0,{}} },
      { { 36, { "X..............." } }, { 45, { "X...x..........." } }, { 41, { "........X...X..." } },
        { 38, { "..............XX" } }, {0,{}} } },

    { "Stoner Shuffle", 2, // 12/8 shuffle on the hats, riff-locked kick
      { { 42, { "x.xx.xx.xx.x", "x.xx.xx.xx.x" } },
        { 36, { "X....xX.....", "X....xX..x.." } },
        { 38, { "...X.....X..", "...X.....X.g" } },
        { 46, { nullptr, "...........o" } }, {0,{}} },
      { { 36, { "X..........." } }, { 38, { "x.xx.x......" } }, { 48, { "......x.x..." } },
        { 41, { ".........xXX" } }, {0,{}} } },

    { "Desert Rock", 2, // straight-eighth stoner rock, open hat lift
      { { 42, { "X.x.x.x.X.x.x.x.", "X.x.x.x.X.x.x..." } },
        { 46, { "................", "..............x." } },
        { 36, { "X.x.....X.x.....", "X.x.....X.x...x." } },
        { 38, { "....X.......X...", "....X.......X..." } }, {0,{}} },
      { { 36, { "X.......X......." } }, { 38, { "x.x.x.x.....xxxx" } }, { 48, { "........xx......" } },
        { 45, { "..........xx...." } }, { 42, { "X..............." } }, {0,{}} } },

    { "Sabbath Gallop", 2, // galloping triplet floor tom under a backbeat
      { { 41, { "x.xx.xx.xx.x", "x.xx.xx.xx.x" } },
        { 36, { "X.....X.....", "X.....X..x.." } },
        { 38, { "...X.....X..", "...X.....X.." } },
        { 49, { "X...........", nullptr } }, {0,{}} },
      { { 38, { "XxxXxx......" } }, { 45, { "......xxx..." } }, { 41, { ".........xxX" } },
        { 36, { "X.....X....." } }, {0,{}} } },

    { "Sludge Toms", 2, // tribal floor toms, snare only on 3
      { { 45, { "x...x.....x.....", "x...x.....x...x." } },
        { 41, { "..x...x.x...x.x.", "..x...x.x...x..." } },
        { 36, { "X.......X.......", "X.......X......x" } },
        { 38, { "........X.......", "........X......." } },
        { 57, { "X...............", nullptr } }, {0,{}} },
      { { 45, { "x.x.x.x........." } }, { 41, { "........x.x.x.x." } }, { 36, { "X...X...X...X..." } },
        { 38, { "..............XX" } }, {0,{}} } },

    { "Sludge Crush", 2, // half-time with double kicks and the big Paiste
      { { 55, { "X.......x.......", "X.......x.......", } },
        { 36, { "XX....x.XX......", "XX....x.XX....x." } },
        { 38, { "........X.......", "........X......." } },
        { 42, { "..o...o...o...o.", "..o...o...o...o." } }, {0,{}} },
      { { 36, { "XX..XX..XX..XX.." } }, { 38, { "..X...X...XxXxXX" } }, { 55, { "X..............." } }, {0,{}} } },

    { "Slow Blues Doom", 2, // 12/8 ride triplets, heavy backbeat
      { { 51, { "XooxooXooxoo", "XooxooXooxoo" } },
        { 36, { "X.....X..x..", "X.....X....." } },
        { 38, { "...X.....X..", "...X.....X.." } }, {0,{}} },
      { { 38, { "...X..XxxXxx" } }, { 36, { "X..........." } }, { 41, { ".........x.X" } }, {0,{}} } },

    { "Tribal Ritual", 2, // toms and kick only, no backbeat (ritual doom intro)
      { { 48, { "x...x.....x.x...", "x...x.....x.x..." } },
        { 45, { "..x...x.x.......", "..x...x.x....x.." } },
        { 41, { "X.......X...x.x.", "X.......X...xxXX" } },
        { 36, { "X.......X.......", "X.......X......." } },
        { 54, { "....x.......x...", "....x.......x..." } }, {0,{}} },
      { { 48, { "xxxx............" } }, { 45, { "....xxxx........" } }, { 41, { "........xxxxXXXX" } },
        { 36, { "X...X...X...X..." } }, {0,{}} } },

    { "Levee Stomp", 2, // heavy half-time shuffle with a triplet kick push (big-room 70s)
      { { 42, { "x.xx.xx.xx.x", "x.xx.xx.xx.x" } },
        { 36, { "X.x......X..", "X.x....x.X.." } },
        { 38, { "......X.....", "......X....." } },
        { 46, { nullptr, "...........x" } }, {0,{}} },
      { { 38, { "X.xX.x......" } }, { 45, { "......X.x..." } }, { 41, { ".........X.X" } },
        { 36, { "X.....X....." } }, {0,{}} } },

    { "Fuzz Pocket", 2, // groovy stoner pocket with tambourine
      { { 42, { "x.x.x.x.x.x.x.x.", "x.x.x.x.x.x.x.x." } },
        { 54, { "....x.......x...", "....x.......x..." } },
        { 36, { "X..x..x.X.....x.", "X..x..x.X..x...." } },
        { 38, { "....X..g....X...", "....X..g....X..g" } }, {0,{}} },
      { { 36, { "X..x..x........." } }, { 38, { "....X..g.xxxXXXX" } }, { 42, { "x.x.x.x........." } }, {0,{}} } },
};

const int kNumGrooves = (int)(sizeof(kGrooves) / sizeof(kGrooves[0]));

struct Event { uint16_t tick; uint8_t note; uint8_t vel; };
struct ParsedGroove
{
    std::string name;
    std::vector<std::vector<Event>> bars;   // main bars
    std::vector<Event> fill;
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
        for (int l = 0; l < 8 && d.fill[l].note != 0; ++l) parseBar(d.fill[l].note, d.fill[l].bars[0], p.fill);
        for (auto& b : p.bars) sortBar(b);
        sortBar(p.fill);
        all.push_back(p);
    }
    return all;
}

} // namespace sabbath

#endif
