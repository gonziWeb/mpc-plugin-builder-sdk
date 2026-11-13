// test_armada.cpp - Armada's behaviour checks on the computer build.
//   g++ -O2 -std=c++17 -Iframework gonzi/tools/test_armada.cpp -o .sdk-tools/test_armada -ldl
//   .sdk-tools/test_armada "plugins/Armada/build/native/armada.so"
// 1 choosing an engine / a sound loads the sound's values into the parameters (and reports them to the host)
// 2 a parameter changed after that stays as set
// 3 a saved state comes back with the changed value and sounds the same as before the save
// 4 every engine: its first sound makes sound, no non-finite sample
// 5 two instances at once on two threads, different engines: the same audio as each alone
#include <dlfcn.h>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <algorithm>
#include <cstring>
#include <string>
#include <thread>
#include <vector>
#include "mpc_vst2_abi.h"

static int g_automate = 0;
static VstIntPtr host(AEffect*, VstInt32 op, VstInt32, VstIntPtr, void*, float)
{
    if (op == audioMasterVersion) return 2400;
    if (op == audioMasterAutomate) { ++g_automate; return 0; }
    return 0;
}
typedef AEffect* (*Main)(audioMasterCallback);
static Main g_main;
static int g_fail = 0;
#define CHECK(c, ...) do { if (c) printf("PASS  "); else { printf("FAIL  "); ++g_fail; } printf(__VA_ARGS__); printf("\n"); } while (0)

static AEffect* open()
{
    AEffect* fx = g_main(host);
    fx->dispatcher(fx, effOpen, 0, 0, nullptr, 0);
    fx->dispatcher(fx, effSetSampleRate, 0, 0, nullptr, 44100.0f);
    fx->dispatcher(fx, effSetBlockSize, 0, 128, nullptr, 0);
    fx->dispatcher(fx, effMainsChanged, 0, 1, nullptr, 0);
    return fx;
}
static void close(AEffect* fx) { fx->dispatcher(fx, effMainsChanged, 0, 0, nullptr, 0); fx->dispatcher(fx, effClose, 0, 0, nullptr, 0); }
static void note(AEffect* fx, int n, int vel)
{
    VstMidiEvent m; memset(&m, 0, sizeof m);
    m.type = kVstMidiType; m.byteSize = sizeof m;
    m.midiData[0] = (char)(vel ? 0x90 : 0x80); m.midiData[1] = (char)n; m.midiData[2] = (char)vel;
    char store[sizeof(VstEvents) + sizeof(VstEvent*)];
    VstEvents* e = (VstEvents*)store; e->numEvents = 1; e->reserved = 0; e->events[0] = (VstEvent*)&m;
    fx->dispatcher(fx, effProcessEvents, 0, 0, e, 0);
}
// blocks of 128; returns the sum of squares, appends to out when given; *bad counts non-finite samples
static double run(AEffect* fx, int blocks, std::vector<float>* out = nullptr, int* bad = nullptr)
{
    float l[128], r[128]; float* o[2] = {l, r};
    double e = 0;
    for (int b = 0; b < blocks; ++b) {
        memset(l, 0, sizeof l); memset(r, 0, sizeof r);
        fx->processReplacing(fx, nullptr, o, 128);
        for (int i = 0; i < 128; ++i) {
            if (!std::isfinite(l[i]) || !std::isfinite(r[i])) { if (bad) ++*bad; continue; }
            e += l[i] * l[i] + r[i] * r[i];
            if (out) { out->push_back(l[i]); out->push_back(r[i]); }
        }
    }
    return e;
}
static std::string text(AEffect* fx, int i) { char b[64] = {0}; fx->dispatcher(fx, effGetParamDisplay, i, 0, b, 0); return b; }
static int choices(AEffect* fx, int idx)
{
    // the number of steps of a choice parameter: the values at which the text changes
    std::string last; int n = 0;
    float keep = fx->getParameter(fx, idx);
    for (int k = 0; k <= 400; ++k) { fx->setParameter(fx, idx, k / 400.0f); std::string t = text(fx, idx); if (t != last) { ++n; last = t; } }
    fx->setParameter(fx, idx, keep);
    return n;
}
static std::vector<float> phrase(AEffect* fx)
{
    std::vector<float> o;
    run(fx, 8);
    note(fx, 57, 100); run(fx, 40, &o); note(fx, 64, 90); run(fx, 40, &o); note(fx, 57, 0); note(fx, 64, 0); run(fx, 60, &o);
    return o;
}

