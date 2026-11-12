/* SPDX-License-Identifier: GPL-3.0-only
 * Copyright (C) 2026 GHOULBOX (on Felucca by Leo Kuroshita, Hügelton Instruments)
 * Grimoire changes: Copyright (C) 2026 Gonzi Audio */
/* GURDY: GHOULBOX's hurdy-gurdy (jasonpersinger/ghoulbox-fm1-dungeon-synth firmware/src/eng_gurdy.c gurdy_render),
 * moved onto GR!MM's engine interface (voices.c) for Grimoire. One wheel bows every string at once: the layer that plays
 * it is monophonic (Grimoire.cpp: legato, the drones never restart) and its drones sound under every note:
 *   the chanterelle  the melody: a band-limited saw, darker with less wheel pressure (WHEEL), the rosin's noise
 *   the bourdon      a drone on DRONE (C2 .. B2), DRONES loud
 *   the mouche       a drone a fifth above it, FIFTH loud
 *   the trompette    the bourdon an octave up on the buzzing bridge: BUZZ how hard; COUP the wrist's strokes that
 *                    kick it, HOLD (always) or 1/4 1/8 1/8T 1/16 of the host tempo from the note-on
 *   the body         two wooden resonances (~270 Hz box, ~1.1 kHz nose), BODY of them against the dry strings
 *   WOBBLE           the wheel's unevenness, a slow random wander of pitch and loudness.
 * Taken out for Grimoire (the MPC has its own ways): COUP CRANK (the FM-1's SELECT knob as a crank), RING (the
 * sequencer's rests), TEMP (GHOULBOX's temperaments: equal temperament here). The level: GR!MM's ENV, as every engine.
 * State: s[0..1] the box's SVF, s[2..3] the nose's; ph[0] chanterelle, ph[1] bourdon (x2: the trompette), ph[2]
 * mouche, ph[3] the noise, ph[4] the wander, ph[5] samples since the note-on, ph[6] the pressure low-pass. */
static const char *const GURDY_DRN_NAME[12] = {"C", "C#", "D", "D#", "E", "F", "F#", "G", "G#", "A", "A#", "B"};
static const char *const GURDY_COUP_NAME[5] = {"HOLD", "1/4", "1/8", "1/8T", "1/16"};
static volatile int32_t host_spb_q8;            /* Grimoire: the host's samples a quarter note (x 256; 0: 120 BPM) */
static uint32_t gurdy_div(uint32_t coup)        /* COUP's division in samples, from the host tempo */
{
    uint32_t q = host_spb_q8 > 0 ? (uint32_t)host_spb_q8 >> 8 : FS / 2u;   /* (a quarter note) */
    switch (coup) {
    case 1: return q;
    case 2: return q / 2u;
    case 3: return q / 3u;
    default: return q / 4u;
    }
}

static void gurdy_begin(voice_t *v)
{
    v->ph[5] = 0;                               /* the strokes start with the note */
    if (!v->ph[3])
        v->ph[3] = 0x5EED1234u + (uint32_t)(v - vc);
    v->s[0] = v->s[1] = v->s[2] = v->s[3] = 0;
    v->ph[4] = 0, v->ph[6] = 0;
}
static void gurdy_retrig(voice_t *v) { v->ph[5] = 0; }   /* a legato note: a new stroke, the wheel turning on */

static inline int32_t gurdy_bp(const tsvf_t *c, int32_t in, int32_t *ic1, int32_t *ic2)   /* the SVF's band-pass */
{
    int32_t v3 = in - *ic2;
    int32_t v1 = (c->a1 * *ic1 + c->a2 * v3) >> 13;
    int32_t v2 = *ic2 + ((c->a2 * *ic1 + c->a3 * v3) >> 13);
    *ic1 = clamp(2 * v1 - *ic1, -150000, 150000);
    *ic2 = clamp(2 * v2 - *ic2, -150000, 150000);
    return v1;
}

