/* SPDX-License-Identifier: Apache-2.0
 * Copyright (C) 2026 Vasily Kazantsev (the C port) */
/* A DX7 voice: MSFA (Dexed's Source/msfa, Copyright 2012-2013 Google Inc., 2016-2025 Pascal
 * Gauthier, Apache License 2.0; see dx7.h and LICENSE-msfa.txt) ported to C for the FM-1.
 * Sections follow MSFA's files: sin / exp2 / freqlut, env.cc, pitchenv.cc, lfo.cc,
 * fm_op_kernel.cc, fm_core.cc, dx7note.cc. Every change from the original is marked FM-1:. */
#include "dx7.h"
#include "dx7_tables.h"                  /* fw/tools/gen_dx7.py */

#include "../../plat.h"
#define DX7_RAM PLAT_POOL                /* the FM-1: in the pool */

#define N DX7_N
#define LG_N DX7_LG_N

static int32_t sintab[2048] DX7_RAM, exp2tab[2048] DX7_RAM, tanhtab[2048] DX7_RAM, freqlut[1025] DX7_RAM;
uint32_t dx7_ram(void) { return (uint32_t)(sizeof sintab + sizeof exp2tab + sizeof tanhtab + sizeof freqlut); }

/* FM-1: the tables come from flash (computed by the generator, not at init); in RAM they are
 * read at full speed. Flash keeps them small, and here they are rebuilt, every value equal to
 * MSFA's: sin by Sin::init's own integer recurrence; exp2, tanh and freqlut from their second
 * differences (fw/tools/gen_dx7.py dd_encode). */
static void dd_decode(int32_t *t, int stride, int32_t v0, int32_t d0, const int16_t *dd, int n)
{
    int32_t a = v0, b = v0 + d0, c;
    int i;
    t[0] = a;
    t[stride] = b;
    for (i = 0; i < n - 2; i++) {
        c = b + (b - a) + dd[i];        /* (not 2b - a: b is near 2^31) */
        t[(i + 2) * stride] = c;
        a = b;
        b = c;
    }
}

static void deltas(int32_t *t, int32_t last)   /* the SIN_DELTA layout: [2i] the step to [2i + 3] */
{
    int i;
    for (i = 0; i < 1023; i++)
        t[2 * i] = t[2 * i + 3] - t[2 * i + 1];
    t[2046] = last;
}

void dx7_init(void)
{
    int64_t u = 1 << 30, v = 0, tt;
    int i;
    for (i = 0; i < 512; i++) {          /* Sin::init (SIN_DELTA) */
        sintab[2 * i + 1] = (int32_t)((v + 32) >> 6);
        sintab[2 * (i + 512) + 1] = -(int32_t)((v + 32) >> 6);
        tt = (u * DX7_SIN_S + v * DX7_SIN_C + (1 << 29)) >> 30;
        u = (u * DX7_SIN_C - v * DX7_SIN_S + (1 << 29)) >> 30;
        v = tt;
    }
    deltas(sintab, -sintab[2047]);
    dd_decode(exp2tab + 1, 2, DX7_EXP2_V0, DX7_EXP2_D0, DX7_EXP2_DD, 1024);
    deltas(exp2tab, (int32_t)(0x80000000u - (uint32_t)exp2tab[2047]));
    dd_decode(tanhtab + 1, 2, DX7_TANH_V0, DX7_TANH_D0, DX7_TANH_DD, 1024);
    deltas(tanhtab, DX7_TANH_END - tanhtab[2047]);
    dd_decode(freqlut, 1, DX7_FREQ_V0, DX7_FREQ_D0, DX7_FREQ_DD, 1025);
}

const int32_t *dx7_table(int which)      /* FM-1 addition: the tables in RAM */
{
    return which == 0 ? sintab : which == 1 ? exp2tab : which == 2 ? tanhtab : freqlut;
}

static int imin(int a, int b) { return a < b ? a : b; }
static int imax(int a, int b) { return a > b ? a : b; }

/* ---------------------------------------------------- sin.h, exp2.h --- */
static inline int32_t sin_lookup(uint32_t phase)   /* FM-1: uint32_t phase (same bits) */
{
    const int SHIFT = 24 - 10;
    int32_t lowbits = (int32_t)(phase & ((1u << SHIFT) - 1u));
    uint32_t phase_int = (phase >> (SHIFT - 1)) & ((1024u - 1u) << 1);
    int32_t dy = sintab[phase_int], y0 = sintab[phase_int + 1u];
    return y0 + (int32_t)(((int64_t)dy * (int64_t)lowbits) >> SHIFT);
}

