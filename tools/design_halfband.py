#!/usr/bin/env python3
"""Design the 2x polyphase-IIR half-band filter used by src/common/oversampling.h.

Independent implementation of the classic elliptic polyphase half-band design
(Valenzuela & Constantinides, "Digital signal processing schemes for efficient
interpolation and decimation", IEE Proc. 1983). Two parallel chains of
first-order allpass sections in z^-2; each chain runs at the base rate.

    python3 tools/design_halfband.py <n_coefs> <transition>

transition is relative to the OVERSAMPLED rate: pass band ends at
(0.25 - transition/2) * fs_os. Prints C initialisers and the measured
stop-band rejection / pass-band ripple.
"""
import cmath
import math
import sys


def transition_param(tbw):
    k = math.tan((1 - tbw * 2) * math.pi / 4) ** 2
    kksqrt = (1 - k * k) ** 0.25
    e = 0.5 * (1 - kksqrt) / (1 + kksqrt)
    e4 = e ** 4
    q = e * (1 + e4 * (2 + e4 * (15 + 150 * e4)))
    return k, q


def acc(q, order, c, num):
    total, i, sign = 0.0, (0 if num else 1), (1 if num else -1)
    while True:
        if num:
            term = q ** (i * (i + 1)) * math.sin((2 * i + 1) * c * math.pi / order) * sign
        else:
            term = q ** (i * i) * math.cos(2 * i * c * math.pi / order) * sign
        total += term
        sign, i = -sign, i + 1
        if abs(term) < 1e-100 or i > 200:
            return total


def design(n, tbw):
    k, q = transition_param(tbw)
    order = 2 * n + 1
    out = []
    for idx in range(n):
        c = idx + 1
        ww = acc(q, order, c, True) * q ** 0.25 / (acc(q, order, c, False) + 0.5)
        x = math.sqrt((1 - ww * ww * k) * (1 - ww * ww / k)) / (1 + ww * ww)
        out.append((1 - x) / (1 + x))
    return out


def response(coefs, f):
    """Low-pass half-band response at normalised frequency f (0..0.5 of fs_os)."""
    z2 = cmath.exp(-2j * math.pi * f * 2)  # z^-2
    a = b = 1
    for i, c in enumerate(coefs):
        ap = (c + z2) / (1 + c * z2)
        if i % 2 == 0:
            a *= ap
        else:
            b *= ap
    return 0.5 * (a + cmath.exp(-2j * math.pi * f) * b)


if __name__ == "__main__":
    n, tbw = int(sys.argv[1]), float(sys.argv[2])
    coefs = design(n, tbw)
    stop = min(-20 * math.log10(abs(response(coefs, 0.25 + tbw / 2 + i * (0.25 - tbw / 2) / 400)) + 1e-30)
               for i in range(401))
    ripple = max(abs(20 * math.log10(abs(response(coefs, i * (0.25 - tbw / 2) / 400)))) for i in range(401))
    print(f"/* {n} coefs, transition {tbw}: stop-band {stop:.1f} dB, pass-band ripple {ripple:.2e} dB */")
    print("{" + ", ".join(f"{c:.12f}f" for c in coefs) + "}")
