/* SPDX-License-Identifier: GPL-3.0-only
 * Copyright (C) 2026 Vasily Kazantsev
 * The keys' pressure (the rest it leans from, its log scale) after Charles Vestal's measurements, fm1-velocity */
/* A held key's pressure (specs/hardware.md#the-keys-velocity): how much harder than it rests it is pressed, 0-127,
 * from its driven readings (lower, harder), worked out every few ms in the main loop, never in the scan's tick. MIDI
 * CONTROLLER's aftertouch (ctl.h) and the tracker's (ui.c: the key's own notes louder) take the same steps.
 *
 *   press_step(w, d, dt)   key's word *w, its driven reading d (0: not held, or not read), dt the ms since its last
 *                          step: its pressure now (one copy: the file that defines PRESS_DEFINE first, ui.c)
 *
 * The rest follows the reading down for PRESS_SETTLE ms after the landing (a key goes on settling), then creeps after it,
 * and comes up at once when the key eases; the pressure is the pill's resistance under the rest in octaves, nothing
 * under 0.04 octave, 127 at 0.46 more, smoothed over ~30 ms (11 / 64 a 5 ms step). One word a key: the rest (bits 0-13,
 * the resistance x 16), the pressure smoothed (14-24, x 16), the settling still to come (25-31, in 2.5 ms: exact for
 * steps 5 ms apart). */
#ifndef PRESS_H
#define PRESS_H
#include <stdint.h>

#define PRESS_SETTLE 250u                /* ms after the landing the rest follows the key down */
#define PRESS_DEAD 164                   /* 0.04 octave (Q12): no pressure under it */
#define PRESS_SPAN 1884                  /* 0.46 octave (Q12) over the dead zone: 127 */

uint32_t press_step(uint32_t *w, uint32_t d, uint32_t dt);
#ifdef PRESS_DEFINE

/* log2(x) in Q12, x >= 1: the leading bit's place, the rest as log2(1 + f) ~ f + 0.343 f (1 - f) (within 0.006) */
static int32_t press_lg(uint32_t x)
{
    uint32_t e = 0, f;
    while (x >> (e + 1u))
        e++;
    f = (e >= 16u ? x >> (e - 16u) : x << (16u - e)) & 0xFFFFu;
    f += (((f * (65536u - f)) >> 16) * 22479u) >> 16;
    return (int32_t)(e << 12) + (int32_t)(f >> 4);
}
uint32_t press_step(uint32_t *w, uint32_t d, uint32_t dt)
{
    uint32_t rest = *w & 0x3FFFu, s = *w >> 14 & 0x7FFu, c = *w >> 25, r16, x = 0;
    int32_t lean;
    if (!d) {
        *w = 0;
        return 0;
    }
    r16 = (d > 186u ? d - 185u : 1u) << 4;
    if (!rest) {                         /* (held and read again: it rests here, settling) */
        rest = r16;
        s = 0;
        c = PRESS_SETTLE * 2u / 5u;
    } else {
        uint32_t u = (dt > 255u ? 255u : dt) * 2u + 2u;   /* (dt in 2.5 ms, rounded) */
        c = c > u / 5u ? c - u / 5u : 0u;
    }
    if (r16 > rest)
        rest = r16;                      /* eased: it rests here now */
    else if (c)
        rest -= (rest - r16) / 4u;
    else if (rest > r16) {
        uint32_t step = (rest - r16) >> 10;
        rest -= step ? step : 1u;
    }
    lean = press_lg(rest) - press_lg(r16);
    if (lean > PRESS_DEAD)
        x = (uint32_t)(((lean - PRESS_DEAD) * 127 + PRESS_SPAN / 2) / PRESS_SPAN);
    if (x > 127u)
        x = 127u;
    s = (uint32_t)((int32_t)s + (((int32_t)(x << 4) - (int32_t)s) * 11) / 64);
    *w = rest | s << 14 | c << 25;
    return (s + 8u) >> 4;
}
#endif
#endif
