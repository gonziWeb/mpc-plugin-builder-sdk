# SPDX-License-Identifier: GPL-3.0-only
# Copyright (C) 2026 Gonzi Audio
"""The Felucca family: Felucca itself and the firmwares forked from it share one shape (core.h, engines.c, params.c,
mod.c, voice.c, fx.c; tests/hostsim.c builds them on a computer). A pack of this family is that firmware's own
audio core, as its hostsim.c includes it, with part 1 played by the plugin: the firmware's voices, envelopes, LFO,
sends, chorus / delay / reverb and master are all its own code."""
import os
import re

SRC = os.environ.get("ARMADA_UPSTREAM", "/home/claude/src")


def read(p):
    with open(p, encoding="utf-8", errors="replace") as f:
        return f.read()


def func_text(text, name):
    """the definition of the static function `name` (balanced braces), '' if absent"""
    m = re.search(r"^static [^\n;{]*\b%s\s*\([^;{]*\)\s*(/\*.*?\*/\s*)?\{" % re.escape(name), text, re.M | re.S)
    if not m:
        return ""
    i, depth = m.end() - 1, 0
    while True:
        c = text[i]
        if c == "{":
            depth += 1
        elif c == "}":
            depth -= 1
            if depth == 0:
                return text[m.start():i + 1] + "\n"
        i += 1


GLUE = r"""
/* ------------------------------------------------------------------ Armada's glue (GPL-3.0-only, Gonzi Audio) --- */
enum { AG_NENGINES = NENGINES, AG_CTL = CTL };
static void ag_init(void)
{
    uint32_t k;
    /*PK_CTORS*/
    host_tracks_init();
    for (k = 1; k < NTRK; k++)
        trk[k].p[P_MUTE] = 1;                       /* the plugin plays part 1 */
    song.sel = 0;
}
static void ag_kill(void)
{
    uint32_t i;
    track_t *t = &trk[0];
    trk_all_off(t);
    for (i = 0; i < NVOICE; i++) {
        t->v[i].active = t->v[i].gate = 0;
        t->v[i].stage = 0;
        t->v[i].env = t->v[i].env_out = 0;
    }
}
static void ag_sound(uint32_t e, uint32_t s)
{
    ag_kill();
    AG_OWN_SOUND
    host_preset(&trk[0], e, s);
    AG_AFTER_SOUND
}
static const char *ag_own_name(uint32_t e, uint32_t s, uint32_t *count)
{
    (void)e; (void)s;
    *count = 0;
    AG_OWN_NAME
    return 0;
}
static int32_t ag_getp(uint32_t id) { return trk[0].p[id]; }
static void ag_setp(uint32_t id, int32_t v)
{
    const param_desc_t *d = &TP[id];
    trk[0].p[id] = (int16_t)(v < d->min ? d->min : v > d->max ? d->max : v);
}
static void ag_sete(uint32_t k, int32_t v)
{
    const param_desc_t *d = &ENGINES[trk[0].engine]->edit[k];
    trk[0].p[P_E0 + k] = (int16_t)(v < d->min ? d->min : v > d->max ? d->max : v);
    AG_AFTER_SETE
}
static void ag_setg(uint32_t id, int32_t v)
{
    const param_desc_t *d = &GP[id];
    song.g[id] = (int16_t)(v < d->min ? d->min : v > d->max ? d->max : v);
}
static void ag_note_on(uint32_t n, uint32_t vel) { trk_note_on(&trk[0], n, vel); }
static void ag_note_off(uint32_t n) { trk_note_off(&trk[0], n); }
static void ag_all_off(void) { trk_all_off(&trk[0]); }
static void ag_bend(int32_t q8) { AG_BEND(q8); }
static void ag_render(int32_t *o, uint32_t n) { mix_block(o, n); }
static void ag_format(uint32_t e, uint32_t k, int32_t v, char *val, const char **unit)
{
    const param_desc_t *d = &ENGINES[e]->edit[k];
    *unit = 0;
    param_format(d, v, val, unit);
}
"""

