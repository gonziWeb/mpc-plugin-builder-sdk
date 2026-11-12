# Gonzi Audio — notes for AI agents working in this fork

This is Gonzi Audio's fork of the official MPC Plugin SDK (`inmusic-software/mpc-plugin-builder-sdk`).
Read `AGENTS.md` first and follow its workflow and hard rules; this file only adds what is specific to us.

## Base: the official SDK only
- Use this SDK and its skills (`.claude/skills/`). Do not use open-mpc/mpc-vst-plugins, Hakai, root SSH or any
  system modification. The only remote route is `tools/mpc_remote.py` (official developer mode).
- Keep upstream changes flowing: sync this fork from inMusic's repository ("Sync fork" on GitHub). Keep our own
  changes to SDK files minimal (this file, a line in `CLAUDE.md`, `.gitignore`) so syncs stay conflict-free.

## Hardware and computer
- MPC One, **first generation** (32-bit: loads the `file32` / arm32 build).
- MPC OS **3.10.1**, Preferences → Enhancements (Beta) → Enable Enhancements + External Plugin Support: **on**.
- Computer: **Windows 11**. Desktop listening build: `python tools/build.py "plugins/<Name>" --target desktop`
  (VST2 .dll, e.g. in Reaper).
- Install: copy `<Manufacturer> - VST - <Name>` into `VST2 Plugins/` at the root of a USB drive or SD card.

## Identity
- Manufacturer / vendor: **Gonzi Audio** (do not ask again).
- Propose each plugin's name and get it confirmed before scaffolding.
- `uid`, parameter ids and parameter order are permanent once released: only append parameters.

## Every plugin
1. `python tools/new_plugin.py "<Name>" --vendor "Gonzi Audio" --kind instrument|effect`
2. Write `src/<Class>.cpp`, `ui.json` (knob/fader/switch, blocks of four), `README.md`, presets with `addProgram`.
3. `python tools/build.py "plugins/<Name>"` → 0 errors.
4. `.sdk-tools/host_sim "plugins/<Name>/build/native/<slug>.so" test --seconds 6 --wav ...` → PASS, 0 FAIL.
5. Musical listening render: `gonzi/tools/render_demo.cpp` feeds a drum + chord loop through each program:
   ```
   g++ -O2 -std=c++17 -Iframework gonzi/tools/render_demo.cpp -o .sdk-tools/render_demo -ldl
   .sdk-tools/render_demo "plugins/<Name>/build/native/<slug>.so" out.wav 90 -1 0 1 2   # -1 = dry, then program numbers
   ```
   Instruments (drums): `gonzi/tools/render_kit.cpp` plays a MIDI groove (GM drum notes) through each program:
   ```
   g++ -O2 -std=c++17 -Iframework gonzi/tools/render_kit.cpp -o .sdk-tools/render_kit -ldl
   .sdk-tools/render_kit "plugins/<Name>/build/native/<slug>.so" out.wav 66 doom 0 1 2   # or "hits": each sound soft+hard
   ```
   Instruments (synths): `gonzi/tools/render_synth.cpp` plays 4 bars of held chords (Dm Bb Gm A) and a melody through
   each program, and prints peak and desktop CPU per program (`MPC_RATE=48000` tests another host rate):
   ```
   g++ -O2 -std=c++17 -Iframework gonzi/tools/render_synth.cpp -o .sdk-tools/render_synth -ldl
   .sdk-tools/render_synth "plugins/<Name>/build/native/<slug>.so" out.wav 70 0 1 2
   ```
6. `python tools/package.py "plugins/<Name>"`; hand over the zip and the WAV.

Our plugin projects live in `plugins/` and are tracked in git in this fork (`build/` and `dist/` stay ignored).

