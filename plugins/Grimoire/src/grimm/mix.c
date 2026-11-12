/* SPDX-License-Identifier: GPL-3.0-only
 * GR!MM: Copyright (C) 2026 GR!MM (grimm.c: the mix, the track filter, the output stage, app_audio); Dinghy by zvenson
 * Grimoire changes: Copyright (C) 2026 Gonzi Audio */
/* Grimoire's sound: GR!MM's audio path (grimm.c app_audio and what it calls), taken over for the plugin with its two
 * layers as GR!MM's tracks. Left out: the CPU meter, the screen's peaks, mute and solo, the FX layer's tricks
 * (perform.c), the sequencer, arps, chords, GENERATE and the clock (the MPC does those). After it, Grimoire's own: the
 * notes from the host's MIDI (gm_*). */
static int32_t tbuf[NT][VB], dbus[VB], rbus[VB], gbus[VB], vbus[VB];   /* the tracks (mono); the DLY, REV, GRAIN and
                                                 * (V2) DRIVE buses */
static uint32_t xb_used;                        /* audio: bit 0 the DRIVE bus fed this block (a send up, or coming
                                                 * down) */
#include "ambience.c"                           /* (Grimoire) GHOULBOX's ambience */
#include "master.c"                             /* GR!MM V2: MASTER (a filter on the whole sound), DRIVE (and a limiter) */

/* a x b >> 15 for a track's sum (its voices, a chord: past 16 bits, up to ~2^20), b Q15: exact, no 32-bit overflow
 * (mulq15's a * b wraps there: a crackle) */
static inline int32_t mulq15w(int32_t a, int32_t b)
{
    return (a >> 15) * b + (int32_t)(((uint32_t)a & 0x7FFFu) * (uint32_t)b >> 15);
}
static int32_t mix_g[NT][6];                    /* audio: each track's gains (L, R, DLY, REV, GRAIN, (V2) DRIVE) as the
                                                 * last block ended: this block's ramps start there */
static void mix_add(int32_t *o, uint32_t st, const int32_t *x, int32_t g0, int32_t g1, uint32_t n)   /* o[i st] +=
                                                 * x[i] times the gain, from g0 to g1 over the block (no zipper) */
{
    uint32_t i;
    int32_t g, dg;
    if (g0 == g1) {
        if (g1)
            for (i = 0; i < n; i++) o[i * st] += mulq15w(x[i], g1);
        return;
    }
    g = g0 << 8, dg = ((g1 - g0) << 8) / (int32_t)n;   /* (8 bits more: small steps) */
    for (i = 0; i < n; i++) {
        g += dg;
        o[i * st] += mulq15w(x[i], g >> 8);
    }
}
static void mix_add5(int32_t *out, const int32_t *x, const int32_t *g0, const int32_t *g1, uint32_t n)
{                                               /* x into out (L, R), the DLY, REV and GRAIN buses, each its gain from g0
                                                 * to g1: GR!MM V2, the five in one pass when all five sound (each sum as
                                                 * mix_add makes it: the same), else mix_add each */
    int32_t g[5], dg[5];
    uint32_t i, k;
    for (k = 0; k < 5u; k++)
        if (!g0[k] && !g1[k]) break;
    if (k < 5u) {
        mix_add(out, 2u, x, g0[0], g1[0], n);
        mix_add(out + 1, 2u, x, g0[1], g1[1], n);
        mix_add(dbus, 1u, x, g0[2], g1[2], n);
        mix_add(rbus, 1u, x, g0[3], g1[3], n);
        mix_add(gbus, 1u, x, g0[4], g1[4], n);
        return;
    }
    for (k = 0; k < 5u; k++)                    /* (as mix_add: constant, g >> 8 is g1; a ramp, its steps) */
        g[k] = g0[k] << 8, dg[k] = g0[k] == g1[k] ? 0 : ((g1[k] - g0[k]) << 8) / (int32_t)n;
    for (i = 0; i < n; i++) {
        int32_t v = x[i];
        g[0] += dg[0], g[1] += dg[1], g[2] += dg[2], g[3] += dg[3], g[4] += dg[4];
        out[2u * i] += mulq15w(v, g[0] >> 8);
        out[2u * i + 1u] += mulq15w(v, g[1] >> 8);
        dbus[i] += mulq15w(v, g[2] >> 8);
        rbus[i] += mulq15w(v, g[3] >> 8);
        gbus[i] += mulq15w(v, g[4] >> 8);
    }
}
/* the track filter (TRACK: CUTOFF, RESONANCE; the LFO's TRK CUTOFF): a low-pass after the track's voices and ENV, before
 * LEVEL, PAN and the sends. dsp.c's SVF (as VA's) at a quarter of the track's scale (a chord's sum: up to ~2^20), the
 * cutoff gliding to where it is turned (~6 ms) with its coefficients every CTL frames, RESONANCE up to a Q of ~2.5, a
 * soft knee after it: no zipper, nothing running away. Open (CUTOFF at the top, the LFO not moving it): bypassed.
 * GR!MM V2: MIXER 2's TYPE: LP (as V1.0, the same code), HP (open, bypassed: CUTOFF at the bottom), BP (never open), the
 * same SVF's outputs (dsp.c tsvf_any); POS: PRE (as V1.0: the sends hear it filtered) or POST (mix_tracks: the sends
 * hear the track before it, only its dry sound filtered) */