int32_t dx7_exp2(int32_t x)
{
    const int SHIFT = 24 - 10;
    int32_t lowbits = x & ((1 << SHIFT) - 1);
    int32_t x_int = (x >> (SHIFT - 1)) & ((1024 - 1) << 1);
    int32_t dy = exp2tab[x_int], y0 = exp2tab[x_int + 1];
    int32_t y = y0 + (int32_t)(((int64_t)dy * (int64_t)lowbits) >> SHIFT);
    return y >> (6 - (x >> 24));
}

/* tanh.h (Tanh::lookup): FM-1: in dx7.h (dx7_tanh_tab), for callers that inline it */
int32_t dx7_tanh(int32_t x) { return dx7_tanh_tab(x, exp2tab, tanhtab); }
int32_t dx7_sin(uint32_t phase) { return sin_lookup(phase); }   /* FM-1 addition: the effects' LFOs */

void dx7_tables(const int32_t **e, const int32_t **t)   /* FM-1 addition */
{
    *e = exp2tab;
    *t = tanhtab;
}

/* ------------------------------------------------------- freqlut.cc --- */
int32_t dx7_freq(int32_t logfreq)
{
    int ix = (logfreq & 0xffffff) >> 14;
    int32_t y0 = freqlut[ix], y1 = freqlut[ix + 1];
    int lowbits = logfreq & ((1 << 14) - 1);
    int32_t y = y0 + (int32_t)(((int64_t)(y1 - y0) * (int64_t)lowbits) >> 14);
    int hibits = logfreq >> 24;
    return y >> (20 - hibits);
}

/* ------------------------------------------------------------ env.cc --- */
static const int levellut[] = {0, 5, 9, 13, 17, 20, 23, 25, 27, 29, 31, 33, 35, 37, 39, 41, 42, 43, 45, 46};

static const int statics[] = {   /* ACCURATE_ENVELOPE */
    1764000, 1764000, 1411200, 1411200, 1190700, 1014300, 992250, 882000, 705600, 705600, 584325, 507150,
    502740, 441000, 418950, 352800, 308700, 286650, 253575, 220500, 220500, 176400, 145530, 145530,
    125685, 110250, 110250, 88200, 88200, 74970, 61740, 61740, 55125, 48510, 44100, 37485, 31311, 30870,
    27562, 27562, 22050, 18522, 17640, 15435, 14112, 13230, 11025, 9261, 9261, 7717, 6615, 6615, 5512,
    5512, 4410, 3969, 3969, 3439, 2866, 2690, 2249, 1984, 1896, 1808, 1411, 1367, 1234, 1146, 926, 837,
    837, 705, 573, 573, 529, 441, 441};

static int scaleoutlevel(int outlevel) { return outlevel >= 20 ? 28 + outlevel : levellut[outlevel]; }

/* FM-1: sr_multiplier is 1 << 24 at 44.1 kHz, so its two scalings are left out (identities) */
static void env_advance(dx7_env_t *e, int newix)
{
    e->ix = (uint8_t)newix;
    if (newix < 4) {
        int newlevel = e->levels[newix];
        int actuallevel = scaleoutlevel(newlevel) >> 1, qrate;
        actuallevel = (actuallevel << 6) + e->outlevel - 4256;
        actuallevel = actuallevel < 16 ? 16 : actuallevel;
        e->targetlevel = actuallevel << 16;
        e->rising = e->targetlevel > e->level;
        qrate = (e->rates[newix] * 41) >> 6;
        qrate += e->rate_scaling;
        qrate = imin(qrate, 63);
        if (e->targetlevel == e->level || (newix == 0 && newlevel == 0)) {
            int staticrate = e->rates[newix];
            staticrate += e->rate_scaling;
            staticrate = imin(staticrate, 99);
            e->staticcount = staticrate < 77 ? statics[staticrate] : 20 * (99 - staticrate);
            if (staticrate < 77 && (newix == 0 && newlevel == 0))
                e->staticcount /= 20;    /* attack is scaled faster */
        } else {
            e->staticcount = 0;
        }
        e->inc = (4 + (qrate & 3)) << (2 + LG_N + (qrate >> 2));
    }
}

static void env_init(dx7_env_t *e, const int r[4], const int l[4], int ol, int rate_scaling)
{
    int i;
    e->initialised = 1;
    for (i = 0; i < 4; i++) {
        e->rates[i] = (uint8_t)r[i];
        e->levels[i] = (uint8_t)l[i];
    }
    e->outlevel = ol;
    e->rate_scaling = rate_scaling;
    e->level = 0;
    e->down = 1;
    env_advance(e, 0);
}

