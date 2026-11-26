/* Unit tests for src/common and an offline circuit cross-check.
 *
 * SPDX-License-Identifier: MIT
 *
 * 1. oversampling: pass band, image rejection (up), alias rejection (down)
 * 2. diode solver: closed form vs exact two-diode solution
 * 3. clipper cross-check ("SPICE-style"): the TS808 feedback clipper and the
 *    DS-1 RC-diode clipper as implemented (float, 2x / 4x, omega closed form,
 *    trapezoidal companion) against a double-precision reference that solves
 *    the same circuit ODE at 16x with Newton to convergence. Compared on the
 *    band-limited result: fundamental and harmonics 2..9 of a 220 Hz sine.
 * 4. smoothing and filter designers
 */
#define PFX_OS 2
#include "common/smoothing.h"
#include "common/filters.h"
#include "common/oversampling.h"
#include "common/nonlinear.h"
#include "common/tapers.h"
#include "common/network.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static int failures;
#define CHECK(cond, ...) do { if (!(cond)) { ++failures; printf("FAIL %s:%d: ", __func__, __LINE__); printf(__VA_ARGS__); printf("\n"); } } while (0)

static double amp_at(const double *x, int n, double f, double fs) {
    double re = 0, im = 0;
    for (int i = 0; i < n; ++i) { re += x[i] * cos(2 * M_PI * f * i / fs); im -= x[i] * sin(2 * M_PI * f * i / fs); }
    return 2 * sqrt(re * re + im * im) / n;
}

/* ---- 1. oversampling ------------------------------------------------------- */
static void test_halfband(void) {
    const int n = 44100;
    static double base[44100], hi[88200];
    double worst_pass = 0;
    const double freqs[] = {100, 1000, 5000, 10000, 15000, 19000, 20000};
    for (int k = 0; k < 7; ++k) {
        pfx_os_s s = {0};
        float up[2];
        for (int i = 0; i < 2 * n; ++i) {
            const float x = (float)sin(2 * M_PI * freqs[k] * i / PFX_FS);
            pfx_os_up(&s, x, up);
            const float y = pfx_os_down(&s, up);
            if (i >= n) base[i - n] = y;
            if (i >= n) { hi[2 * (i - n)] = up[0]; hi[2 * (i - n) + 1] = up[1]; }
        }
        const double a = amp_at(base, n, freqs[k], PFX_FS);
        worst_pass = fmax(worst_pass, fabs(20 * log10(a)));
        if (freqs[k] >= 1000) {
            const double img = amp_at(hi, 2 * n, PFX_FS - freqs[k], 2 * PFX_FS);
            const double sig = amp_at(hi, 2 * n, freqs[k], 2 * PFX_FS);
            CHECK(20 * log10(img / sig) < -90, "upsampler image of %.0f Hz only %.1f dB down", freqs[k], 20 * log10(img / sig));
        }
    }
    CHECK(worst_pass < 0.01, "pass band deviates %.4f dB up to 20 kHz", worst_pass);
    /* decimation: a 2x-rate tone above 22.05 kHz must not come back aliased */
    const double fa[] = {24500, 30000, 40000};
    for (int k = 0; k < 3; ++k) {
        pfx_os_s s = {0};
        for (int i = 0; i < 2 * n; ++i) {
            float in2[2];
            in2[0] = (float)sin(2 * M_PI * fa[k] * (2 * i) / (2 * PFX_FS));
            in2[1] = (float)sin(2 * M_PI * fa[k] * (2 * i + 1) / (2 * PFX_FS));
            const float y = pfx_os_down(&s, in2);
            if (i >= n) base[i - n] = y;
        }
        const double al = amp_at(base, n, PFX_FS - fa[k], PFX_FS); /* folds to fs - f */
        CHECK(20 * log10(al + 1e-12) < -90, "decimator lets %.0f Hz alias at %.1f dB", fa[k], 20 * log10(al + 1e-12));
    }
    printf("  half-band: pass band within %.4f dB to 20 kHz, images/aliases < -90 dB\n", worst_pass);
}

