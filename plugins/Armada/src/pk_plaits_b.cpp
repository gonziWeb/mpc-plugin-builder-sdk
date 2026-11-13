// SPDX-License-Identifier: GPL-3.0-only
// Armada pack: Plaits, the macro-oscillator of Mutable Instruments (Emilie Gillet), from the module's own sources
// (github.com/pichenettes/eurorack, plaits/ and stmlib/, MIT; copied unchanged into src/plaits and src/stmlib).
// This file (Gonzi Audio, GPL-3.0-only) builds them as one unit and plays six of the module's voices polyphonically:
// each of the module's 24 models is an engine of the plugin.
// (This second unit: the files whose names clash with the others' when built together.)
//
// The module runs at 48 kHz; here its voice renders at the plugin's 44.1 kHz, so the note is raised by the ratio
// (1.47 semitones) and the pitch comes out right. A voice gets the note's trigger (the models that are struck or
// plucked need it) and, as the module's LEVEL input, an attack / decay / sustain / release envelope.
#define TEST 1                                    // (plaits/user_data.h: no flash, no user data)
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <new>

#if defined(__clang__)
#pragma clang diagnostic ignored "-Wunused-parameter"
#pragma clang diagnostic ignored "-Wunused-variable"
#pragma clang diagnostic ignored "-Wunused-function"
#pragma clang diagnostic ignored "-Wsign-compare"
#pragma clang diagnostic ignored "-Wmissing-field-initializers"
#pragma clang diagnostic ignored "-Wunused-private-field"
#pragma clang diagnostic ignored "-Wunused-const-variable"
#pragma clang diagnostic ignored "-Wimplicit-fallthrough"
#pragma clang diagnostic ignored "-Wdeprecated-declarations"
#pragma clang diagnostic ignored "-Wshift-negative-value"
#pragma clang diagnostic ignored "-Wextra"
#pragma clang diagnostic ignored "-Wall"
#endif

#include "plaits/dsp/engine2/six_op_engine.cc"
#include "plaits/dsp/engine2/string_machine_engine.cc"
#include "plaits/dsp/fm/algorithms.cc"
#include "plaits/dsp/fm/dx_units.cc"
