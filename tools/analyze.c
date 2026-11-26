/* Offline analysis of one pedal .so (loaded through the v2 ABI).
 *
 *   analyze <pedal.so> <outdir> [mode]
 *
 * mode "all" (default): for Drive/Gain/Distortion = 0.25/0.5/0.75/1.0
 *   - writes <outdir>/drive_XX.wav: pluck phrase, 110 Hz, 1 kHz, sweep
 *   - prints loudness vs input, THD at 110 Hz / 1 kHz, 2nd/3rd harmonic,
 *     even/odd balance and the alias floor (non-harmonic energy of a steady
 *     sine, which for a deterministic system is aliasing + rounding)
 * mode "resp <knob> <v...>": small-signal response (-50 dBFS) for each value
 *   of <knob>, other knobs at default
 * mode "cpu": microseconds per 128-frame block, hot signal, all knobs moving
 * mode "cal": output/input loudness at the defaults (for *_CAL constants)
 * Any extra "key=value" arguments set knobs before measuring.
 *
 * SPDX-License-Identifier: MIT
 */
#define _GNU_SOURCE
#include "host/audio_fx_api_v2.h"
#include <dlfcn.h>
#include <math.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

#define FS 44100
#define BLOCK 128
static audio_fx_api_v2_t *api;
static char keys[16][32];
static char overrides[16][64];
static int noverrides;

static void setf(void *p, const char *k, double v) {
    char b[32];
    snprintf(b, sizeof b, "%.4f", v);
    api->set_param(p, k, b);
}

static void *fresh(void) {
    void *p = api->create_instance(".", NULL);
    for (int i = 0; i < noverrides; ++i) {
        char k[64];
        snprintf(k, sizeof k, "%s", overrides[i]);
        char *eq = strchr(k, '=');
        *eq = 0;
        api->set_param(p, k, eq + 1);
    }
    return p;
}

/* knob 1 used to be the drive control; since v0.6 it is Mix, so the drive
 * knob is looked up by name */
static const char *drive_key(void) {
    static const char *names[] = {"drive", "distortion", "gain", NULL};
    for (int n = 0; names[n]; ++n)
        for (int i = 0; i < 16 && keys[i][0]; ++i)
            if (!strcmp(keys[i], names[n])) return keys[i];
    return keys[0];
}

static void load_keys(void) {
    void *p = api->create_instance(".", NULL);
    char buf[2048];
    api->get_param(p, "state", buf, sizeof buf);
    const char *q = strchr(strstr(buf, "\"preset\":"), ',');
    for (int n = 0; q && n < 8; ++n) {
        const char *a = strchr(q, '"'), *b = strchr(a + 1, '"');
        snprintf(keys[n], sizeof keys[0], "%.*s", (int)(b - a - 1), a + 1);
        q = strchr(b, ',');
    }
    api->destroy_instance(p);
}

/* Mono in (both channels), float out (left). */
static void process(void *p, const float *in, float *out, int n) {
    int16_t buf[BLOCK * 2];
    for (int n0 = 0; n0 < n; n0 += BLOCK) {
        const int m = n - n0 < BLOCK ? n - n0 : BLOCK;
        for (int i = 0; i < m; ++i) {
            const float x = fmaxf(-32768.0f, fminf(32767.0f, in[n0 + i] * 32768.0f));
            buf[2 * i] = buf[2 * i + 1] = (int16_t)lrintf(x);
        }
        api->process_block(p, buf, m);
        for (int i = 0; i < m; ++i) out[n0 + i] = buf[2 * i] / 32768.0f;
    }
}

static void write_wav(const char *path, const float *x, int n) {
    FILE *f = fopen(path, "wb");
    if (!f) { perror(path); return; }
    const uint32_t data = (uint32_t)n * 4, riff = 36 + data, fmt = 16, rate = FS, brate = FS * 4;
    const uint16_t pcm = 1, ch = 2, align = 4, bits = 16;
    fwrite("RIFF", 1, 4, f); fwrite(&riff, 4, 1, f); fwrite("WAVEfmt ", 1, 8, f);
    fwrite(&fmt, 4, 1, f); fwrite(&pcm, 2, 1, f); fwrite(&ch, 2, 1, f); fwrite(&rate, 4, 1, f);
    fwrite(&brate, 4, 1, f); fwrite(&align, 2, 1, f); fwrite(&bits, 2, 1, f);
    fwrite("data", 1, 4, f); fwrite(&data, 4, 1, f);
    for (int i = 0; i < n; ++i) {
        const int16_t s = (int16_t)lrintf(fmaxf(-32768.0f, fminf(32767.0f, x[i] * 32767.0f)));
        fwrite(&s, 2, 1, f); fwrite(&s, 2, 1, f);
    }
    fclose(f);
}

