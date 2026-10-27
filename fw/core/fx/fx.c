/* SPDX-License-Identifier: GPL-3.0-only
 * Copyright (C) 2026 Vasily Kazantsev */
/* The effects: see fx.h; what each does is specs/ui/screens/fx.md's. Fixed point on the mix's 16-bit
 * scale. The REVERB (Dattorro's plate) and the DELAY run at 2/3 of the output's rate, 29.4 kHz, through
 * 2:3 converters (an anti-alias low-pass, then every 1.5th sample; back up by linear steps, the same
 * low-pass after), and the punch-ins' buffer keeps the mix's middle at that rate and its sides at 1/6 of
 * it, so their lines take what specs/files/ram.md gives them.
 *
 * The effects' work may run on either core (specs/second-core.md): it and everything it reaches are marked PLAT_C2,
 * their tables PLAT_C2_RO (core 2's room in RAM on the FM-1, plat.h), with c2_memset / c2_memcpy and plat_c2_us for
 * memset, memcpy and plat_us; core 1's code calls into it through PLAT_FAR. What core 1 alone calls (the commands,
 * the setups, the live settings, the screens' questions) stays unmarked. */
#include <string.h>
#include "fx.h"
#include "../plat.h"
#include "../dbg.h"                      /* the debug build's stages (empty in the release) */
#include "../eng/dx7/dx7.h"              /* dx7_table, dx7_sin_tab: the LFOs */
#include "sound_tables.h"                /* the FX_* tables (fw/tools/gen_fx.py) */

#define B FX_BLOCK
#define SR 44100
#define FX_CHO_N 1024u                   /* CHORUS's lines, a side (14 ms is the most it reads) */
#define FX_DLY_N 29402u                  /* DELAY's line: 1 s at 29.4 kHz */
#define FX_PRE_N 2942u                   /* PRE-DELAY's line: 100 ms at 29.4 kHz */
#define FX_HIST_N 22050u                 /* the punch-in buffer: the last 0.5 s (its places: the output's frames) */
#define FX_MID_N 14700u                  /* its middle, (L + R) / 2: 0.5 s at 29.4 kHz */
#define FX_SIDE_N 3675u                  /* its sides, (L - R) / 2: 0.5 s at 7.35 kHz */
#define FX_HIST_LAG 32u                  /* a replay reads this many frames behind the newest recorded at most (the
                                          * converters' latency: the newest frames are not in the lines yet) */
#define FX_HIST_SPAN (FX_HIST_N - FX_HIST_LAG - FX_BLOCK - 16u)   /* the most a replay reaches back from there
                                          * (recording a block before it is replayed, and the lines' oldest samples) */
#define FX_HIST_CLR 64u                  /* frames of silence in, then the buffer's converters emptied: 0s in, 0s kept */
#define FX_HIST_QUIET (FX_HIST_N + FX_HIST_CLR + FX_BLOCK)   /* frames of silence in, then it holds only 0s */
#define FX_SIDE_AT 278528u               /* the sides are read 4.25 frames on (Q16): their samples' places (a sample
                                          * the middle of its 6:1 sum's 16 frames, 2.5 before the frame it came at)
                                          * and the low-passes' delays (the sides' read 2.9 frames, the middle's two
                                          * 1.15), so both meet in time */
#define FX_KILL 128u                     /* all sound off: the tails fade over this many frames */
#define FX_OUT_FADE 3528u                /* a punch-in let go fades back into the mix over 80 ms */

fx_clock_t fx_clk = {0, 0, 202898u, 21168u, B, 0, 4, 0, 0, 0};
static fx_clock_t fx_wclk = {0, 0, 202898u, 21168u, B, 0, 4, 0, 0, 0};   /* the block's, as its effects work with it
                                                                         * (fx_work) */
volatile uint32_t fx_us[11];
volatile uint32_t fx_strip_us[4];

/* an effect's time since t0 (plat_c2_us): into fx_us (STATUS, the FX screen) and, in the debug build, its stage (the
 * blocks' jobs' own: the effects' work is theirs, on whichever core) */
static inline PLAT_C2 void fx_time(uint32_t e, uint32_t t0)
{
    uint32_t dt = plat_c2_us() - t0;
    fx_us[e] += dt;
#ifdef PTFM_DEBUG
    if (e == FX_REVERB) DBG2_ADD_US(DS_REVERB, dt);
    else if (e == FX_DELAY) DBG2_ADD_US(DS_DELAY, dt);
    else if (e == FX_CHORUS) DBG2_ADD_US(DS_CHORUS, dt);
    else if (e == FX_LIMITER) DBG2_ADD_US(DS_LIMITER, dt);
    else if (e == 10u) DBG2_ADD_US(DS_PUNCH, dt);       /* (fx_us[10]: the punch-ins, their buffer) */
    else if (e <= FX_LOFI) DBG2_ADD_US(DS_INSERTS, dt); /* (GATE, FILTER, DRIVE, LO-FI: the mix's; a deck's the
                                                         * player adds, fx_strip_lr) */
#endif
}

static inline PLAT_C2 int32_t clamp(int32_t v, int32_t lo, int32_t hi) { return v < lo ? lo : v > hi ? hi : v; }
/* Effects asleep (the CHORUS, the DELAY, the REVERB, the punch-ins' buffer): an effect fed nothing for a whole turn
 * of its lines holds nothing more to give, and is skipped while nothing comes in (its clocks going on), which sounds
 * the same: none of them costs time on silence. The CHORUS and the buffer then hold only 0s (no feedback). The DELAY
 * and the REVERB feed back through low-passes rounded toward 0 (mulq15z), which stop a step short of 0: their
 * tails end not in 0s but in a few units that circle for ever under the output's last bit (a DELAY's -1 was heard
 * as a -1 offset of the output, a REVERB's gave 0 out but kept it working). So they sleep once their input was 0
 * and what they gave stayed within FX_REMAINS of 0 for a whole turn of their lines: those remains are cleared then.
 * A run counts samples in a row (it stops counting at FX_ZRUN_MOST) */
#define FX_ZRUN_MOST (1u << 30)
#define FX_REMAINS 2                     /* (16-bit scale: -84 dB) */
static inline PLAT_C2 uint32_t zrun_on(uint32_t run, uint32_t n) { return run >= FX_ZRUN_MOST ? run : run + n; }
static PLAT_C2 int fx_zero(const int32_t *x)    /* a block of 0s (it stops at the first that is not) */
{
    uint32_t i;
    for (i = 0; i < FX_BLOCK; i++)
        if (x[i])
            return 0;
    return 1;
}
static PLAT_C2 int fx_remains(const int32_t *a, const int32_t *b, uint32_t n)   /* every one within FX_REMAINS of 0 */
{
    uint32_t m;
    for (m = 0; m < n; m++)
        if (a[m] > FX_REMAINS || a[m] < -FX_REMAINS || b[m] > FX_REMAINS || b[m] < -FX_REMAINS)
            return 0;
    return 1;
}
static inline PLAT_C2 int16_t sat16(int32_t v) { return (int16_t)(v < -32768 ? -32768 : v > 32767 ? 32767 : v); }
static inline PLAT_C2 int32_t mulq15(int32_t a, int32_t b) { return (int32_t)(((int64_t)a * b) >> 15); }
/* rounded toward 0, for the feedback paths: a tail dies away to 0 (with >>, -1 times a gain under 1 stays -1).
 * The product's sign is a ^ b's (a product of 0 gets 32767, which >> 15 leaves 0): one multiply, not two */
static inline PLAT_C2 int32_t mulq15z(int32_t a, int32_t b)
{
    return (int32_t)(((int64_t)a * b + (((a ^ b) >> 31) & 32767)) >> 15);
}
/* x / 3 as C's (toward 0), by a multiply (the FM-1's divider takes ~23 cycles): Hacker's Delight 10-5, exact for
 * every int32 */
static inline PLAT_C2 int32_t div3(int32_t x) { return (int32_t)(((int64_t)x * 0x55555556) >> 32) - (x >> 31); }
/* (the converters and the plate, in their loops; marked as their callers are: the device's compiler inlines nothing
 * across sections but what must be, so a helper of another section inlined first leaves its own calls for later, and
 * the loops come out other than they were) */
#define FX_INLINE static inline __attribute__((always_inline)) PLAT_C2
/* the LFOs' sine: the DX7's table in RAM (dx7_table(0)), taken at fx_init and read inline (dx7_sin_tab), so the
 * effects' code never calls into the engine (core 2's room) */
static const int32_t *fx_sin;
FX_INLINE int32_t sin15_t(const int32_t *st, uint32_t ph)   /* Q15, st fx_sin */
{
    return clamp(dx7_sin_tab(ph >> 8, st) >> 9, -32767, 32767);
}

/* white noise: the mix's LO-FI DUST and the METRONOME's CLICK draw from rnd_state, DJ's decks' DUST from a noise of
 * their own (their effects run apart from the mix's: fx_strip_lr) */
static uint32_t rnd_state = 0x2545F491u, strip_rnd_state = 0x2545F491u;
static inline PLAT_C2 uint32_t rnd_at(uint32_t *st)   /* (st: a noise's state, its next value) */
{
    uint32_t s = *st;
    s ^= s << 13;
    s ^= s >> 17;
    s ^= s << 5;
    *st = s;
    return s;
}

/* ---- the setup, as the song has it and its commands change it (specs/files/songs.md#the-fx-setup) ---- */
static const uint8_t DEFAULT_SETUP[64] = {
    0,                                   /* ROUTING: GLOBAL */
    0x10, 0xA3, 0x54, 0x26, 0xF7, 0xFF,  /* GATE FILTER LO-FI SENDS CHORUS DELAY REVERB DRIVE LIMITER */
    0x80, 0x00,                          /* only LIMITER on */
    0, 0, 4, 1, 60, 0, 0,                /* METRONOME: FOLLOW, 4 beats, BEEP, 60 % */
    0, 1, 100, 20,                       /* 0x90 GATE: PATTERN 00, 1/16, DEPTH, SMOOTH */
    64, 30,                              /* 0x94 FILTER: OFF, RESO */
    30, 50,                              /* 0x96 DRIVE: DRIVE, TONE */
    6, 21, 20,                           /* 0x98 LO-FI: 8 bits, 22 kHz, DUST */
    0, 30, 50, 50,                       /* 0x9B CHORUS: I, RATE, DEPTH, MIX */
    7, 40, 60, 0, 30,                    /* 0x9F DELAY: 1/8., FEEDBACK, TONE, PING-PONG off, MIX */
    60, 40, 10, 25,                      /* 0xA4 REVERB: SIZE, DAMP, PRE-DELAY, MIX */
    11, 9,                               /* 0xA8 LIMITER: -1 dB, 100 ms */
    0, 60, 13,                           /* 0xAA DUCK: TRACK 00, DEPTH, 150 ms */
    0, 0, 0,                             /* (0xAD free, 0xAE-0xAF no knob) */
    0, 0, 0, 75, 0, 75};                 /* 0xB0 FILTER's envelope: ENV 0 %, KEYS, ATTACK 2.0 ms, DECAY 296 ms,
                                          * SUSTAIN 0 %, RELEASE 296 ms */

/* the live settings (fx.h): what the commands, the setups and the screens change */
static uint8_t fx_route;                 /* 0 GLOBAL, 1 SENDS */
static uint8_t fx_nchain;
static uint16_t fx_on;                   /* bit n: effect n on */
/* (the arrays aligned as fx_set_t's, so fx_set_get copies them a word at a time) */
static uint8_t fx_chain[12] __attribute__((aligned(4)));
static uint8_t fx_kn[48] __attribute__((aligned(4)));   /* the knobs now: code c at fx_kn[c - 0x90], each its step */
static uint8_t fx_met[5] __attribute__((aligned(4)));   /* METRONOME: TEMPO (2), BEATS A BAR, SOUND, LEVEL */
/* the working copy: the settings the effects' work reads, the block's (fx_work); K(c) its knob c */
static fx_set_t fx_w;
#define K(c) fx_w.kn[(c) - 0x90]
static uint32_t fx_act;                  /* what worked in the last block */
/* the settings' generation (fx_set_t.seq): every change of them a new one, never 0, so a copy holding the same one
 * holds the same settings and fx_set_get / fx_work skip it (a half's blocks mostly have them all alike) */
static uint32_t fx_set_seq = 1u;
static void set_changed(void)
{
    if (++fx_set_seq == 0u)
        fx_set_seq = 1u;
}

/* the changes to the effects' own state kept with their block (fx_defer: fx.h, a half's blocks at once), in the
 * order made: the blocks' in turn, block j's from ev_at[j] to ev_end[j]. One writer: core 1 adds them as it makes the
 * blocks (fx_ev); a block's job only reads its own (fx_events), and they stand till the next half's first block, when
 * core 2 has finished every job of the half (specs/second-core.md#who-owns-what). 64 of them, far more than a half
 * makes: one made at once past them would land after the blocks core 2 took, the one way two cores could differ */
#define FX_EVQ 64u
static struct {
    uint8_t kind;
    int8_t a, b, c;
    uint32_t bf;                         /* (fx_clk's beat then: a punch-in's lengths are of it) */
} ev_q[FX_EVQ];
static uint8_t ev_n;                     /* kept in this half */
static uint8_t ev_at[FX_HALF_MOST], ev_end[FX_HALF_MOST];   /* each block's */
static uint8_t ev_lost;                  /* (one made at once, the queue full: fx_blocks_ok says no in that half) */
static int ev_block = -1;                /* the block whose changes are kept now (-1: none, made at once) */
static void fx_ev(uint32_t kind, int a, int b, int c);

/* ---- the 2:3 converters ---- */
typedef struct { int32_t x1, x2, y1, y2; } bq_t;
FX_INLINE int32_t bq(bq_t *s, int32_t x)   /* the anti-alias low-pass, 12.5 kHz at 44.1 kHz (Q14) */
{
#if FX_AA_B1 == 2 * FX_AA_B0 && FX_AA_B2 == FX_AA_B0   /* (a low-pass's: B0 (x + 2 x1 + x2), the sum in 32 bits) */
    int32_t y = (int32_t)(((int64_t)FX_AA_B0 * (x + 2 * s->x1 + s->x2) - (int64_t)FX_AA_A1 * s->y1 -
                           (int64_t)FX_AA_A2 * s->y2) >> 14);
#else
    int32_t y = (int32_t)(((int64_t)FX_AA_B0 * x + (int64_t)FX_AA_B1 * s->x1 + (int64_t)FX_AA_B2 * s->x2 -
                           (int64_t)FX_AA_A1 * s->y1 - (int64_t)FX_AA_A2 * s->y2) >> 14);
#endif
    s->x2 = s->x1;
    s->x1 = x;
    s->y2 = s->y1;
    s->y1 = y;
    return y;
}
FX_INLINE int32_t bq_sd(bq_t *s, int32_t x)   /* the punch-in buffer's sides read back: a low-pass at 3.5 kHz (Q14) */
{
    int32_t y = (int32_t)(((int64_t)FX_SD_B0 * (x + 2 * s->x1 + s->x2) - (int64_t)FX_SD_A1 * s->y1 -
                           (int64_t)FX_SD_A2 * s->y2) >> 14);
    s->x2 = s->x1;
    s->x1 = x;
    s->y2 = s->y1;
    s->y1 = y;
    return y;
}

typedef struct {                         /* 44.1 -> 29.4 kHz: two samples of every three */
    bq_t lp;
    uint8_t ph;
    int32_t held;
} down_t;
FX_INLINE int down_push(down_t *d, int32_t x, int32_t *out)   /* 1 when a sample comes out */
{
    int32_t y = bq(&d->lp, x);
    uint8_t ph = d->ph;
    d->ph = (uint8_t)(ph == 2u ? 0u : ph + 1u);
    if (ph == 0u) {
        *out = y;
        return 1;
    }
    if (ph == 1u) {
        d->held = y;
        return 0;
    }
    *out = (d->held + y) >> 1;           /* (the sample half way between) */
    return 1;
}

/* a block through the down converter (x: its 44.1 kHz frames): the 29.4 kHz samples into p (their count back),
 * made[i] whether frame i made one, *any the samples or'ed; the same values as down_push a frame at a time, its
 * phases 3 frames a turn and its state in locals (a frame at a time the device's compiler tested and kept them
 * in memory: ~60 instructions a frame) */
FX_INLINE void down_one(bq_t *lp, uint32_t ph, int32_t x, int32_t *held, int32_t *p, uint32_t *n, uint8_t *made)
{
    int32_t y = bq(lp, x);
    if (ph == 0u) {
        p[(*n)++] = y;
        *made = 1;
    } else if (ph == 1u) {
        *held = y;
        *made = 0;
    } else {
        p[(*n)++] = (*held + y) >> 1;
        *made = 1;
    }
}
static PLAT_C2 uint32_t down_block(down_t *d, const int32_t *x, int32_t *p, uint8_t *made, int32_t *any)
{
    bq_t lp = d->lp;
    uint32_t ph = d->ph, i = 0, n = 0;
    int32_t held = d->held, a = 0;
    for (; i < B && ph != 0u; i++, ph = ph == 2u ? 0u : ph + 1u)   /* (to the next ph 0) */
        down_one(&lp, ph, x[i], &held, p, &n, &made[i]);
    for (; i + 3u <= B; i += 3u) {       /* (then 3 frames a turn: a sample, one held, the two's middle) */
        int32_t y;
        p[n++] = bq(&lp, x[i]);
        held = bq(&lp, x[i + 1u]);
        y = bq(&lp, x[i + 2u]);
        p[n++] = (held + y) >> 1;
        made[i] = made[i + 2u] = 1;
        made[i + 1u] = 0;
    }
    for (; i < B; i++, ph = ph == 2u ? 0u : ph + 1u)   /* (the block's last frames) */
        down_one(&lp, ph, x[i], &held, p, &n, &made[i]);
    for (i = 0; i < n; i++)
        a |= p[i];
    d->lp = lp;
    d->ph = (uint8_t)ph;
    d->held = held;
    *any = a;
    return n;
}

