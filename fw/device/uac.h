/* SPDX-License-Identifier: GPL-3.0-only
 * Copyright (C) 2026 Vasily Kazantsev */
/* USB audio's stream (specs/connections.md#usb-audio): the mix at the FM-1's own rate (~44,145 Hz) resampled to
 * the computer's, 44.1 or 48 kHz, its ratio steered by how full the ring to the USB packets runs, so every 1 ms
 * packet carries exactly the computer's frames (44 or 45 in 44.1's pattern, 48) and nothing drifts.
 * Portable: usb.c on the device, fw/tests/uac_test.c on the PC.
 *   the producer: uac_rs_start before each half, in the audio interrupt (primes the ring, or steers), then each
 *     block's frames (16-bit stereo, as the computer gets them) written where uac_rs_in says and uac_rs_run (or
 *     uac_rs_frames with them in a buffer), in the block's job (on the FM-1 on core 2 while SECOND CORE is on);
 *   the consumer, TIMER5 (on core 1: beside the producer, or nested in it): uac_rs_packet, a packet's frames.
 * The resampler: a polyphase windowed sinc (uac_fir.h, 16 taps, 128 phases, interpolated between two), its
 * place in the input frames in Q32. The steering: the ring's fill at a half's start (the low point of its saw tooth)
 * against a target, filtered, into a PI loop on the step (input frames an output frame); about 6 s to settle,
 * a few ppm of jitter on the step, starting from the FM-1's measured rate.
 * The ring has one writer and one reader, each publishing with UA_PUBLISH (the includer's: on the FM-1 the chip's
 * sync, the two may run on two cores): the data before the place that shows them, the place after reading them.
 * ua.go and the rates are written on core 1 only (TIMER5, and uac_rs_start once core 2 has finished the half
 * before), each a byte of its own; a block's part only reads them. */
#ifndef UAC_H
#define UAC_H
#include <stdint.h>
#ifndef UAC_FIR_MEM                      /* the table in RAM (read every output frame: never from the flash, whose */
#define UAC_FIR_MEM __attribute__((section(".data.uac_fir")))   /* misses cost ~460 cycles; a table never written */
#endif                                   /* would go there unless its section says) */
#include "uac_fir.h"
#ifndef RING_PUBLISH
#define RING_PUBLISH() __asm__ volatile("" ::: "memory")
#endif
#ifndef UA_PUBLISH                       /* (one core: as the includer's other rings) */
#define UA_PUBLISH() RING_PUBLISH()
#endif
#ifndef UAC_C2                           /* the includer's mark for the producer's per-block part (main.c: core 2's */
#define UAC_C2                           /* room, plat.h's PLAT_C2) */
#endif

#define UA_N 512u                        /* the ring, frames (L | R << 16); a power of two */
#define UA_BLK 64u                       /* the most frames a block (the player's block) */
#define UA_IN_HZ 44145u                  /* the FM-1's rate as measured against a USB clock: where the steering starts */
#define UA_T44 128u                      /* the fill aimed at, at a half's start (the render of 256 frames, at most a */
#define UA_T48 139u                      /* half late, must find a packet and more in hand): 44.1, 48 kHz */
#define UA_MAXF 49u                      /* frames in the largest packet (48, and one for the endpoint's size) */
#define UA_STEP44 (uint64_t)(((uint64_t)UA_IN_HZ << 32) / 44100u)   /* input frames an output frame, Q32 */
#define UA_STEP48 (uint64_t)(((uint64_t)UA_IN_HZ << 32) / 48000u)
#define UA_ILIM 21474836                 /* the integral's reach: 0.5 % */
_Static_assert(UA_T48 + 2u * 256u * 160u / 147u / 2u + UA_MAXF < UA_N, "the ring holds the target, a half and a packet");

static uint32_t ua_ring[UA_N];
static volatile uint32_t ua_w, ua_r;     /* frames written (the producer's), taken (the consumer's) */
static struct {
    volatile uint8_t go;                 /* the ring is primed: the consumer reads it (else silence) */
    volatile uint8_t r48;                /* the computer's rate: 1 48 kHz, 0 44.1 (the consumer's side sets it) */
    uint8_t ring48;                      /* the rate the ring was primed for (another: primed again) */
    uint32_t acc;                        /* 44.1's pattern: tenths of a frame */
    uint32_t last;                       /* the last frame sent, repeated if the ring runs dry */
    uint64_t step;                       /* input frames an output frame, Q32 */
    int64_t integ;                       /* the steering's integral, Q32 */
    int32_t e1, ef8;                     /* the fill's error through the first pole, both, Q8 frames */
    uint32_t pos_i, pos_f;               /* the next output's place in the input: frame, fraction (Q32) */
    uint32_t w;                          /* input frames written before the block under way */
    int16_t hx[UAC_FIR_N + UA_BLK][2];   /* the history, a frame's L, R: the last N frames before the block, then
                                          * the block's (a window never wraps; the tail moved to the front after) */
    uint32_t fill;                       /* the ring's fill at the last half's start */
    uint32_t primes, underruns, overruns, pkts, frames;   /* figures */
} ua;

