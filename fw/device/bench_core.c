/* SPDX-License-Identifier: GPL-3.0-only
 * Copyright (C) 2026 Vasily Kazantsev */
/* The debug build's bench on the FM-1 (specs/debug.md, The bench; core/dbg.h): THE CORE, what the chip itself
 * costs, each measurement run 1 + 8 times with the interrupts off and timed by TIMER4 (24 MHz): loops, adds,
 * multiplies, divides, RAM, memset and memcpy (48 B too, its ends aligned alike and not), the flash's words
 * through the code cache warm and after its lines are dropped, code run from RAM against the same code from the
 * flash; and plat_cache_cold, the cache emptied for the bench's cold runs. Included by main.c in the debug build only, after the HAL. */

extern char _text_start[];              /* (device/app.ld: the image's first byte; fm1_guard.h has _etext) */
static volatile uint32_t bench_sink;

uint32_t plat_ticks(void) { return fm1_ticks(); }

/* the cache's lines of [a, e) dropped: from RAM (fl_inval_xip waits for the cache, which the flash code would use) */
RAMFN static void bench_inval_ram(uint32_t a, uint32_t e) { fl_inval_xip(a, e); }
static void bench_inval(uint32_t a, uint32_t e) { FL_FAR(bench_inval_ram)(a, e); }

void plat_cache_cold(void)              /* the whole image's lines, 32 KB at a time (the interrupts in between) */
{
    uint32_t a = (uint32_t)(uintptr_t)_text_start, e = (uint32_t)(uintptr_t)_etext;
    while (a < e) {
        uint32_t n = e - a > 32768u ? 32768u : e - a, f = irq_save();
        bench_inval(a, a + n);
        irq_restore(f);
        a += n;
    }
}

/* ---- the measurements: each a run over a (256 words of RAM) ---- */
#define BENCH_MAC(a, n)                                                         \
    {                                                                           \
        int64_t acc = 0;                                                        \
        for (uint32_t i = 0; i < (n); i++)                                      \
            acc += (int64_t)(a)[i & 255u] * (a)[(255u - i) & 255u];             \
        return (uint32_t)acc ^ (uint32_t)(acc >> 32);                           \
    }
__attribute__((noinline)) static uint32_t bench_mac_flash(const int32_t *a, uint32_t n) BENCH_MAC(a, n)
RAMFN static uint32_t bench_mac_ram(const int32_t *a, uint32_t n) BENCH_MAC(a, n)

static uint32_t t_loop(int32_t *a)      /* 4096 x an add and a jump back */
{
    uint32_t n = 4096u;
    (void)a;
    do
        RING_PUBLISH();
    while (--n);
    return n;
}
static uint32_t t_add(int32_t *a)       /* 512 x 8 dependent adds and xors, an add and a jump back */
{
    uint32_t x = (uint32_t)a[0], y = (uint32_t)a[1], n = 512u;
    do {
        x += y; y ^= x; x += y; y ^= x;
        x += y; y ^= x; x += y; y ^= x;
    } while (--n);
    return x ^ y;
}
static uint32_t t_mul(int32_t *a)       /* 2048 x a dependent multiply and add */
{
    uint32_t x = (uint32_t)a[0], n = 2048u;
    do
        x = x * 2654435761u + n;
    while (--n);
    return x;
}
static uint32_t t_mul24(int32_t *a)     /* 2048 x the engines' (int64_t)x * y >> 24, y loaded */
{
    int32_t x = a[0] | 1;
    uint32_t n = 2048u;
    do
        x = (int32_t)(((int64_t)x * a[n & 255u]) >> 24) + 1;
    while (--n);
    return (uint32_t)x;
}
static uint32_t t_mac_flash(int32_t *a) { return bench_mac_flash(a, 1024u); }
static uint32_t t_mac_ram(int32_t *a) { return FL_FAR(bench_mac_ram)(a, 1024u); }
static uint32_t t_div(int32_t *a)       /* 1024 x a dependent 32-bit divide */
{
    uint32_t x = (uint32_t)a[0] | 3u, n = 1024u;
    do
        x = 4000000000u / (x | 3u) + n;
    while (--n);
    return x;
}
static uint32_t t_div64(int32_t *a)     /* 1024 x a dependent 64-bit by 32-bit divide (DJ's LFOs': x * 65536 / d) */
{
    int32_t x = a[0] >> 2;              /* (|x| < 2^30, the divisor at least 2^16 either way: the quotient stays */
    uint32_t n = 1024u;                 /* under 2^30; its sign unknown, as the LFOs' are: the CPU's own divide) */
    do {
        int32_t d = a[n & 255u] >> 14;
        d = d < 0 ? d - 65536 : d + 65536;
        x = (int32_t)((int64_t)x * 65536 / d) ^ (int32_t)n;
    } while (--n);
    return (uint32_t)x;
}
static uint32_t t_load(int32_t *a)      /* 1024 words loaded */
{
    uint32_t s = 0;
    for (uint32_t r = 0; r < 4u; r++)
        for (uint32_t i = 0; i < 256u; i++)
            s += (uint32_t)a[i];
    return s;
}
static uint32_t t_store(int32_t *a)     /* 1024 words stored */
{
    for (uint32_t r = 0; r < 4u; r++)
        for (uint32_t i = 0; i < 256u; i++)
            a[i] = (int32_t)(i + r);
    return (uint32_t)a[7];
}
static uint32_t t_memset(int32_t *a) { memset(a, 0, 1024); return (uint32_t)a[9]; }
static uint32_t t_memcpy(int32_t *a) { memcpy(a + 128, a, 512); return (uint32_t)a[200]; }
/* 48 B, as the effects' knobs are copied: the two ends aligned alike (words), and not (a byte at a time) */
static uint32_t t_memcpy48(int32_t *a) { memcpy(a + 128, a, 48); return (uint32_t)a[140]; }
static uint32_t t_memcpy48o(int32_t *a) { memcpy((uint8_t *)(a + 128) + 1, a, 48); return (uint32_t)a[140]; }

