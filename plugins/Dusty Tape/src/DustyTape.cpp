// DustyTape.cpp - lo-fi tape echo for MPC standalone, by Gonzi Audio.
//
// SPDX-License-Identifier: MIT
//
// Based on the SDK's StereoDelay example (MIT), turned into a worn tape echo:
// the delay time wobbles (wow + flutter), every repeat goes through a tape-style
// saturator, gets darker, and is degraded a bit more (bit depth + sample rate),
// and tape hiss follows the signal so it fades away with the echoes.
//
// Signal path (per channel):
//
//   in ─┬──────────────────────────────────────────────────────────┐ (dry)
//       │                                                          │
//       └─► (+) ─► delay line ──► read at time + wow + flutter     │
//            ▲                          │                          │
//            │                   low cut (HP 6 dB)                 │
//            │                   tone    (LP 12 dB)                │
//            │                   drive   (tanh tape saturation)    │
//            │                   crush   (bits + sample rate)      │
//            │                   soft clip (loop can never explode)│
//            │                          │                          │
//            └──── × feedback ◄─────────┤                          │
//                                       └─► (+ hiss) ─► wet ─► mix ┴─► × output
//
// Ping Pong: the mono sum of the input goes to the left tape, left repeats feed the right
// tape and right repeats feed the left one, so echoes bounce between the speakers.

#include "mpc_plugin.h"
#include "dsp/math.h"
#include "dsp/delay_line.h"
#include "dsp/onepole.h"
#include "dsp/smoother.h"
#include "dsp/tempo.h"
#include "dsp/lfo.h"
#include "dsp/noise.h"

#include <cmath>

using namespace mpc::dsp;

class DustyTape : public mpc::Plugin
{
public:
    enum Params
    {
        kOutput,     // master output level (param 0: harmless, the host nudges it on load)
        kSync,       // tempo sync on/off
        kDivision,   // note value when synced
        kTime,       // free delay time when not synced
        kFeedback,   // how much of each repeat goes back on the tape
        kTone,       // darkness of the repeats (low-pass in the loop)
        kLowCut,     // thins the repeats (high-pass in the loop)
        kMix,        // dry/wet balance
        kWow,        // slow pitch drift of a stretched tape
        kFlutter,    // fast pitch shake of a worn capstan
        kDrive,      // tape saturation in the loop
        kHiss,       // tape noise that follows the signal
        kBits,       // bit depth of the repeats
        kRate,       // sample rate of the repeats
        kPingPong,   // bounce repeats left/right
        kNumParams
    };

    static mpc::PluginInfo makeInfo()
    {
        mpc::PluginInfo i;
        i.name = "Dusty Tape";
        i.vendor = "Gonzi Audio";
        i.uniqueId = MPC_FOURCC('G', '6', 'p', 'D');   // == "uid" in plugin.json, never change it
        i.version = 1000;
        i.isInstrument = false;
        i.numInputs = 2;
        i.numOutputs = 2;
        i.tailSamples = 0;   // depends on feedback
        return i;
    }

