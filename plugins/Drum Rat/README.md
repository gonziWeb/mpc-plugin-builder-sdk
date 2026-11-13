# Drum Rat

An 808 and a 909 in one drum machine for Akai MPC standalone hardware, by Gonzi Audio. Eleven tracks, each of which
can play either machine, and a 16-step sequencer with accent and eight patterns that follows the MPC's tempo.

Both engines come from [X0X](https://github.com/charlesvestal/fm1-x0x) (a firmware for the M-VAVE FM-1): its 808 is a
port of 8W8, where every sound is a model of the original circuit; its 909 is a port of 9W9 / ER-99, with modelled
kick, snare, toms, rim and clap, and sampled hats, crash and ride as on the real machine. Licence: **GPL-3.0-only** -
see `LICENSING.md` for who made what. Version 1.0.0, uid `GVUx`. Instrument, stereo out, 83 parameters, 8 programs.

## The eleven tracks

| Track | 808 | 909 |
|---|---|---|
| BD | bass drum | bass drum |
| SD | snare | snare |
| LT, MT, HT | toms, or congas (`Congas`) | toms |
| RS | rim shot, or claves (`Claves`) | rim shot |
| CP | hand clap, or maracas (`Maracas`) | hand clap |
| CH, OH | closed and open hat (the closed one cuts the open one) | the same |
| CY | cymbal | crash |
| PC | cowbell | ride |

**Kit** chooses the machine: `808`, `909`, or `Mix`, where every track uses its own `Mach` switch (pages DRUMS 1-3),
for example a 909 kick under 808 hats and clap.

Each track has **Level**, **Tune** and **Decay**. Tune and Decay are offsets from -100 to +100: 0 is that machine's
own factory setting, so a track switched from one machine to the other always lands on its normal sound. Level 80 %
is the factory level. `BD Tone`, `BD Attack`, `SD Tone` and `SD Snappy` work the same way.

## Playing it from the pads

**Pad Map = MPC** (default): every octave holds the whole kit, so it works from any pad bank or octave of a plugin
track.

| Note | C | C# | D | D# | E | F | F# | G | G# | A | A# | B |
|---|---|---|---|---|---|---|---|---|---|---|---|---|
| Track | BD | RS | SD | CP | CH | OH | PC | LT | CY | MT | PC | HT |

**Pad Map = GM**: General MIDI drum notes (35/36 BD, 37 RS, 38/40 SD, 39 CP, 41/43 LT, 45/47 MT, 48/50 HT, 42/44 CH,
46 OH, 49/52/55/57 CY, the rest PC), for MIDI files and the MPC's "Classic" pad layout.

Velocity sets the loudness; velocity 127 is an accented hit. Notes below 24 do not play drums: 12 to 19 choose
pattern 1 to 8.

## The sequencer (pages SEQ and STEPS)

- **Seq**: `Off` (pads and MIDI tracks only), `Song` (the pattern plays whenever the MPC plays, locked to its
  position), `Run` (the pattern plays all the time at the MPC's tempo, also with the MPC stopped; it locks to the MPC
  when it plays).
- **Pattern** 1-8. They start as House, Electro, Techno, Boom Bap, Trap, Break, Latin and Acid House, all there to be
  overwritten. Each has its own length (**Steps**, 1-16). A change takes effect at once, on the step the clock is on.
  Notes 12 to 19 choose pattern 1 to 8, so a MIDI track can chain patterns.
- **Track** chooses which track's steps are on the STEPS page, like the instrument selector of the real machines.
  With **Pad Sel** on (default), playing a pad while Seq is on shows that drum's steps: stay on the STEPS page, hit
  the snare pad, edit the snare.
- **STEPS**: sixteen keys, four to a row (a row is a beat; the four Q-Link banks are the four beats). Each step is
  `Off`, `On` or `Acc` (accent).
- **Accent**: how much quieter an `On` step is than an `Acc` step. 100 % is the machines' own difference, 0 % plays
  every step at accent level.
- **Rate** 1/8, 1/16, 1/32. **Swing** moves every second step up to half a step late.
- **Clear Trk** empties the track shown, **Clear Pat** the whole pattern. Both spring back by themselves.

The eight patterns are saved with the project. Programs change the sound only: the pattern, the sequencer settings
and the pad map stay as they are.

## Parameters (in saved order - permanent)

| # | Name | Range | What it does |
|---|---|---|---|
| 0 | Master | -40..+6 dB | Output level |
| 1 | Kit | 808 / 909 / Mix | Which machine plays; Mix = each track's Mach switch |
| 2 | Accent | 0..100 % | See above |
| 3 | Drive | 0..100 % | Each voice's drive stage, added to the machine's own setting (the 909 has some by default) |
| 4 | Drive Type | Diode, Clip, SAT, BFZ, PDIST, Fold, Crush | The drive stage's character (Diode is the 909's own) |
| 5 | Width | 0..100 % | Stereo spread of the tracks (kick and snare stay in the middle) |
| 6-8 | Congas, Claves, Maracas | Off / On | The 808's alternative sounds |
| 9-41 | BD .. PC: Level, Tune, Decay | 0..100 %, -100..+100, -100..+100 | Three per track, in track order |
| 42 | BD Tone | -100..+100 | 808: the kick's tone filter. 909: the depth of its pitch sweep |
| 43 | BD Attack | -100..+100 | The kick's click |
| 44 | SD Tone | -100..+100 | |
| 45 | SD Snappy | -100..+100 | The snare's noise |
| 46-56 | BD .. PC Mach | 808 / 909 | Used when Kit = Mix |
| 57 | Pad Map | MPC / GM | |
| 58 | Seq | Off / Song / Run | |
| 59 | Pattern | 1..8 | |
| 60 | Steps | 1..16 | The pattern's length |
| 61 | Rate | 1/8, 1/16, 1/32 | |
| 62 | Swing | 0..100 % | |
| 63 | Track | BD .. PC | The track shown in S1..S16 |
| 64 | Pad Sel | Off / On | |
| 65, 66 | Clear Trk, Clear Pat | momentary | |
| 67-82 | S1..S16 | Off / On / Acc | The steps of the track shown |

