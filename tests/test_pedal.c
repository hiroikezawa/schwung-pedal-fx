/* Host test for one pedal .so, loaded through the real v2 ABI (dlopen +
 * move_audio_fx_init_v2), exactly as the chain host loads it.
 *
 *   test_pedal <path/to/pfx-xxx.so>
 *
 * SPDX-License-Identifier: MIT
 */
#define _GNU_SOURCE
#include "host/audio_fx_api_v2.h"
#include <dlfcn.h>
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define FS 44100
#define BLOCK 128
static audio_fx_api_v2_t *api;
static int failures;
static char keys[16][32];
static int nkeys;

#define CHECK(cond, ...) do { if (!(cond)) { ++failures; printf("FAIL %s:%d: ", __func__, __LINE__); printf(__VA_ARGS__); printf("\n"); } } while (0)

static unsigned rng = 12345;
static float frand(void) { rng = rng * 1664525u + 1013904223u; return (float)((rng >> 8) & 0xffff) / 32768.0f - 1.0f; }

static void setf(void *p, const char *k, float v) {
    char b[32];
    snprintf(b, sizeof b, "%.4f", v);
    api->set_param(p, k, b);
}

/* Run `seconds` of a generator through the plugin. Returns peak |out| and
 * flags any non-finite reconstruction (an int16 cannot hold NaN, so the
 * plugin's guard is what we test: a NaN would come out as silence/garbage;
 * we additionally watch for DC and runaway). */
typedef float (*gen_fn)(int n, int ch);
static double run(void *p, gen_fn g, double seconds, double *rms_out, double *dc_out, int16_t *keep, int keep_len) {
    int16_t buf[BLOCK * 2];
    const int total = (int)(seconds * FS);
    double peak = 0, sum2 = 0, sum = 0;
    int count = 0;
    for (int n0 = 0; n0 < total; n0 += BLOCK) {
        for (int i = 0; i < BLOCK; ++i)
            for (int c = 0; c < 2; ++c) {
                float x = g(n0 + i, c) * 32767.0f;
                buf[2 * i + c] = (int16_t)fmaxf(-32768.0f, fminf(32767.0f, x));
            }
        api->process_block(p, buf, BLOCK);
        for (int i = 0; i < BLOCK * 2; ++i) {
            const double y = buf[i] / 32768.0;
            if (fabs(y) > peak) peak = fabs(y);
            if (n0 > total / 2) { sum2 += y * y; sum += y; ++count; }
            if (keep && n0 * 2 + i < keep_len) keep[n0 * 2 + i] = buf[i];
        }
    }
    if (rms_out) *rms_out = count ? sqrt(sum2 / count) : 0;
    if (dc_out) *dc_out = count ? sum / count : 0;
    return peak;
}

static double g_freq = 1000, g_amp = 0.25;
static float gen_silence(int n, int c) { (void)n; (void)c; return 0; }
static float gen_sine(int n, int c) { (void)c; return (float)(g_amp * sin(2 * M_PI * g_freq * n / FS)); }
static float gen_impulse(int n, int c) { (void)c; return n == 100 ? 1.0f : 0.0f; }
static float gen_noise(int n, int c) { (void)n; (void)c; return (float)(g_amp * frand()); }
static float gen_sweep(int n, int c) {
    (void)c;
    const double t = (double)n / FS, T = 2.0, f0 = 20, f1 = 20000, k = log(f1 / f0);
    return (float)(g_amp * sin(2 * M_PI * f0 * T / k * (exp(t / T * k) - 1)));
}
static float gen_left_only(int n, int c) { return c == 0 ? (float)(0.3 * sin(2 * M_PI * 220 * n / FS)) : 0.0f; }
static float gen_mono(int n, int c) { (void)c; return (float)(0.3 * sin(2 * M_PI * 110 * n / FS) + 0.1 * sin(2 * M_PI * 1700 * n / FS)); }

static void *fresh(void) {
    void *p = api->create_instance(".", NULL);
    CHECK(p, "create_instance returned NULL");
    return p;
}

