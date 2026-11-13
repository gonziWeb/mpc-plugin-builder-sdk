// Patina.cpp - lo-fi texture effect for MPC standalone, by Gonzi Audio.
//
// SPDX-License-Identifier: MIT
//
// Makes a clean track sound old: vinyl (crackle, pops, surface dust, rumble, warp), cassette
// (wow, flutter, saturation, hiss, dropouts, treble loss), an old sampler (bit depth and sample
// rate) and a small dark room. Every noise is synthesised here; there are no samples.
//
// Signal path (stereo):
//
//   in ─┬─────────────────────────────────────────────────────────────────────────┐ (dry)
//       │                                                                         │
//       └─► (+ room) ─► speed wobble ─► dropouts ─► drive ─► hi loss ─► grain     │
//            small        warp + wow      level       tanh     6 dB LP    rate +  │
//            dark         + flutter       dips        tape                bits    │
//            reverb       (short delay)                                    │      │
//                                                                          ▼      │
//                    crackle + pops + dust + rumble + hiss ─► × gate ─►  (+)      │
//                    (gate: always on, or following the input level)       │      │
//                                                                          ▼      │
//                                        low cut ─► tone (12 dB LP) ─► width ─► mix ─► × output
//
// The room comes first on purpose: like a recording that already had its ambience before it
// was pressed or taped, so the tail gets the same wobble, saturation and grain as the sound.
//
// Age is the macro: it scales every "dirt" amount at once (0 % = clean, 100 % = the detail
// pages exactly as set). Tone, Low Cut, Width, the room and Mix are not scaled by Age.

#include "mpc_plugin.h"
#include "dsp/math.h"
#include "dsp/delay_line.h"
#include "dsp/onepole.h"
#include "dsp/smoother.h"
#include "dsp/lfo.h"
#include "dsp/noise.h"

#include <cmath>

using namespace mpc::dsp;

class Patina : public mpc::Plugin
{
public:
    enum Params
    {
        kOutput,     // master output level (param 0: harmless, the host nudges it on load)
        kAge,        // macro: scales all the dirt below at once
        kTone,       // overall darkness (12 dB/oct low-pass on the whole wet signal)
        kWidth,      // stereo width, 0 % = mono like an old record
        kNoise,      // Always = noise even in silence, Follow = noise only while the track plays
        kMix,        // dry / wet
        kCrackle,    // vinyl: dense small clicks
        kPops,       // vinyl: rare loud pops (and a scratch every turn when high)
        kDust,       // vinyl: fine surface noise that breathes with the rotation
        kRumble,     // vinyl: turntable rumble and a little mains hum
        kWarp,       // vinyl: pitch sway of a warped record, once per turn at 33 rpm
        kWow,        // tape: slow pitch drift
        kFlutter,    // tape: fast pitch shake
        kDrive,      // tape: saturation
        kHiss,       // tape: hiss
        kDropouts,   // tape: short random level dips
        kHiLoss,     // tape: treble loss (gentle 6 dB/oct)
        kBits,       // sampler: bit depth
        kRate,       // sampler: sample rate
        kRoomSize,   // room: size and decay
        kRoomTone,   // room: brightness of the tail
        kRoomMix,    // room: level
        kLowCut,     // thins the low end (radio / telephone), also applied to the noises
        kNumParams
    };

    static mpc::PluginInfo makeInfo()
    {
        mpc::PluginInfo i;
        i.name = "Patina";
        i.vendor = "Gonzi Audio";
        i.uniqueId = MPC_FOURCC('G', 'I', 'i', '1');   // == "uid" in plugin.json, never change it
        i.version = 1000;
        i.isInstrument = false;
        i.numInputs = 2;
        i.numOutputs = 2;
        i.tailSamples = 0;   // depends on the room
        return i;
    }

