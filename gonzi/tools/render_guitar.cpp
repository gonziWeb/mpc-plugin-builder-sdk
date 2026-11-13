// render_guitar.cpp - play a dry electric-guitar part (DI) through a VST2 effect, one section per program.
// Gonzi Audio, MIT.
//
// Usage: render_guitar <plugin.so> <out.wav> <bpm> <section> [<section> ...]
//   section = -1                     the dry guitar
//           = <program>              that built-in program
//           = <program>:<i>=<v>,...  the program, then parameters set by index to a normalised value
// MPC_DI=<file.wav> (16-bit PCM, first channel used) replaces the built-in guitar with a real DI take.
// Prints peak, RMS and desktop CPU for each section.
//
// The built-in guitar is a plucked-string model (Karplus-Strong with pick position and a pickup
// low-pass): two bars of doom riff (power chords, palm-muted chugs, a short lead), then a bar of
// silence so delay and reverb tails can be heard.
#include <dlfcn.h>
#include <chrono>
#include <cmath>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
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

static uint32_t g_rng = 2463534242u;
static float rnd() { g_rng ^= g_rng << 13; g_rng ^= g_rng >> 17; g_rng ^= g_rng << 5; return (float)(g_rng >> 8) / 8388608.0f - 1.0f; }

// One plucked string added to `out` at time t. mute > 0 = palm mute; bend = vibrato depth in semitones.
static void pluck(std::vector<float>& out, double t, double midi, double len, float vel, float mute, float bend)
{
    const double f0 = 440.0 * std::pow(2.0, (midi - 69.0) / 12.0);
    const size_t s0 = (size_t)(t * g_sr), n = (size_t)(len * g_sr);
    const int size = (int)(g_sr / f0) + 8;
    std::vector<float> line((size_t)size * 2, 0.0f);
    const double period = g_sr / f0;
    // Excitation: a noise burst, darkened, with a comb for the pick position (1/7 of the string).
    float lp = 0.0f;
    const int pickDelay = std::max(1, (int)(period / 7.0));
    std::vector<float> ex((size_t)period + 2);
    for (float& v : ex) { lp += 0.35f * (rnd() - lp); v = lp; }
    for (size_t i = ex.size(); i-- > (size_t)pickDelay;) ex[i] -= ex[i - (size_t)pickDelay];
    const float damp = 0.5f - 0.32f * mute;                 // loop low-pass: brighter string = closer to 0.5/0.5 average
    const float loss = (float)std::pow(mute > 0.0f ? 0.02 : 0.25, 1.0 / (f0 * (mute > 0.0f ? 0.18 : 3.5)));
    std::vector<float> buf((size_t)size + 4, 0.0f);
    int w = 0; float prev = 0.0f, pk = 0.0f, pk2 = 0.0f;
    for (size_t i = 0; i < n && s0 + i < out.size(); ++i)
    {
        const double vib = bend * std::sin(2.0 * M_PI * 5.2 * (double)i / g_sr) * std::min(1.0, (double)i / (0.25 * g_sr));
        const double d = period * std::pow(2.0, -vib / 12.0) - 0.5;
        const int di = (int)d; const float fr = (float)(d - di);
        const int size2 = (int)buf.size();
        const float a = buf[(size_t)((w - di + size2 * 4) % size2)], b = buf[(size_t)((w - di - 1 + size2 * 4) % size2)];
        float y = a + fr * (b - a);
        const float fb = loss * ((1.0f - damp) * y + damp * prev); prev = y;
        const float x = i < ex.size() ? ex[i] : 0.0f;
        buf[(size_t)w] = x + fb; w = (w + 1) % size2;
        pk += 0.45f * (y - pk); pk2 += 0.45f * (pk - pk2);   // pickup + cable roll-off
        const double rel = (double)(n - i) / (0.02 * g_sr);  // 20 ms release at the end of the note
        out[s0 + i] += vel * pk2 * (float)std::min(1.0, rel);
    }
}

static void makeGuitar(std::vector<float>& x, double bpm)
{
    const double beat = 60.0 / bpm;
    x.assign((size_t)(12.0 * beat * g_sr), 0.0f);
    auto chord = [&](double b, double root, double len) {                    // power chord, strummed
        pluck(x, b * beat, root, len * beat, 0.9f, 0, 0); pluck(x, b * beat + 0.012, root + 7, len * beat, 0.8f, 0, 0);
        pluck(x, b * beat + 0.024, root + 12, len * beat, 0.7f, 0, 0); };
    chord(0, 40, 1.95); chord(2, 43, 0.95); chord(3, 45, 0.95);              // E5, G5, A5
    for (int i = 0; i < 4; ++i) pluck(x, (4 + i * 0.5) * beat, 40, 0.45 * beat, 1.0f, 1.0f, 0);   // palm-muted low E
    const double lick[] = { 64, 67, 69, 71, 69, 67 }; const double at[] = { 6, 6.5, 6.75, 7, 7.5, 7.75 };
    for (int i = 0; i < 6; ++i) pluck(x, at[i] * beat, lick[i], (i == 5 ? 1.2 : 0.3) * beat, 0.85f, 0, i == 5 ? 0.35f : 0.0f);
    float peak = 0.0f; for (float v : x) peak = std::max(peak, std::fabs(v));
    for (float& v : x) v *= 0.25f / peak;                                    // a DI peaks around -12 dBFS
}

