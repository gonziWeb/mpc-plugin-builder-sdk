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
   Instruments that play themselves (Slow Garden): `gonzi/tools/render_garden.cpp` sends no notes; each program runs in
   a fresh instance for the given seconds. `GARDEN_SEED` (default 1, 0 = random), `GARDEN_BPM` (Sync = MPC, Grow = Song,
   transport playing), `MPC_RATE`. `check` runs its behaviour checks (seed, Song restart, beat lock, saved scale,
   two instances):
   ```
   g++ -O2 -std=c++17 -Iframework gonzi/tools/render_garden.cpp -o .sdk-tools/render_garden -ldl
   .sdk-tools/render_garden "plugins/<Name>/build/native/<slug>.so" out.wav 40 0 2 4
   .sdk-tools/render_garden "plugins/<Name>/build/native/<slug>.so" check
   ```
   Guitar effects (Monolith): `gonzi/tools/render_guitar.cpp` plays a dry electric-guitar part (a plucked-string model:
   power chords, palm mutes, a lead; `MPC_DI=<file.wav>` plays a real 16-bit DI take instead) through each section, and
   prints peak, RMS and desktop CPU. A section is `-1` (dry), a program, or `program:index=value,...` to set parameters
   by index (normalised) after the program. It re-prepares the plugin for each section, as the MPC does:
   ```
   g++ -O2 -std=c++17 -Iframework gonzi/tools/render_guitar.cpp -o .sdk-tools/render_guitar -ldl
   .sdk-tools/render_guitar "plugins/<Name>/build/native/<slug>.so" out.wav 70 -1 0 2 2:13=0.4444,37=1
   ```
6. `python tools/package.py "plugins/<Name>"`; hand over the zip and the WAV.

Our plugin projects live in `plugins/` and are tracked in git in this fork (`build/` and `dist/` stay ignored).