## Plugins
| Plugin | Kind | uid | Version | Licence | Folder |
|---|---|---|---|---|---|
| Dusty Tape | effect, lo-fi tape echo | G6pD | 1.0.0 | MIT | `plugins/Dusty Tape/` |
| Sabbath Kit | instrument, synthesised doom/stoner drum kit | Gpnq | 1.0.0 | MIT | `plugins/Sabbath Kit/` |
| Grimoire | instrument, dark ambient / dungeon synth (GR!MM + GHOULBOX FM-1 firmwares ported) | Gp4w | 1.2.0 | **GPL-3.0-only** | `plugins/Grimoire/` |

Dusty Tape parameter order (permanent): Output, Sync, Division, Time, Feedback, Tone, Low Cut, Mix, Wow, Flutter,
Drive, Hiss, Bits, Rate, Ping Pong.

Sabbath Kit parameter order (permanent): Master, Kick Tune, Kick Decay, Kick Level, Kick Beater, Snare Tune,
Snare Decay, Snare Level, Snare Wires, Tom1 Tune/Decay/Level, Tom2 Tune/Decay/Level, Tom3 Tune/Decay/Level,
Hat Tune/Decay/Level, Crash Tune/Decay/Level, Ride Tune/Decay/Level, Room, Room Size, Drive, Comp, Tone, Width,
Humanize, Vel Sens. Notes: General MIDI drum map (MPC Classic pads).

Grimoire parameter order (permanent, 94): Master, Voices, Vel Sens, Bend Range; then layer A (ids `a_*`) and layer B
(`b_*`), each: Source, Knob 1–8, Attack, Decay, Sustain, Release, Level, Pan, Cutoff, Reso, Filt Type, Glide, Mono,
Octave, Fine, LFO Rate, LFO Depth, LFO Dest, LFO Shape, Delay Snd, Reverb Snd, Grain Snd, Tape Snd, Drive Snd; then
Tape Wow/Sat/Tone/Hiss, Grain Size/Dens/Scat/Mix, Freeze, Delay Time/Fdbk/Wear/Mix/Sync, Space Size/Damp/Mix/Pre,
Mstr Cutoff/Reso/Type/Pos, Drive Amt/Type/Tone/Mix, Ambience, Crackle. Source choices (append only): VA, FM, Voice,
Wheel, Phys, Phase, Gurdy. Programs (63): append only, the numbers are what projects recall.

Grimoire notes: the engines are GR!MM's integer C sources (GRiMMBIENT/grimm, GPL-3.0) plus GHOULBOX's gurdy and
ambience, compiled as C++ in `namespace grimm`. `src/grimm/` = adapted sources (readable diff to GR!MM);
`src/core/` is generated from it by `tools/make_instanced.py` (each mutable global → per-instance state behind one
thread-local pointer; re-run after editing `src/grimm/`). GR!MM's effects assume 256-frame blocks at 44.1 kHz: the
plugin always renders 256 frames through a FIFO (latency 256, reported; resampled at other host rates). Being GPL,
every distributed zip must contain the complete source (`Grimoire-<ver>-source.zip` is put inside the MPC zip).
New programs: set `master` to 0, render with `render_synth`, then balance to about -19 dB RMS (peak under -2.5 dBFS).

Grimoire's screen (since 1.1.0): a themed skin in `skins/` (old spell book: leather pages, gilt cartouches, row
captions, sigils; iron/brass knobs, amber faders, wax-red switches). It is generated, not hand-edited:
`python3 plugins/Grimoire/tools/make_skin.py [--preview out.png]` (Pillow) runs the SDK's `gen_ui.py` on `ui.json`,
swaps the kit PNGs for its own drawings (same names and sizes), and inserts a full-tab background `Image` entry per
tab as text (never round-trip TUI.json through a JSON library: overlay widgets carry duplicate `version` keys).
Because `skins/` exists the build copies it verbatim, so after changing `ui.json` or parameters: build → make_skin →
build. `--preview` draws the pages for checking without an MPC. Not yet checked on the MPC's screen. The same recipe
can theme other plugins.
