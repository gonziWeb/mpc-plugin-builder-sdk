#!/usr/bin/env python3
# SPDX-License-Identifier: MIT
# Copyright (C) 2026 Gonzi Audio
"""Monolith's skin: a pedalboard on the MPC's screen.

  python3 plugins/Monolith/tools/make_skin.py [--preview out.png]     (from the SDK root, after a build)

Same recipe as the other Gonzi Audio skins: run the SDK's generator on ui.json (controls, Q-Link map and
overlays stay the generator's, which are known to work on the MPC), then add to it, as text:

  RIG page     the whole board on one line: the six blocks as gear, in signal order, then the input / output
               box. Each block's picture is one film strip (tile.png, a frame a type) bound to the block's
               Type parameter, so the board redraws itself; a lamp (lamp.png) follows On; the type and the
               capture / IR name are Value labels. Touching a block selects it (a radio group of Buttons on
               the UI-only handle Local/7/sel) and its settings appear under the board: the controls of all
               six blocks sit in the same two rows, each shown only while its block is selected. The
               generator cannot stack controls, so ui.json has one helper page a block (E1..E6); they are
               folded into RIG here and removed.
  BLK pages    two blocks a page with all 16 Q-Links live (on RIG the hardware knobs cannot follow the
               selected block: a tab has one fixed knob map). Title bars in the type's colour (plate.png).
  everywhere   File and the knobs a type does not use are hidden. The generator can only show a control for
               ONE choice; the Type choices are ordered so that every rule is a range, and its
               IndexedEnabling handles are rewritten as RangedEnabling here (see RULES).

Everything is drawn here with Pillow: no outside artwork, no brand's gear. The result goes to skins/ (the
build copies skins/ verbatim, so: build -> make_skin -> build). --preview draws what the MPC should show,
with a stand-in font, for a sample rig.
"""
import argparse
import json
import math
import os
import re
import shutil
import subprocess
import sys

from PIL import Image, ImageDraw, ImageFilter, ImageFont

HERE = os.path.dirname(os.path.abspath(__file__))
PROJ = os.path.normpath(os.path.join(HERE, ".."))
SDK = os.path.normpath(os.path.join(PROJ, "..", ".."))
SKINS = os.path.join(PROJ, "skins")

W, H = 1280, 628                                              # a tab's area
XS, YS, SW, SH = (23, 343, 663, 983), (45, 196, 346, 497), 278, 107   # the generator's 4 x 4 grid

# The plugin's parameter layout (src/Monolith.cpp): 4 globals, then 8 a block.
N_GLOBAL, PER_BLOCK, BLOCKS = 4, 8, 6
P_TYPE, P_FILE, P_ON, P_LEVEL, P_K1 = 0, 1, 2, 3, 4


def par(block, which):
    return N_GLOBAL + block * PER_BLOCK + which


TYPES = ("Off", "Gate", "Amp", "Cab IR", "EQ", "Drive", "Delay", "Reverb", "Pedal")   # the Type choice, in order
TITLE = ("EMPTY  -  CHOOSE A TYPE", "NOISE GATE", "AMP  -  NEURAL CAPTURE", "CABINET  -  IMPULSE RESPONSE",
         "EQUALISER", "DRIVE", "DELAY", "REVERB", "PEDAL  -  NEURAL CAPTURE")
COLOUR = ((110, 110, 116), (70, 200, 150), (240, 150, 40), (205, 170, 115), (90, 150, 240), (130, 210, 80),
          (175, 125, 245), (80, 200, 230), (240, 110, 70))
KNOBS = (("", "", "", ""), ("Thresh", "Release", "", ""), ("Gain", "Size", "", ""), ("Lo Cut", "Hi Cut", "Mix", ""),
         ("Bass", "Mid", "Mid Freq", "Treble"), ("Drive", "Tone", "Tight", "Mix"), ("Time", "Feedback", "Tone", "Mix"),
         ("Decay", "Damp", "Mix", "PreDelay"), ("Gain", "Size", "", ""))          # for the preview only
FILE_NAME = ("-", "-", "Amp", "Cab IR", "-", "Preset", "Preset", "Preset", "Pedal")

SEL = "Local/7/sel"                                    # which block the RIG page shows: 0..5, 6 = input / output

# ui.json says visibleWhen <one choice>; that choice names the rule, the types that show the control are here.
# A control with two ranges is written twice (same place, same parameter), one copy a range.
RULES = {1: [(1, 8)],            # K1, K2 (ui.json: "Gate"): every type but Off
         2: [(2, 3), (5, 8)],    # File (ui.json: "Amp"): captures, IRs, and the presets of Drive / Delay / Reverb
         3: [(3, 7)],            # K3 (ui.json: "Cab IR"): Cab IR to Reverb
         4: [(4, 7)]}            # K4 (ui.json: "EQ"): EQ to Reverb

INK = (12, 12, 13)
PANEL = (27, 27, 30)
EDGE = (58, 58, 64)
BONE = (236, 232, 222)
DIM = (140, 138, 132)
AMBER = (224, 160, 32)