    DustyTape() : mpc::Plugin(makeInfo())
    {
        using namespace mpc;
        // The order below is permanent once people have projects: only ever append.
        addParam(floatParam("out",      "Output",   -24.0f, 6.0f, 0.0f, "dB", 1));
        addParam(choiceParam("sync",    "Sync",     {"Off", "On"}, 1));
        addParam(choiceParam("div",     "Division", divisionNames(), kDivisionDottedEighth));
        addParam(logParam  ("time",     "Time",     20.0f, 1500.0f, 320.0f, "ms", 0));
        addParam(floatParam("fb",       "Feedback", 0.0f, 110.0f, 45.0f, "%", 0));
        addParam(logParam  ("tone",     "Tone",     600.0f, 12000.0f, 3200.0f, "Hz", 0));
        addParam(logParam  ("lowcut",   "Low Cut",  20.0f, 1000.0f, 140.0f, "Hz", 0));
        addParam(floatParam("mix",      "Mix",      0.0f, 100.0f, 35.0f, "%", 0));
        addParam(floatParam("wow",      "Wow",      0.0f, 100.0f, 30.0f, "%", 0));
        addParam(floatParam("flutter",  "Flutter",  0.0f, 100.0f, 20.0f, "%", 0));
        addParam(floatParam("drive",    "Drive",    0.0f, 100.0f, 35.0f, "%", 0));
        addParam(floatParam("hiss",     "Hiss",     0.0f, 100.0f, 15.0f, "%", 0));
        addParam(intParam  ("bits",     "Bits",     4, 16, 12, "bit"));
        addParam(logParam  ("rate",     "Rate",     2000.0f, 44100.0f, 16000.0f, "Hz", 0));
        addParam(boolParam ("pingpong", "Ping Pong", false));
        if (numParams() != kNumParams) mpc::log("DustyTape: parameter count mismatch");

        // Built-in programs, values in parameter order:
        //            Out   Sync Div Time  Fb    Tone    LowCut Mix  Wow  Flut Drive Hiss Bits Rate     PP
        addProgram("Dusty Echo",     { 0.0f, 1, 9,  320,  45,  3200,  140,  35,  30,  20,  35,  15,  12, 16000,  0 });
        addProgram("Slapback",       { 0.0f, 0, 9,   95,  10,  5000,  120,  30,  15,  15,  45,   8,  16, 44100,  0 });
        addProgram("Dub Wash",       {-2.0f, 1, 12, 480,  78,  1800,  250,  40,  45,  25,  55,  20,  12, 12000,  1 });
        addProgram("Broken Walkman", { 0.0f, 1, 7,  250,  40,  2200,  300,  45,  85,  70,  60,  45,   8,  8000,  0 });
        addProgram("Lofi Beats",     { 0.0f, 1, 10, 500,  35,  2600,  180,  25,  40,  20,  40,  30,  10, 11025,  1 });
        addProgram("Runaway Tape",   {-4.0f, 1, 9,  320, 102,  2400,  200,  45,  35,  30,  70,  15,  12, 16000,  0 });
        addProgram("Clean Tape",     { 0.0f, 1, 9,  320,  40,  9000,   60,  30,  10,  10,  15,   0,  16, 44100,  0 });
    }

    void prepare(double sampleRate, int /*maxBlockSize*/) override
    {
        sr_ = (float)sampleRate;
        // The only allocation: tape long enough for the longest synced value at slow tempos.
        const int maxSamples = (int)(sampleRate * kMaxDelaySeconds) + 1;
        delayL_.prepare(maxSamples);
        delayR_.prepare(maxSamples);

        time_.setTime(220.0f, sampleRate);     // slow glide: changing the time bends the pitch like tape
        mix_.setTime(20.0f, sampleRate);
        feedback_.setTime(30.0f, sampleRate);
        output_.setTime(20.0f, sampleRate);

        wowL_.setSampleRate(sampleRate);     wowR_.setSampleRate(sampleRate);
        flutA_.setSampleRate(sampleRate);    flutB_.setSampleRate(sampleRate);
        driftClock_.setSampleRate(sampleRate);
        wowL_.setFrequency(0.55f);           wowR_.setFrequency(0.61f);
        flutA_.setFrequency(6.8f);           flutB_.setFrequency(11.3f);
        driftClock_.setShape(LFO::Shape::SampleHold);
        driftClock_.setFrequency(1.7f);
        drift_.setCutoff(0.6f, sampleRate);  // smooths the random steps into a lazy drift

        envAttack_  = 1.0f - std::exp(-1.0f / (0.005f * sr_));
        envRelease_ = 1.0f - std::exp(-1.0f / (0.400f * sr_));
        hissTone_.setCutoff(7000.0f, sampleRate);
        hissTone2_.setCutoff(7000.0f, sampleRate);
    }

