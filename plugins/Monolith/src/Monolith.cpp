// Monolith - a guitar rig builder for the MPC, by Gonzi Audio
//
// Six blocks in a row. For each block you choose what it is; the order of the blocks is the order
// of the signal, so you build the chain you want (pedal -> amp -> cab -> delay -> reverb, or any
// other order).
//
//   input (L+R, Left or Right) -> Input trim -> DI Comp (optional treble lift for a guitar plugged
//     straight into the MPC's line input, which loads the pickups and dulls them)
//     -> Block 1 -> Block 2 -> Block 3 -> Block 4 -> Block 5 -> Block 6
//     -> Output -> soft limiter (rounds off peaks above -3 dBFS instead of letting the track clip)
//     -> both outputs
//
// Block types, in the order of the Type choice (sorted so that the number of knobs a type uses only
// grows: the screen hides the knobs a type does not use with one "from this type upwards" rule each):
//   Off      nothing
//   Gate     noise gate                                              (2 knobs)
//   NAM      a Neural Amp Modeler capture (.nam): pedal, amp or rig   (2 knobs + file)
//   Cab IR   a cabinet impulse response (.wav), zero latency          (3 knobs + file)
//   EQ       bass / sweepable mid / treble                           (4 knobs)
//   Drive    overdrive, to push an amp capture                       (4 knobs)
//   Delay    echo                                                    (4 knobs)
//   Reverb   room / hall / plate / spring, stereo                    (4 knobs + preset)
//   Pedal    a Neural Amp Modeler capture of a pedal                 (2 knobs + file; added in 0.4.0)
// "NAM" is shown as "Amp". Amp and Pedal blocks are the same engine with two separate file lists:
// a capture is a pedal when its metadata says gear_type "pedal", or when "pedal" appears in its
// path (put pedal captures in Content/NAM/Pedals). Drive, Delay and Reverb use the File control to
// choose a preset: a circuit / algorithm plus starting positions for the four knobs.
//
// The signal is mono through the chain (a guitar is mono); a Reverb block makes it stereo, and a
// later mono block folds it back to mono.
//
// Files: every .nam and .wav in the plugin's Content/ folder on the drive is listed (Content/NAM
// and Content/IR are the suggested places; sub-folders are fine). The list is read when the plugin
// is added to a track, so reload the project after copying new files.
//
// Threads: files are loaded by a worker thread (or by prepare()), never by process(). A loaded
// model or impulse response is handed to the audio thread through a three-state flag per block
// (0 idle, 1 new object waiting, 2 old object waiting to be freed), so process() never allocates,
// frees or locks.

#ifndef _FILE_OFFSET_BITS
  #define _FILE_OFFSET_BITS 64       // before any system header: 64-bit readdir / stat on the 32-bit MPCs
#endif

#include "mpc_plugin.h"
#include "dsp/wav.h"

#include "nam_bridge.h"
#include "rig_dsp.h"

#include <algorithm>
#include <atomic>
#include <chrono>
#include <cmath>
#include <dirent.h>
#include <sys/stat.h>
#if defined(__linux__)
  #include <dlfcn.h>
#endif
#include <mutex>
#include <string>
#include <thread>
#include <vector>