static int32_t tf_cs[NT] = {127 << 8, 127 << 8}, tf_ic[NT][4];   /* audio: the cutoff now (<< 8),
                                                 * the states (V2: and what their steps rounded away, tf_svf) */
static uint8_t tf_on[NT];
static int32_t tf_px[NT];                       /* audio: POS's share of the sends taken before the filter, 0 (PRE) ..
                                                 * TF_PX1 (POST): a switch crosses over in 8 blocks (~46 ms), no click */
#define TF_PX1 256
static int tf_open(uint32_t t)                   /* the track filter bypassed now (its cutoff where its TYPE opens) */
{
    int32_t tgt = lfo_move(t, LD_CUT, clamp(trks[t].cut, 0, 127), 127) << 8, cs = tf_cs[t];
    uint32_t ty = (uint32_t)clamp(trks[t].ftype, 0, NFTYPE - 1);
    return ty == FT_LP ? tgt >= 127 << 8 && cs >= (127 << 8) - 64 : ty == FT_HP && tgt <= 0 && cs <= 64;
}
/* (V2) tsvf_any as dsp.c's tsvf_r: what its steps round away carried on (a fading note no longer frozen into a
 * staircase, then a DC offset till the voice ended, and a click) */
static inline int32_t tf_svf(const tsvf_t *c, int32_t k, uint32_t type, int32_t in, int32_t *ic1, int32_t *ic2,
                             int32_t *r1, int32_t *r2)
{
    int32_t v1, v2 = tsvf_r(c, in, ic1, ic2, r1, r2, &v1);
    return type == FT_LP ? v2 : type == FT_HP ? in - ((k * v1) >> 12) - v2 : (k * v1) >> 12;
}
#define TF_IN(s) (((s) + (1 << (1 + PB))) >> (2 + PB))   /* (V2) the SVF's input, rounded: Q15 << PB to Q15 >> 2 */
static void trk_filter(uint32_t t, int32_t *x, uint32_t n)
{
    int32_t tgt = lfo_move(t, LD_CUT, clamp(trks[t].cut, 0, 127), 127) << 8, cs = tf_cs[t], ic1, ic2, r1, r2;
    int32_t r = clamp(trks[t].res, 0, 127) * 7 / 8;
    uint32_t ty = (uint32_t)clamp(trks[t].ftype, 0, NFTYPE - 1), i, j;
    tsvf_t c;
    if (tf_open(t)) {                           /* (open: as it is) */
        tf_cs[t] = ty == FT_LP ? 127 << 8 : 0, tf_on[t] = 0;
        return;
    }
    if (!tf_on[t]) tf_on[t] = 1, tf_ic[t][0] = 0, tf_ic[t][1] = ty == FT_LP ? TF_IN(x[0]) : 0, tf_ic[t][2] = tf_ic[t][3] = 0;
                                                /* (from bypassed: no step; V2: x Q15 << PB, the SVF at Q15 >> 2 as
                                                 * ever) */
    ic1 = tf_ic[t][0], ic2 = tf_ic[t][1], r1 = tf_ic[t][2], r2 = tf_ic[t][3];
    if (ty == FT_LP) {
        for (i = 0; i < n; i += CTL) {
            cs += (tgt - cs) >> 3;
            tsvf_coef(&c, cs, r);
            for (j = i; j < i + CTL && j < n; j++)
                x[j] = soft_knee(tf_svf(&c, 0, FT_LP, TF_IN(x[j]), &ic1, &ic2, &r1, &r2), 100000) << (2 + PB);
        }
    } else {                                    /* (V2: HP, BP) */
        int32_t k = 8192 - r * 7600 / 127;      /* (tsvf_coef's damping) */
        for (i = 0; i < n; i += CTL) {
            cs += (tgt - cs) >> 3;
            tsvf_coef_k(&c, cs, k);
            for (j = i; j < i + CTL && j < n; j++)
                x[j] = soft_knee(tf_svf(&c, k, ty, TF_IN(x[j]), &ic1, &ic2, &r1, &r2), 100000) << (2 + PB);
        }
    }
    tf_cs[t] = cs, tf_ic[t][0] = ic1, tf_ic[t][1] = ic2, tf_ic[t][2] = r1, tf_ic[t][3] = r2;
}
/* (rc9) what HOME says on a track's slab: its filter as good as closed (LP at 66 Hz or under, HP at 8 kHz or over; BP
 * at either end: hardly anything gets through), its FX 0 with a send up (no DELAY, REVERB, GRAIN, TAPE, DRIVE) */