    Patina() : mpc::Plugin(makeInfo())
    {
        using namespace mpc;
        // The order below is permanent once people have projects: only ever append.
        addParam(floatParam ("out",      "Output",    -24.0f, 6.0f, 0.0f, "dB", 1));
        addParam(floatParam ("age",      "Age",       0.0f, 100.0f, 60.0f, "%", 0));
        addParam(logParam   ("tone",     "Tone",      800.0f, 18000.0f, 12000.0f, "Hz", 0));
        addParam(floatParam ("width",    "Width",     0.0f, 100.0f, 100.0f, "%", 0));
        addParam(choiceParam("noise",    "Noise",     {"Always", "Follow"}, 1));
        addParam(floatParam ("mix",      "Mix",       0.0f, 100.0f, 100.0f, "%", 0));
        addParam(floatParam ("crackle",  "Crackle",   0.0f, 100.0f, 35.0f, "%", 0));
        addParam(floatParam ("pops",     "Pops",      0.0f, 100.0f, 20.0f, "%", 0));
        addParam(floatParam ("dust",     "Dust",      0.0f, 100.0f, 30.0f, "%", 0));
        addParam(floatParam ("rumble",   "Rumble",    0.0f, 100.0f, 15.0f, "%", 0));
        addParam(floatParam ("warp",     "Warp",      0.0f, 100.0f, 15.0f, "%", 0));
        addParam(floatParam ("wow",      "Wow",       0.0f, 100.0f, 20.0f, "%", 0));
        addParam(floatParam ("flutter",  "Flutter",   0.0f, 100.0f, 15.0f, "%", 0));
        addParam(floatParam ("drive",    "Drive",     0.0f, 100.0f, 30.0f, "%", 0));
        addParam(floatParam ("hiss",     "Hiss",      0.0f, 100.0f, 20.0f, "%", 0));
        addParam(floatParam ("dropouts", "Dropouts",  0.0f, 100.0f, 10.0f, "%", 0));
        addParam(floatParam ("hiloss",   "Hi Loss",   0.0f, 100.0f, 25.0f, "%", 0));
        addParam(intParam   ("bits",     "Bits",      4, 16, 12, "bit"));
        addParam(logParam   ("rate",     "Rate",      2000.0f, 44100.0f, 22050.0f, "Hz", 0));
        addParam(floatParam ("roomsize", "Room Size", 0.0f, 100.0f, 35.0f, "%", 0));
        addParam(logParam   ("roomtone", "Room Tone", 1000.0f, 10000.0f, 4000.0f, "Hz", 0));
        addParam(floatParam ("roommix",  "Room Mix",  0.0f, 100.0f, 8.0f, "%", 0));
        addParam(logParam   ("lowcut",   "Low Cut",   20.0f, 800.0f, 30.0f, "Hz", 0));
        if (numParams() != kNumParams) mpc::log("Patina: parameter count mismatch");

        // Built-in programs, values in parameter order. Noise: 0 = Always, 1 = Follow.
        //                           Out  Age  Tone  Wid Noi Mix  Crk Pop Dst Rmb Wrp  Wow Flt Drv Hss Drp HiL  Bit Rate   RSz RTone RMx  LowCut
        addProgram("Patina",        { 0.0f, 60, 12000, 100, 1, 100,  35, 20, 30, 15, 15,  20, 15, 30, 20, 10, 25,  12, 22050,  35, 4000,  8,   30 });
        addProgram("Clean Vinyl",   { 0.0f, 50, 16000, 100, 1, 100,  30, 10, 25, 10, 20,   0,  0, 10,  0,  0,  0,  16, 44100,  30, 5000,  0,   20 });
        addProgram("Dusty Crate",   { 0.0f, 85,  7000,  70, 1, 100,  75, 50, 60, 35, 45,  10,  5, 35, 10,  0, 30,  12, 26000,  40, 3500, 10,   60 });
        addProgram("Needle Drop",   {-1.0f, 70, 11000,  90, 0, 100,  55, 35, 50, 40, 30,   0,  0, 20,  0,  0, 10,  16, 44100,  30, 5000,  5,   30 });
        addProgram("Cassette",      { 0.0f, 65, 10000, 100, 1, 100,   0,  0,  0,  0,  0,  35, 30, 45, 40, 20, 45,  16, 44100,  30, 4500,  6,   40 });
        addProgram("Broken Walkman",{ 2.0f, 90,  5000,  60, 1, 100,   0,  0,  0,  5,  0,  85, 75, 65, 60, 70, 70,  12, 16000,  30, 3000,  0,   90 });
        addProgram("SP Grit",       { 0.0f, 80,  9000, 100, 1, 100,   0,  0,  0,  0,  0,   0,  0, 35,  8,  0, 10,  10, 18000,  30, 4000,  0,   25 });
        addProgram("Far Radio",     { 5.0f, 75,  3200,   0, 1, 100,  10,  0, 20,  0,  0,  15, 20, 60, 45, 25, 40,  10, 11025,  55, 2500, 25,  350 });
        addProgram("Master Dust",   { 0.0f, 35, 16000, 100, 1, 100,  25,  5, 25, 10, 10,  12,  8, 20, 15,  0, 15,  16, 44100,  30, 5000,  0,   20 });
        addProgram("Dream Tape",    { 0.0f, 70,  6500, 100, 1, 100,  15,  0, 25, 10, 25,  55, 20, 40, 30, 15, 40,  14, 32000,  70, 3000, 30,   60 });
    }

