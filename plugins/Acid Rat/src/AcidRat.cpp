// SPDX-License-Identifier: GPL-3.0-only
// Acid Rat - a 303-style acid bass line synth for the MPC. Copyright (C) 2026 Gonzi Audio.
//
// The sound engine and the line generator come from X0X, Charles Vestal's firmware for the M-VAVE FM-1
// (github.com/charlesvestal/fm1-x0x, GPL-3.0-only): its bass303 engine is a port of Open303 (Robin Schmidt, MIT)
// with Devilfish-style slide / accent-decay ranges and a Soft / RAT drive, and TB-3PO writes random acid lines.
// Those sources are in src/x0x/ (see LICENSING.md); this file is the MPC plugin around them.
//
// Signal flow (mono until the delay):
//
//   MIDI notes ──┐                     ┌──────────────── bass303 (X0X / Open303) ─────────────────┐
//                ├─> note / accent / ─>│ saw or square -> 4-pole diode ladder -> amp -> Soft / RAT │─┬─> dry ──────┐
//   line         │   slide             │ (4x oversampled)   ^ decay envelope + accent circuit       │ │             ├─> Master -> L / R
//   generator ───┘                     └────────────────────┴──────────────────────────────────────┘ └─> ping-pong ─┘
//   (TB-3PO, follows the MPC tempo)                                                                      tempo delay
//
// Playing it from the MPC (Seq = Off):
//   * one note at a time, like the original;
//   * a note with velocity >= "Acc Vel" (100) is ACCENTED (louder, the filter opens further and "wows");
//   * a note that starts while the previous one is still held SLIDES to it without retriggering the envelopes
//     (in a sequence: make the first note long enough to overlap the next).
// Seq = Keys: the line plays while a note is held, transposed by that note (C3 / note 60 = as written).
// Seq = Song: the line plays whenever the MPC plays; the last note received transposes it.
// The line is either the generator's (Source = Dice) or a 16-step pattern written by hand (Source = Pattern):
// per step a note (C..C), an octave (Down / Mid / Up), gate, accent and slide, as on the original's panel.
// The pattern on the screen is 80 parameters (so the touch grid and the Q-Links edit it); eight patterns are kept
// in the saved state and "Pattern" chooses which one the parameters show. With Write on, each note played is
// stored in the next step. Notes 12..19 choose pattern 1..8.
//
// Realtime: nothing in process() allocates, locks or does I/O. The engine renders in pieces of at most 32 frames,
// cut at MIDI events and at the sequencer's steps, so timing is sample-accurate.

#include "mpc_plugin.h"

#include <atomic>
#include <cmath>
#include <cstdio>
#include <cstdint>
#include <cstring>
#include <string>
#include <stdint.h>
#include <utility>
#include <vector>

// ---- the X0X engine, compiled into this file (C sources that are also valid C++) --------------------------------
#define BASS303_OS 4          // oversampling of oscillator + filter: 4 as Open303 (X0X uses 2 on its microcontroller)
#define BASS303_LITE 0        // X0X's overload guard (drop the oversampling): not needed here
#define BASS303_CTRL 4        // envelopes and filter coefficients every 4 samples, interpolated (X0X's setting)
namespace x0x {
#if defined(__clang__)
#pragma clang diagnostic push
#pragma clang diagnostic ignored "-Wunused-function"
#elif defined(__GNUC__)
#pragma GCC diagnostic push
#pragma GCC diagnostic ignored "-Wunused-function"
#endif
#include "x0x/bass303.c"
#include "x0x/tb3po.c"
#if defined(__clang__)
#pragma clang diagnostic pop
#elif defined(__GNUC__)
#pragma GCC diagnostic pop
#endif
} // namespace x0x

namespace {

// The order here is the parameter order the MPC saves: permanent. Only ever append.
enum ParamIndex
{
    kMaster = 0,   // dB, output level after everything (parameter 0: harmless when the host nudges it)
    kCutoff,       // Hz, the filter's resting cutoff (the 303's range, 314..2394 Hz; the envelope goes above)
    kReso,         // %
    kEnvMod,       // %, how far the decay envelope opens the filter
    kDecay,        // ms, filter envelope decay of normal notes (accented notes use Acc Decay)
    kAccent,       // %, how much an accented note adds (level and filter)
    kWave,         // Saw / Square
    kTune,         // cents
    kOctave,       // -2..+2 octaves, applied to every note
    kDrive,        // %, 0 = bypassed
    kDriveType,    // Off / Soft / RAT
    kSlide,        // ms, slide time (stock 303: 60)
    kAccDecay,     // ms, filter envelope decay of accented notes (stock 303: 200)
    kAccVel,       // velocity from which a played note is accented
    kSeqMode,      // Off / Keys / Song
    kRate,         // step length
    kLength,       // steps in the line
    kSeed,         // which line: every seed is another one
    kDensity,      // %, how many steps play
    kSeqAccent,    // %, chance of an accent
    kSeqSlide,     // %, chance of a slide
    kOctRange,     // 1..3 octaves the line may use
    kRoot,         // C..B
    kScale,
    kBaseOct,      // octave of the root
    kMutate,       // re-roll about a quarter of the steps every N bars
    kGate,         // %, note length within a step
    kSwing,        // %
    kDelayMix,     // %
    kDelayTime,    // tempo division
    kDelayFdbk,    // %
    // ---- 1.1.0: the step sequencer (appended) ----
    kSource,       // which line the sequencer plays: Dice (the generator) / Pattern (the 16 steps below)
    kPattern,      // 1..8: which stored pattern the step parameters show and play
    kSteps,        // 1..16: the pattern's length (stored with the pattern)
    kWrite,        // on: notes played are stored step by step, then it turns itself off
    kCopyDice,     // momentary: copy the generator's line into the pattern
    kClear,        // momentary: empty the pattern
    kPos,          // read-only: the step being played (0 = stopped), for the running light
    kStepNote,                              // 16 x C, C#, .. B, C'
    kStepOct   = kStepNote + 16,            // 16 x Down / Mid / Up
    kStepGate  = kStepOct + 16,             // 16 x note or rest
    kStepAcc   = kStepGate + 16,            // 16 x accent
    kStepSlide = kStepAcc + 16,             // 16 x slide into the next step
    // ---- 1.2.0: beyond the 303 (appended). Every default leaves the sound as it was. ----
    kBlend = kStepSlide + 16,   // 118  %, the other wave mixed in
    kSub,          // %, sub-oscillator level (a square below the note)
    kSubOct,       // 1 or 2 octaves down
    kPulseW,       // %, the square's duty (46.9 = the 303's)
    kNoise,        // %, white noise into the filter
    kCutShift,     // octaves added to the cutoff
    kKeyTrack,     // %, how far the cutoff follows the note
    kFiltFM,       // %, the oscillator moves the cutoff at audio rate
    kLowCut,       // Hz, highpass after the synth (20 = off)
    kEnvAttack,    // ms, the filter envelope's rise (3 = the 303's)
    kAccSweep,     // %, size of the accent's filter sweep (100 = the 303's)
    kAmpAttack,    // ms, volume fade-in (0 = none)
    kAmpDecay,     // ms, how the volume falls while the note is held (1230 = the 303's)
    kRelease,      // ms, after the note ends (0 = the 303's: 1 ms, 50 ms accented)
    kLfoShape,     // Sine / Triangle / Saw / Square / S&H
    kLfoSync,      // Free (Hz) / Sync (tempo division)
    kLfoRate,      // Hz, when Free
    kLfoDiv,       // length of one cycle, when Sync
    kLfoCut,       // %, LFO to cutoff (100 = +-3 octaves)
    kLfoPitch,     // semitones, LFO to pitch
    kLfoPW,        // %, LFO to the square's duty
    kNumParams     // 139
};
constexpr float kStockDuty = 46.9f, kStockAmpDecay = 1230.0f;
constexpr float kLfoDivBeats[10] = {32.0f, 16.0f, 8.0f, 4.0f, 2.0f, 1.0f, 0.5f, 0.25f, 2.0f / 3.0f, 1.0f / 3.0f};

constexpr int kNumPatterns = 8, kPatSteps = 16;

// A step packed into an integer: note 0..12 | octave 0..2 << 4 | gate << 6 | accent << 7 | slide << 8.
// Octave Down starts at MIDI note 24 (C1), Mid at 36, Up at 48; the thirteenth key is the C above.
inline uint32_t packStep(int note, int oct, bool gate, bool acc, bool slide)
{
    return (uint32_t)(note & 15) | ((uint32_t)(oct & 3) << 4) | (gate ? 64u : 0u) | (acc ? 128u : 0u) | (slide ? 256u : 0u);
}
inline uint32_t packMidi(int midi, bool gate, bool acc, bool slide)     // any MIDI note, folded into 24..60
{
    while (midi < 24) midi += 12;
    while (midi > 60) midi -= 12;
    if (midi == 60) return packStep(12, 2, gate, acc, slide);
    return packStep((midi - 24) % 12, (midi - 24) / 12, gate, acc, slide);
}
inline int  stepMidi(uint32_t v)  { return 24 + 12 * (int)((v >> 4) & 3) + (int)(v & 15); }
inline bool stepGate(uint32_t v)  { return (v & 64u) != 0; }
inline bool stepAcc(uint32_t v)   { return (v & 128u) != 0; }
inline bool stepSlide(uint32_t v) { return (v & 256u) != 0; }

enum { kSeqOff = 0, kSeqKeys, kSeqSong };

constexpr float kStepBeats[5]   = {0.5f, 1.0f / 3.0f, 0.25f, 1.0f / 6.0f, 0.125f};   // 1/8 1/8T 1/16 1/16T 1/32
constexpr bool  kStepTriplet[5] = {false, true, false, true, false};
constexpr float kDelayBeats[5]  = {0.25f, 1.0f / 3.0f, 0.5f, 0.75f, 1.0f};           // 1/16 1/8T 1/8 1/8D 1/4
constexpr int   kMutateBars[5]  = {0, 1, 2, 4, 8};
constexpr int   kChunk = 32;            // frames rendered at once at most
constexpr int   kMaxHeld = 8;           // held notes remembered (for slides back to a note still held)
constexpr float kEngineVolume = 96.0f;  // the engine's own Volume pot (X0X's default): it sits before the drive

inline float dbToGain(float db) { return db <= -59.9f ? 0.0f : std::pow(10.0f, db * 0.05f); }

} // namespace

