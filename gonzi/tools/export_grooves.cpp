// export_grooves.cpp - write Sabbath Kit's built-in grooves as Standard MIDI Files for the MPC.
// Usage: export_grooves <out_dir> [bpm]
// Each file: 8 bars of 4/4 (main bars looped, fill on bars 4 and 8, crash on bar 5), GM drum notes,
// MIDI channel 1, 96 ticks per quarter. Load on the MPC with Browser -> the .mid -> Load as a sequence,
// then set the track's program to Sabbath Kit.
#include <cstdio>
#include <cstdlib>
#include <string>
#include <vector>
#include <algorithm>
#include "../../plugins/Sabbath Kit/src/grooves.h"

static void putVar(std::vector<unsigned char>& o, unsigned v)
{
    unsigned char b[5]; int n = 0;
    b[n++] = v & 0x7F;
    while (v >>= 7) b[n++] = (unsigned char)(0x80 | (v & 0x7F));
    while (n--) o.push_back(b[n]);
}
static void put32(std::vector<unsigned char>& o, unsigned v) { for (int i = 3; i >= 0; --i) o.push_back((unsigned char)(v >> (8 * i))); }
static void put16(std::vector<unsigned char>& o, unsigned v) { o.push_back((unsigned char)(v >> 8)); o.push_back((unsigned char)v); }

int main(int argc, char** argv)
{
    if (argc < 2) { fprintf(stderr, "usage: export_grooves <out_dir> [bpm]\n"); return 1; }
    const double bpm = argc > 2 ? atof(argv[2]) : 70.0;
    const auto grooves = sabbath::parseAll();
    int idx = 0;
    for (const auto& g : grooves)
    {
        ++idx;
        struct Ev { unsigned t; int on; int note; int vel; };
        std::vector<Ev> evs;
        const int bars = 8;
        for (int bar = 0; bar < bars; ++bar)
        {
            const bool fill = (bar % 4) == 3;
            const auto& src = fill ? g.fill : g.bars[(size_t)(bar % (int)g.bars.size())];
            bool crash0 = false;
            for (const auto& e : src) if (e.tick == 0 && (e.note == 49 || e.note == 55 || e.note == 57)) crash0 = true;
            const unsigned base = (unsigned)(bar * sabbath::kTicksPerBar);
            if (bar == 4 && !crash0) { evs.push_back({base, 1, 49, 118}); evs.push_back({base + 24, 0, 49, 0}); }
            for (const auto& e : src)
            {
                evs.push_back({base + e.tick, 1, e.note, e.vel});
                evs.push_back({base + e.tick + 24, 0, e.note, 0});   // short notes: drums are one-shots
            }
        }
        std::stable_sort(evs.begin(), evs.end(), [](const Ev& a, const Ev& b) { return a.t != b.t ? a.t < b.t : a.on < b.on; });

        std::vector<unsigned char> trk;
        // track name + tempo + 4/4
        putVar(trk, 0); trk.push_back(0xFF); trk.push_back(0x03); putVar(trk, (unsigned)g.name.size());
        trk.insert(trk.end(), g.name.begin(), g.name.end());
        const unsigned uspq = (unsigned)(60000000.0 / bpm);
        putVar(trk, 0); trk.push_back(0xFF); trk.push_back(0x51); trk.push_back(3);
        trk.push_back((unsigned char)(uspq >> 16)); trk.push_back((unsigned char)(uspq >> 8)); trk.push_back((unsigned char)uspq);
        putVar(trk, 0); trk.push_back(0xFF); trk.push_back(0x58); trk.push_back(4);
        trk.push_back(4); trk.push_back(2); trk.push_back(24); trk.push_back(8);
        unsigned last = 0;
        for (const auto& e : evs)
        {
            putVar(trk, e.t - last); last = e.t;
            trk.push_back(e.on ? 0x90 : 0x80); trk.push_back((unsigned char)e.note); trk.push_back((unsigned char)(e.on ? e.vel : 0));
        }
        const unsigned end = (unsigned)(bars * sabbath::kTicksPerBar);
        putVar(trk, end > last ? end - last : 0); trk.push_back(0xFF); trk.push_back(0x2F); trk.push_back(0);

        std::vector<unsigned char> f;
        f.insert(f.end(), {'M', 'T', 'h', 'd'}); put32(f, 6); put16(f, 0); put16(f, 1); put16(f, sabbath::kTicksPerBeat);
        f.insert(f.end(), {'M', 'T', 'r', 'k'}); put32(f, (unsigned)trk.size());
        f.insert(f.end(), trk.begin(), trk.end());

        char path[512];
        snprintf(path, sizeof path, "%s/%02d %s.mid", argv[1], idx, g.name.c_str());
        FILE* fp = fopen(path, "wb");
        if (!fp) { perror(path); return 1; }
        fwrite(f.data(), 1, f.size(), fp); fclose(fp);
        printf("wrote %s\n", path);
    }
    return 0;
}
