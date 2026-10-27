/* SPDX-License-Identifier: GPL-3.0-only
 * Copyright (C) 2026 Vasily Kazantsev */
/* The sample voice and PTR's decoder: see samp.h; the format is specs/files/ptr.md. */
#include "samp.h"

/* --------------------------------------------------------- decoder --- */
#define PTR_BLOCK 256
#define PTR_PART 64
#define PTR_RAW 7

/* leading zero bits of a byte (8 for 0) */
#define R2(n) n, n
#define R4(n) R2(n), R2(n)
#define R8(n) R4(n), R4(n)
#define R16(n) R8(n), R8(n)
#define R32(n) R16(n), R16(n)
#define R64(n) R32(n), R32(n)
static const uint8_t lz8[256] = {8, 7, R2(6), R4(5), R8(4), R16(3), R32(2), R64(1), R64(0), R64(0)};

/* the record's byte at off; past the record, 0 */
static uint32_t byte_at(ptr_dec_t *d, uint32_t off)
{
    return off < d->rec_len ? d->rec[off] : 0u;
}

static uint32_t rd32(ptr_dec_t *d, uint32_t off)
{
    return byte_at(d, off) | byte_at(d, off + 1) << 8 | byte_at(d, off + 2) << 16 | byte_at(d, off + 3) << 24;
}

/* the bit reader: d->bits holds nbits valid bits, MSB first; past the record, zeros */
static void fill(ptr_dec_t *d)
{
    while (d->nbits <= 24) {
        d->bits |= byte_at(d, d->at) << (24 - d->nbits);
        d->at++;
        d->nbits += 8;
    }
}

static uint32_t getbits(ptr_dec_t *d, int n)     /* n 1-24 */
{
    uint32_t v;
    fill(d);
    v = d->bits >> (32 - n);
    d->bits <<= n;
    d->nbits -= n;
    return v;
}

/* block b starts: its place from the index (the offset of its group of 16, then the lengths before it) */
static void block(ptr_dec_t *d, int32_t b)
{
    int32_t nb = (d->length + PTR_BLOCK - 1) / PTR_BLOCK, groups = (nb + 15) / 16, j;
    uint32_t at = rd32(d, 4u + 4u * (uint32_t)(b / 16));
    uint32_t lens = 4u + 4u * (uint32_t)groups;
    for (j = b - b % 16; j < b; j++)
        at += byte_at(d, lens + 2u * (uint32_t)j) | byte_at(d, lens + 2u * (uint32_t)j + 1u) << 8;
    d->at = at;
    d->bits = 0;
    d->nbits = 0;
    d->left = d->length - b * PTR_BLOCK < PTR_BLOCK ? d->length - b * PTR_BLOCK : PTR_BLOCK;
    d->mode = (int32_t)getbits(d, 8);
    d->warm = d->mode == PTR_RAW ? d->left : d->mode > 3 ? d->left : d->mode;
    if (d->mode > 3 && d->mode != PTR_RAW)       /* (not a block of PTR's: silence) */
        d->mode = -1;
    d->part = 0;
    d->x1 = d->x2 = d->x3 = 0;
}

/* The next n samples into out (16-bit scale), or dropped (out NULL): a run of a block at a time, its parts (the raw
 * samples, then each partition of residuals) in loops of their own, the bit reader and the predictor in locals,
 * the bytes read straight from the record (past its end, 0 as byte_at gives). */
#define P_FILL()                                                                \
    while (nbits <= 24) {                                                       \
        bits |= (at < len ? (uint32_t)rec[at] : 0u) << (24 - nbits);           \
        at++;                                                                   \
        nbits += 8;                                                             \
    }
#define P_GET(v, nb)                                                            \
    do {                                                                        \
        P_FILL();                                                               \
        (v) = bits >> (32 - (nb));                                              \
        bits <<= (nb);                                                          \
        nbits -= (nb);                                                          \
    } while (0)
