# Dusty Tape

A lo-fi tape echo insert effect for Akai MPC standalone hardware, by Gonzi Audio.

Every repeat wobbles (wow and flutter), gets saturated like tape, gets darker and loses a little
more resolution (bit depth and sample rate), and tape hiss follows the signal so it dies away with
the echoes instead of hissing over silence.

Derived from the MPC Plugin SDK's StereoDelay example (MIT).

## Requirements

- MPC OS 3.10.1 or later.
- Preferences → Enhancements (Beta) → **Enable Enhancements** and **External Plugin Support** turned on.

## Install

Copy the folder `Gonzi Audio - VST - Dusty Tape` into a folder named exactly `VST2 Plugins` at the
root of a USB drive or SD card, insert it in the MPC, and add the plugin as an insert effect:
**External Plugins → Dusty Tape**.

## Pages and parameters

**TAPE**

| Control | Range | What it does |
|---|---|---|
| Sync | Off / On | Lock the echo to the MPC tempo |
| Division | 1/64 … 1/1 | Echo note value when Sync is On (default 1/8 dotted) |
| Time | 20–1500 ms | Echo time when Sync is Off |
| Ping Pong | Off / On | Bounce the echoes left and right |
| Feedback | 0–110 % | Number of repeats; above ~95 % the tape runs away (it stays limited) |
| Low Cut | 20–1000 Hz | Thins the repeats |
| Tone | 600–12000 Hz | Darkens the repeats (12 dB/oct low-pass in the loop) |
| Mix | 0–100 % | Dry / wet balance |

**DUST**

| Control | Range | What it does |
|---|---|---|
| Wow | 0–100 % | Slow pitch drift of a stretched tape |
| Flutter | 0–100 % | Fast pitch shake of a worn motor |
| Drive | 0–100 % | Tape saturation in the loop |
| Hiss | 0–100 % | Tape noise, only while there is signal |
| Bits | 4–16 | Bit depth of the repeats (16 = clean) |
| Rate | 2–44.1 kHz | Sample rate of the repeats (44.1 kHz = clean) |
| Output | −24…+6 dB | Output level |

Changing the delay time glides like a real tape machine, so moving Time or Division bends the pitch
of the echoes.

## Programs

Dusty Echo, Slapback, Dub Wash, Broken Walkman, Lofi Beats, Runaway Tape, Clean Tape.

## Notes

- Effects receive no MIDI on the MPC; everything is driven by the parameters and the host tempo.
- CPU: one delay line per side, two one-pole filters, a tanh and a few LFOs per sample. The host
  simulator measures about 0.3 % of a desktop core per instance; expect several times that on the MPC.
- Not implemented: tape head bump EQ, multiple playback heads, crackle.
