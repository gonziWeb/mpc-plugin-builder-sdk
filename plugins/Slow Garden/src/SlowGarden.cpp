// Slow Garden - a generative ambient instrument for the MPC, by Gonzi Audio
// SPDX-License-Identifier: MPL-2.0
// Copyright (C) 2026 Gonzi Audio. The engine is AMB-1's, Copyright (C) 2026 Charles Vestal (see LICENSING.md).
//
// Slow Garden puts the engine of AMB-1 (a free firmware for the M-VAVE FM-1 by Charles Vestal, after Brian Eno's
// Music for Airports and Discreet Music) into an MPC plugin. It plays by itself: you do not play notes, you tend it.
//
// Signal flow (all of it inside the engine, src/amb/amb.c, unchanged):
//
//   one scale + one chord that drifts through it
//      |-> PAD    four breathing voices (Rhodes or sung Voices), each on its own cycle ---.
//      |-> BELLS  FM bells on four loops of 13, 17, 23 and 29 beats ----------------------+-> mix --------------.
//      |-> BASS   a sine on the chord's root, gliding, sometimes the fifth ---------------|                     |
//      '-> PULSE  thump + two hats on Euclidean patterns of 16, 12 and 7 sixteenths ------'                     v
//                     sends -> Echoplex (160 ms) + tape loop (to 5.6 s, two heads) -> plate reverb -> soft clip -> Master -> L / R
//
// What this file adds: the parameters and programs, the link to the MPC (tempo, song position, play / stop),
// pads that edit the scale, a seed for repeatable pieces, and saving the scale with the project.
//
// The engine always runs at 44.1 kHz, which is the MPC's rate. At any other rate (a desktop host) its output is
// resampled with a straight line: good enough to listen, a little duller.

#include "mpc_plugin.h"

#include <atomic>
#include <chrono>
#include <climits>
#include <cmath>
#include <cstdint>
#include <cstring>
#include <new>
#include <string>
#include <vector>

#include "amb/fastmath.h"
#include "core/amb_engine.h"   // generated from src/amb/ by tools/make_engine.py: namespace amb1, struct Engine

namespace {

// Parameter order is permanent (projects store it): only append.
enum ParamIndex
{
    kMaster = 0,   // dB, after everything (parameter 0: harmless when the host nudges it)
    // GARDEN: the whole piece
    kTempo,        // 30..120 bpm, used while Sync is Off
    kEvolve,       // 0..100: how restless (notes change, patterns turn, layers rest)
    kMotion,       // 0..100: how far the slow tide and the wandering go
    kDrift,        // 0..100: how long a chord lasts, 20 s .. 5 min
    // PAD
    kPad,          // level
    kSwell,        // how long the voices breathe
    kBright,       // filter / vowel
    kColour,       // Rhodes, Voices
    // BELLS
    kBells,        // level
    kDensity,      // 0..5 notes a loop
    kBellTone,     // round .. metallic
    kBellOct,      // -2..+2
    // BASS
    kBass,         // level
    kBassOct,      // -1..+1
    kGlide,        // 20 ms .. 1.2 s
    kFifth,        // how often it leans on the fifth
    // PULSE
    kPulse,        // level
    kHits,         // how many hits (0: none)
    kSwing,        // 50..75 %
    kHats,         // dark and long .. bright and crisp
    // SPACE
    kEcho,         // send to the echoes
    kEchoTime,     // 1/4., 1/2, 1/2., 1 bar, 2 bars, 4 bars
    kReverb,       // send to the plate
    kSize,         // the plate's length and the echo's feedback
    // KEY
    kRoot,         // C..B
    kMode,         // 8 modes; choosing one loads its scale
    // ---- from here on the parameters belong to the player, not to the piece: programs leave them alone ----
    kTune,         // -50..+50 cents
    kSeed,         // 0 = a different piece every time; 1..99 = always the same piece
    kGrow,         // Rest, Always, Song (grows while the MPC plays)
    kSync,         // Off: own tempo. MPC: the MPC's tempo and beat
    kFreeze,       // hold the chord, the reverb rings on
    kNextChord,    // every flip of this switch moves to the next chord
    kKeyEdit,      // pads / keys add and remove notes of the scale
    kNumParams
};
constexpr int kPieceParams = kTune;   // a program sets parameters 0 .. kPieceParams - 1

// plugin parameter -> engine parameter (-1: handled here)
constexpr int kEngineParam[kNumParams] = {
    -1,
    amb1::P_TEMPO, amb1::P_EVOLVE, amb1::P_MOTION, amb1::P_DRIFT,
    amb1::P_PAD, amb1::P_SWELL, amb1::P_BRIGHT, amb1::P_PADTONE,
    amb1::P_BELLS, amb1::P_DENSITY, amb1::P_BTONE, amb1::P_BOCT,
    amb1::P_BASS, amb1::P_BASSOCT, amb1::P_GLIDE, amb1::P_FIFTH,
    amb1::P_PULSE, amb1::P_HITS, amb1::P_SWING, amb1::P_HATS,
    amb1::P_ECHO, amb1::P_ETIME, amb1::P_REVERB, amb1::P_SIZE,
    amb1::P_ROOT, -1 /* Mode: loads a scale */,
    amb1::P_TUNE, -1, -1, -1, -1, -1, -1,
};

// Master for AMB-1's eight pieces, dB: at the engine's unity they are about 14 dB apart (Voices loudest, the bell pieces
// quietest), which is fine on an FM-1 with a volume knob and awkward in a mix. The recipes themselves are untouched.
constexpr float kPieceTrim[8] = {1.0f, 4.0f, -5.0f, 3.0f, 3.0f, 4.0f, 4.0f, 1.0f};

constexpr double kEngineRate = 44100.0;
constexpr float  kToFloat    = 1.0f / 8388608.0f;   // the engine renders 24-bit integers
constexpr int    kChunk      = 256;
constexpr int    kFifo       = 64;

inline float dbToGain(float db) { return db <= -59.9f ? 0.0f : std::pow(10.0f, db * 0.05f); }

std::atomic<uint32_t> gInstances{0};

} // namespace

