// Sabbath Kit 2 - a sampled 70s rock drum kit for doom / stoner / sludge, with built-in grooves.
// By Gonzi Audio.
//
// SPDX-License-Identifier: MIT  (this code)
// Samples: "Red Zeppelin" kit from AVL Drumkits by Glen MacArthur, CC BY-SA 3.0 (see Content/LICENSE-samples.txt)
//
// SIGNAL FLOW
//
//   MPC pads / sequence (MIDI) ──┐
//                                ├─► note map ─► voice pool (24 sample voices) ─► pan ─► dry mix ─┬──────────────► + ─►
//   built-in groove player ──────┘   (GM)          5 velocity layers per piece      room send ─► 4-line FDN room ─┘
//   (follows the MPC sequencer, or
//    starts from a pad)                 mix ─► glue compressor (Comp) ─► tape saturation (Drive) ─► tilt EQ (Tone) ─► Master
//
// SAMPLES: 26" Ludwig kick, Porkpie snare (centre + edge, cross-stick), Ludwig 14" / 16" / 18" toms,
// Sabian Rock hats, Zildjian A 20" and K 17" crashes, Paiste 2002 22", Zildjian A 24" ride (bow, bell,
// shank), tambourine, cowbell, clap. 44.1 kHz mono, loaded once from Content/Samples and shared by
// every instance (16-bit in memory, ~24 MB).
//
// NOTE MAP (General MIDI, laid out for the MPC "Classic" pads)
//   35/36 Kick   37 Cross-stick   38 Snare   39 Clap   40 Snare edge (rimshot-ish)
//   48/50 Tom 1 (14")   47 Tom 1 edge   45 Tom 2 (16" floor)   43/41 Tom 3 (18" floor)
//   42 Closed hat   44 Pedal hat   46 Open hat (choked by 42/44)
//   49 Crash A20   57 Crash K17   52/55 Crash Paiste 2002 22"   51/58 Ride   53 Ride bell   59 Ride shank (wash)
//   54/82 Tambourine   56 Cowbell
//   Other notes fold by octaves into 35..59. In Groove Mode "Pad": note 60 starts/stops the groove,
//   note 61 plays a fill on the next bar. This map is used when Pad Map = GM (for MIDI files).
//   Pad Map = C2 / C1 (default C2): a 16-pad layout instead, see kPadLayout below.
//
// CPU: one interpolated sample read per voice per sample; voices end with their sample or their decay.

#include "mpc_plugin.h"
#include "dsp/math.h"
#include "dsp/noise.h"
#include "dsp/onepole.h"
#include "dsp/smoother.h"
#include "dsp/wav.h"
#include "grooves.h"

#include <algorithm>
#include <mutex>
#include <vector>

using namespace mpc::dsp;

