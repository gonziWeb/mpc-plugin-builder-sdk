# SPDX-License-Identifier: GPL-3.0-only
# Copyright (C) 2026 Gonzi Audio
"""OberVave's family (zvenson's engine layout: <px>_params.h with the PDEF table, <px>_engine.h with note_event and
engine_audio, <px>_patches.h with FACTORY): the glue for a firmware of that shape."""
import _std

GLUE = r"""
/* ------------------------------------------------------------------ Armada's glue (GPL-3.0-only, Gonzi Audio) --- */
enum { SG_NENG = 1, SG_FRAMES = 32 };
typedef struct { const char *label; uint8_t p; } sg_slot_t;
static const sg_slot_t SG_E[] = { %(slots)s };
#define SG_NE (sizeof SG_E / sizeof SG_E[0])
static const struct { uint8_t c, p, mode; } SG_C[] = { %(common)s };     /* mode 0: as is, 1: |v| * 2 (a depth), 2: voice */
#define SG_NC (sizeof SG_C / sizeof SG_C[0])
%(help)s
static void sg_init(void)
{
    uint32_t p;
    for (p = 0; p < NPART; p++) patch_defaults(&live_p[p]);
    engine_init();
    %(init)s
}
static const char *sg_eng_name(uint32_t e) { (void)e; return "%(engine)s"; }
static uint32_t sg_num_sounds(uint32_t e) { (void)e; return %(nsounds)s; }
static const char *sg_sound_name(uint32_t e, uint32_t s) { (void)e; return FACTORY[s].name; }
static uint32_t sg_num_params(uint32_t e) { (void)e; return SG_NE; }
static void sg_param(uint32_t e, uint32_t k, const char **label, int32_t *min, int32_t *max, int32_t *def)
{
    (void)e;
    *label = SG_E[k].label; *min = 0; *max = PDEF[SG_E[k].p].max; *def = PDEF[SG_E[k].p].def;
}
static void sg_format(uint32_t e, uint32_t k, int32_t v, char *out)
{
    static const char *const WAVE[] = {"OFF", "SAW", "PLS", "S+P"};
    static const char *const LSH[] = {"TRI", "SAW", "RAMP", "SQR", "S&H", "RND"};
    uint32_t kind = PDEF[SG_E[k].p].kind;
    (void)e;
    (void)WAVE; (void)LSH;
    if (kind == K_ONOFF) sg_scpy(out, v ? "ON" : "OFF");
    %(formats)s
    else if (kind == K_BIP) sg_itoa(v - 64, out);
    else sg_itoa(v, out);
}
static void sg_sound(uint32_t e, uint32_t s)
{
    (void)e;
    patch_from_factory(&live, &FACTORY[s %% (%(nsounds)s)]);
    live.v[P_BEND] = 24;                        /* the plugin's Bend sets the range */
    %(after_sound)s
}
static int32_t sg_gete(uint32_t k) { return live.v[SG_E[k].p]; }
static void sg_sete(uint32_t k, int32_t v)
{
    uint32_t p = SG_E[k].p;
    live.v[p] = (uint8_t)(v < 0 ? 0 : v > PDEF[p].max ? PDEF[p].max : v);
}
static int sg_getc(uint32_t i, int32_t *v)
{
    uint32_t n;
    for (n = 0; n < SG_NC; n++)
        if (SG_C[n].c == i) {
            int32_t x = live.v[SG_C[n].p];
            if (SG_C[n].mode == 1) x = x / 2;
            else if (SG_C[n].mode == 2) x = %(voice_get)s;
            else if (SG_C[n].mode == 3) x = x * 127 / (int32_t)PDEF[SG_C[n].p].max;
            *v = x;
            return 1;
        }
    return 0;
}
static void sg_setc(uint32_t i, int32_t v)
{
    uint32_t n;
    for (n = 0; n < SG_NC; n++)
        if (SG_C[n].c == i) {
            uint32_t p = SG_C[n].p;
            int32_t x = v;
            if (SG_C[n].mode == 1) x = (v < 0 ? -v : v) * 2;
            else if (SG_C[n].mode == 2) x = %(voice_set)s;
            else if (SG_C[n].mode == 3) x = (v * (int32_t)PDEF[p].max + 63) / 127;
            live.v[p] = (uint8_t)(x < 0 ? 0 : x > PDEF[p].max ? PDEF[p].max : x);
        }
}
static void sg_tune(int32_t cents) { %(tune)s }
static void sg_note(uint32_t on, uint32_t note, uint32_t vel) { note_event(on, note, vel); }
static void sg_all_off(void) { voices_release_all(); }
static void sg_bend(int32_t cents) { ctl_bend = cents * 8191 / 2400; }
static void sg_mod(int32_t v) { %(mod)s }
static void sg_render(int32_t *o, uint32_t n)
{
    uint32_t i;
    for (i = 0; i < 2u * n; i++) o[i] = 0;
    engine_audio(o, n);
}
"""


def make(g, name, title, licence, repo, px, engine, slots, common, nsounds="NFACT", init="", formats="", after_sound="",
         tune="(void)cents;", mod="(void)v;", voice_get="0", voice_set="0", extra_includes=(), pre="", defines=(), fixes=(), pre_fixes=()):
    import os
    import re
    src = os.environ.get("ARMADA_UPSTREAM", "/home/claude/src")
    root = os.path.join(src, name) if os.path.isdir(os.path.join(src, name)) else os.path.join(src, repo)
    g["ROOT"] = root
    m = re.search(r" at ([0-9a-f]{7})\)", title)
    g["COMMIT"] = m.group(1) if m else "?"
    inc = "".join('#include "%s/firmware/src/%s"\n' % (root, h) for h in
                  ["%s_dsp.h" % px, "%s_params.h" % px, "%s_fx.h" % px, "%s_engine.h" % px, "%s_seq.h" % px,
                   "%s_patches.h" % px] + list(extra_includes))
    glue = GLUE % {"slots": ", ".join('{"%s", %s}' % s for s in slots),
                   "common": ", ".join("{%s, %s, %d}" % (("armada_" + c[0]), c[1], c[2] if len(c) > 2 else 0) for c in common),
                   "help": _std.HELP, "init": init, "engine": engine, "nsounds": nsounds, "formats": formats,
                   "after_sound": after_sound, "tune": tune, "mod": mod, "voice_get": voice_get, "voice_set": voice_set}
    # the common indices as C constants (armada::CommonIndex, in order)
    names = ("cAtk cDec cSus cRel cFenv cPenv cSenv cLfoRate cLfoWave cLfoFade cLfoPitch cLfoFilter cLfoShape cLfoAmp cVoice "
             "cGlide cDetune cSpread cDist cChorus cDelay cReverb cDlyTime cDlyFdbk cRevSize cRevDamp cRevType cChoRate "
             "cChoDepth cFtype").split()
    cenum = "enum { " + ", ".join("armada_%s" % n for n in names) + " };\n"
    tu = "#define FS 44100u\n#define HALF_FRAMES 256u\n" + pre + inc + cenum + glue
    _std.make(g, name, title, licence, tu, [root + "/firmware/src", root + "/firmware/gen"], defines=defines, fixes=fixes,
              pre_fixes=pre_fixes)


COMMON_ENUM = None
