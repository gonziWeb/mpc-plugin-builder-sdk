# Slow Garden: licensing and credits

Slow Garden is free software under the **Mozilla Public License 2.0** (`MPL-2.0`, full text in `LICENSE`), file by
file. You may use, study, change and share it. If you share a changed version of one of its files, share that file's
source under the MPL too. Each release zip carries the complete source (`slow-garden-<version>-source.zip`), and it
is at <https://github.com/gonziWeb/mpc-plugin-builder-sdk> in `plugins/Slow Garden/`.

Slow Garden puts the engine of **AMB-1**, a free firmware for the M-VAVE FM-1, into the MPC plugin format. Nothing of
M-VAVE's own firmware is used. Slow Garden is not made or endorsed by the authors named below, by M-VAVE, by Brian Eno
or by Akai / inMusic.

## Who made what

| What | Licence | Author |
| --- | --- | --- |
| `src/amb/amb.c`, `amb.h`: the instrument (pad, bells, bass, pulse, the echoes, the plate, the eight pieces) | MPL-2.0 | [AMB-1](https://github.com/charlesvestal/fm1-amb), Copyright (C) 2026 Charles Vestal; commit da0047d, `app/dsp/`. AMB-1 is MPL-2.0 from version 1.0 (0.9 and earlier were GPL-3.0-only); this is the MPL code |
| `src/amb/fastmath.h`: its maths | MPL-2.0 | [fm1-base](https://github.com/charlesvestal/fm1-base), Copyright (C) 2026 Charles Vestal; commit c8e0599, `include/` |
| The plate reverb inside `amb.c` | MPL-2.0 | after Jon Dattorro, "Effect Design, Part 1" (JAES 1997), by way of Charles Vestal's FoMni-1 |
| The Euclidean rhythms inside `amb.c` | MPL-2.0 | Bjorklund's algorithm as in Godfried Toussaint, "The Euclidean Algorithm Generates Traditional Musical Rhythms" |
| `src/core/amb_engine.h`: the same code rearranged by `tools/make_engine.py` | MPL-2.0 | Charles Vestal; the rearrangement Copyright (C) 2026 Gonzi Audio |
| `src/SlowGarden.cpp`, `tools/make_engine.py`, `ui.json`, programs 9 to 12, `gonzi/tools/test_garden_engine.cpp` | MPL-2.0 | Copyright (C) 2026 Gonzi Audio |
| `gonzi/tools/render_garden.cpp` | MIT | Copyright (C) 2026 Gonzi Audio |
| `framework/` (the SDK, not in this folder) and the screen's kit graphics | MIT | inMusic Brands, Inc. |

## What Slow Garden changed (2026-11-13)

- `src/amb/amb.c`, `amb.h`, `fastmath.h`: nothing. They are byte-for-byte copies.
- `src/core/amb_engine.h` is generated from them: every statement is kept, but the constants are placed at namespace
  scope and the mutable globals and the functions become members of `amb1::Engine`, so that several plugin instances
  each have their own engine. `#include` lines, `extern` declarations and prototypes are dropped; the engine's MIDI
  output goes nowhere (the MPC discards a plugin's MIDI). Checked against `amb.c` compiled as C: the same samples
  over 680 s of all eight pieces.
- New in `SlowGarden.cpp`: the plugin's parameters and programs, a Master trim per piece, following the MPC's tempo,
  beat and transport, pads that edit the scale, the seed, saving the scale with the project.
- Not taken from AMB-1: its screen, panel handling, storage, MIDI clock and note output, the screen reader, the
  web app, and everything specific to the FM-1 (fm1-base, felucca-bsp).

## Trademarks

"M-VAVE" and "FM-1" are trademarks of their owners, named only to say where the engine comes from. "MPC" and "Akai"
are trademarks of inMusic Brands, Inc.