    void prepare(double sampleRate, int /*maxBlockSize*/) override
    {
        sr_ = (float)sampleRate;
        srScale_ = sr_ / 44100.0f;

        // Allocations: one short delay per side for the speed wobble, and the room's delays.
        const int wobbleMax = (int)(sampleRate * 0.030) + 8;
        wobL_.prepare(wobbleMax);
        wobR_.prepare(wobbleMax);
        for (int k = 0; k < 4; ++k) roomLine_[k].prepare((int)(kRoomLen[k] * srScale_ * 1.45f) + 8);
        apA_.prepare((int)(kApLenA * srScale_) + 4);
        apB_.prepare((int)(kApLenB * srScale_) + 4);

        output_.setTime(20.0f, sampleRate);
        mix_.setTime(20.0f, sampleRate);
        width_.setTime(20.0f, sampleRate);
        warpDepth_.setTime(120.0f, sampleRate);
        wowDepth_.setTime(120.0f, sampleRate);
        flutDepth_.setTime(120.0f, sampleRate);
        driveGain_.setTime(30.0f, sampleRate);
        driveMakeup_.setTime(30.0f, sampleRate);
        roomMix_.setTime(30.0f, sampleRate);
        roomSize_.setTime(250.0f, sampleRate);
        always_.setTime(40.0f, sampleRate);

        warp_.setSampleRate(sampleRate);     warp2_.setSampleRate(sampleRate);
        wowL_.setSampleRate(sampleRate);     wowR_.setSampleRate(sampleRate);
        flutA_.setSampleRate(sampleRate);    flutB_.setSampleRate(sampleRate);
        driftClock_.setSampleRate(sampleRate);
        warp_.setFrequency(kRotationHz);     warp2_.setFrequency(2.0f * kRotationHz);
        wowL_.setFrequency(0.43f);           wowR_.setFrequency(0.47f);
        flutA_.setFrequency(6.8f);           flutB_.setFrequency(11.3f);
        driftClock_.setShape(LFO::Shape::SampleHold);
        driftClock_.setFrequency(1.7f);
        drift_.setCutoff(0.6f, sampleRate);
        rotInc_ = kRotationHz / sr_;
        humInc_ = 50.0f / sr_;

        envAttack_  = 1.0f - std::exp(-1.0f / (0.005f * sr_));
        envRelease_ = 1.0f - std::exp(-1.0f / (0.500f * sr_));
        dropCoeff_  = 1.0f - std::exp(-1.0f / (0.012f * sr_));
        popDecay_   = std::exp(-1.0f / (0.0018f * sr_));

        clickLP_.setCutoff(9000.0f, sampleRate);  clickHP_.setCutoff(500.0f, sampleRate);
        popLP_.setCutoff(1800.0f, sampleRate);    popHP_.setCutoff(40.0f, sampleRate);
        dustHPL_.setCutoff(1500.0f, sampleRate);  dustHPR_.setCutoff(1500.0f, sampleRate);
        dustLPL_.setCutoff(8000.0f, sampleRate);  dustLPR_.setCutoff(8000.0f, sampleRate);
        hissLPL_.setCutoff(9000.0f, sampleRate);  hissLPR_.setCutoff(9000.0f, sampleRate);
        rumbleA_.setCutoff(35.0f, sampleRate);    rumbleB_.setCutoff(35.0f, sampleRate);
        roomHP_.setCutoff(150.0f, sampleRate);
    }