static bool loadDi(const char* path, std::vector<float>& x)
{
    FILE* f = fopen(path, "rb"); if (!f) return false;
    std::vector<uint8_t> d; uint8_t tmp[65536]; size_t r;
    while ((r = fread(tmp, 1, sizeof tmp, f)) > 0) d.insert(d.end(), tmp, tmp + r);
    fclose(f);
    if (d.size() < 44 || memcmp(d.data(), "RIFF", 4) != 0) return false;
    int ch = 1; size_t pos = 12; uint32_t rate = 44100;
    while (pos + 8 <= d.size()) {
        uint32_t len; memcpy(&len, &d[pos + 4], 4);
        if (memcmp(&d[pos], "fmt ", 4) == 0) { uint16_t c; memcpy(&c, &d[pos + 10], 2); ch = c; memcpy(&rate, &d[pos + 12], 4); }
        if (memcmp(&d[pos], "data", 4) == 0) {
            for (size_t i = pos + 8; i + 2 * (size_t)ch <= std::min(d.size(), pos + 8 + len); i += 2 * (size_t)ch) { int16_t s; memcpy(&s, &d[i], 2); x.push_back(s / 32768.0f); }
            break; }
        pos += 8 + len + (len & 1);
    }
    if (rate != 44100) fprintf(stderr, "note: %s is %u Hz, played as 44100\n", path, rate);
    return !x.empty();
}

int main(int argc, char** argv)
{
    if (argc < 5) { fprintf(stderr, "usage: render_guitar <plugin.so> <out.wav> <bpm> <section>...\n"); return 1; }
    void* lib = dlopen(argv[1], RTLD_NOW);
    if (!lib) { fprintf(stderr, "%s\n", dlerror()); return 1; }
    typedef AEffect* (*Main)(audioMasterCallback);
    AEffect* fx = ((Main)dlsym(lib, "VSTPluginMain"))(host);
    const double bpm = atof(argv[3]);

    std::vector<float> in;
    if (!(getenv("MPC_DI") && loadDi(getenv("MPC_DI"), in))) makeGuitar(in, bpm);
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
            const char* p = strchr(argv[a], ':');
            while (p && *++p) { int idx = atoi(p); const char* eq = strchr(p, '='); if (!eq) break; fx->setParameter(fx, idx, (float)atof(eq + 1)); p = strchr(eq, ','); }
            fx->dispatcher(fx, effSetBlockSize, 0, 128, nullptr, 0);  // makes the resume a full prepare, as on the MPC:
            fx->dispatcher(fx, effMainsChanged, 0, 1, nullptr, 0);   // files are loaded before the first block
        }
        float oL[128], oR[128]; double peak = 0, sum = 0, cpu = 0;
        for (size_t pos = 0; pos < in.size(); pos += 128) {
            int frames = (int)std::min<size_t>(128, in.size() - pos);
            g_time.samplePos = (double)pos; g_time.ppqPos = pos / g_sr * bpm / 60.0;
            float iL[128], iR[128]; memcpy(iL, &in[pos], frames * 4); memcpy(iR, &in[pos], frames * 4);
            float* ins[2] = {iL, iR}; float* outs[2] = {oL, oR};
            auto t0 = std::chrono::steady_clock::now();
            if (prog >= 0) fx->processReplacing(fx, ins, outs, frames);
            else { memcpy(oL, iL, frames * 4); memcpy(oR, iR, frames * 4); }
            cpu += std::chrono::duration<double>(std::chrono::steady_clock::now() - t0).count();
            for (int i = 0; i < frames; ++i) {
                peak = std::max(peak, (double)std::max(std::fabs(oL[i]), std::fabs(oR[i]))); sum += 0.5 * (oL[i] * oL[i] + oR[i] * oR[i]);
                out.push_back((int16_t)lrintf(fmaxf(-1, fminf(1, oL[i])) * 32767));
                out.push_back((int16_t)lrintf(fmaxf(-1, fminf(1, oR[i])) * 32767)); }
        }
        char name[64] = "dry";
        if (prog >= 0) fx->dispatcher(fx, effGetProgramName, 0, 0, name, 0);
        printf("%-16s peak %6.1f dBFS  rms %6.1f dBFS  cpu %5.2f %% (desktop)\n", name, 20 * log10(peak + 1e-12),
               10 * log10(sum / in.size() + 1e-12), 100.0 * cpu / (in.size() / g_sr));
        for (int i = 0; i < (int)(0.4 * g_sr) * 2; ++i) out.push_back(0);
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