/* Amplitude of the component at exactly f Hz in x[0..n) (n = whole cycles). */
static double tone_amp(const float *x, int n, double f) {
    double re = 0, im = 0;
    const double w = 2 * M_PI * f / FS;
    for (int i = 0; i < n; ++i) { re += x[i] * cos(w * i); im -= x[i] * sin(w * i); }
    return 2 * sqrt(re * re + im * im) / n;
}

typedef struct { double gain_db, thd, h2, h3, even_odd, alias_db; } sine_stats;

/* One second of steady state; f integer so the window is whole cycles. */
static sine_stats sine_measure(void *p, double f, double amp) {
    static float in[2 * FS], out[2 * FS];
    for (int i = 0; i < 2 * FS; ++i) in[i] = (float)(amp * sin(2 * M_PI * f * i / FS));
    process(p, in, out, 2 * FS);
    const float *y = out + FS;
    double total = 0, mean = 0;
    for (int i = 0; i < FS; ++i) mean += y[i];
    mean /= FS;
    for (int i = 0; i < FS; ++i) total += (y[i] - mean) * (y[i] - mean);
    total /= FS;
    double harm = 0, even = 0, odd = 0, a1 = 0, a2 = 0, a3 = 0;
    for (int k = 1; k * f < FS / 2; ++k) {
        const double a = tone_amp(y, FS, k * f);
        harm += a * a / 2;
        if (k == 1) a1 = a;
        else if (k % 2) odd += a * a;
        else even += a * a;
        if (k == 2) a2 = a;
        if (k == 3) a3 = a;
    }
    sine_stats s;
    s.gain_db = 20 * log10(sqrt(total) / (amp / sqrt(2)) + 1e-12);
    s.thd = sqrt(even + odd) / (a1 + 1e-12);
    s.h2 = 20 * log10(a2 / (a1 + 1e-12) + 1e-12);
    s.h3 = 20 * log10(a3 / (a1 + 1e-12) + 1e-12);
    s.even_odd = 10 * log10((even + 1e-20) / (odd + 1e-20));
    s.alias_db = 10 * log10(fmax(total - harm, 1e-20) / (harm + 1e-20));
    return s;
}

/* A plucked-string phrase (Karplus-Strong), peaks ~-6 dBFS: guitar-like
 * dynamics for the WAVs and for the loudness calibration. */
static int pluck_phrase(float *x, int max) {
    const double notes[] = {82.41, 110.0, 146.83, 196.0, 82.41, 123.47, 164.81, 220.0};
    int n = 0;
    unsigned r = 1;
    for (int k = 0; k < 8 && n < max; ++k) {
        const int period = (int)lrint(FS / notes[k]), len = FS / 2;
        float line[1024];
        for (int i = 0; i < period; ++i) { r = r * 1664525u + 1013904223u; line[i] = ((r >> 9) & 0xffff) / 32768.0f - 1.0f; }
        for (int i = 0; i < len && n < max; ++i, ++n) {
            const int j = i % period, jn = (i + 1) % period;
            const float v = line[j];
            line[j] = 0.996f * 0.5f * (line[j] + line[jn]);
            x[n] = 0.5f * v * (k % 4 == 3 ? 0.35f : 1.0f); /* every 4th note soft */
        }
    }
    return n;
}

static double rms(const float *x, int n) {
    double s = 0;
    for (int i = 0; i < n; ++i) s += (double)x[i] * x[i];
    return sqrt(s / n);
}

static void mode_all(const char *outdir) {
    static float in[12 * FS], out[12 * FS];
    const double drives[] = {0.25, 0.5, 0.75, 1.0};
    printf("%-6s %8s %8s %8s %8s %8s %9s %9s %9s\n", "drive", "pluckdB", "THD110", "THD1k",
           "H2/H1", "H3/H1", "even/odd", "alias1k", "alias5k");
    for (int d = 0; d < 4; ++d) {
        void *p = fresh();
        setf(p, drive_key(), drives[d]);
        int n = pluck_phrase(in, 4 * FS);
        for (int i = 0; i < FS; ++i) in[n + i] = (float)(0.25 * sin(2 * M_PI * 110 * i / FS));
        for (int i = 0; i < FS; ++i) in[n + FS + i] = (float)(0.25 * sin(2 * M_PI * 1000 * i / FS));
        const double T = 3.0, k = log(1000.0);
        for (int i = 0; i < 3 * FS; ++i) in[n + 2 * FS + i] = (float)(0.25 * sin(2 * M_PI * 20 * T / k * (exp((double)i / FS / T * k) - 1)));
        const int total = n + 5 * FS;
        process(p, in, out, total);
        char path[512];
        snprintf(path, sizeof path, "%s/drive_%03d.wav", outdir, (int)lrint(drives[d] * 100));
        write_wav(path, out, total);
        const double pluck_db = 20 * log10(rms(out, n) / rms(in, n));
        api->destroy_instance(p);
        p = fresh(); setf(p, drive_key(), drives[d]);
        const sine_stats s110 = sine_measure(p, 110, 0.25);
        const sine_stats s1k = sine_measure(p, 1000, 0.25);
        const sine_stats s5k = sine_measure(p, 5000, 0.25);
        api->destroy_instance(p);
        printf("%-6.2f %+8.1f %7.1f%% %7.1f%% %+8.1f %+8.1f %+9.1f %+9.1f %+9.1f\n", drives[d], pluck_db,
               100 * s110.thd, 100 * s1k.thd, s1k.h2, s1k.h3, s1k.even_odd, s1k.alias_db, s5k.alias_db);
    }
    printf("(pluckdB: output vs input RMS of the pluck phrase; THD at -12 dBFS; alias = non-harmonic / harmonic energy)\n");
}