class AcidRat : public mpc::Plugin
{
public:
    AcidRat() : mpc::Plugin(makeInfo())
    {
        addParam(mpc::floatParam ("master",    "Master",     -60.0f, 6.0f, 0.0f, "dB", 1));
        addParam(mpc::logParam   ("cutoff",    "Cutoff",     314.0f, 2394.0f, 870.0f, "Hz", 0));
        addParam(mpc::floatParam ("reso",      "Reso",       0.0f, 100.0f, 60.0f, "%", 0));
        addParam(mpc::floatParam ("envmod",    "Env Mod",    0.0f, 100.0f, 50.0f, "%", 0));
        addParam(mpc::logParam   ("decay",     "Decay",      200.0f, 2000.0f, 630.0f, "ms", 0));
        addParam(mpc::floatParam ("accent",    "Accent",     0.0f, 100.0f, 60.0f, "%", 0));
        addParam(mpc::choiceParam("wave",      "Wave",       {"Saw", "Square"}, 0));
        addParam(mpc::floatParam ("tune",      "Tune",       -100.0f, 100.0f, 0.0f, "ct", 0));
        addParam(mpc::intParam   ("octave",    "Octave",     -2, 2, 0));
        addParam(mpc::floatParam ("drive",     "Drive",      0.0f, 100.0f, 0.0f, "%", 0));
        addParam(mpc::choiceParam("drivetype", "Drive Type", {"Off", "Soft", "RAT"}, 2));
        addParam(mpc::floatParam ("slide",     "Slide",      2.0f, 360.0f, 60.0f, "ms", 0, 0.5f));
        addParam(mpc::floatParam ("accdecay",  "Acc Decay",  30.0f, 3000.0f, 200.0f, "ms", 0, 0.4f));
        addParam(mpc::intParam   ("accvel",    "Acc Vel",    1, 127, 100));
        addParam(mpc::choiceParam("seqmode",   "Seq",        {"Off", "Keys", "Song"}, 0));
        addParam(mpc::choiceParam("rate",      "Rate",       {"1/8", "1/8T", "1/16", "1/16T", "1/32"}, 2));
        addParam(mpc::intParam   ("length",    "Length",     1, 64, 16));
        addParam(mpc::intParam   ("seed",      "Seed",       1, 999, 1));
        addParam(mpc::floatParam ("density",   "Density",    0.0f, 100.0f, 70.0f, "%", 0));
        addParam(mpc::floatParam ("seqaccent", "Seq Accent", 0.0f, 100.0f, 40.0f, "%", 0));
        addParam(mpc::floatParam ("seqslide",  "Seq Slide",  0.0f, 100.0f, 25.0f, "%", 0));
        addParam(mpc::intParam   ("octrange",  "Oct Range",  1, 3, 2));
        addParam(mpc::choiceParam("root",      "Root",       {"C", "C#", "D", "D#", "E", "F", "F#", "G", "G#", "A", "A#", "B"}, 9));
        addParam(mpc::choiceParam("scale",     "Scale",      {"Minor", "Phrygian", "Harm Minor", "Min Pent", "Dorian", "Major"}, 0));
        addParam(mpc::intParam   ("baseoct",   "Base Oct",   0, 3, 1));
        addParam(mpc::choiceParam("mutate",    "Mutate",     {"Off", "1 bar", "2 bars", "4 bars", "8 bars"}, 0));
        addParam(mpc::floatParam ("gate",      "Gate",       10.0f, 95.0f, 50.0f, "%", 0));
        addParam(mpc::floatParam ("swing",     "Swing",      0.0f, 100.0f, 0.0f, "%", 0));
        addParam(mpc::floatParam ("delaymix",  "Delay Mix",  0.0f, 100.0f, 0.0f, "%", 0));
        addParam(mpc::choiceParam("delaytime", "Delay Time", {"1/16", "1/8T", "1/8", "1/8D", "1/4"}, 3));
        addParam(mpc::floatParam ("delayfdbk", "Delay Fdbk", 0.0f, 90.0f, 45.0f, "%", 0));

        // ---- 1.1.0: the step sequencer. Pattern 1 is a classic line in A; 2..8 start as generated lines.
        makeFactoryPatterns();
        addParam(mpc::choiceParam("source",   "Source",   {"Dice", "Pattern"}, 0));
        addParam(mpc::choiceParam("pattern",  "Pattern",  {"1", "2", "3", "4", "5", "6", "7", "8"}, 0));
        addParam(mpc::choiceParam("steps",    "Steps",    {"1", "2", "3", "4", "5", "6", "7", "8", "9", "10", "11", "12", "13", "14", "15", "16"}, 15));
        addParam(notAutomated(mpc::boolParam("write",    "Write",    false)));
        addParam(notAutomated(mpc::boolParam("copydice", "Dice>Pat", false)));
        addParam(notAutomated(mpc::boolParam("clear",    "Clear",    false)));
        addParam(notAutomated(mpc::choiceParam("pos",    "Pos",      {"-", "1", "2", "3", "4", "5", "6", "7", "8", "9", "10", "11", "12", "13", "14", "15", "16"}, 0)));
        static const char* const kinds[5][2] = {{"note", "Note"}, {"oct", "Oct"}, {"gate", "Gate"}, {"acc", "Acc"}, {"slide", "Slide"}};
        for (int kind = 0; kind < 5; ++kind)
            for (int st = 0; st < kPatSteps; ++st)
            {
                char id[24], name[24];
                std::snprintf(id, sizeof id, "s%02d_%s", st + 1, kinds[kind][0]);
                std::snprintf(name, sizeof name, "S%d %s", st + 1, kinds[kind][1]);
                const uint32_t v = factory_[0][st];
                if (kind == 0)      addParam(mpc::choiceParam(id, name, {"C", "C#", "D", "D#", "E", "F", "F#", "G", "G#", "A", "A#", "B", "C'"}, (int)(v & 15)));
                else if (kind == 1) addParam(mpc::choiceParam(id, name, {"Down", "Mid", "Up"}, (int)((v >> 4) & 3)));
                else                addParam(mpc::boolParam(id, name, kind == 2 ? stepGate(v) : kind == 3 ? stepAcc(v) : stepSlide(v)));
            }
        for (int pt = 0; pt < kNumPatterns; ++pt)
        {
            for (int st = 0; st < kPatSteps; ++st) slots_[pt][st].store(factory_[pt][st], std::memory_order_relaxed);
            slotLen_[pt].store(kPatSteps, std::memory_order_relaxed);
        }

        // ---- 1.2.0: beyond the 303
        addParam(mpc::floatParam ("blend",     "Blend",      0.0f, 100.0f, 0.0f, "%", 0));
        addParam(mpc::floatParam ("sub",       "Sub",        0.0f, 100.0f, 0.0f, "%", 0));
        addParam(mpc::choiceParam("suboct",    "Sub Oct",    {"1 down", "2 down"}, 0));
        addParam(mpc::floatParam ("pulsew",    "Pulse W",    5.0f, 95.0f, kStockDuty, "%", 1));
        addParam(mpc::floatParam ("noise",     "Noise",      0.0f, 100.0f, 0.0f, "%", 0));
        addParam(mpc::floatParam ("cutshift",  "Cut Shift",  -3.0f, 3.0f, 0.0f, "oct", 2));
        addParam(mpc::floatParam ("keytrack",  "Key Track",  0.0f, 100.0f, 0.0f, "%", 0));
        addParam(mpc::floatParam ("filtfm",    "Filter FM",  0.0f, 100.0f, 0.0f, "%", 0));
        addParam(mpc::logParam   ("lowcut",    "Low Cut",    20.0f, 800.0f, 20.0f, "", 0));      // (unit in formatValue)
        addParam(mpc::logParam   ("envattack", "Env Attack", 3.0f, 300.0f, 3.0f, "ms", 0));
        addParam(mpc::floatParam ("accsweep",  "Acc Sweep",  0.0f, 200.0f, 100.0f, "%", 0));
        addParam(mpc::floatParam ("ampattack", "Amp Attack", 0.0f, 500.0f, 0.0f, "", 0, 0.4f));
        addParam(mpc::logParam   ("ampdecay",  "Amp Decay",  30.0f, 8000.0f, kStockAmpDecay, "ms", 0));
        addParam(mpc::floatParam ("release",   "Release",    0.0f, 2000.0f, 0.0f, "", 0, 0.4f));
        addParam(mpc::choiceParam("lfoshape",  "LFO Shape",  {"Sine", "Triangle", "Saw", "Square", "S&H"}, 0));
        addParam(mpc::choiceParam("lfosync",   "LFO Sync",   {"Free", "Sync"}, 1));
        addParam(mpc::logParam   ("lforate",   "LFO Rate",   0.05f, 30.0f, 2.0f, "Hz", 2));
        addParam(mpc::choiceParam("lfodiv",    "LFO Div",    {"8 bars", "4 bars", "2 bars", "1 bar", "1/2", "1/4", "1/8", "1/16", "1/4T", "1/8T"}, 3));
        addParam(mpc::floatParam ("lfocut",    "LFO>Cut",    0.0f, 100.0f, 0.0f, "%", 0));
        addParam(mpc::floatParam ("lfopitch",  "LFO>Pitch",  0.0f, 12.0f, 0.0f, "st", 2, 0.5f));
        addParam(mpc::floatParam ("lfopw",     "LFO>PW",     0.0f, 100.0f, 0.0f, "%", 0));

        // Built-in programs: the defaults with a few values changed each. A program sets every parameter (so that
        // choosing "Acid Rat" also turns the sub-oscillator and the LFO back off), which includes the step
        // parameters: reset() puts the patterns, the pattern shown and the sequencer's source back afterwards.
        program("Acid Rat",      {});
        program("Squelch",       {{kCutoff, 520}, {kReso, 88}, {kEnvMod, 78}, {kDecay, 330}, {kAccent, 80}});
        program("Hollow Square", {{kWave, 1}, {kCutoff, 430}, {kReso, 72}, {kEnvMod, 62}, {kDecay, 520}, {kAccent, 70}});
        program("Rat Bite",      {{kCutoff, 700}, {kReso, 80}, {kEnvMod, 70}, {kDecay, 380}, {kAccent, 75},
                                  {kDrive, 55}, {kDriveType, 2}, {kMaster, -7}});
        program("Soft Burn",     {{kCutoff, 600}, {kReso, 66}, {kEnvMod, 55}, {kDecay, 900}, {kDrive, 60}, {kDriveType, 1}, {kMaster, 3}});
        program("Devil Slide",   {{kCutoff, 480}, {kReso, 84}, {kEnvMod, 66}, {kDecay, 450}, {kSlide, 220}, {kAccDecay, 900},
                                  {kAccent, 85}});
        program("Dub Acid",      {{kCutoff, 560}, {kReso, 78}, {kEnvMod, 60}, {kDecay, 300}, {kDelayMix, 38}, {kDelayTime, 3},
                                  {kDelayFdbk, 55}});
        program("Line: Keys",    {{kSeqMode, kSeqKeys}, {kCutoff, 560}, {kReso, 84}, {kEnvMod, 72}, {kDecay, 320}, {kAccent, 80},
                                  {kSeed, 7}, {kDelayMix, 18}});
        program("Line: Rave",    {{kSeqMode, kSeqSong}, {kCutoff, 640}, {kReso, 86}, {kEnvMod, 76}, {kDecay, 300}, {kAccent, 85},
                                  {kDrive, 40}, {kDriveType, 2}, {kSeed, 23}, {kScale, 1}, {kDensity, 82}, {kSeqSlide, 30},
                                  {kMutate, 3}, {kMaster, -8}});
        program("Pattern: Keys", {{kSeqMode, kSeqKeys}, {kSource, 1}, {kCutoff, 540}, {kReso, 84}, {kEnvMod, 74}, {kDecay, 340},
                                  {kAccent, 80}});
        // 1.2.0
        program("Sub Rat",       {{kCutoff, 480}, {kReso, 72}, {kEnvMod, 62}, {kDecay, 380}, {kAccent, 75}, {kSub, 75}, {kLowCut, 20},
                                  {kDrive, 25}, {kDriveType, 2}, {kMaster, -8.5f}});
        program("PWM Hollow",    {{kWave, 1}, {kCutoff, 700}, {kReso, 58}, {kEnvMod, 48}, {kDecay, 700}, {kLfoPW, 75}, {kLfoSync, 0},
                                  {kLfoRate, 0.45f}, {kAmpDecay, 4000}, {kRelease, 120}, {kMaster, 2.5f}});
        program("Wobble",        {{kCutoff, 420}, {kReso, 74}, {kEnvMod, 20}, {kDecay, 900}, {kSub, 45}, {kBlend, 30}, {kLfoCut, 42},
                                  {kLfoSync, 1}, {kLfoDiv, 6}, {kLfoShape, 0}, {kAmpDecay, 6000}, {kRelease, 60}, {kMaster, 1.5f}});
        program("Screamer",      {{kCutoff, 620}, {kReso, 86}, {kEnvMod, 70}, {kDecay, 360}, {kAccent, 85}, {kFiltFM, 55},
                                  {kKeyTrack, 70}, {kCutShift, 0.6f}, {kAccSweep, 160}, {kDrive, 35}, {kDriveType, 2}, {kMaster, -11.5f}});
        program("Slow Riser",    {{kCutoff, 420}, {kReso, 80}, {kEnvMod, 80}, {kDecay, 1100}, {kEnvAttack, 140}, {kAmpAttack, 60},
                                  {kRelease, 350}, {kAmpDecay, 5000}, {kBlend, 40}, {kDelayMix, 25}, {kMaster, 1.5f}});
        program("S&H Robot",     {{kWave, 1}, {kCutoff, 560}, {kReso, 82}, {kEnvMod, 35}, {kDecay, 300}, {kLfoShape, 4}, {kLfoSync, 1},
                                  {kLfoDiv, 7}, {kLfoCut, 38}, {kNoise, 8}, {kPulseW, 30}, {kMaster, 4}});
    }

