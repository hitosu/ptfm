/* SPDX-License-Identifier: GPL-3.0-only
 * Copyright (C) 2026 Vasily Kazantsev
 * The reading method (driven readings, the log map) after Charles Vestal's measurements, fm1-velocity */
/* The keys' velocity (specs/hardware.md#the-keys-velocity): from the scan's readings of the keys' carbon pills
 * (hal/fm1_input.h) to each press's velocity. Code in a header, portable: device/main.c includes it (the scan's hooks,
 * the platform's plat_key_vel), the emulator too (the map, the take: its panel gives a press's landing itself), and
 * the host test (fw/tests/keyvel_test.c).
 *
 * The 20 keys F3-C5 (notes 0-19) lie on rows PA8 and PA7, the converter's channels 1 and 0; each is read every 2.2 ms
 * (hal/fm1_input.h). Pulled up, a reading is 1023 open, under KV_TOUCH once the pill conducts, under KV_SEAT seated,
 * and back over KV_OPEN twice when let go (a landed key can read open once: a bounce). Once a key is touched, every
 * other reading of it is taken with its row driven weakly high: that reading less KV_BASE (the matrix diode and the
 * column's driver) is the pill's resistance, which falls as a power of the force on it. The dome snaps, so every press
 * lands within a reading or two whatever its speed: how hard a key went down is how low it sits just after, the
 * lowest of the first driven readings after the touch, its landing (WAIT 1: the first; kv.wait2, the debug build's
 * switch: the lower of the first two).
 *
 *   tick (TIMER5): kv_reading(n, v, driven, frame)  a reading of key n (KV_NO_READING: its slot had none)
 *                  kv_drive(n), kv_dark(n)          its next reading driven; its column dark while it is read
 *                  kv_note(n, down, frame)          the debounced scan's press or release of note n
 *   the app's side: kv_shown(notes)                 the notes held but for the presses still waiting for their
 *                                                   velocity (at most KV_LATE_FRAMES, then they play at KEYS VOLUME)
 *                  kv_take(shown, hand)             the presses new since the last take: their velocities (the log
 *                                                   map, a divide: here, never in the tick); main loop or 1 kHz tick
 *                  kv_vel(n)                        key n's press's velocity, 1-127; 0 none (KEYS VOLUME)
 *                  kv_now(n)                        a held key's latest driven reading (the controller's aftertouch)
 *   kv_set(curve)                                  KEYS VELOCITY: 0 CONSTANT, 1 SOFT, 2 NORMAL, 3 HARD (interrupts off)
 *
 * The velocity: t = log(r / r_firm) / log(r_light / r_firm) of the landing's resistance r (r_firm = KV_FIRM - KV_BASE,
 * velocity 127; r_light = KV_LIGHT - KV_BASE, velocity 1), velocity = 1 + 126 (1 - t)^c, c by the curve (SOFT 0.25,
 * NORMAL 0.5, HARD 1.0): integers, a log2 table and the curves' tables. The top row (C#5-G5, notes 20-26) has no
 * converter: it plays at the mean of the last four sensed velocities, none before any. */
#ifndef KEYVEL_H
#define KEYVEL_H
#include <stdint.h>

#define KV_KEYS 20u                     /* the keys read: notes 0-19 (F3-C5) */
#define KV_NOTES 27u
#define KV_TOUCH 900u                   /* pulled up, under it: the pill conducts (the touch) */
#define KV_SEAT 400u                    /* pulled up, under it: seated */
#define KV_OPEN 600u                    /* pulled up, over it twice: open again */
#define KV_BASE 185u                    /* driven, no resistance: the diode and the column's driver */
#define KV_FIRM 530u                    /* the driven landing of a firm press: velocity 127 */
#define KV_LIGHT 896u                   /* of the lightest press that seats: velocity 1 */
#define KV_LATE_FRAMES 9u               /* a press waits for its velocity at most 9 frames of the scan (~10 ms) */
#define KV_NO_READING 0xFFFFu           /* kv_reading's v: the slot's conversion gave none */

