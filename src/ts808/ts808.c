/* Green Screamer: TS808-style overdrive for Schwung (Ableton Move).
 *
 * SPDX-License-Identifier: MIT
 * Copyright (C) 2026 Hiro Ikezawa. Original work.
 *
 * Unofficial emulation; not affiliated with or endorsed by Ibanez / Hoshino
 * Gakki or Maxon. Written from the published circuit (component values as in
 * ElectroSmash, "Tube Screamer Analysis") and from first principles; no code
 * was copied from TS-808-Ultra (GPL-3.0) or guitarix ts9sim (GPL-2.0+), which
 * were read for comparison only (see THIRD_PARTY_LICENSES.md).
 *
 * Signal path (per channel):
 *
 *   in ─ input HPF ─ [2x oversampled clipping amplifier] ─ tone amplifier ─
 *   Level ─ output coupling HPF ─ (Bass shelf, Presence shelf: extensions)
 *
 * Clipping amplifier (non-inverting op-amp, ideal: V- = V+ = Vin):
 *   The gain leg R4 4k7 + C3 47n to AC ground draws  i = Vin sC3/(1+sR4C3):
 *   below ~720 Hz little current flows, so little gain -> the TS bass cut
 *   and mid hump. That current flows through the feedback network
 *   (R6 51k + Drive 500k A) || C4 51p || two 1N4148-class diodes, so the
 *   feedback voltage v solves
 *        C4 dv/dt + v / Rf + Id(v) = i
 *   (trapezoidal companion + closed-form diode solve, common/nonlinear.h).
 *   Output = Vin + v: the clean signal plus a soft-clipped, mid-heavy
 *   distortion component that can never exceed the diode drop, which is why
 *   a Tube Screamer stays "overdrive". Diodes are matched to 20%, a real
 *   pair's tolerance, which yields the faint even harmonics of the pedal.
 *
 * Tone amplifier (derived here by nodal analysis):
 *   Vin ─ Rs 1k ─ P ; P: Ri 10k and Cs 220n to ground ; op-amp + at P ;
 *   Rf 1k from out to - (N) ; 20k tone pot from P to N, wiper W to ground via
 *   Rz 220 + Cz 220n. With Ra = P..W, Rb = W..N, g = 1/Ra + 1/Rb:
 *      H(s) = Gs (g + s Cz (g Rz + 1 + Rf/Rb))
 *             / ( (Gs+Gi) g + s [(Gs+Gi) Cz (g Rz + 1) + Cs g + Cz/Ra]
 *                 + s^2 Cs Cz (g Rz + 1) )
 *   Wiper at P (Ra = 0) shunts the signal: dark. Wiper at N (Rb = 0) shunts
 *   the feedback: treble boost. Pot taper: S-curve ("G").
 *
 * Extensions: Bass moves the R4/C3 corner (the classic C3 "bass mod"),
 * 0.5 = stock 720 Hz, left up to 1.8 kHz plus a low-shelf cut, right down to
 * 90 Hz; Presence is a +-9 dB high shelf at 3.5 kHz.
 */
#include <string.h>
#include "common/smoothing.h"
#include "common/filters.h"
#include "common/oversampling.h"
#include "common/nonlinear.h"
#include "common/tapers.h"
#include "pedal_meta.h"

#define PFX_K_INPUT PK_INPUT
#define PFX_K_MIX PK_MIX
#define PFX_K_OUTPUT PK_OUTPUT
#define PFX_DRY_DELAY 4 /* Mix alignment, tools/mix_align.sh: worst Mix-50% dip -1.3 dB */

/* clipping amplifier */
#define TS_R4 4700.0
#define TS_F_GAINLEG 720.0 /* 1 / (2 pi R4 C3), C3 = 47 nF */
#define TS_R6 51000.0
#define TS_DRIVE_POT 500000.0
#define TS_C4 51e-12
#define TS_RAIL 3.8f       /* JRC4558 on 9 V, 4.5 V bias */
/* tone amplifier */
#define TS_RS 1000.0
#define TS_RI 10000.0
#define TS_CS 0.22e-6
#define TS_RZ 220.0
#define TS_CZ 0.22e-6
#define TS_RF 1000.0
#define TS_TONE_POT 20000.0

#define TS_CAL 0.743f /* defaults ~ unity on a -6 dBFS-peak pluck (tools/analyze cal) */
#define TS_FS_OS (PFX_FS * PFX_OS)

