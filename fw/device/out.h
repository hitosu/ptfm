/* SPDX-License-Identifier: GPL-3.0-only
 * Copyright (C) 2026 Vasily Kazantsev */
/* The analog out's last stage (specs/ui/screens/settings.md): what app_audio gives (16-bit stereo) times
 * MASTER and LINE GAIN, mono if OUTPUT says so, then the soft ceiling, as the codec's 24-bit words.
 * Portable: main.c runs it in the audio interrupt, fw/tests/out_test.c on the PC.
 *
 * The codec's full scale is 2^23. At MASTER full and a gain of Q12 4096 (the stage's unity) a full-scale
 * 16-bit sample lands at 2^22, -6 dBFS (x Q12 >> 5); LINE GAIN's 0 dB is 4 dB over that
 * (OUT_LINE_REF: a full-scale sample at -2 dBFS). Peaks above 0.8 of full scale are rounded off toward it,
 * never past it: y = knee + r - r^2 / (e + r), e the excess over the knee, r what is left above it; in
 * units of 256, so the square fits 32 bits. */
#ifndef PTFM_OUT_H
#define PTFM_OUT_H
#include <stdint.h>

#ifndef OUT_C2                           /* the includer's mark for what a block's job runs of it (main.c: core 2's */
#define OUT_C2                           /* room, plat.h's PLAT_C2) */
#endif

#define OUT_FULL 8388607
#define OUT_KNEE 6710886                 /* 0.8 of full scale */

/* a gain over the stage's unity, -24 .. +16 dB in 1 dB steps, as Q12 (round(4096 * 10^(dB / 20))) */
static const uint16_t OUT_LINE_Q12[41] = {
    258, 290, 325, 365, 410, 460, 516, 579, 649, 728, 817, 917, 1029, 1154, 1295, 1453, 1631, 1830, 2053,
    2303, 2584, 2900, 3254, 3651, 4096, 4596, 5157, 5786, 6492, 7284, 8173, 9170, 10289, 11544, 12953,
    14533, 16306, 18296, 20529, 23034, 25844};

static inline int32_t out_line_q12(int db)
{
    return OUT_LINE_Q12[(db < -24 ? -24 : db > 16 ? 16 : db) + 24];
}

/* SETTINGS' LINE GAIN and USB GAIN (-24 .. +12 dB) as their stages' Q12: each setting's 0 dB sits over its stage's
 * unity, LINE GAIN's by 4 dB (a full-scale 16-bit sample at -2 dBFS of the codec), USB GAIN's by 2 dB (the 16-bit
 * mix 2 dB up, its peaks into the soft ceiling) */
#define OUT_LINE_REF 4
#define OUT_USB_REF 2
static inline int32_t out_line_gain_q12(int db) { return out_line_q12((db < -24 ? -24 : db > 12 ? 12 : db) + OUT_LINE_REF); }
static inline int32_t out_usb_gain_q12(int db) { return out_line_q12((db < -24 ? -24 : db > 12 ? 12 : db) + OUT_USB_REF); }

/* MASTER (0..1023) squared: Q12, 4096 at the end */
static inline int32_t out_master_q12(int32_t k10)
{
    if (k10 <= 0)
        return 0;
    return k10 >= 1023 ? 4096 : (k10 * k10) >> 8;
}

static inline OUT_C2 int32_t out_ceiling(int32_t v)
{
    int32_t a = v < 0 ? -v : v, e, r = (OUT_FULL - OUT_KNEE) >> 8;
    if (__builtin_expect(a <= OUT_KNEE, 1))   /* (the usual way straight on: a taken jump costs the device ~6 cycles) */
        return v;
    e = (a - OUT_KNEE) >> 8;
    a = OUT_KNEE + ((r - (int32_t)((uint32_t)(r * r) / (uint32_t)(e + r))) << 8);
    return v < 0 ? -a : a;
}

/* n frames of lr (L, R, ...) into o (24-bit words, L, R, ...); g = MASTER x LINE GAIN, Q12 (<= 25844: the mono
 * sum x g fits 31 bits) */
static inline void out_convert(const int16_t *lr, int32_t *o, uint32_t n, int32_t g, int mono)
{
    uint32_t i;
    if (mono) {
        for (i = 0; i < 2u * n; i += 2u) {
            int32_t m = out_ceiling((((int32_t)lr[i] + lr[i + 1u]) * g) >> 6);
            o[i] = m;
            o[i + 1u] = m;
        }
    } else {
        for (i = 0; i < 2u * n; i++)
            o[i] = out_ceiling((lr[i] * g) >> 5);
    }
}

/* The same from a block as the player hands it out (app_audio_out): 16-bit values in int32 words, so the
 * audio interrupt writes each block into the DMA's half as it is made, and the half never holds anything
 * but finished words or silence */
static inline OUT_C2 void out_convert_block(const int32_t *lr, int32_t *o, uint32_t n, int32_t g, int mono)
{
    uint32_t i;
    if (mono) {
        for (i = 0; i < 2u * n; i += 2u) {
            int32_t m = out_ceiling(((lr[i] + lr[i + 1u]) * g) >> 6);
            o[i] = m;
            o[i + 1u] = m;
        }
    } else {
        for (i = 0; i < 2u * n; i++)
            o[i] = out_ceiling((lr[i] * g) >> 5);
    }
}

#endif