static int32_t env_getsample(dx7_env_t *e)
{
    if (e->staticcount) {
        e->staticcount -= N;
        if (e->staticcount <= 0) {
            e->staticcount = 0;
            env_advance(e, e->ix + 1);
        }
    }
    if (e->ix < 3 || (e->ix < 4 && !e->down)) {
        if (e->staticcount) {
            ;
        } else if (e->rising) {
            const int jumptarget = 1716;
            if (e->level < (jumptarget << 16))
                e->level = jumptarget << 16;
            e->level += (((17 << 24) - e->level) >> 24) * e->inc;
            if (e->level >= e->targetlevel) {
                e->level = e->targetlevel;
                env_advance(e, e->ix + 1);
            }
        } else {
            e->level -= e->inc;
            if (e->level <= e->targetlevel) {
                e->level = e->targetlevel;
                env_advance(e, e->ix + 1);
            }
        }
    }
    return e->level;
}

static void env_keydown(dx7_env_t *e, int d)
{
    if (e->down != d) {
        e->down = (uint8_t)d;
        env_advance(e, d ? 0 : 3);
    }
}

static int env_active(const dx7_env_t *e) { return e->initialised && (e->ix < 4 || e->levels[3] > 0); }

/* ------------------------------------------------------- pitchenv.cc --- */
static const uint8_t pitchenv_rate[] = {
    1, 2, 3, 3, 4, 4, 5, 5, 6, 6, 7, 7, 8, 8, 9, 9, 10, 10, 11, 11, 12, 12, 13, 13, 14, 14, 15, 16, 16, 17,
    18, 18, 19, 20, 21, 22, 23, 24, 25, 26, 27, 28, 30, 31, 33, 34, 36, 37, 38, 39, 41, 42, 44, 46, 47, 49,
    51, 53, 54, 56, 58, 60, 62, 64, 66, 68, 70, 72, 74, 76, 79, 82, 85, 88, 91, 94, 98, 102, 106, 110,
    115, 120, 125, 130, 135, 141, 147, 153, 159, 165, 171, 178, 185, 193, 202, 211, 232, 243, 254, 255};

static const int8_t pitchenv_tab[] = {
    -128, -116, -104, -95, -85, -76, -68, -61, -56, -52, -49, -46, -43, -41, -39, -37, -35, -33, -32, -31,
    -30, -29, -28, -27, -26, -25, -24, -23, -22, -21, -20, -19, -18, -17, -16, -15, -14, -13, -12, -11,
    -10, -9, -8, -7, -6, -5, -4, -3, -2, -1, 0, 1, 2, 3, 4, 5, 6, 7, 8, 9, 10, 11, 12, 13, 14, 15, 16, 17,
    18, 19, 20, 21, 22, 23, 24, 25, 26, 27, 28, 29, 30, 31, 32, 33, 34, 35, 38, 40, 43, 46, 49, 53, 58,
    65, 73, 82, 92, 103, 115, 127};

static void penv_advance(dx7_penv_t *p, int newix)
{
    p->ix = (uint8_t)newix;
    if (newix < 4) {
        int newlevel = p->levels[newix];
        p->targetlevel = pitchenv_tab[newlevel] * (1 << 19);   /* FM-1: * for << of a negative */
        p->rising = p->targetlevel > p->level;
        p->inc = pitchenv_rate[p->rates[newix]] * DX7_PITCHENV_UNIT;
    }
}

static void penv_set(dx7_penv_t *p, const int r[4], const int l[4])
{
    int i;
    for (i = 0; i < 4; i++) {
        p->rates[i] = (uint8_t)r[i];
        p->levels[i] = (uint8_t)l[i];
    }
    p->level = pitchenv_tab[l[3]] * (1 << 19);
    p->down = 1;
    penv_advance(p, 0);
}

static int32_t penv_getsample(dx7_penv_t *p)
{
    if (p->ix < 3 || (p->ix < 4 && !p->down)) {
        if (p->rising) {
            p->level += p->inc;
            if (p->level >= p->targetlevel) {
                p->level = p->targetlevel;
                penv_advance(p, p->ix + 1);
            }
        } else {
            p->level -= p->inc;
            if (p->level <= p->targetlevel) {
                p->level = p->targetlevel;
                penv_advance(p, p->ix + 1);
            }
        }
    }
    return p->level;
}

static void penv_keydown(dx7_penv_t *p, int d)
{
    if (p->down != d) {
        p->down = (uint8_t)d;
        penv_advance(p, d ? 0 : 3);
    }
}