/* ---- 2. diode solver --------------------------------------------------------- */
static double full_res(const pfx_diodes *d, double G, double J, double v) {
    return G * v + d->is_p * (exp(v / d->a_p) - 1) - d->is_n * (exp(-v / d->a_n) - 1) - J;
}
static void test_diodes(void) {
    const float Gs[] = {1 / 2200.f, 1 / 2200.f + 1 / 3400.f + 1.76e-3f, 1 / 51000.f + 9e-6f, 1 / 551000.f + 9e-6f, 1 / 10000.f};
    const pfx_diodes ds[] = {pfx_diodes_si(1, 1, 1.0f), pfx_diodes_si(1, 1, 1.2f), pfx_diodes_si(2, 2, 1.25f)};
    double worst_fast = 0, worst_ref = 0;
    for (int di = 0; di < 3; ++di)
        for (int g = 0; g < 5; ++g) {
            pfx_node nd;
            pfx_node_set(&nd, &ds[di], Gs[g]);
            const double jmax = 10.0 * Gs[g]; /* 10 V through the source */
            for (int k = -4000; k <= 4000; ++k) {
                const double J = jmax * k / 4000.0;
                double lo = -5, hi = 5;
                for (int it = 0; it < 80; ++it) { const double m = 0.5 * (lo + hi); if (full_res(&ds[di], Gs[g], J, m) > 0) hi = m; else lo = m; }
                const double v = 0.5 * (lo + hi);
                worst_fast = fmax(worst_fast, fabs(pfx_diode_fast(&ds[di], &nd, (float)J) - v));
                worst_ref = fmax(worst_ref, fabs(pfx_diode_solve(&ds[di], &nd, (float)J) - v));
            }
        }
    CHECK(worst_fast < 5e-3, "fast diode solve off by %g V", worst_fast); /* 0.4% of a 2-diode drop */
    CHECK(worst_ref < 2e-4, "reference diode solve off by %g V", worst_ref);
    printf("  diode solve: closed form within %.2f mV, with Newton %.3f mV (0..10 V drive)\n", worst_fast * 1e3, worst_ref * 1e3);
}

/* ---- 3. circuit cross-check ------------------------------------------------ */
/* Reference: a node  C dv/dt = J(t) - G v - Id(v)  with J from a linear
 * prefilter, trapezoidal at fs_ref, Newton iterated to 1e-12. */
typedef struct { double G, C, fs, ih, v; pfx_diodes d; } ref_node;
static double ref_step(ref_node *r, double J) {
    const double gc = 2 * r->C * r->fs;
    double v = r->v;
    for (int it = 0; it < 60; ++it) {
        const double ep = exp(v / r->d.a_p), en = exp(-v / r->d.a_n);
        const double f = (r->G + gc) * v + r->d.is_p * (ep - 1) - r->d.is_n * (en - 1) - J - r->ih;
        const double fp = r->G + gc + r->d.is_p / r->d.a_p * ep + r->d.is_n / r->d.a_n * en;
        const double dv = f / fp;
        v -= dv;
        if (fabs(dv) < 1e-12) break;
    }
    const double ic = gc * v - r->ih;
    r->ih = gc * v + ic;
    r->v = v;
    return v;
}

/* Harmonic amplitudes 1..9 (dB) of y (n samples at fs) for f0. */
static void harmonics(const double *y, int n, double fs, double f0, double *db) {
    for (int k = 1; k <= 9; ++k) db[k] = 20 * log10(amp_at(y, n, k * f0, fs) + 1e-12);
}