/* a key's state, one word: the lowest driven reading since the touch (bits 0-9; once the landing is known, the latest
 * driven reading), the landing (10-19, 0 not known yet), the state (20-21), lifting (22: one reading over KV_OPEN), the next reading driven (23), the driven readings
 * since the touch (24-25), its press waited too long (26: KEYS VOLUME) */
enum { KV_S_OPEN, KV_S_TOUCH, KV_S_DOWN, KV_S_LIFT };
#define KV_DMIN(s) ((s) & 1023u)
#define KV_LAND(s) ((s) >> 10 & 1023u)
#define KV_ST(s) ((s) >> 20 & 3u)
#define KV_UP (1u << 22)
#define KV_DRV (1u << 23)
#define KV_DN(s) ((s) >> 24 & 3u)
#define KV_LATE (1u << 26)

typedef struct {
    volatile uint32_t key[KV_KEYS];     /* the tick's: each key's state */
    volatile uint32_t pend;             /* presses (bit n) waiting for their velocity */
    volatile uint8_t pend_at[KV_KEYS];  /* the scan's frame each came at (its low byte) */
    volatile uint8_t curve;             /* KEYS VELOCITY: 0 CONSTANT (nothing read), 1 SOFT, 2 NORMAL, 3 HARD */
    volatile uint8_t wait2;             /* 1: a landing is the lower of two driven readings (WAIT 2, the debug build's) */
    uint8_t vel[KV_NOTES];              /* the take's: each held key's velocity, 0 none */
    uint8_t last[4], nlast, at;         /* the last four sensed velocities (nlast of them), the oldest's place */
    uint32_t seen;                      /* the presses taken */
} kv_t;
static kv_t kv;

#ifdef PTFM_DEBUG
/* the debug build's readings (specs/debug.md: STATUS, FIGURES 2): each key's touch and first two driven readings,
 * the last landing's key, the counts since power-on; each key's lowest pulled-up reading in the window (at rest: how
 * open it reads); the last four landings (a RUN's presses); MASTER from the slot and read directly, the battery (the
 * platform's); KEYS FEED's key and its press (kv_feed) */
#define KV_RING 4u
static struct {
    uint16_t tr[KV_KEYS];               /* each key's touch (bits 0-9: the pulled-up reading that began its press), its
                                         * lowest pulled-up reading / 16 since the window began (10-15, 0 none) */
    uint16_t last_land;                 /* the last landing (the ring's newest: its key, velocity, readings) */
    uint16_t presses, grazes, lates;    /* landings; touches gone without seating; presses that waited too long */
    uint32_t ring_rd[KV_RING];          /* the last landings: their touch (bits 0-9), their first driven reading (10-19),
                                         * the second (20-29) */
    uint16_t ring_kv[KV_RING];          /* ... their key + 1 (bits 0-4, 0 none), velocity (5-11: 0 KEYS VOLUME or not
                                         * taken yet), the scan's frames the press waited (12-15: 15 shown late) */
    uint8_t landed;                     /* the landings so far, mod 256 (the ring's newest: landed - 1) */
    uint8_t fed;                        /* KEYS FEED's key while it is fed (0xFF none): the scan leaves it alone */
    uint32_t fake;                      /* KEYS FEED's presses: the notes held as the scan's (bit n) */
    uint16_t m_slot, m_direct;          /* MASTER from the scan's slot, read directly: at the last battery pause */
    uint16_t batt, batt_n;              /* the battery's last reading (0xFFFF none), the readings since power-on */
} kv_dbg = {{0}, 0, 0, 0, 0, {0}, {0}, 0, 0xFFu, 0, 0xFFFFu, 0xFFFFu, 0xFFFFu, 0};
#define KV_DBG(x) x
#define KV_FAKE kv_dbg.fake
/* the ring's newest landing of key n (KV_RING: none) */
static uint32_t kv_ring_of(uint32_t n)
{
    uint32_t k, i;
    for (k = 1; k <= KV_RING; k++) {
        i = (kv_dbg.landed - k) & (KV_RING - 1u);
        if ((kv_dbg.ring_kv[i] & 31u) == n + 1u)
            return i;
    }
    return KV_RING;
}
/* (the window's lowest pulled-up reading of key n: at rest, how open it reads) */
static inline void kv_rest(uint32_t n, uint32_t v)
{
    uint32_t x = v >> 4 ? v >> 4 : 1u, r = (uint32_t)kv_dbg.tr[n] >> 10;
    if (v != 0xFFFFu && (!r || x < r))
        kv_dbg.tr[n] = (uint16_t)((kv_dbg.tr[n] & 1023u) | x << 10);
}
/* the last landing (the ring's newest): its key (0xFF none), its velocity, its readings (ring_rd's) */
static uint32_t kv_dbg_last(uint32_t *vel, uint32_t *rd)
{
    uint32_t i = (kv_dbg.landed - 1u) & (KV_RING - 1u), k = kv_dbg.ring_kv[i];
    *vel = k >> 5 & 127u;
    *rd = kv_dbg.ring_rd[i];
    return k & 31u ? (k & 31u) - 1u : 0xFFu;
}
#else
#define KV_DBG(x)
#define KV_FAKE 0u
#endif

