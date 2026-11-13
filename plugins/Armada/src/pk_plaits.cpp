// SPDX-License-Identifier: GPL-3.0-only
// Armada pack: Plaits, the macro-oscillator of Mutable Instruments (Emilie Gillet), from the module's own sources
// (github.com/pichenettes/eurorack, plaits/ and stmlib/, MIT; copied unchanged into src/plaits and src/stmlib).
// This file (Gonzi Audio, GPL-3.0-only) builds them as one unit and plays six of the module's voices polyphonically:
// each of the module's 24 models is an engine of the plugin.
//
// The module runs at 48 kHz; here its voice renders at the plugin's 44.1 kHz, so the note is raised by the ratio
// (1.47 semitones) and the pitch comes out right. A voice gets the note's trigger (the models that are struck or
// plucked need it) and, as the module's LEVEL input, an attack / decay / sustain / release envelope.
#define TEST 1                                    // (plaits/user_data.h: no flash, no user data)
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <new>

#if defined(__clang__)
#pragma clang diagnostic ignored "-Wunused-parameter"
#pragma clang diagnostic ignored "-Wunused-variable"
#pragma clang diagnostic ignored "-Wunused-function"
#pragma clang diagnostic ignored "-Wsign-compare"
#pragma clang diagnostic ignored "-Wmissing-field-initializers"
#pragma clang diagnostic ignored "-Wunused-private-field"
#pragma clang diagnostic ignored "-Wunused-const-variable"
#pragma clang diagnostic ignored "-Wimplicit-fallthrough"
#pragma clang diagnostic ignored "-Wdeprecated-declarations"
#pragma clang diagnostic ignored "-Wshift-negative-value"
#pragma clang diagnostic ignored "-Wextra"
#pragma clang diagnostic ignored "-Wall"
#endif

#include "stmlib/dsp/atan.cc"
#include "stmlib/dsp/units.cc"
#include "stmlib/utils/random.cc"
#include "plaits/resources.cc"
#include "plaits/dsp/chords/chord_bank.cc"
#include "plaits/dsp/engine/additive_engine.cc"
#include "plaits/dsp/engine/bass_drum_engine.cc"
#include "plaits/dsp/engine/chord_engine.cc"
#include "plaits/dsp/engine/fm_engine.cc"
#include "plaits/dsp/engine/grain_engine.cc"
#include "plaits/dsp/engine/hi_hat_engine.cc"
#include "plaits/dsp/engine/modal_engine.cc"
#include "plaits/dsp/engine/noise_engine.cc"
#include "plaits/dsp/engine/particle_engine.cc"
#include "plaits/dsp/engine/snare_drum_engine.cc"
#include "plaits/dsp/engine/speech_engine.cc"
#include "plaits/dsp/engine/string_engine.cc"
#include "plaits/dsp/engine/swarm_engine.cc"
#include "plaits/dsp/engine/virtual_analog_engine.cc"
#include "plaits/dsp/engine/waveshaping_engine.cc"
#include "plaits/dsp/engine/wavetable_engine.cc"
#include "plaits/dsp/engine2/chiptune_engine.cc"
#include "plaits/dsp/engine2/phase_distortion_engine.cc"
#include "plaits/dsp/engine2/virtual_analog_vcf_engine.cc"
#include "plaits/dsp/engine2/wave_terrain_engine.cc"
#include "plaits/dsp/physical_modelling/modal_voice.cc"
#include "plaits/dsp/physical_modelling/resonator.cc"
#include "plaits/dsp/physical_modelling/string.cc"
#include "plaits/dsp/physical_modelling/string_voice.cc"
#include "plaits/dsp/speech/lpc_speech_synth.cc"
#include "plaits/dsp/speech/lpc_speech_synth_controller.cc"
#include "plaits/dsp/speech/lpc_speech_synth_phonemes.cc"
#include "plaits/dsp/speech/lpc_speech_synth_words.cc"
#include "plaits/dsp/speech/naive_speech_synth.cc"
#include "plaits/dsp/speech/sam_speech_synth.cc"
#include "plaits/dsp/voice.cc"