typedef struct {                         /* 29.4 -> 44.1 kHz: the samples in hand, three frames from two */
    bq_t lp;
    int32_t f[4];
    uint8_t n, q, go;
} up_t;
FX_INLINE void up_put(up_t *u, int32_t x)
{
    if (u->n < 4u)
        u->f[u->n++] = x;
}
FX_INLINE int32_t up_get(up_t *u)
{
    int32_t y;
    if (!u->go) {                        /* (a little kept in hand: never runs dry) */
        if (u->n < 3u)
            return bq(&u->lp, 0);
        u->go = 1;
    }
    if (u->n < 3u)                       /* (never, once going) */
        return bq(&u->lp, u->n ? u->f[0] : 0);
    if (u->q == 0u)
        y = u->f[0];
    else if (u->q == 1u)
        y = u->f[0] + div3((u->f[1] - u->f[0]) * 2);
    else
        y = u->f[1] + div3(u->f[2] - u->f[1]);
    if (++u->q == 3u) {
        u->q = 0;
        u->f[0] = u->f[2];
        u->f[1] = u->f[3];
        u->n = (uint8_t)(u->n - 2u);
    }
    return bq(&u->lp, y);
}

/* A block through both up converters (made[i]: frame i brings the next of pl / pr; y: the 44.1 kHz frames out),
 * the same values as up_put / up_get a frame at a time. Running steadily it is a fixed formula over the samples
 * in a row: down_push makes two samples of every three frames, so made[] repeats every 3 frames as q does, and
 * once going (n >= 3 at every get, never a sample dropped at n = 4), 3 frames bring n back where it was; the
 * block's first 3 frames counted say whether that holds for all of it (else, and while starting, a frame at a
 * time). Then each side alone: its samples in hand and the block's in one row, its low-pass's state in locals
 * (a frame at a time the device's compiler read and wrote every state at every sample: ~245 instructions a frame
 * for both sides) */
FX_INLINE int32_t up_at(const int32_t *f, uint32_t q)   /* up_get's value at q, f its samples in hand */
{
    return q == 0u ? f[0] : q == 1u ? f[0] + div3((f[1] - f[0]) * 2) : f[1] + div3(f[2] - f[1]);
}
static PLAT_C2 void up_side(up_t *u, const int32_t *in, uint32_t cnt, int32_t *y)
{
    int32_t s[4 + B];
    bq_t lp = u->lp;                     /* (a local: its fields stay in registers) */
    uint32_t n = u->n, q = u->q, k = 0, i = 0, j;
    for (j = 0; j < n; j++)
        s[j] = u->f[j];
    for (j = 0; j < cnt; j++)
        s[n + j] = in[j];
    for (; i < B && q != 0u; i++) {      /* (to the next q 0, a frame at a time) */
        y[i] = bq(&lp, up_at(s + k, q));
        if (++q == 3u) {
            q = 0;
            k += 2u;
        }
    }
    for (; i + 3u <= B; i += 3u, k += 2u) {   /* (then 3 frames a turn: q 0, 1, 2, two samples on) */
        const int32_t *f = s + k;
        y[i] = bq(&lp, f[0]);
        y[i + 1u] = bq(&lp, f[0] + div3((f[1] - f[0]) * 2));
        y[i + 2u] = bq(&lp, f[1] + div3(f[2] - f[1]));
    }
    for (; i < B; i++) {                 /* (the block's last frames) */
        y[i] = bq(&lp, up_at(s + k, q));
        if (++q == 3u) {
            q = 0;
            k += 2u;
        }
    }
    n += cnt;
    for (j = 0; k + j < n; j++)          /* (what is left in hand; past n the old ones stay, never read) */
        u->f[j] = s[k + j];
    u->n = (uint8_t)(n - k);
    u->q = (uint8_t)q;
    u->lp = lp;
}
static PLAT_C2 void up_block(up_t *a, up_t *b, const uint8_t *made, const int32_t *pl, const int32_t *pr,
                             uint32_t cnt, int32_t *yl, int32_t *yr)
{
    uint32_t n = a->n, q = a->q, i, m, ok = a->go && b->go && n == b->n && q == b->q;
    for (i = 0; ok && i < 3u; i++) {     /* (the first 3 frames' counts: steady for the block?) */
        if (made[i] && n++ >= 4u)
            ok = 0;
        else if (n < 3u)
            ok = 0;
        else if (++q == 3u) {
            q = 0;
            n -= 2u;
        }
    }
    if (ok) {
        up_side(a, pl, cnt, yl);
        up_side(b, pr, cnt, yr);
        return;
    }
    for (i = 0, m = 0; i < B; i++) {
        if (made[i]) {
            up_put(a, pl[m]);
            up_put(b, pr[m]);
            m++;
        }
        yl[i] = up_get(a);
        yr[i] = up_get(b);
    }
}

/* The converters at rest: a 29.4 kHz effect's three (down, up left, up right) holding only 0s, so a block of 0s
 * through them gives 0s and leaves them as they were but for where they are. An effect asleep on silence takes
 * that way (conv_rest): only their places move, and the same sound comes out (0s) at no cost */
static PLAT_C2 int conv_still(const down_t *d, const up_t *a, const up_t *b)
{
    int32_t z = d->lp.x1 | d->lp.x2 | d->lp.y1 | d->lp.y2 | d->held;
    uint32_t i;
    z |= a->lp.x1 | a->lp.x2 | a->lp.y1 | a->lp.y2 | b->lp.x1 | b->lp.x2 | b->lp.y1 | b->lp.y2;
    for (i = 0; i < a->n; i++)           /* (the samples in hand: the rest are written before they are read) */
        z |= a->f[i];
    for (i = 0; i < b->n; i++)
        z |= b->f[i];
    return z == 0;
}
static PLAT_C2 void up_skip(up_t *u)     /* up_get's moves, its output (0) not made */
{
    if (!u->go) {
        if (u->n < 3u)
            return;
        u->go = 1;
    }
    if (u->n < 3u)
        return;
    if (++u->q == 3u) {
        u->q = 0;
        u->f[0] = u->f[2];
        u->f[1] = u->f[3];
        u->n = (uint8_t)(u->n - 2u);
    }
}
static PLAT_C2 uint32_t conv_frames(down_t *d, up_t *a, up_t *b, uint32_t k)   /* k frames of 0s: the samples made */
{
    uint32_t i, n = 0;
    for (i = 0; i < k; i++) {
        uint8_t ph = d->ph;
        d->ph = (uint8_t)(ph == 2u ? 0u : ph + 1u);
        if (ph != 1u) {                  /* (down_push made one: 0 through the line, 0 back) */
            up_put(a, 0);
            up_put(b, 0);
            n++;
        }
        up_skip(a);
        up_skip(b);
    }
    return n;
}
/* a block of 0s through them: the samples made at 29.4 kHz. Their places go round with the 2:3: once 3 frames bring
 * them back where they were (running steadily, they do), every 3 after do too, so after those 3 only the block's
 * odd frame is run (64 = 21 x 3 + 1); still settling, every frame is. The samples in hand are 0s, or old ones never
 * read before they are written: set to 0, so the places are all that changes (a frame at a time cost the FM-1
 * ~25 us a block an effect) */
static PLAT_C2 uint32_t conv_rest(down_t *d, up_t *a, up_t *b)
{
    uint8_t ph = d->ph, an = a->n, aq = a->q, ago = a->go, bn = b->n, bq0 = b->q, bgo = b->go;
    uint32_t n;
    c2_memset(a->f, 0, sizeof a->f);
    c2_memset(b->f, 0, sizeof b->f);
    n = conv_frames(d, a, b, 3u);
    if (d->ph == ph && a->n == an && a->q == aq && a->go == ago && b->n == bn && b->q == bq0 && b->go == bgo)
        return n * (B / 3u) + conv_frames(d, a, b, B % 3u);
    return n + conv_frames(d, a, b, B - 3u);
}

/* GATE, FILTER, DRIVE and LO-FI work on the states they are given (fx_ins_t: the mix's, ins; a DJ deck's, its
 * strip's) with the knobs they are given (kn: code c at kn[c - 0x90], KN(c); the mix's the working copy, a deck's its
 * own) and the music clock they are given (the block's; a deck's own) */
#define KN(c) kn[(c) - 0x90]
static fx_ins_t ins;                     /* the mix's (fx_reset sets them) */

/* ---- GATE ---- */
static const uint8_t GATE_SPB[5] PLAT_C2_RO = {2, 4, 8, 3, 6};   /* steps a beat: 1/8, 1/16, 1/32, 1/8T, 1/16T */

/* the GATE patterns: 16 built in, 16 of the user's (the user's own may be changed: fx_gate_pattern) */
static uint8_t gate_pat[32][16];

static PLAT_C2 void fx_gate(fx_ins_t *st, const uint8_t *kn, const fx_clock_t *clk, int32_t *l, int32_t *r, int on)
{
    const uint8_t *pat = gate_pat[KN(0x90) & 31u];
    uint32_t spb = GATE_SPB[KN(0x91) < 5u ? KN(0x91) : 1u], i;
    uint64_t pos = ((uint64_t)(clk->beat % 48u) << 32 | clk->frac) * spb;   /* steps, Q32 (48 beats: whole patterns) */
    uint32_t step = (uint32_t)(pos >> 32) & 15u, f = (uint32_t)pos, inc = clk->inc * spb, hold = clk->hold;
    int32_t closed = (1 << 24) - (int32_t)((KN(0x92) > 100u ? 100u : KN(0x92)) * 167772u);
    int32_t k = FX_GATE_K[KN(0x93) > 100u ? 100u : KN(0x93)], g = st->gate_g;
    for (i = 0; i < B; i++) {
        uint32_t v = pat[step], nf = f + inc;
        int32_t to;
        if (!on)
            to = 1 << 24;
        else if (v == 0u)
            to = closed;
        else if (v == 1u)
            to = f < 0xC0000000u ? 1 << 24 : closed;   /* (open for the step's first 3/4) */
        else
            to = f * v < 0x8CCCCCCDu ? 1 << 24 : closed;   /* (each hit open for its first 55 %) */
        g += (int32_t)(((int64_t)(to - g) * k) >> 15);
        if (g - to < 1024 && to - g < 1024)
            g = to;
        l[i] = (int32_t)(((int64_t)l[i] * g) >> 24);
        r[i] = (int32_t)(((int64_t)r[i] * g) >> 24);
        if (i + 1u >= hold)
            continue;                    /* (the clock waits for the row) */
        if (nf < f)
            step = (step + 1u) & 15u;
        f = nf;
    }
    st->gate_g = g;
    st->gate_step = (uint8_t)step;
}

/* ---- a TPT state variable filter (A. Simper's): g from FX_G_Q12, k = 1 / q, Q12 ---- */
typedef struct { int32_t a1, a2, a3, k; } svf_c_t;
typedef struct { int32_t ic1, ic2; } svf_s_t;
static PLAT_C2 void svf_coef(svf_c_t *c, int32_t g, int32_t k)
{
    int32_t den = 4096 + (int32_t)(((int64_t)g * (g + k)) >> 12);
    c->a1 = (int32_t)((4096u << 13) / (uint32_t)den);
    c->a2 = (int32_t)(((int64_t)c->a1 * g) >> 12);
    c->a3 = (int32_t)(((int64_t)c->a2 * g) >> 12);
    c->k = k;
}
static inline PLAT_C2 int32_t svf_run(const svf_c_t *c, svf_s_t *s, int32_t x, int hp)
{
    int32_t v3 = x - s->ic2;
    int32_t v1 = (int32_t)(((int64_t)c->a1 * s->ic1 + (int64_t)c->a2 * v3) >> 13);
    int32_t v2 = s->ic2 + (int32_t)(((int64_t)c->a2 * s->ic1 + (int64_t)c->a3 * v3) >> 13);
    s->ic1 = clamp(2 * v1 - s->ic1, -400000, 400000);
    s->ic2 = clamp(2 * v2 - s->ic2, -400000, 400000);
    return hp ? x - (int32_t)(((int64_t)c->k * v1) >> 12) - v2 : v2;
}
/* a block of one channel through it: svf_run's arithmetic with its states in locals (svf_run's in memory cost a
 * load and a store each a frame, and a stereo loop tested its other channel and hp each frame) */
static inline PLAT_C2 int32_t svf_cl(int32_t v, int32_t lim)   /* clamp's value as a max then a min: no jumps */
{
    v = v < -lim ? -lim : v;
    return v > lim ? lim : v;
}
static inline __attribute__((always_inline)) PLAT_C2 void svf_block(const svf_c_t *c, svf_s_t *s, int32_t *x,
                                                                    const int hp)
{
    int32_t ic1 = s->ic1, ic2 = s->ic2;
    uint32_t i;
    for (i = 0; i < B; i++) {
        int32_t in = svf_cl(x[i], 200000), v3 = in - ic2, v1, v2;
        v1 = (int32_t)(((int64_t)c->a1 * ic1 + (int64_t)c->a2 * v3) >> 13);
        v2 = ic2 + (int32_t)(((int64_t)c->a2 * ic1 + (int64_t)c->a3 * v3) >> 13);
        ic1 = svf_cl(2 * v1 - ic1, 400000);
        ic2 = svf_cl(2 * v2 - ic2, 400000);
        x[i] = hp ? in - (int32_t)(((int64_t)c->k * v1) >> 12) - v2 : v2;
    }
    s->ic1 = ic1;
    s->ic2 = ic2;
}
/* the g of a log index 0-255 with a fraction (Q8), interpolated */
static PLAT_C2 int32_t g_at(int32_t ix8)
{
    int32_t i = clamp(ix8 >> 8, 0, 254), f = ix8 & 255;
    if (ix8 >= 255 << 8)
        return FX_G_Q12[255];
    return FX_G_Q12[i] + (((FX_G_Q12[i + 1] - FX_G_Q12[i]) * f) >> 8);
}

/* ---- FILTER: low-pass to the left, high-pass to the right, OFF in the middle; its cutoff glides ----
 * its envelope (specs/ui/screens/fx.md#filters-envelope): ENV 0xB0, SOURCE 0xB1 (0 the keys, 1-16 a track, 17-21 the
 * beat every 4, 2, 1, 1/2, 1/4 beats), ATTACK 0xB2, DECAY 0xB3, SUSTAIN 0xB4, RELEASE 0xB5; its picture (open: the
 * share of the way to fully open, Q16) */
uint32_t fx_filter_open(void) { return ins.filt_env.open; }
uint32_t fx_filter_hits(void) { return ins.filt_env.hits; }
uint32_t fx_env_t10(int decay, int step) { return FX_ENV_T10[decay ? 1 : 0][step < 0 ? 0 : step > 127 ? 127 : step]; }

/* the envelope a block on (its source's hit and hold read from the clock), and the cutoff it sets: ix8, the glided
 * CUT, moved its share of the way toward open8 (fully open: LP's top, HP's bottom; a log index, so in octaves); its
 * level the block's mean, from where it began to where it ends */
static PLAT_C2 int32_t fenv_block(fx_fenv_t *e, const uint8_t *kn, const fx_clock_t *clk, int32_t ix8, int32_t open8)
{
    uint32_t src = KN(0xB1), env = KN(0xB0) > 100u ? 100u : KN(0xB0), fire, held, v0 = e->v, amt;
    if (src <= 16u) {
        fire = clk->fire >> src & 1u;
        held = clk->held >> src & 1u;
    } else {                             /* the beat: a hit where the clock crosses each 2^sh (beats Q32) */
        uint32_t sh = 34u - (src < 21u ? src - 17u : 4u), n = clk->hold < B ? clk->hold : B;
        uint64_t p0 = (uint64_t)clk->beat << 32 | clk->frac, p1, from;
        /* (the block's span: where its frames stand, the clock held at a row's start as GATE holds it) */
        p1 = n < B ? p0 + (uint64_t)(n ? n - 1u : 0u) * clk->inc + 1u : p0 + (uint64_t)B * clk->inc;
        from = p0 < e->pos || p0 - e->pos > 1ull << 30 ? p0 : e->pos;   /* (a jump: from where it landed) */
        fire = ((from + (1ull << sh) - 1u) >> sh << sh) < p1;
        held = fire || (p0 & ((1ull << sh) - 1u)) < 1ull << (sh - 1u);   /* (half its time) */
        e->pos = p1;
    }
    if (fire) {
        e->stage = 1;
        e->hits++;
    } else if (!held && (e->stage == 1u || e->stage == 2u)) {
        e->stage = 3;
    }
    if (e->stage == 1u) {                /* ATTACK: the whole way in its time, at one pace */
        uint32_t a = FX_ENV_AINC[KN(0xB2) > 127u ? 127u : KN(0xB2)];
        if (e->v >= (1u << 24) - a) {
            e->v = 1u << 24;
            e->stage = 2;
        } else {
            e->v += a;
        }
    } else if (e->stage == 2u) {         /* DECAY toward SUSTAIN, then held there */
        int32_t s = (int32_t)(((KN(0xB4) > 100u ? 100u : KN(0xB4)) << 24) / 100u);
        e->v = (uint32_t)(s + (int32_t)(((int64_t)((int32_t)e->v - s) * FX_ENV_K[KN(0xB3) > 127u ? 127u : KN(0xB3)]) >> 24));
    } else if (e->stage == 3u) {         /* RELEASE back to CUT */
        e->v = (uint32_t)(((uint64_t)e->v * FX_ENV_K[KN(0xB5) > 127u ? 127u : KN(0xB5)]) >> 24);
        if (!e->v)
            e->stage = 0;
    }
    amt = ((v0 >> 1) + (e->v >> 1)) * env / 100u;   /* (Q24) */
    e->open = amt >> 8;
    if (!amt)
        return ix8;
    return ix8 + (int32_t)(((int64_t)(open8 - ix8) * amt) >> 24);
}