BOLD = "/usr/share/fonts/truetype/dejavu/DejaVuSans-Bold.ttf"
SANS = "/usr/share/fonts/truetype/dejavu/DejaVuSans.ttf"


def font(path, size):
    return ImageFont.truetype(path, int(size))


def shade(c, k):
    return tuple(max(0, min(255, int(v * k))) for v in c)


# ------------------------------------------------------------------------------------- ordered JSON
class Obj(list):
    """a JSON object as a list of (key, value): TUI.json repeats keys (ui-reference.md 1.8) and their order matters"""
    def get(self, key):
        return next(v for k, v in self if k == key)

    def set(self, key, value):
        for i, (k, _) in enumerate(self):
            if k == key:
                self[i] = (k, value)
                return
        self.append((key, value))

    def has(self, key):
        return any(k == key for k, _ in self)


def loads(txt):
    return json.loads(txt, object_pairs_hook=Obj)


def dumps(v, ind=0):
    pad, pad2 = " " * ind, " " * (ind + 4)
    if isinstance(v, Obj):
        if not v:
            return "{}"
        return "{\n" + ",\n".join(pad2 + json.dumps(k) + ": " + dumps(x, ind + 4) for k, x in v) + "\n" + pad + "}"
    if isinstance(v, list):
        if not v:
            return "[]"
        return "[\n" + ",\n".join(pad2 + dumps(x, ind + 4) for x in v) + "\n" + pad + "]"
    return json.dumps(v)


def obj(d):
    """a plain dict (insertion order) -> Obj, recursively"""
    if isinstance(d, dict):
        return Obj((k, obj(x)) for k, x in d.items())
    if isinstance(d, list):
        return [obj(x) for x in d]
    return d


# ------------------------------------------------------------------------------------------------- the gear
COLS, TW, GAP, X0 = 7, 173, 8, 10                        # the board: seven places across the screen
GEAR_H = 150
BOARD_Y, BOARD_H = 6, 270                                # a place: number + lamp, gear, type, file name
GEAR_Y, TYPE_Y, FILE_Y = BOARD_Y + 30, BOARD_Y + 184, BOARD_Y + 212


def place_x(i):
    return X0 + i * (TW + GAP)


def knob(d, x, y, r, s, col=BONE):
    d.ellipse(((x - r) * s, (y - r) * s, (x + r) * s, (y + r) * s), fill=(20, 20, 22), outline=(0, 0, 0), width=s)
    d.ellipse(((x - r + 2) * s, (y - r + 2) * s, (x + r - 2) * s, (y + r - 2) * s), outline=(70, 70, 76), width=s)
    d.line((x * s, y * s, (x + r * 0.55) * s, (y - r * 0.75) * s), fill=col, width=2 * s)


def pedal(d, s, col, title, glyph, n_knobs):
    """a stompbox: coloured case, knobs along the top, a drawing of what it does, a footswitch"""
    x0, y0, x1, y1 = (TW - 120) / 2, 6, (TW + 120) / 2, GEAR_H - 6
    d.rounded_rectangle((x0 * s, (y0 + 3) * s, x1 * s, (y1 + 3) * s), radius=12 * s, fill=(0, 0, 0))            # shadow
    d.rounded_rectangle((x0 * s, y0 * s, x1 * s, y1 * s), radius=12 * s, fill=shade(col, 0.72), outline=shade(col, 1.15), width=2 * s)
    d.rounded_rectangle(((x0 + 5) * s, (y0 + 5) * s, (x1 - 5) * s, (y0 + 46) * s), radius=8 * s, fill=shade(col, 0.5))
    cx = (x0 + x1) / 2
    for i in range(n_knobs):
        knob(d, cx + (i - (n_knobs - 1) / 2) * (96 / max(n_knobs, 3) + 4), y0 + 25, 12 if n_knobs <= 3 else 10, s)
    glyph(d, s, cx, y0 + 66, INK)
    d.text((cx * s, (y0 + 90) * s), title, font=font(BOLD, 13 * s), fill=INK, anchor="mm")
    d.ellipse(((cx - 12) * s, (y1 - 32) * s, (cx + 12) * s, (y1 - 8) * s), fill=(196, 196, 200), outline=(40, 40, 44), width=2 * s)
    d.ellipse(((cx - 7) * s, (y1 - 27) * s, (cx + 7) * s, (y1 - 13) * s), outline=(120, 120, 126), width=s)


def g_gate(d, s, cx, cy, col):                      # a signal that only passes above a line
    pts = [(cx - 44 + i * 4, cy - (10 if 6 <= i <= 15 else 2) * math.sin(i * 1.3)) for i in range(23)]
    d.line([(x * s, y * s) for x, y in pts], fill=col, width=2 * s, joint="curve")
    d.line(((cx - 46) * s, (cy - 6) * s, (cx + 46) * s, (cy - 6) * s), fill=col, width=s)


def g_drive(d, s, cx, cy, col):                     # a clipped wave
    pts = [(cx - 44 + i * 2, cy - max(-7, min(7, 14 * math.sin(i * 0.36)))) for i in range(45)]
    d.line([(x * s, y * s) for x, y in pts], fill=col, width=2 * s, joint="curve")


