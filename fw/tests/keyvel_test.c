/* SPDX-License-Identifier: GPL-3.0-only
 * Copyright (C) 2026 Vasily Kazantsev */
/* Host test of the keys' velocity (fw/device/keyvel.h, specs/hardware.md#the-keys-velocity): the per-key readings'
 * state machine fed with reading curves as the scan gives them (a reading of each key every second frame, every other
 * one driven once it is touched), the digital scan's presses beside them; the log map and its curves against the
 * formula in doubles; the take (the top row's mean, a hand's presses only); KEYS VELOCITY CONSTANT. Checked:
 *   - a hard hit (driven ~390-450) plays at 127, a gentle press (515-584) under it, a light one (~800) low; the press
 *     waits for its velocity, known at the first driven reading after the touch (WAIT 1), at most one reading on;
 *   - the digital press before the touch: it waits, its velocity the landing that follows;
 *   - a graze: no landing, the key open again; a bounce while held: no new landing;
 *   - let go (two readings over 600, or the digital scan's release): the next press its own landing;
 *   - two keys at once, each its own; a press never read: shown after 9 frames, at KEYS VOLUME (0);
 *   - the top row at the mean of the last four sensed, none before any; WAIT 2 the lower of two driven readings;
 *   - the map: 127 at the firm end, 1 at the light end, never rising with the landing, SOFT >= NORMAL >= HARD,
 *     within 1 of 1 + 126 (1 - t)^c from velocity 20 up and 5 under it, but for the last 64th of t, where the table
 *     runs straight to 1 (the curves' steep light end: SOFT's falls from 46); its checksum, which the
 *     playground's twin must give too (tests/playground/keyvel.js);
 *   - a held key's latest driven reading (kv_now: MIDI CONTROLLER's aftertouch);
 *   - CONSTANT: no press waits, no velocity, no reading.
 *   keyvel_test [-q] */
#include <math.h>
#include <stdio.h>
#include <string.h>
#define PTFM_DEBUG 1
#include "../device/keyvel.h"

static int fails, checks, quiet;
static void check(const char *what, int ok)
{
    checks++;
    if (!ok || !quiet)
        printf("%-86s %s\n", what, ok ? "ok" : "FAIL");
    fails += !ok;
}

/* the scan, as fm1_input.h runs it: frame f, key n read on its frames (every second one), driven when kv_drive says;
 * a key's physical state at a frame: its pulled-up reading and its driven one */
static uint32_t frame;
typedef struct { uint32_t pulled, driven; } phys_t;
static phys_t key_now[KV_KEYS];
static uint32_t notes;                  /* the digital scan's debounced notes */
static void open_all(void)
{
    for (uint32_t n = 0; n < KV_KEYS; n++)
        key_now[n] = (phys_t){1023, 1023};
}
static void read_key(uint32_t n)
{
    int drv = kv_drive(n);
    kv_reading(n, drv ? key_now[n].driven : key_now[n].pulled, (uint32_t)drv, frame);
}
static void frames(uint32_t k)          /* k frames: every key read on its frames (n even on even frames, as rows go) */
{
    while (k--) {
        frame++;
        for (uint32_t n = 0; n < KV_KEYS; n++)
            if ((n + frame) % 2u == 0u)
                read_key(n);
    }
}
static void dig(uint32_t n, int down)
{
    notes = down ? notes | 1u << n : notes & ~(1u << n);
    kv_note(n, (uint32_t)down, frame);
}
static uint32_t shown(void) { return kv_shown(notes); }
static void fresh(uint32_t curve)
{
    memset(&kv, 0, sizeof kv);
    memset(&kv_dbg, 0, sizeof kv_dbg);
    kv_dbg.fed = 0xFFu;
    kv_set(curve, 0);
    notes = 0;
    open_all();
}
static uint32_t take_vel(uint32_t n)    /* the app's take, then key n's velocity */
{
    uint32_t s = shown();
    kv_take(s, s);
    return kv_vel(n);
}

static double formula(uint32_t d, uint32_t c)
{
    static const double C[3] = {0.25, 0.5, 1.0};
    double t;
    if (d <= KV_FIRM)
        return 127;
    if (d >= KV_LIGHT)
        return 1;
    t = log((double)(d - KV_BASE) / (KV_FIRM - KV_BASE)) / log((double)(KV_LIGHT - KV_BASE) / (KV_FIRM - KV_BASE));
    return 1 + 126 * pow(1 - t, C[c - 1]);
}

