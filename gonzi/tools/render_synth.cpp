// render_synth.cpp - play a slow chord progression and a melody into a VST2 instrument, one section per program,
// and time the plugin (desktop CPU, for orientation).
// Usage: render_synth <plugin.so> <out.wav> <bpm> <program> [<program> ...]
//   Each section: 4 bars (Dm - Bb - Gm - A), the chords held a bar each in the low octave, a melody on top, then a
//   tail of 3 s for the reverb. Velocities vary. Sample rate 44100 (set MPC_RATE=48000 to test resampling).
//   SYNTH_BARS=2 plays only the first bars, SYNTH_TAIL=1.2 sets the tail (s): short sections for plugins with many
//   programs (Armada).
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

struct Ev { double beat; int note; int vel; };   // vel 0: note off

static void song(std::vector<Ev>& e)
{
    const int chords[4][3] = {{50, 53, 57}, {46, 50, 53}, {43, 46, 50}, {45, 49, 52}};   // Dm Bb Gm A
    for (int b = 0; b < 4; ++b)
        for (int k = 0; k < 3; ++k)
        {
            e.push_back({b * 4.0 + k * 0.02, chords[b][k], 70 + 15 * k});
            e.push_back({b * 4.0 + 3.9, chords[b][k], 0});
        }
    // melody (beats, note, length)
    const double mel[][3] = {{0, 74, 1.5}, {1.5, 72, 0.5}, {2, 69, 2}, {4, 70, 1}, {5, 69, 1}, {6, 65, 2},
                             {8, 67, 1.5}, {9.5, 69, 0.5}, {10, 70, 1}, {11, 74, 1}, {12, 73, 3}, {15, 69, 1}};
    for (auto& m : mel)
    {
        e.push_back({m[0], (int)m[1], 100});
        e.push_back({m[0] + m[2] - 0.05, (int)m[1], 0});
    }
}