/* the flash: 64 KB of constants at the image's end, read through the code cache */
static const uint32_t *bench_flash(void) { return (const uint32_t *)(((uint32_t)(uintptr_t)_etext - 65536u) & ~31u); }
static uint32_t t_fl_seq(int32_t *a)    /* 256 words in a row (1 KB) */
{
    const uint32_t *p = bench_flash();
    uint32_t s = 0;
    (void)a;
    for (uint32_t i = 0; i < 256u; i++)
        s += p[i];
    return s;
}
static uint32_t t_fl_32(int32_t *a)     /* 256 words 32 B apart (8 KB) */
{
    const uint32_t *p = bench_flash();
    uint32_t s = 0;
    (void)a;
    for (uint32_t i = 0; i < 256u; i++)
        s += p[i * 8u];
    return s;
}
static uint32_t t_fl_64(int32_t *a)     /* 256 words 64 B apart (16 KB) */
{
    const uint32_t *p = bench_flash();
    uint32_t s = 0;
    (void)a;
    for (uint32_t i = 0; i < 256u; i++)
        s += p[i * 16u];
    return s;
}
/* USB audio (specs/connections.md#usb-audio): an output frame of the resampler as it is (uac.h's ua_frame) and in its
 * earlier form (the window in a loop, both phases' sums, 4 multiply-accumulates a tap: the same values), 256
 * frames over a made history, the phases and fractions moving; then usb_block's whole 64-frame block (the mix's
 * values, the resampler, the ring) at 44.1 and 48 kHz, warm and with its code's cache lines dropped first, from a
 * stream set up for it and put back as it was (the interrupts are off: a stream running meanwhile never sees it);
 * and the debug build's test tone alone, 64 frames */