#include "armada_pack.h"

namespace pk_plaits {

enum { kModels = 24, kVoices = 6, kParams = 9 };
// the module's models, in its own order (the 1.2 firmware: the second bank first)
static const char *const kModelName[kModels] = {
    "VA VCF", "PHASE DIST", "6-OP FM 1", "6-OP FM 2", "6-OP FM 3", "WAVE TERRAIN", "STRING MACH", "CHIPTUNE",
    "PAIR OF VA", "WAVESHAPER", "2-OP FM", "GRAIN FORMANT", "HARMONIC", "WAVETABLE", "CHORDS", "SPEECH",
    "SWARM", "FILTERED NOISE", "PARTICLE", "INHARM STRING", "MODAL", "BASS DRUM", "SNARE DRUM", "HI-HAT"};
static const char *const kParamName[kParams] = {"HARMONICS", "TIMBRE", "MORPH", "FM ENV", "TIMBRE ENV", "MORPH ENV",
                                                "DECAY", "LPG COLOUR", "AUX MIX"};
static const int kParamDef[kParams] = {64, 64, 64, 64, 64, 64, 64, 64, 0};

// the six-operator models: HARMONICS picks one of the bank's 32 patches; the first is the starting point
static int paramDef(int model, int k) { return k == 0 && model >= 2 && model <= 4 ? 0 : kParamDef[k]; }
static armada::EngineInfo g_info[kModels];
static const armada::EngineInfo *engine(int e)
{
    static bool made = false;
    if (!made) {
        for (int i = 0; i < kModels; i++) {
            g_info[i].name = kModelName[i];
            g_info[i].numSounds = 1;
            g_info[i].numParams = kParams;
            for (int k = 0; k < kParams; k++) {
                g_info[i].p[k].label = kParamName[k];
                g_info[i].p[k].min = 0; g_info[i].p[k].max = 127; g_info[i].p[k].def = paramDef(i, k);
            }
        }
        made = true;
    }
    return &g_info[e];
}
static int numEngines() { return kModels; }
static const char *soundName(int, int) { return "INIT"; }

// the firmwares' 0..127 time scale, roughly: 1 ms .. 8 s
static float timeOf(int v) { return 0.001f * std::pow(8000.0f, (float)(v < 0 ? 0 : v > 127 ? 127 : v) / 127.0f); }

struct Part final : armada::Part {
    struct V {
        plaits::Voice voice;
        char ram[16384];
        int note = -1, vel = 0;
        bool gate = false, trig = false;
        float env = 0.0f;
        int stage = 0;                             // 0 off, 1 attack, 2 decay / sustain, 3 release
        float pitch = 60.0f;
        unsigned age = 0;
    };
    V *v;
    int model = 0;
    int e[kParams];
    armada::Common c;
    float bendSt = 0.0f, lfoPh = 0.0f;
    unsigned clock = 0;
    int held[16], nheld = 0;                       // mono: the keys down, in order

    Part()
    {
        v = (V *)std::calloc(kVoices, sizeof(V));
        for (int i = 0; i < kVoices; i++) {
            new (&v[i]) V();
            stmlib::BufferAllocator alloc(v[i].ram, sizeof v[i].ram);
            v[i].voice.Init(&alloc);
        }
        for (int k = 0; k < kParams; k++) e[k] = kParamDef[k];
    }
    ~Part() override { std::free(v); }

