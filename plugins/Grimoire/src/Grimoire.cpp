// Grimoire - Gonzi Audio. A dark ambient / dungeon synth for the MPC, built from two FM-1 firmwares:
// GR!MM (GRiMMBIENT/grimm, on Dinghy and Felucca) and GHOULBOX (jasonpersinger/ghoulbox-fm1-dungeon-synth).
//
// SPDX-License-Identifier: GPL-3.0-only
// Copyright (C) 2026 Gonzi Audio. The sound engines and effects are GR!MM's, Felucca's, GHOULBOX's and msfa's
// (src/grimm/, see LICENSING.md). This program is free software under the GNU GPL, version 3 only.
//
// Signal flow (GR!MM's, grimm.c app_audio; its two "tracks" are Grimoire's layers A and B):
//
//   MIDI notes --> layer A: SOURCE (VA, FM, VOICE, WHEEL, PHYS, PHASE, GURDY) x ENV --> TAPE share --> FILTER
//              \-> layer B: the same, its own source and settings          |
//   each layer --> LEVEL, PAN --> mix                                        |
//              \-> sends: DELAY, SPACE (reverb), GRAIN, DRIVE  <-------------/
//   ambience (GHOULBOX: wind, crackle) --> mix and SPACE
//   GRAIN --> mix + SPACE;  DELAY --> mix + SPACE;  SPACE --> mix
//   mix --> MASTER filter (PRE / POST / END) --> DRIVE (+ limiter) --> TAPE hiss --> Master level --> limiter --> out
//
// GR!MM runs at 44.1 kHz in blocks of 256 frames (its effects are built on that). Grimoire renders it that way and
// hands the host its frames through a short FIFO (resampled when the host runs at another rate): 256 frames of
// latency, reported to the host, so every MIDI event lands on its own control step (32 frames) in time.

#include "mpc_plugin.h"
#include "grimm_core.h"

#include <cmath>
#include <cstdio>
#include <cstring>

