// Sabbath Kit - a synthesised acoustic drum kit for doom / stoner / sludge, by Gonzi Audio.
//
// SPDX-License-Identifier: MIT
//
// No samples: every hit is built at trigger time by a small physical-ish model, so no two hits
// are identical (velocity, humanize and overlapping tails all change the sound).
//
// SIGNAL FLOW
//
//   MIDI note ──► note map (General MIDI / MPC "Classic" pads) ──► voice pool (20 voices)
//
//   each voice:
//     shells  (kick, snare, toms): up to 6 membrane modes (sines at circular-membrane ratios,
//              each with its own decay) + pitch drop on the strike (tension modulation)
//              + stick / beater noise + snare-wire noise (snare)
//     metals  (hats, crash, ride): ring-modulated inharmonic square pairs + noise, through
//              band-pass / high-pass / a darkening low-pass, + pitched "ping" and "bell" partials
//
//   voices ──► pan (drummer's view, scaled by Width) ──► dry mix ─┬──────────────► + ──►
//                                                    room send ──► 4-line FDN room ─┘
//   sympathetic snare buzz: kick and tom hits make the snare wires rattle a little
//
//   mix ──► glue compressor (Comp) ──► tape saturation (Drive) ──► tilt EQ (Tone) ──► Master
//
// NOTE MAP (GM; also what the MPC "Classic" pad layout sends)
//   35/36 Kick   37 Cross-stick   38/39 Snare   40 Rimshot
//   48/50/47 Tom 1 (rack)   45/43 Tom 2 (floor)   41 Tom 3 (low floor)
//   42/54 Closed hat   44 Pedal hat   46 Open hat
//   49/52/55/57 Crash   51/58 Ride   53/56 Ride bell   59 Ride crashed (wash)
//   Any other note is folded by octaves into 35..59, so every pad plays something.
//
// CPU: voices sleep when they reach -70 dB; a dense doom groove keeps ~6-10 voices alive.
// Coefficients are computed at trigger time or once per block, never per sample.

#include "mpc_plugin.h"
#include "dsp/math.h"
#include "dsp/noise.h"
#include "dsp/svf.h"
#include "dsp/onepole.h"
#include "dsp/smoother.h"

#include <algorithm>
#include <vector>

using namespace mpc::dsp;

namespace {

// ---------------------------------------------------------------------------
// Parameters. ORDER AND IDS ARE PERMANENT: only append new ones at the end.
// ---------------------------------------------------------------------------
enum ParamIndex
{
    kMaster = 0,                                    // dB, output level (harmless when nudged)
    kKickTune, kKickDecay, kKickLevel, kKickBeater, // beater: felt (0) -> hard plastic (100)
    kSnareTune, kSnareDecay, kSnareLevel, kSnareWires,
    kTom1Tune, kTom1Decay, kTom1Level,
    kTom2Tune, kTom2Decay, kTom2Level,
    kTom3Tune, kTom3Decay, kTom3Level,
    kHatTune, kHatDecay, kHatLevel,
    kCrashTune, kCrashDecay, kCrashLevel,
    kRideTune, kRideDecay, kRideLevel,
    kRoom,      // % room level
    kRoomSize,  // % size / length of the room
    kDrive,     // % tape saturation on the mix
    kComp,      // % glue compression on the mix (0 = off)
    kTone,      // -100 dark .. +100 bright (tilt EQ around 900 Hz)
    kWidth,     // % stereo spread of the kit
    kHumanize,  // % random variation per hit (pitch, decay, level, brightness)
    kVelSens,   // % how much velocity changes level and brightness
    kNumParams
};

// Kit pieces (parameter groups) and the sounds that use them.
enum Group { gKick = 0, gSnare, gTom1, gTom2, gTom3, gHat, gCrash, gRide, kNumGroups };
enum Sound { sKick = 0, sSnare, sRim, sXStick, sTom1, sTom2, sTom3,
             sHatClosed, sHatPedal, sHatOpen, sCrash, sRide, sRideBell, sRideCrash, kNumSounds };

const Group kSoundGroup[kNumSounds] = { gKick, gSnare, gSnare, gSnare, gTom1, gTom2, gTom3,
                                        gHat, gHat, gHat, gCrash, gRide, gRide, gRide };
// Pan from the drummer's seat (right-handed kit): hats left, floor toms and ride right.
const float kGroupPan[kNumGroups]  = { 0.0f, -0.12f, -0.08f, 0.38f, 0.55f, -0.5f, -0.6f, 0.5f };
// How much of each piece feeds the room.
const float kGroupSend[kNumGroups] = { 0.55f, 1.0f, 0.95f, 0.95f, 0.95f, 0.55f, 0.7f, 0.55f };
// How many overlapping hits of one sound may ring at once before the oldest is faded out.
const int   kMaxOverlap[kNumGroups] = { 2, 2, 2, 2, 2, 3, 3, 3 };

const int kTuneIdx[kNumGroups]  = { kKickTune, kSnareTune, kTom1Tune, kTom2Tune, kTom3Tune, kHatTune, kCrashTune, kRideTune };
const int kDecayIdx[kNumGroups] = { kKickDecay, kSnareDecay, kTom1Decay, kTom2Decay, kTom3Decay, kHatDecay, kCrashDecay, kRideDecay };
const int kLevelIdx[kNumGroups] = { kKickLevel, kSnareLevel, kTom1Level, kTom2Level, kTom3Level, kHatLevel, kCrashLevel, kRideLevel };

// Sine of 2*pi*phase, phase in [0,1). Parabola + one refinement step: ~0.1 % error, cheap.
inline float sin01(float p)
{
    float x = 2.0f * p; if (x >= 1.0f) x -= 2.0f;     // -1..1 ~ -pi..pi
    float y = 4.0f * x * (1.0f - std::fabs(x));
    return 0.225f * (y * std::fabs(y) - y) + y;
}

inline float t60Coef(float ms, double sr)            // per-sample multiplier that falls 60 dB in ms
{
    ms = ms < 0.5f ? 0.5f : ms;
    return std::exp(-6.9078f / (0.001f * ms * (float)sr));
}

// Note -> sound, after folding into 35..59.
int soundForNote(int n)
{
    while (n < 35) n += 12;
    while (n > 59) n -= 12;
    switch (n)
    {
        case 35: case 36: return sKick;
        case 37: return sXStick;
        case 38: case 39: return sSnare;
        case 40: return sRim;
        case 41: return sTom3;
        case 43: case 45: return sTom2;
        case 47: case 48: case 50: return sTom1;
        case 42: case 54: return sHatClosed;
        case 44: return sHatPedal;
        case 46: return sHatOpen;
        case 49: case 52: case 55: case 57: return sCrash;
        case 51: case 58: return sRide;
        case 53: case 56: return sRideBell;
        case 59: return sRideCrash;
        default: return sSnare;
    }
}

// ---------------------------------------------------------------------------
// One drum voice
// ---------------------------------------------------------------------------
struct Voice
{
    static const int kModes = 6;