    void loadSound(int en, int, armada::SoundValues *sv) override
    {
        allNotesOff();
        model = en < 0 ? 0 : en >= kModels ? kModels - 1 : en;
        for (int k = 0; k < kParams; k++) e[k] = paramDef(model, k);
        if (sv) {
            for (int k = 0; k < kParams; k++) sv->e[k] = e[k];
            const bool hit = model >= 21;          // the drums: one shot
            sv->set(armada::cAtk, 0);
            sv->set(armada::cDec, 60);
            sv->set(armada::cSus, hit ? 0 : 127);
            sv->set(armada::cRel, hit ? 60 : 40);
        }
    }
    void setE(int k, int val) override { if (k >= 0 && k < kParams) e[k] = val < 0 ? 0 : val > 127 ? 127 : val; }
    void setCommon(const armada::Common &cm) override { c = cm; }
    bool mono() const { return c.v[armada::cVoice] == 1 || c.v[armada::cVoice] == 2; }
    void start(V &x, int note, int vel, bool retrig)
    {
        x.note = note; x.vel = vel; x.gate = true;
        if (retrig || x.stage == 0) { x.trig = true; x.stage = 1; }
        if (x.stage == 3) x.stage = 1;
        if (c.v[armada::cGlide] == 0 || x.age == 0) x.pitch = (float)note;
        x.age = ++clock;
    }
    void noteOn(int note, int vel) override
    {
        if (mono()) {
            if (nheld < 16) held[nheld++] = note;
            start(v[0], note, vel, c.v[armada::cVoice] == 1 || !v[0].gate);
            return;
        }
        int best = 0;
        for (int i = 0; i < kVoices; i++) if (v[i].note == note && v[i].stage) { best = i; goto found; }
        for (int i = 0; i < kVoices; i++) if (!v[i].stage) { best = i; goto found; }
        for (int i = 1; i < kVoices; i++)          // steal: a released one first, else the oldest
            if ((v[i].gate ? 1 : 0) < (v[best].gate ? 1 : 0) || ((v[i].gate == v[best].gate) && v[i].age < v[best].age)) best = i;
    found:
        start(v[best], note, vel, true);
    }
    void noteOff(int note) override
    {
        if (mono()) {
            int w = 0;
            for (int i = 0; i < nheld; i++) if (held[i] != note) held[w++] = held[i];
            nheld = w;
            if (v[0].note == note) {
                if (nheld) start(v[0], held[nheld - 1], v[0].vel, false);
                else { v[0].gate = false; if (v[0].stage) v[0].stage = 3; }
            }
            return;
        }
        for (int i = 0; i < kVoices; i++)
            if (v[i].note == note && v[i].gate) { v[i].gate = false; if (v[i].stage) v[i].stage = 3; }
    }
    void allNotesOff() override
    {
        nheld = 0;
        for (int i = 0; i < kVoices; i++) { v[i].gate = false; if (v[i].stage) v[i].stage = 3; }
    }
    void bend(int cents) override { bendSt = (float)cents * 0.01f; }