static int32_t fx_send(uint32_t t, int32_t s)   /* a send (0..127) times FX (0..200 %): x 258 its Q15 gain, to ~2x */
{
    return s * 258 * clamp(trks[t].fx, 0, 200) / 100;
}
static void mix_xb(uint32_t t, const int32_t *x, const int32_t *g, int32_t a, int32_t b, uint32_t n)   /* (V2) x into
                                                 * the DRIVE send's bus, its gain x a, x b / TF_PX1 (POS's share) */
{
    if (g[5] | mix_g[t][5])
        mix_add(vbus, 1u, x, (mix_g[t][5] * a) >> 8, (g[5] * b) >> 8, n);
}
static void mix_tracks(int32_t *out, uint32_t used, uint32_t n)   /* the sounding tracks into out and the buses (LEVEL,
                                                 * PAN and the sends as the LFO moves them, lfo.c; the sends times
                                                 * TRACK's FX); (V2) each first through TAPE as far as its tile is up */
{
    uint32_t t, k;
    for (t = 0; t < NT; t++) {
        int32_t *x = tbuf[t];
        tape_trk(t, used >> t & 1u ? x : 0, n);
        int32_t lv = lfo_move(t, LD_VOLUME, clamp(trks[t].level, 0, 127), 127) * 258;   /* (a
                                                 * silent track: all its gains down, ramped) */
        int32_t pan = lfo_move(t, LD_PAN, clamp(trks[t].pan, 0, 127), 127), g[6];
        g[0] = mulq15(lv, pan <= 64 ? 32767 : (127 - pan) * 520);   /* PAN: the far side down, the near one full */
        g[1] = mulq15(lv, pan >= 64 ? 32767 : pan * 512);
        g[2] = mulq15(lv, fx_send(t, clamp(trks[t].dly, 0, 127)));   /* the sends:
                                                 * after LEVEL (ECHO THROW, REV THROW held: full) */
        g[3] = mulq15(lv, fx_send(t, lfo_move(t, LD_REV, clamp(trks[t].rev, 0, 127), 127)));
        g[4] = mulq15(lv, fx_send(t, lfo_move(t, LD_GRAIN, clamp(trks[t].grn, 0, 127), 127)));
        g[5] = trks[t].drv > 0 ? mulq15(lv, fx_send(t, clamp(trks[t].drv, 0, 127))) : 0;   /* (V2: DRIVE's send; 0:
                                                 * none, nothing done) */
        if ((used >> t & 1u) && (g[5] | mix_g[t][5]) && !(xb_used & 1u)) {   /* (the bus cleared as it is first fed
                                                 * this block) */
            int32_t i;
            for (i = 0; i < (int32_t)n; i++) vbus[i] = 0;
            xb_used |= 1u;
        }
        int32_t px0 = tf_px[t], pw = trks[t].fpos ? TF_PX1 : 0;
        int32_t px1 = !(used >> t & 1u) ? pw : px0 < pw ? (px0 + 32 < pw ? px0 + 32 : pw) : (px0 - 32 > pw ? px0 - 32 : pw);
        tf_px[t] = px1;                         /* (V2: POS; silent: at once) */
        if ((used >> t & 1u) && (px0 | px1) && !tf_open(t)) {   /* (POST, or crossing over: the sends' share
                                                 * before the filter, the rest after it) */
            int32_t *bus[3] = {dbus, rbus, gbus};
            if (px0 == TF_PX1 && px1 == TF_PX1) {
                for (k = 0; k < 3u; k++) mix_add(bus[k], 1u, x, mix_g[t][2u + k], g[2u + k], n);
                mix_xb(t, x, g, TF_PX1, TF_PX1, n);
                trk_filter(t, x, n);
            } else {
                for (k = 0; k < 3u; k++)
                    mix_add(bus[k], 1u, x, (mix_g[t][2u + k] * px0) >> 8, (g[2u + k] * px1) >> 8, n);
                mix_xb(t, x, g, px0, px1, n);
                trk_filter(t, x, n);
                for (k = 0; k < 3u; k++)
                    mix_add(bus[k], 1u, x, (mix_g[t][2u + k] * (TF_PX1 - px0)) >> 8, (g[2u + k] * (TF_PX1 - px1)) >> 8, n);
                mix_xb(t, x, g, TF_PX1 - px0, TF_PX1 - px1, n);
            }
            mix_add(out, 2u, x, mix_g[t][0], g[0], n);
            mix_add(out + 1, 2u, x, mix_g[t][1], g[1], n);
        } else {
            if (used >> t & 1u) trk_filter(t, x, n);
            else tf_on[t] = 0;                  /* (silent: from bypassed when it sounds again) */
            if (used >> t & 1u) {
                    mix_add5(out, x, mix_g[t], g, n);
                mix_xb(t, x, g, TF_PX1, TF_PX1, n);   /* (V2: DRIVE's send) */
            }
        }
        for (k = 0; k < 6u; k++) mix_g[t][k] = g[k];
    }
}

