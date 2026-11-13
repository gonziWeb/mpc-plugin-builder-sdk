// test_acid_steps.cpp - Acid Rat's step sequencer, 24 checks: pattern play, running light, Write, pattern change,
// Clear, Dice>Pat, save and load. Exit code = number of failures.
//   g++ -O2 -std=c++17 -Iframework gonzi/tools/test_acid_steps.cpp -o .sdk-tools/test_acid_steps -ldl
//   .sdk-tools/test_acid_steps "plugins/Acid Rat/build/native/acid-rat.so"
#include <dlfcn.h>
#include <cmath>
#include <cstdio>
#include <cstring>
#include <vector>
#include "mpc_vst2_abi.h"
static VstTimeInfo g_time; static double g_sr = 44100.0; static int g_auto = 0;
static VstIntPtr host(AEffect*, VstInt32 op, VstInt32, VstIntPtr, void*, float)
{ if (op == audioMasterVersion) return 2400; if (op == audioMasterGetTime) return (VstIntPtr)&g_time; if (op == audioMasterAutomate) ++g_auto; return 0; }
typedef AEffect* (*Main)(audioMasterCallback);
static Main g_main; static int fails = 0;
static void check(bool ok, const char* what) { printf("%s  %s\n", ok ? "ok  " : "FAIL", what); if (!ok) ++fails; }
enum { SEQ = 14, SRC = 31, PAT = 32, STEPS = 33, WRITE = 34, COPY = 35, CLEAR = 36, POS = 37, NOTE = 38, OCT = 54, GATE = 70, ACC = 86, SLIDE = 102 };
struct Inst {
    AEffect* fx; double pos = 0; bool playing = false; std::vector<float> out;
    Inst() { fx = g_main(host); fx->dispatcher(fx, effOpen, 0, 0, nullptr, 0); fx->dispatcher(fx, effSetSampleRate, 0, 0, nullptr, (float)g_sr);
             fx->dispatcher(fx, effSetBlockSize, 0, 128, nullptr, 0); fx->dispatcher(fx, effMainsChanged, 0, 1, nullptr, 0); }
    void set(int i, float n) { fx->setParameter(fx, i, n); }
    float get(int i) { return fx->getParameter(fx, i); }
    int choice(int i, int n) { return (int)lround(get(i) * (n - 1)); }
    void note(int n, int vel) {
        static VstMidiEvent m; memset(&m, 0, sizeof m); m.type = kVstMidiType; m.byteSize = sizeof m;
        m.midiData[0] = (char)(vel ? 0x90 : 0x80); m.midiData[1] = (char)n; m.midiData[2] = (char)vel;
        static char store[sizeof(VstEvents) + sizeof(VstEvent*)]; VstEvents* e = (VstEvents*)store; e->numEvents = 1; e->events[0] = (VstEvent*)&m;
        fx->dispatcher(fx, effProcessEvents, 0, 0, e, 0); }
    void run(double beats) {            // 128 bpm
        const double bps = 128.0 / 60 / g_sr; float l[128], r[128]; float* o[2] = {l, r};
        for (double end = pos + beats / bps; pos < end; pos += 128) {
            g_time.tempo = 128; g_time.sampleRate = g_sr; g_time.ppqPos = pos * bps; g_time.flags = kVstTempoValid | kVstPpqPosValid | (playing ? kVstTransportPlaying : 0);
            fx->processReplacing(fx, nullptr, o, 128); for (float v : l) out.push_back(v); } }
    void blocks(int n) { bool p = playing; playing = false; double keep = pos; run(n * 128 * 128.0 / 60 / g_sr); pos = keep; playing = p; }
    std::vector<int> pattern() { std::vector<int> v; for (int s = 0; s < 16; ++s) v.push_back(choice(NOTE + s, 13) | choice(OCT + s, 3) << 4 | (get(GATE + s) > 0.5f) << 6 | (get(ACC + s) > 0.5f) << 7 | (get(SLIDE + s) > 0.5f) << 8); return v; }
    float peak(size_t from) { float p = 0; for (size_t i = from; i < out.size(); ++i) p = fmaxf(p, fabsf(out[i])); return p; }
};
static double f0(const std::vector<float>& x, size_t at, int n = 4096) {
    double best = 0; int lag = 0; for (int l = 150; l < 1200; ++l) { double a = 0; for (int i = 0; i < n; ++i) a += x[at + i] * x[at + i + l]; if (a > best) { best = a; lag = l; } } return g_sr / lag; }
