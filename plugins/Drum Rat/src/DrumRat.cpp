// SPDX-License-Identifier: GPL-3.0-only
// Drum Rat - an 808 and a 909 in one drum machine for the MPC. Copyright (C) 2026 Gonzi Audio.
//
// Both sound engines come from X0X, Charles Vestal's firmware for the M-VAVE FM-1
// (github.com/charlesvestal/fm1-x0x at 512c0da, GPL-3.0-only):
//   * drum808: a port of 8W8 (athousanddetails / Gustavo Lima), circuit models after the TR-808 service notes and
//     Werner / Abel / Smith; every sound is synthesised.
//   * drum909: a port of 9W9 (athousanddetails), itself after ER-99 (Matthew Cieplak): circuit-modelled kick, snare,
//     toms, rim and clap; hats, crash and ride are samples (as on the real machine).
// Those sources are in src/x0x/, byte for byte (see LICENSING.md); this file is the MPC plugin around them.
//
// Signal flow:
//
//   MPC pads / track (MIDI) ──┐                 ┌─> 808 engine (11 tracks) ─┐
//                             ├─> 11 tracks ───>│   chosen per track by      ├─> stereo sum ─> Master ─> soft limit ─> L / R
//   16-step sequencer ────────┘   BD SD LT MT   └─> 909 engine (11 tracks) ─┘
//   (8 patterns, accent,          HT RS CP CH       Kit = 808 / 909 / Mix (Mix: each track's own switch)
//    follows the MPC tempo)       OH CY PC
//
// The eleven tracks:  BD kick, SD snare, LT / MT / HT toms (808: or congas), RS rim shot (808: or claves),
// CP clap (808: or maracas), CH closed hat, OH open hat, CY cymbal (909: crash), PC cowbell on the 808 / ride on the 909.
//
// Each track has Level, Tune and Decay. Tune and Decay are offsets: 0 is the machine's own factory setting, so
// switching a track from 808 to 909 always lands on that machine's normal sound.
//
// The sequencer: 8 patterns of 11 tracks x 16 steps; a step is Off, On or Accent. The sixteen step parameters show
// ONE track of the current pattern (parameter "Track"), as the step keys of the real machines do. The patterns are
// kept in the saved state. Notes 12..19 choose pattern 1..8.
//
// The engines run at 44.1 kHz (their circuit constants are fixed for it). At another host rate their output is
// resampled (linear interpolation).
//
// Realtime: nothing in process() allocates, locks or does I/O.

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

// ---- the X0X drum engines, compiled into this file (C sources that are also valid C++) ---------------------------
#define _Static_assert static_assert
#define X0X_TLS thread_local      // the engines' three "render in progress" variables: one set per thread
namespace x0x {
#if defined(__clang__)
#pragma clang diagnostic push
#pragma clang diagnostic ignored "-Wunused-function"
#pragma clang diagnostic ignored "-Wc99-designator"
#pragma clang diagnostic ignored "-Winitializer-overrides"
#pragma clang diagnostic ignored "-Wreorder-init-list"
#pragma clang fp contract(off)
#elif defined(__GNUC__)
#pragma GCC diagnostic push
#pragma GCC diagnostic ignored "-Wunused-function"
#endif
#include "x0x/drum808.c"
#undef SR
#undef PP
#undef TRK
#include "x0x/drum909.c"
#if defined(__clang__)
#pragma clang diagnostic pop
#elif defined(__GNUC__)
#pragma GCC diagnostic pop
#endif
} // namespace x0x
#undef LIN
#undef EXP
#undef DRIVE
#undef DIST
#undef SENDS
#undef LEVEL
#undef PAN
#undef NP
#undef FITS

