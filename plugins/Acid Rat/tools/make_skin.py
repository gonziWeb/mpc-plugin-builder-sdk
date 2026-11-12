#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-3.0-only
# Copyright (C) 2026 Gonzi Audio
"""Acid Rat's skin: a black lab panel with acid-green print on the MPC's screen.

  python3 "plugins/Acid Rat/tools/make_skin.py"          (from the SDK root, after a build: it needs build/params.json)

It runs the SDK's generator on ui.json (the layout, the Q-Link map: unchanged), then replaces the kit's graphics with
Acid Rat's own, drawn here with Pillow (nothing is downloaded, no outside artwork):
  bg_<page>.jpg     a page a tab: black powder-coat, a printed frame and caption a row, hazard stripes, the rat
  knob_*.png        a graphite cap in a chrome ring, an acid-green pointer and value arc
  fader_h_uni.png   a slot filling with green, a chrome cap
  seg*              a lit green key when on
and writes the result to skins/ (the build copies skins/ verbatim). --preview OUT.png also draws what the MPC should
show (the labels in a stand-in font: the MPC has its own). The same recipe as Grimoire's make_skin.py.
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
SKINS = os.path.join(PROJ, "skins")

# the palette
PANEL = (17, 18, 16)
ACID = (183, 255, 26)          # the print and the lights
ACID_HI = (232, 255, 150)
ACID_DIM = (96, 134, 14)
ACID_DARK = (44, 62, 8)
CHROME_HI = (236, 240, 236)
CHROME_LO = (70, 76, 72)
GRAPHITE = (38, 40, 38)

MONO_BOLD = "/usr/share/fonts/truetype/dejavu/DejaVuSansMono-Bold.ttf"
SANS = "/usr/share/fonts/truetype/dejavu/DejaVuSans.ttf"


def font(path, size):
    try:
        return ImageFont.truetype(path, size)
    except OSError:
        return ImageFont.truetype(SANS, size)


# the generator's 4 x 4 grid
XS, YS, SW, SH = (23, 343, 663, 983), (45, 196, 346, 497), 278, 107
W, H = 1280, 628

PAGES = {   # tab -> file, the rows' captions
    "ACID": ("bg_acid.jpg", ("FILTER", "VOICE", "DRIVE / SLIDE", "ECHO")),
    "LINE": ("bg_line.jpg", ("LINE", "DICE", "KEY", "FEEL")),
}


# ---------------------------------------------------------------------------------------------------- backgrounds
def panel(seed):
    """black powder-coat: fine grain, a soft light from the top left, darker corners"""
    random.seed(seed)
    base = Image.new("RGB", (W, H), PANEL)
    n1 = Image.effect_noise((W, H), 30).filter(ImageFilter.GaussianBlur(0.6))
    n2 = Image.effect_noise((W // 8, H // 8), 50).resize((W, H), Image.BICUBIC).filter(ImageFilter.GaussianBlur(10))
    grain = ImageChops.add(n1, n2, 2.0, 0)
    base = ImageChops.multiply(base, Image.merge("RGB", [grain.point(lambda v: 170 + v * 85 // 255)] * 3))
    v = Image.new("L", (W, H), 0)
    ImageDraw.Draw(v).ellipse((-W * 0.35, -H * 0.9, W * 1.05, H * 1.25), fill=255)
    v = v.filter(ImageFilter.GaussianBlur(140)).point(lambda p: 120 + p * 135 // 255)
    return ImageChops.multiply(base, Image.merge("RGB", [v] * 3))


def rat(size):
    """the mark: a rat's head seen from the front, in a few strokes (white on black, used as a mask)"""
    s = 3
    n = size * s
    im = Image.new("L", (n, n), 0)
    d = ImageDraw.Draw(im)
    c, w = n / 2, 5 * s
    # ears
    for sx in (-1, 1):
        ex, ey, er = c + sx * n * 0.27, n * 0.27, n * 0.17
        d.ellipse((ex - er, ey - er, ex + er, ey + er), outline=255, width=w)
        d.ellipse((ex - er * 0.5, ey - er * 0.5, ex + er * 0.5, ey + er * 0.5), outline=255, width=s * 2)
    # the head: a wide brow narrowing to the snout (drawn over the ears' lower halves)
    head = [(c - n * 0.30, n * 0.36), (c + n * 0.30, n * 0.36), (c + n * 0.33, n * 0.50), (c + n * 0.10, n * 0.86),
            (c, n * 0.90), (c - n * 0.10, n * 0.86), (c - n * 0.33, n * 0.50)]
    d.polygon(head, fill=0)
    d.line(head + [head[0]], fill=255, width=w, joint="curve")
    # eyes: two slits, slanted
    for sx in (-1, 1):
        d.line((c + sx * n * 0.20, n * 0.50, c + sx * n * 0.08, n * 0.56), fill=255, width=w + 2 * s)
    # the nose and the teeth
    d.polygon(((c - n * 0.05, n * 0.80), (c + n * 0.05, n * 0.80), (c, n * 0.86)), fill=255)
    d.rectangle((c - n * 0.035, n * 0.905, c - n * 0.004, n * 0.965), outline=255, width=2 * s)
    d.rectangle((c + n * 0.004, n * 0.905, c + n * 0.035, n * 0.965), outline=255, width=2 * s)
    # whiskers
    for sx in (-1, 1):
        for k, dy in enumerate((-0.07, 0.0, 0.07)):
            d.line((c + sx * n * 0.12, n * 0.78 + k * n * 0.012, c + sx * n * 0.46, n * (0.76 + dy)), fill=255, width=2 * s)
    return im.resize((size, size), Image.LANCZOS)