/* (always in place: ua_frame's, in a block's job, and the debug build's bench's own loop take it, never a call) */
static inline __attribute__((always_inline)) int32_t ua_s16(int32_t v)
{
    return v > 32767 ? 32767 : v < -32768 ? -32768 : v;
}

/* before a half renders (the producer): feed 0 when the computer does not read; the ring primed with silence
 * at the target, or the step steered from the fill */
static void uac_rs_start(int feed)
{
    uint32_t fill, target = ua.r48 ? UA_T48 : UA_T44, i;
    int64_t s;
    int fast;
    if (!feed) {
        ua.go = 0;
        return;
    }
    if (!ua.go || ua.ring48 != ua.r48) {
        uint32_t r = ua_r;
        ua.go = 0;
        ua.ring48 = ua.r48;
        for (i = 0; i < UAC_FIR_N + UA_BLK; i++)
            ua.hx[i][0] = ua.hx[i][1] = 0;
        ua.w = ua.pos_i = ua.pos_f = 0;
        ua.step = ua.ring48 ? UA_STEP48 : UA_STEP44;
        ua.integ = 0;
        ua.e1 = ua.ef8 = 0;
        for (i = 0; i < target; i++)
            ua_ring[(r + i) & (UA_N - 1u)] = 0;
        UA_PUBLISH();
        ua_w = r + target;
        ua.fill = target;
        ua.primes++;
        UA_PUBLISH();
        ua.go = 1;
        return;
    }
    fill = ua_w - ua_r;
    ua.fill = fill;
    /* the fill read at a half's start jumps by up to a packet as the packets and the halves pass each other: two
     * poles of ~90 ms smooth it; then the PI loop: ~4 ppm a frame, the integral ~0.001 ppm a frame a half, and
     * 8x / 64x while it is over 24 frames out (acquiring) */
    ua.e1 += (((int32_t)fill - (int32_t)target) * 256 - ua.e1) >> 4;
    ua.ef8 += (ua.e1 - ua.ef8) >> 4;
    fast = ua.ef8 > 24 * 256 || ua.ef8 < -24 * 256;
    ua.integ += fast ? ua.ef8 : ua.ef8 >> 6;
    if (ua.integ > UA_ILIM)
        ua.integ = UA_ILIM;
    if (ua.integ < -UA_ILIM)
        ua.integ = -UA_ILIM;
    s = (int64_t)(ua.ring48 ? UA_STEP48 : UA_STEP44) + ua.integ + (int64_t)ua.ef8 * (fast ? 512 : 64);
    ua.step = (uint64_t)s;
}

/* An output between two phases takes their taps interpolated, in Q30: tap j
 * a * 2^15 + (b - a) * fr (a this phase's, b the next one's, fr Q15). Its sum over the window is then
 * 2^15 L0 + fr (L1 - L0), L0 and L1 the two phases' own sums, so >> 15 gives L0 + (fr (L1 - L0) >> 15) exactly:
 * the two sums interpolated, as the stream has always had it. A tap costs a multiply and two multiply-accumulates
 * (the two sums of each channel took four), and the window is unrolled (a taken jump costs the device ~6 cycles):
 * an output frame 366 cycles on the FM-1 (the bench's USB FRAME; the loop of four sums 593) */
#define UA_TAP(j)                                                                                             \
    {                                                                                                         \
        int32_t a = c[j], k = a * 32768 + (c[UAC_FIR_N + (j)] - a) * fr;  /* (< 2^30: |a| < 2^15, |b - a| < 2^8) */ \
        l += (int64_t)k * x[2 * (j)];                                                                         \
        r += (int64_t)k * x[2 * (j) + 1];                                                                     \
    }
_Static_assert(UAC_FIR_N == 16, "ua_frame unrolls a window of 16 taps");
/* an output frame: the window x (L, R a frame) through the phase c's taps (c[N + j] the next phase's), fr (Q15)
 * of the way to the next; rounded to 16 bits ((l >> 15) + 2^14 >> 15 in one shift; a sum is under 1.6 x 2^45).
 * Not inlined: in its caller the device's compiler lost the multiply-accumulates' shape (64-bit shifts and pairs
 * on the stack: 420 instructions a frame against these 204) */