/* ------------------------------------------------------------ lfo.cc --- */
static void lfo_reset(dx7_lfo_t *o, const uint8_t params[6])
{
    /* FM-1: rate and delay clamped to 0..99 (the table's range; valid patches are) */
    int rate = params[0] > 99 ? 99 : params[0], a = 99 - (params[1] > 99 ? 99 : params[1]);
    o->delta = DX7_LFO_DELTA[rate];
    if (a == 99) {
        o->delayinc = ~0u;
        o->delayinc2 = ~0u;
    } else {
        a = (16 + (a & 15)) << (1 + (a >> 4));
        o->delayinc = (uint32_t)DX7_LFO_UNIT * (uint32_t)a;
        a &= 0xff80;
        a = imax(0x80, a);
        o->delayinc2 = (uint32_t)DX7_LFO_UNIT * (uint32_t)a;
    }
    o->waveform = params[5];
    o->sync = params[4] != 0;
}

static int32_t lfo_getsample(dx7_lfo_t *o)
{
    int32_t x;
    o->phase += o->delta;
    switch (o->waveform) {
    case 0:                              /* triangle */
        x = (int32_t)(o->phase >> 7);
        x ^= -(int32_t)(o->phase >> 31);
        x &= (1 << 24) - 1;
        return x;
    case 1:                              /* sawtooth down */
        return (int32_t)((~o->phase ^ (1u << 31)) >> 8);
    case 2:                              /* sawtooth up */
        return (int32_t)((o->phase ^ (1u << 31)) >> 8);
    case 3:                              /* square */
        return (int32_t)(((~o->phase) >> 7) & (1u << 24));
    case 4:                              /* sine */
        return (1 << 23) + (sin_lookup(o->phase >> 8) >> 1);
    case 5:                              /* sample & hold */
        if (o->phase < o->delta)
            o->randstate = (uint8_t)((o->randstate * 179 + 17) & 0xff);
        x = o->randstate ^ 0x80;
        return (x + 1) << 16;
    }
    return 1 << 23;
}

static int32_t lfo_getdelay(dx7_lfo_t *o)
{
    uint32_t delta = o->delaystate < (1u << 31) ? o->delayinc : o->delayinc2;
    uint64_t d = (uint64_t)o->delaystate + delta;
    if (d > ~0u)
        return 1 << 24;
    o->delaystate = (uint32_t)d;
    if (d < (1u << 31))
        return 0;
    return (int32_t)((d >> 7) & ((1 << 24) - 1));
}

static void lfo_keydown(dx7_lfo_t *o)
{
    if (o->sync)
        o->phase = (1u << 31) - 1u;
    o->delaystate = 0;
}

/* --------------------------------------------------- fm_op_kernel.cc --- */
/* FM-1: each kernel a loop for adding and one for writing (MSFA tests add a sample; the device's compiler kept the
 * test in its loop: 2-3 instructions of ~20 an operator's sample) */
#define OP_LOOP(ADD, IN)                                                                                              \
    for (i = 0; i < N; i++) {                                                                                         \
        int32_t y, y1;                                                                                                \
        gain += dgain;                                                                                                \
        y = sin_lookup(phase + (IN));                                                                                 \
        y1 = (int32_t)(((int64_t)y * (int64_t)gain) >> 24);                                                           \
        output[i] = ADD ? output[i] + y1 : y1;                                                                        \
        phase += (uint32_t)freq;                                                                                      \
    }
static void op_compute(int32_t *output, const int32_t *input, uint32_t phase0, int32_t freq, int32_t gain1,
                       int32_t gain2, int add)
{
    int32_t dgain = (gain2 - gain1 + (N >> 1)) >> LG_N, gain = gain1;
    uint32_t phase = phase0;
    int i;
    if (add)
        OP_LOOP(1, (uint32_t)input[i])
    else
        OP_LOOP(0, (uint32_t)input[i])
}

static void op_compute_pure(int32_t *output, uint32_t phase0, int32_t freq, int32_t gain1, int32_t gain2, int add)
{
    int32_t dgain = (gain2 - gain1 + (N >> 1)) >> LG_N, gain = gain1;
    uint32_t phase = phase0;
    int i;
    if (add)
        OP_LOOP(1, 0u)
    else
        OP_LOOP(0, 0u)
}

#define OP_FB_LOOP(ADD)                                                                                               \
    for (i = 0; i < N; i++) {                                                                                         \
        int32_t scaled_fb;                                                                                            \
        gain += dgain;                                                                                                \
        scaled_fb = (y0 + y) >> (fb_shift + 1);                                                                       \
        y0 = y;                                                                                                       \
        y = sin_lookup(phase + (uint32_t)scaled_fb);                                                                  \
        y = (int32_t)(((int64_t)y * (int64_t)gain) >> 24);                                                            \
        output[i] = ADD ? output[i] + y : y;                                                                          \
        phase += (uint32_t)freq;                                                                                      \
    }