static PLAT_C2 int fx_filter(fx_ins_t *st, const uint8_t *kn, const fx_clock_t *clk, int32_t *l, int32_t *r, int on)
{                                        /* (1: it worked on the block) */
    int32_t step = KN(0x94) > 127u ? 127 : KN(0x94), c = on ? step - 64 : 0, to, ix;
    svf_c_t cs, *co = &cs;
    svf_s_t *fs = (svf_s_t *)(void *)st->filt_s;
    fx_fenv_t *e = &st->filt_env;
    if (c < 0 && st->filt_mode >= 0) {   /* (switching side: from open) */
        st->filt_mode = -1;
        st->filt_ix8 = 255 << 8;
        c2_memset(st->filt_s, 0, sizeof st->filt_s);
    } else if (c > 0 && st->filt_mode <= 0) {
        st->filt_mode = 1;
        st->filt_ix8 = 0;
        c2_memset(st->filt_s, 0, sizeof st->filt_s);
    }
    if (!st->filt_mode)
        return 0;
    to = c ? FX_FILT_IX[step] << 8 : st->filt_mode < 0 ? 255 << 8 : 0;
    st->filt_ix8 += clamp(to - st->filt_ix8, -2048, 2048);   /* (a glide: no zipper) */
    if (!c && st->filt_ix8 == to) {
        st->filt_mode = 0;               /* fully open again: off */
        e->stage = 0, e->v = e->open = 0;
        return 0;
    }
    ix = st->filt_ix8;
    if (c && KN(0xB0))                   /* (ENV 0 %, or CUT gliding open to OFF: the envelope rests) */
        ix = fenv_block(e, kn, clk, st->filt_ix8, st->filt_mode < 0 ? 255 << 8 : 0);
    else if (e->stage | e->v | e->open)
        e->stage = 0, e->v = e->open = 0;
    svf_coef(co, g_at(ix), FX_RESO_K[KN(0x95) > 100u ? 100u : KN(0x95)]);
    if (st->filt_mode > 0) {
        svf_block(co, &fs[0], l, 1);
        svf_block(co, &fs[1], r, 1);
    } else {
        svf_block(co, &fs[0], l, 0);
        svf_block(co, &fs[1], r, 0);
    }
    return 1;
}

/* ---- DRIVE (on the mix): a ~55 Hz low cut, the drive (1x at 0 % to ~9x) into a soft clip with a
 * little bias for the even harmonics, a make-up gain falling with the drive, then TONE's low-pass ---- */
static inline PLAT_C2 int32_t softclip(int32_t x)   /* tanh(x / 32768) x 32767, x up to 2 */
{
    int32_t a = x < 0 ? -x : x, i = a >> 8, y;
    if (i >= 256)
        y = FX_TANH_Q15[256];
    else
        y = FX_TANH_Q15[i] + (((FX_TANH_Q15[i + 1] - FX_TANH_Q15[i]) * (a & 255)) >> 8);
    return x < 0 ? -y : y;
}
static PLAT_C2 void fx_drive(fx_ins_t *st, const uint8_t *kn, int32_t *l, int32_t *r)
{
    int32_t d = (KN(0x96) > 100u ? 100 : KN(0x96)) * 127 / 100, g = 4096 + d * d * 2;
    int32_t k = 1638 + (KN(0x97) > 100u ? 100 : KN(0x97)) * 29491 / 100, mk = 30000 - d * 120, bias = 2400;
    int32_t b0 = softclip(bias), c;
    volatile int32_t gv = g;             /* (read in the loop: hoisted, the device's compiler made the product
                                          * 64 x 64, two multiplies) */
    uint32_t i;
    for (c = 0; c < 2; c++) {            /* (the filters' states with 8 bits more: no dead band, no DC left) */
        int32_t *x = c ? r : l, hp = st->dr_hp[c], lp = st->dr_lp[c];
        for (i = 0; i < B; i++) {
            int32_t v = clamp(x[i], -4194304, 4194304), y;   /* (2^22: the state's 8 bits more fit) */
            hp += ((v << 8) - hp) >> 7;  /* ~55 Hz low cut: the bass stays out of the clipper */
            v = clamp(v - (hp >> 8), -230000, 230000);
            y = mulq15(softclip(clamp((int32_t)(((int64_t)(v >> 2) * gv) >> 10) + bias, -65536, 65536)) - b0, mk);
            lp += (int32_t)(((int64_t)((y << 8) - lp) * k) >> 15);
            x[i] = lp >> 8;
        }
        st->dr_hp[c] = hp;
        st->dr_lp[c] = lp;
    }
}

/* ---- LO-FI: BITS (2-16), RATE (1-44 kHz, held samples), DUST (specks: from the noise rs) ---- */
static PLAT_C2 void fx_lofi(fx_ins_t *st, const uint8_t *kn, uint32_t *rs, int32_t *l, int32_t *r)
{
    int32_t bits = (KN(0x98) > 14u ? 14 : KN(0x98)) + 2, rate = (KN(0x99) > 43u ? 43 : KN(0x99)) + 1;
    int32_t hold = (SR + rate * 500) / (rate * 1000), dust = KN(0x9A) > 100u ? 100 : KN(0x9A), sh = 16 - bits;
    uint32_t i, p = (uint32_t)dust * 64424u;   /* (a speck's chance a frame: DUST x 0.15 %, of 2^32) */
    uint32_t hl = hold < 1 ? 1u : (uint32_t)hold, ph = (uint32_t)st->lofi_n % hl;   /* (lofi_n % hold a frame, counted
                                                                                     * on from the block's: no divide a
                                                                                     * frame) */
    uint32_t ns = *rs;                   /* (the noise in a local: a register in the loop) */
    int32_t h[2];
    h[0] = st->lofi_h[0];
    h[1] = st->lofi_h[1];
    for (i = 0; i < B; i++) {
        int32_t n = 0, on = ph == 0u;
        ph = ph + 1u == hl ? 0u : ph + 1u;
        if (on) {
            int32_t c;
            for (c = 0; c < 2; c++) {
                int32_t v = c ? r[i] : l[i];
                if (sh > 0) {            /* (rounded to the nearest step) */
                    int32_t s = 1 << sh;
                    v = (v >= 0 ? (v + s / 2) >> sh : -((-v + s / 2) >> sh)) << sh;
                }
                h[c] = v;
            }
        }
        if (dust && rnd_at(&ns) < p)     /* (+-1/4 of full scale at 100 %) */
            n = (((int32_t)(rnd_at(&ns) >> 16) - 32768) >> 2) * dust / 100;
        l[i] = h[0] + n;
        r[i] = h[1] + n;
    }
    *rs = ns;
    st->lofi_h[0] = h[0];
    st->lofi_h[1] = h[1];
    st->lofi_n += B;
    if (st->lofi_n > 1 << 20)
        st->lofi_n = 0;
}

/* ---- CHORUS: MODE I, II, I+II (Juno-style rates and depths), a short delay a side, the LFO half a turn
 * apart left and right ---- */
static int16_t cho_l[FX_CHO_N] PLAT_POOL, cho_r[FX_CHO_N] PLAT_POOL;
static uint32_t cho_w, cho_ph;
static uint32_t cho_zrun;                /* frames in a row it took 0 in: past FX_CHO_N its lines hold only 0 */
static PLAT_C2 void fx_chorus(int32_t *l, int32_t *r, int wet_only, const int32_t *in)
{
    uint32_t mode = K(0x9B) > 2u ? 2u : K(0x9B), inc = FX_CHO_INC[mode][K(0x9C) > 100u ? 100u : K(0x9C)];
    int32_t dep = FX_CHO_DEP[mode][K(0x9D) > 100u ? 100u : K(0x9D)], mix = (K(0x9E) > 100u ? 100 : K(0x9E)) * 32768 / 100;
    int32_t base = 45158, any = 0;       /* 4 ms, Q8 */
    uint32_t i;
    for (i = 0; i < B; i++)
        any |= in ? in[i] : l[i] | r[i];
    if (!any && cho_zrun >= FX_CHO_N) {  /* asleep: 0 in, its lines 0, so 0 out (and l, r stay 0); its clocks go on */
        cho_w += B;
        cho_ph += inc * B;
        fx_act |= 1u << FX_CHORUS;
        return;
    }
    cho_zrun = any ? 0u : zrun_on(cho_zrun, B);
    /* Three passes over the block, the same values as a frame at a time: the lines take the block's frames (a tap
     * reads 176 frames back at least, 689 at most (dep < 2^16) of FX_CHO_N: never a frame of this block), the
     * taps, the mix. In the taps' pass the LFO's sine inline (a call to dx7_sin a frame kept the loop's state in
     * memory) and the depth's products in 32 bits (dep < 2^16, 32768 +- m in 1 .. 65535: below 2^32); a tap's
     * value lies between two 16-bit samples, so x mix (<= 32768) fits 31 bits too. Only the dry, a mix value,
     * needs 64 */
    {
        const int32_t *st = fx_sin;
        uint32_t w = cho_w, ph = cho_ph;
        int32_t yl[B], yr[B];
        volatile int32_t dry_m = 32768 - mix / 2;   /* (read in the loop: hoisted, the device's compiler made the
                                                     * multiply 64 x 64) */
        if (in)
            for (i = 0; i < B; i++)
                cho_l[(w + i) & (FX_CHO_N - 1u)] = cho_r[(w + i) & (FX_CHO_N - 1u)] = sat16(in[i]);
        else
            for (i = 0; i < B; i++) {
                cho_l[(w + i) & (FX_CHO_N - 1u)] = sat16(l[i]);
                cho_r[(w + i) & (FX_CHO_N - 1u)] = sat16(r[i]);
            }
        for (i = 0; i < B; i++, w++) {
            int32_t m, da, db, a, b, ia, ib;
            ph += inc;
            m = sin15_t(st, ph);
            da = base + (int32_t)(((uint32_t)dep * (uint32_t)(32768 + m)) >> 15);
            db = base + (int32_t)(((uint32_t)dep * (uint32_t)(32768 - m)) >> 15);
            ia = da >> 8;
            ib = db >> 8;
            a = cho_l[(w - (uint32_t)ia) & (FX_CHO_N - 1u)];
            b = cho_l[(w - (uint32_t)ia - 1u) & (FX_CHO_N - 1u)];
            yl[i] = a + (((b - a) * (da & 255)) >> 8);
            a = cho_r[(w - (uint32_t)ib) & (FX_CHO_N - 1u)];
            b = cho_r[(w - (uint32_t)ib - 1u) & (FX_CHO_N - 1u)];
            yr[i] = a + (((b - a) * (db & 255)) >> 8);
        }
        cho_w = w;
        cho_ph = ph;
        if (wet_only)
            for (i = 0; i < B; i++) {
                l[i] += (yl[i] * mix) >> 15;
                r[i] += (yr[i] * mix) >> 15;
            }
        else if (in)
            for (i = 0; i < B; i++) {
                int32_t x = mulq15(in[i], dry_m);
                l[i] = x + ((yl[i] * mix) >> 15);
                r[i] = x + ((yr[i] * mix) >> 15);
            }
        else
            for (i = 0; i < B; i++) {
                l[i] = mulq15(l[i], dry_m) + ((yl[i] * mix) >> 15);
                r[i] = mulq15(r[i], dry_m) + ((yr[i] * mix) >> 15);
            }
    }
    fx_act |= 1u << FX_CHORUS;
}

/* ---- DELAY: one line of 1 s at 29.4 kHz, fed one signal (the send, or the mix summed); TONE's low-pass
 * darkens each repeat; PING-PONG reads it at the time (left) and twice the time (right, the low-pass twice
 * over), feeding back from the right, so its time is at most 0.5 s ---- */
static int16_t dly_buf[FX_DLY_N] PLAT_POOL;
static uint32_t dly_w;
static int32_t dly_a, dly_b1, dly_b2;
static down_t dly_dn;
static up_t dly_upl, dly_upr;
static struct { int32_t on, len, fb, k; } dly_over;   /* ECHO THROW's own settings while it runs */
static uint32_t dly_hand;                /* the line just back from ECHO THROW: frames left of its MIX gliding from the
                                          * throw's (0.8) to the setting's (80 ms) */
static int32_t dly_from;                 /* ECHO THROW pressed: the DELAY's MIX then (0: off), its echoes gliding from it
                                          * to the throw's over 80 ms */
static uint32_t dly_qrun;                /* samples (29.4 kHz) in a row it took 0 in and gave FX_REMAINS out at most */
static uint8_t dly_asleep;               /* its line and low-passes 0: skipped while nothing comes in (above) */

static PLAT_C2 uint32_t dly_len(uint32_t nv, uint32_t pp)   /* TIME's frames at 29.4 kHz (the longest that fits) */
{
    uint32_t most = pp ? FX_DLY_N / 2u - 2u : FX_DLY_N - 2u, n;
    for (;;) {
        n = FX_NV24[nv] * fx_wclk.beat_frames / 36u;   /* (/24 beats, x 2/3; beat_frames < 2^25) */
        if (n <= most || nv == 0u)
            break;
        nv--;
    }
    return n < 1u ? 1u : n > most ? most : n;
}

/* n samples at 29.4 kHz (v in, ol / orr out). len 1 .. FX_DLY_N - 2 (PING-PONG: .. FX_DLY_N / 2 - 2, dly_len and ECHO
 * THROW keep it so), so the places it reads need one subtraction at most to come round, not a divide. A loop for
 * each way, the states in locals; fb and k read in the loop (volatile: hoisted, the device's compiler made the
 * products 64 x 64, two multiplies each) */
static PLAT_C2 void dly_run(const int32_t *v, uint32_t n, uint32_t len, int32_t fb, int32_t k, uint32_t pp,
                            int32_t *ol, int32_t *orr)
{
    volatile int32_t fbv = fb, kv = k;
    int32_t a = dly_a, b1 = dly_b1, b2 = dly_b2;
    uint32_t w = dly_w, m;
    if (pp)
        for (m = 0; m < n; m++) {
            int32_t yr;
            uint32_t j = w + FX_DLY_N - len, j2 = w + FX_DLY_N - 2u * len;
            if (j >= FX_DLY_N)
                j -= FX_DLY_N;
            if (j2 >= FX_DLY_N)
                j2 -= FX_DLY_N;
            a += mulq15z(dly_buf[j] - a, kv);
            b1 += mulq15z(dly_buf[j2] - b1, kv);
            b2 += mulq15z(b1 - b2, kv);
            yr = mulq15z(b2, fbv);
            dly_buf[w] = sat16(v[m] + mulq15z(yr, fbv));
            if (++w >= FX_DLY_N)
                w = 0;
            ol[m] = a;
            orr[m] = yr;
        }
    else
        for (m = 0; m < n; m++) {
            uint32_t j = w + FX_DLY_N - len;
            if (j >= FX_DLY_N)
                j -= FX_DLY_N;
            a += mulq15z(dly_buf[j] - a, kv);
            dly_buf[w] = sat16(v[m] + mulq15z(a, fbv));
            if (++w >= FX_DLY_N)
                w = 0;
            ol[m] = orr[m] = a;
        }
    dly_a = a;
    dly_b1 = b1;
    dly_b2 = b2;
    dly_w = w;
}

static PLAT_C2 void fx_delay(int32_t *l, int32_t *r, int wet_only, const int32_t *in, int32_t extra_mix,
                             const int32_t *extra)
{
    uint32_t pp = K(0xA2) != 0u, len = dly_len(K(0x9F) < 12u ? K(0x9F) : 11u, pp), i;
    int32_t fb = (K(0xA0) > 100u ? 100 : K(0xA0)) * 30147 / 100;   /* (x 0.92) */
    int32_t k = FX_DLY_TONE[K(0xA1) > 100u ? 100u : K(0xA1)], mix = (K(0xA3) > 100u ? 100 : K(0xA3)) * 32768 / 100;
    if (dly_over.on) {                   /* ECHO THROW: a dotted 1/8, high feedback, ping-pong */
        len = dly_over.len;
        fb = dly_over.fb;
        k = dly_over.k;
        pp = 1;
        mix = extra_mix;
    } else if (dly_hand) {               /* (just back from ECHO THROW: its MIX from the throw's) */
        mix += mulq15(26214 - mix, (int32_t)((dly_hand << 15) / FX_OUT_FADE));
        dly_hand = dly_hand > B ? dly_hand - B : 0u;
    }
    /* three passes over the block, each state worked in the same order as a frame at a time (the same sound,
     * fewer values held at once): down to 29.4 kHz, the line, back up and mixed; the converters as fx_reverb's */
    int32_t d[B], dl[B], dr[B], yl[B], yr[B], any;   /* (yl first holds the frames going down) */
    uint8_t made[B];
    uint32_t n;
    if (dly_asleep && (extra ? fx_zero(extra) : in ? fx_zero(in) : fx_zero(l) && fx_zero(r)) &&
        conv_still(&dly_dn, &dly_upl, &dly_upr)) {   /* (at rest: 0s through, l and r as they were) */
        if ((dly_w += conv_rest(&dly_dn, &dly_upl, &dly_upr)) >= FX_DLY_N)
            dly_w -= FX_DLY_N;
        fx_act |= 1u << FX_DELAY;
        return;
    }
    if (!extra && !in)
        for (i = 0; i < B; i++)
            yl[i] = (l[i] + r[i]) >> 1;
    n = down_block(&dly_dn, extra ? extra : in ? in : yl, d, made, &any);
    if (!any && dly_asleep) {            /* (asleep: 0 in, 0 out; its place goes on) */
        if ((dly_w += n) >= FX_DLY_N)
            dly_w -= FX_DLY_N;
        c2_memset(dl, 0, sizeof dl);
        c2_memset(dr, 0, sizeof dr);
    } else {
        dly_asleep = 0;
        dly_run(d, n, len, fb, k, pp, dl, dr);
        dly_qrun = !any && fx_remains(dl, dr, n) ? zrun_on(dly_qrun, n) : 0u;
        if (dly_qrun >= FX_DLY_N) {      /* a whole turn of the line quiet: what is left goes, it sleeps */
            c2_memset(dly_buf, 0, sizeof dly_buf);
            dly_a = dly_b1 = dly_b2 = 0;
            dly_asleep = 1;
            dly_qrun = 0;
        }
    }
    up_block(&dly_upl, &dly_upr, made, dl, dr, n, yl, yr);
    {
        volatile int32_t mixv = mix;     /* (read in the loop: see dly_run) */
        for (i = 0; i < B; i++) {
            l[i] += mulq15(yl[i], mixv);   /* (the dry stays as it is: a send's return, or the mix with echoes) */
            r[i] += mulq15(yr[i], mixv);
        }
    }
    fx_act |= 1u << FX_DELAY;
}

/* ---- REVERB: Dattorro's plate (J. Dattorro, "Effect Design, Part 1", JAES 1997): four input diffusers, then
 * a figure-eight tank of two halves, each a modulated allpass, a delay, a damping lowpass, an allpass and a
 * delay, feeding the other half; stereo out from seven taps a side. PRE-DELAY runs before it. SIZE sets the
 * tank's decay (about 1.3 s to 11 s to -60 dB), DAMP its lowpass ---- */
