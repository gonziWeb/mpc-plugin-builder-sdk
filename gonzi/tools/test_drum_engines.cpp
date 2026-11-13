// test_drum_engines.cpp - Drum Rat's copies of X0X's drum808 / drum909 (compiled as C++, in a namespace, with their
// three per-render variables thread-local) against X0X's own sources compiled as C: the same samples.
// Build (X = a checkout of github.com/charlesvestal/fm1-x0x at 512c0da, after `python3 tools/gen_drum_samples.py build/gen/x0x_drum_samples.h`):
//   gcc -O2 -ffp-contract=off -std=c99 -I$X/firmware/src/dsp -I$X/build/gen -c $X/firmware/src/dsp/drum808.c -o d8.o
//   gcc -O2 -ffp-contract=off -std=c99 -I$X/firmware/src/dsp -I$X/build/gen -c $X/firmware/src/dsp/drum909.c -o d9.o
//   g++ -O2 -ffp-contract=off -std=c++17 -I"plugins/Drum Rat/src" gonzi/tools/test_drum_engines.cpp d8.o d9.o -o test_drum_engines
#include <stdint.h>
#include <cstdio>
#include <cstring>
#include <cmath>
#define _Static_assert static_assert
#define X0X_TLS thread_local
namespace ours {
#include "x0x/drum808.c"
#undef SR
#undef PP
#undef TRK
#include "x0x/drum909.c"
}
extern "C" {
struct drum808; struct drum909;
void drum808_init(drum808*); void drum808_trigger(drum808*, int, float); void drum808_set(drum808*, int, int, int);
void drum808_render_st(drum808*, float*, float*, float*, float*, int);
void drum909_init(drum909*); void drum909_trigger(drum909*, int, float); void drum909_set(drum909*, int, int, int);
void drum909_render_st(drum909*, float*, float*, float*, float*, int);
}
int main()
{
    static ours::drum808_t a8; static ours::drum909_t a9;
    static char b8[sizeof(ours::drum808_t) + 64], b9[sizeof(ours::drum909_t) + 64];
    drum808* r8 = (drum808*)b8; drum909* r9 = (drum909*)b9;
    ours::drum808_init(&a8); drum808_init(r8); ours::drum909_init(&a9); drum909_init(r9);
    long diff = 0, total = 0; float pk = 0; uint32_t rng = 1;
    for (int blk = 0; blk < 44100 * 60 / 64; ++blk)
    {
        rng = rng * 1664525u + 1013904223u;
        if ((blk % 43) == 0)
        {
            const int t = (rng >> 8) % 11; const float v = ((rng >> 16) & 1) ? 1.0f : 0.6f;
            ours::drum808_trigger(&a8, t, v); drum808_trigger(r8, t, v);
            ours::drum909_trigger(&a9, t, v); drum909_trigger(r9, t, v);
        }
        if ((blk % 211) == 0)
        {
            const int t = (rng >> 4) % 11, i = (rng >> 12) % 10, val = (rng >> 20) % 128;
            ours::drum808_set(&a8, t, i, val); drum808_set(r8, t, i, val);
            ours::drum909_set(&a9, t, i, val); drum909_set(r9, t, i, val);
        }
        float l1[64] = {0}, r1[64] = {0}, l2[64] = {0}, r2[64] = {0}, x[64], y[64];
        ours::drum808_render_st(&a8, l1, r1, x, y, 64); drum808_render_st(r8, l2, r2, x, y, 64);
        for (int i = 0; i < 64; ++i) { diff += l1[i] != l2[i] || r1[i] != r2[i]; pk = fmaxf(pk, fabsf(l1[i])); }
        memset(l1, 0, sizeof l1); memset(r1, 0, sizeof r1); memset(l2, 0, sizeof l2); memset(r2, 0, sizeof r2);
        ours::drum909_render_st(&a9, l1, r1, x, y, 64); drum909_render_st(r9, l2, r2, x, y, 64);
        for (int i = 0; i < 64; ++i) { diff += l1[i] != l2[i] || r1[i] != r2[i]; pk = fmaxf(pk, fabsf(l1[i])); }
        total += 128;
    }
    printf("%ld samples compared over 60 s, %ld differ, peak %.2f\n%s\n", total, diff, pk, diff ? "FAIL" : "PASS");
    return diff ? 1 : 0;
}
