// SPDX-License-Identifier: GPL-3.0-only
// Armada: a pack on its own. Every engine x its first sounds: a chord and a melody to a WAV, peak / RMS / cost;
// two Parts side by side must give the same samples as one alone (instances share nothing).
//   c++ -std=c++17 -O2 -Isrc tools/test_pack.cpp src/pk_<name>.cpp -DPACK=armada_pack_<name> -o test_pack && ./test_pack out.wav [sounds per engine]
#include "armada_pack.h"
#include <chrono>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <vector>
#if defined(__SSE__)
#include <xmmintrin.h>
#endif
extern const armada::Pack PACK;
static void phrase(armada::Part *p, int blk, int base)
{
    static const int ch[4][3] = {{0, 3, 7}, {-4, 0, 3}, {-7, -4, 0}, {-5, -1, 2}};
    int bar = blk / 500, in = blk % 500;           // a bar = 500 blocks of 32 = 0.36 s
    if (bar < 4) {
        if (in == 0) for (int k = 0; k < 3; k++) p->noteOn(base + ch[bar][k], 96);
        if (in == 420) for (int k = 0; k < 3; k++) p->noteOff(base + ch[bar][k]);
        int st = in / 125;
        if (in % 125 == 0) p->noteOn(base + 12 + ch[bar][st % 3] + (st == 3 ? 12 : 0), 110);
        if (in % 125 == 90) p->noteOff(base + 12 + ch[bar][st % 3] + (st == 3 ? 12 : 0));
    }
}
// every value of every small parameter, the ends and a few values of the others, random settings of all of them and
// of the shared ones, notes all the while: nothing may crash or give a non-finite sample
static int fuzz()
{
    unsigned rs = 12345u;
    auto rnd = [&rs](int lo, int hi) { rs = rs * 1664525u + 1013904223u; return lo + (int)((rs >> 8) % (unsigned)(hi - lo + 1)); };
    int ne = PACK.numEngines(), bad = 0;
    long total = 0;
    for (int e = 0; e < ne; e++) {
        const armada::EngineInfo *ei = PACK.engine(e);
        if (!std::strcmp(ei->name, "-")) continue;
        armada::Part *a = PACK.create();
        int nan = 0;
        auto play = [&](int blocks) {
            float l[32], r[32];
            for (int b = 0; b < blocks; b++) {
                if (b == 0) { a->noteOn(rnd(24, 96), rnd(1, 127)); a->noteOn(rnd(36, 84), 100); }
                if (b == blocks / 2) a->allNotesOff();
                a->render(l, r, 32);
                for (int i = 0; i < 32; i++) if (!std::isfinite(l[i]) || !std::isfinite(r[i])) nan++;
                total++;
            }
        };
        int ns = ei->numSounds < 1 ? 1 : ei->numSounds;
        for (int s = 0; s < ns; s++) {                       // every sound loads and plays
            fprintf(stderr, "\r%-12s sound %d      ", ei->name, s);
            a->loadSound(e, s, nullptr);
            play(6);
        }
        for (int k = 0; k < ei->numParams; k++) {
            const armada::ParamDesc &d = ei->p[k];
            if (!d.label[0]) continue;
            a->loadSound(e, 0, nullptr);
            int span = d.max - d.min;
            for (int j = 0; j <= (span <= 40 ? span : 8); j++) {
                int v = span <= 40 ? d.min + j : j == 0 ? d.min : j == 1 ? d.max : rnd(d.min, d.max);
                fprintf(stderr, "\r%-12s %s=%d        ", ei->name, d.label, v);
                a->setE(k, v);
                play(4);
            }
        }
        for (int j = 0; j < 150; j++) {                       // everything at once
            fprintf(stderr, "\r%-12s random %d      ", ei->name, j);
            if (j % 10 == 0) a->loadSound(e, rnd(0, ns - 1), nullptr);
            for (int k = 0; k < ei->numParams; k++) if (ei->p[k].label[0] && rnd(0, 2)) a->setE(k, rnd(ei->p[k].min, ei->p[k].max));
            armada::Common c;
            for (int i = 0; i < armada::kNumCommon; i++) c.v[i] = rnd(-64, 127);
            c.v[armada::cVoice] = rnd(0, 3); c.v[armada::cRevType] = rnd(0, 2); c.v[armada::cFtype] = rnd(0, 2); c.v[armada::cLfoWave] = rnd(0, 7);
            c.tune = rnd(-100, 100);
            a->setCommon(c);
            a->bend(rnd(-2400, 2400));
            play(6);
        }
        if (nan) { bad++; printf("  %-12s %d non-finite samples\n", ei->name, nan); }
        delete a;
    }
    fprintf(stderr, "\r%60s\r", "");
    printf("%s: fuzz, %ld blocks, %d engine(s) with problems\n", PACK.id, total, bad);
    return bad ? 1 : 0;
}
int main(int argc, char **argv)
{
    if (argc > 1 && !std::strcmp(argv[1], "fuzz")) return fuzz();
#if defined(__SSE__)
    _mm_setcsr(_mm_getcsr() | 0x8040u);          // flush denormals, as the plugin framework does around process()
#endif
    const char *path = argc > 1 ? argv[1] : "pack.wav";
    int per = argc > 2 ? atoi(argv[2]) : 2;
    std::vector<int16_t> wav;
    int ne = PACK.numEngines(), bad = 0;
    printf("%s: %d engines\n", PACK.title, ne);
    for (int e = 0; e < ne; e++) {
        const armada::EngineInfo *ei = PACK.engine(e);
        int ns = ei->numSounds < per ? ei->numSounds : per;
        if (ns < 1) ns = 1;
        for (int s = 0; s < ns; s++) {
            armada::Part *a = PACK.create(), *b = PACK.create(), *c = PACK.create();
            armada::SoundValues sv;
            a->loadSound(e, s, &sv); b->loadSound(e, s, nullptr);
            c->loadSound((e + 1) % ne, 0, nullptr);          // another engine, played between the two
            double sum = 0, peak = 0; long diff = 0; int nan = 0;
            auto t0 = std::chrono::steady_clock::now();
            double ta = 0;
            const int N = 2400;
            for (int blk = 0; blk < N; blk++) {
                float l[32], r[32], l2[32], r2[32], l3[32], r3[32];
                phrase(a, blk, 57); phrase(c, blk, 45); phrase(b, blk, 57);
                auto x0 = std::chrono::steady_clock::now();
                a->render(l, r, 32);
                ta += std::chrono::duration<double>(std::chrono::steady_clock::now() - x0).count();
                c->render(l3, r3, 32);
                b->render(l2, r2, 32);
                for (int i = 0; i < 32; i++) {
                    if (!std::isfinite(l[i]) || !std::isfinite(r[i])) { nan++; l[i] = r[i] = 0; }
                    if (l[i] != l2[i] || r[i] != r2[i]) diff++;
                    double m = 0.5 * (l[i] + r[i]);
                    sum += m * m; if (std::fabs(l[i]) > peak) peak = std::fabs(l[i]); if (std::fabs(r[i]) > peak) peak = std::fabs(r[i]);
                    float cl = l[i] > 1 ? 1 : l[i] < -1 ? -1 : l[i], cr = r[i] > 1 ? 1 : r[i] < -1 ? -1 : r[i];
                    wav.push_back((int16_t)(cl * 32767)); wav.push_back((int16_t)(cr * 32767));
                }
            }
            (void)t0;
            double rms = std::sqrt(sum / (N * 32.0));
            char v0[32]; a->format(e, 0, sv.e[0], v0, sizeof v0);
            const char *flag = (diff || nan || rms < 1e-4 || peak > 1.0) ? "  <-- CHECK" : "";
            if (diff || nan || rms < 1e-4) bad++;
            printf("  %-10s %2d %-14s peak %6.1f dB  rms %6.1f dB  cpu %5.2f %%  %s=%s%s%s\n", ei->name, s, PACK.soundName(e, s),
                   20 * std::log10(peak + 1e-9), 20 * std::log10(rms + 1e-9), 100.0 * ta / (N * 32.0 / 44100.0), ei->p[0].label, v0,
                   diff ? "  INSTANCES DIFFER" : "", flag);
            delete a; delete b; delete c;
        }
    }
    FILE *f = fopen(path, "wb");
    uint32_t n = (uint32_t)wav.size() * 2, v;
    fwrite("RIFF", 1, 4, f); v = 36 + n; fwrite(&v, 4, 1, f); fwrite("WAVEfmt ", 1, 8, f); v = 16; fwrite(&v, 4, 1, f);
    uint16_t h[2] = {1, 2}; fwrite(h, 2, 2, f); v = 44100; fwrite(&v, 4, 1, f); v = 44100 * 4; fwrite(&v, 4, 1, f);
    uint16_t h2[2] = {4, 16}; fwrite(h2, 2, 2, f); fwrite("data", 1, 4, f); fwrite(&n, 4, 1, f);
    fwrite(wav.data(), 2, wav.size(), f); fclose(f);
    printf("%s: %d problem(s); %s\n", PACK.id, bad, path);
    return bad ? 1 : 0;
}