    double sr = 44100.0;
    bool active = false;
    int sound = sKick, group = gKick;
    uint32_t age = 0;              // trigger order, for stealing
    float gain = 0.0f, gl = 0.7f, gr = 0.7f, send = 0.5f;
    float fade = 1.0f, fadeCoef = 1.0f;   // 1 = no fade; < 1 = being choked / stolen

    // membrane / partial modes
    float ph[kModes] = {}, inc[kModes] = {}, amp[kModes] = {}, coef[kModes] = {};
    int nModes = 0;
    float pitchEnv = 0.0f, pitchCoef = 0.0f, glide = 0.0f;
    float modeDrive = 1.0f;

    // noise components
    WhiteNoise noise;
    float clickEnv = 0.0f, clickCoef = 0.0f, clickAmp = 0.0f;   // stick / beater
    float thumpEnv = 0.0f, thumpCoef = 0.0f, thumpAmp = 0.0f;   // low felt thud
    float wireEnv = 0.0f, wireCoef = 0.0f, wireAmp = 0.0f, wireAtt = 0.0f; // snare wires
    SVF fClick, fWire, fA, fB, fBody;
    OnePoleLP lpThump, lpWire;
    OnePoleHP hpWire;

    // metal
    float mph[6] = {}, minc[6] = {};
    float metalEnv = 0.0f, metalCoef = 0.0f, metalAmp = 0.0f;
    float burstEnv = 0.0f, burstCoef = 0.0f;        // crash "explosion"
    float bodyEnv = 0.0f, bodyCoef = 0.0f, bodyAmp = 0.0f;
    float noiseMix = 0.4f;
    float bright = 12000.0f, brightEnd = 5000.0f, brightCoef = 1.0f; // darkening low-pass
    float hpHz = 3000.0f;

    void prepare(double s, uint32_t seed)
    {
        sr = s;
        noise.reseed(seed);
        fClick.setSampleRate(sr); fWire.setSampleRate(sr); fA.setSampleRate(sr);
        fB.setSampleRate(sr); fBody.setSampleRate(sr);
        kill();
    }

    void kill()
    {
        active = false; fade = 1.0f; fadeCoef = 1.0f;
        fClick.reset(); fWire.reset(); fA.reset(); fB.reset(); fBody.reset();
        lpThump.reset(); lpWire.reset(); hpWire.reset();
    }

    void startFade(float ms) { fadeCoef = t60Coef(ms, sr); }   // choke without a click

    void setMode(int i, float hz, float a, float t60ms)
    {
        hz = clamp(hz, 10.0f, 0.45f * (float)sr);
        ph[i] = 0.0f; inc[i] = hz / (float)sr; amp[i] = a; coef[i] = t60Coef(t60ms, sr);
    }

    void setMetal(float base, const float* ratios)
    {
        for (int k = 0; k < 6; ++k) { mph[k] = 0.0f; minc[k] = clamp(base * ratios[k] / (float)sr, 0.0f, 0.45f); }
    }

