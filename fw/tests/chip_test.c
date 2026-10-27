/* SPDX-License-Identifier: GPL-3.0-only
 * Copyright (C) 2026 Vasily Kazantsev */
/* The CHIP engine on the PC (fw/tests/run_chip.sh): renders the scenarios fw/tests/chip_scenarios.py writes, one a
 * line on stdin, and prints for each its output's hash (FNV-1a over the samples), its peak, the blocks it sounded
 * and the loudest 300 ms's RMS (16-bit scale), as fw/tests/chip_twin.js does with the playground's twin.
 *
 *   chip_test < scenarios           a line a scenario: HASH PEAK BLOCKS RMS300
 *   chip_test -d < scenario         every sample of the first, one a line
 *   chip_test -l < scenarios        a line a scenario: its LEVEL as AUTO LEVEL measures it (specs/files/instruments.md:
 *                                   the loudest 300 ms of 1.2 s brought to -20 dB RMS, 30 dB at most either way, the
 *                                   peak under -6 dB, in half dBs), its RMS and peak in dB; the scenario's own
 *                                   BLOCKS and OFF are taken as given (826 and 689 for a LEVEL). The firmware's own
 *                                   whole numbers (player.c's pm_run, ui_level.c's ui_level_measure): a fresh voice,
 *                                   a block's mean square of (L + R) / 2 to 8 bits less, the window moved a block at
 *                                   a time, its 10 log10 in Q16, the peak's room applied before the half dB's
 *                                   rounding */
#include <math.h>
#include <stdio.h>
#include <string.h>
#include <stdint.h>
#include "eng/chip/chip.h"

static int32_t db10_q16(uint64_t x)                     /* (ui_level.c's) */
{
    int n = 63;
    while (n > 0 && !(x >> n)) n--;
    uint32_t y = (uint32_t)(n >= 30 ? x >> (n - 30) : x << (30 - n));
    int32_t f = 0;
    for (int i = 0; i < 16; i++) {
        y = (uint32_t)(((uint64_t)y * y) >> 30);
        f <<= 1;
        if (y >= (1u << 31)) { y >>= 1; f |= 1; }
    }
    int64_t l2 = ((int64_t)n << 16) | f;
    return (int32_t)((l2 * 197283) >> 16);
}

static uint32_t isqrt64(uint64_t x)
{
    uint64_t r = 0, b = 1ull << 62;
    while (b > x) b >>= 2;
    while (b) {
        if (x >= r + b) { x -= r + b; r = (r >> 1) + b; } else r >>= 1;
        b >>= 2;
    }
    return (uint32_t)r;
}

int main(int argc, char **argv)
{
    int dump = argc > 1 && !strcmp(argv[1], "-d"), lev = argc > 1 && !strcmp(argv[1], "-l");
    int tune = argc > 1 && !strcmp(argv[1], "-t");     /* each scenario's instrument: chip_tuning at C-1 ... C-9 */
    char hex[64];
    int note, blocks, off, bb, bend, pb, code, val, pf, pt, ne, xa[8], xc[8], xv[8];
    static chip_voice_t v;
    static int32_t sig[44100 * 4];
    while (scanf("%63s %d %d %d %d %d %d %d %d %d %d %d", hex, &note, &blocks, &off, &bb, &bend, &pb, &code, &val, &pf, &pt,
                 &ne) == 12) {
        for (int i = 0; i < ne && i < 8; i++)
            if (scanf("%d %d %d", &xa[i], &xc[i], &xv[i]) != 3)
                return 2;
        uint8_t slot[CHIP_SLOT_BYTES];
        chip_patch_t p;
        for (int i = 0; i < CHIP_SLOT_BYTES; i++) {
            unsigned x;
            sscanf(hex + 2 * i, "%2x", &x);
            slot[i] = (uint8_t)x;
        }
        chip_unpack(slot, &p);
        if (tune) {
            for (int n = 24; n <= 120; n += 12) {
                uint32_t reg;
                int32_t c10;
                chip_tuning(&p, n, &reg, &c10);
                printf("%u %d%s", reg, c10, n < 120 ? " " : "\n");
            }
            continue;
        }
        if (lev) memset(&v, 0, sizeof v);              /* (AUTO LEVEL's voice: fresh) */
        chip_note_on(&v, &p, note);
        uint32_t h = 2166136261u;
        int32_t peak = 0;
        int act = 0, n = 0;
        for (int b = 0; b < blocks; b++) {
            int32_t out[CHIP_N] = {0};
            if (b == off) chip_note_off(&v);
            if (b == bb) chip_pitch(&v, bend);
            if (b == pb) chip_param(&v, (uint32_t)code, (uint32_t)val);
            for (int i = 0; i < ne && i < 8; i++)
                if (b == xa[i]) chip_param(&v, (uint32_t)xc[i], (uint32_t)xv[i]);
            if (b >= pf && b < pt) {     /* (parked: its time goes on unheard) */
                if (chip_active(&v)) chip_skip(&v);
            } else if (chip_active(&v)) {
                chip_render(&v, out);
                act = b + 1;
            }
            for (int i = 0; i < CHIP_N; i++) {
                h = (h ^ (uint32_t)out[i]) * 16777619u;
                int32_t a = out[i] < 0 ? -out[i] : out[i];
                if (a > peak) peak = a;
                if (n < (int)(sizeof sig / sizeof sig[0])) sig[n++] = out[i];
                if (dump) printf("%d\n", out[i]);
            }
        }
        /* the loudest 300 ms (13,230 samples) in steps of 1,024 */
        uint64_t best = 0;
        for (int s = 0; s + 13230 <= n; s += 1024) {
            uint64_t e = 0;
            for (int i = s; i < s + 13230; i++) e += (uint64_t)((int64_t)sig[i] * sig[i]);
            if (e > best) best = e;
        }
        if (lev) {                       /* AUTO LEVEL's window: 206 blocks (300 ms), moved a block at a time */
            static uint32_t win[206];
            uint64_t sum = 0, bw = 0;
            int nb = n / CHIP_N;
            for (int b = 0; b < nb; b++) {
                uint64_t s = 0;
                for (int i = b * CHIP_N; i < (b + 1) * CHIP_N; i++) {
                    int64_t m = 2 * (int64_t)sig[i];
                    s += (uint64_t)(m * m);
                }
                s >>= 8;
                if (b >= 206) sum -= win[b % 206];
                win[b % 206] = s > 0xFFFFFFFFu ? 0xFFFFFFFFu : (uint32_t)s;
                sum += win[b % 206];
                if (b + 1 >= 206 && sum > bw) bw = sum;
            }
            uint64_t ms = bw / 206;
            if (!ms) { printf("silent\n"); continue; }
            int32_t lv = db10_q16(ms) - 30 * 197283, db = -20 * 65536 - lv;
            if (db > 30 * 65536) db = 30 * 65536;
            if (db < -30 * 65536) db = -30 * 65536;
            if (peak > 0) {
                int32_t pkdb = 2 * (db10_q16((uint64_t)peak) - 15 * 197283), room = -6 * 65536 - pkdb;
                if (db > room) db = room;
            }
            int32_t hl = db >= 0 ? (db * 2 + 32768) >> 16 : -((-db * 2 + 32768) >> 16);
            printf("%.1f %.2f %.2f\n", hl / 2.0, lv / 65536.0, 20 * log10((peak ? peak : 1) / 32768.0));
        } else if (!dump) printf("%08x %d %d %u\n", h, peak, act, isqrt64(best / 13230));
        if (dump) break;
    }
    return 0;
}
