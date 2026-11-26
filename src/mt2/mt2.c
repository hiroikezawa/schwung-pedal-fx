/* Metal Sweep: MT-2-style metal distortion for Schwung (Ableton Move).
 *
 * SPDX-License-Identifier: MIT
 * Copyright (C) 2026 Hiro Ikezawa. Original work.
 *
 * Unofficial emulation; not affiliated with or endorsed by BOSS / Roland.
 * Written from the published circuit analysis (Electric Druid, "Boss MT-2
 * Metal Zone pedal analysis") and BOSS's published EQ ranges. brummer10's
 * MetalTone (ToneTwistPlugs) was NOT copied: that repository carries no
 * licence file and its DSP sources no licence header, so only its
 * externally visible structure (pre / distortion / post / EQ, 200 Hz-5 kHz
 * mid sweep, +-15 dB) was taken as a cross-check. See THIRD_PARTY_LICENSES.md.
 *
 * Structure (per channel):
 *
 *   in ─ input filter: HPF 106 Hz (15n / 100k)
 *      ─ pre-emphasis / gain shaping, IC3b: G = 1 + 55 BP(909 Hz, Q 2.1)
 *        with the 220k || 100p roll-off above 7.2 kHz: a 35 dB mid hump
 *   [2x: IC3b swing limit ─ 10k/10k divider with its low-pass (262 Hz)
 *        ─ HPF 48 Hz (33n / 100k)
 *        ─ high-gain stage IC3a: G = 1 + Rf/Rg, Rf || 47p (13 kHz at full
 *          gain), x2 .. x252 on the Distortion pot
 *        ─ IC3a swing limit ─ R033 2k2 into a silicon diode pair]
 *      ─ post-clip RC soft low-pass
 *      ─ post-distortion EQ: the two fixed gyrator peaks, 101 Hz (+9.4 dB)
 *        and 4.6 kHz (+11 dB): the double-peaked, mid-scooped voice
 *      ─ active EQ: Low (105 Hz bell) +-19 dB and High (2.4 kHz shelf)
 *        +-20 dB, with the cut/boost circuit's pot law
 *      ─ sweepable parametric Mid +-15.7 dB, 244 Hz .. 4.6 kHz boosting and
 *        up to 6.1 kHz cutting, as the Wien-bridge circuit does
 *      ─ Level ─ Mix / Output
 *
 * Two stages clip (IC3b's rails and IC3a's rails + diodes), with ~83 dB of
 * gain in front of the diodes at full Distortion.
 *
 * v0.2 calibration: the frequency-shaping constants that Electric Druid does
 * not publish (IC3b's gyrator Q, the effective post-gyrator peaks, the EQ
 * bandwidths and pot laws) were fitted to frequency responses MEASURED from
 * brummer10's MetalTone (a schematic-derived simulation) run as a black
 * box. None of its code or coefficients is used; only the measured curves,
 * which describe the BOSS circuit, informed the numbers above.
 */
#include <string.h>
#include "common/smoothing.h"
#include "common/filters.h"
#include "common/oversampling.h"
#include "common/nonlinear.h"
#include "common/tapers.h"
#include "pedal_meta.h"

#define PFX_K_INPUT (-1) /* MT-2 layout has no Input knob */
#define PFX_K_MIX PK_MIX
#define PFX_K_OUTPUT PK_OUTPUT
#define PFX_DRY_DELAY 5 /* Mix alignment, tools/mix_align.sh: worst Mix-50% dip -4.8 dB */

#define MT_FS_OS (PFX_FS * PFX_OS)
/* IC3b gyrator peak and the R045/C031 low-pass after it. Fitted (1.3 dB rms)
 * to the circuit-derived pre-stage response, see the header. */
#define MT_PRE_PEAK 54.8f     /* +34.8 dB (Electric Druid: 36 dB) */
#define MT_PRE_F 909.0
#define MT_PRE_Q 2.12
#define MT_PRE_LP 262.0
#define MT_RAIL 3.8f
#define MT_RG 1040.0          /* gain 2 at Rf = Rg; 252 at Rf = 260k */
#define MT_RF_MAX 260000.0
#define MT_CF 47e-12
#define MT_R_DIODE 2200.0
#define MT_R_LOAD 10000.0     /* post-clip network loading the diode node */
#define MT_LOW_DB 19.2f      /* Electric Druid: +-20 dB */
#define MT_HIGH_DB 20.6f
#define MT_MID_DB 15.7f

#define MT_CAL 0.152f /* defaults ~ unity on a -6 dBFS-peak pluck (tools/analyze cal) */

typedef struct {
    pfx_fo_s hp_in, hp106, lp7k, lp340, hp48, stage, shelf, dc;
    pfx_bq_s bp, pk105, pk4k9, low, high, mid;
    pfx_os_s os;
} mt_ch;