    // vel = loudness 0..1, vb = brightness 0..1, ratio = tuning, dec = decay knob 0..1 (already humanized)
    void trigger(int snd, float vel, float vb, float ratio, float dec, float level, float beaterOrWires,
                 float panG_L, float panG_R)
    {
        sound = snd; group = kSoundGroup[snd];
        active = true; fade = 1.0f; fadeCoef = 1.0f;
        gl = panG_L; gr = panG_R; send = kGroupSend[group];
        gain = level * vel;
        nModes = 0; pitchEnv = 1.0f; glide = 0.0f; modeDrive = 1.0f;
        clickEnv = thumpEnv = wireEnv = metalEnv = burstEnv = bodyEnv = 0.0f;
        clickAmp = thumpAmp = wireAmp = metalAmp = bodyAmp = 0.0f;
        wireAtt = 0.0f;
        for (int i = 0; i < kModes; ++i) amp[i] = 0.0f;
        fClick.reset(); fWire.reset(); fA.reset(); fB.reset(); fBody.reset();

        switch (snd)
        {
            case sKick:
            {
                // 22-24" kick, low tuning, resonant head on: big fundamental, a few quick upper modes.
                const float f0 = 50.0f * ratio;
                const float T = lerp(220.0f, 1100.0f, dec);
                const float up = 0.35f + 0.65f * vb;          // soft hits are darker
                setMode(0, f0,         1.00f,      T);
                setMode(1, f0 * 1.48f, 0.45f * up, T * 0.40f);
                setMode(2, f0 * 2.05f, 0.25f * up, T * 0.22f);
                setMode(3, f0 * 2.62f, 0.10f * up, T * 0.14f);
                nModes = 4;
                glide = 0.16f + 0.34f * vb; pitchCoef = t60Coef(70.0f, sr);
                modeDrive = 1.35f;
                const float b = beaterOrWires;                // beater hardness
                fClick.set(lerp(1400.0f, 4200.0f, b) * (0.7f + 0.5f * vb), 0.25f);
                clickEnv = 1.0f; clickCoef = t60Coef(lerp(14.0f, 6.0f, b), sr);
                clickAmp = lerp(0.5f, 2.0f, b) * (0.3f + 0.7f * vb);
                lpThump.setCutoff(260.0f, sr);
                thumpEnv = 1.0f; thumpCoef = t60Coef(28.0f, sr); thumpAmp = 1.6f;
                break;
            }
            case sSnare:
            case sRim:
            {
                // 14x6.5" metal snare, tuned low and fat. Circular-membrane mode ratios.
                const float f0 = 182.0f * ratio;
                const float T = lerp(110.0f, 420.0f, dec);
                const float up = 0.4f + 0.6f * vb;
                setMode(0, f0,          0.85f,      T);
                setMode(1, f0 * 1.594f, 0.55f * up, T * 0.70f);
                setMode(2, f0 * 2.136f, 0.40f * up, T * 0.50f);
                setMode(3, f0 * 2.296f, 0.28f * up, T * 0.45f);
                nModes = 4;
                if (snd == sRim)
                {   // rimshot: stick hits head and rim together -> metallic ring + more crack
                    setMode(4, 905.0f * ratio,  0.32f, 190.0f);
                    setMode(5, 1530.0f * ratio, 0.20f, 140.0f);
                    nModes = 6; gain *= 1.15f;
                }
                glide = 0.04f + 0.10f * vb; pitchCoef = t60Coef(30.0f, sr);
                fClick.set(lerp(2500.0f, 6000.0f, vb), 0.15f);
                clickEnv = 1.0f; clickCoef = t60Coef(snd == sRim ? 9.0f : 6.0f, sr);
                clickAmp = (snd == sRim ? 1.6f : 0.9f) * (0.3f + 0.7f * vb);
                // wires: broadband rattle 0.7-10 kHz, louder and brighter on harder hits
                const float w = beaterOrWires;
                hpWire.setCutoff(700.0f, sr);
                lpWire.setCutoff(lerp(4500.0f, 10000.0f, vb), sr);
                fWire.set(lerp(3200.0f, 5200.0f, vb), 0.2f);
                wireEnv = 1.0f; wireCoef = t60Coef(lerp(160.0f, 650.0f, dec) * (0.6f + 0.4f * w), sr);
                wireAmp = 1.1f * w;
                break;
            }
            case sXStick:
            {
                // cross-stick: stick laid on the head, clicked on the rim. Woody, short, no wires.
                setMode(0, 470.0f * ratio,  1.00f, 55.0f);
                setMode(1, 1190.0f * ratio, 0.60f, 38.0f);
                setMode(2, 1730.0f * ratio, 0.45f, 30.0f);
                setMode(3, 2610.0f * ratio, 0.30f, 22.0f);
                setMode(4, 182.0f * ratio,  0.35f, 90.0f);    // a little head body
                nModes = 5;
                fClick.set(3500.0f, 0.3f);
                clickEnv = 1.0f; clickCoef = t60Coef(4.0f, sr); clickAmp = 0.8f;
                wireAmp = 0.12f * beaterOrWires; wireEnv = 1.0f; wireCoef = t60Coef(90.0f, sr);
                hpWire.setCutoff(700.0f, sr); lpWire.setCutoff(6000.0f, sr); fWire.set(4000.0f, 0.2f);
                gain *= 0.7f;
                break;
            }
            case sTom1:
            case sTom2:
            case sTom3:
            {
                // 12" rack, 16" and 18" floor toms. Strong pitch drop on hard hits.
                const float base = snd == sTom1 ? 108.0f : (snd == sTom2 ? 80.0f : 62.0f);
                const float f0 = base * ratio;
                const float T = lerp(350.0f, 2400.0f, dec) * (snd == sTom1 ? 0.8f : 1.0f);
                const float up = 0.35f + 0.65f * vb;
                setMode(0, f0,          0.95f,      T);
                setMode(1, f0 * 1.594f, 0.38f * up, T * 0.50f);
                setMode(2, f0 * 2.136f, 0.22f * up, T * 0.35f);
                setMode(3, f0 * 2.296f, 0.14f * up, T * 0.30f);
                nModes = 4;
                glide = 0.06f + 0.16f * vb; pitchCoef = t60Coef(160.0f, sr);
                modeDrive = 1.2f;
                fClick.set(3000.0f * (0.8f + 0.4f * vb), 0.2f);
                clickEnv = 1.0f; clickCoef = t60Coef(8.0f, sr); clickAmp = 1.3f * (0.3f + 0.7f * vb);
                lpThump.setCutoff(320.0f, sr);
                thumpEnv = 1.0f; thumpCoef = t60Coef(18.0f, sr); thumpAmp = 0.5f;
                break;
            }
            case sHatClosed:
            case sHatPedal:
            case sHatOpen:
            {
                // heavy 15" hats: darker than pop hats.
                static const float hr[6] = { 1.0f, 1.483f, 1.802f, 2.546f, 2.633f, 3.903f };
                setMetal(330.0f * ratio, hr);
                noiseMix = 0.45f;
                if (snd == sHatClosed)
                {
                    fA.set(8200.0f * ratio * (0.85f + 0.3f * vb), 0.3f);
                    hpHz = 5500.0f * ratio;
                    metalEnv = 1.0f; metalCoef = t60Coef(lerp(55.0f, 170.0f, dec), sr); metalAmp = 1.0f;
                    clickEnv = 1.0f; clickCoef = t60Coef(5.0f, sr); clickAmp = 0.5f * vb;
                    fClick.set(9500.0f, 0.1f);
                    bright = brightEnd = 14000.0f; brightCoef = 1.0f;
                }
                else if (snd == sHatPedal)
                {
                    // foot "chick": the two cymbals clapping shut
                    fA.set(7500.0f * ratio, 0.3f);
                    hpHz = 4500.0f * ratio;
                    metalEnv = 1.0f; metalCoef = t60Coef(70.0f, sr); metalAmp = 0.45f;
                    fBody.set(1150.0f * ratio, 0.55f);
                    bodyEnv = 1.0f; bodyCoef = t60Coef(30.0f, sr); bodyAmp = 1.2f;
                    bright = brightEnd = 11000.0f; brightCoef = 1.0f;
                    gain *= 0.75f;
                }
                else
                {
                    fA.set(7000.0f * ratio * (0.85f + 0.3f * vb), 0.22f);
                    hpHz = 4200.0f * ratio;
                    metalEnv = 1.0f; metalCoef = t60Coef(lerp(500.0f, 3200.0f, dec), sr); metalAmp = 1.0f;
                    clickEnv = 1.0f; clickCoef = t60Coef(6.0f, sr); clickAmp = 0.4f * vb;
                    fClick.set(9000.0f, 0.1f);
                    bright = 15000.0f; brightEnd = 7000.0f; brightCoef = t60Coef(1400.0f, sr);
                }
                fB.set(hpHz, 0.1f);
                gain *= 1.4f;
                break;
            }
            case sCrash:
            {
                // 20" crash: explosion, wash that slowly darkens, some low gong body.
                static const float cr[6] = { 1.0f, 1.341f, 1.697f, 2.213f, 2.861f, 3.517f };
                setMetal(255.0f * ratio, cr);
                noiseMix = 0.6f;
                fA.set(5200.0f * ratio, 0.12f);
                hpHz = 1500.0f * ratio; fB.set(hpHz, 0.05f);
                metalEnv = 1.0f; metalCoef = t60Coef(lerp(1500.0f, 6500.0f, dec), sr); metalAmp = 1.0f;
                burstEnv = 1.0f; burstCoef = t60Coef(110.0f, sr);
                fBody.set(640.0f * ratio, 0.6f);
                bodyEnv = 1.0f; bodyCoef = t60Coef(lerp(300.0f, 900.0f, dec), sr); bodyAmp = 0.35f;
                bright = lerp(11000.0f, 17000.0f, vb); brightEnd = 4500.0f; brightCoef = t60Coef(2200.0f, sr);
                clickEnv = 1.0f; clickCoef = t60Coef(4.0f, sr); clickAmp = 0.5f * vb; fClick.set(7000.0f, 0.1f);
                break;
            }
            case sRide:
            case sRideBell:
            case sRideCrash:
            {
                // 22" heavy ride: stick ping (pitched partials) over a wash, low hum underneath.
                static const float rr[6] = { 1.0f, 1.297f, 1.618f, 2.173f, 2.707f, 3.302f };
                setMetal(235.0f * ratio, rr);
                noiseMix = 0.35f;
                const float T = lerp(1500.0f, 5500.0f, dec);
                fA.set(6400.0f * ratio, 0.18f);
                hpHz = 2200.0f * ratio; fB.set(hpHz, 0.05f);
                metalEnv = 1.0f; metalCoef = t60Coef(T, sr);
                bright = 12500.0f; brightEnd = 6000.0f; brightCoef = t60Coef(2500.0f, sr);
                clickEnv = 1.0f; clickCoef = t60Coef(3.0f, sr); fClick.set(8000.0f, 0.1f);
                if (snd == sRideBell)
                {
                    setMode(0, 1020.0f * ratio, 0.55f, T * 0.55f);
                    setMode(1, 1530.0f * ratio, 0.42f, T * 0.45f);
                    setMode(2, 2290.0f * ratio, 0.33f, T * 0.35f);
                    setMode(3, 3310.0f * ratio, 0.24f, T * 0.25f);
                    setMode(4, 4390.0f * ratio, 0.15f, T * 0.18f);
                    setMode(5, 415.0f * ratio,  0.04f, T);
                    nModes = 6; metalAmp = 0.25f; clickAmp = 0.5f;
                }
                else if (snd == sRideCrash)
                {
                    setMode(0, 2950.0f * ratio, 0.12f, 300.0f);
                    setMode(1, 415.0f * ratio,  0.05f, T);
                    setMode(2, 622.0f * ratio,  0.04f, T * 0.8f);
                    nModes = 3; metalAmp = 0.95f; clickAmp = 0.3f;
                    burstEnv = 1.0f; burstCoef = t60Coef(160.0f, sr);
                    bright = lerp(11000.0f, 16000.0f, vb); brightEnd = 5500.0f;
                }
                else
                {
                    const float P = lerp(250.0f, 650.0f, dec);
                    const float pv = 0.5f + 0.5f * vb;
                    setMode(0, 2950.0f * ratio, 0.30f * pv, P);
                    setMode(1, 3610.0f * ratio, 0.26f * pv, P * 0.9f);
                    setMode(2, 4370.0f * ratio, 0.19f * pv, P * 0.8f);
                    setMode(3, 5230.0f * ratio, 0.13f * pv, P * 0.7f);
                    setMode(4, 415.0f * ratio,  0.035f, T);
                    setMode(5, 622.0f * ratio,  0.028f, T * 0.8f);
                    nModes = 6; metalAmp = 0.38f + 0.15f * vb; clickAmp = 0.35f;
                }
                break;
            }
        }
        // per-hit brightness for metals: refresh low-pass once now, then per block
        fWire.reset();
    }

