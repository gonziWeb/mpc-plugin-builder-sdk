# Sabbath Kit 2 — Gonzi Audio

A **sampled** 70s rock drum kit for **doom, stoner and sludge**, with **12 ready-made grooves** that play in time
with the MPC sequencer. Real recorded drums: a 26" Ludwig kick, a Porkpie snare (centre, edge, cross-stick),
Ludwig 14" / 16" / 18" toms, Sabian Rock hats, Zildjian A 20" and K 17" crashes, a Paiste 2002 22", a
Zildjian A 24" ride (bow, bell, shank), tambourine, cowbell and clap — 5 velocity layers each. On top: room,
glue compressor, tape drive and tilt EQ.

Instrument, stereo out, 39 parameters, 6 programs. MPC OS 3.10.1 or later, External Plugin Support on.

## Grooves

Pick one on the **GROOVE** page (or set `Groove` with a Q-Link):

| # | Groove | Feel |
|---|---|---|
| 1 | Iron Ride | half-time doom, ride on quarters |
| 2 | Wizard Wash | big Paiste wash on every beat, slow and huge |
| 3 | Funeral | funeral-doom crawl, one hit per beat at most |
| 4 | Stoner Shuffle | 12/8 shuffle hats, riff-locked kick |
| 5 | Desert Rock | straight-eighth stoner rock with open-hat lift |
| 6 | Sabbath Gallop | galloping triplet floor tom under a backbeat |
| 7 | Sludge Toms | tribal floor toms, snare on 3 |
| 8 | Sludge Crush | half-time, double kicks, big Paiste |
| 9 | Slow Blues Doom | 12/8 ride triplets, heavy backbeat |
| 10 | Tribal Ritual | toms, kick and tambourine, no backbeat |
| 11 | Levee Stomp | heavy half-time shuffle, triplet kick push |
| 12 | Fuzz Pocket | groovy stoner pocket with tambourine |

- **Groove Mode = Seq** (default): the groove plays whenever the MPC sequencer plays, locked to its tempo and bar
  position. Put Sabbath Kit on a track, choose a groove, press Play. Pads still play the kit on top.
- **Groove Mode = Pad**: pad/note **60** starts and stops the groove (at the MPC tempo, 70 BPM if none), note **61**
  plays a fill on the next bar.
- **Fill**: Off, every 4 bars or every 8 bars. The bar after a fill gets a crash.
- **Groove Vel**: overall dynamics. **Humanize** adds small timing/velocity variations and lays the snare back.

**MIDI files**: the same 12 grooves are in `Content/MIDI Grooves/` (and in the zip) as 8-bar MIDI files (fills on
bars 4 and 8). Load one on the MPC as a sequence and edit it note by note, then leave Groove on Off.

## Pads / notes

General MIDI, laid out so the MPC **Classic** pad bank A plays the whole kit:

| Note | Sound | Note | Sound |
|---|---|---|---|
| 35, 36 | Kick | 42 | Closed hat |
| 37 | Cross-stick | 44 | Pedal hat |
| 38 | Snare | 46 | Open hat (choked by 42/44) |
| 39 | Clap | 49 | Crash Zildjian A 20" |
| 40 | Snare edge | 57 | Crash Zildjian K 17" |
| 48, 50 | Tom 1 (14") | 52, 55 | Crash Paiste 2002 22" |
| 47 | Tom 1 edge | 51, 58 | Ride |
| 45 | Tom 2 (16" floor) | 53 | Ride bell |
| 43, 41 | Tom 3 (18" floor) | 59 | Ride shank (wash) |
| 54, 82 | Tambourine | 56 | Cowbell |

Other notes fold by octaves into 35–59.

## Parameters (order is permanent)

| # | Name | What it does |
|---|---|---|
| 0 | Master | Output level (dB) |
| 1–4 | Kick Tune / Decay / Level / Beater | Tune ±12 st (resampled), Decay 100 % = natural ring, Beater = softer ↔ more click |
| 5–8 | Snare Tune / Decay / Level / Wires | Wires = duller ↔ crisper snare |
| 9–17 | Tom1/2/3 Tune / Decay / Level | |
| 18–26 | Hat, Crash, Ride Tune / Decay / Level | |
| 27–28 | Room, Room Size | Room level; small wooden room → big live room |
| 29–31 | Drive, Comp, Tone | Tape saturation, glue compression, dark ↔ bright |
| 32–34 | Width, Humanize, Vel Sens | Stereo spread (drummer's view), per-hit variation, velocity response |
| 35 | Groove | Off / 12 grooves |
| 36 | Groove Mode | Seq / Pad |
| 37 | Fill | Off / 4 Bars / 8 Bars |
| 38 | Groove Vel | Groove dynamics |

Pages: **GROOVE**, **KIT**, **TOMS**, **CYMBALS**.

Programs: Iron Doom · Funeral March (tuned down, huge dark room) · Desert Fuzz · Sludge Crush · Garage Live · Dry Studio.

## Honest notes

- v2 replaces v1's synthesised engine with samples; the parameter list is the same with four groove parameters
  appended, so v1 projects still open (sounds differ, of course).
- 5 velocity layers, no round robins: Humanize hides repetition with neighbour layers, small pitch and level changes.
- Grooves are 4/4 (12/8 grooves are written as triplets). Seq mode needs the MPC to send its transport position to
  plugins; if a groove does not start with Play, use Pad mode.
- Memory: the samples load once (~24 MB, 16-bit) and are shared by all instances. CPU: a few percent on the MPC One.

## Licences

- Code: MIT (Gonzi Audio).
- Samples: "Red Zeppelin" kit from **AVL Drumkits** by **Glen MacArthur** (http://www.bandshed.net/avldrumkits/),
  **CC BY-SA 3.0**; converted to WAV, trimmed and renamed by Gonzi Audio, shared under the same licence. See
  `Content/LICENSE-samples.txt`. Music you make with them is yours.
- Grooves and MIDI files: original, MIT.