/* ---- the tick's side: no loop, no divide ---- */
static inline int kv_drive(uint32_t n) { return n < KV_KEYS && (kv.key[n] & KV_DRV) != 0u; }
static inline int kv_dark(uint32_t n) { return n < KV_KEYS && KV_ST(kv.key[n]) != KV_S_OPEN; }

static void kv_reading(uint32_t n, uint32_t v, uint32_t driven, uint32_t frame)
{
    uint32_t s, st, b = 1u << n, late;
    if (n >= KV_KEYS)
        return;
    s = kv.key[n];
    if (!s && !driven && v >= KV_TOUCH && !(kv.pend & b)) {   /* an open key reading open (or none), nothing waiting:
                                                         * nothing to do (the tick's usual way at rest) */
        KV_DBG(kv_rest(n, v));
        return;
    }
    st = KV_ST(s);
    if ((kv.pend & b) && (uint8_t)(frame - kv.pend_at[n]) >= KV_LATE_FRAMES) {
        kv.pend &= ~b;                  /* waited long enough: shown, at KEYS VOLUME (till its release) */
        s |= KV_LATE;
        KV_DBG(kv_dbg.lates++);
    }
    late = s & KV_LATE;
    if (v == KV_NO_READING) {           /* (none this time: the next reading as this one would have been) */
        kv.key[n] = s;
        return;
    }
    if (driven) {
        s &= ~KV_DRV;
        if ((st == KV_S_TOUCH || st == KV_S_DOWN) && KV_LAND(s)) {
            s = (s & ~1023u) | v;       /* (the landing known: the latest driven reading in its place, the controller's
                                         * aftertouch's: kv_now) */
#ifdef PTFM_DEBUG
            if (KV_DN(s) == 1u) {       /* (WAIT 1: the second driven reading, for the record) */
                uint32_t i = kv_ring_of(n);
                s = (s & ~(3u << 24)) | 2u << 24;
                if (i < KV_RING)
                    kv_dbg.ring_rd[i] = (kv_dbg.ring_rd[i] & ~(1023u << 20)) | v << 20;
            }
#endif
        } else if (st == KV_S_TOUCH || st == KV_S_DOWN) {
            uint32_t m = KV_DMIN(s), dn = KV_DN(s) + 1u;
#ifdef PTFM_DEBUG
            uint32_t d1 = dn == 1u ? v : m;  /* (the first driven reading: the lowest so far, at the second) */
#endif
            if (v < m)
                m = v;
            s = (s & ~(1023u | 3u << 24)) | m | (dn > 3u ? 3u : dn) << 24;
            if (dn > kv.wait2) {        /* the landing: the velocity is known, the press waiting for it shown */
                s |= (m ? m : 1u) << 10;
#ifdef PTFM_DEBUG
                {                       /* (the ring: how long its press waited, in the scan's frames) */
                    uint32_t w = (s & KV_LATE) ? 15u : (kv.pend & b) ? (uint8_t)(frame - kv.pend_at[n]) : 0u, i;
                    i = kv_dbg.landed & (KV_RING - 1u);
                    kv_dbg.ring_rd[i] = (kv_dbg.tr[n] & 1023u) | d1 << 10 | (dn == 2u ? v << 20 : 0u);
                    kv_dbg.ring_kv[i] = (uint16_t)((n + 1u) | (w < 15u ? w : 15u) << 12);
                    kv_dbg.landed++;
                }
#endif
                kv.pend &= ~b;
                KV_DBG(kv_dbg.last_land = (uint16_t)m; kv_dbg.presses++);
            }
        }
        kv.key[n] = s;
        return;
    }
    KV_DBG(kv_rest(n, v));
    switch (st) {
    case KV_S_OPEN:
        if (v < KV_TOUCH) {             /* the touch: a fresh landing to come, the next reading driven */
            s = (uint32_t)KV_S_TOUCH << 20 | 1023u | KV_DRV;
            KV_DBG(kv_dbg.tr[n] = (uint16_t)((kv_dbg.tr[n] & ~1023u) | v));
        } else
            s = 0;
        break;
    case KV_S_TOUCH:
        if (v >= KV_TOUCH) {            /* gone before it seated: a graze */
            s = 0;
            KV_DBG(kv_dbg.grazes++);
        } else
            s = (s & ~(3u << 20)) | (uint32_t)(v < KV_SEAT ? KV_S_DOWN : KV_S_TOUCH) << 20 | KV_DRV;   /* (seated, or not yet) */
        break;
    default:                            /* DOWN, LIFT */
        if (v < KV_SEAT)
            s = (s & ~(3u << 20 | KV_UP)) | (uint32_t)KV_S_DOWN << 20 | KV_DRV;
        else if (v < KV_OPEN)
            s = (s & ~(3u << 20 | KV_UP)) | (uint32_t)KV_S_LIFT << 20 | KV_DRV;
        else if (st == KV_S_LIFT && (s & KV_UP))
            s = 0;                      /* open twice in a row: let go */
        else
            s = (s & ~(3u << 20)) | (uint32_t)KV_S_LIFT << 20 | KV_UP | KV_DRV;
        break;
    }
    kv.key[n] = s | late;               /* (a press shown late stays so till the digital scan lets it go) */
}