    // Called once per render chunk: slow coefficient updates (cymbal darkening).
    void blockUpdate(int frames)
    {
        if (!active) return;
        if (group >= gHat)
        {
            // advance the exponential glide of the low-pass cutoff by `frames` samples
            const float k = brightCoef >= 1.0f ? 1.0f : std::pow(brightCoef, (float)frames);
            bright = brightEnd + (bright - brightEnd) * k;
            fWire.set(bright, 0.05f);   // fWire doubles as the cymbal darkening low-pass
        }
    }

    inline float metal()
    {
        float s[6];
        for (int k = 0; k < 6; ++k)
        {
            s[k] = mph[k] < 0.5f ? 1.0f : -1.0f;
            mph[k] += minc[k]; if (mph[k] >= 1.0f) mph[k] -= 1.0f;
        }
        // ring-modulated pairs: dense, clangorous, inharmonic spectrum
        return (s[0] * s[1] + s[2] * s[3] + s[4] * s[5]) * 0.333f;
    }

    // One mono sample.
    inline float process()
    {
        float y = 0.0f;

        // modes (shells, pings, bells)
        if (nModes > 0)
        {
            const float pm = 1.0f + glide * pitchEnv;
            float m = 0.0f;
            for (int i = 0; i < nModes; ++i)
            {
                ph[i] += inc[i] * pm; if (ph[i] >= 1.0f) ph[i] -= 1.0f;
                m += sin01(ph[i]) * amp[i];
                amp[i] *= coef[i];
            }
            pitchEnv *= pitchCoef;
            y += modeDrive > 1.0f ? fastTanh(m * modeDrive) * (1.0f / modeDrive) * 1.15f : m;
        }

        if (group <= gTom3)
        {
            if (clickEnv > 1e-4f) { y += fClick.process(noise.process(), SVF::Mode::Bandpass) * clickEnv * clickAmp; clickEnv *= clickCoef; }
            if (thumpEnv > 1e-4f) { y += lpThump.process(noise.process()) * thumpEnv * thumpAmp; thumpEnv *= thumpCoef; }
            if (wireAmp > 0.0f && wireEnv > 1e-4f)
            {
                wireAtt += (1.0f - wireAtt) * 0.02f;            // wires lag the stick by ~1 ms
                float n = hpWire.process(noise.process());
                n = lpWire.process(n) * 0.7f + fWire.process(n, SVF::Mode::Bandpass) * 0.6f;
                y += n * wireEnv * wireAmp * wireAtt;
                wireEnv *= wireCoef;
            }
        }
        else
        {
            const float n = noise.process();
            float src = metal() * (1.0f - noiseMix) + n * noiseMix;
            float lpS, bpS, hpS;
            fA.processAll(src, lpS, bpS, hpS);
            float m = bpS * 1.6f + hpS * 0.35f;                 // band + air
            m = fB.process(m, SVF::Mode::Highpass);
            m = fWire.process(m, SVF::Mode::Lowpass);           // darkening low-pass
            const float env = metalEnv * (1.0f + 1.6f * burstEnv);
            y += m * env * metalAmp;
            metalEnv *= metalCoef; burstEnv *= burstCoef;
            if (bodyEnv > 1e-4f) { y += fBody.process(n, SVF::Mode::Bandpass) * bodyEnv * bodyAmp; bodyEnv *= bodyCoef; }
            if (clickEnv > 1e-4f) { y += fClick.process(n, SVF::Mode::Highpass) * clickEnv * clickAmp; clickEnv *= clickCoef; }
        }

        fade *= fadeCoef;
        // asleep below -70 dB
        float level = clickEnv + thumpEnv + wireEnv * wireAmp + metalEnv * metalAmp + bodyEnv;
        for (int i = 0; i < nModes; ++i) level += amp[i];
        if (level * fade < 3e-4f) active = false;
        return y * gain * fade;
    }
};

// ---------------------------------------------------------------------------
// Room: 4-line feedback delay network with damping (a wooden rehearsal room
// at small sizes, a big live room at large sizes).
// ---------------------------------------------------------------------------
struct Room
{
    std::vector<float> buf[4];
    int len[4] = {1, 1, 1, 1}, pos[4] = {0, 0, 0, 0};
    float g[4] = {0, 0, 0, 0};
    float lp[4] = {0, 0, 0, 0};
    float damp = 0.5f;
    double sr = 44100.0;
    int maxLen = 1;
    float lastSize = -1.0f;
    OnePoleHP hpIn;