namespace {

constexpr char kUid[5] = "GMdM";   // must match plugin.json "uid"; never change after release

constexpr int kSlots = 6;
constexpr int kKnobs = 4;
constexpr int kMaxChunk = 512;

// Block types. The order is saved in projects and wired into the skin (tools/make_skin.py): from
// 0.2.0 on, only ever append.
enum Type { kOff = 0, kGate, kNam, kCab, kEq, kDrive, kDelay, kReverb, kPedal, kNumTypes };

// Global parameters, then eight per block, then whatever was added later (kDiComp, 0.3.0).
// The order is permanent: only ever append.
enum GlobalParam { kOutput = 0, kInput, kInChan, kQuality, kNumGlobals };
enum SlotParam { kType = 0, kFile, kOn, kLevel, kK1, kK2, kK3, kK4, kSlotParams };
inline int slotParam(int slot, int which) { return kNumGlobals + slot * kSlotParams + which; }

// What the four knobs of a block mean for each type: name ("" = unused) and default position (0..1).
struct KnobSpec { const char* name; float def; };
const KnobSpec kKnob[kNumTypes][kKnobs] = {
    /* Off    */ { { "", 0.0f },        { "", 0.0f },         { "", 0.0f },         { "", 0.0f } },
    /* Gate   */ { { "Thresh", 0.43f }, { "Release", 0.59f }, { "", 0.0f },         { "", 0.0f } },
    /* NAM    */ { { "Gain", 0.5f },    { "Size", 0.0f },     { "", 0.0f },         { "", 0.0f } },
    /* Cab IR */ { { "Lo Cut", 0.0f },  { "Hi Cut", 1.0f },   { "Mix", 1.0f },      { "", 0.0f } },
    /* EQ     */ { { "Bass", 0.5f },    { "Mid", 0.5f },      { "Mid Freq", 0.5f }, { "Treble", 0.5f } },
    /* Drive  */ { { "Drive", 0.4f },   { "Tone", 0.55f },    { "Tight", 0.6f },    { "Mix", 1.0f } },
    /* Delay  */ { { "Time", 0.66f },   { "Feedback", 0.37f },{ "Tone", 0.6f },     { "Mix", 0.25f } },
    /* Reverb */ { { "Decay", 0.55f },  { "Damp", 0.5f },     { "Mix", 0.2f },      { "PreDelay", 0.1f } },
    /* Pedal  */ { { "Gain", 0.5f },    { "Size", 0.0f },     { "", 0.0f },         { "", 0.0f } },
};

// Presets of the built-in effects: a name, which circuit / algorithm (see rig_dsp.h), and where the
// four knobs start. Choosing a preset moves the knobs there; after that they are yours.
// The lists are saved by position: only ever append.
struct FxPreset { const char* name; int algo; float k[kKnobs]; };
const FxPreset kDrivePresets[] = {      // Drive, Tone, Tight, Mix
    { "Screamer",      0, { 0.40f, 0.55f, 0.60f, 1.0f } },
    { "Screamer Hot",  0, { 0.75f, 0.60f, 0.65f, 1.0f } },
    { "Blues",         1, { 0.45f, 0.60f, 0.35f, 1.0f } },
    { "Blues Edge",    1, { 0.75f, 0.65f, 0.40f, 1.0f } },
    { "Rodent",        2, { 0.45f, 0.50f, 0.40f, 1.0f } },
    { "Rodent Heavy",  2, { 0.85f, 0.42f, 0.45f, 1.0f } },
    { "Fuzz",          3, { 0.55f, 0.50f, 0.30f, 1.0f } },
    { "Fuzz Wall",     3, { 0.95f, 0.40f, 0.20f, 1.0f } },
    { "Clean Boost",   4, { 0.40f, 0.30f, 0.30f, 1.0f } },
    { "Treble Boost",  4, { 0.50f, 0.90f, 0.75f, 1.0f } },
};
const FxPreset kDelayPresets[] = {      // Time, Feedback, Tone, Mix
    { "Digital",       0, { 0.66f, 0.37f, 0.80f, 0.25f } },
    { "Slapback",      0, { 0.33f, 0.10f, 0.70f, 0.35f } },
    { "Long Lead",     0, { 0.76f, 0.45f, 0.65f, 0.30f } },
    { "Tape",          1, { 0.66f, 0.42f, 0.60f, 0.30f } },
    { "Tape Worn",     1, { 0.70f, 0.55f, 0.40f, 0.35f } },
    { "Analog",        2, { 0.62f, 0.45f, 0.55f, 0.30f } },
    { "Analog Dark",   2, { 0.66f, 0.60f, 0.35f, 0.35f } },
    { "Ping Pong",     3, { 0.66f, 0.45f, 0.70f, 0.30f } },
};
const FxPreset kReverbPresets[] = {     // Decay, Damp, Mix, PreDelay
    { "Room",          0, { 0.45f, 0.50f, 0.20f, 0.05f } },
    { "Studio",        0, { 0.25f, 0.60f, 0.15f, 0.00f } },
    { "Hall",          1, { 0.60f, 0.45f, 0.25f, 0.20f } },
    { "Cathedral",     1, { 0.85f, 0.35f, 0.35f, 0.30f } },
    { "Plate",         2, { 0.50f, 0.50f, 0.22f, 0.00f } },
    { "Bright Plate",  2, { 0.60f, 0.15f, 0.25f, 0.05f } },
    { "Spring",        3, { 0.50f, 0.50f, 0.30f, 0.00f } },
    { "Surf Spring",   3, { 0.62f, 0.35f, 0.50f, 0.00f } },
};
inline const FxPreset* fxPresets(int type, int& count)
{
    switch (type)
    {
        case kDrive:  count = (int)(sizeof(kDrivePresets) / sizeof(FxPreset)); return kDrivePresets;
        case kDelay:  count = (int)(sizeof(kDelayPresets) / sizeof(FxPreset)); return kDelayPresets;
        case kReverb: count = (int)(sizeof(kReverbPresets) / sizeof(FxPreset)); return kReverbPresets;
        default:      count = 0; return nullptr;
    }
}

constexpr int kDiComp = kNumGlobals + kSlots * kSlotParams;    // parameter 52

// Transparent up to 0.7 (-3 dBFS), then bends towards 0.99: a loud pick attack is rounded off
// instead of being cut square by the track's converter.
inline float softLimit(float x)
{
    const float a = std::fabs(x);
    if (a <= 0.7f) return x;
    const float y = 0.7f + 0.29f * mpc::dsp::fastTanh((a - 0.7f) * (1.0f / 0.29f));
    return x < 0.0f ? -y : y;
}

inline float expRange(float t, float lo, float hi) { return lo * std::pow(hi / lo, mpc::clamp(t, 0.0f, 1.0f)); }

// Knob position -> real value, one function per meaning so that the audio code and the value text agree.
inline float namGainDb(float k)   { return -24.0f + 48.0f * k; }
inline float cabLoCutHz(float k)  { return expRange(k, 20.0f, 500.0f); }
inline float cabHiCutHz(float k)  { return expRange(k, 2000.0f, 20000.0f); }
inline float gateThreshDb(float k){ return -90.0f + 70.0f * k; }
inline float gateReleaseMs(float k){ return expRange(k, 10.0f, 1000.0f); }
inline float driveTightHz(float k){ return expRange(k, 20.0f, 800.0f); }
inline float eqGainDb(float k)    { return -15.0f + 30.0f * k; }
inline float eqMidHz(float k)     { return expRange(k, 200.0f, 3000.0f); }
inline float delayTimeMs(float k) { return expRange(k, 30.0f, 1200.0f); }
inline float delayFeedback(float k){ return 0.95f * k; }
inline float delayToneHz(float k) { return expRange(k, 800.0f, 12000.0f); }
inline float verbDecaySec(float k){ return expRange(k, 0.3f, 8.0f); }
inline float verbPreMs(float k)   { return 120.0f * k; }

std::string fmt(const char* f, double v) { char b[32]; std::snprintf(b, sizeof(b), f, v); return b; }
std::string fmtHz(float hz) { return hz >= 1000.0f ? fmt("%.1f kHz", hz / 1000.0f) : fmt("%.0f Hz", hz); }
std::string fmtDb(float db) { return fmt("%+.1f dB", std::fabs(db) < 0.05f ? 0.0 : db); }
std::string fmtPct(float v) { return fmt("%.0f %%", v * 100.0f); }

struct FileEntry { std::string path, name, sortKey; bool pedal = false; };

// Keep this library loaded until the host process ends. The engine has static objects (its model
// registries, the C++ streams) whose destructors are registered to run at exit; the SDK's Zig
// build has no code that runs them when the library is unloaded, so a host that unloads the
// plugin and exits later would call into memory that is gone (seen as a crash at exit in the
// emulated 32-bit test). Opening ourselves once more with RTLD_NODELETE prevents the unload.
void pinLibrary()
{
#if defined(__linux__)
    static std::once_flag once;
    std::call_once(once, [] {
        Dl_info info;
        if (dladdr(reinterpret_cast<void*>(&pinLibrary), &info) != 0 && info.dli_fname != nullptr)
            dlopen(info.dli_fname, RTLD_NOW | RTLD_NODELETE);
    });
#endif
}

std::string lower(std::string s) { for (char& c : s) c = (char)std::tolower((unsigned char)c); return s; }

} // namespace

