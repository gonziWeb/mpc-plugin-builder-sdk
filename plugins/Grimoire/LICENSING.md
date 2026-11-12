# Grimoire: licensing and credits

Grimoire is free software under the **GNU General Public License, version 3 only** (`GPL-3.0-only`, full text in
`LICENSE`). You may use, study, change and share it. If you give someone the plugin, give them its complete source
under the same licence too (the source zip that comes with each release, or this folder).

Grimoire is a port, to the MPC plugin format, of the sound engines and effects of two free firmwares for the M-VAVE
FM-1. Nothing of M-VAVE's own firmware is used. Grimoire is not made or endorsed by M-VAVE, by the firmwares' authors
or by Akai / inMusic.

## Who made what

| What | Licence | Author |
| --- | --- | --- |
| `src/grimm/` (except the files below): the engines VA, FM, VOICE (and its choirs), WHEEL, PHYS, PHASE; the envelope, glide and LFO; the effects TAPE, GRAIN, DELAY, SPACE, MASTER, DRIVE; the mix, the track filter and the output stage (`mix.c`, from `grimm.c`) | GPL-3.0-only | [GR!MM](https://github.com/GRiMMBIENT/grimm) (Copyright (C) 2026 GR!MM), on [Dinghy](https://github.com/zvenson/dinghy) (zvenson), [Felucca](https://github.com/hugelton/Felucca) (Copyright (C) 2026 Leo Kuroshita, Hügelton Instruments) and [SLOOP](https://github.com/isod89/sloop-fm1) (isod89). Each file's header says whose it is; GR!MM's `LICENSING.md` gives the details |
| `src/grimm/gurdy.c`: the hurdy-gurdy | GPL-3.0-only | [GHOULBOX](https://github.com/jasonpersinger/ghoulbox-fm1-dungeon-synth) (`eng_gurdy.c`, on Felucca) |
| `src/grimm/ambience.c`: the cave wind and the torch's crackle | GPL-3.0-only | GHOULBOX (`ambience.c`) |
| `src/grimm/grimoire_fm.h`: the FM patches. BELL, ORGAN, SOFT PAD, REED are GR!MM's own (`tools/gen_fm_patches.py`); FRENCH HORN, HARPSICHORD, GLASS CHOIR, DARK STRINGS, LOFI FLUTE, CRYPT ORGAN, TUBA are GHOULBOX's own (F9..F15, `tools/gen_fm6_patches.py`). No factory ROM data of any instrument | GPL-3.0-only | GR!MM, GHOULBOX |
| `src/grimm/fm6_core.c`: msfa, the 6-operator FM core of Dexed, ported to integer C by Leo Kuroshita | Apache-2.0 | Google Inc., Pascal Gauthier; `Content/LICENSES/Apache-2.0-msfa.txt` |
| `src/grimm/phys_dsp.c`: DaisySP's modal and string models, ported to fixed point by Leo Kuroshita | MIT | Electrosmith, Corp and Emilie Gillet; `Content/LICENSES/MIT-DaisySP.txt` |
| `src/grimm/phys_symp.c`: Rings' sympathetic strings, ported to fixed point by Leo Kuroshita | MIT | Emilie Gillet; `Content/LICENSES/MIT-Rings.txt` |
| `src/grimm/grimm_choir.h`: the choir wavetables (VOICE's TYPE) | CC BY 4.0 | Made by GR!MM from recordings of **VocalSet** (Wilkins et al., 2018, <https://doi.org/10.5281/zenodo.1442513>), licensed CC BY 4.0. Changes made by GR!MM: single cycles of six singers' steady tones measured into 64 harmonics and redrawn at four band-limited sizes (see GR!MM's `LICENSING.md`). No recording is included whole; no endorsement by the authors or singers is implied. `Content/LICENSES/CC-BY-4.0.txt` |
| `src/Grimoire.cpp`, `src/programs.h`, `src/grimm_core.h`, `tools/make_instanced.py`, `ui.json`, the changes marked "Grimoire" in `src/grimm/` | GPL-3.0-only | Copyright (C) 2026 Gonzi Audio |
| `skins/` (the screen: pages, knobs, faders, switches), drawn by `tools/make_skin.py`; `TUI.json` and `Q-Links.json` there come from the SDK's generator. The pages' captions are set in Latin Modern Roman Caps (GUST Font License) and DejaVu Serif (Bitstream Vera / DejaVu licence), as pixels in the images; no font file is included | GPL-3.0-only | Copyright (C) 2026 Gonzi Audio |
| `framework/` (the SDK, not in this folder) | MIT | inMusic Brands, Inc. |

Apache-2.0, MIT and CC BY 4.0 material may be combined into a GPL-3.0 work; the plugin built from it is GPL-3.0-only
as a whole.

## What Grimoire changed

- GR!MM's files are compiled as C++ inside `namespace grimm` (`src/grimm_core.h`). `src/core/` is made from
  `src/grimm/` by `tools/make_instanced.py`: every global that changes moves into the plugin instance's own state (so
  several Grimoires can run at once); nothing else changes.
- Two of GR!MM's four tracks (Grimoire's layers A and B), 16 voices instead of 6, up to 8 knobs a source.
- No SAMPLE source (the MPC samples), no sequencer, arpeggiator, chords, GENERATE, song mode, clock, screens, saves or
  USB (the MPC does those).
- Added: GHOULBOX's GURDY engine (without its crank, RING and temperaments) and ambience; GHOULBOX's seven FM patches;
  Grimoire's MIDI handling (velocity, sustain pedal, mod wheel, pitch bend, mono/legato), delay sync to the host tempo,
  resampling when the host does not run at 44.1 kHz.