    void reset() override
    {
        delayL_.reset(); delayR_.reset();
        lowCutL_.reset(); lowCutR_.reset();
        toneL1_.reset(); toneL2_.reset(); toneR1_.reset(); toneR2_.reset();
        drift_.reset(); hissTone_.reset(); hissTone2_.reset(); pink_.reset(); pink2_.reset();
        wowL_.reset(0.0f); wowR_.reset(0.3f); flutA_.reset(0.0f); flutB_.reset(0.5f); driftClock_.reset();
        holdL_ = holdR_ = 0.0f; holdPhase_ = 1.0f;
        env_ = 0.0f;
        time_.snap(time_.target());
        mix_.snap(get(kMix) * 0.01f);
        feedback_.snap(get(kFeedback) * 0.01f);
        output_.snap(dbToGain(get(kOutput)));
    }

    void process(mpc::ProcessContext& ctx) override
    {
        // ----- per block: parameters -> targets and coefficients ------------------------
        float ms;
        if (getInt(kSync) == 1)
            ms = beatsToSeconds(divisionBeats(getInt(kDivision)), ctx.transport.bpm) * 1000.0f;
        else
            ms = get(kTime);
        const float maxDelay = (float)delayL_.maxDelay() - 2.0f;
        time_.setTarget(clamp(msToSamples(ms, ctx.sampleRate), 2.0f, maxDelay));
        mix_.setTarget(get(kMix) * 0.01f);
        feedback_.setTarget(get(kFeedback) * 0.01f);
        output_.setTarget(dbToGain(get(kOutput)));

        const float tone = clamp(get(kTone), 200.0f, 0.45f * (float)ctx.sampleRate);
        toneL1_.setCutoff(tone, ctx.sampleRate); toneL2_.setCutoff(tone, ctx.sampleRate);
        toneR1_.setCutoff(tone, ctx.sampleRate); toneR2_.setCutoff(tone, ctx.sampleRate);
        lowCutL_.setCutoff(get(kLowCut), ctx.sampleRate);
        lowCutR_.setCutoff(get(kLowCut), ctx.sampleRate);

        // Wow: up to 6 ms of slow drift. Flutter: up to 0.5 ms of fast shake.
        const float wow01 = get(kWow) * 0.01f;
        const float flut01 = get(kFlutter) * 0.01f;
        const float wowDepth = wow01 * wow01 * 0.006f * sr_;
        const float flutDepth = flut01 * 0.0005f * sr_;

        // Drive: gain 1..7 into tanh; small signals gain at most +4 dB so the loop stays tame.
        const float driveGain = 1.0f + 6.0f * get(kDrive) * 0.01f;
        const float driveMakeup = 1.0f / std::pow(driveGain, 0.78f);

        // Crush: bit depth and sample-and-hold rate of the repeats.
        const int bits = clamp(getInt(kBits), 4, 16);
        const bool crushBits = bits < 16;
        const float steps = (float)(1 << (bits - 1));
        const float invSteps = 1.0f / steps;
        const float holdInc = clamp(get(kRate) / (float)ctx.sampleRate, 0.01f, 1.0f);

        // Hiss: at most about -30 dBFS, only while there is signal.
        const float hissLevel = get(kHiss) * 0.01f * 0.032f;
        const bool pingPong = getBool(kPingPong);

        const float* inL = ctx.inputs[0];
        const float* inR = ctx.numInputs > 1 ? ctx.inputs[1] : ctx.inputs[0];
        float* outL = ctx.outputs[0];
        float* outR = ctx.numOutputs > 1 ? ctx.outputs[1] : ctx.outputs[0];

        // ----- per sample ---------------------------------------------------------------
        for (int i = 0; i < ctx.numFrames; ++i)
        {
            const float base = time_.process();
            const float fb = feedback_.process();
            const float mix = mix_.process();
            const float outGain = output_.process();
            const float dryL = inL[i], dryR = inR[i];

            // Tape speed modulation: two slow sines (L/R slightly apart) plus a random drift,
            // and two faster sines for flutter.
            driftClock_.process();
            const float drift = drift_.process(driftClock_.last());
            const float flutter = 0.65f * flutA_.process() + 0.35f * flutB_.process();
            const float dL = base + wowDepth * (0.7f * (wowL_.process() + 1.0f) + 0.3f * (drift + 1.0f))
                                  + flutDepth * (flutter + 1.0f);
            const float dR = base + wowDepth * (0.7f * (wowR_.process() + 1.0f) + 0.3f * (drift + 1.0f))
                                  + flutDepth * (flutter + 1.0f);

            float wetL = delayL_.readLinear(dL);
            float wetR = delayR_.readLinear(dR);

            // Each repeat: thinner, darker, saturated...
            wetL = toneL2_.process(toneL1_.process(lowCutL_.process(wetL)));
            wetR = toneR2_.process(toneR1_.process(lowCutR_.process(wetR)));
            wetL = fastTanh(wetL * driveGain) * driveMakeup;
            wetR = fastTanh(wetR * driveGain) * driveMakeup;

            // ...and a little more digital dust (sample-and-hold, then bit reduction).
            holdPhase_ += holdInc;
            if (holdPhase_ >= 1.0f) { holdPhase_ -= 1.0f; holdL_ = wetL; holdR_ = wetR; }
            wetL = holdL_; wetR = holdR_;
            if (crushBits)
            {
                wetL = std::floor(wetL * steps + 0.5f) * invSteps;
                wetR = std::floor(wetR * steps + 0.5f) * invSteps;
            }
            wetL = flushDenormal(softClip(wetL));
            wetR = flushDenormal(softClip(wetR));

            if (pingPong)
            {
                const float mono = 0.5f * (dryL + dryR);
                delayL_.write(mono + wetR * fb);
                delayR_.write(wetL * fb);
            }
            else
            {
                delayL_.write(dryL + wetL * fb);
                delayR_.write(dryR + wetR * fb);
            }

            // Hiss rides on an envelope of the signal, so silence stays silent.
            const float level = 0.5f * (std::fabs(dryL) + std::fabs(dryR)) + 0.5f * (std::fabs(wetL) + std::fabs(wetR));
            env_ += (level > env_ ? envAttack_ : envRelease_) * (level - env_);
            env_ = flushDenormal(env_);
            const float hissAmt = hissLevel * clamp(env_ * 4.0f, 0.0f, 1.0f);
            float hissL = 0.0f, hissR = 0.0f;
            if (hissAmt > 0.0f)
            {
                const float h = hissTone_.process(pink_.process());
                hissL = h * hissAmt;
                hissR = hissTone2_.process(pink2_.process()) * hissAmt;
            }

            const float wL = wetL + hissL, wR = wetR + hissR;
            outL[i] = (dryL + (wL - dryL) * mix) * outGain;
            outR[i] = (dryR + (wR - dryR) * mix) * outGain;
        }
    }

private:
    static constexpr float kMaxDelaySeconds = 6.0f;   // 1/2D at 30 bpm, plus wow headroom
    static constexpr int kDivisionDottedEighth = 9;   // "1/8D" in divisionNames()

    float sr_ = 44100.0f;
    DelayLine delayL_, delayR_;
    OnePoleHP lowCutL_, lowCutR_;
    OnePoleLP toneL1_, toneL2_, toneR1_, toneR2_;
    Smoother time_, mix_, feedback_, output_;

    LFO wowL_, wowR_, flutA_, flutB_, driftClock_;
    OnePoleLP drift_;

    float holdL_ = 0.0f, holdR_ = 0.0f, holdPhase_ = 1.0f;

    float env_ = 0.0f, envAttack_ = 0.01f, envRelease_ = 0.0001f;
    PinkNoise pink_{0x1357911u}, pink2_{0x2468ACEu};
    OnePoleLP hissTone_, hissTone2_;
};

MPC_PLUGIN_ENTRY(DustyTape)
