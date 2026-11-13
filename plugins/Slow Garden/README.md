# Slow Garden

A generative ambient instrument for the MPC, by Gonzi Audio. It plays itself from the moment it is loaded: you do not
play notes, you tend it. Slow Garden is the engine of **AMB-1**, Charles Vestal's free firmware for the M-VAVE FM-1
(after Brian Eno's *Music for Airports* and *Discreet Music*), put into an MPC plugin. The engine's code is unchanged;
its output is the same as AMB-1's source, sample for sample.

Version 1.0.0 · instrument, stereo out · MPL-2.0 (see `LICENSING.md`).

## What you hear

Four layers grow from one scale and one chord that drifts slowly through it:

- **Pad**: four voices that each swell in, hang and fade on their own cycle, so the chord forms and dissolves.
  Two colours: a soft Rhodes, or sung Voices.
- **Bells**: FM bells on four loops of 13, 17, 23 and 29 beats. Each loop holds a few notes; the loops slide against
  each other and the melody never quite repeats.
- **Bass**: a low sine on the chord's root, gliding, sometimes leaning on the fifth.
- **Pulse**: a soft thump and two hats, each on its own Euclidean pattern (16, 12 and 7 sixteenths).

Then the space: a short echo, a tape loop up to 5.6 s long with two heads, and a plate reverb. A slow tide, minutes
long, moves everything between sparse and full.

## On the MPC

1. Load Slow Garden on a plugin track. It starts growing at once (`Grow` = Always). Nothing needs to be recorded.
2. Pick a program: the first eight are AMB-1's pieces, the last four are ours.
3. **Pads / keys change the scale** (`Key Edit` on): each note adds its note to the scale or takes it out. All
   octaves of a note are the same. The root always stays and the scale keeps at least three notes. Choosing a `Mode`
   or a program loads a fresh scale. The scale is saved with the project.
4. To play along with a beat: set `Sync` to **MPC** (tempo and sixteenths follow the MPC, the tape echo too) and
   `Grow` to **Song** (it grows while the MPC plays and fades to rest when it stops).
5. To get the same piece every time (for a bounce): give it a `Seed` from 1 to 99 and set `Grow` to **Song**. Every
   press of Play then starts that piece from the top. With `Seed` on Random each load and each start is a new piece.

Recorded pad notes on the track would edit the scale again on every pass: leave the track empty, or turn `Key Edit`
off once the scale is right.

## Parameters

Values are AMB-1's, 0 to 100 unless said otherwise.

| Page | Parameter | What it does |
|---|---|---|
| GARDEN | Tempo | 30 to 120 bpm. Used while Sync is Off. |
| | Evolve | How restless: at 0 the loops repeat exactly; higher, notes change, the pulse patterns turn, layers take rests. |
| | Motion | How far the slow tide swings and how much pitch, pan, vowel and echo heads wander. |
| | Drift | How long a chord lasts before the pad moves on: 20 s to 5 min. |
| | Grow | Rest (fades out, tails ring), Always, or Song (grows while the MPC plays). |
| | Sync | Off: own tempo. MPC: the MPC's tempo and beat. Above 120 bpm it runs at half tempo (140 gives 70), below 30 at double. |
| | Freeze | Holds the chord; the reverb rings on forever while bells and pulse go on above it. |
| | Next Chord | Each flip of the switch (on or off) moves to the next chord, on the next bar. |
| | Root, Mode | The key. Mode loads one of eight scales: Ionian, Dorian, Phrygian, Lydian, Mixolydian, Aeolian, pentatonic major or minor. |
| | Key Edit | On: pads and keys edit the scale. Off: notes are ignored. |
| | Seed | Random, or 1 to 99 for a piece that repeats. Changing it grows a new piece. |
| | Echo, Reverb | The sends to the echoes and to the plate. |
| | Tune | -50 to +50 cents. |
| | Master | Output level, -60 to +6 dB. |
| LAYERS | Pad, Swell, Bright, Colour | Level; how long the voices breathe; filter / vowel; Rhodes or Voices. |
| | Bells, Density, Bell Tone, Bell Oct | Level; 0 to 5 notes a loop; round to metallic; -2 to +2 octaves. |
| | Bass, Bass Oct, Glide, Fifth | Level; -1 to +1 octave; 20 ms to 1.2 s; how often it leans on the fifth. |
| | Pulse, Hits, Swing, Hats | Level; how many hits (0: none); 50 to 75 %; dark and long to bright and crisp. |
| SPACE | Echo Time | The tape loop: dotted quarter, half, dotted half, 1, 2 or 4 bars (shortened to fit 5.6 s at slow tempos). |
| | Size | How long the plate rings and how many times the echo comes back. |

A program sets the piece (Master to Mode). Tune, Seed, Grow, Sync, Freeze, Next Chord and Key Edit belong to you and
stay as they are when you change program.

## Programs

| # | Name | |
|---|---|---|
| 1 | Garden | D Dorian, 72: everything, gently. Rhodes. |
| 2 | Departures | C# Ionian, 60: soft, slowed-piano bells over a quiet Rhodes. |
| 3 | Voices | F Aeolian, 56: only the sung voices and a low bass. |
| 4 | Afternoon | G Lydian, 84: a bright Rhodes, busier bells, a light pulse. |
| 5 | Night Bus | A Aeolian, 96: the pulse forward, metallic bells, short echoes. |
| 6 | Shoreline | E pentatonic major, 64: dark, round bells lost in long echoes. |
| 7 | Glass | Eb pentatonic minor, 76: clangy bells an octave up, dense. |
| 8 | Tides | Bb Mixolydian, 70: sung voices, the most changeable. |
| 9 | Crypt | D Phrygian, 48: slow sung voices, a few low dark bells, a very long plate. (Gonzi Audio) |
| 10 | Rainlight | G pentatonic major, 80: dense bright bells an octave up in long echoes. (Gonzi Audio) |
| 11 | Slow Pulse | A Aeolian, 90: bass and pulse forward, made to sit under a beat with Sync on MPC. (Gonzi Audio) |
| 12 | Still Water | F Lydian, 60: pad and bass only, a chord for about five minutes, nothing moves but the tide. (Gonzi Audio) |

Programs 1 to 8 are AMB-1's recipes, read from the engine's own table. Only their Master differs (+4 to -5 dB), to
bring them to a similar loudness: at the engine's unity they are about 14 dB apart.

## What is checked, and what is not

Checked on a computer:

- The build passes the SDK's host simulator (no failure; its one warning, "still sounding after all-notes-off", is
  what a self-playing instrument does).
