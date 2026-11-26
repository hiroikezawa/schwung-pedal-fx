/* Orange Distortion: DS-1-style distortion for Schwung (Ableton Move).
 *
 * SPDX-License-Identifier: MIT
 * Copyright (C) 2026 Hiro Ikezawa. Original work.
 *
 * Unofficial emulation; not affiliated with or endorsed by BOSS / Roland.
 * Written from the published circuit (component values as in ElectroSmash,
 * "Boss DS-1 Distortion Analysis") and first principles. guitarix's
 * bossds1.dsp (GPL-2.0+) was looked at for comparison only; none of its code
 * or generated coefficients were used (see THIRD_PARTY_LICENSES.md).
 *
 * Signal path (per channel):
 *
 *   in ─ input buffer HPF 7.2 Hz ─ transistor booster (shunt feedback through
 *   C3: high-pass ~310 Hz, ~33 dB above, inverting) ─ [2x: booster saturation (asymmetric: soft
 *   toward cut-off, harder into saturation) ─ coupling HPF 23 Hz ─ op-amp
 *   gain 1 + (Dist 100k || C7 100p) / (R13 4k7 + C8 0.47u) ─ op-amp swing
 *   limit ─ R14 2k2 into D4/D5 (1N4148 pair) || C10 10n: hard clipping
 *   with its 7.2 kHz RC] ─ passive tone network ─ Level ─ output HPF ─
 *   (Bass / Presence shelves: extensions)
 *
 * v0.2: the clipper node and the whole passive tone network are solved as
 * ONE circuit per sample (common/network.h), so the tone network loads the
 * diodes with its real, frequency- and Tone-dependent impedance (v0.1 used a
 * fixed 3k4, which misplaced the level and the onset of clipping for quiet
 * input). The output is flipped back to non-inverting so the Mix knob blends
 * in phase.
 *
 * Tone network (derived here by nodal analysis): Vs ─ R16 6k8 ─ A, C12 100n
 * A-gnd (LPF 234 Hz) ; Vs ─ C11 22n ─ B, R17 6k8 B-gnd (HPF 1063 Hz) ;
 * 20k pot A..B, wiper W into the output buffer (500k). Ra = A..W, Rb = W..B:
 *   H = [gA GA DB + gB C11 s DA] / [(gA+gB+GL) DA DB - gA^2 DB - gB^2 DA]
 *   DA = C12 s + GA + gA,  DB = C11 s + G17 + gB
 * which gives the DS-1's ~500 Hz scoop at noon and its big Tone sweep.
 */
#include <string.h>
#include "common/smoothing.h"
#include "common/filters.h"
#include "common/oversampling.h"
#include "common/nonlinear.h"
#include "common/network.h"
#include "common/tapers.h"
#include "pedal_meta.h"

#define PFX_K_INPUT PK_INPUT
#define PFX_K_MIX PK_MIX
#define PFX_K_OUTPUT PK_OUTPUT
#define PFX_DRY_DELAY 5 /* Mix alignment, tools/mix_align.sh: worst Mix-50% dip +5.8 dB */

#define DS_FS_OS (PFX_FS * PFX_OS)
/* booster */
/* Transistor booster Q2: common emitter (R8 10k / R9 22R, A ~ 56) with shunt
 * feedback R7 470k || C4 250p, fed through C3 47n from the buffer (~100R).
 * As an inverting amplifier of finite gain A:
 *     G = -A Zf / (Zf + (1 + A) Zs),  Zf = R7 || C4,  Zs = Rs + 1/(s C3)
 *       = -A R7 C3 s / ((1+A) + s [R7 C3 + (1+A)(Rs C3 + R7 C4)]
 *                       + s^2 (1+A) Rs C3 R7 C4)
 * i.e. a first-order HIGH-PASS at ~310 Hz with ~33 dB above it. v0.1-0.2
 * treated it as flat 35 dB from 33 Hz, so the whole low end went into the
 * clipping stages: muddy where the DS-1 is tight and bright. */
#define DS_BOOST_GAIN 56.0
#define DS_R7 470e3
#define DS_C4 250e-12
#define DS_C3 47e-9
#define DS_RSRC 100.0
#define DS_BOOST_UP 4.0f  /* toward Vcc: the collector resistor, soft */
#define DS_BOOST_DN 3.4f  /* into saturation: harder */
/* op-amp stage */
#define DS_DIST_POT 100000.0
#define DS_C7 100e-12
#define DS_R13 4700.0
#define DS_C8 0.47e-6
#define DS_RAIL 3.7f
/* clipper */
#define DS_R14 2200.0
#define DS_C10 0.01e-6
/* tone */
#define DS_R16 6800.0
#define DS_C12 0.1e-6
#define DS_C11 0.022e-6
#define DS_R17 6800.0
#define DS_TONE_POT 20000.0
#define DS_RLOAD 500000.0

#define DS_CAL 0.5013f /* defaults ~ unity on a -6 dBFS-peak pluck (tools/analyze cal) */