    void prepare(double s)
    {
        sr = s;
        maxLen = (int)(0.090 * sr) + 8;                       // longest line at max size
        for (auto& b : buf) b.assign((size_t)maxLen, 0.0f);
        hpIn.setCutoff(120.0f, sr);                           // keep the kick's sub out of the room
        lastSize = -1.0f;
        reset();
    }
    void reset()
    {
        for (auto& b : buf) std::fill(b.begin(), b.end(), 0.0f);
        for (int i = 0; i < 4; ++i) { pos[i] = 0; lp[i] = 0.0f; }
        hpIn.reset();
    }
    void setSize(float size01)                                // block rate
    {
        if (std::fabs(size01 - lastSize) < 1e-4f) return;
        lastSize = size01;
        static const float baseMs[4] = { 23.1f, 29.7f, 34.3f, 41.9f };
        const float scale = lerp(0.45f, 2.0f, size01);
        const float rt60 = lerp(0.35f, 2.4f, size01);         // seconds
        for (int i = 0; i < 4; ++i)
        {
            len[i] = clamp((int)(baseMs[i] * scale * 0.001f * (float)sr), 16, maxLen - 1);
            if (pos[i] >= len[i]) pos[i] = 0;
            g[i] = std::pow(10.0f, -3.0f * (float)len[i] / (rt60 * (float)sr));
        }
        damp = 1.0f - std::exp(-2.0f * kPi * lerp(6500.0f, 3500.0f, size01) / (float)sr);
    }
    inline void process(float in, float& outL, float& outR)
    {
        in = hpIn.process(in);
        float d[4];
        for (int i = 0; i < 4; ++i) d[i] = buf[i][(size_t)pos[i]];
        // Hadamard mixing (orthogonal, energy preserving)
        const float a = d[0] + d[1], b = d[0] - d[1], c = d[2] + d[3], e = d[2] - d[3];
        float m[4] = { (a + c) * 0.5f, (b + e) * 0.5f, (a - c) * 0.5f, (b - e) * 0.5f };
        static const float sign[4] = { 1.0f, -1.0f, 1.0f, -1.0f };
        for (int i = 0; i < 4; ++i)
        {
            lp[i] += damp * (m[i] * g[i] - lp[i]);
            float v = lp[i] + in * sign[i];
            buf[i][(size_t)pos[i]] = flushDenormal(v);
            if (++pos[i] >= len[i]) pos[i] = 0;
        }
        outL = (d[0] + d[2] * 0.6f) * 0.5f;
        outR = (d[1] + d[3] * 0.6f) * 0.5f;
    }
};

} // namespace