class SlowGarden : public mpc::Plugin
{
public:
    SlowGarden() : mpc::Plugin(makeInfo())
    {
        eng_ = new amb1::Engine();   // value-initialised: every member without an initialiser starts at zero

        addParam(mpc::floatParam ("master",   "Master",     -60.0f, 6.0f, 0.0f, "dB", 1));
        addParam(mpc::intParam   ("tempo",    "Tempo",      30, 120, 72, "bpm"));
        addParam(mpc::intParam   ("evolve",   "Evolve",     0, 100, 35));
        addParam(mpc::intParam   ("motion",   "Motion",     0, 100, 50));
        addParam(mpc::intParam   ("drift",    "Drift",      0, 100, 25));
        addParam(mpc::intParam   ("pad",      "Pad",        0, 100, 60));
        addParam(mpc::intParam   ("swell",    "Swell",      0, 100, 60));
        addParam(mpc::intParam   ("bright",   "Bright",     0, 100, 40));
        addParam(mpc::choiceParam("colour",   "Colour",     {"Rhodes", "Voices"}, 0));
        addParam(mpc::intParam   ("bells",    "Bells",      0, 100, 65));
        addParam(mpc::intParam   ("density",  "Density",    0, 100, 40));
        addParam(mpc::intParam   ("belltone", "Bell Tone",  0, 100, 50));
        addParam(mpc::intParam   ("belloct",  "Bell Oct",   -2, 2, 0));
        addParam(mpc::intParam   ("bass",     "Bass",       0, 100, 45));
        addParam(mpc::intParam   ("bassoct",  "Bass Oct",   -1, 1, 0));
        addParam(mpc::intParam   ("glide",    "Glide",      0, 100, 20));
        addParam(mpc::intParam   ("fifth",    "Fifth",      0, 100, 35));
        addParam(mpc::intParam   ("pulse",    "Pulse",      0, 100, 40));
        addParam(mpc::intParam   ("hits",     "Hits",       0, 100, 35));
        addParam(mpc::intParam   ("swing",    "Swing",      0, 100, 0));
        addParam(mpc::intParam   ("hats",     "Hats",       0, 100, 50));
        addParam(mpc::intParam   ("echo",     "Echo",       0, 100, 45));
        addParam(mpc::choiceParam("echotime", "Echo Time",  {"1/4.", "1/2", "1/2.", "1 bar", "2 bars", "4 bars"}, 3));
        addParam(mpc::intParam   ("reverb",   "Reverb",     0, 100, 65));
        addParam(mpc::intParam   ("size",     "Size",       0, 100, 80));
        addParam(mpc::choiceParam("root",     "Root",       {"C", "C#", "D", "Eb", "E", "F", "F#", "G", "Ab", "A", "Bb", "B"}, 2));
        addParam(mpc::choiceParam("mode",     "Mode",       {"Ionian", "Dorian", "Phrygian", "Lydian", "Mixolyd.", "Aeolian",
                                                             "Pent maj", "Pent min"}, 1));
        addParam(mpc::intParam   ("tune",     "Tune",       -50, 50, 0, "ct"));
        addParam(mpc::intParam   ("seed",     "Seed",       0, 99, 0));
        addParam(mpc::choiceParam("grow",     "Grow",       {"Rest", "Always", "Song"}, 1));
        addParam(mpc::choiceParam("sync",     "Sync",       {"Off", "MPC"}, 0));
        addParam(mpc::boolParam  ("freeze",   "Freeze",     false));
        addParam(mpc::boolParam  ("next",     "Next Chord", false));
        addParam(mpc::boolParam  ("keyedit",  "Key Edit",   true));

        // Programs 1-8: AMB-1's eight pieces, read from the engine's own table (so they are its recipes exactly).
        // A program sets the piece's parameters only (0 .. kPieceParams - 1); Tune, Seed, Grow, Sync, Freeze,
        // Next Chord and Key Edit stay as the player left them. Append only: projects recall programs by number.
        for (int i = 0; i < EN_NPIECES; ++i)
        {
            int16_t par[amb1::P_NPARAMS];
            uint16_t pool;
            eng_->en_piece(i, par, &pool);
            std::vector<float> v((size_t)kPieceParams, 0.0f);
            for (int p = 1; p < kPieceParams; ++p)
                v[(size_t)p] = (float)par[p == kMode ? (int)amb1::P_MODE : kEngineParam[p]];
            v[kMaster] = kPieceTrim[i];
            addProgram(eng_->en_piece_name(i), v);
        }
        // Programs 9-12: Gonzi Audio's own.
        //                      Mst Tem Evo Mot Dri  Pad Swe Bri Col  Bel Den Ton Oct  Bas BOc Gli Fif  Pul Hit Swi Hat  Ech ET Rev Siz  Rt Md
        addProgram("Crypt",       {-1, 48, 30, 60, 60,  62, 85, 25, 1,   45, 20, 15, -1,  40, -1, 60, 20,   0,  0,  0, 30,  35, 5, 85, 95,  2, 2});
        addProgram("Rainlight",   {4, 80, 60, 70, 30,  30, 50, 55, 0,   80, 80, 35,  1,  20,  0, 20, 30,   0,  0,  0, 60,  75, 2, 60, 80,  7, 6});
        addProgram("Slow Pulse",  {3, 90, 40, 45, 35,  50, 55, 35, 0,   45, 35, 40,  0,  60,  0, 25, 40,  70, 50, 20, 45,  50, 1, 50, 70,  9, 5});
        addProgram("Still Water", {-2, 60,  0, 80, 100, 62, 100, 30, 0,   0,  0, 30,  0,  35, -1, 80,  0,   0,  0,  0, 40,  30, 5, 80, 100, 5, 3});

        // a different piece in every instance and on every load, unless Seed says otherwise
        const uint64_t t = (uint64_t)std::chrono::steady_clock::now().time_since_epoch().count();
        entropy_ = (uint32_t)(t ^ (t >> 32)) ^ (gInstances.fetch_add(1) * 0x9E3779B9u) ^ (uint32_t)(uintptr_t)this;
    }