class Monolith : public mpc::Plugin
{
public:
    Monolith() : mpc::Plugin(makeInfo())
    {
        pinLibrary();
        scanFiles();

        addParam(mpc::floatParam("output", "Output", -60.0f, 12.0f, 0.0f, "dB", 1));     // parameter 0: harmless
        addParam(mpc::floatParam("input", "Input", -24.0f, 24.0f, 0.0f, "dB", 1));
        addParam(mpc::choiceParam("in_chan", "In Chan", { "L+R", "Left", "Right" }, 0));
        addParam(mpc::choiceParam("quality", "Quality", { "Hi-Fi", "Eco" }, 0));

        static const int defaultType[kSlots] = { kGate, kNam, kCab, kOff, kOff, kOff };
        for (int s = 0; s < kSlots; ++s)
        {
            const std::string id = "s" + std::to_string(s + 1) + "_";
            const std::string n = std::to_string(s + 1) + " ";
            const int t = defaultType[s];
            addParam(mpc::choiceParam((id + "type").c_str(), ("Block " + std::to_string(s + 1)).c_str(),
                                      { "Off", "Gate", "Amp", "Cab IR", "EQ", "Drive", "Delay", "Reverb", "Pedal" }, t));
            addParam(mpc::floatParam((id + "file").c_str(), (n + "File").c_str(), 0.0f, 1.0f, 0.0f, "", 3));
            addParam(mpc::boolParam((id + "on").c_str(), (n + "On").c_str(), true));
            addParam(mpc::floatParam((id + "level").c_str(), (n + "Level").c_str(), -24.0f, 24.0f, 0.0f, "dB", 1));
            for (int k = 0; k < kKnobs; ++k)
                addParam(mpc::floatParam((id + "k" + std::to_string(k + 1)).c_str(), (n + "K" + std::to_string(k + 1)).c_str(),
                                         0.0f, 1.0f, kKnob[t][k].def, "", 3));
            type_[s].store(t);
            preset_[s].store(0);
        }
        // The File and K names change with the block type. Give the strings room now so that a later
        // rename never reallocates while the host may be reading them.
        for (int s = 0; s < kSlots; ++s)
            for (int w : { (int)kFile, (int)kK1, (int)kK2, (int)kK3, (int)kK4 }) params_[(size_t)slotParam(s, w)].name.reserve(24);

        addParam(mpc::boolParam("di_comp", "DI Comp", false));
        addPrograms();
    }

    ~Monolith() override
    {
        quit_.store(true);
        if (worker_.joinable()) worker_.join();
        for (Slot& sl : slot_)
        {
            if (sl.namState.load() != 0) rig::namFree(sl.namOther);
            if (sl.cabState.load() != 0) delete sl.cabOther;
            rig::namFree(sl.nam); delete sl.cab;
        }
    }

    // Called by the host, off the audio thread, before audio starts and whenever the rate or block
    // size changes. Loads whatever the parameters ask for, so the first block already sounds right.
    void prepare(double sampleRate, int /*maxBlockSize*/) override
    {
        std::lock_guard<std::mutex> lock(loadMutex_);
        const bool rateChanged = std::fabs(sampleRate - loadedRate_) > 0.5;
        loadedRate_ = sampleRate;
        for (int s = 0; s < kSlots; ++s)
        {
            Slot& sl = slot_[s];
            sl.gate.prepare(sampleRate); sl.drive.prepare(sampleRate); sl.eq.prepare(sampleRate);
            sl.echo.prepare(sampleRate); sl.verb.prepare(sampleRate);
            sl.level.setTime(15.0f, sampleRate); sl.gain.setTime(15.0f, sampleRate); sl.on.setTime(8.0f, sampleRate);
            sl.loCut = sl.hiCut = -1.0f;
            // Audio is stopped here, so the objects can be replaced directly.
            // (A new object that was still waiting was never put in use: forget its key so it is rebuilt.)
            if (sl.namState.load() == 1) sl.namKey = -2;
            if (sl.cabState.load() == 1) sl.cabKey = -2;
            if (sl.namState.load() != 0) { rig::namFree(sl.namOther); sl.namOther = nullptr; sl.namState.store(0); }
            if (sl.cabState.load() != 0) { delete sl.cabOther; sl.cabOther = nullptr; sl.cabState.store(0); }
            const int wantNam = wantedNamKey(s), wantCab = wantedCabKey(s);
            if (rateChanged || wantNam != sl.namKey) { rig::namFree(sl.nam); sl.nam = buildNam(wantNam); sl.namKey = wantNam; sl.namFull = -1; }
            if (rateChanged || wantCab != sl.cabKey) { delete sl.cab; sl.cab = buildCab(wantCab); sl.cabKey = wantCab; }
        }
        inGain_.setTime(15.0f, sampleRate); outGain_.setTime(15.0f, sampleRate);
        diComp_.setCoeffs(mpc::dsp::BiquadCoeffs::highShelf(sampleRate, 2800.0f, 0.7071f, 8.0f));
        if (!worker_.joinable()) worker_ = std::thread([this] { workerLoop(); });
        ready_.store(true);
        namesDirty_.store(true);
    }

