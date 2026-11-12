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
6. `python tools/package.py "plugins/<Name>"`; hand over the zip and the WAV.

Our plugin projects live in `plugins/` and are tracked in git in this fork (`build/` and `dist/` stay ignored).

## Plugins
| Plugin | Kind | uid | Version | Folder |
|---|---|---|---|---|
| Dusty Tape | effect, lo-fi tape echo | G6pD | 1.0.0 | `plugins/Dusty Tape/` |

Dusty Tape parameter order (permanent): Output, Sync, Division, Time, Feedback, Tone, Low Cut, Mix, Wow, Flutter,
Drive, Hiss, Bits, Rate, Ping Pong.