    bool formatValue(int index, float plain, std::string& out) override
    {
        char buf[32];
        if (index == kLowCut)         { if (plain <= 20.5f) out = "Off"; else { std::snprintf(buf, sizeof buf, "%d Hz", (int)std::lround(plain)); out = buf; } return true; }
        if (index == kAmpAttack)      { if (plain < 0.5f) out = "Off"; else { std::snprintf(buf, sizeof buf, "%d ms", (int)std::lround(plain)); out = buf; } return true; }
        if (index == kRelease)        { if (plain < 0.5f) out = "303"; else { std::snprintf(buf, sizeof buf, "%d ms", (int)std::lround(plain)); out = buf; } return true; }
        return false;
    }

    void prepare(double sampleRate, int maxBlockSize) override
    {
        (void)maxBlockSize;
        sampleRate_ = (float)sampleRate;
        x0x::bass303_set_rate(sampleRate_);
        size_t want = 1;
        while (want < (size_t)(sampleRate_ * 2.05f)) want <<= 1;    // 2 s of delay: a quarter note down to 30 bpm
        if (delayL_.size() != want) { delayL_.assign(want, 0.0f); delayR_.assign(want, 0.0f); }
        reset();
    }

    void reset() override
    {
        x0x::bass303_init(&eng_);
        x0x::bass303_set(&eng_, x0x::BASS303_VOLUME, kEngineVolume);
        for (float& v : potSet_) v = -1.0f;
        smoothInit_ = true;
        std::fill(delayL_.begin(), delayL_.end(), 0.0f);
        std::fill(delayR_.begin(), delayR_.end(), 0.0f);
        delayPos_ = 0; delayLen_ = 0.0f; fbLpL_ = fbLpR_ = fbHpL_ = fbHpR_ = 0.0f;
        gain_ = dbToGain(get(kMaster));
        numHeld_ = 0; latchNote_ = 60;
        seqRunning_ = false; seqSounding_ = false; seqPrevSlide_ = false; offCountdown_ = -1;
        beat_ = 0.0; wasPlaying_ = false; lastMode_ = -1; patternKey_ = 0; lastMutBar_ = INT64_MIN;

        for (float& v : deepSet_) v = -1e9f;      // the engine was initialised: give it the 1.2 settings again
        lowCutX_ = lowCutY_ = 0.0f; lfoPhase_ = 0.0f; lfoCycle_ = 0; lfoHold_ = 0.0f;

        // The patterns: after a state load the saved ones are waiting in the staging copy (see loadExtraState).
        // Otherwise a program may just have written defaults over the step parameters: the patterns, which pattern
        // is shown and (unless the program chooses it) what the sequencer plays are put back from what the audio
        // thread last had. When no program was applied this changes nothing.
        const bool staged = stagedReady_.exchange(false, std::memory_order_acq_rel);
        if (!staged)
        {
            const int prog = currentProgram();
            if (!(prog >= 0 && prog < (int)programSetsSource_.size() && programSetsSource_[(size_t)prog]))
                setParam(kSource, (float)source_);
            setParam(kPattern, (float)curPattern_);
            setParam(kWrite, 0.0f);
            showPattern(curPattern_);
        }
        seenProgram_ = currentProgram();
        curPattern_ = mpc::clamp(getInt(kPattern), 0, kNumPatterns - 1);
        if (staged)
        {
            for (int pt = 0; pt < kNumPatterns; ++pt)
            {
                for (int st = 0; st < kPatSteps; ++st) slots_[pt][st].store(staged_[pt][st], std::memory_order_relaxed);
                slotLen_[pt].store(stagedLen_[pt], std::memory_order_relaxed);
            }
            showPattern(curPattern_);
        }
        lastCopy_ = getBool(kCopyDice); lastClear_ = getBool(kClear);
        // Write and the running light as they were saved: the light is only moved when the step changes, so a
        // state that is loaded comes back exactly (a light saved mid-pattern stays until the next start or stop).
        writing_ = lastWrite_ = getBool(kWrite); writeCursor_ = 0; writeHeld_ = -1;
        pos_ = writing_ ? 1 : 0;
    }