CXX = r"""
// ------------------------------------------------------------------ the pack: Part and Pack (C++, Gonzi Audio) ---
static const char *const AG_EXPOSE[] = {%(expose)s};
static int ag_fw_engine(int e)                       // the pack's engine e -> the firmware's engine number
{
    static int map[32], n = -1;
    if (n < 0) {
        int c = 0;
        if (!std::strcmp(AG_EXPOSE[0], "*")) {
            for (unsigned i = 0; i < AG_NENGINES && c < 32; i++)
                if (ENGINES[i] && ENGINES[i]->name && ENGINES[i]->render) map[c++] = (int)i;
        } else
        for (unsigned x = 0; x < sizeof(AG_EXPOSE) / sizeof(AG_EXPOSE[0]); x++)
            for (unsigned i = 0; i < AG_NENGINES; i++)
                if (ENGINES[i] && ENGINES[i]->name && !std::strcmp(ENGINES[i]->name, AG_EXPOSE[x])) { map[c++] = (int)i; break; }
        n = c;
    }
    return e < 0 ? n : map[e < n ? e : 0];
}
static const armada::EngineInfo *ag_engine(int e)
{
    static armada::EngineInfo info[32];
    static bool made = false;
    if (!made) {
        int n = ag_fw_engine(-1);
        for (int i = 0; i < n; i++) {
            const engine_t *g = ENGINES[ag_fw_engine(i)];
            info[i].name = g->name;
            uint32_t own = 0;
            ag_own_name((uint32_t)ag_fw_engine(i), 0, &own);
            info[i].numSounds = own ? (int)own : g->npresets;
            info[i].numParams = 8;
            for (int k = 0; k < 8; k++) {
                info[i].p[k].label = g->edit[k].label ? g->edit[k].label : "";
                info[i].p[k].min = g->edit[k].min;
                info[i].p[k].max = g->edit[k].max;
                info[i].p[k].def = g->edit[k].def;
            }
        }
        made = true;
    }
    return &info[e];
}
static int ag_num_engines() { return ag_fw_engine(-1); }
static const char *ag_sound_name(int e, int s)
{
    const engine_t *g = ENGINES[ag_fw_engine(e)];
    uint32_t own = 0;
    const char *on = ag_own_name((uint32_t)ag_fw_engine(e), (uint32_t)(s < 0 ? 0 : s), &own);
    if (own) return on ? on : "";
    return s >= 0 && s < g->npresets && g->presets && g->presets[s].name ? g->presets[s].name : "";
}
struct AgPart final : armada::Part {
    unsigned char *blk;
    int eng = 0;
    explicit AgPart(unsigned char *b) : blk(b) { pk_scope s(blk); ag_init(); ag_sound((uint32_t)ag_fw_engine(0), 0); }
    ~AgPart() override { std::free(blk); }
    void loadSound(int e, int snd, armada::SoundValues *v) override
    {
        pk_scope s(blk);
        eng = e;
        ag_sound((uint32_t)ag_fw_engine(e), (uint32_t)snd);
        if (v) {
            for (int k = 0; k < 8; k++) v->e[k] = ag_getp(P_E0 + k);
            %(readback)s
        }
    }
    void setE(int k, int v) override { if (k < 8) { pk_scope s(blk); ag_sete((uint32_t)k, v); } }
    void setCommon(const armada::Common &c) override
    {
        pk_scope s(blk);
        %(common_extra)s
    }
    void noteOn(int n, int vel) override { pk_scope s(blk); ag_note_on((uint32_t)n, (uint32_t)vel); }
    void noteOff(int n) override { pk_scope s(blk); ag_note_off((uint32_t)n); }
    void allNotesOff() override { pk_scope s(blk); ag_all_off(); }
    void bend(int cents) override { pk_scope s(blk); ag_bend(cents * 256 / 100); }
    void render(float *l, float *r, int n) override
    {
        int32_t o[2 * AG_CTL];
        pk_scope s(blk);
        ag_render(o, (uint32_t)n);
        for (int i = 0; i < n; i++) { l[i] = (float)o[2 * i] * (1.0f / 32768.0f); r[i] = (float)o[2 * i + 1] * (1.0f / 32768.0f); }
    }
    void format(int e, int k, int v, char *out, int outSize) override
    {
        char val[48] = {0};
        const char *unit = nullptr;
        if (k >= 8 || outSize < 2) { if (outSize > 0) out[0] = 0; return; }
        pk_scope s(blk);
        ag_format((uint32_t)ag_fw_engine(e), (uint32_t)k, v, val, &unit);
        int j = 0;
        for (const char *q = val; *q && j < outSize - 1; q++) out[j++] = *q;
        if (unit && *unit) { if (j < outSize - 1) out[j++] = ' '; for (const char *q = unit; *q && j < outSize - 1; q++) out[j++] = *q; }
        out[j] = 0;
    }
};
static armada::Part *ag_create()
{
    unsigned char *b = pk_new_block();
    return b ? new AgPart(b) : nullptr;
}
} // (closed again below)
extern const armada::Pack armada_pack_%(name)s;
const armada::Pack armada_pack_%(name)s = {"%(name)s", "%(title)s", pk_%(name)s::ag_num_engines, pk_%(name)s::ag_engine,
                                          pk_%(name)s::ag_sound_name, pk_%(name)s::ag_create};
namespace pk_%(name)s {
"""


def chars(text):
    """a char array filled to its last element (C allows the string literal without its NUL; C++ does not)"""
    return "{" + ", ".join("'%s'" % c for c in text) + "}"