typedef struct {
    pfx_fo_s hp_in, gainleg, hp_out;
    pfx_cap_s c4;
    pfx_os_s os;
    pfx_bq_s tone, bass, presence;
} ts_ch;

struct pedal {
    pfx_smooth drive, tone, level, bass, presence;
    pfx_fo_c hp_in_c, gainleg_c, hp_out_c;
    pfx_diodes d;
    pfx_node node;
    float gc4;
    pfx_bq_c tone_c, bass_c, presence_c;
    int dirty;
    pfx_ramp lvl_r;
    float lvl_next;
    ts_ch ch[2];
};

static void ts_tone_coefs(pfx_bq_c *c, double t) {
    const double ra = fmax(t * TS_TONE_POT, 10.0), rb = fmax((1.0 - t) * TS_TONE_POT, 10.0);
    const double g = 1.0 / ra + 1.0 / rb, gs = 1.0 / TS_RS, gi = 1.0 / TS_RI;
    const double k = g * TS_RZ + 1.0;
    const double n0 = gs * g, n1 = gs * TS_CZ * (k + TS_RF / rb);
    const double d0 = (gs + gi) * g;
    const double d1 = (gs + gi) * TS_CZ * k + TS_CS * g + TS_CZ / ra;
    const double d2 = TS_CS * TS_CZ * k;
    pfx_blt2(c, 0.0, n1, n0, d2, d1, d0, pfx_blt_k(PFX_FS, 1000.0));
}

static void ts_coefs(struct pedal *p) {
    /* Drive: 500k audio-taper pot in series with R6. */
    const double rf = TS_R6 + TS_DRIVE_POT * pfx_taper_log(p->drive.cur, 4.6f);
    pfx_node_set(&p->node, &p->d, (float)(1.0 / rf) + p->gc4);
    /* Bass extension: the gain-leg corner. */
    const float b = p->bass.cur;
    const double f_leg = TS_F_GAINLEG * (b < 0.5f ? pow(2.5, (0.5 - b) * 2.0) : pow(0.125, (b - 0.5) * 2.0));
    const double c3 = 1.0 / (2.0 * M_PI * TS_R4 * f_leg);
    /* i = Vin * s C3 / (1 + s R4 C3), at the oversampled rate */
    pfx_blt1(&p->gainleg_c, c3, 0.0, TS_R4 * c3, 1.0, pfx_blt_k(TS_FS_OS, f_leg));
    /* Always running: at 0 dB a shelf is an exact identity, so there is no
     * on/off switch to click. */
    pfx_bq_lowshelf(&p->bass_c, 180.0, 0.6, b < 0.5f ? -8.0 * (0.5 - b) * 2.0 : 0.0, PFX_FS);
    ts_tone_coefs(&p->tone_c, pfx_taper_s(p->tone.cur, 1.6f));
    pfx_bq_highshelf(&p->presence_c, 3500.0, 0.6, pfx_db_knob(p->presence.cur, 9.0f), PFX_FS);
}

static void pedal_reset(struct pedal *p) { memset(p->ch, 0, sizeof p->ch); }

static void pedal_set(struct pedal *p, int idx, float v) {
    switch (idx) {
    case PK_DRIVE: pfx_sm_set(&p->drive, v); break;
    case PK_TONE: pfx_sm_set(&p->tone, v); break;
    case PK_LEVEL: pfx_sm_set(&p->level, v); break;
    case PK_BASS: pfx_sm_set(&p->bass, v); break;
    case PK_PRESENCE: pfx_sm_set(&p->presence, v); break;
    default: return;
    }
    p->dirty = 1;
}

static void pedal_snap(struct pedal *p) {
    pfx_sm_snap(&p->drive); pfx_sm_snap(&p->tone); pfx_sm_snap(&p->level);
    pfx_sm_snap(&p->bass); pfx_sm_snap(&p->presence);
    ts_coefs(p);
    p->lvl_next = pfx_level_gain(p->level.cur) * TS_CAL;
    pfx_ramp_end(&p->lvl_r, p->lvl_next);
    p->dirty = 0;
}