static void op_compute_fb(int32_t *output, uint32_t phase0, int32_t freq, int32_t gain1, int32_t gain2,
                          int32_t *fb_buf, int fb_shift, int add)
{
    int32_t dgain = (gain2 - gain1 + (N >> 1)) >> LG_N, gain = gain1;
    uint32_t phase = phase0;
    int32_t y0 = fb_buf[0], y = fb_buf[1];
    int i;
    if (add)
        OP_FB_LOOP(1)
    else
        OP_FB_LOOP(0)
    fb_buf[0] = y0;
    fb_buf[1] = y;
}

/* ------------------------------------------------------- fm_core.cc --- */
enum { OUT_BUS_ONE = 1, OUT_BUS_TWO = 2, OUT_BUS_ADD = 4, IN_BUS_ONE = 16, IN_BUS_TWO = 32, FB_IN = 64, FB_OUT = 128 };

static const uint8_t algorithms[32][6] = {
    {0xc1, 0x11, 0x11, 0x14, 0x01, 0x14}, {0x01, 0x11, 0x11, 0x14, 0xc1, 0x14}, /* 1, 2 */
    {0xc1, 0x11, 0x14, 0x01, 0x11, 0x14}, {0xc1, 0x11, 0x94, 0x01, 0x11, 0x14}, /* 3, 4 */
    {0xc1, 0x14, 0x01, 0x14, 0x01, 0x14}, {0xc1, 0x94, 0x01, 0x14, 0x01, 0x14}, /* 5, 6 */
    {0xc1, 0x11, 0x05, 0x14, 0x01, 0x14}, {0x01, 0x11, 0xc5, 0x14, 0x01, 0x14}, /* 7, 8 */
    {0x01, 0x11, 0x05, 0x14, 0xc1, 0x14}, {0x01, 0x05, 0x14, 0xc1, 0x11, 0x14}, /* 9, 10 */
    {0xc1, 0x05, 0x14, 0x01, 0x11, 0x14}, {0x01, 0x05, 0x05, 0x14, 0xc1, 0x14}, /* 11, 12 */
    {0xc1, 0x05, 0x05, 0x14, 0x01, 0x14}, {0xc1, 0x05, 0x11, 0x14, 0x01, 0x14}, /* 13, 14 */
    {0x01, 0x05, 0x11, 0x14, 0xc1, 0x14}, {0xc1, 0x11, 0x02, 0x25, 0x05, 0x14}, /* 15, 16 */
    {0x01, 0x11, 0x02, 0x25, 0xc5, 0x14}, {0x01, 0x11, 0x11, 0xc5, 0x05, 0x14}, /* 17, 18 */
    {0xc1, 0x14, 0x14, 0x01, 0x11, 0x14}, {0x01, 0x05, 0x14, 0xc1, 0x14, 0x14}, /* 19, 20 */
    {0x01, 0x14, 0x14, 0xc1, 0x14, 0x14}, {0xc1, 0x14, 0x14, 0x14, 0x01, 0x14}, /* 21, 22 */
    {0xc1, 0x14, 0x14, 0x01, 0x14, 0x04}, {0xc1, 0x14, 0x14, 0x14, 0x04, 0x04}, /* 23, 24 */
    {0xc1, 0x14, 0x14, 0x04, 0x04, 0x04}, {0xc1, 0x05, 0x14, 0x01, 0x14, 0x04}, /* 25, 26 */
    {0x01, 0x05, 0x14, 0xc1, 0x14, 0x04}, {0x04, 0xc1, 0x11, 0x14, 0x01, 0x14}, /* 27, 28 */
    {0xc1, 0x14, 0x01, 0x14, 0x04, 0x04}, {0x04, 0xc1, 0x11, 0x14, 0x04, 0x04}, /* 29, 30 */
    {0xc1, 0x14, 0x04, 0x04, 0x04, 0x04}, {0xc4, 0x04, 0x04, 0x04, 0x04, 0x04}, /* 31, 32 */
};

/* bus: FmCore::buf_, the operators' scratch. FM-1: the render's own, on its stack (MSFA's one for all voices would be
 * shared by the two cores rendering at once: specs/second-core.md); never read before written (has_contents) */
static void core_render(int32_t *output, dx7_op_t *params, int algorithm, int32_t *fb_buf, int feedback_shift,
                        int32_t (*bus)[N])
{
    const int kLevelThresh = 1120;
    const uint8_t *alg = algorithms[algorithm];
    int has_contents[3] = {1, 0, 0}, op;
    for (op = 0; op < 6; op++) {
        int flags = alg[op], add = (flags & OUT_BUS_ADD) != 0;
        dx7_op_t *param = &params[op];
        int inbus = (flags >> 4) & 3, outbus = flags & 3;
        int32_t *outptr = outbus == 0 ? output : bus[outbus - 1];
        int32_t gain1 = param->gain_out, gain2 = dx7_exp2(param->level_in - (14 * (1 << 24)));
        param->gain_out = gain2;
        if (gain1 >= kLevelThresh || gain2 >= kLevelThresh) {
            if (!has_contents[outbus])
                add = 0;
            if (inbus == 0 || !has_contents[inbus]) {
                if ((flags & 0xc0) == 0xc0 && feedback_shift < 16)
                    op_compute_fb(outptr, param->phase, param->freq, gain1, gain2, fb_buf, feedback_shift, add);
                else
                    op_compute_pure(outptr, param->phase, param->freq, gain1, gain2, add);
            } else {
                op_compute(outptr, bus[inbus - 1], param->phase, param->freq, gain1, gain2, add);
            }
            has_contents[outbus] = 1;
        } else if (!add) {
            has_contents[outbus] = 0;
        }
        param->phase += (uint32_t)param->freq << LG_N;
    }
}

