// render_demo.cpp - feed a synthetic beat through a VST2 effect, one section per program.
// Usage: render_demo <plugin.so> <out.wav> <bpm> <program> [<program> ...]   (program -1 = dry)
#include <dlfcn.h>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <cstdint>
#include <cstring>
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

// A simple 2-bar boom-bap loop with an electric-piano-ish chord stab.
static void makeLoop(std::vector<float>& L, std::vector<float>& R, double bpm, int bars, int silentBars)
{
    const double beat = 60.0 / bpm;
    const size_t n = (size_t)((bars + silentBars) * 4 * beat * g_sr);
    L.assign(n, 0.0f); R.assign(n, 0.0f);
    uint32_t rng = 12345;
    auto rnd = [&]() { rng ^= rng << 13; rng ^= rng >> 17; rng ^= rng << 5; return (float)(rng >> 8) / 8388608.0f - 1.0f; };
    auto addKick = [&](double t) {
        size_t s0 = (size_t)(t * g_sr); double ph = 0;
        for (size_t i = 0; i < (size_t)(0.35 * g_sr) && s0 + i < n; ++i) {
            double tt = i / g_sr; double f = 45 + 110 * std::exp(-tt * 30); ph += 2 * M_PI * f / g_sr;
            float v = (float)(0.9 * std::sin(ph) * std::exp(-tt * 9)); L[s0 + i] += v; R[s0 + i] += v; } };
    auto addSnare = [&](double t) {
        size_t s0 = (size_t)(t * g_sr); float lp = 0;
        for (size_t i = 0; i < (size_t)(0.25 * g_sr) && s0 + i < n; ++i) {
            double tt = i / g_sr; float nz = rnd(); lp += 0.5f * (nz - lp);
            float v = (float)((0.45 * lp + 0.25 * std::sin(2 * M_PI * 190 * tt)) * std::exp(-tt * 16));
            L[s0 + i] += v; R[s0 + i] += v; } };
    auto addHat = [&](double t, float g) {
        size_t s0 = (size_t)(t * g_sr); float prev = 0;
        for (size_t i = 0; i < (size_t)(0.06 * g_sr) && s0 + i < n; ++i) {
            double tt = i / g_sr; float nz = rnd(); float hp = nz - prev; prev = nz;
            float v = (float)(g * 0.18 * hp * std::exp(-tt * 70)); L[s0 + i] += v * 0.8f; R[s0 + i] += v; } };
    auto addChord = [&](double t, const double* notes, int count, double len) {
        size_t s0 = (size_t)(t * g_sr);
        for (size_t i = 0; i < (size_t)(len * g_sr) && s0 + i < n; ++i) {
            double tt = i / g_sr; double env = std::min(1.0, tt * 200) * std::exp(-tt * 3.5); double v = 0;
            for (int k = 0; k < count; ++k) { double f = 440 * std::pow(2.0, (notes[k] - 69) / 12.0);
                v += std::sin(2 * M_PI * f * tt) + 0.3 * std::sin(4 * M_PI * f * tt) * std::exp(-tt * 8); }
            float s = (float)(0.07 * v * env); L[s0 + i] += s; R[s0 + i] += s; } };
    const double chordA[] = {57, 60, 64, 67}, chordB[] = {53, 57, 60, 64};
    for (int b = 0; b < bars; ++b) {
        double t0 = b * 4 * beat;
        addKick(t0); addKick(t0 + 1.5 * beat); addKick(t0 + 2.75 * beat);
        addSnare(t0 + beat); addSnare(t0 + 3 * beat);
        for (int h = 0; h < 8; ++h) addHat(t0 + h * 0.5 * beat, (h % 2) ? 0.6f : 1.0f);
        addChord(t0, b % 2 ? chordB : chordA, 4, 1.2 * beat);
    }
}