def g_eq(d, s, cx, cy, col):                        # three sliders
    for i in (-1, 0, 1):
        x = cx + i * 26
        d.line((x * s, (cy - 11) * s, x * s, (cy + 11) * s), fill=col, width=2 * s)
        y = cy + (-6, 4, -2)[i + 1]
        d.rectangle(((x - 8) * s, (y - 3) * s, (x + 8) * s, (y + 3) * s), fill=col)


def g_delay(d, s, cx, cy, col):                     # repeats, fading
    for i in range(5):
        h = 11 - i * 2.2
        x = cx - 40 + i * 20
        d.rounded_rectangle(((x - 4) * s, (cy - h) * s, (x + 4) * s, (cy + h) * s), radius=2 * s, fill=col)


def g_capture(d, s, cx, cy, col):                   # a small net: this pedal is a neural capture
    pts = ((-30, -8), (-30, 8), (0, -11), (0, 0), (0, 11), (30, 0))
    for a in (0, 1):
        for b in (2, 3, 4):
            d.line(((cx + pts[a][0]) * s, (cy + pts[a][1]) * s, (cx + pts[b][0]) * s, (cy + pts[b][1]) * s), fill=col, width=s)
    for b in (2, 3, 4):
        d.line(((cx + pts[b][0]) * s, (cy + pts[b][1]) * s, (cx + pts[5][0]) * s, (cy + pts[5][1]) * s), fill=col, width=s)
    for x, y in pts:
        d.ellipse(((cx + x - 4) * s, (cy + y - 4) * s, (cx + x + 4) * s, (cy + y + 4) * s), fill=col)


def g_reverb(d, s, cx, cy, col):                    # a sound spreading in a room
    for i in range(4):
        r = 7 + i * 8
        d.arc(((cx - 22 - r) * s, (cy - r) * s, (cx - 22 + r) * s, (cy + r) * s), -30, 30, fill=col, width=2 * s)
    d.ellipse(((cx - 26) * s, (cy - 4) * s, (cx - 18) * s, (cy + 4) * s), fill=col)


def amp(d, s, col):
    """an amp head: tolex box, grille, a control strip"""
    x0, y0, x1, y1 = 7, 26, TW - 7, GEAR_H - 18
    d.rounded_rectangle((x0 * s, (y0 + 3) * s, x1 * s, (y1 + 3) * s), radius=10 * s, fill=(0, 0, 0))
    d.rounded_rectangle((x0 * s, y0 * s, x1 * s, y1 * s), radius=10 * s, fill=(30, 30, 33), outline=(86, 86, 92), width=2 * s)
    d.rounded_rectangle(((x0 + 40) * s, (y0 - 9) * s, (x1 - 40) * s, (y0 + 2) * s), radius=5 * s, fill=(52, 52, 56))   # the handle
    gx0, gy0, gx1, gy1 = x0 + 9, y0 + 10, x1 - 9, y0 + 58
    d.rectangle((gx0 * s, gy0 * s, gx1 * s, gy1 * s), fill=(56, 46, 30), outline=(110, 96, 70), width=s)
    for i in range(int((gx1 - gx0) / 5)):                                          # grille cloth
        x = gx0 + 3 + i * 5
        d.line((x * s, (gy0 + 2) * s, x * s, (gy1 - 2) * s), fill=(84, 70, 46), width=s)
    d.text(((gx0 + gx1) / 2 * s, (gy0 + gy1) / 2 * s), "NAM", font=font(BOLD, 24 * s), fill=shade(col, 1.05), anchor="mm")
    py0, py1 = gy1 + 8, y1 - 9
    d.rounded_rectangle((gx0 * s, py0 * s, gx1 * s, py1 * s), radius=4 * s, fill=shade(col, 0.85), outline=shade(col, 0.5), width=s)
    for i in range(5):
        knob(d, gx0 + 16 + i * 23, (py0 + py1) / 2, 8, s)
    d.ellipse(((gx1 - 17) * s, ((py0 + py1) / 2 - 4) * s, (gx1 - 9) * s, ((py0 + py1) / 2 + 4) * s), fill=(250, 70, 50))


def cab(d, s, col):
    """a cabinet: two speakers behind a grille"""
    x0, y0, x1, y1 = 7, 18, TW - 7, GEAR_H - 14
    d.rounded_rectangle((x0 * s, (y0 + 3) * s, x1 * s, (y1 + 3) * s), radius=10 * s, fill=(0, 0, 0))
    d.rounded_rectangle((x0 * s, y0 * s, x1 * s, y1 * s), radius=10 * s, fill=(30, 30, 33), outline=(86, 86, 92), width=2 * s)
    d.rounded_rectangle(((x0 + 8) * s, (y0 + 8) * s, (x1 - 8) * s, (y1 - 8) * s), radius=5 * s, fill=shade(col, 0.34), outline=shade(col, 0.7), width=s)
    for cx in (TW / 2 - 37, TW / 2 + 37):
        cy, r = (y0 + y1) / 2, 33
        d.ellipse(((cx - r) * s, (cy - r) * s, (cx + r) * s, (cy + r) * s), fill=(22, 22, 24), outline=shade(col, 0.8), width=2 * s)
        d.ellipse(((cx - r + 8) * s, (cy - r + 8) * s, (cx + r - 8) * s, (cy + r - 8) * s), outline=(60, 60, 64), width=s)
        d.ellipse(((cx - 11) * s, (cy - 11) * s, (cx + 11) * s, (cy + 11) * s), fill=(44, 44, 48), outline=(90, 90, 96), width=s)