## Plugins
| Plugin | Kind | uid | Version | Licence | Folder |
|---|---|---|---|---|---|
| Dusty Tape | effect, lo-fi tape echo | G6pD | 1.0.0 | MIT | `plugins/Dusty Tape/` |
| Sabbath Kit | instrument, sampled doom/stoner drum kit + 12 built-in grooves | Gpnq | 2.2.0 | MIT code, **samples CC BY-SA 3.0** | `plugins/Sabbath Kit/` |
| Grimoire | instrument, dark ambient / dungeon synth (GR!MM + GHOULBOX FM-1 firmwares ported) | Gp4w | 1.2.0 | **GPL-3.0-only** | `plugins/Grimoire/` |
| Acid Rat | instrument, 303-style acid bass line (X0X's Open303 engine + TB-3PO line generator + 8 x 16-step patterns + osc / filter / envelope / LFO extensions) | G37u | 1.2.0 | **GPL-3.0-only** | `plugins/Acid Rat/` |
| Slow Garden | instrument, generative ambient that plays itself (AMB-1's engine: breathing pad, bells on loops of 13/17/23/29 beats, gliding bass, Euclidean pulse, tape echo + plate) | GggB | 1.0.0 | **MPL-2.0** | `plugins/Slow Garden/` |
| Monolith | effect, guitar rig builder: 6 blocks in any order (Neural Amp Modeler amp and pedal captures, cabinet IRs, gate, EQ, 10 drives, 8 delays, 8 reverbs), pedalboard screen | GMdM | 0.4.0 | MIT (NAM Core MIT, Eigen MPL-2.0, json MIT) | `plugins/Monolith/` |
| Patina | effect, lo-fi texture (vinyl crackle / pops / dust / rumble / warp, cassette wow / flutter / drive / hiss / dropouts, sampler bits + rate, small dark room, Age macro) | GIi1 | 1.0.0 | MIT | `plugins/Patina/` |
| Armada | instrument, 89 synth engines: the audio cores of 18 open FM-1 firmwares (Felucca and its family, OberVave, Shine On) + Mutable Instruments' Plaits, state per instance | GlB7 | 1.0.0 | **GPL-3.0-only** | `plugins/Armada/` |

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
v2.2: the user's MPC One pads on a plugin track are Pad Perform "Notes", C major, bank C from C4 (note 72), not
chromatic. `Pad Map` gained a 4th choice **MPC** (now the default): every C = kick, every D = snare, other notes
alternate between two sets by octave parity (`pieceForScale`), so any bank works. Grooves rewritten (user found them
weak): 4-bar phrases, ghost notes, foot hat, flams (`F`), and three fill variants per groove (`fill[].bars[0..2]`,
taken in turn; shared sets `FILLS_STRAIGHT/TRIPLET/SLOW/TRIBAL`).

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

Slow Garden parameter order (permanent, 34): Master, Tempo, Evolve, Motion, Drift, Pad, Swell, Bright, Colour, Bells,
Density, Bell Tone, Bell Oct, Bass, Bass Oct, Glide, Fifth, Pulse, Hits, Swing, Hats, Echo, Echo Time, Reverb, Size,
Root, Mode (0-26: the piece, what a program sets), then the player's, which programs leave alone (their value lists
stop at 27): Tune, Seed, Grow, Sync, Freeze, Next Chord, Key Edit. Choices (append only): Colour Rhodes/Voices; Echo
Time 1/4., 1/2, 1/2., 1 bar, 2 bars, 4 bars; Root C..B; Mode Ionian, Dorian, Phrygian, Lydian, Mixolyd., Aeolian,
Pent maj, Pent min; Grow Rest/Always/Song; Sync Off/MPC. Programs (12, append only): 1-8 AMB-1's pieces (read from the
engine's table; only Master differs, `kPieceTrim`), 9-12 ours (Crypt, Rainlight, Slow Pulse, Still Water).

Slow Garden notes: the engine is AMB-1 (charlesvestal/fm1-amb at da0047d, `app/dsp/amb.c` + `amb.h`, and `fastmath.h`
from fm1-base at c8e0599; MPL-2.0 since AMB-1 1.0, user chose to port it rather than rewrite). `src/amb/` holds the
three files byte for byte. AMB-1 keeps its state in file-level globals, so `tools/make_engine.py` rearranges
`amb.h` + `amb.c` into `src/core/amb_engine.h`: constants at namespace scope (`namespace amb1`), every mutable global
and every function a member of `amb1::Engine` (re-run after changing `src/amb/`; a simpler recipe than Grimoire's
`make_instanced.py`, usable when the engine is one file). `gonzi/tools/test_garden_engine.cpp` compares two Engine
objects with `amb.c` compiled as C: the same samples over 680 s (build lines in its header; run after any engine
change). The plugin (`SlowGarden.cpp`) makes one Engine per instance (290 KB, in the constructor) and rebuilds it in
place (`restart()`, no allocation, last sample faded) only on the first `reset()`, after a state load, and on a Song
start with a seed; a plain resume leaves the garden growing, and a program change regrows over the old tails as
AMB-1 does. The engine is always rendered 64 frames at a time and read from there, so a seed gives the same piece at
any host block size. The engine's members are public: with Sync on, the plugin calls `drain()`, then overwrites
`step_len` and `echo_len_t` with the MPC's exact tempo (the engine's own tempo is a whole number) and moves
`pulse_left` / `pulse` / `sixteenth` onto the MPC's beat (`lockToBeat`, which allows for the frames already rendered
ahead). Above 120 bpm the tempo is halved, below 30 doubled. Pads toggle scale notes (the pool's 12 bits, relative to
the root); the pool is the extra state ("SGD1"). Being MPL, the MPC zip carries `slow-garden-<ver>-source.zip`,
`LICENSE` and `LICENSING.md` (added by hand after `package.py`), and `Content/` holds the licence on the device.
Screen: the SDK kit, lime theme, pages GARDEN, LAYERS, SPACE (no themed skin yet). Not yet tried on the MPC: CPU on the
One (0.5 % of a desktop core), the screen, whether Sync / Grow = Song get tempo, position and play state as in the
simulator, and whether a project load applies the program after the state (an edited scale would then be lost).