int main(int, char** argv)
{
    void* lib = dlopen(argv[1], RTLD_NOW); g_main = (Main)dlsym(lib, "VSTPluginMain");
    Inst a;
    printf("%d parameters, %d programs\n", a.fx->numParams, a.fx->numPrograms);
    auto p1 = a.pattern();
    check(p1[0] == (9 | 0 << 4 | 64 | 128) && !(p1[4] & 64) && (p1[2] & 256), "pattern 1 is the factory line (A down accented, step 5 a rest, step 3 slides)");
    // play it with the transport: Seq = Song, Source = Pattern
    a.set(SEQ, 1.0f); a.set(SRC, 1.0f); a.playing = true; g_auto = 0;
    a.run(0.20);
    check(a.choice(POS, 17) == 1, "running light on step 1 during the first step");
    check(fabs(f0(a.out, 2000) - 55.0) < 1.0, "step 1 sounds A1 (55 Hz)");
    a.run(0.25 * 5 - 0.20 + 0.1);
    check(a.choice(POS, 17) == 6, "running light on step 6 after five steps");
    check(g_auto >= 6, "the host was told about the light");
    a.playing = false; a.run(0.25);
    check(a.choice(POS, 17) == 0, "light out when the transport stops");
    // steps = 4: the pattern wraps after four
    a.set(STEPS, 3 / 15.0f); a.pos = 0; a.playing = true; a.run(0.25 * 5 + 0.1);
    check(a.choice(POS, 17) == 2, "with Steps = 4 the sixth step played is step 2");
    a.playing = false; a.run(0.25); a.set(STEPS, 1.0f);
    // pattern 2 differs, an edit in it survives going away and back
    a.set(PAT, 1 / 7.0f); a.blocks(2);
    auto p2 = a.pattern();
    check(p2 != p1, "pattern 2 shows different steps");
    a.set(NOTE + 0, 7 / 12.0f); a.set(ACC + 0, 1.0f); a.blocks(2);
    auto p2e = a.pattern();
    a.set(PAT, 0.0f); a.blocks(2);
    check(a.pattern() == p1, "back on pattern 1: unchanged");
    a.set(PAT, 1 / 7.0f); a.blocks(2);
    check(a.pattern() == p2e, "pattern 2 kept its edit");
    // notes 12..19 choose the pattern
    a.note(12 + 4, 100); a.blocks(2); a.note(12 + 4, 0);
    check(a.choice(PAT, 8) == 4, "note 16 selects pattern 5");
    // write: three notes into pattern 5, then stop writing by hand
    a.set(WRITE, 1.0f); a.blocks(2);
    check(a.choice(POS, 17) == 1, "Write: the light shows step 1");
    a.note(40, 120); a.blocks(2); a.note(40, 0); a.blocks(2);
    a.note(55, 60); a.blocks(2); a.note(55, 0); a.blocks(2);
    a.note(24, 60); a.blocks(2); a.note(24, 0); a.blocks(2);
    auto w = a.pattern();
    check(w[0] == (4 | 1 << 4 | 64 | 128) && w[1] == (7 | 2 << 4 | 64) && w[2] == (0 | 0 << 4 | 64), "Write stored E mid accented, G up, C down");
    check(a.choice(POS, 17) == 4, "Write: the light moved to step 4");
    a.set(STEPS, 4 / 15.0f); a.blocks(2);
    a.note(36, 60); a.blocks(2); a.note(36, 0); a.note(38, 60); a.blocks(2); a.note(38, 0); a.blocks(2);
    check(a.get(WRITE) < 0.5f && a.choice(POS, 17) == 0, "Write turns itself off after the last step (Steps = 5)");
    auto p5 = a.pattern();
    // clear pattern 3
    a.set(PAT, 2 / 7.0f); a.blocks(2); a.set(CLEAR, 1.0f); a.blocks(2); a.set(CLEAR, 0.0f); a.blocks(2);
    bool empty = true; for (int v : a.pattern()) if (v & 64) empty = false;
    check(empty, "Clear empties the pattern shown");
    size_t mark = a.out.size(); a.pos = 0; a.playing = true; a.run(4); a.playing = false; a.run(0.25);
    check(a.peak(mark + 20000) < 1e-4f, "an empty pattern is silent");
    // copy the generator's line into pattern 4
    a.set(PAT, 3 / 7.0f); a.blocks(2); auto p4 = a.pattern();
    a.set(COPY, 1.0f); a.blocks(2); a.set(COPY, 0.0f); a.blocks(2);
    auto p4c = a.pattern(); int gates = 0; for (int v : p4c) gates += (v >> 6) & 1;
    check(p4c != p4 && gates >= 6, "Dice>Pat copies the generated line into the pattern");
    // save, load into a new instance
    void* chunk = nullptr; VstIntPtr n = a.fx->dispatcher(a.fx, effGetChunk, 0, 0, &chunk, 0);
    std::vector<char> saved((char*)chunk, (char*)chunk + n);
    Inst b; b.fx->dispatcher(b.fx, effSetChunk, 0, (VstIntPtr)saved.size(), saved.data(), 0); b.blocks(2);
    check(b.choice(PAT, 8) == 3 && b.pattern() == p4c, "new instance after load: pattern 4 shown, as copied");
    b.set(PAT, 4 / 7.0f); b.blocks(2); check(b.pattern() == p5 && b.choice(STEPS, 16) == 4, "loaded pattern 5 = what Write stored, 5 steps");
    b.set(PAT, 1 / 7.0f); b.blocks(2); check(b.pattern() == p2e, "loaded pattern 2 = the edited one");
    b.set(PAT, 0.0f); b.blocks(2); check(b.pattern() == p1, "loaded pattern 1 = the factory line");
    // a program change leaves the patterns alone
    b.fx->dispatcher(b.fx, effSetProgram, 0, 3, nullptr, 0); b.blocks(2);
    check(b.pattern() == p1 && b.choice(SRC, 2) == 1, "a 1.0 program changes the sound, not the pattern or its source");
    b.fx->dispatcher(b.fx, effSetProgram, 0, 9, nullptr, 0); b.blocks(2);
    check(b.choice(SRC, 2) == 1 && b.choice(SEQ, 3) == 1, "program 10 selects the pattern in Keys mode");
    printf(fails ? "%d FAILED\n" : "all passed\n", fails);
    return fails;
}