    void reset() override
    {
        for (int s = 0; s < kSlots; ++s)
        {
            Slot& sl = slot_[s];
            sl.gate.reset(); sl.drive.reset(); sl.eq.reset(); sl.echo.reset(); sl.verb.reset();
            sl.hp.reset(); sl.lp.reset();
            if (sl.cab != nullptr) sl.cab->reset();
            sl.level.snap(mpc::dsp::dbToGain(get(slotParam(s, kLevel))));
            sl.gain.snap(1.0f);
            sl.on.snap(blockActive(s) ? 1.0f : 0.0f);
            sl.liveType = -1;
        }
        diComp_.reset();
        inGain_.snap(mpc::dsp::dbToGain(get(kInput)));
        outGain_.snap(mpc::dsp::dbToGain(get(kOutput)));
    }

    // Choosing another type for a block puts its knobs and file at that type's defaults. A state or
    // program load writes Type first and the knobs after it, so loaded values still win.
    void parameterChanged(int index, float plain) override
    {
        if (index < kNumGlobals || index >= kDiComp) return;
        const int s = (index - kNumGlobals) / kSlotParams, which = (index - kNumGlobals) % kSlotParams;
        if (which == kType)
        {
            const int t = mpc::clamp((int)std::lround(plain), 0, kNumTypes - 1);
            if (type_[s].exchange(t) == t) return;
            preset_[s].store(0);
            int count = 0;
            const FxPreset* fx = fxPresets(t, count);
            for (int k = 0; k < kKnobs; ++k) set(slotParam(s, kK1 + k), fx != nullptr ? fx[0].k[k] : kKnob[t][k].def);
            set(slotParam(s, kFile), 0.0f);
            notifyMask_.fetch_or(1u << s);
            namesDirty_.store(true);
        }
        else if (which == kFile)      // Drive / Delay / Reverb: another preset moves the knobs to its starting positions
        {
            int count = 0;
            const FxPreset* fx = fxPresets(type_[s].load(), count);
            if (fx == nullptr) return;
            const int i = indexFromNorm(plain, count);
            if (preset_[s].exchange(i) == i) return;
            for (int k = 0; k < kKnobs; ++k) set(slotParam(s, kK1 + k), fx[i].k[k]);
            notifyMask_.fetch_or(1u << s);
        }
    }

    bool formatValue(int index, float plain, std::string& out) override
    {
        if (index < kNumGlobals || index >= kDiComp) return false;
        const int s = (index - kNumGlobals) / kSlotParams, which = (index - kNumGlobals) % kSlotParams;
        if (s >= kSlots) return false;
        // Until the host has prepared the plugin, File and the knobs show plain numbers. The SDK's
        // build reads the parameter list in that state and must see them as continuous controls
        // (file names baked into the skin as fixed steps would be wrong on every other MPC).
        if (!ready_.load() && which != kType) return false;
        const int t = type_[s].load();
        if (which == kFile)
        {
            if (!hasList(t)) { out = ""; return true; }
            const int i = indexFromNorm(plain, listCount(t));
            out = i < 0 ? std::string(t == kCab ? "no .wav" : "no .nam") : listName(t, i);
            return true;
        }
        if (which < kK1) return false;
        const int k = which - kK1;
        if (kKnob[t][k].name[0] == '\0') { out = "-"; return true; }
        switch (t * 10 + k)
        {
            case kNam * 10 + 0: case kPedal * 10 + 0: out = fmtDb(namGainDb(plain)); break;
            case kNam * 10 + 1: case kPedal * 10 + 1: out = plain >= 0.5f ? "Full" : "Lite"; break;
            case kCab * 10 + 0:    out = plain <= 0.001f ? std::string("Off") : fmtHz(cabLoCutHz(plain)); break;
            case kCab * 10 + 1:    out = plain >= 0.999f ? std::string("Off") : fmtHz(cabHiCutHz(plain)); break;
            case kGate * 10 + 0:   out = fmt("%.0f dB", gateThreshDb(plain)); break;
            case kGate * 10 + 1:   out = fmt("%.0f ms", gateReleaseMs(plain)); break;
            case kDrive * 10 + 2:  out = fmtHz(driveTightHz(plain)); break;
            case kEq * 10 + 0: case kEq * 10 + 1: case kEq * 10 + 3: out = fmtDb(eqGainDb(plain)); break;
            case kEq * 10 + 2:     out = fmtHz(eqMidHz(plain)); break;
            case kDelay * 10 + 0:  out = fmt("%.0f ms", delayTimeMs(plain)); break;
            case kDelay * 10 + 1:  out = fmtPct(delayFeedback(plain)); break;
            case kDelay * 10 + 2:  out = fmtHz(delayToneHz(plain)); break;
            case kReverb * 10 + 0: out = fmt("%.1f s", verbDecaySec(plain)); break;
            case kReverb * 10 + 3: out = fmt("%.0f ms", verbPreMs(plain)); break;
            default:               out = fmtPct(plain); break;
        }
        return true;
    }

    // Extra state: the file NAME of every block (version byte, then per block: length byte, text).
    // The File parameter is a position in the list, which moves when files are added; the name does not.
    void saveExtraState(std::vector<uint8_t>& out) override
    {
        out.push_back(1);
        for (int s = 0; s < kSlots; ++s)
        {
            std::string name;
            const int t = type_[s].load();
            if (isFileType(t))
            {
                const int i = indexFromNorm(get(slotParam(s, kFile)), listCount(t));
                if (i >= 0) name = listName(t, i);
            }
            if (name.size() > 255) name.resize(255);
            out.push_back((uint8_t)name.size());
            out.insert(out.end(), name.begin(), name.end());
        }
    }