static void test_metadata(void *p) {
    char buf[8192];
    CHECK(api->get_param(p, "name", buf, sizeof buf) > 0, "name");
    CHECK(api->get_param(p, "chain_params", buf, sizeof buf) > 0 && buf[0] == '[', "chain_params");
    CHECK(api->get_param(p, "ui_hierarchy", buf, sizeof buf) > 0 && strstr(buf, "\"presets\""), "ui_hierarchy");
    int n = api->get_param(p, "state", buf, sizeof buf);
    CHECK(n > 0, "state");
    /* keys = every "key": after "preset" */
    const char *q = strstr(buf, "\"preset\":");
    q = q ? strchr(q, ',') : NULL;
    nkeys = 0;
    while (q && nkeys < 16) {
        const char *a = strchr(q, '"'), *b = a ? strchr(a + 1, '"') : NULL;
        if (!b) break;
        snprintf(keys[nkeys], sizeof keys[0], "%.*s", (int)(b - a - 1), a + 1);
        ++nkeys;
        q = strchr(b, ',');
    }
    CHECK(nkeys >= 8, "expected at least 8 knobs in state, got %d", nkeys);
    /* switches beyond the eight knobs: enum on the wire, index or name */
    for (int i = 8; i < nkeys; ++i) {
        char v[16];
        api->set_param(p, keys[i], "1");
        api->get_param(p, keys[i], v, sizeof v);
        CHECK(!strcmp(v, "1"), "switch %s: set 1 -> get %s", keys[i], v);
        api->set_param(p, keys[i], "0.3");
        api->get_param(p, keys[i], v, sizeof v);
        CHECK(!strcmp(v, "0"), "switch %s: 0.3 must round to 0, got %s", keys[i], v);
        api->set_param(p, keys[i], "bogus");
        api->get_param(p, keys[i], v, sizeof v);
        CHECK(!strcmp(v, "0"), "switch %s: garbage must be ignored, got %s", keys[i], v);
    }
    CHECK(api->get_param(p, "preset_count", buf, sizeof buf) > 0 && atoi(buf) >= 5, "preset_count >= 5");
    char small[4];
    CHECK(api->get_param(p, "chain_params", small, sizeof small) == -1 && small[0] == 0, "short buffer must fail cleanly");
    printf("  knobs:");
    for (int i = 0; i < nkeys; ++i) printf(" %s", keys[i]);
    printf("\n");
}

static void test_silence(void *p) {
    double rms, dc;
    const double peak = run(p, gen_silence, 1.0, &rms, &dc, NULL, 0);
    CHECK(peak == 0.0, "silence in -> %g peak out (expected exact silence)", peak);
}

static void test_impulse(void *p) {
    static int16_t out[FS * 2];
    run(p, gen_impulse, 1.0, NULL, NULL, out, FS * 2);
    double tail = 0;
    for (int i = FS; i < FS * 2; ++i) tail += fabs(out[i] / 32768.0);
    CHECK(tail < 1e-3 * FS, "impulse response does not decay: tail sum %g", tail);
}

static void test_sines(void *p) {
    const double f[] = {100, 1000, 5000};
    for (int i = 0; i < 3; ++i) {
        g_freq = f[i]; g_amp = 0.25;
        double rms, dc;
        const double peak = run(p, gen_sine, 0.5, &rms, &dc, NULL, 0);
        CHECK(rms > 1e-4 && peak <= 1.0, "%g Hz: rms %g peak %g", f[i], rms, peak);
        CHECK(fabs(dc) < 0.01, "%g Hz: DC offset %g", f[i], dc);
        printf("  sine %5.0f Hz @ -12 dBFS: out rms %.1f dBFS, DC %.5f\n", f[i], 20 * log10(rms + 1e-12), dc);
    }
}

static void test_sweep_noise_hot(void *p) {
    double rms, dc, peak;
    g_amp = 0.5;
    peak = run(p, gen_sweep, 2.0, &rms, &dc, NULL, 0);
    CHECK(rms > 1e-4 && peak <= 1.0, "sweep rms %g", rms);
    peak = run(p, gen_noise, 1.0, &rms, &dc, NULL, 0);
    CHECK(rms > 1e-4 && fabs(dc) < 0.01, "noise rms %g dc %g", rms, dc);
    g_amp = 1.0; g_freq = 60;  /* full-scale, clipped at the int16 input */
    peak = run(p, gen_sine, 1.0, &rms, &dc, NULL, 0);
    CHECK(rms > 1e-4 && fabs(dc) < 0.02, "hot 60 Hz rms %g dc %g", rms, dc);
    for (int i = 0; i < nkeys; ++i) setf(p, keys[i], 1.0f); /* everything up */
    peak = run(p, gen_sine, 1.0, &rms, &dc, NULL, 0);
    CHECK(rms > 1e-4 && (fabs(dc) < 0.02 || peak >= 0.99), "all-max hot: rms %g dc %g", rms, dc);
    printf("  all knobs max, full-scale 60 Hz: out rms %.1f dBFS peak %.2f\n", 20 * log10(rms), peak);
}