def empty(d, s):
    x0, y0, x1, y1 = 34, 22, TW - 34, GEAR_H - 22
    for i in range(0, 16, 2):                                                      # a dashed outline: a free place on the board
        for y in (y0, y1):
            d.line(((x0 + (x1 - x0) * i / 15) * s, y * s, (x0 + (x1 - x0) * (i + 1) / 15) * s, y * s), fill=(84, 84, 90), width=2 * s)
        for x in (x0, x1):
            d.line((x * s, (y0 + (y1 - y0) * i / 15) * s, x * s, (y0 + (y1 - y0) * (i + 1) / 15) * s), fill=(84, 84, 90), width=2 * s)
    cx, cy = (x0 + x1) / 2, (y0 + y1) / 2
    d.line(((cx - 12) * s, cy * s, (cx + 12) * s, cy * s), fill=(110, 110, 116), width=3 * s)
    d.line((cx * s, (cy - 12) * s, cx * s, (cy + 12) * s), fill=(110, 110, 116), width=3 * s)


def tile(t):
    s = 3
    im = Image.new("RGBA", (TW * s, GEAR_H * s), (0, 0, 0, 0))
    d = ImageDraw.Draw(im)
    col = COLOUR[t]
    if t == 0:
        empty(d, s)
    elif t == 2:
        amp(d, s, col)
    elif t == 3:
        cab(d, s, col)
    else:
        glyph, name, n = {1: (g_gate, "GATE", 2), 4: (g_eq, "EQ", 4), 5: (g_drive, "DRIVE", 3), 6: (g_delay, "DELAY", 3),
                          7: (g_reverb, "REVERB", 3), 8: (g_capture, "NAM PEDAL", 2)}[t]
        pedal(d, s, col, name, glyph, n)
    return im.resize((TW, GEAR_H), Image.LANCZOS)


def strip(frames):
    w, h = frames[0].size
    im = Image.new("RGBA", (w, h * len(frames)), (0, 0, 0, 0))
    for i, f in enumerate(frames):
        im.paste(f, (0, i * h))
    return im


def lamp(on):
    s, n = 4, 26
    im = Image.new("RGBA", (n * s, n * s), (0, 0, 0, 0))
    d = ImageDraw.Draw(im)
    c = n * s / 2
    if on:
        g = Image.new("RGBA", im.size, (0, 0, 0, 0))
        ImageDraw.Draw(g).ellipse((c - 11 * s, c - 11 * s, c + 11 * s, c + 11 * s), fill=(255, 70, 40, 190))
        im = Image.alpha_composite(im, g.filter(ImageFilter.GaussianBlur(3 * s)))
        d = ImageDraw.Draw(im)
    d.ellipse((c - 7 * s, c - 7 * s, c + 7 * s, c + 7 * s), fill=(255, 84, 52) if on else (64, 26, 22), outline=(0, 0, 0), width=s)
    d.ellipse((c - 4 * s, c - 5 * s, c, c - s), fill=(255, 190, 170) if on else (96, 50, 44))
    return im.resize((n, n), Image.LANCZOS)


def select_frame(on):
    """the selected place's outline (a Button image: drawn at its own size, so it is exactly a place)"""
    s = 2
    # not fully transparent: if the MPC only takes touches on a button's visible pixels, an invisible one would be dead
    im = Image.new("RGBA", (TW * s, BOARD_H * s), (0, 0, 0, 6))
    if on:
        d = ImageDraw.Draw(im)
        d.rounded_rectangle((s, s, TW * s - s - 1, BOARD_H * s - s - 1), radius=10 * s, outline=AMBER, width=3 * s)
        d.polygon(((TW / 2 - 12) * s, BOARD_H * s - 1, (TW / 2 + 12) * s, BOARD_H * s - 1, TW / 2 * s, (BOARD_H - 11) * s), fill=AMBER)
    return im.resize((TW, BOARD_H), Image.LANCZOS)


PLATE_W, PLATE_H = 1010, 34


def plate(title, col, lit=True):
    s = 2
    im = Image.new("RGBA", (PLATE_W * s, PLATE_H * s), (0, 0, 0, 0))
    d = ImageDraw.Draw(im)
    d.rounded_rectangle((0, 0, PLATE_W * s - 1, PLATE_H * s - 1), radius=7 * s, fill=shade(col, 0.30), outline=shade(col, 0.9), width=s)
    d.rounded_rectangle((0, 0, 14 * s, PLATE_H * s - 1), radius=6 * s, fill=col)
    d.text((30 * s, PLATE_H * s / 2), title, font=font(BOLD, 17 * s), fill=shade(col, 1.25) if lit else DIM, anchor="lm")
    return im.resize((PLATE_W, PLATE_H), Image.LANCZOS)


