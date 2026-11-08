/* SPDX-License-Identifier: GPL-3.0-only
 * Copyright (C) 2026 Vasily Kazantsev */
/* What the effects report as at work (fx_active), as another core reads it at any moment: what worked in the last whole
 * block, never a block's part. The effects' work runs on core 2 while core 1's main loop reads the report for the state and
 * FIGURES. Here the moment is plat_us: the effects' timing reads it between one effect and the next */
#include <stdio.h>
#include <string.h>
#include "fx/fx.h"

static int armed, looks, bad;
static uint32_t want, last;

uint32_t plat_us(void)
{
    static uint32_t t = 1;
    if (armed) {
        looks++;
        last = fx_active();
        if (last != want)
            bad++;
    }
    return t += 7;
}
uint32_t plat_ms(void) { return 0; }
void plat_c2_wait(void) {}

static int32_t mix[2 * FX_BLOCK];
static fx_set_t set;
static uint32_t seed = 12345;

static void noise(void)
{
    for (int i = 0; i < FX_BLOCK; i++) {
        seed = seed * 1103515245u + 12345u;
        mix[2 * i] = mix[2 * i + 1] = ((int32_t)((seed >> 16) & 0x7FFF) - 16384) / 2;
    }
}
static void block(void)
{
    noise();
    fx_work(&set, &fx_clk);
    fx_block(mix, 0);
    fx_clk.frac += fx_clk.inc * FX_BLOCK;
}

int main(void)
{
    uint8_t s[64];
    memset(s, 0, sizeof s);
    s[1] = (uint8_t)(FX_DELAY | FX_REVERB << 4);        /* the chain: DELAY, then REVERB */
    s[2] = s[3] = s[4] = s[5] = s[6] = 0xFF;
    s[7] = (uint8_t)(1u << FX_DELAY | 1u << FX_REVERB);
    fx_init();
    fx_setup(s);
    fx_clk.beat_frames = 22050;                         /* (120 BPM) */
    fx_clk.inc = 0xFFFFFFFFu / 22050u;
    fx_clk.hold = FX_BLOCK;
    fx_clk.playing = 1;
    set.seq = 0;
    fx_set_get(&set);
    for (int b = 0; b < 40; b++)
        block();
    want = fx_active();
    if (!(want >> FX_DELAY & 1u) || !(want >> FX_REVERB & 1u)) {
        printf("fxact: FAIL DELAY and REVERB not at work after noise (%x)\n", (unsigned)want);
        return 1;
    }
    fx_timed(1);
    armed = 1;
    for (int b = 0; b < 40; b++)
        block();
    armed = 0;
    if (!looks) {
        printf("fxact: FAIL no look inside a block\n");
        return 1;
    }
    if (bad) {
        printf("fxact: FAIL %d of %d looks inside a block saw a part of it (%x, not %x)\n", bad, looks, (unsigned)last, (unsigned)want);
        return 1;
    }
    printf("fxact: %d looks inside a block, each saw the last whole block's\n", looks);
    return 0;
}
