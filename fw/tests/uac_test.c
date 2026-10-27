/* SPDX-License-Identifier: GPL-3.0-only
 * Copyright (C) 2026 Vasily Kazantsev */
/* USB audio's stream on the PC (fw/device/uac.h): the FM-1's clock and the computer's side by side. The FM-1
 * renders halves of 256 frames at its own rate, each in a burst (blocks of 64 spread over 3 ms, the consumer
 * running between them as TIMER5 nests in the render); the computer takes a packet every 1 ms of its clock.
 * A sine goes in; checked: the steering locks (the fill near its target, no dry or full ring once settled), the
 * packets are the computer's exact frames, and the sine comes out at its frequency, clean (its SNR by a fit).
 *   uac_test */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <math.h>
#define RING_PUBLISH() ((void)0)
#include "../device/uac.h"

static int fails;
static void check(int ok, const char *what)
{
    printf("%s %s\n", ok ? "ok  " : "FAIL", what);
    if (!ok) fails++;
}

#define OUT_MAX (48000 * 40)
static int16_t out_l[OUT_MAX], out_r[OUT_MAX];

typedef struct {
    double snr_l, snr_r, ppm_lo, ppm_hi;   /* the SNR; the step's reach once settled, ppm of its mean */
    double ppm_jump;                       /* its largest change from a half to the next, ppm */
    int fill_lo, fill_hi, under, over, pkts_bad;
    long frames;
} run_t;
static int steer = 1;                    /* 0: the step kept at its start (the resampler alone, exact rates) */

/* a least-squares fit of a sin + b cos + c at f (cycles a frame) over n frames: the signal to the residual, dB */
static double snr(const int16_t *x, int n, double f)
{
    double s[3][3] = {{0}}, v[3] = {0}, a[3], e = 0, p = 0;
    for (int i = 0; i < n; i++) {
        double b[3] = {sin(2 * M_PI * f * i), cos(2 * M_PI * f * i), 1};
        for (int r = 0; r < 3; r++) {
            v[r] += b[r] * x[i];
            for (int c = 0; c < 3; c++) s[r][c] += b[r] * b[c];
        }
    }
    for (int k = 0; k < 3; k++)                          /* Gauss-Jordan */
        for (int r = 0; r < 3; r++) {
            if (r == k) continue;
            double m = s[r][k] / s[k][k];
            for (int c = 0; c < 3; c++) s[r][c] -= m * s[k][c];
            v[r] -= m * v[k];
        }
    for (int k = 0; k < 3; k++) a[k] = v[k] / s[k][k];
    for (int i = 0; i < n; i++) {
        double y = a[0] * sin(2 * M_PI * f * i) + a[1] * cos(2 * M_PI * f * i) + a[2], d = x[i] - y;
        e += d * d;
        p += (y - a[2]) * (y - a[2]);
    }
    return 10 * log10(p / (e > 1e-9 ? e : 1e-9));
}

/* seconds of a sine at hz (amplitude amp), the FM-1 at dev_hz, the computer at 44.1 or 48 kHz; the fill and the
 * glitches counted after `settle` s; the SNR over the last 8192 frames */
static run_t run(double dev_hz, int r48, double hz, double amp, double secs, double settle)
{
    run_t o = {.fill_lo = 1 << 30};
    memset(&ua, 0, sizeof ua);
    ua_w = ua_r = 0;
    uac_rs_rate(r48 ? 48000u : 44100u);
    ua.r48 = (uint8_t)r48;
    double t = 0, half_t = 256.0 / dev_hz, next_half = 0.0003, next_pkt = 0.001, ph = 0;
    long nout = 0, k = 0;
    int blocks_left = 0;
    double next_block = 0;
    uint32_t pkt[UA_MAXF];
    uint32_t u0 = 0, o0 = 0;
    int settled = 0;
    double smin = 1e30, smax = 0, slast = 0;
    while (t < secs) {
        double tn = next_pkt;                            /* the next event */
        if (next_half < tn) tn = next_half;
        if (blocks_left && next_block < tn) tn = next_block;
        t = tn;
        if (!settled && t >= settle) { settled = 1; u0 = ua.underruns; o0 = ua.overruns; }
        if (t == next_half) {                            /* a half starts: the steering, then 4 blocks over 3 ms */
            if (steer || !ua.go)
                uac_rs_start(1);
            if (settled) {
                double st = (double)ua.step;
                if ((int)ua.fill < o.fill_lo) o.fill_lo = (int)ua.fill;
                if ((int)ua.fill > o.fill_hi) o.fill_hi = (int)ua.fill;
                if (st < smin) smin = st;
                if (st > smax) smax = st;
                if (slast > 0 && fabs(st / slast - 1) * 1e6 > o.ppm_jump) o.ppm_jump = fabs(st / slast - 1) * 1e6;
                slast = st;
            }
            blocks_left = 4;
            next_block = t + 0.00005;
            next_half += half_t;
        } else if (blocks_left && t == next_block) {     /* a block of 64 frames */
            int16_t lr[128];
            for (int i = 0; i < 64; i++, k++) {
                double v = amp * sin(ph);
                ph += 2 * M_PI * hz / dev_hz;
                if (ph > 2 * M_PI) ph -= 2 * M_PI;
                lr[2 * i] = (int16_t)lrint(v);
                lr[2 * i + 1] = (int16_t)lrint(-v);
            }
            uac_rs_frames(lr, 64);
            blocks_left--;
            next_block = t + 0.00075;
        } else {                                          /* the computer takes a packet */
            uint32_t n = uac_rs_packet(pkt);
            int want = r48 ? 48 : ((ua.acc == 0) ? 45 : 44);
            if ((int)n != want) o.pkts_bad++;
            for (uint32_t i = 0; i < n && nout < OUT_MAX; i++, nout++) {
                out_l[nout] = (int16_t)(pkt[i] & 0xFFFF);
                out_r[nout] = (int16_t)(pkt[i] >> 16);
            }
            next_pkt += 0.001;
        }
    }
    o.under = (int)(ua.underruns - u0);
    o.over = (int)(ua.overruns - o0);
    o.frames = nout;
    o.ppm_lo = (smin / ((smin + smax) / 2) - 1) * 1e6;
    o.ppm_hi = (smax / ((smin + smax) / 2) - 1) * 1e6;
    if (!steer) {                                        /* the step fixed: the output's frequency exactly */
        double fo = hz / dev_hz * ((double)ua.step / 4294967296.0);
        o.snr_l = snr(out_l + nout - 8192, 8192, fo);
        o.snr_r = snr(out_r + nout - 8192, 8192, fo);
    } else {                                             /* steered: its frequency fitted too (a ppm off is no noise) */
        double f0 = hz / (r48 ? 48000.0 : 44100.0), best = -1e9, bf = f0;
        for (int pass = 0, span = 400; pass < 3; pass++, span /= 20) {
            double c = bf;
            for (int i = -20; i <= 20; i++) {
                double f = c * (1 + i * span * 1e-6 / 20), s = snr(out_l + nout - 8192, 8192, f);
                if (s > best) { best = s; bf = f; }
            }
        }
        o.snr_l = best;
        o.snr_r = snr(out_r + nout - 8192, 8192, bf);
    }
    return o;
}