# --------------------------------------------------------------------------------------------- backgrounds
def board():
    im = Image.new("RGB", (W, H), INK)
    px = im.load()
    for y in range(H):                                   # a faint vertical falloff, like a lit stage floor
        k = 1.0 + 0.5 * math.sin(math.pi * y / H)
        for x in range(W):
            px[x, y] = shade(INK, k)
    n = Image.effect_noise((W, H), 9).convert("RGB")
    return Image.blend(im, n, 0.035)


EDIT_Y = 284                                             # under the board: the selected block's settings (rows 2 and 3)


def bg_rig():
    s = 2
    im = board().resize((W * s, H * s), Image.BICUBIC)
    d = ImageDraw.Draw(im)
    f_num, f_small = font(BOLD, 16 * s), font(BOLD, 12 * s)
    cy = GEAR_Y + GEAR_H / 2
    d.line((0, cy * s, W * s, cy * s), fill=(0, 0, 0), width=9 * s)                # the cable through the whole board
    d.line((0, cy * s, W * s, cy * s), fill=(70, 70, 76), width=5 * s)
    for i in range(COLS):
        x = place_x(i)
        d.rounded_rectangle((x * s, BOARD_Y * s, (x + TW) * s, (BOARD_Y + BOARD_H) * s), radius=10 * s, fill=PANEL, outline=EDGE, width=s)
        if i < BLOCKS:
            d.ellipse(((x + 9) * s, (BOARD_Y + 7) * s, (x + 31) * s, (BOARD_Y + 29) * s), fill=(48, 48, 54))
            d.text(((x + 20) * s, (BOARD_Y + 18) * s), "%d" % (i + 1), font=f_num, fill=BONE, anchor="mm")
    # the last place: the input / output box
    x = place_x(BLOCKS)
    bx0, by0, bx1, by1 = x + 22, GEAR_Y + 22, x + TW - 22, GEAR_Y + GEAR_H - 22
    d.rounded_rectangle((bx0 * s, by0 * s, bx1 * s, by1 * s), radius=8 * s, fill=(34, 34, 38), outline=AMBER, width=2 * s)
    for k, name in enumerate(("IN", "OUT")):
        jx, jy = (bx0 + bx1) / 2 + (k * 2 - 1) * 30, (by0 + by1) / 2 - 8
        d.ellipse(((jx - 15) * s, (jy - 15) * s, (jx + 15) * s, (jy + 15) * s), fill=(150, 150, 156), outline=(20, 20, 22), width=2 * s)
        d.ellipse(((jx - 7) * s, (jy - 7) * s, (jx + 7) * s, (jy + 7) * s), fill=(10, 10, 12))
        d.text((jx * s, (jy + 30) * s), name, font=f_small, fill=AMBER, anchor="mm")
    d.text(((x + TW / 2) * s, (TYPE_Y + 13) * s), "In / Out", font=font(SANS, 17 * s), fill=BONE, anchor="mm")
    # the settings area
    d.rounded_rectangle((8 * s, EDIT_Y * s, (W - 8) * s, (H - 4) * s), radius=12 * s, fill=PANEL, outline=EDGE, width=s)
    d.text(((W - 22) * s, (EDIT_Y + 26) * s), "TOUCH A BLOCK TO EDIT IT", font=f_small, fill=DIM, anchor="rm")
    return im.resize((W, H), Image.LANCZOS)


BAND_Y = (2, 306)                                        # the two blocks of a BLK page (its rows: 0-1 and 2-3)
BAND_H = 303


def bg_blocks(first):
    s = 2
    im = board().resize((W * s, H * s), Image.BICUBIC)
    d = ImageDraw.Draw(im)
    for i in range(2):
        y = BAND_Y[i]
        d.rounded_rectangle((8 * s, y * s, (W - 8) * s, (y + BAND_H) * s), radius=12 * s, fill=PANEL, outline=EDGE, width=s)
        d.text((26 * s, (y + 21) * s), "BLOCK %d" % (first + i), font=font(BOLD, 19 * s), fill=BONE, anchor="lm")
    return im.resize((W, H), Image.LANCZOS)


# ------------------------------------------------------------------------------------------------ the skin
def entry(name, wtype, data, bounds, invalidating=None, focus="No"):
    b = {"version": 2 if invalidating else 1, "acceptsHWFocus": focus,
         "showWhenDataModelInvalid": "Hide" if invalidating else "Show", "whenVisible": "Always",
         "boundsType": "Absolute", "bounds": "%d %d %d %d" % bounds}
    if invalidating:
        b["additionalInvalidatingHandles"] = invalidating
    return obj({"version": 2, "componentData": {"version": 1, "name": name, "type": wtype, "data": data},
                "handle remapping": {"version": 1, "map": []}, "bounds": b})


def image(name, fname, bounds, invalidating=None):
    return entry(name, "Image", {"version": 2, "imageType": "Regular", "colour": "0", "image": fname}, bounds, invalidating)


def film(name, fname, frames, handle, bounds, invalidating=None):
    return entry(name, "Image", {"version": 2, "imageType": "FilmStrip", "colour": "0", "image": fname,
                                 "numFrames": frames, "handleName": handle}, bounds, invalidating)


