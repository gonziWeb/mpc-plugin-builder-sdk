// render_garden.cpp - listen to and check a self-playing instrument (Slow Garden): no notes are sent, it plays itself.
// SPDX-License-Identifier: MIT   Copyright (C) 2026 Gonzi Audio
//
//   render_garden <plugin.so> <out.wav> <seconds> <program> [<program> ...]
//       Each program in a fresh instance, <seconds> long, one after the other in one WAV. Prints peak, RMS and desktop
//       CPU per program. Environment: GARDEN_SEED (the Seed parameter, default 1 so that a render can be repeated;
//       0 = random), GARDEN_BPM (if set: Sync = MPC, Grow = Song, the transport playing at that tempo),
//       MPC_RATE (host sample rate, default 44100).
//   render_garden <plugin.so> check
//       Checks: a seed repeats, no seed does not, Song + seed restarts the same piece, the pulse lands on the MPC's
//       sixteenths at a fractional tempo and across a loop, the scale edited from the pads is saved and restored,
//       two instances do not disturb each other. Exit code 1 if one fails.
//
//   g++ -O2 -std=c++17 -Iframework gonzi/tools/render_garden.cpp -o .sdk-tools/render_garden -ldl
#include <dlfcn.h>
#include <algorithm>
#include <chrono>
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

struct Inst
{
    AEffect* fx = nullptr;
    std::vector<std::string> names;
    Inst()
    {
        fx = g_main(host);
        fx->dispatcher(fx, effOpen, 0, 0, nullptr, 0);
        fx->dispatcher(fx, effSetSampleRate, 0, 0, nullptr, (float)g_sr);
        fx->dispatcher(fx, effSetBlockSize, 0, 128, nullptr, 0);
        for (int i = 0; i < fx->numParams; ++i)
        {
            char n[64] = {0};
            fx->dispatcher(fx, effGetParamName, i, 0, n, 0);
            names.push_back(n);
        }
    }
    ~Inst()
    {
        fx->dispatcher(fx, effMainsChanged, 0, 0, nullptr, 0);
        fx->dispatcher(fx, effClose, 0, 0, nullptr, 0);
    }
    int index(const char* name) const
    {
        for (size_t i = 0; i < names.size(); ++i) if (names[i] == name) return (int)i;
        fprintf(stderr, "no parameter named %s\n", name); exit(2);
    }
    // set by the text the screen shows ("Song", "MPC", "7"): tries every step of the 0..1 range
    void set(const char* name, const char* text)
    {
        const int i = index(name);
        for (int k = 0; k <= 1000; ++k)
        {
            fx->setParameter(fx, i, (float)k / 1000.0f);
            char d[64] = {0};
            fx->dispatcher(fx, effGetParamDisplay, i, 0, d, 0);
            if (!strcmp(d, text)) return;
        }
        fprintf(stderr, "%s has no value shown as %s\n", name, text); exit(2);
    }
    void program(int p) { fx->dispatcher(fx, effSetProgram, 0, p, nullptr, 0); }
    void start() { fx->dispatcher(fx, effMainsChanged, 0, 1, nullptr, 0); }
    void note(int n)
    {
        VstMidiEvent m; memset(&m, 0, sizeof m);
        m.type = kVstMidiType; m.byteSize = sizeof m; m.midiData[0] = (char)0x90; m.midiData[1] = (char)n; m.midiData[2] = 100;
        char store[sizeof(VstEvents) + sizeof(VstEvent*)];
        VstEvents* e = (VstEvents*)store;
        e->numEvents = 1; e->events[0] = (VstEvent*)&m;
        fx->dispatcher(fx, effProcessEvents, 0, 0, e, 0);
    }
    std::vector<uint8_t> chunk()
    {
        void* p = nullptr;
        const VstIntPtr n = fx->dispatcher(fx, effGetChunk, 0, 0, &p, 0);
        return std::vector<uint8_t>((uint8_t*)p, (uint8_t*)p + n);
    }
    void setChunk(const std::vector<uint8_t>& c) { fx->dispatcher(fx, effSetChunk, 0, (VstIntPtr)c.size(), (void*)c.data(), 0); }
    // frames of audio appended to out (interleaved), in blocks of `block`
    double render(std::vector<float>& out, size_t frames, int block = 128)
    {
        std::vector<float> l((size_t)block), r((size_t)block);
        double busy = 0;
        for (size_t pos = 0; pos < frames; pos += (size_t)block)
        {
            const int n = (int)std::min<size_t>((size_t)block, frames - pos);
            float* outs[2] = {l.data(), r.data()};
            std::fill(l.begin(), l.end(), 0.0f); std::fill(r.begin(), r.end(), 0.0f);
            auto t0 = std::chrono::steady_clock::now();
            fx->processReplacing(fx, nullptr, outs, n);
            busy += std::chrono::duration<double>(std::chrono::steady_clock::now() - t0).count();
            for (int i = 0; i < n; ++i) { out.push_back(l[(size_t)i]); out.push_back(r[(size_t)i]); }
            if (g_time.flags & kVstTransportPlaying)
            {
                g_time.samplePos += n;
                g_time.ppqPos += n / g_sr * g_time.tempo / 60.0;
            }
        }
        return busy;
    }
};

