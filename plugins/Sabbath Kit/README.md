# Sabbath Kit 2 — Gonzi Audio

A **sampled** 70s rock drum kit for **doom, stoner and sludge**, with **12 ready-made grooves** that play in time
with the MPC sequencer. Real recorded drums: a 26" Ludwig kick, a Porkpie snare (centre, edge, cross-stick),
Ludwig 14" / 16" / 18" toms, Sabian Rock hats, Zildjian A 20" and K 17" crashes, a Paiste 2002 22", a
Zildjian A 24" ride (bow, bell, shank), tambourine, cowbell and clap — 5 velocity layers each. On top: room,
glue compressor, tape drive and tilt EQ.

Instrument, stereo out, 41 parameters, 6 programs. MPC OS 3.10.1 or later, External Plugin Support on.

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

- **Play** (the switch, top left of the GROOVE page): turn it on and the groove **plays by itself**, at the MPC's tempo,
  whether or not the MPC sequencer is running (if it is, the groove locks to it). With `Groove` on **Off**, Play picks
  a groove **at random** each time you switch it on. Switch it off to stop.
- **Groove Mode = Seq** (default): the groove plays whenever the MPC sequencer plays, locked to its tempo and bar
  position. Put Sabbath Kit on a track, choose a groove, press Play. Pads still play the kit on top.
- **Groove Mode = Pad** (with Pad Map = GM): note **60** starts and stops the groove (at the MPC tempo, 70 BPM if none), note **61**
  plays a fill on the next bar.
- **Fill**: Off, every 4 bars or every 8 bars; each groove has three fills that take turns. The bar after a fill gets
  a crash. Since 2.2 every groove is a 4-bar phrase with ghost notes, foot hat and kick variations.
- **Groove Vel**: overall dynamics. **Humanize** adds small timing/velocity variations and lays the snare back.

**MIDI files**: the same 12 grooves are in `Content/MIDI Grooves/` (and in the zip) as 8-bar MIDI files (fills on
bars 4 and 8). Load one on the MPC as a sequence and edit it note by note, then leave Groove on Off and set
**Pad Map to GM**.

## Pads

**Pad Map** (GROOVE page) decides which pad plays which drum. The default, **MPC**, is made for the MPC's own pad
setup on a plugin track (Pad Perform: Notes, C major): **every C is a kick, every D a snare**, and the other notes
alternate between two sets from one octave to the next, so the 16 pads always hold the whole kit:

| | | | |
|---|---|---|---|
| 13 Crash 2002 | 14 Pedal hat | 15 Kick | 16 Snare |
| 9 Snare edge | 10 Ride | 11 Ride bell | 12 Crash A20 |
| 5 Tom 3 | 6 Tom 2 | 7 Tom 1 | 8 Kick |
| **1 Kick** | **2 Snare** | 3 Closed hat | 4 Open hat |

(In a bank that starts one octave higher or lower the two halves swap: pads 1-7 hold kick, snare edge, ride, bell,
crashes, pedal hat and pads 8-14 the hats and toms. Pad 1 is always the kick.) With Pad Perform on Chromatic the
black keys add cross-stick, clap, tambourine, cowbell, crash K17 / ride wash.

- **C2 / C1**: a 16-pad layout for chromatic pads starting at note 48 / 36 (kick, snare, hat, open hat / toms, pedal
  hat / snare edge, cross-stick, ride, bell / crashes, ride wash).
- **GM**: General MIDI notes, for the MIDI groove files and GM keyboards/sequences: 36 kick, 37 cross-stick, 38 snare,
  39 clap, 40 snare edge, 41/43 tom 3, 45 tom 2, 47 tom 1 edge, 48/50 tom 1, 42 closed hat, 44 pedal hat, 46 open hat,
  49 crash A20, 57 crash K17, 52/55 crash 2002, 51 ride, 53 ride bell, 59 ride shank, 54 tambourine, 56 cowbell.

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
| 39 | Play | On: the groove plays by itself (random groove if Groove is Off) |
| 40 | Pad Map | MPC (default) / C2 / C1 / GM |

Pages: **GROOVE**, **KIT**, **TOMS**, **CYMBALS**.

Programs: Iron Doom · Funeral March (tuned down, huge dark room) · Desert Fuzz · Sludge Crush · Garage Live · Dry Studio.

## Screen

Since 2.1 the plugin has its own look (black amp panel, steel knobs, orange valve glow, captions per row, the pad map
on the GROOVE page). It is generated by `tools/make_skin.py` into `skins/` (after changing `ui.json` or parameters:
build, make_skin, build). Not yet checked on a real MPC screen: if the plugin shows no tabs, delete `skins/` and
rebuild to get the SDK's standard look back (the `-plain-ui` zip is that build).

## Honest notes

- Which notes the MPC's pads send to a plugin track is not documented in the SDK; Pad Map C2 / C1 covers the two
  likely cases and any 16 chromatic pads always hold the whole kit, possibly rotated.

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
