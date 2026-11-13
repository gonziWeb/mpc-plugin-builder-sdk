# Drum Rat: licensing and credits

Drum Rat is free software under the **GNU General Public License, version 3 only** (`GPL-3.0-only`, full text in
`LICENSE`). You may use, study, change and share it. If you give someone the plugin, give them its complete source
under the same licence too (the source zip that comes inside each release zip, or this folder).

Drum Rat puts the two drum machines of **X0X**, a free firmware for the M-VAVE FM-1, into the MPC plugin format.
Nothing of M-VAVE's own firmware is used. Drum Rat is not made or endorsed by the authors named below, by M-VAVE, by
Roland or by Akai / inMusic.

## Who made what

| What | Licence | Author |
| --- | --- | --- |
| `src/x0x/drum808.c`, `drum808.h`: the 808 | GPL-3.0-only | [X0X](https://github.com/charlesvestal/fm1-x0x) (Charles Vestal and contributors; commit 512c0da, `firmware/src/dsp/`), with speed-ups after [Optimist](https://github.com/w0ts/optimist) (henri / hdavid). X0X ported it from [8W8](https://github.com/athousanddetails/schwung-8W8) (athousanddetails and Gustavo Lima, GPL-3.0); circuit models after the TR-808 service notes and the papers of Kurt James Werner, Jonathan S. Abel and Julius O. Smith; the rim shot after sc808 (Yoshinosuke Horiuchi / Sam Aaron, MIT) |
| `src/x0x/drum909.c`, `drum909.h`, `drum909_dsp.h`: the 909 | GPL-3.0-only | X0X, ported from [9W9](https://github.com/athousanddetails/schwung-9W9) (athousanddetails, GPL-3.0), itself after [ER-99](https://github.com/matthewcieplak/er-99) (Matthew Cieplak, GPL-3.0) |
| The 909's hi-hat, ride and crash recordings: `tools/909/*.wav`, and as data in `src/x0x/x0x_drum_samples.h` | GPL-3.0 | ER-99, through 9W9 and X0X |
| `src/x0x/x0x_drum_samples.h`, `x0x_drum_tables.h`: written by `tools/gen_drum_samples.py` (X0X's, unchanged) from those recordings | GPL-3.0-only | X0X |
| `src/x0x/fastmath.h`, `x0x_param.h` | GPL-3.0-only | X0X |
| `src/DrumRat.cpp`, `ui.json`, the three lines marked "Drum Rat" in `src/x0x/`, `gonzi/tools/render_seq.cpp`, `test_drum_rat.cpp`, `test_drum_engines.cpp` | GPL-3.0-only | Copyright (C) 2026 Gonzi Audio |
| `framework/` (the SDK, not in this folder) | MIT | inMusic Brands, Inc. |

MIT material may be combined into a GPL-3.0 work; the plugin built from it is GPL-3.0-only as a whole.

## What Drum Rat changed (2026-11-13)

- `drum808.c` and `drum909.c`: three file-level variables that hold "the render in progress" (`d8_dry_r`, `d9_dry_r`,
  `d9_pl0` / `d9_pr0`) are declared `X0X_TLS`, which the plugin defines as `thread_local`, so that two plugin
  instances can render at the same time on two processor cores. Nothing else is changed: the output is the same as
  X0X's own sources compiled as C, sample for sample (`gonzi/tools/test_drum_engines.cpp`: 60 s of random hits and
  pot changes on both machines).
- The sources are compiled as C++ inside `namespace x0x`, in `DrumRat.cpp`.
- New in `DrumRat.cpp`, by Gonzi Audio: the eleven shared tracks and the choice of machine per track, the note maps,
  the 16-step sequencer and its eight patterns, the clock that follows the MPC, the level trims between the two
  machines, the resampling at host rates other than 44.1 kHz, the master level and soft limiter, the programs.
- Not taken from X0X: its reverb and delay sends and master stage (`fxbus`, `master`), its sequencer, pattern
  editor, probability and motion, the 303s (those are in Acid Rat), the break player and everything specific to the
  FM-1.

## Trademarks

TR-808 and TR-909 are names of Roland Corporation, used only to say what the engines model.