// ---------------------------------------------------------------------------
class SabbathKit : public mpc::Plugin
{
public:
    static mpc::PluginInfo makeInfo()
    {
        mpc::PluginInfo i;
        i.name = "Sabbath Kit";
        i.vendor = "Gonzi Audio";
        i.uniqueId = MPC_FOURCC('G', 'p', 'n', 'q');   // == "uid" in plugin.json. Never change.
        i.version = 1000;
        i.isInstrument = true;
        i.numInputs = 0;
        i.numOutputs = 2;
        return i;
    }

    SabbathKit() : mpc::Plugin(makeInfo())
    {
        using namespace mpc;
        addParam(floatParam("master", "Master", -40.0f, 6.0f, -4.0f, "dB", 1));

        addParam(floatParam("kick_tune",   "Kick Tune",  -12.0f, 12.0f, 0.0f, "st", 1));
        addParam(floatParam("kick_decay",  "Kick Decay",   0.0f, 100.0f, 45.0f, "%", 0));
        addParam(floatParam("kick_level",  "Kick Level",   0.0f, 100.0f, 85.0f, "%", 0));
        addParam(floatParam("kick_beater", "Kick Beater",  0.0f, 100.0f, 40.0f, "%", 0));

        addParam(floatParam("snare_tune",  "Snare Tune", -12.0f, 12.0f, 0.0f, "st", 1));
        addParam(floatParam("snare_decay", "Snare Decay",  0.0f, 100.0f, 50.0f, "%", 0));
        addParam(floatParam("snare_level", "Snare Level",  0.0f, 100.0f, 80.0f, "%", 0));
        addParam(floatParam("snare_wires", "Snare Wires",  0.0f, 100.0f, 70.0f, "%", 0));

        const char* tomIds[3]  = { "tom1", "tom2", "tom3" };
        const char* tomName[3] = { "Tom1", "Tom2", "Tom3" };
        for (int t = 0; t < 3; ++t)
        {
            std::string id(tomIds[t]), nm(tomName[t]);
            addParam(floatParam((id + "_tune").c_str(),  (nm + " Tune").c_str(),  -12.0f, 12.0f, 0.0f, "st", 1));
            addParam(floatParam((id + "_decay").c_str(), (nm + " Decay").c_str(),   0.0f, 100.0f, 55.0f, "%", 0));
            addParam(floatParam((id + "_level").c_str(), (nm + " Level").c_str(),   0.0f, 100.0f, 78.0f, "%", 0));
        }
        addParam(floatParam("hat_tune",    "Hat Tune",   -12.0f, 12.0f, 0.0f, "st", 1));
        addParam(floatParam("hat_decay",   "Hat Decay",    0.0f, 100.0f, 50.0f, "%", 0));
        addParam(floatParam("hat_level",   "Hat Level",    0.0f, 100.0f, 55.0f, "%", 0));
        addParam(floatParam("crash_tune",  "Crash Tune", -12.0f, 12.0f, 0.0f, "st", 1));
        addParam(floatParam("crash_decay", "Crash Decay",  0.0f, 100.0f, 60.0f, "%", 0));
        addParam(floatParam("crash_level", "Crash Level",  0.0f, 100.0f, 55.0f, "%", 0));
        addParam(floatParam("ride_tune",   "Ride Tune",  -12.0f, 12.0f, 0.0f, "st", 1));
        addParam(floatParam("ride_decay",  "Ride Decay",   0.0f, 100.0f, 55.0f, "%", 0));
        addParam(floatParam("ride_level",  "Ride Level",   0.0f, 100.0f, 55.0f, "%", 0));

        addParam(floatParam("room",      "Room",      0.0f, 100.0f, 30.0f, "%", 0));
        addParam(floatParam("room_size", "Room Size", 0.0f, 100.0f, 55.0f, "%", 0));
        addParam(floatParam("drive",     "Drive",     0.0f, 100.0f, 25.0f, "%", 0));
        addParam(floatParam("comp",      "Comp",      0.0f, 100.0f, 30.0f, "%", 0));
        addParam(floatParam("tone",      "Tone",   -100.0f, 100.0f, -15.0f, "", 0));
        addParam(floatParam("width",     "Width",     0.0f, 100.0f, 80.0f, "%", 0));
        addParam(floatParam("humanize",  "Humanize",  0.0f, 100.0f, 35.0f, "%", 0));
        addParam(floatParam("vel_sens",  "Vel Sens",  0.0f, 100.0f, 80.0f, "%", 0));
        if (numParams() != kNumParams) mpc::log("SabbathKit: parameter count mismatch");

        addPresets();
    }

    void prepare(double sampleRate, int /*maxBlockSize*/) override
    {
        sr_ = sampleRate;
        for (int v = 0; v < kVoices; ++v) voices_[v].prepare(sampleRate, 0x9E3779B9u ^ (uint32_t)(v * 2654435761u + 17u));
        room_.prepare(sampleRate);
        symNoise_.reseed(0xBADC0FFEu);
        symHp_.setCutoff(700.0f, sampleRate); symLp_.setCutoff(7000.0f, sampleRate);
        symCoef_ = t60Coef(220.0f, sampleRate);
        compAtt_ = 1.0f - std::exp(-1.0f / (0.003f * (float)sampleRate));
        compRel_ = 1.0f - std::exp(-1.0f / (0.160f * (float)sampleRate));
        compSmooth_ = 1.0f - std::exp(-1.0f / (0.002f * (float)sampleRate));
        master_.setTime(15.0f, sampleRate);
        reset();
    }

    void reset() override
    {
        for (auto& v : voices_) v.kill();
        room_.reset();
        symEnv_ = 0.0f; symHp_.reset(); symLp_.reset();
        compEnv_ = 0.0f; compGain_ = 1.0f; compTarget_ = 1.0f;
        toneL_.reset(); toneR_.reset();
        master_.snap(dbToGain(get(kMaster)));
    }

