/* Small linear RC networks solved per sample (modified nodal analysis), with
 * an optional diode pair on one node.
 *
 * SPDX-License-Identifier: MIT
 * Copyright (C) 2026 Hiro Ikezawa. Original work.
 *
 * A network is a handful of nodes driven by ONE voltage source (the stage in
 * front of it), built from resistors and capacitors to ground, to the source
 * or between nodes. Each capacitor is a trapezoidal companion (conductance
 * 2 C fs plus a history current), so the network is linear per sample:
 *
 *     Y v = y_s vs + h        (h = capacitor history injections)
 *
 * Y depends only on component values and pot positions, so its inverse is
 * built in double precision at the control rate (pfx_net_build) and each
 * sample costs one n x n matrix-vector product. If a diode pair hangs on node
 * p, the rest of the network is its Thevenin source: v_p = v0_p - Z_pp Id(v_p),
 * which is exactly the single-node diode equation of nonlinear.h with
 * G = 1/Z_pp and J = v0_p/Z_pp; the other nodes then follow from column p of
 * Y^-1. This is how a pedal's tone network LOADS its clipper as in the
 * circuit, instead of being a fixed resistor or a filter after the fact.
 */
#ifndef PFX_NETWORK_H
#define PFX_NETWORK_H

#include <math.h>
#include <string.h>
#include "filters.h"
#include "nonlinear.h"

#define PFX_NET_N 6   /* max unknown nodes */
#define PFX_NET_C 6   /* max capacitors */
#define PFX_GND (-1)
#define PFX_SRC (-2)

typedef struct {
    int n, ncap;
    int ia[PFX_NET_C], ib[PFX_NET_C];  /* capacitor terminals: node, n (source) or n+1 (ground) */
    float gc[PFX_NET_C];               /* companion conductances 2 C fs */
    float zi[PFX_NET_N][PFX_NET_N];    /* Y^-1 */
    float w[PFX_NET_N];                /* Y^-1 y_s: response to 1 V of source */
    int port;                          /* diode node, or -1 */
    pfx_node node;                     /* diode solve constants for G = 1/Z_pp */
    float ypp;                         /* 1 / Z_pp */
} pfx_net_c;

typedef struct { float ih[PFX_NET_C]; } pfx_net_s;

/* Build-time description, double precision. */
typedef struct {
    int n;
    double y[PFX_NET_N][PFX_NET_N];
    double ys[PFX_NET_N];
    int ncap, ca[PFX_NET_C], cb[PFX_NET_C];
    double gc[PFX_NET_C];
    double fs;
} pfx_net_b;

static inline void pfx_netb_init(pfx_net_b *b, int n, double fs) {
    memset(b, 0, sizeof *b);
    b->n = n;
    b->fs = fs;
}

/* conductance g between a and b (node index, PFX_GND or PFX_SRC) */
static inline void pfx_netb_g(pfx_net_b *b, int a, int c, double g) {
    if (a >= 0) b->y[a][a] += g;
    if (c >= 0) b->y[c][c] += g;
    if (a >= 0 && c >= 0) { b->y[a][c] -= g; b->y[c][a] -= g; }
    if (a == PFX_SRC && c >= 0) b->ys[c] += g;
    if (c == PFX_SRC && a >= 0) b->ys[a] += g;
}
static inline void pfx_netb_r(pfx_net_b *b, int a, int c, double r) {
    pfx_netb_g(b, a, c, 1.0 / (r < 1.0 ? 1.0 : r));
}
static inline void pfx_netb_cap(pfx_net_b *b, int a, int c, double cap) {
    const double g = 2.0 * cap * b->fs;
    pfx_netb_g(b, a, c, g);
    b->ca[b->ncap] = a;
    b->cb[b->ncap] = c;
    b->gc[b->ncap] = g;
    b->ncap++;
}

/* Invert Y (Gauss-Jordan, partial pivoting, double) and fill the runtime
 * constants. port: diode node or -1; d: that node's diode pair. */
