/* SPDX-License-Identifier: GPL-3.0-only
 * Copyright (C) 2026 Vasily Kazantsev */
/* One effect of the chain alone on a second of noise and three of silence, as fx_pg.js runs the playground's:
 * their outputs' RMS, a quarter second at a time, to compare (fx_cmp.py).
 *   fx_probe EFFECT KNOB0 KNOB1 KNOB2 KNOB3 KNOB4      (the effect's number, its knobs' steps as a setup keeps them) */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <math.h>
#include "../../core/fx/fx.h"

static const uint8_t FIRST[10] = {0x90, 0x94, 0x96, 0x98, 0x9B, 0x9F, 0xA4, 0xA8, 0xAA, 0};

int main(int argc, char **argv)
{
    uint8_t s[64];
    fx_set_t set;
    int e = argc > 1 ? atoi(argv[1]) : 6, i, b;
    uint32_t seed = 12345;
    static int32_t mix[2 * FX_BLOCK];
    double acc = 0;
    int n = 0;
    memset(s, 0, sizeof s);
    s[1] = (uint8_t)(e | 0xF0);          /* the chain: the effect alone */
    s[2] = s[3] = s[4] = s[5] = s[6] = 0xFF;
    s[7] = (uint8_t)(1u << e);
    s[8] = (uint8_t)((1u << e) >> 8);
    for (i = 0; i < 5 && 2 + i < argc; i++)
        s[16 + FIRST[e] - 0x90 + i] = (uint8_t)atoi(argv[2 + i]);
    fx_init();
    fx_setup(s);
    fx_clk.beat_frames = 22050;          /* (120 BPM) */
    fx_clk.inc = 0xFFFFFFFFu / 22050u;
    fx_clk.hold = FX_BLOCK;
    fx_clk.playing = 1;
    set.seq = 0;
    fx_set_get(&set);
    for (b = 0; b < 4 * 44100 / FX_BLOCK; b++) {
        for (i = 0; i < FX_BLOCK; i++) {
            int32_t v = 0;
            if (b * FX_BLOCK + i < 44100) {      /* noise at -10 dB of full, then (as the playground's mixer: x 0.5) */
                seed = seed * 1103515245u + 12345u;
                v = (int32_t)((seed >> 16) & 0x7FFF) - 16384;
                v = v * 2 * 10362 / 32768 / 2;
            }
            mix[2 * i] = mix[2 * i + 1] = v;
        }
        fx_work(&set, &fx_clk);          /* (the settings and the clock in place for the block's effects) */
        fx_block(mix, 0);
        for (i = 0; i < FX_BLOCK; i++) {
            acc += (double)mix[2 * i] * mix[2 * i] + (double)mix[2 * i + 1] * mix[2 * i + 1];
            if (++n == 11025) {
                printf("%.2f\n", 10 * log10(acc / (2.0 * n) / (32768.0 * 32768.0) + 1e-20));
                acc = 0;
                n = 0;
            }
        }
        fx_clk.frac += fx_clk.inc * FX_BLOCK;
    }
    return 0;
}