/* (V2) the output: 6 dB of headroom after every effect (the mix at half: six loud voices, four tracks fit), MASTER
 * (platform.c's knob, squared; its top quarter up to 6 dB more: at the top, the level before V2), then a limiter: a
 * peak past OUT_LIM (just under full scale) at once, let go over ~90 ms: nothing reaches the clip. Out: the DAC's 24-bit
 * words (Q15 << 7, platform.c OUT_FS): MASTER and the headroom lose no resolution; USB a 16-bit copy (out_usb16) */
#define OUT_LIM 32440                           /* (-0.09 dBFS, Q15; the limiter works on it << 7) */
#define OUT_MQ 2304                             /* (MASTER at 3/4: 768^2 / 256) */
static int32_t out_env;                         /* audio: the limiter's peak (its gain: OUT_LIM / it) */
static volatile int32_t gm_master = 4096;        /* (Grimoire) MASTER: Q12, from the plugin's Master (dB) */
static int32_t out_gain(void) { return clamp(gm_master, 0, 16384); }
static void out_stage(int32_t *out, uint32_t n)
{
    int32_t g = out_gain(), env = out_env;
    uint32_t i;
    for (i = 0; i < n; i++) {
        int32_t l = (int32_t)(((int64_t)clamp(out[2u * i], -(500000 << PB), 500000 << PB) * g) >> (12 + PB - 7));   /* (Q15
                                                 * << PB x Q12 -> the 24-bit word, Q15 << 7) */
        int32_t r = (int32_t)(((int64_t)clamp(out[2u * i + 1u], -(500000 << PB), 500000 << PB) * g) >> (12 + PB - 7));
        int32_t a = l < 0 ? -l : l, b = r < 0 ? -r : r;
        a = a > b ? a : b;
        env = a > env - (env >> 12) ? a : env - (env >> 12);   /* (let go, never under this sample) */
        if (env > OUT_LIM << 7) {
            int32_t k = (int32_t)(((uint32_t)OUT_LIM << 15) / (uint32_t)((env + 127) >> 7));   /* (Q15; the peak rounded
                                                 * up: never past the ceiling) */
            l = (int32_t)(((int64_t)l * k) >> 15), r = (int32_t)(((int64_t)r * k) >> 15);
        }
        out[2u * i] = l, out[2u * i + 1u] = r;
    }
    out_env = env;
}

