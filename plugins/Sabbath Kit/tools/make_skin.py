#!/usr/bin/env python3
# SPDX-License-Identifier: MIT
# Copyright (C) 2026 Gonzi Audio
"""Sabbath Kit's skin: a black amp panel lit by orange valves, on the MPC's screen.

  python3 "plugins/Sabbath Kit/tools/make_skin.py"       (from the SDK root, after a build: it needs build/params.json)

Same recipe as Grimoire's make_skin.py (same author, reused here under MIT): it runs the SDK's generator on ui.json
(layout and Q-Link map unchanged), then replaces the kit's graphics with its own, drawn with Pillow (nothing is
downloaded, no outside artwork):
  bg_<page>.jpg     a page a tab: black tolex, a steel frame, a plate a control, a caption a row, a faint emblem;
                    the GROOVE page also shows which pad plays which drum
  knob_*.png        a black amp knob in a steel ring, a white pointer, an orange valve-glow arc
  fader_h_uni.png   a slot filling with orange, a steel cap
  toggle_*, seg*    orange when on
and writes the result to skins/ (the build copies skins/ verbatim). --preview OUT.png also draws what the MPC should
show (the labels in a stand-in font: the MPC has its own).
"""
import argparse
import json
import math
import os
import random
import shutil
import subprocess
import sys

from PIL import Image, ImageChops, ImageDraw, ImageFilter, ImageFont

HERE = os.path.dirname(os.path.abspath(__file__))
PROJ = os.path.normpath(os.path.join(HERE, ".."))
SDK = os.path.normpath(os.path.join(PROJ, "..", ".."))
OUT_DIR = os.path.join(PROJ, "build", "Gonzi Audio - VST - Sabbath Kit")
SKINS = os.path.join(PROJ, "skins")

# the palette (names kept from Grimoire's script: GOLD* is the line work, AMBER the value colour, WAX the "on" colour)
INK = (6, 5, 8)
LEATHER = (21, 18, 25)
GOLD = (196, 184, 214)        # pale steel-violet: captions, studs
GOLD_DIM = (116, 100, 140)
GOLD_DARK = (62, 52, 78)
BONE = (240, 236, 228)
AMBER = (242, 122, 26)        # valve orange
AMBER_HI = (255, 200, 120)
WAX = (176, 74, 10)
WAX_HI = (255, 150, 50)
IRON = (40, 38, 46)
VIOLET = (120, 60, 190)

CAPS = "/usr/share/texmf/fonts/opentype/public/lm/lmromancaps10-regular.otf"
SERIF = "/usr/share/fonts/truetype/dejavu/DejaVuSerif.ttf"
SANS = "/usr/share/fonts/truetype/dejavu/DejaVuSans.ttf"
BOLD = "/usr/share/fonts/truetype/dejavu/DejaVuSans-Bold.ttf"


def font(path, size):
    try:
        return ImageFont.truetype(path, size)
    except OSError:
        return ImageFont.truetype(SERIF, size)


# the generator's 4 x 4 grid
XS, YS, SW, SH = (23, 343, 663, 983), (45, 196, 346, 497), 278, 107
W, H = 1280, 628

PAGES = {   # tab -> file, the rows' captions, the emblem
    "GROOVE": ("bg_groove.jpg", ("Groove Player", "Feel, Pads & Master", "", ""), "moon"),
    "KIT": ("bg_kit.jpg", ("Kick", "Snare", "Room & Master", "Bus"), "sun"),
    "TOMS": ("bg_toms.jpg", ("Tune   -   Tom 1 / Tom 2 / Tom 3", "Decay", "Level", "Response"), "star"),
    "CYMBALS": ("bg_cymbals.jpg", ("Tune   -   Hat / Crash / Ride", "Decay", "Level", "Response"), "eye"),
}
PADS = (("Crash 2002", "Pedal Hat", "Kick", "Snare"),      # top row of pads first (Pad Map MPC, the MPC's C major pads)
        ("Snare Edge", "Ride", "Ride Bell", "Crash A20"),
        ("Tom 3", "Tom 2", "Tom 1", "Kick"),
        ("Kick", "Snare", "Hat", "Open Hat"))
NUMERAL = ("I", "II", "III", "IV")