struct pedal {
    pfx_smooth dist, low, high, mid, midf, level;
    pfx_fo_c hp_in_c, hp106_c, lp7k_c, lp340_c, hp48_c, stage_c, shelf_c, dc_c;
    pfx_bq_c bp_c, pk105_c, pk4k9_c, low_c, high_c, mid_c;
    pfx_diodes d;
    pfx_node node;
    float g_diode;
    int dirty;
    pfx_ramp lvl_r;
    float lvl_next;
    mt_ch ch[2];
};

static void mt_coefs(struct pedal *p) {
    /* IC3a: H(s) = (s Rf C + 1 + Rf/Rg) / (s Rf C + 1) */
    const double rf = MT_RG + (MT_RF_MAX - MT_RG) * pfx_taper_log(p->dist.cur, 4.6f);
    pfx_blt1(&p->stage_c, rf * MT_CF, 1.0 + rf / MT_RG, rf * MT_CF, 1.0, pfx_blt_k(MT_FS_OS, 3000.0));
    /* Active EQ with the circuit's own pot laws (see pfx_cutboost_db). Low is
     * the op-amp gyrator (its effective bell is broad, Q 0.84), High an RC
     * shelf, Mid the Wien bridge whose top frequency is higher when cutting. */
    pfx_bq_peak(&p->low_c, 105.0, 0.84, pfx_cutboost_db(p->low.cur, MT_LOW_DB, 0.88f), PFX_FS);
    pfx_bq_highshelf(&p->high_c, 2390.0, 0.45, pfx_cutboost_db(p->high.cur, MT_HIGH_DB, 0.88f), PFX_FS);
    const float mdb = pfx_cutboost_db(p->mid.cur, MT_MID_DB, 0.5f);
    const float fmid = pfx_freq_log(p->midf.cur, 244.0f, mdb >= 0.0f ? 4600.0f : 6100.0f);
    pfx_bq_peak(&p->mid_c, fmid, 0.66, mdb, PFX_FS);
}

static void pedal_reset(struct pedal *p) { memset(p->ch, 0, sizeof p->ch); }

static void pedal_set(struct pedal *p, int idx, float v) {
    switch (idx) {
    case PK_DISTORTION: pfx_sm_set(&p->dist, v); break;
    case PK_LOW: pfx_sm_set(&p->low, v); break;
    case PK_HIGH: pfx_sm_set(&p->high, v); break;
    case PK_MID: pfx_sm_set(&p->mid, v); break;
    case PK_MID_FREQ: pfx_sm_set(&p->midf, v); break;
    case PK_LEVEL: pfx_sm_set(&p->level, v); break;
    default: return;
    }
    p->dirty = 1;
}

static void pedal_snap(struct pedal *p) {
    pfx_sm_snap(&p->dist); pfx_sm_snap(&p->low); pfx_sm_snap(&p->high);
    pfx_sm_snap(&p->mid); pfx_sm_snap(&p->midf); pfx_sm_snap(&p->level);
    mt_coefs(p);
    p->lvl_next = pfx_level_gain(p->level.cur) * MT_CAL;
    pfx_ramp_end(&p->lvl_r, p->lvl_next);
    p->dirty = 0;
}

static void pedal_init(struct pedal *p) {
    pfx_sm_init(&p->dist, 0.5f, 5.0f);
    pfx_sm_init(&p->low, 0.5f, 10.0f);
    pfx_sm_init(&p->high, 0.5f, 10.0f);
    pfx_sm_init(&p->mid, 0.5f, 10.0f);
    pfx_sm_init(&p->midf, 0.5f, 20.0f); /* a sweep: slower, never zippers */
    pfx_sm_init(&p->level, 0.5f, 15.0f);
    pfx_fo_hp(&p->hp_in_c, 3.4, PFX_FS);
    pfx_fo_hp(&p->hp106_c, 106.0, PFX_FS);
    pfx_bq_bp(&p->bp_c, MT_PRE_F, MT_PRE_Q, PFX_FS);
    pfx_fo_lp(&p->lp7k_c, 7200.0, PFX_FS);
    pfx_fo_lp(&p->lp340_c, MT_PRE_LP, MT_FS_OS);
    pfx_fo_hp(&p->hp48_c, 48.0, MT_FS_OS);
    /* Post-distortion shaping, fitted (0.2 dB rms) to the circuit-derived
     * response: the two gyrator peaks as the transistors can actually
     * deliver them (the 105 Hz one's nominal Q 14.6 is out of reach, as
     * Electric Druid notes), and the R032/R031/C023 soft low-pass, which
     * only bites at the very top. */
    {
        const double wp = 2.0 * M_PI * 16660.0, wz = 2.0 * M_PI * 62400.0;
        pfx_blt1(&p->shelf_c, 1.0 / wz, 1.0, 1.0 / wp, 1.0, pfx_blt_k(PFX_FS, 10000.0));
    }
    pfx_bq_peak(&p->pk105_c, 101.0, 2.62, 9.4, PFX_FS);
    pfx_bq_peak(&p->pk4k9_c, 4629.0, 0.98, 11.0, PFX_FS);
    pfx_fo_hp(&p->dc_c, 10.0, PFX_FS);
    p->d = pfx_diodes_si(1, 1, 1.0f);
    p->g_diode = (float)(1.0 / MT_R_DIODE);
    pfx_node_set(&p->node, &p->d, p->g_diode + (float)(1.0 / MT_R_LOAD));
    pedal_reset(p);
    pedal_snap(p);
}