namespace {

constexpr char kUid[] = "Gp4w";

// ---- parameter layout (PERMANENT once released: append only) --------------------------------------------------------
enum Global { kMaster = 0, kVoices, kVelSens, kBendRange, kNumGlobal };

enum LayerParam {
    L_SRC = 0, L_K1, L_K2, L_K3, L_K4, L_K5, L_K6, L_K7, L_K8,
    L_ATK, L_DEC, L_SUS, L_REL,
    L_LEVEL, L_PAN, L_CUT, L_RES, L_FTYPE, L_GLIDE, L_MONO,
    L_OCT, L_FINE,
    L_LFORATE, L_LFODEPTH, L_LFODEST, L_LFOSHAPE,
    L_DLY, L_REV, L_GRN, L_TAPE, L_DRV,
    L_COUNT
};
constexpr int kLayerA = kNumGlobal;
constexpr int kLayerB = kLayerA + L_COUNT;

enum FxParam {
    F_TWOW = 0, F_TSAT, F_TTONE, F_THISS,
    F_GSIZE, F_GDENS, F_GSCAT, F_GMIX, F_GFREEZE,
    F_DTIME, F_DFB, F_DWEAR, F_DMIX, F_DSYNC,
    F_SSIZE, F_SDAMP, F_SMIX, F_SPRE,
    F_MCUT, F_MRES, F_MTYPE, F_MPOS,
    F_DRAMT, F_DRTYPE, F_DRTONE, F_DRMIX,
    F_AMB, F_CRKL,
    F_COUNT
};
constexpr int kFx = kLayerB + L_COUNT;
static_assert(kFx + F_COUNT == 94, "the parameter count (permanent order)");

const std::vector<std::string> kSources = {"VA", "FM", "Voice", "Wheel", "Phys", "Phase", "Gurdy"};
const std::vector<std::string> kLfoDest = {"Pitch", "Volume", "Pan", "Knob 1", "Knob 2", "Knob 3", "Knob 4",
                                           "Rev Send", "Grain Send", "Cutoff"};
const std::vector<std::string> kLfoShape = {"Sine", "Triangle", "Saw Up", "Saw Down", "Square", "Random", "Drift"};
const std::vector<std::string> kFiltType = {"LP", "HP", "BP"};
const std::vector<std::string> kMasterPos = {"Pre FX", "Post Rev", "End"};
const std::vector<std::string> kDriveType = {"Soft", "Fuzz", "Clip", "Fold"};
// DELAY SYNC: off (TIME), then divisions of a 4/4 bar in quarter notes
const std::vector<std::string> kSync = {"Off", "1/32", "1/16T", "1/16", "1/8T", "1/16.", "1/8", "1/4T", "1/8.",
                                        "1/4", "1/2T", "1/4.", "1/2", "1/1"};
const double kSyncQ[] = {0, 0.125, 1.0 / 6, 0.25, 1.0 / 3, 0.375, 0.5, 2.0 / 3, 0.75, 1.0, 4.0 / 3, 1.5, 2.0, 4.0};

// The knobs past GR!MM's four: what they do for each source (empty: nothing)
const char* const kExtraName[grimm::NENG][4] = {
    {"", "", "", ""},                              // VA
    {"", "", "", ""},                              // FM
    {"TYPE", "", "", ""},                          // VOICE: SOLO / GREGORIAN / NUNS / CATHEDRAL
    {"", "", "", ""},                              // WHEEL
    {"", "", "", ""},                              // PHYS
    {"WAVE L2", "RING", "DCW ENV", "ENV DCY"},     // PHASE: line 2's wave, ring mod, DCW envelope, its decay
    {"BUZZ", "COUP", "BODY", "WOBBLE"},            // GURDY: its own 8 (K1..K4 above)
};
const int kExtraMax[grimm::NENG][4] = {
    {0, 0, 0, 0}, {0, 0, 0, 0}, {3, 0, 0, 0}, {0, 0, 0, 0}, {0, 0, 0, 0}, {8, 1, 127, 127}, {127, 4, 127, 127},
};
const char* const kVoiceType[4] = {"SOLO", "GREGORIAN", "NUNS", "CATHEDRAL"};
const char* const kPdWave2[9] = {"SAME", "SAW", "SQR", "PLS", "DSIN", "SPLS", "RSAW", "RTRI", "RTRP"};

int scaleKnob(int k, int max) { return max <= 0 ? 0 : (k * max + 63) / 127; }   // GR!MM's CC mapping (grimm.c)

// GR!MM's knob names, short enough for the MPC's value line
const char* shortName(const char* n)
{
    static const char* const map[][2] = {{"RESONANCE", "RESO"}, {"STRUCTURE", "STRUCT"}, {"DRAWBARS", "DRAW"},
                                         {"CUTOFF", "CUT"}, {"BRIGHT", "BRIGHT"}};
    for (const auto& m : map) if (!std::strcmp(n, m[0])) return m[1];
    return n;
}

} // namespace