namespace {

// ---------------------------------------------------------------------------
// Parameters. ORDER AND IDS ARE PERMANENT (v1 had 0..34): only append at the end.
// ---------------------------------------------------------------------------
enum ParamIndex
{
    kMaster = 0,
    kKickTune, kKickDecay, kKickLevel, kKickBeater,    // Beater: soft felt (0) .. hard click (100)
    kSnareTune, kSnareDecay, kSnareLevel, kSnareWires, // Wires: dull (0) .. crisp/bright (100)
    kTom1Tune, kTom1Decay, kTom1Level,
    kTom2Tune, kTom2Decay, kTom2Level,
    kTom3Tune, kTom3Decay, kTom3Level,
    kHatTune, kHatDecay, kHatLevel,
    kCrashTune, kCrashDecay, kCrashLevel,
    kRideTune, kRideDecay, kRideLevel,
    kRoom, kRoomSize, kDrive, kComp, kTone, kWidth, kHumanize, kVelSens,
    // --- v2 ---
    kGroove,      // Off / one of the built-in grooves
    kGrooveMode,  // Seq: plays while the MPC sequencer plays, locked to its tempo; Pad: note 60 starts/stops
    kFill,        // Off / every 4 bars / every 8 bars
    kGrooveVel,   // % dynamics of the groove
    // --- v2.1 ---
    kGroovePlay,  // On: the groove plays by itself (no need to press Play on the MPC). Groove Off + Play = a random groove
    kPadMap,      // which notes play which drum: MPC (default) = the MPC's own pad scale, C2 / C1 = 16 chromatic pads from note 48 / 36, GM = General MIDI
    kNumParams
};

enum Group { gKick = 0, gSnare, gTom1, gTom2, gTom3, gHat, gCrash, gRide, kNumGroups };

// Kit pieces = sample sets (5 velocity layers each).
enum Piece { pKick = 0, pSnare, pSnareEdge, pXStick, pClap, pTom1, pTom1Edge, pTom2, pTom3,
             pHatClosed, pHatPedal, pHatOpen, pCrashA20, pCrashK17, pCrash2002,
             pRide, pRideBell, pRideShank, pTamb, pCowbell, kNumPieces };

struct PieceInfo { const char* file; Group group; float pan; float gain; };
// pan from the drummer's seat (right-handed kit). gain balances the peak-normalised samples.
const PieceInfo kPieces[kNumPieces] = {
    { "kick",       gKick,   0.00f, 1.00f },
    { "snare",      gSnare, -0.15f, 1.30f },
    { "snare_edge", gSnare, -0.15f, 1.30f },
    { "xstick",     gSnare, -0.15f, 0.90f },
    { "clap",       gSnare,  0.00f, 0.50f },
    { "tom1",       gTom1,  -0.05f, 0.78f },
    { "tom1_edge",  gTom1,  -0.05f, 0.78f },
    { "tom2",       gTom2,   0.35f, 0.82f },
    { "tom3",       gTom3,   0.55f, 0.85f },
    { "hat_closed", gHat,   -0.50f, 0.62f },
    { "hat_pedal",  gHat,   -0.45f, 0.60f },
    { "hat_open",   gHat,   -0.50f, 0.55f },
    { "crash_a20",  gCrash, -0.55f, 0.48f },
    { "crash_k17",  gCrash,  0.30f, 0.45f },
    { "crash_2002", gCrash, -0.25f, 0.50f },
    { "ride",       gRide,   0.50f, 0.58f },
    { "ride_bell",  gRide,   0.50f, 0.50f },
    { "ride_shank", gRide,   0.50f, 0.45f },
    { "tambourine", gHat,    0.70f, 0.30f },
    { "cowbell",    gRide,  -0.60f, 0.32f },
};
const float kGroupSend[kNumGroups]  = { 0.50f, 1.0f, 0.95f, 0.95f, 0.95f, 0.50f, 0.60f, 0.50f };
const int   kMaxOverlap[kNumGroups] = { 2, 2, 2, 2, 2, 3, 3, 3 };
const float kMaxDecayMs[kNumGroups] = { 2500.0f, 1400.0f, 1800.0f, 2600.0f, 2000.0f, 900.0f, 9000.0f, 12000.0f };
const int kTuneIdx[kNumGroups]  = { kKickTune, kSnareTune, kTom1Tune, kTom2Tune, kTom3Tune, kHatTune, kCrashTune, kRideTune };
const int kDecayIdx[kNumGroups] = { kKickDecay, kSnareDecay, kTom1Decay, kTom2Decay, kTom3Decay, kHatDecay, kCrashDecay, kRideDecay };
const int kLevelIdx[kNumGroups] = { kKickLevel, kSnareLevel, kTom1Level, kTom2Level, kTom3Level, kHatLevel, kCrashLevel, kRideLevel };

inline float t60Coef(float ms, double sr)
{
    ms = ms < 0.5f ? 0.5f : ms;
    return std::exp(-6.9078f / (0.001f * ms * (float)sr));
}

int pieceForNote(int n)
{
    if (n == 82) return pTamb;
    while (n < 35) n += 12;
    while (n > 59) n -= 12;
    switch (n)
    {
        case 35: case 36: return pKick;
        case 37: return pXStick;
        case 38: return pSnare;
        case 39: return pClap;
        case 40: return pSnareEdge;
        case 41: case 43: return pTom3;
        case 45: return pTom2;
        case 47: return pTom1Edge;
        case 48: case 50: return pTom1;
        case 42: return pHatClosed;
        case 44: return pHatPedal;
        case 46: return pHatOpen;
        case 49: return pCrashA20;
        case 57: return pCrashK17;
        case 52: case 55: return pCrash2002;
        case 51: case 58: return pRide;
        case 53: return pRideBell;
        case 59: return pRideShank;
        case 54: return pTamb;
        case 56: return pCowbell;
        default: return pSnare;
    }
}

// 16-pad layout (Pad Map C2 / C1): pad 1 is bottom-left, rows of four going up, like the MPC's pads.
//   13 Crash A20   14 Crash 2002   15 Crash K17   16 Ride shank (wash)
//    9 Snare edge  10 Cross-stick  11 Ride        12 Ride bell
//    5 Tom 3       6 Tom 2         7 Tom 1         8 Pedal hat
//    1 Kick        2 Snare         3 Closed hat    4 Open hat
// The layout repeats every 16 notes, so any 16 pads in a row always hold the whole kit.
const int kPadLayout[16] = { pKick, pSnare, pHatClosed, pHatOpen, pTom3, pTom2, pTom1, pHatPedal,
                             pSnareEdge, pXStick, pRide, pRideBell, pCrashA20, pCrash2002, pCrashK17, pRideShank };
inline int pieceForPad(int note, int base) { return kPadLayout[(((note - base) % 16) + 16) % 16]; }

// Pad Map MPC (default): made for the MPC's own pad setup on a plugin track (Pad Perform: Notes, C major, or
// Chromatic). Every C is a kick and every D a snare; the other white keys alternate between two sets from one
// octave to the next, so 16 pads in a row always hold the kick on pad 1, the snare on pad 2 and the whole kit.
//   octave A:  C kick  D snare       E closed hat  F open hat   G tom 3      A tom 2       B tom 1
//   octave B:  C kick  D snare edge  E ride        F ride bell  G crash A20  A crash 2002  B pedal hat
//   black keys: C# cross-stick, D# clap, F# tambourine, G# cowbell, A# crash K17 / ride shank
inline int pieceForScale(int note)
{
    static const int a[12] = { pKick, pXStick, pSnare,     pClap, pHatClosed, pHatOpen,  pTamb, pTom3,     pCowbell, pTom2,      pCrashK17,  pTom1 };
    static const int b[12] = { pKick, pXStick, pSnareEdge, pClap, pRide,      pRideBell, pTamb, pCrashA20, pCowbell, pCrash2002, pRideShank, pHatPedal };
    if (note < 0) note = 0;
    return ((note / 12) & 1) ? b[note % 12] : a[note % 12];
}

const int kInternal = 1000;   // added to the note of groove events: those are always General MIDI

// ---------------------------------------------------------------------------
// Sample bank: loaded once (first prepare()), shared by all instances, never freed while loaded.
// ---------------------------------------------------------------------------
struct Sample { std::vector<int16_t> d; float rate = 44100.0f; float norm = 1.0f; };

struct Bank
{
    Sample s[kNumPieces][5];
    bool fromFiles = true;