    void reset() override
    {
        wobL_.reset(); wobR_.reset();
        for (int k = 0; k < 4; ++k) { roomLine_[k].reset(); roomDamp_[k].reset(); }
        apA_.reset(); apB_.reset(); roomHP_.reset();
        hiLossL_.reset(); hiLossR_.reset();
        toneL1_.reset(); toneL2_.reset(); toneR1_.reset(); toneR2_.reset();
        lowCutL_.reset(); lowCutR_.reset();
        clickLP_.reset(); clickHP_.reset(); popLP_.reset(); popHP_.reset();
        dustHPL_.reset(); dustHPR_.reset(); dustLPL_.reset(); dustLPR_.reset();
        hissLPL_.reset(); hissLPR_.reset(); rumbleA_.reset(); rumbleB_.reset();
        pinkL_.reset(); pinkR_.reset(); drift_.reset();
        warp_.reset(0.0f); warp2_.reset(0.25f);
        wowL_.reset(0.0f); wowR_.reset(0.2f); flutA_.reset(0.0f); flutB_.reset(0.5f); driftClock_.reset();
        rot_ = 0.0f; hum_ = 0.0f;
        click_ = 0.0f; clickDecay_ = 0.5f; clickGL_ = clickGR_ = 0.7f;
        pop_ = 0.0f; popGL_ = popGR_ = 0.7f;
        dropL_ = dropR_ = 0.0f; dropTargetL_ = dropTargetR_ = 0.0f; dropHold_ = 0; dropWait_ = (int)(sr_ * 1.5f);
        holdL_ = holdR_ = 0.0f; holdPhase_ = 1.0f;
        env_ = 0.0f;
        roomActive_ = false;

        output_.snap(dbToGain(get(kOutput)));
        mix_.snap(get(kMix) * 0.01f);
        width_.snap(get(kWidth) * 0.01f);
        warpDepth_.snap(0.0f); wowDepth_.snap(0.0f); flutDepth_.snap(0.0f);
        driveGain_.snap(1.0f); driveMakeup_.snap(1.0f);
        roomMix_.snap(get(kRoomMix) * 0.01f);
        roomSize_.snap(0.5f + 0.9f * get(kRoomSize) * 0.01f);
        always_.snap(getInt(kNoise) == 0 ? 1.0f : 0.0f);
    }

