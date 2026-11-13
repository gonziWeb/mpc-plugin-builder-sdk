// test_drum_rat.cpp - behaviour checks for Drum Rat's sequencer, kits and saved patterns.
// Build: g++ -O2 -std=c++17 -Iframework gonzi/tools/test_drum_rat.cpp -o .sdk-tools/test_drum_rat -ldl
// Run:   .sdk-tools/test_drum_rat "plugins/Drum Rat/build/native/drum-rat.so"
#include <dlfcn.h>
#include <algorithm>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <cstdint>
#include <cstring>
#include <string>
#include <vector>
#include "mpc_vst2_abi.h"

static VstTimeInfo g_time;
static double g_sr = 44100.0;
static VstIntPtr host(AEffect*, VstInt32 op, VstInt32, VstIntPtr, void*, float)
{
    if (op == audioMasterVersion) return 2400;
    if (op == audioMasterGetTime) return (VstIntPtr)&g_time;
    if (op == audioMasterGetSampleRate) return (VstIntPtr)g_sr;
    if (op == audioMasterGetBlockSize) return 128;
    return 0;
}
typedef AEffect* (*Main)(audioMasterCallback);
static Main g_main;
static int g_fail = 0, g_pass = 0;
static void check(bool ok, const char* what) { printf("%s  %s\n", ok ? "PASS" : "FAIL", what); ok ? ++g_pass : ++g_fail; }

// parameter indices (see ParamIndex in DrumRat.cpp)
enum { pMaster = 0, pKit = 1, pMach = 46, pPadMap = 57, pSeq, pPattern, pSteps, pRate, pSwing, pTrack, pPadSel, pClearTrk, pClearPat, pStep };
static float choice(int i, int n) { return n <= 1 ? 0.0f : (float)i / (float)(n - 1); }

struct Inst
{
    AEffect* fx = nullptr;
    double bpm = 120.0, beat = 0.0;
    bool playing = false;
    int block = 128;
    std::vector<float> L, R;
    std::vector<VstMidiEvent> pend;
    Inst(int blk = 128) : block(blk)
    {
        fx = g_main(host);
        fx->dispatcher(fx, effOpen, 0, 0, nullptr, 0);
        fx->dispatcher(fx, effSetSampleRate, 0, 0, nullptr, (float)g_sr);
        fx->dispatcher(fx, effSetBlockSize, 0, block, nullptr, 0);
        fx->dispatcher(fx, effMainsChanged, 0, 1, nullptr, 0);
    }
    ~Inst() { fx->dispatcher(fx, effMainsChanged, 0, 0, nullptr, 0); fx->dispatcher(fx, effClose, 0, 0, nullptr, 0); }
    void set(int i, float v) { fx->setParameter(fx, i, v); }
    float get(int i) { return fx->getParameter(fx, i); }
    void note(int n, int vel)
    {
        VstMidiEvent m; memset(&m, 0, sizeof m);
        m.type = kVstMidiType; m.byteSize = sizeof m; m.midiData[0] = (char)0x90; m.midiData[1] = (char)n; m.midiData[2] = (char)vel;
        pend.push_back(m);
    }
    void run(double seconds)
    {
        size_t total = (size_t)(seconds * g_sr);
        std::vector<float> l(block), r(block);
        for (size_t pos = 0; pos < total; pos += block)
        {
            const int n = (int)std::min<size_t>(block, total - pos);
            if (!pend.empty())
            {
                std::vector<char> store(sizeof(VstEvents) + sizeof(VstEvent*) * pend.size());
                VstEvents* e = (VstEvents*)store.data();
                e->numEvents = (VstInt32)pend.size();
                for (size_t k = 0; k < pend.size(); ++k) e->events[k] = (VstEvent*)&pend[k];
                fx->dispatcher(fx, effProcessEvents, 0, 0, e, 0);
            }
            memset(&g_time, 0, sizeof g_time);
            g_time.sampleRate = g_sr; g_time.tempo = bpm; g_time.ppqPos = beat;
            g_time.timeSigNumerator = 4; g_time.timeSigDenominator = 4;
            g_time.flags = kVstTempoValid | kVstPpqPosValid | kVstTimeSigValid | (playing ? kVstTransportPlaying : 0);
            float* outs[2] = {l.data(), r.data()};
            std::fill(l.begin(), l.end(), 0.0f); std::fill(r.begin(), r.end(), 0.0f);
            fx->processReplacing(fx, nullptr, outs, n);
            pend.clear();
            L.insert(L.end(), l.begin(), l.begin() + n); R.insert(R.end(), r.begin(), r.begin() + n);
            if (playing) beat += n / g_sr * bpm / 60.0;
        }
    }
    float peak(size_t from = 0) const { float p = 0; for (size_t i = from; i < L.size(); ++i) p = std::max(p, std::max(std::fabs(L[i]), std::fabs(R[i]))); return p; }
    // onsets: samples where the level rises above thr after at least `gap` seconds below it
    std::vector<double> onsets(float thr, double gap) const
    {
        std::vector<double> o; long last = -1000000;
        for (size_t i = 0; i < L.size(); ++i)
            if (std::fabs(L[i]) > thr || std::fabs(R[i]) > thr)
            {
                if ((long)i - last > (long)(gap * g_sr)) o.push_back(i / g_sr);
                last = (long)i;
            }
        return o;
    }
    std::vector<uint8_t> save() { void* p = nullptr; VstIntPtr n = fx->dispatcher(fx, effGetChunk, 0, 0, &p, 0); return std::vector<uint8_t>((uint8_t*)p, (uint8_t*)p + n); }
    void load(const std::vector<uint8_t>& c) { fx->dispatcher(fx, effSetChunk, 0, (VstIntPtr)c.size(), (void*)c.data(), 0); }
    int step(int st) { return (int)std::lround(get(pStep + st) * 2.0f); }
};