int main(int argc, char** argv)
{
    if (argc < 5) { fprintf(stderr, "usage\n"); return 1; }
    void* lib = dlopen(argv[1], RTLD_NOW);
    if (!lib) { fprintf(stderr, "%s\n", dlerror()); return 1; }
    typedef AEffect* (*Main)(audioMasterCallback);
    AEffect* fx = ((Main)dlsym(lib, "VSTPluginMain"))(host);
    const double bpm = atof(argv[3]);

    std::vector<float> inL, inR;
    makeLoop(inL, inR, bpm, 2, 2);
    for (size_t i = 0; i < inL.size(); ++i) { inL[i] *= 0.6f; inR[i] *= 0.6f; }
    std::vector<int16_t> out;

    fx->dispatcher(fx, effOpen, 0, 0, nullptr, 0);
    fx->dispatcher(fx, effSetSampleRate, 0, 0, nullptr, (float)g_sr);
    fx->dispatcher(fx, effSetBlockSize, 0, 128, nullptr, 0);
    memset(&g_time, 0, sizeof g_time);
    g_time.sampleRate = g_sr; g_time.tempo = bpm; g_time.timeSigNumerator = 4; g_time.timeSigDenominator = 4;
    g_time.flags = kVstTempoValid | kVstPpqPosValid | kVstTransportPlaying | kVstTimeSigValid;

    for (int a = 4; a < argc; ++a) {
        const int prog = atoi(argv[a]);
        if (prog >= 0) {
            fx->dispatcher(fx, effMainsChanged, 0, 0, nullptr, 0);
            fx->dispatcher(fx, effSetProgram, 0, prog, nullptr, 0);
            fx->dispatcher(fx, effMainsChanged, 0, 1, nullptr, 0);   // resume -> reset() clears the tape
        }
        float oL[128], oR[128];
        for (size_t pos = 0; pos < inL.size(); pos += 128) {
            int frames = (int)std::min<size_t>(128, inL.size() - pos);
            g_time.samplePos = (double)pos; g_time.ppqPos = pos / g_sr * bpm / 60.0;
            float* ins[2] = {&inL[pos], &inR[pos]};
            float* outs[2] = {oL, oR};
            if (prog >= 0) fx->processReplacing(fx, ins, outs, frames);
            else { memcpy(oL, ins[0], frames * 4); memcpy(oR, ins[1], frames * 4); }
            for (int i = 0; i < frames; ++i) {
                out.push_back((int16_t)lrintf(fmaxf(-1, fminf(1, oL[i])) * 32767));
                out.push_back((int16_t)lrintf(fmaxf(-1, fminf(1, oR[i])) * 32767)); }
        }
        for (int i = 0; i < (int)(0.4 * g_sr) * 2; ++i) out.push_back(0);   // short gap
    }
    fx->dispatcher(fx, effMainsChanged, 0, 0, nullptr, 0);
    fx->dispatcher(fx, effClose, 0, 0, nullptr, 0);

    FILE* f = fopen(argv[2], "wb");
    uint32_t dataBytes = (uint32_t)(out.size() * 2), riff = 36 + dataBytes, fmtLen = 16, sr = 44100, byteRate = sr * 4;
    uint16_t fmt = 1, ch = 2, align = 4, bitsPer = 16;
    fwrite("RIFF", 1, 4, f); fwrite(&riff, 4, 1, f); fwrite("WAVEfmt ", 1, 8, f); fwrite(&fmtLen, 4, 1, f);
    fwrite(&fmt, 2, 1, f); fwrite(&ch, 2, 1, f); fwrite(&sr, 4, 1, f); fwrite(&byteRate, 4, 1, f);
    fwrite(&align, 2, 1, f); fwrite(&bitsPer, 2, 1, f); fwrite("data", 1, 4, f); fwrite(&dataBytes, 4, 1, f);
    fwrite(out.data(), 2, out.size(), f); fclose(f);
    printf("wrote %s (%.1f s)\n", argv[2], out.size() / 2 / g_sr);
    return 0;
}