namespace {

constexpr int kTracks = 11;
enum Track { tBD = 0, tSD, tLT, tMT, tHT, tRS, tCP, tCH, tOH, tCY, tPC };
const char* const kTrackId[kTracks]   = {"bd", "sd", "lt", "mt", "ht", "rs", "cp", "ch", "oh", "cy", "pc"};
const char* const kTrackName[kTracks] = {"BD", "SD", "LT", "MT", "HT", "RS", "CP", "CH", "OH", "CY", "PC"};
// our track -> each engine's own track number
const int kTo808[kTracks] = {x0x::D8_BD, x0x::D8_SD, x0x::D8_LT, x0x::D8_MT, x0x::D8_HT, x0x::D8_RS, x0x::D8_CP,
                             x0x::D8_CH, x0x::D8_OH, x0x::D8_CY, x0x::D8_CB};
const int kTo909[kTracks] = {x0x::DR_BD, x0x::DR_SD, x0x::DR_LT, x0x::DR_MT, x0x::DR_HT, x0x::DR_RS, x0x::DR_CP,
                             x0x::DR_CH, x0x::DR_OH, x0x::DR_CR, x0x::DR_RD};
// where each track sits in the stereo picture at Width 100 % (-1 left .. +1 right)
const float kPanLayout[kTracks] = {0.0f, 0.0f, -0.45f, 0.0f, 0.45f, -0.25f, 0.15f, 0.30f, 0.30f, -0.40f, 0.50f};

// The order here is the parameter order the MPC saves: permanent. Only ever append.
enum ParamIndex
{
    kMaster = 0,     // dB, output level (parameter 0: harmless when the host nudges it)
    kKit,            // 808 / 909 / Mix (Mix: every track uses its own "Mach" switch)
    kAccent,         // %, how much quieter a normal step is than an accented one (100 = the machines' own difference)
    kDrive,          // %, each voice's drive stage, added to the machine's own setting
    kDriveType,      // Diode / Clip / SAT / BFZ / PDIST / Fold / Crush
    kWidth,          // %, stereo spread of the tracks
    kCongas,         // 808: the three toms become congas
    kClaves,         // 808: the rim shot becomes claves
    kMaracas,        // 808: the clap becomes maracas
    kTrackParams,    // 11 x (Level, Tune, Decay)
    kBdTone = kTrackParams + 3 * kTracks,   // 808: the kick's Tone; 909: its pitch sweep
    kBdAttack,       // the kick's click
    kSdTone,         // the snare's Tone
    kSdSnappy,       // the snare's noise
    kMach,           // 11 x 808 / 909, used when Kit = Mix
    kNumSound = kMach + kTracks,            // 57: a program sets parameters 0..56 and leaves the rest alone
    kPadMap = kNumSound,   // which notes play which track: MPC (every octave holds the kit) / GM
    kSeq,            // Off / Song (plays while the MPC plays) / Run (plays all the time, at the MPC's tempo)
    kPattern,        // 1..8
    kSteps,          // 1..16: the pattern's length (kept with the pattern)
    kRate,           // 1/8, 1/16, 1/32: step length
    kSwing,          // %, odd steps late
    kTrack,          // which track's steps are shown in S1..S16
    kPadSel,         // On: playing a pad shows that track's steps (while Seq is on)
    kClearTrk,       // momentary: empty the track shown
    kClearPat,       // momentary: empty the whole pattern
    kStep,           // 16 x Off / On / Accent: the track shown
    kNumParams = kStep + 16    // 83
};
inline int pLevel(int t) { return kTrackParams + 3 * t; }
inline int pTune(int t)  { return kTrackParams + 3 * t + 1; }
inline int pDecay(int t) { return kTrackParams + 3 * t + 2; }

enum { kKit808 = 0, kKit909, kKitMix };
enum { kSeqOff = 0, kSeqSong, kSeqRun };
constexpr int   kNumPatterns = 8, kPatSteps = 16;
constexpr float kStepBeats[3] = {0.5f, 0.25f, 0.125f};
constexpr int   kChunk = 64;                  // host frames rendered at once at most
constexpr float kEngineRate = 44100.0f;

// What a step that is "On" (not accented) is to each engine, as X0X plays them: the 909 at velocity 88, the 808 at
// its 4 V trigger. An accented step is 1.0 on both.
constexpr float kNormal808 = 0.502f, kNormal909 = 88.0f / 127.0f;
// Output trims that put the two kits at a comparable loudness (set by measurement, see README).
constexpr float kTrim808 = 0.80f, kTrim909 = 0.18f;
// The 808's cowbell is 6 dB hotter than anything else in the kit when accented: its factory level is halved here.
const float kLevel808[kTracks] = {1.0f, 1.0f, 1.0f, 1.0f, 1.0f, 1.0f, 1.0f, 1.0f, 1.0f, 1.0f, 0.5f};

inline float dbToGain(float db) { return db <= -59.9f ? 0.0f : std::pow(10.0f, db * 0.05f); }

// An offset knob (-100..+100, 0 = the machine's factory pot) onto the engine's 0..127 pot.
inline int offsetPot(int def, float off)
{
    const float v = off >= 0.0f ? (float)def + (127.0f - (float)def) * off * 0.01f : (float)def + (float)def * off * 0.01f;
    return mpc::clamp((int)std::lround(v), 0, 127);
}
// A level fader (0..100 %, 80 = the machine's factory level).
inline int levelPot(int def, float lvl)
{
    const float v = lvl <= 80.0f ? (float)def * lvl / 80.0f : (float)def + (127.0f - (float)def) * (lvl - 80.0f) / 20.0f;
    return mpc::clamp((int)std::lround(v), 0, 127);
}

// ---- note maps -------------------------------------------------------------------------------------------------
// MPC (default): made for the MPC's own pad setup on a plugin track (Pad Perform: Notes, C major - white keys only -
// or Chromatic), whatever the bank or octave. Every C is the kick; the other white keys alternate between two sets
// from one octave to the next, so 16 pads in a row hold the whole kit.
//   even octaves (C2, C4, ..):  C kick  D snare  E closed hat  F open hat        G low tom  A mid tom     B high tom
//   odd octaves  (C1, C3, ..):  C kick  D clap   E rim         F cowbell / ride  G cymbal   A closed hat  B open hat
//   black keys: C# rim, D# clap, F# cowbell / ride, G# cymbal, A# cowbell / ride
inline int trackForScale(int note)
{
    static const int a[12] = {tBD, tRS, tSD, tCP, tCH, tOH, tPC, tLT, tCY, tMT, tPC, tHT};
    static const int b[12] = {tBD, tRS, tCP, tCP, tRS, tPC, tPC, tCY, tCY, tCH, tPC, tOH};
    if (note < 0) note = 0;
    return ((note / 12) & 1) ? b[note % 12] : a[note % 12];
}
// GM: General MIDI drum notes (for MIDI files and the MPC's "Classic" pad layout).
inline int trackForGM(int n)
{
    while (n < 35) n += 12;
    while (n > 59) n -= 12;
    switch (n)
    {
        case 35: case 36: return tBD;
        case 37: return tRS;
        case 38: case 40: return tSD;
        case 39: return tCP;
        case 41: case 43: return tLT;
        case 45: case 47: return tMT;
        case 48: case 50: return tHT;
        case 42: case 44: return tCH;
        case 46: return tOH;
        case 49: case 52: case 55: case 57: return tCY;
        default: return tPC;       // 51 53 59 ride, 56 cowbell, 54 58
    }
}

// ---- factory patterns: one string per track, '.' off, 'x' on, 'X' accent -----------------------------------------
struct FactoryPattern { const char* name; const char* row[kTracks]; };
//                                    BD                  SD                  LT                  MT                  HT                  RS                  CP                  CH                  OH                  CY                  PC
const FactoryPattern kFactory[kNumPatterns] = {
    {"House",    {"X...X...X...X...", "................", "................", "................", "................", "................", "....X.......X...", "xx.xxx.xxx.xxx.x", "..X...X...X...X.", "................", "................"}},
    {"Electro",  {"X.....X...x.X...", "....X.......X...", "................", "................", "..............x.", "................", "....x.......x...", "x.xxx.xxx.xxx.xx", "................", "................", "..x...x...x.x..."}},
    {"Techno",   {"X...X...X...X...", "................", "................", "................", "......x.......x.", "...x..x....x..x.", "....X.......X...", "..x...x...x...x.", "................", "................", "x.x.x.x.x.x.x.x."}},
    {"Boom Bap", {"X.......x.X.....", "....X.......X...", "................", "................", "................", "................", "................", "x.x.x.x.x.x.x.x.", "..............x.", "................", "................"}},
    {"Trap",     {"X......x..x.....", "................", "................", "................", "................", "................", "........X.......", "x.x.x.x.xxx.x.xx", "......x.........", "................", "................"}},
    {"Break",    {"X.x.......X..x..", "....X..x.x..X...", "................", "................", "................", "................", "................", "x.x.x.x.x.x.x.x.", "................", "................", "................"}},
    {"Latin",    {"X...x...X...x...", "................", "..x.....x..x....", "....x.....x.....", "x..x..x......x.x", "X..X..X...X.X...", "x.x.x.x.x.x.x.x.", "................", "................", "................", "................"}},
    {"Acid Hse", {"X...X...X...X...", "............x.xx", "................", "................", "................", "................", "....X.......X..x", "xx.xxx.xxx.xxx.x", "..X...X...X...X.", "X...............", "................"}},
};
inline uint8_t factoryStep(int pt, int t, int st)
{
    const char c = kFactory[pt].row[t][st];
    return c == 'X' ? 2 : c == 'x' ? 1 : 0;
}

} // namespace

