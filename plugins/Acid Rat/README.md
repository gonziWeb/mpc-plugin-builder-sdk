# Acid Rat

A 303-style acid bass line synth for Akai MPC standalone hardware, by Gonzi Audio. One oscillator (saw or square),
the 4-pole diode-ladder filter, the decay envelope, **accent** and **slide** - plus a Soft / RAT drive, a generator
that writes acid lines and follows the MPC's tempo, and a ping-pong delay.

The engine is the one of [X0X](https://github.com/charlesvestal/fm1-x0x) (a firmware for the M-VAVE FM-1), which is a
port of Robin Schmidt's Open303 with Devilfish-style ranges; the line generator is X0X's TB-3PO. Licence:
**GPL-3.0-only** - see `LICENSING.md` for who made what. Version 1.0.1, uid `G37u` (1.0.1 = 1.0.0 with its own
screen design; sound, parameters and programs unchanged).

## Playing it

It is monophonic, like the original. With **Seq = Off** you play it from pads, keys or a MIDI track:

- **Accent**: a note with velocity 100 or more (`Acc Vel`) is accented: louder, and the filter opens further with the
  "wow" of the accent circuit. `Accent` sets how much. Set pads to full level for accents, or draw velocities.
- **Slide**: a note that starts while the previous one is still held glides to it and does not retrigger the
  envelope. In a sequence, lengthen a note until it overlaps the next one. Notes that do not overlap retrigger.
- Releasing a note while an earlier one is still held slides back to that one.

**Seq = Keys**: the generated line plays for as long as a note is held, transposed by that note (note 60 plays it as
written; `Root` and `Base Oct` set that key). With the MPC stopped the line starts the moment you press; with the MPC
playing it stays on the grid (a pad hit up to a third of a step late still plays that step). One long note in a track
is enough to place the line in a sequence, and other notes move it to other keys.

**Seq = Song**: the line plays whenever the MPC plays. The last note received transposes it (back to "as written" each
time playback starts).

The line is fully determined by `Seed`, `Length`, `Density`, `Seq Accent`, `Seq Slide`, `Oct Range`, `Root`, `Scale`
and `Base Oct`: the same settings always give the same line, and it is saved with the project as those settings.
`Mutate` re-rolls about a quarter of the steps every 1, 2, 4 or 8 bars, so the line drifts; it starts again from the
seed's line each time playback starts.

## Parameters (in saved order - permanent)

| # | Name | Range | What it does |
|---|---|---|---|
| 0 | Master | -60..+6 dB | Output level |
| 1 | Cutoff | 314..2394 Hz | Resting cutoff (the original's range; the envelope goes far above) |
| 2 | Reso | 0..100 % | Resonance |
| 3 | Env Mod | 0..100 % | How far the decay envelope opens the filter |
| 4 | Decay | 200..2000 ms | Filter envelope decay of normal notes |
| 5 | Accent | 0..100 % | Strength of accented notes |
| 6 | Wave | Saw / Square | |
| 7 | Tune | +-100 cents | Applies from the next note |
| 8 | Octave | -2..+2 | Shifts every note |
| 9 | Drive | 0..100 % | 0 = bypassed |
| 10 | Drive Type | Off / Soft / RAT | Soft: warm tanh clip. RAT: op-amp distortion pedal model |
| 11 | Slide | 2..360 ms | Slide time (original: 60 ms) |
| 12 | Acc Decay | 30..3000 ms | Filter envelope decay of accented notes (original: 200 ms) |
| 13 | Acc Vel | 1..127 | Velocity from which a played note is accented |
| 14 | Seq | Off / Keys / Song | See above |
| 15 | Rate | 1/8, 1/8T, 1/16, 1/16T, 1/32 | Step length |
| 16 | Length | 1..64 steps | |
| 17 | Seed | 1..999 | Which line |
| 18 | Density | 0..100 % | How many steps play |
| 19 | Seq Accent | 0..100 % | Chance of an accent on a step |
| 20 | Seq Slide | 0..100 % | Chance of a slide into the next step |
| 21 | Oct Range | 1..3 | Octaves the line may use |
| 22 | Root | C..B | |
| 23 | Scale | Minor, Phrygian, Harm Minor, Min Pent, Dorian, Major | |
| 24 | Base Oct | 0..3 | Octave of the root (1 with Root A = A1, 55 Hz) |
| 25 | Mutate | Off, 1, 2, 4, 8 bars | |
| 26 | Gate | 10..95 % | Note length within a step (a slide always holds to the next step) |
| 27 | Swing | 0..100 % | Odd steps up to half a step late (not on triplet rates) |
| 28 | Delay Mix | 0..100 % | Level of the echoes (the dry sound stays) |
| 29 | Delay Time | 1/16, 1/8T, 1/8, 1/8D, 1/4 | Follows the MPC tempo |
| 30 | Delay Fdbk | 0..90 % | |

Screen: page **ACID** (the sound: the four classic knobs in the top row) and page **LINE** (the generator).
The screen has its own look (black panel, acid-green print, chrome-ringed knobs), in `skins/`. It is generated, not
hand-edited: after changing `ui.json` or the parameters, build, run `python3 "plugins/Acid Rat/tools/make_skin.py"`
(needs Pillow; `--preview out.png` draws the pages without an MPC), then build again. Not yet checked on the MPC's
screen; if it misbehaves there, delete `skins/` and rebuild to get the SDK's standard kit back.

Programs: Acid Rat, Squelch, Hollow Square, Rat Bite, Soft Burn, Devil Slide, Dub Acid, Line: Keys, Line: Rave.

## Tips

- The classic move: Reso around 80 %, Env Mod 60-80 %, short Decay, then ride Cutoff. Cutoff, Reso and Env Mod are
  smoothed, so sweeping them from a Q-Link does not step.
- With Cutoff and Env Mod both near the top and high resonance, accented notes whistle very high (the filter goes up
  to 20 kHz for a few milliseconds). That is the model's behaviour, not a fault; lower Env Mod or Accent to tame it.
- The engine's level sits before the drive, so Drive gets fiercer on accents. Use Master to trim afterwards.
- Decay and Tune apply from the next note, as on the engine they come from.

## What is not there

- No step editor on the screen: write your own lines in the MPC's sequencer (Seq = Off), or let the generator do it.
  The generator's line cannot be edited step by step or exported as MIDI (plugins cannot send MIDI to the MPC).
- No pitch bend, no mod wheel, no sustain pedal.
- Of the Devilfish modifications only the slide time and accent decay ranges; the rest is a stock 303 model.
- Mono signal apart from the delay's echoes.

## CPU and testing

One voice, 4x oversampled: about 0.5 % of a desktop core in the simulator, which should be a few percent of one core
on a first-generation MPC One (estimated, not yet measured on the device). Checked: SDK validator, host simulator
(block sizes 32-512, parameter sweep, state round trip), sequencer timing at several block sizes, with swing and across
a loop, 44.1 and 48 kHz. Not yet checked on an MPC.

## Build

```
python tools/build.py "plugins/Acid Rat"
python3 "plugins/Acid Rat/tools/make_skin.py"     # only after changing ui.json or parameters; then build again
python tools/package.py "plugins/Acid Rat"
g++ -O2 -std=c++17 -Iframework gonzi/tools/render_acid.cpp -o .sdk-tools/render_acid -ldl
.sdk-tools/render_acid "plugins/Acid Rat/build/native/acid-rat.so" out.wav 128 0 1 2 3 4 5 6 7 8
```

Install: copy `Gonzi Audio - VST - Acid Rat` into `VST2 Plugins/` at the root of a USB drive or SD card. The MPC needs
MPC OS 3.10.1 or later with Preferences -> Enhancements (Beta) -> Enable Enhancements and External Plugin Support on.