static void mode_resp(const char *knob, int nv, char **vals) {
    const double f[] = {40, 60, 80, 100, 150, 200, 300, 400, 500, 700, 1000, 1500, 2000, 3000, 4000, 5000, 7000, 10000, 15000};
    const int nf = (int)(sizeof f / sizeof f[0]);
    printf("%-7s", "Hz");
    for (int i = 0; i < nf; ++i) printf(" %6.0f", f[i]);
    printf("\n");
    for (int v = 0; v < nv; ++v) {
        printf("%-7s", vals[v]);
        double ref = 0;
        double db[32];
        for (int i = 0; i < nf; ++i) {
            void *p = fresh();
            api->set_param(p, knob, vals[v]);
            const sine_stats s = sine_measure(p, f[i], pow(10, -50 / 20.0));
            api->destroy_instance(p);
            db[i] = s.gain_db;
            if (f[i] == 1000) ref = s.gain_db;
        }
        (void)ref;
        for (int i = 0; i < nf; ++i) printf(" %+6.1f", db[i]);
        printf("\n");
    }
    printf("(absolute small-signal gain in dB at -50 dBFS)\n");
}

/* Strongest spectral lines of a steady sine through the pedal (1 Hz bins). */
static void mode_spec(double f, double amp) {
    static float in[2 * FS], out[2 * FS];
    void *p = fresh();
    for (int i = 0; i < 2 * FS; ++i) in[i] = (float)(amp * sin(2 * M_PI * f * i / FS));
    process(p, in, out, 2 * FS);
    api->destroy_instance(p);
    static double mag[FS / 2];
    for (int b = 1; b < FS / 2; b += 1) mag[b] = tone_amp(out + FS, FS, b);
    for (int t = 0; t < 16; ++t) {
        int best = 1;
        for (int b = 1; b < FS / 2; ++b) if (mag[b] > mag[best]) best = b;
        printf("  %6d Hz %+7.1f dB%s\n", best, 20 * log10(mag[best] + 1e-12), fmod(best, f) == 0 ? "  (harmonic)" : "");
        mag[best] = 0;
    }
}

static void mode_cpu1(int mono);
static void mode_cpu(void) { mode_cpu1(0); mode_cpu1(1); }
static void mode_cpu1(int mono) {
    void *p = fresh();
    int16_t buf[BLOCK * 2];
    unsigned r = 7;
    const int blocks = 20000; /* 58 s of audio */
    struct timespec t0, t1;
    clock_gettime(CLOCK_MONOTONIC, &t0);
    for (int b = 0; b < blocks; ++b) {
        if (b % 64 == 0) setf(p, drive_key(), (b / 64) % 2 ? 0.9 : 0.4); /* keep coefficient updates alive */
        for (int i = 0; i < BLOCK * 2; ++i) { r = r * 1664525u + 1013904223u; buf[i] = (int16_t)((int)(r >> 16) - 32768) / 2; }
        if (mono) for (int i = 0; i < BLOCK; ++i) buf[2 * i + 1] = buf[2 * i];
        api->process_block(p, buf, BLOCK);
    }
    clock_gettime(CLOCK_MONOTONIC, &t1);
    const double us = ((t1.tv_sec - t0.tv_sec) * 1e9 + (t1.tv_nsec - t0.tv_nsec)) / 1e3 / blocks;
    printf("cpu %s: %.2f us per 128-frame stereo block = %.2f%% of one core (block period %.0f us)\n",
           mono ? "mono  " : "stereo", us, 100 * us / (1e6 * BLOCK / FS), 1e6 * BLOCK / FS);
    api->destroy_instance(p);
}