    void process(mpc::ProcessContext& ctx) override
    {
        const float sr = (float)ctx.sampleRate;

        // ----- per block: parameters -> targets and coefficients ------------------------
        const float age = get(kAge) * 0.01f;                 // the macro, 0..1
        output_.setTarget(dbToGain(get(kOutput)));
        mix_.setTarget(get(kMix) * 0.01f);
        width_.setTarget(get(kWidth) * 0.01f);
        always_.setTarget(getInt(kNoise) == 0 ? 1.0f : 0.0f);

        // Speed wobble depths in samples. Warp: up to 5 ms once per turn. Wow: up to 4 ms of slow
        // drift. Flutter: up to 0.3 ms of fast shake. (About 30 / 25 / 20 cents at the maximum.)
        const float warp01 = get(kWarp) * 0.01f * age;
        const float wow01  = get(kWow) * 0.01f * age;
        const float flut01 = get(kFlutter) * 0.01f * age;
        warpDepth_.setTarget(warp01 * warp01 * 0.005f * sr);
        wowDepth_.setTarget(wow01 * wow01 * 0.004f * sr);
        flutDepth_.setTarget(flut01 * 0.0003f * sr);
        const bool wobble = warp01 > 0.0f || wow01 > 0.0f || flut01 > 0.0f
                         || warpDepth_.value() + wowDepth_.value() + flutDepth_.value() > 0.01f;

        // Drive: gain 1..6 into tanh, with partial make-up so it gets a little louder, not a lot.
        const float dGain = 1.0f + 5.0f * get(kDrive) * 0.01f * age;
        driveGain_.setTarget(dGain);
        driveMakeup_.setTarget(1.0f / std::pow(dGain, 0.7f));

        // Dropouts.
        const float drop01 = get(kDropouts) * 0.01f * age;

        // Hi Loss: 6 dB/oct low-pass sliding from 20 kHz down to 1.8 kHz.
        const float hiLoss01 = get(kHiLoss) * 0.01f * age;
        const bool useHiLoss = hiLoss01 > 0.001f;
        if (useHiLoss)
        {
            const float hz = clamp(20000.0f * std::pow(0.09f, hiLoss01), 200.0f, 0.45f * sr);
            hiLossL_.setCutoff(hz, ctx.sampleRate); hiLossR_.setCutoff(hz, ctx.sampleRate);
        }

        // Grain: Age slides the bit depth from 16 to Bits and the rate from 44.1 kHz to Rate.
        const float bits = 16.0f - (16.0f - (float)clamp(getInt(kBits), 4, 16)) * age;
        const bool crushBits = bits < 15.95f;
        const float steps = std::pow(2.0f, bits - 1.0f);
        const float invSteps = 1.0f / steps;
        const float rateHz = 44100.0f * std::pow(clamp(get(kRate), 2000.0f, 44100.0f) / 44100.0f, age);
        const bool crushRate = rateHz < 44000.0f;
        const float holdInc = clamp(rateHz / sr, 0.01f, 1.0f);

        // Tone and Low Cut (not scaled by Age); fully open = bypassed.
        const float toneHz = get(kTone);
        const bool useTone = toneHz < 17800.0f;
        if (useTone)
        {
            const float hz = clamp(toneHz, 200.0f, 0.45f * sr);
            toneL1_.setCutoff(hz, ctx.sampleRate); toneL2_.setCutoff(hz, ctx.sampleRate);
            toneR1_.setCutoff(hz, ctx.sampleRate); toneR2_.setCutoff(hz, ctx.sampleRate);
        }
        const float lowCutHz = get(kLowCut);
        const bool useLowCut = lowCutHz > 21.0f;
        if (useLowCut) { lowCutL_.setCutoff(lowCutHz, ctx.sampleRate); lowCutR_.setCutoff(lowCutHz, ctx.sampleRate); }

        // Noise amounts.
        const float crk = get(kCrackle) * 0.01f * age;
        const float pops = get(kPops) * 0.01f * age;
        const float dust = get(kDust) * 0.01f * age;
        const float rumble = get(kRumble) * 0.01f * age;
        const float hiss = get(kHiss) * 0.01f * age;
        const bool anyNoise = crk > 0.0f || pops > 0.0f || dust > 0.0f || rumble > 0.0f || hiss > 0.0f;

        const float crkProb  = crk > 0.0f ? (1.0f + 40.0f * crk * crk) / sr : 0.0f;    // clicks per sample
        const float crkAmp   = 0.06f + 0.34f * crk;
        const float popProb  = pops > 0.0f ? (0.15f + 2.5f * pops * pops) / sr : 0.0f;
        const float popAmp   = 0.30f * (0.4f + 0.6f * pops);
        const float turnPop  = pops > 0.5f ? (pops - 0.5f) * 0.5f : 0.0f;              // a scratch every turn
        const float dustAmp  = 0.020f * dust;
        const float tickProb = dust > 0.0f ? 300.0f * dust / sr : 0.0f;
        const float tickAmp  = 0.05f * dust;
        const float rumbleAmp = 1.3f * rumble * std::sqrt(srScale_);
        const float humAmp   = 0.006f * rumble;
        const float hissAmp  = 0.090f * hiss;

        // Room: four-delay feedback network. Size sets both the delay lengths and the decay.
        const float size01 = get(kRoomSize) * 0.01f;
        roomSize_.setTarget(0.5f + 0.9f * size01);
        roomMix_.setTarget(get(kRoomMix) * 0.01f);
        const bool room = roomMix_.target() > 0.0f || roomMix_.value() > 0.0001f;
        float roomFb[4] = {0.0f, 0.0f, 0.0f, 0.0f};
        if (room)
        {
            if (!roomActive_)   // was skipped: start from silence, not from an old tail
            {
                for (int k = 0; k < 4; ++k) { roomLine_[k].reset(); roomDamp_[k].reset(); }
                apA_.reset(); apB_.reset(); roomHP_.reset();
            }
            const float rt60 = 0.25f + 1.75f * size01 * size01;
            const float sizeNow = roomSize_.target();
            for (int k = 0; k < 4; ++k)
            {
                const float seconds = kRoomLen[k] * sizeNow / 44100.0f;
                roomFb[k] = std::pow(10.0f, -3.0f * seconds / rt60);
                roomDamp_[k].setCutoff(clamp(get(kRoomTone), 300.0f, 0.45f * sr), ctx.sampleRate);
            }
        }
        roomActive_ = room;
        const int apLenA = (int)(kApLenA * srScale_), apLenB = (int)(kApLenB * srScale_);

        const float* inL = ctx.inputs[0];
        const float* inR = ctx.numInputs > 1 ? ctx.inputs[1] : ctx.inputs[0];
        float* outL = ctx.outputs[0];
        float* outR = ctx.numOutputs > 1 ? ctx.outputs[1] : ctx.outputs[0];

        // ----- per sample ---------------------------------------------------------------
        for (int i = 0; i < ctx.numFrames; ++i)
        {
            const float dryL = inL[i], dryR = inR[i];
            float xL = dryL, xR = dryR;

            // Envelope of the input: drives the noise gate in Follow mode.
            const float level = 0.5f * (std::fabs(dryL) + std::fabs(dryR));
            env_ += (level > env_ ? envAttack_ : envRelease_) * (level - env_);
            const float always = always_.process();
            const float gate = always + (1.0f - always) * clamp(env_ * 30.0f, 0.0f, 1.0f);

            // ---- room ----
            const float roomMix = roomMix_.process();
            const float sizeF = roomSize_.process();
            if (room)
            {
                float in = roomHP_.process(0.5f * (xL + xR));
                // two allpass diffusers smear the attack
                float d = apA_.readInt(apLenA); float v = in - 0.6f * d; apA_.write(v); in = d + 0.6f * v;
                d = apB_.readInt(apLenB);       v = in - 0.6f * d;       apB_.write(v); in = d + 0.6f * v;
                float t[4];
                for (int k = 0; k < 4; ++k)
                    t[k] = roomDamp_[k].process(roomLine_[k].readLinear(kRoomLen[k] * srScale_ * sizeF));
                const float half = 0.5f * (t[0] + t[1] + t[2] + t[3]);      // Householder mix (lossless)
                for (int k = 0; k < 4; ++k)
                    roomLine_[k].write(in + roomFb[k] * (t[k] - half));
                xL += 0.6f * roomMix * (t[0] - t[2]);
                xR += 0.6f * roomMix * (t[1] - t[3]);
            }

            // ---- speed wobble: warp + wow + flutter on a short delay ----
            wobL_.write(xL); wobR_.write(xR);
            const float warpD = warpDepth_.process(), wowD = wowDepth_.process(), flutD = flutDepth_.process();
            if (wobble)
            {
                driftClock_.process();
                const float drift = drift_.process(driftClock_.last());
                const float warp = 0.8f * warp_.process() + 0.2f * warp2_.process();
                const float flutter = 0.65f * flutA_.process() + 0.35f * flutB_.process();
                const float common = 1.0f + warpD * (warp + 1.0f) + flutD * (flutter + 1.0f) + wowD * 0.3f * (drift + 1.0f);
                xL = wobL_.readLinear(common + wowD * 0.7f * (wowL_.process() + 1.0f));
                xR = wobR_.readLinear(common + wowD * 0.7f * (wowR_.process() + 1.0f));
            }

            // ---- dropouts: short random dips in level ----
            if (drop01 > 0.0f)
            {
                if (dropHold_ > 0) { if (--dropHold_ == 0) dropTargetL_ = dropTargetR_ = 0.0f; }
                else if (--dropWait_ <= 0)
                {
                    const float dip = clamp(drop01 * (0.3f + 0.6f * rng_.nextFloat()), 0.0f, 0.75f);
                    const float side = rng_.nextFloat();          // half the time one side dips less
                    dropTargetL_ = dip * (side > 0.75f ? 0.4f : 1.0f);
                    dropTargetR_ = dip * (side < 0.25f ? 0.4f : 1.0f);
                    dropHold_ = (int)(sr * (0.03f + 0.12f * rng_.nextFloat()));
                    dropWait_ = (int)(sr * (3.5f - 3.1f * drop01) * (0.3f + 1.4f * rng_.nextFloat()));
                }
            }
            else dropTargetL_ = dropTargetR_ = 0.0f;
            dropL_ += dropCoeff_ * (dropTargetL_ - dropL_);
            dropR_ += dropCoeff_ * (dropTargetR_ - dropR_);
            xL *= 1.0f - dropL_; xR *= 1.0f - dropR_;

            // ---- tape drive ----
            const float dg = driveGain_.process(), dm = driveMakeup_.process();
            if (dg > 1.001f)
            {
                xL = fastTanh(xL * dg) * dm;
                xR = fastTanh(xR * dg) * dm;
            }

            // ---- treble loss ----
            if (useHiLoss) { xL = hiLossL_.process(xL); xR = hiLossR_.process(xR); }

            // ---- grain: sample-and-hold, then bit reduction ----
            if (crushRate)
            {
                holdPhase_ += holdInc;
                if (holdPhase_ >= 1.0f) { holdPhase_ -= 1.0f; holdL_ = xL; holdR_ = xR; }
                xL = holdL_; xR = holdR_;
            }
            if (crushBits)
            {
                xL = std::floor(xL * steps + 0.5f) * invSteps;
                xR = std::floor(xR * steps + 0.5f) * invSteps;
            }

            // ---- noises ----
            if (anyNoise && gate > 0.0005f)
            {
                float nL = 0.0f, nR = 0.0f;

                rot_ += rotInc_;
                bool newTurn = false;
                if (rot_ >= 1.0f) { rot_ -= 1.0f; newTurn = true; }

                // crackle: random very short clicks, each at its own place in the stereo field
                if (crkProb > 0.0f)
                {
                    if (rng_.nextFloat() < crkProb)
                    {
                        float u = rng_.nextFloat(); u *= u; u *= u;            // mostly small, a few big
                        click_ = crkAmp * (0.1f + 0.9f * u) * (rng_.next() & 1u ? 1.0f : -1.0f);
                        clickDecay_ = 0.3f + 0.5f * rng_.nextFloat();
                        const float pan = rng_.nextFloat();
                        clickGL_ = 0.35f + 0.65f * pan; clickGR_ = 1.0f - 0.65f * pan;
                    }
                    const float c = clickHP_.process(clickLP_.process(click_));
                    click_ *= clickDecay_;
                    nL += c * clickGL_; nR += c * clickGR_;
                }

                // pops: rare, bigger and duller; above 50 % also one scratch on every turn
                if (popProb > 0.0f)
                {
                    if (rng_.nextFloat() < popProb)
                    {
                        pop_ = popAmp * (0.3f + 0.7f * rng_.nextFloat()) * (rng_.next() & 1u ? 1.0f : -1.0f);
                        const float pan = rng_.nextFloat();
                        popGL_ = 0.6f + 0.4f * pan; popGR_ = 1.0f - 0.4f * pan;
                    }
                    if (newTurn && turnPop > 0.0f) { pop_ = turnPop; popGL_ = 0.9f; popGR_ = 0.6f; }
                    const float p = popHP_.process(popLP_.process(pop_));
                    pop_ *= popDecay_;
                    nL += 2.5f * p * popGL_; nR += 2.5f * p * popGR_;
                }

                // dust: fine surface noise that breathes with the rotation, plus tiny ticks
                if (dustAmp > 0.0f)
                {
                    const float breathe = 0.7f + 0.3f * fastSin01(rot_);
                    float dl = dustAmp * breathe * dustHPL_.process(white_.nextBipolar());
                    float dr = dustAmp * breathe * dustHPR_.process(white_.nextBipolar());
                    if (rng_.nextFloat() < tickProb)
                    {
                        float u = rng_.nextFloat(); u *= u;
                        const float tk = tickAmp * (0.2f + 0.8f * u) * 4.0f;
                        if (rng_.next() & 1u) dl += tk; else dr -= tk;
                    }
                    nL += dustLPL_.process(dl); nR += dustLPR_.process(dr);
                }

                // rumble: very low noise plus a little 50 / 100 Hz hum
                if (rumbleAmp > 0.0f)
                {
                    hum_ += humInc_; if (hum_ >= 1.0f) hum_ -= 1.0f;
                    const float r = rumbleAmp * rumbleB_.process(rumbleA_.process(white_.nextBipolar()))
                                  + humAmp * (fastSin01(hum_) + 0.5f * fastSin01(2.0f * hum_));
                    nL += r; nR += r;
                }

                // hiss: pink noise, different left and right
                if (hissAmp > 0.0f)
                {
                    nL += hissAmp * hissLPL_.process(pinkL_.process());
                    nR += hissAmp * hissLPR_.process(pinkR_.process());
                }

                xL += nL * gate; xR += nR * gate;
            }

            // ---- low cut, tone ----
            if (useLowCut) { xL = lowCutL_.process(xL); xR = lowCutR_.process(xR); }
            if (useTone)
            {
                xL = toneL2_.process(toneL1_.process(xL));
                xR = toneR2_.process(toneR1_.process(xR));
            }

            // ---- width (mid / side) ----
            const float w = width_.process();
            const float mid = 0.5f * (xL + xR), sideSig = 0.5f * (xL - xR) * w;
            xL = mid + sideSig; xR = mid - sideSig;

            const float mix = mix_.process();
            const float outGain = output_.process();
            outL[i] = (dryL + (xL - dryL) * mix) * outGain;
            outR[i] = (dryR + (xR - dryR) * mix) * outGain;
        }
        env_ = flushDenormal(env_);
        dropL_ = flushDenormal(dropL_); dropR_ = flushDenormal(dropR_);
        click_ = flushDenormal(click_); pop_ = flushDenormal(pop_);
    }

private:
    static constexpr float kRotationHz = 33.333f / 60.0f;                       // one turn of a 33 rpm record
    static constexpr float kRoomLen[4] = {1117.0f, 1543.0f, 1949.0f, 2351.0f};  // samples at 44.1 kHz, size 1.0
    static constexpr float kApLenA = 142.0f, kApLenB = 379.0f;