__attribute__((noinline)) static uint32_t bench_frame_loop(const int16_t *c, const int16_t *x, int32_t fr)
{
    const int16_t *e = c + UAC_FIR_N;
    int64_t l0 = 0, l1 = 0, r0 = 0, r1 = 0;
    int32_t al, ar;
    do {
        int32_t a = c[0], b = c[UAC_FIR_N], xl = x[0], xr = x[1];
        l0 += (int64_t)a * xl;
        l1 += (int64_t)b * xl;
        r0 += (int64_t)a * xr;
        r1 += (int64_t)b * xr;
        c++;
        x += 2;
    } while (c != e);
    al = (int32_t)l0 + (int32_t)(((int64_t)((int32_t)l1 - (int32_t)l0) * fr) >> 15) + (1 << 14);
    ar = (int32_t)r0 + (int32_t)(((int64_t)((int32_t)r1 - (int32_t)r0) * fr) >> 15) + (1 << 14);
    return (uint32_t)(uint16_t)ua_s16(al >> 15) | (uint32_t)(uint16_t)ua_s16(ar >> 15) << 16;
}
static uint32_t t_usb_frames(int32_t *a, uint32_t (*frame)(const int16_t *, const int16_t *, int32_t))
{
    const int16_t *h = (const int16_t *)a;              /* (the words as 512 values: a history of 64 frames) */
    uint32_t s = 0;
    for (uint32_t f = 0; f < 256u; f++)
        s += frame(UAC_FIR[(f * 37u) & (UAC_FIR_P - 1u)], h + 2u * ((f * 5u) & 31u), (int32_t)((f * 1234u) & 0x7FFFu));
    return s;
}
static uint32_t t_usb_frame(int32_t *a) { return t_usb_frames(a, ua_frame); }
static uint32_t t_usb_frame_loop(int32_t *a) { return t_usb_frames(a, bench_frame_loop); }
static uint32_t t_usb_block(int32_t *a, int r48)
{
    __typeof__(ua) keep;                                /* (the stream as it was: put back before the interrupts) */
    uint32_t w = ua_w, r = ua_r, k;
    uint8_t tone = dbg_usb_tone;
    int32_t lr[128];
    memcpy(&keep, (const void *)&ua, sizeof ua);
    for (k = 0; k < 128u; k++)
        lr[k] = a[k] >> 15;                             /* (+-65536: the mix's values, now and then into the ceiling) */
    ua.go = 1;
    ua.r48 = ua.ring48 = (uint8_t)r48;
    ua.step = r48 ? UA_STEP48 : UA_STEP44;
    ua.w = ua.pos_i = 0;
    ua.pos_f = (uint32_t)a[200];
    ua_r = ua_w;                                        /* (an empty ring: every frame goes in) */
    dbg_usb_tone = 0;
    PLAT_FAR(usb_block)(lr, 0, 64);                     /* (in core 2's room) */
    k = ua_w - ua_r;
    dbg_usb_tone = tone;
    memcpy((void *)&ua, &keep, sizeof ua);
    ua_w = w;
    ua_r = r;
    return k;
}
static uint32_t t_usb_block44(int32_t *a) { return t_usb_block(a, 0); }
static uint32_t t_usb_block48(int32_t *a) { return t_usb_block(a, 1); }
static uint32_t t_usb_tone(int32_t *a)
{
    int16_t b[128];
    (void)a;
    PLAT_FAR(dbg_tone_block)(b, 64);
    return (uint32_t)b[17];
}
static void drop_usb(void)              /* usb_block's code and ua_frame's (2 KB and 1 KB cover them) */
{
    uint32_t b = (uint32_t)(uintptr_t)&usb_block, f = (uint32_t)(uintptr_t)&ua_frame;
    bench_inval(b, b + 2048u);
    bench_inval(f, f + 1024u);
}

static void drop_1k(void) { uint32_t b = (uint32_t)(uintptr_t)bench_flash(); bench_inval(b, b + 1024u); }
static void drop_8k(void) { uint32_t b = (uint32_t)(uintptr_t)bench_flash(); bench_inval(b, b + 8192u); }
static void drop_16k(void) { uint32_t b = (uint32_t)(uintptr_t)bench_flash(); bench_inval(b, b + 16384u); }
static void drop_mac(void)              /* bench_mac_flash's code (it is short: 256 B covers it) */
{
    uint32_t b = (uint32_t)(uintptr_t)&bench_mac_flash;
    bench_inval(b, b + 256u);
}