/* TS808 clipping amplifier, drive pot fraction t. */
static double ts_compare(double drive_frac, double amp) {
    const double fs = PFX_FS, f0 = 220, R4 = 4700, C3 = 0.047e-6, C4 = 51e-12;
    const double Rf = 51000 + 500000 * drive_frac;
    const int n = 44100;
    static double yt[44100], yr[44100];
    /* model under test: float, 2x, as in src/ts808 */
    {
        pfx_os_s os = {0};
        pfx_fo_c leg; pfx_fo_s legs = {0};
        pfx_blt1(&leg, C3, 0, R4 * C3, 1, pfx_blt_k(2 * fs, 1 / (2 * M_PI * R4 * C3)));
        pfx_diodes d = pfx_diodes_si(1, 1, 1.2f);
        const float gc = (float)(2 * C4 * 2 * fs);
        pfx_node nd; pfx_node_set(&nd, &d, (float)(1 / Rf) + gc);
        pfx_cap_s cap = {0};
        for (int i = 0; i < 2 * n; ++i) {
            float up[2];
            pfx_os_up(&os, (float)(amp * sin(2 * M_PI * f0 * i / fs)), up);
            for (int j = 0; j < 2; ++j) {
                const float cur = pfx_fo(&leg, &legs, up[j]);
                const float v = pfx_diode_fast(&d, &nd, cur + cap.ih);
                pfx_cap_update(&cap, gc, v);
                up[j] += v;
            }
            const float y = pfx_os_down(&os, up);
            if (i >= n) yt[i - n] = y;
        }
    }
    /* reference: double, 16x, exact Newton, then ideal decimation by DFT */
    {
        const int os = 16;
        const double fr = fs * os;
        pfx_fo_c leg;
        pfx_blt1(&leg, C3, 0, R4 * C3, 1, pfx_blt_k(fr, 1 / (2 * M_PI * R4 * C3)));
        double z = 0;
        ref_node r = {1 / Rf, C4, fr, 0, 0, pfx_diodes_si(1, 1, 1.2f)};
        for (int i = 0; i < 2 * n; ++i) {
            double acc = 0;
            for (int j = 0; j < os; ++j) {
                const double x = amp * sin(2 * M_PI * f0 * (i + (double)j / os) / fs);
                const double cur = leg.b0 * x + z;
                z = leg.b1 * x - leg.a1 * cur;
                const double v = ref_step(&r, cur);
                if (j == 0) acc = x + v; /* point sample; harmonics <= 2 kHz are far below any image */
            }
            if (i >= n) yr[i - n] = acc;
        }
    }
    double ht[10], hr[10], worst = 0;
    harmonics(yt, n, fs, f0, ht);
    harmonics(yr, n, fs, f0, hr);
    for (int k = 1; k <= 9; ++k)
        if (hr[k] > hr[1] - 50) worst = fmax(worst, fabs(ht[k] - hr[k])); /* harmonics within 50 dB of the fundamental */
    if (getenv("VERBOSE")) {
        printf("    ts drive %.2f amp %.2f:", drive_frac, amp);
        for (int k = 1; k <= 9; ++k) printf(" H%d %.1f/%.1f", k, ht[k], hr[k]);
        printf("\n");
    }
    return worst;
}

/* DS-1 clipper: 2k2 into 1N4148 pair || 10 nF, loaded by 3k4, driven by a
 * (pre-clipped) op-amp swing. */