static void produce(ptr_dec_t *d, int16_t *out, int32_t n)
{
    while (n > 0) {
        const uint8_t *rec = d->rec;
        uint32_t len = d->rec_len, at, bits;
        int32_t run, nbits, x1, x2, x3, i = 0, mul = d->wide ? 1 : 256;
        if (d->pos >= d->length) {       /* past the end: silence */
            for (; out && n > 0; n--, d->pos++)
                *out++ = 0;
            d->pos += n;
            return;
        }
        if (d->left <= 0)
            block(d, d->pos / PTR_BLOCK);
        run = n < d->left ? n : d->left; /* (a block never runs past the sample's end) */
        at = d->at;
        bits = d->bits;
        nbits = d->nbits;
        x1 = d->x1;
        x2 = d->x2;
        x3 = d->x3;
        if (d->mode < 0) {               /* (not a block of PTR's: silence) */
            for (; i < run; i++) {
                x3 = x2;
                x2 = x1;
                x1 = 0;
                if (out)
                    *out++ = 0;
            }
        } else {
            int32_t c1 = d->mode == 1 ? 1 : d->mode == 2 ? 2 : d->mode == 3 ? 3 : 0;   /* the predictor's */
            int32_t c2 = d->mode == 2 ? -1 : d->mode == 3 ? -3 : 0, c3 = d->mode == 3 ? 1 : 0;
            int32_t w = d->warm < run ? d->warm : run;
            for (; i < w; i++) {         /* raw samples: a predictor's first ones, or a raw block */
                uint32_t v;
                int32_t x;
                if (d->wide) {
                    P_GET(v, 16);
                    x = (int32_t)(v ^ 0x8000u) - 0x8000;
                } else {
                    P_GET(v, 8);
                    x = (int32_t)(v ^ 0x80u) - 0x80;
                }
                x3 = x2;
                x2 = x1;
                x1 = x;
                if (out) {
                    int32_t y = x * mul;
                    *out++ = (int16_t)(y > 32767 ? 32767 : y < -32768 ? -32768 : y);
                }
            }
            d->warm -= w;
            while (i < run) {            /* residuals, a partition at a time */
                int32_t k = d->k, esc = d->wide ? 31 : 15, m;
                if (d->part == 0) {
                    uint32_t v;
                    P_GET(v, d->wide ? 5 : 4);
                    k = d->k = (int32_t)v;
                    d->part = PTR_PART;
                }
                m = run - i < d->part ? run - i : d->part;
                d->part -= m;
                for (m += i; i < m; i++) {
                    uint32_t u, q = 0, z;
                    int32_t x;
                    if (k == esc) {
                        P_GET(u, d->wide ? 20 : 12);
                    } else {
                        P_FILL();
                        while ((z = lz8[bits >> 24]) == 8) {
                            q += 8;
                            bits <<= 8;
                            nbits -= 8;
                            P_FILL();
                        }
                        q += z;
                        bits <<= z + 1;
                        nbits -= (int32_t)z + 1;
                        if (k > 24) {
                            uint32_t hi, lo;
                            P_GET(hi, k - 16);
                            P_GET(lo, 16);
                            u = q << k | hi << 16 | lo;
                        } else if (k) {
                            uint32_t lo;
                            P_GET(lo, k);
                            u = q << k | lo;
                        } else {
                            u = q;
                        }
                    }
                    x = ((int32_t)(u >> 1) ^ -(int32_t)(u & 1u)) + c1 * x1 + c2 * x2 + c3 * x3;
                    x3 = x2;
                    x2 = x1;
                    x1 = x;
                    if (out) {
                        int32_t y = x * mul;
                        *out++ = (int16_t)(y > 32767 ? 32767 : y < -32768 ? -32768 : y);
                    }
                }
            }
        }
        d->at = at;
        d->bits = bits;
        d->nbits = nbits;
        d->x1 = x1;
        d->x2 = x2;
        d->x3 = x3;
        d->left -= run;
        d->pos += run;
        n -= run;
    }
}
#undef P_FILL
#undef P_GET

static int32_t ptr_read_src(ptr_dec_t *d, const uint8_t *rec, uint32_t rec_len, int32_t pos, int32_t n, int16_t *out)
{
    int32_t i;
    if (!rec || rec_len < 4u || pos < 0) {
        for (i = 0; i < n; i++)
            out[i] = 0;
        return 0;
    }
    if (d->rec != rec || pos < d->pos || pos - d->pos > PTR_BLOCK) {
        uint32_t coding;                 /* elsewhere: from its block's start */
        d->rec = rec;
        d->rec_len = rec_len;
        coding = byte_at(d, 0);
        if (coding != 1u && coding != 2u) {
            d->rec = 0;
            for (i = 0; i < n; i++)
                out[i] = 0;
            return 0;
        }
        d->wide = coding == 2u;
        d->length = (int32_t)(byte_at(d, 1) | byte_at(d, 2) << 8 | byte_at(d, 3) << 16);
        d->pos = pos / PTR_BLOCK * PTR_BLOCK;
        d->left = 0;
        if (d->pos < d->length)
            block(d, d->pos / PTR_BLOCK);
    }
    if (pos > d->pos)
        produce(d, 0, pos - d->pos);
    produce(d, out, n);
    return n;
}

