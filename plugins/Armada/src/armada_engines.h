// SPDX-License-Identifier: GPL-3.0-only
// Copyright (C) 2026 Gonzi Audio
// Armada: the engines, in the order of the Engine parameter. PERMANENT: projects store the number.
// Append new engines at the end; never reorder or remove (an engine that goes away keeps its line and plays silence).
//   {name on the MPC, pack, the engine's name inside that firmware, first sound, number of sounds (0 = all)}
// A range of sounds: the firmware's bank holds sounds that are not ours to ship; the plugin offers the others.
#ifndef ARMADA_ENGINES_H
#define ARMADA_ENGINES_H
#include "armada_pack.h"

#define ARMADA_PACKS(X) X(felucca) X(melodee) X(guru600) X(opx) X(caravel) X(switchblade) X(minitone) X(sqrl) X(golana) X(atlatl) X(araminta) X(kriechstrom) X(dirge) X(jangada) X(jiant) X(clipper) X(obervave) X(shineon) X(plaits)

#define X(n) extern const armada::Pack armada_pack_##n;
ARMADA_PACKS(X)
#undef X

namespace armada {
struct EngineRef { const char *name; const Pack *pack; const char *fw; int base, count; };
static const EngineRef kEngines[] = {
    // Felucca
    {"Analog", &armada_pack_felucca, "ANALOG", 0, 0},
    {"FM6", &armada_pack_felucca, "FM6", 0, 0},
    {"Phase", &armada_pack_felucca, "PHASE", 0, 0},
    {"LoFi", &armada_pack_felucca, "LOFI", 0, 0},
    {"Sample", &armada_pack_felucca, "SAMPLE", 0, 0},
    {"Voice", &armada_pack_felucca, "VOICE", 0, 0},
    {"Trio", &armada_pack_felucca, "TRIO", 0, 0},
    {"Wheel", &armada_pack_felucca, "WHEEL", 0, 0},
    {"Grain", &armada_pack_felucca, "GRAIN", 0, 0},
    {"Phys", &armada_pack_felucca, "PHYS", 0, 0},
    {"Noise", &armada_pack_felucca, "NOISE", 0, 0},
    {"Slice", &armada_pack_felucca, "SLICE", 0, 0},
    {"Drum", &armada_pack_felucca, "DRUM", 0, 0},
    // Melodee (keremimo)
    {"Pro-5", &armada_pack_melodee, "PROPHET", 0, 0},
    {"SID", &armada_pack_melodee, "SID", 0, 0},
    {"X0X Kit", &armada_pack_melodee, "DRUM", 0, 0},
    // Guru-600 (Marc Masse): its 55 own sounds (201-255)
    {"Pro-600", &armada_pack_guru600, "P600", 200, 55},
    // Felucca OPX (thomasgregorymarsden-code)
    {"OPX", &armada_pack_opx, "OPX", 0, 0},
    // Caravel (bonszaimusic)
    {"SEM", &armada_pack_caravel, "SEM", 0, 0},
    {"Hades", &armada_pack_caravel, "HADES", 0, 0},
    {"Drone", &armada_pack_caravel, "DRONE", 0, 0},
    {"Ensemble", &armada_pack_caravel, "ENSEMB", 0, 0},
    // Switch Blade (OllieNicklinUK)
    {"OB-Z", &armada_pack_switchblade, "OB-Z", 0, 0},
    {"Void Wave", &armada_pack_switchblade, "WAVE", 0, 0},
    {"Nano", &armada_pack_switchblade, "NANO", 0, 0},
    {"Drift", &armada_pack_switchblade, "JUNO", 0, 0},
    // Mini-Tone (eldontyrellcorp)
    {"Swarm", &armada_pack_minitone, "SWARM", 0, 0},
    {"FM Tone", &armada_pack_minitone, "FMTONE", 0, 0},
    {"Wavetone", &armada_pack_minitone, "WAVETONE", 0, 0},
    {"FM Drum", &armada_pack_minitone, "FMDRUM", 0, 0},
    {"SID Lite", &armada_pack_minitone, "SID", 0, 0},
    {"Chip", &armada_pack_minitone, "CHIP", 0, 0},
    {"Dual VCO", &armada_pack_minitone, "DUALVCO", 0, 0},
    {"Analog 4", &armada_pack_minitone, "ANALOG4", 0, 0},
    {"Braids", &armada_pack_minitone, "BRAIDS", 0, 0},
    {"TB-303", &armada_pack_minitone, "TB303", 0, 0},
    {"TR-808", &armada_pack_minitone, "TR808", 0, 0},
    // SQRL-1 (hed0rah)
    {"Lizard", &armada_pack_sqrl, "LIZARD", 0, 0},
    {"Syllable", &armada_pack_sqrl, "SYLLABLE", 0, 0},
    {"Voder", &armada_pack_sqrl, "VODER", 0, 0},
    {"Shock", &armada_pack_sqrl, "SHOCK", 0, 0},
    // Golana (Gadlele)
    {"Golana", &armada_pack_golana, "GOLANA", 0, 0},
    // Skiff Atlatl (LIkst4)
    {"Atlatl", &armada_pack_atlatl, "SYNTH", 0, 0},
    {"Mini", &armada_pack_atlatl, "FMINI", 0, 0},
    // Araminta (Wulgaren)
    {"Wavetable", &armada_pack_araminta, "WAVETABLE", 0, 0},
    {"Drum-X", &armada_pack_araminta, "DRUM-X", 0, 0},
    // KRIECHSTROM (Xforsa)
    {"Glitch", &armada_pack_kriechstrom, "GLCH", 0, 0},
    // DIRGE (sinscum)
    {"Dirge", &armada_pack_dirge, "DRONE", 0, 0},
    {"Dirge Bass", &armada_pack_dirge, "BASS", 0, 0},
    // Jangada (zednaked)
    {"4-Op FM", &armada_pack_jangada, "DIGITAL", 0, 0},
    {"Robo", &armada_pack_jangada, "ROBO", 0, 0},
    // JIANT FM (juanjiant-bit)
    {"Animal", &armada_pack_jiant, "ANIMAL", 0, 0},
    {"Bytebeat", &armada_pack_jiant, "LOFI", 0, 0},
    // Clipper (3dSam)
    {"Hybrid", &armada_pack_clipper, "HYBRID", 0, 0},
    {"Acid", &armada_pack_clipper, "ACID", 0, 0},
    {"Solina", &armada_pack_clipper, "STRINGS", 0, 0},
    {"Macro", &armada_pack_clipper, "MACRO", 0, 0},
    {"VPS", &armada_pack_clipper, "VPS", 0, 0},
    {"Terrain", &armada_pack_clipper, "TERRAIN", 0, 0},
    {"FMX", &armada_pack_clipper, "FMX", 0, 0},
    {"Wave Seq", &armada_pack_clipper, "WSEQ", 0, 0},
    {"Juno", &armada_pack_clipper, "JUNO", 0, 0},
    {"Vector", &armada_pack_clipper, "VECTOR", 0, 0},
    // OberVave, Shine On (zvenson)
    {"OberVave", &armada_pack_obervave, "OBERVAVE", 0, 0},
    {"Shine On", &armada_pack_shineon, "SHINE ON", 0, 0},
    // Plaits (Emilie Gillet, Mutable Instruments): its 24 models
    {"P VA VCF", &armada_pack_plaits, "VA VCF", 0, 0},
    {"P Phase Dist", &armada_pack_plaits, "PHASE DIST", 0, 0},
    {"P 6-Op FM 1", &armada_pack_plaits, "6-OP FM 1", 0, 0},
    {"P 6-Op FM 2", &armada_pack_plaits, "6-OP FM 2", 0, 0},
    {"P 6-Op FM 3", &armada_pack_plaits, "6-OP FM 3", 0, 0},
    {"P Terrain", &armada_pack_plaits, "WAVE TERRAIN", 0, 0},
    {"P String Mach", &armada_pack_plaits, "STRING MACH", 0, 0},
    {"P Chiptune", &armada_pack_plaits, "CHIPTUNE", 0, 0},
    {"P Pair of VA", &armada_pack_plaits, "PAIR OF VA", 0, 0},
    {"P Waveshaper", &armada_pack_plaits, "WAVESHAPER", 0, 0},
    {"P 2-Op FM", &armada_pack_plaits, "2-OP FM", 0, 0},
    {"P Formant", &armada_pack_plaits, "GRAIN FORMANT", 0, 0},
    {"P Harmonic", &armada_pack_plaits, "HARMONIC", 0, 0},
    {"P Wavetable", &armada_pack_plaits, "WAVETABLE", 0, 0},
    {"P Chords", &armada_pack_plaits, "CHORDS", 0, 0},
    {"P Speech", &armada_pack_plaits, "SPEECH", 0, 0},
    {"P Swarm", &armada_pack_plaits, "SWARM", 0, 0},
    {"P Noise", &armada_pack_plaits, "FILTERED NOISE", 0, 0},
    {"P Particle", &armada_pack_plaits, "PARTICLE", 0, 0},
    {"P String", &armada_pack_plaits, "INHARM STRING", 0, 0},
    {"P Modal", &armada_pack_plaits, "MODAL", 0, 0},
    {"P Bass Drum", &armada_pack_plaits, "BASS DRUM", 0, 0},
    {"P Snare", &armada_pack_plaits, "SNARE DRUM", 0, 0},
    {"P Hi-Hat", &armada_pack_plaits, "HI-HAT", 0, 0},
};
enum { kNumEngines = sizeof(kEngines) / sizeof(kEngines[0]) };
} // namespace armada
#endif