class Grimoire : public mpc::Plugin
{
public:
    Grimoire() : mpc::Plugin(makeInfo())
    {
        // ---- global
        addParam(mpc::floatParam("master", "Master", -40.0f, 12.0f, 0.0f, "dB", 1));
        addParam(mpc::intParam("voices", "Voices", 1, 16, 8));                 // polyphony of each layer
        addParam(mpc::floatParam("velsens", "Vel Sens", 0.0f, 100.0f, 50.0f, "%", 0));
        addParam(mpc::intParam("bend", "Bend Range", 0, 24, 2, "st"));
        // ---- layers A and B
        for (int l = 0; l < 2; ++l)
        {
            const char* P = l ? "B" : "A";
            const char* p = l ? "b" : "a";
            auto id = [&](const char* s) { return std::string(p) + "_" + s; };
            auto nm = [&](const char* s) { return std::string(P) + " " + s; };
            auto I = [&](const char* i, const char* n, int lo, int hi, int def, const char* unit = "") {
                return mpc::intParam(id(i).c_str(), nm(n).c_str(), lo, hi, def, unit);
            };
            addParam(mpc::choiceParam(id("src").c_str(), nm("Source").c_str(), kSources, l ? 2 : 0));
            for (int k = 0; k < 8; ++k)
            {
                char i[8], n[12];
                std::snprintf(i, sizeof i, "k%d", k + 1);
                std::snprintf(n, sizeof n, "Knob %d", k + 1);
                addParam(I(i, n, 0, 127, k < 4 ? 64 : 0));   // (5..8: the extra knobs, off)
            }
            addParam(I("atk", "Attack", 0, 127, 40));
            addParam(I("dec", "Decay", 0, 127, 85));
            addParam(I("sus", "Sustain", 0, 127, 127));
            addParam(I("rel", "Release", 0, 127, 80));
            addParam(I("level", "Level", 0, 127, l ? 0 : 100));
            addParam(I("pan", "Pan", 0, 127, 64));
            addParam(I("cut", "Cutoff", 0, 127, 127));
            addParam(I("res", "Reso", 0, 127, 0));
            addParam(mpc::choiceParam(id("ftype").c_str(), nm("Filt Type").c_str(), kFiltType, 0));
            addParam(I("glide", "Glide", 0, 127, 0));
            addParam(mpc::boolParam(id("mono").c_str(), nm("Mono").c_str(), false));
            addParam(I("oct", "Octave", -3, 3, 0));
            addParam(I("fine", "Fine", -50, 50, 0, "ct"));
            addParam(I("lforate", "LFO Rate", 0, 127, 50));
            addParam(I("lfodepth", "LFO Depth", 0, 100, 0, "%"));
            addParam(mpc::choiceParam(id("lfodest").c_str(), nm("LFO Dest").c_str(), kLfoDest, 0));
            addParam(mpc::choiceParam(id("lfoshape").c_str(), nm("LFO Shape").c_str(), kLfoShape, 0));
            addParam(I("dly", "Delay Snd", 0, 127, 20));
            addParam(I("rev", "Reverb Snd", 0, 127, 70));
            addParam(I("grn", "Grain Snd", 0, 127, 0));
            addParam(I("tape", "Tape Snd", 0, 127, 0));
            addParam(I("drv", "Drive Snd", 0, 127, 0));
        }
        // ---- effects (GR!MM's FX pages, MASTER, DRIVE; GHOULBOX's ambience)
        auto F = [&](const char* i, const char* n, int def, int hi = 127) { return mpc::intParam(i, n, 0, hi, def); };
        addParam(F("t_wow", "Tape Wow", 0));
        addParam(F("t_sat", "Tape Sat", 0));
        addParam(F("t_tone", "Tape Tone", 0));
        addParam(F("t_hiss", "Tape Hiss", 0));
        addParam(F("g_size", "Grain Size", 80));
        addParam(F("g_dens", "Grain Dens", 80));
        addParam(F("g_scat", "Grain Scat", 30));
        addParam(F("g_mix", "Grain Mix", 100));
        addParam(mpc::boolParam("g_freeze", "Freeze", false));
        addParam(F("d_time", "Delay Time", 88));
        addParam(F("d_fb", "Delay Fdbk", 64));
        addParam(F("d_wear", "Delay Wear", 50));
        addParam(F("d_mix", "Delay Mix", 100));
        addParam(mpc::choiceParam("d_sync", "Delay Sync", kSync, 0));
        addParam(F("s_size", "Space Size", 80));
        addParam(F("s_damp", "Space Damp", 48));
        addParam(F("s_mix", "Space Mix", 100));
        addParam(F("s_pre", "Space Pre", 16));
        addParam(F("m_cut", "Mstr Cutoff", 127));
        addParam(F("m_res", "Mstr Reso", 0));
        addParam(mpc::choiceParam("m_type", "Mstr Type", kFiltType, 0));
        addParam(mpc::choiceParam("m_pos", "Mstr Pos", kMasterPos, 2));
        addParam(F("dr_amt", "Drive Amt", 0));
        addParam(mpc::choiceParam("dr_type", "Drive Type", kDriveType, 0));
        addParam(F("dr_tone", "Drive Tone", 127));
        addParam(F("dr_mix", "Drive Mix", 127));
        addParam(F("amb", "Ambience", 0));
        addParam(F("crkl", "Crackle", 0));

        addPrograms();

        grimm::grimm_new_parts(parts_);
        bind();
        grimm::gm_init();
        for (int t = 0; t < 2; ++t) lastPatch_[t] = -1;
    }

    ~Grimoire() override { grimm::grimm_free_parts(parts_); }

