// render_acid.cpp - play an acid bass line into a VST2 mono-synth (Acid Rat), one section per program, and time the
// plugin (desktop CPU, for orientation).
// Usage: render_acid <plugin.so> <out.wav> <bpm> <program> [<program> ...]
//   Each section is 4 bars with the transport running, then a 1.5 s tail:
//   * a program whose "Seq" parameter (index 14) is Off gets a written 16-step line as MIDI: accents are velocity
//     112 (others 80), slides are notes that overlap the next one;
//   * a program whose Seq is on gets one held note instead (60 for two bars, then 63): the plugin plays its own line.
//   In bars 3-4 the cutoff (parameter 1) is swept up and back, as a hand on the knob would.
//   Sample rate 44100 (set MPC_RATE=48000 to test another host rate).
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

// note (0 = rest), accent, slide into the next step
static const int LINE[16][3] = {
    {33, 1, 0}, {33, 0, 0}, {45, 0, 1}, {33, 0, 0}, {0, 0, 0}, {36, 1, 0}, {33, 0, 1}, {43, 0, 0},
    {33, 0, 0}, {0, 0, 0}, {45, 1, 1}, {40, 0, 0}, {33, 0, 0}, {31, 0, 1}, {33, 1, 0}, {38, 0, 0}};

static void writtenLine(std::vector<Ev>& e)
{
    for (int bar = 0; bar < 4; ++bar)
        for (int s = 0; s < 16; ++s)
        {
            if (!LINE[s][0]) continue;
            const double t = bar * 4.0 + s * 0.25;
            e.push_back({t, LINE[s][0], LINE[s][1] ? 112 : 80});
            e.push_back({t + (LINE[s][2] ? 0.30 : 0.125), LINE[s][0], 0});
        }
}

static void heldKeys(std::vector<Ev>& e)
{
    e.push_back({0.0, 60, 100}); e.push_back({7.95, 60, 0});
    e.push_back({8.0, 63, 100}); e.push_back({15.95, 63, 0});
}

int main(int argc, char** argv)
{
    if (argc < 5) { fprintf(stderr, "usage: render_acid plugin.so out.wav bpm prog...\n"); return 1; }
    if (getenv("MPC_RATE")) g_sr = atof(getenv("MPC_RATE"));
    void* lib = dlopen(argv[1], RTLD_NOW);
    if (!lib) { fprintf(stderr, "%s\n", dlerror()); return 1; }
    typedef AEffect* (*Main)(audioMasterCallback);
    AEffect* fx = ((Main)dlsym(lib, "VSTPluginMain"))(host);
    const double bpm = atof(argv[3]);
    const size_t songLen = (size_t)(16.0 * 60.0 / bpm * g_sr), total = songLen + (size_t)(1.5 * g_sr);

    fx->dispatcher(fx, effOpen, 0, 0, nullptr, 0);
    fx->dispatcher(fx, effSetSampleRate, 0, 0, nullptr, (float)g_sr);
    fx->dispatcher(fx, effSetBlockSize, 0, 128, nullptr, 0);
    memset(&g_time, 0, sizeof g_time);
    g_time.sampleRate = g_sr; g_time.tempo = bpm; g_time.timeSigNumerator = 4; g_time.timeSigDenominator = 4;

    std::vector<float> out;
    float peakAll = 0;
    for (int a = 4; a < argc; ++a)
    {
        const int prog = atoi(argv[a]);
        fx->dispatcher(fx, effMainsChanged, 0, 0, nullptr, 0);
        fx->dispatcher(fx, effSetProgram, 0, prog, nullptr, 0);
        fx->dispatcher(fx, effMainsChanged, 0, 1, nullptr, 0);
        char pname[64] = {0}; fx->dispatcher(fx, effGetProgramName, 0, 0, pname, 0);
        const bool seqOn = fx->getParameter(fx, 14) > 0.25f;
        const float cut0 = fx->getParameter(fx, 1);
        std::vector<Ev> evs;
        if (seqOn) heldKeys(evs); else writtenLine(evs);
        std::vector<std::pair<size_t, Ev>> ev;
        for (auto& x : evs) ev.push_back({(size_t)(x.beat * 60.0 / bpm * g_sr), x});
        std::stable_sort(ev.begin(), ev.end(), [](auto& p, auto& q) { return p.first < q.first; });
        size_t ei = 0;
        float oL[128], oR[128], peak = 0;
        double busy = 0, worst = 0, sumSq = 0;
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
            const double beat = pos / g_sr * bpm / 60.0;
            const bool playing = pos < songLen;
            g_time.samplePos = (double)pos; g_time.ppqPos = beat;
            g_time.flags = kVstTempoValid | kVstPpqPosValid | kVstTimeSigValid | (playing ? kVstTransportPlaying : 0);
            if (beat >= 8.0 && beat < 16.0)      // the knob: up over bars 3-4 and back
            {
                const double s = sin(M_PI * (beat - 8.0) / 8.0);
                fx->setParameter(fx, 1, (float)std::min(1.0, cut0 + (0.95 - cut0) * s * s));
            }
            else fx->setParameter(fx, 1, cut0);
            float* outs[2] = {oL, oR};
            memset(oL, 0, sizeof oL); memset(oR, 0, sizeof oR);
            auto t0 = std::chrono::steady_clock::now();
            fx->processReplacing(fx, nullptr, outs, frames);
            double dt = std::chrono::duration<double>(std::chrono::steady_clock::now() - t0).count();
            busy += dt; worst = std::max(worst, dt / (frames / g_sr));
            for (int i = 0; i < frames; ++i)
            {
                out.push_back(oL[i]); out.push_back(oR[i]);
                peak = std::max(peak, std::max(fabsf(oL[i]), fabsf(oR[i])));
                if (pos < songLen) sumSq += 0.5 * ((double)oL[i] * oL[i] + (double)oR[i] * oR[i]);
            }
        }
        peakAll = std::max(peakAll, peak);
        printf("program %2d %-16s %s peak %6.1f dBFS  rms %6.1f dBFS  cpu avg %.2f%%  worst block %.1f%%\n", prog, pname,
               seqOn ? "(own line)" : "(MIDI line)", 20 * log10(peak + 1e-9), 10 * log10(sumSq / songLen + 1e-12),
               100.0 * busy / (total / g_sr), 100.0 * worst);
        for (int i = 0; i < (int)(0.3 * g_sr) * 2; ++i) out.push_back(0);
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