static void pedal_init(struct pedal *p) {
    pfx_sm_init(&p->drive, 0.5f, 5.0f);
    pfx_sm_init(&p->tone, 0.5f, 10.0f);
    pfx_sm_init(&p->level, 0.5f, 15.0f);
    pfx_sm_init(&p->bass, 0.5f, 10.0f);
    pfx_sm_init(&p->presence, 0.5f, 10.0f);
    p->d = pfx_diodes_si(1, 1, 1.2f);
    p->gc4 = (float)(2.0 * TS_C4 * TS_FS_OS);
    pfx_fo_hp(&p->hp_in_c, 7.0, PFX_FS);   /* input coupling */
    pfx_fo_hp(&p->hp_out_c, 16.0, PFX_FS); /* 10 uF into 10k output load, ~1.6 Hz, plus 1 uF volume coupling */
    pedal_reset(p);
    pedal_snap(p);
}

static void pedal_control(struct pedal *p) {
    const int moving = pfx_sm_moving(&p->drive) | pfx_sm_moving(&p->tone) |
                       pfx_sm_moving(&p->bass) | pfx_sm_moving(&p->presence);
    pfx_sm_step(&p->drive); pfx_sm_step(&p->tone); pfx_sm_step(&p->bass);
    pfx_sm_step(&p->presence); pfx_sm_step(&p->level);
    if (moving || p->dirty) ts_coefs(p);
    p->dirty = 0;
    p->lvl_next = pfx_level_gain(p->level.cur) * TS_CAL;
    pfx_ramp_to(&p->lvl_r, p->lvl_next, PFX_CTRL);
}

static inline float ts_clip_stage(const struct pedal *p, ts_ch *c, float x) {
    float up[PFX_OS];
    pfx_os_up(&c->os, x, up);
    for (int j = 0; j < PFX_OS; ++j) {
        const float i = pfx_fo(&p->gainleg_c, &c->gainleg, up[j]);
        const float v = pfx_diode_fast(&p->d, &p->node, i + c->c4.ih);
        pfx_cap_update(&c->c4, p->gc4, v);
        up[j] = pfx_rail(up[j] + v, TS_RAIL, TS_RAIL, 0.7f);
    }
    return pfx_os_down(&c->os, up);
}

static inline float ts_sample(const struct pedal *p, ts_ch *c, float x, float lvl) {
    x = pfx_fo(&p->hp_in_c, &c->hp_in, x * PFX_V_IN);
    x = ts_clip_stage(p, c, x);
    x = pfx_bq(&p->tone_c, &c->tone, x) * lvl;
    x = pfx_fo(&p->hp_out_c, &c->hp_out, x);
    x = pfx_bq(&p->bass_c, &c->bass, x);
    x = pfx_bq(&p->presence_c, &c->presence, x);
    return x;
}

static void pedal_run(struct pedal *p, float *l, float *r, int n) {
    /* Mono content through identical states: run one channel, copy it
     * (bit-exact with running both). Otherwise both channels advance in the
     * same loop so their independent recursions overlap in the pipeline. */
    const int mono = pfx_is_mono(l, r, n) && !memcmp(&p->ch[0], &p->ch[1], sizeof p->ch[0]);
    pfx_ramp lvl = p->lvl_r;
    if (mono) {
        for (int i = 0; i < n; ++i) l[i] = ts_sample(p, &p->ch[0], l[i], pfx_ramp_tick(&lvl));
        p->ch[1] = p->ch[0];
        memcpy(r, l, (size_t)n * sizeof *l);
    } else {
        for (int i = 0; i < n; ++i) {
            const float g = pfx_ramp_tick(&lvl);
            l[i] = ts_sample(p, &p->ch[0], l[i], g);
            r[i] = ts_sample(p, &p->ch[1], r[i], g);
        }
    }
    pfx_ramp_end(&p->lvl_r, p->lvl_next);
}

static void pedal_flush(struct pedal *p) {
    for (int ch = 0; ch < 2; ++ch) {
        ts_ch *c = &p->ch[ch];
        c->hp_in.z = pfx_flush(c->hp_in.z); c->gainleg.z = pfx_flush(c->gainleg.z);
        c->hp_out.z = pfx_flush(c->hp_out.z); c->c4.ih = pfx_flush(c->c4.ih);
        pfx_bq_s *b[3] = {&c->tone, &c->bass, &c->presence};
        for (int k = 0; k < 3; ++k) { b[k]->z1 = pfx_flush(b[k]->z1); b[k]->z2 = pfx_flush(b[k]->z2); }
        pfx_os_flush(&c->os);
    }
}

#include "common/plugin_v2.h"