def hazard(w, h, stripe=18):
    """diagonal stripes, acid on black (RGBA)"""
    s = 3
    im = Image.new("RGBA", (w * s, h * s), (8, 9, 7, 255))
    d = ImageDraw.Draw(im)
    for x in range(-h * s, w * s + h * s, 2 * stripe * s):
        d.polygon(((x, h * s), (x + stripe * s, h * s), (x + stripe * s + h * s, 0), (x + h * s, 0)), fill=ACID_DIM + (255,))
    return im.resize((w, h), Image.LANCZOS)


def background(tab, seed):
    fname, rows = PAGES[tab]
    im = panel(seed).convert("RGBA")
    # the rat, faint, behind the right-hand controls
    size = 470
    m = rat(size).point(lambda v: v * 44 // 255)
    layer = Image.new("RGBA", (W, H), (0, 0, 0, 0))
    layer.paste(Image.new("RGBA", (size, size), ACID + (255,)), (W - size - 150, (H - size) // 2 + 14), m)
    im = Image.alpha_composite(im, layer)
    # hazard stripes: a band at the very top and bottom
    hz = hazard(W, 9)
    im.alpha_composite(hz, (0, 0))
    im.alpha_composite(hz, (0, H - 9))

    s = 2
    ov = Image.new("RGBA", (W * s, H * s), (0, 0, 0, 0))
    d = ImageDraw.Draw(ov)
    f_cap, f_small = font(MONO_BOLD, 15 * s), font(MONO_BOLD, 10 * s)
    for i, y in enumerate(YS):
        # a printed frame round the row, cut at the top left for its caption (a label plate: acid, black letters)
        x0, x1, y0, y1 = 12 * s, (W - 12) * s, (y - 9) * s, (y + SH + 7) * s
        d.rounded_rectangle((x0, y0, x1, y1), radius=10 * s, fill=(0, 0, 0, 70), outline=ACID_DARK + (255,), width=s)
        cap = rows[i]
        tw = d.textlength(cap, font=f_cap)
        px0, py0 = 30 * s, (y - 30) * s
        d.rounded_rectangle((px0, py0, px0 + tw + 20 * s, py0 + 21 * s), radius=3 * s, fill=ACID + (255,))
        d.text((px0 + 10 * s, py0 + 10.5 * s), cap, font=f_cap, fill=(10, 12, 6, 255), anchor="lm")
        # the row's number and a run of ticks to the right of the plate
        tx = px0 + tw + 32 * s
        d.text((tx, py0 + 10.5 * s), "%02d" % (i + 1), font=f_small, fill=ACID_DIM + (255,), anchor="lm")
        for k in range(24):
            xx = tx + 30 * s + k * 7 * s
            d.line((xx, py0 + (6 if k % 4 else 3) * s, xx, py0 + 15 * s), fill=ACID_DARK + (255,), width=s)
        # corner marks on the frame
        for cx, cy, sx, sy in ((x0, y0, 1, 1), (x1, y0, -1, 1), (x0, y1, 1, -1), (x1, y1, -1, -1)):
            d.line((cx + sx * 10 * s, cy, cx + sx * 26 * s, cy), fill=ACID + (255,), width=2 * s)
            d.line((cx, cy + sy * 10 * s, cx, cy + sy * 22 * s), fill=ACID + (255,), width=2 * s)
        # faint dividers between the row's four cells
        for x in XS[1:]:
            xm = (x - 21) * s
            d.line((xm, (y + 12) * s, xm, (y + SH - 14) * s), fill=ACID_DARK + (150,), width=s)
    # the name, top right, in the gap above the first row
    d.text(((W - 30) * s, 22 * s), "ACID RAT", font=font(MONO_BOLD, 17 * s), fill=ACID + (255,), anchor="rm")
    nw = d.textlength("ACID RAT", font=font(MONO_BOLD, 17 * s))
    d.text(((W - 40) * s - nw, 22 * s), "GONZI AUDIO  //", font=f_small, fill=ACID_DIM + (255,), anchor="rm")
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
    # what does not move: the unlit arc, the chrome ring, the cap with its grip marks
    static = Image.new("RGBA", (n, n), (0, 0, 0, 0))
    d = ImageDraw.Draw(static)
    ra = 41 * s
    d.arc((c - ra, c - ra, c + ra, c + ra), a0, a0 + sweep, fill=(34, 42, 18, 255), width=5 * s)
    rr = 33 * s
    ring = radial(n, CHROME_HI, CHROME_LO, cy=0.22).convert("RGBA")
    m = Image.new("L", (n, n), 0)
    ImageDraw.Draw(m).ellipse((c - rr, c - rr, c + rr, c + rr), fill=255)
    static.paste(ring, (0, 0), m)
    d.ellipse((c - rr, c - rr, c + rr, c + rr), outline=(6, 7, 6, 255), width=s)
    rd = 29 * s
    cap = radial(n, (72, 76, 72), (14, 15, 14), cy=0.28).convert("RGBA")
    m = Image.new("L", (n, n), 0)
    ImageDraw.Draw(m).ellipse((c - rd, c - rd, c + rd, c + rd), fill=255)
    static.paste(cap, (0, 0), m)
    d.ellipse((c - rd, c - rd, c + rd, c + rd), outline=(4, 5, 4, 255), width=s)
    for k in range(36):                         # grip marks round the cap's edge
        a = k * math.tau / 36
        r0, r1 = 25.5 * s, 28.5 * s
        d.line((c + r0 * math.cos(a), c + r0 * math.sin(a), c + r1 * math.cos(a), c + r1 * math.sin(a)), fill=(8, 9, 8, 255), width=s)
    rt = 19 * s                                 # the flat top
    top = radial(n, (58, 61, 58), (26, 28, 26), cy=0.40).convert("RGBA")
    m = Image.new("L", (n, n), 0)
    ImageDraw.Draw(m).ellipse((c - rt, c - rt, c + rt, c + rt), fill=255)
    static.paste(top, (0, 0), m)
    d.ellipse((c - rt, c - rt, c + rt, c + rt), outline=(90, 96, 90, 255), width=s // 2)

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
            da.arc((c - ra, c - ra, c + ra, c + ra), lo, hi, fill=ACID + (255,), width=5 * s)
            da.arc((c - ra + 1.5 * s, c - ra + 1.5 * s, c + ra - 1.5 * s, c + ra - 1.5 * s), lo, hi, fill=ACID_HI + (255,), width=s)
        if bipolar:                             # the centre mark
            a = math.radians(a0 + sweep / 2)
            da.line((c + (ra - 5 * s) * math.cos(a), c + (ra - 5 * s) * math.sin(a), c + ra * math.cos(a), c + ra * math.sin(a)),
                    fill=(255, 255, 255, 255), width=2 * s)
        glow = arc.filter(ImageFilter.GaussianBlur(3 * s))
        im = Image.alpha_composite(im, Image.blend(Image.new("RGBA", (n, n), (0, 0, 0, 0)), glow, 0.6))
        im = Image.alpha_composite(im, arc)
        dp = ImageDraw.Draw(im)                 # the pointer: an acid line from the top's edge to the cap's
        a = math.radians(ang)
        r0, r1 = 8 * s, 27 * s
        p = (c + r0 * math.cos(a), c + r0 * math.sin(a), c + r1 * math.cos(a), c + r1 * math.sin(a))
        dp.line(p, fill=(4, 5, 4, 255), width=6 * s)
        dp.line(p, fill=ACID + (255,), width=3 * s)
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
        d.rounded_rectangle((x0 - 5 * s, cy - 6 * s, x1 + 5 * s, cy + 6 * s), radius=3 * s, fill=(4, 5, 4, 255),
                            outline=ACID_DARK + (255,), width=s)
        for k in range(11):                     # marks under the slot
            x = x0 + (x1 - x0) * k / 10
            d.line((x, cy + 9 * s, x, cy + (13 if k % 5 == 0 else 11) * s), fill=ACID_DIM + (255,), width=s)
        x = x0 + (x1 - x0) * v
        if v > 0.004:
            fill = Image.new("RGBA", im.size, (0, 0, 0, 0))
            df = ImageDraw.Draw(fill)
            df.rounded_rectangle((x0 - 2 * s, cy - 3 * s, x, cy + 3 * s), radius=2 * s, fill=ACID + (255,))
            df.line((x0, cy - 1.5 * s, x, cy - 1.5 * s), fill=ACID_HI + (255,), width=s)
            im = Image.alpha_composite(im, Image.blend(Image.new("RGBA", im.size, (0, 0, 0, 0)), fill.filter(ImageFilter.GaussianBlur(3 * s)), 0.6))
            im = Image.alpha_composite(im, fill)
            d = ImageDraw.Draw(im)
        q, r = 8 * s, 14 * s                    # the cap: a chrome block, an acid line across it
        d.rounded_rectangle((x - q - s, cy - r - s, x + q + s, cy + r + s), radius=3 * s, fill=(4, 5, 4, 255))
        d.rounded_rectangle((x - q, cy - r, x + q, cy + r), radius=2 * s, fill=(96, 102, 98, 255))
        d.rounded_rectangle((x - q, cy - r, x + q, cy - r * 0.2), radius=2 * s, fill=(206, 212, 206, 255))
        d.rectangle((x - q, cy + r * 0.55, x + q, cy + r - s), fill=(50, 54, 52, 255))
        d.line((x, cy - r + 3 * s, x, cy + r - 3 * s), fill=(8, 10, 6, 255), width=3 * s)
        d.line((x, cy - r + 3 * s, x, cy + r - 3 * s), fill=ACID + (255,), width=s)
        strip.paste(im.resize((w, h), Image.LANCZOS), (0, f * h))
    return strip


def seg(w, h, on):
    """a key of a switch: the MPC writes the choice's name over it in white, so the lit key stays dark enough to read"""
    s = 4
    im = Image.new("RGBA", (w * s, h * s), (0, 0, 0, 0))
    d = ImageDraw.Draw(im)
    box = (s, s, (w - 1) * s, (h - 1) * s)
    if on:
        g = Image.new("RGBA", im.size, (0, 0, 0, 0))
        ImageDraw.Draw(g).rounded_rectangle(box, radius=4 * s, fill=ACID + (120,))
        im = Image.alpha_composite(im, g.filter(ImageFilter.GaussianBlur(3 * s)))
        d = ImageDraw.Draw(im)
        d.rounded_rectangle(box, radius=4 * s, fill=(52, 80, 6, 255), outline=ACID + (255,), width=2 * s)
        d.line((8 * s, (h - 6) * s, (w - 8) * s, (h - 6) * s), fill=ACID + (255,), width=2 * s)     # a light bar under the name
    else:
        d.rounded_rectangle(box, radius=4 * s, fill=(10, 11, 10, 235), outline=(58, 62, 58, 255), width=s)
        d.line((8 * s, (h - 6) * s, (w - 8) * s, (h - 6) * s), fill=(40, 44, 40, 255), width=2 * s)
    return im.resize((w, h), Image.LANCZOS)


ART = {"knob_uni.png": lambda: knob_strip(False), "knob_bi.png": lambda: knob_strip(True),
       "fader_h_uni.png": fader_strip,
       "seg2_on.png": lambda: seg(122, 40, True), "seg2_off.png": lambda: seg(122, 40, False),
       "seg3_on.png": lambda: seg(80, 40, True), "seg3_off.png": lambda: seg(80, 40, False),
       "seg4_on.png": lambda: seg(58, 40, True), "seg4_off.png": lambda: seg(58, 40, False)}

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
    kit = sorted(f for f in os.listdir(SKINS) if f.endswith(".png"))
    for f in kit:
        if f not in ART:
            sys.exit("make_skin: the layout uses %s, which this script does not draw yet" % f)
        old = Image.open(os.path.join(SKINS, f)).size
        new = ART[f]()
        if old != new.size:
            sys.exit("make_skin: %s is %s, the kit's is %s" % (new.size, f, old))
        new.save(os.path.join(SKINS, f), optimize=True)
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