static int32_t fxo[2u * VB] __attribute__((section(".pool")));   /* (V2) the effects' returns, Q15, before they join
                                                 * the mix (Q15 << PB) */
static void q_down(int32_t *x, uint32_t n)      /* (V2) n samples Q15 << PB -> Q15 (rounded): an effect's range */
{
    uint32_t i;
    for (i = 0; i < n; i++) x[i] = (x[i] + (1 << (PB - 1))) >> PB;
}
static void q_up(int32_t *x, uint32_t n)        /* .. and back (an effect's work at Q15: its own resolution) */
{
    uint32_t i;
    for (i = 0; i < n; i++) x[i] <<= PB;
}
static void fx_zero(uint32_t n) { uint32_t i; for (i = 0; i < 2u * n; i++) fxo[i] = 0; }
static void fx_join(int32_t *out, uint32_t n)   /* the returns (Q15) into the mix (Q15 << PB) */
{
    uint32_t i;
    for (i = 0; i < 2u * n; i++) out[i] += fxo[i] << PB;
}
static void mf_at(int32_t *out, uint32_t n, uint32_t at)   /* MASTER's filter at at: at Q15 while it works */
{
    if (!master_here(at)) return;
    q_down(out, 2u * n);
    master_filter(out, n, at);
    q_up(out, 2u * n);
}
static void app_audio(int32_t *out, uint32_t n)
{
    uint32_t used, i, lo;
    if (n > VB) n = VB;
    for (i = 0; i < 2u * n; i++) out[i] = 0;
    tape_begin();                               /* (V2: TAPE: how far each track goes through it; WOW, the warbles:
                                                 * the bend for the voices) */
    used = voices_render(tbuf, n);
    for (i = 0; i < n; i++)
        dbus[i] = rbus[i] = gbus[i] = 0;
    xb_used = 0;
    mix_tracks(out, used, n);                   /* (V2: the mix and the buses Q15 << PB; the buses to the effects'
                                                 * range, their returns (fxo) back up into the mix) */
    q_down(dbus, n), q_down(rbus, n), q_down(gbus, n);
    amb_block(out, rbus, n);                    /* (Grimoire) GHOULBOX's ambience: the cave's wind, the torch's crackle */
    if (xb_used & 1u) {                         /* (V2) the tracks' DRIVE sends: their bus driven, back into the mix */
        q_down(vbus, n);
        fx_zero(n);
        drive_send(fxo, n);
        fx_join(out, n);
    }
    mf_at(out, n, MP_PRE);                      /* (V2) MASTER, PRE FX: the tracks' dry mix */
    fx_zero(n);
    grain_render(gbus, fxo, rbus, n);           /* GRAIN: the GRAIN bus in; the cloud out, and into the REV bus */
    tape_render(dbus, fxo, rbus, n);            /* DELAY: the DLY bus in; the echoes out, and into the REV bus */
    hall_render(rbus, fxo, n);                  /* SPACE: the REV bus in, the reverb out */
    fx_join(out, n);
    lo = master_here(MP_POST) || dr_amt > 0 || (tf.hl | tf.hn) != 0 || master_here(MP_END);
    if (lo) q_down(out, 2u * n);                /* (V2: MASTER's filter, DRIVE, TAPE's HISS work at Q15) */
    master_filter(out, n, MP_POST);             /* (V2) MASTER, POST REVERB */
    drive_render(out, n);                       /* (V2) DRIVE and its limiter, */
    tapefx_render(out, n);                      /* TAPE's HISS (V2: the tracks through it: mix_tracks), */
    master_filter(out, n, MP_END);              /* (V2) MASTER, END: the very last */
    if (lo) q_up(out, 2u * n);
    out_stage(out, n);                          /* (V2) the headroom, the MASTER knob, the limiter */
}

