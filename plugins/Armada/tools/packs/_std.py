# SPDX-License-Identifier: GPL-3.0-only
# Copyright (C) 2026 Gonzi Audio
"""A pack written by hand around a firmware's engine: the spec gives the C translation unit (the firmware's engine
files and a small glue with the sg_* functions below); this module adds the C++ Part / Pack around them.

The glue (C, in the firmware's own terms):
    enum { SG_NENG = .., SG_FRAMES = 32 };
    static void sg_init(void);
    static const char *sg_eng_name(uint32_t e);
    static uint32_t sg_num_sounds(uint32_t e);
    static const char *sg_sound_name(uint32_t e, uint32_t s);
    static uint32_t sg_num_params(uint32_t e);                       (at most 32)
    static void sg_param(uint32_t e, uint32_t k, const char **label, int32_t *min, int32_t *max, int32_t *def);
    static void sg_format(uint32_t e, uint32_t k, int32_t v, char *out);     (at most 23 characters)
    static void sg_sound(uint32_t e, uint32_t s);                    (switch to engine e, load its sound s)
    static int32_t sg_gete(uint32_t k);  static void sg_sete(uint32_t k, int32_t v);
    static int sg_getc(uint32_t i, int32_t *v);                      (common parameter i of the loaded sound; 0: none)
    static void sg_setc(uint32_t i, int32_t v);                      (armada::CommonIndex; ignore what there is none of)
    static void sg_tune(int32_t cents);
    static void sg_note(uint32_t on, uint32_t note, uint32_t vel);
    static void sg_all_off(void);
    static void sg_bend(int32_t cents);
    static void sg_mod(int32_t v);
    static void sg_render(int32_t *o, uint32_t n);                   (n frames, stereo interleaved, 32768 = full scale)
"""

CXX = r"""
// ------------------------------------------------------------------ the pack: Part and Pack (C++, Gonzi Audio) ---
static const armada::EngineInfo *sgx_engine(int e)
{
    static armada::EngineInfo info[SG_NENG];
    static bool made = false;
    if (!made) {
        for (int i = 0; i < SG_NENG; i++) {
            info[i].name = sg_eng_name((uint32_t)i);
            info[i].numSounds = (int)sg_num_sounds((uint32_t)i);
            info[i].numParams = (int)sg_num_params((uint32_t)i);
            for (int k = 0; k < info[i].numParams && k < armada::kNumE; k++) {
                const char *lab = "";
                int32_t mn = 0, mx = 0, df = 0;
                sg_param((uint32_t)i, (uint32_t)k, &lab, &mn, &mx, &df);
                info[i].p[k].label = lab ? lab : "";
                info[i].p[k].min = mn; info[i].p[k].max = mx; info[i].p[k].def = df;
            }
        }
        made = true;
    }
    return &info[e];
}
static int sgx_num_engines() { return SG_NENG; }
static const char *sgx_sound_name(int e, int s)
{
    const char *n = s >= 0 && s < (int)sg_num_sounds((uint32_t)e) ? sg_sound_name((uint32_t)e, (uint32_t)s) : "";
    return n ? n : "";
}
struct SgPart final : armada::Part {
    unsigned char *blk;
    explicit SgPart(unsigned char *b) : blk(b) { pk_scope s(blk); sg_init(); sg_sound(0, 0); }
    ~SgPart() override { std::free(blk); }
    void loadSound(int e, int snd, armada::SoundValues *v) override
    {
        pk_scope s(blk);
        sg_all_off();
        sg_sound((uint32_t)e, (uint32_t)snd);
        if (v) {
            int np = (int)sg_num_params((uint32_t)e);
            for (int k = 0; k < np && k < armada::kNumE; k++) v->e[k] = sg_gete((uint32_t)k);
            for (int i = 0; i < armada::kNumCommon; i++) { int32_t x = 0; if (sg_getc((uint32_t)i, &x)) v->set(i, x); }
        }
    }
    void setE(int k, int v) override { pk_scope s(blk); sg_sete((uint32_t)k, v); }
    void setCommon(const armada::Common &c) override
    {
        pk_scope s(blk);
        for (int i = 0; i < armada::kNumCommon; i++) sg_setc((uint32_t)i, c.v[i]);
        sg_tune(c.tune);
    }
    void noteOn(int n, int vel) override { pk_scope s(blk); sg_note(1, (uint32_t)n, (uint32_t)vel); }
    void noteOff(int n) override { pk_scope s(blk); sg_note(0, (uint32_t)n, 0); }
    void allNotesOff() override { pk_scope s(blk); sg_all_off(); }
    void bend(int cents) override { pk_scope s(blk); sg_bend(cents); }
    void modWheel(int v) override { pk_scope s(blk); sg_mod(v); }
    void render(float *l, float *r, int n) override
    {
        int32_t o[2 * SG_FRAMES];
        pk_scope s(blk);
        sg_render(o, (uint32_t)n);
        for (int i = 0; i < n; i++) { l[i] = (float)o[2 * i] * (1.0f / 32768.0f); r[i] = (float)o[2 * i + 1] * (1.0f / 32768.0f); }
    }
    void format(int e, int k, int v, char *out, int outSize) override
    {
        char val[48] = {0};
        if (outSize < 2) return;
        { pk_scope s(blk); sg_format((uint32_t)e, (uint32_t)k, v, val); }
        int j = 0;
        for (const char *q = val; *q && j < outSize - 1; q++) out[j++] = *q;
        out[j] = 0;
    }
};
static armada::Part *sgx_create()
{
    unsigned char *b = pk_new_block();
    return b ? new SgPart(b) : nullptr;
}
} // (closed again below)
extern const armada::Pack armada_pack_%(name)s;
const armada::Pack armada_pack_%(name)s = {"%(name)s", "%(title)s", pk_%(name)s::sgx_num_engines, pk_%(name)s::sgx_engine,
                                          pk_%(name)s::sgx_sound_name, pk_%(name)s::sgx_create};
namespace pk_%(name)s {
"""

# small C helpers a glue may use
HELP = r"""
static void sg_itoa(int32_t v, char *o)
{
    char t[12]; int n = 0, neg = v < 0; uint32_t u = neg ? (uint32_t)(-v) : (uint32_t)v;
    do { t[n++] = (char)('0' + u % 10u); u /= 10u; } while (u);
    if (neg) *o++ = '-';
    while (n) *o++ = t[--n];
    *o = 0;
}
static void sg_scpy(char *o, const char *s) { uint32_t i = 0; while (s && s[i] && i < 23u) { o[i] = s[i]; i++; } o[i] = 0; }
"""


def make(g, name, title, licence, tu, incdirs, defines=(), fixes=(), pre_fixes=(), shared=()):
    g["TITLE"] = title
    g["LICENCE"] = licence
    g["TU"] = "#include <stdint.h>\n#include <string.h>\n#include <stddef.h>\n" + tu
    g["INCDIRS"] = list(incdirs)
    g["DEFINES"] = list(defines)
    g["PRE_FIXES"] = list(pre_fixes)
    g["FIXES"] = list(fixes)
    g["SHARED"] = tuple(shared)
    g["CXX"] = CXX % {"name": name, "title": title.replace('"', "'")}