    void prepare(double sampleRate, int maxBlockSize) override
    {
        (void)maxBlockSize;
        hostRate_ = sampleRate > 0 ? sampleRate : 44100.0;
        step_ = 44100.0 / hostRate_;
        setLatency((int)std::lround(kLatency / step_));
        reset();
    }

    void reset() override
    {
        bind();
        grimm::grimm_reset_parts(parts_);
        grimm::gm_init();
        for (int t = 0; t < 2; ++t) lastPatch_[t] = -1;
        // the FIFO starts with the latency's silence: an event at host frame f is due at internal frame f
        fifoR_ = 0;
        fifoW_ = kLatency;
        std::memset(fifo_, 0, sizeof fifo_);
        readPos_ = 0.0;
        inFrames_ = 0;
        pushParams(nullptr);
        grimm::tapefx_poll();
    }

    void process(mpc::ProcessContext& ctx) override
    {
        bind();
        pushParams(&ctx);
        grimm::tapefx_poll();                   // TAPE SATURATION's curve when it moved (GR!MM's main loop does this)

        // MIDI into the core's queue, each event at its internal frame
        const double base = readPos_ + (double)inFrames_;   // internal frame of this block's first output frame
        for (int e = 0; e < ctx.numMidiEvents; ++e)
        {
            const mpc::MidiEvent& m = ctx.midi[e];
            const uint32_t at = (uint32_t)(int64_t)std::floor(base + m.sampleOffset * step_);
            if (m.isNoteOn()) grimm::gm_event(at, grimm::GE_ON, (uint32_t)m.note(), (uint32_t)m.velocity());
            else if (m.isNoteOff()) grimm::gm_event(at, grimm::GE_OFF, (uint32_t)m.note(), 0);
            else if (m.isAllNotesOff()) grimm::gm_event(at, grimm::GE_ALLOFF, 0, 0);
            else if (m.isController() && m.controller() == 64) grimm::gm_event(at, grimm::GE_SUS, 64, (uint32_t)m.controllerValue());
            else if (m.isController() && m.controller() == 1) grimm::gm_event(at, grimm::GE_MOD, 1, (uint32_t)m.controllerValue());
            else if (m.isPitchBend()) grimm::gm_event(at, grimm::GE_BEND, m.data[1], m.data[2]);
        }

        float* outL = ctx.outputs[0];
        float* outR = ctx.numOutputs > 1 ? ctx.outputs[1] : nullptr;
        const bool same = std::fabs(step_ - 1.0) < 1e-9;
        for (int i = 0; i < ctx.numFrames; ++i)
        {
            float l, r;
            if (same)
            {
                while (fifoW_ - fifoR_ < 1) render();
                const uint32_t k = fifoR_ & kFifoMask;
                l = fifo_[k][0], r = fifo_[k][1];
                ++fifoR_;
                inFrames_ += 1;
            }
            else
            {   // cubic (Hermite) between internal frames
                while (fifoW_ - fifoR_ < 4) render();
                const float f = (float)readPos_;
                const uint32_t k0 = fifoR_ & kFifoMask;
                const float* y0 = fifo_[k0];
                const float* y1 = fifo_[(k0 + 1) & kFifoMask];
                const float* y2 = fifo_[(k0 + 2) & kFifoMask];
                const float* y3 = fifo_[(k0 + 3) & kFifoMask];
                float o[2];
                for (int c = 0; c < 2; ++c)
                {
                    const float c1 = 0.5f * (y2[c] - y0[c]);
                    const float c2 = y0[c] - 2.5f * y1[c] + 2.0f * y2[c] - 0.5f * y3[c];
                    const float c3 = 0.5f * (y3[c] - y0[c]) + 1.5f * (y1[c] - y2[c]);
                    o[c] = ((c3 * f + c2) * f + c1) * f + y1[c];
                }
                l = o[0], r = o[1];
                readPos_ += step_;
                while (readPos_ >= 1.0) { readPos_ -= 1.0; ++fifoR_; inFrames_ += 1; }
            }
            outL[i] = l;
            if (outR) outR[i] = r;
        }
        // keep the internal frame count of the read position small: it only matters relative to gm_now
        (void)0;
    }