class DrumRat : public mpc::Plugin
{
public:
    DrumRat() : mpc::Plugin(makeInfo())
    {
        findSlots();

        addParam(mpc::floatParam ("master",    "Master",     -40.0f, 6.0f, 0.0f, "dB", 1));
        addParam(mpc::choiceParam("kit",       "Kit",        {"808", "909", "Mix"}, 0));
        addParam(mpc::floatParam ("accent",    "Accent",     0.0f, 100.0f, 100.0f, "%", 0));
        addParam(mpc::floatParam ("drive",     "Drive",      0.0f, 100.0f, 0.0f, "%", 0));
        addParam(mpc::choiceParam("drivetype", "Drive Type", {"Diode", "Clip", "SAT", "BFZ", "PDIST", "Fold", "Crush"}, 0));
        addParam(mpc::floatParam ("width",     "Width",      0.0f, 100.0f, 60.0f, "%", 0));
        addParam(mpc::boolParam  ("congas",    "Congas",     false));
        addParam(mpc::boolParam  ("claves",    "Claves",     false));
        addParam(mpc::boolParam  ("maracas",   "Maracas",    false));
        for (int t = 0; t < kTracks; ++t)
        {
            const std::string id(kTrackId[t]), nm(kTrackName[t]);
            addParam(mpc::floatParam((id + "_level").c_str(), (nm + " Level").c_str(), 0.0f, 100.0f, 80.0f, "%", 0));
            addParam(mpc::floatParam((id + "_tune").c_str(),  (nm + " Tune").c_str(),  -100.0f, 100.0f, 0.0f, "", 0));
            addParam(mpc::floatParam((id + "_decay").c_str(), (nm + " Decay").c_str(), -100.0f, 100.0f, 0.0f, "", 0));
        }
        addParam(mpc::floatParam ("bd_tone",   "BD Tone",    -100.0f, 100.0f, 0.0f, "", 0));
        addParam(mpc::floatParam ("bd_attack", "BD Attack",  -100.0f, 100.0f, 0.0f, "", 0));
        addParam(mpc::floatParam ("sd_tone",   "SD Tone",    -100.0f, 100.0f, 0.0f, "", 0));
        addParam(mpc::floatParam ("sd_snappy", "SD Snappy",  -100.0f, 100.0f, 0.0f, "", 0));
        for (int t = 0; t < kTracks; ++t)
        {
            const std::string id(kTrackId[t]), nm(kTrackName[t]);
            addParam(mpc::choiceParam((id + "_mach").c_str(), (nm + " Mach").c_str(), {"808", "909"}, 0));
        }

        addParam(mpc::choiceParam("padmap",  "Pad Map", {"MPC", "GM"}, 0));
        addParam(mpc::choiceParam("seq",     "Seq",     {"Off", "Song", "Run"}, 0));
        addParam(mpc::choiceParam("pattern", "Pattern", {"1", "2", "3", "4", "5", "6", "7", "8"}, 0));
        addParam(mpc::choiceParam("steps",   "Steps",   {"1", "2", "3", "4", "5", "6", "7", "8", "9", "10", "11", "12", "13", "14", "15", "16"}, 15));
        addParam(mpc::choiceParam("rate",    "Rate",    {"1/8", "1/16", "1/32"}, 1));
        addParam(mpc::floatParam ("swing",   "Swing",   0.0f, 100.0f, 0.0f, "%", 0));
        addParam(mpc::choiceParam("track",   "Track",   {"BD Kick", "SD Snare", "LT Low Tom", "MT Mid Tom", "HT Hi Tom", "RS Rim", "CP Clap",
                                                         "CH Cl Hat", "OH Op Hat", "CY Cymbal", "PC Cow/Ride"}, 0));
        addParam(mpc::boolParam  ("padsel",  "Pad Sel", true));
        addParam(notAutomated(mpc::boolParam("cleartrk", "Clear Trk", false)));
        addParam(notAutomated(mpc::boolParam("clearpat", "Clear Pat", false)));
        for (int st = 0; st < kPatSteps; ++st)
        {
            char id[16], name[16];
            std::snprintf(id, sizeof id, "s%02d", st + 1);
            std::snprintf(name, sizeof name, "S%d", st + 1);
            addParam(mpc::choiceParam(id, name, {"Off", "On", "Acc"}, factoryStep(0, 0, st)));
        }
        if (numParams() != kNumParams) mpc::log("Drum Rat: parameter count mismatch");

        for (int pt = 0; pt < kNumPatterns; ++pt)
        {
            for (int t = 0; t < kTracks; ++t)
                for (int st = 0; st < kPatSteps; ++st) slots_[pt][t][st].store(factoryStep(pt, t, st), std::memory_order_relaxed);
            slotLen_[pt].store(kPatSteps, std::memory_order_relaxed);
        }

        // Built-in programs: the sound only (parameters 0..56). The sequencer, its patterns and the pad map are
        // left as they are.
        program("Boom 808",   {});
        program("Club 909",   {{kKit, kKit909}});
        program("Rat Mix",    {{kKit, kKitMix}, {kMach + tBD, 1}, {kMach + tCP, 1}, {kMach + tCY, 1}, {kMach + tOH, 0},
                               {pDecay(tBD), 15}, {kDrive, 15}});
        program("Long Boom",  {{pDecay(tBD), 75}, {pTune(tBD), -25}, {kBdTone, -40}, {pLevel(tBD), 85}, {pDecay(tCH), -20}});
        program("Hard 909",   {{kKit, kKit909}, {kDrive, 45}, {pDecay(tBD), -15}, {kBdAttack, 40}, {pTune(tSD), 15}, {kMaster, 2}});
        program("Latin 808",  {{kCongas, 1}, {kClaves, 1}, {kMaracas, 1}, {pDecay(tBD), -25}});
        program("Tight 909",  {{kKit, kKit909}, {pDecay(tBD), -45}, {pDecay(tSD), -35}, {pDecay(tOH), -40}, {pDecay(tCH), -30},
                               {pDecay(tCP), -30}, {kSdSnappy, 30}});
        program("Dirt Box",   {{kKit, kKitMix}, {kMach + tSD, 1}, {kMach + tCH, 1}, {kMach + tOH, 1}, {kMach + tPC, 1},
                               {kDrive, 55}, {kDriveType, 2}, {pDecay(tBD), 30}});
    }

