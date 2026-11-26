/* Blues Drive: BD-2-style overdrive for Schwung (Ableton Move).
 *
 * SPDX-License-Identifier: MIT
 * Copyright (C) 2026 Hiro Ikezawa. Original work.
 *
 * Unofficial emulation; not affiliated with or endorsed by BOSS / Roland.
 * v0.2 is written from the full stock BD-2 schematic and parts list published
 * by Aion FX ("Sapphire Amp Overdrive" project documentation, v1.0.1), plus
 * the PedalPCB breadboard measurements (gyrator bump: +6 dB at 120 Hz). No
 * third-party code; PedalKernel (AGPL) was not used.
 *
 * Signal path (per channel), component designators as in that schematic:
 *
 *   in ─ JFET buffer (C1 47n/R2 1M, C2 100n/R4 220k: coupling high-passes)
 *   [2x oversampled through stage 2]
 *   ─ STAGE 1, discrete op-amp (Q2/Q3 JFET pair + Q4 PNP), non-inverting:
 *       G1 = 1 + (R8 22k + GAIN B 250kA || C4 47p) / (R7 1k5 + C3 150n)
 *     with its own output swing: hard toward the PNP's rail, soft into R10
 *   ─ fixed tone stack (a Fender TMB frozen at T0/M6/B10): C6 220p + R11 330k
 *     and R12 100k / C7 100n / C8 47n + R14 15k / R13 1M, whose output node
 *     carries D2-D5 (two 1N914 in series each way), coupled by C9 2n2 to
 *   ─ STAGE 2: G2 = 1 + (R19 33k + GAIN A 250kA || C10 100p) / (R18 2k2 + C11 1u)
 *     and its swing
 *   [back to the base rate: everything after is linear or soft]
 *   ─ R22 5k6 || C13 6n8 into C14 5n6 (treble shelf down ~5 dB), C15 18n,
 *     TONE 10kB to C16 18n: a PASSIVE treble cut, wiper into
 *   ─ VOLUME 100kA, then C17 47n into D6/D7 (1N914 pair): turning Level up
 *     makes the output clip too, as on the pedal
 *   ─ IC1B with the Q8 gyrator (L = 31.6 H with C19 56n): +6 dB at 120 Hz
 *   ─ output buffer, (Bass / Presence shelves: extensions)
 *
 * Every passive network is solved as a circuit (common/network.h), so the
 * tone stack's impedance is what the D2-D5 diodes see, and the Tone pot
 * loads its source as it does on the board. The whole pedal is non-inverting
 * (both discrete op-amps are), so the Mix knob blends in phase.
 * The discrete op-amps are modelled as their linear closed-loop gain followed
 * by an asymmetric output-swing limit; their JFET input pairs' own soft
 * compression is not modelled separately.
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
#define PFX_DRY_DELAY 4 /* Mix alignment, tools/mix_align.sh: worst Mix-50% dip -4.8 dB */

#define BD_FS_OS ((double)PFX_FS * PFX_OS)
#define BD_GAIN_POT 250000.0
/* discrete op-amp output swing (9 V supply, ~4.5 V bias): Q4 / Q7 (PNP)
 * pull up hard toward the rail; R10 / R21 2k2 pull down, softly */
#define BD_UP 3.9f
#define BD_UP_KNEE 0.65f
#define BD_DN 3.1f
#define BD_DN_KNEE 0.25f
/* IC1B gyrator bass stage */
#define BD_BUMP_HZ 119.6       /* 1 / (2 pi sqrt(31.6 H x 56 nF)) */
#define BD_BUMP_Q 3.5
#define BD_BUMP_K 1.0          /* 1 + K at resonance: +6 dB (PedalPCB) */

#define BD_CAL 1.609f /* defaults ~ unity on a -6 dBFS-peak pluck (tools/analyze cal) */

/* tone-stack network nodes */
enum { N1_T, N1_N, N1_P, N1_D, N1_Q, N1_COUNT };
/* post-stage-2 network nodes */
enum { N2_E, N2_F, N2_W, N2_H, N2_COUNT };

typedef struct {
    pfx_bq_s s1, s2, bump, bass, presence;
    pfx_fo_s hp_in, hp_out;
    pfx_net_s net1, net2;
    pfx_os_s os;
} bd_ch;

struct pedal {
    pfx_smooth gain, tone, level, bass, presence;
    pfx_bq_c s1_c, s2_c, bump_c, bass_c, presence_c;
    pfx_fo_c hp_in_c, hp_out_c;
    pfx_diodes d_stack, d_out;
    pfx_net_c net1, net2;
    pfx_node node_out;     /* D6/D7 behind the volume pot */
    float vol, g_vol;      /* volume pot ratio and its source conductance */
    int dirty;
    bd_ch ch[2];
};

/* Non-inverting stage, Zf = Rf || Cf, Zg = Rg + 1/(s Cg):
 * G = [(1 + s Rf Cf)(1 + s Rg Cg) + s Rf Cg] / [(1 + s Rf Cf)(1 + s Rg Cg)] */