/* Every corner of the 8-dimensional knob cube, 0.1 s each. */
static void test_extremes(void *p) {
    g_freq = 220; g_amp = 0.5;
    int bad = 0;
    for (int mask = 0; mask < 256; ++mask) {
        for (int i = 0; i < nkeys; ++i) setf(p, keys[i], (mask >> (i % 8)) & 1 ? 1.0f : 0.0f);
        double rms, dc;
        /* 0.3 s per corner, DC over the last half: a corner change is an abrupt
         * jump of all eight knobs (up to +38 dB of gain), and the RAT's C7
         * settles through the diodes, as on the pedal */
        const double peak = run(p, gen_sine, 0.3, &rms, &dc, NULL, 0);
        /* int16 clipping of an asymmetric wave makes DC; only judge the pedal */
        if (peak < 0.99 && !(fabs(dc) < 0.05)) { ++bad; if (getenv("VERBOSE")) printf("    corner %d: dc %.3f peak %.2f\n", mask, dc, peak); }
    }
    CHECK(bad == 0, "%d of 256 unclipped knob corners left DC >= 0.05", bad);
}

/* Random automation of every knob, every block: stays finite and alive. */
static void test_automation(void *p) {
    int16_t buf[BLOCK * 2];
    long nonzero = 0;
    for (int b = 0; b < 2000; ++b) {
        for (int i = 0; i < nkeys; ++i) if (frand() > 0.5f) setf(p, keys[i], 0.5f * (frand() + 1.0f));
        for (int i = 0; i < BLOCK; ++i) {
            const float x = 0.25f * sinf(2 * (float)M_PI * 330 * (b * BLOCK + i) / FS);
            buf[2 * i] = buf[2 * i + 1] = (int16_t)(x * 32767);
        }
        api->process_block(p, buf, BLOCK);
        for (int i = 0; i < BLOCK * 2; ++i) nonzero += buf[i] != 0;
    }
    CHECK(nonzero > 2000L * BLOCK, "automation left the output (nearly) silent: %ld", nonzero);
}

/* Zipper / click: jump a gain-type knob across its range on a quiet sine and
 * compare the largest sample step with the steady signal's. */
static int has_key(const char *k) { for (int i = 0; i < nkeys; ++i) if (!strcmp(keys[i], k)) return 1; return 0; }
static double max_step(void *p, int blocks, double *prev) {
    int16_t buf[BLOCK * 2];
    static long n = 0;
    double worst = 0;
    for (int b = 0; b < blocks; ++b) {
        for (int i = 0; i < BLOCK; ++i, ++n) {
            const float x = 0.05f * sinf(2 * (float)M_PI * 220 * n / FS);
            buf[2 * i] = buf[2 * i + 1] = (int16_t)(x * 32767);
        }
        api->process_block(p, buf, BLOCK);
        for (int i = 0; i < BLOCK; ++i) {
            const double j = fabs(buf[2 * i] / 32768.0 - *prev);
            if (j > worst) worst = j;
            *prev = buf[2 * i] / 32768.0;
        }
    }
    return worst;
}
static void test_zipper(void *p) {
    const char *gain_keys[] = {"mix", "output", "input", "level", "volume", NULL};
    for (int k = 0; gain_keys[k]; ++k) {
        if (!has_key(gain_keys[k])) continue;
        double prev = 0;
        setf(p, gain_keys[k], 0.2f);
        max_step(p, 200, &prev);
        const double before = max_step(p, 100, &prev);
        setf(p, gain_keys[k], 0.8f);
        const double moved = max_step(p, 20, &prev);
        max_step(p, 100, &prev);
        const double after = max_step(p, 100, &prev);
        const double steady = before > after ? before : after;
        setf(p, gain_keys[k], 0.5f);
        max_step(p, 50, &prev);
        /* a ramped gain never steps further than the louder steady state */
        CHECK(moved < steady * 1.25 + 1e-3, "%s 0.2 -> 0.8 steps %.4f vs steady %.4f", gain_keys[k], moved, steady);
        printf("  zipper %-7s steady step %.4f, while jumping 0.2->0.8 %.4f\n", gain_keys[k], steady, moved);
    }
}