int main(void)
{
    /* the resampler alone (the step kept, the clocks exactly as it assumes): sines across the band, both rates */
    static const double HZ[5] = {100, 1000, 5000, 10000, 15000};
    steer = 0;
    for (int r48 = 0; r48 < 2; r48++)
        for (int f = 0; f < 5; f++) {
            run_t o = run(44145.0, r48, HZ[f], 16000, 4, 2);
            char b[160];
            double want = HZ[f] <= 10000 ? 70 : 55;
            snprintf(b, sizeof b, "the resampler, %s kHz: a %.0f Hz sine, SNR %.1f / %.1f dB (L / R), at least %.0f",
                     r48 ? "48" : "44.1", HZ[f], o.snr_l, o.snr_r, want);
            check(o.snr_l >= want && o.snr_r >= want && o.under == 0 && o.over == 0, b);
        }
    /* the steering: the FM-1 as measured, and 500 / 1000 ppm either side of it; both rates. Settled within 20 s,
     * then 20 s: the fill near its target, no glitch, the step steady: under 1.5 ppm from a half to the next, its
     * slow wander (the packets and the halves passing each other) a few ppm, 0.02 cent; the sine's SNR by a fit of
     * one frequency over 0.19 s, which that wander lowers to ~70 dB (the resampler alone: above) */
    steer = 1;
    static const double DEV[5] = {44145.0, 44145.0 * 0.9995, 44145.0 * 1.0005, 44145.0 * 0.999, 44145.0 * 1.001};
    for (int r48 = 0; r48 < 2; r48++)
        for (int d = 0; d < 5; d++) {
            run_t o = run(DEV[d], r48, 1000, 16000, 40, 20);
            char b[240];
            int target = r48 ? UA_T48 : UA_T44;
            snprintf(b, sizeof b, "the steering, %s kHz, the FM-1 at %.0f Hz: the fill %d..%d (target %d), dry %d, full "
                     "%d, packets not the computer's %d, the step %+.1f..%+.1f ppm, %.2f ppm a half at most; 1 kHz SNR %.1f dB",
                     r48 ? "48" : "44.1", DEV[d], o.fill_lo, o.fill_hi, target, o.under, o.over, o.pkts_bad, o.ppm_lo,
                     o.ppm_hi, o.ppm_jump, o.snr_l);
            check(o.under == 0 && o.over == 0 && o.pkts_bad == 0 && o.fill_lo > target - 64 &&
                  o.fill_hi < target + 64 && o.ppm_hi - o.ppm_lo < 60 && o.ppm_jump < 1.5 && o.snr_l > 65, b);
        }
    /* the rate changed in the stream (SET_CUR): silence while it primes again, then locked as before */
    {
        run_t o = run(44145.0, 0, 1000, 16000, 2, 1);
        (void)o;
        uac_rs_rate(48000u);
        uint32_t pkt[UA_MAXF];
        uint32_t n = uac_rs_packet(pkt), z = 1;
        for (uint32_t i = 0; i < n; i++) z &= pkt[i] == 0;
        check(n == 48 && z && !ua.go, "a new rate: 48 frames of silence until the next half primes the ring");
        uac_rs_start(1);
        check(ua.go && ua_w - ua_r == UA_T48, "the next half primes it at 48 kHz's target");
    }
    printf(fails ? "uac_test: %d FAILED\n" : "uac_test: all passed\n", fails);
    return fails ? 1 : 0;
}
