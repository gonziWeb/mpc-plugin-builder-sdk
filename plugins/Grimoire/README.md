# Grimoire — Gonzi Audio

A **dark ambient / dungeon synth** for the MPC, built from two free firmwares for the M-VAVE FM-1:
[GR!MM](https://github.com/GRiMMBIENT/grimm) (dark ambient, dungeon synth and drone) and
[GHOULBOX](https://github.com/jasonpersinger/ghoulbox-fm1-dungeon-synth) (dungeon synth). Their sound engines and
effects are compiled in as they are (integer DSP, 44.1 kHz, GR!MM's own code paths). The MPC does the rest: Grimoire
keeps the sounds and leaves out the firmwares' sequencers, arpeggiators, song modes and screens.

Instrument, stereo out, 94 parameters, 63 programs, two layers, 16 voices. MPC OS 3.10.1 or later, Preferences →
Enhancements (Beta) → Enable Enhancements and External Plugin Support on. Free software: GPL-3.0 (see `LICENSING.md`).

## How it is laid out

Two **layers**, A and B (GR!MM's tracks). Every note plays on each layer whose Level is above 0, each with its own
source, knobs, envelope, filter, LFO and effect sends. Then GR!MM's effects for both, and GHOULBOX's ambience.

```
note -> layer A: SOURCE x ENV -> tape share -> FILTER -> level, pan -> mix
     -> layer B: (the same)                         \-> sends: DELAY, SPACE, GRAIN, DRIVE
ambience (wind, crackle) -> mix and SPACE
GRAIN, DELAY -> mix + SPACE;  SPACE -> mix
mix -> MASTER filter (pre / post reverb / end) -> DRIVE (+ limiter) -> TAPE hiss -> Master -> limiter -> out
```

## Sources and their knobs

Knob 1–4 are GR!MM's four knobs of the source; Knob 5–8 do something only where the table says so. The screen shows
what each knob is doing ("WAVE SUPERSAW", "CUT 1200 Hz", …).

| Source | Knob 1 | Knob 2 | Knob 3 | Knob 4 | Knob 5–8 |
|---|---|---|---|---|---|
| **VA** (Felucca's analog + GR!MM's supersaw) | Wave: saw, square, supersaw | Detune (cents; supersaw: spread) | Cutoff (key-tracked) | Resonance | – |
| **FM** (msfa / Dexed core, 6 operators) | Patch (11, below) | Bright: modulator levels ±36 dB (64 = as made) | Decay: modulator envelopes slower / faster | Detune: carriers spread apart | – |
| **Voice** (formant voice, and real choirs) | Vowel (A … U) | Shift | Buzz (tone) | Breath | 5: Type: Solo, Gregorian, Nuns, Cathedral |
| **Wheel** (tonewheel organ, slow rotary) | Drawbars (16 registrations) | Perc | Click | Drive | – |
| **Phys** (DaisySP / Rings models) | Model: modal, string, membrane, sympathetic | Structure | Damp | Bow | – |
| **Phase** (CZ-style phase distortion) | Wave | Wave 2 (every other cycle) | DCW (the bend) | Detune (a 2nd line) | 5: line 2 wave, 6: ring mod, 7: DCW envelope, 8: its decay |
| **Gurdy** (GHOULBOX's hurdy-gurdy, mono) | Drone key (C … B, octave 2) | Drones level | Fifth level | Wheel pressure | 5: Buzz (trompette), 6: Coup: hold, 1/4, 1/8, 1/8T, 1/16 of the tempo, 7: Body, 8: Wobble |

FM patches: Bell, Organ, Soft Pad, Reed (GR!MM's); French Horn, Harpsichord, Glass Choir, Dark Strings, Lofi Flute,
Crypt Organ, Tuba (GHOULBOX's dungeon patches).

**Gurdy** is one wheel: its layer is always monophonic and legato. The drones sound under every note and do not
restart; the melody string slides from note to note.

## Parameters (order is permanent)

| # | Name | What it does |
|---|---|---|
| 0 | Master | Output level, −40…+12 dB (a limiter keeps the output under 0 dBFS) |
| 1 | Voices | Polyphony of each layer, 1–16 (16 voices shared; Phys at most 6 per layer: its models are the heaviest) |
| 2 | Vel Sens | 0 %: every note full; 100 %: GR!MM's velocity curve, (velocity / 127)² |
| 3 | Bend Range | Pitch bend, 0–24 semitones |
| 4–34 | A … | Layer A, in this order: Source, Knob 1–8, Attack, Decay, Sustain, Release, Level, Pan, Cutoff, Reso, Filt Type (LP/HP/BP), Glide, Mono, Octave (±3), Fine (±50 ct), LFO Rate, LFO Depth, LFO Dest, LFO Shape, Delay Snd, Reverb Snd, Grain Snd, Tape Snd, Drive Snd |
| 35–65 | B … | Layer B, the same |
| 66–69 | Tape Wow, Sat, Tone, Hiss | GR!MM's TAPE: worn-tape slips and wow, saturation, darkness, hiss. The layers' Tape Snd sets how much of each goes through it |
| 70–74 | Grain Size, Dens, Scat, Mix, Freeze | GR!MM's GRAIN: a granular cloud of the Grain sends (10–390 ms grains, 1–98 a second); Freeze holds it |
| 75–79 | Delay Time, Fdbk, Wear, Mix, Sync | GR!MM's tape DELAY: 20–3500 ms (or synced to the MPC's tempo, 1/32 … 1/1), feedback up to 110 %, wear (darker, more wobbly repeats) |
| 80–83 | Space Size, Damp, Mix, Pre | GR!MM's SPACE reverb: 0.4 s … ~32 s, and INF (a freeze) at the top; pre-delay 0–250 ms |
| 84–87 | Mstr Cutoff, Reso, Type, Pos | GR!MM's MASTER filter on everything (LP/HP/BP), before the effects, after the reverb, or at the very end |
| 88–91 | Drive Amt, Type, Tone, Mix | GR!MM's DRIVE: soft, fuzz, clip or wavefold, with a limiter |
| 92–93 | Ambience, Crackle | GHOULBOX's cave: wind (filtered noise wandering slowly) and a torch's crackle, into the mix and the reverb |

Envelope times follow GR!MM: Attack ~1 ms … ~10 s, Decay ~10 ms … ~10 s, Release ~10 ms … ~20 s. The LFO is free
running (~60 s a cycle … ~20 Hz); its destination can be the pitch, volume, pan, one of the source's four knobs, the
reverb or grain send, or the cutoff. The mod wheel adds LFO depth; the sustain pedal holds notes.

Pages: **LAYER A** (knobs, envelope, source, level, octave), **A MIX** (filter, glide, LFO, sends, pan, mono),
**LAYER B**, **B MIX**, **FX** (tape, grain, delay, reverb), **MASTER** (master filter, drive, delay sync, freeze,
ambience, master level, voices, velocity, bend).

## The look

The screen is a page of an old spell book: dark leather, a gilt frame, a cartouche around each control, a caption a
row (The Source, Deeper Workings, Envelope…) and a faint sigil behind (an eye for the layers, a moon for their mixes, a
seven-pointed star for the effects, a sun for the master). Knobs are blackened iron dials in a brass ring with a bone
pointer and a candle-amber arc; faders fill with amber; switches turn wax red. All of it is drawn by
`tools/make_skin.py` (Pillow) into `skins/`: change `ui.json` or the script, build once, run it, build again.
Checked in the SDK's validator and in a rendered preview, not yet on an MPC screen.

## Programs

| # | Program | What it is |
|---|---|---|
| 0 | Grimoire | GR!MM's CRYPT PAD (supersaw) under a GREGORIAN choir, long hall, worn tape |
| 1–3 | Gregorian · Cathedral Choir · Nuns | GR!MM's choirs (real singers) |
| 4 | Monk Drone | MONK DRONE (VA) an octave down with GR!MM's MONKS, cave wind |
| 5 | Iron Lung | PHYS drone, tempo-synced echoes |
| 6 | Static Prayer | VOICE drone into the grain cloud |
| 7 | Frozen Hall | FM soft pad, long bright hall |
| 8 | Last Light | PHASE pad on worn tape |
| 9–11 | Funeral Chime · Rust Gong · Bowed Bell | Bells and metal (FM, PHYS) |
| 12–14 | Funeral Organ · Crypt Organ · Virginal | Tonewheel, GHOULBOX's pipe organ and harpsichord (FM) |
| 15–18 | Haunted Strings · Dark Strings · French Horn · Lofi Flute | PHYS string with GHOULBOX's dark strings, and GHOULBOX's FM patches |
| 19–21 | Hurdy Gurdy · Drone Wheel · Bagpipe | GHOULBOX's hurdy-gurdy presets (play in D: the drones are on D) |
| 22–24 | Lute · Wraith · Wind Thru Stone | PHYS sympathetic strings, a gliding PHASE lead, a breathy texture with wind and crackle |
| 25–30 | Ash Veil · Moss · Black Velvet · Abyss · Tomb Hum · Deep Well | GR!MM's other pads and drones (VA, FM, tonewheel), the last three an octave down |
| 31–34 | Iron Bell · Glass Harp · Shrine · Music Box | FM bells; Music Box on very worn tape |
| 35–41 | Choir of Ghosts · Monks · Whisper Chorus · Vigil · Plainchant · Hollow Hymn · Glass Choir | Voices: GR!MM's formant voices, a mono gliding Gregorian line, FM choirs (Glass Choir is GHOULBOX's) |
| 42–46 | Chapel · Dead Harpsichord · Cathedral Keys · Bone Piano · Spider Pluck | Organ and keys |
| 47–53 | Dungeon Brass · War Horn · Tuba · Cello Grave · Sorrow Strings · Lantern · Candle | Brass, strings and leads (Tuba and Lantern are mono) |
| 54–56 | Dance Gurdy · Trompette · Vielle | GHOULBOX's other hurdy-gurdies: buzzing in sixteenths, buzzing held, the melody string alone |
| 57–62 | Catacomb · Black Mass · Frost Bells · Dungeon Keep · Reliquary · Procession | Two layers: drone wheel + cathedral choir; crypt organ + Gregorian with fuzz; bells in a grain cloud over a pad; brass + dark strings; lute on tape + whispers; bagpipe + tuba |

## Tips

- Play slowly and hold: Attack and Release are long on most programs, the reverb longer. Layer B adds a second
  source on the same notes: turn its Level up and pick a Source (a choir under any pad works).
- For drones, set Space Size to the top (INF) and Freeze the grain: the sound keeps going after you let go.
- Gurdy's drones are on Knob 1's key: set it to your track's key. Coup's strokes follow the MPC's tempo.

## Honest notes

- It is GR!MM's and GHOULBOX's sound, ported: the engines are theirs, compiled as they are (integer DSP at 44.1 kHz).
  Not ported: GR!MM's SAMPLE source, sequencer, arpeggiator, one-key chords, GENERATE, song mode, per-step locks, the
  FX performance tricks, FM operator editing (the 11 patches and four macros are there); GHOULBOX's crank, RING,
  temperaments, DREAM and its other engines. The MPC does sequencing, arpeggios and sampling.
- Latency: 256 samples (5.8 ms), reported to the host: GR!MM works in blocks of 256 frames, and this way every note
  lands on time (to 0.7 ms). At host rates other than 44.1 kHz the output is resampled (cubic).
- CPU: on a desktop 0.2–1.5 % of a core for the programs, ~3 % in the worst case (both layers Phys, 16 notes, every
  effect). On the MPC One (1st gen, Cortex-A17) expect several times that: a few percent for most programs, the most
  with Phys and the choirs on both layers. Lower Voices if an MPC One runs short.
- Velocity changes the level only (GR!MM's way), not the timbre.

Licence: GPL-3.0-only. Grimoire contains code under Apache-2.0 (msfa) and MIT (DaisySP, Rings) and wavetables under
CC BY 4.0 (VocalSet); see `LICENSING.md` and `Content/LICENSES/`. The complete source comes with every copy.
