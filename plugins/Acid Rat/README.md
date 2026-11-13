# Acid Rat

A 303-style acid bass line synth for Akai MPC standalone hardware, by Gonzi Audio. One oscillator (saw or square),
the 4-pole diode-ladder filter, the decay envelope, **accent** and **slide** - plus a Soft / RAT drive, a generator
that writes acid lines and follows the MPC's tempo, and a ping-pong delay.

The engine is the one of [X0X](https://github.com/charlesvestal/fm1-x0x) (a firmware for the M-VAVE FM-1), which is a
port of Robin Schmidt's Open303 with Devilfish-style ranges; the line generator is X0X's TB-3PO. Licence:
**GPL-3.0-only** - see `LICENSING.md` for who made what. Version 1.2.0, uid `G37u`.

- 1.1.0 added the **step sequencer**: eight 16-step patterns you write yourself (page STEP).
- 1.2.0 goes **beyond the 303** (pages TONE and LFO): oscillator blend, pulse width, sub-oscillator and noise; cutoff
  shift, key tracking, filter FM and a low cut; attack, decay and release for the volume, an attack for the filter
  envelope, the size of the accent sweep; a tempo-synced LFO to cutoff, pitch and pulse width.

Every addition starts at a value that changes nothing: with pages TONE and LFO left alone the plugin sounds exactly
as 1.0 did (checked sample for sample), and projects made with earlier versions load unchanged.

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

## Beyond the 303 (pages TONE and LFO)

**TONE**

| Control | Range | What it does | Leaves the 303 alone at |
|---|---|---|---|
| Blend | 0..100 % | Mixes the other waveform in (saw into the square, or the reverse) | 0 |
| Pulse W | 5..95 % | The square's duty cycle (also heard through Blend) | 46.9 |
| Sub, Sub Oct | 0..100 %, 1 or 2 down | A square sub-oscillator below the note, into the filter | Sub 0 |
| Noise | 0..100 % | White noise into the filter | 0 |
| Cut Shift | -3..+3 oct | Moves the whole cutoff range: the knob on page ACID stays the 303's, this takes it lower or much higher | 0 |
| Key Track | 0..100 % | The cutoff follows the note (100 %: an octave up the keys opens it an octave) | 0 |
| Filter FM | 0..100 % | The oscillator opens the filter at audio rate: grit and growl, more with Sub and Noise | 0 |
| Low Cut | Off, 21..800 Hz | Highpass after the synth, to thin the bass | Off |
| Env Attack | 3..300 ms | How fast the filter envelope rises: slow gives a "wow" on every note | 3 |
| Acc Sweep | 0..200 % | How far an accent opens the filter | 100 |
| Amp Attack | Off..500 ms | Volume fade-in (time to full level) | Off |
| Amp Decay | 30..8000 ms | How the volume falls while a note is held: short = plucked, long = sustained | 1230 |
| Release | 303, 1..2000 ms | How the volume falls after the note ends ("303": 1 ms, 50 ms on accents) | 303 |

**LFO**: Shape (sine, triangle, falling saw, square, sample & hold), Sync (free in Hz, or a division of the MPC's
tempo from 8 bars to 1/8 triplet; synced, it follows the song position while the MPC plays), and three depths:
LFO>Cut (100 % = 3 octaves up and down), LFO>Pitch (up to 12 semitones) and LFO>PW (the square's duty).

Filter FM only opens the filter (from the knob's cutoff upwards), which keeps the resonance stable at any setting.
With a volume attack, decay or release of your own, the accent's loudness follows that shape too.

## The step sequencer (page STEP)

Set **Source** to **Pattern** (page STEP, "PLAYS", or page LINE) and the sequencer plays the pattern on the screen
instead of the generator's line. Seq, Rate, Gate and Swing work as before: Seq = Keys plays it while a note is held
(transposed by that note, 60 = as written), Seq = Song whenever the MPC plays. With Seq = Off nothing plays by itself.

Sixteen columns, one a step, as on the original's panel:

| Row | What it is |
|---|---|
| RUN | A red light on the step being played (and, in Write, on the step the next note goes to) |
| LAST | Touch a step to make it the last one: the pattern's length, 1 to 16 |
| NOTE | The step's note, C to the C above: drag the bar, or turn a Q-Link (the four banks are the four beats) |
| OCT | Up / middle / down: the octave. Down starts at C1 (note 24), middle at C2, up at C3 |
| GATE | Lit: the step plays. Dark: a rest |
| ACCENT | Lit: the step is accented |
| SLIDE | Lit: this step glides into the next one, which does not retrigger. A slide onto the same note is a tie |

Along the top:

- **PATTERN 1-8**: which pattern is shown, edited and played. Each has its own length. Pattern 1 starts as a classic
  line in A, 2 to 8 as generated lines, all there to be overwritten. Notes 12 to 19 (the octave below the lowest pad
  bank) choose pattern 1 to 8 as well, so a MIDI track can chain patterns. A change takes effect at once, at the step
  the clock is on.
- **WRITE**: step-writing. Turn it on and play notes on the pads: the first goes into step 1, the next into step 2,
  and so on (velocity 100 or more = accent), each one sounding as you play it. After the last step Write turns itself
  off. Notes are folded into the three octaves the pattern can hold (24 to 60). Rests, slides and corrections: on the
  grid. While Write is on the sequencer waits.
- **DICE>PAT**: copies the generator's current line (page LINE) into the pattern, to edit it by hand. Lines longer
  than 16 steps are cut.
- **CLEAR**: empties the pattern shown (all rests).

The eight patterns are saved with the project. Programs change the sound, not the patterns.

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
| 31 | Source | Dice / Pattern | Which line the sequencer plays |
| 32 | Pattern | 1..8 | The pattern shown and played |
| 33 | Steps | 1..16 | Its length |
| 34 | Write | Off / On | Step-writing from the pads |
| 35 | Dice>Pat | momentary | Copy the generated line into the pattern |
| 36 | Clear | momentary | Empty the pattern |
| 37 | Pos | -, 1..16 | Read-only: the step being played (drives the running light) |
| 38-53 | S1..S16 Note | C..C' | |
| 54-69 | S1..S16 Oct | Down / Mid / Up | |
| 70-85 | S1..S16 Gate | Off / On | |
| 86-101 | S1..S16 Acc | Off / On | |
| 102-117 | S1..S16 Slide | Off / On | |
| 118 | Blend | 0..100 % | See "Beyond the 303" |
| 119 | Sub | 0..100 % | |
| 120 | Sub Oct | 1 down / 2 down | |
| 121 | Pulse W | 5..95 % | |
| 122 | Noise | 0..100 % | |
| 123 | Cut Shift | -3..+3 oct | |
| 124 | Key Track | 0..100 % | |
| 125 | Filter FM | 0..100 % | |
| 126 | Low Cut | 20 (Off)..800 Hz | |
| 127 | Env Attack | 3..300 ms | |
| 128 | Acc Sweep | 0..200 % | |
| 129 | Amp Attack | 0 (Off)..500 ms | |
| 130 | Amp Decay | 30..8000 ms | |
| 131 | Release | 0 (303)..2000 ms | |
| 132 | LFO Shape | Sine, Triangle, Saw, Square, S&H | |
| 133 | LFO Sync | Free / Sync | |
| 134 | LFO Rate | 0.05..30 Hz | Used when Free |
| 135 | LFO Div | 8 bars, 4 bars, 2 bars, 1 bar, 1/2, 1/4, 1/8, 1/16, 1/4T, 1/8T | Used when Sync |
| 136 | LFO>Cut | 0..100 % | |
| 137 | LFO>Pitch | 0..12 st | |
| 138 | LFO>PW | 0..100 % | |

The 80 step parameters are the pattern on the screen. Choosing another pattern loads its steps into them, so do not
automate them; automate `Pattern` instead.

Screen, left to right: **ACID** (the 303's own controls, the four classic knobs in the top row), **TONE** and **LFO**
(beyond the 303), **LINE** (the generator) and **STEP** (the step sequencer).
The screen has its own look (black panel, acid-green print, chrome-ringed knobs), in `skins/`. It is generated, not
hand-edited: after changing `ui.json` or the parameters, build, run `python3 "plugins/Acid Rat/tools/make_skin.py"`
(needs Pillow; `--preview out.png` draws the pages without an MPC), then build again. Not yet checked on the MPC's
screen; if it misbehaves there, delete `skins/` and rebuild to get the SDK's standard kit back.

Programs: Acid Rat, Squelch, Hollow Square, Rat Bite, Soft Burn, Devil Slide, Dub Acid, Line: Keys, Line: Rave,
Pattern: Keys (hold a pad: pattern 1 plays), then six that use 1.2: Sub Rat, PWM Hollow, Wobble, Screamer,
Slow Riser, S&H Robot. Choosing a program sets the whole sound (so "Acid Rat" is always the plain 303) and keeps your
patterns, the pattern shown and what the sequencer plays.

## Tips

- The classic move: Reso around 80 %, Env Mod 60-80 %, short Decay, then ride Cutoff. Cutoff, Reso and Env Mod are
  smoothed, so sweeping them from a Q-Link does not step.
- With Cutoff and Env Mod both near the top and high resonance, accented notes whistle very high (the filter goes up
  to 20 kHz for a few milliseconds). That is the model's behaviour, not a fault; lower Env Mod or Accent to tame it.
- The engine's level sits before the drive, so Drive gets fiercer on accents. Use Master to trim afterwards.
- Decay and Tune apply from the next note, as on the engine they come from.

## What is not there

- Patterns cannot be exported as MIDI (plugins cannot send MIDI to the MPC), and there is no chaining inside the
  plugin: chain patterns from a MIDI track with notes 12 to 19.
- No separate tie: use a slide onto the same note. No per-step "time mode" entry as on the original; rests are set on
  the grid.
- The page STEP relies on things only an MPC can confirm: that the screen follows values the plugin changes itself
  (the running light, the steps of a newly chosen pattern, Write going off), the lamp widget, and the two momentary
  keys. If the screen does not follow, the pattern still plays and edits correctly; leaving the page and coming back,
  or reloading the project, shows the right values.
- No pitch bend, no mod wheel, no sustain pedal.
- One oscillator and one filter: no second oscillator, no other filter types, no step-by-step parameter locks.
- The LFO runs free or with the tempo; it does not restart on each note.
- Mono signal apart from the delay's echoes.

## CPU and testing

One voice, 4x oversampled: about 0.5 to 0.7 % of a desktop core in the simulator (the higher figure with the 1.2
extras on), which should be a few percent of one core
on a first-generation MPC One (estimated, not yet measured on the device). Checked: SDK validator, host simulator
(block sizes 32-512, parameter sweep, state round trip), also with the arm32 build under emulation; sequencer timing at
several block sizes, with swing and across a loop; the step sequencer (play, light, Write, pattern change, Clear,
Dice>Pat, save and load, a 1.0 state loading unchanged, programs keeping the patterns); each 1.2 control measured for
its effect; 400 random extreme settings stay finite and bounded; 44.1 and 48 kHz.

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