/* Loudness of every factory preset on the pluck phrase (dB vs input). */
static void mode_presets(void) {
    static float in[4 * FS], out[4 * FS];
    const int n = pluck_phrase(in, 4 * FS);
    void *q = fresh();
    char buf[64];
    api->get_param(q, "preset_count", buf, sizeof buf);
    const int count = atoi(buf);
    api->destroy_instance(q);
    for (int i = 0; i < count; ++i) {
        void *p = fresh();
        snprintf(buf, sizeof buf, "%d", i);
        api->set_param(p, "preset", buf);
        char name[64];
        api->get_param(p, "preset_name", name, sizeof name);
        process(p, in, out, FS / 4); /* let the preset's smoothing land */
        process(p, in, out, n);
        const sine_stats s = sine_measure(p, 220, 0.25);
        printf("  %-14s pluck %+6.1f dB   THD(220 Hz, -12 dBFS) %5.1f%%\n", name, 20 * log10(rms(out, n) / rms(in, n)), 100 * s.thd);
        api->destroy_instance(p);
    }
}

/* Dynamics: output level of a 1 kHz sine and of the pluck phrase as the input
 * level rises, at the current knob settings. A slope of 1 dB/dB is clean, 0
 * is a brick-wall limiter. */
static void mode_dyn(void) {
    static float in[4 * FS], out[4 * FS], ref[4 * FS];
    const int n = pluck_phrase(ref, 4 * FS);
    printf("in dBFS   sine1k out  slope   pluck out  slope\n");
    double ps = 0, pp = 0;
    for (int db = -40; db <= 0; db += 4) {
        const double g = pow(10, db / 20.0);
        void *p = fresh();
        for (int i = 0; i < FS; ++i) in[i] = (float)(g * sin(2 * M_PI * 1000 * i / FS));
        process(p, in, out, FS);
        const double s = 20 * log10(rms(out + FS / 2, FS / 2) + 1e-12);
        api->destroy_instance(p);
        p = fresh();
        for (int i = 0; i < n; ++i) in[i] = (float)(ref[i] * g / 0.5); /* phrase peaks at -6 dBFS: scale to db */
        process(p, in, out, n);
        const double q = 20 * log10(rms(out, n) + 1e-12);
        api->destroy_instance(p);
        if (db > -40) printf("%+5d     %+8.1f  %5.2f   %+8.1f  %5.2f\n", db, s, (s - ps) / 4, q, (q - pp) / 4);
        else printf("%+5d     %+8.1f          %+8.1f\n", db, s, q);
        ps = s; pp = q;
    }
}

static void mode_cal(void) {
    static float in[4 * FS], out[4 * FS];
    const int n = pluck_phrase(in, 4 * FS);
    void *p = fresh();
    process(p, in, out, n);
    printf("cal: defaults, pluck phrase: out/in RMS = %+.2f dB\n", 20 * log10(rms(out, n) / rms(in, n)));
    api->destroy_instance(p);
}

int main(int argc, char **argv) {
    if (argc < 3) { fprintf(stderr, "usage: %s pedal.so outdir [all|resp knob v...|cpu|cal] [key=value...]\n", argv[0]); return 2; }
    void *lib = dlopen(argv[1], RTLD_NOW | RTLD_LOCAL);
    if (!lib) { fprintf(stderr, "%s\n", dlerror()); return 1; }
    audio_fx_init_v2_fn init = (audio_fx_init_v2_fn)dlsym(lib, AUDIO_FX_INIT_V2_SYMBOL);
    host_api_v1_t host;
    memset(&host, 0, sizeof host);
    host.api_version = 1; host.sample_rate = FS; host.frames_per_block = BLOCK;
    api = init(&host);
    load_keys();
    const char *mode = argc > 3 ? argv[3] : "all";
    char *vals[32];
    int nv = 0;
    for (int i = 4; i < argc; ++i) {
        if (strchr(argv[i], '=') && noverrides < 16) snprintf(overrides[noverrides++], 64, "%s", argv[i]);
        else if (nv < 32) vals[nv++] = argv[i];
    }
    if (!strcmp(mode, "all")) mode_all(argv[2]);
    else if (!strcmp(mode, "resp") && nv >= 2) mode_resp(vals[0], nv - 1, vals + 1);
    else if (!strcmp(mode, "cpu")) mode_cpu();
    else if (!strcmp(mode, "spec") && nv >= 1) mode_spec(atof(vals[0]), nv > 1 ? atof(vals[1]) : 0.25);
    else if (!strcmp(mode, "cal")) mode_cal();
    else if (!strcmp(mode, "presets")) mode_presets();
    else if (!strcmp(mode, "dyn")) mode_dyn();
    else { fprintf(stderr, "bad mode\n"); return 2; }
    return 0;
}
