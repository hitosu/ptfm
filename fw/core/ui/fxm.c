/* SPDX-License-Identifier: GPL-3.0-only
 * Copyright (C) 2026 Vasily Kazantsev */
/* Fixed-point maths for the screens' pictures (fxm.h). */
#include "fxm.h"

#define Q30 (1LL << 30)
#define HALF_PI_Q30 1686629713LL        /* pi / 2 in Q30 */

static int32_t sin_quarter(uint32_t t)  /* t: [0, 2^30) of a quarter turn -> sin, Q30 */
{
    int64_t a = (int64_t)t * HALF_PI_Q30 >> 30, a2 = a * a >> 30;
    int64_t s = Q30 - a2 / 72;          /* Horner: a (1 - a2/6 (1 - a2/20 (1 - a2/42 (1 - a2/72)))) */
    s = Q30 - (a2 * s >> 30) / 42;
    s = Q30 - (a2 * s >> 30) / 20;
    s = Q30 - (a2 * s >> 30) / 6;
    return (int32_t)(a * s >> 30);
}
int32_t fxm_sin(uint32_t ph)
{
    uint32_t q = ph >> 30, t = ph & 0x3FFFFFFFu;
    int32_t v;
    if (q & 1u) v = t ? sin_quarter(0x40000000u - t) : (int32_t)Q30;
    else v = sin_quarter(t);
    v >>= 15;
    if (v > 32767) v = 32767;
    return q & 2u ? -v : v;
}
int32_t fxm_cos(uint32_t ph) { return fxm_sin(ph + 0x40000000u); }

static int top_bit(uint64_t x)
{
    int n = 0;
    while (x >> 1) { x >>= 1; n++; }
    return n;
}
int32_t fxm_log2(uint64_t x)
{
    if (!x) return -(64 << 16);
    int n = top_bit(x);
    uint64_t m = n >= 30 ? x >> (n - 30) : x << (30 - n);
    int32_t y = n << 16;
    for (int i = 0; i < 16; i++) {
        m = m * m >> 30;
        if (m >= (uint64_t)2 << 30) { m >>= 1; y += 1 << (15 - i); }
    }
    return y;
}
static const uint32_t ROOTS[16] = {     /* 2^(2^-i) in Q30, i = 1..16 */
    1518500250u, 1276901417u, 1170923762u, 1121280436u, 1097253708u, 1085434106u, 1079572136u, 1076653033u,
    1075196443u, 1074468888u, 1074105294u, 1073923544u, 1073832680u, 1073787251u, 1073764537u, 1073753181u};
uint64_t fxm_exp2(int32_t y)
{
    if (y < -(16 << 16)) return 0;
    if (y > (30 << 16)) y = 30 << 16;
    int32_t n = y >> 16;                /* floor */
    uint32_t f = (uint32_t)y & 0xFFFFu;
    uint64_t r = (uint64_t)Q30;
    for (int i = 0; i < 16; i++)
        if (f & (1u << (15 - i))) r = r * ROOTS[i] >> 30;
    int sh = n - 14;                    /* Q30 -> Q16, times 2^n */
    return sh >= 0 ? r << sh : r >> -sh;
}
int32_t fxm_db(uint64_t num, uint64_t den)
{
    if (!num) return -200 * 256;
    if (!den) return 200 * 256;
    int64_t d = (int64_t)fxm_log2(num) - fxm_log2(den);
    return (int32_t)(d * 1541 / 65536);  /* 20 log10(2) = 6.0206 dB a doubling: 1541 / 256 */
}
uint64_t fxm_pow(uint64_t base_q16, int32_t e_q16)
{
    if (!base_q16) return 0;
    int64_t l = (int64_t)fxm_log2(base_q16) - (16 << 16);
    return fxm_exp2((int32_t)(l * e_q16 / 65536));
}
uint32_t fxm_sqrt(uint64_t x)
{
    uint64_t r = 0, b = (uint64_t)1 << 62;
    while (b > x) b >>= 2;
    while (b) {
        if (x >= r + b) { x -= r + b; r = (r >> 1) + b; }
        else r >>= 1;
        b >>= 2;
    }
    return (uint32_t)r;
}
int32_t fxm_tanh(int32_t x)
{
    if (x > 4 * 65536) x = 4 * 65536;
    if (x < -4 * 65536) x = -4 * 65536;
    int64_t x2 = (int64_t)x * x >> 16, x4 = x2 * x2 >> 16, x6 = x4 * x2 >> 16;   /* Pade [7/6] */
    int64_t n = (135135LL << 16) + 17325 * x2 + 378 * x4 + x6;
    int64_t d = (135135LL << 16) + 62370 * x2 + 3150 * x4 + 28 * x6;
    return (int32_t)((int64_t)x * n / d);
}
int32_t fxm_div_round(int64_t a, int64_t b)
{
    if (b < 0) { a = -a; b = -b; }
    return (int32_t)(a >= 0 ? (a + b / 2) / b : -((-a + b / 2) / b));
}

int32_t fxm_round(int64_t a, int64_t b)
{
    int64_t n = 2 * a + b, d = 2 * b;
    return (int32_t)(n >= 0 ? n / d : -((-n + d - 1) / d));
}

void fxm_fixed(char *dst, int64_t milli, int places)
{
    int neg = milli < 0;
    if (neg) milli = -milli;
    int64_t div = places == 0 ? 1000 : places == 1 ? 100 : 10, t = (milli + div / 2) / div;
    char b[24];
    int n = 0;
    int64_t ip = places == 0 ? t : places == 1 ? t / 10 : t / 100;
    do { b[n++] = (char)('0' + ip % 10); ip /= 10; } while (ip);
    char *p = dst;
    if (neg && t) *p++ = '-';
    while (n) *p++ = b[--n];
    if (places == 1) { *p++ = '.'; *p++ = (char)('0' + t % 10); }
    if (places == 2) { *p++ = '.'; *p++ = (char)('0' + t / 10 % 10); *p++ = (char)('0' + t % 10); }
    *p = 0;
}