static void transport(double bpm, bool playing, double ppq = 0.0)
{
    memset(&g_time, 0, sizeof g_time);
    g_time.sampleRate = g_sr; g_time.tempo = bpm; g_time.timeSigNumerator = 4; g_time.timeSigDenominator = 4;
    g_time.ppqPos = ppq;
    g_time.flags = kVstTempoValid | kVstPpqPosValid | kVstTimeSigValid | (playing ? kVstTransportPlaying : 0);
}

static void stats(const std::vector<float>& v, size_t from, float& peak, float& rms)
{
    double s = 0; peak = 0;
    for (size_t i = from; i < v.size(); ++i) { peak = std::max(peak, fabsf(v[i])); s += (double)v[i] * v[i]; }
    rms = (float)sqrt(s / std::max<size_t>(1, v.size() - from));
}
static float db(float x) { return 20.0f * log10f(x + 1e-9f); }

static float maxDiff(const std::vector<float>& a, const std::vector<float>& b, size_t from = 0)
{
    float d = a.size() == b.size() ? 0.0f : 99.0f;
    for (size_t i = from; i < std::min(a.size(), b.size()); ++i) d = std::max(d, fabsf(a[i] - b[i]));
    return d;
}

static int g_fail;
static void report(bool ok, const char* what, const std::string& detail)
{
    printf("%s  %s%s%s\n", ok ? "PASS" : "FAIL", what, detail.empty() ? "" : ": ", detail.c_str());
    if (!ok) g_fail++;
}
static std::string fmt(const char* f, double a, double b = 0, double c = 0) { char s[160]; snprintf(s, sizeof s, f, a, b, c); return s; }

static std::vector<float> fresh(const char* seed, int program, double secs)
{
    Inst a; a.program(program); a.set("Seed", seed); a.start();
    std::vector<float> o; a.render(o, (size_t)(secs * g_sr));
    return o;
}