static double ds_compare(double amp, int osf) {
    const double fs = PFX_FS, f0 = 220, R = 2200, C = 0.01e-6, RL = 3400;
    const int n = 44100;
    static double yt[44100], yr[44100];
    {
        pfx_diodes d = pfx_diodes_si(1, 1, 1.0f);
        const float gc = (float)(2 * C * osf * fs);
        pfx_node nd; pfx_node_set(&nd, &d, (float)(1 / R + 1 / RL) + gc);
        pfx_cap_s cap = {0};
        pfx_hb_s up1, dn1, up2, dn2;
        memset(&up1, 0, sizeof up1); memset(&dn1, 0, sizeof dn1); memset(&up2, 0, sizeof up2); memset(&dn2, 0, sizeof dn2);
        for (int i = 0; i < 2 * n; ++i) {
            const float x = (float)(amp * sin(2 * M_PI * f0 * i / fs));
            float u[4], y;
            if (osf == 2) pfx_hb_up(pfx_hb1, 8, &up1, x, &u[0], &u[1]);
            else { float a, b; pfx_hb_up(pfx_hb1, 8, &up1, x, &a, &b); pfx_hb_up(pfx_hb2, 4, &up2, a, &u[0], &u[1]); pfx_hb_up(pfx_hb2, 4, &up2, b, &u[2], &u[3]); }
            for (int j = 0; j < osf; ++j) {
                const float v = pfx_diode_fast(&d, &nd, u[j] / (float)R + cap.ih);
                pfx_cap_update(&cap, gc, v);
                u[j] = v;
            }
            if (osf == 2) y = pfx_hb_down(pfx_hb1, 8, &dn1, u[0], u[1]);
            else { const float a = pfx_hb_down(pfx_hb2, 4, &dn2, u[0], u[1]), b = pfx_hb_down(pfx_hb2, 4, &dn2, u[2], u[3]); y = pfx_hb_down(pfx_hb1, 8, &dn1, a, b); }
            if (i >= n) yt[i - n] = y;
        }
    }
    {
        const int os = 16;
        ref_node r = {1 / R + 1 / RL, C, fs * os, 0, 0, pfx_diodes_si(1, 1, 1.0f)};
        for (int i = 0; i < 2 * n; ++i) {
            double y0 = 0;
            for (int j = 0; j < os; ++j) {
                const double v = ref_step(&r, amp * sin(2 * M_PI * f0 * (i + (double)j / os) / fs) / R);
                if (j == 0) y0 = v;
            }
            if (i >= n) yr[i - n] = y0;
        }
    }
    double ht[10], hr[10], worst = 0;
    harmonics(yt, n, fs, f0, ht);
    harmonics(yr, n, fs, f0, hr);
    for (int k = 1; k <= 9; ++k)
        if (hr[k] > hr[1] - 50) worst = fmax(worst, fabs(ht[k] - hr[k]));
    return worst;
}

static void test_circuits(void) {
    const double drives[] = {0.0, 0.1, 1.0}, amps[] = {0.05, 0.25};
    double worst = 0;
    for (int d = 0; d < 3; ++d)
        for (int a = 0; a < 2; ++a) worst = fmax(worst, ts_compare(drives[d], amps[a]));
    CHECK(worst < 1.0, "TS808 clipper vs 16x reference: harmonic error %.2f dB", worst);
    printf("  TS808 clipper (2x, float, closed form) vs 16x exact reference: harmonics 1-9 (within 50 dB of H1) within %.2f dB\n", worst);
    const double dsa[] = {0.3, 1.0, 3.7};
    double w2 = 0, w4 = 0;
    for (int a = 0; a < 3; ++a) { w2 = fmax(w2, ds_compare(dsa[a], 2)); w4 = fmax(w4, ds_compare(dsa[a], 4)); }
    CHECK(w4 < 0.5, "DS-1 clipper (4x) vs reference: %.2f dB", w4);
    printf("  DS-1 RC-diode clipper vs 16x exact reference: harmonics 1-9 (within 50 dB of H1) within %.2f dB at 2x, %.2f dB at 4x\n", w2, w4);
}

/* ---- 4. smoothing / filters ------------------------------------------------ */
static void test_smoothing_filters(void) {
    pfx_smooth s;
    pfx_sm_init(&s, 0.0f, 10.0f);
    pfx_sm_set(&s, 1.0f);
    int steps = 0;
    while (pfx_sm_moving(&s) && steps < 10000) { pfx_sm_step(&s); ++steps; }
    const double ms = steps * PFX_CTRL / (double)PFX_FS * 1000.0;
    CHECK(ms > 40 && ms < 200, "10 ms smoother settled in %.1f ms", ms);
    CHECK(s.cur == 1.0f, "smoother did not land exactly");
    pfx_bq_c c;
    pfx_bq_peak(&c, 1000, 1, 12, PFX_FS);
    CHECK(fabs(20 * log10(pfx_bq_mag(&c, 1000, PFX_FS)) - 12) < 0.01, "peak gain");
    pfx_bq_lowshelf(&c, 200, 0.7, -9, PFX_FS);
    CHECK(fabs(20 * log10(pfx_bq_mag(&c, 5, PFX_FS)) + 9) < 0.05 && fabs(20 * log10(pfx_bq_mag(&c, 10000, PFX_FS))) < 0.05, "low shelf");
    pfx_bq_highshelf(&c, 3000, 0.7, 9, PFX_FS);
    CHECK(fabs(20 * log10(pfx_bq_mag(&c, 20000, PFX_FS)) - 9) < 0.3 && fabs(20 * log10(pfx_bq_mag(&c, 20, PFX_FS))) < 0.05, "high shelf");
    CHECK(fabsf(pfx_level_gain(0.5f) - 1.0f) < 1e-6f && pfx_level_gain(0.0f) == 0.0f, "level taper");
    CHECK(fabsf(pfx_taper_log(0.5f, 4.6f) - 0.091f) < 0.005f, "A taper at noon %.3f", pfx_taper_log(0.5f, 4.6f));
    printf("  smoothing: 10 ms knob settles in %.0f ms; shelves/peaks/tapers exact\n", ms);
}