Monolith parameter order (permanent, 53): Output, Input, In Chan, Quality; then for each block n = 1..6 (ids `s<n>_*`):
Type ("Block n"), File, On, Level, K1, K2, K3, K4; then DI Comp. Type choices (append only): Off, Gate, Amp, Cab IR, EQ,
Drive, Delay, Reverb, Pedal. The effect preset lists (`kDrivePresets`, `kDelayPresets`, `kReverbPresets`) and the
programs (6) are append only too.

Monolith notes: the engine is Neural Amp Modeler Core (sdatkinson/NeuralAmpModelerCore at d3189ed, MIT), unchanged in
`src/NAM/` and compiled as one translation unit by `src/nam_engine.cpp` with `NAM_SAMPLE_FLOAT` and `NAM_ENABLE_A2_FAST`;
it needs `src/Eigen` (bc3b398, MPL-2.0, headers only), `src/unsupported/Eigen/FFT` and `src/json.hpp`. `nam_bridge.h` is
the only interface `Monolith.cpp` sees. A first build takes about 3 minutes per target (Zig caches it afterwards;
`nam_engine.cpp` includes `rig_dsp.h`, so editing that header recompiles the engine). Captures (`.nam`) and impulse
responses (`.wav`) are whatever is under the plugin's `Content/` on the drive, listed when the plugin is created; a
capture is a pedal (Pedal blocks) when "pedal" is in its path or its metadata says `gear_type: pedal`, otherwise an amp.
Files are loaded by a worker thread or by `prepare()` and handed to the audio thread through a three-state atomic per
block. A capture trained at 48 kHz runs between two polyphase resamplers (Quality Hi-Fi, about 0.7 ms) or at 44.1 kHz as
is (Eco). The File and K parameters change name with the block type (the plugin asks the MPC to re-read them: this works
on the MPC One); until the first `prepare()` they are `1 File`, `1 K1`, ... and show plain numbers, which is what the
build's parameter dump and `ui.json` rely on. Two things found with the arm32 build under qemu, both handled in
`Monolith.cpp` and worth knowing for other plugins: `std::filesystem` directory iteration fails with EOVERFLOW (plain
`opendir` with `_FILE_OFFSET_BITS 64` is used), and a Zig-built library with static destructors crashes the host at
exit if it was unloaded (nothing runs `__cxa_finalize`; Monolith re-opens itself with `RTLD_NODELETE`; check a plugin
with `readelf -W --dyn-syms <lib> | grep cxa_atexit`).