static int check()
{
    transport(120, false);
    {   // 1, 2: the seed
        auto a = fresh("7", 0, 30), b = fresh("7", 0, 30), c = fresh("Random", 0, 30), d = fresh("Random", 0, 30);
        report(maxDiff(a, b) == 0.0f, "Seed 7 twice: the same piece", fmt("largest difference %g over 30 s", maxDiff(a, b)));
        report(maxDiff(c, d) > 1e-3f, "Seed Random twice: two pieces", fmt("largest difference %g", maxDiff(c, d)));
    }
    {   // 3: Song + seed: every start of the MPC plays the piece from the top
        Inst a; a.program(4); a.set("Seed", "7"); a.set("Grow", "Song"); a.start();
        std::vector<float> idle, one, rest, two;
        a.render(idle, (size_t)(3 * g_sr));
        transport(120, true);  a.render(one, (size_t)(20 * g_sr));
        transport(120, false); a.render(rest, (size_t)(7 * g_sr));
        transport(120, true);  a.render(two, (size_t)(20 * g_sr));
        float p, r; stats(idle, 0, p, r);
        const float d = maxDiff(one, two, (size_t)(0.3 * g_sr) * 2);
        report(p < 1e-4f, "Grow = Song: silent until the MPC plays", fmt("peak %.1f dBFS", db(p)));
        report(d < 1e-4f, "Grow = Song + Seed: the second start repeats the first", fmt("largest difference %g after 0.3 s", d));
        transport(120, false);
    }
    {   // 4: Sync = MPC at 97.3 bpm, the song looping 8 beats: every hit of the pulse on one of the MPC's sixteenths
        Inst a; a.program(0);
        for (const char* n : {"Pad", "Bells", "Bass", "Echo", "Reverb", "Evolve"}) a.set(n, "0");
        a.set("Swing", "50%");
        a.set("Pulse", "100"); a.set("Hits", "20"); a.set("Hats", "100"); a.set("Motion", "0"); a.set("Sync", "MPC"); a.set("Grow", "Song"); a.set("Seed", "3");
        a.start();
        const double bpm = 97.3;
        transport(bpm, true, 0.0);
        std::vector<float> o; std::vector<double> ppq;
        const int block = 96;
        float l[96], r[96];
        for (int b = 0; b < (int)(40 * g_sr / block); ++b)
        {
            if (g_time.ppqPos >= 8.0) g_time.ppqPos -= 8.0;   // the loop
            float* outs[2] = {l, r};
            a.fx->processReplacing(a.fx, nullptr, outs, block);
            for (int i = 0; i < block; ++i) { o.push_back(fabsf(l[i]) + fabsf(r[i])); ppq.push_back(g_time.ppqPos + i / g_sr * bpm / 60.0); }
            g_time.ppqPos += block / g_sr * bpm / 60.0;
        }
        // an onset: the level rises through 0.02 after 20 ms below 0.01 (one hit of each ring a cycle: mostly apart)
        int hits = 0; double worst = 0; size_t quiet = 0;
        for (size_t i = 0; i < o.size(); ++i)
        {
            if (o[i] < 0.01f) { quiet++; continue; }
            if (o[i] > 0.02f)
            {
                if (quiet > 882)
                {
                    const double s = ppq[i] * 4.0, off = fabs(s - floor(s + 0.5)) / 4.0 * 60.0 / bpm * 1000.0;   // ms from a sixteenth
                    worst = std::max(worst, off); hits++;
                }
                quiet = 0;
            }
        }
        report(hits > 20 && worst < 3.0, "Sync = MPC, 97.3 bpm, looping: the pulse on the MPC's sixteenths",
               fmt("%.0f isolated hits, the furthest %.2f ms from a sixteenth", hits, worst));
        transport(120, false);
    }
    {   // 5: the scale: pads take notes out, the project keeps it
        Inst a; a.program(0); a.set("Seed", "5"); a.start();
        std::vector<float> o; a.render(o, 4410);
        const auto before = a.chunk();
        a.note(64); a.render(o, 4410); a.note(69); a.render(o, 4410);     // E and A out of D Dorian
        const auto edited = a.chunk();
        Inst b; b.setChunk(edited); b.start(); b.render(o, 4410);
        const auto back = b.chunk();
        report(before != edited && edited == back, "pads edit the scale; a saved project gets it back", fmt("chunk %g bytes", (double)edited.size()));
        Inst c; c.program(0); c.set("Seed", "5"); c.set("Key Edit", "Off"); c.start(); c.render(o, 4410);
        const auto c0 = c.chunk(); c.note(64); c.render(o, 4410);
        report(c0 == c.chunk(), "Key Edit off: notes leave the scale alone", "");
    }
    {   // 6: two instances side by side
        auto alone = fresh("9", 2, 20);                               // in blocks of 128
        Inst a, b; a.program(2); a.set("Seed", "9"); b.program(4); b.set("Seed", "4"); a.start(); b.start();
        std::vector<float> oa, ob;
        for (int k = 0; k < 20 * 10; ++k) { a.render(oa, (size_t)(g_sr / 10), 96); b.render(ob, (size_t)(g_sr / 10), 512); }   // other blocks
        report(maxDiff(alone, oa) == 0.0f, "an instance sounds the same with another one running, and at another block size", fmt("largest difference %g", maxDiff(alone, oa)));
    }
    printf(g_fail ? "%d check(s) FAILED\n" : "all checks passed\n", g_fail);
    return g_fail ? 1 : 0;
}

