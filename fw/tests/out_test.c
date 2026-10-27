/* SPDX-License-Identifier: GPL-3.0-only
 * Copyright (C) 2026 Vasily Kazantsev */
/* Host test of the analog out's last stage (fw/device/out.h): the soft ceiling never passes full scale,
 * leaves everything under the knee alone, rises without a step; LINE GAIN's table is 10^(dB / 20); MASTER
 * squares; mono is the two sides' mean; the scale: -6 dBFS for a full 16-bit sample.
 *   cc -o out_test fw/tests/out_test.c -lm && ./out_test */
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "../device/out.h"

static int failed;
static void ok(int cond, const char *what)
{
    printf("%-64s %s\n", what, cond ? "ok" : "FAIL");
    if (!cond)
        failed++;
}

int main(void)
{
    int32_t v, last = 0, worst = 0, step = 0;
    int db, i, under = 1, mono_ok = 1, stereo_ok = 1;
    for (v = 0; v <= 16306 * 32767 / 32 + 1000; v += 7) {      /* every level the stage can see */
        int32_t y = out_ceiling(v);
        if (y > worst)
            worst = y;
        if (y < last)
            step = 1;
        if (v <= OUT_KNEE && y != v)
            under = 0;
        if (out_ceiling(-v) != -y)
            step = 1;
        last = y;
    }
    ok(worst <= OUT_FULL, "the ceiling never passes full scale (2^23 - 1)");
    ok(under, "under 0.8 of full scale nothing changes");
    ok(!step, "it rises with the input, never back, and is symmetric");
    ok(out_ceiling(OUT_KNEE + 256) - OUT_KNEE <= 256, "no step at the knee");
    ok(out_ceiling(16306 * 32767 / 32) > OUT_KNEE + (OUT_FULL - OUT_KNEE) * 8 / 10,
       "+12 dB over the stage's unity comes close to full scale");
    for (db = -24; db <= 16; db++)
        if (abs(out_line_q12(db) - (int32_t)lround(4096.0 * pow(10.0, db / 20.0))) > 1)
            break;
    ok(db == 17, "the gains' table: Q12 of 10^(dB / 20), -24 .. +16 dB");
    ok(out_line_q12(-99) == out_line_q12(-24) && out_line_q12(99) == out_line_q12(16), "the table clamps");
    ok(out_line_gain_q12(0) == out_line_q12(4) && out_line_gain_q12(-24) == out_line_q12(-20) &&
       out_line_gain_q12(12) == out_line_q12(16) && out_line_gain_q12(99) == out_line_gain_q12(12),
       "LINE GAIN: its 0 dB 4 dB over the stage's unity (a full-scale sample at -2 dBFS), -24 .. +12 dB, clamped");
    ok(out_usb_gain_q12(0) == out_line_q12(2) && out_usb_gain_q12(12) == out_line_q12(14) &&
       out_usb_gain_q12(-99) == out_usb_gain_q12(-24), "USB GAIN: its 0 dB 2 dB over the stage's unity, clamped");
    ok((int64_t)65535 * out_line_gain_q12(12) < 2147483647, "LINE GAIN's loudest: the mono sum x g fits 31 bits");
    ok(out_master_q12(1023) == 4096 && out_master_q12(0) == 0 && out_master_q12(512) == 1024,
       "MASTER: squared (half way is -12 dB), full at the end");
    {
        int16_t lr[8] = {32767, 32767, -32768, -32768, 1000, -1000, 20000, 0};
        int32_t o[8];
        out_convert(lr, o, 4, 4096, 0);
        for (i = 0; i < 8; i++)
            if (o[i] != (lr[i] * 4096) >> 5)
                stereo_ok = 0;
        ok(stereo_ok && o[0] == 32767 * 128, "stereo: x Q12 >> 5, a full sample at 2^22 (-6 dBFS)");
        out_convert(lr, o, 4, 4096, 1);
        for (i = 0; i < 8; i += 2)
            if (o[i] != o[i + 1] || o[i] != (((int32_t)lr[i] + lr[i + 1]) * 4096) >> 6)
                mono_ok = 0;
        ok(mono_ok && o[4] == 0, "mono: both sides the mean of L and R");
    }
    {   /* a block at a time, as the audio interrupt runs it: the player's 16-bit values in int32 words */
        int32_t ref[512], buf[512], blk[512];
        int16_t lr[512];
        int same = 1, m, g, b;
        for (m = 0; m < 2; m++)
            for (g = 1024; g <= 16306; g += 15282) {
                for (i = 0; i < 512; i++)
                    blk[i] = lr[i] = (int16_t)((i * 7919 + m * 31) % 65536 - 32768);
                out_convert(lr, ref, 256, g, m);
                memset(buf, 0, sizeof buf);
                for (b = 0; b < 256; b += 64)
                    out_convert_block(blk + 2 * b, buf + 2 * b, 64, g, m);
                if (memcmp(buf, ref, sizeof ref))
                    same = 0;
            }
        ok(same, "a block at a time (16-bit values in int32 words): the same words");
    }
    printf(failed ? "out test FAILED\n" : "out test passed\n");
    return failed != 0;
}