static void bd_stage(pfx_bq_c *c, double rf, double cf, double rg, double cg) {
    const double t1 = rf * cf, t2 = rg * cg;
    pfx_blt2(c, t1 * t2, t1 + t2 + rf * cg, 1.0, t1 * t2, t1 + t2, 1.0, pfx_blt_k(BD_FS_OS, 2000.0));
}

static void bd_build_stack(struct pedal *p) {
    pfx_net_b b;
    pfx_netb_init(&b, N1_COUNT, BD_FS_OS);
    pfx_netb_cap(&b, PFX_SRC, N1_T, 220e-12);  /* C6 */
    pfx_netb_r(&b, N1_T, N1_D, 330e3);         /* R11 */
    pfx_netb_r(&b, PFX_SRC, N1_N, 100e3);      /* R12 */
    pfx_netb_cap(&b, N1_N, N1_D, 100e-9);      /* C7 */
    pfx_netb_cap(&b, N1_N, N1_P, 47e-9);       /* C8 */
    pfx_netb_r(&b, N1_P, PFX_GND, 15e3);       /* R14 */
    pfx_netb_r(&b, N1_D, PFX_GND, 1e6);        /* R13 */
    pfx_netb_cap(&b, N1_D, N1_Q, 2.2e-9);      /* C9 */
    pfx_netb_r(&b, N1_Q, PFX_GND, 1e6);        /* R15 (to bias) */
    pfx_net_build(&p->net1, &b, N1_D, &p->d_stack);
}

static void bd_build_tone(struct pedal *p, double t) {
    pfx_net_b b;
    pfx_netb_init(&b, N2_COUNT, PFX_FS); /* after the last clipping stage: base rate */
    pfx_netb_r(&b, PFX_SRC, N2_E, 5.6e3);      /* R22 */
    pfx_netb_cap(&b, PFX_SRC, N2_E, 6.8e-9);   /* C13 */
    pfx_netb_cap(&b, N2_E, PFX_GND, 5.6e-9);   /* C14 */
    pfx_netb_cap(&b, N2_E, N2_F, 18e-9);       /* C15 */
    pfx_netb_r(&b, N2_F, N2_W, (1.0 - t) * 10e3); /* TONE 10kB, F side */
    pfx_netb_r(&b, N2_W, N2_H, t * 10e3);      /* TONE, C16 side */
    pfx_netb_cap(&b, N2_H, PFX_GND, 18e-9);    /* C16 */
    pfx_netb_r(&b, N2_W, PFX_GND, 100e3);      /* VOLUME 100kA as a load */
    pfx_net_build(&p->net2, &b, -1, NULL);
}

static void bd_coefs(struct pedal *p) {
    const double vr = BD_GAIN_POT * pfx_taper_log(p->gain.cur, 4.6f); /* dual 250kA */
    bd_stage(&p->s1_c, 22e3 + vr, 47e-12, 1.5e3, 150e-9);
    bd_stage(&p->s2_c, 33e3 + vr, 100e-12, 2.2e3, 1e-6);
    bd_build_tone(p, p->tone.cur);
    /* VOLUME 100kA: wiper ratio, and the wiper's source resistance seen by
     * D6/D7 (pot halves in parallel, plus the tone network's own ~2k). */
    p->vol = pfx_taper_log(p->level.cur, 4.6f);
    const double rth = p->vol * (1.0 - p->vol) * 100e3 + 2e3;
    p->g_vol = (float)(1.0 / rth);
    pfx_node_set(&p->node_out, &p->d_out, p->g_vol + (float)(1.0 / 470e3) /* R23 bias */);
    const float bd = pfx_db_knob(p->bass.cur, 9.0f), pd = pfx_db_knob(p->presence.cur, 9.0f);
    /* extensions; always running: at 0 dB a shelf is an exact identity */
    pfx_bq_lowshelf(&p->bass_c, 100.0, 0.6, bd, PFX_FS);
    pfx_bq_highshelf(&p->presence_c, 4500.0, 0.6, pd, PFX_FS);
}

static void pedal_reset(struct pedal *p) { memset(p->ch, 0, sizeof p->ch); }

static void pedal_set(struct pedal *p, int idx, float v) {
    switch (idx) {
    case PK_GAIN: pfx_sm_set(&p->gain, v); break;
    case PK_TONE: pfx_sm_set(&p->tone, v); break;
    case PK_LEVEL: pfx_sm_set(&p->level, v); break;
    case PK_BASS: pfx_sm_set(&p->bass, v); break;
    case PK_PRESENCE: pfx_sm_set(&p->presence, v); break;
    default: return;
    }
    p->dirty = 1;
}

static void pedal_snap(struct pedal *p) {
    pfx_sm_snap(&p->gain); pfx_sm_snap(&p->tone); pfx_sm_snap(&p->level);
    pfx_sm_snap(&p->bass); pfx_sm_snap(&p->presence);
    bd_coefs(p);
    p->dirty = 0;
}

