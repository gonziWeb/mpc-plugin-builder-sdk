# Sabbath Kit — Gonzi Audio

A synthesised acoustic drum kit for **doom, stoner and sludge**: a big low kick, a fat 14" snare, a rack tom and two
floor toms, heavy 15" hats, a 20" crash and a 22" ride, played in a room and glued on a bus with compression and
tape drive. No samples: every hit is modelled when it is played, so velocity, humanize and overlapping tails make each
one a little different (no machine-gun repeats).

Instrument, stereo out, 35 parameters, 6 programs. MPC OS 3.10.1 or later, External Plugin Support on.

## Pads / notes

General MIDI drum notes — what the MPC **Classic** pad layout sends:

| Note | Sound | Note | Sound |
|---|---|---|---|
| 35, 36 | Kick | 42, 54 | Closed hat |
| 37 | Cross-stick | 44 | Pedal hat (foot "chick") |
| 38, 39 | Snare | 46 | Open hat (choked by 42/44) |
| 40 | Rimshot | 49, 52, 55, 57 | Crash |
| 47, 48, 50 | Tom 1 (12" rack) | 51, 58 | Ride |
| 43, 45 | Tom 2 (16" floor) | 53, 56 | Ride bell |
| 41 | Tom 3 (18" floor) | 59 | Ride crashed (wash) |

Any other note is folded by octaves into 35–59, so every pad plays something (Classic pad A04 = note 82 → ride).
Note-offs are ignored: drums ring out.

## How it works

- **Shells** (kick, snare, toms): up to six membrane modes at circular-membrane ratios (1, 1.594, 2.136, 2.296…), each
  with its own decay; the pitch drops on hard hits (head tension); stick / beater noise; snare-wire rattle that lags
  the stick by a millisecond. Kick and tom hits make the snare wires buzz in sympathy, like a real kit.
- **Metals** (hats, crash, ride): ring-modulated inharmonic square pairs plus noise, band-passed, with a low-pass that
  darkens as the cymbal decays; the ride adds pitched stick "ping" partials, the bell its own partials.
- **Realism**: velocity changes level *and* brightness *and* pitch bend; humanize varies pitch, decay, level and
  brightness per hit; two or three overlapping hits of the same piece can ring together (the oldest fades, no clicks).
- **Bus**: 4-line feedback-delay room → glue compressor (4:1, auto make-up) → tape saturation → tilt EQ → master,
  with a transparent soft limiter above −3 dBFS so the MPC never sees overs.

## Parameters (order is permanent)

| # | Name | Range | What it does |
|---|---|---|---|
| 0 | Master | −40…+6 dB | Output level |
| 1–4 | Kick Tune / Decay / Level / Beater | ±12 st, %, %, % | Beater: felt (0) → hard plastic (100) |
| 5–8 | Snare Tune / Decay / Level / Wires | ±12 st, %, %, % | Wires: snare rattle amount (also the sympathetic buzz) |
| 9–17 | Tom1/2/3 Tune / Decay / Level | ±12 st, %, % | |
| 18–20 | Hat Tune / Decay / Level | | Decay mostly affects the open hat |
| 21–23 | Crash Tune / Decay / Level | | |
| 24–26 | Ride Tune / Decay / Level | | Also ride bell and ride crash |
| 27 | Room | % | Room level |
| 28 | Room Size | % | Small wooden room → big live room (0.35–2.4 s) |
| 29 | Drive | % | Tape saturation on the whole kit |
| 30 | Comp | % | Glue compression (0 = off, 100 = threshold −30 dB) |
| 31 | Tone | −100…+100 | Tilt EQ: dark ↔ bright |
| 32 | Width | % | Stereo spread (drummer's view: hats left, floor toms and ride right) |
| 33 | Humanize | % | Random variation per hit |
| 34 | Vel Sens | % | How much velocity changes level and brightness |

Pages: **KIT** (kick, snare, room/bus), **TOMS**, **CYMBALS**.

## Programs

Iron Doom (default, warm medium room) · Funeral March (detuned, long decays, huge dark room) · Desert Fuzz (brighter,
driven and squashed) · Sludge Crush (heavy comp + drive, dark) · Garage Live (roomy, bright, loose) · Dry Studio
(almost no processing — the raw kit).

## Tips

- Play slow: 55–75 BPM, half-time snare on 3, ride or crash washes on quarters.
- Use the ride crashed (note 59) for the stoner "wash" on choruses; pedal hat (44) for time keeping under riffs.
- For more size, raise Room and Comp together: the compressor pumps the room like a 70s live recording.

## Honest notes

- It is a synthesis model, not a sampled kit: very convincing for shells and the room; cymbals are good in a mix,
  less so soloed than a multisampled cymbal.
- No multi-outs, no per-drum pan or choke editing, no MIDI-learn.
- CPU: voices sleep at −70 dB; a dense groove keeps 6–10 of the 20 voices alive. On a desktop it uses well under 1 %
  of a core; on the MPC One (1st gen) expect a few percent per instance.

Licence: MIT. All DSP is original to this project (mode ratios are textbook circular-membrane values).