    // The eight patterns travel in the saved state next to the parameters: "ARP1", then per pattern its length
    // and 16 steps of two bytes. Both run on the UI thread; the audio thread only sees them through atomics and
    // the staging copy that reset() swaps in.
    void saveExtraState(std::vector<uint8_t>& out) override
    {
        out.insert(out.end(), {'A', 'R', 'P', '1'});
        const bool pending = stagedReady_.load(std::memory_order_acquire);   // loaded but not yet taken by reset()
        for (int pt = 0; pt < kNumPatterns; ++pt)
        {
            out.push_back((uint8_t)(pending ? stagedLen_[pt] : slotLen_[pt].load(std::memory_order_relaxed)));
            for (int st = 0; st < kPatSteps; ++st)
            {
                const uint32_t v = pending ? staged_[pt][st] : slots_[pt][st].load(std::memory_order_relaxed);
                out.push_back((uint8_t)(v & 255)); out.push_back((uint8_t)(v >> 8));
            }
        }
    }

    void loadExtraState(const uint8_t* data, size_t size) override
    {
        const size_t need = 4 + (size_t)kNumPatterns * (1 + 2 * kPatSteps);
        if (size < need || std::memcmp(data, "ARP1", 4) != 0) return;
        const uint8_t* q = data + 4;
        for (int pt = 0; pt < kNumPatterns; ++pt)
        {
            stagedLen_[pt] = mpc::clamp((int)*q++, 1, kPatSteps);
            for (int st = 0; st < kPatSteps; ++st, q += 2)
            {
                const uint32_t v = (uint32_t)q[0] | ((uint32_t)q[1] << 8);
                staged_[pt][st] = packStep(mpc::clamp((int)(v & 15), 0, 12), mpc::clamp((int)((v >> 4) & 3), 0, 2),
                                           stepGate(v), stepAcc(v), stepSlide(v));
            }
        }
        stagedReady_.store(true, std::memory_order_release);
    }