__attribute__((noinline)) static UAC_C2 uint32_t ua_frame(const int16_t *c, const int16_t *x, int32_t fr)
{
    int64_t l = 0, r = 0;
    UA_TAP(0) UA_TAP(1) UA_TAP(2) UA_TAP(3) UA_TAP(4) UA_TAP(5) UA_TAP(6) UA_TAP(7)
    UA_TAP(8) UA_TAP(9) UA_TAP(10) UA_TAP(11) UA_TAP(12) UA_TAP(13) UA_TAP(14) UA_TAP(15)
    return (uint32_t)(uint16_t)ua_s16((int32_t)((l + (1 << 29)) >> 30)) |
           (uint32_t)(uint16_t)ua_s16((int32_t)((r + (1 << 29)) >> 30)) << 16;
}
/* where a block's frames go (the producer writes up to UA_BLK, L, R a frame, then uac_rs_run): after the history's
 * last N frames; 0 while the ring is not primed (nothing to write) */
static UAC_C2 int16_t *uac_rs_in(void) { return ua.go ? &ua.hx[0][0] + 2u * UAC_FIR_N : 0; }

/* the block's n frames written: every output frame they complete into the ring, at once; then the history's last
 * N frames to its front for the next block */
static UAC_C2 void uac_rs_run(uint32_t n)
{
    uint32_t i, w = ua_w, w0 = ua.w, hw = w0 + n, pi = ua.pos_i, pf = ua.pos_f;
    uint64_t step = ua.step;
    if (!ua.go || n > UA_BLK)
        return;
    while ((int32_t)(hw - pi) > (int32_t)(UAC_FIR_N / 2u)) {         /* its window's newest frame is in */
        uint64_t p;
        if (__builtin_expect(w - ua_r < UA_N, 1))   /* (the window from frame pi - N/2 + 1, the history's from w0 - N; */
            ua_ring[w++ & (UA_N - 1u)] = ua_frame(UAC_FIR[pf >> 25],        /* fr: Q15 between the phases) */
                                                  ua.hx[pi - w0 + UAC_FIR_N / 2u + 1u], (int32_t)(pf >> 10 & 0x7FFFu));
        else
            ua.overruns++;               /* (nobody reads: dropped) */
        p = (uint64_t)pf + step;
        pi += (uint32_t)(p >> 32);
        pf = (uint32_t)p;
    }
    for (i = 0; i < UAC_FIR_N; i++) {    /* (n < N: still in order, each read before it is written) */
        ua.hx[i][0] = ua.hx[n + i][0];
        ua.hx[i][1] = ua.hx[n + i][1];
    }
    ua.w = hw;
    ua.pos_i = pi;
    ua.pos_f = pf;
    UA_PUBLISH();
    ua_w = w;
}

/* a block's frames from a buffer (the computer's 16-bit values, L, R a frame) */
static void uac_rs_frames(const int16_t *lr, uint32_t n)
{
    int16_t *h = uac_rs_in();
    uint32_t i;
    if (!h || n > UA_BLK)
        return;
    for (i = 0; i < 2u * n; i++)
        h[i] = lr[i];
    uac_rs_run(n);
}

/* the next packet into d (the consumer): the computer's frames for 1 ms, from the ring once it is primed at
 * this rate, else silence; a dry ring repeats the last frame. Returns the frame count. */
static uint32_t uac_rs_packet(uint32_t *d)
{
    uint32_t n = 48u, r, fill, k, i;
    if (!ua.r48) {
        n = 44u;
        ua.acc += 1u;                    /* 44.1: a 45 every tenth packet */
        if (ua.acc >= 10u) {
            ua.acc = 0;
            n = 45u;
        }
    }
    if (!ua.go || ua.ring48 != ua.r48) {
        for (i = 0; i < n; i++)
            d[i] = 0;
        return n;
    }
    r = ua_r;
    fill = ua_w - r;
    UA_PUBLISH();
    k = fill < n ? fill : n;
    for (i = 0; i < k; i++)
        d[i] = ua_ring[(r + i) & (UA_N - 1u)];
    if (k < n) {
        uint32_t last = k ? d[k - 1u] : ua.last;
        ua.underruns++;
        for (; i < n; i++)
            d[i] = last;
    }
    ua.last = d[n - 1u];
    UA_PUBLISH();
    ua_r = r + k;
    ua.pkts++;
    ua.frames += n;
    return n;
}

/* the computer's rate (SET_CUR, the consumer's side; 44.1 after a bus reset): a change primes the ring again */
static void uac_rs_rate(uint32_t hz)
{
    uint8_t r48 = (uint8_t)(hz >= (44100u + 48000u) / 2u);
    if (r48 == ua.r48)
        return;
    ua.go = 0;
    UA_PUBLISH();
    ua.r48 = r48;
    ua.acc = 0;
}
#endif
