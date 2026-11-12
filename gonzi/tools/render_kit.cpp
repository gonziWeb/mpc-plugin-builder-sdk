// render_kit.cpp - play a MIDI drum groove into a VST2 instrument, one section per program.
// Usage: render_kit <plugin.so> <out.wav> <bpm> <groove> <program> [<program> ...]
//   groove: "doom" (4-bar half-time groove + tom fill) or "hits" (each sound alone, soft then hard)
// Notes are General MIDI drum notes (what the MPC "Classic" pad layout sends).
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

struct Hit { double beat; int note; int vel; };

static void doomGroove(std::vector<Hit>& h)
{
    // 4/4, half-time feel. Beats counted from 0.
    for (int bar = 0; bar < 4; ++bar)
    {
        const double b0 = bar * 4.0;
        if (bar < 2)
        {   // bars 1-2: crash + ride
            if (bar == 0) h.push_back({b0, 49, 120});
            for (int q = (bar == 0 ? 1 : 0); q < 4; ++q) h.push_back({b0 + q, 51, q == 0 ? 105 : 88});
            h.push_back({b0 + 0.0, 36, 122}); h.push_back({b0 + 1.5, 36, 100});
            h.push_back({b0 + 2.0, 38, 124});
            h.push_back({b0 + 3.75, 36, 96});
        }
        else if (bar == 2)
        {   // bar 3: hats, open on the "and" of 4
            for (int e = 0; e < 8; ++e)
                h.push_back({b0 + e * 0.5, e == 7 ? 46 : 42, (e % 2) ? 78 : 104});
            h.push_back({b0 + 0.0, 36, 122}); h.push_back({b0 + 0.5, 36, 104});
            h.push_back({b0 + 2.0, 38, 124}); h.push_back({b0 + 3.0, 44, 70});
            h.push_back({b0 + 3.5, 36, 100});
        }
        else
        {   // bar 4: slow tom fill down the kit, rimshot, crash on the next "one"
            h.push_back({b0 + 0.0, 36, 120}); h.push_back({b0 + 0.0, 57, 110});
            h.push_back({b0 + 1.0, 40, 127});
            const int toms[8] = {48, 48, 45, 45, 43, 43, 41, 41};
            for (int k = 0; k < 8; ++k) h.push_back({b0 + 2.0 + k * 0.25, toms[k], 112 - (k % 2) * 14});
            h.push_back({b0 + 4.0, 36, 124}); h.push_back({b0 + 4.0, 49, 124});
        }
    }
}

static void hitsList(std::vector<Hit>& h)
{
    const int notes[] = {36, 38, 40, 37, 48, 45, 41, 42, 44, 46, 49, 51, 53, 59};
    double t = 0;
    for (int n : notes) { h.push_back({t, n, 60}); h.push_back({t + 1.0, n, 124}); t += 2.5; }
}

