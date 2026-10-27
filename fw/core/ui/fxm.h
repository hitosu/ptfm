/* SPDX-License-Identifier: GPL-3.0-only
 * Copyright (C) 2026 Vasily Kazantsev */
/* Fixed-point maths for the screens' pictures (FX's, VISUAL's): no floating point. Angles are a turn in
 * 2^32, levels Q15 or Q16, logarithms Q16. Accurate to the pixel, not to the sound. */
#ifndef UI_FXM_H
#define UI_FXM_H
#include <stdint.h>

#define FXM_Q16 65536
int32_t fxm_sin(uint32_t ph);           /* sin, Q15 (+-32767) */
int32_t fxm_cos(uint32_t ph);
int32_t fxm_log2(uint64_t x);           /* log2(x) Q16 for x >= 1; x 0 gives -64 << 16 */
uint64_t fxm_exp2(int32_t y);           /* 2^(y / 65536) in Q16 (y up to 30 << 16; under -16 << 16: 0) */
int32_t fxm_db(uint64_t num, uint64_t den);     /* 20 log10(num / den), Q8 dB; num 0: -200 dB */
uint64_t fxm_pow(uint64_t base_q16, int32_t e_q16);  /* base^e, both Q16 (base > 0) */
uint32_t fxm_sqrt(uint64_t x);          /* floor(sqrt(x)) */
int32_t fxm_tanh(int32_t x_q16);        /* tanh, Q16 in and out (x within +-4) */
int32_t fxm_div_round(int64_t a, int64_t b);    /* a / b rounded half away from 0 */
int32_t fxm_round(int64_t a, int64_t b);        /* a / b rounded as JavaScript's Math.round: floor(x + 1/2); b > 0 */

/* a number as the screens write a decimal with one place (`5.1`), or none when `places` is 0, rounded
 * half up as the playground's toFixed does for these values; q: the value x 1000 */
void fxm_fixed(char *dst, int64_t milli, int places);

#endif
