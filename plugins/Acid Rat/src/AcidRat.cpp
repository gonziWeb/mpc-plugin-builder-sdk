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
// Seq = Keys: the generated line plays while a note is held, transposed by that note (C3 / note 60 = as written).
// Seq = Song: the line plays whenever the MPC plays; the last note received transposes it.
//
// Realtime: nothing in process() allocates, locks or does I/O. The engine renders in pieces of at most 32 frames,
// cut at MIDI events and at the sequencer's steps, so timing is sample-accurate.

#include "mpc_plugin.h"

#include <cmath>
#include <cstdint>
#include <cstring>
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
    kNumParams
};

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

        // Built-in programs: the defaults with a few values changed each.
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
        seqLen_ = mpc::clamp(getInt(kLength), 1, NSTEPS);
        accVel_ = getInt(kAccVel);
        octaveShift_ = 12 * getInt(kOctave);
        updatePattern();

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

        if (mode != lastMode_)            // changing mode: nothing left hanging
        {
            if (lastMode_ >= 0) { x0x::bass303_note_off(&eng_); numHeld_ = 0; }
            seqRunning_ = false; seqSounding_ = false; offCountdown_ = -1;
            lastMode_ = mode;
        }
        if (resync && seqRunning_) alignToGrid(0.1);

        // ---- delay time (glides to a new division or tempo), master ----------------------------------------------
        const float delayTarget = mpc::clamp(kDelayBeats[mpc::clamp(getInt(kDelayTime), 0, 4)] * 60.0f / bpm * sr,
                                             16.0f, (float)delayL_.size() - 8.0f);
        if (delayLen_ <= 0.0f) delayLen_ = delayTarget;
        const float delayMix = get(kDelayMix) * 0.01f, delayFb = get(kDelayFdbk) * 0.01f;
        const float gainTarget = dbToGain(get(kMaster));
        const float lpC = 1.0f - std::exp(-6.2832f * 4500.0f / sr), hpC = 1.0f - std::exp(-6.2832f * 160.0f / sr);
        const size_t mask = delayL_.size() - 1;

        // ---- render, cut at MIDI events and sequencer steps ------------------------------------------------------
        int pos = 0, mi = 0;
        float mono[kChunk];
        while (pos < numFrames)
        {
            while (mi < ctx.numMidiEvents && ctx.midi[mi].sampleOffset <= pos) handleMidi(ctx.midi[mi++], mode);
            int until = runSequencer(mode, playing);      // frames until the sequencer needs us again
            int n = numFrames - pos;
            if (n > kChunk) n = kChunk;
            if (until < n) n = until;
            if (mi < ctx.numMidiEvents && ctx.midi[mi].sampleOffset - pos < n) n = ctx.midi[mi].sampleOffset - pos;
            if (n < 1) n = 1;

            smoothPots(n, sr);
            x0x::bass303_render(&eng_, mono, n);

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
        while (mi < ctx.numMidiEvents) handleMidi(ctx.midi[mi++], mode);
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
            else seqRelease();
        }
        if (!run) return kChunk;

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
            if (lastMutBar_ != INT64_MIN && mutateBars_ > 0 && bar % mutateBars_ == 0) x0x::tb3po_mutate(&part_, &mutateRng_);
            lastMutBar_ = bar;
        }

        const x0x::bstep_t& st = part_.step[p];
        if (x0x::bstep_gate(&st) == x0x::G_REST) { seqRelease(); return; }

        const int base = (getInt(kSeqMode) == kSeqKeys && numHeld_ > 0) ? held_[numHeld_ - 1].note : latchNote_;
        const int note = (int)st.note + (base - 60);
        const bool slide = seqSounding_ && seqPrevSlide_;         // the step before slides into this one
        engineOn(note, (st.flags & BS_ACCENT) != 0, slide);
        seqSounding_ = true;
        seqPrevSlide_ = (st.flags & BS_SLIDE) != 0;
        if (seqPrevSlide_) offCountdown_ = -1;                    // held until the next step
        else
        {
            offCountdown_ = (int)((double)gateFrac_ * stepBeats_ / beatsPerSample_);
            if (offCountdown_ < 1) offCountdown_ = 1;
        }
    }

    // ---- programs ----------------------------------------------------------------------------------------------
    void program(const char* name, std::vector<std::pair<int, float>> changes)
    {
        std::vector<float> v((size_t)numParams());
        for (int i = 0; i < numParams(); ++i) v[(size_t)i] = param(i).def;
        for (auto& c : changes) v[(size_t)c.first] = c.second;
        addProgram(name, v);
    }

    static mpc::PluginInfo makeInfo()
    {
        mpc::PluginInfo info;
        info.name         = "Acid Rat";
        info.vendor       = "Gonzi Audio";   // must match plugin.json "manufacturer"
        info.uniqueId     = MPC_FOURCC(kUid[0], kUid[1], kUid[2], kUid[3]);   // must match plugin.json "uid"
        info.version      = 1001;           // 1.0.0.1 = 1.0.1 (the skin; same sound and parameters as 1.0.0)
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

    std::vector<float> delayL_, delayR_;
    size_t delayPos_ = 0;
    float  delayLen_ = 0.0f, fbLpL_ = 0, fbLpR_ = 0, fbHpL_ = 0, fbHpR_ = 0, gain_ = 1.0f;
};

// This macro creates the exported VSTPluginMain entry point. Keep it last.
MPC_PLUGIN_ENTRY(AcidRat)