int main(int argc, char** argv)
{
    if (argc < 6) { fprintf(stderr, "usage: render_kit plugin.so out.wav bpm doom|hits prog...\n"); return 1; }
    void* lib = dlopen(argv[1], RTLD_NOW);
    if (!lib) { fprintf(stderr, "%s\n", dlerror()); return 1; }
    typedef AEffect* (*Main)(audioMasterCallback);
    AEffect* fx = ((Main)dlsym(lib, "VSTPluginMain"))(host);
    const double bpm = atof(argv[3]);
    const std::string groove = argv[4];

    std::vector<Hit> hits;
    if (groove == "hits") hitsList(hits); else doomGroove(hits);
    double lastBeat = 0; for (auto& x : hits) lastBeat = std::max(lastBeat, x.beat);
    const size_t total = (size_t)((lastBeat * 60.0 / bpm + 3.5) * g_sr);

    fx->dispatcher(fx, effOpen, 0, 0, nullptr, 0);
    fx->dispatcher(fx, effSetSampleRate, 0, 0, nullptr, (float)g_sr);
    fx->dispatcher(fx, effSetBlockSize, 0, 128, nullptr, 0);
    memset(&g_time, 0, sizeof g_time);
    g_time.sampleRate = g_sr; g_time.tempo = bpm; g_time.timeSigNumerator = 4; g_time.timeSigDenominator = 4;
    g_time.flags = kVstTempoValid | kVstPpqPosValid | kVstTransportPlaying | kVstTimeSigValid;

    std::vector<float> out;
    float peak = 0;
    for (int a = 5; a < argc; ++a)
    {
        const int prog = atoi(argv[a]);
        fx->dispatcher(fx, effMainsChanged, 0, 0, nullptr, 0);
        fx->dispatcher(fx, effSetProgram, 0, prog, nullptr, 0);
        fx->dispatcher(fx, effMainsChanged, 0, 1, nullptr, 0);
        char pname[64] = {0}; fx->dispatcher(fx, effGetProgramName, 0, 0, pname, 0);
        printf("program %d: %s\n", prog, pname);

        // samples of every hit
        std::vector<std::pair<size_t, Hit>> ev;
        for (auto& x : hits) ev.push_back({(size_t)(x.beat * 60.0 / bpm * g_sr), x});
        std::sort(ev.begin(), ev.end(), [](auto& p, auto& q) { return p.first < q.first; });
        size_t ei = 0;
        float oL[128], oR[128];
        for (size_t pos = 0; pos < total; pos += 128)
        {
            const int frames = (int)std::min<size_t>(128, total - pos);
            // MIDI for this block
            std::vector<VstMidiEvent> mev;
            while (ei < ev.size() && ev[ei].first < pos + frames)
            {
                VstMidiEvent m; memset(&m, 0, sizeof m);
                m.type = kVstMidiType; m.byteSize = sizeof m;
                m.deltaFrames = (VstInt32)(ev[ei].first - pos);
                m.midiData[0] = (char)0x99; m.midiData[1] = (char)ev[ei].second.note; m.midiData[2] = (char)ev[ei].second.vel;
                mev.push_back(m); ++ei;
            }
            if (!mev.empty())
            {
                std::vector<char> store(sizeof(VstEvents) + sizeof(VstEvent*) * mev.size());
                VstEvents* evs = (VstEvents*)store.data();
                evs->numEvents = (VstInt32)mev.size();
                for (size_t k = 0; k < mev.size(); ++k) evs->events[k] = (VstEvent*)&mev[k];
                fx->dispatcher(fx, effProcessEvents, 0, 0, evs, 0);
            }
            g_time.samplePos = (double)pos; g_time.ppqPos = pos / g_sr * bpm / 60.0;
            float* outs[2] = {oL, oR};
            memset(oL, 0, sizeof oL); memset(oR, 0, sizeof oR);
            fx->processReplacing(fx, nullptr, outs, frames);
            for (int i = 0; i < frames; ++i) { out.push_back(oL[i]); out.push_back(oR[i]); peak = std::max(peak, std::max(fabsf(oL[i]), fabsf(oR[i]))); }
        }
        for (int i = 0; i < (int)(0.5 * g_sr) * 2; ++i) out.push_back(0);
    }
    fx->dispatcher(fx, effMainsChanged, 0, 0, nullptr, 0);
    fx->dispatcher(fx, effClose, 0, 0, nullptr, 0);

    std::vector<int16_t> pcm(out.size());
    for (size_t i = 0; i < out.size(); ++i) pcm[i] = (int16_t)lrintf(fmaxf(-1, fminf(1, out[i])) * 32767);
    FILE* f = fopen(argv[2], "wb");
    uint32_t dataBytes = (uint32_t)(pcm.size() * 2), riff = 36 + dataBytes, fmtLen = 16, sr = 44100, byteRate = sr * 4;
    uint16_t fmt = 1, ch = 2, align = 4, bitsPer = 16;
    fwrite("RIFF", 1, 4, f); fwrite(&riff, 4, 1, f); fwrite("WAVEfmt ", 1, 8, f); fwrite(&fmtLen, 4, 1, f);
    fwrite(&fmt, 2, 1, f); fwrite(&ch, 2, 1, f); fwrite(&sr, 4, 1, f); fwrite(&byteRate, 4, 1, f);
    fwrite(&align, 2, 1, f); fwrite(&bitsPer, 2, 1, f); fwrite("data", 1, 4, f); fwrite(&dataBytes, 4, 1, f);
    fwrite(pcm.data(), 2, pcm.size(), f); fclose(f);
    printf("wrote %s (%.1f s), peak %.2f dBFS\n", argv[2], pcm.size() / 2 / g_sr, 20 * log10(peak + 1e-9));
    return 0;
}