# ---------------------------------------------------------------------------------------------------- backgrounds
def leather(seed):
    """the page: dark leather with grain, worn lighter towards the middle, darker at the edges"""
    rnd = random.Random(seed)
    base = Image.new("RGB", (W, H), LEATHER)
    # grain: two scales of noise
    n1 = Image.effect_noise((W, H), 38).filter(ImageFilter.GaussianBlur(0.7))
    n2 = Image.effect_noise((W // 6, H // 6), 60).resize((W, H), Image.BICUBIC).filter(ImageFilter.GaussianBlur(6))
    grain = ImageChops.add(n1, n2, 2.0, 0)
    base = ImageChops.multiply(base, Image.merge("RGB", [grain.point(lambda v: 150 + v * 105 // 255)] * 3))
    # a few scuffs
    sc = Image.new("L", (W, H), 0)
    d = ImageDraw.Draw(sc)
    for _ in range(26):
        x, y = rnd.randrange(W), rnd.randrange(H)
        a, ln = rnd.uniform(0, math.pi), rnd.randrange(30, 160)
        d.line((x, y, x + ln * math.cos(a), y + ln * math.sin(a)), fill=rnd.randrange(10, 26), width=1)
    base = ImageChops.add(base, Image.merge("RGB", [sc.filter(ImageFilter.GaussianBlur(0.8))] * 3))
    # the vignette
    v = Image.new("L", (W, H), 0)
    ImageDraw.Draw(v).ellipse((-W * 0.18, -H * 0.42, W * 1.18, H * 1.42), fill=255)
    v = v.filter(ImageFilter.GaussianBlur(110)).point(lambda p: 95 + p * 160 // 255)
    return ImageChops.multiply(base, Image.merge("RGB", [v] * 3))


def sigil(kind, size):
    """a faint emblem behind the controls (white on black, used as a mask)"""
    s = 3
    n = size * s
    im = Image.new("L", (n, n), 0)
    d = ImageDraw.Draw(im)
    c = n / 2
    w = 2 * s

    def circle(r, width=w):
        d.ellipse((c - r, c - r, c + r, c + r), outline=255, width=width)

    R = n * 0.47
    circle(R)
    circle(R * 0.93, s)
    circle(R * 0.74, s)
    for k in range(48):                         # the ring's marks, every fourth longer
        a = k * math.tau / 48
        r0, r1 = R * (0.76 if k % 4 else 0.74), R * (0.80 if k % 4 else 0.91)
        d.line((c + r0 * math.cos(a), c + r0 * math.sin(a), c + r1 * math.cos(a), c + r1 * math.sin(a)), fill=255, width=s)
    for k in range(12):                         # small lozenges between them
        a = (k + 0.5) * math.tau / 12
        x, y, q = c + R * 0.855 * math.cos(a), c + R * 0.855 * math.sin(a), 5 * s
        d.polygon(((x, y - q), (x + q, y), (x, y + q), (x - q, y)), outline=255)
    r = R * 0.70
    if kind == "eye":                           # the eye that watches the music (GR!MM's), in a triangle
        pts = [(c + r * math.cos(a), c + r * math.sin(a)) for a in (-math.pi / 2, math.pi / 6, 5 * math.pi / 6)]
        d.line(pts + [pts[0]], fill=255, width=w, joint="curve")
        ew, eh = r * 0.42, r * 0.20
        cy = c + r * 0.12
        up = [(c - ew + 2 * ew * t / 40, cy - eh * math.sin(math.pi * t / 40)) for t in range(41)]
        lo = [(c - ew + 2 * ew * t / 40, cy + eh * math.sin(math.pi * t / 40)) for t in range(41)]
        d.line(up, fill=255, width=w)
        d.line(lo, fill=255, width=w)
        d.ellipse((c - eh * 0.8, cy - eh * 0.8, c + eh * 0.8, cy + eh * 0.8), outline=255, width=w)
        d.ellipse((c - eh * 0.3, cy - eh * 0.3, c + eh * 0.3, cy + eh * 0.3), fill=255)
    elif kind == "moon":                        # the moon's phases round a crescent
        d.ellipse((c - r * 0.42, c - r * 0.42, c + r * 0.42, c + r * 0.42), fill=255)
        d.ellipse((c - r * 0.42 + r * 0.22, c - r * 0.48, c + r * 0.42 + r * 0.22, c + r * 0.36), fill=0)
        for k in range(8):
            a = k * math.tau / 8 - math.pi / 2
            x, y, q = c + r * 0.78 * math.cos(a), c + r * 0.78 * math.sin(a), r * 0.085
            d.ellipse((x - q, y - q, x + q, y + q), outline=255, width=s)
            if k in (3, 4, 5):
                d.ellipse((x - q, y - q, x + q, y + q), fill=255)
    elif kind == "star":                        # a seven-pointed star, drawn in one line
        pts = [(c + r * math.cos(-math.pi / 2 + k * 3 * math.tau / 7), c + r * math.sin(-math.pi / 2 + k * 3 * math.tau / 7))
               for k in range(8)]
        d.line(pts, fill=255, width=w, joint="curve")
        circle(r * 0.22)
    else:                                       # a sun: sixteen rays
        circle(r * 0.34)
        circle(r * 0.26, s)
        for k in range(16):
            a = k * math.tau / 16
            r0, r1 = r * 0.42, r * (0.98 if k % 2 == 0 else 0.70)
            b = 0.07
            d.polygon(((c + r0 * math.cos(a - b), c + r0 * math.sin(a - b)), (c + r1 * math.cos(a), c + r1 * math.sin(a)),
                       (c + r0 * math.cos(a + b), c + r0 * math.sin(a + b))), outline=255)
    return im.resize((size, size), Image.LANCZOS)


def lozenge(d, x, y, q, fill):
    d.polygon(((x, y - q), (x + q, y), (x, y + q), (x - q, y)), fill=fill)


def cells(tab):
    """which grid cells hold a control on this tab (ui.json, reading order)"""
    ui = json.load(open(os.path.join(PROJ, "ui.json"), encoding="utf-8"))
    page = next(p for p in ui["pages"] if p["name"] == tab)
    return {(i // 4, i % 4) for i, c in enumerate(page["controls"]) if isinstance(c, dict) and "param" in c}


def background(tab, seed):
    fname, rows, kind = PAGES[tab]
    used = cells(tab)
    im = leather(seed).convert("RGBA")
    # a violet haze low on the page, like stage light through smoke
    haze = Image.new("L", (W, H), 0)
    ImageDraw.Draw(haze).ellipse((W * 0.1, H * 0.55, W * 0.9, H * 1.5), fill=70)
    haze = haze.filter(ImageFilter.GaussianBlur(120))
    hz = Image.new("RGBA", (W, H), VIOLET + (0,))
    hz.putalpha(haze)
    im = Image.alpha_composite(im, hz)
    # the sigil, faint, in the middle
    sg = sigil(kind, 560)
    glow = Image.new("RGBA", (W, H), (0, 0, 0, 0))
    glow.paste(Image.new("RGBA", sg.size, VIOLET + (0,)), ((W - 560) // 2, (H - 560) // 2 + 6))
    mask = Image.new("L", (W, H), 0)
    mask.paste(sg.point(lambda v: v * 60 // 255), ((W - 560) // 2, (H - 560) // 2 + 6))
    glow.putalpha(mask)
    im = Image.alpha_composite(im, glow)

    s = 2                                       # the line work at twice the size
    ov = Image.new("RGBA", (W * s, H * s), (0, 0, 0, 0))
    d = ImageDraw.Draw(ov)

    def rect(x0, y0, x1, y1, col, width=1, radius=0):
        d.rounded_rectangle((x0 * s, y0 * s, x1 * s, y1 * s), radius=radius * s, outline=col, width=width * s)

    # the page's gilt frame
    rect(6, 5, W - 7, H - 6, GOLD_DIM + (255,), 2, 4)
    rect(11, 10, W - 12, H - 11, GOLD_DARK + (255,), 1, 2)
    for cx, cy in ((6, 5), (W - 7, 5), (6, H - 6), (W - 7, H - 6)):
        lozenge(d, cx * s, cy * s, 7 * s, INK + (255,))
        lozenge(d, cx * s, cy * s, 5 * s, GOLD + (255,))
    # a cartouche a control
    for r, y in enumerate(YS):
        for c, x in enumerate(XS):
            if (r, c) not in used:
                continue
            d.rounded_rectangle((x * s, y * s, (x + SW) * s, (y + SH) * s), radius=7 * s, fill=(8, 6, 4, 150),
                                outline=GOLD_DARK + (255,), width=s)
            d.rounded_rectangle(((x + 3) * s, (y + 3) * s, (x + SW - 3) * s, (y + SH - 3) * s), radius=5 * s,
                                outline=(255, 226, 160, 16), width=s)
            for cx, cy in ((x, y), (x + SW, y), (x, y + SH), (x + SW, y + SH)):   # corner studs
                lozenge(d, cx * s, cy * s, 4 * s, GOLD_DIM + (255,))
    # a caption a row: numeral, name, a rule to the right edge
    f_cap, f_num = font(CAPS, 21 * s), font(SERIF, 13 * s)
    for i, y in enumerate(YS):
        if not rows[i]:
            continue
        cy = (y - 21) * s
        num = NUMERAL[i]
        nw = d.textlength(num, font=f_num)
        lozenge(d, 32 * s, cy, 4 * s, GOLD + (255,))
        d.text((44 * s, cy), num, font=f_num, fill=GOLD_DIM + (255,), anchor="lm")
        tx = 44 * s + nw + 10 * s
        d.text((tx, cy - s), rows[i], font=f_cap, fill=GOLD + (255,), anchor="lm")
        tw = d.textlength(rows[i], font=f_cap)
        x0, x1 = tx + tw + 14 * s, (W - 30) * s
        d.line((x0, cy, x1, cy), fill=GOLD_DARK + (255,), width=s)
        lozenge(d, x0, cy, 3 * s, GOLD_DIM + (255,))
        lozenge(d, x1, cy, 4 * s, GOLD + (255,))
    if tab == "GROOVE":                         # the pads: which one plays which drum
        f_t, f_p, f_n = font(CAPS, 21 * s), font(BOLD, 17 * s), font(SANS, 11 * s)
        top = YS[2] - 21
        lozenge(d, 32 * s, top * s, 4 * s, GOLD + (255,))
        d.text((44 * s, top * s - s), "The Pads   -   Pad Map MPC  (in another bank rows 1-2 and 3-4 may swap; pad 1 is always the kick)", font=f_t,
               fill=GOLD + (255,), anchor="lm")
        cw, ch, gap = 200, 52, 10
        x0 = (W - (4 * cw + 3 * gap)) // 2
        y0 = top + 26
        for r in range(4):
            for c in range(4):
                x, y = x0 + c * (cw + gap), y0 + r * (ch + gap)
                n = (3 - r) * 4 + c + 1
                hot = PADS[r][c] in ("Kick", "Snare")
                d.rounded_rectangle((x * s, y * s, (x + cw) * s, (y + ch) * s), radius=7 * s,
                                    fill=(52, 26, 6, 235) if hot else (10, 8, 13, 215),
                                    outline=(AMBER if hot else GOLD_DARK) + (255,), width=(2 if hot else 1) * s)
                d.text(((x + 12) * s, (y + 14) * s), "%d" % n, font=f_n, fill=GOLD_DIM + (255,), anchor="lm")
                d.text(((x + cw / 2 + 6) * s, (y + ch / 2) * s), PADS[r][c], font=f_p,
                       fill=(AMBER_HI if hot else BONE) + (255,), anchor="mm")
    im = Image.alpha_composite(im, ov.resize((W, H), Image.LANCZOS))
    return fname, im.convert("RGB")


# -------------------------------------------------------------------------------------------------------- widgets
def radial(size, inner, outer, cx=0.5, cy=0.35):
    """a radial gradient image (RGB), lit from above"""
    im = Image.new("RGB", (size, size))
    px = im.load()
    for y in range(size):
        for x in range(size):
            t = min(1.0, math.hypot(x - size * cx, y - size * cy) / (size * 0.75))
            px[x, y] = tuple(int(inner[k] + (outer[k] - inner[k]) * t) for k in range(3))
    return im


def knob_strip(bipolar, frames=127, size=90):
    s = 4
    n = size * s
    c = n / 2
    a0, sweep = 135.0, 270.0
    # what does not move: the track, the ring, the dial
    static = Image.new("RGBA", (n, n), (0, 0, 0, 0))
    d = ImageDraw.Draw(static)
    ra = 41 * s
    d.arc((c - ra, c - ra, c + ra, c + ra), a0, a0 + sweep, fill=(40, 34, 48, 255), width=5 * s)
    for k in range(11):                         # ticks outside the dial
        a = math.radians(a0 + sweep * k / 10)
        r0, r1 = 33.5 * s, 35.5 * s
        d.line((c + r0 * math.cos(a), c + r0 * math.sin(a), c + r1 * math.cos(a), c + r1 * math.sin(a)),
               fill=GOLD_DIM + (255,), width=s)
    rr = 31 * s
    ring = radial(n, (214, 212, 224), (70, 66, 82)).convert("RGBA")
    m = Image.new("L", (n, n), 0)
    ImageDraw.Draw(m).ellipse((c - rr, c - rr, c + rr, c + rr), fill=255)
    static.paste(ring, (0, 0), m)
    rd = 27.5 * s
    dial = radial(n, (62, 60, 68), (10, 9, 12), cy=0.30).convert("RGBA")
    m = Image.new("L", (n, n), 0)
    ImageDraw.Draw(m).ellipse((c - rd, c - rd, c + rd, c + rd), fill=255)
    static.paste(dial, (0, 0), m)
    d.ellipse((c - rd, c - rd, c + rd, c + rd), outline=(8, 6, 4, 255), width=s)
    d.ellipse((c - 7 * s, c - 7 * s, c + 7 * s, c + 7 * s), outline=(96, 90, 110, 255), width=s)   # a ring in the middle

    strip = Image.new("RGBA", (size, size * frames), (0, 0, 0, 0))
    for f in range(frames):
        v = f / (frames - 1)
        im = static.copy()
        arc = Image.new("RGBA", (n, n), (0, 0, 0, 0))
        da = ImageDraw.Draw(arc)
        ang = a0 + sweep * v
        if bipolar:
            mid = a0 + sweep / 2
            lo, hi = (ang, mid) if ang < mid else (mid, ang)
        else:
            lo, hi = a0, ang
        if hi - lo > 0.5:
            da.arc((c - ra, c - ra, c + ra, c + ra), lo, hi, fill=AMBER + (255,), width=5 * s)
            da.arc((c - ra + 1.5 * s, c - ra + 1.5 * s, c + ra - 1.5 * s, c + ra - 1.5 * s), lo, hi, fill=AMBER_HI + (255,), width=s)
        if bipolar:                             # the centre mark
            a = math.radians(a0 + sweep / 2)
            da.line((c + (ra - 5 * s) * math.cos(a), c + (ra - 5 * s) * math.sin(a), c + ra * math.cos(a), c + ra * math.sin(a)),
                    fill=BONE + (255,), width=2 * s)
        glow = arc.filter(ImageFilter.GaussianBlur(3 * s))
        im = Image.alpha_composite(im, Image.blend(Image.new("RGBA", (n, n), (0, 0, 0, 0)), glow, 0.55))
        im = Image.alpha_composite(im, arc)
        dp = ImageDraw.Draw(im)                 # the pointer: bone, a wax-red gem at its tip
        a = math.radians(ang)
        r0, r1 = 9 * s, 24 * s
        dp.line((c + r0 * math.cos(a), c + r0 * math.sin(a), c + r1 * math.cos(a), c + r1 * math.sin(a)), fill=(8, 6, 4, 255), width=6 * s)
        dp.line((c + r0 * math.cos(a), c + r0 * math.sin(a), c + r1 * math.cos(a), c + r1 * math.sin(a)), fill=BONE + (255,), width=3 * s)
        gx, gy, q = c + r1 * math.cos(a), c + r1 * math.sin(a), 3 * s
        dp.ellipse((gx - q, gy - q, gx + q, gy + q), fill=AMBER + (255,), outline=(8, 6, 4, 255), width=s // 2)
        strip.paste(im.resize((size, size), Image.LANCZOS), (0, f * size))
    return strip


def fader_strip(frames=127, w=248, h=36):
    s = 4
    strip = Image.new("RGBA", (w, h * frames), (0, 0, 0, 0))
    x0, x1, cy = 12 * s, (w - 12) * s, h * s / 2
    for f in range(frames):
        v = f / (frames - 1)
        im = Image.new("RGBA", (w * s, h * s), (0, 0, 0, 0))
        d = ImageDraw.Draw(im)
        d.rounded_rectangle((x0 - 5 * s, cy - 6 * s, x1 + 5 * s, cy + 6 * s), radius=6 * s, fill=(6, 5, 3, 255),
                            outline=GOLD_DARK + (255,), width=s)
        for k in range(11):                     # marks under the groove
            x = x0 + (x1 - x0) * k / 10
            d.line((x, cy + 9 * s, x, cy + (13 if k % 5 == 0 else 11) * s), fill=GOLD_DIM + (255,), width=s)
        x = x0 + (x1 - x0) * v
        if v > 0.004:
            fill = Image.new("RGBA", im.size, (0, 0, 0, 0))
            df = ImageDraw.Draw(fill)
            df.rounded_rectangle((x0 - 2 * s, cy - 3 * s, x, cy + 3 * s), radius=3 * s, fill=AMBER + (255,))
            df.line((x0, cy - 1.5 * s, x, cy - 1.5 * s), fill=AMBER_HI + (255,), width=s)
            im = Image.alpha_composite(im, Image.blend(Image.new("RGBA", im.size, (0, 0, 0, 0)), fill.filter(ImageFilter.GaussianBlur(3 * s)), 0.6))
            im = Image.alpha_composite(im, fill)
            d = ImageDraw.Draw(im)
        q, r = 9 * s, 14 * s                    # the handle: a brass lozenge, a bone line
        d.polygon(((x, cy - r - s), (x + q + s, cy), (x, cy + r + s), (x - q - s, cy)), fill=(8, 6, 4, 255))
        d.polygon(((x, cy - r), (x + q, cy), (x, cy + r), (x - q, cy)), fill=(120, 116, 132, 255))
        d.polygon(((x, cy - r), (x + q, cy), (x, cy), (x - q, cy)), fill=(214, 212, 224, 255))
        d.line((x, cy - r + 4 * s, x, cy + r - 4 * s), fill=BONE + (255,), width=s)
        strip.paste(im.resize((w, h), Image.LANCZOS), (0, f * h))
    return strip


def seg(w, h, on):
    s = 4
    im = Image.new("RGBA", (w * s, h * s), (0, 0, 0, 0))
    d = ImageDraw.Draw(im)
    box = (s, s, (w - 1) * s, (h - 1) * s)
    if on:
        d.rounded_rectangle(box, radius=6 * s, fill=WAX + (255,), outline=GOLD + (255,), width=2 * s)
        d.rounded_rectangle((4 * s, 4 * s, (w - 4) * s, h * s // 2), radius=4 * s, fill=WAX_HI + (70,))
    else:
        d.rounded_rectangle(box, radius=6 * s, fill=(14, 11, 8, 235), outline=GOLD_DARK + (255,), width=s)
    return im.resize((w, h), Image.LANCZOS)


def toggle(on, w=64, h=32):
    s = 4
    im = Image.new("RGBA", (w * s, h * s), (0, 0, 0, 0))
    d = ImageDraw.Draw(im)
    d.rounded_rectangle((s, s, (w - 1) * s, (h - 1) * s), radius=15 * s, fill=(70, 30, 6, 255) if on else (10, 8, 6, 255),
                        outline=(GOLD if on else GOLD_DIM) + (255,), width=2 * s)
    cx, cy, r = ((w - 16) if on else 16) * s, h * s / 2, 11 * s
    if on:                                      # a lit candle-coloured stone
        g = Image.new("RGBA", im.size, (0, 0, 0, 0))
        ImageDraw.Draw(g).ellipse((cx - r - 3 * s, cy - r - 3 * s, cx + r + 3 * s, cy + r + 3 * s), fill=AMBER + (200,))
        im = Image.alpha_composite(im, g.filter(ImageFilter.GaussianBlur(4 * s)))
        d = ImageDraw.Draw(im)
        d.ellipse((cx - r, cy - r, cx + r, cy + r), fill=AMBER + (255,), outline=(90, 60, 20, 255), width=s)
        d.ellipse((cx - r * 0.5, cy - r * 0.6, cx + r * 0.1, cy - r * 0.05), fill=AMBER_HI + (255,))
    else:
        d.ellipse((cx - r, cy - r, cx + r, cy + r), fill=IRON + (255,), outline=GOLD_DARK + (255,), width=s)
        d.ellipse((cx - r * 0.5, cy - r * 0.6, cx + r * 0.1, cy - r * 0.05), fill=(74, 66, 58, 255))
    return im.resize((w, h), Image.LANCZOS)


# ------------------------------------------------------------------------------------------------------ the skin
def image_entry(fname, indent):
    """a full-tab background image, as the SDK's reference gives it (ui-reference.md 6.6), the tab's first entry"""
    e = {"version": 2,
         "componentData": {"version": 1, "name": "page", "type": "Image",
                           "data": {"version": 2, "imageType": "Regular", "colour": "0", "image": fname}},
         "handle remapping": {"version": 1, "map": []},
         "bounds": {"version": 1, "acceptsHWFocus": "No", "showWhenDataModelInvalid": "Show", "whenVisible": "Always",
                    "boundsType": "Absolute", "bounds": "0 0 %d %d" % (W, H)}}
    txt = json.dumps(e, indent=4)
    return "\n".join(indent + ln for ln in txt.split("\n"))


def build_skin():
    params = os.path.join(PROJ, "build", "params.json")
    if not os.path.isfile(params):
        sys.exit("make_skin: build the plugin once first (build/params.json is missing)")
    tmp = os.path.join(PROJ, "build", "skin-gen")
    shutil.rmtree(tmp, ignore_errors=True)
    subprocess.check_call([sys.executable, os.path.join(SDK, "tools", "gen_ui.py"), "--params", params, "--ui",
                           os.path.join(PROJ, "ui.json"), "--out", tmp, "--kit-cache", os.path.join(PROJ, "build")])
    shutil.rmtree(SKINS, ignore_errors=True)
    shutil.copytree(tmp, SKINS)
    # the graphics, in place of the kit's (same names, same sizes: the layout stands)
    print("make_skin: drawing the knobs and faders")
    art = {"knob_uni.png": knob_strip(False), "knob_bi.png": knob_strip(True), "fader_h_uni.png": fader_strip(),
           "toggle_on.png": toggle(True), "toggle_off.png": toggle(False),
           "seg3_on.png": seg(80, 40, True), "seg3_off.png": seg(80, 40, False),
           "seg2_on.png": seg(122, 40, True), "seg2_off.png": seg(122, 40, False),
           "seg4_on.png": seg(58, 40, True), "seg4_off.png": seg(58, 40, False)}
    kit = sorted(f for f in os.listdir(SKINS) if f.endswith(".png"))
    for f in kit:
        if f not in art:
            sys.exit("make_skin: the layout uses %s, which this script does not draw yet" % f)
        old = Image.open(os.path.join(SKINS, f)).size
        if old != art[f].size:
            sys.exit("make_skin: %s is %s, the kit's is %s" % (f, art[f].size, old))
        art[f].save(os.path.join(SKINS, f), optimize=True)
    # the pages: the image goes in as text (the file has keys a JSON library would drop: ui-reference.md 1.8)
    path = os.path.join(SKINS, "TUI.json")
    txt = open(path, encoding="utf-8").read()
    print("make_skin: drawing the pages")
    for i, tab in enumerate(PAGES):
        fname, im = background(tab, 1000 + i)
        im.save(os.path.join(SKINS, fname), quality=90, subsampling=0)
        at = txt.find('"tabName": "%s"' % tab)
        if at < 0:
            sys.exit("make_skin: no tab %s in TUI.json" % tab)
        key = '"componentsData": ['
        k = txt.find(key, at) + len(key)
        nl = txt.find("\n", k)
        indent = txt[nl + 1:txt.find("{", nl)]
        txt = txt[:k] + "\n" + image_entry(fname, indent) + "," + txt[k:]
    open(path, "w", encoding="utf-8").write(txt)
    print("make_skin: skins/ written (%d files)" % len(os.listdir(SKINS)))


# --------------------------------------------------------------------------------------------------- the preview
def preview(out, tabs):
    tui = json.load(open(os.path.join(SKINS, "TUI.json"), encoding="utf-8"))["pageData"]
    params = {p["name"]: p for p in json.load(open(os.path.join(PROJ, "build", "params.json")))["params"]}
    defs = {d["key"]: d["value"] for d in tui["componentDefinitions"]["localComponentDefinitions"]}
    shots = []

    def colour(c):
        v = int(c, 16)
        return ((v >> 16) & 255, (v >> 8) & 255, v & 255, (v >> 24) & 255)

    def draw(im, d, cd, bx, by, p):
        t, data = cd["type"], cd["data"]
        norm = p["default"] if p else 0.0
        if t == "Image":
            im.paste(Image.open(os.path.join(SKINS, data["image"])).convert("RGBA"), (bx, by))
        elif t == "Knob":
            st = Image.open(os.path.join(SKINS, data["filmStrip"]))
            n = data["numFrames"]
            fh = st.height // n
            fr = st.crop((0, round(norm * (n - 1)) * fh, st.width, round(norm * (n - 1)) * fh + fh))
            im.alpha_composite(fr, (bx, by))
        elif t == "Button":
            if p and p["kind"] == "choice":
                on = round(norm * (len(p["choices"]) - 1)) == data["buttonId"]
            else:
                on = norm >= 0.5
            b = Image.open(os.path.join(SKINS, data["onImage"] if on else data["offImage"])).convert("RGBA")
            im.alpha_composite(b, (bx + (cd["_w"] - b.width) // 2, by + (cd["_h"] - b.height) // 2))
        elif t == "Label":
            ts = data["textStyle"]
            text = data.get("text") or (p["name"] if data.get("type") == "Name" else
                                        ((p["display"] + (" " + p["label"] if p["label"] else "")) if p else ""))
            f = font(SANS, int(ts["font"]["height"] * 0.74))
            j = ts["justification"]
            if "entred" in j:
                d.text((bx + cd["_w"] / 2, by + cd["_h"] / 2), text, font=f, fill=colour(ts["colour"]), anchor="mm")
            elif j == "right":
                d.text((bx + cd["_w"], by + cd["_h"] / 2), text, font=f, fill=colour(ts["colour"]), anchor="rm")
            else:
                d.text((bx, by + cd["_h"] / 2), text, font=f, fill=colour(ts["colour"]), anchor="lm")
        elif t == "Slider":                     # the MPC's own bar: a stand-in
            d.rounded_rectangle((bx, by, bx + cd["_w"], by + cd["_h"]), radius=4, fill=(40, 36, 32, 255))
            d.text((bx + 10, by + 22), p["name"] if p else "", font=font(SANS, 18), fill=(160, 150, 130, 255), anchor="lm")
            d.text((bx + 10, by + 54), p["display"] if p else "", font=font(SANS, 22), fill=(255, 255, 255, 255), anchor="lm")

    for tab in tui["tabs"]:
        if tabs and tab["tabName"] not in tabs:
            continue
        im = Image.new("RGBA", (W, H), (0, 0, 0, 255))
        d = ImageDraw.Draw(im)
        for e in tab["componentDefinition"]["componentsData"]:
            cd = dict(e["componentData"])
            x, y, w, h = (int(v) for v in e["bounds"]["bounds"].split())
            p = params.get(cd["name"])
            if cd["type"] in defs:
                for sub in defs[cd["type"]]["componentsData"]:
                    if sub["bounds"]["whenVisible"] != "Always":
                        continue
                    sd = dict(sub["componentData"])
                    sx, sy, sd["_w"], sd["_h"] = (int(v) for v in sub["bounds"]["bounds"].split())
                    draw(im, d, sd, x + sx, y + sy, p)
            else:
                cd["_w"], cd["_h"] = w, h
                draw(im, d, cd, x, y, None)
        shots.append(im.convert("RGB"))
    sheet = Image.new("RGB", (W, (H + 12) * len(shots) - 12), (0, 0, 0))
    for i, s in enumerate(shots):
        sheet.paste(s, (0, i * (H + 12)))
    sheet.save(out)
    print("make_skin: preview ->", out)


if __name__ == "__main__":
    ap = argparse.ArgumentParser()
    ap.add_argument("--preview", help="also draw what the MPC should show, into this PNG")
    ap.add_argument("--tabs", nargs="*", help="the preview's tabs (default: all)")
    ap.add_argument("--no-build", action="store_true", help="only the preview, from skins/ as it is")
    a = ap.parse_args()
    if not a.no_build:
        build_skin()
    if a.preview:
        preview(a.preview, a.tabs)