def make(g, name, repo, title, licence, expose, drop_includes=(), extra_c="", pre_c="", own_sound="", own_name="", after_sound="", after_sete="",
         common_extra=None, skip_common=(), incdirs=(), defines=(), fixes=(), pre_fixes=(), shared=(), hostsim="tests/hostsim.c"):
    """fills the pack spec's module globals g"""
    root = os.path.join(SRC, name) if os.path.isdir(os.path.join(SRC, name)) else os.path.join(SRC, repo)
    g["ROOT"] = root
    m = re.search(r" at ([0-9a-f]{7})\)", title)
    g["COMMIT"] = m.group(1) if m else "?"
    hs = read(os.path.join(root, hostsim))
    lines = hs.split("\n")
    last = max(i for i, ln in enumerate(lines) if re.match(r'\s*#include "\.\./firmware', ln))
    pre = []
    for ln in lines[:last + 1]:
        if re.match(r"\s*#include <(stdio|unistd|sys/wait|time|math|stdlib)\.h>", ln):
            continue
        if any(d in ln for d in drop_includes):
            continue
        pre.append(ln.replace('"../firmware/', '"' + root + "/firmware/"))
    core = read(os.path.join(root, "firmware/src/core.h")) + read(os.path.join(root, "firmware/src/voice.c"))
    hostf = "".join(func_text(hs, f) for f in ("host_tracks_init", "host_preset_values", "host_preset_req", "host_preset"))
    bend = "midi_bend_target[0] = (q8)" if "midi_bend_target" in core else "(void)(q8)"
    glue = GLUE.replace("AG_OWN_SOUND", own_sound).replace("AG_OWN_NAME", own_name).replace("AG_AFTER_SOUND", after_sound).replace("AG_AFTER_SETE", after_sete).replace("AG_BEND(q8)", bend)
    has = lambda n: re.search(r"\b%s\b" % n, core) is not None
    ce, rb = [], []
    for pid, field in (("P_ATK", "cAtk"), ("P_DEC", "cDec"), ("P_SUS", "cSus"), ("P_REL", "cRel"),
                       ("P_ED_FLT", "cFenv"), ("P_ED_PIT", "cPenv"), ("P_ED_SHP", "cSenv"), ("P_LRATE", "cLfoRate"),
                       ("P_LWAVE", "cLfoWave"), ("P_LFADE", "cLfoFade"), ("P_LD_PIT", "cLfoPitch"), ("P_LD_FLT", "cLfoFilter"),
                       ("P_LD_SHP", "cLfoShape"), ("P_LD_AMP", "cLfoAmp"), ("P_VOICE", "cVoice"), ("P_GLIDE", "cGlide"),
                       ("P_DETUNE", "cDetune"), ("P_DIST", "cDist"), ("P_CHOR", "cChorus"), ("P_DLY", "cDelay"),
                       ("P_REV", "cReverb"), ("P_SPRD", "cSpread"), ("P_FTYPE", "cFtype")):
        if has(pid) and pid not in skip_common:
            ce.append("ag_setp(%s, c.v[armada::%s]);" % (pid, field))
            if pid in ("P_ATK", "P_DEC", "P_SUS", "P_REL", "P_ED_FLT", "P_VOICE", "P_DIST", "P_CHOR", "P_DLY", "P_REV"):
                rb.append("v->set(armada::%s, ag_getp(%s));" % (field, pid))   # what a factory sound sets
    for gid, field in (("G_DTIME", "cDlyTime"), ("G_DFDBK", "cDlyFdbk"), ("G_RSIZE", "cRevSize"), ("G_RDAMP", "cRevDamp"),
                       ("G_CRATE", "cChoRate"), ("G_CDEPTH", "cChoDepth"), ("G_RTYPE", "cRevType")):
        if has(gid) and gid not in skip_common:
            ce.append("ag_setg(%s, c.v[armada::%s]);" % (gid, field))
    if has("G_TUNE") and "G_TUNE" not in skip_common:
        ce.append("ag_setg(G_TUNE, c.tune);")
    if common_extra is None:
        common_extra = "\n        ".join(ce)
    readback = "\n            ".join(rb)
    g["TITLE"] = title
    g["LICENCE"] = licence
    g["TU"] = "#include <stdint.h>\n#include <string.h>\n#include <stddef.h>\n" + pre_c + "\n".join(pre) + "\n" + hostf + extra_c + glue
    g["INCDIRS"] = [os.path.join(root, d) for d in ("build/gen", "firmware/src", "firmware/hal", "firmware/bsp/chip",
                                                    "firmware/bsp/boards/fm1", "firmware/engines/src", "tests")] + list(incdirs)
    g["DEFINES"] = list(defines)
    g["PRE_FIXES"] = list(pre_fixes)
    g["FIXES"] = list(fixes)
    g["SHARED"] = tuple(shared)
    g["CXX"] = CXX % {"name": name, "title": title.replace('"', "'"), "expose": ", ".join('"%s"' % e for e in expose),
                      "common_extra": common_extra, "readback": readback}