    void loadExtraState(const uint8_t* data, size_t size) override
    {
        if (size < 1 || data[0] != 1) return;
        size_t pos = 1;
        for (int s = 0; s < kSlots && pos < size; ++s)
        {
            const size_t len = data[pos++];
            if (pos + len > size) return;
            const std::string name(reinterpret_cast<const char*>(data + pos), len); pos += len;
            const int t = type_[s].load();
            if (!isFileType(t) || name.empty()) continue;
            const int count = listCount(t);
            for (int i = 0; i < count; ++i)
            {
                if (listName(t, i) != name) continue;
                if (indexFromNorm(get(slotParam(s, kFile)), count) != i)
                {
                    set(slotParam(s, kFile), count > 1 ? (float)i / (float)(count - 1) : 0.0f);
                    notifyMask_.fetch_or(1u << s);
                }
                break;
            }
        }
    }

    void process(mpc::ProcessContext& ctx) override
    {
        // Take over anything the worker finished loading.
        for (Slot& sl : slot_)
        {
            if (sl.namState.load(std::memory_order_acquire) == 1)
            { std::swap(sl.nam, sl.namOther); sl.namFull = -1; sl.namState.store(2, std::memory_order_release); }
            if (sl.cabState.load(std::memory_order_acquire) == 1)
            { std::swap(sl.cab, sl.cabOther); sl.cabState.store(2, std::memory_order_release); }
        }

        inGain_.setTarget(mpc::dsp::dbToGain(get(kInput)));
        const float outDb = get(kOutput);
        outGain_.setTarget(outDb <= -59.9f ? 0.0f : mpc::dsp::dbToGain(outDb));
        const int inChan = getInt(kInChan);
        const bool diComp = getBool(kDiComp);
        if (diComp != diCompOn_) { diComp_.reset(); diCompOn_ = diComp; }

        const float* inL = ctx.inputs != nullptr ? ctx.inputs[0] : nullptr;
        const float* inR = ctx.inputs != nullptr && ctx.numInputs > 1 ? ctx.inputs[1] : inL;
        float* outL = ctx.outputs[0];
        float* outR = ctx.numOutputs > 1 ? ctx.outputs[1] : nullptr;

        for (int done = 0; done < ctx.numFrames; done += kMaxChunk)
        {
            const int n = std::min(kMaxChunk, ctx.numFrames - done);
            float* L = bufL_; float* R = bufR_;
            for (int i = 0; i < n; ++i)
            {
                const float l = inL != nullptr ? inL[done + i] : 0.0f, r = inR != nullptr ? inR[done + i] : l;
                const float x = inChan == 1 ? l : (inChan == 2 ? r : 0.5f * (l + r));
                L[i] = mpc::dsp::sanitize(x) * inGain_.process();
            }
            if (diComp) diComp_.processBlock(L, n);
            bool stereo = false;
            for (int s = 0; s < kSlots; ++s) processBlock(s, L, R, n, stereo, ctx.sampleRate);

            for (int i = 0; i < n; ++i)
            {
                const float g = outGain_.process();
                const float l = softLimit(mpc::dsp::sanitize(L[i]) * g);
                const float r = stereo ? softLimit(mpc::dsp::sanitize(R[i]) * g) : l;
                outL[done + i] = l;
                if (outR != nullptr) outR[done + i] = r;
            }
        }
    }

private:
    // ----- one block of the chain ---------------------------------------------------------------
    struct Slot
    {
        // Loaded objects. `nam` / `cab` belong to the audio thread; `*Other` is the object in transit
        // (new one when the state is 1, retired one when it is 2).
        rig::NamModel* nam = nullptr; rig::NamModel* namOther = nullptr; std::atomic<int> namState{0}; int namKey = -1;
        rig::Convolver* cab = nullptr; rig::Convolver* cabOther = nullptr; std::atomic<int> cabState{0}; int cabKey = -1;
        int namFull = -1;
        rig::Gate gate; rig::Drive drive; rig::Eq3 eq; rig::Echo echo; rig::Verb verb;
        mpc::dsp::Biquad hp, lp; float loCut = -1.0f, hiCut = -1.0f;
        mpc::dsp::Smoother level, gain, on;
        int liveType = -1;
    };

    bool blockActive(int s) const { return getInt(slotParam(s, kType)) != kOff && getBool(slotParam(s, kOn)); }