typedef struct {
    const char *name;
    uint8_t unit;                       /* DBU_* */
    uint32_t ops;
    uint32_t (*run)(int32_t *a);
    void (*before)(void);               /* each run's, the interrupts off already: the cache's lines dropped */
} bench_core_t;
static const bench_core_t BENCH_CORE[] = {   /* (the ops: a loop's turns; MAC's 9 instructions a turn) */
    {"LOOP: ADD, JUMP", DBU_CYCLES, 4096, t_loop, 0},
    {"LOOP: 8 ADD/XOR", DBU_CYCLES, 512, t_add, 0},
    {"MULTIPLY-ADD 32", DBU_CYCLES, 2048, t_mul, 0},
    {"MUL 64 >> 24 + LOAD", DBU_CYCLES, 2048, t_mul24, 0},
    {"MAC 64, 2 LOADS", DBU_CYCLES, 1024, t_mac_flash, 0},
    {"MAC 64, CODE IN RAM", DBU_CYCLES, 1024, t_mac_ram, 0},
    {"MAC 64, CODE COLD", DBU_CYCLES, 1024, t_mac_flash, drop_mac},
    {"DIVIDE 32", DBU_CYCLES, 1024, t_div, 0},
    {"DIVIDE 64 / 32", DBU_CYCLES, 1024, t_div64, 0},
    {"RAM LOAD", DBU_CYCLES, 1024, t_load, 0},
    {"RAM STORE", DBU_CYCLES, 1024, t_store, 0},
    {"MEMSET 1 KB", DBU_BYTES, 1024, t_memset, 0},
    {"MEMCPY 512 B", DBU_BYTES, 512, t_memcpy, 0},
    {"MEMCPY 48 B", DBU_BYTES, 48, t_memcpy48, 0},
    {"MEMCPY 48 B, ODD", DBU_BYTES, 48, t_memcpy48o, 0},
    {"FLASH WORD, WARM", DBU_CYCLES, 256, t_fl_seq, 0},
    {"FLASH WORD, COLD", DBU_CYCLES, 256, t_fl_seq, drop_1k},
    {"FLASH LINE, 32 B", DBU_CYCLES, 256, t_fl_32, drop_8k},
    {"FLASH LINE, 64 B", DBU_CYCLES, 256, t_fl_64, drop_16k},
    {"FLASH 32 B, WARM", DBU_CYCLES, 256, t_fl_32, 0},
    {"USB FRAME", DBU_CYCLES, 256, t_usb_frame, 0},
    {"USB FRAME, IN A LOOP", DBU_CYCLES, 256, t_usb_frame_loop, 0},
    {"USB BLOCK, 44.1 KHZ", DBU_CALL, 1, t_usb_block44, 0},
    {"USB BLOCK, 44.1 COLD", DBU_CALL, 1, t_usb_block44, drop_usb},
    {"USB BLOCK, 48 KHZ", DBU_CALL, 1, t_usb_block48, 0},
    {"USB TONE, 64 FRAMES", DBU_CALL, 1, t_usb_tone, 0},
};
#define BENCH_NCORE (sizeof BENCH_CORE / sizeof BENCH_CORE[0])

int plat_bench_core(int i, dbg_bench_t *r)
{
    int32_t a[256];
    uint32_t seed = 0x2545F491u, k;
    if (i < 0 || (uint32_t)i >= BENCH_NCORE + 1u)
        return 0;
    if (!r)
        return 1;
    if ((uint32_t)i == BENCH_NCORE) {   /* the last: the whole cache emptied, as the bench's cold runs have it */
        str_cpy(r->name, "CACHE EMPTIED, ALL", sizeof r->name);
        r->unit = DBU_CALL;
        r->ops = 1;
        for (k = 0; k < 4u; k++) {
            uint32_t t0 = fm1_ticks(), dt;
            plat_cache_cold();
            dt = fm1_ticks() - t0;
            if (!k)
                r->first = dt;
            else {
                if (k == 1u || dt < r->min) r->min = dt;
                if (dt > r->max) r->max = dt;
                r->sum += dt;
            }
        }
        r->runs = 3;
        return 1;
    }
    const bench_core_t *b = &BENCH_CORE[i];
    str_cpy(r->name, b->name, sizeof r->name);
    r->unit = b->unit;
    r->ops = b->ops;
    for (k = 0; k <= 8u; k++) {
        uint32_t j, f, t0, dt;
        for (j = 0; j < 256u; j++) {    /* (the data afresh: a store or memcpy run changed it) */
            seed = seed * 1664525u + 1013904223u;
            a[j] = (int32_t)seed;
        }
        f = irq_save();
        if (b->before)
            b->before();
        t0 = fm1_ticks();
        bench_sink += b->run(a);
        dt = fm1_ticks() - t0;
        irq_restore(f);
        if (!k)
            r->first = dt;
        else {
            if (k == 1u || dt < r->min) r->min = dt;
            if (dt > r->max) r->max = dt;
            r->sum += dt;
        }
    }
    r->runs = 8;
    return 1;
}
