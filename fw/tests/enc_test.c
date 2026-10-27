/* SPDX-License-Identifier: GPL-3.0-only
 * Copyright (C) 2026 Vasily Kazantsev */
/* Host test of the knobs' decoder in hal/fm1_input.h (fm1__frame), against a model of a turned knob and
 * of the scan: the timer reads one column every 100 us, a frame (the decoder) every 11 columns (~1.1 ms).
 * A click is one full quadrature cycle (clockwise: B A B A from the detent), its 4 transitions spread
 * evenly over the click or bunched in its middle half, the knob resting in the detent state between.
 * Checked:
 *   - 12-click turns at 50-300 clicks/s (even) and 50-150 clicks/s (bunched), both ways, over many
 *     phases of the scan, from the detent states 0 and 3: every click counted (a two-frame filter lost
 *     whole clicks above ~60-110 clicks/s, a fast turn moved the knob a little);
 *   - slow turns (3-40 clicks/s) with contacts bouncing for 1.5 ms: one step a click;
 *   - a turn, a pause, a turn back: both counted, in their own directions;
 *   - a still knob, one contact chattering or both lost together for 1-5 frames: never a step.
 * The GPIO / timer helpers of the header are compiled, never called. */
#include <stdint.h>
#include <stdio.h>
#include <string.h>
#pragma GCC diagnostic ignored "-Wint-to-pointer-cast"
#include "../hal/fm1_input.h"

#define TICK_US 100u
static int fails;
static void check(const char *what, int ok)
{
    printf("%-78s %s\n", what, ok ? "ok" : "FAIL");
    if (!ok)
        fails++;
}

static uint32_t rng = 0x2468ACEu;
static uint32_t rnd(uint32_t n)
{
    rng ^= rng << 13;
    rng ^= rng >> 17;
    rng ^= rng << 5;
    return rng % n;
}

/* the knob's two contacts (0 = A, 1 = B): the times (us, rising) at which each toggles */
#define NE 4096
static struct { uint32_t t[NE], n, i, s; } line[2];
static void knob_clear(uint32_t rest)
{
    memset(line, 0, sizeof line);
    line[0].s = (rest >> 1) & 1u;
    line[1].s = rest & 1u;
}
static void knob_edge(uint32_t l, uint32_t t)
{
    if (line[l].n < NE && (!line[l].n || t > line[l].t[line[l].n - 1u]))
        line[l].t[line[l].n++] = t;
}
/* n clicks from t0 (dir +1 clockwise), a click every `per` us, its transitions over `w` of it (1 = even),
 * each bouncing (runs of 30..30+bmax us) for up to bspan us; returns the end of the turn */
static uint32_t knob_turn(uint32_t t0, int32_t n, int32_t dir, double per, double w, uint32_t bspan, uint32_t bmax)
{
    int32_t i, j;
    for (i = 0; i < n; i++)
        for (j = 0; j < 4; j++) {
            double f = 0.5 - w / 2 + (j + 0.5) * w / 4;
            uint32_t t = t0 + (uint32_t)(per * (i + f)), gap = (uint32_t)(per * w / 4);
            uint32_t l = (uint32_t)(j & 1) ^ (dir > 0 ? 1u : 0u), end = t + (bspan < gap / 2u ? bspan : gap / 2u);
            knob_edge(l, t);
            while (bspan && bmax) {                    /* bounce: back and forth, the new level last */
                uint32_t a = t + 30u + rnd(bmax), b = a + 30u + rnd(bmax);
                if (b >= end)
                    break;
                knob_edge(l, a);
                knob_edge(l, b);
                t = b;
            }
        }
    return t0 + (uint32_t)(per * n);
}
static uint32_t knob_at(uint32_t l, uint32_t us)
{
    while (line[l].i < line[l].n && line[l].t[line[l].i] <= us) {
        line[l].i++;
        line[l].s ^= 1u;
    }
    return line[l].s;
}

static uint32_t scan_us, scan_col;
static void reset(void)
{
    uint32_t i;
    memset((void *)&fm1_in, 0, sizeof fm1_in);
    for (i = 0; i < FM1_NENC; i++)
        fm1_in.enc_prev[i] = fm1_in.enc_last[i] = 0xFF;
    scan_us = scan_col = 0;
}
/* the scan of knob e on to `until` us, as fm1_input_tick reads it; the steps counted since reset() */
static int32_t scan(uint32_t e, uint32_t until)
{
    const uint8_t *m = FM1_ENC[e];
    for (; scan_us < until; scan_us += TICK_US) {
        uint32_t col = scan_col;
        fm1_in.raw[col] = 0;
        if (col == m[0])
            fm1_in.raw[col] |= (uint8_t)(knob_at(0, scan_us) << m[1]);
        if (col == m[2])
            fm1_in.raw[col] |= (uint8_t)(knob_at(1, scan_us) << m[3]);
        scan_col = col + 1u == FM1_NCOL ? 0u : col + 1u;
        if (!scan_col)
            fm1__frame();
    }
    return fm1_in.enc_steps[e];
}

