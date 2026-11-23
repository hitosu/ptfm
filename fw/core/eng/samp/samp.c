/* SPDX-License-Identifier: GPL-3.0-only
 * Copyright (C) 2026 Vasily Kazantsev */
/* The sample voice and PTR's decoder: see samp.h; the format is specs/files/ptr.md. */
#include "samp.h"
#ifndef PLAT_COLD
#define PLAT_COLD                        /* (a screen's work: built for size on the FM-1, core/plat.h) */
#endif

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

/* the step samp_pitch gives a voice of s (a ghost's too, which has none) */
uint32_t samp_step(const samp_t *s, int note, int32_t bend64)
{
    int32_t n = (note - (int32_t)s->base) * 64 + bend64 + s->tune / 2;
    int32_t oct = n >= 0 ? n / 768 : -((767 - n) / 768), r = n - 768 * oct;
    uint32_t q = s->rate / 44100u;
    uint64_t step = (q << 16) + (((s->rate - q * 44100u) << 16) / 44100u);
    step = (step * SEMI_Q15[r >> 6]) >> 15;
    step = (step * FINE_Q15[r & 63]) >> 15;
    step = oct >= 0 ? (oct < 8 ? step << oct : STEP_MAX) : (oct > -24 ? step >> -oct : 0u);
    return step > STEP_MAX ? STEP_MAX : step ? (uint32_t)step : 1u;
}

void samp_pitch(samp_voice_t *v, int note, int32_t bend64)
{
    v->step = samp_step(&v->s, note, bend64);
}

/* the loop's end as a voice plays it: the lesser of LOOP END and END at every moment (specs/files/commands.md: END is
 * taken for LOOP END when it comes first). A command turns either one as it is, so END turned down past LOOP END and up
 * again brings the loop's end back */
static inline __attribute__((always_inline)) int32_t loop_end_of(const samp_t *s)
{
    return s->loop_end < s->end ? s->loop_end : s->end;
}

/* the loop's points as they stand: a loop with room (two samples at least) inside the sample and END */
static int loop_room(const samp_voice_t *v)
{
    return v->s.loop != SAMP_LOOP_OFF && loop_end_of(&v->s) > v->s.loop_start + 1 && v->s.loop_start >= 0;
}

