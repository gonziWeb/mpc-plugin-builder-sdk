// grimm_core.h - GR!MM's sound engines and effects (and GHOULBOX's hurdy-gurdy), compiled into Grimoire.
//
// SPDX-License-Identifier: GPL-3.0-only
// Copyright (C) 2026 Gonzi Audio (this file). The sources under grimm/ are GR!MM's, Felucca's, GHOULBOX's and msfa's,
// see ../LICENSING.md.
//
// GR!MM is one C program built as a single unit (grimm.c #includes the rest). Grimoire keeps that: the files under
// grimm/ are included here, inside namespace grimm, as C++. They were written for one FM-1, with their state in file
// globals; a plugin can be loaded many times in one process, so every global that changes was moved into a state
// struct of its file, reached through a pointer that Grimoire points at the instance's own state before it runs the
// sound (grimm_bind). The read-only tables stay shared.
#pragma once
#include <stdint.h>
#include <string.h>
#include <new>

#define _Static_assert static_assert
#define FS 44100u                                // GR!MM's sample rate (Grimoire resamples to the host's if it differs)

#if defined(__GNUC__)
#pragma GCC diagnostic push
#pragma GCC diagnostic ignored "-Wunused-function"
#endif

namespace grimm {

static thread_local void **grimm_parts;          // the running instance's state (set by the plugin before it runs the sound)

#define __attribute__(x)                         // (section(".pool") etc.: the FM-1's memory map)
#include "core/analog_tables.h"

/* 2^(i/192), Q16 (GR!MM samples.c, Felucca's eng_sample.c) */
static uint32_t pow2_q16(int32_t d16)
{
    uint32_t u = (uint32_t)(d16 + 192 * 16), oct = u / 192u;
    uint32_t r = pitch_inc(1600 + u % 192u) / (pitch_inc(1600) >> 16);
    return oct >= 16u ? r << (oct - 16u) : r >> (16u - oct);
}

#include "core/dsp.c"
#include "core/wear.c"
#include "core/voices.c"
#include "core/grain.c"
#define LV_COUNT(c, x, k) ((void)0)
#include "core/hall.c"
#include "core/tape.c"
#include "core/tapefx.c"
#include "core/lfo.c"
#include "core/mix.c"
#undef __attribute__
#include "core/parts.h"

} // namespace grimm

#if defined(__GNUC__)
#pragma GCC diagnostic pop
#endif