    void process(mpc::ProcessContext& ctx) override
    {
        // ---- block-rate parameter reads --------------------------------------
        room_.setSize(get(kRoomSize) * 0.01f);
        roomLevel_ = get(kRoom) * 0.01f;
        roomLevel_ = roomLevel_ * roomLevel_ * 1.6f;
        width_ = get(kWidth) * 0.01f;
        wires_ = get(kSnareWires) * 0.01f;
        snareLevel_ = get(kSnareLevel) * 0.01f;

        const float comp = get(kComp) * 0.01f;
        compOn_ = comp > 0.001f;
        compThDb_ = -30.0f * comp;
        compMakeup_ = dbToGain(-compThDb_ * 0.45f);

        const float drive = get(kDrive) * 0.01f;
        driveG_ = 1.0f + 9.0f * drive * drive;
        driveMk_ = 1.0f / std::pow(driveG_, 0.75f);
        driveOn_ = drive > 0.001f;

        const float tone = get(kTone) * 0.01f;
        toneLo_ = dbToGain(-5.0f * tone);
        toneHi_ = dbToGain(6.0f * tone);
        toneL_.setCutoff(900.0f, sr_); toneR_.setCutoff(900.0f, sr_);

        master_.setTarget(dbToGain(get(kMaster)));

        // Work in chunks of at most kMaxBlock frames (the room send buffer size).
        int e = 0;
        for (int off = 0; off < ctx.numFrames; off += kMaxBlock)
        {
            const int n = std::min(kMaxBlock, ctx.numFrames - off);
            float* L = ctx.outputs[0] + off;
            float* R = ctx.outputs[1] + off;

            // ---- sample-accurate MIDI --------------------------------------
            int pos = 0;
            for (; e < ctx.numMidiEvents && ctx.midi[e].sampleOffset < off + n; ++e)
            {
                const mpc::MidiEvent& ev = ctx.midi[e];
                const int at = clamp(ev.sampleOffset - off, 0, n);
                if (at > pos) { renderVoices(L, R, pos, at); pos = at; }
                handleMidi(ev);
            }
            renderVoices(L, R, pos, n);
            processBus(L, R, n);
        }
    }

private:
    void processBus(float* L, float* R, int numFrames)
    {
        for (int i = 0; i < numFrames; ++i)
        {
            float l = L[i], r = R[i];

            // room (mono send stored in sendBuf_, filled by renderVoices)
            float rl, rr;
            room_.process(sendBuf_[i], rl, rr);
            l += rl * roomLevel_; r += rr * roomLevel_;

            // glue compressor: stereo-linked peak detector, 4:1, gain computed every 16 samples
            if (compOn_)
            {
                const float pk = std::fmax(std::fabs(l), std::fabs(r));
                compEnv_ += (pk > compEnv_ ? compAtt_ : compRel_) * (pk - compEnv_);
                if ((i & 15) == 0)
                {
                    const float envDb = gainToDb(compEnv_ + 1e-9f);
                    const float over = envDb - compThDb_;
                    compTarget_ = over > 0.0f ? dbToGain(-over * 0.75f) : 1.0f;
                }
                compGain_ += compSmooth_ * (compTarget_ - compGain_);
                const float g = compGain_ * compMakeup_;
                l *= g; r *= g;
            }

            // tape-style saturation
            if (driveOn_)
            {
                l = fastTanh(l * driveG_) * driveMk_;
                r = fastTanh(r * driveG_) * driveMk_;
            }

            // tilt EQ
            const float lpL = toneL_.process(l), lpR = toneR_.process(r);
            l = lpL * toneLo_ + (l - lpL) * toneHi_;
            r = lpR * toneLo_ + (r - lpR) * toneHi_;

            const float m = master_.process();
            L[i] = sanitize(softLimit(l * m));
            R[i] = sanitize(softLimit(r * m));
        }
    }

    static const int kVoices = 20;
    static const int kMaxBlock = 4096;

    // Transparent below -3 dBFS, rounds off anything hotter so the MPC never sees overs.
    static inline float softLimit(float x)
    {
        const float t = 0.7f;
        const float a = std::fabs(x);
        if (a <= t) return x;
        const float s = x < 0.0f ? -1.0f : 1.0f;
        return s * (t + (1.0f - t) * fastTanh((a - t) / (1.0f - t)));
    }

    void handleMidi(const mpc::MidiEvent& ev)
    {
        if (ev.isNoteOn()) trigger(soundForNote(ev.note()), ev.velocity());
        else if (ev.isController() && (ev.controller() == 120 || ev.controller() == 123))
            for (auto& v : voices_) if (v.active) v.startFade(30.0f);
        // note-offs are ignored: drums ring out naturally
    }

    void trigger(int snd, int velocity)
    {
        const int grp = kSoundGroup[snd];
        const float hum = get(kHumanize) * 0.01f;
        const float sens = get(kVelSens) * 0.01f;
        const float v = clamp((float)velocity / 127.0f, 0.0f, 1.0f);

        // humanize: every hit slightly different (a tiny amount even at 0 %)
        const float rPitch = rng_.nextBipolar(), rDec = rng_.nextBipolar(), rLvl = rng_.nextBipolar(), rBr = rng_.nextBipolar();
        const float cents = rPitch * (1.5f + 16.0f * hum);
        const float vel = lerp(1.0f, std::pow(v, 1.6f), sens) * dbToGain(rLvl * (0.3f + 1.7f * hum));
        const float vb  = clamp(lerp(0.85f, v, sens) + rBr * 0.08f * hum, 0.0f, 1.0f);
        const float dec = clamp(get(kDecayIdx[grp]) * 0.01f * (1.0f + rDec * 0.1f * hum), 0.0f, 1.0f);
        const float ratio = semitonesToRatio(get(kTuneIdx[grp])) * centsToRatio(cents);
        const float level = get(kLevelIdx[grp]) * 0.01f;
        const float extra = grp == gKick ? get(kKickBeater) * 0.01f : get(kSnareWires) * 0.01f;

        // hi-hat choke: closed or pedal shuts the open hat (fast fade, no click)
        if (snd == sHatClosed || snd == sHatPedal)
            for (auto& vo : voices_) if (vo.active && vo.sound == sHatOpen) vo.startFade(25.0f);

        // limit overlapping hits of one piece: fade the oldest
        int count = 0, oldest = -1; uint32_t oldestAge = 0xFFFFFFFFu;
        for (int i = 0; i < kVoices; ++i)
            if (voices_[i].active && voices_[i].group == grp && voices_[i].fadeCoef >= 1.0f)
            {
                ++count;
                if (voices_[i].age < oldestAge) { oldestAge = voices_[i].age; oldest = i; }
            }
        if (count >= kMaxOverlap[grp] && oldest >= 0) voices_[oldest].startFade(grp >= gHat ? 120.0f : 60.0f);

        // find a free voice, else steal the oldest
        int slot = -1; oldestAge = 0xFFFFFFFFu; int steal = 0;
        for (int i = 0; i < kVoices; ++i)
        {
            if (!voices_[i].active) { slot = i; break; }
            if (voices_[i].age < oldestAge) { oldestAge = voices_[i].age; steal = i; }
        }
        if (slot < 0) slot = steal;

        float pan = kGroupPan[grp] * width_;
        if (snd == sHatPedal) pan *= 0.9f;
        float gl, gr; equalPowerPan(pan, gl, gr);
        voices_[slot].trigger(snd, vel, vb, ratio, dec, level, extra, gl, gr);
        voices_[slot].age = ++ageCounter_;

        // sympathetic snare buzz from kick and toms
        if (grp == gKick || (grp >= gTom1 && grp <= gTom3))
            symEnv_ = std::fmax(symEnv_, (grp == gKick ? 0.10f : 0.16f) * vel * level);
    }

