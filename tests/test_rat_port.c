/* The RAT port against dm-Rat's own unit tests.
 *
 * SPDX-License-Identifier: GPL-3.0-only
 * Expected vectors copied from davemollen/dm-Rat rat/src/op_amp.rs (tests
 * module), GPL-3.0, Copyright (C) Dave Mollen.
 *
 * Also checks that the op-amp filter's gain is dm-Rat's: 0 dB at Distortion
 * 0 and 67.26 dB (x2307) of mid-band gain at Distortion 1.
 */
#include "rat/rat.c"
#include <stdio.h>

static int failures;

static void expect(float d, const double num[4], const double den[4]) {
    float n[4], a[4];
    rat_opamp_s(d, n, a);
    for (int i = 0; i < 4; ++i) {
        const double en = fabs(n[i] - num[i]) / fabs(num[i]), ea = fabs(a[i] - den[i]) / fabs(den[i]);
        if (en > 1e-5 || ea > 1e-5) {
            ++failures;
            printf("FAIL d=%g coef %d: num %.9g vs %.9g, den %.9g vs %.9g\n", d, i, n[i], num[i], a[i], den[i]);
        }
    }
}

int main(void) {
    { const double n[4] = {2.72149e-12, 0.000627937503, 0.6927454, 1.0}, a[4] = {2.72149e-12, 2.99503e-07, 0.0027454, 1.0}; expect(1.0f, n, a); }
    { const double n[4] = {2.72149e-13, 6.30386844e-05, 0.0717364, 1.0}, a[4] = {2.72149e-13, 2.748844e-07, 0.0027364, 1.0}; expect(0.1f, n, a); }
    { const double n[4] = {2.72149e-14, 6.54880254e-06, 0.009635499999999998, 1.0}, a[4] = {2.72149e-14, 2.7242254e-07, 0.0027354999999999997, 1.0}; expect(0.01f, n, a); }
    { const double n[4] = {2.72149e-15, 8.99814354e-07, 0.00342541, 1.0}, a[4] = {2.72149e-15, 2.72176354e-07, 0.0027354099999999998, 1.0}; expect(0.001f, n, a); }
    { const double n[4] = {2.7214899999999998e-17, 2.7842565354e-07, 0.0027423000999999996, 1.0}, a[4] = {2.7214899999999998e-17, 2.7214927354e-07, 0.0027354001, 1.0}; expect(0.00001f, n, a); }
    /* peak gain of the discretised op-amp filter over 20 Hz .. 20 kHz */
    const float ds[2] = {0.0f, 1.0f};
    const double want[2] = {0.0, 67.26};
    for (int k = 0; k < 2; ++k) {
        rat_iir_c c;
        rat_opamp_coefs(&c, ds[k]);
        double g = -1e9, fpk = 0;
        for (double f = 20; f < 20000; f *= 1.01) {
            const double w = 2 * M_PI * f / PFX_FS;
            double nr = 0, ni = 0, dr = 0, di = 0;
            for (int i = 0; i < 4; ++i) { nr += c.b[i] * cos(w * i); ni -= c.b[i] * sin(w * i); dr += c.a[i] * cos(w * i); di -= c.a[i] * sin(w * i); }
            const double gi = 10 * log10((nr * nr + ni * ni) / (dr * dr + di * di));
            if (gi > g) { g = gi; fpk = f; }
        }
        printf("  op-amp filter peak, Distortion %.0f: %+.2f dB at %.0f Hz (dm-Rat max gain: %+.2f)\n", ds[k], g, fpk, want[k]);
        if (fabs(g - want[k]) > 1.0) { ++failures; printf("FAIL gain\n"); }
    }
    printf(failures ? "FAILED: %d\n" : "PASS (s-domain coefficients match dm-Rat's tests)\n", failures);
    return failures != 0;
}