int32_t ptr_read(ptr_dec_t *d, const uint8_t *rec, uint32_t rec_len, int32_t pos, int32_t n, int16_t *out)
{
    return ptr_read_src(d, rec, rec_len, pos, n, out);
}

/* ----------------------------------------------------------- voice --- */
static const uint16_t SEMI_Q15[12] = {   /* 2^(n/12), Q15 */
    32768, 34716, 36781, 38968, 41285, 43740, 46341, 49097, 52016, 55109, 58386, 61858};
static const uint16_t FINE_Q15[64] = {   /* 2^(j/768), Q15: 1/64 semitones */
    32768, 32798, 32827, 32857, 32887, 32916, 32946, 32976, 33005, 33035, 33065, 33095, 33125, 33155, 33185, 33215,
    33245, 33275, 33305, 33335, 33365, 33395, 33425, 33455, 33486, 33516, 33546, 33576, 33607, 33637, 33667, 33698,
    33728, 33759, 33789, 33820, 33850, 33881, 33911, 33942, 33973, 34003, 34034, 34065, 34095, 34126, 34157, 34188,
    34219, 34250, 34281, 34312, 34343, 34374, 34405, 34436, 34467, 34498, 34529, 34560, 34591, 34623, 34654, 34685};
static const uint16_t ATT_Q15[64] = {    /* 2^(-j/64), Q15: the envelope's gain within an octave */
    32768, 32415, 32066, 31720, 31379, 31041, 30706, 30376, 30048, 29725, 29405, 29088, 28774, 28464, 28158, 27855,
    27554, 27258, 26964, 26674, 26386, 26102, 25821, 25543, 25268, 24995, 24726, 24460, 24196, 23936, 23678, 23423,
    23170, 22921, 22674, 22430, 22188, 21949, 21713, 21479, 21247, 21019, 20792, 20568, 20347, 20127, 19911, 19696,
    19484, 19274, 19066, 18861, 18658, 18457, 18258, 18061, 17867, 17674, 17484, 17296, 17109, 16925, 16743, 16562};
#define STEP_MAX (16u << 16)             /* 16 samples a frame at most (4 octaves up from 44.1 kHz) */

/* The envelope, a DX7 operator's, a step a block of 64 frames as MSFA's env.cc: the level in its units (Q24
 * octaves, ENV_FULL the full level; the gain 2^(level / 2^24 - 15)), from silence; a rising stage first jumps
 * to 1716 << 16 (about -50 dB), as the DX7 does; a stage already at its level, or an attack to level 0, holds
 * for a time its rate sets (the statics); the levels on the DX7's output curve, at full output level */
#define ENV_FULL (15 << 24)
#define ENV_SILENT (99 << 21)            /* (below full: silent from here on) */
static const uint8_t LEVEL_LUT[20] = {0, 5, 9, 13, 17, 20, 23, 25, 27, 29, 31, 33, 35, 37, 39, 41, 42, 43, 45, 46};
static const int32_t STATICS[77] = {     /* MSFA's statics: frames, by rate */
    1764000, 1764000, 1411200, 1411200, 1190700, 1014300, 992250, 882000, 705600, 705600, 584325, 507150, 502740,
    441000, 418950, 352800, 308700, 286650, 253575, 220500, 220500, 176400, 145530, 145530, 125685, 110250, 110250,
    88200, 88200, 74970, 61740, 61740, 55125, 48510, 44100, 37485, 31311, 30870, 27562, 27562, 22050, 18522, 17640,
    15435, 14112, 13230, 11025, 9261, 9261, 7717, 6615, 6615, 5512, 5512, 4410, 3969, 3969, 3439, 2866, 2690, 2249,
    1984, 1896, 1808, 1411, 1367, 1234, 1146, 926, 837, 837, 705, 573, 573, 529, 441, 441};