/* ---- 5. network solver ------------------------------------------------------ */
static void test_network(void) {
    const double fs = 2 * PFX_FS;
    /* (a) source - 1k - node - 100n - gnd: a 1.59 kHz RC low-pass */
    pfx_net_b b; pfx_net_c c; pfx_net_s st;
    pfx_netb_init(&b, 1, fs);
    pfx_netb_r(&b, PFX_SRC, 0, 1000.0);
    pfx_netb_cap(&b, 0, PFX_GND, 100e-9);
    pfx_net_build(&c, &b, -1, NULL);
    double worst = 0;
    const double fr[] = {100, 1000, 1592, 5000, 15000};
    for (int k = 0; k < 5; ++k) {
        memset(&st, 0, sizeof st);
        static double y[88200];
        float v[PFX_NET_N];
        for (int i = 0; i < 2 * 88200; ++i) {
            pfx_net_run_n(&c, &st, NULL, (float)sin(2 * M_PI * fr[k] * i / fs), v, 1, 1);
            if (i >= 88200) y[i - 88200] = v[0];
        }
        const double got = 20 * log10(amp_at(y, 88200, fr[k], fs));
        /* trapezoidal = bilinear: analog frequency 2 fs tan(pi f / fs) */
        const double w = 2 * fs * tan(M_PI * fr[k] / fs) * 1000.0 * 100e-9, want = -10 * log10(1 + w * w);
        worst = fmax(worst, fabs(got - want));
    }
    CHECK(worst < 0.1, "RC network off the analytic response by %.3f dB", worst);
    /* (b) a diode node with a capacitor, as a network, against the
     * hand-written single-node solve it generalises */
    const pfx_diodes d = pfx_diodes_si(1, 1, 1.0f);
    pfx_netb_init(&b, 1, fs);
    pfx_netb_r(&b, PFX_SRC, 0, 2200.0);
    pfx_netb_cap(&b, 0, PFX_GND, 10e-9);
    pfx_netb_r(&b, 0, PFX_GND, 3400.0);
    pfx_net_build(&c, &b, 0, &d);
    memset(&st, 0, sizeof st);
    pfx_node nd; pfx_cap_s cap = {0};
    const float gc = (float)(2 * 10e-9 * fs);
    pfx_node_set(&nd, &d, 1.0f / 2200.0f + 1.0f / 3400.0f + gc);
    double dev = 0;
    for (int i = 0; i < 44100; ++i) {
        const float x = (float)(3.0 * sin(2 * M_PI * 440 * i / fs));
        float v[PFX_NET_N];
        pfx_net_run_n(&c, &st, &d, x, v, 1, 1);
        const float ref = pfx_diode_fast(&d, &nd, x / 2200.0f + cap.ih);
        pfx_cap_update(&cap, gc, ref);
        dev = fmax(dev, fabs(v[0] - ref));
    }
    CHECK(dev < 1e-4, "network diode node deviates %g V from the single-node solve", dev);
    printf("  network solver: RC within %.3f dB of analytic; diode node matches the single-node solve to %.1e V\n", worst, dev);
}

int main(void) {
    test_halfband();
    test_diodes();
    test_circuits();
    test_smoothing_filters();
    test_network();
    printf(failures ? "FAILED: %d\n" : "PASS\n", failures);
    return failures != 0;
}