    void load()
    {
        WhiteNoise nz(12345);
        for (int p = 0; p < kNumPieces; ++p)
            for (int l = 0; l < 5; ++l)
            {
                Sample& smp = s[p][l];
                ::dsp::WavData w;
                char rel[96];
                std::snprintf(rel, sizeof rel, "Samples/%s_%d.wav", kPieces[p].file, l + 1);
                if (::dsp::loadWav(mpc::contentPath(rel), w) && w.numFrames() > 4)
                {
                    const std::vector<float>& c = w.channels[0];
                    smp.d.resize(c.size() + 2, 0);
                    float pk = 1e-6f;
                    for (size_t i = 0; i < c.size(); ++i)
                    {
                        smp.d[i] = (int16_t)clamp((int)std::lround(c[i] * 32767.0f), -32768, 32767);
                        pk = std::fmax(pk, std::fabs(c[i]));
                    }
                    smp.rate = (float)w.sampleRate;
                    smp.norm = 1.0f / pk / 32768.0f;
                }
                else
                {   // missing file: a plain generated hit so the plugin still makes sound
                    fromFiles = false;
                    mpc::log("Sabbath Kit: missing %s, using a generated fallback", rel);
                    const int n = 44100 / 3;
                    smp.d.resize((size_t)n + 2, 0);
                    float ph = 0.0f;
                    for (int i = 0; i < n; ++i)
                    {
                        const float t = (float)i / 44100.0f;
                        float v = p == pKick ? std::sin(ph) * std::exp(-t * 9.0f)
                                             : nz.process() * std::exp(-t * (kPieces[p].group >= gHat ? 14.0f : 22.0f));
                        ph += 2.0f * kPi * (55.0f + 60.0f * std::exp(-t * 40.0f)) / 44100.0f;
                        smp.d[(size_t)i] = (int16_t)(v * 30000.0f);
                    }
                    smp.rate = 44100.0f; smp.norm = 1.0f / 30000.0f;
                }
            }
    }
};

Bank& bank() { static Bank b; return b; }
std::once_flag gBankOnce;

// ---------------------------------------------------------------------------
// One sample voice
// ---------------------------------------------------------------------------
struct Voice
{
    bool active = false;
    int piece = 0, group = 0;
    uint32_t age = 0;
    const int16_t* d = nullptr;
    int len = 0;
    float pos = 0.0f, rate = 1.0f;
    float gain = 0.0f, gl = 0.7f, gr = 0.7f, send = 0.5f;
    float env = 1.0f, envCoef = 1.0f;     // Decay knob
    float fade = 1.0f, fadeCoef = 1.0f;   // choke / steal
    float bright = 0.0f, atk = 0.0f, atkCoef = 1.0f, lpA = 0.3f, lpZ = 0.0f; // Beater / Wires tone shaping

    inline float process()
    {
        const int i = (int)pos;
        const float fr = pos - (float)i;
        float y = ((float)d[i] + fr * (float)(d[i + 1] - d[i])) * gain;
        if (bright != 0.0f)
        {
            lpZ += lpA * (y - lpZ);
            y += (y - lpZ) * bright * atk;     // boost/cut the highs (only the attack for the kick beater)
            atk *= atkCoef;
        }
        pos += rate;
        y *= env * fade;
        env *= envCoef; fade *= fadeCoef;
        if (pos >= (float)(len - 1) || env * fade < 1e-4f) active = false;
        return y;
    }
};

// ---------------------------------------------------------------------------
// Room: 4-line feedback delay network with damping.
// ---------------------------------------------------------------------------
struct Room
{
    std::vector<float> buf[4];
    int len[4] = {1, 1, 1, 1}, pos[4] = {0, 0, 0, 0};
    float g[4] = {0, 0, 0, 0}, lp[4] = {0, 0, 0, 0};
    float damp = 0.5f;
    double sr = 44100.0;
    int maxLen = 1;
    float lastSize = -1.0f;
    OnePoleHP hpIn;