    void process(mpc::ProcessContext& ctx) override
    {
        const int numFrames = ctx.numFrames;
        float* outL = ctx.outputs[0];
        float* outR = ctx.numOutputs > 1 ? ctx.outputs[1] : nullptr;
        const float sr = (float)ctx.sampleRate > 1000.0f ? (float)ctx.sampleRate : sampleRate_;

        // ---- parameters that only matter per block --------------------------------------------------------------
        updateEnginePots(false);
        const int mode = getInt(kSeqMode);
        const int rate = mpc::clamp(getInt(kRate), 0, 4);
        stepBeats_ = (double)kStepBeats[rate];
        swingBeats_ = kStepTriplet[rate] ? 0.0 : (double)(get(kSwing) * 0.005f) * stepBeats_;   // up to half a step late
        gateFrac_ = get(kGate) * 0.01f;
        accVel_ = getInt(kAccVel);
        octaveShift_ = 12 * getInt(kOctave);
        updatePattern();
        if (currentProgram() == seenProgram_) updateStepSequencer();   // (a program is being applied: wait for reset)
        updateDeep();
        seqLen_ = source_ == 1 ? patSteps_ : mpc::clamp(getInt(kLength), 1, NSTEPS);

        // ---- the clock: the MPC's song position while it plays, our own count otherwise --------------------------
        const mpc::Transport& tr = ctx.transport;
        const float bpm = (tr.bpm > 20.0 && tr.bpm < 999.0) ? (float)tr.bpm : 120.0f;
        beatsPerSample_ = (double)bpm / 60.0 / (double)sr;
        const bool playing = tr.valid && tr.playing;
        bool resync = false;
        if (playing && tr.ppqValid)
        {
            if (!wasPlaying_ || std::fabs(tr.ppqPos - beat_) > 0.02) resync = true;   // started, looped or jumped
            beat_ = tr.ppqPos;
        }
        if (playing && !wasPlaying_)
        {
            regenerate();                 // every playback starts from the line the seed gives
            latchNote_ = 60;
        }
        wasPlaying_ = playing;

        const int modeNow = writing_ ? (int)kSeqOff + 100 : mode;     // Write behaves as its own mode
        if (modeNow != lastMode_)         // changing mode: nothing left hanging
        {
            if (lastMode_ >= 0) { x0x::bass303_note_off(&eng_); numHeld_ = 0; }
            seqRunning_ = false; seqSounding_ = false; offCountdown_ = -1;
            lastMode_ = modeNow;
            if (!writing_) setPos(0);
        }
        if (resync && seqRunning_) alignToGrid(0.1);

        // ---- delay time (glides to a new division or tempo), master ----------------------------------------------
        const float delayTarget = mpc::clamp(kDelayBeats[mpc::clamp(getInt(kDelayTime), 0, 4)] * 60.0f / bpm * sr,
                                             16.0f, (float)delayL_.size() - 8.0f);
        if (delayLen_ <= 0.0f) delayLen_ = delayTarget;
        const float delayMix = get(kDelayMix) * 0.01f, delayFb = get(kDelayFdbk) * 0.01f;
        const float gainTarget = dbToGain(get(kMaster));
        const float lpC = 1.0f - std::exp(-6.2832f * 4500.0f / sr), hpC = 1.0f - std::exp(-6.2832f * 160.0f / sr);
        const float lowCut = get(kLowCut);
        lowCutC_ = lowCut > 20.5f ? std::exp(-6.2832f * lowCut / sr) : 0.0f;
        lfoSynced_ = getInt(kLfoSync) == 1;
        lfoDivBeats_ = (double)kLfoDivBeats[mpc::clamp(getInt(kLfoDiv), 0, 9)];
        lfoStep_ = get(kLfoRate) / sr;
        const size_t mask = delayL_.size() - 1;

        // ---- render, cut at MIDI events and sequencer steps ------------------------------------------------------
        int pos = 0, mi = 0;
        float mono[kChunk];
        while (pos < numFrames)
        {
            while (mi < ctx.numMidiEvents && ctx.midi[mi].sampleOffset <= pos) handleMidi(ctx.midi[mi++], writing_ ? (int)kSeqOff : mode);
            int until = runSequencer(writing_ ? (int)kSeqOff : mode, playing);      // frames until the sequencer needs us again
            int n = numFrames - pos;
            if (n > chunk_) n = chunk_;
            if (until < n) n = until;
            if (mi < ctx.numMidiEvents && ctx.midi[mi].sampleOffset - pos < n) n = ctx.midi[mi].sampleOffset - pos;
            if (n < 1) n = 1;

            smoothPots(n, sr);
            modulate(n, sr);
            x0x::bass303_render(&eng_, mono, n);
            if (lowCutC_ > 0.0f)                      // Low Cut: a one-pole highpass after the synth
                for (int i = 0; i < n; ++i)
                {
                    const float x = mono[i];
                    lowCutY_ = lowCutC_ * (lowCutY_ + x - lowCutX_);
                    lowCutX_ = x;
                    mono[i] = lowCutY_;
                }
            if (std::fabs(lowCutY_) < 1e-18f) lowCutY_ = 0.0f;      // (its tail: stop before the numbers get tiny)

            for (int i = 0; i < n; ++i)
            {
                delayLen_ += (delayTarget - delayLen_) * 0.0004f;
                const float rd = (float)delayPos_ - delayLen_;
                const int   ri = (int)std::floor(rd);
                const float fr = rd - (float)ri;
                const size_t i0 = (size_t)ri & mask, i1 = (size_t)(ri + 1) & mask;
                const float dl = delayL_[i0] + fr * (delayL_[i1] - delayL_[i0]);
                const float dr = delayR_[i0] + fr * (delayR_[i1] - delayR_[i0]);
                // feedback: left -> right -> left (ping-pong), band-limited so repeats get thinner and darker
                fbLpL_ += lpC * (dr - fbLpL_); fbHpL_ += hpC * (fbLpL_ - fbHpL_);
                fbLpR_ += lpC * (dl - fbLpR_); fbHpR_ += hpC * (fbLpR_ - fbHpR_);
                const float x = mono[i];
                delayL_[delayPos_] = x + delayFb * (fbLpL_ - fbHpL_);
                delayR_[delayPos_] = delayFb * (fbLpR_ - fbHpR_);
                delayPos_ = (delayPos_ + 1) & mask;
                gain_ += (gainTarget - gain_) * 0.002f;
                outL[pos + i] = (x + delayMix * dl) * gain_;
                if (outR) outR[pos + i] = (x + delayMix * dr) * gain_;
            }

            pos += n;
            beat_ += beatsPerSample_ * (double)n;
            if (offCountdown_ > 0) offCountdown_ -= n;
        }
        while (mi < ctx.numMidiEvents) handleMidi(ctx.midi[mi++], writing_ ? (int)kSeqOff : mode);

        // Tell the MPC which parameters the plugin moved itself (a pattern shown, the running light, Write going
        // off), so the screen follows. Nothing else in the plugin depends on this being honoured.
        if (anyDirty_)
        {
            anyDirty_ = false;
            for (int i = 0; i < kNumParams; ++i)
                if (dirty_[i]) { dirty_[i] = false; notifyHostParameterChanged(i); }
        }
    }

private:
    // ---- engine parameters -------------------------------------------------------------------------------------
    // The engine takes "pots" 0..127 as X0X's panel does; these convert our units to its scale.
    float potTarget(int which) const
    {
        switch (which)
        {
            case x0x::BASS303_CUTOFF:  return 127.0f * std::log2(get(kCutoff) / 314.0f) / 2.930586688f;
            case x0x::BASS303_RESO:    return 1.27f * get(kReso);
            case x0x::BASS303_ENVMOD:  return 1.27f * get(kEnvMod);
            case x0x::BASS303_DECAY:   return 127.0f * std::log2(get(kDecay) / 200.0f) / 3.321928095f;
            case x0x::BASS303_ACCENT:  return 1.27f * get(kAccent);
            case x0x::BASS303_WAVE:    return (float)getInt(kWave);
            case x0x::BASS303_TUNE:    return 64.0f + (440.0f * std::pow(2.0f, get(kTune) / 1200.0f) - 440.0f) / 0.625f;
            case x0x::BASS303_DRIVE:   return 1.27f * get(kDrive);
            case x0x::BASS303_DRVTYPE: return (float)getInt(kDriveType);
            case x0x::BASS303_SLIDE:   return 127.0f * (get(kSlide) - 2.0f) / 358.0f;
            case x0x::BASS303_ACCDEC:  return 127.0f * (get(kAccDecay) - 30.0f) / 2970.0f;
            default:                   return kEngineVolume;
        }
    }

    void setPot(int which, float v)
    {
        if (std::fabs(v - potSet_[which]) < 0.002f) return;
        potSet_[which] = v;
        x0x::bass303_set(&eng_, which, v);
    }

    // Once per block: the controls that may step. Cutoff, Reso and Env Mod glide instead (smoothPots).
    void updateEnginePots(bool all)
    {
        static const int stepped[] = {x0x::BASS303_DECAY, x0x::BASS303_ACCENT, x0x::BASS303_WAVE, x0x::BASS303_TUNE,
                                      x0x::BASS303_DRIVE, x0x::BASS303_DRVTYPE, x0x::BASS303_SLIDE, x0x::BASS303_ACCDEC};
        for (int which : stepped) setPot(which, potTarget(which));
        (void)all;
    }