- `gonzi/tools/test_garden_engine.cpp`: the engine as built into the plugin gives the same samples as AMB-1's
  `amb.c`, over 680 s of all eight pieces with parameter, scale, freeze and rest changes, two engines side by side.
- `gonzi/tools/render_garden.cpp check`: a seed repeats exactly, at any block size; Random does not; Song + Seed
  restarts the same piece; with Sync on MPC at 97.3 bpm and a looping song every pulse hit lands within 0.4 ms of one
  of the MPC's sixteenths; the edited scale is saved and restored; two instances do not disturb each other.

Not yet tried on an MPC:

- CPU on the MPC One. On a desktop one instance takes about 0.5 % of a core; the MPC is far slower, so expect a few
  percent, to be measured.
- The screen (the SDK's standard kit, lime theme) and the Q-Link layout.
- Whether the MPC gives the plugin its tempo and song position the way the simulator does (Sync, Grow = Song).
- Which comes last when a project is loaded, the saved state or the saved program number. If it is the program, an
  edited scale would come back as the program's mode.
- With Sync on MPC, a seed repeats exactly only at the same tempo and buffer size.

The arm32 and arm64 builds are cross-compiled from the same source as the tested native build; they were not run.

At a sample rate other than 44.1 kHz (a desktop host; the MPC is always 44.1 kHz) the engine still runs at 44.1 kHz
and is resampled with a straight line, which dulls it a little.

## Building

```
python3 "plugins/Slow Garden/tools/make_engine.py"      # only after changing src/amb/
python tools/build.py "plugins/Slow Garden"
.sdk-tools/host_sim "plugins/Slow Garden/build/native/slow-garden.so" test --seconds 6
g++ -O2 -std=c++17 -Iframework gonzi/tools/render_garden.cpp -o .sdk-tools/render_garden -ldl
.sdk-tools/render_garden "plugins/Slow Garden/build/native/slow-garden.so" check
.sdk-tools/render_garden "plugins/Slow Garden/build/native/slow-garden.so" out.wav 40 0 2 4     # seconds, programs
```

`src/amb/` holds AMB-1's files unchanged. `tools/make_engine.py` rearranges them into `src/core/amb_engine.h`
(constants at namespace scope, state and functions as members of one struct) so that each instance has its own
engine; `src/SlowGarden.cpp` is the plugin around it.

## Credits

AMB-1 is by Charles Vestal: <https://github.com/charlesvestal/fm1-amb>. Its idea comes from Brian Eno's generative
music; its reverb is Jon Dattorro's plate; its Euclidean rhythms are Godfried Toussaint's reading of Bjorklund's
algorithm. Slow Garden is not made or endorsed by Charles Vestal, M-VAVE, Brian Eno or Akai / inMusic. Details in
`LICENSING.md`.