static int16_t rv_buf[FX_REV_TOTAL] PLAT_POOL;   /* every line one after another (rv_line) */
static int16_t rv_pre[FX_PRE_N] PLAT_POOL;
typedef struct { uint16_t off, n, i; } line_t;
static line_t rv_line[12];
static uint32_t rv_pw;
static int32_t rv_bw, rv_dl, rv_dr, rv_s, rv_c = 1 << 30;
static down_t rv_dn;
static up_t rv_upl, rv_upr;
/* the plate asleep (above): its lines and states 0, skipped while nothing comes in, its LFO and the lines' places
 * going on, so waking is the same sound to the sample */
static uint32_t rv_qrun;                 /* samples (29.4 kHz) in a row it took 0 in and gave FX_REMAINS out at most */
static uint32_t rv_qneed;                /* a whole turn of its lines: the longest line or PRE-DELAY */
static uint8_t rv_asleep;

static void rv_layout(void)
{
    uint32_t i, off = 0;
    rv_qneed = FX_PRE_N;
    for (i = 0; i < 12u; i++) {
        rv_line[i].off = (uint16_t)off;
        rv_line[i].n = (uint16_t)(FX_REV_LEN[i] + (i == 4u || i == 8u ? FX_REV_EXC + 2u : 0u));
        rv_line[i].i = 0;
        off += rv_line[i].n;
        rv_qneed = rv_line[i].n > rv_qneed ? rv_line[i].n : rv_qneed;
    }
}
/* The modulated allpasses' lines (4 and 8) as constants (rv_layout's, from the same tables): their length and start;
 * rv_tap: a line's sample written k samples ago */
#define RV_N(k) ((uint32_t)FX_REV_LEN[k] + ((k) == 4 || (k) == 8 ? FX_REV_EXC + 2u : 0u))
#define RV_OFF4 (RV_N(0) + RV_N(1) + RV_N(2) + RV_N(3))
#define RV_OFF8 (RV_OFF4 + RV_N(4) + RV_N(5) + RV_N(6) + RV_N(7))
FX_INLINE int32_t rv_tap(uint32_t i, uint32_t k, uint32_t n, uint32_t off)
{
    int32_t j = (int32_t)i - (int32_t)k;
    return rv_buf[off + (uint32_t)(j < 0 ? j + (int32_t)n : j)];
}

/* PRE-DELAY (pre < FX_PRE_N: one subtraction brings its place round) and the plate over n samples at 29.4 kHz:
 * p in, pl and pr out.
 * In runs of samples where no place it reads or writes comes round, so each is a pointer and the run's sample
 * number (a sample at a time every place went round on its own: ~5 instructions a tap, of 625 a sample). The same
 * reads and writes in the same order as a sample at a time: each line's write place (an allpass's own tap, the
 * tank's feedback taps, all at the place written next), lines 5 and 9 read just after their write (the place
 * after), the 14 output taps (after the tank's writes: one place on), PRE-DELAY's write and read; the modulated
 * allpasses' moving taps a sample at a time, from their line's place */