static int32_t env_inc(uint32_t rate)
{
    uint32_t q = (rate * 41u) >> 6;
    q = q > 63u ? 63u : q;
    return (int32_t)((4u + (q & 3u)) << (8u + (q >> 2)));
}

static int32_t env_gain(int32_t att)     /* Q15, from the attenuation below full (Q24 octaves) */
{
    if (att >= ENV_SILENT)
        return 0;
    if (att < 0)
        att = 0;
    return (int32_t)(((32768u >> (att >> 24)) * ATT_Q15[(att >> 18) & 63]) >> 15);
}

/* MSFA's advance(): stage 0-2 to L1-L3, 3 to L4 (moving only once the key is up), 4 done */
static void env_stage(samp_voice_t *v, uint32_t stage)
{
    uint32_t l, r;
    int32_t a;
    v->stage = (uint8_t)stage;
    if (stage > 3u)
        return;
    l = v->s.env[4u + stage];
    r = v->s.env[stage];
    a = (int32_t)((l >= 20u ? 28u + l : LEVEL_LUT[l]) >> 1) * 64 + (127 << 5) - 4256;   /* (full output level) */
    v->target = (a < 16 ? 16 : a) << 16;
    v->rising = v->target > v->level;
    if (v->target == v->level || (stage == 0u && l == 0u)) {
        v->statics = r < 77u ? STATICS[r] : 20 * (99 - (int32_t)r);
        if (r < 77u && stage == 0u && l == 0u)
            v->statics /= 20;
    } else {
        v->statics = 0;
    }
}

/* MSFA's getsample(): one block's step */
static void env_block(samp_voice_t *v)
{
    int32_t inc;
    if (v->statics) {
        v->statics -= SAMP_N;
        if (v->statics <= 0) {
            v->statics = 0;
            env_stage(v, v->stage + 1u);
        }
    }
    if (!(v->stage < 3u || (v->stage == 3u && !v->down)) || v->statics)
        return;
    inc = env_inc(v->s.env[v->stage]);
    if (v->rising) {
        if (v->level < (1716 << 16))
            v->level = 1716 << 16;
        v->level += (((17 << 24) - v->level) >> 24) * inc;
        if (v->level >= v->target) {
            v->level = v->target;
            env_stage(v, v->stage + 1u);
        }
    } else {
        v->level -= inc;
        if (v->level <= v->target) {
            v->level = v->target;
            env_stage(v, v->stage + 1u);
        }
    }
}

void samp_pitch(samp_voice_t *v, int note, int32_t bend64)
{
    const samp_t *s = &v->s;
    int32_t n = (note - (int32_t)s->base) * 64 + bend64 + s->tune / 2;
    int32_t oct = n >= 0 ? n / 768 : -((767 - n) / 768), r = n - 768 * oct;
    uint32_t q = s->rate / 44100u;
    uint64_t step = (q << 16) + (((s->rate - q * 44100u) << 16) / 44100u);
    step = (step * SEMI_Q15[r >> 6]) >> 15;
    step = (step * FINE_Q15[r & 63]) >> 15;
    step = oct >= 0 ? (oct < 8 ? step << oct : STEP_MAX) : (oct > -24 ? step >> -oct : 0u);
    v->step = step > STEP_MAX ? STEP_MAX : step ? (uint32_t)step : 1u;
}

/* the loop's points as they stand: a loop with room (two samples at least) inside the sample and END */
static int loop_room(const samp_voice_t *v)
{
    return v->s.loop != SAMP_LOOP_OFF && v->s.loop_end > v->s.loop_start + 1 && v->s.loop_start >= 0;
}

void samp_note_on(samp_voice_t *v, const samp_t *s, int note, int32_t offset)
{
    uint32_t i;
    v->s = *s;
    if (v->s.end <= 0 || v->s.end > v->s.length)
        v->s.end = v->s.length;
    if (v->s.loop_end > v->s.end)
        v->s.loop_end = v->s.end;
    for (i = 0; i < 8u; i++)
        v->s.env[i] = s->env[i] > 99u ? 99u : s->env[i];
    v->note = (uint8_t)note;
    samp_pitch(v, note, 0);
    v->pos = v->s.start + (offset > 0 ? offset : 0);
    if (v->pos > v->s.end - 1)
        v->pos = v->s.end - 1;
    v->frac = 0;
    v->dir = 1;
    v->looping = (uint8_t)loop_room(v);
    v->on = v->s.rec && v->s.end > 0 && v->pos >= 0;
    v->dec.rec = 0;                      /* (the window starts afresh) */
    v->w0 = 0;
    v->wn = 0;
    v->ls_val = 0;
    if (v->on && v->looping)
        ptr_read_src(&v->dec, v->s.rec, v->s.rec_len, v->s.loop_start, 1, &v->ls_val);
    v->level = 0;                        /* (from silence, as MSFA's init) */
    v->down = 1;
    v->amp = 0;
    env_stage(v, 0);
}