void samp_note_on(samp_voice_t *v, const samp_t *s, int note, int32_t offset)
{
    v->s = *s;
    samp_prep(&v->s);
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
    int32_t ls = v->s.loop_start, le = loop_end_of(&v->s), end = v->s.end;
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
PLAT_COLD int32_t samp_env_q15(const samp_voice_t *v) { return env_gain(ENV_FULL - v->level); }

/* a block of a sounding voice's time without its sound (what the forward work stands for): its envelope and its place
 * in the sample go on as samp_render's (the loop, a ping-pong's way, END); nothing is decoded: the window is
 * filled again where it plays when it sounds again. Its one caller is the sampler screen's picture of a note at the
 * BASE NOTE, traced a block at a time in the main loop */
PLAT_COLD void samp_skip(samp_voice_t *v)
{
    int32_t i, ls = v->s.loop_start, le = loop_end_of(&v->s), end = v->s.end;
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

/* ------------------------------------------------ a note without its voice --- */
/* (specs/proposals/202611121830-ghost-notes.md, sections 2 and 5) A ghost keeps its place in the sample, moved on as
 * samp_skip's frames move a voice's; it comes back into a fresh voice at that place, its envelope worked forward. */

/* what samp_note_on makes of its own copy (its first step: a ghost's place walks by the same). LOOP END is kept as it
 * is: the loop's end is taken as the lesser of it and END wherever it is read (loop_end_of) */
PLAT_COLD void samp_prep(samp_t *s)
{
    uint32_t i;
    if (s->end <= 0 || s->end > s->length)
        s->end = s->length;
    for (i = 0; i < 8u; i++)
        s->env[i] = s->env[i] > 99u ? 99u : s->env[i];
}

/* the place from a position x (Q16) and its flags; an ended one keeps no position */
static PLAT_COLD void place_put(samp_place_t *p, int64_t x, uint32_t flags)
{
    if (flags & SAMP_PL_END)
        x = 0;
    p->frac = (uint16_t)(x & 0xFFFF);
    p->lo = (uint16_t)(x >> 16);
    p->hi = (uint16_t)(((uint32_t)(x >> 32) & SAMP_PL_POS) | flags);
}

static PLAT_COLD int32_t place_pos(const samp_place_t *p)
{
    return (int32_t)((uint32_t)(p->hi & SAMP_PL_POS) << 16 | p->lo);
}

PLAT_COLD void samp_place_of(const samp_voice_t *v, samp_place_t *p)
{
    uint32_t f = (v->dir < 0 ? SAMP_PL_BACK : 0u) | (v->looping ? SAMP_PL_LOOP : 0u);
    if (!v->on || v->pos < 0 || v->pos > SAMP_PL_MOST)   /* (a position run away that far: ended, samp.h) */
        f |= SAMP_PL_END;
    place_put(p, (int64_t)v->pos << 16 | v->frac, f);
}

/* as samp_note_on starts its voice: START and the offset within END, forward, in its loop if the loop has room */
PLAT_COLD void samp_place_start(const samp_t *s, int32_t offset, samp_place_t *p)
{
    int32_t end = s->end <= 0 || s->end > s->length ? s->length : s->end;
    int32_t le = s->loop_end > end ? end : s->loop_end, pos = s->start + (offset > 0 ? offset : 0);
    uint32_t f;
    if (pos > end - 1)
        pos = end - 1;
    f = s->loop != SAMP_LOOP_OFF && le > s->loop_start + 1 && s->loop_start >= 0 ? SAMP_PL_LOOP : 0u;
    if (!(s->rec && end > 0 && pos >= 0))
        f |= SAMP_PL_END;
    place_put(p, (int64_t)pos << 16, f);
}

PLAT_COLD void samp_place_set(samp_voice_t *v, const samp_place_t *p)
{
    if (p->hi & SAMP_PL_END) {
        v->on = 0;
        return;
    }
    v->pos = place_pos(p);
    v->frac = p->frac;
    v->dir = (int8_t)(p->hi & SAMP_PL_BACK ? -1 : 1);
    v->looping = (uint8_t)((p->hi & SAMP_PL_LOOP) != 0);
}

PLAT_COLD void samp_note_at(samp_voice_t *v, const samp_t *s, int note, const samp_place_t *p)
{
    samp_note_on(v, s, note, 0);
    samp_place_set(v, p);
}

/* n / d (d > 0), or 0xFFFFFFFF when it is that or more: by halving, with no 64-bit division */
static PLAT_COLD uint32_t qdiv(uint64_t n, uint64_t d)
{
    uint32_t q = 0;
    int i;
    if ((n >> 32) >= d)
        return 0xFFFFFFFFu;
    for (i = 31; i >= 0; i--)
        if ((n >> i) >= d) {
            n -= d << i;
            q |= 1u << i;
        }
    return q;
}

/* (u + n x step) taken round a loop of len samples (u < len << 16, len < 2^22): its samples and its fraction apart,
 * in 32 bits */
static PLAT_COLD int64_t round_loop(int64_t u, uint32_t n, uint32_t step, uint32_t len)
{
    uint64_t t = (uint64_t)n * (step & 0xFFFFu) + (uint32_t)(u & 0xFFFF);
    uint32_t w = (uint32_t)(u >> 16) + n % len * (step >> 16) % len + (uint32_t)(t >> 16) % len;
    return (int64_t)(w % len) << 16 | (int64_t)(t & 0xFFFFu);
}

#define X_MOST ((int64_t)(SAMP_PL_MOST + 1) << 16)   /* (a position run away this far: the walk ends) */
#define X_FAR ((int64_t)1 << 62)         /* (a threshold a fall crosses at its first frame) */
#define A_END 1u                         /* at the crossing: the end */
#define A_OUT 2u                         /* out of the loop, and past END then or not */
#define A_LS 4u                          /* mirrored at the loop's start, forward from there */

/* x moved on by v a frame, up to the frame at which it crosses T (v > 0: reaches T or passes it; v < 0: falls under
 * it), that frame the last, or left frames if it does not cross in them (v 0: it never does); *hit: it crossed. The
 * frames before the crossing: (T - x - 1) / v rising, (x - T) / -v falling, none when that is under 0 */
static PLAT_COLD uint32_t run(int64_t *x, int64_t v, int64_t T, uint32_t left, int *hit)
{
    int64_t g = v > 0 ? T - *x - 1 : *x - T;
    uint32_t k = !v ? 0xFFFFFFFFu : g < 0 ? 0u : qdiv((uint64_t)g, (uint64_t)(v > 0 ? v : -v));
    *hit = k < left;
    k = *hit ? k + 1u : left;
    *x += (int64_t)k * v;
    return k;
}

/* One stretch of the walk (walk): x (the position, Q16), its flags *f (SAMP_PL_*) and the frames left moved on to
 * where its way changes. The positions in Q16, as samp_skip's frame moves them: on by the step S (back by it on a
 * ping-pong's pass back); held in the loop, a FORWARD loop's end LE takes it back by the loop Lq = LE - LS, a
 * PING-PONG's ends mirror it (x to 2 LE - 1 - x, 2 LS - 1 - x); let go, out of the loop at its end (or a pass back's
 * turn); then past END (x >= E1) or before the sample's start, the end. LE is the lesser of LOOP END and END, as the
 * render plays it (loop_end_of) */
static PLAT_COLD void stretch(const samp_t *s, uint32_t step, int held, int64_t *px, uint32_t *f, uint32_t *pleft)
{
    int64_t x = *px, S = step, v = -S;
    int64_t LS = (int64_t)s->loop_start << 16, LE = (int64_t)loop_end_of(s) << 16, Lq = LE - LS, T = LS;
    int64_t E1 = ((int64_t)s->end - 1) << 16;     /* (pos >= end - 1: x >= E1) */
    /* (the tests in whole samples, 32 bits: every threshold is a whole sample, so x >= T is xi >= t, and the step
     * against one is the step rounded up (sc) or down (sf); xb, xf where the next frame back or forward lands) */
    int32_t ls = s->loop_start, le = loop_end_of(s), lq = le - ls, le2 = 2 * le;
    int32_t sc = (int32_t)((step + 0xFFFFu) >> 16), sf = (int32_t)(step >> 16), xi = (int32_t)(x >> 16);
    int32_t xb = (int32_t)((x - S) >> 16), xf = (int32_t)((x + S) >> 16);
    int in = sc <= lq;                   /* (S <= Lq: the step within the loop's length) */
    uint32_t back = *f & SAMP_PL_BACK, loop = *f & SAMP_PL_LOOP, end = 0, left = *pleft, n = left, j, act = A_LS;
    int pp = s->loop == SAMP_LOOP_PINGPONG, hit, two = 0;
    if (!loop) {                         /* out of its loop: on past END; or back (its loop turned off on a pass back)
                                          * before the sample's start, or past END at once if it stands there */
        if (!back)
            v = S, T = E1;
        else
            T = xb >= s->end - 1 ? X_FAR : 0;
        act = A_END;
    } else if (!pp && back) {            /* a FORWARD loop going back (LOOP turned from PING-PONG on a pass back): before
                                          * the sample's start; past the loop's end, out of it (let go), or round it each
                                          * frame, back by the step and the loop (or standing, or running away) */
        if (xb < le)
            T = 0, act = A_END;
        else if (!held)
            T = X_FAR, act = A_OUT;
        else
            v = -S - Lq, T = sc + lq > 0 ? LE + S : X_MOST, act = sf + lq < 0 ? A_END : 0u;
    } else if (!held) {                  /* let go: on to the loop's end and out of it (a PING-PONG's pass back: to its
                                          * start, turned there and out), then on to END */
        if (!back)
            v = S, T = LE, act = A_OUT;
        else
            act = A_LS | A_OUT;
    } else if (in && xi >= ls && xi < le) {   /* held in it: round a FORWARD loop, to and fro in a PING-PONG one (its two
                                                * ways one turn of 2 Lq): one sum */
        int64_t u = back ? 2 * LE - 1 - LS - x : x - LS;   /* (a FORWARD loop's: never back here, u < Lq) */
        u = round_loop(u, left, step, (uint32_t)(Lq >> 16) << pp);
        back = u >= Lq ? SAMP_PL_BACK : 0u;
        x = back ? 2 * LE - 1 - LS - u : LS + u;
        *px = x;
        *f = back | loop;
        *pleft = 0;
        return;
    } else if (back) {
        /* a PING-PONG's pass back (from past its end, or at a step past its length): to its start, turned there (the
         * values set first) */
    } else if (xf < le) {                /* on to the loop's end, up to the frame that passes it */
        v = S, T = LE - S, act = 0;
    } else if (!pp) {                    /* a FORWARD loop held, past its end (started there, or its end moved): round
                                          * each frame, nearer by Lq - S, into it; at a step past its length (or its
                                          * points turned over), round each frame running away by S - Lq */
        v = S - Lq, T = in ? LE - S : X_MOST, act = in ? 0u : A_END;
    } else {                             /* a PING-PONG's, past its end: mirrored there (before the sample's start: the
                                          * end) into the loop; or before it, turned at its start, past its end again:
                                          * turns of two frames, nearer by 2 (Lq - S) a turn, or at a step past its
                                          * length (or its points turned over) farther out by 2 (S - Lq) until the
                                          * mirror at its end lands before the sample's start; an odd frame left, that
                                          * mirror */
        two = 1;
        v = 2 * (S - Lq), T = in ? LE + Lq - S : 2 * LE - S;
        n = xf >= le2 || (in && xf < le2 - ls) ? 0u : left / 2u;
    }
    j = run(&x, v, T, n, &hit) << two;
    if (two) {
        if (j < left && (!hit || !in)) {
            x = 2 * LE - 1 - x - S;
            back = SAMP_PL_BACK;
            end = x < 0;
            j++;
        }
    } else if (hit) {
        if (act & A_LS) {
            x = 2 * LS - 1 - x;
            back = 0;
        }
        if (act & A_OUT) {
            loop = 0;
            end = (int32_t)(x >> 16) >= s->end - 1;   /* (x >= E1) */
        }
        end |= act & A_END;
    }
    *px = x;
    *f = back | loop | (end ? SAMP_PL_END : 0u);
    *pleft = left - j;
}

/* the place walked as samp_skip's frames walk a voice's, a stretch at a time (each a sum up to where its way changes);
 * *went: the frames walked (the last the one it ended at, when it ended). Returns the stretches */
static PLAT_COLD uint32_t walk(const samp_t *s, samp_place_t *p, uint32_t step, uint32_t frames, int held,
                               uint32_t *went)
{
    int64_t x = (int64_t)place_pos(p) << 16 | p->frac;
    uint32_t f = p->hi & ~SAMP_PL_POS, n = 0, left = frames;
    *went = 0;
    if (f & SAMP_PL_END)
        return 0;
    while (left && !(f & SAMP_PL_END)) {
        n++;
        stretch(s, step, held, &x, &f, &left);
    }
    *went = frames - left;
    place_put(p, x, (f & SAMP_PL_END) || x < 0 || x >= X_MOST ? f | SAMP_PL_END : f);
    return n;
}

PLAT_COLD uint32_t samp_place_go(const samp_t *s, samp_place_t *p, uint32_t step, uint32_t frames, int held)
{
    uint32_t went;
    return walk(s, p, step, frames, held, &went);
}

PLAT_COLD uint32_t samp_end_blocks(const samp_t *s, const samp_place_t *p, uint32_t step, int held)
{
    samp_place_t q = *p;
    uint32_t went;
    if (p->hi & SAMP_PL_END)
        return 0;
    walk(s, &q, step, 0xFFFFFFFFu, held, &went);
    if (!(q.hi & SAMP_PL_END))
        return SAMP_NEVER;
    return went / SAMP_N + (went % SAMP_N != 0);
}

/* ----------------------------------------------------- the forward work --- */
/* The envelope worked on over many blocks at once, to the state samp_skip's blocks leave it in: within a stretch the
 * change a block is the same, so its blocks are one sum, and the block that may end it (a stage reaching its target, a
 * rising level leaving its band, a static stage's frames running out) is env_block's own */
#define ENV_JUMP (1716 << 16)            /* (env_block's: a rising stage starts here at the least) */

/* the envelope moved on by up to n blocks (n > 0) of its stretch from its state now (the blocks to where its change a
 * block ends, the last of them the one that may end it): those before that last one in one sum, the last by env_block
 * itself. Returns the blocks gone: n, or the stretch's when it ends first; n when it stands (its sustain held, or
 * done) */
static PLAT_COLD uint32_t env_run(samp_voice_t *v, uint32_t n)
{
    int32_t l = v->level, m, d;
    uint32_t span, band;
    if (v->statics) {
        d = 0;
        span = (uint32_t)(v->statics + SAMP_N - 1) / SAMP_N;
    } else if (!(v->stage < 3u || (v->stage == 3u && !v->down))) {
        return n;
    } else {
        d = env_inc(v->s.env[v->stage]);
        if (!v->rising) {
            span = l > v->target ? (uint32_t)(l - v->target - 1) / (uint32_t)d + 1u : 1u;
            d = -d;
        } else {
            l = l < ENV_JUMP ? ENV_JUMP : l;
            m = ((17 << 24) - l) >> 24;  /* the band: m x inc a block (2 at least: the levels are 15 << 24 at most) */
            d *= m;
            span = v->target > l ? (uint32_t)(v->target - l - 1) / (uint32_t)d + 1u : 1u;
            band = (uint32_t)(((17 - m) << 24) - l) / (uint32_t)d + 1u;   /* (the blocks that start within the band) */
            span = span < band ? span : band;
        }
    }
    band = n < span ? n : span - 1u;     /* (the blocks in the sum) */
    if (v->statics)
        v->statics -= (int32_t)band * SAMP_N;
    else
        v->level = l + (int32_t)band * d;
    if (n < span)
        return n;
    env_block(v);
    return span;
}

/* the envelope moved on by up to n blocks, a stretch a step, until the budget runs out (where a stretch ended); then
 * amp, the last block's gain, as those blocks leave it. Returns the blocks gone */
static PLAT_COLD uint32_t fwd_run(samp_voice_t *v, uint32_t n, int32_t *steps)
{
    uint32_t gone = 0;
    do {
        gone += env_run(v, n - gone);
        --*steps;
    } while (gone < n && *steps > 0);
    v->amp = (int32_t)(((int64_t)env_gain(ENV_FULL - v->level) * SAMP_OUT_Q16) >> 16);
    return gone;
}

/* samp.h: the held time, the note off, the release */
PLAT_COLD int samp_forward(samp_voice_t *v, uint32_t *on, uint32_t *off, int32_t *steps)
{
    if (!v->on) {                        /* (it does not sound: nothing goes on) */
        *on = 0;
        if (*off != FWD_HELD)
            *off = 0;
        return 1;
    }
    for (;;) {
        if (!*on && (*off == FWD_HELD || (!*off && !v->down)))
            return 1;
        if (*steps <= 0)
            return 0;
        if (*on) {
            *on -= fwd_run(v, *on, steps);
        } else if (v->down) {            /* the note off, once (its key up tells it was) */
            samp_note_off(v);
            --*steps;
        } else {
            *off -= fwd_run(v, *off, steps);
        }
    }
}

/* samp.h: the envelope's gain at its level, or at its target while a stage rises now (no static stage), and never
 * under the last block's, where the next ramps from */
PLAT_COLD int32_t samp_heard(const samp_voice_t *v)
{
    int up = v->rising && !v->statics && (v->stage < 3u || (v->stage == 3u && !v->down));
    int32_t g;
    if (!v->on)
        return 0;
    g = (int32_t)(((int64_t)env_gain(ENV_FULL - (up ? v->target : v->level)) * SAMP_OUT_Q16) >> 16);
    return g > v->amp ? g : v->amp;
}