#define RV_NP 30                         /* the pointers: 0-11 the lines' places, 12 / 13 lines 5 / 9 after, 14-27 the
                                          * taps (FX_REV_TAP's order), 28 / 29 PRE-DELAY's write / read */
static const uint8_t RV_TAP_LINE[14] PLAT_C2_RO = {9, 9, 10, 11, 5, 6, 7, 5, 5, 6, 7, 9, 10, 11};
FX_INLINE int32_t rv_ap_at(int16_t *w, int32_t x, int32_t g)   /* an allpass whose tap is its own place */
{
    int32_t z = *w, v = x - mulq15z(z, g);
    *w = sat16(v);
    return z + mulq15z(v, g);
}
FX_INLINE int32_t rv_ap_mod_at(uint32_t i, uint32_t n, uint32_t off, int16_t *w, int32_t len8, int32_t x, int32_t g)
{
    uint32_t k = (uint32_t)(len8 >> 8);
    int32_t z0 = rv_tap(i, k, n, off), z = z0 + (((rv_tap(i, k + 1u, n, off) - z0) * (len8 & 255)) >> 8);
    int32_t v = x - mulq15z(z, g);
    *w = sat16(v);
    return z + mulq15z(v, g);
}
static PLAT_C2 void rv_plate(const int32_t *p, uint32_t n, int32_t size, int32_t damp, uint32_t pre, int32_t *pl,
                             int32_t *pr)
{
    int32_t decay = FX_REV_DECAY[size], dd2 = FX_REV_DD2[size], dk = FX_REV_DAMP[damp];
    int32_t bw = rv_bw, dl = rv_dl, dr = rv_dr, rs = rv_s, rc = rv_c;
    uint32_t done, k, I[12], pw = rv_pw;
    rv_asleep = 0;                       /* (worked: awake, REVERB SWELL's too) */
    for (k = 0; k < 12u; k++)
        I[k] = rv_line[k].i;
    for (done = 0; done < n;) {
        int16_t *P[RV_NP];
        uint32_t run = n - done, m, s, nk;
        for (k = 0; k < 12u; k++) {      /* (the run: as long as no pointer reaches its line's end) */
            nk = rv_line[k].n;
            P[k] = rv_buf + rv_line[k].off + I[k];
            run = run < nk - I[k] ? run : nk - I[k];
        }
        for (k = 0; k < 2u; k++) {
            uint32_t ln = k ? 9u : 5u;
            nk = rv_line[ln].n;
            s = I[ln] + 1u == nk ? 0u : I[ln] + 1u;
            P[12u + k] = rv_buf + rv_line[ln].off + s;
            run = run < nk - s ? run : nk - s;
        }
        for (k = 0; k < 14u; k++) {
            uint32_t ln = RV_TAP_LINE[k];
            int32_t j = (int32_t)(I[ln] + 1u) - (int32_t)FX_REV_TAP[k];
            nk = rv_line[ln].n;
            s = (uint32_t)(j < 0 ? j + (int32_t)nk : j);
            P[14u + k] = rv_buf + rv_line[ln].off + s;
            run = run < nk - s ? run : nk - s;
        }
        P[28] = rv_pre + pw;
        run = run < FX_PRE_N - pw ? run : FX_PRE_N - pw;
        s = pw + FX_PRE_N - pre;
        s = s >= FX_PRE_N ? s - FX_PRE_N : s;
        P[29] = rv_pre + s;
        run = run < FX_PRE_N - s ? run : FX_PRE_N - s;
        for (m = 0; m < run; m++) {
            int32_t fbl = P[11][m], fbr = P[7][m], x, s1, c1, el, er;
            P[28][m] = sat16(p[done + m]);
            x = P[29][m];
            bw += mulq15z(x - bw, FX_REV_BW);   /* the input's bandwidth */
            x = rv_ap_at(&P[0][m], bw, 24576);
            x = rv_ap_at(&P[1][m], x, 24576);
            x = rv_ap_at(&P[2][m], x, 20480);
            x = rv_ap_at(&P[3][m], x, 20480);
            s1 = rs + (int32_t)(((int64_t)FX_REV_LFO * rc) >> 30);   /* ~0.9 Hz: sine and cosine for the halves */
            c1 = rc - (int32_t)(((int64_t)FX_REV_LFO * s1) >> 30);
            rs = s1;
            rc = c1;
            el = (int32_t)(FX_REV_LEN[4] << 8) + (int32_t)(((int64_t)(FX_REV_EXC << 8) * s1) >> 30);
            er = (int32_t)(FX_REV_LEN[8] << 8) + (int32_t)(((int64_t)(FX_REV_EXC << 8) * c1) >> 30);
            P[5][m] = sat16(rv_ap_mod_at(I[4] + m, RV_N(4), RV_OFF4, &P[4][m], el, x + mulq15z(fbl, decay), -22938));
            dl += mulq15z(P[12][m] - dl, dk);
            P[7][m] = sat16(rv_ap_at(&P[6][m], mulq15z(dl, decay), dd2));
            P[9][m] = sat16(rv_ap_mod_at(I[8] + m, RV_N(8), RV_OFF8, &P[8][m], er, x + mulq15z(fbr, decay), -22938));
            dr += mulq15z(P[13][m] - dr, dk);
            P[11][m] = sat16(rv_ap_at(&P[10][m], mulq15z(dr, decay), dd2));
            pl[done + m] = mulq15(P[14][m] + P[15][m] - P[16][m] + P[17][m] - P[18][m] - P[19][m] - P[20][m], 19661);
            pr[done + m] = mulq15(P[21][m] + P[22][m] - P[23][m] + P[24][m] - P[25][m] - P[26][m] - P[27][m], 19661);
        }
        for (k = 0; k < 12u; k++)
            I[k] = I[k] + run == rv_line[k].n ? 0u : I[k] + run;
        pw = pw + run == FX_PRE_N ? 0u : pw + run;
        done += run;
    }
    for (k = 0; k < 12u; k++)
        rv_line[k].i = (uint16_t)I[k];
    rv_pw = pw;
    rv_bw = bw;
    rv_dl = dl;
    rv_dr = dr;
    rv_s = rs;
    rv_c = rc;
}

/* n samples of the plate asleep (rv_asleep: its lines and states 0, its input 0): its clocks as rv_plate moves them */
static PLAT_C2 void rv_idle(uint32_t n)
{
    uint32_t m, k;
    for (m = 0; m < n; m++) {
        int32_t s = rv_s + (int32_t)(((int64_t)FX_REV_LFO * rv_c) >> 30);
        rv_c -= (int32_t)(((int64_t)FX_REV_LFO * s) >> 30);
        rv_s = s;
    }
    if ((rv_pw += n) >= FX_PRE_N)        /* (n < every line and PRE-DELAY: one turn at most) */
        rv_pw -= FX_PRE_N;
    for (k = 0; k < 12u; k++) {
        uint32_t i = rv_line[k].i + n;
        rv_line[k].i = (uint16_t)(i >= rv_line[k].n ? i - rv_line[k].n : i);
    }
}

static PLAT_C2 void rv_clear(void)       /* the plate's lines and states 0 (its LFO and places go on) */
{
    c2_memset(rv_buf, 0, sizeof rv_buf);
    c2_memset(rv_pre, 0, sizeof rv_pre);
    rv_bw = rv_dl = rv_dr = 0;
}

/* a block of the plate fed x (its input as the playground's: the sum of both sides at 0.4: 0.8 x a mono input; x is
 * scaled in place), its two sides back at 44.1 kHz (REVERB SWELL's: never resting while it runs). The same values
 * as a frame at a time (down_push, the plate a sample, up_put / up_get), fx_reverb's three passes */
static PLAT_C2 void rv_block(int32_t *x, int32_t size, int32_t damp, uint32_t pre, int32_t *yl, int32_t *yr)
{
    int32_t p[B], pl[B], pr[B], any;
    uint8_t made[B];
    uint32_t i, n;
    for (i = 0; i < B; i++)
        x[i] = mulq15(x[i], 26214);
    n = down_block(&rv_dn, x, p, made, &any);
    rv_plate(p, n, size, damp, pre, pl, pr);
    up_block(&rv_upl, &rv_upr, made, pl, pr, n, yl, yr);
}

static int32_t rv_swell_size = -1;       /* REVERB SWELL: the plate's SIZE while it runs (else -1) */
/* REVERB SWELL let go: the plate back with REVERB at once, its wet and dry gliding from the swell's last (fx_punches
 * keeps them) to the setting's over 80 ms: left, frames of it */
static struct { uint32_t left; int32_t wet, dry; } rv_hand;
/* REVERB SWELL pressed: REVERB's levels then (its wet, its dry, the plate fed the mix or not; REVERB off: none), the
 * swell rising from them to its own over its two beats */
static struct { int32_t wet, dry, in; } rv_from;

static PLAT_C2 void fx_reverb(int32_t *l, int32_t *r, int wet_only, const int32_t *in)
{
    int32_t size = K(0xA4) > 100u ? 100 : K(0xA4), damp = K(0xA5) > 100u ? 100 : K(0xA5);
    int32_t mix = (K(0xA7) > 100u ? 100 : K(0xA7)) * 32768 / 100, dry = 32768 - mix * 2 / 5;
    uint32_t pre = (uint32_t)(K(0xA6) > 100u ? 100 : K(0xA6)) * 294u / 10u, i, n = 0;   /* (ms -> 29.4 kHz) */
    int32_t p[B], pl[B], pr[B];
    uint8_t made[B];
    pre = pre >= FX_PRE_N ? FX_PRE_N - 1u : pre;
    if (rv_asleep && (in ? fx_zero(in) : fx_zero(l) && fx_zero(r)) && conv_still(&rv_dn, &rv_upl, &rv_upr)) {
        rv_idle(conv_rest(&rv_dn, &rv_upl, &rv_upr));   /* (at rest: 0s through; dry or not, 0 x dry + 0 is 0) */
        if (!wet_only) {
            c2_memset(l, 0, B * sizeof *l);
            c2_memset(r, 0, B * sizeof *r);
        }
        fx_act |= 1u << FX_REVERB;
        return;
    }
    /* three passes over the block, each state worked in the same order as a frame at a time (down_push, the plate a
     * sample, up_put / up_get: the same sound, fewer values held at once): down to 29.4 kHz (down_block), PRE-DELAY
     * and the plate, back up (up_block) and mixed */
    int32_t any, yl[B], yr[B];           /* (yl first holds the frames going down) */
    if (in)
        for (i = 0; i < B; i++)
            yl[i] = mulq15((in[i] + in[i]) >> 1, 26214);
    else
        for (i = 0; i < B; i++)
            yl[i] = mulq15((l[i] + r[i]) >> 1, 26214);
    n = down_block(&rv_dn, yl, p, made, &any);
    if (!any && rv_asleep) {             /* (asleep: 0 in, 0 out) */
        rv_idle(n);
        c2_memset(pl, 0, sizeof pl);
        c2_memset(pr, 0, sizeof pr);
    } else {
        rv_plate(p, n, size, damp, pre, pl, pr);
        rv_qrun = !any && fx_remains(pl, pr, n) ? zrun_on(rv_qrun, n) : 0u;
        if (rv_qrun >= rv_qneed) {       /* a whole turn of its lines quiet: what is left goes, it sleeps */
            rv_clear();
            rv_asleep = 1;
            rv_qrun = 0;
        }
    }
    up_block(&rv_upl, &rv_upr, made, pl, pr, n, yl, yr);
    uint32_t hand = rv_hand.left;
    if (hand) {                          /* (just back from REVERB SWELL: its levels gliding to these; the sends' dry,
                                          * which the swell had lowered, from it to whole) */
        int32_t t = (int32_t)((hand << 15) / FX_OUT_FADE);
        dry = wet_only ? 32768 : dry;
        mix += mulq15(rv_hand.wet - mix, t);
        dry += mulq15(rv_hand.dry - dry, t);
        rv_hand.left = hand > B ? hand - B : 0u;
    }
    {
        volatile int32_t mixv = mix, dryv = dry;   /* (read in the loops: hoisted, the device's compiler made the
                                                    * products 64 x 64, two multiplies each) */
        if (wet_only && !hand)           /* (mixed: each way a loop of its own, nothing tested a frame) */
            for (i = 0; i < B; i++) {
                l[i] += mulq15(yl[i], mixv);
                r[i] += mulq15(yr[i], mixv);
            }
        else if (in && !wet_only)
            for (i = 0; i < B; i++) {
                int32_t x = mulq15(in[i], dryv);
                l[i] = x + mulq15(yl[i], mixv);
                r[i] = x + mulq15(yr[i], mixv);
            }
        else
            for (i = 0; i < B; i++) {
                l[i] = mulq15(l[i], dryv) + mulq15(yl[i], mixv);
                r[i] = mulq15(r[i], dryv) + mulq15(yr[i], mixv);
            }
    }
    fx_act |= 1u << FX_REVERB;
}

/* ---- LIMITER: a block's gain is what its loudest sample needs at once (no overshoot: the block is whole
 * before it goes out), back up in a straight line over RELEASE ---- */
static int32_t lim_g = 32768, lim_least = 32768;
static PLAT_C2 void fx_limit(int32_t *l, int32_t *r)
{
    int32_t ceil = FX_DB_Q15[64 + (int32_t)(K(0xA8) > 12u ? 12u : K(0xA8)) - 12] - 1;   /* (dB under the 16-bit full scale) */
    uint32_t rel = (10u + 10u * (K(0xA9) > 99u ? 99u : K(0xA9))) * 441u / 10u;   /* RELEASE in frames */
    int32_t step = (int32_t)(32768u * B / rel), pk = 0, need, g0 = lim_g, g1, a, acc, d;
    uint32_t i;
    for (i = 0; i < B; i++) {
        a = l[i] < 0 ? -l[i] : l[i];
        pk = a > pk ? a : pk;
        a = r[i] < 0 ? -r[i] : r[i];
        pk = a > pk ? a : pk;
    }
    need = pk > ceil ? (ceil << 15) / pk : 32768;   /* (ceil < 2^15: in 32 bits) */
    g1 = g0 + (step < 1 ? 1 : step);
    g1 = g1 > 32768 ? 32768 : g1;
    if (need < g1)
        g0 = g1 = need;                  /* (at once, the whole block) */
    if (g0 == 32768 && g1 == 32768) {
        lim_g = g1;
        return;
    }
    acc = g0 << 6;
    d = g1 - g0;
    for (i = 0; i < B; i++) {
        acc += d;
        l[i] = (int32_t)(((int64_t)l[i] * (acc >> 6)) >> 15);
        r[i] = (int32_t)(((int64_t)r[i] * (acc >> 6)) >> 15);
    }
    lim_g = g1;
    lim_least = g0 < lim_least ? g0 : lim_least;
    fx_act |= 1u << FX_LIMITER;
}

/* ---- the METRONOME: its own clicks, after the effects (specs/ui/screens/fx.md#metronome) ---- */
static struct {
    int32_t n, env;                      /* frames since the click (FX_MET_LEN: none), its envelope (Q30) */
    uint8_t accent;
    uint32_t ph, ph2, inc;
    int8_t sound, level;                 /* a count-in's click: its own SOUND and LEVEL (-1: the METRONOME's) */
} met;
static uint32_t met_own;                 /* an own TEMPO's place in the beat (2^32 a beat) */
static uint32_t met_beat;                /* its beats counted (the bar's first) */
static volatile int8_t met_shown = -1;   /* the bar's beat it clicked last, -1 while it does not count (fx_met_beat) */
static int16_t met_out[B];               /* the clicks this block added (fx_clicks: USB audio leaves them out) */
static uint8_t met_out_on;
PLAT_C2 const int16_t *fx_clicks(void) { return met_out_on ? met_out : 0; }

static PLAT_C2 void met_click(int accent)
{
    met.n = 0;
    met.env = 1 << 30;
    met.accent = (uint8_t)accent;
    met.ph = met.ph2 = 0;
    met.sound = met.level = -1;
}

void fx_click(int sound, int level, int accent) { fx_ev(FX_EV_CLICK, sound, level, accent); }

static PLAT_C2 void fx_metronome(int32_t *l, int32_t *r, int on)
{
    uint32_t bpm = fx_w.met[0] | (uint32_t)fx_w.met[1] << 8, beats = fx_w.met[2] ? fx_w.met[2] : 4u, sound, i, ns;
    int32_t level, amp;
    if (!on) {
        met_shown = -1;                  /* (off: only a count-in's click sounds) */
    } else if (bpm) {                    /* its own tempo: it clicks with nothing playing */
        uint32_t inc = bpm * 1623u + bpm / 5u, o = met_own;   /* (2^32 / (60 x 44100) = 1623.2 a BPM) */
        met_own += inc * B;
        if (met_own < o) {
            met_shown = (int8_t)(met_beat % beats);
            met_click(met_shown == 0);
            met_beat++;
        }
    } else if (fx_wclk.playing) {        /* FOLLOW: on the song's beats */
        uint32_t nf = fx_wclk.frac + fx_wclk.inc * (fx_wclk.hold < B ? fx_wclk.hold : B);
        if (fx_wclk.frac == 0u || nf < fx_wclk.frac) {
            met_shown = (int8_t)((fx_wclk.beat + (fx_wclk.frac ? 1u : 0u)) % beats);
            met_click(met_shown == 0);
        }
    } else if (fx_wclk.keys) {           /* FOLLOW on THE KEYS: at their tempo, counted as its own */
        uint32_t o = met_own;
        met_own += fx_wclk.inc * B;
        if (met_own < o) {
            met_shown = (int8_t)(met_beat % beats);
            met_click(met_shown == 0);
            met_beat++;
        }
    } else {
        met_shown = -1;                  /* (FOLLOW with a song's or the loops' setup, nothing playing: no beats) */
    }
    if (met.n >= FX_MET_LEN)
        return;
    c2_memset(met_out, 0, sizeof met_out);
    met_out_on = 1;
    sound = met.sound >= 0 ? (uint32_t)met.sound : fx_w.met[3] & 3u;
    level = (met.level >= 0 ? met.level : fx_w.met[4] > 100u ? 100 : fx_w.met[4]) * 32768 / 100;
    amp = mulq15(level, met.accent ? 8192 : 5243);   /* x 0.5 or x 0.32, x 0.5 (in the mix as a voice is) */
    ns = rnd_state;                      /* (the noise in a local: a register in the loop) */
    for (i = 0; i < B && met.n < FX_MET_LEN; i++, met.n++) {
        int32_t x;
        if (sound == 0u) {               /* CLICK: white noise */
            x = (int32_t)(rnd_at(&ns) >> 17) - 16384;
            x *= 2;
        } else if (sound == 1u) {        /* BEEP */
            met.ph += FX_MET_HZ[met.accent ? 1 : 0];
            x = sin15_t(fx_sin, met.ph);
        } else if (sound == 2u) {        /* WOOD: its pitch falls as it rings */
            met.ph += FX_MET_HZ[met.accent ? 3 : 2] - FX_MET_WC[met.accent ? 1 : 0] * (uint32_t)met.n;
            x = sin15_t(fx_sin, met.ph);
        } else {                         /* COWBELL: two squares */
            met.ph += FX_MET_HZ[4];
            met.ph2 += FX_MET_HZ[5];
            x = ((met.ph >> 31 ? -13107 : 13107) + (met.ph2 >> 31 ? -13107 : 13107));
        }
        x = (int32_t)(((int64_t)x * met.env) >> 30);
        met.env = (int32_t)(((int64_t)met.env * FX_MET_DECAY[sound]) >> 30);
        x = mulq15(x, amp);
        l[i] += x;
        r[i] += x;
        met_out[i] = (int16_t)x;
    }
    rnd_state = ns;
}

/* CUT held: a block with nothing worked on, the time going on as fx_metronome counts it (its own tempo's beats, so
 * its clicks are on the beat again after); a click started meanwhile (a count-in's) is dropped, never heard late.
 * (The player's, in its block's place, with the settings live: CUT's halves have no effects' work handed over) */
void fx_hold_block(void)
{
    uint32_t bpm = fx_met[0] | (uint32_t)fx_met[1] << 8;
    if (fx_on >> FX_METRONOME & 1u && bpm) {
        uint32_t inc = bpm * 1623u + bpm / 5u, o = met_own;
        met_own += inc * B;
        if (met_own < o)
            met_beat++;
    }
    met.n = FX_MET_LEN;
    met_out_on = 0;
}

/* ---- the punch-ins (specs/ui/screens/fx.md#punch-in-effects), from the last 0.5 s of the mix ----
 * The buffer keeps the mix as its middle, (L + R) / 2, at 29.4 kHz (the 2:3 converter's way down, as the DELAY's
 * and the REVERB's inputs take), and its sides, (L - R) / 2, at 7.35 kHz: a 6:1 converter, three running sums and
 * their comb (a CIC filter of order 3: 16 frames to a sample, its nulls on what would fold back), then a
 * compensator for its droop, -0.4 1.8 -0.4 (stereo flat to 2 kHz, -6 dB at 3 kHz). Read back at any place, each
 * line linear between its samples, through a low-pass (the middle's the 2:3 converter's, the sides' one at
 * 3.5 kHz): left = middle + sides, right = middle - sides, so a mono output hears the middle exactly. A place
 * counts the output's frames (FX_HIST_N of them): frame f's middle sample is at 2f / 3, its sides' at
 * (f + 2.5) / 6; the converters run on in the frames' phases (f % 3, f % 6), so where a place's samples lie never
 * depends on how it was reached. */
static int16_t hist_m[FX_MID_N] PLAT_POOL, hist_s[FX_SIDE_N] PLAT_POOL;
static uint32_t hist_w;                  /* the next frame's place */
static uint32_t hist_zrun;               /* frames in a row it was fed 0: from FX_HIST_CLR on, its converters hold
                                          * nothing (emptied), past FX_HIST_QUIET it holds only 0, and 0s more need not
                                          * be written (its place goes on) */
static struct {
    down_t dn;                           /* the middle's way down (its phase: the frame's place % 3) */
    uint32_t i1, i2, i3;                 /* the sides' running sums (wrapping: the comb takes their differences) */
    uint32_t c1, c2, c3;                 /* the comb's values at the last sample */
    int32_t s1, s2;                      /* the sides' last two samples, x 216 (the compensator writes the one before
                                          * the newest: it needs a sample on each side) */
} hr;
static struct {
    int8_t k;                            /* the punch-in running (-1 none) */
    uint8_t keep, up, out_on;            /* the buffer kept (a replay); vinyl's spin-up; let go: fading out */
    uint32_t t, len, start, out, hs, w0;
    uint32_t rp;                         /* the read place, Q16 frames */
    int32_t speed;                       /* Q16 */
    int32_t hl, hr, g;
    svf_s_t f[2];
    bq_t um, us;                         /* the buffer read back: the middle's and the sides' low-passes */
} pu;                                    /* (fx_init: k -1, none) */
static int32_t pu_send[B];               /* ECHO THROW's, REVERB SWELL's input */

/* the block's frames from `from` on into the buffer, from hist_w on (its place moved on) */
static PLAT_C2 void hist_rec(const int32_t *l, const int32_t *r, uint32_t from)
{
    int32_t m[B], p[B], any;
    uint8_t made[B];
    uint32_t i, n, w = hist_w, k, ph = w % 6u, j = w / 6u, i1 = hr.i1, i2 = hr.i2, i3 = hr.i3;
    for (i = from; i < B; i++) {         /* each side as kept before (a 16-bit value): the middle for its converter;
                                          * the sides summed, a sample every 6 frames (j: its place) */
        int32_t a = sat16(l[i]), c = sat16(r[i]);
        m[i] = (a + c) >> 1;
        i1 += (uint32_t)((a - c) >> 1);
        i2 += i1;
        i3 += i2;
        if (ph == 5u) {                  /* (the comb: three differences; a sample x 216) */
            uint32_t y1 = i3 - hr.c1, y2 = y1 - hr.c2;
            int32_t v = (int32_t)(y2 - hr.c3), q = 9 * hr.s1 - 2 * hr.s2 - 2 * v;   /* (the one before, x 1080) */
            hr.c1 = i3;
            hr.c2 = y1;
            hr.c3 = y2;
            hist_s[j ? j - 1u : FX_SIDE_N - 1u] = sat16((int32_t)(((int64_t)q * 3976822) >> 32));   /* (/ 1080) */
            hr.s2 = hr.s1;
            hr.s1 = v;
            if (++j == FX_SIDE_N)
                j = 0;
            ph = 0;
        } else {
            ph++;
        }
    }
    hr.i1 = i1;
    hr.i2 = i2;
    hr.i3 = i3;
    k = 2u * (w / 3u) + (w % 3u ? 1u : 0u);   /* the middle: the next sample's place (two every three frames) */
    hr.dn.ph = (uint8_t)(w % 3u);
    if (!from) {
        n = down_block(&hr.dn, m, p, made, &any);
    } else {                             /* (a part of a block: the same values a frame at a time) */
        for (n = 0, i = from; i < B; i++)
            n += (uint32_t)down_push(&hr.dn, m[i], &p[n]);
    }
    for (i = 0; i < n; i++) {
        if (k >= FX_MID_N)
            k -= FX_MID_N;
        hist_m[k++] = sat16(p[i]);
    }
    w += B - from;
    hist_w = w >= FX_HIST_N ? w - FX_HIST_N : w;
}

/* the buffer at place f16 (Q16 frames) as heard, times g (Q15): its middle and its sides, each linear between its
 * samples, through their low-passes (the punch-in's own) */
static PLAT_C2 void hist_play(uint32_t f16, int32_t g, int32_t *l, int32_t *r)
{
    /* (x / 3 and x / 6 by a multiply, exact for any 32-bit x: the FM-1's divider takes ~23 cycles) */
    uint32_t pm = (uint32_t)(((uint64_t)(2u * f16) * 0xAAAAAAABu) >> 33), jm = pm >> 16;
    uint32_t jm1 = jm + 1u == FX_MID_N ? 0u : jm + 1u, fs = f16 + FX_SIDE_AT, ps, js, js1;
    int32_t m = hist_m[jm], s;
    m += (int32_t)(((int64_t)(hist_m[jm1] - m) * (int32_t)(pm & 0xFFFFu)) >> 16);
    if (fs >= FX_HIST_N << 16)
        fs -= FX_HIST_N << 16;
    ps = (uint32_t)(((uint64_t)fs * 0xAAAAAAABu) >> 34);
    js = ps >> 16;
    js1 = js + 1u == FX_SIDE_N ? 0u : js + 1u;
    s = hist_s[js];
    s += (int32_t)(((int64_t)(hist_s[js1] - s) * (int32_t)(ps & 0xFFFFu)) >> 16);
    m = bq(&pu.um, mulq15(m, g));
    s = bq_sd(&pu.us, mulq15(s, g));
    *l = m + s;
    *r = m - s;
}

static PLAT_C2 uint32_t beat_frames(void) { return fx_wclk.beat_frames ? fx_wclk.beat_frames : 22050u; }

void fx_punch(int k, int down) { fx_ev(FX_EV_PUNCH, k, down, 0); }
void fx_punch_stop(void) { fx_ev(FX_EV_PUNCH_STOP, 0, 0, 0); }

static PLAT_C2 void punch_now(int k, int down, uint32_t b)   /* (b: a beat's frames) */
{
    if (!down) {                         /* let go (k -1: a command's rows are over: whichever runs) */
        if (pu.k < 0 || (k >= 0 && pu.k != k) || pu.out_on)
            return;
        if (pu.k == 15) {                /* vinyl: spins back up from what was heard last, landing in time */
            pu.up = 1;
            pu.t = 0;
            pu.rp = ((hist_w + FX_HIST_N - 2205u) % FX_HIST_N) << 16;
        } else {
            pu.out_on = 1;
            pu.out = 0;
            if (pu.k == 11) {            /* REVERB SWELL: the plate back with REVERB from its next block (fx_punches
                                          * fades the swell out meanwhile, or over 80 ms when REVERB is off) */
                rv_swell_size = -1;
                rv_hand.left = fx_w.on >> FX_REVERB & 1u || fx_w.forced & 2u ? FX_OUT_FADE : 0u;
            }
        }
        return;
    }
    if (k < 0 || k > 15)
        return;
    if (pu.k == 10 && dly_over.on) {     /* (replaced: ECHO THROW's echoes left in the line for DELAY) */
        dly_over.on = 0;
        dly_hand = fx_w.on >> FX_DELAY & 1u || fx_w.forced & 1u ? FX_OUT_FADE : 0u;
    }
    if (pu.k == 11 && rv_swell_size >= 0) {   /* (replaced: REVERB SWELL's plate to REVERB, as let go) */
        rv_swell_size = -1;
        rv_hand.left = fx_w.on >> FX_REVERB & 1u || fx_w.forced & 2u ? FX_OUT_FADE : 0u;
    }
    c2_memset(&pu, 0, sizeof pu);
    pu.k = (int8_t)k;
    if (k <= 3) {                        /* the loops: 1/4 .. 1/32 */
        static const uint8_t DIV[4] PLAT_C2_RO = {1, 2, 4, 8};
        pu.len = b / DIV[k];
    } else if (k == 4) {
        pu.len = b / 6u;                 /* stutter 1/16T */
    } else if (k == 12) {
        pu.len = b;                      /* reverse: the last beat */
    }
    if (pu.len) {
        pu.len = pu.len < 64u ? 64u : pu.len;
        while (pu.len > FX_HIST_SPAN)    /* a beat that does not fit: the longest note value that does */
            pu.len >>= 1;
        pu.start = (hist_w + 2u * FX_HIST_N - FX_HIST_LAG - pu.len) % FX_HIST_N;
        pu.keep = 1;                     /* the buffer keeps what it replays */
    }
    if (k == 12)
        pu.start = (hist_w + FX_HIST_N - FX_HIST_LAG) % FX_HIST_N;
    if (k >= 13) {
        pu.rp = ((hist_w + FX_HIST_N - 1u - FX_HIST_LAG) % FX_HIST_N) << 16;
        pu.speed = 65536;
    }
    if (k == 10) {                       /* echo throw: the DELAY's line, a dotted 1/8, feedback 65 % */
        uint32_t len = b / 2u;           /* (0.75 beat at 29.4 kHz) */
        dly_over.on = 1;
        dly_over.len = (int32_t)(len > FX_DLY_N / 2u - 2u ? FX_DLY_N / 2u - 2u : len);
        dly_over.fb = 21299;
        dly_over.k = FX_DLY_TONE[60];
        dly_hand = 0;
        dly_from = fx_w.on >> FX_DELAY & 1u || fx_w.forced & 1u ? (K(0xA3) > 100u ? 100 : K(0xA3)) * 32768 / 100 : 0;
    }
    if (k == 11) {                       /* (from REVERB's levels: its MIX, its dry in the chain, the mix fed to it) */
        int32_t on = fx_w.on >> FX_REVERB & 1u || fx_w.forced & 2u;
        int32_t mix = (K(0xA7) > 100u ? 100 : K(0xA7)) * 32768 / 100, chain = on && !fx_w.route;
        rv_swell_size = 95;
        rv_from.wet = on ? mix : 0;
        rv_from.dry = chain ? 32768 - mix * 2 / 5 : 32768;
        rv_from.in = chain ? 32768 : 0;
        rv_hand.left = 0;
        rv_hand.wet = rv_from.wet;
        rv_hand.dry = rv_from.dry;
    }
}

static PLAT_C2 void punch_stop(void)
{
    pu.k = -1;
    pu.keep = 0;
    dly_over.on = 0;
    rv_swell_size = -1;
    rv_hand.left = 0;
    dly_hand = 0;
}

/* one sample of the punch-in in place (at: the buffer's place after this frame, as if recorded frame by frame) */
static PLAT_C2 void pu_sample(int32_t *l, int32_t *r, uint32_t i, uint32_t at)
{
    uint32_t k = (uint32_t)pu.k, t = pu.t++, b = beat_frames();
    if (k <= 4u) {                       /* loops and the stutter: the last bit, again */
        uint32_t m = t % pu.len, j = (pu.start + m) % FX_HIST_N;
        int32_t fade = (int32_t)(m < 64u ? m : 64u);
        uint32_t back = pu.len - m;
        fade = (int32_t)back < fade ? (int32_t)back : fade;
        hist_play(j << 16, fade << 9, &l[i], &r[i]);
    } else if (k == 5u) {                /* bit crush: 8 steps a side, held 6 frames */
        if (t % 6u == 0u) {
            pu.hl = ((l[i] + (l[i] >= 0 ? 2048 : -2048)) / 4096) * 4096;
            pu.hr = ((r[i] + (r[i] >= 0 ? 2048 : -2048)) / 4096) * 4096;
        }
        l[i] = pu.hl;
        r[i] = pu.hr;
    } else if (k == 6u || k == 7u) {     /* gate 1/16, 1/32 */
        uint32_t len = b / (k == 6u ? 4u : 8u);
        int32_t open = (t % (len ? len : 1u)) < len / 2u ? 32768 : 0;
        pu.g += (open - pu.g) / 50;
        l[i] = mulq15(l[i], pu.g);
        r[i] = mulq15(r[i], pu.g);
    } else if (k == 8u || k == 9u) {     /* the sweeps, over a bar, faded in over 80 ms (no bump at the press) */
        uint32_t bar = 4u * b;
        int32_t x = (int32_t)(t >= bar ? 32768u : (uint32_t)(((uint64_t)t * (0xFFFFFFFFu / (bar ? bar : 1u))) >> 17));
        int32_t ix8 = k == 8u ? (243 << 8) - (int32_t)(((int64_t)(243 - 85) << 8) * x >> 15)   /* 18 kHz -> 200 Hz */
                              : (15 << 8) + (int32_t)(((int64_t)(196 - 15) << 8) * x >> 15);   /* 30 Hz -> 6 kHz */
        int32_t dl = l[i], dr = r[i];
        static svf_c_t c;
        if ((t & 15u) == 0u)             /* (the coefficients every 16 frames, q 2.5) */
            svf_coef(&c, g_at(ix8), 1638);
        l[i] = svf_run(&c, &pu.f[0], clamp(l[i], -200000, 200000), k == 9u);
        r[i] = svf_run(&c, &pu.f[1], clamp(r[i], -200000, 200000), k == 9u);
        if (t < FX_OUT_FADE) {           /* (t < 3528: in 32 bits) */
            int32_t g = (int32_t)((t << 15) / FX_OUT_FADE);
            l[i] = dl + mulq15(l[i] - dl, g);
            r[i] = dr + mulq15(r[i] - dr, g);
        }
    } else if (k == 12u) {               /* reverse: the last beat backwards */
        uint32_t j = (pu.start + 2u * FX_HIST_N - 1u - (t % pu.len)) % FX_HIST_N;
        hist_play(j << 16, 32768, &l[i], &r[i]);
    } else if (k >= 13u) {               /* half speed, tape stop, vinyl stop */
        int32_t g = 32768;
        if (k == 13u) {                  /* half speed: falls behind by half the time held; at the buffer's end,
                                          * again from the moment, a short fade over the seam */
            if (t == 0u || (t - pu.hs) / 2u >= FX_HIST_SPAN - 1u) {
                pu.up = t != 0u;         /* (a seam) */
                pu.hs = t;
                pu.w0 = (at + 2u * FX_HIST_N - 1u - FX_HIST_LAG) % FX_HIST_N;
            }
            {
                uint32_t u = t - pu.hs, left = FX_HIST_SPAN - 1u - u / 2u;
                pu.rp = ((pu.w0 + u / 2u) % FX_HIST_N) << 16 | (u & 1u ? 0x8000u : 0u);
                g = pu.up && u < 64u ? (int32_t)(u << 9) : 32768;
                g = left < 32u ? (int32_t)(left << 10) : g;
            }
        } else {
            if (k == 14u) {              /* tape stop: winds down over a beat, at most 1 s */
                uint32_t len = b < (uint32_t)SR ? b : (uint32_t)SR;
                pu.speed = t >= len ? 0 : (int32_t)(65536u - (t << 16) / len);   /* (t < 44100: in 32 bits) */
            } else if (pu.up) {          /* vinyl's spin-up: 0.1 s */
                pu.speed = t >= 4410u ? 65536 : (int32_t)((t << 16) / 4410u);
            } else {                     /* vinyl stop: 0.5 s */
                pu.speed = t >= 22050u ? 0 : (int32_t)(65536u - (t << 16) / 22050u);
            }
            pu.rp = (pu.rp + (uint32_t)pu.speed) % (FX_HIST_N << 16);
            g = pu.speed >= 6554 ? 32768 : pu.speed * 5;   /* (silence when stopped: x 10 of the speed) */
            if ((hist_w + 2u * FX_HIST_N - 1u - (pu.rp >> 16)) % FX_HIST_N + 1u > FX_HIST_N - 16u)
                g = 0;                   /* (fallen behind past what the buffer holds: a slow tape stop's last
                                          * moments, silent by then) */
        }
        hist_play(pu.rp, g, &l[i], &r[i]);
        if (k == 15u && pu.up && pu.speed >= 65536)
            pu.k = -1;                   /* spun back up: the song again */
    }
}

static PLAT_C2 void fx_punches(int32_t *l, int32_t *r)
{
    uint32_t i, b = beat_frames();
    int32_t sw = 0;
    if ((pu.k == 10 || pu.k == 11) && fx_act >> (pu.k == 10 ? FX_DELAY : FX_REVERB) & 1u)
        pu.k = -1;                       /* (the chain ran the line / the plate in this block: it has them back, the
                                          * swell let go or a setup taken; never run twice in a block) */
    if (pu.k == 10 || pu.k == 11) {      /* ECHO THROW, REVERB SWELL: the mix into the DELAY / the plate */
        if (pu.k == 11) {
            uint32_t span = 2u * b;
            sw = pu.t >= span ? 32768 : (int32_t)(((uint64_t)pu.t * (0xFFFFFFFFu / (span ? span : 1u))) >> 17);
            if (pu.out_on)
                sw = pu.out >= FX_OUT_FADE ? 0 : mulq15(sw, 32768 - (int32_t)((pu.out << 15) / FX_OUT_FADE));
            /* (from REVERB's levels to the swell's, wet 1.2 and dry 0.5: where REVERB takes the plate back from) */
            rv_hand.wet = rv_from.wet + mulq15(39322 - rv_from.wet, sw);
            rv_hand.dry = rv_from.dry + mulq15(16384 - rv_from.dry, sw);
        }
        for (i = 0; i < B; i++)
            pu_send[i] = pu.out_on ? 0 : (l[i] + r[i]) >> 1;
        if (pu.k == 10) {
            int32_t dl[B], dr[B];
            int32_t m = pu.t >= FX_OUT_FADE ? 26214 : dly_from + (26214 - dly_from) * (int32_t)pu.t / (int32_t)FX_OUT_FADE;
            c2_memset(dl, 0, sizeof dl);
            c2_memset(dr, 0, sizeof dr);
            fx_delay(dl, dr, 1, 0, m, pu_send);   /* (x 0.8, from DELAY's MIX) */
            for (i = 0; i < B; i++) {
                l[i] += dl[i];
                r[i] += dr[i];
            }
        } else {                         /* (the plate a block at a time: a sample at a time each paid its run's
                                          * setting up) */
            int32_t xs[B], yl[B], yr[B];
            volatile int32_t gin = rv_from.in + mulq15(32768 - rv_from.in, sw), wet = rv_hand.wet, dry = rv_hand.dry;
            for (i = 0; i < B; i++)      /* (the factors read in the loops: see fx_reverb) */
                xs[i] = mulq15(pu_send[i], gin);
            rv_block(xs, 95, 20, 0, yl, yr);
            for (i = 0; i < B; i++) {
                l[i] = mulq15(l[i], dry) + mulq15(yl[i], wet);
                r[i] = mulq15(r[i], dry) + mulq15(yr[i], wet);
            }
        }
        pu.t += B;
        if (pu.out_on) {
            pu.out += B;
            if (pu.k == 10 && pu.out >= FX_OUT_FADE * 30u) {   /* (the echoes rung out: the line back with DELAY, its
                                                                * MIX gliding in from the throw's) */
                pu.k = -1;
                dly_over.on = 0;
                dly_hand = fx_w.on >> FX_DELAY & 1u || fx_w.forced & 1u ? FX_OUT_FADE : 0u;
            } else if (pu.k == 11 && pu.out >= FX_OUT_FADE) {   /* (faded out with no REVERB taking the plate: its
                                                                 * tail emptied, so none comes back with REVERB) */
                pu.k = -1;
                rv_hand.left = 0;
                rv_clear();
                rv_asleep = 1;
                rv_qrun = 0;
            }
        }
    }
    if (pu.k < 0 || pu.k == 10 || pu.k == 11) {   /* (none replays the buffer: it only records, a block at a time) */
        if (pu.k < 0 || !pu.keep) {
            int32_t any = 0;
            for (i = 0; i < B; i++)
                any |= l[i] | r[i];
            if (!any && hist_zrun >= FX_HIST_QUIET) {   /* (it holds only 0s: 0s more change nothing) */
                if ((hist_w += B) >= FX_HIST_N)
                    hist_w -= FX_HIST_N;
            } else {
                if (!any && hist_zrun >= FX_HIST_CLR)
                    c2_memset(&hr, 0, sizeof hr);   /* (fed 0s a while: its converters emptied, so 0s give 0s) */
                hist_rec(l, r, 0);
            }
            hist_zrun = any ? 0u : zrun_on(hist_zrun, B);
        }
        if (pu.k >= 0)
            fx_act |= 1u << 15;
        return;
    }
    if (!pu.keep) {                      /* the buffer records whenever anything plays (the block first: a replay
                                          * reads FX_HIST_LAG frames behind each frame at least, never what the
                                          * block adds after it) */
        hist_rec(l, r, 0);
        hist_zrun = 0;                   /* (a punch-in plays: counted afresh after it) */
    }
    for (i = 0; i < B; i++) {
        int32_t dl = l[i], dr = r[i];
        pu_sample(l, r, i, pu.keep ? hist_w : (hist_w + FX_HIST_N - (B - 1u - i)) % FX_HIST_N);
        if (pu.out_on && pu.k >= 0) {   /* let go: fading back into the mix */
            int32_t g = 32768 - (int32_t)((pu.out << 15) / FX_OUT_FADE);   /* (out < 3528: in 32 bits) */
            l[i] = dl + mulq15(l[i] - dl, g);
            r[i] = dr + mulq15(r[i] - dr, g);
            if (++pu.out >= FX_OUT_FADE)
                pu.k = -1;
        }
        if (pu.k < 0) {                  /* over: the rest of the block as it is, recorded if it was not */
            if (pu.keep && i + 1u < B) {
                hist_rec(l, r, i + 1u);
                hist_zrun = 0;
            }
            break;
        }
    }
    if (pu.k >= 0)
        fx_act |= 1u << 15;
}

/* ---- all of it ---- */
static uint32_t kill_left;               /* all sound off: the tails fading */

static PLAT_C2 void clear_lines(void)
{
    c2_memset(cho_l, 0, sizeof cho_l);
    c2_memset(cho_r, 0, sizeof cho_r);
    c2_memset(dly_buf, 0, sizeof dly_buf);
    dly_a = dly_b1 = dly_b2 = 0;
    c2_memset(&dly_dn, 0, sizeof dly_dn);
    c2_memset(&dly_upl, 0, sizeof dly_upl);
    c2_memset(&dly_upr, 0, sizeof dly_upr);
    c2_memset(rv_buf, 0, sizeof rv_buf);
    c2_memset(rv_pre, 0, sizeof rv_pre);
    rv_bw = rv_dl = rv_dr = 0;
    c2_memset(&rv_dn, 0, sizeof rv_dn);
    c2_memset(&rv_upl, 0, sizeof rv_upl);
    c2_memset(&rv_upr, 0, sizeof rv_upr);
    c2_memset(hist_m, 0, sizeof hist_m);
    c2_memset(hist_s, 0, sizeof hist_s);
    c2_memset(&hr, 0, sizeof hr);
    cho_zrun = hist_zrun = FX_ZRUN_MOST;   /* (all 0 now: asleep till something comes in) */
    dly_asleep = rv_asleep = 1;
    dly_qrun = rv_qrun = 0;
    punch_stop();
}

void fx_kill(void) { fx_ev(FX_EV_KILL, 0, 0, 0); }
void fx_empty(void) { fx_ev(FX_EV_EMPTY, 0, 0, 0); }

static PLAT_C2 void empty_now(void)
{
    clear_lines();
    c2_memset(ins.filt_s, 0, sizeof ins.filt_s);
    ins.filt_env.stage = 0, ins.filt_env.v = ins.filt_env.open = 0;
    ins.dr_hp[0] = ins.dr_hp[1] = ins.dr_lp[0] = ins.dr_lp[1] = 0;
    ins.lofi_h[0] = ins.lofi_h[1] = 0;
    ins.gate_g = 1 << 24;
    lim_g = 32768;
    met.n = FX_MET_LEN;
    kill_left = 0;
}

void fx_reset(void)
{
    PLAT_FAR(clear_lines)();             /* (in core 2's room) */
    rv_layout();
    rv_s = 0;
    rv_c = 1 << 30;
    ins.gate_g = 1 << 24;
    ins.filt_mode = 0;
    ins.filt_ix8 = 255 << 8;
    memset(ins.filt_s, 0, sizeof ins.filt_s);
    memset(&ins.filt_env, 0, sizeof ins.filt_env);
    ins.dr_hp[0] = ins.dr_hp[1] = ins.dr_lp[0] = ins.dr_lp[1] = 0;
    lim_g = 32768;
    met.n = FX_MET_LEN;
    kill_left = 0;
    rnd_state = strip_rnd_state = 0x2545F491u;
}

void fx_setup(const uint8_t *s)
{
    uint32_t i;
    if (!s)
        s = DEFAULT_SETUP;
    fx_route = s[0] ? 1u : 0u;
    fx_nchain = 0;
    for (i = 0; i < 12u; i++) {
        uint32_t v = s[1 + i / 2u] >> (4u * (i % 2u)) & 15u;
        if (v == 15u)
            break;
        fx_chain[fx_nchain++] = (uint8_t)v;
    }
    fx_on = (uint16_t)(s[7] | s[8] << 8);
    memcpy(fx_met, s + 9, 2);
    fx_met[2] = s[11];
    fx_met[3] = s[12];
    fx_met[4] = s[13];
    memcpy(fx_kn, s + 16, 48);
    set_changed();
    fx_ev(FX_EV_SETUP, 0, 0, 0);         /* (the settings at once: the player reads them; the punch-ins' at the block) */
}

void fx_setup_keep(const uint8_t *s, uint32_t e, uint64_t codes)
{
    uint8_t kn[48], met[5];
    uint16_t on = fx_on;
    uint32_t i;
    memcpy(kn, fx_kn, 48);
    memcpy(met, fx_met, 5);
    fx_setup(s);
    if (e >= FX_N)
        return;
    for (i = 0; i < 48u; i++)
        if (codes >> i & 1u)
            fx_kn[i] = kn[i];
    fx_on = (uint16_t)((fx_on & ~(1u << e)) | (on & 1u << e));
    if (e == FX_METRONOME)
        memcpy(fx_met, met, 5);
    set_changed();
}

void fx_cmd(uint8_t code, uint8_t value)
{
    if (code == 0xAEu || code == 0xAFu) {   /* FX ON / OFF: the value the effect's number */
        if (value < FX_N) {
            fx_on = (uint16_t)(code == 0xAEu ? fx_on | 1u << value : fx_on & ~(1u << value));
            set_changed();
        }
        return;
    }
    if (code >= 0x90u && code < 0xC0u) {
        fx_kn[code - 0x90u] = value;     /* (a value past its range acts as its top: each effect clamps) */
        set_changed();
    }
}

int fx_sends(void) { return fx_route; }

static uint8_t fx_byp;                   /* BYPASS held */
static uint8_t fx_forced;                /* DJ's ECHO OUT (1: the DELAY), REVERB OUT (2: the REVERB): run though off */
void fx_force(int delay, int reverb)
{
    uint8_t f = (uint8_t)((delay ? 1u : 0u) | (reverb ? 2u : 0u));
    if (f == fx_forced)                  /* (every DJ block: a new generation only when it changes) */
        return;
    fx_forced = f;
    set_changed();
}
void fx_bypass(int on)
{
    fx_byp = (uint8_t)(on != 0);
    set_changed();
}

void fx_now(uint8_t *s)
{
    uint32_t i;
    memset(s, 0, 64);
    s[0] = fx_route;
    for (i = 0; i < 12u; i++)
        s[1 + i / 2u] |= (uint8_t)((i < fx_nchain ? fx_chain[i] : 15u) << (4u * (i % 2u)));
    s[7] = (uint8_t)fx_on;
    s[8] = (uint8_t)(fx_on >> 8);
    memcpy(s + 9, fx_met, 5);
    memcpy(s + 16, fx_kn, 48);
}

void fx_gate_pattern(int i, const uint8_t *steps)
{
    uint32_t k;
    if (i < 16 || i > 31)
        return;
    for (k = 0; k < 16u; k++)
        gate_pat[i][k] = (uint8_t)(steps[k] > 4u ? 4u : steps[k]);
}

void fx_gate_steps(int i, uint8_t *steps)
{
    memcpy(steps, gate_pat[i & 31], 16);
}

void fx_duck(fx_duck_t *d)               /* (the voices' stage: the live settings) */
{
    uint8_t src = fx_kn[0xAA - 0x90], dep = fx_kn[0xAB - 0x90], rel = fx_kn[0xAC - 0x90];
    d->on = (uint8_t)(fx_on >> FX_DUCK & 1u && !fx_byp);
    d->src = (uint8_t)(src > FX_DUCK_KEYS ? FX_DUCK_KEYS : src);
    d->depth_q15 = (dep > 100u ? 100 : dep) * 32768 / 100;
    d->release_frames = (20u + 10u * (rel > 98u ? 98u : rel)) * 441u / 10u;
}

uint32_t fx_active(void) { return fx_act; }
uint32_t fx_asleep(void)
{
    return (cho_zrun >= FX_CHO_N ? 1u << FX_CHORUS : 0u) | (dly_asleep ? 1u << FX_DELAY : 0u) |
           (rv_asleep ? 1u << FX_REVERB : 0u);
}
uint8_t fx_gate_step(void) { return ins.gate_step; }
int fx_met_beat(void) { return fx_on >> FX_METRONOME & 1u ? met_shown : -1; }   /* (off: its blocks may rest) */
int32_t fx_limiter_gain(void)
{
    int32_t g = lim_least;
    lim_least = 32768;
    return g;
}

uint32_t fx_ram(void)
{
    return (uint32_t)(sizeof cho_l + sizeof cho_r + sizeof dly_buf + sizeof rv_buf + sizeof rv_pre + sizeof hist_m +
                      sizeof hist_s);
}

void fx_init(void)
{
    uint32_t i;
    fx_sin = dx7_table(0);               /* (the LFOs' sine: read in place, never called) */
    for (i = 0; i < 32u; i++)
        memcpy(gate_pat[i], FX_GATE_PAT[i], 16);
    fx_setup(0);
    fx_reset();
    fx_set_get(&fx_w);                   /* (the working copy as the settings, till a block puts its own) */
}

/* ---- FX EDIT's activity meters (specs/ui/screens/fx.md#edit): how hard each effect works, Q15 of full scale (the
 * mix's 16-bit scale), measured while EDIT shows (fx_watch) on one block in 4: one at work |RMS after - RMS before| x 4
 * + RMS before / 2 (at most full), held to its peak, falling x 0.85 a measured block; a send its change x 3, falling
 * x 0.8; one off (or its line a punch-in's) falling x 0.8. Core 2's; the screen reads them as they are ---- */
static volatile uint8_t fx_watch_on;
static uint8_t fx_look;                  /* (this block is measured) */
static uint32_t fx_wblk;
static int32_t fx_meter[FX_N];
void fx_watch(int on) { fx_watch_on = (uint8_t)(on != 0); }
int32_t fx_meter_q15(int e) { return e >= 0 && e < (int)FX_N ? fx_meter[e] : 0; }
static PLAT_C2 void meter_look(void) { fx_look = fx_watch_on && !(fx_wblk++ & 3u); }
static PLAT_C2 __attribute__((noinline)) int32_t blk_rms(const int32_t *l, const int32_t *r)   /* every 4th frame's,
                                          * both sides, in steps of 32 (a bar's pixel is 1.7 %), all in 32 bits (one
                                          * copy: measuring is rare) */
{
    uint32_t i, s = 0, y = 0, bit = 1u << 30;
    for (i = 0; i < B; i += 4u) {
        int32_t a = clamp(l[i] >> 5, -8191, 8191), b = clamp(r[i] >> 5, -8191, 8191);
        s += (uint32_t)(a * a + b * b);  /* (B / 2 squares of 8191 at most: under 2^32) */
    }
    s >>= 5;                             /* (their mean) */
    while (bit > s)
        bit >>= 2;
    for (; bit; bit >>= 2) {             /* (its square root, bit by bit) */
        if (s >= y + bit) {
            s -= y + bit;
            y = (y >> 1) + bit;
        } else {
            y >>= 1;
        }
    }
    return (int32_t)(y << 5);
}
static PLAT_C2 __attribute__((noinline)) void meter_fall(uint32_t e, int32_t q15)
{
    fx_meter[e] = (int32_t)(((int64_t)fx_meter[e] * q15) >> 15);
}
static PLAT_C2 __attribute__((noinline)) void meter_put(uint32_t e, int32_t before, int32_t after, int send)
{
    int32_t d = after > before ? after - before : before - after, v;
    if (send) {
        v = d * 3;
        meter_fall(e, 26214);
    } else {
        v = d * 4 + before / 2;
        v = v > 32768 ? 32768 : v;
        meter_fall(e, 27853);
    }
    if (v > fx_meter[e])
        fx_meter[e] = v;
}
static PLAT_C2 int line_taken(uint32_t e) { return (e == FX_DELAY && dly_over.on) || (e == FX_REVERB && rv_swell_size >= 0); }
/* a silent block at rest, measured: every meter falls as a measured block of silence makes it */
static PLAT_C2 __attribute__((noinline)) void meter_rest(void)
{
    uint32_t n, e;
    for (n = 0; n < (fx_w.byp ? 0u : fx_w.nchain); n++) {
        e = fx_w.chain[n];
        if (e == FX_SENDS) {
            for (e = FX_CHORUS; fx_w.route && e <= FX_REVERB; e++)
                if (fx_w.on >> e & 1u && !line_taken(e))
                    meter_fall(e, 26214);
        } else if (e < FX_N && !(fx_w.route && e >= FX_CHORUS && e <= FX_REVERB)) {
            meter_fall(e, fx_w.on >> e & 1u && !line_taken(e) ? 27853 : 26214);
        }
    }
    meter_fall(FX_METRONOME, fx_w.on >> FX_METRONOME & 1u ? 27853 : 26214);
}

/* one element of the chain on a block (l, r; sends: SENDS routing's three, or NULL) */
static PLAT_C2 void fx_el(uint32_t e, int32_t *l, int32_t *r, const int32_t *sends)
{
    uint32_t on = e < FX_N && (fx_w.on >> e & 1u), t0;
    if (e == FX_SENDS) {                 /* the sends' returns join here */
        if (!fx_w.route || !sends)
            return;
        int32_t was = fx_look ? blk_rms(l, r) : 0, now;   /* (the meters: each send's change) */
        t0 = plat_c2_us();
        if (fx_w.on >> FX_CHORUS & 1u) {
            fx_chorus(l, r, 1, sends);
            fx_time(FX_CHORUS, t0);
            if (fx_look) {
                now = blk_rms(l, r);
                meter_put(FX_CHORUS, was, now, 1);
                was = now;
            }
        }
        t0 = plat_c2_us();
        if ((fx_w.on >> FX_DELAY & 1u || fx_w.forced & 1u) && !dly_over.on) {
            fx_delay(l, r, 1, sends + B, 0, 0);
            fx_time(FX_DELAY, t0);
            if (fx_look) {
                now = blk_rms(l, r);
                meter_put(FX_DELAY, was, now, 1);
                was = now;
            }
        }
        t0 = plat_c2_us();
        if ((fx_w.on >> FX_REVERB & 1u || fx_w.forced & 2u) && rv_swell_size < 0) {
            fx_reverb(l, r, 1, sends + 2u * B);
            fx_time(FX_REVERB, t0);
            if (fx_look)
                meter_put(FX_REVERB, was, blk_rms(l, r), 1);
        }
        return;
    }
    if (fx_w.route && (e == FX_CHORUS || e == FX_DELAY || e == FX_REVERB))
        return;
    if (e == FX_GATE) {                  /* (switched off it glides back open first) */
        if (on || ins.gate_g != 1 << 24) {
            t0 = plat_c2_us();
            fx_gate(&ins, fx_w.kn, &fx_wclk, l, r, (int)on);
            fx_act |= on ? 1u << FX_GATE : 0u;
            fx_time(FX_GATE, t0);
        }
        return;
    }
    if (e == FX_FILTER) {
        if (on || ins.filt_mode) {
            t0 = plat_c2_us();
            fx_act |= (uint32_t)fx_filter(&ins, fx_w.kn, &fx_wclk, l, r, (int)on) << FX_FILTER;
            fx_time(FX_FILTER, t0);
        }
        return;
    }
    if (!on || e >= FX_N)
        return;
    t0 = plat_c2_us();
    switch (e) {
    case FX_DRIVE: fx_drive(&ins, fx_w.kn, l, r); fx_act |= 1u << FX_DRIVE; break;
    case FX_LOFI: fx_lofi(&ins, fx_w.kn, &rnd_state, l, r); fx_act |= 1u << FX_LOFI; break;
    case FX_CHORUS: fx_chorus(l, r, 0, 0); break;
    case FX_DELAY: if (!dly_over.on) fx_delay(l, r, 0, 0, 0, 0); break;
    case FX_REVERB: if (rv_swell_size < 0) fx_reverb(l, r, 0, 0); break;
    case FX_LIMITER: fx_limit(l, r); break;
    default: break;
    }
    fx_time(e, t0);
}

/* ... measured for its meter on a measured block (SENDS: its three in fx_el) */
static PLAT_C2 void fx_el_m(uint32_t e, int32_t *l, int32_t *r, const int32_t *sends)
{
    int32_t was = 0;
    int m = fx_look && e < FX_N && !(fx_w.route && e >= FX_CHORUS && e <= FX_REVERB);
    if (__builtin_expect(m, 0)) {        /* (measured: one block in 4, while EDIT shows) */
        if (!(fx_w.on >> e & 1u) || line_taken(e)) {
            meter_fall(e, 26214);        /* (off, or its line a punch-in's; GATE and FILTER still glide open) */
            m = 0;
        } else {
            was = blk_rms(l, r);
        }
    }
    fx_el(e, l, r, sends);               /* (one call: one copy of it) */
    if (__builtin_expect(m, 0))
        meter_put(e, was, blk_rms(l, r), 0);
}

static PLAT_C2 void fx_split(const int32_t *mix, int32_t *l, int32_t *r)   /* a block's frames into its two sides */
{
    uint32_t i;
    for (i = 0; i < B; i += 4u) {        /* (four frames a turn: here a turn costs as much as its work) */
        l[i] = mix[2u * i];
        r[i] = mix[2u * i + 1u];
        l[i + 1u] = mix[2u * i + 2u];
        r[i + 1u] = mix[2u * i + 3u];
        l[i + 2u] = mix[2u * i + 4u];
        r[i + 2u] = mix[2u * i + 5u];
        l[i + 3u] = mix[2u * i + 6u];
        r[i + 3u] = mix[2u * i + 7u];
    }
}
static PLAT_C2 void fx_join(int32_t *mix, const int32_t *l, const int32_t *r)   /* and back */
{
    uint32_t i;
    for (i = 0; i < B; i += 4u) {
        mix[2u * i] = l[i];
        mix[2u * i + 1u] = r[i];
        mix[2u * i + 2u] = l[i + 1u];
        mix[2u * i + 3u] = r[i + 1u];
        mix[2u * i + 4u] = l[i + 2u];
        mix[2u * i + 5u] = r[i + 2u];
        mix[2u * i + 6u] = l[i + 3u];
        mix[2u * i + 7u] = r[i + 3u];
    }
}

PLAT_C2 void fx_block(int32_t *mix, const int32_t *sends)
{
    int32_t l[B], r[B];
    uint32_t i, n, nch = fx_w.byp ? 0u : fx_w.nchain;   /* (BYPASS: the chain off) */
    fx_act = 0;
    met_out_on = 0;                      /* (fx_metronome says when it adds) */
    meter_look();
    fx_split(mix, l, r);
    for (n = 0; n < nch; n++)
        fx_el_m(fx_w.chain[n], l, r, sends);
    {
        uint32_t t0 = plat_c2_us(), mon = fx_w.on >> FX_METRONOME & 1u;
        int32_t was;
        fx_punches(l, r);
        fx_time(10u, t0);
        was = fx_look ? blk_rms(l, r) : 0;
        if (!mon)
            met_shown = -1;              /* (off: it does not count) */
        if (mon || met.n < FX_MET_LEN) { /* (off: a count-in's click rings out) */
            t0 = plat_c2_us();
            fx_metronome(l, r, (int)mon);
            fx_time(FX_METRONOME, t0);
            if (mon)
                fx_act |= 1u << FX_METRONOME;   /* (on: at work, as the state and FIGURES say) */
        }
        if (fx_look && mon)              /* (its meter: what its clicks add, as a chain effect's) */
            meter_put(FX_METRONOME, was, blk_rms(l, r), 0);
        else if (fx_look)
            meter_fall(FX_METRONOME, 26214);
    }
    if (kill_left) {                     /* all sound off: the tails fade out, then the lines are cleared */
        for (i = 0; i < B; i++) {        /* (FX_KILL is 128) */
            int32_t g = kill_left > i ? (int32_t)(kill_left - i) : 0;
            l[i] = (int32_t)(((int64_t)l[i] * g) >> 7);
            r[i] = (int32_t)(((int64_t)r[i] * g) >> 7);
        }
        kill_left = kill_left > B ? kill_left - B : 0u;
        if (!kill_left)
            clear_lines();
    }
    fx_join(mix, l, r);
}

/* ---- the settings live and working, a half's blocks at once (fx.h) ---- */
void fx_set_get(fx_set_t *s)
{
    if (s->seq == fx_set_seq)            /* (it holds them already) */
        return;
    s->seq = fx_set_seq;
    s->on = fx_on;
    s->route = fx_route;
    s->nchain = fx_nchain;
    memcpy(s->chain, fx_chain, sizeof s->chain);
    memcpy(s->met, fx_met, sizeof s->met);
    memcpy(s->kn, fx_kn, sizeof s->kn);
    s->forced = fx_forced;
    s->byp = fx_byp;
}
PLAT_C2 void fx_work(const fx_set_t *s, const fx_clock_t *clk)
{
    if (s->seq != fx_w.seq || !s->seq)   /* (not these already; a copy that never had a generation, copied) */
        c2_memcpy(&fx_w, s, sizeof fx_w);
    fx_wclk.beat = clk->beat;            /* (the clock field by field: no call, every block and effect of fx_blocks) */
    fx_wclk.frac = clk->frac;
    fx_wclk.inc = clk->inc;
    fx_wclk.beat_frames = clk->beat_frames;
    fx_wclk.hold = clk->hold;
    fx_wclk.playing = clk->playing;
    fx_wclk.rpb = clk->rpb;
    fx_wclk.keys = clk->keys;
    fx_wclk.fire = clk->fire;
    fx_wclk.held = clk->held;
}

/* a change to the effects' own state, made now: as a block's events make them (fx_events) and as the commands do
 * when nothing is kept (fx_ev) */
PLAT_C2 void fx_ev_now(uint32_t kind, int a, int b, int c, uint32_t bf)
{
    switch (kind) {
    case FX_EV_CLICK:
        met_click(c);
        met.sound = (int8_t)(a & 3);
        met.level = (int8_t)(b < 0 ? 0 : b > 100 ? 100 : b);
        break;
    case FX_EV_PUNCH: punch_now(a, b, bf ? bf : 22050u); break;   /* (the beat it was pressed in) */
    case FX_EV_PUNCH_STOP: punch_stop(); break;
    case FX_EV_KILL:
        if (!kill_left)
            kill_left = FX_KILL;
        break;
    case FX_EV_EMPTY: empty_now(); break;
    case FX_EV_NOISE: rnd_state = strip_rnd_state; break;
    case FX_EV_SETUP:                    /* (a setup's punch-ins over: REVERB SWELL as let go, its plate gliding into the
                                          * setup's REVERB; ECHO THROW at once, its echoes in the line for the setup's
                                          * DELAY) */
        if (pu.k == 11 && !pu.out_on)
            punch_now(11, 0, 0);
        if (pu.k == 10)
            pu.k = -1;
        dly_over.on = 0;
        rv_swell_size = -1;
        break;
    default:
        break;
    }
}
/* a change to the effects' own state: kept with the block being made (fx_defer), else made now; the queue full, made
 * now once the effects' work handed over is done (plat_c2_wait), the half's blocks then taken one at a time
 * (fx_blocks_ok) */
static void fx_ev(uint32_t kind, int a, int b, int c)
{
    if (ev_block >= 0) {
        if (ev_n < FX_EVQ) {
            ev_q[ev_n].kind = (uint8_t)kind;
            ev_q[ev_n].a = (int8_t)a;
            ev_q[ev_n].b = (int8_t)b;
            ev_q[ev_n].c = (int8_t)c;
            ev_q[ev_n].bf = fx_clk.beat_frames;
            ev_end[ev_block] = ++ev_n;
            return;
        }
        ev_lost = 1;                     /* (full: made at once, as before the queue) */
        plat_c2_wait();
    }
    PLAT_FAR(fx_ev_now)(kind, a, b, c, fx_clk.beat_frames);   /* (in core 2's room) */
}
void fx_defer(int block)
{
    ev_block = block;
    if (block < 0)
        return;
    if (!block)                          /* (a half's first block: the half before's all made) */
        ev_n = ev_lost = 0;
    ev_at[block] = ev_end[block] = ev_n;
}
PLAT_C2 void fx_events(int block)
{
    uint32_t i;
    for (i = ev_at[block]; i < ev_end[block]; i++)
        fx_ev_now(ev_q[i].kind, ev_q[i].a, ev_q[i].b, ev_q[i].c, ev_q[i].bf);
}
int fx_blocks_ok(uint32_t n, const fx_set_t set[])
{
    uint32_t j, i;
    if (ev_lost || kill_left || met.n < FX_MET_LEN || n > FX_HALF_MOST)
        return 0;
    for (i = 0; i < ev_n; i++)           /* (only punch-ins that leave the DELAY and the plate where they are: none
                                          * starting ECHO THROW or REVERB SWELL, letting REVERB SWELL go, or pressed
                                          * over either: they take them from the chain, or give them back) */
        if (ev_q[i].kind != FX_EV_PUNCH ||
            (ev_q[i].b ? ev_q[i].a == 10 || ev_q[i].a == 11 || pu.k == 10 || pu.k == 11 : pu.k == 11))
            return 0;
    if (pu.k == 10 && pu.out_on && pu.out + n * B >= FX_OUT_FADE * 30u)
        return 0;                        /* (ECHO THROW ends: the chain takes the DELAY back) */
    for (j = 0; j < n; j++) {
        if (set[j].on >> FX_METRONOME & 1u)
            return 0;
        if (set[j].route != set[0].route || set[j].nchain != set[0].nchain || set[j].byp != set[0].byp ||
            memcmp(set[j].chain, set[0].chain, sizeof set[0].chain))
            return 0;
    }
    return 1;
}
PLAT_C2 void fx_blocks(int32_t *const mix[], const int32_t *const sends[], uint32_t n, const fx_clock_t clk[],
                       const fx_set_t set[])
{
    uint32_t j, k, act[FX_HALF_MOST];
    uint8_t look[FX_HALF_MOST];
    for (j = 0; j < n; j++) {            /* (each block's sides apart in its own place: l its first half, r its second;
                                          * r aside, l moved down, r after it) */
        int32_t t[B], *m = mix[j];
        uint32_t i;
        for (i = 0; i < B; i += 4u) {    /* (four a turn, as fx_split; l's moved down in order: each read before its
                                          * place is written) */
            t[i] = m[2u * i + 1u];
            t[i + 1u] = m[2u * i + 3u];
            t[i + 2u] = m[2u * i + 5u];
            t[i + 3u] = m[2u * i + 7u];
        }
        for (i = 0; i < B; i += 4u) {
            m[i] = m[2u * i];
            m[i + 1u] = m[2u * i + 2u];
            m[i + 2u] = m[2u * i + 4u];
            m[i + 3u] = m[2u * i + 6u];
        }
        c2_memcpy(m + B, t, sizeof t);
        act[j] = 0;
        meter_look();                    /* (each block measured or not, in order) */
        look[j] = fx_look;
    }
    for (k = 0; k < (set[0].byp ? 0u : set[0].nchain); k++)   /* each effect over the blocks in turn (the settings
                                          * copied only where they differ: their generations) */
        for (j = 0; j < n; j++) {
            fx_work(&set[j], &clk[j]);
            fx_act = act[j];
            fx_look = look[j];
            fx_el_m(set[0].chain[k], mix[j], mix[j] + B, sends[j]);
            act[j] = fx_act;
        }
    for (j = 0; j < n; j++)              /* (the METRONOME off in every block here: its meter falling) */
        if (look[j])
            meter_fall(FX_METRONOME, 26214);
    for (j = 0; j < n; j++) {            /* the punch-ins (those pressed in a block: there) */
        uint32_t t0;
        fx_work(&set[j], &clk[j]);
        fx_events((int)j);
        fx_act = act[j];
        t0 = plat_c2_us();
        fx_punches(mix[j], mix[j] + B);
        fx_time(10u, t0);
        act[j] = fx_act;
    }
    for (j = 0; j < n; j++) {            /* (back together: r aside, l moved up from the top, r between) */
        int32_t t[B], *m = mix[j];
        uint32_t i;
        c2_memcpy(t, m + B, sizeof t);
        for (i = B; i > 0u;) {           /* (l's moved up from the top: each read before its place is written) */
            i -= 4u;
            m[2u * i + 6u] = m[i + 3u];
            m[2u * i + 4u] = m[i + 2u];
            m[2u * i + 2u] = m[i + 1u];
            m[2u * i] = m[i];
        }
        for (i = 0; i < B; i += 4u) {
            m[2u * i + 1u] = t[i];
            m[2u * i + 3u] = t[i + 1u];
            m[2u * i + 5u] = t[i + 2u];
            m[2u * i + 7u] = t[i + 3u];
        }
    }
    fx_act = act[n - 1u];
    met_out_on = 0;
}

/* the METRONOME on, its own TEMPO's step a block (0: FOLLOW), and whether fx_metronome starts a click in this block
 * (its own tests, nothing moved) */
static PLAT_C2 uint32_t met_inc(void)   /* its place's step a block: its own TEMPO's, or FOLLOW's on THE KEYS */
{
    uint32_t bpm = fx_w.met[0] | (uint32_t)fx_w.met[1] << 8;
    return bpm ? (bpm * 1623u + bpm / 5u) * B : !fx_wclk.playing && fx_wclk.keys ? fx_wclk.inc * B : 0u;
}
static PLAT_C2 int met_due(void)
{
    uint32_t inc = met_inc(), nf;
    if (inc)
        return met_own + inc < met_own;
    if (!fx_wclk.playing)
        return 0;
    nf = fx_wclk.frac + fx_wclk.inc * (fx_wclk.hold < B ? fx_wclk.hold : B);
    return fx_wclk.frac == 0u || nf < fx_wclk.frac;
}

/* A block of 0s at rest (the player's silent block: no voice, nothing on the buses), as fx_block takes it when every
 * effect at work rests: the CHORUS, DELAY and REVERB asleep with their converters still (their clocks and places
 * on, as their own rest paths move them), the LIMITER fully open, the punch-ins' buffer holding only 0s (its place
 * on), the METRONOME (on) with no click due in this block (its own TEMPO's place on); nothing else at work (GATE,
 * FILTER, DRIVE, LO-FI, a click ringing, a punch-in, all sound off fading, an effect forced). The same state as
 * fx_block's after the same block, without a frame looked at. */
PLAT_C2 int fx_rest_block(void)
{
    uint32_t n, rest = 0, met_on = fx_w.on >> FX_METRONOME & 1u;
    if (kill_left || pu.k >= 0 || hist_zrun < FX_HIST_QUIET || fx_w.forced || met.n < FX_MET_LEN ||
        (met_on && met_due()))
        return 0;
    for (n = 0; n < (fx_w.byp ? 0u : fx_w.nchain); n++) {
        uint32_t e = fx_w.chain[n], on = e < FX_N && (fx_w.on >> e & 1u);
        if (e == FX_SENDS) {
            if (!fx_w.route)
                continue;
            rest |= (fx_w.on & (1u << FX_CHORUS)) | (!dly_over.on ? fx_w.on & (1u << FX_DELAY) : 0u) |
                    (rv_swell_size < 0 ? fx_w.on & (1u << FX_REVERB) : 0u);
            continue;
        }
        if (fx_w.route && (e == FX_CHORUS || e == FX_DELAY || e == FX_REVERB))
            continue;
        if (e == FX_GATE || e == FX_FILTER) {
            if (on || (e == FX_GATE ? ins.gate_g != 1 << 24 : ins.filt_mode != 0))
                return 0;
            continue;
        }
        if (!on || e >= FX_N)
            continue;
        if (e == FX_CHORUS || (e == FX_DELAY && !dly_over.on) || (e == FX_REVERB && rv_swell_size < 0))
            rest |= 1u << e;
        else if (e == FX_LIMITER ? lim_g != 32768 : e != FX_DELAY && e != FX_REVERB)
            return 0;                    /* (a LIMITER coming back up, DRIVE, LO-FI: at work) */
    }
    if (((rest >> FX_CHORUS & 1u) && cho_zrun < FX_CHO_N) ||
        ((rest >> FX_DELAY & 1u) && !(dly_asleep && conv_still(&dly_dn, &dly_upl, &dly_upr))) ||
        ((rest >> FX_REVERB & 1u) && !(rv_asleep && conv_still(&rv_dn, &rv_upl, &rv_upr))))
        return 0;
    if (rest >> FX_CHORUS & 1u) {        /* (as fx_chorus asleep) */
        uint32_t mode = K(0x9B) > 2u ? 2u : K(0x9B);
        cho_w += B;
        cho_ph += FX_CHO_INC[mode][K(0x9C) > 100u ? 100u : K(0x9C)] * B;
    }
    if ((rest >> FX_DELAY & 1u) && (dly_w += conv_rest(&dly_dn, &dly_upl, &dly_upr)) >= FX_DLY_N)
        dly_w -= FX_DLY_N;               /* (as fx_delay at rest) */
    if (rest >> FX_REVERB & 1u)
        rv_idle(conv_rest(&rv_dn, &rv_upl, &rv_upr));   /* (as fx_reverb at rest) */
    if ((hist_w += B) >= FX_HIST_N)      /* (as fx_punches with only 0s: its place on) */
        hist_w -= FX_HIST_N;
    hist_zrun = zrun_on(hist_zrun, B);
    if (met_on)                          /* (as fx_metronome with no click: its own TEMPO's place on; at work, as the
                                          * state and FIGURES say) */
        met_own += met_inc();
    fx_act = rest | met_on << FX_METRONOME;
    met_out_on = 0;
    meter_look();
    if (fx_look)
        meter_rest();
    return 1;
}

/* ---- DJ: a DJ FILTER (fx_djf_t), and a deck's own effects (fx_strip_t) ---- */

/* a setting in 1/256 of a step as an FX_G_Q12 index (Q8), as FILTER's CUT steps go (FX_FILT_IX: LP's end
 * 0 .. OFF 64 .. HP's end 127; at OFF the side's open end: LP 255, HP 0) */
static PLAT_C2 int32_t djf_index(int32_t set_q8, int hp)
{
    int32_t x = set_q8 + (64 << 8), i0 = clamp(x >> 8, 0, 127), fr = x & 255, i1 = i0 < 127 ? i0 + 1 : 127;
    int32_t a = i0 == 64 ? (hp ? 0 : 255) : FX_FILT_IX[i0], b = i1 == 64 ? (hp ? 0 : 255) : FX_FILT_IX[i1];
    if (x >= 127 << 8)
        return FX_FILT_IX[127] << 8;
    return (a << 8) + (b - a) * fr;
}

/* a block of a DJ FILTER's setting: its side and its cutoff's glide; 0 when it is off (open) */
static PLAT_C2 int djf_follow(fx_djf_t *f, int32_t c)
{
    int32_t to;
    if (c < 0 && f->mode >= 0) {         /* (switching side: from open) */
        f->mode = -1;
        f->ix8 = 255 << 8;
        c2_memset(f->s, 0, sizeof f->s);
    } else if (c > 0 && f->mode <= 0) {
        f->mode = 1;
        f->ix8 = 0;
        c2_memset(f->s, 0, sizeof f->s);
    }
    if (!f->mode)
        return 0;
    to = c ? djf_index(c, f->mode > 0) : f->mode < 0 ? 255 << 8 : 0;
    f->ix8 += clamp(to - f->ix8, -4096, 4096);   /* (a glide: no zipper, quick enough for the LFOs) */
    if (!c && f->ix8 == to) {
        f->mode = 0;                     /* fully open again: off */
        return 0;
    }
    return 1;
}

PLAT_C2 void fx_djfilter_rest(fx_djf_t *f, int32_t set_q8)
{
    if (djf_follow(f, clamp(set_q8, -16384, 63 << 8)) && (f->s[0] | f->s[1] | f->s[2] | f->s[3])) {
        c2_memset(f->s, 0, sizeof f->s);   /* (what nobody hears holds nothing: afresh when it is heard again) */
        f->quiet = 0;
    }
}

uint32_t fx_sends_on(int forced)
{
    return (fx_on >> FX_CHORUS & 1u) | ((fx_on >> FX_DELAY & 1u) || (forced & 1) ? 2u : 0u) |
           ((fx_on >> FX_REVERB & 1u) || (forced & 2) ? 4u : 0u);
}

PLAT_C2 void fx_djfilter(fx_djf_t *f, int32_t *l, int32_t *r, int32_t set_q8, int reso)
{
    svf_s_t *st = (svf_s_t *)(void *)f->s;
    svf_c_t cs, *co = &cs;
    int32_t c = clamp(set_q8, -16384, 63 << 8);   /* (-64 << 8 .. 63 << 8) */
    int fed;
    if (!djf_follow(f, c))
        return;
    /* fed 0 (a paused deck): with its states 0 it gives 0 as it is, so it rests; a few units circling in them
     * (rounded down, they stop a step short of 0) go once it gave at most FX_REMAINS for 32 blocks (46 ms) */
    fed = !fx_zero(l) || (r && !fx_zero(r));
    if (!fed && !(f->s[0] | f->s[1] | f->s[2] | f->s[3]))
        return;
    svf_coef(co, g_at(f->ix8), FX_RESO_K[reso < 0 ? 0 : reso > 100 ? 100 : reso]);
    if (f->mode > 0) {
        svf_block(co, &st[0], l, 1);
        if (r)
            svf_block(co, &st[1], r, 1);
    } else {
        svf_block(co, &st[0], l, 0);
        if (r)
            svf_block(co, &st[1], r, 0);
    }
    if (fed || !fx_remains(l, r ? r : l, B)) {
        f->quiet = 0;
    } else if (++f->quiet >= 32u) {
        c2_memset(f->s, 0, sizeof f->s);
        f->quiet = 0;
    }
}

void fx_strip_setup(fx_strip_t *s, const uint8_t *setup)
{
    uint32_t i;
    if (!setup)
        setup = DEFAULT_SETUP;
    memset(s, 0, sizeof *s);
    s->route = setup[0] ? 1u : 0u;
    for (i = 0; i < 12u; i++) {
        uint32_t v = setup[1 + i / 2u] >> (4u * (i % 2u)) & 15u;
        if (v == 15u)
            break;
        s->chain[s->nchain++] = (uint8_t)v;
    }
    s->on = (uint16_t)(setup[7] | setup[8] << 8);
    memcpy(s->kn, setup + 16, 48);
    s->ins.gate_g = 1 << 24;
    s->ins.filt_ix8 = 255 << 8;
}

void fx_strip_cmd(fx_strip_t *s, uint8_t code, uint8_t value)
{
    if (code == 0xAEu || code == 0xAFu) {   /* FX ON / OFF: its own effects and DUCK only */
        if (value == FX_GATE || value == FX_FILTER || value == FX_DRIVE || value == FX_LOFI || value == FX_DUCK)
            s->on = (uint16_t)(code == 0xAEu ? s->on | 1u << value : s->on & ~(1u << value));
        return;
    }
    if ((code >= 0x90u && code <= 0x9Au) || (code >= 0xAAu && code <= 0xACu) || (code >= 0xB0u && code <= 0xB5u))
        s->kn[code - 0x90u] = value;     /* (GATE .. LO-FI; DUCK; FILTER's envelope) */
}

void fx_strip_duck(const fx_strip_t *s, fx_duck_t *d)
{
    uint8_t src = s->kn[0xAA - 0x90], dep = s->kn[0xAB - 0x90], rel = s->kn[0xAC - 0x90];
    d->on = (uint8_t)(s->on >> FX_DUCK & 1u && !fx_byp);
    d->src = (uint8_t)(src > FX_DUCK_KEYS ? FX_DUCK_KEYS : src);
    d->depth_q15 = (dep > 100u ? 100 : dep) * 32768 / 100;
    d->release_frames = (20u + 10u * (rel > 98u ? 98u : rel)) * 441u / 10u;
}

/* a strip with nothing to do this block, its chain gone through as fx_strip_lr goes (every effect off, none gliding
 * back open, nothing sent): fx_strip_lr would leave all as it was */
static PLAT_C2 int strip_idle(const fx_strip_t *s, const int32_t *sends)
{
    uint32_t n;
    for (n = 0; n < s->nchain; n++) {
        uint32_t e = s->chain[n], on = e < FX_N && (s->on >> e & 1u);
        if (e == FX_GATE ? on || s->ins.gate_g != 1 << 24
            : e == FX_FILTER ? on || s->ins.filt_mode
            : e == FX_DRIVE || e == FX_LOFI ? on
            : on && !s->route && sends && (e == FX_CHORUS || e == FX_DELAY || e == FX_REVERB))
            return 0;
    }
    return 1;
}

/* (the deck's own knobs, states and music clock given to the effects' code: nothing of the mix's effects touched, not
 * even their pictures, what worked, their time; its DUST from the decks' noise. Core 1's, in core 2's room with the
 * code it shares) */
PLAT_C2 uint32_t fx_strip_lr(fx_strip_t *s, int32_t *l, int32_t *r, int32_t *sends)
{
    static const uint8_t MIXK[3] PLAT_C2_RO = {0x9E, 0xA3, 0xA7};   /* CHORUS, DELAY, REVERB: their MIX */
    uint32_t i, n, us = 0;
    if (fx_byp || strip_idle(s, sends))
        return 0;
    for (n = 0; n < s->nchain; n++) {
        uint32_t e = s->chain[n], on = e < FX_N && (s->on >> e & 1u), t0 = plat_c2_us();
        if (e == FX_GATE && (on || s->ins.gate_g != 1 << 24))
            fx_gate(&s->ins, s->kn, &fx_clk, l, r, (int)on);
        else if (e == FX_FILTER && (on || s->ins.filt_mode))
            fx_filter(&s->ins, s->kn, &fx_clk, l, r, (int)on);
        else if (e == FX_DRIVE && on)
            fx_drive(&s->ins, s->kn, l, r);
        else if (e == FX_LOFI && on)
            fx_lofi(&s->ins, s->kn, &strip_rnd_state, l, r);
        else if (on && !s->route && sends && (e == FX_CHORUS || e == FX_DELAY || e == FX_REVERB)) {
            uint32_t k = e - FX_CHORUS, m = s->kn[MIXK[k] - 0x90];   /* GLOBAL: the mix sent at its MIX */
            int32_t g = (int32_t)(m > 100u ? 100u : m) * 32768 / 100, *b = sends + k * B;
            for (i = 0; i < B; i++)
                b[i] += mulq15((l[i] + r[i]) >> 1, g);
            continue;
        } else
            continue;
        t0 = plat_c2_us() - t0;
        fx_strip_us[e] += t0;
        us += t0;
    }
    return us;
}

/* (the decks' DUST and the mix's noise, handed over as DJ starts and ends: one noise as on one core before, unless
 * a deck's DUST and the METRONOME's CLICK both sound during DJ. At its end the mix's takes the decks' only if they drew
 * from it: else the CLICK's draws meanwhile stand) */
static uint32_t strip_rnd_from;          /* the decks' noise as DJ started */
void fx_strip_noise(int on)
{
    if (on)
        strip_rnd_state = strip_rnd_from = rnd_state;
    else if (strip_rnd_state != strip_rnd_from)
        fx_ev(FX_EV_NOISE, 0, 0, 0);
}

#ifdef PTFM_DEBUG
/* the debug build's bench (core/bench.c): one block of an effect on l, r with its knobs as they stand (FILTER: its
 * cut at a quarter, low-pass). The effects' own state: the bench switches them off in the audio context first. The
 * effects' cases in core 2's room, as their code runs there; SETS on core 1's side (it reads the live settings) */
static PLAT_C2 void fx_bench_room(int which, int32_t *l, int32_t *r)
{
    static fx_djf_t djf[2];
    uint8_t cut;
    switch (which) {
    case FXB_REVERB: fx_reverb(l, r, 0, 0); break;
    case FXB_DELAY: fx_delay(l, r, 0, 0, 0, 0); break;
    case FXB_CHORUS: fx_chorus(l, r, 0, 0); break;
    case FXB_FILTER:
        cut = K(0x94);
        K(0x94) = 32;
        fx_filter(&ins, fx_w.kn, &fx_wclk, l, r, 1);
        K(0x94) = cut;
        break;
    case FXB_DRIVE: fx_drive(&ins, fx_w.kn, l, r); break;
    case FXB_LOFI: fx_lofi(&ins, fx_w.kn, &rnd_state, l, r); break;
    case FXB_GATE: fx_gate(&ins, fx_w.kn, &fx_wclk, l, r, 1); break;
    case FXB_LIMITER: fx_limit(l, r); break;
    case FXB_PUNCH: fx_punches(l, r); break;
    case FXB_DJF: fx_djfilter(&djf[0], l, r, -(32 << 8), 50); break;     /* (half way to LP's end) */
    case FXB_DJF1: fx_djfilter(&djf[1], l, 0, -(32 << 8), 50); break;
    default: break;
    }
}
void fx_bench(int which, int32_t *l, int32_t *r)
{
    if (which == FXB_SETS) {             /* (the live settings put in place as they are (the working ones a block's
                                          * till the next block puts its own); copied, as when a block's differ: the
                                          * generations kept from skipping them) */
        fx_set_t s;
        fx_clock_t c;
        s.seq = 0;
        fx_set_get(&s);
        s.seq = 0;
        memcpy(&c, &fx_wclk, sizeof c);
        PLAT_FAR(fx_work)(&s, &c);
        return;
    }
    PLAT_FAR(fx_bench_room)(which, l, r);
}
#endif