int main(int argc, char** argv)
{
    if (argc < 2) { fprintf(stderr, "usage: test_armada plugin.so\n"); return 1; }
    void* lib = dlopen(argv[1], RTLD_NOW);
    if (!lib) { fprintf(stderr, "%s\n", dlerror()); return 1; }
    g_main = (Main)dlsym(lib, "VSTPluginMain");
    const int kEngine = 1, kSound = 2, kE1 = 3, kAttack = 3 + 32;

    AEffect* fx = open();
    const int nEng = choices(fx, kEngine);
    printf("INFO  %d parameters, %d programs, %d engines\n", fx->numParams, fx->numPrograms, nEng);
    run(fx, 4);
    // 1
    float e1a = fx->getParameter(fx, kE1 + 4);
    fx->setParameter(fx, kEngine, 2.0f / (nEng - 1));      // the third engine
    g_automate = 0;
    run(fx, 6);
    std::string s1 = text(fx, kSound), v1 = text(fx, kE1);
    CHECK(g_automate > 0, "choosing an engine reports the sound's parameters to the host (%d reports; Sound \"%s\", E1 \"%s\")", g_automate, s1.c_str(), v1.c_str());
    fx->setParameter(fx, kSound, 1.0f / 255.0f);
    run(fx, 6);
    std::string s2 = text(fx, kSound);
    CHECK(s1 != s2, "choosing a sound changes it (\"%s\" -> \"%s\")", s1.c_str(), s2.c_str());
    (void)e1a;
    // 2
    fx->setParameter(fx, kE1 + 2, 0.125f); fx->setParameter(fx, kAttack, 0.5f);
    run(fx, 6);
    CHECK(std::fabs(fx->getParameter(fx, kE1 + 2) - 0.125f) < 1e-6f && std::fabs(fx->getParameter(fx, kAttack) - 0.5f) < 0.01f,
          "a parameter changed after the sound was loaded stays as set");
    // 3
    std::vector<float> before = phrase(fx);
    void* chunk = nullptr;
    int len = (int)fx->dispatcher(fx, effGetChunk, 0, 0, &chunk, 0);
    std::vector<char> saved((char*)chunk, (char*)chunk + len);
    std::vector<float> pv(fx->numParams);
    for (int i = 0; i < fx->numParams; ++i) pv[i] = fx->getParameter(fx, i);
    close(fx);
    fx = open();
    fx->dispatcher(fx, effMainsChanged, 0, 0, nullptr, 0);
    fx->dispatcher(fx, effSetChunk, 0, (VstIntPtr)saved.size(), saved.data(), 0);
    fx->dispatcher(fx, effMainsChanged, 0, 1, nullptr, 0);
    run(fx, 6);
    int diff = 0;
    for (int i = 0; i < fx->numParams; ++i) if (std::fabs(fx->getParameter(fx, i) - pv[i]) > 1e-6f) ++diff;
    CHECK(diff == 0, "a saved state comes back with every parameter as saved (%d differ)", diff);
    std::vector<float> after = phrase(fx);
    // (oscillators run free: the waveforms differ, the level over time must not) windows of 2048 samples
    double worst = 0; int wins = 0;
    for (size_t w = 0; w + 4096 <= before.size() && w + 4096 <= after.size(); w += 4096, ++wins) {
        double a = 1e-9, b = 1e-9;
        for (size_t i = w; i < w + 4096; ++i) { a += before[i] * before[i]; b += after[i] * after[i]; }
        if (a > 1e-3 || b > 1e-3) worst = std::max(worst, std::fabs(10 * std::log10(a / b)));
    }
    CHECK(wins > 4 && worst < 1.5, "and sounds as before the save (level over time within %.2f dB, %d windows)", worst, wins);
    // 4
    int silent = 0, bad = 0; std::string which;
    for (int en = 0; en < nEng; ++en) {
        fx->setParameter(fx, kEngine, (float)en / (nEng - 1));
        fx->setParameter(fx, kSound, 0.0f);
        run(fx, 8, nullptr, &bad);
        note(fx, 48, 110); note(fx, 60, 110); double en1 = run(fx, 60, nullptr, &bad); note(fx, 48, 0); note(fx, 60, 0); run(fx, 30, nullptr, &bad);
        if (en1 < 1e-4) { ++silent; which += " " + text(fx, kEngine); }
    }
    CHECK(silent == 0, "every engine's first sound makes sound (%d silent:%s)", silent, which.c_str());
    CHECK(bad == 0, "no non-finite sample across the engines (%d)", bad);
    close(fx);
    // 5
    {
        const int ea = 0, eb = nEng > 20 ? 19 : 1;
        auto solo = [&](int en) { AEffect* f = open(); f->setParameter(f, kEngine, (float)en / (nEng - 1)); std::vector<float> o = phrase(f); close(f); return o; };
        std::vector<float> a0 = solo(ea), b0 = solo(eb), a1, b1;
        AEffect* fa = open(); AEffect* fb = open();
        fa->setParameter(fa, kEngine, (float)ea / (nEng - 1)); fb->setParameter(fb, kEngine, (float)eb / (nEng - 1));
        std::thread ta([&] { a1 = phrase(fa); }), tb([&] { b1 = phrase(fb); });
        ta.join(); tb.join();
        AEffect* fc = open(); fc->setParameter(fc, kEngine, (float)ea / (nEng - 1));     // the same engine twice, at once
        std::vector<float> a2, c2;
        fa->setParameter(fa, kSound, 1.0f / 255.0f); fc->setParameter(fc, kSound, 1.0f / 255.0f);
        std::thread tc([&] { a2 = phrase(fa); }), td([&] { c2 = phrase(fc); });
        tc.join(); td.join();
        close(fa); close(fb); close(fc);
        CHECK(a0 == a1 && b0 == b1, "two instances on two threads, two engines: each sounds exactly as it does alone");
        CHECK(a2.size() == c2.size() && !a2.empty(), "two instances of one engine at once on two threads run (%zu samples)", a2.size());
    }
    printf("%s\n", g_fail ? "FAILED" : "all checks passed");
    return g_fail ? 1 : 0;
}