def value_label(name, handle, bounds, colour, height):
    return entry(name, "Label",
                 {"version": 1,
                  "textStyle": {"version": 1, "font": {"version": 1, "name": "Titillium Web", "style": "Regular", "height": float(height)},
                                "colour": colour, "justification": "horizontallyCentred verticallyCentred", "case": "Original"},
                  "type": "Value", "handleName": handle}, bounds)


def selected(i):
    return "IndexedEnabling/%d/%d/%s" % (i, COLS, SEL)


def only_when(e, handle):
    """add a visibility condition to an entry the generator wrote"""
    b = e.get("bounds")
    b.set("version", 2)
    b.set("showWhenDataModelInvalid", "Hide")
    b.set("additionalInvalidatingHandles", (b.get("additionalInvalidatingHandles") if b.has("additionalInvalidatingHandles") else []) + [handle])


def widen(entries):
    """the generator's "shown for choice i of Type" -> the ranges of RULES (an entry a range)"""
    out = []
    for e in entries:
        b = e.get("bounds")
        hs = b.get("additionalInvalidatingHandles") if b.has("additionalInvalidatingHandles") else []
        m = next((re.fullmatch(r"IndexedEnabling/(\d+)/(\d+)/(Parameter \d+)", h) for h in hs
                  if re.fullmatch(r"IndexedEnabling/(\d+)/(\d+)/(Parameter \d+)", h)), None)
        if m is None:
            out.append(e)
            continue
        for lo, hi in RULES[int(m.group(1))]:
            c = loads(dumps(e))
            c.get("bounds").set("additionalInvalidatingHandles",
                                ["RangedEnabling/%d/%d/%s/%s" % (lo, hi, m.group(2), m.group(3)) if h == m.group(0) else h for h in hs])
            out.append(c)
    return out


def build_skin():
    params = os.path.join(PROJ, "build", "params.json")
    if not os.path.isfile(params):
        sys.exit("make_skin: build the plugin once first (build/params.json is missing)")
    names = [p["name"] for p in json.load(open(params))["params"]]
    for b in range(BLOCKS):          # the handles written below are parameter numbers: make sure they still mean what we think
        if names[par(b, P_TYPE)] != "Block %d" % (b + 1) or names[par(b, P_FILE)] != "%d File" % (b + 1) or names[par(b, P_ON)] != "%d On" % (b + 1):
            sys.exit("make_skin: the parameter layout changed; update N_GLOBAL / PER_BLOCK / P_* in this script")
    tmp = os.path.join(PROJ, "build", "skin-gen")
    shutil.rmtree(tmp, ignore_errors=True)
    subprocess.check_call([sys.executable, os.path.join(SDK, "tools", "gen_ui.py"), "--params", params, "--ui",
                           os.path.join(PROJ, "ui.json"), "--out", tmp, "--kit-cache", os.path.join(PROJ, "build")])
    shutil.rmtree(SKINS, ignore_errors=True)
    shutil.copytree(tmp, SKINS)

    print("make_skin: drawing")
    strip([tile(t) for t in range(len(TYPES))]).save(os.path.join(SKINS, "tile.png"), optimize=True)
    strip([lamp(False), lamp(True)]).save(os.path.join(SKINS, "lamp.png"), optimize=True)
    strip([plate(TITLE[t], COLOUR[t], t > 0) for t in range(len(TYPES))]).save(os.path.join(SKINS, "plate.png"), optimize=True)
    plate("INPUT  /  OUTPUT", AMBER).save(os.path.join(SKINS, "plate_io.png"), optimize=True)
    select_frame(True).save(os.path.join(SKINS, "sel_on.png"), optimize=True)
    select_frame(False).save(os.path.join(SKINS, "sel_off.png"), optimize=True)
    bg_rig().save(os.path.join(SKINS, "bg_rig.jpg"), quality=90, subsampling=0)
    for first in (1, 3, 5):
        bg_blocks(first).save(os.path.join(SKINS, "bg_blk%d.jpg" % first), quality=90, subsampling=0)

    path = os.path.join(SKINS, "TUI.json")
    txt = open(path, encoding="utf-8").read()

    tui = loads(txt)
    tabs = tui.get("pageData").get("tabs")
    by_name = {t.get("tabName"): t for t in tabs}

    def entries(tab):
        return by_name[tab].get("componentDefinition").get("componentsData")

    for t in tabs:                                       # one choice -> ranges of choices
        e = t.get("componentDefinition").get("componentsData")
        e[:] = widen(e)
    # RIG: what the generator put there is the input / output box's settings
    rig = entries("RIG")
    for e in rig:
        only_when(e, selected(BLOCKS))
    art, plates, buttons, controls = [image("board", "bg_rig.jpg", (0, 0, W, H))], [], [], []
    for b in range(BLOCKS):
        x = place_x(b)
        t = "Parameter %d" % par(b, P_TYPE)
        art += [film("gear %d" % (b + 1), "tile.png", len(TYPES), t, (x, GEAR_Y, TW, GEAR_H)),
                film("lamp %d" % (b + 1), "lamp.png", 2, "Parameter %d" % par(b, P_ON), (x + TW - 33, BOARD_Y + 5, 26, 26)),
                value_label("type %d" % (b + 1), t, (x, TYPE_Y, TW, 26), "ffece8de", 23),
                value_label("file %d" % (b + 1), "Parameter %d" % par(b, P_FILE), (x + 2, FILE_Y, TW - 4, 24), "ffe0a020", 19)]
        plates.append(film("plate %d" % (b + 1), "plate.png", len(TYPES), t, (20, EDIT_Y + 9, PLATE_W, PLATE_H), [selected(b)]))
        for e in entries("E%d" % (b + 1)):               # this block's controls, from its helper page
            only_when(e, selected(b))
            controls.append(e)
    plates.append(image("plate io", "plate_io.png", (20, EDIT_Y + 9, PLATE_W, PLATE_H), [selected(BLOCKS)]))
    for i in range(COLS):                                # touch a place to select it
        buttons.append(entry("select %d" % (i + 1), "Button",
                             {"version": 2, "onImage": "sel_on.png", "offImage": "sel_off.png", "buttonId": i,
                              "numButtonsInGroup": COLS, "handleName": SEL, "gestureBehaviour": "Instant"},
                             (place_x(i), BOARD_Y, TW, BOARD_H), focus="Yes"))
    rig[:] = art + buttons + plates + rig + controls
    # the BLK pages
    for first in (1, 3, 5):
        e = entries("BLK %d-%d" % (first, first + 1))
        e[:] = [image("panel", "bg_blk%d.jpg" % first, (0, 0, W, H))] + \
               [film("plate %d" % (first + i), "plate.png", len(TYPES), "Parameter %d" % par(first - 1 + i, P_TYPE),
                     (150, BAND_Y[i] + 4, PLATE_W, PLATE_H)) for i in range(2)] + e
    # the helper pages go
    keep = [t for t in tabs if not re.fullmatch(r"E\d", t.get("tabName"))]
    tabs[:] = keep
    open(path, "w", encoding="utf-8").write(dumps(tui) + "\n")
    qpath = os.path.join(SKINS, "Q-Links.json")
    q = loads(open(qpath, encoding="utf-8").read())
    qmap = q.get("Screen Mode Q-Links").get("map")
    qmap[:] = [m for m in qmap if m.get("Tab") <= len(keep)]
    open(qpath, "w", encoding="utf-8").write(dumps(q) + "\n")
    print("make_skin: skins/ written (%d files, %d tabs, %d entries on RIG)" % (len(os.listdir(SKINS)), len(keep), len(rig)))