Monolith's screen: a skin in `skins/`, generated by `python3 plugins/Monolith/tools/make_skin.py [--preview out.png]`
(Pillow). RIG shows the six blocks as gear on one line (film strips bound to each block's Type) and, under it, the
settings of the block that was touched (a radio group of buttons on the UI-only handle `Local/7/sel`; the controls of
all six blocks are stacked, each shown for its block only; `ui.json` has helper pages E1..E6 that the script folds into
RIG and removes). BLK 1-2 / 3-4 / 5-6 give the same settings to the Q-Links. After changing `ui.json` or parameters:
delete `skins/`, build, make_skin, build. On the MPC One so far: the plugin loads, the gear pictures follow the types,
a guitar plugged straight in plays. Not yet confirmed there: touching a block to select it, the hidden knobs, CPU
(estimated 12-15 % of a core per A2 Lite capture, two captures at most; A1 "standard" captures are too heavy), and
whether the output limiter cured a crackle on hard pick attacks.

Patina parameter order (permanent, 23): Output, Age, Tone, Width, Noise, Mix, Crackle, Pops, Dust, Rumble, Warp, Wow,
Flutter, Drive, Hiss, Dropouts, Hi Loss, Bits, Rate, Room Size, Room Tone, Room Mix, Low Cut. Choices (append only):
Noise Always/Follow. Programs (10, append only): Patina, Clean Vinyl, Dusty Crate, Needle Drop, Cassette,
Broken Walkman, SP Grit, Far Radio, Master Dust, Dream Tape.

Patina notes: our own DSP, no samples (every noise is synthesised), no third-party code. Chain: in -> room (4-delay
Householder FDN + 2 allpass diffusers) -> speed wobble on a short delay (warp = 33 rpm, wow, flutter) -> dropouts ->
tanh drive -> Hi Loss -> sample-and-hold + bit reduction -> + noises x gate -> Low Cut -> Tone -> width -> mix ->
output. The room is first so its tail gets the dirt too. Age multiplies every VINYL and TAPE amount and slides Bits
from 16 and Rate from 44.1 kHz towards their settings; Tone, Low Cut, Width, the room and Mix are outside Age.
Noise = Follow gates the noises on the input envelope (500 ms release). Sections at zero are skipped. Listening render:
`render_demo ... 90 -1 0 1 2 3 4 5 6 7 8 9`. Simulator: PASS at blocks 32 / 128 / 512, about 0.5 % of a desktop core.
Not yet tried on the MPC: CPU on the One, the screen (SDK kit, amber, pages MAIN / VINYL / TAPE / GRAIN), and whether
the crackle level suits real drum tracks (tuned on the synthetic demo loop only).

Armada (1.0.0, uid GlB7, instrument, GPL-3.0-only): 89 synth engines in one instrument. Each "pack" is the audio core
of one open M-VAVE FM-1 firmware (Felucca and 15 firmwares forked from it, OberVave, Shine On), compiled from that
firmware's own C sources, plus Mutable Instruments' Plaits (24 models, MIT, `src/plaits` + `src/stmlib` unchanged).
`ENGINES.md` lists every engine, its pack and its parameters; `LICENSING.md` every firmware, author and licence.

Armada parameter order (permanent, 71): Master, Engine (choice, 89: the order of `src/armada_engines.h`, append
only), Sound (0-255: the engine's own factory sounds), E1..E32 (the engine's own parameters, normalised 0..1 over the
engine's range; the value text is "<the parameter's name> <its value>", "-" = unused), then Attack, Decay, Sustain,
Release, Flt Env, Pit Env, Shp Env, LFO Rate, LFO Wave, LFO Fade, LFO>Pitch, LFO>Filter, LFO>Shape, LFO>Level, Voice,
Glide, Detune, Spread, Drive, Chorus, Delay, Reverb, Dly Time, Dly Fdbk, Rev Size, Rev Damp, Rev Type, Cho Rate,
Cho Depth, Flt Type (these 30 are `armada::CommonIndex` in `src/armada_pack.h`, same order, in the firmwares' 0..127
units), then Octave, Tune, Bend, Vel Sens, Low Cut, Cutoff. Programs (152, append only with the engines): each
engine's first one or two sounds. Extra state "ARM1": the engine and sound the parameters belong to.

How it works. `tools/make_pack.py <pack>` writes `src/pk_<pack>.cpp` from `tools/packs/<pack>.py`: it flattens the
firmware's translation unit with gcc -E (comments kept, system headers left as #include), parses it with libclang
(`pip install libclang`) as C, and rewrites every reference to a variable with static storage (file level, and
function statics, hoisted) as `PKV(k)`: the same variable inside a block per plugin instance, reached through one
thread-local pointer (`pk_tls`) plus a compile-time offset. Const tables that hold such variables' addresses are
instanced too, and the addresses inside a new block are moved to it. It also turns C-only constructs into C++
(implicit `void *` conversions, tentative definitions, C++ keywords used as names, `restrict`, `&(T){v}`), runs
`__attribute__((constructor))` functions per instance, and replaces `fm1_xip_ptr` (the FM-1's flash at a fixed
address: user samples / wavetables) by a page of zeros. NEVER let a pack read a hardware address: it segfaults the
host; `tools/test_pack.cpp fuzz` (every value of every small parameter, random settings, notes) must pass for every
pack, and the test's two-instances check must show no "INSTANCES DIFFER" (Plaits' noise models differ: its random
generator is one shared static, harmless).
- Felucca family (`tools/packs/_fel.py`): the pack is the firmware's `tests/hostsim.c` include list (core.h,
  engines.c, params.c, mod.c, voice.c, fx.c, seq.c) and its own `host_tracks_init` / `host_preset`, so the voices,
  envelopes, LFO, sends, chorus / delay / reverb and master are the firmware's own code; the plugin plays part 1
  (`trk_note_on`, `mix_block`). A spec's `expose` lists the engines offered; `fixes` / `pre_fixes` are regex fix-ups;
  `own_sound` / `own_name` give an engine sounds of ours (Melodee's Prophet).
- OberVave family (`_obv.py`, zvenson's layout: PDEF table, `live` patch, `note_event`, `engine_audio`): the spec maps
  up to 32 patch parameters to E1.. and others to the shared parameters.
- Plaits: `src/pk_plaits.cpp` + `pk_plaits_b.cpp` (two units: names clash in one), six voices, the module's TRIGGER
  held as a gate, an ADSR on its LEVEL input, +1.47 semitones because the module's code assumes 48 kHz.
- `tools/make_upstream.py` keeps the unchanged upstream files each pack needs in `upstream/<pack>.tar.xz`
  (`ARMADA_UPSTREAM=<dir of the unpacked archives> python3 tools/make_pack.py <pack>` regenerates byte for byte; the
  working clones were in /home/claude/src). `tools/pack_info.cpp` lists a pack's engines / parameters (ENGINES.md was
  made from it). The firmwares' generated headers (build/gen) came from their own tools (`tools/gen_upstream.py`).
- The plugin (`src/Armada.cpp`): a pack's Part is made (about 300 KB) on the first use of one of its engines, from
  the host's parameter / program / state calls, never in process(). An Engine or Sound change fades one block out,
  loads the sound, writes the sound's values into E1.. and the shared parameters and reports them to the host
  (`notifyHostParameterChanged`, end of the block); after a state load the sound is loaded for its hidden parts and
  the saved parameters win. Parts render 32 frames at 44.1 kHz into a small FIFO; other host rates: linear resampling.
  Output: Low Cut, Cutoff (the plugin's own), Master, a soft knee above 0.8.
- Sound banks left out on purpose: Melodee's Prophet-5 and CZ-1 factory banks (zeroed in the generated file; Pro-5 has
  12 programs of ours, CZ-1 is not offered), Guru-600's factory and community sounds (only its 55 own, sounds 201-255,
  are offered: `base` / `count` in `armada_engines.h`), Saadet (PPG wavetable ROM data).
- Checks: `host_sim` PASS (the one WARN is the tail after its random parameter sweep); `gonzi/tools/test_armada.cpp`
  (sound loading, state round trip, every engine sounds, two instances on two threads); `render_synth` over the 152
  programs (peaks -17.6 .. -1.4 dBFS, desktop CPU 0.14 .. 1.4 %, median 0.36 %). `SYNTH_BARS` / `SYNTH_TAIL` shorten
  render_synth's sections for the listening WAVs.
- Not yet tried on the MPC: CPU on the One (thread-local access on arm32 is one more cost), the 89-entry Engine menu,
  whether the screen follows the parameters a sound load moves. The ARM builds were not run (no qemu here).
- Candidates not done: Dungeon Tracker (its hostsim needs UI code), Saadet, S1Vave (OberVave layout, its own
  parameter list), and the firmwares with their own architectures (CS-1, Scintilla, FM-106, setBfree organ, MONK-1,
  Frodhes, CYMA-1, OPN-1, SID-1, OPLBOX, Chipbox...): each needs its own glue like `_obv.py` (`tools/packs/_std.py`
  gives the C++ side for a glue of ~15 sg_* functions). No licence in the repository = not usable (fm1-piano, fm1-p6).
