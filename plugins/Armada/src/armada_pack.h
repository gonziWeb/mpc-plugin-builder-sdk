// SPDX-License-Identifier: GPL-3.0-only
// Copyright (C) 2026 Gonzi Audio
// Armada: what a "pack" (one ported firmware: a family of engines) gives the plugin.
//
// A pack is one generated translation unit (src/pk_<name>.cpp, made by tools/make_pack.py from the firmware's own C
// sources). Its state is per instance: Pack::create() makes a Part, the plugin's own copy of that firmware.
// A Part renders at 44100 Hz, at most kBlock frames a call (the firmwares' control block).
#ifndef ARMADA_PACK_H
#define ARMADA_PACK_H
#include <cstdint>

// A page of zeros for the firmwares' reads of the FM-1's flash (user samples, user wavetables: there is no such
// flash here; a zero header is "empty slot" to every one of them). One for the whole plugin.
inline unsigned char armada_zero_page[1u << 18];

namespace armada {

enum { kNumE = 32, kBlock = 32, kRate = 44100 };

struct ParamDesc {
    const char *label = "";            // "" = unused
    int min = 0, max = 0, def = 0;
};

struct EngineInfo {
    const char *name = "";
    int numSounds = 0;                 // the engine's own factory sounds
    int numParams = 0;                 // E1..: how many are used
    ParamDesc p[kNumE];
};

// The plugin's parameters every engine shares, in the firmwares' own units (0..127 unless noted). The order is the
// plugin's parameter order from Attack on.
enum CommonIndex {
    cAtk, cDec, cSus, cRel,
    cFenv, cPenv, cSenv,                       // env -> filter / pitch / shape, -64..63
    cLfoRate, cLfoWave, cLfoFade,
    cLfoPitch, cLfoFilter, cLfoShape, cLfoAmp, // -64..63 (amp 0..127)
    cVoice,                                    // 0 poly, 1 mono, 2 legato, 3 unison
    cGlide, cDetune, cSpread,
    cDist, cChorus, cDelay, cReverb,           // the part's sends
    cDlyTime, cDlyFdbk, cRevSize, cRevDamp, cRevType, cChoRate, cChoDepth,
    cFtype,                                    // filter type where the engine has one: LP BP HP
    kNumCommon
};
struct Common {
    int v[kNumCommon] = {};
    int tune = 0;                              // cents, -100..100
};

// What a factory sound sets: the engine's own parameters, and those of the common ones it has a value for
// (bit i of valid: c[i]). The plugin's parameters take these values when the sound is chosen.
struct SoundValues {
    int e[kNumE] = {};
    int c[kNumCommon] = {};
    uint32_t valid = 0;
    void set(int i, int value) { c[i] = value; valid |= 1u << i; }
};

class Part {
public:
    virtual ~Part() {}
    // Switch to engine e and load its factory sound s (the hidden parts of a patch too). vals, when given, gets
    // the values the sound set, for the plugin's parameters.
    virtual void loadSound(int e, int s, SoundValues *vals) = 0;
    virtual void setE(int k, int v) = 0;                     // engine parameter k, in the engine's units
    virtual void setCommon(const Common &c) = 0;
    virtual void noteOn(int note, int vel) = 0;
    virtual void noteOff(int note) = 0;
    virtual void allNotesOff() = 0;
    virtual void bend(int cents) = 0;
    virtual void modWheel(int v) { (void)v; }
    virtual void render(float *l, float *r, int n) = 0;      // n <= kBlock; adds nothing: writes l and r
    virtual void format(int e, int k, int v, char *out, int outSize) = 0;   // an engine parameter's text
};

struct Pack {
    const char *id;                    // "felucca"
    const char *title;                 // "Felucca 1.8.2 (Hügelton Instruments)"
    int (*numEngines)();
    const EngineInfo *(*engine)(int e);
    const char *(*soundName)(int e, int s);
    Part *(*create)();                 // not on the audio thread
};

} // namespace armada
#endif
