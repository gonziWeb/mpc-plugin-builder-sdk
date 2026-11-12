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
   .sdk-tools/render_kit "plugins/<Name>/build/native/<slug>.so" out.wav 68 builtin <prog> <paramIdx> <numChoices> <bars> <groove>...
   ```
   Instruments (synths): `gonzi/tools/render_synth.cpp` plays 4 bars of held chords (Dm Bb Gm A) and a melody through
   each program, and prints peak and desktop CPU per program (`MPC_RATE=48000` tests another host rate):
   ```
   g++ -O2 -std=c++17 -Iframework gonzi/tools/render_synth.cpp -o .sdk-tools/render_synth -ldl
   .sdk-tools/render_synth "plugins/<Name>/build/native/<slug>.so" out.wav 70 0 1 2
   ```
   Instruments (mono bass lines): `gonzi/tools/render_acid.cpp` plays 4 bars of a 16-step acid line with the transport
   running (accents = velocity 112, slides = overlapping notes; a cutoff sweep on parameter 1 in bars 3-4). A program
   whose parameter 14 ("Seq") is on gets a held note instead, so the plugin's own line plays. Prints peak, RMS and
   desktop CPU per program (`MPC_RATE=48000` tests another host rate):
   ```
   g++ -O2 -std=c++17 -Iframework gonzi/tools/render_acid.cpp -o .sdk-tools/render_acid -ldl
   .sdk-tools/render_acid "plugins/<Name>/build/native/<slug>.so" out.wav 128 0 1 2
   ```
6. `python tools/package.py "plugins/<Name>"`; hand over the zip and the WAV.

Our plugin projects live in `plugins/` and are tracked in git in this fork (`build/` and `dist/` stay ignored).

## Plugins
| Plugin | Kind | uid | Version | Licence | Folder |
|---|---|---|---|---|---|
| Dusty Tape | effect, lo-fi tape echo | G6pD | 1.0.0 | MIT | `plugins/Dusty Tape/` |
| Sabbath Kit | instrument, sampled doom/stoner drum kit + 12 built-in grooves | Gpnq | 2.0.0 | MIT code, **samples CC BY-SA 3.0** | `plugins/Sabbath Kit/` |
| Grimoire | instrument, dark ambient / dungeon synth (GR!MM + GHOULBOX FM-1 firmwares ported) | Gp4w | 1.2.0 | **GPL-3.0-only** | `plugins/Grimoire/` |
| Acid Rat | instrument, 303-style acid bass line (X0X's Open303 engine + TB-3PO line generator) | G37u | 1.0.1 | **GPL-3.0-only** | `plugins/Acid Rat/` |

Dusty Tape parameter order (permanent): Output, Sync, Division, Time, Feedback, Tone, Low Cut, Mix, Wow, Flutter,
Drive, Hiss, Bits, Rate, Ping Pong.

Sabbath Kit parameter order (permanent): Master, Kick Tune, Kick Decay, Kick Level, Kick Beater, Snare Tune,
Snare Decay, Snare Level, Snare Wires, Tom1 Tune/Decay/Level, Tom2 Tune/Decay/Level, Tom3 Tune/Decay/Level,
Hat Tune/Decay/Level, Crash Tune/Decay/Level, Ride Tune/Decay/Level, Room, Room Size, Drive, Comp, Tone, Width,
Humanize, Vel Sens, then (v2) Groove, Groove Mode, Fill, Groove Vel. Groove choices (append only): Off + the order of
`kGrooves` in `src/grooves.h`.

Sabbath Kit notes: v2 (2.0.0, code version 2000) is a 5-layer sampler; v1 was synthesised (rejected by the user as
unrealistic). Samples: AVL Drumkits "Red Zeppelin" (Glen MacArthur, CC BY-SA 3.0, from github.com/studiorack/avl-drumkits)
converted to 16-bit mono WAV in `Content/Samples/<piece>_<1..5>.wav`; attribution in `Content/LICENSE-samples.txt`.
Loaded once in the first `prepare()` (std::call_once) and shared by all instances, ~24 MB. Note map = GM laid out for
the MPC Classic pads (48 tom1, 47 tom1 edge, 45 tom2, 43/41 tom3). Grooves are drum-grid strings in `src/grooves.h`
(16 = 16ths, 12 = 8th triplets); Groove Mode Seq follows the host transport ppq, Pad mode starts/stops on note 60
(61 = fill). `gonzi/tools/export_grooves.cpp` writes the same grooves as MIDI files
(`.sdk-tools/export_grooves "plugins/Sabbath Kit/Content/MIDI Grooves" 70`). Not yet confirmed on the MPC that
Seq mode receives the transport.

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

Acid Rat parameter order (permanent, 31): Master, Cutoff, Reso, Env Mod, Decay, Accent, Wave, Tune, Octave, Drive,
Drive Type, Slide, Acc Decay, Acc Vel, Seq, Rate, Length, Seed, Density, Seq Accent, Seq Slide, Oct Range, Root, Scale,
Base Oct, Mutate, Gate, Swing, Delay Mix, Delay Time, Delay Fdbk. Choices (append only): Wave Saw/Square; Drive Type
Off/Soft/RAT; Seq Off/Keys/Song; Rate 1/8, 1/8T, 1/16, 1/16T, 1/32; Scale Minor, Phrygian, Harm Minor, Min Pent, Dorian,
Major; Mutate Off/1/2/4/8 bars; Delay Time 1/16, 1/8T, 1/8, 1/8D, 1/4. Programs (9): append only.

Acid Rat notes: the engine is X0X's `bass303` (charlesvestal/fm1-x0x at 512c0da, GPL-3.0-only; a float C port of
Open303 with Devilfish slide / accent-decay ranges and a Soft / RAT drive) and its TB-3PO line generator, copied into
`src/x0x/` and included from `AcidRat.cpp` inside `namespace x0x` (the build only compiles `src/*.cpp`, so C sources
in a subfolder are pulled in by `#include`). Our two changes to the engine are marked "Acid Rat": the sample rate is
the host's (`bass303_set_rate`, one static shared by all instances) and pots are fractional. With whole-number pots at
44.1 kHz the output equals X0X's source sample for sample (a harness that includes both files in two namespaces and
compares them: do this again after any engine edit). Built with `BASS303_OS 4`. MIDI: velocity >= Acc Vel = accent,
a note overlapping the previous one = slide. Seq Keys = the line plays while a note is held (transposed by it,
60 = as written); Seq Song = the line follows the transport. The step clock runs on `ctx.transport.ppqPos` while the
MPC plays and on its own count otherwise; after a jump or loop it re-aligns and still plays a step that began less
than a tenth of a step earlier. The line is a pure function of the generator parameters (no extra state saved).
Being GPL, the MPC zip carries `acid-rat-<ver>-source.zip`, `LICENSE` and `LICENSING.md` (added to the zip by hand
after `package.py`), and `Content/` holds the licence texts on the device. Not yet tried on the MPC: CPU on the One
(estimated a few percent), and whether the MPC reports `ppqPos` continuously across a looping sequence.

Acid Rat's screen (since 1.0.1, same sound as 1.0.0): a skin in `skins/` (black panel, acid-green print, a framed and
captioned row per block of four, hazard stripes, chrome-ringed knobs, a faint rat), generated by
`python3 "plugins/Acid Rat/tools/make_skin.py" [--preview out.png]` with the same recipe as Grimoire's: build ->
make_skin -> build after changing `ui.json` or parameters. Not yet checked on the MPC's screen.