    void prepare(double sampleRate, int maxBlockSize) override
    {
        (void)maxBlockSize;
        sampleRate_ = (float)sampleRate;
        resample_ = std::fabs(sampleRate_ - kEngineRate) > 1.0f;
        reset();
    }

    void reset() override
    {
        x0x::drum808_init(&e8_);
        x0x::drum909_init(&e9_);
        x0x::drum808_set(&e8_, x0x::D8_KIT, 0, 127);          // the kit's own level: full (as X0X runs it)
        for (auto& tr : pot8_) for (int& v : tr) v = -1;       // the engines were initialised: send every pot again
        for (auto& tr : pot9_) for (int& v : tr) v = -1;
        for (int& v : sw8_) v = -1;
        pushPots();
        x0x::drum808_pan_settle(&e8_);
        x0x::drum909_pan_settle(&e9_);
        gain_ = dbToGain(get(kMaster));
        rsFill_ = rsRead_ = 0; rsPhase_ = 1.0f; rsPrevL_ = rsPrevR_ = rsCurL_ = rsCurR_ = 0.0f;
        running_ = false; wasPlaying_ = false; beat_ = 0.0; nextStep_ = 0;

        // After a state load the saved patterns are waiting in the staging copy (see loadExtraState).
        if (stagedReady_.exchange(false, std::memory_order_acq_rel))
            for (int pt = 0; pt < kNumPatterns; ++pt)
            {
                for (int t = 0; t < kTracks; ++t)
                    for (int st = 0; st < kPatSteps; ++st) slots_[pt][t][st].store(staged_[pt][t][st], std::memory_order_relaxed);
                slotLen_[pt].store(stagedLen_[pt], std::memory_order_relaxed);
            }
        curPattern_ = mpc::clamp(getInt(kPattern), 0, kNumPatterns - 1);
        curTrack_ = mpc::clamp(getInt(kTrack), 0, kTracks - 1);
        showRow();
        setParam(kSteps, (float)(mpc::clamp((int)slotLen_[curPattern_].load(std::memory_order_relaxed), 1, kPatSteps) - 1));
        lastClearTrk_ = getBool(kClearTrk); lastClearPat_ = getBool(kClearPat);
    }