static void pedal_init(struct pedal *p) {
    pfx_sm_init(&p->gain, 0.5f, 5.0f);
    pfx_sm_init(&p->tone, 0.5f, 10.0f);
    pfx_sm_init(&p->level, 0.5f, 15.0f);
    pfx_sm_init(&p->bass, 0.5f, 10.0f);
    pfx_sm_init(&p->presence, 0.5f, 10.0f);
    p->d_stack = pfx_diodes_si(2, 2, 1.0f); /* D2+D3 / D4+D5 */
    p->d_out = pfx_diodes_si(1, 1, 1.0f);   /* D6 / D7 */
    bd_build_stack(p);
    {   /* IC1B: 1 + K BP(s): the gyrator's series RLC in the gain leg */
        const double w = 2.0 * M_PI * BD_BUMP_HZ;
        pfx_blt2(&p->bump_c, 1.0, w * (1.0 + BD_BUMP_K) / BD_BUMP_Q, w * w, 1.0, w / BD_BUMP_Q, w * w,
                 pfx_blt_k(PFX_FS, BD_BUMP_HZ));
    }
    pfx_fo_hp(&p->hp_in_c, 7.2, PFX_FS);
    pfx_fo_hp(&p->hp_out_c, 8.0, PFX_FS);
    pedal_reset(p);
    pedal_snap(p);
}

static void pedal_control(struct pedal *p) {
    const int moving = pfx_sm_moving(&p->gain) | pfx_sm_moving(&p->tone) | pfx_sm_moving(&p->level) |
                       pfx_sm_moving(&p->bass) | pfx_sm_moving(&p->presence);
    pfx_sm_step(&p->gain); pfx_sm_step(&p->tone); pfx_sm_step(&p->level);
    pfx_sm_step(&p->bass); pfx_sm_step(&p->presence);
    if (moving || p->dirty) bd_coefs(p);
    p->dirty = 0;
}

static inline float bd_swing(float x) {
    return pfx_rail2k(x, BD_UP, BD_DN, BD_UP_KNEE, BD_DN_KNEE);
}

static inline float bd_sample(const struct pedal *p, bd_ch *c, float x) {
    x = pfx_fo(&p->hp_in_c, &c->hp_in, x * PFX_V_IN);
    float up[PFX_OS], v[PFX_NET_N];
    pfx_os_up(&c->os, x, up);
    for (int j = 0; j < PFX_OS; ++j) {
        const float o1 = bd_swing(pfx_bq(&p->s1_c, &c->s1, up[j]));
        pfx_net_run_n(&p->net1, &c->net1, &p->d_stack, o1, v, N1_COUNT, 4);
        up[j] = bd_swing(pfx_bq(&p->s2_c, &c->s2, v[N1_Q]));
    }
    /* Back at the base rate after the last hard clip: the tone network is
     * linear, and D6/D7 clip softly behind the volume pot's ~10k, so their
     * aliasing stays far below the stages' (measured, tools/os_sweep.sh). */
    x = pfx_os_down(&c->os, up);
    pfx_net_run_n(&p->net2, &c->net2, NULL, x, v, N2_COUNT, 4);
    /* VOLUME wiper into D6/D7 (Thevenin: vol x v_W behind the pot) */
    x = pfx_diode_fast(&p->d_out, &p->node_out, p->vol * v[N2_W] * p->g_vol);
    x = pfx_bq(&p->bump_c, &c->bump, x) * BD_CAL;
    x = pfx_fo(&p->hp_out_c, &c->hp_out, x);
    x = pfx_bq(&p->bass_c, &c->bass, x);
    return pfx_bq(&p->presence_c, &c->presence, x);
}

static void pedal_run(struct pedal *p, float *l, float *r, int n) {
    const int mono = pfx_is_mono(l, r, n) && !memcmp(&p->ch[0], &p->ch[1], sizeof p->ch[0]);
    if (mono) {
        for (int i = 0; i < n; ++i) l[i] = bd_sample(p, &p->ch[0], l[i]);
        p->ch[1] = p->ch[0];
        memcpy(r, l, (size_t)n * sizeof *l);
    } else {
        for (int i = 0; i < n; ++i) {
            l[i] = bd_sample(p, &p->ch[0], l[i]);
            r[i] = bd_sample(p, &p->ch[1], r[i]);
        }
    }
}

static void pedal_flush(struct pedal *p) {
    for (int ch = 0; ch < 2; ++ch) {
        bd_ch *c = &p->ch[ch];
        pfx_bq_s *b[5] = {&c->s1, &c->s2, &c->bump, &c->bass, &c->presence};
        for (int k = 0; k < 5; ++k) { b[k]->z1 = pfx_flush(b[k]->z1); b[k]->z2 = pfx_flush(b[k]->z2); }
        c->hp_in.z = pfx_flush(c->hp_in.z); c->hp_out.z = pfx_flush(c->hp_out.z);
        pfx_net_flush(&c->net1); pfx_net_flush(&c->net2);
        pfx_os_flush(&c->os);
    }
}

#include "common/plugin_v2.h"