    void smoothPots(int n, float sr)
    {
        static const int smooth[3] = {x0x::BASS303_CUTOFF, x0x::BASS303_RESO, x0x::BASS303_ENVMOD};
        const float a = smoothInit_ ? 1.0f : (float)n / ((float)n + 0.006f * sr);    // ~6 ms
        smoothInit_ = false;
        for (int k = 0; k < 3; ++k)
        {
            const float target = potTarget(smooth[k]);
            potSmooth_[k] += (target - potSmooth_[k]) * a;
            if (std::fabs(target - potSmooth_[k]) < 0.01f) potSmooth_[k] = target;
            setPot(smooth[k], potSmooth_[k]);
        }
    }

    // ---- notes -------------------------------------------------------------------------------------------------
    void engineOn(int note, bool accent, bool slide)
    {
        x0x::bass303_note_on(&eng_, mpc::clamp(note + octaveShift_, 0, 127), accent ? 1 : 0, slide ? 1 : 0);
    }

    void handleMidi(const mpc::MidiEvent& e, int mode)
    {
        if (e.isNoteOn() && !writing_ && e.note() >= 12 && e.note() < 12 + kNumPatterns)
        {
            setParam(kPattern, (float)(e.note() - 12));       // notes 12..19 choose the pattern (from the next block)
            return;
        }
        if (e.isNoteOn() && writing_)                         // Write: the note goes into the next step, and sounds
        {
            const bool accent = e.velocity() >= accVel_;
            const uint32_t v = packMidi(e.note(), true, accent, false);
            showStep(writeCursor_, v);
            x0x::bass303_note_on(&eng_, mpc::clamp(stepMidi(v) + octaveShift_, 0, 127), accent ? 1 : 0, 0);
            writeHeld_ = e.note();
            if (++writeCursor_ >= patSteps_) { writing_ = false; lastWrite_ = false; setParam(kWrite, 0.0f); setPos(0); }
            else setPos(writeCursor_ + 1);
            return;
        }
        if (e.isNoteOff() && e.note() == writeHeld_)
        {
            writeHeld_ = -1;
            if (numHeld_ == 0) x0x::bass303_note_off(&eng_);
            return;
        }
        if (e.isNoteOn())
        {
            const int note = e.note();
            const bool accent = e.velocity() >= accVel_;
            const bool slide = numHeld_ > 0;                  // another key is still down: slide, no retrigger
            for (int i = 0; i < numHeld_; ++i)                // (a key cannot be held twice)
                if (held_[i].note == note) { removeHeld(i); break; }
            if (numHeld_ == kMaxHeld) removeHeld(0);
            held_[numHeld_].note = (uint8_t)note; held_[numHeld_].accent = accent; ++numHeld_;
            latchNote_ = note;
            if (mode == kSeqOff) engineOn(note, accent, slide);
        }
        else if (e.isNoteOff())
        {
            const int note = e.note();
            for (int i = 0; i < numHeld_; ++i)
            {
                if (held_[i].note != note) continue;
                const bool wasTop = (i == numHeld_ - 1);
                removeHeld(i);
                if (mode == kSeqOff && wasTop)
                {
                    if (numHeld_ > 0) engineOn(held_[numHeld_ - 1].note, held_[numHeld_ - 1].accent, true);   // slide back
                    else x0x::bass303_note_off(&eng_);
                }
                break;
            }
        }
        else if (e.isAllNotesOff())
        {
            numHeld_ = 0;
            if (mode == kSeqOff) x0x::bass303_note_off(&eng_);
        }
    }

    void removeHeld(int i)
    {
        for (int k = i; k + 1 < numHeld_; ++k) held_[k] = held_[k + 1];
        --numHeld_;
    }

    // ---- the line generator (TB-3PO) ----------------------------------------------------------------------------
    // The line depends only on these settings: when one changes it is written again from the seed.
    void updatePattern()
    {
        uint64_t key = 1469598103934665603ull;
        const int vals[] = {getInt(kSeed), seqLen_, (int)std::lround(get(kDensity)), (int)std::lround(get(kSeqAccent)),
                            (int)std::lround(get(kSeqSlide)), getInt(kOctRange), getInt(kRoot), getInt(kScale), getInt(kBaseOct)};
        for (int v : vals) key = (key ^ (uint64_t)(uint32_t)v) * 1099511628211ull;
        if (key != patternKey_) { patternKey_ = key; regenerate(); }
        mutateBars_ = kMutateBars[mpc::clamp(getInt(kMutate), 0, 4)];
    }

    void regenerate()
    {
        x0x::tb3po_cfg_t& g = part_.gen;
        g.density   = (uint8_t)mpc::clamp((int)std::lround(get(kDensity)), 0, 100);
        g.accent    = (uint8_t)mpc::clamp((int)std::lround(get(kSeqAccent)), 0, 100);
        g.slide     = (uint8_t)mpc::clamp((int)std::lround(get(kSeqSlide)), 0, 100);
        g.oct_range = (uint8_t)mpc::clamp(getInt(kOctRange), 1, 3);
        g.root      = (uint8_t)mpc::clamp(getInt(kRoot), 0, 11);
        g.scale     = (uint8_t)mpc::clamp(getInt(kScale), 0, TB3PO_NSCALES - 1);
        g.base_oct  = (uint8_t)mpc::clamp(getInt(kBaseOct), 0, 3);
        g.mutate_bars = 0;
        g.seed      = x0x::tb3po_new_seed((uint32_t)getInt(kSeed));
        part_.len   = (uint8_t)mpc::clamp(getInt(kLength), 1, NSTEPS);
        x0x::tb3po_generate(&part_);
        mutateRng_ = 0;
        lastMutBar_ = INT64_MIN;
    }

    // When step k plays, in beats: the grid, odd steps late by the swing.
    double stepTime(int64_t k) const { return (double)k * stepBeats_ + ((k & 1) ? swingBeats_ : 0.0); }

    // Find the next step to play from where the clock is. A step that began less than "catchUp" of a step ago
    // is played now rather than skipped: a pad hit slightly late (Keys), or a loop that the host reports a few
    // samples after its first beat.
    void alignToGrid(double catchUp)
    {
        int64_t k = (int64_t)std::floor(beat_ / stepBeats_) - 1;
        while (stepTime(k) < beat_ - 1e-7) ++k;
        if (beat_ - stepTime(k - 1) < catchUp * stepBeats_) --k;
        nextStep_ = k;
    }

    void seqRelease()
    {
        if (seqSounding_) x0x::bass303_note_off(&eng_);
        seqSounding_ = false; seqPrevSlide_ = false; offCountdown_ = -1;
    }

    // Plays what is due now; returns the frames until the next step or gate end (at least 1).
    int runSequencer(int mode, bool playing)
    {
        const bool run = (mode == kSeqKeys && numHeld_ > 0) || (mode == kSeqSong && playing);
        if (run != seqRunning_)
        {
            seqRunning_ = run;
            if (run)
            {
                if (!playing) { beat_ = 0.0; nextStep_ = 0; }     // free-running: the line starts with the key
                else alignToGrid(mode == kSeqKeys ? 0.35 : 0.1);
                lastMutBar_ = INT64_MIN;
            }
            else { seqRelease(); setPos(0); }
        }
        if (!run) { if (!writing_) setPos(0); return kChunk; }

        if (offCountdown_ == 0 || (offCountdown_ < 0 && offCountdown_ != -1)) { seqRelease(); }

        int guard = 0;
        while (stepTime(nextStep_) <= beat_ + 1e-9 && guard++ < 4) fireStep(nextStep_++);

        double frames = std::ceil((stepTime(nextStep_) - beat_) / beatsPerSample_);
        int until = frames > (double)kChunk ? kChunk : (frames < 1.0 ? 1 : (int)frames);
        if (offCountdown_ > 0 && offCountdown_ < until) until = offCountdown_;
        return until;
    }