    void processBlock(int s, float* L, float* R, int n, bool& stereo, double sr)
    {
        Slot& sl = slot_[s];
        const int t = mpc::clamp(getInt(slotParam(s, kType)), 0, kNumTypes - 1);
        sl.on.setTarget(t != kOff && getBool(slotParam(s, kOn)) ? 1.0f : 0.0f);
        if (sl.on.value() == 0.0f && sl.on.target() == 0.0f) { sl.liveType = -1; return; }   // bypassed: costs nothing

        if (t != sl.liveType)      // the block just became something else: start it clean
        {
            sl.gate.reset(); sl.drive.reset(); sl.eq.reset(); sl.echo.reset(); sl.verb.reset(); sl.hp.reset(); sl.lp.reset();
            if (sl.cab != nullptr) sl.cab->reset();
            sl.gain.snap(1.0f);
            sl.liveType = t;
        }
        const float k[kKnobs] = { get(slotParam(s, kK1)), get(slotParam(s, kK2)), get(slotParam(s, kK3)), get(slotParam(s, kK4)) };

        const bool wasStereo = stereo;
        std::memcpy(dryL_, L, sizeof(float) * (size_t)n);
        if (wasStereo) std::memcpy(dryR_, R, sizeof(float) * (size_t)n);
        if (stereo) { for (int i = 0; i < n; ++i) L[i] = 0.5f * (L[i] + R[i]); stereo = false; }   // every block takes mono in

        int presetCount = 0;
        const FxPreset* fx = fxPresets(t, presetCount);
        const int algo = fx != nullptr ? fx[indexFromNorm(get(slotParam(s, kFile)), presetCount)].algo : 0;

        switch (t)
        {
            case kNam: case kPedal:
                sl.gain.setTarget(mpc::dsp::dbToGain(namGainDb(k[0])));
                for (int i = 0; i < n; ++i) L[i] *= sl.gain.process();
                if (sl.nam != nullptr)
                {
                    const int full = k[1] >= 0.5f ? 1 : 0;
                    if (full != sl.namFull) { rig::namSetFull(sl.nam, full == 1); sl.namFull = full; }
                    rig::namProcess(sl.nam, L, n);
                }
                break;
            case kCab:
                if (sl.cab != nullptr)
                {
                    const float mix = k[2];
                    if (mix < 0.999f) std::memcpy(tmp_, L, sizeof(float) * (size_t)n);
                    sl.cab->process(L, n);
                    if (k[0] != sl.loCut) { sl.loCut = k[0]; sl.hp.setCoeffs(mpc::dsp::BiquadCoeffs::highpass(sr, cabLoCutHz(k[0]))); }
                    if (k[1] != sl.hiCut) { sl.hiCut = k[1]; sl.lp.setCoeffs(mpc::dsp::BiquadCoeffs::lowpass(sr, std::min(cabHiCutHz(k[1]), 0.45f * (float)sr))); }
                    if (k[0] > 0.001f) sl.hp.processBlock(L, n);
                    if (k[1] < 0.999f) sl.lp.processBlock(L, n);
                    if (mix < 0.999f) for (int i = 0; i < n; ++i) L[i] = tmp_[i] + mix * (L[i] - tmp_[i]);
                }
                break;
            case kGate:
                sl.gate.set(gateThreshDb(k[0]), gateReleaseMs(k[1]));
                sl.gate.process(L, n);
                break;
            case kDrive:
                sl.drive.set(algo, k[0], k[1], driveTightHz(k[2]), k[3]);
                sl.drive.process(L, n);
                break;
            case kEq:
                sl.eq.set(eqGainDb(k[0]), eqGainDb(k[1]), eqMidHz(k[2]), eqGainDb(k[3]));
                sl.eq.process(L, n);
                break;
            case kDelay:
                sl.echo.set(algo, delayTimeMs(k[0]), delayFeedback(k[1]), delayToneHz(k[2]), k[3]);
                sl.echo.process(L, R, n);
                if (sl.echo.stereoOut()) stereo = true;
                break;
            case kReverb:
                sl.verb.set(algo, verbDecaySec(k[0]), k[1], k[2], verbPreMs(k[3]));
                std::memcpy(tmp_, L, sizeof(float) * (size_t)n);
                sl.verb.process(tmp_, L, R, n);
                stereo = true;
                break;
            default: break;
        }

        // Block level, then the on/off crossfade against the untouched input.
        sl.level.setTarget(mpc::dsp::dbToGain(get(slotParam(s, kLevel))));
        const bool fading = sl.on.isSmoothing() || sl.on.value() < 1.0f;
        if (fading && !stereo && wasStereo) { std::memcpy(R, L, sizeof(float) * (size_t)n); stereo = true; }
        for (int i = 0; i < n; ++i)
        {
            const float g = sl.level.process();
            L[i] *= g; if (stereo) R[i] *= g;
            if (fading)
            {
                const float a = sl.on.process();
                const float dl = dryL_[i], dr = wasStereo ? dryR_[i] : dl;
                L[i] = dl + a * (L[i] - dl); if (stereo) R[i] = dr + a * (R[i] - dr);
            }
        }
    }

    // ----- files ----------------------------------------------------------------------------------
    static int indexFromNorm(float norm, int count)
    {
        if (count <= 0) return -1;
        return mpc::clamp((int)std::lround(norm * (float)(count - 1)), 0, count - 1);
    }

    // Every .nam and .wav in the plugin's Content/ folder (any depth) and loose in the plugin folder
    // itself, sorted by path so that files in the same pack stay together.
    // Plain opendir/readdir with 64-bit file offsets (see the top of this file): on the 32-bit MPCs
    // the C++ <filesystem> iterator can fail with EOVERFLOW on drives that use 64-bit directory
    // positions or inode numbers.
    void scanFolder(const std::string& dir, const std::string& key, int depth, bool recurse)
    {
        DIR* d = opendir(dir.c_str());
        if (d == nullptr) return;
        while (struct dirent* e = readdir(d))
        {
            const std::string leaf = e->d_name;
            if (leaf.empty() || leaf[0] == '.') continue;        // ".", "..", hidden files, macOS "._" companions
            const std::string path = dir + "/" + leaf;
            struct stat st;
            if (stat(path.c_str(), &st) != 0) continue;
            if (S_ISDIR(st.st_mode)) { if (recurse && depth < 5) scanFolder(path, key + leaf + "/", depth + 1, true); continue; }
            if (!S_ISREG(st.st_mode) || leaf.size() < 5) continue;
            const std::string ext = lower(leaf.substr(leaf.size() - 4));
            if (ext != ".nam" && ext != ".wav") continue;
            if (nams_.size() + cabs_.size() >= 2000) break;
            FileEntry f;
            f.path = path;
            f.name = leaf.substr(0, leaf.size() - 4);
            if (f.name.size() > 22) f.name.resize(22);
            f.sortKey = lower(key + leaf);
            if (ext == ".nam") f.pedal = f.sortKey.find("pedal") != std::string::npos || gearType(path) == "pedal";
            (ext == ".nam" ? nams_ : cabs_).push_back(f);
        }
        closedir(d);
    }
    void scanFiles()
    {
        scanFolder(mpc::pluginFolder(), "", 0, false);
        scanFolder(mpc::contentFolder(), "content/", 0, true);
        auto byKey = [](const FileEntry& a, const FileEntry& b) { return a.sortKey < b.sortKey; };
        std::sort(nams_.begin(), nams_.end(), byKey);
        std::sort(cabs_.begin(), cabs_.end(), byKey);
        for (int i = 0; i < (int)nams_.size(); ++i) (nams_[(size_t)i].pedal ? pedals_ : amps_).push_back(i);
    }