The sixteen step parameters are the track on the screen. Choosing another track or pattern loads its steps into them,
so do not automate them; automate `Pattern` instead (or use notes 12 to 19).

Programs: Boom 808, Club 909, Rat Mix (909 kick, clap and crash with the 808's snare, hats and cowbell), Long Boom
(a long, low 808 kick), Hard 909, Latin 808 (congas, claves, maracas: try pattern 7), Tight 909, Dirt Box.

## What is not there

- **No themed screen yet**: the pages use the SDK's standard knobs, faders and switches (amber), and the steps are a
  4 x 4 grid of three-way switches, not a row of sixteen keys with a running light. There is no running light at all.
- On the 909 the rim shot has no Decay (as in the engine), and the 909's toms have an Attack control that is not
  brought out.
- In Kit = Mix, a closed hat only cuts the open hat of the same machine.
- No per-step velocity, flam, probability or parameter locks; no pattern chaining inside the plugin (chain with notes
  12 to 19 from a MIDI track); patterns cannot be exported as MIDI (plugins cannot send MIDI to the MPC).
- No reverb or delay sends: X0X's effects were not ported. Use the MPC's insert and send effects.
- One stereo output: no separate outputs per drum.
- The engines run at 44.1 kHz. In a 48 kHz project their output is resampled with linear interpolation, which costs
  a little of the very top of the hats.
- The first time a second processor core renders the plugin, the system may allocate a few hundred bytes for the
  engines' per-thread variables (once per thread).

## Levels, CPU and testing

The two machines are trimmed against each other by measurement (808 x 0.80, 909 x 0.18; the 808's cowbell starts at
half level because it is 6 dB hotter than the rest of its kit), so that the same pattern has about the same loudness
on both (-17.6 and -18.4 dBFS RMS for the House pattern). A soft limiter rounds off anything above -2 dBFS.

CPU: 0.2 to 0.4 % of a desktop core in the simulator with a full pattern playing (0.7 % at 48 kHz); idle voices cost
nothing. That should be a few percent of one core on a first-generation MPC One (estimated, not measured on the
device).

Checked: SDK validator; host simulator (block sizes 32-512, parameter sweep, state round trip, silence);
`gonzi/tools/test_drum_rat.cpp` (26 checks: step timing at three block sizes and at 48 kHz on both machines, Song /
Run / Off, pads and pad maps, Kit Mix, the step row following Track and Pattern, editing, pattern length, Clear,
swing, a jump of the song position, programs keeping the patterns, saved patterns coming back, two instances, 150
random extreme settings); `gonzi/tools/test_drum_engines.cpp` (the engines give X0X's samples exactly).
**Not yet tried on an MPC**, and the ARM builds were not run under emulation this time (they pass the validator):
CPU on the One, whether the screen follows values the plugin changes itself (the steps of a newly chosen track or
pattern, a Clear key springing back; if it does not, the pattern still plays and edits correctly, and leaving the
page and coming back shows the right values), and which notes the MPC's pads send to a plugin track.

## Build

```
python tools/build.py "plugins/Drum Rat"
python tools/package.py "plugins/Drum Rat"
g++ -O2 -std=c++17 -Iframework gonzi/tools/render_seq.cpp -o .sdk-tools/render_seq -ldl
.sdk-tools/render_seq "plugins/Drum Rat/build/native/drum-rat.so" out.wav 122 2 0/58=0.5 1/58=0.5   # program/param=normalised
.sdk-tools/render_seq "plugins/Drum Rat/build/native/drum-rat.so" hits.wav 120 hits 0 1                # every drum, soft then accent
g++ -O2 -std=c++17 -Iframework gonzi/tools/test_drum_rat.cpp -o .sdk-tools/test_drum_rat -ldl
.sdk-tools/test_drum_rat "plugins/Drum Rat/build/native/drum-rat.so"
```

`src/x0x/x0x_drum_samples.h` and `x0x_drum_tables.h` are generated: `python3 tools/gen_drum_samples.py
src/x0x/x0x_drum_samples.h` (X0X's script, unchanged) reads the recordings from `assets/909/` next to the `tools`
folder it is in - in this project they are kept in `tools/909/`, so run it from a checkout of X0X or adjust `ASSETS`.

Install: copy `Gonzi Audio - VST - Drum Rat` into `VST2 Plugins/` at the root of a USB drive or SD card. The MPC needs
MPC OS 3.10.1 or later with Preferences -> Enhancements (Beta) -> Enable Enhancements and External Plugin Support on.