/* ------------------------------------------------------- dx7note.cc --- */
static const int32_t coarsemul[] = {
    -16777216, 0, 16777216, 26591258, 33554432, 38955489, 43368474, 47099600, 50331648, 53182516, 55732705,
    58039632, 60145690, 62083076, 63876816, 65546747, 67108864, 68576247, 69959732, 71268397, 72509921,
    73690858, 74816848, 75892776, 76922906, 77910978, 78860292, 79773775, 80654032, 81503396, 82323963,
    83117622};

/* FM-1: standard tuning only; the double arithmetic of mode 0 (DETUNE, FINE) is in tables */
static int32_t osc_freq(int midinote, int mode, int coarse, int fine, int detune)
{
    int32_t logfreq;
    if (mode == 0) {
        logfreq = DX7_LOGFREQ[midinote][detune > 14 ? 14 : detune];
        logfreq += coarsemul[coarse & 31];
        if (fine)
            logfreq += DX7_FINE[fine > 99 ? 99 : fine];
    } else {
        logfreq = (4458616 * ((coarse & 3) * 100 + fine)) >> 3;
        logfreq += detune > 7 ? 13457 * (detune - 7) : 0;
    }
    return logfreq;
}

static const uint8_t velocity_data[64] = {
    0, 70, 86, 97, 106, 114, 121, 126, 132, 138, 142, 148, 152, 156, 160, 163, 166, 170, 173, 174, 178, 181,
    184, 186, 189, 190, 194, 196, 198, 200, 202, 205, 206, 209, 211, 214, 216, 218, 220, 222, 224, 225,
    227, 229, 230, 232, 233, 235, 237, 238, 240, 241, 242, 243, 244, 246, 246, 248, 249, 250, 251, 252,
    253, 254};

static int ScaleVelocity(int velocity, int sensitivity)
{
    int clamped_vel = imax(0, imin(127, velocity));
    int vel_value = velocity_data[clamped_vel >> 1] - 239;
    return ((sensitivity * vel_value + 7) >> 3) * 16;   /* FM-1: * for << of a negative */
}

static int ScaleRate(int midinote, int sensitivity)
{
    int x = imin(31, imax(0, midinote / 3 - 7));
    return (sensitivity * x) >> 3;
}

static const uint8_t exp_scale_data[] = {0, 1, 2, 3, 4, 5, 6, 7, 8, 9, 11, 14, 16, 19, 23, 27, 33,
                                         39, 47, 56, 66, 80, 94, 110, 126, 142, 158, 174, 190, 206, 222, 238, 250};

static int ScaleCurve(int group, int depth, int curve)
{
    int scale;
    if (curve == 0 || curve == 3) {
        scale = (group * depth * 329) >> 12;   /* linear */
    } else {
        int n_scale_data = sizeof exp_scale_data;
        int raw_exp = exp_scale_data[imin(group, n_scale_data - 1)];
        scale = (raw_exp * depth * 329) >> 15;
    }
    if (curve < 2)
        scale = -scale;
    return scale;
}

static int ScaleLevel(int midinote, int break_pt, int left_depth, int right_depth, int left_curve, int right_curve)
{
    int offset = midinote - break_pt - 17;
    if (offset >= 0)
        return ScaleCurve((offset + 1) / 3, right_depth, right_curve);
    return ScaleCurve(-(offset - 1) / 3, left_depth, left_curve);
}

static const uint8_t pitchmodsenstab[] = {0, 10, 20, 33, 55, 92, 153, 255};
static const uint32_t ampmodsenstab[] = {0, 4342338, 7171437, 16777216};