    bool formatValue(int index, float plain, std::string& out) override;

private:
    static constexpr uint32_t kLatency = 256;           // internal frames (one GR!MM block)
    static constexpr uint32_t kFifo = 2048, kFifoMask = kFifo - 1;

    void* parts_[GRIMM_NPARTS] = {};
    float fifo_[kFifo][2] = {};
    uint32_t fifoR_ = 0, fifoW_ = kLatency;
    double readPos_ = 0.0;      // fraction between fifoR_ and the next frame (resampling)
    int64_t inFrames_ = 0;      // internal frames read out so far (= the internal clock of the output)
    double hostRate_ = 44100.0, step_ = 1.0;
    int lastPatch_[2] = {-1, -1};
    int32_t outBuf_[2 * VB];

    void bind() { grimm::grimm_parts = parts_; }

    // one GR!MM block: 256 frames at 44.1 kHz into the FIFO
    void render()
    {
        grimm::app_audio(outBuf_, VB);
        const float k = 1.0f / (32768.0f * 128.0f);     // the 24-bit words (Q15 << 7) as floats
        for (uint32_t i = 0; i < VB; ++i)
        {
            float* d = fifo_[(fifoW_ + i) & kFifoMask];
            d[0] = (float)outBuf_[2 * i] * k;
            d[1] = (float)outBuf_[2 * i + 1] * k;
        }
        fifoW_ += VB;
    }

    // the parameters into GR!MM's knobs (they are read by the audio code at each block)
    void pushParams(const mpc::ProcessContext* ctx)
    {
        using namespace grimm;
        gm_master = (int32_t)std::lround(4096.0 * std::pow(10.0, get(kMaster) / 20.0));
        gm_poly = getInt(kVoices);
        gm_velsens = (int32_t)std::lround(get(kVelSens) * 1.27f);
        gm_bendr = getInt(kBendRange);
        for (uint32_t t = 0; t < NT; ++t)
        {
            const int b = t ? kLayerB : kLayerA;
            track_t& T = trks[t];
            const int eng = mpc::clamp(getInt(b + L_SRC), 0, (int)NENG - 1);
            const engine_t* E = ENGINES[eng];
            T.eng = eng;
            for (int k = 0; k < 4; ++k) T.p[eng][k] = scaleKnob(getInt(b + L_K1 + k), E->max[k]);
            for (int k = 4; k < 8; ++k) T.p[eng][k] = scaleKnob(getInt(b + L_K1 + k), kExtraMax[eng][k - 4]);
            if (eng == ENG_FM && T.p[eng][0] != lastPatch_[t])     // PATCH: GR!MM loads the built-in patch (fm_builtin)
            {
                fm_builtin(t, T.p[eng][0]);
                lastPatch_[t] = T.p[eng][0];
            }
            if (eng == ENG_VOICE) T.vty = T.p[eng][4];
            if (eng == ENG_PHASE)
            {
                T.pd_wav = T.p[eng][4], T.pd_ring = T.p[eng][5], T.pd_noise = 0, T.pd_dcw = 0;
                T.pd_de = T.p[eng][6], T.pd_dd = T.p[eng][7], T.pd_pe = 0, T.pd_pd = 0;
            }
            T.env[0] = getInt(b + L_ATK), T.env[1] = getInt(b + L_DEC);
            T.env[2] = getInt(b + L_SUS), T.env[3] = getInt(b + L_REL);
            T.level = getInt(b + L_LEVEL), T.pan = getInt(b + L_PAN);
            T.cut = getInt(b + L_CUT), T.res = getInt(b + L_RES), T.ftype = getInt(b + L_FTYPE), T.fpos = 0;
            T.glide = getInt(b + L_GLIDE);
            T.lfo_rate = getInt(b + L_LFORATE), T.lfo_dest = getInt(b + L_LFODEST), T.lfo_shape = getInt(b + L_LFOSHAPE);
            gm_mlfo[t] = getInt(b + L_LFODEPTH);
            T.dly = getInt(b + L_DLY), T.rev = getInt(b + L_REV), T.grn = getInt(b + L_GRN);
            T.tap = getInt(b + L_TAPE), T.drv = getInt(b + L_DRV), T.fx = 100;
            T.latch = 0, T.chord = 0, T.arp = 0;
            gm_mono[t] = getBool(b + L_MONO);
            gm_on[t] = T.level > 0;
            gm_tr[t] = 12 * getInt(b + L_OCT);
            gm_fine16[t] = (int32_t)std::lround(getInt(b + L_FINE) * 16 / 100.0);
        }
        tf_wow = getInt(kFx + F_TWOW), tf_sat = getInt(kFx + F_TSAT);
        tf_tone = getInt(kFx + F_TTONE), tf_hiss = getInt(kFx + F_THISS);
        ge_size = getInt(kFx + F_GSIZE), ge_dens = getInt(kFx + F_GDENS);
        ge_scat = getInt(kFx + F_GSCAT), ge_mix = getInt(kFx + F_GMIX);
        ge_freeze = getBool(kFx + F_GFREEZE) ? 1u : 0u;
        dl_time = getInt(kFx + F_DTIME), dl_fb = getInt(kFx + F_DFB);
        dl_wear = getInt(kFx + F_DWEAR), dl_mix = getInt(kFx + F_DMIX);
        rv_size = getInt(kFx + F_SSIZE), rv_damp = getInt(kFx + F_SDAMP);
        rv_mix = getInt(kFx + F_SMIX), rv_pre = getInt(kFx + F_SPRE);
        mf_cut = getInt(kFx + F_MCUT), mf_res = getInt(kFx + F_MRES);
        mf_type = getInt(kFx + F_MTYPE), mf_pos = getInt(kFx + F_MPOS);
        dr_amt = getInt(kFx + F_DRAMT), dr_type = getInt(kFx + F_DRTYPE);
        dr_tone = getInt(kFx + F_DRTONE), dr_mix = getInt(kFx + F_DRMIX);
        gm_amb = getInt(kFx + F_AMB), gm_crkl = getInt(kFx + F_CRKL);
        double bpm = 120.0;
        if (ctx && ctx->transport.tempoValid && ctx->transport.bpm > 20.0) bpm = ctx->transport.bpm;
        host_spb_q8 = (int32_t)std::lround(44100.0 * 60.0 / bpm * 256.0);
        const int sync = getInt(kFx + F_DSYNC);
        dl_sync_ms = sync > 0 ? (int32_t)std::lround(60000.0 / bpm * kSyncQ[sync] * 16.0) : 0;
    }