    // What the capture says it is ("amp", "pedal", "amp_cab", ...): the gear_type of its metadata, which
    // sits at the start or at the end of the file. Only those two ends are read: a capture is half a megabyte.
    static std::string gearType(const std::string& path)
    {
        std::string text;
        if (std::FILE* f = std::fopen(path.c_str(), "rb"))
        {
            char buf[16384];
            size_t n = std::fread(buf, 1, sizeof(buf), f);
            text.assign(buf, n);
            if (n == sizeof(buf) && std::fseek(f, -(long)sizeof(buf), SEEK_END) == 0) { n = std::fread(buf, 1, sizeof(buf), f); text.append(buf, n); }
            std::fclose(f);
        }
        size_t at = text.find("\"gear_type\"");
        if (at == std::string::npos) return std::string();
        at = text.find(':', at);
        const size_t q0 = at == std::string::npos ? at : text.find('"', at);
        const size_t q1 = q0 == std::string::npos ? q0 : text.find('"', q0 + 1);
        if (q1 == std::string::npos || q1 - q0 > 32 || text.find_first_not_of(" \t\r\n", at + 1) != q0) return std::string();   // null, or not a string
        return lower(text.substr(q0 + 1, q1 - q0 - 1));
    }

    // The File control's list for a type: captures (amps or pedals), impulse responses, or effect presets.
    static bool isFileType(int t) { return t == kNam || t == kPedal || t == kCab; }
    static bool hasList(int t) { int n = 0; return isFileType(t) || fxPresets(t, n) != nullptr; }
    int listCount(int t) const
    {
        if (t == kNam) return (int)amps_.size();
        if (t == kPedal) return (int)pedals_.size();
        if (t == kCab) return (int)cabs_.size();
        int n = 0; fxPresets(t, n); return n;
    }
    std::string listName(int t, int i) const
    {
        if (t == kNam) return nams_[(size_t)amps_[(size_t)i]].name;
        if (t == kPedal) return nams_[(size_t)pedals_[(size_t)i]].name;
        if (t == kCab) return cabs_[(size_t)i].name;
        int n = 0; const FxPreset* fx = fxPresets(t, n);
        return fx != nullptr && i < n ? std::string(fx[i].name) : std::string();
    }

    // What a block should have loaded, as one comparable number (-1 = nothing).
    int wantedNamKey(int s) const
    {
        const int t = type_[s].load();
        if (t != kNam && t != kPedal) return -1;
        const std::vector<int>& list = t == kNam ? amps_ : pedals_;
        const int i = indexFromNorm(get(slotParam(s, kFile)), (int)list.size());
        return i < 0 ? -1 : list[(size_t)i] * 2 + (getInt(kQuality) == 0 ? 1 : 0);
    }
    int wantedCabKey(int s) const
    {
        if (type_[s].load() != kCab) return -1;
        return indexFromNorm(get(slotParam(s, kFile)), (int)cabs_.size());
    }
    rig::NamModel* buildNam(int key) const
    {
        if (key < 0) return nullptr;
        std::string error;
        rig::NamModel* m = rig::namLoad(nams_[(size_t)(key / 2)].path, loadedRate_, (key & 1) != 0, error);
        if (m == nullptr) mpc::log("Monolith: %s: %s", nams_[(size_t)(key / 2)].path.c_str(), error.c_str());
        return m;
    }
    rig::Convolver* buildCab(int key) const
    {
        if (key < 0) return nullptr;
        ::dsp::WavData wav;
        if (!::dsp::loadWav(cabs_[(size_t)key].path, wav) || wav.numChannels() < 1) return nullptr;
        std::vector<float> ir = wav.channels[0];
        if (ir.size() > 400000) ir.resize(400000);
        if (!rig::conditionIr(ir, wav.sampleRate, loadedRate_)) return nullptr;
        rig::Convolver* c = new rig::Convolver();
        c->init(ir);
        return c;
    }

    // ----- worker thread: loads files, frees retired objects, keeps the screen names up to date ----
    void workerLoop()
    {
        while (!quit_.load())
        {
            {
                std::lock_guard<std::mutex> lock(loadMutex_);
                for (int s = 0; s < kSlots && !quit_.load(); ++s)
                {
                    Slot& sl = slot_[s];
                    if (sl.namState.load(std::memory_order_acquire) == 2) { rig::namFree(sl.namOther); sl.namOther = nullptr; sl.namState.store(0); }
                    if (sl.cabState.load(std::memory_order_acquire) == 2) { delete sl.cabOther; sl.cabOther = nullptr; sl.cabState.store(0); }
                    const int wantNam = wantedNamKey(s);
                    if (sl.namState.load() == 0 && wantNam != sl.namKey)
                    { sl.namOther = buildNam(wantNam); sl.namKey = wantNam; sl.namState.store(1, std::memory_order_release); }
                    const int wantCab = wantedCabKey(s);
                    if (sl.cabState.load() == 0 && wantCab != sl.cabKey)
                    { sl.cabOther = buildCab(wantCab); sl.cabKey = wantCab; sl.cabState.store(1, std::memory_order_release); }
                }
            }
            const unsigned mask = notifyMask_.exchange(0);
            for (int s = 0; s < kSlots; ++s)
                if (mask & (1u << s))
                    for (int w : { (int)kFile, (int)kK1, (int)kK2, (int)kK3, (int)kK4 }) notifyHostParameterChanged(slotParam(s, w));
            if (namesDirty_.exchange(false) && updateNames()) requestDisplayUpdate();
            std::this_thread::sleep_for(std::chrono::milliseconds(40));
        }
    }