/* ---- Grimoire's notes: the host's MIDI into GR!MM's voices (voices.c voice_start / voice_release, GR!MM's way: a
 * request a voice takes at its next control step). Every layer that sounds (LEVEL above 0) plays each note, a voice a
 * layer; POLY voices at most a layer (the 16 shared), PHYS at most PHYS_MAX at once (GR!MM: 4 on the FM-1; its strings
 * are the heaviest). A new note past the limit takes the layer's quietest release, else its oldest note (it fades,
 * ~10 ms, as GR!MM's). A layer in MONO (and GURDY, a hurdy-gurdy has one wheel) has one voice: notes played over it
 * slide there (GLIDE) without a new attack, and letting go goes back to the last key still held. The sustain pedal
 * (CC 64) holds released notes; the mod wheel (CC 1) adds LFO depth; pitch bend +-BEND semitones. The events come in
 * with their time (gm_now's frames) and are taken at the control step they fall in (0.7 ms). */
#define PHYS_MAX 6u
#define GM_EVQ 512u
enum { GE_ON = 1, GE_OFF, GE_ALLOFF, GE_SUS, GE_BEND, GE_MOD, GE_PANIC };
typedef struct { uint32_t at; uint8_t kind, a, b, pad; } gm_ev_t;
static gm_ev_t gm_q[GM_EVQ];
static uint32_t gm_qw, gm_qr;                   /* written by gm_event (the plugin's thread), read by ctl_hook: one thread
                                                 * (the plugin calls both from process) */
static uint32_t gm_now;                         /* frames rendered so far, at the control step running */
static uint8_t vc_key[NV], vc_held[NV], vc_sus[NV];   /* a voice's MIDI key (255: none); its key down; held by the pedal */
static uint32_t vc_age[NV], gm_age;
static uint8_t gm_pedal, gm_mod;                /* CC 64 down; CC 1 (0..127) */
static int32_t gm_bend;                         /* -8192..8191 */
/* the plugin's settings (written before each render) */
static volatile int32_t gm_poly = 8, gm_velsens = 64, gm_bendr = 2;
static volatile int32_t gm_mono[NT], gm_on[NT], gm_tr[NT], gm_fine16[NT];   /* MONO; sounds; transpose (semitones);
                                                 * fine (1/16 semitones) */
static int32_t gm_mlfo[NT];                     /* (the LFO DEPTH as set, the mod wheel adds to it) */
static uint8_t gm_stack[NT][16], gm_nst[NT];    /* MONO: the keys held, the last on top */
static int16_t gm_last[NT] = {-1, -1};          /* GLIDE: a layer's last note (1/16 semitones) */