# --------------------------------------------------------------------------------------------- the preview
SAMPLE = {"types": (1, 8, 2, 3, 6, 7), "files": ("", "TS Capture", "Example A2", "Gonzi 412 Dark", "Tape", "Plate"), "on": (1, 1, 1, 1, 0, 1),
          "values": {"Thresh": "-60 dB", "Release": "151 ms", "Gain": "+0.0 dB", "Size": "Lite", "Lo Cut": "Off", "Hi Cut": "Off",
                     "Mix": "100 %", "Drive": "40 %", "Tone": "3.2 kHz", "Tight": "183 Hz", "Time": "343 ms", "Feedback": "35 %",
                     "Decay": "1.8 s", "Damp": "50 %", "PreDelay": "12 ms", "Bass": "+0.0 dB", "Mid": "+0.0 dB", "Mid Freq": "775 Hz",
                     "Treble": "+0.0 dB"}}


def preview(out, selections):
    tui = json.load(open(os.path.join(SKINS, "TUI.json"), encoding="utf-8"))["pageData"]
    plist = json.load(open(os.path.join(PROJ, "build", "params.json")))["params"]
    defs = {d["key"]: d["value"] for d in tui["componentDefinitions"]["localComponentDefinitions"]}
    sel = [0]

    def state(h):                                        # (normalised value, name, value text) behind a handle, for the sample rig
        if h.endswith(SEL):
            return sel[0] / (COLS - 1), "", ""
        m = re.search(r"Parameter (\d+)$", h)
        if not m:
            return 0.0, "", ""
        n = int(m.group(1))
        p = plist[n]
        if n < N_GLOBAL or n >= N_GLOBAL + BLOCKS * PER_BLOCK:
            return p["default"], p["name"], p["display"] + (" " + p["label"] if p["label"] else "")
        b, w = divmod(n - N_GLOBAL, PER_BLOCK)
        t = SAMPLE["types"][b]
        if w == P_TYPE:
            return t / (len(TYPES) - 1), p["name"], TYPES[t]
        if w == P_FILE:
            return 0.0, "%d %s" % (b + 1, FILE_NAME[t]), SAMPLE["files"][b]
        if w == P_ON:
            return float(SAMPLE["on"][b]), p["name"], "On" if SAMPLE["on"][b] else "Off"
        if w == P_LEVEL:
            return 0.5, p["name"], "0.0 dB"
        k = KNOBS[t][w - P_K1]
        return 0.5, "%d %s" % (b + 1, k or "-"), SAMPLE["values"].get(k, "-")

    def visible(bounds):
        for h in bounds.get("additionalInvalidatingHandles", []):
            m = re.match(r"(Indexed|Ranged)Enabling/((?:\d+/)+)(.+)$", h)
            a = [int(v) for v in m.group(2).strip("/").split("/")]
            step = round(state(m.group(3))[0] * (a[-1] - 1))
            if not (a[0] <= step <= (a[1] if m.group(1) == "Ranged" else a[0])):
                return False
        return True

    def colour(c):
        v = int(c, 16)
        return ((v >> 16) & 255, (v >> 8) & 255, v & 255, (v >> 24) & 255)

    def draw(im, d, cd, bx, by, h):
        t, data = cd["type"], cd["data"]
        if data.get("handleName", "Data") != "Data":
            h = data["handleName"]
        norm, name, text = state(h or "")
        if t == "Image":
            src = Image.open(os.path.join(SKINS, data["image"])).convert("RGBA")
            if data.get("imageType") == "FilmStrip":
                fh = src.height // data["numFrames"]
                f = round(norm * (data["numFrames"] - 1))
                src = src.crop((0, f * fh, src.width, f * fh + fh))
            im.alpha_composite(src.resize((cd["_w"], cd["_h"])) if src.size != (cd["_w"], cd["_h"]) else src, (bx, by))
        elif t == "Knob":
            st = Image.open(os.path.join(SKINS, data["filmStrip"])).convert("RGBA")
            fh = st.height // data["numFrames"]
            f = round(norm * (data["numFrames"] - 1))
            im.alpha_composite(st.crop((0, f * fh, st.width, f * fh + fh)), (bx, by))
        elif t == "Button":
            n = max(2, data["numButtonsInGroup"])
            on = round(norm * (n - 1)) == data["buttonId"]
            b = Image.open(os.path.join(SKINS, data["onImage"] if on else data["offImage"])).convert("RGBA")
            im.alpha_composite(b, (bx + (cd["_w"] - b.width) // 2, by + (cd["_h"] - b.height) // 2))
        elif t == "Label":
            ts = data["textStyle"]
            s = data.get("text") or (name if data.get("type") == "Name" else text)
            f = font(SANS, ts["font"]["height"] * 0.74)
            j = ts["justification"]
            if "entred" in j:
                d.text((bx + cd["_w"] / 2, by + cd["_h"] / 2), s, font=f, fill=colour(ts["colour"]), anchor="mm")
            elif j == "right":
                d.text((bx + cd["_w"], by + cd["_h"] / 2), s, font=f, fill=colour(ts["colour"]), anchor="rm")
            else:
                d.text((bx, by + cd["_h"] / 2), s, font=f, fill=colour(ts["colour"]), anchor="lm")
        elif t == "Slider":                              # the MPC's own name + value bar: a stand-in
            d.rounded_rectangle((bx, by, bx + cd["_w"], by + cd["_h"]), radius=4, fill=(44, 44, 48, 255))
            d.text((bx + 10, by + 22), name.upper(), font=font(SANS, 17), fill=(150, 150, 150, 255), anchor="lm")
            d.text((bx + 10, by + 56), text, font=font(SANS, 22), fill=(255, 255, 255, 255), anchor="lm")

    def shot(tab):
        im = Image.new("RGBA", (W, H), (20, 20, 20, 255))
        d = ImageDraw.Draw(im)
        for e in tab["componentDefinition"]["componentsData"]:
            if not visible(e["bounds"]):
                continue
            cd = dict(e["componentData"])
            x, y, w, h = (int(v) for v in e["bounds"]["bounds"].split())
            remap = {m["key"]: m["value"] for m in e["handle remapping"]["map"]}
            if cd["type"] in defs:
                for sub in defs[cd["type"]]["componentsData"]:
                    if sub["bounds"]["whenVisible"] != "Always":
                        continue
                    sd = dict(sub["componentData"])
                    sx, sy, sd["_w"], sd["_h"] = (int(v) for v in sub["bounds"]["bounds"].split())
                    draw(im, d, sd, x + sx, y + sy, remap.get("Data"))
            else:
                cd["_w"], cd["_h"] = w, h
                draw(im, d, cd, x, y, None)
        return im.convert("RGB")

    shots = []
    for tab in tui["tabs"]:
        for k in (selections if tab["tabName"] == "RIG" else [0]):
            sel[0] = k
            shots.append(shot(tab))
    sheet = Image.new("RGB", (W, (H + 12) * len(shots) - 12), (0, 0, 0))
    for i, s in enumerate(shots):
        sheet.paste(s, (0, i * (H + 12)))
    sheet.save(out)
    print("make_skin: preview ->", out)


if __name__ == "__main__":
    ap = argparse.ArgumentParser()
    ap.add_argument("--preview", help="also draw what the MPC should show, into this PNG")
    ap.add_argument("--select", type=int, nargs="*", default=[2, 4, 6], help="which places the preview selects on RIG (0-6)")
    ap.add_argument("--no-build", action="store_true", help="only the preview, from skins/ as it is")
    a = ap.parse_args()
    if not a.no_build:
        build_skin()
    if a.preview:
        preview(a.preview, a.select)