/* Output must be a pure gain, even when the wet signal is far above full scale
 * before it: Output 0 % (-18 dB) and 5 % (-16.2 dB) must differ by exactly
 * 1.8 dB. v0.1-0.4 clipped the wet signal at +-4 before Output, so it came
 * out flattened however far Output was turned down (MT-2, Level 75 %, Low and
 * Mid boosted, 150 Hz: 65 % of samples at that limit). */
static int has_key(const char *k);
static void test_output_headroom(void *p) {
    double peak[2], flat[2];
    for (int k = 0; k < 2; ++k) {
        for (int i = 0; i < nkeys && i < 8; ++i) setf(p, keys[i], 1.0f);
        for (int i = 0; i < nkeys; ++i)                    /* Drive / Gain / Distortion at noon */
            if (!strcmp(keys[i], "drive") || !strcmp(keys[i], "distortion") || !strcmp(keys[i], "gain"))
                setf(p, keys[i], 0.5f);
        if (has_key("level")) setf(p, "level", 0.75f);    /* +10 dB: hot, but */
        if (has_key("volume")) setf(p, "volume", 0.75f);  /* fits after -18 dB */
        if (has_key("input")) setf(p, "input", 0.5f);
        if (has_key("high")) setf(p, "high", 0.5f);       /* MT-2: Low + Mid boosted, as reported */
        if (has_key("mid_freq")) setf(p, "mid_freq", 0.0f);
        setf(p, "output", k ? 0.05f : 0.0f);
        g_freq = 150; g_amp = 0.5;
        /* run() restarts the sine at phase 0, a kink that rings through
         * MT-2's high-gain, high-Q path; judge only the settled second half */
        static int16_t kept[FS];
        run(p, gen_sine, 0.5, NULL, NULL, kept, FS);
        peak[k] = 0;
        for (int i = FS / 2; i < FS; ++i) peak[k] = fmax(peak[k], fabs(kept[i] / 32768.0));
        /* a clip flattens the tops: count samples pinned at the peak */
        int pinned = 0;
        for (int i = FS / 2; i < FS; ++i) pinned += fabs(kept[i] / 32768.0) > 0.995 * peak[k];
        flat[k] = pinned / (double)(FS / 2);
        if (getenv("VERBOSE")) { char st[1024]; api->get_param(p, "state", st, sizeof st); printf("    %s -> peak %.3f\n", st, peak[k]); }
    }
    const double db = 20 * log10(peak[1] / peak[0]);
    if (peak[1] < 0.99) {
        CHECK(fabs(db - 1.8) < 0.15, "Output 0%%->5%% changed the peak by %.2f dB, not 1.8", db);
        CHECK(flat[0] < 0.2, "%.0f%% of samples pinned at the peak with Output at -18 dB: clipped before Output", 100 * flat[0]);
    }
    printf("  output headroom: Output 0%% -> 5%% = %+.2f dB (peaks %.3f / %.3f), %.0f%% of samples at the peak\n",
           db, peak[0], peak[1], 100 * flat[0]);
}

static void test_bypass(void *p) {
    setf(p, "mix", 0.0f);
    setf(p, "output", 0.5f);
    for (int i = 0; i < 20; ++i) { g_freq = 440; run(p, gen_noise, 0.05, NULL, NULL, NULL, 0); } /* settle */
    int16_t in[BLOCK * 2], out[BLOCK * 2];
    int mismatch = 0;
    for (int b = 0; b < 100; ++b) {
        for (int i = 0; i < BLOCK * 2; ++i) in[i] = (int16_t)(frand() * 20000);
        memcpy(out, in, sizeof in);
        api->process_block(p, out, BLOCK);
        mismatch += memcmp(in, out, sizeof in) != 0;
    }
    CHECK(mismatch == 0, "Mix 0 / Output 0 dB is not bit-exact bypass (%d blocks differ)", mismatch);
}

