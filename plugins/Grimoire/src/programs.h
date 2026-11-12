// programs.h - Grimoire's built-in programs (included by Grimoire.cpp).
// SPDX-License-Identifier: GPL-3.0-only
// Copyright (C) 2026 Gonzi Audio
//
// Each program starts from the parameters' defaults and sets what it changes, by parameter id. The single-layer ones
// are GR!MM's factory sounds (GRiMMBIENT/grimm firmware/src/store.c SND_FACTORY: source, its four knobs, ENV, LFO) and
// GHOULBOX's (jasonpersinger/ghoulbox-fm1-dungeon-synth: the GURDY presets, the FM patches F9..F15), with effects set
// for each (on the FM-1 the effects are the project's, not the sound's). The layered ones are Grimoire's.
//
// A knob's value here is the source's own (GR!MM's screen value): K(x, max) turns it into the 0..127 the parameter
// holds (the plugin scales it back the same way: Grimoire.cpp scaleKnob).

#include <cmath>
#include <initializer_list>
#include <utility>

namespace {
inline float K(int x, int max) { return max > 0 ? (float)std::lround(x * 127.0 / max) : 0.0f; }
// GR!MM's knob ranges, by source
constexpr int VA_WAVE = 2, FM_PATCH = 10, VOICE_SHIFT = 24, WHEEL_DRAW = 15, WHEEL_PERC = 6, PHYS_MODEL = 3,
              PD_WAVE = 7, PD_WAVE2 = 8, GURDY_DRN = 11, GURDY_COUP = 4;
enum { S_VA = 0, S_FM, S_VOICE, S_WHEEL, S_PHYS, S_PHASE, S_GURDY };
// FM patches: GR!MM's then GHOULBOX's
enum { P_BELL = 0, P_ORGAN, P_SOFTPAD, P_REED, P_HORN, P_HARPSI, P_GLASSCHOIR, P_DARKSTR, P_FLUTE, P_CRYPTORGAN, P_TUBA };
} // namespace