int main(int argc, char** argv)
{
    if (argc < 3) { fprintf(stderr, "usage: render_garden plugin.so out.wav seconds prog...   |   render_garden plugin.so check\n"); return 1; }
    if (getenv("MPC_RATE")) g_sr = atof(getenv("MPC_RATE"));
    void* lib = dlopen(argv[1], RTLD_NOW);
    if (!lib) { fprintf(stderr, "%s\n", dlerror()); return 1; }
    g_main = (Main)dlsym(lib, "VSTPluginMain");
    if (!strcmp(argv[2], "check")) return check();
    if (argc < 5) { fprintf(stderr, "usage: render_garden plugin.so out.wav seconds prog...\n"); return 1; }

    const double secs = atof(argv[3]);
    const char* seed = getenv("GARDEN_SEED") ? getenv("GARDEN_SEED") : "1";
    const double bpm = getenv("GARDEN_BPM") ? atof(getenv("GARDEN_BPM")) : 0.0;
    std::vector<float> out;
    for (int a = 4; a < argc; ++a)
    {
        transport(bpm > 0 ? bpm : 120.0, bpm > 0);
        Inst in;
        in.program(atoi(argv[a]));
        in.set("Seed", !strcmp(seed, "0") ? "Random" : seed);
        if (bpm > 0) { in.set("Sync", "MPC"); in.set("Grow", "Song"); }
        in.start();
        char pname[64] = {0}; in.fx->dispatcher(in.fx, effGetProgramName, 0, 0, pname, 0);
        std::vector<float> o;
        const double busy = in.render(o, (size_t)(secs * g_sr));
        float peak, rms, peakLate, rmsLate;
        stats(o, 0, peak, rms);
        stats(o, o.size() / 3 & ~(size_t)1, peakLate, rmsLate);
        printf("program %2d %-12s peak %6.1f dBFS  rms %6.1f dBFS (last two thirds %6.1f)  cpu %.2f%%\n", atoi(argv[a]), pname,
               db(peak), db(rms), db(rmsLate), 100.0 * busy / secs);
        const size_t fade = std::min<size_t>(o.size() / 2, (size_t)(1.5 * g_sr));   // the piece would go on: fade it out
        for (size_t i = 0; i < fade; ++i)
        {
            const float g = (float)i / (float)fade;
            o[o.size() - 2 * (i + 1)] *= g; o[o.size() - 2 * (i + 1) + 1] *= g;
        }
        out.insert(out.end(), o.begin(), o.end());
        for (int i = 0; i < (int)(0.5 * g_sr) * 2; ++i) out.push_back(0);
    }

    std::vector<int16_t> pcm(out.size());
    for (size_t i = 0; i < out.size(); ++i) pcm[i] = (int16_t)lrintf(fmaxf(-1, fminf(1, out[i])) * 32767);
    FILE* f = fopen(argv[2], "wb");
    if (!f) { fprintf(stderr, "cannot write %s\n", argv[2]); return 1; }
    uint32_t dataBytes = (uint32_t)(pcm.size() * 2), riff = 36 + dataBytes, fmtLen = 16, sr = (uint32_t)g_sr, byteRate = sr * 4;
    uint16_t fm = 1, ch = 2, align = 4, bitsPer = 16;
    fwrite("RIFF", 1, 4, f); fwrite(&riff, 4, 1, f); fwrite("WAVEfmt ", 1, 8, f); fwrite(&fmtLen, 4, 1, f);
    fwrite(&fm, 2, 1, f); fwrite(&ch, 2, 1, f); fwrite(&sr, 4, 1, f); fwrite(&byteRate, 4, 1, f);
    fwrite(&align, 2, 1, f); fwrite(&bitsPer, 2, 1, f); fwrite("data", 1, 4, f); fwrite(&dataBytes, 4, 1, f);
    fwrite(pcm.data(), 2, pcm.size(), f); fclose(f);
    printf("wrote %s (%.1f s)\n", argv[2], pcm.size() / 2 / g_sr);
    return 0;
}