    ~SlowGarden() override { delete eng_; }

    void prepare(double sampleRate, int maxBlockSize) override
    {
        (void)maxBlockSize;
        ratio_ = kEngineRate / (sampleRate > 1000.0 ? sampleRate : kEngineRate);
        resample_ = std::fabs(ratio_ - 1.0) > 1e-6;
        frac_ = 1.0;
        fifoPos_ = fifoLen_ = 0;
        x0l_ = x0r_ = x1l_ = x1r_ = 0.0f;
        gain_ = dbToGain(get(kMaster));
    }

    // Called on resume, after a program change and after a state load, on the audio thread. The garden must not
    // start over each time the host resumes, so only a loaded state or the very first call makes a fresh engine;
    // a new program grows a new piece over the old one's tails, as AMB-1 does.
    void reset() override
    {
        const int staged = stagedPool_.exchange(-1, std::memory_order_acq_rel);
        const int program = currentProgram();
        if (staged >= 0)
            restart((uint32_t)staged);
        else if (!started_)
            restart(amb1::EN_MODE_POOL[modeIndex()]);
        else if (program != lastProgram_)
        {
            setPool(amb1::EN_MODE_POOL[modeIndex()]);
            lastMode_ = modeIndex();
            regrow_ = true;
        }
        lastProgram_ = program;
    }