    void renderVoices(float* L, float* R, int from, int to)
    {
        if (to <= from) return;
        for (int i = from; i < to; ++i) sendBuf_[i] = 0.0f;
        for (auto& v : voices_)
        {
            if (!v.active) continue;
            v.blockUpdate(to - from);
            const float gl = v.gl, gr = v.gr, s = v.send;
            for (int i = from; i < to && v.active; ++i)
            {
                const float y = v.process();
                L[i] += y * gl; R[i] += y * gr;
                sendBuf_[i] += y * s;
            }
        }
        // snare wires rattling in sympathy (panned with the snare)
        if (symEnv_ > 1e-4f && wires_ > 0.0f)
        {
            float gl, gr; equalPowerPan(kGroupPan[gSnare] * width_, gl, gr);
            const float a = wires_ * snareLevel_;
            for (int i = from; i < to; ++i)
            {
                const float n = symLp_.process(symHp_.process(symNoise_.process())) * symEnv_ * a;
                symEnv_ *= symCoef_;
                L[i] += n * gl; R[i] += n * gr; sendBuf_[i] += n;
            }
        }
    }

    void addPresets()
    {
        // Values in parameter order (see ParamIndex).
        //                Mstr  Kick: T    D    L    Bt   Snare: T   D    L    W    Tom1 T  D  L   Tom2 T  D  L   Tom3 T  D  L   Hat T  D  L   Crash T D L   Ride T D L   Room Size Drv Comp Tone Wid Hum Vel
        addProgram("Iron Doom",     { -4,   0,  45,  85,  40,   0,  50,  80,  70,   0, 55, 78,   0, 55, 78,   0, 55, 78,   0, 50, 55,   0, 60, 55,   0, 55, 55,   30,  55,  25,  30, -15,  80,  35,  80 });
        addProgram("Funeral March", { -4,  -3,  75,  88,  20,  -3,  70,  78,  60,  -2, 80, 80,  -2, 85, 82,  -3, 90, 82,  -2, 70, 45,  -2, 85, 60,  -2, 80, 50,   55,  90,  20,  35, -45,  90,  30,  75 });
        addProgram("Desert Fuzz",   { -5,   1,  35,  85,  65,   1,  40,  82,  80,   0, 45, 76,   0, 45, 76,   0, 50, 76,   1, 40, 60,   0, 50, 55,   1, 45, 60,   25,  40,  60,  55,   5,  75,  40,  80 });
        addProgram("Sludge Crush",  { -6,  -2,  50,  90,  55,  -1,  55,  85,  85,  -1, 60, 80,  -1, 65, 80,  -2, 70, 80,  -1, 55, 50,  -1, 70, 55,  -1, 60, 50,   45,  65,  80,  85, -30,  85,  40,  70 });
        addProgram("Garage Live",   { -4,   2,  40,  82,  50,   2,  45,  80,  75,   1, 50, 76,   1, 50, 76,   1, 55, 76,   2, 45, 60,   1, 55, 60,   2, 50, 60,   60,  45,  35,  25,  10,  70,  50,  85 });
        addProgram("Dry Studio",    { -4,   0,  40,  85,  45,   0,  45,  80,  70,   0, 50, 78,   0, 50, 78,   0, 55, 78,   0, 45, 55,   0, 55, 55,   0, 50, 55,   10,  35,   0,   0,   0,  80,  25,  85 });
    }

    double sr_ = 44100.0;
    Voice voices_[kVoices];
    uint32_t ageCounter_ = 0;
    Xorshift32 rng_{0x51ABBA7u};
    Room room_;
    float sendBuf_[kMaxBlock] = {};

    // sympathetic snare buzz
    WhiteNoise symNoise_;
    OnePoleHP symHp_;
    OnePoleLP symLp_;
    float symEnv_ = 0.0f, symCoef_ = 0.99f;

    // bus state
    float roomLevel_ = 0.0f, width_ = 1.0f, wires_ = 0.7f, snareLevel_ = 0.8f;
    bool compOn_ = false, driveOn_ = false;
    float compThDb_ = 0.0f, compMakeup_ = 1.0f, compEnv_ = 0.0f, compGain_ = 1.0f, compTarget_ = 1.0f;
    float compAtt_ = 0.1f, compRel_ = 0.001f, compSmooth_ = 0.01f;
    float driveG_ = 1.0f, driveMk_ = 1.0f;
    float toneLo_ = 1.0f, toneHi_ = 1.0f;
    OnePoleLP toneL_, toneR_;
    Smoother master_;
};

MPC_PLUGIN_ENTRY(SabbathKit)