void samp_note_off(samp_voice_t *v)
{
    if (v->on && v->down) {              /* (MSFA's keydown(false): to L4 from where it is) */
        v->down = 0;
        env_stage(v, 3);
    }
}

void samp_cut(samp_voice_t *v) { v->on = 0; }

int samp_active(const samp_voice_t *v) { return v->on; }

/* sample i (16-bit scale) from the window, which is filled again around it when it is not there: forward
 * from i, or backward (a ping-pong pass) ending just after it */
static int32_t at(samp_voice_t *v, int32_t i)
{
    if (i < v->w0 || i >= v->w0 + v->wn) {
        int32_t from = v->dir > 0 ? i : i - (SAMP_WIN - 2);
        if (from < 0)
            from = 0;
        v->w0 = from;
        v->wn = SAMP_WIN;
        ptr_read_src(&v->dec, v->s.rec, v->s.rec_len, from, SAMP_WIN, v->win);
    }
    return v->win[i - v->w0];
}

/* the window holding samples lo .. hi (hi - lo < SAMP_WIN), going forward: what it holds of them kept, the rest
 * decoded on from its end (a sample is the same whichever way the window comes to hold it) */
static void ensure(samp_voice_t *v, int32_t lo, int32_t hi)
{
    if (lo >= v->w0 && hi < v->w0 + v->wn)
        return;
    if (lo >= v->w0 && lo < v->w0 + v->wn) {
        int32_t keep = v->w0 + v->wn - lo, i;
        for (i = 0; i < keep; i++)
            v->win[i] = v->win[lo - v->w0 + i];
        ptr_read_src(&v->dec, v->s.rec, v->s.rec_len, v->w0 + v->wn, SAMP_WIN - keep, v->win + keep);
    } else {
        ptr_read_src(&v->dec, v->s.rec, v->s.rec_len, lo, SAMP_WIN, v->win);
    }
    v->w0 = lo;
    v->wn = SAMP_WIN;
}