typedef struct {
    pfx_fo_s hp_in, hp_b1, hp_c, hp_out;
    pfx_bq_s boost;
    pfx_bq_s gain, bass, presence;
    pfx_net_s net;     /* clipper node + tone network */
    pfx_os_s os;
} ds_ch;

struct pedal {
    pfx_smooth dist, tone, level, bass, presence;
    pfx_fo_c hp_in_c, hp_b1_c, hp_c_c, hp_out_c;
    pfx_bq_c boost_c;
    pfx_bq_c gain_c, bass_c, presence_c;
    pfx_diodes d;
    pfx_net_c net;
    int dirty;
    pfx_ramp lvl_r;
    float lvl_next;
    ds_ch ch[2];
};

/* Clipper node D and the passive tone network, solved TOGETHER (v0.2): the
 * tone network's impedance -- which depends on frequency and on the Tone pot
 * -- is exactly what the diodes and C10 see, instead of a fixed 3k4.
 *   source (op-amp) ─ R14 2k2 ─ D ; D: C10 10n and D4/D5 to ground
 *   D ─ R16 6k8 ─ A, C12 100n A-gnd       (low-pass branch)
 *   D ─ C11 22n ─ B, R17 6k8 B-gnd        (high-pass branch)
 *   A ─ Ra ─ W ─ Rb ─ B (20k Tone pot), W ─ 500k (output buffer) ─ gnd
 * Output = W. Tone 0 puts the wiper at A (dark). */
enum { DN_D, DN_A, DN_B, DN_W, DN_COUNT };
static void ds_build_net(struct pedal *p, double t) {
    pfx_net_b b;
    pfx_netb_init(&b, DN_COUNT, DS_FS_OS);
    pfx_netb_r(&b, PFX_SRC, DN_D, DS_R14);
    pfx_netb_cap(&b, DN_D, PFX_GND, DS_C10);
    pfx_netb_r(&b, DN_D, DN_A, DS_R16);
    pfx_netb_cap(&b, DN_A, PFX_GND, DS_C12);
    pfx_netb_cap(&b, DN_D, DN_B, DS_C11);
    pfx_netb_r(&b, DN_B, PFX_GND, DS_R17);
    pfx_netb_r(&b, DN_A, DN_W, t * DS_TONE_POT);
    pfx_netb_r(&b, DN_W, DN_B, (1.0 - t) * DS_TONE_POT);
    pfx_netb_r(&b, DN_W, PFX_GND, DS_RLOAD);
    pfx_net_build(&p->net, &b, DN_D, &p->d);
}

static void ds_coefs(struct pedal *p) {
    /* G(s) = 1 + Zf/Zg, Zf = Rd || C7, Zg = R13 + 1/(s C8):
     *      = [(1 + s Rd C7)(1 + s R13 C8) + s Rd C8] / [(1 + s Rd C7)(1 + s R13 C8)] */
    const double rd = DS_DIST_POT * pfx_taper_log(p->dist.cur, 3.5f);
    const double t1 = rd * DS_C7, t2 = DS_R13 * DS_C8;
    pfx_blt2(&p->gain_c, t1 * t2, t1 + t2 + rd * DS_C8, 1.0, t1 * t2, t1 + t2, 1.0, pfx_blt_k(DS_FS_OS, 1000.0));
    ds_build_net(p, p->tone.cur);
    const float bd = pfx_db_knob(p->bass.cur, 10.0f), pd = pfx_db_knob(p->presence.cur, 9.0f);
    /* Always running: at 0 dB a shelf is an exact identity (no switch to click). */
    pfx_bq_lowshelf(&p->bass_c, 120.0, 0.6, bd, PFX_FS);
    pfx_bq_highshelf(&p->presence_c, 3500.0, 0.6, pd, PFX_FS);
}

static void pedal_reset(struct pedal *p) { memset(p->ch, 0, sizeof p->ch); }

static void pedal_set(struct pedal *p, int idx, float v) {
    switch (idx) {
    case PK_DISTORTION: pfx_sm_set(&p->dist, v); break;
    case PK_TONE: pfx_sm_set(&p->tone, v); break;
    case PK_LEVEL: pfx_sm_set(&p->level, v); break;
    case PK_BASS: pfx_sm_set(&p->bass, v); break;
    case PK_PRESENCE: pfx_sm_set(&p->presence, v); break;
    default: return;
    }
    p->dirty = 1;
}

static void pedal_snap(struct pedal *p) {
    pfx_sm_snap(&p->dist); pfx_sm_snap(&p->tone); pfx_sm_snap(&p->level);
    pfx_sm_snap(&p->bass); pfx_sm_snap(&p->presence);
    ds_coefs(p);
    p->lvl_next = pfx_level_gain(p->level.cur) * DS_CAL;
    pfx_ramp_end(&p->lvl_r, p->lvl_next);
    p->dirty = 0;
}