    void fireStep(int64_t k)
    {
        const int len = seqLen_;
        const int p = (int)(((k % len) + len) % len);

        // Mutate: at the first step of every Nth bar (bars of four beats), re-roll about a quarter of the steps.
        const int64_t bar = (int64_t)std::floor(((double)k * stepBeats_ + 1e-6) / 4.0);
        if (bar != lastMutBar_)
        {
            if (lastMutBar_ != INT64_MIN && mutateBars_ > 0 && source_ == 0 && bar % mutateBars_ == 0) x0x::tb3po_mutate(&part_, &mutateRng_);
            lastMutBar_ = bar;
        }

        int stepNote; bool accent, slideNext;
        if (source_ == 1)                                         // the hand-written pattern: the step parameters
        {
            const uint32_t v = readStep(p);
            setPos(p + 1);
            if (!stepGate(v)) { seqRelease(); return; }
            stepNote = stepMidi(v); accent = stepAcc(v); slideNext = stepSlide(v);
        }
        else                                                      // the generator's line
        {
            const x0x::bstep_t& st = part_.step[p];
            if (x0x::bstep_gate(&st) == x0x::G_REST) { seqRelease(); return; }
            stepNote = (int)st.note; accent = (st.flags & BS_ACCENT) != 0; slideNext = (st.flags & BS_SLIDE) != 0;
        }

        const int base = (getInt(kSeqMode) == kSeqKeys && numHeld_ > 0) ? held_[numHeld_ - 1].note : latchNote_;
        const bool slide = seqSounding_ && seqPrevSlide_;         // the step before slides into this one
        engineOn(stepNote + (base - 60), accent, slide);
        seqSounding_ = true;
        seqPrevSlide_ = slideNext;
        if (seqPrevSlide_) offCountdown_ = -1;                    // held until the next step
        else
        {
            offCountdown_ = (int)((double)gateFrac_ * stepBeats_ / beatsPerSample_);
            if (offCountdown_ < 1) offCountdown_ = 1;
        }
    }

    // ---- beyond the 303 (1.2.0) --------------------------------------------------------------------------------
    // Once per block: what does not move within a block. The engine is only told when a value changed.
    void updateDeep()
    {
        const float osc[4] = {get(kBlend) * 0.01f, get(kSub) * 0.01f, (float)(getInt(kSubOct) + 1), get(kNoise) * 0.006f};
        if (std::memcmp(osc, deepSet_, sizeof osc) != 0)
        {
            std::memcpy(deepSet_, osc, sizeof osc);
            x0x::bass303_ext_osc(&eng_, osc[0], osc[1], (int)osc[2], osc[3]);
        }
        const float env[4] = {get(kEnvAttack), get(kAmpAttack) < 0.5f ? 0.0f : get(kAmpAttack),
                              std::fabs(get(kAmpDecay) - kStockAmpDecay) < 1.0f ? kStockAmpDecay : get(kAmpDecay),
                              get(kRelease) < 0.5f ? 0.0f : get(kRelease)};
        if (std::memcmp(env, deepSet_ + 4, sizeof env) != 0)
        {
            std::memcpy(deepSet_ + 4, env, sizeof env);
            x0x::bass303_ext_env(&eng_, env[0] <= 3.05f ? 3.0f : env[0], env[1], env[2], env[3]);
        }
        cutShift_ = get(kCutShift);
        keyTrack_ = get(kKeyTrack) * 0.01f; filtFM_ = get(kFiltFM) * 0.01f; accSweep_ = get(kAccSweep) * 0.01f;
        lfoCut_ = get(kLfoCut) * 0.03f;                       // octaves
        lfoPitch_ = get(kLfoPitch) / 12.0f;                   // octaves
        lfoPW_ = get(kLfoPW) * 0.0045f;                       // duty
        dutyBase_ = std::fabs(get(kPulseW) - kStockDuty) < 0.05f ? 0.0f : get(kPulseW) * 0.01f;   // 0: the 303's own
        lfoShape_ = getInt(kLfoShape);
        const bool lfoOn = lfoCut_ > 0.0f || lfoPitch_ > 0.0f || lfoPW_ > 0.0f;
        chunk_ = lfoOn ? 16 : kChunk;                         // a moving LFO: finer steps
    }

    // The LFO, -1..1, at the clock's position (Sync) or its own phase (Free).
    float lfoValue()
    {
        double ph; int64_t cycle;
        if (lfoSynced_) { const double c = beat_ / lfoDivBeats_; cycle = (int64_t)std::floor(c); ph = c - (double)cycle; }
        else            { ph = (double)lfoPhase_; cycle = lfoCycle_; }
        const float p = (float)ph;
        switch (lfoShape_)
        {
            case 1:  return p < 0.5f ? 4.0f * p - 1.0f : 3.0f - 4.0f * p;         // triangle
            case 2:  return 1.0f - 2.0f * p;                                       // saw, falling
            case 3:  return p < 0.5f ? 1.0f : -1.0f;                               // square
            case 4:                                                                // sample and hold: one value a cycle
            {
                uint32_t h = (uint32_t)cycle * 2654435761u + 0x9e3779b9u;
                h ^= h >> 15; h *= 2246822519u; h ^= h >> 13;
                return (float)(h & 0xffff) * (2.0f / 65535.0f) - 1.0f;
            }
            default: return std::sin(6.2831853f * p);
        }
    }

    // Before each piece of audio: cutoff shift and the LFO's three destinations.
    void modulate(int n, float sr)
    {
        (void)sr;
        const bool lfoOn = lfoCut_ > 0.0f || lfoPitch_ > 0.0f || lfoPW_ > 0.0f;
        const float v = lfoOn ? lfoValue() : 0.0f;
        const float filt[4] = {cutShift_ + v * lfoCut_, keyTrack_, filtFM_, accSweep_};
        if (std::memcmp(filt, deepSet_ + 8, sizeof filt) != 0)
        {
            std::memcpy(deepSet_ + 8, filt, sizeof filt);
            x0x::bass303_ext_filter(&eng_, filt[0] == 0.0f ? 1.0f : std::exp2(filt[0]), filt[1], filt[2], filt[3]);
        }
        const float pitch = v * lfoPitch_;
        if (pitch != deepSet_[12]) { deepSet_[12] = pitch; x0x::bass303_ext_pitch(&eng_, pitch == 0.0f ? 1.0f : std::exp2(pitch)); }
        float duty = dutyBase_;
        if (lfoPW_ > 0.0f) duty = mpc::clamp((dutyBase_ > 0.0f ? dutyBase_ : kStockDuty * 0.01f) + v * lfoPW_, 0.05f, 0.95f);
        if (duty != deepSet_[13]) { deepSet_[13] = duty; x0x::bass303_ext_duty(&eng_, duty); }
        lfoPhase_ += lfoStep_ * (float)n;
        if (lfoPhase_ >= 1.0f) { lfoPhase_ -= std::floor(lfoPhase_); ++lfoCycle_; }
    }

    // ---- the step sequencer (1.1.0) ----------------------------------------------------------------------------
    static mpc::Param notAutomated(mpc::Param p) { p.automatable = false; return p; }

    // A parameter the plugin moves itself: store it, and remember to tell the host at the end of the block.
    void setParam(int i, float plain)
    {
        if (std::fabs(get(i) - plain) < 1e-4f) return;
        set(i, plain);
        dirty_[i] = true; anyDirty_ = true;
    }

    void setPos(int v) { if (v != pos_) { pos_ = v; setParam(kPos, (float)v); } }

    uint32_t readStep(int st) const
    {
        return packStep(mpc::clamp(getInt(kStepNote + st), 0, 12), mpc::clamp(getInt(kStepOct + st), 0, 2),
                        getBool(kStepGate + st), getBool(kStepAcc + st), getBool(kStepSlide + st));
    }

    void showStep(int st, uint32_t v)
    {
        setParam(kStepNote + st, (float)(v & 15));
        setParam(kStepOct + st, (float)((v >> 4) & 3));
        setParam(kStepGate + st, stepGate(v) ? 1.0f : 0.0f);
        setParam(kStepAcc + st, stepAcc(v) ? 1.0f : 0.0f);
        setParam(kStepSlide + st, stepSlide(v) ? 1.0f : 0.0f);
    }