    void process(mpc::ProcessContext& ctx) override
    {
        amb1::Engine& e = *eng_;
        const mpc::Transport& tr = ctx.transport;

        // ---- Seed: a new seed grows a new piece now -----------------------------------------------------------------
        const int seed = getInt(kSeed);
        if (seed != lastSeed_)
        {
            lastSeed_ = seed;
            e.rng = seedValue();
            regrow_ = true;
        }

        // ---- Grow: Rest / Always / Song (while the MPC plays). With a seed, Song starts the same piece from the
        //      top every time the MPC starts, so a bounce repeats what was heard --------------------------------------
        const int grow = getInt(kGrow);
        const bool want = grow == 1 || (grow == 2 && tr.playing);
        if (want && !playing_ && grow == 2 && seed > 0)
            restart(pool_);

        // ---- tempo: the plugin's own, or the MPC's folded into the engine's 30..120 ---------------------------------
        double bpm = (double)getInt(kTempo), fold = 1.0;
        const bool sync = getInt(kSync) == 1 && tr.tempoValid && tr.bpm > 1.0;
        if (sync)
        {
            bpm = tr.bpm;
            while (bpm > 120.0) { bpm *= 0.5; fold *= 0.5; }
            while (bpm < 30.0)  { bpm *= 2.0; fold *= 2.0; }
        }

        // ---- parameters that changed -> the engine ------------------------------------------------------------------
        for (int i = 1; i < kNumParams; ++i)
        {
            const int p = kEngineParam[i];
            if (p < 0) continue;
            const int v = i == kTempo ? (int)std::lround(bpm) : getInt(i);
            if (v != sent_[i]) { sent_[i] = v; e.en_set(p, v); }
        }
        const int mode = modeIndex();
        if (mode != lastMode_) { lastMode_ = mode; setPool(amb1::EN_MODE_POOL[mode]); }

        // ---- pads and keys: each note adds its note to the scale or takes it out (the root stays, three at least) ---
        if (getBool(kKeyEdit))
        {
            const int root = getInt(kRoot);
            for (int m = 0; m < ctx.numMidiEvents; ++m)
            {
                if (!ctx.midi[m].isNoteOn()) continue;
                const int rel = ((ctx.midi[m].note() - root) % 12 + 12) % 12;
                const uint32_t np = pool_ ^ (1u << rel);
                if (rel == 0 || popcount12(np) < 3) continue;
                setPool(np);
            }
        }
        if (regrow_) { regrow_ = false; e.en_regrow(); }

        if (want != playing_)
        {
            playing_ = want;
            if (want && seed == 0)
                e.en_seed(nextEntropy());            // as AMB-1 does on PLAY
            e.en_play(want ? 1 : 0);
        }
        const bool frozen = getBool(kFreeze);
        if (frozen != frozen_) { frozen_ = frozen; e.en_freeze(frozen ? 1 : 0); }
        const bool next = getBool(kNextChord);
        if (next != lastNext_) { lastNext_ = next; e.en_next(); }

        // ---- the MPC's clock: the exact tempo (the engine's own is a whole number) and the beat ----------------------
        e.drain();                                   // the commands above take effect now (en_render would do it)
        if (sync)
        {
            exactTempo((float)bpm);
            if (tr.playing && tr.ppqValid && e.en_playing)
                lockToBeat(tr.ppqPos * fold);
        }

        // ---- sound ---------------------------------------------------------------------------------------------------
        const float target = dbToGain(get(kMaster));
        float* outL = ctx.outputs[0];
        float* outR = ctx.numOutputs > 1 ? ctx.outputs[1] : nullptr;
        int done = 0;
        while (done < ctx.numFrames)
        {
            const int n = ctx.numFrames - done < kChunk ? ctx.numFrames - done : kChunk;
            if (resample_) renderResampled(n); else renderEngine(n);
            for (int j = 0; j < n; ++j)
            {
                gain_ += (target - gain_) * 0.002f;
                clickL_ *= 0.996f;
                clickR_ *= 0.996f;
                const float l = (bufL_[j] + clickL_) * gain_, r = (bufR_[j] + clickR_) * gain_;
                if (outL) outL[done + j] = l;
                if (outR) outR[done + j] = r;
            }
            lastL_ = bufL_[n - 1] + clickL_;
            lastR_ = bufR_[n - 1] + clickR_;
            if (std::fabs(clickL_) < 1e-9f) clickL_ = 0.0f;      // nothing left to fade: no denormals
            if (std::fabs(clickR_) < 1e-9f) clickR_ = 0.0f;
            if (gain_ < 1e-9f) gain_ = 0.0f;
            done += n;
        }
    }