    // The eight patterns travel in the saved state next to the parameters: "DRP1", then per pattern its length and
    // 11 x 16 steps, one byte each. Both run on the UI thread; the audio thread sees them through atomics and the
    // staging copy that reset() swaps in.
    void saveExtraState(std::vector<uint8_t>& out) override
    {
        out.insert(out.end(), {'D', 'R', 'P', '1'});
        const bool pending = stagedReady_.load(std::memory_order_acquire);
        for (int pt = 0; pt < kNumPatterns; ++pt)
        {
            out.push_back((uint8_t)(pending ? stagedLen_[pt] : slotLen_[pt].load(std::memory_order_relaxed)));
            for (int t = 0; t < kTracks; ++t)
                for (int st = 0; st < kPatSteps; ++st)
                    out.push_back(pending ? staged_[pt][t][st] : slots_[pt][t][st].load(std::memory_order_relaxed));
        }
    }

    void loadExtraState(const uint8_t* data, size_t size) override
    {
        const size_t need = 4 + (size_t)kNumPatterns * (1 + kTracks * kPatSteps);
        if (size < need || std::memcmp(data, "DRP1", 4) != 0) return;
        const uint8_t* q = data + 4;
        for (int pt = 0; pt < kNumPatterns; ++pt)
        {
            stagedLen_[pt] = (uint8_t)mpc::clamp((int)*q++, 1, kPatSteps);
            for (int t = 0; t < kTracks; ++t)
                for (int st = 0; st < kPatSteps; ++st, ++q) staged_[pt][t][st] = (uint8_t)(*q > 2 ? 0 : *q);
        }
        stagedReady_.store(true, std::memory_order_release);
    }

    void process(mpc::ProcessContext& ctx) override
    {
        const int numFrames = ctx.numFrames;
        float* outL = ctx.outputs[0];
        float* outR = ctx.numOutputs > 1 ? ctx.outputs[1] : nullptr;
        const float sr = (float)ctx.sampleRate > 1000.0f ? (float)ctx.sampleRate : sampleRate_;

        // ---- per block: the sound's settings into the engines, the sequencer's parameters ------------------------
        pushPots();
        const float acc = get(kAccent) * 0.01f;
        normal8_ = 1.0f - acc * (1.0f - kNormal808);
        normal9_ = 1.0f - acc * (1.0f - kNormal909);
        padMap_ = getInt(kPadMap);
        updateSequencerParams();
        const int mode = getInt(kSeq);
        seqOn_ = mode != kSeqOff;
        stepBeats_ = (double)kStepBeats[mpc::clamp(getInt(kRate), 0, 2)];
        swingBeats_ = (double)(get(kSwing) * 0.005f) * stepBeats_;      // up to half a step late

        // ---- the clock: the MPC's song position while it plays, our own count otherwise --------------------------
        const mpc::Transport& tr = ctx.transport;
        const float bpm = (tr.valid && tr.bpm > 20.0 && tr.bpm < 999.0) ? (float)tr.bpm : 120.0f;
        beatsPerSample_ = (double)bpm / 60.0 / (double)sr;
        const bool playing = tr.valid && tr.playing;
        bool resync = false;
        if (playing && tr.ppqValid)
        {
            if (!wasPlaying_ || std::fabs(tr.ppqPos - beat_) > 0.02) resync = true;   // started, looped or jumped
            beat_ = tr.ppqPos;
        }
        wasPlaying_ = playing;
        const bool run = (mode == kSeqSong && playing) || mode == kSeqRun;
        if (run != running_)
        {
            running_ = run;
            if (run)
            {
                if (!playing) { beat_ = 0.0; nextStep_ = 0; }     // free-running: from step 1
                else alignToGrid(0.1);
            }
        }
        else if (resync && running_) alignToGrid(0.1);

        const float gainTarget = dbToGain(get(kMaster));

        // ---- render, cut at MIDI events and sequencer steps ------------------------------------------------------
        int pos = 0, mi = 0;
        while (pos < numFrames)
        {
            while (mi < ctx.numMidiEvents && ctx.midi[mi].sampleOffset <= pos) handleMidi(ctx.midi[mi++]);
            int n = numFrames - pos;
            if (n > kChunk) n = kChunk;
            if (running_)
            {
                int guard = 0;
                while (stepTime(nextStep_) <= beat_ + 1e-9 && guard++ < 4) fireStep(nextStep_++);
                const double frames = std::ceil((stepTime(nextStep_) - beat_) / beatsPerSample_);
                if (frames < (double)n) n = frames < 1.0 ? 1 : (int)frames;
            }
            if (mi < ctx.numMidiEvents && ctx.midi[mi].sampleOffset - pos < n) n = ctx.midi[mi].sampleOffset - pos;
            if (n < 1) n = 1;

            float bl[kChunk], br[kChunk];
            renderHost(bl, br, n, sr);
            for (int i = 0; i < n; ++i)
            {
                gain_ += (gainTarget - gain_) * 0.002f;
                outL[pos + i] = softLimit(bl[i] * gain_);
                if (outR) outR[pos + i] = softLimit(br[i] * gain_);
            }
            pos += n;
            beat_ += beatsPerSample_ * (double)n;
        }
        while (mi < ctx.numMidiEvents) handleMidi(ctx.midi[mi++]);

        // Tell the MPC which parameters the plugin moved itself (the track shown, a pattern's length, a Clear key springing back), so the screen
        // follows. Nothing else in the plugin depends on this being honoured.
        if (anyDirty_)
        {
            anyDirty_ = false;
            for (int i = 0; i < kNumParams; ++i)
                if (dirty_[i]) { dirty_[i] = false; notifyHostParameterChanged(i); }
        }
    }

private:
    // ---- the engines' pots -------------------------------------------------------------------------------------
    // Each engine lists a track's knobs by name; we look up the ones we drive once, and their factory positions.
    enum Slot { sLevel = 0, sTune, sDecay, sDrive, sDist, sPan, sX1, sX2, sSound, kSlots };
    struct TrackSlots { int idx[kSlots]; int def[2][kSlots]; };     // def[0]: normal sound, def[1]: 808 alternative