/* the debounced scan's press (down 1) or release of note n: a press of a key read waits for its velocity unless its
 * landing is known already; a release lets the key's readings start afresh */
static void kv_note(uint32_t n, uint32_t down, uint32_t frame)
{
    uint32_t b = 1u << n;
    if (n >= KV_KEYS || !kv.curve)
        return;
    if (!down) {
        kv.key[n] = 0;
        kv.pend &= ~b;
        return;
    }
    if (!KV_LAND(kv.key[n])) {
        kv.pend_at[n] = (uint8_t)frame;
        kv.pend |= b;
    }
}

/* ---- the app's side ---- */
static inline uint32_t kv_shown(uint32_t notes) { return notes & ~kv.pend; }

/* log2 of x (1-1023) in Q12: the leading bit's place, and the rest from a 65-entry table, interpolated */
static const uint16_t KV_LOG2[65] = {
    0, 92, 182, 271, 358, 445, 530, 613, 696, 778, 858, 937, 1016, 1093, 1169, 1244, 1319, 1392, 1465, 1536, 1607,
    1677, 1746, 1814, 1882, 1949, 2015, 2080, 2145, 2208, 2272, 2334, 2396, 2457, 2518, 2578, 2637, 2696, 2754, 2812,
    2869, 2926, 2982, 3037, 3092, 3146, 3200, 3254, 3307, 3359, 3412, 3463, 3514, 3565, 3615, 3665, 3715, 3764, 3812,
    3861, 3908, 3956, 4003, 4050, 4096};    /* round(4096 log2(1 + i / 64)) */