    // ---- the scale goes into the project beside the parameters ("SGD1", the pool's twelve bits) ---------------------
    void saveExtraState(std::vector<uint8_t>& out) override
    {
        const int staged = stagedPool_.load(std::memory_order_acquire);
        const uint32_t p = staged >= 0 ? (uint32_t)staged : poolMirror_.load(std::memory_order_relaxed);
        out.assign({'S', 'G', 'D', '1', (uint8_t)(p & 0xFF), (uint8_t)(p >> 8 & 0x0F)});
    }

    void loadExtraState(const uint8_t* data, size_t size) override
    {
        if (size < 6 || std::memcmp(data, "SGD1", 4) != 0) return;
        const uint32_t p = ((uint32_t)data[4] | (uint32_t)data[5] << 8) & 0xFFFu;
        stagedPool_.store((int)(p | 1u), std::memory_order_release);   // taken in reset(), on the audio thread
    }

    bool formatValue(int index, float plain, std::string& out) override
    {
        const int v = (int)std::lround(plain);
        char buf[16];
        switch (index)
        {
            case kDrift: case kSwing: case kBellOct: case kBassOct: case kTune:
                eng_->en_param_text(kEngineParam[index], v, buf);   // as AMB-1's screen shows it; touches no state
                out = buf;
                return true;
            case kSeed:
                if (v == 0) { out = "Random"; return true; }
                return false;
            default: return false;
        }
    }

private:
    static mpc::PluginInfo makeInfo()
    {
        mpc::PluginInfo info;
        info.name         = "Slow Garden";
        info.vendor       = "Gonzi Audio";   // must match plugin.json "manufacturer"
        info.uniqueId     = MPC_FOURCC('G', 'g', 'g', 'B');   // must match plugin.json "uid"
        info.version      = 1000;            // 1.0.0.0
        info.isInstrument = true;
        info.numInputs    = 0;
        info.numOutputs   = 2;
        info.tailSamples  = 0;
        return info;
    }

    int modeIndex() const { return mpc::clamp(getInt(kMode), 0, (int)amb1::EN_NMODES - 1); }

    static int popcount12(uint32_t p)
    {
        int c = 0;
        for (int i = 0; i < 12; ++i) c += (int)(p >> i & 1u);
        return c;
    }

    void setPool(uint32_t p)
    {
        pool_ = (p | 1u) & 0xFFFu;
        poolMirror_.store(pool_, std::memory_order_relaxed);
        eng_->en_pool(pool_);
    }

    uint32_t nextEntropy()
    {
        entropy_ = entropy_ * 1664525u + 1013904223u;
        return entropy_ >> 8;
    }

    // the engine's random generator at the start of a piece: from Seed, or different every time
    uint32_t seedValue()
    {
        const int seed = getInt(kSeed);
        const uint32_t r = seed > 0 ? 0x9E3779B9u ^ ((uint32_t)seed * 2654435761u) : nextEntropy() * 2654435761u;
        return r ? r : 1u;
    }

    // A fresh engine (nothing is allocated: it is rebuilt in place), with the parameters as they stand and the
    // given scale. What was sounding is cut, so the last output sample is faded out over a few milliseconds.
    void restart(uint32_t pool)
    {
        clickL_ = lastL_;
        clickR_ = lastR_;
        eng_->~Engine();
        new (eng_) amb1::Engine();
        amb1::Engine& e = *eng_;
        lastSeed_ = getInt(kSeed);
        e.rng = seedValue();
        e.en_init();
        e.en_set(amb1::P_MIDI, 0);
        for (int i = 0; i < kNumParams; ++i) sent_[i] = INT_MIN;   // process() sends them all
        lastMode_ = modeIndex();
        setPool(pool);
        regrow_ = true;
        playing_ = false;
        frozen_ = false;
        lastNext_ = getBool(kNextChord);
        fifoPos_ = fifoLen_ = 0;
        started_ = true;
    }