int main(int argc, char** argv)
{
    if (argc < 5) { fprintf(stderr, "usage: render_synth plugin.so out.wav bpm prog...\n"); return 1; }
    if (getenv("MPC_RATE")) g_sr = atof(getenv("MPC_RATE"));
    void* lib = dlopen(argv[1], RTLD_NOW);
    if (!lib) { fprintf(stderr, "%s\n", dlerror()); return 1; }
    typedef AEffect* (*Main)(audioMasterCallback);
    AEffect* fx = ((Main)dlsym(lib, "VSTPluginMain"))(host);
    const double bpm = atof(argv[3]);
    std::vector<Ev> evs;
    song(evs);
    const double bars = getenv("SYNTH_BARS") ? std::max(1.0, std::min(4.0, atof(getenv("SYNTH_BARS")))) : 4.0;
    const double tail = getenv("SYNTH_TAIL") ? atof(getenv("SYNTH_TAIL")) : 3.0;
    {
        std::vector<Ev> keep;                       // the notes of the bars played; the ones still held end with them
        for (auto& x : evs)
            if (x.beat < bars * 4.0 - 0.01) keep.push_back(x);
            else if (!x.vel) keep.push_back({bars * 4.0 - 0.1, x.note, 0});
        evs = keep;
    }
    const size_t total = (size_t)((bars * 4.0 * 60.0 / bpm + tail) * g_sr);

    fx->dispatcher(fx, effOpen, 0, 0, nullptr, 0);
    fx->dispatcher(fx, effSetSampleRate, 0, 0, nullptr, (float)g_sr);
    fx->dispatcher(fx, effSetBlockSize, 0, 128, nullptr, 0);
    memset(&g_time, 0, sizeof g_time);
    g_time.sampleRate = g_sr; g_time.tempo = bpm; g_time.timeSigNumerator = 4; g_time.timeSigDenominator = 4;
    g_time.flags = kVstTempoValid | kVstPpqPosValid | kVstTransportPlaying | kVstTimeSigValid;

    std::vector<float> out;
    float peakAll = 0;
    for (int a = 4; a < argc; ++a)
    {
        const int prog = atoi(argv[a]);
        fx->dispatcher(fx, effMainsChanged, 0, 0, nullptr, 0);
        fx->dispatcher(fx, effSetProgram, 0, prog, nullptr, 0);
        fx->dispatcher(fx, effMainsChanged, 0, 1, nullptr, 0);
        char pname[64] = {0}; fx->dispatcher(fx, effGetProgramName, 0, 0, pname, 0);
        std::vector<std::pair<size_t, Ev>> ev;
        for (auto& x : evs) ev.push_back({(size_t)(x.beat * 60.0 / bpm * g_sr), x});
        std::stable_sort(ev.begin(), ev.end(), [](auto& p, auto& q) { return p.first < q.first; });
        size_t ei = 0;
        float oL[128], oR[128], peak = 0;
        double busy = 0, worst = 0;
        size_t blocks = 0;
        for (size_t pos = 0; pos < total; pos += 128)
        {
            const int frames = (int)std::min<size_t>(128, total - pos);
            std::vector<VstMidiEvent> mev;
            while (ei < ev.size() && ev[ei].first < pos + frames)
            {
                VstMidiEvent m; memset(&m, 0, sizeof m);
                m.type = kVstMidiType; m.byteSize = sizeof m;
                m.deltaFrames = (VstInt32)(ev[ei].first - pos);
                m.midiData[0] = (char)(ev[ei].second.vel ? 0x90 : 0x80);
                m.midiData[1] = (char)ev[ei].second.note; m.midiData[2] = (char)ev[ei].second.vel;
                mev.push_back(m); ++ei;
            }
            if (!mev.empty())
            {
                std::vector<char> store(sizeof(VstEvents) + sizeof(VstEvent*) * mev.size());
                VstEvents* e = (VstEvents*)store.data();
                e->numEvents = (VstInt32)mev.size();
                for (size_t k = 0; k < mev.size(); ++k) e->events[k] = (VstEvent*)&mev[k];
                fx->dispatcher(fx, effProcessEvents, 0, 0, e, 0);
            }
            g_time.samplePos = (double)pos; g_time.ppqPos = pos / g_sr * bpm / 60.0;
            float* outs[2] = {oL, oR};
            memset(oL, 0, sizeof oL); memset(oR, 0, sizeof oR);
            auto t0 = std::chrono::steady_clock::now();
            fx->processReplacing(fx, nullptr, outs, frames);
            double dt = std::chrono::duration<double>(std::chrono::steady_clock::now() - t0).count();
            busy += dt; worst = std::max(worst, dt / (frames / g_sr)); ++blocks;
            for (int i = 0; i < frames; ++i)
            {
                out.push_back(oL[i]); out.push_back(oR[i]);
                peak = std::max(peak, std::max(fabsf(oL[i]), fabsf(oR[i])));
            }
        }
        peakAll = std::max(peakAll, peak);
        printf("program %2d %-18s peak %6.1f dBFS  cpu avg %.2f%%  worst block %.1f%%\n", prog, pname,
               20 * log10(peak + 1e-9), 100.0 * busy / (total / g_sr), 100.0 * worst);
        for (int i = 0; i < (int)(0.4 * g_sr) * 2; ++i) out.push_back(0);
    }
    fx->dispatcher(fx, effMainsChanged, 0, 0, nullptr, 0);
    fx->dispatcher(fx, effClose, 0, 0, nullptr, 0);

    std::vector<int16_t> pcm(out.size());
    for (size_t i = 0; i < out.size(); ++i) pcm[i] = (int16_t)lrintf(fmaxf(-1, fminf(1, out[i])) * 32767);
    FILE* f = fopen(argv[2], "wb");
    uint32_t dataBytes = (uint32_t)(pcm.size() * 2), riff = 36 + dataBytes, fmtLen = 16, sr = (uint32_t)g_sr, byteRate = sr * 4;
    uint16_t fmt = 1, ch = 2, align = 4, bitsPer = 16;
    fwrite("RIFF", 1, 4, f); fwrite(&riff, 4, 1, f); fwrite("WAVEfmt ", 1, 8, f); fwrite(&fmtLen, 4, 1, f);
    fwrite(&fmt, 2, 1, f); fwrite(&ch, 2, 1, f); fwrite(&sr, 4, 1, f); fwrite(&byteRate, 4, 1, f);
    fwrite(&align, 2, 1, f); fwrite(&bitsPer, 2, 1, f); fwrite("data", 1, 4, f); fwrite(&dataBytes, 4, 1, f);
    fwrite(pcm.data(), 2, pcm.size(), f); fclose(f);
    printf("wrote %s (%.1f s), peak %.2f dBFS\n", argv[2], pcm.size() / 2 / g_sr, 20 * log10(peakAll + 1e-9));
    return 0;
}