int main(int argc, char **argv)
{
    char what[160];
    quiet = argc > 1 && !strcmp(argv[1], "-q");

    {   /* the map */
        uint32_t c, d, mono = 1, order = 1, ends = 1;
        double worst = 0, worst20 = 0;
        unsigned long sum = 0;
        for (c = 1; c <= 3; c++) {
            uint32_t prev = 200;
            for (d = 0; d <= 1023; d++) {
                uint32_t v = kv_map(d, c);
                double f = formula(d, c), e = fabs((double)v - f);
                if (d > KV_FIRM && d < KV_LIGHT && log((double)(d - KV_BASE) / (KV_FIRM - KV_BASE)) /
                    log((double)(KV_LIGHT - KV_BASE) / (KV_FIRM - KV_BASE)) > 63.0 / 64.0)
                    e = 0;              /* (the last 64th: the table runs straight to 1) */
                if (e > worst)
                    worst = e;
                if (f >= 20 && e > worst20)
                    worst20 = e;
                if (v > prev)
                    mono = 0;
                prev = v;
                sum += (unsigned long)v * (d + 1u) * c;
            }
            ends &= kv_map(KV_FIRM, c) == 127u && kv_map(KV_LIGHT, c) == 1u && kv_map(300, c) == 127u &&
                    kv_map(1000, c) == 1u;
        }
        for (d = KV_FIRM; d <= KV_LIGHT; d++)
            if (kv_map(d, 1) < kv_map(d, 2) || kv_map(d, 2) < kv_map(d, 3))
                order = 0;
        check("the map: 127 at the firm end (530 and under), 1 at the light end (896 and over), each curve", ends);
        check("the map never rises as the landing rises (lower is firmer), each curve", mono);
        check("SOFT >= NORMAL >= HARD for every landing (softer playing reaches 127 at SOFT)", order);
        snprintf(what, sizeof what, "the map within 1 of 1 + 126 (1 - t)^c from velocity 20 up (worst %.2f), within 5 under it "
                 "(%.2f), but for the last 64th of t (the curves' steep light end)", worst20, worst);
        check(what, worst20 <= 1.0 && worst <= 5.0);
        snprintf(what, sizeof what, "the map's checksum (sum of velocity x (landing + 1) x curve): %lu", sum);
        check(what, sum == 211832164UL);
        snprintf(what, sizeof what, "NORMAL: 420 -> %u, 584 -> %u, 700 -> %u, 800 -> %u, 890 -> %u", kv_map(420, 2),
                 kv_map(584, 2), kv_map(700, 2), kv_map(800, 2), kv_map(890, 2));
        check(what, kv_map(420, 2) == 127u && kv_map(584, 2) > 90u && kv_map(584, 2) < 127u && kv_map(800, 2) < 70u &&
                    kv_map(890, 2) < 20u);
    }

    {   /* a hard hit: touched and seated at once, driven at 420 */
        fresh(2);
        frames(3);
        key_now[4] = (phys_t){300, 420};
        frames(1);                      /* (frame 4: key 4 read, pulled: the touch) */
        check("a hard hit: the touch read, the key's next reading driven, its column dark", kv_drive(4) && kv_dark(4));
        dig(4, 1);
        check("... the digital press waits for the velocity", !(shown() >> 4 & 1u) && (kv.pend >> 4 & 1u));
        key_now[4] = (phys_t){185, 425};
        frames(2);                      /* (its next reading: driven) */
        check("... shown at the first driven reading after the touch (WAIT 1), one reading on",
              (shown() >> 4 & 1u) && KV_LAND(kv.key[4]) == 425u);
        {
            uint32_t lv, lrd, lk;
            check("... its velocity 127 (the firm end)", take_vel(4) == 127u);
            lk = kv_dbg_last(&lv, &lrd);
            check("... the record's last press: key 4 at 127, touch 300, driven 425", lk == 4u && lv == 127u &&
                  (lrd & 1023u) == 300u && (lrd >> 10 & 1023u) == 425u);
        }
        check("... the next reading pulled again, the one after driven (every other one)", !kv_drive(4));
        frames(2);
        check("...", kv_drive(4));
        frames(2);
        {
            uint32_t lv, lrd;
            kv_dbg_last(&lv, &lrd);
            snprintf(what, sizeof what, "... the second driven reading kept for the record (%u)", lrd >> 20 & 1023u);
            check(what, (lrd >> 20 & 1023u) == 425u && kv_dbg.presses == 1u);
        }
        key_now[4].driven = 390;
        frames(4);
        snprintf(what, sizeof what, "... held, pressed on: the latest driven reading for the controller's aftertouch (%u), the landing kept",
                 kv_now(4));
        check(what, kv_now(4) == 390u && KV_LAND(kv.key[4]) == 425u && kv_now(5) == 0u);
    }

    {   /* a gentle press: touched at 700, seated after, driven 584 */
        fresh(2);
        frames(2);
        key_now[7] = (phys_t){700, 584};
        frames(2);
        dig(7, 1);
        frames(2);
        snprintf(what, sizeof what, "a gentle press (driven 584): velocity %u, under the hard hit's", take_vel(7));
        check(what, (shown() >> 7 & 1u) && kv_vel(7) > 80u && kv_vel(7) < 127u);
        key_now[7] = (phys_t){190, 560};
        frames(4);
        check("... seated later: its velocity stays the landing's", kv_vel(7) == kv_map(584, 2) && KV_ST(kv.key[7]) == KV_S_DOWN);
        /* let go: two pulled readings over 600 */
        key_now[7] = (phys_t){1023, 1023};
        frames(2);
        check("... one reading over 600: lifting, not open (a bounce could follow)", KV_ST(kv.key[7]) == KV_S_LIFT);
        frames(4);
        check("... two in a row: open, its landing gone", KV_ST(kv.key[7]) == KV_S_OPEN && !KV_LAND(kv.key[7]));
        dig(7, 0);
        kv_take(shown(), shown());
        check("... the release: no velocity", kv_vel(7) == 0u);
        /* pressed again, firmer: its own landing */
        key_now[7] = (phys_t){260, 450};
        frames(2);
        dig(7, 1);
        frames(2);
        check("pressed again, harder: its own landing (127), not the last one's", take_vel(7) == 127u);
    }

    {   /* the digital press first: the touch comes a reading later */
        fresh(2);
        frames(1);
        dig(2, 1);
        frames(2);
        check("the digital press before any touch: it waits", !(shown() >> 2 & 1u));
        key_now[2] = (phys_t){500, 700};
        frames(2);
        check("... the touch read: still waiting", !(shown() >> 2 & 1u));
        frames(2);
        snprintf(what, sizeof what, "... the driven reading after it: shown, its velocity the landing's (%u)", take_vel(2));
        check(what, (shown() >> 2 & 1u) && kv_vel(2) == kv_map(700, 2));
    }

    {   /* a graze: touched, never seated, open again; no digital press */
        fresh(2);
        key_now[10] = (phys_t){850, 880};
        frames(2);
        check("a graze: touched", KV_ST(kv.key[10]) == KV_S_TOUCH);
        frames(2);
        key_now[10] = (phys_t){950, 1023};
        frames(2);
        check("... open again before it seated: a graze, no landing kept", KV_ST(kv.key[10]) == KV_S_OPEN &&
              !KV_LAND(kv.key[10]) && kv_dbg.grazes == 1u && !kv_dark(10));
    }

    {   /* a bounce while held */
        fresh(2);
        key_now[12] = (phys_t){200, 460};
        frames(2);
        dig(12, 1);
        frames(4);
        uint32_t v0 = take_vel(12), land = KV_LAND(kv.key[12]);
        key_now[12].pulled = 650;
        frames(2);
        frames(2);                      /* (a pulled reading over 600, once) */
        key_now[12] = (phys_t){190, 470};
        frames(4);
        check("a bounce while held (one reading open): down again, the same landing and velocity",
              KV_ST(kv.key[12]) == KV_S_DOWN && KV_LAND(kv.key[12]) == land && kv_vel(12) == v0);
    }

    {   /* the digital release forces the readings open */
        fresh(2);
        key_now[5] = (phys_t){200, 470};
        frames(2);
        dig(5, 1);
        frames(2);
        dig(5, 0);
        check("the digital scan's release: the key's readings open at once, its landing gone",
              KV_ST(kv.key[5]) == KV_S_OPEN && !KV_LAND(kv.key[5]));
        key_now[5] = (phys_t){200, 800};
        frames(2);
        dig(5, 1);
        frames(2);
        check("... pressed again: a fresh landing (800)", take_vel(5) == kv_map(800, 2));
    }

    {   /* two keys at once */
        fresh(2);
        key_now[3] = (phys_t){250, 430};
        key_now[8] = (phys_t){600, 760};
        frames(1);
        dig(3, 1);
        dig(8, 1);
        frames(4);
        kv_take(shown(), shown());
        snprintf(what, sizeof what, "two keys at once: each its own velocity (%u, %u)", kv_vel(3), kv_vel(8));
        check(what, kv_vel(3) == 127u && kv_vel(8) == kv_map(760, 2));
    }

    {   /* a press never read: at KEYS VOLUME after 9 frames */
        fresh(2);
        dig(9, 1);
        frames(8);
        check("a press never read: waiting 8 frames on", !(shown() >> 9 & 1u));
        frames(4);
        check("... shown by frame 9-11 (a reading's slot), at KEYS VOLUME (0)", (shown() >> 9 & 1u) && take_vel(9) == 0u &&
              (kv.key[9] & KV_LATE) && kv_dbg.lates == 1u);
        key_now[9] = (phys_t){250, 430};
        frames(4);
        kv_take(shown(), shown());
        check("... a landing after that: the press keeps KEYS VOLUME", kv_vel(9) == 0u);
    }

    {   /* the top row: the mean of the last four sensed */
        static const uint32_t D[5] = {460, 640, 700, 760, 820};
        uint32_t v[5], k;
        fresh(2);
        dig(22, 1);
        check("the top row before any key read: none (KEYS VOLUME)", take_vel(22) == 0u);
        dig(22, 0);
        for (k = 0; k < 5; k++) {
            key_now[k] = (phys_t){300, D[k]};
            frames(2);
            dig(k, 1);
            frames(2);
            v[k] = take_vel(k);
            dig(k, 0);
            key_now[k] = (phys_t){1023, 1023};
            frames(1);
            kv_take(shown(), shown());
            if (k == 1u) {
                dig(20, 1);
                snprintf(what, sizeof what, "... after two read (%u, %u): the top row at their mean, %u", v[0], v[1], take_vel(20));
                check(what, kv_vel(20) == (v[0] + v[1] + 1u) / 2u);
                dig(20, 0);
                kv_take(shown(), shown());
            }
        }
        dig(26, 1);
        snprintf(what, sizeof what, "... after five: the mean of the last four (%u)", take_vel(26));
        check(what, kv_vel(26) == (v[1] + v[2] + v[3] + v[4] + 2u) / 4u);
        dig(26, 0);
        dig(25, 1);
        kv_take(shown(), 0);
        check("a press not a hand's (the emulator's keyboard): none", kv_vel(25) == 0u);
    }

    {   /* WAIT 2: the lower of two driven readings */
        fresh(2);
        kv.wait2 = 1;
        key_now[6] = (phys_t){300, 600};
        frames(2);
        dig(6, 1);
        frames(2);
        check("WAIT 2: one driven reading in, still waiting", !(shown() >> 6 & 1u));
        key_now[6] = (phys_t){190, 520};
        frames(4);
        check("... the second: shown, its landing the lower of the two (520)", (shown() >> 6 & 1u) &&
              KV_LAND(kv.key[6]) == 520u && take_vel(6) == 127u);
    }

    {   /* the curves on one landing, and OFF */
        uint32_t c, v[4];
        for (c = 1; c <= 3; c++) {
            fresh(c);
            key_now[1] = (phys_t){300, 700};
            frames(2);
            dig(1, 1);
            frames(2);
            v[c] = take_vel(1);
        }
        snprintf(what, sizeof what, "one landing (700) on SOFT, NORMAL, HARD: %u, %u, %u", v[1], v[2], v[3]);
        check(what, v[1] > v[2] && v[2] > v[3] && v[2] == kv_map(700, 2));
        fresh(0);
        key_now[1] = (phys_t){300, 700};
        frames(2);
        dig(1, 1);
        check("CONSTANT: a press never waits, has no velocity, no reading", (shown() >> 1 & 1u) && take_vel(1) == 0u && !kv.pend &&
              kv_now(1) == 0u);
    }

    printf("keyvel_test: %d checks, %d failed\n", checks, fails);
    return fails != 0;
}