#define GURDY_LV 9000                           /* a voice's peak (Q15), near VA's */
static void gurdy_block(voice_t *v, const volatile int32_t *p, int32_t *out, int32_t amp0, int32_t amp1)
{
    const uint32_t n = CTL;
    int32_t drn = 36 + clamp(p[0], 0, 11), dlvl = p[1] * 200, flvl = p[2] * 170, whl = p[3];
    int32_t buzz = p[4] * 258, body = p[6] * 258, wobl = p[7];
    uint32_t coup = (uint32_t)clamp(p[5], 0, 4), i;
    int32_t fine = tune_f + wow_f;
    uint32_t inc = vinc(VP16(v)), dinc = cents_inc(drn * 16, 0, fine), finc = cents_inc((drn + 7) * 16, 0, fine);
    uint32_t ph0 = v->ph[0], ph1 = v->ph[1], ph2 = v->ph[2];
    int32_t b1 = v->s[0], b2 = v->s[1], n1 = v->s[2], n2 = v->s[3], nst = (int32_t)v->ph[3], wob = (int32_t)v->ph[4];
    int32_t cnt = (int32_t)v->ph[5], lp = (int32_t)v->ph[6];
    int32_t kp = 6000 + whl * 200, fric = whl * 24, e0, e1, de, e, w;
    int32_t a0 = amp_lv(amp0, GURDY_LV), damp = amp_lv(amp1, GURDY_LV) - a0;
    tsvf_t box, nose;
    /* the wheel's wander: a leaky random walk, once per block; pitch +-~12 ct and loudness +-~8 % at WOBBLE 127 */
    wob += (int32_t)(noise32(&nst) >> 22) - 512;
    wob -= wob >> 6;
    w = (wob * wobl) >> 10;
    inc += (uint32_t)((int32_t)(inc >> 12) * (w >> 10));
    /* the trompette's strokes: its envelope at this block's start and end, linear between */
    if (!buzz)
        e0 = e1 = 0;
    else if (!coup)
        e0 = e1 = 32767;
    else {
        uint32_t per = gurdy_div(coup), a = (uint32_t)cnt % per, fall = per * 2u / 5u + 1u;
        uint32_t kf = (32767u << 8) / fall;     /* each stroke falls over 2/5 of its division */
        e0 = a < fall ? 32767 - (int32_t)((a * kf) >> 8) : 0;
        a += n;
        e1 = a < fall ? 32767 - (int32_t)((a * kf) >> 8) : a >= per ? 32767 : 0;
    }
    de = (e1 - e0) / (int32_t)n;
    e = e0;
    tsvf_coef(&box, 44 << 8, 70);              /* ~270 Hz */
    tsvf_coef(&nose, 73 << 8, 90);             /* ~1.1 kHz */
    for (i = 0; i < n; i++) {
        int32_t s, x, r, tr, nz = (int32_t)(noise32(&nst) >> 17) - 16384;
        s = osc_saw(ph0, inc);                  /* the chanterelle, through the pressure low-pass */
        lp += mulq15(s - lp, kp);
        x = lp + mulq15(nz, fric);              /* .. and the rosin */
        x += mulq15(osc_saw(ph1, dinc), dlvl) + mulq15(osc_saw(ph2, finc), flvl);   /* the drones */
        if (e > 0) {                            /* the trompette: the dog chatters at each period's start */
            tr = osc_saw(ph1 << 1, dinc << 1);
            tr = clamp(tr * 3, -24000, 24000) + ((ph1 << 1) < 0x30000000u ? nz * 2 : 0);
            x += mulq15(mulq15(tr, e), buzz);
        }
        e += de;
        ph0 += inc;
        ph1 += dinc;
        ph2 += finc;
        x >>= 1;                                /* (four sources: the filters' range) */
        r = gurdy_bp(&box, x, &b1, &b2) + (gurdy_bp(&nose, x, &n1, &n2) >> 1);
        x += mulq15(r - x, body);
        x = soft_knee(x, 20000);
        out[i] += vca(x, amp_at(a0, damp, (int32_t)i)) << 1;
    }
    v->ph[0] = ph0, v->ph[1] = ph1, v->ph[2] = ph2;
    v->s[0] = b1, v->s[1] = b2, v->s[2] = n1, v->s[3] = n2;
    v->ph[3] = (uint32_t)nst, v->ph[4] = (uint32_t)wob, v->ph[5] = (uint32_t)(cnt + (int32_t)n), v->ph[6] = (uint32_t)lp;
}

static void gurdy_text(const volatile int32_t *p, uint32_t k, char *s)
{
    int32_t v = p[k];
    if (k == 0) { put_s(s, GURDY_DRN_NAME[clamp(v, 0, 11)]); return; }
    if (k == 5) { put_s(s, GURDY_COUP_NAME[clamp(v, 0, 4)]); return; }
    put_u(s, (uint32_t)clamp(v, 0, 127));
}

static const engine_t GURDY_ENGINE = {"GURDY", {"DRONE", "DRONES", "FIFTH", "WHEEL", "BUZZ", "COUP", "BODY", "WOBBLE"},
                                      {11, 127, 127, 127, 127, 4, 127, 127}, gurdy_begin, gurdy_block, gurdy_text};