void samp_render(samp_voice_t *v, int32_t *out)
{
    int32_t i, g, acc, da;
    int32_t ls = v->s.loop_start, le = v->s.loop_end, end = v->s.end;
    if (!v->on)
        return;
    env_block(v);
    g = (int32_t)(((int64_t)env_gain(ENV_FULL - v->level) * SAMP_OUT_Q16) >> 16);
    acc = v->amp << 6;                   /* the gain ramps from the last block's across this one */
    da = g - v->amp;
    if (v->dir > 0) {                    /* forward with no loop point and no end in the block: a plain run */
        uint32_t fp = v->frac + (SAMP_N - 1u) * v->step;   /* (Q16 from pos: < 2^26) */
        int32_t last = v->pos + (int32_t)(fp >> 16), after = v->pos + (int32_t)((fp + v->step) >> 16);
        if (last + 1 < (v->looping ? le : end) && after < (v->looping ? le : end - 1) &&
            last + 1 - v->pos < SAMP_WIN) {
            const int16_t *w;
            ensure(v, v->pos, last + 1);
            w = v->win + (v->pos - v->w0);
            fp = v->frac;
            for (i = 0; i < SAMP_N; i++, fp += v->step) {
                int32_t a = w[fp >> 16], b = w[(fp >> 16) + 1u];
                int32_t y = a + (((b - a) * (int32_t)((fp & 0xFFFFu) >> 1)) >> 15);   /* (< 2^31: 32 bits) */
                acc += da;
                out[i] += (y * (acc >> 6)) >> 15;
            }
            v->pos += (int32_t)(fp >> 16);
            v->frac = fp & 0xFFFFu;
            v->amp = g;
            if (!g && ENV_FULL - v->level >= ENV_SILENT && !v->down)
                v->on = 0;
            return;
        }
    }
    for (i = 0; i < SAMP_N; i++) {
        int32_t ip = v->pos, a, b, y, nx;
        uint32_t fr = v->frac;
        int loop = v->looping;
        nx = ip + 1 < (loop ? le : end) ? ip + 1 : -1;
        a = at(v, ip);
        b = nx >= 0 ? at(v, nx) : loop && v->s.loop == SAMP_LOOP_FORWARD ? v->ls_val : a;
        y = a + (int32_t)(((int64_t)(b - a) * (int32_t)(fr >> 1)) >> 15);
        acc += da;
        out[i] += (y * (acc >> 6)) >> 15;
        /* on: forward or back by the step */
        if (v->dir > 0) {
            uint32_t f = fr + v->step;
            v->pos += (int32_t)(f >> 16);
            v->frac = f & 0xFFFFu;
        } else {
            int32_t f = (int32_t)fr - (int32_t)(v->step & 0xFFFFu);
            v->pos -= (int32_t)(v->step >> 16);
            if (f < 0) {
                f += 65536;
                v->pos--;
            }
            v->frac = (uint32_t)f;
        }
        if (loop) {
            if (v->s.loop == SAMP_LOOP_PINGPONG) {
                if (v->dir > 0 && v->pos >= le) {
                    if (!v->down) {
                        v->looping = 0;
                    } else {
                        v->pos = 2 * le - 1 - v->pos;
                        v->frac = 65535u - v->frac;
                        v->dir = -1;
                    }
                } else if (v->dir < 0 && v->pos < ls) {
                    v->pos = 2 * ls - 1 - v->pos;
                    v->frac = 65535u - v->frac;
                    v->dir = 1;
                    if (!v->down)
                        v->looping = 0;
                }
            } else if (v->pos >= le) {
                if (!v->down)
                    v->looping = 0;
                else
                    v->pos -= le - ls;
            }
        }
        if (v->pos < 0 || (v->pos >= end - 1 && !v->looping)) {   /* (past END) */
            v->on = 0;
            break;
        }
    }
    v->amp = g;
    if (!g && ENV_FULL - v->level >= ENV_SILENT && !v->down)
        v->on = 0;
}

/* the envelope's gain now, Q15 (1.0 full), without the engine's output scale: the screens' picture of it */
int32_t samp_env_q15(const samp_voice_t *v) { return env_gain(ENV_FULL - v->level); }

/* a block of a sounding voice's time without its sound (the player's parked voices): its envelope and its place
 * in the sample go on as samp_render's (the loop, a ping-pong's way, END); nothing is decoded: the window is
 * filled again where it plays when it sounds again */
void samp_skip(samp_voice_t *v)
{
    int32_t i, ls = v->s.loop_start, le = v->s.loop_end, end = v->s.end;
    if (!v->on)
        return;
    env_block(v);
    v->amp = (int32_t)(((int64_t)env_gain(ENV_FULL - v->level) * SAMP_OUT_Q16) >> 16);
    for (i = 0; i < SAMP_N; i++) {
        if (v->dir > 0) {
            uint32_t f = v->frac + v->step;
            v->pos += (int32_t)(f >> 16);
            v->frac = f & 0xFFFFu;
        } else {
            int32_t f = (int32_t)v->frac - (int32_t)(v->step & 0xFFFFu);
            v->pos -= (int32_t)(v->step >> 16);
            if (f < 0) {
                f += 65536;
                v->pos--;
            }
            v->frac = (uint32_t)f;
        }
        if (v->looping) {
            if (v->s.loop == SAMP_LOOP_PINGPONG) {
                if (v->dir > 0 && v->pos >= le) {
                    if (!v->down) {
                        v->looping = 0;
                    } else {
                        v->pos = 2 * le - 1 - v->pos;
                        v->frac = 65535u - v->frac;
                        v->dir = -1;
                    }
                } else if (v->dir < 0 && v->pos < ls) {
                    v->pos = 2 * ls - 1 - v->pos;
                    v->frac = 65535u - v->frac;
                    v->dir = 1;
                    if (!v->down)
                        v->looping = 0;
                }
            } else if (v->pos >= le) {
                if (!v->down)
                    v->looping = 0;
                else
                    v->pos -= le - ls;
            }
        }
        if (v->pos < 0 || (v->pos >= end - 1 && !v->looping)) {   /* (past END) */
            v->on = 0;
            return;
        }
    }
}
