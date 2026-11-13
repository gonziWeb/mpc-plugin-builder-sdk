// test_garden_engine.cpp - Slow Garden: is the rearranged engine (src/core/amb_engine.h) still AMB-1's?
// SPDX-License-Identifier: MPL-2.0   Copyright (C) 2026 Gonzi Audio
//
// Runs AMB-1's own amb.c (compiled as C, one global engine) and two amb1::Engine objects side by side through the
// same script (every piece, parameter moves, scale changes, freeze, next chord, rest and grow) and compares every
// sample. The two objects are rendered interleaved, so state shared between instances would show as a difference.
//
// Build and run from the SDK folder (run it again after  python3 "plugins/Slow Garden/tools/make_engine.py"):
//   P="plugins/Slow Garden/src"
//   gcc -O2 -ffp-contract=off -DEN_HOST -I"$P/amb" -c "$P/amb/amb.c" -o .sdk-tools/amb_ref.o
//   g++ -O2 -ffp-contract=off -std=c++17 -I"$P" gonzi/tools/test_garden_engine.cpp .sdk-tools/amb_ref.o -o .sdk-tools/test_garden_engine
//   .sdk-tools/test_garden_engine
#include <cstdint>
#include <cstdio>
#include <cstring>

extern "C" {
void en_init(void);
void en_set(int param, int value);
void en_pool(uint32_t pool);
void en_play(int on);
void en_next(void);
void en_freeze(int on);
void en_seed(uint32_t s);
void en_regrow(void);
void en_render(int32_t* out_lr, uint32_t n, uint32_t gain_q12);
void en_midi_out(uint32_t, uint32_t, uint32_t) {}
}

#include "amb/fastmath.h"
#include "core/amb_engine.h"

static amb1::Engine* A;
static amb1::Engine* B;
static long long frames, diffs;
static int32_t ref[2 * 512], a[2 * 512], b[2 * 512];

template <typename F> static void all(F f) { f(0); f(1); f(2); }
#define ALL(call) do { call; A->call; B->call; } while (0)

static void run(double secs, uint32_t n)
{
    long long todo = (long long)(secs * 44100.0);
    while (todo > 0)
    {
        en_render(ref, n, 2048);
        A->en_render(a, n, 2048);
        B->en_render(b, n, 2048);
        if (memcmp(ref, a, 8 * n) != 0) diffs++;
        if (memcmp(ref, b, 8 * n) != 0) diffs++;
        frames += n;
        todo -= n;
    }
}

int main()
{
    A = new amb1::Engine();
    B = new amb1::Engine();
    ALL(en_init());
    ALL(en_seed(12345));
    ALL(en_play(1));
    static const uint32_t blocks[4] = {256, 64, 512, 37};
    int16_t par[amb1::P_NPARAMS];
    uint16_t pool;
    for (int piece = 0; piece < EN_NPIECES; ++piece)
    {
        A->en_piece(piece, par, &pool);
        for (int p = 0; p < amb1::P_NPARAMS; ++p)
            if (p != amb1::P_TUNE && p != amb1::P_MIDI) ALL(en_set(p, par[p]));
        ALL(en_pool(pool));
        ALL(en_regrow());
        run(40.0, blocks[piece & 3]);
        ALL(en_next());
        ALL(en_set(amb1::P_DRIFT, 0));
        ALL(en_set(amb1::P_EVOLVE, 100));
        run(25.0, blocks[(piece + 1) & 3]);
        ALL(en_pool(pool ^ 0x0A4u));
        ALL(en_freeze(1));
        run(6.0, 128);
        ALL(en_freeze(0));
        ALL(en_set(amb1::P_ROOT, (piece * 5) % 12));
        ALL(en_set(amb1::P_PADTONE, piece & 1));
        ALL(en_play(0));
        run(4.0, 256);
        ALL(en_play(1));
        run(10.0, 256);
    }
    printf("%lld frames (%.0f s) x 2 engines compared with AMB-1's amb.c: %lld blocks differ\n", frames,
           (double)frames / 44100.0, diffs);
    printf(diffs ? "FAIL\n" : "PASS: sample for sample\n");
    return diffs ? 1 : 0;
}