static void pedal_init(struct pedal *p) {
    pfx_sm_init(&p->dist, 0.5f, 5.0f);
    pfx_sm_init(&p->tone, 0.5f, 10.0f);
    pfx_sm_init(&p->level, 0.5f, 15.0f);
    pfx_sm_init(&p->bass, 0.5f, 10.0f);
    pfx_sm_init(&p->presence, 0.5f, 10.0f);
    p->d = pfx_diodes_si(1, 1, 1.0f);
    pfx_fo_hp(&p->hp_in_c, 7.2, PFX_FS);
    pfx_fo_hp(&p->hp_b1_c, 3.3, PFX_FS);
    {
        const double A = DS_BOOST_GAIN, k = 1.0 + A;
        pfx_blt2(&p->boost_c, 0.0, -A * DS_R7 * DS_C3, 0.0,
                 k * DS_RSRC * DS_C3 * DS_R7 * DS_C4,
                 DS_R7 * DS_C3 + k * (DS_RSRC * DS_C3 + DS_R7 * DS_C4), k, pfx_blt_k(PFX_FS, 1000.0));
    }
    pfx_fo_hp(&p->hp_c_c, 23.0, DS_FS_OS);
    pfx_fo_hp(&p->hp_out_c, 8.0, PFX_FS);
    pedal_reset(p);
    pedal_snap(p);
}

static void pedal_control(struct pedal *p) {
    const int moving = pfx_sm_moving(&p->dist) | pfx_sm_moving(&p->tone) |
                       pfx_sm_moving(&p->bass) | pfx_sm_moving(&p->presence);
    pfx_sm_step(&p->dist); pfx_sm_step(&p->tone); pfx_sm_step(&p->bass);
    pfx_sm_step(&p->presence); pfx_sm_step(&p->level);
    if (moving || p->dirty) ds_coefs(p);
    p->dirty = 0;
    p->lvl_next = pfx_level_gain(p->level.cur) * DS_CAL;
    pfx_ramp_to(&p->lvl_r, p->lvl_next, PFX_CTRL);
}

static inline float ds_sample(const struct pedal *p, ds_ch *c, float x, float lvl) {
    x = pfx_fo(&p->hp_in_c, &c->hp_in, x * PFX_V_IN);
    x = pfx_fo(&p->hp_b1_c, &c->hp_b1, x);
    x = pfx_bq(&p->boost_c, &c->boost, x); /* CE booster: inverting, high-passed */
    float up[PFX_OS], nv[PFX_NET_N];
    pfx_os_up(&c->os, x, up);
    for (int j = 0; j < PFX_OS; ++j) {
        float v = pfx_rail(up[j], DS_BOOST_UP, DS_BOOST_DN, 0.35f);
        v = pfx_fo(&p->hp_c_c, &c->hp_c, v);
        v = pfx_rail(pfx_bq(&p->gain_c, &c->gain, v), DS_RAIL, DS_RAIL, 0.75f);
        pfx_net_run_n(&p->net, &c->net, &p->d, v, nv, DN_COUNT, 3);
        up[j] = nv[DN_W];
    }
    /* The DS-1 inverts (its booster is a common-emitter stage). Flipped
     * back here so the Mix knob adds the dry signal in phase instead of
     * cancelling it; a lone pedal's absolute polarity is inaudible. */
    x = -pfx_os_down(&c->os, up);
    x = x * lvl;
    x = pfx_fo(&p->hp_out_c, &c->hp_out, x);
    x = pfx_bq(&p->bass_c, &c->bass, x);
    x = pfx_bq(&p->presence_c, &c->presence, x);
    return x;
}

static void pedal_run(struct pedal *p, float *l, float *r, int n) {
    const int mono = pfx_is_mono(l, r, n) && !memcmp(&p->ch[0], &p->ch[1], sizeof p->ch[0]);
    pfx_ramp lvl = p->lvl_r;
    if (mono) {
        for (int i = 0; i < n; ++i) l[i] = ds_sample(p, &p->ch[0], l[i], pfx_ramp_tick(&lvl));
        p->ch[1] = p->ch[0];
        memcpy(r, l, (size_t)n * sizeof *l);
    } else {
        for (int i = 0; i < n; ++i) {
            const float g = pfx_ramp_tick(&lvl);
            l[i] = ds_sample(p, &p->ch[0], l[i], g);
            r[i] = ds_sample(p, &p->ch[1], r[i], g);
        }
    }
    pfx_ramp_end(&p->lvl_r, p->lvl_next);
}

static void pedal_flush(struct pedal *p) {
    for (int ch = 0; ch < 2; ++ch) {
        ds_ch *c = &p->ch[ch];
        pfx_fo_s *f[4] = {&c->hp_in, &c->hp_b1, &c->hp_c, &c->hp_out};
        c->boost.z1 = pfx_flush(c->boost.z1); c->boost.z2 = pfx_flush(c->boost.z2);
        for (int k = 0; k < 4; ++k) f[k]->z = pfx_flush(f[k]->z);
        pfx_bq_s *b[3] = {&c->gain, &c->bass, &c->presence};
        for (int k = 0; k < 3; ++k) { b[k]->z1 = pfx_flush(b[k]->z1); b[k]->z2 = pfx_flush(b[k]->z2); }
        pfx_net_flush(&c->net);
        pfx_os_flush(&c->os);
    }
}

#include "common/plugin_v2.h"