    static int find808(int track, const char* name)
    {
        for (int i = 0; i < x0x::drum808_nparams(track); ++i)
            if (std::strcmp(x0x::drum808_param(track, i)->name, name) == 0) return i;
        return -1;
    }
    static int find909(int voice, const char* name)
    {
        for (int i = 0; i < x0x::drum909_nparams(voice); ++i)
            if (std::strcmp(x0x::drum909_param(voice, i)->name, name) == 0) return i;
        return -1;
    }

    void findSlots()
    {
        x0x::drum808_init(&e8_);
        x0x::drum909_init(&e9_);
        for (int t = 0; t < kTracks; ++t)
        {
            const int a = kTo808[t], b = kTo909[t];
            TrackSlots& s8 = slot8_[t];
            TrackSlots& s9 = slot9_[t];
            const char* x1 = t == tBD ? "Tone" : t == tSD ? "Tone" : "";
            const char* x2 = t == tBD ? "Attack" : t == tSD ? "Snappy" : "";
            s8.idx[sLevel] = find808(a, "Level"); s8.idx[sTune] = find808(a, "Tune"); s8.idx[sDecay] = find808(a, "Decay");
            s8.idx[sDrive] = find808(a, "Drive"); s8.idx[sDist] = find808(a, "Dist"); s8.idx[sPan] = find808(a, "Pan");
            s8.idx[sX1] = *x1 ? find808(a, x1) : -1; s8.idx[sX2] = *x2 ? find808(a, x2) : -1;
            s8.idx[sSound] = find808(a, "Sound");
            for (int alt = 0; alt < 2; ++alt)
            {
                if (s8.idx[sSound] >= 0) x0x::drum808_set(&e8_, a, s8.idx[sSound], alt);
                for (int k = 0; k < kSlots; ++k) s8.def[alt][k] = s8.idx[k] >= 0 ? x0x::drum808_get(&e8_, a, s8.idx[k]) : 0;
            }
            if (s8.idx[sSound] >= 0) x0x::drum808_set(&e8_, a, s8.idx[sSound], 0);

            const char* y1 = t == tBD ? "Pitch" : t == tSD ? "Tone" : "";
            s9.idx[sLevel] = find909(b, "Level"); s9.idx[sTune] = find909(b, "Tune");
            s9.idx[sDecay] = find909(b, t == tCP ? "Tail" : "Decay");
            s9.idx[sDrive] = find909(b, "Drive"); s9.idx[sDist] = find909(b, "Dist"); s9.idx[sPan] = find909(b, "Pan");
            s9.idx[sX1] = *y1 ? find909(b, y1) : -1; s9.idx[sX2] = *x2 ? find909(b, x2) : -1;
            s9.idx[sSound] = -1;
            for (int k = 0; k < kSlots; ++k) s9.def[0][k] = s9.def[1][k] = s9.idx[k] >= 0 ? x0x::drum909_get(&e9_, b, s9.idx[k]) : 0;
        }
    }

    // Once per block: every setting as the engines' integer pots; only the ones that changed are sent.
    void pushPots()
    {
        const float drive = get(kDrive), width = get(kWidth) * 0.01f;
        const int dist = mpc::clamp(getInt(kDriveType), 0, 6);
        const int kit = getInt(kKit);
        for (int t = 0; t < kTracks; ++t)
        {
            use909_[t] = kit == kKit909 || (kit == kKitMix && getInt(kMach + t) == 1);

            float off[kSlots] = {};
            off[sTune] = get(pTune(t)); off[sDecay] = get(pDecay(t));
            if (t == tBD) { off[sX1] = get(kBdTone); off[sX2] = get(kBdAttack); }
            if (t == tSD) { off[sX1] = get(kSdTone); off[sX2] = get(kSdSnappy); }
            const float level = get(pLevel(t));
            const int pan = mpc::clamp((int)std::lround(64.0f + 63.0f * kPanLayout[t] * width), 0, 127);

            // 808: the alternative sound first (a track's knobs edit the sound its switch selects)
            const int a = kTo808[t];
            const TrackSlots& s8 = slot8_[t];
            int alt = 0;
            if (s8.idx[sSound] >= 0)
            {
                alt = (t == tRS ? getBool(kClaves) : t == tCP ? getBool(kMaracas) : getBool(kCongas)) ? 1 : 0;
                if (alt != sw8_[t])
                {
                    sw8_[t] = alt;
                    x0x::drum808_set(&e8_, a, s8.idx[sSound], alt);
                    for (int k = 0; k < kSlots; ++k) pot8_[t][k] = -1;
                }
            }
            for (int k = 0; k < sSound; ++k)
            {
                if (s8.idx[k] < 0) continue;
                const int def = k == sLevel ? (int)std::lround((float)s8.def[alt][k] * kLevel808[t]) : s8.def[alt][k];
                const int v = k == sLevel ? levelPot(def, level) : k == sDrive ? offsetPot(def, drive) : k == sDist ? dist
                            : k == sPan ? pan : offsetPot(def, off[k]);
                if (v != pot8_[t][k]) { pot8_[t][k] = v; x0x::drum808_set(&e8_, a, s8.idx[k], v); }
            }
            const int b = kTo909[t];
            const TrackSlots& s9 = slot9_[t];
            for (int k = 0; k < sSound; ++k)
            {
                if (s9.idx[k] < 0) continue;
                const int def = s9.def[0][k];
                const int v = k == sLevel ? levelPot(def, level) : k == sDrive ? offsetPot(def, drive) : k == sDist ? dist
                            : k == sPan ? pan : offsetPot(def, off[k]);
                if (v != pot9_[t][k]) { pot9_[t][k] = v; x0x::drum909_set(&e9_, b, s9.idx[k], v); }
            }
        }
    }