/* Dx7Note::init */
static void note_init(dx7_voice_t *v, const uint8_t *patch, int midinote, int velocity)
{
    int rates[4], levels[4], op, i, feedback;
    v->initialised = 1;
    for (op = 0; op < 6; op++) {
        int off = op * 21, outlevel, level_scaling, rate_scaling;
        for (i = 0; i < 4; i++) {
            rates[i] = patch[off + i];
            levels[i] = patch[off + 4 + i];
        }
        outlevel = patch[off + 16];
        outlevel = scaleoutlevel(outlevel);
        level_scaling = ScaleLevel(midinote, patch[off + 8], patch[off + 9], patch[off + 10], patch[off + 11],
                                   patch[off + 12]);
        outlevel += level_scaling;
        outlevel = imin(127, outlevel);
        outlevel = outlevel * 32;        /* FM-1: * for << of a negative */
        outlevel += ScaleVelocity(velocity, patch[off + 15]);
        outlevel = imax(0, outlevel);
        rate_scaling = ScaleRate(midinote, patch[off + 13]);
        env_init(&v->env[op], rates, levels, outlevel, rate_scaling);
        v->opmode[op] = patch[off + 17];
        v->basepitch[op] = osc_freq(midinote, patch[off + 17], patch[off + 18], patch[off + 19], patch[off + 20]);
        v->ampmodsens[op] = (int32_t)ampmodsenstab[patch[off + 14] & 3];
    }
    for (i = 0; i < 4; i++) {
        rates[i] = patch[126 + i];
        levels[i] = patch[130 + i];
    }
    penv_set(&v->penv, rates, levels);
    v->algorithm = (uint8_t)(patch[134] & 31);
    feedback = patch[135];
    v->fb_shift = feedback != 0 ? 8 - feedback : 16;   /* FEEDBACK_BITDEPTH 8 */
    v->pitchmoddepth = (patch[139] * 165) >> 6;
    v->pitchmodsens = pitchmodsenstab[patch[143] & 7];
    v->ampmoddepth = (patch[140] * 165) >> 6;
}

/* FM-1: Dexed's exp(sensamp / 262144 * 0.07 + 12.2) for the amplitude modulation, from the
 * generator's table at sensamp = i << 16 (exact there), linear in between */
static uint32_t ampmod_pt(uint32_t sensamp)
{
    uint32_t i = sensamp >> 16, f = sensamp & 0xFFFFu;
    return DX7_AMPMOD[i] + (uint32_t)(((uint64_t)(DX7_AMPMOD[i + 1u] - DX7_AMPMOD[i]) * f) >> 16);
}

/* Dx7Note::compute with Dexed's controllers at rest (no bend, wheel, aftertouch, portamento) */
/* FM-1: Dx7Note::compute in two: a block's pitches and levels (note_params), then its sound (core_render) */
static void note_params(dx7_voice_t *v, int32_t lfo_val, int32_t lfo_delay)
{
    uint32_t pmd = (uint32_t)v->pitchmoddepth * (uint32_t)lfo_delay;   /* Q32 */
    int32_t senslfo = v->pitchmodsens * (lfo_val - (1 << 23));
    int32_t pmod_1 = (int32_t)(((int64_t)pmd * (int64_t)senslfo) >> 39), pitch_mod;
    uint32_t amod_1, amd_mod;
    int op;
    pmod_1 = pmod_1 < 0 ? -pmod_1 : pmod_1;
    pitch_mod = pmod_1;                  /* max(pmod_1, pmod_2): the mod wheel is at 0 */
    pitch_mod = penv_getsample(&v->penv) + (pitch_mod * (senslfo < 0 ? -1 : 1));
    pitch_mod += v->bend;                /* FM-1 addition: the player's bend; master tune: 0 */
    lfo_val = (1 << 24) - lfo_val;
    amod_1 = (uint32_t)(((int64_t)v->ampmoddepth * (int64_t)lfo_delay) >> 8);   /* Q24 */
    amod_1 = (uint32_t)(((int64_t)amod_1 * (int64_t)lfo_val) >> 24);
    amd_mod = amod_1;                    /* max(amod_1, amod_2), max((1 << 24) - eg_mod, ..): 0 */
    for (op = 0; op < 6; op++) {
        int32_t level;
        v->op[op].freq = dx7_freq(v->basepitch[op] + (v->opmode[op] ? 0 : pitch_mod));
        level = env_getsample(&v->env[op]);
        if (v->ampmodsens[op] != 0) {
            uint32_t sensamp = (uint32_t)(((uint64_t)amd_mod * (uint64_t)(uint32_t)v->ampmodsens[op]) >> 24);
            uint32_t pt = ampmod_pt(sensamp);
            uint32_t ldiff = (uint32_t)(((uint64_t)level * ((uint64_t)pt << 4)) >> 28);
            level -= (int32_t)ldiff;
        }
        v->op[op].level_in = level;
    }
}

static void note_compute(dx7_voice_t *v, int32_t *buf, int32_t lfo_val, int32_t lfo_delay, int32_t (*bus)[N])
{
    note_params(v, lfo_val, lfo_delay);
    core_render(buf, v->op, v->algorithm, v->fb_buf, v->fb_shift, bus);
}