static void *test_stereo(void *p) {
    static int16_t out[FS];
    run(p, gen_left_only, 0.5, NULL, NULL, out, FS);
    int right_nonzero = 0;
    for (int i = 1; i < FS; i += 2) right_nonzero += out[i] != 0;
    CHECK(right_nonzero == 0, "left-only input leaked into right (%d samples)", right_nonzero);
    api->destroy_instance(p);
    p = fresh();
    run(p, gen_mono, 0.5, NULL, NULL, out, FS);
    int differ = 0;
    for (int i = 0; i < FS; i += 2) differ += out[i] != out[i + 1];
    CHECK(differ == 0, "mono input produced a non-mono output (%d frames)", differ);
    return p;
}

static void test_state_presets(void *p) {
    char a[2048], b[2048], name[64];
    int count;
    api->get_param(p, "preset_count", a, sizeof a);
    count = atoi(a);
    for (int i = 0; i < count; ++i) {
        snprintf(a, sizeof a, "%d", i);
        api->set_param(p, "preset", a);
        api->get_param(p, "preset", b, sizeof b);
        CHECK(atoi(b) == i, "preset %d not selected", i);
        CHECK(api->get_param(p, "preset_name", name, sizeof name) > 0, "preset_name %d", i);
        printf("  preset %d: %s\n", i, name);
        double rms, dc;
        g_freq = 110; g_amp = 0.25;
        run(p, gen_sine, 0.3, &rms, &dc, NULL, 0);
        CHECK(rms > 1e-3, "preset %s is silent", name);
    }
    /* round trip */
    for (int i = 0; i < nkeys; ++i) setf(p, keys[i], 0.123f * (i + 1));
    api->get_param(p, "state", a, sizeof a);
    void *q = fresh();
    api->set_param(q, "state", a);
    api->get_param(q, "state", b, sizeof b);
    CHECK(!strcmp(a, b), "state round trip:\n  %s\n  %s", a, b);
    api->set_param(q, "state", "{\"v\":1,\"distortion\":NaN}");
    api->set_param(q, "state", "{\"v\":2}");
    api->set_param(q, "state", "garbage");
    api->set_param(q, keys[0], "nan");
    api->set_param(q, keys[0], "7");  /* clamps to 1 */
    api->get_param(q, "state", b, sizeof b);
    CHECK(!strncmp(a + 0, b + 0, 10), "bad state input changed the state");
    api->get_param(q, keys[0], b, sizeof b);
    CHECK(atof(b) == 1.0, "out-of-range knob not clamped: %s", b);
    api->destroy_instance(q);
}

int main(int argc, char **argv) {
    if (argc < 2) { fprintf(stderr, "usage: %s pedal.so\n", argv[0]); return 2; }
    void *lib = dlopen(argv[1], RTLD_NOW | RTLD_LOCAL);
    if (!lib) { fprintf(stderr, "%s\n", dlerror()); return 1; }
    audio_fx_init_v2_fn init = (audio_fx_init_v2_fn)dlsym(lib, AUDIO_FX_INIT_V2_SYMBOL);
    CHECK(init, "no %s export", AUDIO_FX_INIT_V2_SYMBOL);
    if (!init) return 1;
    host_api_v1_t host;
    memset(&host, 0, sizeof host);
    host.api_version = 1; host.sample_rate = 48000; host.frames_per_block = BLOCK;
    CHECK(init(&host) == NULL, "must refuse a 48 kHz host");
    host.sample_rate = FS;
    api = init(&host);
    CHECK(api && api->api_version == 2, "init");
    printf("%s\n", argv[1]);
    void *p;
#define RUN(t) do { p = fresh(); t(p); api->destroy_instance(p); } while (0)
    p = fresh(); test_metadata(p); api->destroy_instance(p);
    RUN(test_silence);
    RUN(test_impulse);
    RUN(test_sines);
    RUN(test_sweep_noise_hot);
    RUN(test_extremes);
    RUN(test_automation);
    RUN(test_output_headroom);
    RUN(test_zipper);
    RUN(test_bypass);
    p = fresh(); p = test_stereo(p); api->destroy_instance(p);
    RUN(test_state_presets);
    /* pool exhaustion must fail cleanly, never crash */
    void *many[40]; int got = 0;
    for (int i = 0; i < 40; ++i) if ((many[i] = api->create_instance(".", NULL))) ++got; else break;
    CHECK(got >= 8, "only %d instances", got);
    for (int i = 0; i < got; ++i) api->destroy_instance(many[i]);
    printf(failures ? "FAILED: %d\n" : "PASS\n", failures);
    return failures != 0;
}