    // The engine keeps its tempo as a whole number. With Sync on, its step and its echo are set to the MPC's exact
    // tempo instead (the echo as the engine's echo_time() works it out), so nothing creeps against the beat.
    void exactTempo(float bpm)
    {
        amb1::Engine& e = *eng_;
        int i = mpc::clamp((int)e.par[amb1::P_ETIME], 0, 5);
        const float beat = 60.0f / bpm, maxLen = (float)(TAPE_N - 8);
        float t = amb1::ETIME_BEATS[i] * beat;
        while (t * TAPE_SR > maxLen && i > 0) t = amb1::ETIME_BEATS[--i] * beat;
        if (t * TAPE_SR > maxLen) t = maxLen / TAPE_SR;
        e.echo_len_t = t * TAPE_SR;
        e.step_len = EN_SR * 60.0f / (bpm * 4.0f);
    }

    // While the MPC plays: the engine's clock pulses (24 a beat, a sixteenth every six) are moved onto the MPC's.
    // `beats` is the song position in the engine's (folded) quarter notes.
    void lockToBeat(double beats)
    {
        amb1::Engine& e = *eng_;
        const double period = (double)e.step_len / 6.0;          // engine samples a pulse
        if (period < 8.0) return;
        const double hp = beats * 24.0;                          // the MPC's position, in pulses
        const double toNext = (std::ceil(hp) - hp) * period;     // samples until its next pulse
        const double ahead = (double)(fifoLen_ - fifoPos_);      // the engine has already rendered this far past now
        double d = toNext - ((double)e.pulse_left + ahead);
        d -= period * std::floor(d / period + 0.5);              // the nearest way round
        e.pulse_left = (float)((double)e.pulse_left + d);
        const long long n = (long long)std::floor(hp + ((double)e.pulse_left + ahead) / period + 0.5);   // the pulse it fires next
        e.pulse = (int)((n % 6 + 6) % 6);
        const long long tick = (n + ((6 - n % 6) % 6 + 6) % 6) / 6;                             // and the sixteenth after
        e.sixteenth = (e.sixteenth & ~15u) | (uint32_t)((tick % 16 + 16) % 16);
    }

    // The engine is always rendered 64 frames at a time, whatever the host's block size, and read from there: the
    // piece a seed gives then does not depend on the buffer size chosen on the MPC. No delay: it runs ahead.
    inline void pull(float& l, float& r)
    {
        if (fifoPos_ >= fifoLen_)
        {
            fifoLen_ = kFifo;
            fifoPos_ = 0;
            eng_->en_render(raw_, (uint32_t)kFifo, 2048u);       // 2048: the engine's unity
        }
        l = (float)raw_[2 * fifoPos_] * kToFloat;                // the engine renders 24-bit integers
        r = (float)raw_[2 * fifoPos_ + 1] * kToFloat;
        ++fifoPos_;
    }

    void renderEngine(int n)                    // n <= kChunk frames at 44.1 kHz into bufL_ / bufR_
    {
        for (int j = 0; j < n; ++j) pull(bufL_[j], bufR_[j]);
    }

    void renderResampled(int n)                 // other host rates: a straight line between the engine's samples
    {
        for (int j = 0; j < n; ++j)
        {
            while (frac_ >= 1.0)
            {
                x0l_ = x1l_; x0r_ = x1r_;
                pull(x1l_, x1r_);
                frac_ -= 1.0;
            }
            const float f = (float)frac_;
            bufL_[j] = x0l_ + (x1l_ - x0l_) * f;
            bufR_[j] = x0r_ + (x1r_ - x0r_) * f;
            frac_ += ratio_;
        }
    }

    amb1::Engine* eng_ = nullptr;
    int32_t raw_[2 * kFifo] = {};
    float bufL_[kChunk] = {}, bufR_[kChunk] = {};

    // audio thread
    int  sent_[kNumParams] = {};
    uint32_t pool_ = 0xAADu;
    int  lastMode_ = -1, lastSeed_ = -1, lastProgram_ = 0;
    bool started_ = false, playing_ = false, frozen_ = false, lastNext_ = false, regrow_ = false;
    uint32_t entropy_ = 1u;
    float gain_ = 1.0f, lastL_ = 0.0f, lastR_ = 0.0f, clickL_ = 0.0f, clickR_ = 0.0f;
    bool resample_ = false;
    double ratio_ = 1.0, frac_ = 1.0;
    int fifoPos_ = 0, fifoLen_ = 0;
    float x0l_ = 0.0f, x0r_ = 0.0f, x1l_ = 0.0f, x1r_ = 0.0f;

    // shared with the UI thread
    std::atomic<int> stagedPool_{-1};            // a loaded scale waiting for reset()
    std::atomic<uint32_t> poolMirror_{0xAADu};   // the scale in use, for saving
};

MPC_PLUGIN_ENTRY(SlowGarden)