    void prepare(double s)
    {
        sr = s;
        maxLen = (int)(0.090 * sr) + 8;
        for (auto& b : buf) b.assign((size_t)maxLen, 0.0f);
        hpIn.setCutoff(120.0f, sr);
        lastSize = -1.0f;
        reset();
    }
    void reset()
    {
        for (auto& b : buf) std::fill(b.begin(), b.end(), 0.0f);
        for (int i = 0; i < 4; ++i) { pos[i] = 0; lp[i] = 0.0f; }
        hpIn.reset();
    }
    void setSize(float size01)
    {
        if (std::fabs(size01 - lastSize) < 1e-4f) return;
        lastSize = size01;
        static const float baseMs[4] = { 23.1f, 29.7f, 34.3f, 41.9f };
        const float scale = lerp(0.45f, 2.0f, size01);
        const float rt60 = lerp(0.35f, 2.4f, size01);
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
        const float a = d[0] + d[1], b = d[0] - d[1], c = d[2] + d[3], e = d[2] - d[3];
        const float m[4] = { (a + c) * 0.5f, (b + e) * 0.5f, (a - c) * 0.5f, (b - e) * 0.5f };
        static const float sign[4] = { 1.0f, -1.0f, 1.0f, -1.0f };
        for (int i = 0; i < 4; ++i)
        {
            lp[i] += damp * (m[i] * g[i] - lp[i]);
            buf[i][(size_t)pos[i]] = flushDenormal(lp[i] + in * sign[i]);
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
        i.version = 2200;
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
        addParam(floatParam("kick_decay",  "Kick Decay",   0.0f, 100.0f, 100.0f, "%", 0));
        addParam(floatParam("kick_level",  "Kick Level",   0.0f, 100.0f, 85.0f, "%", 0));
        addParam(floatParam("kick_beater", "Kick Beater",  0.0f, 100.0f, 50.0f, "%", 0));
        addParam(floatParam("snare_tune",  "Snare Tune", -12.0f, 12.0f, 0.0f, "st", 1));
        addParam(floatParam("snare_decay", "Snare Decay",  0.0f, 100.0f, 100.0f, "%", 0));
        addParam(floatParam("snare_level", "Snare Level",  0.0f, 100.0f, 80.0f, "%", 0));
        addParam(floatParam("snare_wires", "Snare Wires",  0.0f, 100.0f, 50.0f, "%", 0));
        const char* tomIds[3]  = { "tom1", "tom2", "tom3" };
        const char* tomName[3] = { "Tom1", "Tom2", "Tom3" };
        for (int t = 0; t < 3; ++t)
        {
            std::string id(tomIds[t]), nm(tomName[t]);
            addParam(floatParam((id + "_tune").c_str(),  (nm + " Tune").c_str(),  -12.0f, 12.0f, 0.0f, "st", 1));
            addParam(floatParam((id + "_decay").c_str(), (nm + " Decay").c_str(),   0.0f, 100.0f, 100.0f, "%", 0));
            addParam(floatParam((id + "_level").c_str(), (nm + " Level").c_str(),   0.0f, 100.0f, 78.0f, "%", 0));
        }
        addParam(floatParam("hat_tune",    "Hat Tune",   -12.0f, 12.0f, 0.0f, "st", 1));
        addParam(floatParam("hat_decay",   "Hat Decay",    0.0f, 100.0f, 100.0f, "%", 0));
        addParam(floatParam("hat_level",   "Hat Level",    0.0f, 100.0f, 70.0f, "%", 0));
        addParam(floatParam("crash_tune",  "Crash Tune", -12.0f, 12.0f, 0.0f, "st", 1));
        addParam(floatParam("crash_decay", "Crash Decay",  0.0f, 100.0f, 100.0f, "%", 0));
        addParam(floatParam("crash_level", "Crash Level",  0.0f, 100.0f, 70.0f, "%", 0));
        addParam(floatParam("ride_tune",   "Ride Tune",  -12.0f, 12.0f, 0.0f, "st", 1));
        addParam(floatParam("ride_decay",  "Ride Decay",   0.0f, 100.0f, 100.0f, "%", 0));
        addParam(floatParam("ride_level",  "Ride Level",   0.0f, 100.0f, 70.0f, "%", 0));
        addParam(floatParam("room",      "Room",      0.0f, 100.0f, 30.0f, "%", 0));
        addParam(floatParam("room_size", "Room Size", 0.0f, 100.0f, 55.0f, "%", 0));
        addParam(floatParam("drive",     "Drive",     0.0f, 100.0f, 20.0f, "%", 0));
        addParam(floatParam("comp",      "Comp",      0.0f, 100.0f, 30.0f, "%", 0));
        addParam(floatParam("tone",      "Tone",   -100.0f, 100.0f, -10.0f, "", 0));
        addParam(floatParam("width",     "Width",     0.0f, 100.0f, 80.0f, "%", 0));
        addParam(floatParam("humanize",  "Humanize",  0.0f, 100.0f, 35.0f, "%", 0));
        addParam(floatParam("vel_sens",  "Vel Sens",  0.0f, 100.0f, 80.0f, "%", 0));
        // v2
        grooves_ = sabbath::parseAll();
        std::vector<std::string> names{ "Off" };
        for (const auto& g : grooves_) names.push_back(g.name);
        addParam(choiceParam("groove", "Groove", names, 0));
        addParam(choiceParam("groove_mode", "Groove Mode", { "Seq", "Pad" }, 0));
        addParam(choiceParam("groove_fill", "Fill", { "Off", "4 Bars", "8 Bars" }, 1));
        addParam(floatParam("groove_vel", "Groove Vel", 0.0f, 100.0f, 80.0f, "%", 0));
        // v2.1
        addParam(boolParam("groove_play", "Play", false));
        addParam(choiceParam("pad_map", "Pad Map", { "C2", "C1", "GM", "MPC" }, 3));
        if (numParams() != kNumParams) mpc::log("SabbathKit: parameter count mismatch");
        addPresets();
    }

    void prepare(double sampleRate, int /*maxBlockSize*/) override
    {
        std::call_once(gBankOnce, [] { bank().load(); });
        sr_ = sampleRate;
        room_.prepare(sampleRate);
        compAtt_ = 1.0f - std::exp(-1.0f / (0.003f * (float)sampleRate));
        compRel_ = 1.0f - std::exp(-1.0f / (0.160f * (float)sampleRate));
        compSmooth_ = 1.0f - std::exp(-1.0f / (0.002f * (float)sampleRate));
        lpA_ = 1.0f - std::exp(-2.0f * kPi * 2500.0f / (float)sampleRate);
        master_.setTime(15.0f, sampleRate);
        reset();
    }

    void reset() override
    {
        for (auto& v : voices_) v.active = false;
        room_.reset();
        compEnv_ = 0.0f; compGain_ = 1.0f; compTarget_ = 1.0f;
        toneL_.reset(); toneR_.reset();
        numPending_ = 0; padRunning_ = false; padPpq_ = 0.0; padFillBar_ = -1; play_ = false;
        seqLastEnd_ = -1.0;
        master_.snap(dbToGain(get(kMaster)));
    }

    void process(mpc::ProcessContext& ctx) override
    {
        readBlockParams();

        // ---- collect this block's notes: host MIDI + groove (+ notes delayed from earlier blocks)
        numEv_ = 0;
        for (int e = 0; e < ctx.numMidiEvents && numEv_ < kMaxEv; ++e)
        {
            const mpc::MidiEvent& ev = ctx.midi[e];
            if (ev.isNoteOn())
            {
                if (grooveMode_ == 1 && padMap_ == 2 && (ev.note() == 60 || ev.note() == 61)) { padControl(ev.note(), ctx); continue; }
                pushEv(ev.sampleOffset, ev.note(), ev.velocity());
            }
            else if (ev.isController() && (ev.controller() == 120 || ev.controller() == 123))
                pushEv(ev.sampleOffset, -1, 0);   // all sound off
        }
        runGroove(ctx);
        drainPending(ctx.numFrames);
        for (int i = 1; i < numEv_; ++i)   // insertion sort by offset (no allocation; few events)
        {
            const Ev key = ev_[i]; int j = i - 1;
            while (j >= 0 && ev_[j].offset > key.offset) { ev_[j + 1] = ev_[j]; --j; }
            ev_[j + 1] = key;
        }

        // ---- render in chunks of at most kMaxBlock frames
        int e = 0;
        for (int off = 0; off < ctx.numFrames; off += kMaxBlock)
        {
            const int n = std::min(kMaxBlock, ctx.numFrames - off);
            float* L = ctx.outputs[0] + off;
            float* R = ctx.outputs[1] + off;
            int pos = 0;
            for (; e < numEv_ && ev_[e].offset < off + n; ++e)
            {
                const int at = clamp(ev_[e].offset - off, 0, n);
                if (at > pos) { renderVoices(L, R, pos, at); pos = at; }
                if (ev_[e].note < 0) { for (auto& v : voices_) if (v.active) v.fadeCoef = t60Coef(30.0f, sr_); }
                else trigger(ev_[e].note, ev_[e].vel);
            }
            renderVoices(L, R, pos, n);
            processBus(L, R, n);
        }
    }

private:
    static const int kVoices = 24;
    static const int kMaxBlock = 4096;
    static const int kMaxEv = 1200;
    static const int kMaxPending = 128;

    struct Ev { int offset; int note; int vel; };
    struct Pending { int delay; int note; int vel; };

    void pushEv(int offset, int note, int vel)
    {
        if (numEv_ < kMaxEv) ev_[numEv_++] = { offset, note, vel };
    }

    void readBlockParams()
    {
        room_.setSize(get(kRoomSize) * 0.01f);
        roomLevel_ = get(kRoom) * 0.01f; roomLevel_ = roomLevel_ * roomLevel_ * 1.6f;
        width_ = get(kWidth) * 0.01f;
        const float comp = get(kComp) * 0.01f;
        compOn_ = comp > 0.001f; compThDb_ = -30.0f * comp; compMakeup_ = dbToGain(-compThDb_ * 0.45f);
        const float drive = get(kDrive) * 0.01f;
        driveG_ = 1.0f + 9.0f * drive * drive; driveMk_ = 1.0f / std::pow(driveG_, 0.75f); driveOn_ = drive > 0.001f;
        const float tone = get(kTone) * 0.01f;
        toneLo_ = dbToGain(-5.0f * tone); toneHi_ = dbToGain(6.0f * tone);
        toneL_.setCutoff(900.0f, sr_); toneR_.setCutoff(900.0f, sr_);
        master_.setTarget(dbToGain(get(kMaster)));
        groove_ = getInt(kGroove);
        grooveMode_ = getInt(kGrooveMode);
        fillEvery_ = getInt(kFill) == 0 ? 0 : (getInt(kFill) == 1 ? 4 : 8);
        grooveVel_ = get(kGrooveVel) * 0.01f;
        hum_ = get(kHumanize) * 0.01f;
        padMap_ = getInt(kPadMap);
        const bool play = getBool(kGroovePlay);
        if (play && !play_)
        {   // Play just switched on: start from bar 1; with Groove on Off, pick a groove at random
            padPpq_ = 0.0; padFillBar_ = -1;
            const int n = (int)grooves_.size();
            int g = 1 + (int)(rng_.next() % (uint32_t)n);
            if (g == randomGroove_) g = 1 + (g % n);
            randomGroove_ = g;
        }
        play_ = play;
        if (groove_ == 0 && play_) groove_ = randomGroove_;
    }

    // ---------------------------------------------------------------- groove player
    double hostBpm(const mpc::ProcessContext& ctx) const
    {
        return (ctx.transport.valid && ctx.transport.tempoValid && ctx.transport.bpm > 20.0) ? ctx.transport.bpm : 70.0;
    }

    void padControl(int note, const mpc::ProcessContext& ctx)
    {
        (void)ctx;
        if (note == 60)
        {
            padRunning_ = !padRunning_;
            padPpq_ = 0.0; padFillBar_ = -1;
        }
        else if (note == 61 && padRunning_)
            padFillBar_ = (int)std::floor(padPpq_ / 4.0) + 1;
    }

    void runGroove(const mpc::ProcessContext& ctx)
    {
        if (groove_ <= 0 || groove_ > (int)grooves_.size()) { seqLastEnd_ = -1.0; return; }
        const double bpm = hostBpm(ctx);
        const double beatsPerSample = bpm / (60.0 * sr_);
        const double span = beatsPerSample * ctx.numFrames;
        const mpc::Transport& tr = ctx.transport;
        const bool hostRolling = tr.valid && tr.playing && tr.ppqValid;
        if (play_ && !hostRolling)
        {   // Play switch, MPC stopped: free-running at the MPC's tempo
            seqLastEnd_ = -1.0;
            const double p0 = padPpq_, p1 = p0 + span;
            schedule(p0, p1, beatsPerSample, -1);
            padPpq_ = p1;
        }
        else if (grooveMode_ == 0 || play_)
        {
            const mpc::Transport& t = ctx.transport;
            if (!hostRolling) { seqLastEnd_ = -1.0; return; }
            double p0 = t.ppqPos;
            if (seqLastEnd_ >= 0.0 && std::fabs(p0 - seqLastEnd_) < 1e-3) p0 = seqLastEnd_;   // seamless blocks
            const double p1 = p0 + span;
            schedule(p0, p1, beatsPerSample, -1);
            seqLastEnd_ = p1;
        }
        else
        {
            if (!padRunning_) return;
            const double p0 = padPpq_, p1 = p0 + span;
            schedule(p0, p1, beatsPerSample, padFillBar_);
            padPpq_ = p1;
        }
    }

    // Schedule every groove event whose time (in beats) falls in [p0, p1).
    void schedule(double p0, double p1, double beatsPerSample, int forcedFillBar)
    {
        const sabbath::ParsedGroove& g = grooves_[(size_t)(groove_ - 1)];
        const int nb = (int)g.bars.size();
        const float velScale = lerp(0.55f, 1.05f, grooveVel_);
        const long b0 = (long)std::floor(p0 / 4.0), b1 = (long)std::floor((p1 - 1e-9) / 4.0);
        for (long bar = std::max(0L, b0); bar <= b1; ++bar)
        {
            const bool fill = (fillEvery_ > 0 && (bar % fillEvery_) == fillEvery_ - 1) || bar == forcedFillBar;
            const bool crashAfterFill = (fillEvery_ > 0 && bar > 0 && (bar % fillEvery_) == 0) || (forcedFillBar >= 0 && bar == forcedFillBar + 1);
            const int every = fillEvery_ > 0 ? fillEvery_ : 4;
            const std::vector<sabbath::Event>& evs = fill ? g.fills[(size_t)((bar / every) % (long)g.fills.size())] : g.bars[(size_t)(bar % nb)];
            bool crashAtZero = false;
            for (const auto& e : evs)
                if (e.tick == 0 && (e.note == 49 || e.note == 55 || e.note == 57)) crashAtZero = true;
            if (crashAfterFill && !crashAtZero) addGrooveHit(bar * 4.0, 49, 118, p0, p1, beatsPerSample, 1.0f);
            for (const auto& e : evs)
                addGrooveHit(bar * 4.0 + (double)e.tick / sabbath::kTicksPerBeat, e.note, e.vel, p0, p1, beatsPerSample, velScale);
        }
    }

    void addGrooveHit(double beat, int note, int vel, double p0, double p1, double beatsPerSample, float velScale)
    {
        if (beat < p0 || beat >= p1) return;
        int offset = (int)((beat - p0) / beatsPerSample);
        // feel: snare laid back a few ms, small random timing and dynamics (Humanize)
        float lateMs = rng_.nextFloat() * 5.0f * hum_;
        if (note == 38 || note == 40 || note == 37) lateMs += 3.0f + 5.0f * hum_;
        offset += (int)(lateMs * 0.001f * (float)sr_);
        const int v = clamp((int)std::lround((float)vel * velScale * (1.0f + rng_.nextBipolar() * 0.08f * hum_)), 1, 127);
        if (numPending_ < kMaxPending) pend_[numPending_++] = { offset, note + kInternal, v };
    }

    void drainPending(int frames)
    {
        int k = 0;
        for (int i = 0; i < numPending_; ++i)
        {
            if (pend_[i].delay < frames) pushEv(std::max(0, pend_[i].delay), pend_[i].note, pend_[i].vel);
            else { pend_[k] = pend_[i]; pend_[k].delay -= frames; ++k; }
        }
        numPending_ = k;
    }

    // ---------------------------------------------------------------- voices
    void trigger(int note, int velocity)
    {
        const int pc = note >= kInternal ? pieceForNote(note - kInternal)
                     : padMap_ == 3 ? pieceForScale(note) : padMap_ == 0 ? pieceForPad(note, 48) : padMap_ == 1 ? pieceForPad(note, 36) : pieceForNote(note);
        const PieceInfo& pi = kPieces[pc];
        const int grp = pi.group;
        const float sens = get(kVelSens) * 0.01f;
        const float v = clamp((float)velocity / 127.0f, 0.0f, 1.0f);

        // velocity layer (AVL zones 1-26-52-77-102-127); humanize sometimes picks a neighbour
        const float vl = lerp(0.8f, v, sens);
        int layer = vl * 127.0f < 27.0f ? 0 : vl * 127.0f < 53.0f ? 1 : vl * 127.0f < 78.0f ? 2 : vl * 127.0f < 103.0f ? 3 : 4;
        if (rng_.nextFloat() < 0.35f * hum_) layer = clamp(layer + (rng_.nextFloat() < 0.5f ? -1 : 1), 0, 4);
        const Sample& smp = bank().s[pc][layer];
        if (smp.d.size() < 4) return;

        const float loud = lerp(1.0f, std::pow(v, 1.7f), sens) * dbToGain(rng_.nextBipolar() * (0.2f + 1.3f * hum_));
        const float level = get(kLevelIdx[grp]) * 0.01f;
        const float cents = rng_.nextBipolar() * (1.0f + 9.0f * hum_);
        const float rate = smp.rate / (float)sr_ * semitonesToRatio(get(kTuneIdx[grp])) * centsToRatio(cents);
        const float dec = get(kDecayIdx[grp]) * 0.01f;

        // hi-hat choke
        if (pc == pHatClosed || pc == pHatPedal)
            for (auto& vo : voices_) if (vo.active && vo.piece == pHatOpen) vo.fadeCoef = t60Coef(30.0f, sr_);

        // too many overlapping hits of one piece: fade the oldest
        int count = 0, oldest = -1; uint32_t oldestAge = 0xFFFFFFFFu;
        for (int i = 0; i < kVoices; ++i)
            if (voices_[i].active && voices_[i].group == grp && voices_[i].fadeCoef >= 1.0f)
            {
                ++count;
                if (voices_[i].age < oldestAge) { oldestAge = voices_[i].age; oldest = i; }
            }
        if (count >= kMaxOverlap[grp] && oldest >= 0) voices_[oldest].fadeCoef = t60Coef(grp >= gHat ? 150.0f : 80.0f, sr_);

        int slot = -1, steal = 0; oldestAge = 0xFFFFFFFFu;
        for (int i = 0; i < kVoices; ++i)
        {
            if (!voices_[i].active) { slot = i; break; }
            if (voices_[i].age < oldestAge) { oldestAge = voices_[i].age; steal = i; }
        }
        if (slot < 0) slot = steal;

        Voice& vo = voices_[slot];
        vo.active = true; vo.piece = pc; vo.group = grp; vo.age = ++ageCounter_;
        vo.d = smp.d.data(); vo.len = (int)smp.d.size() - 1;
        vo.pos = 0.0f; vo.rate = rate;
        vo.gain = smp.norm * pi.gain * level * loud;
        float gl, gr; equalPowerPan(pi.pan * width_, gl, gr);
        vo.gl = gl; vo.gr = gr; vo.send = kGroupSend[grp];
        vo.env = 1.0f; vo.envCoef = dec >= 0.99f ? 1.0f : t60Coef(60.0f * std::pow(kMaxDecayMs[grp] / 60.0f, dec), sr_);
        vo.fade = 1.0f; vo.fadeCoef = 1.0f;
        vo.lpZ = 0.0f; vo.lpA = lpA_;
        if (grp == gKick)       { vo.bright = (get(kKickBeater) * 0.01f - 0.5f) * 3.0f; vo.atk = 1.0f; vo.atkCoef = t60Coef(60.0f, sr_); }
        else if (grp == gSnare) { vo.bright = (get(kSnareWires) * 0.01f - 0.5f) * 1.6f; vo.atk = 1.0f; vo.atkCoef = 1.0f; }
        else                    { vo.bright = 0.0f; }
        vo.bright = clamp(vo.bright, -0.9f, 2.0f);
    }

    void renderVoices(float* L, float* R, int from, int to)
    {
        if (to <= from) return;
        for (int i = from; i < to; ++i) sendBuf_[i] = 0.0f;
        for (auto& v : voices_)
        {
            if (!v.active) continue;
            const float gl = v.gl, gr = v.gr, s = v.send;
            for (int i = from; i < to && v.active; ++i)
            {
                const float y = v.process();
                L[i] += y * gl; R[i] += y * gr; sendBuf_[i] += y * s;
            }
        }
    }

    // ---------------------------------------------------------------- bus
    static inline float softLimit(float x)
    {
        const float t = 0.7f, a = std::fabs(x);
        if (a <= t) return x;
        return (x < 0.0f ? -1.0f : 1.0f) * (t + (1.0f - t) * fastTanh((a - t) / (1.0f - t)));
    }

    void processBus(float* L, float* R, int numFrames)
    {
        for (int i = 0; i < numFrames; ++i)
        {
            float l = L[i], r = R[i];
            float rl, rr;
            room_.process(sendBuf_[i], rl, rr);
            l += rl * roomLevel_; r += rr * roomLevel_;
            if (compOn_)
            {
                const float pk = std::fmax(std::fabs(l), std::fabs(r));
                compEnv_ += (pk > compEnv_ ? compAtt_ : compRel_) * (pk - compEnv_);
                if ((i & 15) == 0)
                {
                    const float over = gainToDb(compEnv_ + 1e-9f) - compThDb_;
                    compTarget_ = over > 0.0f ? dbToGain(-over * 0.75f) : 1.0f;
                }
                compGain_ += compSmooth_ * (compTarget_ - compGain_);
                const float g = compGain_ * compMakeup_;
                l *= g; r *= g;
            }
            if (driveOn_) { l = fastTanh(l * driveG_) * driveMk_; r = fastTanh(r * driveG_) * driveMk_; }
            const float lpL = toneL_.process(l), lpR = toneR_.process(r);
            l = lpL * toneLo_ + (l - lpL) * toneHi_;
            r = lpR * toneLo_ + (r - lpR) * toneHi_;
            const float m = master_.process();
            L[i] = sanitize(softLimit(l * m));
            R[i] = sanitize(softLimit(r * m));
        }
    }

    void addPresets()
    {
        // Values in parameter order (see ParamIndex). Last four: Groove, Groove Mode, Fill, Groove Vel.
        //                Mstr Kick: T  D    L   Bt  Snare: T  D    L   W   Tom1 T  D   L   Tom2 T  D   L   Tom3 T  D   L   Hat T  D   L   Crash T D  L   Ride T  D   L   Room Size Drv Comp Tone Wid Hum Vel  Grv Md Fl GVel
        addProgram("Iron Doom",     { -4,  0, 100, 85, 50,   0, 100, 80, 50,   0,100, 78,   0,100, 78,   0,100, 78,   0,100, 70,   0,100, 70,   0,100, 70,   30, 55, 20, 30, -10, 80, 35, 80,   0, 0, 1, 80 });
        addProgram("Funeral March", { -4, -2, 100, 88, 30,  -3, 100, 78, 40,  -3,100, 80,  -3,100, 82,  -3,100, 82,  -2,100, 60,  -2,100, 72,  -2,100, 62,   60, 90, 15, 35, -40, 90, 30, 75,   0, 0, 1, 80 });
        addProgram("Desert Fuzz",   { -5,  1,  85, 85, 65,   1,  90, 82, 65,   1, 90, 76,   1, 90, 76,   1, 90, 76,   1,100, 75,   1,100, 70,   1,100, 72,   25, 40, 55, 55,   5, 75, 40, 80,   0, 0, 1, 80 });
        addProgram("Sludge Crush",  { -6, -2, 100, 90, 60,  -2, 100, 85, 55,  -2,100, 80,  -2,100, 80,  -2,100, 80,  -1,100, 62,  -1,100, 72,  -1,100, 62,   45, 65, 75, 80, -25, 85, 40, 70,   0, 0, 1, 80 });
        addProgram("Garage Live",   { -4,  1,  90, 82, 55,   1,  95, 80, 60,   1, 95, 76,   1, 95, 76,   1, 95, 76,   1,100, 72,   1,100, 72,   1,100, 72,   60, 45, 30, 25,  10, 70, 50, 85,   0, 0, 1, 80 });
        addProgram("Dry Studio",    { -4,  0, 100, 85, 50,   0, 100, 80, 50,   0,100, 78,   0,100, 78,   0,100, 78,   0,100, 70,   0,100, 70,   0,100, 70,    5, 35,  0,  0,   0, 80, 25, 85,   0, 0, 1, 80 });
    }

    // ---------------------------------------------------------------- state
    double sr_ = 44100.0;
    Voice voices_[kVoices];
    uint32_t ageCounter_ = 0;
    Xorshift32 rng_{0x51ABBA7u};
    Room room_;
    float sendBuf_[kMaxBlock] = {};
    float lpA_ = 0.3f;

    std::vector<sabbath::ParsedGroove> grooves_;
    Ev ev_[kMaxEv];
    int numEv_ = 0;
    Pending pend_[kMaxPending];
    int numPending_ = 0;
    int groove_ = 0, grooveMode_ = 0, fillEvery_ = 4;
    float grooveVel_ = 0.8f, hum_ = 0.35f;
    double seqLastEnd_ = -1.0;
    bool padRunning_ = false;
    double padPpq_ = 0.0;
    int padFillBar_ = -1;
    bool play_ = false;
    int padMap_ = 3, randomGroove_ = 0;

    float roomLevel_ = 0.0f, width_ = 1.0f;
    bool compOn_ = false, driveOn_ = false;
    float compThDb_ = 0.0f, compMakeup_ = 1.0f, compEnv_ = 0.0f, compGain_ = 1.0f, compTarget_ = 1.0f;
    float compAtt_ = 0.1f, compRel_ = 0.001f, compSmooth_ = 0.01f;
    float driveG_ = 1.0f, driveMk_ = 1.0f;
    float toneLo_ = 1.0f, toneHi_ = 1.0f;
    OnePoleLP toneL_, toneR_;
    Smoother master_;
};

MPC_PLUGIN_ENTRY(SabbathKit)