    float sr_ = 44100.0f, srScale_ = 1.0f;

    Smoother output_, mix_, width_, always_;
    Smoother warpDepth_, wowDepth_, flutDepth_, driveGain_, driveMakeup_, roomMix_, roomSize_;

    // speed wobble
    DelayLine wobL_, wobR_;
    LFO warp_, warp2_, wowL_, wowR_, flutA_, flutB_, driftClock_;
    OnePoleLP drift_;

    // dropouts
    float dropL_ = 0.0f, dropR_ = 0.0f, dropTargetL_ = 0.0f, dropTargetR_ = 0.0f, dropCoeff_ = 0.01f;
    int dropHold_ = 0, dropWait_ = 44100;

    // filters on the signal
    OnePoleLP hiLossL_, hiLossR_, toneL1_, toneL2_, toneR1_, toneR2_;
    OnePoleHP lowCutL_, lowCutR_;

    // grain
    float holdL_ = 0.0f, holdR_ = 0.0f, holdPhase_ = 1.0f;

    // noises
    Xorshift32 rng_{0x51A7E5u}, white_{0xC0FFEEu};
    PinkNoise pinkL_{0x1357911u}, pinkR_{0x2468ACEu};
    float env_ = 0.0f, envAttack_ = 0.01f, envRelease_ = 0.0001f;
    float rot_ = 0.0f, rotInc_ = 0.0f, hum_ = 0.0f, humInc_ = 0.0f;
    float click_ = 0.0f, clickDecay_ = 0.5f, clickGL_ = 0.7f, clickGR_ = 0.7f;
    float pop_ = 0.0f, popDecay_ = 0.98f, popGL_ = 0.7f, popGR_ = 0.7f;
    OnePoleLP clickLP_, popLP_, dustLPL_, dustLPR_, hissLPL_, hissLPR_, rumbleA_, rumbleB_;
    OnePoleHP clickHP_, popHP_, dustHPL_, dustHPR_;

    // room
    DelayLine roomLine_[4], apA_, apB_;
    OnePoleLP roomDamp_[4];
    OnePoleHP roomHP_;
    bool roomActive_ = false;
};

constexpr float Patina::kRoomLen[4];

MPC_PLUGIN_ENTRY(Patina)