    void showPattern(int pt)                    // a stored pattern into the step parameters
    {
        for (int st = 0; st < kPatSteps; ++st) showStep(st, slots_[pt][st].load(std::memory_order_relaxed));
        setParam(kSteps, (float)(mpc::clamp(slotLen_[pt].load(std::memory_order_relaxed), 1, kPatSteps) - 1));
    }

    // Once per block: the pattern chosen, the step parameters kept in their slot, the three buttons.
    void updateStepSequencer()
    {
        source_ = getInt(kSource);
        const int want = mpc::clamp(getInt(kPattern), 0, kNumPatterns - 1);
        if (want != curPattern_) { curPattern_ = want; showPattern(want); }
        patSteps_ = mpc::clamp(getInt(kSteps) + 1, 1, kPatSteps);

        const bool copy = getBool(kCopyDice), clear = getBool(kClear), write = getBool(kWrite);
        if (copy && !lastCopy_)                 // the generator's line, as far as 16 steps hold it
        {
            const int len = mpc::clamp((int)part_.len, 1, kPatSteps);
            for (int st = 0; st < kPatSteps; ++st)
            {
                const x0x::bstep_t& b = part_.step[st % len];
                showStep(st, packMidi(b.note, x0x::bstep_gate(&b) != x0x::G_REST, (b.flags & BS_ACCENT) != 0, (b.flags & BS_SLIDE) != 0));
            }
            setParam(kSteps, (float)(len - 1));
            patSteps_ = len;
        }
        if (clear && !lastClear_)
            for (int st = 0; st < kPatSteps; ++st) showStep(st, packStep(0, 1, false, false, false));
        lastCopy_ = copy; lastClear_ = clear;

        if (write != lastWrite_)                // Write on: start at step 1, the sequencer waits
        {
            lastWrite_ = write; writing_ = write; writeCursor_ = 0; writeHeld_ = -1;
            setPos(write ? 1 : 0);
        }

        for (int st = 0; st < kPatSteps; ++st) slots_[curPattern_][st].store(readStep(st), std::memory_order_relaxed);
        slotLen_[curPattern_].store(patSteps_, std::memory_order_relaxed);
    }

    void makeFactoryPatterns()
    {
        // pattern 1: note (0 = rest), accent, slide into the next step
        static const int line[kPatSteps][3] = {
            {33, 1, 0}, {33, 0, 0}, {45, 0, 1}, {33, 0, 0}, {0, 0, 0}, {36, 1, 0}, {33, 0, 1}, {43, 0, 0},
            {33, 0, 0}, {0, 0, 0}, {45, 1, 1}, {40, 0, 0}, {33, 0, 0}, {31, 0, 1}, {33, 1, 0}, {38, 0, 0}};
        for (int st = 0; st < kPatSteps; ++st)
            factory_[0][st] = line[st][0] ? packMidi(line[st][0], true, line[st][1] != 0, line[st][2] != 0)
                                          : packStep(0, 1, false, false, false);
        // patterns 2..8: generated lines in A minor, one seed each, to have something to edit
        for (int pt = 1; pt < kNumPatterns; ++pt)
        {
            x0x::bpart_t part = {};
            part.gen.density = 70; part.gen.accent = 40; part.gen.slide = 25; part.gen.oct_range = 2;
            part.gen.root = 9; part.gen.scale = 0; part.gen.base_oct = 1; part.gen.mutate_bars = 0;
            part.gen.seed = x0x::tb3po_new_seed((uint32_t)(100 + pt));
            part.len = kPatSteps;
            x0x::tb3po_generate(&part);
            for (int st = 0; st < kPatSteps; ++st)
            {
                const x0x::bstep_t& b = part.step[st];
                factory_[pt][st] = packMidi(b.note, x0x::bstep_gate(&b) != x0x::G_REST, (b.flags & BS_ACCENT) != 0, (b.flags & BS_SLIDE) != 0);
            }
        }
    }

    // ---- programs ----------------------------------------------------------------------------------------------
    void program(const char* name, std::vector<std::pair<int, float>> changes)
    {
        std::vector<float> v((size_t)numParams());
        bool setsSource = false;
        for (int i = 0; i < numParams(); ++i) v[(size_t)i] = param(i).def;
        for (auto& c : changes) { v[(size_t)c.first] = c.second; setsSource |= c.first == kSource; }
        programSetsSource_.push_back(setsSource);
        addProgram(name, v);
    }

    static mpc::PluginInfo makeInfo()
    {
        mpc::PluginInfo info;
        info.name         = "Acid Rat";
        info.vendor       = "Gonzi Audio";   // must match plugin.json "manufacturer"
        info.uniqueId     = MPC_FOURCC(kUid[0], kUid[1], kUid[2], kUid[3]);   // must match plugin.json "uid"
        info.version      = 1200;           // 1.2.0.0: beyond the 303 (parameters 118..138 appended)
        info.isInstrument = true;
        info.numInputs    = 0;
        info.numOutputs   = 2;
        info.tailSamples  = 0;
        return info;
    }

    static constexpr char kUid[5] = "G37u";

    struct Held { uint8_t note; bool accent; };

    float sampleRate_ = 44100.0f;
    x0x::bass303_t eng_;
    float potSet_[x0x::BASS303_NPARAMS];        // what the engine was last given
    float potSmooth_[3] = {0, 0, 0};
    bool  smoothInit_ = true;

    Held held_[kMaxHeld];
    int  numHeld_ = 0, latchNote_ = 60, accVel_ = 100, octaveShift_ = 0;

    x0x::bpart_t part_ = {};
    uint64_t patternKey_ = 0;
    uint32_t mutateRng_ = 0;
    int      mutateBars_ = 0, seqLen_ = 16, lastMode_ = -1;
    int64_t  nextStep_ = 0, lastMutBar_ = INT64_MIN;
    double   beat_ = 0.0, beatsPerSample_ = 0.0, stepBeats_ = 0.25, swingBeats_ = 0.0;
    float    gateFrac_ = 0.5f;
    int      offCountdown_ = -1;                // frames until the gate falls; -1 = none pending
    bool     seqRunning_ = false, seqSounding_ = false, seqPrevSlide_ = false, wasPlaying_ = false;

    // the step sequencer
    uint32_t factory_[kNumPatterns][kPatSteps];
    std::atomic<uint32_t> slots_[kNumPatterns][kPatSteps];     // the eight patterns (the audio thread writes, save reads)
    std::atomic<int>      slotLen_[kNumPatterns];
    uint32_t staged_[kNumPatterns][kPatSteps];                 // a loaded state, until reset() takes it
    int      stagedLen_[kNumPatterns];
    std::atomic<bool> stagedReady_{false};
    int  source_ = 0, curPattern_ = 0, patSteps_ = kPatSteps, pos_ = -1, writeCursor_ = 0, writeHeld_ = -1;
    bool writing_ = false, lastWrite_ = false, lastCopy_ = false, lastClear_ = false;
    bool dirty_[kNumParams] = {}, anyDirty_ = false;

    // beyond the 303
    float deepSet_[14];                          // what the engine was last given (osc 4, env 4, filter 4, pitch, duty)
    float cutShift_ = 0, keyTrack_ = 0, filtFM_ = 0, accSweep_ = 1, lfoCut_ = 0, lfoPitch_ = 0, lfoPW_ = 0, dutyBase_ = 0;
    float lowCutC_ = 0, lowCutX_ = 0, lowCutY_ = 0, lfoPhase_ = 0, lfoStep_ = 0, lfoHold_ = 0;
    double lfoDivBeats_ = 4.0;
    int64_t lfoCycle_ = 0;
    int  lfoShape_ = 0, chunk_ = kChunk, seenProgram_ = 0;
    std::vector<bool> programSetsSource_;
    bool lfoSynced_ = true;

    std::vector<float> delayL_, delayR_;
    size_t delayPos_ = 0;
    float  delayLen_ = 0.0f, fbLpL_ = 0, fbLpR_ = 0, fbHpL_ = 0, fbHpR_ = 0, gain_ = 1.0f;
};

// This macro creates the exported VSTPluginMain entry point. Keep it last.
MPC_PLUGIN_ENTRY(AcidRat)