    // ---- hits --------------------------------------------------------------------------------------------------
    // vel: 0..1, 1 = an accented hit.
    void hit(int t, float vel)
    {
        if (use909_[t]) x0x::drum909_trigger(&e9_, kTo909[t], vel);
        else            x0x::drum808_trigger(&e8_, kTo808[t], vel >= 0.999f ? 1.0f : vel * (kNormal808 / kNormal909));
    }

    void handleMidi(const mpc::MidiEvent& e)
    {
        if (!e.isNoteOn()) return;
        const int note = e.note();
        if (note >= 12 && note < 12 + kNumPatterns) { setParam(kPattern, (float)(note - 12)); return; }
        if (note < 24) return;
        const int t = padMap_ == 1 ? trackForGM(note) : trackForScale(note);
        hit(t, (float)e.velocity() / 127.0f);
        if (seqOn_ && getBool(kPadSel)) setParam(kTrack, (float)t);      // the steps shown follow the pad (next block)
    }

    // ---- the sequencer -----------------------------------------------------------------------------------------
    double stepTime(int64_t k) const { return (double)k * stepBeats_ + ((k & 1) ? swingBeats_ : 0.0); }

    // The first step at or after the current position; a step that began less than catchUp of a step ago still plays.
    void alignToGrid(double catchUp)
    {
        int64_t k = (int64_t)std::floor(beat_ / stepBeats_) - 1;
        while (stepTime(k) < beat_ - 1e-7) ++k;
        if (beat_ - stepTime(k - 1) < catchUp * stepBeats_) --k;
        nextStep_ = k;
    }

    void fireStep(int64_t k)
    {
        const int len = patSteps_;
        const int st = (int)(((k % len) + len) % len);
        for (int t = 0; t < kTracks; ++t)
        {
            const uint8_t v = slots_[curPattern_][t][st].load(std::memory_order_relaxed);
            if (v == 0) continue;
            if (use909_[t]) x0x::drum909_trigger(&e9_, kTo909[t], v == 2 ? 1.0f : normal9_);
            else            x0x::drum808_trigger(&e8_, kTo808[t], v == 2 ? 1.0f : normal8_);
        }
    }

    static mpc::Param notAutomated(mpc::Param p) { p.automatable = false; return p; }

    // A parameter the plugin moves itself: store it, and remember to tell the host at the end of the block.
    void setParam(int i, float plain)
    {
        if (std::fabs(get(i) - plain) < 1e-4f) return;
        set(i, plain);
        dirty_[i] = true; anyDirty_ = true;
    }

    void showRow()          // the current pattern's current track into the sixteen step parameters
    {
        for (int st = 0; st < kPatSteps; ++st)
            setParam(kStep + st, (float)slots_[curPattern_][curTrack_][st].load(std::memory_order_relaxed));
    }

    void updateSequencerParams()
    {
        const int wantPat = mpc::clamp(getInt(kPattern), 0, kNumPatterns - 1);
        const int wantTrk = mpc::clamp(getInt(kTrack), 0, kTracks - 1);
        const bool clearTrk = getBool(kClearTrk), clearPat = getBool(kClearPat);
        if (wantPat != curPattern_ || wantTrk != curTrack_)
        {
            if (wantPat != curPattern_)
            {
                curPattern_ = wantPat;
                setParam(kSteps, (float)(mpc::clamp((int)slotLen_[wantPat].load(std::memory_order_relaxed), 1, kPatSteps) - 1));
            }
            curTrack_ = wantTrk;
            showRow();
        }
        else
        {
            if (clearPat && !lastClearPat_)
                for (int t = 0; t < kTracks; ++t)
                    for (int st = 0; st < kPatSteps; ++st) slots_[curPattern_][t][st].store(0, std::memory_order_relaxed);
            if ((clearTrk && !lastClearTrk_) || (clearPat && !lastClearPat_))
                for (int st = 0; st < kPatSteps; ++st) setParam(kStep + st, 0.0f);
            for (int st = 0; st < kPatSteps; ++st)       // the steps on the screen are the track's steps
                slots_[curPattern_][curTrack_][st].store((uint8_t)mpc::clamp(getInt(kStep + st), 0, 2), std::memory_order_relaxed);
        }
        // the two Clear keys spring back by themselves
        if (clearTrk) setParam(kClearTrk, 0.0f);
        if (clearPat) setParam(kClearPat, 0.0f);
        lastClearTrk_ = lastClearPat_ = false;
        patSteps_ = mpc::clamp(getInt(kSteps) + 1, 1, kPatSteps);
        slotLen_[curPattern_].store((uint8_t)patSteps_, std::memory_order_relaxed);
    }

