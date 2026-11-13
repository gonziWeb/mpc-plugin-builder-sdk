# Patina

A lo-fi texture insert effect for Akai MPC standalone hardware, by Gonzi Audio.

It makes a clean drum bus, sample or whole mix sound old: vinyl (crackle, pops, surface dust,
rumble, warp), cassette (wow, flutter, saturation, hiss, dropouts, treble loss), an old sampler
(bit depth and sample rate) and a small dark room. One macro, **Age**, turns all the dirt up or
down at once. Every noise is synthesised by the plugin: no samples, so nothing ever loops.

Written for the MPC Plugin SDK (MIT). No third-party DSP.

## Requirements

- MPC OS 3.10.1 or later.
- Preferences → Enhancements (Beta) → **Enable Enhancements** and **External Plugin Support** turned on.

## Install

Copy the folder `Gonzi Audio - VST - Patina` into a folder named exactly `VST2 Plugins` at the
root of a USB drive or SD card, insert it in the MPC, and add the plugin as an insert effect:
**External Plugins → Patina**.

## Signal path

input → room → speed wobble (warp, wow, flutter) → dropouts → drive → hi loss → grain (rate, bits)
→ + noises (crackle, pops, dust, rumble, hiss) → low cut → tone → width → mix → output

The room comes first, like a recording that had its ambience before it was pressed or taped, so
the tail gets the same wobble, saturation and grain as the sound.

## Pages and parameters

**MAIN**

| Control | Range | What it does |
|---|---|---|
| Age | 0–100 % | Macro. Scales every control of the VINYL and TAPE pages and the grain: 0 % = clean, 100 % = exactly as set on those pages |
| Tone | 800 Hz–18 kHz | Darkens everything, noises included (12 dB/oct; fully up = off) |
| Low Cut | 20–800 Hz | Thins the low end for a radio / telephone sound (fully down = off) |
| Width | 0–100 % | Stereo width; 0 % = mono like an old record |
| Noise | Always / Follow | Always: the noises play even in silence, like a real record. Follow: they only play while the track plays and fade out about half a second after it stops |
| Mix | 0–100 % | Dry / wet |
| Output | −24…+6 dB | Output level |

**VINYL**

| Control | What it does |
|---|---|
| Crackle | Small dense clicks, each at its own place in the stereo field (1 to about 40 per second) |
| Pops | Rare, louder, duller pops; above 50 % also a scratch that comes back on every turn of the record |
| Dust | Fine surface noise that breathes with the rotation, plus tiny ticks |
| Rumble | Very low turntable rumble and a little 50 Hz hum |
| Warp | Pitch sway of a warped record, once per turn at 33 rpm |

**TAPE**

| Control | What it does |
|---|---|
| Wow | Slow, slightly random pitch drift |
| Flutter | Fast pitch shake of a worn motor |
| Drive | Tape saturation (gets a little louder as it goes up) |
| Hiss | Tape hiss, different left and right |
| Dropouts | Short random dips in level, sometimes more on one side |
| Hi Loss | Gentle treble loss (6 dB/oct, from 20 kHz down to 1.8 kHz) |

**GRAIN**

| Control | Range | What it does |
|---|---|---|
| Bits | 4–16 | Bit depth (16 = clean). Age slides it from 16 down to this value |
| Rate | 2–44.1 kHz | Sample rate, with the aliasing of an old sampler (44.1 kHz = clean). Age slides it from 44.1 kHz down to this value |
| Room Size | 0–100 % | Size and decay of the room (about 0.25 to 2 s) |
| Room Tone | 1–10 kHz | Brightness of the tail |
| Room Mix | 0–100 % | Room level (0 % = off, and the room then costs no CPU). Not scaled by Age |

## Programs

| # | Name | Use |
|---|---|---|
| 0 | Patina | A bit of everything, the starting point |
| 1 | Clean Vinyl | A record in good condition: light crackle and warp, nothing else |
| 2 | Dusty Crate | A worn record: heavy crackle, pops, dark and narrow |
| 3 | Needle Drop | Vinyl noise that never stops (Noise = Always), for intros and breaks |
| 4 | Cassette | Wow, flutter, saturation and hiss |
| 5 | Broken Walkman | A dying cassette player: heavy wobble, dropouts, dull |
| 6 | SP Grit | Old 12-bit sampler grit, no noise |
| 7 | Far Radio | Mono, band-limited, crushed and distant |
| 8 | Master Dust | Gentle, meant for the master |
| 9 | Dream Tape | Dark, wobbly, with more room |

## Notes

- With Wow, Flutter or Warp up and Mix below 100 %, the dry and the wobbling signal mix into a
  chorus / flanger. That can be nice, but for a plain "old" sound keep Mix at 100 % and use Age.
- The speed wobble delays the sound by a few milliseconds (up to about 18 ms with Warp and Wow at
  maximum). It is not reported as latency because it moves.
- On the master, start from "Master Dust": the noises are added at a fixed level, so they are
  relatively louder on a quiet mix.
- Noise = Always keeps making sound while the MPC is stopped. That is the point of it.
- Effects receive no MIDI on the MPC.
- CPU: the host simulator measures about 0.4–0.5 % of a desktop core per instance with everything
  on; expect several times that on an MPC One. Not yet measured on the hardware.
- Not implemented: sampled (real) vinyl noise, 45 / 78 rpm, tape head bump EQ, noise ducking
  (side-chain) by the signal.

## Parameter order (permanent)

Output, Age, Tone, Width, Noise, Mix, Crackle, Pops, Dust, Rumble, Warp, Wow, Flutter, Drive, Hiss,
Dropouts, Hi Loss, Bits, Rate, Room Size, Room Tone, Room Mix, Low Cut.

## Build

```
python tools/build.py plugins/Patina
python tools/package.py plugins/Patina
```
