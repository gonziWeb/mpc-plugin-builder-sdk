# Acid Rat: licensing and credits

Acid Rat is free software under the **GNU General Public License, version 3 only** (`GPL-3.0-only`, full text in
`LICENSE`). You may use, study, change and share it. If you give someone the plugin, give them its complete source
under the same licence too (the source zip that comes inside each release zip, or this folder).

Acid Rat puts the 303 bass engine and the acid line generator of **X0X**, a free firmware for the M-VAVE FM-1, into
the MPC plugin format. Nothing of M-VAVE's own firmware is used. Acid Rat is not made or endorsed by the authors named
below, by M-VAVE, by Roland or by Akai / inMusic.

## Who made what

| What | Licence | Author |
| --- | --- | --- |
| `src/x0x/bass303.c`, `bass303.h`, `fastmath.h`, `x0x_param.h`: the bass engine | GPL-3.0-only | [X0X](https://github.com/charlesvestal/fm1-x0x) (Charles Vestal and contributors; commit 512c0da, `firmware/src/dsp/`), with speed-ups after [Optimist](https://github.com/w0ts/optimist) (henri / hdavid) and portions Copyright (C) 2026 Kerem Kilic (Ellic Studio). X0X ported it from [schwung-303](https://github.com/charlesvestal/schwung-303) (Copyright (C) 2026 Charles Vestal and contributors, GPL-3.0) |
| The synthesis model inside it: oscillator shapes, the diode-ladder filter ("TeeBeeFilter", coefficients by mystran and kunn), envelopes, accent, slide | MIT | [Open303](https://sourceforge.net/projects/open303/), Copyright (c) 2009 Robin Schmidt (www.rs-met.com); `Content/LICENSES/MIT-Open303.txt` |
| The Devilfish-style ranges (slide time, accent decay) | GPL-3.0 | after jc303 (midilab and contributors), through schwung-303 |
| The RAT drive | GPL-3.0 | after [dm-Rat](https://github.com/davemollen/dm-Rat) (Copyright (C) Dave Mollen), through schwung-303's `drive.h` |
| `src/x0x/tb3po.c`, `tb3po.h`: the line generator | GPL-3.0-only | X0X (`firmware/src/seq/`), ported from [schwung-tb3po](https://github.com/charlesvestal/schwung-tb3po), itself a port of the Phazerville Hemisphere Suite `TB_3PO` applet (djphazer and contributors, GPL-3.0) |
| `src/x0x/pattern.h`: the step and line types, cut down from X0X's `firmware/src/seq/pattern.h` | GPL-3.0-only | X0X; the cut by Gonzi Audio |
| `src/AcidRat.cpp`, `ui.json`, the changes marked "Acid Rat" in `src/x0x/`, `gonzi/tools/render_acid.cpp` | GPL-3.0-only | Copyright (C) 2026 Gonzi Audio |
| `skins/` (the screen: pages, knobs, faders, switches), drawn by `tools/make_skin.py`; `TUI.json` and `Q-Links.json` there come from the SDK's generator. The pages' captions are set in DejaVu Sans Mono Bold (Bitstream Vera / DejaVu licence), as pixels in the images; no font file is included | GPL-3.0-only | Copyright (C) 2026 Gonzi Audio |
| `framework/` (the SDK, not in this folder) | MIT | inMusic Brands, Inc. |

MIT material may be combined into a GPL-3.0 work; the plugin built from it is GPL-3.0-only as a whole.

## What Acid Rat changed (2026-11-12)

- `bass303.c` / `bass303.h`: the sample rate is the host's instead of a fixed 44.1 kHz (`bass303_set_rate`), and the
  pots take fractional values instead of whole steps 0..127 so that a knob sweep is smooth. With whole-number pots at
  44.1 kHz the output is the same as X0X's source, sample for sample (checked over 1.15 million samples: both waves,
  drive off / Soft / RAT, accents and slides).
- Built with 4x oversampling (X0X runs 2x on the FM-1) and without X0X's overload guard.
- `tb3po.c`, `tb3po.h`, `fastmath.h`, `x0x_param.h`: unchanged apart from a line saying where they come from.
- The sources are compiled as C++ inside `namespace x0x`, in `AcidRat.cpp`.
- New in `AcidRat.cpp`: MIDI handling (accent from velocity, slide from overlapping notes), a step clock that follows
  the MPC's tempo and song position, swing and gate length, the ping-pong delay, the master level, the programs.
- Not taken from X0X: its drum machines, break player, pattern editor, song mode, effects and everything specific to
  the FM-1.

## Trademarks

TB-303 and Devil Fish are names of their respective owners (Roland Corporation; Real World Interfaces), used only to
say what the engine models. RAT is a trademark of its owner, used only to name the circuit the drive is modelled on.