static uint32_t kv_log2(uint32_t x)
{
    uint32_t e = 0, f, i;
    if (!x)
        x = 1u;
    while (x >> (e + 1u))
        e++;
    f = (x << (16u - e)) & 0xFFFFu;     /* the mantissa, 16 bits (x <= 1023: e <= 9) */
    i = f >> 10;
    return (e << 12) + KV_LOG2[i] + (((uint32_t)(KV_LOG2[i + 1u] - KV_LOG2[i]) * (f & 1023u) + 512u) >> 10);
}
/* round(1 + 126 (1 - i / 64)^c): SOFT 0.25, NORMAL 0.5, HARD 1.0 */
static const uint8_t KV_CURVE[3][65] = {
    {127, 127, 126, 125, 125, 124, 124, 123, 123, 122, 122, 121, 121, 120, 119, 119, 118, 118, 117, 116, 116, 115, 114,
     114, 113, 112, 112, 111, 110, 109, 109, 108, 107, 106, 105, 104, 103, 103, 102, 101, 100, 99, 97, 96, 95, 94, 93,
     91, 90, 89, 87, 86, 84, 82, 80, 78, 76, 73, 71, 68, 64, 60, 54, 46, 1},
    {127, 126, 125, 124, 123, 122, 121, 120, 119, 118, 117, 116, 115, 113, 112, 111, 110, 109, 108, 107, 105, 104, 103,
     102, 101, 99, 98, 97, 96, 94, 93, 91, 90, 89, 87, 86, 84, 83, 81, 80, 78, 77, 75, 73, 71, 70, 68, 66, 64, 62, 60,
     58, 56, 53, 51, 48, 46, 43, 40, 36, 33, 28, 23, 17, 1},
    {127, 125, 123, 121, 119, 117, 115, 113, 111, 109, 107, 105, 103, 101, 99, 97, 96, 94, 92, 90, 88, 86, 84, 82, 80,
     78, 76, 74, 72, 70, 68, 66, 64, 62, 60, 58, 56, 54, 52, 50, 48, 46, 44, 42, 40, 38, 36, 34, 33, 31, 29, 27, 25, 23,
     21, 19, 17, 15, 13, 11, 9, 7, 5, 3, 1}};
/* the velocity (1-127) of a driven landing d on curve c (1 SOFT, 2 NORMAL, 3 HARD) */
static uint32_t kv_map(uint32_t d, uint32_t c)
{
    uint32_t l0, span, t, i, f;
    const uint8_t *k = KV_CURVE[c >= 1u && c <= 3u ? c - 1u : 1u];
    if (d <= KV_FIRM)
        return 127u;
    if (d >= KV_LIGHT)
        return 1u;
    l0 = kv_log2(KV_FIRM - KV_BASE);
    span = kv_log2(KV_LIGHT - KV_BASE) - l0;
    t = ((kv_log2(d - KV_BASE) - l0) << 12) / span;   /* 0-4096 */
    if (t > 4096u)
        t = 4096u;
    i = t >> 6;
    f = t & 63u;
    return i >= 64u ? k[64] : k[i] - ((uint32_t)(k[i] - k[i + 1u]) * f + 32u) / 64u;
}

/* the presses new since the last take (in shown, not before): each its velocity, from its landing (a key read), or the
 * mean of the last four sensed (the top row); hand: the presses a hand made (the emulator's panel tells; the FM-1's
 * are all), the others none. The keys let go: none */