int main(void)
{
    char what[120];
    {   /* fast turns: every click */
        static const double EVEN[] = {50, 100, 150, 200, 250, 300}, BUNCH[] = {50, 100, 150};
        uint32_t r, ph, d, rest, bad = 0, n = 0, worst = 0;
        for (r = 0; r < 9u; r++)
            for (ph = 0; ph < 40u; ph++)
                for (d = 0; d < 2u; d++)
                    for (rest = 0; rest < 4u; rest += 3u) {
                        int32_t dir = d ? -1 : 1, s;
                        uint32_t e = (ph + r) % FM1_NENC, off;
                        double rate = r < 6u ? EVEN[r] : BUNCH[r - 6u];
                        reset();
                        knob_clear(rest);
                        s = scan(e, knob_turn(5000u + ph * 53u, 12, dir, 1e6 / rate, r < 6u ? 1.0 : 0.5, 0u, 0u) + 20000u);
                        off = (uint32_t)(s * dir < 12 ? 12 - s * dir : s * dir - 12);
                        n++;
                        bad += off != 0u;
                        worst = off > worst ? off : worst;
                    }
        printf("fast turns: %u turns of 12 clicks, %u miscounted (worst by %u)\n", (unsigned)n, (unsigned)bad,
               (unsigned)worst);
        check("12-click turns up to 300 clicks/s (even) / 150/s (bunched): every click", !bad);
    }
    {   /* slow turns, bouncing contacts */
        static const double SLOW[] = {3, 8, 20, 40};
        uint32_t r, ph, d, bad = 0;
        for (r = 0; r < 4u; r++)
            for (ph = 0; ph < 20u; ph++)
                for (d = 0; d < 2u; d++) {
                    int32_t dir = d ? -1 : 1;
                    reset();
                    knob_clear(0);
                    bad += scan(2, knob_turn(5000u + ph * 71u, 6, dir, 1e6 / SLOW[r], ph & 1u ? 1.0 : 0.4, 1500u, 400u) +
                                       20000u) != 6 * dir;
                }
        snprintf(what, sizeof what, "slow turns (3-40 clicks/s), contacts bouncing 1.5 ms: one step a click (%u off)",
                 (unsigned)bad);
        check(what, !bad);
    }
    {   /* a turn, a pause, a turn back */
        uint32_t ph, bad = 0;
        for (ph = 0; ph < 40u; ph++) {
            int32_t a, b;
            uint32_t back, t;
            reset();
            knob_clear(0);
            back = knob_turn(5000u + ph * 37u, 8, 1, 1e6 / 150.0, 1.0, 0u, 0u) + 50000u;
            t = knob_turn(back, 5, -1, 1e6 / 150.0, 1.0, 0u, 0u);
            a = scan(3, back);
            b = scan(3, t + 20000u);
            bad += a != 8 || b != 3;
        }
        check("8 clicks at 150/s, 50 ms still, 5 back: +8, then 3 in all", !bad);
    }
    {   /* a still knob: one contact chattering, or both lost together (1-5 frames), for every detent state */
        uint32_t rest, mode, f, g, moved = 0;
        const uint8_t *m = FM1_ENC[0];
        for (mode = 0; mode < 3u; mode++)
            for (rest = 0; rest < 4u; rest++) {
                reset();
                for (f = 0, g = 0; f < 200000u; f++) {
                    uint32_t st = rest;
                    if (g) {
                        st ^= mode == 0u ? 2u : mode == 1u ? 1u : 3u;
                        g--;
                    } else if (f >= 100u && rnd(300) == 0u) {
                        g = 1u + rnd(5);
                    }
                    memset((void *)fm1_in.raw, 0, sizeof fm1_in.raw);
                    fm1_in.raw[m[0]] |= (uint8_t)(((st >> 1) & 1u) << m[1]);
                    fm1_in.raw[m[2]] |= (uint8_t)((st & 1u) << m[3]);
                    fm1__frame();
                    moved |= fm1_in.enc_steps[0] != 0;
                }
            }
        check("a still knob, a contact chattering or both lost together: never a step", !moved);
    }
    {   /* the order of clicks and presses between two takes (plat_input's knob_pre): the clicks before the first
         * new press are told apart from those after it; with no press, all of them come before one */
        int16_t steps[FM1_NENC], pre[FM1_NENC];
        uint32_t i;
        fm1_input_take(steps, pre);                     /* (from nothing) */
        fm1_in.enc_steps[0] = 2;                        /* SELECT two clicks, */
        for (i = 0; i < FM1_DEB_PRESS; i++)
            fm1__key(3, 1);                             /* then SEL (id 3) down, */
        fm1_in.enc_steps[0] = (int16_t)(fm1_in.enc_steps[0] + 1);   /* then a click more */
        fm1_input_take(steps, pre);
        check("two clicks, a press, a click: 3 clicks, 2 of them before the press", steps[0] == 3 && pre[0] == 2);
        fm1_in.enc_steps[0] = 1;                        /* SEL still held (no new press): a click */
        fm1_input_take(steps, pre);
        check("a click with no new press: before any", steps[0] == 1 && pre[0] == 1);
        for (i = 0; i < FM1_DEB_RELEASE; i++)
            fm1__key(3, 0);                             /* SEL let go: no press */
        for (i = 0; i < FM1_DEB_PRESS; i++)
            fm1__key(14, 1);                            /* a note key down first, */
        fm1_in.enc_steps[1] = -2;                       /* then ALGORITHM two clicks back */
        fm1_input_take(steps, pre);
        check("a key pressed, then two clicks back: none of them before the press", steps[1] == -2 && pre[1] == 0);
    }
    if (fails) {
        printf("enc_test: %d FAILED\n", fails);
        return 1;
    }
    printf("enc_test: all passed\n");
    return 0;
}
