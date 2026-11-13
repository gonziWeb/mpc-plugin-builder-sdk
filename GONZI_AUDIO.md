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
| Sabbath Kit | instrument, sampled doom/stoner drum kit + 12 built-in grooves | Gpnq | 2.1.0 | MIT code, **samples CC BY-SA 3.0** | `plugins/Sabbath Kit/` |
| Grimoire | instrument, dark ambient / dungeon synth (GR!MM + GHOULBOX FM-1 firmwares ported) | Gp4w | 1.2.0 | **GPL-3.0-only** | `plugins/Grimoire/` |
| Acid Rat | instrument, 303-style acid bass line (X0X's Open303 engine + TB-3PO line generator + 8 x 16-step patterns + osc / filter / envelope / LFO extensions) | G37u | 1.2.0 | **GPL-3.0-only** | `plugins/Acid Rat/` |

Dusty Tape parameter order (permanent): Output, Sync, Division, Time, Feedback, Tone, Low Cut, Mix, Wow, Flutter,
Drive, Hiss, Bits, Rate, Ping Pong.

Sabbath Kit parameter order (permanent): Master, Kick Tune, Kick Decay, Kick Level, Kick Beater, Snare Tune,
Snare Decay, Snare Level, Snare Wires, Tom1 Tune/Decay/Level, Tom2 Tune/Decay/Level, Tom3 Tune/Decay/Level,
Hat Tune/Decay/Level, Crash Tune/Decay/Level, Ride Tune/Decay/Level, Room, Room Size, Drive, Comp, Tone, Width,
Humanize, Vel Sens, then (v2) Groove, Groove Mode, Fill, Groove Vel, then (v2.1) Play, Pad Map. Groove choices (append only): Off + the order of
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
v2.1 (user could not find the snare nor start a groove): `Pad Map` C2/C1 = 16-pad layout repeating every 16 notes from
note 48/36 (kick pad 1, snare pad 2), GM = the old map (needed for the MIDI files); which notes MPC pads send to a
plugin track is still unconfirmed. `Play` switch = groove free-runs at host tempo without the sequencer, random groove
when Groove is Off. Themed skin (black amp panel, orange glow, pad map drawn on the GROOVE page) generated by
`plugins/Sabbath Kit/tools/make_skin.py` (Grimoire's recipe, MIT here); not yet checked on the MPC's screen, so a
`-plain-ui` zip (built without `skins/`) is handed over as a fallback.

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
Major; Mutate Off/1/2/4/8 bars; Delay Time 1/16, 1/8T, 1/8, 1/8D, 1/4. Then (1.1.0, 118 in all): Source (Dice/Pattern),
Pattern (1-8), Steps (1-16), Write, Dice>Pat, Clear, Pos, then S1..S16 Note (38-53), S1..S16 Oct (54-69), S1..S16 Gate
(70-85), S1..S16 Acc (86-101), S1..S16 Slide (102-117). Then (1.2.0, 139 in all): Blend, Sub, Sub Oct, Pulse W, Noise,
Cut Shift, Key Track, Filter FM, Low Cut, Env Attack, Acc Sweep, Amp Attack, Amp Decay, Release, LFO Shape, LFO Sync,
LFO Rate, LFO Div, LFO>Cut, LFO>Pitch, LFO>PW (118-138). Programs (16): append only. Since 1.2.0 every program sets
all 139 parameters (so an old program also switches the 1.2 extras off); `reset()` then puts the patterns, the
pattern shown and - unless the program sets Source itself - the sequencer's source back from what the audio thread
last had (`slots_`, `curPattern_`, `source_`).

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

Acid Rat's step sequencer (1.1.0): the 80 step parameters are the pattern on the screen; eight patterns live in
`slots_` (atomics, packed: note | oct << 4 | gate << 6 | acc << 7 | slide << 8) and in the extra state ("ARP1").
Each block the step parameters are copied into the current slot; when `Pattern` changes the new slot is written into
the parameters (`showPattern`). A loaded state waits in a staging copy until `reset()` (and is what a save writes
until then). Parameters the plugin moves itself (pattern shown, `Pos` for the running light, Write going off) are
flagged and reported with `notifyHostParameterChanged` at the end of the block, from the audio thread. `Pos` and
Write are left as loaded so the simulator's state round trip passes. The STEP tab is not a generator page:
`make_skin.py` (`step_tab`, `add_step_page`) appends it to TUI.json as text with direct Button / Slider (ColourBar) /
Label / Indicator entries bound to `Parameter N`, adds their `replacementStrings`, and a Q-Links map (RowTopDown,
the four banks = the four beats, knobs = notes); its preview draws those widgets too. The two momentary keys use
`ResetOnGestureEnd/`. To confirm on the MPC: the screen follows plugin-moved parameters, the Indicator lamps, the
momentary keys, the translucent ColourBar bars, and whether a host call from the audio thread causes any glitch.
`gonzi/tools/test_acid_steps.cpp` runs 24 checks on it (build line in its header); run it after any change to the
sequencer. A 1.0.1 state was checked to load unchanged into 1.1.0, and the arm32 build passes host_sim under qemu.

Acid Rat 1.2.0 ("beyond the 303"): the engine got `bass303_ext_t` (`b->x`, set by `bass303_ext_osc / _duty /
_filter / _pitch / _env`, all marked "Acid Rat 1.2"). Neutral values leave X0X's own code paths running, so the
sample-for-sample comparison with X0X's source still holds, and programs 1-9 render the same bytes as 1.0 (compare a
`render_acid ... 0 1 2 3 4 5 6 7 8` WAV with an older one after any engine change). Extended paths: `run_osc_ext`
(blend, duty, polyBLEP sub square, noise), `run_ladder_ext` (filter FM: the input raises the coefficient up to 3x,
upwards only - downwards the feedback computed for the unmodulated cutoff makes the loop unstable, which was tried
and blew up), and branches in `run_control` (cutoff multiplier, key tracking against 110 Hz, accent sweep scale,
pitch multiplier, amp attack / decay / release; with any of the last three the envelope boost multiplies the amp
instead of adding to it). In the plugin: `updateDeep()` per block, `modulate()` before each piece of audio (pieces
of 16 frames while the LFO is in use), the LFO from `beat_` when synced, Low Cut as a one-pole after the engine.
UI pages are now ACID, TONE, LFO, LINE (ui.json) + STEP (make_skin.py, placed after the generator's pages).