    // "2 Gain", "2 Model", ... following each block's type. True if anything changed.
    bool updateNames()
    {
        bool changed = false;
        auto rename = [&](int index, const std::string& name)
        {
            std::string& cur = params_[(size_t)index].name;
            if (cur != name) { cur.assign(name); changed = true; }
        };
        for (int s = 0; s < kSlots; ++s)
        {
            const int t = type_[s].load();
            const std::string n = std::to_string(s + 1) + " ";
            rename(slotParam(s, kFile), n + (t == kNam ? "Amp" : (t == kPedal ? "Pedal" : (t == kCab ? "Cab IR" : (hasList(t) ? "Preset" : "-")))));
            for (int k = 0; k < kKnobs; ++k)
                rename(slotParam(s, kK1 + k), n + (kKnob[t][k].name[0] != '\0' ? kKnob[t][k].name : "-"));
        }
        return changed;
    }

    // ----- built-in rigs --------------------------------------------------------------------------
    void addPrograms()
    {
        struct B { int type; float level; float k[kKnobs]; };
        auto add = [this](const char* name, std::initializer_list<B> blocks)
        {
            std::vector<float> v((size_t)numParams(), 0.0f);
            for (int i = 0; i < kNumGlobals; ++i) v[(size_t)i] = param(i).def;
            int s = 0;
            for (int i = 0; i < kSlots; ++i) { v[(size_t)slotParam(i, kType)] = kOff; v[(size_t)slotParam(i, kOn)] = 1.0f; }
            for (const B& b : blocks)
            {
                if (s >= kSlots) break;
                v[(size_t)slotParam(s, kType)] = (float)b.type;
                v[(size_t)slotParam(s, kLevel)] = b.level;
                for (int k = 0; k < kKnobs; ++k) v[(size_t)slotParam(s, kK1 + k)] = b.k[k] < 0.0f ? kKnob[b.type][k].def : b.k[k];
                ++s;
            }
            addProgram(name, v);
        };
        const B gate   { kGate,  0.0f, { -1, -1, -1, -1 } };
        const B amp    { kNam,   0.0f, { -1, -1, -1, -1 } };
        const B cab    { kCab,   0.0f, { -1, -1, -1, -1 } };
        add("Amp + Cab",    { gate, amp, cab });
        add("Boosted Amp",  { gate, { kDrive, 0.0f, { 0.3f, 0.6f, 0.7f, 1.0f } }, amp, cab });
        add("Lead Echo",    { gate, { kDrive, 0.0f, { 0.45f, 0.6f, 0.65f, 1.0f } }, amp, cab,
                              { kDelay, 0.0f, { 0.68f, 0.4f, 0.55f, 0.28f } }, { kReverb, 0.0f, { 0.5f, 0.5f, 0.18f, 0.15f } } });
        add("Ambient Wash", { { kNam, 0.0f, { 0.4f, 0.0f, 0.0f, 0.0f } }, cab, { kEq, 0.0f, { 0.45f, 0.5f, 0.5f, 0.4f } },
                              { kDelay, 0.0f, { 0.78f, 0.55f, 0.45f, 0.38f } }, { kReverb, 0.0f, { 0.8f, 0.4f, 0.45f, 0.3f } } });
        add("Pedal + Amp",  { gate, { kPedal, 0.0f, { 0.5f, 0.0f, 0.0f, 0.0f } }, amp, cab });
        add("Cab Only",     { { kEq, 0.0f, { -1, -1, -1, -1 } }, cab });
    }

    static mpc::PluginInfo makeInfo()
    {
        mpc::PluginInfo info;
        info.name         = "Monolith";
        info.vendor       = "Gonzi Audio";   // must match plugin.json "manufacturer"
        info.uniqueId     = MPC_FOURCC(kUid[0], kUid[1], kUid[2], kUid[3]);
        info.version      = 400;             // 0.4.0.0 (prototype)
        info.isInstrument = false;
        info.numInputs    = 2;
        info.numOutputs   = 2;
        info.tailSamples  = 0;               // delays and reverbs ring on
        return info;
    }

    std::vector<FileEntry> nams_, cabs_;     // filled once in the constructor, read-only afterwards
    std::vector<int> amps_, pedals_;         // positions in nams_: the Amp list and the Pedal list
    std::atomic<int> preset_[kSlots];        // each block's effect preset, to see when it changes
    Slot slot_[kSlots];
    std::atomic<int> type_[kSlots];          // each block's type, for the threads that cannot call get()
    mpc::dsp::Smoother inGain_, outGain_;
    mpc::dsp::Biquad diComp_; bool diCompOn_ = false;
    float bufL_[kMaxChunk], bufR_[kMaxChunk], dryL_[kMaxChunk], dryR_[kMaxChunk], tmp_[kMaxChunk];

    std::thread worker_;
    std::mutex loadMutex_;                   // worker <-> prepare(); never taken by process()
    std::atomic<bool> quit_{false}, namesDirty_{false}, ready_{false};
    std::atomic<unsigned> notifyMask_{0};
    double loadedRate_ = 0.0;
};

MPC_PLUGIN_ENTRY(Monolith)