static void kv_take(uint32_t shown, uint32_t hand)
{
    uint32_t nw = shown & ~kv.seen, n;
    if (shown == kv.seen)
        return;
    for (n = 0; n < KV_NOTES; n++) {
        uint32_t v = 0;
        if (!(shown >> n & 1u))
            kv.vel[n] = 0;
        if (!(nw >> n & 1u & hand >> n))
            continue;                   /* (not new; new, not a hand's: none) */
        if (n < KV_KEYS) {
            uint32_t s = kv.key[n], d = KV_LAND(s);
            if (d && !(s & KV_LATE)) {
                v = kv_map(d, kv.curve);
                kv.last[kv.at] = (uint8_t)v;
                kv.at = (uint8_t)((kv.at + 1u) & 3u);
                if (kv.nlast < 4u)
                    kv.nlast++;
            }
#ifdef PTFM_DEBUG
            {
                uint32_t i = kv_ring_of(n);
                if (i < KV_RING)
                    kv_dbg.ring_kv[i] = (uint16_t)((kv_dbg.ring_kv[i] & ~(127u << 5)) | v << 5);
            }
#endif
        } else if (kv.nlast) {
            uint32_t k, sum = 0;
            for (k = 0; k < kv.nlast; k++)
                sum += kv.last[(kv.at + 4u - 1u - k) & 3u];
            v = (sum + kv.nlast / 2u) / kv.nlast;
        }
        kv.vel[n] = (uint8_t)v;
    }
    kv.seen = shown;
}
static inline uint32_t kv_vel(uint32_t n) { return kv.curve && n < KV_NOTES && (kv.seen >> n & 1u) ? kv.vel[n] : 0u; }
/* key n's latest driven reading while it is held down and read (its landing known), 0 none: the controller's
 * aftertouch (fw/core/ctl.h) works the pressure out of it */
static inline uint32_t kv_now(uint32_t n)
{
    uint32_t s = n < KV_KEYS ? kv.key[n] : 0u, st = KV_ST(s);
    return kv.curve && (st == KV_S_TOUCH || st == KV_S_DOWN) && KV_LAND(s) && !(s & KV_LATE) ? KV_DMIN(s) : 0u;
}

#ifdef PTFM_DEBUG
/* KEYS FEED (specs/debug.md#keys-feed, the debug build's): key n's readings as the scan would bring them, from frame
 * on, through the same state machine: op[2k] = kind << 3 | the value's bits 7-9, op[2k + 1] its bits 0-6. Kinds: 0 a
 * pulled-up reading, 1 a driven one, 2 the slot's none (each 2 frames on: a key's readings come every other frame),
 * 3 the debounced scan's press (the key held: KV_FAKE), 4 its release (the key back to the scan), 5 the value's frames
 * passing. While it is fed the scan leaves key n alone (main.c). With interrupts off */
static void kv_feed(uint32_t n, const uint8_t *op, uint32_t len, uint32_t frame)
{
    uint32_t i;
    if (n >= KV_NOTES)
        return;
    for (i = 0; i + 1u < len; i += 2u) {
        uint32_t kind = op[i] >> 3 & 15u, v = (op[i] & 7u) << 7 | (op[i + 1u] & 127u);
        if (kind != 4u)
            kv_dbg.fed = (uint8_t)n;
        if (kind <= 2u) {
            frame += 2u;
            kv_reading(n, kind == 2u ? KV_NO_READING : v, kind == 1u, frame);
        } else if (kind == 3u) {
            kv_note(n, 1u, frame);
            kv_dbg.fake |= 1u << n;
        } else if (kind == 4u) {
            kv_note(n, 0u, frame);
            kv_dbg.fake &= ~(1u << n);
            if (n < KV_KEYS)
                kv.key[n] = 0;          /* (at CONSTANT kv_note leaves it: the fed readings go with the key) */
            kv_dbg.fed = 0xFFu;
        } else if (kind == 5u)
            frame += v;
    }
}
#endif

/* KEYS VELOCITY: every key's readings start afresh (the presses waiting shown); the keys held now are no new
 * presses. With interrupts off (the tick reads and writes the same) */
static void kv_set(uint32_t curve, uint32_t held)
{
    uint32_t n;
    for (n = 0; n < KV_KEYS; n++)
        kv.key[n] = 0;
    for (n = 0; n < KV_NOTES; n++)
        kv.vel[n] = 0;
    kv.pend = 0;
    kv.seen = held;
    kv.curve = (uint8_t)(curve <= 3u ? curve : 2u);
}
#endif
