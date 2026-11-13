// render_seq.cpp - render an instrument that has its own sequencer (Drum Rat), one section per spec, and time it
// (desktop CPU, for orientation).
// Usage: render_seq <plugin.so> <out.wav> <bpm> <bars> <spec> [<spec> ...]
//   spec: <program>[/<paramIndex>=<normalised 0..1>]...   e.g. 1/58=0.5/59=0.142857
//   Each section: the program is chosen, the listed parameters are set, the transport runs for <bars> bars, then a
//   1.5 s tail with the transport stopped. With "hits" as <bars>, no transport: the fourteen white keys from note
//   48 (two octaves of an MPC pad map) are played one by one, soft (velocity 88) then accented (127).
//   Sample rate 44100 (set MPC_RATE=48000 to test another host rate). MPC_BLOCK sets the block size (default 128).
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

struct Ev { double beat; int note; int vel; };

int main(int argc, char** argv)
{
    if (argc < 6) { fprintf(stderr, "usage: render_seq plugin.so out.wav bpm bars|hits spec...\n"); return 1; }
    if (getenv("MPC_RATE")) g_sr = atof(getenv("MPC_RATE"));
    const int block = getenv("MPC_BLOCK") ? atoi(getenv("MPC_BLOCK")) : 128;
    void* lib = dlopen(argv[1], RTLD_NOW);
    if (!lib) { fprintf(stderr, "%s\n", dlerror()); return 1; }
    typedef AEffect* (*Main)(audioMasterCallback);
    AEffect* fx = ((Main)dlsym(lib, "VSTPluginMain"))(host);
    const double bpm = atof(argv[3]);
    const bool hits = !strcmp(argv[4], "hits");
    const double bars = hits ? 7.0 : atof(argv[4]);
    const size_t songLen = (size_t)(bars * 4.0 * 60.0 / bpm * g_sr), total = songLen + (size_t)(1.5 * g_sr);

    fx->dispatcher(fx, effOpen, 0, 0, nullptr, 0);
    fx->dispatcher(fx, effSetSampleRate, 0, 0, nullptr, (float)g_sr);
    fx->dispatcher(fx, effSetBlockSize, 0, block, nullptr, 0);
    memset(&g_time, 0, sizeof g_time);
    g_time.sampleRate = g_sr; g_time.tempo = bpm; g_time.timeSigNumerator = 4; g_time.timeSigDenominator = 4;

    std::vector<float> out;
    float peakAll = 0;
    for (int a = 5; a < argc; ++a)
    {
        std::string spec(argv[a]);
        const int prog = atoi(spec.c_str());
        fx->dispatcher(fx, effMainsChanged, 0, 0, nullptr, 0);
        fx->dispatcher(fx, effSetProgram, 0, prog, nullptr, 0);
        for (size_t p = spec.find('/'); p != std::string::npos; p = spec.find('/', p + 1))
        {
            int idx = 0; float v = 0;
            if (sscanf(spec.c_str() + p + 1, "%d=%f", &idx, &v) == 2) fx->setParameter(fx, idx, v);
        }
        fx->dispatcher(fx, effMainsChanged, 0, 1, nullptr, 0);
        char pname[64] = {0}; fx->dispatcher(fx, effGetProgramName, 0, 0, pname, 0);
        std::vector<std::pair<size_t, Ev>> ev;
        if (hits)
            for (int k = 0; k < 28; ++k)
            {
                static const int white[7] = {0, 2, 4, 5, 7, 9, 11};
                const double beat = k * 1.0;
                const int note = 48 + 12 * ((k / 2) / 7) + white[(k / 2) % 7];
                ev.push_back({(size_t)(beat * 60.0 / bpm * g_sr), Ev{beat, note, (k & 1) ? 127 : 88}});
                ev.push_back({(size_t)((beat + 0.5) * 60.0 / bpm * g_sr), Ev{beat, note, 0}});
            }
        size_t ei = 0;
        std::vector<float> oL(block), oR(block);
        float peak = 0;
        double busy = 0, worst = 0, sumSq = 0;
        for (size_t pos = 0; pos < total; pos += block)
        {
            const int frames = (int)std::min<size_t>(block, total - pos);
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
            const bool playing = !hits && pos < songLen;
            g_time.samplePos = (double)pos; g_time.ppqPos = beat;
            g_time.flags = kVstTempoValid | kVstPpqPosValid | kVstTimeSigValid | (playing ? kVstTransportPlaying : 0);
            float* outs[2] = {oL.data(), oR.data()};
            std::fill(oL.begin(), oL.end(), 0.0f); std::fill(oR.begin(), oR.end(), 0.0f);
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
        printf("%-22s %-12s peak %6.1f dBFS  rms %6.1f dBFS  cpu avg %.2f%%  worst block %.1f%%\n", argv[a], pname,
               20 * log10(peak + 1e-9), 10 * log10(sumSq / songLen + 1e-12), 100.0 * busy / (total / g_sr), 100.0 * worst);
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