// only the kick of a pattern: every other track's level to 0
static void soloKick(Inst& a) { for (int t = 1; t < 11; ++t) a.set(9 + 3 * t, 0.0f); a.set(11, 0.0f); }   // and its Decay at minimum

static bool kicksOnBeats(int block, double sr, int kit)
{
    g_sr = sr;
    Inst a(block);
    a.set(pKit, choice(kit, 3)); soloKick(a);
    a.set(pSeq, choice(1, 3));              // Song
    a.playing = true; a.run(2.0);           // 120 bpm: kicks at 0, 0.5, 1.0, 1.5 s
    auto o = a.onsets(0.08f, 0.12);
    bool ok = o.size() == 4;
    for (size_t i = 0; ok && i < 4; ++i) ok = std::fabs(o[i] - 0.5 * i) < 0.004;
    if (!ok) { printf("      onsets:"); for (double t : o) printf(" %.4f", t); printf("\n"); }
    g_sr = 44100.0;
    return ok;
}

int main(int argc, char** argv)
{
    if (argc < 2) { fprintf(stderr, "usage: test_drum_rat plugin.so\n"); return 1; }
    void* lib = dlopen(argv[1], RTLD_NOW);
    if (!lib) { fprintf(stderr, "%s\n", dlerror()); return 1; }
    g_main = (Main)dlsym(lib, "VSTPluginMain");

    {   // 1-3: Song mode puts the kicks on the beats, at any block size, both kits, 48 kHz too
        check(kicksOnBeats(128, 44100, 0), "Song: 808 kicks on the four beats (block 128)");
        check(kicksOnBeats(32, 44100, 1),  "Song: 909 kicks on the four beats (block 32)");
        check(kicksOnBeats(512, 44100, 0), "Song: 808 kicks on the four beats (block 512)");
        check(kicksOnBeats(128, 48000, 1), "Song: 909 kicks on the four beats at 48 kHz (resampled engine)");
        check(kicksOnBeats(128, 48000, 0), "Song: 808 kicks on the four beats at 48 kHz");
    }
    {   // Seq Off: the transport alone makes no sound; Song with the MPC stopped is silent too
        Inst a; a.playing = true; a.run(1.0);
        check(a.peak() == 0.0f, "Seq Off: silent while the MPC plays");
        Inst b; b.set(pSeq, choice(1, 3)); b.run(1.0);
        check(b.peak() == 0.0f, "Seq Song: silent while the MPC is stopped");
    }
    {   // Run: plays with the MPC stopped, from step 1
        Inst a; soloKick(a); a.set(pSeq, choice(2, 3)); a.run(2.0);
        auto o = a.onsets(0.08f, 0.12);
        check(o.size() == 4 && o[0] < 0.01 && std::fabs(o[1] - 0.5) < 0.004, "Seq Run: plays with the MPC stopped, at its tempo");
    }
    {   // pads: note 36 is the kick, velocity matters, 127 = accent; pad map GM
        Inst a; a.note(36, 88); a.run(0.5); const float soft = a.peak();
        Inst b; b.note(36, 127); b.run(0.5); const float hard = b.peak();
        check(soft > 0.05f && hard > soft * 1.3f, "pads: C plays the kick, an accent is louder");
        Inst c; c.note(40, 100); c.run(0.3); Inst d; d.set(pPadMap, 1.0f); d.note(40, 100); d.run(0.3);
        Inst e; e.note(40, 100); e.run(0.3);
        bool same = true, diff = false;
        for (size_t i = 0; i < c.L.size(); ++i) { same &= c.L[i] == e.L[i]; diff |= c.L[i] != d.L[i]; }
        check(same && diff, "pads: the same hit twice is the same audio; Pad Map GM plays another drum on note 40");
    }
    {   // kits differ; Mix takes each track's machine
        Inst a; a.note(36, 100); a.run(0.4);
        Inst b; b.set(pKit, choice(1, 3)); b.note(36, 100); b.run(0.4);
        Inst c; c.set(pKit, choice(2, 3)); c.note(36, 100); c.run(0.4);                       // Mix, BD Mach = 808
        Inst d; d.set(pKit, choice(2, 3)); d.set(pMach, 1.0f); d.note(36, 100); d.run(0.4);   // Mix, BD Mach = 909
        bool ab = false, ac = true, bd = true;
        for (size_t i = 0; i < a.L.size(); ++i) { ab |= a.L[i] != b.L[i]; ac &= a.L[i] == c.L[i]; bd &= b.L[i] == d.L[i]; }
        check(ab, "Kit: the 808 and the 909 kick are different sounds");
        check(ac && bd, "Kit Mix: each track plays the machine its Mach switch says");
    }
    {   // the step row follows Track and Pattern
        Inst a; a.run(0.05);
        bool bd = a.step(0) == 2 && a.step(1) == 0 && a.step(4) == 2;
        a.set(pTrack, choice(6, 11)); a.run(0.05);                      // CP of "House": ....X.......X...
        bool cp = a.step(0) == 0 && a.step(4) == 2 && a.step(12) == 2;
        a.set(pPattern, choice(3, 8)); a.set(pTrack, choice(1, 11)); a.run(0.05);   // "Boom Bap" SD ....X.......X...
        bool sd = a.step(4) == 2 && a.step(12) == 2 && a.step(0) == 0;
        check(bd && cp && sd, "steps: S1..S16 show the track and the pattern chosen");
    }
    {   // editing a step changes what plays; other tracks and patterns are untouched; saved and loaded
        Inst a; soloKick(a);
        a.run(0.05);
        a.set(pStep + 2, choice(1, 3));                                  // BD: add a hit on step 3
        a.L.clear(); a.R.clear();
        a.set(pSeq, choice(1, 3)); a.playing = true; a.run(2.0);
        auto o = a.onsets(0.08f, 0.12);
        check(o.size() >= 5 && std::fabs(o[1] - 0.25) < 0.004, "edit: a step switched on plays (kick on step 3)");
        a.set(pTrack, choice(6, 11)); a.run(0.05);
        check(a.step(4) == 2 && a.step(2) == 0, "edit: the other tracks keep their steps");
        a.set(pSteps, choice(11, 16)); a.run(0.05);                      // 12 steps
        auto chunk = a.save();
        Inst b; b.load(chunk); b.run(0.05);
        b.set(pTrack, choice(0, 11)); b.run(0.05);
        bool row = b.step(2) == 1 && b.step(0) == 2 && std::lround(b.get(pSteps) * 15.0f) == 11;
        b.set(pPattern, choice(1, 8)); b.run(0.05);
        bool other = std::lround(b.get(pSteps) * 15.0f) == 15 && b.step(6) == 2;   // "Electro" BD X.....X...x.X...
        b.set(pPattern, choice(0, 8)); b.run(0.05);
        check(row && other && b.step(2) == 1 && std::lround(b.get(pSteps) * 15.0f) == 11, "state: patterns, edits and lengths come back from a saved project");
    }
    {   // pattern length: 12 steps at 1/16 repeat every 3 beats
        Inst a; soloKick(a); a.run(0.05);
        a.set(pSteps, choice(11, 16)); a.set(pSeq, choice(1, 3)); a.playing = true; a.run(3.2);
        auto o = a.onsets(0.08f, 0.12);     // kicks at steps 0 4 8 | 12=0 ...: 0, .5, 1.0, 1.5, 2.0, 2.5, 3.0
        check(o.size() == 7, "Steps: a 12-step pattern loops after 12 steps");
    }
    {   // Clear Trk empties the row and springs back; Clear Pat empties everything
        Inst a; a.run(0.05);
        a.set(pClearTrk, 1.0f); a.run(0.05);
        bool row = true; for (int st = 0; st < 16; ++st) row &= a.step(st) == 0;
        a.set(pTrack, choice(6, 11)); a.run(0.05);
        const bool kept = a.step(4) == 2, sprung = a.get(pClearTrk) < 0.5f;
        a.set(pClearPat, 1.0f); a.run(0.05);
        a.set(pSeq, choice(2, 3)); a.run(1.0);
        check(row && kept && sprung, "Clear Trk: empties the track shown only, and springs back");
        check(a.peak() == 0.0f && a.get(pClearPat) < 0.5f && a.step(4) == 0, "Clear Pat: empties the whole pattern");
    }
    {   // swing: step 2 (the second 16th) comes late
        Inst a; a.run(0.05);
        for (int t = 0; t < 11; ++t) if (t != 7) a.set(9 + 3 * t, 0.0f);       // closed hat only: xx.xxx.x...
        a.set(pSwing, 1.0f); a.set(pSeq, choice(1, 3));
        a.L.clear(); a.R.clear(); a.playing = true; a.run(0.5);
        auto o = a.onsets(0.004f, 0.03);
        check(o.size() >= 2 && std::fabs(o[1] - (0.125 + 0.0625)) < 0.004, "Swing 100 %: odd steps half a step late");
    }
    {   // a jump of the song position: the grid follows
        Inst a; soloKick(a); a.set(pSeq, choice(1, 3)); a.playing = true; a.run(0.7);
        a.L.clear(); a.R.clear(); a.beat = 8.0; a.run(0.3);
        auto o = a.onsets(0.08f, 0.12);
        check(!o.empty() && o[0] < 0.004, "loop / jump: the kick of the new position plays at once");
    }
    {   // programs change the sound and leave the sequencer and its patterns alone
        Inst a; a.run(0.05);
        a.set(pStep + 2, choice(1, 3)); a.set(pSeq, choice(1, 3)); a.set(pPattern, choice(0, 8)); a.run(0.05);
        a.fx->dispatcher(a.fx, effMainsChanged, 0, 0, nullptr, 0);
        a.fx->dispatcher(a.fx, effSetProgram, 0, 1, nullptr, 0);
        a.fx->dispatcher(a.fx, effMainsChanged, 0, 1, nullptr, 0);
        a.run(0.05);
        check(std::lround(a.get(pKit) * 2.0f) == 1 && a.step(2) == 1 && std::lround(a.get(pSeq) * 2.0f) == 1,
              "programs: the kit changes, the sequencer and the edited steps stay");
    }
    {   // notes 12..19 choose the pattern; Pad Sel shows the pad's track while Seq is on
        Inst a; a.set(pSeq, choice(2, 3)); a.run(0.05);
        a.note(14, 100); a.run(0.05);
        const bool pat = std::lround(a.get(pPattern) * 7.0f) == 2;
        a.note(38, 100); a.run(0.05);                                    // D = snare
        check(pat, "notes 12..19 choose pattern 1..8");
        check(std::lround(a.get(pTrack) * 10.0f) == 1, "Pad Sel: playing the snare pad shows the snare's steps");
    }
    {   // two instances do not share anything
        Inst a, b; b.set(pKit, choice(1, 3));
        a.note(36, 100); a.run(0.3); b.note(36, 100); b.run(0.3);
        Inst c; c.note(36, 100); c.run(0.3);
        bool same = true; for (size_t i = 0; i < a.L.size(); ++i) same &= a.L[i] == c.L[i];
        check(same, "two instances: a second one with another kit does not change the first one's sound");
    }
    {   // extreme settings stay finite and bounded
        Inst a; srand(7); bool ok = true;
        a.set(pSeq, choice(2, 3));
        for (int round = 0; round < 150 && ok; ++round)
        {
            for (int k = 0; k < 12; ++k) { const int i = rand() % 57; a.set(i, (rand() % 3 == 0) ? (float)(rand() % 2) : (float)rand() / RAND_MAX); }
            a.set(pMaster, 0.87f);
            a.L.clear(); a.R.clear(); a.run(0.25);
            for (size_t i = 0; i < a.L.size(); ++i) ok &= std::isfinite(a.L[i]) && std::fabs(a.L[i]) <= 1.0f && std::isfinite(a.R[i]) && std::fabs(a.R[i]) <= 1.0f;
        }
        check(ok, "150 random extreme sound settings: output finite and within full scale");
    }
    printf("\n%d passed, %d failed\n", g_pass, g_fail);
    return g_fail ? 1 : 0;
}