/* ------------------------------------------------------- the voice --- */
/* as Dexed's keydown: the patch's transpose (24 = none), init, the LFO's key down, and the
 * oscillators' phases reset if OSC KEY SYNC is on */
PLAT_COLD void dx7_note_on(dx7_voice_t *v, const uint8_t *vced, int note, int velocity)   /* FM-1: PLAT_COLD */
{
    int op;
    note += vced[144] - 24;
    note = note < 0 ? 0 : note > 127 ? 127 : note;   /* FM-1: Dexed indexes its tuning table unchecked */
    lfo_reset(&v->lfo, vced + 137);
    lfo_keydown(&v->lfo);
    note_init(v, vced, note, velocity);
    v->bend = 0;                         /* FM-1 addition */
    if (vced[136])
        for (op = 0; op < 6; op++) {     /* Dx7Note::oscSync */
            v->op[op].gain_out = 0;
            v->op[op].phase = 0;
        }
}

void dx7_note_off(dx7_voice_t *v)
{
    int op;
    for (op = 0; op < 6; op++)
        env_keydown(&v->env[op], 0);
    penv_keydown(&v->penv, 0);
}

/* FM-1 addition: silence now (FX); the next note starts from nothing */
void dx7_cut(dx7_voice_t *v)
{
    int op;
    v->initialised = 0;
    for (op = 0; op < 6; op++)
        v->op[op].gain_out = 0;
    v->fb_buf[0] = v->fb_buf[1] = 0;
}

void dx7_render(dx7_voice_t *v, int32_t *buf)
{
    int32_t lfo_val = lfo_getsample(&v->lfo), lfo_delay = lfo_getdelay(&v->lfo), out[DX7_N], bus[2][DX7_N];
    int i;
    for (i = 0; i < DX7_N; i++)
        out[i] = 0;
    note_compute(v, out, lfo_val, lfo_delay, bus);
    for (i = 0; i < DX7_N; i++)          /* FM-1: MSFA's scale -> 16 bits, x the output scale (dx7.h) */
        buf[i] += (int32_t)(((int64_t)out[i] * DX7_OUT_Q16) >> 29);
}

/* FM-1 addition: a block of the voice's time without its sound (the player's parked voices): the LFO, the
 * envelopes, each operator's gain and phase move on as core_render moves them; only the feedback's last two
 * samples stay as they were */
void dx7_skip(dx7_voice_t *v)
{
    int32_t lfo_val = lfo_getsample(&v->lfo), lfo_delay = lfo_getdelay(&v->lfo);
    int op;
    note_params(v, lfo_val, lfo_delay);
    for (op = 0; op < 6; op++) {
        v->op[op].gain_out = dx7_exp2(v->op[op].level_in - (14 * (1 << 24)));
        v->op[op].phase += (uint32_t)v->op[op].freq << LG_N;
    }
}

/* FM-1 addition: a parameter of a sounding voice turned (the player's parameter commands), by its byte
 * in the 156-byte layout: the operators' rates, the feedback, the LFO's speed, depths, wave and pitch
 * sensitivity act at once (a rate from the envelope's next stage on); the rest from the next note */
void dx7_live(dx7_voice_t *v, int at, int value)
{
    int op = at / 21, k = at % 21;
    if (at < 126) {
        if (k < 4)
            v->env[op].rates[k] = (uint8_t)(value > 99 ? 99 : value);
    } else if (at == 135) {
        v->fb_shift = (value & 7) != 0 ? 8 - (value & 7) : 16;
    } else if (at == 137) {
        v->lfo.delta = DX7_LFO_DELTA[value > 99 ? 99 : value];
    } else if (at == 139) {
        v->pitchmoddepth = ((value > 99 ? 99 : value) * 165) >> 6;
    } else if (at == 140) {
        v->ampmoddepth = ((value > 99 ? 99 : value) * 165) >> 6;
    } else if (at == 142) {
        v->lfo.waveform = (uint8_t)(value > 5 ? 5 : value);
    } else if (at == 143) {
        v->pitchmodsens = pitchmodsenstab[value & 7];
    }
}

int dx7_carrier(int algorithm, int op)   /* FM-1 addition (the views) */
{
    return (algorithms[algorithm & 31][6 - op] & OUT_BUS_ADD) != 0;
}

int dx7_active(const dx7_voice_t *v)
{
    int i;
    if (!v->initialised)
        return 0;
    for (i = 0; i < 6; i++)
        if ((algorithms[v->algorithm][i] & OUT_BUS_ADD) && env_active(&v->env[i]))
            return 1;
    return 0;
}