void Grimoire::addPrograms()
{
    using Set = std::initializer_list<std::pair<const char*, float>>;
    // a GR!MM factory sound on a layer: source, its four (native), ENV, LFO (rate, depth, destination, shape)
    auto snd = [](int src, int k1, int k2, int k3, int k4, int a, int d, int s, int r, int lr, int ld, int lt, int ls,
                  std::initializer_list<int> max) {
        std::vector<std::pair<const char*, float>> o;
        const int* m = max.begin();
        o.push_back({"src", (float)src});
        o.push_back({"k1", K(k1, m[0])}); o.push_back({"k2", K(k2, m[1])});
        o.push_back({"k3", K(k3, m[2])}); o.push_back({"k4", K(k4, m[3])});
        o.push_back({"atk", (float)a}); o.push_back({"dec", (float)d}); o.push_back({"sus", (float)s}); o.push_back({"rel", (float)r});
        o.push_back({"lforate", (float)lr}); o.push_back({"lfodepth", (float)ld});
        o.push_back({"lfodest", (float)lt}); o.push_back({"lfoshape", (float)ls});
        return o;
    };
    // a layer from snd() plus more settings
    std::vector<std::vector<std::pair<const char*, float>>> keep;   // (the lists live until addProgram has run)
    auto L = [&](std::vector<std::pair<const char*, float>> base, Set more) {
        for (const auto& m : more) base.push_back(m);
        keep.push_back(base);
        return keep.size() - 1;
    };
    auto progL = [&](const char* name, int la, int lb, Set fx) {
        std::vector<float> v((size_t)numParams());
        for (int i = 0; i < numParams(); ++i) v[(size_t)i] = param(i).def;
        auto put = [&](const std::string& prefix, const std::vector<std::pair<const char*, float>>& set) {
            for (const auto& s : set)
            {
                const std::string id = prefix + s.first;
                int i = 0;
                while (i < numParams() && param(i).id != id) ++i;
                if (i < numParams()) v[(size_t)i] = s.second;
                else mpc::log("program %s: no parameter %s", name, id.c_str());
            }
        };
        if (la >= 0) put("a_", keep[(size_t)la]);
        if (lb >= 0) put("b_", keep[(size_t)lb]);
        else put("b_", {{"level", 0.0f}});
        put("", std::vector<std::pair<const char*, float>>(fx));
        addProgram(name, v);
    };

    const std::initializer_list<int> VA = {VA_WAVE, 127, 127, 127}, FM = {FM_PATCH, 127, 127, 127},
                                     VO = {127, VOICE_SHIFT, 127, 127}, WH = {WHEEL_DRAW, WHEEL_PERC, 127, 127},
                                     PH = {PHYS_MODEL, 127, 127, 127}, PD = {PD_WAVE, PD_WAVE2, 127, 127},
                                     GU = {GURDY_DRN, 127, 127, 127};
    auto choir = [&](int type) { return K(type, 3); };

    // 0 GRIMOIRE: CRYPT PAD (supersaw) under a GREGORIAN choir, a long hall, worn tape
    progL("Grimoire",
          L(snd(S_VA, 2, 70, 62, 20, 105, 100, 110, 100, 30, 15, 5, 0, VA), {{"level", 92}, {"rev", 80}, {"dly", 10}, {"tape", 60}}),
          L(snd(S_VOICE, 95, 12, 75, 15, 82, 90, 120, 110, 40, 0, 0, 0, VO), {{"k5", choir(1)}, {"level", 88}, {"rev", 100}, {"grn", 30}}),
          {{"master", -2}, {"t_wow", 30}, {"t_sat", 25}, {"t_tone", 40}, {"s_size", 100}, {"s_damp", 60}, {"s_mix", 100}, {"g_mix", 80}});
    // 1..3 the choirs (GR!MM V2: real singers)
    progL("Gregorian", L(snd(S_VOICE, 95, 12, 75, 15, 82, 90, 120, 110, 40, 0, 0, 0, VO), {{"k5", choir(1)}, {"rev", 100}}), -1,
          {{"master", -3}, {"s_size", 105}, {"s_damp", 55}});
    progL("Cathedral Choir", L(snd(S_VOICE, 0, 15, 80, 12, 85, 90, 105, 112, 40, 0, 0, 0, VO), {{"k5", choir(3)}, {"rev", 110}}), -1,
          {{"master", -2}, {"s_size", 112}, {"s_pre", 30}});
    progL("Nuns", L(snd(S_VOICE, 95, 12, 90, 20, 84, 90, 120, 108, 40, 0, 0, 0, VO), {{"k5", choir(2)}, {"rev", 100}}), -1,
          {{"master", -2.5}, {"s_size", 100}});
    // 4..8 drones and pads
    progL("Monk Drone", L(snd(S_VA, 0, 15, 50, 0, 100, 90, 120, 105, 20, 8, 1, 0, VA), {{"oct", -1}, {"rev", 90}, {"tape", 40}}),
          L(snd(S_VOICE, 120, 4, 90, 45, 75, 80, 65, 90, 40, 0, 0, 0, VO), {{"level", 60}, {"k5", choir(1)}, {"oct", -1}, {"rev", 100}}),
          {{"master", 2.5}, {"amb", 70}, {"t_wow", 20}, {"s_size", 100}});
    progL("Iron Lung", L(snd(S_PHYS, 0, 100, 120, 60, 90, 90, 90, 100, 70, 20, 6, 5, PH), {{"rev", 90}, {"dly", 30}}), -1,
          {{"master", 5}, {"s_size", 95}, {"d_sync", 9}, {"d_fb", 70}, {"d_wear", 80}});
    progL("Static Prayer", L(snd(S_VOICE, 96, 6, 100, 90, 90, 90, 115, 100, 115, 30, 1, 5, VO), {{"rev", 100}, {"grn", 60}}), -1,
          {{"master", 0}, {"g_size", 90}, {"g_dens", 70}, {"t_hiss", 30}, {"t_wow", 40}});
    progL("Frozen Hall", L(snd(S_FM, P_SOFTPAD, 90, 100, 60, 100, 90, 110, 105, 70, 15, 4, 1, FM), {{"rev", 100}, {"grn", 40}}), -1,
          {{"master", 3.5}, {"s_size", 110}, {"s_damp", 30}});
    progL("Last Light", L(snd(S_PHASE, 5, 0, 60, 8, 110, 100, 120, 100, 40, 10, 5, 0, PD), {{"rev", 90}, {"tape", 50}}), -1,
          {{"master", 10}, {"t_wow", 35}, {"t_sat", 30}, {"s_size", 95}});
    // 9..11 bells and metal
    progL("Funeral Chime", L(snd(S_FM, P_BELL, 80, 50, 0, 0, 100, 0, 95, 40, 0, 0, 0, FM), {{"rev", 90}, {"dly", 40}}), -1,
          {{"master", 2}, {"d_sync", 8}, {"d_fb", 55}, {"s_size", 100}});
    progL("Rust Gong", L(snd(S_PHYS, 0, 110, 115, 0, 0, 90, 127, 110, 40, 0, 0, 0, PH), {{"oct", -1}, {"rev", 95}}), -1,
          {{"master", 5.5}, {"s_size", 105}, {"dr_amt", 20}});
    progL("Bowed Bell", L(snd(S_PHYS, 0, 70, 110, 45, 85, 90, 90, 100, 40, 0, 0, 0, PH), {{"rev", 100}, {"grn", 30}}), -1,
          {{"master", 7.5}, {"s_size", 100}});
    // 12..14 organs and keys
    progL("Funeral Organ", L(snd(S_WHEEL, 15, 0, 20, 10, 10, 20, 75, 60, 40, 0, 0, 0, WH), {{"rev", 85}}), -1, {{"master", 3}, {"s_size", 105}});
    progL("Crypt Organ", L(snd(S_FM, P_CRYPTORGAN, 64, 64, 0, 20, 90, 127, 70, 40, 0, 0, 0, FM), {{"rev", 100}, {"level", 90}}), -1,
          {{"master", -2.5}, {"s_size", 110}, {"s_damp", 40}, {"amb", 40}});
    progL("Virginal", L(snd(S_FM, P_HARPSI, 64, 64, 0, 0, 75, 0, 50, 40, 0, 0, 0, FM), {{"rev", 60}, {"dly", 25}}), -1,
          {{"master", 6}, {"d_sync", 6}, {"d_fb", 40}, {"s_size", 80}});
    // 15..18 strings, brass, winds
    progL("Haunted Strings", L(snd(S_PHYS, 1, 64, 110, 70, 100, 100, 65, 95, 40, 0, 0, 0, PH), {{"rev", 90}, {"cut", 78}, {"res", 20}}),
          L(snd(S_FM, P_DARKSTR, 64, 64, 30, 90, 90, 120, 95, 40, 0, 0, 0, FM), {{"level", 70}, {"rev", 90}, {"oct", -1}}),
          {{"master", 3}, {"s_size", 100}});
    progL("Dark Strings", L(snd(S_FM, P_DARKSTR, 64, 64, 30, 95, 90, 120, 100, 30, 10, 0, 0, FM), {{"rev", 105}}), -1, {{"master", 1}, {"s_size", 105}});
    progL("French Horn", L(snd(S_FM, P_HORN, 64, 64, 10, 80, 90, 120, 90, 40, 0, 0, 0, FM), {{"rev", 100}}), -1, {{"master", 2}, {"s_size", 100}});
    progL("Lofi Flute", L(snd(S_FM, P_FLUTE, 64, 64, 15, 60, 90, 120, 85, 50, 8, 0, 0, FM), {{"rev", 95}, {"tape", 80}}), -1,
          {{"master", 0.5}, {"t_wow", 45}, {"t_hiss", 25}, {"s_size", 95}});
    // 19..21 GHOULBOX's hurdy-gurdy: DRONE, DRONES, FIFTH, WHEEL; BUZZ, COUP, BODY, WOBBLE
    auto gurdy = [&](int drn, int dlvl, int fifth, int whl, int a, int r) {   // (BUZZ, COUP, BODY, WOBBLE: k5..k8)
        return snd(S_GURDY, drn, dlvl, fifth, whl, a, 85, 127, r, 40, 0, 0, 0, GU);
    };
    progL("Hurdy Gurdy", L(gurdy(2, 90, 50, 80, 25, 60),
                           {{"k5", 85}, {"k6", K(2, GURDY_COUP)}, {"k7", 70}, {"k8", 40}, {"rev", 70}, {"dly", 10}}), -1,
          {{"master", 3.5}, {"s_size", 85}});
    progL("Drone Wheel", L(gurdy(2, 127, 110, 45, 60, 95),
                           {{"k5", 0}, {"k6", 0}, {"k7", 105}, {"k8", 70}, {"rev", 100}, {"oct", -1}}), -1,
          {{"master", 3}, {"s_size", 105}, {"amb", 50}});
    progL("Bagpipe", L(gurdy(2, 127, 90, 127, 20, 50),
                       {{"k5", 0}, {"k6", 0}, {"k7", 25}, {"k8", 0}, {"rev", 80}}), -1, {{"master", 1.5}, {"s_size", 90}});
    // 22..24 leads, plucks, texture
    progL("Lute", L(snd(S_PHYS, 3, 0, 85, 0, 0, 90, 127, 60, 40, 0, 0, 0, PH), {{"rev", 70}, {"dly", 20}}), -1,
          {{"master", 10}, {"d_sync", 6}, {"d_fb", 35}, {"s_size", 85}});
    progL("Wraith", L(snd(S_PHASE, 6, 0, 90, 0, 30, 90, 110, 80, 105, 1, 0, 0, PD), {{"mono", 1}, {"glide", 55}, {"rev", 90}, {"dly", 40}}), -1,
          {{"master", 11}, {"d_sync", 8}, {"d_fb", 60}, {"d_wear", 70}, {"s_size", 100}});
    progL("Wind Thru Stone", L(snd(S_VOICE, 96, 12, 0, 127, 100, 90, 120, 105, 30, 90, 4, 0, VO), {{"rev", 110}, {"grn", 50}}), -1,
          {{"master", 5.5}, {"amb", 90}, {"crkl", 50}, {"s_size", 115}, {"g_size", 100}});

    // ---- 25.. added in 1.2.0 (appended: the numbers above stay). GR!MM's other factory sounds, GHOULBOX's, layers.
    // pads and drones
    progL("Ash Veil", L(snd(S_VA, 0, 25, 50, 10, 95, 90, 115, 95, 35, 10, 5, 6, VA), {{"rev", 95}, {"tape", 50}}), -1,
          {{"master", 4.5}, {"t_wow", 25}, {"t_tone", 30}, {"s_size", 100}});
    progL("Moss", L(snd(S_VA, 0, 45, 60, 25, 90, 90, 110, 95, 35, 1, 0, 6, VA), {{"rev", 90}, {"grn", 35}}), -1,
          {{"master", 3}, {"s_size", 95}, {"g_size", 95}});
    progL("Black Velvet", L(snd(S_FM, P_SOFTPAD, 40, 64, 30, 95, 90, 115, 100, 40, 0, 0, 0, FM), {{"rev", 100}}), -1,
          {{"master", 3}, {"s_size", 105}, {"s_damp", 70}});
    progL("Abyss", L(snd(S_VA, 1, 5, 52, 30, 100, 90, 120, 110, 25, 30, 5, 0, VA), {{"oct", -1}, {"rev", 100}, {"drv", 40}}), -1,
          {{"master", -4}, {"s_size", 112}, {"dr_amt", 30}, {"amb", 50}});
    progL("Tomb Hum", L(snd(S_WHEEL, 0, 0, 10, 0, 70, 40, 60, 90, 15, 0, 1, 0, WH), {{"oct", -1}, {"rev", 95}, {"tape", 60}}), -1,
          {{"master", 2}, {"t_wow", 30}, {"t_hiss", 20}, {"s_size", 100}});
    progL("Deep Well", L(snd(S_FM, P_ORGAN, 50, 64, 12, 95, 90, 120, 100, 40, 0, 0, 0, FM), {{"oct", -1}, {"rev", 105}, {"dly", 35}}), -1,
          {{"master", 5}, {"d_time", 100}, {"d_fb", 75}, {"d_wear", 90}, {"s_size", 108}});
    // bells
    progL("Iron Bell", L(snd(S_FM, P_BELL, 64, 90, 10, 0, 120, 0, 110, 40, 0, 0, 0, FM), {{"rev", 100}}), -1,
          {{"master", 1}, {"s_size", 108}});
    progL("Glass Harp", L(snd(S_FM, P_BELL, 85, 30, 20, 80, 100, 70, 95, 40, 0, 0, 0, FM), {{"rev", 100}, {"grn", 40}}), -1,
          {{"master", 0.5}, {"s_size", 100}, {"s_damp", 30}});
    progL("Shrine", L(snd(S_FM, P_BELL, 92, 20, 30, 0, 70, 0, 70, 40, 0, 0, 0, FM), {{"rev", 90}, {"dly", 50}, {"cut", 104}}), -1,
          {{"master", 5}, {"d_sync", 8}, {"d_fb", 60}, {"s_size", 95}});
    progL("Music Box", L(snd(S_FM, P_BELL, 90, 20, 0, 0, 75, 0, 60, 40, 0, 0, 0, FM), {{"rev", 70}, {"tape", 110}, {"cut", 106}}), -1,
          {{"master", 5.5}, {"t_wow", 70}, {"t_sat", 40}, {"t_tone", 60}, {"t_hiss", 35}, {"s_size", 85}});
    // voices
    progL("Choir of Ghosts", L(snd(S_VOICE, 0, 14, 30, 40, 85, 90, 90, 95, 100, 1, 0, 0, VO), {{"rev", 105}, {"grn", 30}}), -1,
          {{"master", 2}, {"s_size", 108}});
    progL("Monks", L(snd(S_VOICE, 120, 4, 90, 45, 75, 80, 65, 90, 40, 0, 0, 0, VO), {{"oct", -1}, {"rev", 100}}), -1,
          {{"master", 2}, {"s_size", 100}});
    progL("Whisper Chorus", L(snd(S_VOICE, 20, 14, 20, 110, 80, 90, 115, 95, 35, 10, 3, 6, VO), {{"rev", 100}, {"dly", 30}}), -1,
          {{"master", 4.5}, {"d_time", 95}, {"d_fb", 60}, {"s_size", 105}});
    progL("Vigil", L(snd(S_VOICE, 64, 10, 45, 30, 90, 95, 65, 100, 25, 100, 3, 0, VO), {{"rev", 100}}), -1,
          {{"master", 1}, {"s_size", 100}});
    progL("Plainchant", L(snd(S_VOICE, 0, 9, 85, 18, 80, 90, 120, 106, 40, 0, 0, 0, VO), {{"k5", choir(1)}, {"mono", 1}, {"glide", 40}, {"rev", 105}}), -1,
          {{"master", 4.5}, {"s_size", 110}, {"s_pre", 25}});
    progL("Hollow Hymn", L(snd(S_FM, P_SOFTPAD, 70, 64, 40, 85, 90, 115, 95, 95, 1, 0, 0, FM), {{"rev", 100}}), -1,
          {{"master", 2}, {"s_size", 105}});
    progL("Glass Choir", L(snd(S_FM, P_GLASSCHOIR, 64, 64, 20, 85, 90, 120, 100, 40, 0, 0, 0, FM), {{"rev", 110}}), -1,
          {{"master", 2}, {"s_size", 110}});
    // organs and keys
    progL("Chapel", L(snd(S_WHEEL, 12, 0, 10, 0, 10, 90, 127, 55, 40, 0, 0, 0, WH), {{"rev", 95}}), -1,
          {{"master", 3}, {"s_size", 108}});
    progL("Dead Harpsichord", L(snd(S_FM, P_BELL, 45, 10, 5, 0, 75, 0, 50, 40, 0, 0, 0, FM), {{"rev", 65}, {"tape", 70}}), -1,
          {{"master", 5}, {"t_sat", 50}, {"t_tone", 50}, {"s_size", 85}});
    progL("Cathedral Keys", L(snd(S_PHASE, 3, 0, 30, 6, 0, 105, 20, 80, 40, 0, 0, 0, PD), {{"rev", 100}}), -1,
          {{"master", 3.5}, {"s_size", 110}});
    progL("Bone Piano", L(snd(S_PHYS, 0, 30, 75, 0, 0, 90, 127, 60, 40, 0, 0, 0, PH), {{"rev", 75}, {"dly", 15}}), -1,
          {{"master", 8.5}, {"s_size", 90}});
    progL("Spider Pluck", L(snd(S_FM, P_BELL, 40, 10, 0, 0, 55, 0, 30, 40, 0, 0, 0, FM), {{"rev", 60}, {"dly", 60}}), -1,
          {{"master", 7}, {"d_sync", 5}, {"d_fb", 65}, {"d_wear", 60}, {"s_size", 85}});
    // strings, brass, leads
    progL("Dungeon Brass", L(snd(S_VA, 0, 20, 70, 15, 65, 85, 105, 75, 100, 1, 0, 0, VA), {{"rev", 85}}), -1,
          {{"master", 2.5}, {"s_size", 95}});
    progL("War Horn", L(snd(S_FM, P_REED, 85, 64, 15, 75, 90, 115, 85, 40, 0, 0, 0, FM), {{"rev", 100}, {"dly", 25}}), -1,
          {{"master", 1.5}, {"d_time", 100}, {"d_fb", 40}, {"s_size", 108}});
    progL("Tuba", L(snd(S_FM, P_TUBA, 64, 64, 0, 60, 90, 120, 80, 40, 0, 0, 0, FM), {{"oct", -1}, {"mono", 1}, {"rev", 80}}), -1,
          {{"master", 7.5}, {"s_size", 90}});
    progL("Cello Grave", L(snd(S_PHYS, 1, 50, 115, 60, 85, 95, 70, 95, 95, 1, 0, 0, PH), {{"oct", -1}, {"rev", 90}, {"cut", 84}}), -1,
          {{"master", 10.5}, {"s_size", 100}});
    progL("Sorrow Strings", L(snd(S_VA, 2, 40, 75, 5, 90, 90, 115, 95, 70, 1, 0, 0, VA), {{"rev", 100}}), -1,
          {{"master", 5}, {"s_size", 105}});
    progL("Lantern", L(snd(S_VA, 1, 6, 55, 10, 40, 90, 110, 75, 40, 0, 0, 0, VA), {{"mono", 1}, {"glide", 45}, {"rev", 85}, {"dly", 45}}), -1,
          {{"master", 12}, {"d_sync", 8}, {"d_fb", 55}, {"s_size", 95}});
    progL("Candle", L(snd(S_PHASE, 3, 0, 40, 4, 80, 90, 70, 80, 100, 1, 0, 0, PD), {{"rev", 95}, {"tape", 60}}), -1,
          {{"master", 2}, {"t_wow", 40}, {"s_size", 100}});
    // GHOULBOX's other hurdy-gurdies
    progL("Dance Gurdy", L(gurdy(2, 45, 0, 120, 10, 40),
                           {{"k5", 127}, {"k6", K(4, GURDY_COUP)}, {"k7", 35}, {"k8", 20}, {"rev", 60}}), -1,
          {{"master", 2.5}, {"s_size", 80}});
    progL("Trompette", L(gurdy(2, 60, 0, 95, 15, 55),
                         {{"k5", 110}, {"k6", 0}, {"k7", 60}, {"k8", 30}, {"rev", 75}}), -1,
          {{"master", 1.5}, {"s_size", 90}});
    progL("Vielle", L(gurdy(2, 0, 0, 70, 55, 80),
                      {{"k5", 0}, {"k6", 0}, {"k7", 100}, {"k8", 45}, {"rev", 95}, {"dly", 20}}), -1,
          {{"master", 5.5}, {"s_size", 100}});
    // two layers (Grimoire's)
    progL("Catacomb",
          L(gurdy(2, 127, 110, 45, 60, 95), {{"k5", 0}, {"k6", 0}, {"k7", 105}, {"k8", 70}, {"oct", -1}, {"level", 85}, {"rev", 100}}),
          L(snd(S_VOICE, 0, 15, 80, 12, 95, 90, 105, 112, 40, 0, 0, 0, VO), {{"k5", choir(3)}, {"level", 90}, {"rev", 110}}),
          {{"master", -1.5}, {"s_size", 114}, {"amb", 60}, {"crkl", 30}});
    progL("Black Mass",
          L(snd(S_FM, P_CRYPTORGAN, 64, 64, 0, 20, 90, 127, 80, 40, 0, 0, 0, FM), {{"level", 80}, {"rev", 100}, {"drv", 14}}),
          L(snd(S_VOICE, 95, 12, 75, 15, 90, 90, 120, 110, 40, 0, 0, 0, VO), {{"k5", choir(1)}, {"level", 90}, {"oct", -1}, {"rev", 105}}),
          {{"master", -9}, {"s_size", 112}, {"dr_amt", 25}, {"dr_type", 1}});
    progL("Frost Bells",
          L(snd(S_FM, P_BELL, 85, 40, 20, 0, 110, 0, 105, 40, 0, 0, 0, FM), {{"level", 85}, {"rev", 100}, {"grn", 70}, {"cut", 104}}),
          L(snd(S_FM, P_SOFTPAD, 90, 100, 60, 105, 90, 110, 105, 70, 15, 4, 1, FM), {{"level", 80}, {"rev", 100}, {"grn", 30}}),
          {{"master", 3}, {"g_size", 100}, {"g_dens", 90}, {"g_scat", 70}, {"s_size", 112}, {"s_damp", 25}});
    progL("Dungeon Keep",
          L(snd(S_VA, 0, 20, 70, 15, 65, 85, 105, 75, 100, 1, 0, 0, VA), {{"level", 85}, {"rev", 85}}),
          L(snd(S_FM, P_DARKSTR, 64, 64, 30, 85, 90, 120, 95, 40, 0, 0, 0, FM), {{"level", 85}, {"oct", -1}, {"rev", 95}}),
          {{"master", 0}, {"s_size", 100}});
    progL("Reliquary",
          L(snd(S_PHYS, 3, 0, 85, 0, 0, 90, 127, 75, 40, 0, 0, 0, PH), {{"level", 90}, {"rev", 85}, {"dly", 40}, {"tape", 90}}),
          L(snd(S_VOICE, 20, 14, 20, 110, 95, 90, 115, 100, 35, 10, 3, 6, VO), {{"level", 55}, {"rev", 110}, {"grn", 50}}),
          {{"master", 8.5}, {"t_wow", 50}, {"t_sat", 35}, {"t_hiss", 20}, {"d_sync", 8}, {"d_fb", 60}, {"d_wear", 80}, {"s_size", 105}});
    progL("Procession",
          L(gurdy(2, 127, 90, 127, 20, 50), {{"k5", 60}, {"k6", K(1, GURDY_COUP)}, {"k7", 25}, {"k8", 10}, {"level", 80}, {"rev", 80}}),
          L(snd(S_FM, P_TUBA, 64, 64, 0, 60, 90, 120, 80, 40, 0, 0, 0, FM), {{"level", 90}, {"oct", -2}, {"mono", 1}, {"rev", 85}}),
          {{"master", 1}, {"s_size", 95}});
}
