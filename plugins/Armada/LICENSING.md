# Armada licensing

Armada is free software under the GNU General Public License, version 3 only (`GPL-3.0-only`, text in `LICENSE`).
Copyright (C) 2026 Gonzi Audio for the plugin (`src/Armada.cpp`, `src/armada_*.h`, `src/pk_plaits*.cpp`, `tools/`), and
the authors below for what each pack is made of.

Armada is a combined work: each pack is a firmware's own source code, compiled into the plugin. All of those
firmwares are GPL-3.0-only (two also carry MPL-2.0 files), and some of their parts keep a more permissive licence of
their own. The complete source of everything in the binary is in this folder: the generated files `src/pk_*.cpp` are
what is compiled, `upstream/<pack>.tar.xz` holds the unchanged upstream files they are generated from, `tools/` the
generator. If you pass Armada on, pass the source on with it (the release zip contains it).

| Pack | Firmware, author, source | Licence |
|---|---|---|
| felucca | Felucca, Leo Kuroshita / Hügelton Instruments, github.com/hugelton/Felucca | GPL-3.0-only; the ANALOG, LOFI, TRIO, WHEEL, NOISE and PHASE engines MIT (Felucca-Engines) |
| melodee | Melodee, keremimo (Kerem Kilic), github.com/keremimo/melodee | GPL-3.0-only |
| guru600 | Guru-600, Marc Massé, github.com/MarcGoudy/guru-600 | GPL-3.0-only |
| opx | Felucca OPX, thomasgregorymarsden-code, github.com/thomasgregorymarsden-code/Felucca-opx | GPL-3.0-only |
| caravel | Caravel, bonszaimusic, github.com/bonszaimusic/caravel | GPL-3.0-only |
| switchblade | Switch Blade, OllieNicklinUK, github.com/OllieNicklinUK/switchblade | GPL-3.0-only |
| minitone | Mini-Tone, Stephane Schmitz (eldontyrellcorp), github.com/eldontyrellcorp/Mini-Tone | GPL-3.0-only |
| sqrl | SQRL-1, hed0rah, github.com/hed0rah/sqrl-1 | GPL-3.0-only |
| golana | Golana, Gadlele, github.com/Gadlele/Golana | GPL-3.0-only |
| atlatl | Skiff Atlatl, LIkst4, github.com/LIkst4/skiff-atlatl (Skiff by illuminus5; its FMINI engine from FMini-1 by Guilherme Ruppert) | GPL-3.0-only |
| araminta | Araminta, Wulgaren, github.com/Wulgaren/Araminta | GPL-3.0-only |
| kriechstrom | KRIECHSTROM, Xforsa, github.com/Xforsa/kriechstrom | GPL-3.0-only |
| dirge | DIRGE, sinscum, github.com/sinscum/DIRGE | GPL-3.0-only |
| jangada | Jangada, zednaked, github.com/zednaked/jangada | GPL-3.0-only |
| jiant | JIANT FM, juanjiant-bit, github.com/juanjiant-bit/JIANT | GPL-3.0-only |
| clipper | Clipper, 3dSam (isod89), github.com/isod89/clipper-fm1 | GPL-3.0-only |
| obervave | OberVave, zvenson, github.com/zvenson/obervave | GPL-3.0-only |
| shineon | Shine On, zvenson, github.com/zvenson/shineon | GPL-3.0-only |
| plaits | Plaits and stmlib, Emilie Gillet / Mutable Instruments, github.com/pichenettes/eurorack and github.com/pichenettes/stmlib (`src/plaits/`, `src/stmlib/`, unchanged) | MIT |

The upstream commit each pack was taken at is in its `tools/packs/<pack>.py` and in `ARMADA-UPSTREAM.txt` inside its
archive. Each firmware's own `LICENSING.md` (what it took from whom) and the texts of the licences it names are in
`Content/LICENSES/<pack>/`, and on the MPC beside the plugin. What recurs in most of them:

- msfa by Google Inc. and Pascal Gauthier (from Dexed): the FM6 engines' synthesis. Apache-2.0.
- DaisySP by Electrosmith and Emilie Gillet, and Rings by Emilie Gillet: the PHYS engine's models. MIT.
- Versilian Studios VSCO-2 Community Edition / VCSL: the SAMPLE, GRAIN and SLICE engines' instrument samples. CC0 1.0.
- The Hügelton Sample Pack (Felucca's drum sounds). CC0 1.0.
- snes_spc by Shay Green in Felucca's sources (the ADPCM engine: not built here). LGPL-2.1-or-later.
- Mini-Tone's SID engine carries GPL-2.0-or-later files (after reSID); the combination is distributed under GPL-3.0.
- felucca-bsp and parts of fm1-base (charlesvestal) in the firmwares' hardware layers: MPL-2.0; those files stay
  MPL-2.0. Almost none of that layer is compiled into Armada (it plays no hardware), a few helper files are.

## What is deliberately not in Armada

- Sequential's Prophet-5 factory programs and Casio's CZ-1 factory tones, which Melodee's resources carry: the two
  tables are zeroed in `src/pk_melodee.cpp` (see `tools/packs/melodee.py`). The Pro-5 engine's 12 programs are Gonzi
  Audio's own.
- Guru-600's bank holds Sequential's 100 Prophet-600 factory programs and a 100-sound community collection besides
  its 55 own sounds. Armada offers only the 55 (`src/armada_engines.h`); the other entries are still present in
  Guru-600's source file as upstream wrote it, and cannot be selected.
- Firmwares whose sound data comes from an instrument's ROM (the PPG wavetables in Saadet) were left out.

Engine names such as Pro-5, Juno, SEM, OB-Z, TB-303 or SID describe what an engine models. They are the trademarks
of their owners, who have nothing to do with Armada.