    // ---- audio -------------------------------------------------------------------------------------------------
    // n frames of both engines at their own rate (44.1 kHz), n <= 256.
    void renderEngines(float* l, float* r, int n)
    {
        float rev[256], dly[256];
        std::memset(l, 0, sizeof(float) * (size_t)n);
        std::memset(r, 0, sizeof(float) * (size_t)n);
        if (x0x::drum808_active(&e8_))
        {
            x0x::drum808_render_st(&e8_, l, r, rev, dly, n);
            if (kTrim808 != 1.0f) for (int i = 0; i < n; ++i) { l[i] *= kTrim808; r[i] *= kTrim808; }
        }
        if (x0x::drum909_active(&e9_))
        {
            if (kTrim909 == 1.0f) x0x::drum909_render_st(&e9_, l, r, rev, dly, n);
            else
            {
                float l9[256], r9[256];
                std::memset(l9, 0, sizeof(float) * (size_t)n);
                std::memset(r9, 0, sizeof(float) * (size_t)n);
                x0x::drum909_render_st(&e9_, l9, r9, rev, dly, n);
                for (int i = 0; i < n; ++i) { l[i] += l9[i] * kTrim909; r[i] += r9[i] * kTrim909; }
            }
        }
    }

    // n host frames. At 44.1 kHz the engines render straight into the block; otherwise their output is read
    // through a linear interpolator, eight engine frames at a time.
    void renderHost(float* l, float* r, int n, float sr)
    {
        if (!resample_) { renderEngines(l, r, n); return; }
        const float step = kEngineRate / sr;
        for (int i = 0; i < n; ++i)
        {
            while (rsPhase_ >= 1.0f)
            {
                if (rsRead_ >= rsFill_) { renderEngines(rsL_, rsR_, kRsBlock); rsFill_ = kRsBlock; rsRead_ = 0; }
                rsPrevL_ = rsCurL_; rsPrevR_ = rsCurR_;
                rsCurL_ = rsL_[rsRead_]; rsCurR_ = rsR_[rsRead_]; ++rsRead_;
                rsPhase_ -= 1.0f;
            }
            l[i] = rsPrevL_ + rsPhase_ * (rsCurL_ - rsPrevL_);
            r[i] = rsPrevR_ + rsPhase_ * (rsCurR_ - rsPrevR_);
            rsPhase_ += step;
        }
    }

    // Clean below 0.8, then rounds off towards 1.0: several loud drums on one step never clip hard.
    static inline float softLimit(float x)
    {
        const float t = 0.8f, a = std::fabs(x);
        if (a <= t) return x;
        const float u = (a - t) / (1.0f - t);
        const float y = t + (1.0f - t) * (u / (1.0f + u));
        return x < 0.0f ? -y : y;
    }

    void program(const char* name, std::vector<std::pair<int, float>> changes)
    {
        std::vector<float> v((size_t)kNumSound);
        for (int i = 0; i < kNumSound; ++i) v[(size_t)i] = param(i).def;
        for (auto& c : changes) v[(size_t)c.first] = c.second;
        addProgram(name, v);
    }

    static mpc::PluginInfo makeInfo()
    {
        mpc::PluginInfo info;
        info.name         = "Drum Rat";
        info.vendor       = "Gonzi Audio";                    // must match plugin.json "manufacturer"
        info.uniqueId     = MPC_FOURCC('G', 'V', 'U', 'x');   // must match plugin.json "uid". Never change.
        info.version      = 1000;
        info.isInstrument = true;
        info.numInputs    = 0;
        info.numOutputs   = 2;
        info.tailSamples  = 0;
        return info;
    }

    // ---- state -------------------------------------------------------------------------------------------------
    float sampleRate_ = 44100.0f;
    x0x::drum808_t e8_;
    x0x::drum909_t e9_;
    TrackSlots slot8_[kTracks], slot9_[kTracks];
    int pot8_[kTracks][kSlots], pot9_[kTracks][kSlots], sw8_[kTracks];
    bool use909_[kTracks] = {};
    float normal8_ = kNormal808, normal9_ = kNormal909;
    float gain_ = 1.0f;
    int padMap_ = 0;

    static const int kRsBlock = 8;
    bool resample_ = false;
    float rsL_[kRsBlock] = {}, rsR_[kRsBlock] = {};
    int rsFill_ = 0, rsRead_ = 0;
    float rsPhase_ = 1.0f, rsPrevL_ = 0.0f, rsPrevR_ = 0.0f, rsCurL_ = 0.0f, rsCurR_ = 0.0f;

    std::atomic<uint8_t> slots_[kNumPatterns][kTracks][kPatSteps];
    std::atomic<uint8_t> slotLen_[kNumPatterns];
    uint8_t staged_[kNumPatterns][kTracks][kPatSteps] = {};
    uint8_t stagedLen_[kNumPatterns] = {};
    std::atomic<bool> stagedReady_{false};
    int curPattern_ = 0, curTrack_ = 0, patSteps_ = 16;
    bool lastClearTrk_ = false, lastClearPat_ = false, seqOn_ = false;
    bool running_ = false, wasPlaying_ = false;
    double beat_ = 0.0, beatsPerSample_ = 0.0, stepBeats_ = 0.25, swingBeats_ = 0.0;
    int64_t nextStep_ = 0;
    bool dirty_[kNumParams] = {}, anyDirty_ = false;
};

MPC_PLUGIN_ENTRY(DrumRat)