    void addPrograms();

    static mpc::PluginInfo makeInfo()
    {
        mpc::PluginInfo info;
        info.name         = "Grimoire";
        info.vendor       = "Gonzi Audio";
        info.uniqueId     = MPC_FOURCC(kUid[0], kUid[1], kUid[2], kUid[3]);
        info.version      = 1200;
        info.isInstrument = true;
        info.numInputs    = 0;
        info.numOutputs   = 2;
        info.latencySamples = (int)kLatency;
        info.tailSamples  = 44100 * 30;         // SPACE rings for up to ~32 s
        return info;
    }
};

// ---- value text: what GR!MM's screen would say ---------------------------------------------------------------------
bool Grimoire::formatValue(int index, float plain, std::string& out)
{
    using namespace grimm;
    char buf[48];
    const int v = (int)std::lround(plain);
    auto ms = [&](double x) {
        if (x >= 1000.0) std::snprintf(buf, sizeof buf, "%.1f s", x / 1000.0);
        else std::snprintf(buf, sizeof buf, "%.0f ms", x);
        out = buf;
        return true;
    };
    if (index >= kLayerA && index < kFx)
    {
        const int b = index >= kLayerB ? kLayerB : kLayerA, lp = index - b;
        const int eng = mpc::clamp(getInt(b + L_SRC), 0, (int)NENG - 1);
        const engine_t* E = ENGINES[eng];
        switch (lp)
        {
            case L_K1: case L_K2: case L_K3: case L_K4: {
                const int k = lp - L_K1;
                int32_t p[NPARAM] = {};
                p[k] = scaleKnob(v, E->max[k]);
                char t[32] = {};
                bind();
                E->text(p, (uint32_t)k, t);
                std::snprintf(buf, sizeof buf, "%s %s", shortName(E->knob[k]), t);
                out = buf;
                return true;
            }
            case L_K5: case L_K6: case L_K7: case L_K8: {
                const int k = lp - L_K5;
                const char* n = kExtraName[eng][k];
                if (!n[0]) { out = "-"; return true; }
                const int x = scaleKnob(v, kExtraMax[eng][k]);
                if (eng == ENG_GURDY) { int32_t p[NPARAM] = {}; p[4 + k] = x; char t[32] = {}; E->text(p, (uint32_t)(4 + k), t);
                                        std::snprintf(buf, sizeof buf, "%s %s", n, t); }
                else if (eng == ENG_VOICE) std::snprintf(buf, sizeof buf, "%s %s", n, kVoiceType[mpc::clamp(x, 0, 3)]);
                else if (eng == ENG_PHASE && k == 0) std::snprintf(buf, sizeof buf, "%s %s", n, kPdWave2[mpc::clamp(x, 0, 8)]);
                else if (eng == ENG_PHASE && k == 1) std::snprintf(buf, sizeof buf, "%s %s", n, x ? "ON" : "OFF");
                else std::snprintf(buf, sizeof buf, "%s %d", n, x);
                out = buf;
                return true;
            }
            case L_ATK: return ms(std::pow(2.0, v * 20.0 / 192.0));
            case L_DEC: return ms(10.0 * std::pow(2.0, v * 15.0 / 192.0));
            case L_REL: return ms(10.0 * std::pow(2.0, v * 16.5 / 192.0));
            case L_SUS: std::snprintf(buf, sizeof buf, "%d %%", v * 100 / 127); out = buf; return true;
            case L_GLIDE: if (!v) { out = "Off"; return true; } return ms(10.0 * std::pow(2.0, v * 12.0 / 192.0));
            case L_CUT: if ((getInt(b + L_FTYPE) == 0 && v >= 127) || (getInt(b + L_FTYPE) == 1 && v <= 0)) { out = "Open"; return true; }
                        std::snprintf(buf, sizeof buf, "%u Hz", (unsigned)CUTOFF_HZ[mpc::clamp(v, 0, 127)]); out = buf; return true;
            case L_PAN: if (v == 64) out = "C"; else { std::snprintf(buf, sizeof buf, v < 64 ? "L%d" : "R%d", std::abs(v - 64)); out = buf; } return true;
            case L_LFORATE: {
                const double hz = std::pow(2.0, mpc::clamp(v, 0, 127) * 10.23 / 127.0) / 60.0;
                if (hz < 1.0) std::snprintf(buf, sizeof buf, "%.1f s", 1.0 / hz); else std::snprintf(buf, sizeof buf, "%.1f Hz", hz);
                out = buf;
                return true;
            }
            case L_LFODEST:
                if (v >= 3 && v <= 6) { std::snprintf(buf, sizeof buf, "%s", E->knob[v - 3] ? E->knob[v - 3] : "-"); out = buf; return true; }
                return false;
            default: return false;
        }
    }
    if (index >= kFx)
    {
        switch (index - kFx)
        {
            case F_DTIME: if (getInt(kFx + F_DSYNC) > 0) { out = "Synced"; return true; } return ms(DL_MS[mpc::clamp(v, 0, 127)]);
            case F_SSIZE: if (v >= 127) { out = "INF"; return true; }
                          std::snprintf(buf, sizeof buf, "%.1f s", RV_RT60_X10[mpc::clamp(v, 0, 126)] / 10.0); out = buf; return true;
            case F_SPRE: return ms(v * 250.0 / 127.0);
            case F_GSIZE: return ms(10.0 * std::pow(2.0, v * 8.0 / 192.0));
            case F_GDENS: std::snprintf(buf, sizeof buf, "%.1f /s", std::pow(2.0, v * 10.0 / 192.0)); out = buf; return true;
            case F_MCUT: if ((getInt(kFx + F_MTYPE) == 0 && v >= 127) || (getInt(kFx + F_MTYPE) == 1 && v <= 0)) { out = "Open"; return true; }
                         std::snprintf(buf, sizeof buf, "%u Hz", (unsigned)CUTOFF_HZ[mpc::clamp(v, 0, 127)]); out = buf; return true;
            default: return false;
        }
    }
    return false;
}

#include "programs.h"

MPC_PLUGIN_ENTRY(Grimoire)