static void pedal_control(struct pedal *p) {
    const int moving = pfx_sm_moving(&p->dist) | pfx_sm_moving(&p->low) | pfx_sm_moving(&p->high) |
                       pfx_sm_moving(&p->mid) | pfx_sm_moving(&p->midf);
    pfx_sm_step(&p->dist); pfx_sm_step(&p->low); pfx_sm_step(&p->high);
    pfx_sm_step(&p->mid); pfx_sm_step(&p->midf); pfx_sm_step(&p->level);
    if (moving || p->dirty) mt_coefs(p);
    p->dirty = 0;
    p->lvl_next = pfx_level_gain(p->level.cur) * MT_CAL;
    pfx_ramp_to(&p->lvl_r, p->lvl_next, PFX_CTRL);
}

static inline float mt_sample(const struct pedal *p, mt_ch *c, float x, float lvl) {
    x = pfx_fo(&p->hp_in_c, &c->hp_in, x * PFX_V_IN);
    x = pfx_fo(&p->hp106_c, &c->hp106, x);
    x += MT_PRE_PEAK * pfx_fo(&p->lp7k_c, &c->lp7k, pfx_bq(&p->bp_c, &c->bp, x));
    float up[PFX_OS];
    pfx_os_up(&c->os, x, up);
    for (int j = 0; j < PFX_OS; ++j) {
        float v = pfx_rail(up[j], MT_RAIL, MT_RAIL, 0.7f);
        v = 0.5f * pfx_fo(&p->lp340_c, &c->lp340, v);
        v = pfx_fo(&p->hp48_c, &c->hp48, v);
        v = pfx_rail(pfx_fo(&p->stage_c, &c->stage, v), MT_RAIL, MT_RAIL, 0.7f);
        up[j] = pfx_diode_fast(&p->d, &p->node, v * p->g_diode);
    }
    x = pfx_os_down(&c->os, up);
    x = pfx_fo(&p->shelf_c, &c->shelf, x);
    x = pfx_bq(&p->pk4k9_c, &c->pk4k9, pfx_bq(&p->pk105_c, &c->pk105, x));
    x = pfx_bq(&p->low_c, &c->low, x);
    x = pfx_bq(&p->high_c, &c->high, x);
    x = pfx_bq(&p->mid_c, &c->mid, x) * lvl;
    return pfx_fo(&p->dc_c, &c->dc, x);
}

static void pedal_run(struct pedal *p, float *l, float *r, int n) {
    const int mono = pfx_is_mono(l, r, n) && !memcmp(&p->ch[0], &p->ch[1], sizeof p->ch[0]);
    pfx_ramp lvl = p->lvl_r;
    if (mono) {
        for (int i = 0; i < n; ++i) l[i] = mt_sample(p, &p->ch[0], l[i], pfx_ramp_tick(&lvl));
        p->ch[1] = p->ch[0];
        memcpy(r, l, (size_t)n * sizeof *l);
    } else {
        for (int i = 0; i < n; ++i) {
            const float g = pfx_ramp_tick(&lvl);
            l[i] = mt_sample(p, &p->ch[0], l[i], g);
            r[i] = mt_sample(p, &p->ch[1], r[i], g);
        }
    }
    pfx_ramp_end(&p->lvl_r, p->lvl_next);
}

static void pedal_flush(struct pedal *p) {
    for (int ch = 0; ch < 2; ++ch) {
        mt_ch *c = &p->ch[ch];
        pfx_fo_s *f[8] = {&c->hp_in, &c->hp106, &c->lp7k, &c->lp340, &c->hp48, &c->stage, &c->shelf, &c->dc};
        for (int k = 0; k < 8; ++k) f[k]->z = pfx_flush(f[k]->z);
        pfx_bq_s *b[6] = {&c->bp, &c->pk105, &c->pk4k9, &c->low, &c->high, &c->mid};
        for (int k = 0; k < 6; ++k) { b[k]->z1 = pfx_flush(b[k]->z1); b[k]->z2 = pfx_flush(b[k]->z2); }
        pfx_os_flush(&c->os);
    }
}

#include "common/plugin_v2.h"
