# Armada

A fleet of synth engines for the MPC, by Gonzi Audio. Instrument, 89 engines, GPL-3.0-only.

In the summer of 2026 the M-VAVE FM-1 (a small FM synth) got an open firmware, Felucca by Leo Kuroshita (Hügelton
Instruments), and then dozens of firmwares built on it, each with engines of its own. Armada carries the audio core
of 18 of those firmwares, plus Plaits, the macro-oscillator of Mutable Instruments (Emilie Gillet), and plays any of
their engines from one instrument on the MPC. The code that makes the sound is the firmwares' own; the complete
list of engines, where each one comes from and what its parameters are is in [ENGINES.md](ENGINES.md), the
licences in [LICENSING.md](LICENSING.md).

## Requirements and installation

- MPC OS 3.10.1 or later, with **Preferences > Enhancements (Beta) > Enable Enhancements** and **External Plugin
  Support** switched on.
- Copy the folder `Gonzi Audio - VST - Armada` into a folder named `VST2 Plugins` at the root of a USB drive or SD
  card, and load Armada as a plugin program.

## Playing it

One instance plays one engine. For several engines at once, load Armada on several tracks.

| Page | What is on it |
|---|---|
| MAIN | **Engine** (89, a menu), **Sound** (the engine's own factory sounds), Master, Voice (poly / mono / legato / unison); the amplitude envelope; Cutoff and Low Cut (the plugin's own filters after the engine), Octave, Vel Sens; Glide, Detune, Spread, Bend |
| ENGINE 1, ENGINE 2 | **E1 to E32**, the engine's own parameters. What each one is depends on the engine, so its value on the screen starts with its name: `CUT 1.2 kHz`, `WAVE SAW`. A slot the engine does not use shows `-`. The firmwares' engines have 8; OberVave, Shine On and Plaits have more |
| MOD | the envelope to the filter, the pitch and the engine's "shape"; the LFO and where it goes; the filter type; Tune |
| FX | the sends into the firmware's own drive, chorus, delay and reverb, and those effects' settings |

Choosing an Engine or a Sound loads that sound: E1 to E32, the envelope, the voice mode and the sends take the
sound's values, and are yours to change from there. A saved project comes back with your changes. The 152 programs
are the first one or two sounds of each engine.

Not every firmware has every shared parameter (Melodee has no sends, Plaits has no chorus): what an engine does not
have does nothing on it. Engines with envelopes of their own (FM6, Pro-5, Pro-600, OPX) ignore Attack to Release.

Sounds not included: the Prophet-5, CZ-1 and Prophet-600 factory banks that Melodee and Guru-600 carry are the
manufacturers' programs, so Armada does not ship them. Pro-5 has 12 programs of our own instead, Pro-600 offers
Guru-600's 55 own sounds, and Melodee's CZ-1 engine (which plays only factory tones) is not offered.

## What works, and what has not been tried

- Built for 32-bit ARM (the first MPC One, MPC Live, X) and 64-bit ARM; the host simulator passes (0 failures)
  on the computer build, and every program was rendered to audio there.
- Every engine was put through random settings of all its parameters while notes played (`tools/test_pack.cpp
  fuzz`): no crash, no non-finite sample. Two instances side by side give the same samples as one alone.
- **Not yet tried on an MPC.** In particular: the processor load on the MPC One (on a desktop the programs take
  0.15 to 1.4 % of a core, half of them under 0.4 %; the MPC is several times slower), the 89-entry Engine menu on the
  touch screen, and whether the screen follows the parameters when a sound is loaded.
- The ARM builds themselves were not run here (no emulator in the build environment): the tests ran on the
  computer build of the same sources.
- The engines render at 44.1 kHz. At another project rate the output is resampled (linear interpolation).
- Each pack an instance uses takes about 300 KB of memory, allocated the first time one of its engines is chosen.
- An engine parameter that selects a user sample or user wavetable (the FM-1 keeps those in its flash) finds the
  slot empty here.

## Building

`python tools/build.py plugins/Armada` (a first build takes several minutes: 21 large source files, three targets).

The files `src/pk_<pack>.cpp` are generated: `tools/make_pack.py` takes a firmware's own sources (kept unchanged in
`upstream/<pack>.tar.xz`), flattens them into one file, moves every variable with static storage into a block per
plugin instance (so several Armadas, on several threads, never share state) and adds the C++ around it. To change a
pack, edit `tools/packs/<pack>.py` (or the firmware's sources) and run it again; `tools/make_upstream.py` explains
how. `tools/test_pack.cpp` tests one pack on its own, `tools/pack_info.cpp` lists its engines and parameters.

Adding an engine: append a line to `src/armada_engines.h`. The order of that table, like the order of the plugin's
parameters, is permanent once projects exist.