static inline void pfx_net_build(pfx_net_c *c, const pfx_net_b *b, int port, const pfx_diodes *d) {
    const int n = b->n;
    double a[PFX_NET_N][2 * PFX_NET_N];
    for (int i = 0; i < n; ++i)
        for (int j = 0; j < 2 * n; ++j) a[i][j] = j < n ? b->y[i][j] : (j - n == i);
    for (int col = 0; col < n; ++col) {
        int piv = col;
        for (int r = col + 1; r < n; ++r)
            if (fabs(a[r][col]) > fabs(a[piv][col])) piv = r;
        if (piv != col)
            for (int j = 0; j < 2 * n; ++j) { const double t = a[col][j]; a[col][j] = a[piv][j]; a[piv][j] = t; }
        const double inv = 1.0 / a[col][col];
        for (int j = 0; j < 2 * n; ++j) a[col][j] *= inv;
        for (int r = 0; r < n; ++r) {
            if (r == col || a[r][col] == 0.0) continue;
            const double f = a[r][col];
            for (int j = 0; j < 2 * n; ++j) a[r][j] -= f * a[col][j];
        }
    }
    c->n = n;
    c->ncap = b->ncap;
    for (int i = 0; i < n; ++i) {
        double wi = 0.0;
        for (int j = 0; j < n; ++j) {
            c->zi[i][j] = (float)a[i][n + j];
            wi += a[i][n + j] * b->ys[j];
        }
        c->w[i] = (float)wi;
    }
    for (int k = 0; k < b->ncap; ++k) {
        c->ia[k] = b->ca[k] >= 0 ? b->ca[k] : (b->ca[k] == PFX_SRC ? n : n + 1);
        c->ib[k] = b->cb[k] >= 0 ? b->cb[k] : (b->cb[k] == PFX_SRC ? n : n + 1);
        c->gc[k] = (float)b->gc[k];
    }
    c->port = port;
    c->ypp = port >= 0 ? (float)(1.0 / a[port][n + port]) : 0.0f;
    if (port >= 0) pfx_node_set(&c->node, d, c->ypp);
}

/* One sample: source voltage vs in, node voltages out (v[PFX_NET_N]).
 * N and NC are the network's node and capacitor counts and must be compile-
 * time constants at the call site (pass the enum count, not c->n): the
 * function is inlined, so every loop below unrolls and the per-capacitor
 * terminal lookups become fixed loads. Capacitor terminals index an extended
 * vector [v0 .. vN-1, vs, 0] (PFX_SRC -> N, PFX_GND -> N+1), built once. */
static inline void pfx_net_run_n(const pfx_net_c *c, pfx_net_s *s, const pfx_diodes *d,
                                 float vs, float *v, const int N, const int NC) {
    float h[PFX_NET_N + 2] = {0};
    for (int k = 0; k < NC; ++k) {           /* i(a->b) = Gc (va - vb) - Ih */
        h[c->ia[k]] += s->ih[k];
        h[c->ib[k]] -= s->ih[k];
    }
    for (int i = 0; i < N; ++i) {
        float acc = c->w[i] * vs;
        for (int j = 0; j < N; ++j) acc += c->zi[i][j] * h[j];
        v[i] = acc;
    }
    if (c->port >= 0) {
        const int p = c->port;
        const float vp = pfx_diode_fast(d, &c->node, v[p] * c->ypp);
        const float id = (v[p] - vp) * c->ypp; /* current the diodes take */
        for (int i = 0; i < N; ++i) v[i] -= c->zi[i][p] * id;
        v[p] = vp;
    }
    float ext[PFX_NET_N + 2];
    for (int i = 0; i < N; ++i) ext[i] = v[i];
    ext[N] = vs;
    ext[N + 1] = 0.0f;
    for (int k = 0; k < NC; ++k) {
        const float vc = ext[c->ia[k]] - ext[c->ib[k]];
        const float ic = c->gc[k] * vc - s->ih[k];
        s->ih[k] = c->gc[k] * vc + ic;
    }
}

static inline void pfx_net_flush(pfx_net_s *s) {
    for (int k = 0; k < PFX_NET_C; ++k) s->ih[k] = pfx_flush(s->ih[k]);
}

#endif