static int gm_layer_mono(uint32_t t) { return gm_mono[t] || trks[t].eng == ENG_GURDY; }
static uint32_t gm_layer_count(uint32_t t, int phys)
{
    uint32_t v, n = 0;
    for (v = 0; v < NV; v++)
        if (!voice_idle(v) && vc[v].stage != ST_FADE && (vc[v].req != vc[v].seen ? vc[v].want_trk : vc[v].trk) == t &&
            (!phys || vc[v].eng == ENG_PHYS || (vc[v].req != vc[v].seen && trks[t].eng == ENG_PHYS)))
            n++;
    return n;
}
static int gm_pick(uint32_t t, uint32_t note, uint32_t *out)
{
    uint32_t v, best = NV, old = NV;
    int32_t lv = 0x7FFFFFFF;
    int full = gm_layer_count(t, 0) >= (uint32_t)clamp(gm_poly, 1, (int32_t)NV) ||
               (trks[t].eng == ENG_PHYS && gm_layer_count(t, 1) >= PHYS_MAX);
    if (!full) {
        for (v = 0; v < NV; v++)                /* its own release of this note: it again */
            if (!voice_idle(v) && vc[v].trk == t && vc[v].note == note && vc[v].stage == ST_REL) { *out = v; return 1; }
        for (v = 0; v < NV; v++)
            if (voice_idle(v)) { *out = v; return 1; }
    }
    for (v = 0; v < NV; v++) {                  /* the layer's quietest release, else its oldest note */
        if (voice_idle(v) || vc[v].trk != t) continue;
        if (vc[v].stage == ST_REL && voice_level(v) < lv) lv = voice_level(v), best = v;
        if (old == NV || vc_age[v] < vc_age[old]) old = v;
    }
    if (best == NV) best = old;
    if (best == NV)                             /* none of its own (the other layer holds them all): the quietest of all */
        for (v = 0; v < NV; v++)
            if (best == NV || voice_level(v) < voice_level(best)) best = v;
    *out = best;
    return best < NV;
}
static int gm_vel(uint32_t vel)                 /* GR!MM V2's VELOCITY ON (gen_vel): (v / 127)^2 at VEL SENS 100 % */
{
    int32_t s = clamp(gm_velsens, 0, 127), q = (int32_t)(vel * vel * 255u / 16129u), x;
    if (!s) return 0;
    x = 255 - ((255 - q) * s) / 127;
    return clamp(x, 1, 255);
}
static uint32_t gm_mono_voice(uint32_t t)       /* MONO: the layer's voice now sounding (NV: none) */
{
    uint32_t v;
    for (v = 0; v < NV; v++)
        if (!voice_idle(v) && vc[v].stage != ST_FADE && vc[v].stage != ST_REL && vc[v].trk == t && vc_key[v] != 255u)
            return v;
    return NV;
}
static void gm_glide_to(uint32_t v, uint32_t t, uint32_t note)   /* MONO: voice v slides to note, no new attack */
{
    int32_t from = (int32_t)vc[v].note * 16 + (vc_gl[v] >> 8);
    vc_gl[v] = trks[t].glide > 0 ? (from - (int32_t)note * 16) * 256 : 0;
    vc[v].note = vc[v].want_note = (uint8_t)note;
    if (vc[v].eng == ENG_GURDY) gurdy_retrig(&vc[v]);
}
static void gm_note_on(uint32_t key, uint32_t vel)
{
    uint32_t t, v;
    for (t = 0; t < NT; t++) {
        int32_t n = (int32_t)key + gm_tr[t];
        if (!gm_on[t] || n < 0 || n > 127) continue;
        if (gm_layer_mono(t)) {
            uint32_t k;
            for (k = 0; k < gm_nst[t] && gm_stack[t][k] != key; k++)
                ;
            if (k < gm_nst[t]) memmove(&gm_stack[t][k], &gm_stack[t][k + 1], gm_nst[t] - k - 1u), gm_nst[t]--;
            if (gm_nst[t] == 16u) memmove(&gm_stack[t][0], &gm_stack[t][1], 15u), gm_nst[t]--;
            gm_stack[t][gm_nst[t]++] = (uint8_t)key;
            if ((v = gm_mono_voice(t)) < NV) {  /* legato */
                gm_glide_to(v, t, (uint32_t)n);
                vc_key[v] = (uint8_t)key, vc_held[v] = 1, vc_sus[v] = 0, vc_age[v] = ++gm_age;
                gm_last[t] = (int16_t)(n * 16);
                continue;
            }
        }
        if (!gm_pick(t, (uint32_t)n, &v)) continue;
        gl_want = trks[t].glide > 0 && gm_last[t] >= 0 ? (gm_last[t] - n * 16) * 256 : 0;
        gm_last[t] = (int16_t)(n * 16);
        gen_vel = (uint8_t)gm_vel(vel);
        voice_start(v, t, (uint32_t)n);
        gen_vel = 0, gl_want = 0;
        vc_key[v] = (uint8_t)key, vc_held[v] = 1, vc_sus[v] = 0, vc_age[v] = ++gm_age;
    }
}
static void gm_note_off(uint32_t key)
{
    uint32_t t, v, k;
    for (t = 0; t < NT; t++) {                  /* MONO: back to the last key still held */
        if (!gm_layer_mono(t)) continue;
        for (k = 0; k < gm_nst[t] && gm_stack[t][k] != key; k++)
            ;
        if (k == gm_nst[t]) continue;
        memmove(&gm_stack[t][k], &gm_stack[t][k + 1], gm_nst[t] - k - 1u), gm_nst[t]--;
        if (gm_nst[t] && (v = gm_mono_voice(t)) < NV && vc_key[v] == key) {
            uint32_t back = gm_stack[t][gm_nst[t] - 1u];
            int32_t n = (int32_t)back + gm_tr[t];
            if (n >= 0 && n <= 127) {
                gm_glide_to(v, t, (uint32_t)n);
                vc_key[v] = (uint8_t)back;
                gm_last[t] = (int16_t)(n * 16);
            }
        }
    }
    for (v = 0; v < NV; v++) {
        if (vc_key[v] != key || !vc_held[v]) continue;
        vc_held[v] = 0;
        if (gm_pedal) vc_sus[v] = 1;
        else voice_release(v), vc_key[v] = 255;
    }
}
static void gm_all_off(int hard)
{
    uint32_t v, t;
    for (v = 0; v < NV; v++) {
        if (hard) voice_fade(v);
        else if (vc_key[v] != 255u) voice_release(v);
        vc_key[v] = 255, vc_held[v] = vc_sus[v] = 0;
    }
    for (t = 0; t < NT; t++) gm_nst[t] = 0;
    gm_pedal = 0;
}
static void gm_take(const gm_ev_t *e)
{
    uint32_t v;
    switch (e->kind) {
    case GE_ON: gm_note_on(e->a, e->b); break;
    case GE_OFF: gm_note_off(e->a); break;
    case GE_ALLOFF: gm_all_off(0); break;
    case GE_PANIC: gm_all_off(1); break;
    case GE_SUS:
        gm_pedal = e->b >= 64;
        if (!gm_pedal)
            for (v = 0; v < NV; v++)
                if (vc_sus[v]) vc_sus[v] = 0, voice_release(v), vc_key[v] = 255;
        break;
    case GE_BEND: gm_bend = (int32_t)(e->a | e->b << 7) - 8192; break;
    case GE_MOD: gm_mod = e->b; break;
    default: break;
    }
}
static void gm_event(uint32_t at, uint32_t kind, uint32_t a, uint32_t b)   /* the plugin: an event at frame at */
{
    gm_ev_t *e;
    if (gm_qw - gm_qr >= GM_EVQ) return;        /* (full: dropped) */
    e = &gm_q[gm_qw % GM_EVQ];
    e->at = at, e->kind = (uint8_t)kind, e->a = (uint8_t)a, e->b = (uint8_t)b;
    gm_qw++;
}
static void ctl_hook(void)                      /* voices.c, every control step before the voices: the events due, the
                                                 * LFO, the bend and the fine tune on each layer's pitch */
{
    uint32_t t;
    while (gm_qr != gm_qw && (int32_t)(gm_q[gm_qr % GM_EVQ].at - (gm_now + CTL)) < 0) {
        gm_take(&gm_q[gm_qr % GM_EVQ]);
        gm_qr++;
    }
    for (t = 0; t < NT; t++)                    /* (the mod wheel: more LFO depth) */
        trks[t].lfo_depth = clamp(gm_mlfo[t] + (int32_t)gm_mod * (100 - gm_mlfo[t]) / 127, 0, 100);
    lfo_ctl();
    for (t = 0; t < NT; t++)
        lfo_p16[t] += gm_fine16[t] + gm_bend * clamp(gm_bendr, 0, 24) * 16 / 8192;
    gm_now += CTL;
}
static void gm_init(void)
{
    uint32_t v;
    for (v = 0; v < NV; v++) vc_key[v] = 255;
}