    void render(float *l, float *r, int n) override
    {
        const float dt = (float)n / (float)armada::kRate;
        const float atk = timeOf(c.v[armada::cAtk]), dec = timeOf(c.v[armada::cDec]), rel = timeOf(c.v[armada::cRel]);
        const float sus = (float)c.v[armada::cSus] / 127.0f;
        // one LFO a part: a triangle, 0.05 .. 20 Hz
        lfoPh += dt * 0.05f * std::pow(400.0f, (float)c.v[armada::cLfoRate] / 127.0f);
        lfoPh -= std::floor(lfoPh);
        const float lfo = lfoPh < 0.5f ? 4.0f * lfoPh - 1.0f : 3.0f - 4.0f * lfoPh;
        const float glide = c.v[armada::cGlide] ? 1.0f - std::exp(-dt / (0.002f * std::pow(1000.0f, (float)c.v[armada::cGlide] / 127.0f))) : 1.0f;
        const float aux = (float)e[8] / 127.0f;
        const float gain = (model >= 2 && model <= 4 ? 0.22f : 0.4f) / 32768.0f;   // (six operators, several notes: hot)
        for (int i = 0; i < n; i++) l[i] = r[i] = 0.0f;
        const int nv = mono() ? 1 : kVoices;
        for (int i = 0; i < nv; i++) {
            V &x = v[i];
            if (!x.stage) continue;
            // the envelope, once a block
            if (x.stage == 1) { x.env += dt / atk; if (x.env >= 1.0f) { x.env = 1.0f; x.stage = 2; } }
            else if (x.stage == 2) x.env += (sus - x.env) * (1.0f - std::exp(-dt * 4.6f / dec));
            else { x.env -= x.env * (1.0f - std::exp(-dt * 4.6f / rel)); if (x.env < 0.0005f) { x.env = 0.0f; x.stage = 0; } }
            x.pitch += ((float)x.note - x.pitch) * glide;

            plaits::Patch p;
            plaits::Modulations m;
            std::memset(&m, 0, sizeof m);
            p.engine = model;
            p.note = x.pitch + 1.4656f + bendSt + (float)c.tune * 0.01f + lfo * (float)c.v[armada::cLfoPitch] * (2.0f / 64.0f)
                     + x.env * (float)c.v[armada::cPenv] * (12.0f / 64.0f);
            p.harmonics = (float)e[0] / 127.0f;
            p.timbre = (float)e[1] / 127.0f + lfo * (float)c.v[armada::cLfoShape] / 128.0f + x.env * (float)c.v[armada::cSenv] / 128.0f;
            p.morph = (float)e[2] / 127.0f + lfo * (float)c.v[armada::cLfoFilter] / 128.0f + x.env * (float)c.v[armada::cFenv] / 128.0f;
            p.timbre = p.timbre < 0.0f ? 0.0f : p.timbre > 1.0f ? 1.0f : p.timbre;
            p.morph = p.morph < 0.0f ? 0.0f : p.morph > 1.0f ? 1.0f : p.morph;
            p.frequency_modulation_amount = ((float)e[3] - 64.0f) / 64.0f;
            p.timbre_modulation_amount = ((float)e[4] - 64.0f) / 64.0f;
            p.morph_modulation_amount = ((float)e[5] - 64.0f) / 64.0f;
            p.decay = (float)e[6] / 127.0f;
            p.lpg_colour = (float)e[7] / 127.0f;
            m.trigger_patched = true;
            m.level_patched = true;
            m.level = x.env * (0.25f + 0.75f * (float)x.vel / 127.0f) * (1.0f - 0.5f * (lfo + 1.0f) * (float)c.v[armada::cLfoAmp] / 127.0f);
            // the module's TRIGGER input as a gate: high while the key is down (the six-operator models hold their
            // envelopes on it), with a low first chunk when a note starts so that every note is a rising edge
            plaits::Voice::Frame f[plaits::kMaxBlockSize];
            int done = 0;
            while (done < n) {
                int chunk = n - done > 16 ? 16 : n - done;
                m.trigger = x.gate && !(x.trig && done == 0) ? 1.0f : 0.0f;
                x.voice.Render(p, m, f, (size_t)chunk);
                for (int k = 0; k < chunk; k++) {
                    const float s = ((float)f[k].out * (1.0f - aux) + (float)f[k].aux * aux) * gain;
                    l[done + k] += s; r[done + k] += s;
                }
                done += chunk;
            }
            x.trig = false;
        }
    }
    void format(int, int k, int val, char *out, int outSize) override
    {
        if (k >= 3 && k <= 5) std::snprintf(out, (size_t)outSize, "%d", val - 64);
        else std::snprintf(out, (size_t)outSize, "%d", val);
    }
};

static armada::Part *create() { return new (std::nothrow) Part(); }

} // namespace pk_plaits

extern const armada::Pack armada_pack_plaits;
const armada::Pack armada_pack_plaits = {"plaits", "Plaits by Emilie Gillet, Mutable Instruments (github.com/pichenettes/eurorack)",
                                         pk_plaits::numEngines, pk_plaits::engine, pk_plaits::soundName, pk_plaits::create};
