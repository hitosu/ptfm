/* SPDX-License-Identifier: GPL-3.0-only
 * Copyright (C) 2026 Vasily Kazantsev */
/* The forward work (fw/core/eng/fwd.h; specs/proposals/202611121830-ghost-notes.md, section 5): a note taken from its
 * engine comes back worked forward to where it would be. One driver for every engine: a note started fresh and worked
 * forward (its xx_forward) against the same note started fresh and moved unit by unit (its skip step, its note off at
 * the let-go):
 *  - ages from a unit to 60 s held, let go after 0 to 60 s and for up to 10 s, and let go where any of its
 *    envelopes' stages changes (that unit, the one before, the one after);
 *  - each return in one call, in pieces of 1, 3 and 7 steps a call (never leaving the budget under the engine's stated
 *    bound), and with the time going on between calls (a unit a call, at 3 steps a call and at 1; a note let go during
 *    its return; where the engine gives one, a parameter changed on both between calls), its state after every call
 *    the same as its steps' to as far;
 *  - the steps a return takes printed, and held under ceilings (the budget counts them).
 * Each engine's section gives its patches, what must be equal, and its own checks:
 *  - the DX7 (dx7_forward, dx7_heard): every built-in patch (the cartridges F0-F7), the disk's, patches made at random
 *    (envelopes, scalings and LFOs over the patches' ranges, a few past them) and one whose release swells: every
 *    operator's envelope, the pitch envelope and the LFO equal field for field, and nothing else of the voice changed
 *    but the operators' running values; the next block the same (its levels, gains and pitches) and its sound the same
 *    once the phases are; dx7_heard against that block's peak; the sample-and-hold's period.
 *  - the Juno-106 (juno_forward, juno_heard, juno_release_ticks), in ticks of its firmware loop: the built-in patches
 *    (the cartridge F8), the disk's, patches made at random (every slider 0-127, every switch, a level up to +24 dB)
 *    and made at the sliders' ends: every field of the voice equal but the oscillator's phase and the sub; the next
 *    tick the same, and its sound the same once the phases are; juno_heard against the peaks of the blocks rendered
 *    after it comes back and of the note never taken, rendered through; juno_release_ticks against the release's own
 *    ticks, by RELEASE.
 *  - the sampler (samp_forward, samp_heard; the place: samp_place_go, samp_end_blocks, samp_note_at), in blocks: the
 *    disk's sampler instruments (one-shot and looping, FORWARD and PING-PONG) at three notes (steps up and down), and
 *    instruments made at random on their samples (envelopes over their ranges and past them, loops FORWARD and
 *    PING-PONG anywhere, some shorter than the step, starts past the loop, END inside, bends: other steps): the
 *    envelope and amp equal field for field, the place moved with the forward work (samp_place_go over its blocks,
 *    held, then let go) equal to the steps' position, fraction, way, loop and end, nothing else of the voice changed;
 *    a parameter changed between calls as the player's commands turn a sounding voice (a rate, a level, END, LOOP,
 *    the loop's points, TUNE); a voice started at that place (samp_note_at), its envelope worked forward, rendering
 *    as the steps' voice sample for sample; samp_end_blocks against the block its steps end it in; samp_heard against
 *    the peaks rendered. Then the place alone: walked stretch by stretch with the pitch and the commands changed
 *    between stretches against samp_skip's blocks, and frame by frame from any state (every way of the walk, a loop
 *    turned over, a step past the loop's length) against samp_skip's own frames.
 *  - the CHIP (chip_forward, chip_heard, chip_end_blocks), in blocks: the built-in instruments (the cartridge F9), the
 *    disk's, and instruments made at random (every field over what a slot holds; every table at every speed; sweeps
 *    up and down on every clock to their limits; the PSG envelope's every shape, fast and slow; the step envelope, the
 *    vibrato with its delay, every RELEASE; some bent): everything it keeps equal field for field (the tables, this
 *    frame's values, the frames' clock, the envelopes, the release, the sweep, the lengths the next block reads, the
 *    end), nothing else of the voice changed but the oscillators' phases, the noise register and the band-limited
 *    sum; a parameter turned between calls as the player's commands turn a sounding note (the tables' speeds,
 *    RELEASE, the tables, FPS, the clock, the sweep, the envelopes, the curve, the oscillator, ...); the next blocks
 *    rendered as the steps' voice's once those parts are the same, and chip_heard against their peaks; let go where
 *    its release counter steps (and the block before and after); chip_end_blocks against the block chip_skip ends the
 *    note in, block by block, held and let go.
 *   forward_test DISK [dx7 | juno | samp | chip]   (one engine's section alone) */
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "../../core/eng/fwd.h"
#include "../../core/eng/chip/chip.h"
#include "../../core/eng/dx7/dx7.h"
#include "../../core/eng/juno/juno.h"
#include "../../core/eng/samp/samp.h"
#include "../../core/play/rec.h"
#include "../../core/store/store.h"

int disk_load(const char *path);

static int fails;
static void failed(const char *what)
{
    if (++fails <= 20)
        printf("forward: FAILED: %s\n", what);
}

static uint32_t rnd_s = 20261112u;
static int rnd(int n)
{
    rnd_s = rnd_s * 1103515245u + 12345u;
    return (int)((rnd_s >> 8) % (uint32_t)n);
}

/* ---- counts: steps a return (or calls), sorted when told ---- */
typedef struct {
    uint32_t *v, most;
    int n, room;
    char worst[64];
} tally_t;

static void tally(tally_t *t, uint32_t x, const char *what)
{
    if (!t->n || x > t->most) {
        t->most = x;
        snprintf(t->worst, sizeof t->worst, "%s", what);
    }
    if (t->n == t->room) {
        t->room = t->room ? 2 * t->room : 1024;
        t->v = realloc(t->v, (size_t)t->room * sizeof *t->v);
    }
    t->v[t->n++] = x;
}

static int by_value(const void *a, const void *b)
{
    uint32_t x = *(const uint32_t *)a, y = *(const uint32_t *)b;
    return x < y ? -1 : x > y;
}

static uint32_t median(tally_t *t)
{
    qsort(t->v, (size_t)t->n, sizeof *t->v, by_value);
    return t->n ? t->v[(t->n - 1) / 2] : 0;
}

static void tally_line(const char *what, tally_t *t)   /* its median, 90th percentile and most */
{
    if (!t->n)
        return;
    qsort(t->v, (size_t)t->n, sizeof *t->v, by_value);
    printf("  %-34s %6d returns: median %3u, 90%% %3u, worst %4u steps (%s)\n", what, t->n, t->v[(t->n - 1) / 2],
           t->v[(t->n - 1) * 9 / 10], t->most, t->worst);
}

/* ============================================================================================== the driver === */
typedef struct {
    samp_voice_t v;
    samp_place_t pl;                     /* the forward work's: its place, moved with it (the steps' is the voice's) */
} smv_t;

typedef union {
    dx7_voice_t dx;
    juno_voice_t ju;
    smv_t sm;
    chip_voice_t ch;
} fv_t;                                  /* a voice of any engine here */

typedef struct {
    const char *name;
    double per_s;                        /* its units a second */
    int32_t low;                         /* the lowest *steps a call may leave (its header's bound) */
    uint32_t most, most_made, typical;   /* ceilings: the instruments' worst return and median, the made ones' worst
                                          * (a few steps over what is measured: meant to trip when the steps are
                                          * weighed anew, so the ceilings are set again on purpose) */
    int npatch;
    const char *(*patch_name)(int p);
    int (*patch_made)(int p);            /* 0 an instrument (built in, the disk's), 1 made, 2 made past the ranges */
    void (*start)(fv_t *v, int p, int note);   /* a fresh state's note, as the player starts it */
    void (*skip)(fv_t *v);               /* a unit */
    void (*note_off)(fv_t *v);
    int (*forward)(fv_t *v, uint32_t *on, uint32_t *off, int32_t *steps);
    int (*let_go)(const fv_t *v);        /* its note off done */
    const char *(*diff)(const fv_t *ref, const fv_t *f);   /* what differs, 0 nothing that must not */
    uint32_t (*stages)(const fv_t *v);   /* its envelopes' stages as one number (a let-go where it changes) */
    /* its own checks of a return done whole (ref the steps', f the forward work's), what: the case so far */
    void (*check)(int p, int note, const fv_t *ref, const fv_t *f, uint32_t on, uint32_t off, char *what, size_t room);
    /* the k-th parameter change (from 1) made alike to two voices of one note, a return's and its reference, between
     * calls (a return under way is a voice like any: its parameter commands and level reach it); 0: none */
    void (*change)(fv_t *a, fv_t *b, int k);
} engine_t;

static const int PIECE[3] = {1, 3, 7};
static struct {
    tally_t held[5], let, made, inst, pieces[3], going[2], turned;
    long cases, calls[3], steps[3], whole, going_let;
} D;

static uint32_t units(const engine_t *E, double s) { return (uint32_t)(s * E->per_s + 0.5); }

static int below(const engine_t *E, int32_t st, char *what, size_t room, const char *how)
{
    if (st >= E->low)
        return 0;
    snprintf(what + strlen(what), room - strlen(what), "%s: a call left the budget at %d (its bound %d)", how,
             (int)st, (int)E->low);
    failed(what);
    return 1;
}

/* with the time going on: per steps a call (3 or 1), a unit going by between calls (held: to *on; let go: to *off; let
 * go at the let_at-th call: *off switched from FWD_HELD to 0, then added to), its state after every call the same as a
 * reference moved unit by unit to as far; turn: a parameter changed on both between calls (E->change) */
static void going(const engine_t *E, int p, int note, uint32_t on, uint32_t off, int let_at, int per, int turn,
                  char *what, size_t room)
{
    fv_t g, r;
    uint32_t o = on, l = off, ton = on, toff = off, ron = 0, roff = 0;
    int done, noted = 0;
    long calls = 0;
    const char *d;
    E->start(&g, p, note);
    E->start(&r, p, note);
    do {
        int32_t st = per;
        done = E->forward(&g, &o, &l, &st);
        calls++;
        if (below(E, st, what, room, "with the time going on"))
            return;
        for (; ron < ton - o; ron++)     /* the reference to where the forward work stands */
            E->skip(&r);
        if (l != FWD_HELD && E->let_go(&g)) {
            if (!noted) {
                E->note_off(&r);
                noted = 1;
            }
            for (; roff < toff - l; roff++)
                E->skip(&r);
        }
        if ((d = E->diff(&r, &g)) != 0) {
            snprintf(what + strlen(what), room - strlen(what), "with the time going on (%d steps a call, let go at "
                     "call %d%s), after call %ld: %s", per, let_at, turn ? ", parameters changed" : "", calls, d);
            failed(what);
            return;
        }
        if (!done && turn)               /* (a parameter turned on both) */
            E->change(&g, &r, (int)calls);
        if (!done) {                     /* (a unit goes by) */
            if (l == FWD_HELD && calls != let_at) {
                o++;
                ton++;
            } else {
                if (l == FWD_HELD) {
                    l = 0;
                    toff = 0;
                    D.going_let++;
                }
                l++;
                toff++;
            }
        }
    } while (!done && calls < 1000000);
    if (!done || o || (l && l != FWD_HELD)) {
        snprintf(what + strlen(what), room - strlen(what), "with the time going on: never done");
        failed(what);
        return;
    }
    tally(turn ? &D.turned : &D.going[per == 1], (uint32_t)calls, "");
}

/* one return checked: patch p's note, ref the steps' (on units held, then off units let go; FWD_HELD held) */
static void one(const engine_t *E, int p, int note, const fv_t *ref, uint32_t on, uint32_t off)
{
    fv_t f, g;
    uint32_t o, l;
    int32_t st;
    int k, r, i, made = E->patch_made(p);
    long calls, used;
    char what[240];
    const char *d;
    D.cases++;
    snprintf(what, sizeof what, "%s %s note %d, %u units held, then %s%u: ", E->name, E->patch_name(p), note, on,
             off == FWD_HELD ? "held " : "let go ", off == FWD_HELD ? 0u : off);
    E->start(&f, p, note);
    o = on;
    l = off;
    st = 1 << 30;
    r = E->forward(&f, &o, &l, &st);
    if (!r || o || (l && l != FWD_HELD)) {
        strcat(what, "not done in one call");
        failed(what);
        return;
    }
    if ((d = E->diff(ref, &f)) != 0) {
        strcat(what, d);
        failed(what);
        return;
    }
    {
        uint32_t u = (uint32_t)((1 << 30) - st);
        char w[64];
        snprintf(w, sizeof w, "%s note %d, %.2f s%s", E->patch_name(p), note, on / E->per_s,
                 off == FWD_HELD ? " held" : " then let go");
        D.whole += u;
        if (made) {
            tally(&D.made, u, w);
        } else {
            tally(&D.inst, u, w);
            if (off != FWD_HELD)
                tally(&D.let, u, w);
            else
                for (i = 0; i < 5; i++)  /* held 0.1 s, 1 s, 10 s, 30 s, 60 s */
                    if (on == units(E, i == 0 ? 0.1 : i == 1 ? 1 : i == 2 ? 10 : i == 3 ? 30 : 60))
                        tally(&D.held[i], u, w);
        }
    }
    for (k = 0; k < 3; k++) {            /* in pieces */
        E->start(&g, p, note);
        o = on;
        l = off;
        calls = used = 0;
        do {
            st = PIECE[k];
            r = E->forward(&g, &o, &l, &st);
            used += PIECE[k] - st;
            calls++;
            if (below(E, st, what, sizeof what, "in pieces"))
                return;
        } while (!r && calls < 1000000);
        if (!r || (d = E->diff(ref, &g)) != 0) {
            snprintf(what + strlen(what), sizeof what - strlen(what), "in pieces of %d: %s", PIECE[k],
                     r ? d : "never done");
            failed(what);
            return;
        }
        D.calls[k] += calls;
        D.steps[k] += used;
        tally(&D.pieces[k], (uint32_t)calls, "");
    }
    if ((on == units(E, 0.1) || on == units(E, 1)) && (off == FWD_HELD || off == units(E, 0.3))) {
        for (k = 3; k > 0; k -= 2) {     /* (3 steps a call, then 1) */
            going(E, p, note, on, off, -1, k, 0, what, sizeof what);
            if (off == FWD_HELD)
                going(E, p, note, on, off, 2, k, 0, what, sizeof what);
        }
        if (E->change && on == units(E, 1)) {   /* (parameters changed between calls: 3 steps a call, then 1) */
            going(E, p, note, on, off, -1, 3, 1, what, sizeof what);
            if (off == FWD_HELD)
                going(E, p, note, on, off, 2, 1, 1, what, sizeof what);
        }
    }
    E->check(p, note, ref, &f, on, off, what, sizeof what);
}

/* the release from ref (held h units) checked at its let-go points, up to `most` units */
static void release(const engine_t *E, int p, int note, const fv_t *ref, uint32_t h, double most_s)
{
    static const double OFF[] = {0, -1, 0.05, 0.3, 1, 3, 10};   /* (-n: n units) */
    fv_t rel = *ref;
    uint32_t u = 0, w, most = units(E, most_s);
    int j;
    E->note_off(&rel);
    for (j = 0; j < (int)(sizeof OFF / sizeof OFF[0]); j++) {
        w = OFF[j] < 0 ? (uint32_t)-OFF[j] : units(E, OFF[j]);
        if (w > most)
            break;
        for (; u < w; u++)
            E->skip(&rel);
        one(E, p, note, &rel, h, w);
    }
}

/* every return of patch p's note: the steps taken once from its start, the returns checked on the way; at_stages:
 * also let go where its stages change (that unit, the one before, the one after) */
static void note_all(const engine_t *E, int p, int note, double held_most_s, int at_stages)
{
    static const double HELD[] = {0, -1, -2, -3, 0.1, 0.3, 1, 3, 5, 10, 30, 60};
    fv_t ref, prev;
    uint32_t t, next = 0, most = units(E, held_most_s), last = FWD_HELD, then = FWD_HELD, s, s0 = 0;
    int i = 0;
#define REGULAR(h) ((h) == 0 || (h) == 1 || (h) == units(E, 0.1) || (h) == units(E, 1) || (h) == units(E, 5) || \
                    (h) == most)
#define AT_STAGE(v, h)                                                                                               \
    do {                                                                                                             \
        if ((h) != last && !REGULAR(h)) {                                                                            \
            last = (h);                                                                                              \
            release(E, p, note, (v), (h), 0.3);                                                                      \
        }                                                                                                            \
    } while (0)
    E->start(&ref, p, note);
    prev = ref;
    for (t = 0;; t++) {
        if (at_stages) {                 /* (ref at t, prev at t - 1) */
            s = E->stages(&ref);
            if (t && s != s0) {
                AT_STAGE(&prev, t - 1);
                AT_STAGE(&ref, t);
                then = t + 1;
            } else if (t == then) {
                AT_STAGE(&ref, t);
            }
            s0 = s;
        }
        while (i < (int)(sizeof HELD / sizeof HELD[0]) &&
               (next = HELD[i] < 0 ? (uint32_t)-HELD[i] : units(E, HELD[i])) < t)
            i++;
        if (i < (int)(sizeof HELD / sizeof HELD[0]) && next == t && t <= most) {
            one(E, p, note, &ref, t, FWD_HELD);
            if (REGULAR(t))              /* let go here: for 10 s from 0.1 s and the longest held, 3 s elsewhere */
                release(E, p, note, &ref, t, t == units(E, 0.1) || t == most ? 10 : 3);
            i++;
        }
        if (t >= most)
            break;
        if (at_stages)
            prev = ref;
        E->skip(&ref);
    }
#undef AT_STAGE
#undef REGULAR
}

/* every patch's notes: an instrument's C-2, C-4 (held 60 s, let go where its stages change) and C-6 (10 s); a patch
 * made, one note at random (60 s; 10 s past the ranges) */
static void drive(const engine_t *E)
{
    int p, k;
    memset(&D, 0, sizeof D);
    for (p = 0; p < E->npatch; p++) {
        int made = E->patch_made(p);
        if (!made) {
            note_all(E, p, 36, 10, 0);
            note_all(E, p, 60, 60, 1);
            note_all(E, p, 84, 10, 0);
        } else {
            note_all(E, p, 24 + rnd(85), made == 1 ? 60 : 10, 1);
        }
    }
    printf("%s: %ld returns\n", E->name, D.cases);
    printf("  steps a return in one call:\n");
    tally_line("held 0.1 s", &D.held[0]);
    tally_line("held 1 s", &D.held[1]);
    tally_line("held 10 s", &D.held[2]);
    tally_line("held 30 s", &D.held[3]);
    tally_line("held 60 s", &D.held[4]);
    tally_line("let go (held a unit to 60 s)", &D.let);
    tally_line("the instruments', every return", &D.inst);
    tally_line("the patches made", &D.made);
    for (k = 0; k < 3; k++)
        printf("  in pieces of %d steps a call: %.2f calls and %.1f steps a return on average (one call: %.1f), the "
               "most calls %u\n", PIECE[k], (double)D.calls[k] / D.cases, (double)D.steps[k] / D.cases,
               (double)D.whole / D.cases, D.pieces[k].most);
    printf("  with the time going on, 3 steps a call: %d returns, the most calls %u; 1 step a call: %d returns, the "
           "most calls %u; %ld let go during it; every call's state as its steps'\n", D.going[0].n, D.going[0].most,
           D.going[1].n, D.going[1].most, D.going_let);
    if (E->change)
        printf("  with the time going on and a parameter changed on both between calls: %d returns, the most calls %u, "
               "every call's state as its steps'\n", D.turned.n, D.turned.most);
    {
        char w[160];
        uint32_t m = median(&D.inst);
        if (D.inst.most > E->most || D.made.most > E->most_made || m > E->typical) {
            snprintf(w, sizeof w, "%s: the steps a return over their ceilings (worst %u of %u, made %u of %u, median "
                     "%u of %u)", E->name, D.inst.most, E->most, D.made.most, E->most_made, m, E->typical);
            failed(w);
        }
    }
}

/* ================================================================================================ the DX7 === */
typedef struct {
    char name[16];
    uint8_t vc[DX7_VCED], made;
} dxp_t;
static dxp_t *dxp;
static int dxn;

static void dx_add(const char *name, const uint8_t *vc, int made)
{
    dxp = realloc(dxp, (size_t)(dxn + 1) * sizeof *dxp);
    snprintf(dxp[dxn].name, sizeof dxp[dxn].name, "%s", name);
    memcpy(dxp[dxn].vc, vc, DX7_VCED);
    dxp[dxn].made = (uint8_t)made;
    dxn++;
}

static int dx_carts(int lo, int hi)      /* the DX7 cartridges lo..hi: their patches added */
{
    int num, n = 0, s;
    for (num = lo; num <= hi; num++) {
        rec_cart_t c;
        if (rec_cart((uint8_t)num, &c) || c.kind != REC_DX7)
            continue;
        for (s = 0; s < c.nslots; s++) {
            const uint8_t *p = rec_slot(&c, (uint8_t)s);
            uint8_t vc[DX7_VCED];
            char name[16];
            if (!p)
                continue;
            rec_dx7_vced(p, vc);
            snprintf(name, sizeof name, "%02X:%02X", num, s);
            dx_add(name, vc, 0);
            n++;
        }
    }
    return n;
}

/* a patch made at random on base: its envelopes (some stages to the level they start at: holds), scalings, LFO and
 * pitch envelope over their ranges; an operator's rates and levels and its output up to top (99 the patches' range,
 * 127 the fields' own) */
static void dx_random(const uint8_t *base, uint8_t *vc, int top)
{
    int op, i, r;
    memcpy(vc, base, DX7_VCED);
    for (op = 0; op < 6; op++) {
        uint8_t *o = vc + op * 21;
        for (i = 0; i < 4; i++) {
            r = rnd(8);
            o[i] = (uint8_t)(r == 0 ? 0 : r == 1 ? top : rnd(top + 1));
            r = rnd(8);
            o[4 + i] = (uint8_t)(r < 2 && i ? o[3 + i] : r == 2 ? 0 : r == 3 ? top : rnd(top + 1));
        }
        o[8] = (uint8_t)rnd(100);        /* the level scaling: break point, depths, curves */
        o[9] = (uint8_t)rnd(100);
        o[10] = (uint8_t)rnd(100);
        o[11] = (uint8_t)rnd(4);
        o[12] = (uint8_t)rnd(4);
        o[13] = (uint8_t)rnd(8);         /* rate scaling, amplitude modulation, velocity, output */
        o[14] = (uint8_t)rnd(4);
        o[15] = (uint8_t)rnd(8);
        o[16] = (uint8_t)(rnd(8) ? rnd(top + 1) : top);
    }
    for (i = 0; i < 4; i++) {            /* the pitch envelope (its rates and levels index tables of 100) */
        vc[126 + i] = (uint8_t)rnd(100);
        vc[130 + i] = (uint8_t)(rnd(8) < 2 && i ? vc[129 + i] : rnd(100));
    }
    vc[134] = (uint8_t)rnd(32);          /* algorithm, feedback, OSC KEY SYNC */
    vc[135] = (uint8_t)rnd(8);
    vc[136] = (uint8_t)rnd(2);
    vc[137] = (uint8_t)rnd(100);         /* the LFO: speed, delay, depths, sync, wave, pitch sensitivity */
    vc[138] = (uint8_t)(rnd(4) ? rnd(100) : 0);
    vc[139] = (uint8_t)rnd(100);
    vc[140] = (uint8_t)rnd(100);
    vc[141] = (uint8_t)rnd(2);
    vc[142] = (uint8_t)rnd(6);
    vc[143] = (uint8_t)rnd(8);
    vc[144] = (uint8_t)(12 + rnd(25));   /* transpose */
}

/* a release that swells: algorithm 32, operator 1 alone heard, its sustain at 0 and its release to 85 (slowly), the
 * others silent */
static void dx_swell(const uint8_t *base, uint8_t *vc)
{
    static const uint8_t R[4] = {99, 60, 60, 25}, L[4] = {99, 40, 0, 85};
    int op;
    memcpy(vc, base, DX7_VCED);
    for (op = 0; op < 6; op++) {
        uint8_t *o = vc + op * 21;
        memcpy(o, R, 4);
        memcpy(o + 4, L, 4);
        o[16] = (uint8_t)(op == 5 ? 99 : 0);   /* (operator 1 is the last of the six) */
    }
    vc[134] = 31;
    vc[140] = 0;                         /* (no amplitude modulation) */
}

static const char *dx_name(int p) { return dxp[p].name; }
static int dx_made(int p) { return dxp[p].made; }
static void dx_start(fv_t *v, int p, int note)
{
    memset(&v->dx, 0, sizeof v->dx);
    dx7_note_on(&v->dx, dxp[p].vc, note, 100, 100);   /* (PL_VEL, its modulators at 100 too: the forward work is
                                          * the same at any) */
}
static void dx_skip(fv_t *v) { dx7_skip(&v->dx); }
static void dx_off(fv_t *v) { dx7_note_off(&v->dx); }
static int dx_fwd(fv_t *v, uint32_t *on, uint32_t *off, int32_t *steps) { return dx7_forward(&v->dx, on, off, steps); }
static int dx_let_go(const fv_t *v) { return !v->dx.penv.down; }
static uint32_t dx_stages(const fv_t *v)
{
    uint32_t s = v->dx.penv.ix;
    int i;
    for (i = 0; i < 6; i++)
        s = s << 3 | v->dx.env[i].ix;
    return s;
}

/* what differs: the parts the forward work moves, then anything else of the voice but the operators' running values
 * (their phases, gains, levels in, frequencies) and the feedback's last samples */
static const char *dx_diff(const fv_t *fa, const fv_t *fb)
{
    static char w[160];
    const dx7_voice_t *a = &fa->dx, *b = &fb->dx;
    dx7_voice_t x = *a, y = *b;
    int i;
    for (i = 0; i < 6; i++) {
        const dx7_env_t *p = &a->env[i], *q = &b->env[i];
        if (p->level != q->level || p->targetlevel != q->targetlevel || p->inc != q->inc ||
            p->staticcount != q->staticcount || p->ix != q->ix || p->rising != q->rising || p->down != q->down) {
            snprintf(w, sizeof w, "operator %d's envelope: level %d / %d, stage %d / %d, hold %d / %d, target %d / %d",
                     6 - i, (int)p->level, (int)q->level, p->ix, q->ix, (int)p->staticcount, (int)q->staticcount,
                     (int)p->targetlevel, (int)q->targetlevel);
            return w;
        }
    }
    if (a->penv.level != b->penv.level || a->penv.targetlevel != b->penv.targetlevel || a->penv.inc != b->penv.inc ||
        a->penv.ix != b->penv.ix || a->penv.rising != b->penv.rising || a->penv.down != b->penv.down) {
        snprintf(w, sizeof w, "the pitch envelope: level %d / %d, stage %d / %d", (int)a->penv.level,
                 (int)b->penv.level, a->penv.ix, b->penv.ix);
        return w;
    }
    if (a->lfo.phase != b->lfo.phase || a->lfo.delaystate != b->lfo.delaystate ||
        a->lfo.randstate != b->lfo.randstate) {
        snprintf(w, sizeof w, "the LFO: phase %08X / %08X, delay %08X / %08X, random %d / %d", a->lfo.phase,
                 b->lfo.phase, a->lfo.delaystate, b->lfo.delaystate, a->lfo.randstate, b->lfo.randstate);
        return w;
    }
    memcpy(y.op, x.op, sizeof x.op);
    memcpy(y.fb_buf, x.fb_buf, sizeof x.fb_buf);
    if (memcmp(&x, &y, sizeof x))
        return "another field of the voice";
    return 0;
}

/* dx7_heard against the peak of the block rendered next: over the larger of its values before and after that block
 * by at most 1, always; over its value before by more than 1 only where a hold ended into a rising stage in the block.
 * For the instruments: peak / heard in dB, and, let go, the most over and the blocks under PL_QUIET (33, -60 dB) that
 * peak at it or more */
static double hd_min = 1e9, hd_max = -1e9, hd_sum;
static long hd_n, hd_blocks, hd_rel_under, hd_held_under, hd_holds;
static int32_t hd_rel_most = -99, hd_held_most = -99, hd_ends_most = -99;

/* the next block the same as the steps' (its levels, gains, pitches; its sound once the phases are), and dx7_heard
 * against its peak */
static void dx_check(int p, int note, const fv_t *ref, const fv_t *f, uint32_t on, uint32_t off, char *what,
                     size_t room)
{
    dx7_voice_t a = ref->dx, b = f->dx, a0;
    int32_t x[DX7_N], y[DX7_N], pk = 0, h0, h1, top;
    int i;
    (void)note;
    (void)on;
    dx7_skip(&a);
    dx7_skip(&b);
    for (i = 0; i < 6; i++)
        if (a.op[i].level_in != b.op[i].level_in || a.op[i].gain_out != b.op[i].gain_out ||
            a.op[i].freq != b.op[i].freq) {
            snprintf(what + strlen(what), room - strlen(what), "the next block: operator %d's level %d / %d", 6 - i,
                     (int)a.op[i].level_in, (int)b.op[i].level_in);
            failed(what);
            return;
        }
    for (i = 0; i < 6; i++)
        b.op[i].phase = a.op[i].phase;
    memcpy(b.fb_buf, a.fb_buf, sizeof a.fb_buf);
    a0 = a;
    h0 = dx7_heard(&a);
    memset(x, 0, sizeof x);
    memset(y, 0, sizeof y);
    dx7_render(&a, x);
    dx7_render(&b, y);
    if (memcmp(x, y, sizeof x)) {
        snprintf(what + strlen(what), room - strlen(what), "the block after: its sound differs with the phases the "
                 "same");
        failed(what);
        return;
    }
    h1 = dx7_heard(&a);
    for (i = 0; i < DX7_N; i++)
        pk = abs(x[i]) > pk ? abs(x[i]) : pk;
    top = h0 > h1 ? h0 : h1;
    hd_ends_most = pk - top > hd_ends_most ? pk - top : hd_ends_most;
    if (pk > top + 1) {
        snprintf(what + strlen(what), room - strlen(what), "a block peaks at %d, dx7_heard %d before it, %d after",
                 (int)pk, (int)h0, (int)h1);
        failed(what);
    }
    if (pk > h0 + 1) {                   /* (only where a hold ended into a rising stage) */
        int ended = 0;
        char st[120] = "";
        for (i = 0; i < 6; i++) {
            ended |= a0.env[i].staticcount && a.env[i].ix != a0.env[i].ix;
            snprintf(st + strlen(st), sizeof st - strlen(st), " %d:%d%s%s>%d", 6 - i, a0.env[i].ix,
                     a0.env[i].staticcount ? "h" : "", a0.env[i].rising ? "r" : "", a.env[i].ix);
        }
        hd_holds++;
        if (!ended) {
            snprintf(what + strlen(what), room - strlen(what), "a block peaks at %d over dx7_heard %d before it, no "
                     "hold ending (stages%s)", (int)pk, (int)h0, st);
            failed(what);
        }
    }
    if (!dxp[p].made) {
        hd_blocks++;
        if (off != FWD_HELD) {
            hd_rel_most = pk - h0 > hd_rel_most ? pk - h0 : hd_rel_most;
            hd_rel_under += h0 < 33 && pk >= 33;
        } else {
            hd_held_most = pk - h0 > hd_held_most ? pk - h0 : hd_held_most;
            hd_held_under += h0 < 33 && pk >= 33;
        }
        if (h0 > 0 && pk > 0) {
            double db = 20 * log10((double)pk / h0);
            hd_min = db < hd_min ? db : hd_min;
            hd_max = db > hd_max ? db : hd_max;
            hd_sum += db;
            hd_n++;
        }
    }
}

/* the swelling release: let go at its sustain (silent), dx7_heard counts the release's target at once; every block
 * after peaks at most 1 over that, and the swell is heard (the envelopes' levels alone would read it as silent) */
static void dx_swelling(int p)
{
    dx7_voice_t v;
    int32_t h, lv = 0, most = 0, x[DX7_N];
    int i, k, ok = 1;
    memset(&v, 0, sizeof v);
    dx7_note_on(&v, dxp[p].vc, 60, 100, 100);
    for (k = 0; k < (int)(2 * 44100 / 64); k++)   /* (2 s: at its sustain) */
        dx7_skip(&v);
    dx7_note_off(&v);
    h = dx7_heard(&v);
    for (i = 0; i < 6; i++)              /* (algorithm 32: every operator a carrier) */
        lv += (int32_t)(((int64_t)dx7_exp2(v.env[i].level - (14 * (1 << 24))) * DX7_OUT_Q16) >> 29);
    for (k = 0; k < (int)(10 * 44100 / 64); k++) {
        memset(x, 0, sizeof x);
        dx7_render(&v, x);
        for (i = 0; i < DX7_N; i++)
            most = abs(x[i]) > most ? abs(x[i]) : most;
    }
    ok = h >= 33 && lv < 33 && most >= 33 && most <= h + 1;
    printf("  a swelling release, let go at its sustain: dx7_heard %d at once (its levels alone %d), the swell's "
           "peak %d over 10 s %s\n", (int)h, (int)lv, (int)most, ok ? "ok" : "WRONG");
    if (!ok)
        failed("DX7: the swelling release's dx7_heard");
}

/* the LFO's sample-and-hold: its random value stepped once a cycle; dx7_forward counts the cycles mod 128, the order
 * of 179x + 17 on 8 bits from any start (not 256, 179 being 3 mod 4; why 128 is a fact of 179 alone): checked here,
 * every start back after 128 steps, not after 64 */
static void dx_random_period(void)
{
    int x, k, back128 = 1, back64 = 1;
    for (x = 0; x < 256; x++) {
        int y = x;
        for (k = 1; k <= 128; k++) {
            y = (y * 179 + 17) & 0xff;
            if (k == 64)
                back64 &= y == x;
        }
        back128 &= y == x;
    }
    if (!back128 || back64)
        failed("DX7: the sample-and-hold's random sequence does not come round in 128 steps");
}

static const engine_t DX7 = {
    "DX7", 44100.0 / 64, -8, 120, 150, 45, 0, dx_name, dx_made, dx_start, dx_skip, dx_off, dx_fwd, dx_let_go, dx_diff,
    dx_stages, dx_check, 0};

static void dx7_tests(void)
{
    engine_t E = DX7;
    int nb, nd, i, base;
    dx7_init();
    nb = dx_carts(0xF0, 0xFF);
    nd = dx_carts(0x00, 0xEF);
    base = dxn;
    for (i = 0; i < 256; i++) {          /* made at random, within the patches' ranges */
        uint8_t vc[DX7_VCED];
        char name[16];
        dx_random(dxp[i % base].vc, vc, 99);
        snprintf(name, sizeof name, "random %d", i);
        dx_add(name, vc, 1);
    }
    for (i = 0; i < 32; i++) {           /* past them: an operator's rates, levels and output to 127 */
        uint8_t vc[DX7_VCED];
        char name[16];
        dx_random(dxp[i % base].vc, vc, 127);
        snprintf(name, sizeof name, "past %d", i);
        dx_add(name, vc, 2);
    }
    {
        uint8_t vc[DX7_VCED];
        dx_swell(dxp[0].vc, vc);
        dx_add("swell", vc, 1);
    }
    if (!nb)
        failed("DX7: no built-in patch found");
    printf("DX7: %d patches: %d built in, %d the disk's, %d made at random (one with a swelling release), %d past the "
           "patches' ranges; a step a stretch, the LFO's move one, the note off 7\n", dxn, nb, nd, 257, 32);
    dx_random_period();
    E.npatch = dxn;
    drive(&E);
    dx_swelling(dxn - 1);
    printf("  dx7_heard against the next block's peak: every block at most %d over the larger of its values before "
           "and after it, %ld over its value before (a hold ended into a rising stage);\n    the instruments' (%ld "
           "blocks): peak / heard %.1f to %+.1f dB, %.1f dB on average; let go, the peak at most %d over it (%ld "
           "blocks under 33 that peak at 33 or more); held, at most %d over it (%ld)\n", (int)hd_ends_most, hd_holds,
           hd_blocks, hd_min, hd_max, hd_n ? hd_sum / hd_n : 0.0, (int)hd_rel_most, hd_rel_under, (int)hd_held_most,
           hd_held_under);
}

/* =========================================================================================== the Juno-106 === */
typedef struct {
    char name[16];
    uint8_t p[JUNO_PATCH_BYTES], made;
    int32_t level;                       /* the note's level (juno_note_on's, Q12) */
} jup_t;
static jup_t *jup;
static int jun;

static void ju_add(const char *name, const uint8_t *p, int made, int32_t level)
{
    jup = realloc(jup, (size_t)(jun + 1) * sizeof *jup);
    snprintf(jup[jun].name, sizeof jup[jun].name, "%s", name);
    memcpy(jup[jun].p, p, JUNO_PATCH_BYTES);
    jup[jun].made = (uint8_t)made;
    jup[jun].level = level;
    jun++;
}

static int ju_carts(int lo, int hi)      /* the Juno-106 cartridges lo..hi: their patches added */
{
    int num, n = 0, s;
    for (num = lo; num <= hi; num++) {
        rec_cart_t c;
        if (rec_cart((uint8_t)num, &c) || c.kind != REC_JUNO)
            continue;
        for (s = 0; s < c.nslots; s++) {
            const uint8_t *p = rec_slot(&c, (uint8_t)s);
            uint8_t jp[JUNO_PATCH_BYTES];
            char name[16];
            if (!p)
                continue;
            rec_juno_patch(p, jp);
            snprintf(name, sizeof name, "%02X:%02X", num, s);
            ju_add(name, jp, 0, 4096);
            n++;
        }
    }
    return n;
}

/* the switches' bytes at random: RANGE, PULSE, SAW, CHORUS; PWM, ENV polarity, VCA mode, HPF (juno.h) */
static void ju_switches(uint8_t *p)
{
    p[16] = (uint8_t)((1 << rnd(3)) | rnd(2) << 3 | rnd(2) << 4 | (rnd(3) == 0 ? 0x20 : rnd(2) ? 0x40 : 0));
    p[17] = (uint8_t)(rnd(2) | rnd(2) << 1 | rnd(2) << 2 | rnd(4) << 3);
}

/* the sliders at their ends (0 or 127, the envelope's and the LFO's in turn), the switches at random */
static void ju_ends(uint8_t *p, int k)
{
    int i;
    for (i = 0; i < 16; i++)
        p[i] = (uint8_t)(rnd(2) * 127);
    p[11] = (uint8_t)(k & 1 ? 127 : 0);  /* A, D, S, R; the LFO's rate and delay */
    p[12] = (uint8_t)(k & 2 ? 127 : 0);
    p[13] = (uint8_t)(k & 4 ? 127 : 0);
    p[14] = (uint8_t)(k & 8 ? 127 : 0);
    p[0] = (uint8_t)(k & 16 ? 127 : 0);
    p[1] = (uint8_t)(k & 32 ? 127 : 0);
    ju_switches(p);
}

/* the k-th change between calls, alike on a return's voice and its reference (both then read one copy of the patch,
 * as the player's parameter commands turn a sounding voice's): the LFO's RATE, the DECAY, the RELEASE, the SUSTAIN
 * (juno_patch), then the level (juno_level), in turn */
static uint8_t ju_turned[JUNO_PATCH_BYTES];
static void ju_change(fv_t *a, fv_t *b, int k)
{
    static const uint8_t AT[4] = {0, 12, 14, 13};   /* (RATE, D, R, S) */
    if (k == 1)
        memcpy(ju_turned, a->ju.p, JUNO_PATCH_BYTES);
    if (k % 5 == 0) {
        int32_t l = 256 + (k * 7919) % 65280;
        juno_level(&a->ju, l);
        juno_level(&b->ju, l);
        return;
    }
    ju_turned[AT[k % 5 - 1]] = (uint8_t)((ju_turned[AT[k % 5 - 1]] + 23u + (uint32_t)k * 41u) & 127u);
    juno_patch(&a->ju, ju_turned);
    juno_patch(&b->ju, ju_turned);
}

static const char *ju_name(int p) { return jup[p].name; }
static int ju_made(int p) { return jup[p].made; }
static void ju_start(fv_t *v, int p, int note)
{
    memset(&v->ju, 0, sizeof v->ju);
    juno_note_on(&v->ju, jup[p].p, note, jup[p].level);
}
static void ju_skip(fv_t *v) { juno_skip(&v->ju, 1); }   /* (a tick: the unit) */
static void ju_off(fv_t *v) { juno_note_off(&v->ju); }
static int ju_fwd(fv_t *v, uint32_t *on, uint32_t *off, int32_t *steps) { return juno_forward(&v->ju, on, off, steps); }
static int ju_let_go(const fv_t *v) { return !v->ju.gate; }
/* the envelope's stage (its sustain apart: juno.c's stage 1, the DECAY, at env == sus), the LFO's hold and its ramp */
static uint32_t ju_stages(const fv_t *v)
{
    const juno_voice_t *j = &v->ju;
    return j->env_state | (j->env_state == 1 && j->env == j->sus) << 2 | j->in_hold << 3 | (j->lfo_amp == 255) << 4;
}

/* what differs: the firmware loop's state, what each tick makes from it, then anything else of the voice but the
 * oscillator's phase and the sub */
static const char *ju_diff(const fv_t *fa, const fv_t *fb)
{
    static char w[200];
    const juno_voice_t *a = &fa->ju, *b = &fb->ju;
    juno_voice_t x = *a, y = *b;
    if (a->env != b->env || a->env_state != b->env_state || a->gate != b->gate) {
        snprintf(w, sizeof w, "the envelope: %d / %d, stage %d / %d, gate %d / %d", a->env, b->env, a->env_state,
                 b->env_state, a->gate, b->gate);
        return w;
    }
    if (a->lfo_acc != b->lfo_acc || a->lfo_rising != b->lfo_rising || a->lfo_pol != b->lfo_pol ||
        a->in_hold != b->in_hold || a->hold_acc != b->hold_acc || a->ramp_acc != b->ramp_acc ||
        a->lfo_amp != b->lfo_amp) {
        snprintf(w, sizeof w, "the LFO: %d / %d, rising %d / %d, sign %d / %d, hold %d:%d / %d:%d, ramp %d / %d, amp "
                 "%d / %d", a->lfo_acc, b->lfo_acc, a->lfo_rising, b->lfo_rising, a->lfo_pol, b->lfo_pol, a->in_hold,
                 a->hold_acc, b->in_hold, b->hold_acc, a->ramp_acc, b->ramp_acc, a->lfo_amp, b->lfo_amp);
        return w;
    }
    if (a->dac != b->dac || a->dac_s != b->dac_s || a->dphase != b->dphase || a->pw != b->pw ||
        a->vca != b->vca || a->vca_target != b->vca_target) {
        snprintf(w, sizeof w, "a tick's values: cutoff %d / %d (smoothed %d / %d), pitch %u / %u, pulse %u / %u, VCA "
                 "%d / %d (target %d / %d)", (int)a->dac, (int)b->dac, (int)a->dac_s, (int)b->dac_s, a->dphase,
                 b->dphase, a->pw, b->pw, (int)a->vca, (int)b->vca, (int)a->vca_target, (int)b->vca_target);
        return w;
    }
    y.phase = x.phase;
    y.sub = x.sub;
    if (memcmp(&x, &y, sizeof x))
        return "another field of the voice";
    return 0;
}

/* juno_heard against rendered peaks. A block's peak bound: at most 1 over the larger of juno_heard before and after it;
 * with the HPF switch not flat, where its filters ring on after the VCA has fallen (the bass boost's shelves, the HPF:
 * their own memory, after the VCA), over a block k blocks before at its juno_heard x (3/4)^k (counted: how far back),
 * so that an overshoot lasting is caught */
#define JH_BACK 64                       /* (the blocks the HPF's ringing may go back: a bound asserted) */
typedef struct {
    long blocks, ring, n, rel_under, ring_under;
    int back_most;
    int32_t over, rel_most;
    double min, max, sum;                /* the instruments' audible blocks (a peak of 33 or more, the VCA's target not
                                          * 0): peak / heard, dB */
} jh_t;
static jh_t jh_ret = {0, 0, 0, 0, 0, 0, -99, -99, 1e9, -1e9, 0}, jh_thru = {0, 0, 0, 0, 0, 0, -99, -99, 1e9, -1e9, 0};

typedef struct {
    int32_t h[JH_BACK + 1];              /* the larger juno_heard of each block, the latest first */
    int n;
} jh_hist_t;

/* the HPF switch flat (its mode 1, as juno_render reads it) */
static int ju_flat(const juno_voice_t *v) { return 3 - ((v->p[17] >> 3) & 3) == 1; }

/* a block's peak against its juno_heard before (h0) and after (h1) and the history, counted in s (flat: the HPF switch
 * flat, no ringing allowed; open: the VCA's target not 0 after it; inst: an instrument's; let: let go); returns how far
 * back the bound had to look (0: h0 or h1 held it; -1: none) */
static int jh_block(jh_t *s, jh_hist_t *hs, int32_t h0, int32_t h1, int32_t pk, int flat, int open, int inst, int let)
{
    int k, back = -1;
    int32_t top = h0 > h1 ? h0 : h1;
    double most = 0, w = 1;
    memmove(hs->h + 1, hs->h, JH_BACK * sizeof hs->h[0]);
    hs->h[0] = top;
    hs->n += hs->n <= JH_BACK;
    for (k = 0; k < hs->n && k <= (flat ? 0 : JH_BACK); k++, w *= 0.75) {
        most = hs->h[k] * w > most ? hs->h[k] * w : most;
        if (pk <= most + 1) {
            back = k;
            break;
        }
    }
    s->blocks++;
    if (back < 0)
        return back;
    s->ring += back > 0;
    s->back_most = back > s->back_most ? back : s->back_most;
    if (let && h0 < 33 && pk >= 33)      /* (under hearing at unity gain, yet heard: ringing alone) */
        *(back ? &s->ring_under : &s->rel_under) += 1;
    if (back)
        return back;
    top = h0 > h1 ? h0 : h1;
    s->over = pk - top > s->over ? pk - top : s->over;
    if (let)
        s->rel_most = pk - h0 > s->rel_most ? pk - h0 : s->rel_most;
    if (inst && open && pk >= 33) {
        double db = 20 * log10((double)pk / top);
        s->min = db < s->min ? db : s->min;
        s->max = db > s->max ? db : s->max;
        s->sum += db;
        s->n++;
    }
    return 0;
}

static void jh_line(const char *what, const jh_t *s)
{
    printf("    %s: %ld blocks, at most %d over the larger juno_heard before and after; %ld ringing (%d blocks back at "
           "most); the instruments' audible blocks (their VCA's target not 0) peak / heard %.1f to %+.1f dB, %.1f dB on "
           "average; let go, the peak at most %d over juno_heard before it, %ld blocks under 33 before that peak at 33 "
           "or more (%ld ringing)\n",
           what, s->blocks, (int)s->over, s->ring, s->back_most, s->min, s->max, s->n ? s->sum / s->n : 0.0,
           (int)s->rel_most, s->rel_under, s->ring_under);
}

/* the next tick the same; then from both, the phases made the same, blocks rendered as the player ticks them (from a
 * place on its clock made of the case): their sound the same, and each block's peak within juno_heard's bound */
static void ju_check(int p, int note, const fv_t *ref, const fv_t *f, uint32_t on, uint32_t off, char *what,
                     size_t room)
{
    juno_voice_t a = ref->ju, b = f->ju;
    fv_t fa, fb;
    jh_hist_t hs;
    int32_t x[JUNO_N], y[JUNO_N];
    uint32_t acc = (on * 2654435761u + off) & 0xFFFFu;
    int i, k;
    const char *d;
    (void)note;
    juno_skip(&a, 1);
    juno_skip(&b, 1);
    fa.ju = a;
    fb.ju = b;
    if ((d = ju_diff(&fa, &fb)) != 0) {
        snprintf(what + strlen(what), room - strlen(what), "the next tick: %s", d);
        failed(what);
        return;
    }
    b.phase = a.phase;
    b.sub = a.sub;
    memset(&hs, 0, sizeof hs);
    for (k = 0; k < 8; k++) {
        int32_t h0 = juno_heard(&a), h1, pk = 0;
        int tick;
        acc += juno_tick_q16;
        tick = acc >= 65536u;
        acc &= 0xFFFFu;
        juno_block(tick);
        memset(x, 0, sizeof x);
        memset(y, 0, sizeof y);
        juno_render(&a, x, tick);
        juno_render(&b, y, tick);
        if (memcmp(x, y, sizeof x)) {
            snprintf(what + strlen(what), room - strlen(what), "block %d after: its sound differs with the phases the "
                     "same", k);
            failed(what);
            return;
        }
        h1 = juno_heard(&a);
        for (i = 0; i < JUNO_N; i++)
            pk = abs(x[i]) > pk ? abs(x[i]) : pk;
        if (jh_block(&jh_ret, &hs, h0, h1, pk, ju_flat(&a), a.vca_target != 0, !jup[p].made, off != FWD_HELD) < 0) {
            snprintf(what + strlen(what), room - strlen(what), "block %d after it comes back peaks at %d, juno_heard "
                     "%d before it, %d after, none so high before", k, (int)pk, (int)h0, (int)h1);
            failed(what);
            return;
        }
    }
}

/* the note never taken, rendered through (held 3 s, let go 3 s) as the player ticks it: every block's peak within
 * juno_heard's bound */
static void ju_through(int p, int note)
{
    juno_voice_t v;
    jh_hist_t hs;
    int32_t x[JUNO_N];
    uint32_t acc = 0;
    int b, i;
    memset(&v, 0, sizeof v);
    memset(&hs, 0, sizeof hs);
    juno_note_on(&v, jup[p].p, note, jup[p].level);
    for (b = 0; b < 6 * 44100 / JUNO_N; b++) {
        int32_t h0, h1, pk = 0;
        int tick;
        if (b == 3 * 44100 / JUNO_N)
            juno_note_off(&v);
        h0 = juno_heard(&v);
        acc += juno_tick_q16;
        tick = acc >= 65536u;
        acc &= 0xFFFFu;
        juno_block(tick);
        memset(x, 0, sizeof x);
        juno_render(&v, x, tick);
        h1 = juno_heard(&v);
        for (i = 0; i < JUNO_N; i++)
            pk = abs(x[i]) > pk ? abs(x[i]) : pk;
        if (jh_block(&jh_thru, &hs, h0, h1, pk, ju_flat(&v), v.vca_target != 0, !jup[p].made, !v.gate) < 0) {
            char w[160];
            snprintf(w, sizeof w, "Juno-106 %s note %d rendered through: block %d peaks at %d, juno_heard %d before "
                     "it, %d after, none so high in the %d before", jup[p].name, note, b, (int)pk, (int)h0, (int)h1,
                     JH_BACK);
            failed(w);
            return;
        }
    }
}

/* juno_release_ticks against the release's own ticks: a note at the highest level a note has (LEVEL 127, +24 dB), let
 * go at its envelope's top (an attack of 0), by RELEASE, and with VCA GATE: its VCA's target 0 from that tick on, not
 * the tick before; and from lower places (let go during a slow attack) never later */
static void ju_release_table(void)
{
    int r, k, ok = 1, good;
    uint32_t most = 0, right[128];
    for (r = 0; r < 128; r++)
        for (k = 0; k < 3; k++) {        /* (k: let go at the top, during an attack, with VCA GATE) */
            uint8_t pt[JUNO_PATCH_BYTES];
            juno_voice_t v;
            uint32_t t, want, at = 0;
            memset(pt, 0, sizeof pt);
            pt[5] = 127;                 /* VCF FREQ, VCA LEVEL, A, D, S, R */
            pt[10] = 127;
            pt[11] = (uint8_t)(k == 1 ? 90 : 0);
            pt[13] = 127;
            pt[14] = (uint8_t)r;
            pt[16] = 2 | 0x10 | 0x20;    /* 8', SAW, no chorus; ENV, the HPF flat */
            pt[17] = (uint8_t)(2 << 3 | (k == 2 ? 4 : 0));
            memset(&v, 0, sizeof v);
            juno_note_on(&v, pt, 60, 65535);
            for (t = 0; k == 1 && t < (uint32_t)(r * 7 % 50); t++)
                juno_skip(&v, 1);
            good = k == 1 || v.env == 0x3FFF;
            juno_note_off(&v);
            want = juno_release_ticks(pt);
            for (t = 1; t <= 8000; t++) {
                juno_skip(&v, 1);
                if (!at && !v.vca_target)
                    at = t;
                if (at && (v.vca_target || juno_heard(&v)))
                    good = 0;    /* (silent for good once there) */
            }
            if (k == 0)
                right[r] = at;
            if (k == 1 ? !at || at > want : at != want)
                good = 0;
            most = want > most ? want : most;
            if (!good)
                printf("forward: FAILED: Juno-106 RELEASE %d (%s): its VCA's target 0 after %u ticks, the table %u\n",
                       r, k == 0 ? "let go at the top" : k == 1 ? "let go in an attack" : "VCA GATE", at, want);
            ok &= good;
        }
    if (!ok) {
        printf("forward: the release's own ticks from the top, by RELEASE (JUNO_REL_TICKS as it should be):\n");
        for (r = 0; r < 128; r++)
            printf("%s%u,%s", r % 16 ? " " : "    ", right[r], r % 16 == 15 ? "\n" : "");
        failed("Juno-106: juno_release_ticks");
    }
    printf("  juno_release_ticks against the release's own ticks: every RELEASE (0-127) from the envelope's top at the "
           "highest level, its VCA's target 0 from that tick on (%u ticks at most, %.1f s), lower places never later, "
           "VCA GATE 1 %s\n", most, most / (44100.0 / 64 * juno_tick_q16 / 65536), ok ? "ok" : "WRONG");
}

/* the RELEASE's and the DECAY's ticks worked on by stretches (juno.c's env_go) at every distance to their end (1 to
 * 0x3FFF, the sustain's 16 for the DECAY) of every slider value: from there, a few ticks to some hundred (past a
 * stretch's most, 256) in one call, the envelope as calc_decay's own ticks leave it (KR106ADSR CalcDecay, written out
 * here: the D7811G's 16 x 16 multiply from three 8 x 8 products) */
static uint32_t ju_decay(uint32_t v, uint32_t c)
{
    uint32_t vh = v >> 8, vl = v & 255u, ch = c >> 8, cl = c & 255u;
    return (vh * ch + ((vh * cl) >> 8) + ((vl * ch) >> 8)) & 0xFFFFu;
}

static void ju_stretches(void)
{
    uint8_t pt[JUNO_PATCH_BYTES];
    long cases = 0;
    int r, kind, ok = 1;
    for (kind = 0; kind < 2 && ok; kind++)   /* (the RELEASE, then the DECAY to a sustain of 0: 16) */
        for (r = 0; r < 128 && ok; r++) {
            juno_voice_t v0;
            uint32_t d, c, s, top;
            memset(pt, 0, sizeof pt);
            pt[10] = 127;
            pt[12] = (uint8_t)r;         /* D, S 0, R */
            pt[14] = (uint8_t)r;
            pt[16] = 2 | 0x10 | 0x20;
            pt[17] = 2 << 3;
            memset(&v0, 0, sizeof v0);
            juno_note_on(&v0, pt, 60, 4096);   /* (A 0: at the top, the DECAY's start) */
            if (!kind)
                juno_note_off(&v0);
            c = kind ? v0.dec : v0.rel;
            s = kind ? v0.sus : 0u;
            top = 0x3FFFu - s;
            for (d = 1; d <= top && ok; d++) {
                juno_voice_t v = v0;
                uint32_t n = 1u + (d * 7u + (uint32_t)r * 13u) % 300u, off = n, on = n, x = d, k;
                int32_t st = 1 << 30;
                v.env = (uint16_t)(d + s);
                if (kind)
                    juno_forward(&v, &on, &(uint32_t){FWD_HELD}, &st);
                else
                    juno_forward(&v, &(uint32_t){0}, &off, &st);
                for (k = 0; k < n && x; k++)
                    x = ju_decay(x, c);
                cases++;
                if (v.env != x + s) {
                    printf("forward: FAILED: Juno-106 %s %d from %u, %u ticks: %u, its ticks %u\n",
                           kind ? "DECAY" : "RELEASE", r, d, n, v.env - s, x);
                    ok = 0;
                }
            }
        }
    if (!ok)
        failed("Juno-106: the DECAY's and RELEASE's stretches");
    printf("  the RELEASE's and the DECAY's stretches: every slider value from every place (%ld returns of up to 300 "
           "ticks) as calc_decay's own ticks %s\n", cases, ok ? "ok" : "WRONG");
}

static const engine_t JUNO = {
    "Juno-106", 0, -1, 210, 170, 24, 0, ju_name, ju_made, ju_start, ju_skip, ju_off, ju_fwd, ju_let_go, ju_diff,
    ju_stages, ju_check, ju_change};

static void juno_tests(void)
{
    engine_t E = JUNO;
    int nb, nd, i;
    juno_init();
    nb = ju_carts(0xF8, 0xF8);
    nd = ju_carts(0x00, 0xEF);
    for (i = 0; i < 256; i++) {          /* made at random: every slider 0-127 (its ends often), every switch, a level
                                          * up to +24 dB */
        uint8_t p[JUNO_PATCH_BYTES];
        char name[16];
        int k;
        for (k = 0; k < 16; k++)
            p[k] = (uint8_t)(rnd(4) ? rnd(128) : rnd(2) * 127);
        ju_switches(p);
        snprintf(name, sizeof name, "random %d", i);
        ju_add(name, p, 1, rnd(4) ? 4096 : 1 + rnd(65535));
    }
    for (i = 0; i < 64; i++) {           /* the sliders at their ends: the envelope's and the LFO's every way */
        uint8_t p[JUNO_PATCH_BYTES];
        char name[16];
        ju_ends(p, i);
        snprintf(name, sizeof name, "ends %d", i);
        ju_add(name, p, 2, i & 1 ? 65535 : 4096);
    }
    if (!nb)
        failed("Juno-106: no built-in patch found");
    E.per_s = 44100.0 / JUNO_N * juno_tick_q16 / 65536;
    printf("Juno-106: %d patches: %d built in, %d the disk's, %d made at random, %d at the sliders' ends; in ticks "
           "(%.1f a second): the attack a step, the DECAY and the RELEASE a stretch a step (43 and up), 4 ticks a step "
           "(under 43), the LFO's move with the last tick one, the note off one\n", jun, nb, nd, 256, 64, E.per_s);
    E.npatch = jun;
    drive(&E);
    for (i = 0; i < jun; i++)
        if (jup[i].made)
            ju_through(i, 24 + (i * 37) % 85);
        else {
            ju_through(i, 36);
            ju_through(i, 60);
            ju_through(i, 84);
        }
    printf("  juno_heard against the peaks rendered (every block at most 1 over the larger of its values before and "
           "after it; with the HPF switch not flat, where its filters ring on after the VCA closes, under a block k "
           "before at its juno_heard x (3/4)^k):\n");
    jh_line("8 blocks as each comes back", &jh_ret);
    jh_line("the notes never taken, held 3 s, let go 3 s", &jh_thru);
    if (jh_ret.over > 1 || jh_thru.over > 1 || jh_ret.rel_under || jh_thru.rel_under)
        failed("Juno-106: a block over juno_heard");
    ju_stretches();
    ju_release_table();
}

/* ============================================================================================ the sampler === */
typedef struct {
    char name[16];
    samp_t s;                            /* as samp_note_on takes it (the player's pl_sample_of) */
    int32_t offset, bend;                /* its SAMPLE OFFSET; a bend after its note on (1/64 semitones: other steps) */
    uint8_t made;
} smp_t;
static smp_t *smp;
static int smn;

static void sm_add(const char *name, const samp_t *s, int32_t offset, int32_t bend, int made)
{
    smp = realloc(smp, (size_t)(smn + 1) * sizeof *smp);
    snprintf(smp[smn].name, sizeof smp[smn].name, "%s", name);
    smp[smn].s = *s;
    smp[smn].offset = offset;
    smp[smn].bend = bend;
    smp[smn].made = (uint8_t)made;
    smn++;
}

static int sm_carts(int lo, int hi)      /* the sampler cartridges lo..hi: their instruments added, as the player plays
                                          * them (pl_sample_of) */
{
    int num, n = 0, k;
    for (num = lo; num <= hi; num++) {
        rec_cart_t c;
        if (rec_cart((uint8_t)num, &c) || c.kind != REC_SAMPLER)
            continue;
        for (k = 0; k < c.nslots; k++) {
            const uint8_t *p = rec_slot(&c, (uint8_t)k);
            rec_sinst_t si;
            rec_sample_t sa;
            samp_t s;
            char name[16];
            if (!p)
                continue;
            rec_sampler_inst(p, &si);
            if (rec_sample(si.sample, &sa))
                continue;
            memset(&s, 0, sizeof s);
            s.rec = sa.audio;
            s.rec_len = sa.audio_len;
            s.length = (int32_t)sa.length;
            s.rate = sa.rate ? sa.rate : 44100u;
            s.start = (int32_t)si.start;
            s.end = (int32_t)(si.end ? si.end : sa.length);
            s.loop_start = (int32_t)si.loop_start;
            s.loop_end = (int32_t)si.loop_end;
            s.loop = si.loop;
            s.base = si.base;
            s.tune = (int16_t)((int32_t)si.tune - 128);
            memcpy(s.env, si.env, 8);
            if (s.start >= s.length)
                s.start = 0;
            snprintf(name, sizeof name, "%02X:%02X", num, k);
            sm_add(name, &s, 0, 0, 0);
            n++;
        }
    }
    return n;
}

/* a sample's position parameter (START, END, the loop's), as the player's pl_samp_pos: the value x 1/256 of its length */
static int32_t sm_pos(const samp_t *s, uint32_t v)
{
    return v >= 255u ? s->length : (int32_t)(((uint32_t)s->length >> 8) * v + (((uint32_t)s->length & 255u) * v >> 8));
}

/* an instrument made at random on base's sample, of kind k: 0 its envelope over the ranges (holds: a level the one
 * before it; the first level 0; the rates' ends), 1 past them (100-127, samp_note_on's to clamp), 2 a FORWARD loop
 * anywhere (some a few samples long), 3 a PING-PONG one, 4 a start past the loop, END inside the sample; all bent at
 * random (other steps, up to 16 samples a frame) */
static void sm_random(const smp_t *b, smp_t *m, int k)
{
    samp_t *s = &m->s;
    int i, r, len = b->s.length;
    *m = *b;
    m->made = (uint8_t)(k == 1 ? 2 : 1);
    if (k <= 1 || rnd(3) == 0) {
        int top = k == 1 ? 127 : 99;
        for (i = 0; i < 4; i++) {
            r = rnd(8);
            s->env[i] = (uint8_t)(r == 0 ? 0 : r == 1 ? top : r == 2 && k == 1 ? 100 + rnd(28) : rnd(top + 1));
            r = rnd(8);
            s->env[4 + i] = (uint8_t)(r < 2 && i ? s->env[3 + i] : r == 2 ? 0 : r == 3 ? top : rnd(top + 1));
        }
    }
    if ((k == 2 || k == 3 || k == 4) && len > 4) {
        int l = rnd(4) == 0 ? 2 + rnd(15) : 2 + rnd(len - 2);
        s->loop = (uint8_t)(k == 3 || (k == 4 && rnd(2)) ? SAMP_LOOP_PINGPONG : SAMP_LOOP_FORWARD);
        s->loop_start = rnd(len - l + 1);
        s->loop_end = s->loop_start + l;
        s->end = rnd(3) ? len : s->loop_end + rnd(len - s->loop_end + 1);
        s->start = rnd(4) ? 0 : rnd(s->loop_start + 1);
    }
    if (k == 4 && len > 4) {             /* a start past the loop (or near END) */
        m->offset = s->loop_end + rnd(len - s->loop_end + 1);
        if (rnd(2))
            s->end = 1 + rnd(len);
    }
    m->bend = rnd(4) ? (rnd(2) ? 1 : -1) * rnd(48 * 64) : 0;
}

static const char *sm_name(int p) { return smp[p].name; }
static int sm_made(int p) { return smp[p].made; }
static void sm_start(fv_t *f, int p, int note)
{
    memset(&f->sm, 0, sizeof f->sm);
    samp_note_on(&f->sm.v, &smp[p].s, note, smp[p].offset);
    if (smp[p].bend)
        samp_pitch(&f->sm.v, note, smp[p].bend);
    samp_place_of(&f->sm.v, &f->sm.pl);
}
static void sm_skip(fv_t *f) { samp_skip(&f->sm.v); }
static void sm_off(fv_t *f) { samp_note_off(&f->sm.v); }

static long sm_walks;                    /* the places walked, and their most stretches a walk */
static uint32_t sm_walk_most;
static void sm_go(smv_t *f, uint32_t blocks, int held)
{
    uint32_t n = samp_place_go(&f->v.s, &f->pl, f->v.step, blocks * SAMP_N, held);
    sm_walks++;
    sm_walk_most = n > sm_walk_most ? n : sm_walk_most;
}

/* the forward work, and the place moved over the blocks it went through: held, then let go */
static int sm_fwd(fv_t *f, uint32_t *on, uint32_t *off, int32_t *steps)
{
    uint32_t o = *on, l = *off;
    int r = samp_forward(&f->sm.v, on, off, steps);
    if (o != *on)
        sm_go(&f->sm, o - *on, 1);
    if (l != FWD_HELD && l != *off)
        sm_go(&f->sm, l - *off, 0);
    return r;
}
static int sm_let_go(const fv_t *f) { return !f->sm.v.down; }
static uint32_t sm_stages(const fv_t *f) { return f->sm.v.stage | (f->sm.v.statics != 0) << 3; }

/* what differs: once its steps ended it, only that its place did; else its place (position, fraction, way, loop),
 * its envelope and amp, then anything else of the voice */
static const char *sm_diff(const fv_t *fa, const fv_t *fb)
{
    static char w[200];
    const samp_voice_t *a = &fa->sm.v, *b = &fb->sm.v;
    samp_voice_t x = *a, y = *b;
    samp_place_t pl;
    if (!a->on)
        return fb->sm.pl.hi & SAMP_PL_END ? 0 : "its steps ended it, its place goes on";
    if (fb->sm.pl.hi & SAMP_PL_END)
        return "its place ended, its steps go on";
    samp_place_of(a, &pl);
    if (memcmp(&pl, &fb->sm.pl, sizeof pl)) {
        snprintf(w, sizeof w, "the place: position %d / %d, fraction %u / %u, flags %04X / %04X", (int)a->pos,
                 (int)(fb->sm.pl.lo | (fb->sm.pl.hi & SAMP_PL_POS) << 16), (unsigned)a->frac, fb->sm.pl.frac,
                 pl.hi & ~SAMP_PL_POS, fb->sm.pl.hi & ~SAMP_PL_POS);
        return w;
    }
    if (a->stage != b->stage || a->rising != b->rising || a->down != b->down || a->level != b->level ||
        a->target != b->target || a->statics != b->statics || a->amp != b->amp) {
        snprintf(w, sizeof w, "the envelope: level %d / %d, stage %d / %d, static %d / %d, target %d / %d, amp %d / %d",
                 (int)a->level, (int)b->level, a->stage, b->stage, (int)a->statics, (int)b->statics, (int)a->target,
                 (int)b->target, (int)a->amp, (int)b->amp);
        return w;
    }
    y.pos = x.pos;
    y.frac = x.frac;
    y.dir = x.dir;
    y.looping = x.looping;
    if (memcmp(&x, &y, sizeof x))
        return "another field of the voice";
    return 0;
}

/* the k-th change between calls, alike on a return's voice (and its place) and its reference, as the player's
 * pl_param_live turns a sounding sampler voice: a rate, a level (both from the envelope's next block or stage on), END,
 * LOOP (off: out of the loop for good), LOOP START, LOOP END, TUNE (its pitch again), in turn */
static void sm_change(fv_t *fa, fv_t *fb, int k)
{
    samp_voice_t *a = &fa->sm.v, *b = &fb->sm.v;
    uint32_t val = (uint32_t)(k * 7919 + 13) & 255u;
    int i;
    switch (k % 7) {
    case 0:
        i = k / 7 % 4;
        a->s.env[i] = b->s.env[i] = (uint8_t)(val % 100u);
        break;
    case 1:
        i = 4 + k / 7 % 4;
        a->s.env[i] = b->s.env[i] = (uint8_t)(val % 100u);
        break;
    case 2: {
        int32_t e = sm_pos(&a->s, 128u + val / 2u);
        a->s.end = b->s.end = e < 1 ? 1 : e;
        break;
    }
    case 3:
        a->s.loop = b->s.loop = (uint8_t)(val % 3u);
        a->looping = (uint8_t)(a->looping && a->s.loop != SAMP_LOOP_OFF);
        b->looping = (uint8_t)(b->looping && b->s.loop != SAMP_LOOP_OFF);
        if (a->s.loop == SAMP_LOOP_OFF)
            fa->sm.pl.hi &= (uint16_t)~SAMP_PL_LOOP;
        break;
    case 4:
        a->s.loop_start = b->s.loop_start = sm_pos(&a->s, val);
        break;
    case 5:
        a->s.loop_end = b->s.loop_end = sm_pos(&a->s, val);
        break;
    default:
        samp_pitch(a, a->note, (int32_t)val - 128);
        samp_pitch(b, b->note, (int32_t)val - 128);
        break;
    }
}

/* samp_heard against the peaks rendered: every block at most at the larger of its values before and after it, over
 * its value before only where a static stage ended into a rising one in the block; the instruments' audible blocks:
 * peak / heard in dB (the samples under full scale) */
static double sh_min = 1e9, sh_max = -1e9, sh_sum;
static long sh_n, sh_blocks, sh_holds, sm_rendered, sm_ends, sm_ends_never, sm_env_silent, sm_end_env, sm_end_place;
static int32_t sh_over = -99;
#define SM_SILENT ((15 << 24) - (99 << 21))   /* (samp.c's ENV_FULL - ENV_SILENT: let go and at or under it, samp_render
                                               * ends the note) */

#define SM_CAP 3000                      /* (the blocks a return's end is looked for: 4.4 s) */

/* a voice started at the return's place (samp_note_at), its envelope worked forward: 4 blocks rendered as the steps'
 * voice renders them, sample for sample, and samp_heard against their peaks; samp_end_blocks from the place against
 * the block its steps end it in */
static void sm_check(int p, int note, const fv_t *ref, const fv_t *f, uint32_t on, uint32_t off, char *what,
                     size_t room)
{
    samp_voice_t a = ref->sm.v, b;
    uint32_t o = on, l = off;
    int32_t st = 1 << 30, x[SAMP_N], y[SAMP_N], h0, h1, pk;
    int i, k;
    memset(&b, 0, sizeof b);
    samp_note_at(&b, &smp[p].s, note, &f->sm.pl);
    if (smp[p].bend)
        samp_pitch(&b, note, smp[p].bend);
    if (!samp_forward(&b, &o, &l, &st)) {
        snprintf(what + strlen(what), room - strlen(what), "started at its place: not worked forward in one call");
        failed(what);
        return;
    }
    if (a.on && !a.down && a.level <= SM_SILENT) {   /* (its envelope's end, samp_render's: heard 0, the ghost dropped) */
        sm_env_silent++;
        if (samp_heard(&a) && !(a.rising && !a.statics)) {
            snprintf(what + strlen(what), room - strlen(what), "let go, its envelope silent: samp_heard %d",
                     (int)samp_heard(&a));
            failed(what);
            return;
        }
    }
    for (k = 0; k < 4; k++) {
        samp_voice_t a0 = a;
        h0 = samp_heard(&a);
        memset(x, 0, sizeof x);
        memset(y, 0, sizeof y);
        samp_render(&a, x);
        samp_render(&b, y);
        if (memcmp(x, y, sizeof x) || a.on != b.on || (a.on && (a.pos != b.pos || a.frac != b.frac || a.amp != b.amp))) {
            snprintf(what + strlen(what), room - strlen(what), "started at its place: block %d after differs from its "
                     "steps' (position %d / %d, amp %d / %d)", k, (int)a.pos, (int)b.pos, (int)a.amp, (int)b.amp);
            failed(what);
            return;
        }
        sm_rendered++;
        if (a0.on && !a.on) {            /* (ended in the block: by its envelope, or by its place) */
            if (!a.down && a.level <= SM_SILENT)
                sm_end_env++;
            else
                sm_end_place++;
        }
        h1 = samp_heard(&a);
        for (pk = 0, i = 0; i < SAMP_N; i++)
            pk = abs(x[i]) > pk ? abs(x[i]) : pk;
        sh_blocks++;
        sh_over = pk - (h0 > h1 ? h0 : h1) > sh_over ? pk - (h0 > h1 ? h0 : h1) : sh_over;
        if (pk > h0 && pk > h1) {
            snprintf(what + strlen(what), room - strlen(what), "block %d after peaks at %d, samp_heard %d before it, "
                     "%d after", k, (int)pk, (int)h0, (int)h1);
            failed(what);
            return;
        }
        if (pk > h0) {                   /* (only where a static stage ended into a rising one) */
            sh_holds++;
            if (!(a0.statics && a.stage != a0.stage)) {
                snprintf(what + strlen(what), room - strlen(what), "block %d after peaks at %d over samp_heard %d "
                         "before it, no static stage ending (stage %d to %d)", k, (int)pk, (int)h0, a0.stage, a.stage);
                failed(what);
                return;
            }
        }
        if (!smp[p].made && pk >= 33 && h0 > 0) {
            double db = 20 * log10((double)pk / (h0 > h1 ? h0 : h1));
            sh_min = db < sh_min ? db : sh_min;
            sh_max = db > sh_max ? db : sh_max;
            sh_sum += db;
            sh_n++;
        }
    }
    if (ref->sm.v.on) {                  /* its end by itself, from the place */
        samp_voice_t c = ref->sm.v;
        uint32_t e = samp_end_blocks(&c.s, &f->sm.pl, c.step, off == FWD_HELD), at = 0, t;
        for (t = 1; t <= SM_CAP && !at; t++) {
            samp_skip(&c);
            if (!c.on)
                at = t;
        }
        sm_ends++;
        sm_ends_never += e == SAMP_NEVER;
        if (at ? e != at : e <= SM_CAP) {
            snprintf(what + strlen(what), room - strlen(what), "samp_end_blocks %u, its steps end it at block %u", e,
                     at);
            failed(what);
        }
    }
}

/* one frame of samp_skip's walk, as samp.c's loop makes it (sm_frame_check keeps the two the same; the loop's end the
 * lesser of LOOP END and END); 0: it ended */
static int sm_frame(samp_voice_t *v)
{
    int32_t ls = v->s.loop_start, le = v->s.loop_end < v->s.end ? v->s.loop_end : v->s.end, end = v->s.end;
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
    if (v->pos < 0 || (v->pos >= end - 1 && !v->looping)) {
        v->on = 0;
        return 0;
    }
    return 1;
}

/* a state of the walk at random, any way it can stand (a place past the loop or the sample's END, a loop a few
 * samples long or turned over, going back with any loop, a step from 1/65536 of a sample to 16) */
static void sm_state(samp_voice_t *v, int len)
{
    int r = rnd(10);
    memset(v, 0, sizeof *v);
    v->s.length = len;
    v->s.end = rnd(4) ? len : 1 + rnd(len);
    v->s.loop = (uint8_t)rnd(3);
    v->s.loop_start = rnd(len + 1);
    v->s.loop_end = r < 2 ? v->s.loop_start + 2 + rnd(15) : r < 3 ? rnd(v->s.loop_start + 1) : rnd(len + 1);
    if (r >= 3 && v->s.loop_end < v->s.loop_start) {
        int32_t t = v->s.loop_end;
        v->s.loop_end = v->s.loop_start;
        v->s.loop_start = t;
    }
    v->pos = rnd(4) ? rnd(len) : rnd(len + 64);
    v->frac = (uint32_t)rnd(65536);
    v->looping = (uint8_t)(rnd(4) != 0);
    v->dir = (int8_t)(v->looping && rnd(3) == 0 ? -1 : rnd(12) ? 1 : -1);
    v->down = (uint8_t)rnd(2);
    v->on = 1;
    r = rnd(6);
    v->step = r == 0 ? 1u + (uint32_t)rnd(65535) : r == 1 ? (16u << 16) : r == 2 ? (uint32_t)(1 + rnd(16 << 16))
            : (uint32_t)(32768 + rnd(3 << 16));
}

/* sm_frame against samp_skip: 64 of its frames move a voice as one samp_skip */
static void sm_frame_check(void)
{
    int i, k, bad = 0;
    for (i = 0; i < 20000 && !bad; i++) {
        samp_voice_t a, b;
        sm_state(&a, 2 + rnd(3000));
        b = a;
        samp_skip(&a);
        for (k = 0; k < SAMP_N && sm_frame(&b); k++)
            ;
        bad = a.on != b.on || (a.on && (a.pos != b.pos || a.frac != b.frac || a.dir != b.dir ||
                                        a.looping != b.looping));
    }
    if (bad)
        failed("sampler: the test's frame is not samp_skip's");
}

/* the place frame by frame: from states at random, samp_place_go over any count of frames (0 to some 20,000) equal to
 * as many frames of samp_skip's walk, and samp_end_blocks to the block it ends in (or past them) */
static void sm_fuzz(void)
{
    long n, ended = 0, never = 0;
    uint32_t most = 0;
    for (n = 0; n < 200000; n++) {
        samp_voice_t v;
        samp_place_t p;
        uint32_t frames, f, e, st;
        int len = rnd(8) ? 2 + rnd(2000) : 2 + rnd(200000), at = 0;
        char w[240];
        sm_state(&v, len);
        frames = (uint32_t)(rnd(4) ? rnd(3000) : rnd(25000));
        if ((uint64_t)frames * (uint64_t)(len + 17) >= 1u << 29)   /* (a walk run away kept under SAMP_PL_MOST) */
            frames = (1u << 29) / (uint32_t)(len + 17) - 1u;
        samp_place_of(&v, &p);
        e = samp_end_blocks(&v.s, &p, v.step, v.down);
        st = samp_place_go(&v.s, &p, v.step, frames, v.down);
        most = st > most ? st : most;
        for (f = 1; f <= frames && !at; f++)
            if (!sm_frame(&v))
                at = (int)f;
        ended += at != 0;
        never += e == SAMP_NEVER;
        if (at ? !(p.hi & SAMP_PL_END) || e != ((uint32_t)at + SAMP_N - 1) / SAMP_N
               : (p.hi & SAMP_PL_END) || (e != SAMP_NEVER && (uint64_t)e * SAMP_N <= frames)) {
            snprintf(w, sizeof w, "sampler: walked %u frames from a state at random (case %ld): its frames end it at %d, "
                     "the place %s, samp_end_blocks %u", frames, n, at, p.hi & SAMP_PL_END ? "ended" : "goes on", e);
            failed(w);
            return;
        }
        if (!at) {
            samp_place_t q;
            samp_place_of(&v, &q);
            if (memcmp(&p, &q, sizeof p)) {
                snprintf(w, sizeof w, "sampler: walked %u frames from a state at random (case %ld): position %d / %d, "
                         "fraction %u / %u, flags %04X / %04X", frames, n, (int)v.pos, (int)(p.lo | (p.hi & 0x1FFF) << 16),
                         (unsigned)v.frac, p.frac, q.hi & ~SAMP_PL_POS, p.hi & ~SAMP_PL_POS);
                failed(w);
                return;
            }
        }
    }
    printf("  the place frame by frame from states at random: %ld walks (%ld ended in them; samp_end_blocks found no end "
           "for %ld), every one as samp_skip's frames; %u stretches a walk at most\n", n, ended, never, most);
    if (most > 4 || sm_walk_most > 4)
        failed("sampler: a walk over 4 stretches (samp.h says 4 at most)");
}

/* a place run away (a FORWARD loop turned over, its end at 0 and its start near the sample's end: on by some 2^20
 * samples a frame): ended where its position would pass SAMP_PL_MOST, as samp.h says */
static void sm_runaway(void)
{
    samp_voice_t v;
    samp_place_t p;
    uint32_t e, f;
    memset(&v, 0, sizeof v);
    v.s.length = v.s.end = (1 << 20) - 1;
    v.s.loop = SAMP_LOOP_FORWARD;
    v.s.loop_start = (1 << 20) - 2;
    v.s.loop_end = 0;
    v.looping = v.down = v.on = 1;
    v.dir = 1;
    v.step = 3u << 15;
    samp_place_of(&v, &p);
    e = samp_end_blocks(&v.s, &p, v.step, 1);
    for (f = 1; v.pos <= SAMP_PL_MOST; f++)
        sm_frame(&v);
    if (e != (f - 1 + SAMP_N - 1) / SAMP_N)
        failed("sampler: a place run away ends where samp.h says");
    printf("  a place run away (%u frames to pass SAMP_PL_MOST): ended in block %u %s\n", f - 1, e,
           e == (f - 1 + SAMP_N - 1) / SAMP_N ? "ok" : "WRONG");
}

/* the place stretch by stretch: a note's voice stepped by samp_skip, its pitch and its commands changed at stretches'
 * starts (a bend, a slide's steps, TUNE; END, LOOP, the loop's points), held then let go; its place moved by
 * samp_place_go a stretch at a time at each stretch's step equals the voice's after every stretch; samp_end_blocks at
 * each stretch's start says where in it the voice ends, or that it does not */
static void sm_stretches(void)
{
    long cases = 0, ends = 0, stretches = 0;
    int p, r;
    for (p = 0; p < smn; p++)
        for (r = 0; r < (smp[p].made ? 4 : 24); r++) {
            samp_voice_t v;
            samp_place_t pl;
            uint32_t t = 0, held = (uint32_t)rnd(3000), most = held + (uint32_t)rnd(3000);
            int note = 24 + rnd(85), bend = smp[p].bend;
            char w[240];
            memset(&v, 0, sizeof v);
            samp_note_on(&v, &smp[p].s, note, smp[p].offset);
            samp_pitch(&v, note, bend);
            samp_place_of(&v, &pl);
            cases++;
            while (t < most && v.on) {
                uint32_t n = 1u + (uint32_t)(rnd(4) ? rnd(40) : rnd(800)), e, k, at = 0;
                uint32_t room = v.pos >= 1 << 28 ? 0u
                              : (uint32_t)((1 << 28) - v.pos) / (SAMP_N * (uint32_t)(v.s.length + 17));
                int h = t < held;
                if (t == held)           /* (its let-go: a stretch's start) */
                    samp_note_off(&v);
                if (!room)               /* (a loop turned over by its commands runs its place away, as far as the
                                          * voice's own position goes: kept under SAMP_PL_MOST, samp.h) */
                    break;
                n = n > room ? room : n;
                if (h && t + n > held)
                    n = held - t;
                if (rnd(3) == 0) {       /* (its pitch: a bend, or a slide's next step) */
                    bend = rnd(2) ? bend + rnd(129) - 64 : (rnd(2) ? 1 : -1) * rnd(36 * 64);
                    samp_pitch(&v, note, bend);
                }
                if (rnd(12) == 0) {      /* (a command: END, LOOP, the loop's points) */
                    uint32_t val = (uint32_t)rnd(256);
                    switch (rnd(4)) {
                    case 0:
                        v.s.end = sm_pos(&v.s, val) < 1 ? 1 : sm_pos(&v.s, val);
                        break;
                    case 1:
                        v.s.loop = (uint8_t)rnd(3);
                        v.looping = (uint8_t)(v.looping && v.s.loop != SAMP_LOOP_OFF);
                        if (v.s.loop == SAMP_LOOP_OFF)
                            pl.hi &= (uint16_t)~SAMP_PL_LOOP;
                        break;
                    case 2:
                        v.s.loop_start = sm_pos(&v.s, val);
                        break;
                    default:
                        v.s.loop_end = sm_pos(&v.s, val);
                        break;
                    }
                }
                e = samp_end_blocks(&v.s, &pl, v.step, h);
                samp_place_go(&v.s, &pl, v.step, n * SAMP_N, h);
                for (k = 1; k <= n && !at; k++) {
                    samp_skip(&v);
                    if (!v.on)
                        at = k;
                }
                stretches++;
                t += n;
                if (at ? e != at || !(pl.hi & SAMP_PL_END) : e <= n || (pl.hi & SAMP_PL_END)) {
                    snprintf(w, sizeof w, "sampler %s note %d, a stretch of %u blocks at %u: samp_end_blocks %u, its "
                             "steps end it at %u, its place %s", smp[p].name, note, n, t - n, e, at,
                             pl.hi & SAMP_PL_END ? "ended" : "goes on");
                    failed(w);
                    return;
                }
                ends += at != 0;
                if (!at) {
                    samp_place_t q;
                    samp_place_of(&v, &q);
                    if (memcmp(&q, &pl, sizeof q)) {
                        snprintf(w, sizeof w, "sampler %s note %d, a stretch of %u blocks at %u: position %d / %d, "
                                 "fraction %u / %u, flags %04X / %04X", smp[p].name, note, n, t - n, (int)v.pos,
                                 (int)(pl.lo | (pl.hi & SAMP_PL_POS) << 16), (unsigned)v.frac, pl.frac,
                                 q.hi & ~SAMP_PL_POS, pl.hi & ~SAMP_PL_POS);
                        failed(w);
                        return;
                    }
                }
            }
        }
    printf("  the place stretch by stretch (its pitch and commands changed between): %ld notes, %ld stretches, %ld "
           "ending in one, every place as samp_skip's blocks leave it, every end where samp_end_blocks said\n", cases,
           stretches, ends);
}

/* the place a note starts at: samp_place_start as samp_note_on's voice */
static void sm_starts(void)
{
    int p, k, bad = 0;
    for (p = 0; p < smn && !bad; p++)
        for (k = 0; k < 8 && !bad; k++) {
            samp_voice_t v;
            samp_place_t a, b;
            int32_t offset = k == 0 ? smp[p].offset : k == 1 ? 0 : rnd(smp[p].s.length + 100) - 10;
            memset(&v, 0, sizeof v);
            samp_note_on(&v, &smp[p].s, 60, offset);
            samp_place_of(&v, &a);
            samp_place_start(&smp[p].s, offset, &b);
            bad = memcmp(&a, &b, sizeof a) != 0;
        }
    if (bad)
        failed("sampler: samp_place_start is not where samp_note_on starts");
}

static const engine_t SAMP = {
    "sampler", 44100.0 / SAMP_N, 0, 18, 26, 6, 0, sm_name, sm_made, sm_start, sm_skip, sm_off, sm_fwd, sm_let_go,
    sm_diff, sm_stages, sm_check, sm_change};

static void samp_tests(void)
{
    engine_t E = SAMP;
    int nd, i, base, loops[3] = {0, 0, 0};
    nd = sm_carts(0x00, 0xFF);
    base = smn;
    for (i = 0; i < base; i++)
        loops[smp[i].s.loop > 2 ? 0 : smp[i].s.loop]++;
    for (i = 0; i < 320; i++) {
        smp_t m;
        if (!base)
            break;
        sm_random(&smp[i % base], &m, i % 5);
        snprintf(m.name, sizeof m.name, "%s %d", i % 5 == 1 ? "past" : "random", i);
        sm_add(m.name, &m.s, m.offset, m.bend, m.made);
    }
    if (!nd)
        failed("sampler: no sampler instrument found on the disk");
    printf("sampler: %d instruments: %d the disk's (%d one-shot, %d FORWARD, %d PING-PONG), %d made at random (their "
           "envelopes, loops anywhere, starts past the loop, END inside, bends), %d with envelopes past the ranges; a "
           "step a stretch, the note off one\n", smn, nd, loops[0], loops[1], loops[2], smn - nd - 64, 64);
    sm_frame_check();
    sm_starts();
    E.npatch = smn;
    drive(&E);
    printf("  the place moved with the forward work: %ld walks, %u stretches a walk at most\n", sm_walks, sm_walk_most);
    printf("  a voice started at its place (samp_note_at), its envelope worked forward: %ld blocks rendered as its "
           "steps' voice, sample for sample (%ld notes ending in them by their place, %ld by their envelope); "
           "samp_end_blocks against its steps' end: %ld returns (%ld that never end held in their loop, or past %d "
           "blocks); let go with its envelope silent (samp_render's end): samp_heard 0, %ld returns\n", sm_rendered,
           sm_end_place, sm_end_env, sm_ends, sm_ends_never, SM_CAP, sm_env_silent);
    printf("  samp_heard against the next blocks' peaks: every block at most %d over the larger of its values before "
           "and after it, %ld over its value before (a static stage ended into a rising one); the instruments' audible "
           "blocks peak / heard %.1f to %+.1f dB, %.1f dB on average (the samples' own peaks under full scale)\n",
           (int)sh_over, sh_holds, sh_min, sh_max, sh_n ? sh_sum / sh_n : 0.0);
    sm_stretches();
    sm_fuzz();
    sm_runaway();
}

/* =============================================================================================== the CHIP === */
typedef struct {
    char name[16];
    chip_patch_t p;                      /* as chip_note_on takes it (chip_unpack's, the player's) */
    int32_t bend;                        /* a bend after its note on (1/64 semitones: other periods) */
    uint8_t made;
} chp_t;
static chp_t *chp;
static int chn;

static void ch_add(const char *name, const chip_patch_t *p, int32_t bend, int made)
{
    chp = realloc(chp, (size_t)(chn + 1) * sizeof *chp);
    snprintf(chp[chn].name, sizeof chp[chn].name, "%s", name);
    chp[chn].p = *p;
    chp[chn].bend = bend;
    chp[chn].made = (uint8_t)made;
    chn++;
}

static int ch_carts(int lo, int hi)      /* the CHIP cartridges lo..hi: their instruments added */
{
    int num, n = 0, s;
    for (num = lo; num <= hi; num++) {
        rec_cart_t c;
        if (rec_cart((uint8_t)num, &c) || c.kind != REC_CHIP)
            continue;
        for (s = 0; s < c.nslots; s++) {
            const uint8_t *q = rec_slot(&c, (uint8_t)s);
            chip_patch_t p;
            char name[16];
            if (!q)
                continue;
            chip_unpack(q, &p);
            snprintf(name, sizeof name, "%02X:%02X", num, s);
            ch_add(name, &p, 0, 0);
            n++;
        }
    }
    return n;
}

static int ch_count(int kind)            /* a kind's built-in tables */
{
    int n = 0, len, loop;
    while (chip_table(kind, n + 1, &len, &loop))
        n++;
    return n;
}

/* an instrument made at random, of kind k: 0 every field at random over what its slot holds (tables past their
 * counts: none), then 1 every table there at a speed at random; 2 a sweep up or down on any clock, at any pace and
 * shift; 3 the PSG envelope, any shape, a period short or any; 4 the step envelope, the vibrato with its delay, any
 * RELEASE; a third of them bent */
static void ch_random(chip_patch_t *p, int32_t *bend, int k)
{
    uint8_t s[CHIP_SLOT_BYTES];
    int i;
    for (i = 0; i < CHIP_SLOT_BYTES; i++)
        s[i] = (uint8_t)rnd(256);
    chip_unpack(s, p);
    if (k && (k == 1 || rnd(2)))
        for (i = 0; i < CHIP_NTK; i++) {
            p->tab[i] = (uint8_t)(rnd(5) ? 1 + rnd(ch_count(i)) : 0);
            p->spd[i] = (uint8_t)(1 + rnd(4));
        }
    if (k == 2) {
        p->osc = (uint8_t)rnd(4);        /* (not NOISE: it has no sweep) */
        p->sweep = (uint8_t)(1 + rnd(2));
        p->sw_shift = (uint8_t)rnd(8);
        p->sw_pace = (uint8_t)rnd(8);
        p->clock = (uint8_t)rnd(7);
    } else if (k == 3) {
        p->env = CHIP_ENV_PSG;
        p->psg_shape = (uint8_t)rnd(16);
        p->psg_period = (uint16_t)(rnd(2) ? 1 + rnd(40) : 1 + rnd(65535));
    } else if (k == 4) {
        p->env = (uint8_t)(1 + rnd(2));
        p->env_step = (uint8_t)rnd(8);
        p->vib_depth = (uint8_t)(2 * rnd(32));
        p->vib_delay = (uint8_t)(2 * rnd(32));
        p->vib_speed = (uint8_t)(1 + rnd(4));
        p->release = (uint8_t)rnd(16);
    }
    *bend = rnd(3) ? 0 : (rnd(2) ? 1 : -1) * rnd(36 * 64);
}

static const char *ch_name(int p) { return chp[p].name; }
static int ch_made(int p) { return chp[p].made; }
static void ch_start(fv_t *v, int p, int note)
{
    chip_note_on(&v->ch, &chp[p].p, note);   /* (it starts from a voice all zeros) */
    if (chp[p].bend)
        chip_pitch(&v->ch, chp[p].bend);
}
static void ch_skip(fv_t *v) { chip_skip(&v->ch); }
static void ch_off(fv_t *v) { chip_note_off(&v->ch); }
static int ch_fwd(fv_t *v, uint32_t *on, uint32_t *off, int32_t *steps) { return chip_forward(&v->ch, on, off, steps); }
static int ch_let_go(const fv_t *v) { return !v->ch.gate; }
/* where what it keeps changes, roughly: its first 32 frames one by one (the tables mostly move there), the step
 * envelope's volume, the PSG envelope's round (to 3) and running, the sweep, its end */
static uint32_t ch_stages(const fv_t *f)
{
    const chip_voice_t *v = &f->ch;
    return (v->frames < 32u ? v->frames : 32u) | (uint32_t)v->vol << 6 | (v->e_r < 3u ? v->e_r : 3u) << 10 |
           (v->len_e != 0) << 12 | v->sweeping << 13 | v->sw_off << 14 | v->dead << 15;
}

/* the parts the forward work does not keep (the oscillators' phases, the noise register, the band-limited sum) made
 * x's in y */
static void ch_unkept(chip_voice_t *y, const chip_voice_t *x)
{
    y->step = x->step;
    y->sq_step = x->sq_step;
    y->bz_r = x->bz_r;
    y->lfsr = x->lfsr;
    y->nbit = x->nbit;
    y->nx_o = x->nx_o;
    y->nx_q = x->nx_q;
    y->nx_n = x->nx_n;
    y->last = x->last;
    y->acc = x->acc;
    memcpy(y->tail, x->tail, sizeof y->tail);
    memcpy(y->hx, x->hx, sizeof y->hx);
    memcpy(y->hy, x->hy, sizeof y->hy);
    y->ly = x->ly;
}

/* what differs: once its steps ended it, only that the forward work did too; else each part it keeps, then anything
 * else of the voice but what it does not keep */
static const char *ch_diff(const fv_t *fa, const fv_t *fb)
{
    static char w[200];
    const chip_voice_t *a = &fa->ch, *b = &fb->ch;
    chip_voice_t y = *b;
    int k;
    if (a->dead || b->dead)
        return a->dead && b->dead ? 0 : a->dead ? "its steps ended it, the forward work not" : "the forward work ended it, "
               "its steps not";
    for (k = 0; k < CHIP_NTK; k++)
        if (a->tp[k] != b->tp[k] || a->tc[k] != b->tc[k]) {
            snprintf(w, sizeof w, "table %d: place %d / %d, count %d / %d", k, a->tp[k], b->tp[k], a->tc[k], b->tc[k]);
            return w;
        }
    if (a->lvol != b->lvol || a->arp != b->arp || a->cents != b->cents || a->duty != b->duty || a->nrate != b->nrate ||
        a->frames != b->frames || a->f_acc != b->f_acc) {
        snprintf(w, sizeof w, "the frames: %u / %u (clock %u / %u), VOL %d / %d, ARP %d / %d, cents %d / %d",
                 a->frames, b->frames, a->f_acc, b->f_acc, a->lvol, b->lvol, a->arp, b->arp, a->cents, b->cents);
        return w;
    }
    if (a->vol != b->vol || a->e_acc != b->e_acc) {
        snprintf(w, sizeof w, "the step envelope: %d / %d (clock %u / %u)", a->vol, b->vol, a->e_acc, b->e_acc);
        return w;
    }
    if (a->rel != b->rel || a->rel_c != b->rel_c || a->gate != b->gate) {
        snprintf(w, sizeof w, "the release: %d / %d (count %d / %d), gate %d / %d", a->rel, b->rel, a->rel_c, b->rel_c,
                 a->gate, b->gate);
        return w;
    }
    if (a->reg != b->reg || a->len_x != b->len_x || a->s_acc != b->s_acc || a->sweeping != b->sweeping ||
        a->sw_off != b->sw_off) {
        snprintf(w, sizeof w, "the sweep: register %u / %u, length %u / %u, clock %u / %u, on %d / %d, off %d / %d",
                 a->reg, b->reg, a->len_x, b->len_x, a->s_acc, b->s_acc, a->sweeping, b->sweeping, a->sw_off, b->sw_off);
        return w;
    }
    if (a->e_pos != b->e_pos || a->e_r != b->e_r || a->nx_e != b->nx_e || a->len_e != b->len_e) {
        snprintf(w, sizeof w, "the PSG envelope: place %d / %d, round %d / %d, next %u / %u, step %u / %u", a->e_pos,
                 b->e_pos, a->e_r, b->e_r, a->nx_e, b->nx_e, a->len_e, b->len_e);
        return w;
    }
    if (a->len_o != b->len_o || a->len_q != b->len_q || a->len_n != b->len_n || a->silent != b->silent) {
        snprintf(w, sizeof w, "the lengths: %u / %u, %u / %u, %u / %u, silent %d / %d", a->len_o, b->len_o, a->len_q,
                 b->len_q, a->len_n, b->len_n, a->silent, b->silent);
        return w;
    }
    ch_unkept(&y, a);
    if (memcmp(a, &y, sizeof y))
        return "another field of the voice";
    return 0;
}

/* the k-th change between calls, alike on a return's voice and its reference, as the player's pl_param_live turns a
 * sounding CHIP note (chip_param), each code in turn over the calls: a table's speed, RELEASE, the VOL table, FPS, the
 * other tables and speeds, the sweep's PACE, SHIFT and way, CLOCK, ENV, STEP, VOL, CURVE, the PSG envelope's SHAPE and
 * PERIOD, OSC, the vibrato, DUTY, the noise's RATE, BUZZ's SHAPE, STEPS and GATE, the noise's MIX, SHORT and KEY,
 * FILTER, WAVE */
static const struct {
    uint8_t code, top;                   /* a parameter command and its top as chip_set takes it (0: a table, its count
                                          * + 1, one past its tables: none) */
} CH_TURN[] = {{0x68, 3}, {0x57, 15}, {0x60, 0}, {0x4F, 1}, {0x69, 3}, {0x61, 0}, {0x6A, 3}, {0x62, 0}, {0x6B, 3},
               {0x63, 0}, {0x6C, 3}, {0x64, 0}, {0x52, 7}, {0x51, 7}, {0x50, 2}, {0x4C, 6}, {0x55, 3}, {0x56, 7},
               {0x54, 15}, {0x4D, 3}, {0x58, 15}, {0x59, 255}, {0x5A, 255}, {0x40, 4}, {0x6D, 31}, {0x6E, 3},
               {0x6F, 31}, {0x41, 3}, {0x4A, 15}, {0x42, 2}, {0x43, 1}, {0x44, 1}, {0x48, 1}, {0x49, 1}, {0x4B, 1},
               {0x4E, 3}, {0x5B, 15}};
#define CH_TURNS ((int)(sizeof CH_TURN / sizeof CH_TURN[0]))
static uint32_t ch_top(int i) { return CH_TURN[i].top ? CH_TURN[i].top : (uint32_t)ch_count(CH_TURN[i].code - 0x60) + 1u; }

static void ch_change(fv_t *a, fv_t *b, int k)
{
    static unsigned turns;
    int i = (int)(turns++ % CH_TURNS);
    uint32_t val = (uint32_t)(k * 7919 + 13 + (int)turns * 31) % (ch_top(i) + 1u);
    chip_param(&a->ch, CH_TURN[i].code, val);
    chip_param(&b->ch, CH_TURN[i].code, val);
}

/* a note walked on in pieces of any length (a few blocks, or up to 6 s), held, then let go at a round at random (its
 * return under way or not), the forward work called with 1 to 5 steps each time and the pieces added as the time goes
 * on, a parameter turned on both at random between calls (ch_change's codes, values past their tops too; a bend):
 * its state after every call as its steps' to as far; and where a piece starts from a return done, chip_end_blocks
 * there (held, or let go now) against the block its steps end it in within the piece, or none */
static void ch_walks(void)
{
    int p, r, i;
    long calls = 0, turned = 0, lets = 0, ended = 0, ends = 0;
    for (p = 0; p < chn; p++)
        for (i = 0; i < 4; i++) {
            fv_t g, ref;
            chip_voice_t c;
            uint32_t on = 0, off = FWD_HELD, ton = 0, toff = 0, ron = 0, roff = 0, e = 0, t;
            int note = 36 + rnd(48), let = 2 + rnd(50), noted = 0, done = 1, check;
            const char *d;
            ch_start(&g, p, note);
            ch_start(&ref, p, note);
            for (r = 0; r < 60; r++) {
                int32_t st = 1 + rnd(5);
                uint32_t n = rnd(3) ? 1u + (uint32_t)rnd(4) : 1u + (uint32_t)rnd(4000);
                if (r == let) {          /* (let go: its held time done or not) */
                    off = 0;
                    toff = 0;
                    lets++;
                }
                if ((check = done) != 0) {   /* (its end from here, as the piece goes on alone) */
                    c = ref.ch;
                    e = chip_end_blocks(&c, off == FWD_HELD);
                    if (off != FWD_HELD && c.gate)
                        chip_note_off(&c);
                }
                if (off == FWD_HELD) {
                    on += n;
                    ton += n;
                } else if (done || rnd(2)) {
                    off += n;
                    toff += n;
                }
                done = chip_forward(&g.ch, &on, &off, &st);
                calls++;
                if (st < 0) {
                    failed("CHIP: a walk's call left the budget under 0");
                    return;
                }
                for (; ron < ton - on; ron++)
                    chip_skip(&ref.ch);
                if (off != FWD_HELD && !g.ch.gate) {
                    if (!noted) {
                        chip_note_off(&ref.ch);
                        noted = 1;
                    }
                    for (; roff < toff - off; roff++)
                        chip_skip(&ref.ch);
                }
                if ((d = ch_diff((const fv_t *)&ref, (const fv_t *)&g)) != 0) {
                    char w[240];
                    snprintf(w, sizeof w, "CHIP %s note %d: walked, round %d (%u held, %u let go): %s", chp[p].name,
                             note, r, ron, roff, d);
                    failed(w);
                    return;
                }
                if (check && done) {     /* (the piece went on alone: n blocks) */
                    int bad = e > n ? ref.ch.dead : 0;
                    for (t = 1; t <= e && e <= n && !bad; t++) {
                        chip_skip(&c);
                        bad = c.dead != (t == e);
                    }
                    if (bad || (e == 0 && !c.dead)) {
                        char w[200];
                        snprintf(w, sizeof w, "CHIP %s note %d: walked, round %d: chip_end_blocks %u, its steps end it "
                                 "elsewhere in the %u blocks after", chp[p].name, note, r, e, n);
                        failed(w);
                        return;
                    }
                    ends++;
                }
                if (ref.ch.dead) {
                    ended++;
                    break;
                }
                if (rnd(2)) {            /* (a parameter turned; one time in 8 a bend, as a track's pitch moves) */
                    int c = rnd(CH_TURNS);
                    uint32_t val = (uint32_t)rnd((int)ch_top(c) + 1 + (rnd(4) ? 0 : 40));
                    if (rnd(8)) {
                        chip_param(&g.ch, CH_TURN[c].code, val);
                        chip_param(&ref.ch, CH_TURN[c].code, val);
                    } else {
                        chip_pitch(&g.ch, rnd(48 * 64) - 24 * 64);
                        ref.ch.bend64 = g.ch.bend64;
                    }
                    turned++;
                }
            }
        }
    printf("  walked in pieces with parameters turned between calls: %ld calls, %ld parameters turned, %ld let go, "
           "%ld ended; every call's state as its steps', chip_end_blocks right over %ld pieces\n", calls, turned, lets,
           ended, ends);
}

/* a sweep's PACE turned from its slowest to its fastest a few blocks before it runs out of its range (BRK and 8BIT
 * sweeps up and down: its count then past the new step, several ticks in the next block, the last of them its end
 * there): the return to as far, PACE turned on both, then on */
static void ch_pace_turns(void)
{
    int k, d, cases = 0, offs = 0;
    for (k = 0; k < 200; k++) {
        chip_patch_t m;
        chip_voice_t a, b;
        int32_t bend;
        uint32_t t, end = 0, on, off;
        int note = 30 + rnd(70);
        ch_random(&m, &bend, 2);
        m.osc = CHIP_PULSE;
        m.clock = (uint8_t)(rnd(2) ? CHIP_BRK : CHIP_8BIT);
        m.sw_shift = (uint8_t)rnd(3);
        m.sw_pace = 7;
        chip_note_on(&a, &m, note);
        for (t = 1; t <= 20000 && !end; t++) {
            chip_skip(&a);
            if (a.sw_off)
                end = t;
        }
        if (!end)
            continue;
        offs++;
        for (d = 1; d <= 50 && (uint32_t)d < end; d++) {
            int32_t st = 1 << 30;
            const char *w;
            chip_note_on(&a, &m, note);
            chip_note_on(&b, &m, note);
            for (t = 0; t < end - (uint32_t)d; t++)
                chip_skip(&a);
            on = end - (uint32_t)d;
            off = FWD_HELD;
            chip_forward(&b, &on, &off, &st);
            chip_param(&a, 0x52, m.clock == CHIP_8BIT ? 0u : 1u);
            chip_param(&b, 0x52, m.clock == CHIP_8BIT ? 0u : 1u);
            for (t = 0; t < 40; t++)
                chip_skip(&a);
            on = 40;
            st = 1 << 30;
            chip_forward(&b, &on, &off, &st);
            cases++;
            if ((w = ch_diff((const fv_t *)&a, (const fv_t *)&b)) != 0) {
                char what[200];
                snprintf(what, sizeof what, "CHIP: PACE turned %d blocks before its sweep's end (block %u): %s", d, end,
                         w);
                failed(what);
                return;
            }
        }
    }
    printf("  a sweep's PACE turned faster just before it runs out of its range: %d sweeps, %d returns, every one as "
           "its steps\n", offs, cases);
}

/* chip_heard against the blocks rendered after a return: the forward work's voice, its parts not kept made the
 * steps', rendering as the steps' voice sample for sample; each block's peak at most 1 over the most chip_heard of it
 * (before and after) and the blocks before it since the return (chip.h: its 1.5 for the edges' ringing and the
 * high-pass's memory of a louder level), and how many go over the block's own chip_heard (that memory) */
static double chh_min = 1e9, chh_max = -1e9, chh_sum, chh_ring;
static long chh_n, chh_blocks, chh_over, ch_rendered;

static void ch_check(int p, int note, const fv_t *ref, const fv_t *f, uint32_t on, uint32_t off, char *what,
                     size_t room)
{
    chip_voice_t a = ref->ch, b = f->ch;
    int32_t x[CHIP_N], y[CHIP_N], h0, h1, pk, own, most = 0;
    int i, k;
    (void)note;
    (void)on;
    (void)off;
    if (a.dead)
        return;
    ch_unkept(&b, &a);
    for (k = 0; k < 4; k++) {
        h0 = chip_heard(&a);
        memset(x, 0, sizeof x);
        memset(y, 0, sizeof y);
        chip_render(&a, x);
        chip_render(&b, y);
        if (memcmp(x, y, sizeof x) || memcmp(&a, &b, sizeof a)) {
            snprintf(what + strlen(what), room - strlen(what), "block %d after: its sound differs from its steps' with "
                     "the phases the same", k);
            failed(what);
            return;
        }
        ch_rendered++;
        h1 = chip_heard(&a);
        if (h1 > h0) {                   /* (the most still to come never rises as the note goes on untouched) */
            snprintf(what + strlen(what), room - strlen(what), "block %d after: chip_heard rose from %d to %d", k,
                     (int)h0, (int)h1);
            failed(what);
            return;
        }
        for (pk = 0, i = 0; i < CHIP_N; i++)
            pk = abs(x[i]) > pk ? abs(x[i]) : pk;
        chh_blocks++;
        own = h0 > h1 ? h0 : h1;
        most = own > most ? own : most;
        if (pk > most + 1) {
            snprintf(what + strlen(what), room - strlen(what), "block %d after peaks at %d, chip_heard %d before it, %d "
                     "after, %d at the most since its return", k, (int)pk, (int)h0, (int)h1, (int)most);
            failed(what);
            return;
        }
        chh_over += pk > own + 1;
        if (most >= 33 && (double)pk / most > chh_ring)
            chh_ring = (double)pk / most;
        if (!chp[p].made && pk >= 33 && h0 > 0) {
            double db = 20 * log10((double)pk / (h0 > h1 ? h0 : h1));
            chh_min = db < chh_min ? db : chh_min;
            chh_max = db > chh_max ? db : chh_max;
            chh_sum += db;
            chh_n++;
        }
    }
}

/* let go where its release counter steps (the block before, that block, the one after), held 0.1 s and 1 s: each
 * return through the driver */
static void ch_release_points(const engine_t *E)
{
    int p, h, n = 0;
    for (p = 0; p < chn; p++) {
        if (chp[p].made && p % 4)
            continue;
        for (h = 0; h < 2; h++) {
            fv_t ref, prev;
            uint32_t on = units(E, h ? 1 : 0.1), w, then = FWD_HELD, last = FWD_HELD;
            E->start(&ref, p, 60);
            for (w = 0; w < on; w++)
                E->skip(&ref);
            E->note_off(&ref);
            prev = ref;
            for (w = 0; w < 4000 && !prev.ch.dead; w++) {
                if (w && (ref.ch.rel != prev.ch.rel || ref.ch.dead != prev.ch.dead)) {
                    if (w - 1 != last)
                        one(E, p, 60, &prev, on, w - 1);
                    one(E, p, 60, &ref, on, w);
                    last = w;
                    then = w + 1;
                    n++;
                } else if (w == then) {
                    one(E, p, 60, &ref, on, w);
                    last = w;
                }
                prev = ref;
                E->skip(&ref);
            }
        }
    }
    printf("  let go where its release counter steps: %d steps, each with the block before and after\n", n);
}

/* chip_end_blocks against the block chip_skip ends the note in: held from its start (to 10 s; 3 s for the
 * instruments made: a VOL table ends within a second), each block; let go at the start, after a unit, at 0.1 s, 1 s
 * and a time at random, as if let go then (held 0) and let go, then each block to its end */
#define CH_HELD_MOST 6890u               /* (10 s of blocks) */
static void ch_ends(const engine_t *E)
{
    static uint32_t got[CH_HELD_MOST + 1];
    int p, j;
    long blocks = 0, ends = 0, nevers = 0;
    for (p = 0; p < chn; p++) {
        fv_t v;
        uint32_t t, end = CHIP_NEVER, most = chp[p].made ? CH_HELD_MOST * 3 / 10 : CH_HELD_MOST,
                 lets[5] = {0, 1, units(E, 0.1), units(E, 1), (uint32_t)rnd(2000)};
        E->start(&v, p, 60);
        for (t = 0; t <= most && end == CHIP_NEVER; t++) {
            got[t] = chip_end_blocks(&v.ch, 1);
            E->skip(&v);
            if (v.ch.dead)
                end = t + 1;
        }
        for (t = 0; t < (end == CHIP_NEVER ? most + 1 : end); t++, blocks++)
            if (end == CHIP_NEVER ? got[t] != CHIP_NEVER && got[t] <= most - t : got[t] != end - t) {
                char w[160];
                snprintf(w, sizeof w, "CHIP %s: held, chip_end_blocks %u at block %u, its steps end it at block %u",
                         chp[p].name, got[t], t, end);
                failed(w);
                break;
            }
        nevers += end == CHIP_NEVER;
        for (j = 0; j < 5; j++) {        /* let go */
            uint32_t now, as;
            E->start(&v, p, 60);
            for (t = 0; t < lets[j] && !v.ch.dead; t++)
                E->skip(&v);
            if (v.ch.dead)
                continue;
            as = chip_end_blocks(&v.ch, 0);
            chip_note_off(&v.ch);
            if (chip_end_blocks(&v.ch, 1) != as || chip_end_blocks(&v.ch, 0) != as) {
                failed("CHIP: chip_end_blocks let go now and let go differ");
                break;
            }
            for (now = 0, end = CHIP_NEVER; now < 20000 && end == CHIP_NEVER; now++) {
                got[now % (CH_HELD_MOST + 1)] = chip_end_blocks(&v.ch, 1);
                if (v.ch.dead)
                    end = now;
                else
                    E->skip(&v);
            }
            if (end == CHIP_NEVER || end > CH_HELD_MOST || as != end) {
                char w[160];
                snprintf(w, sizeof w, "CHIP %s: let go after %u blocks, chip_end_blocks %u, its steps end it at block %u",
                         chp[p].name, lets[j], as, end);
                failed(w);
                continue;
            }
            for (t = 0; t <= end; t++, blocks++)
                if (got[t] != end - t) {
                    char w[160];
                    snprintf(w, sizeof w, "CHIP %s: let go, chip_end_blocks %u at block %u of its release, its end at "
                             "%u", chp[p].name, got[t], t, end);
                    failed(w);
                    break;
                }
            ends++;
        }
    }
    printf("  chip_end_blocks against the block its steps end it in: %ld blocks, held (%ld never ending within 10 s) "
           "and %ld let go, every one where its steps end it\n", blocks, nevers, ends);
}

static const engine_t CHIP = {
    "CHIP", 44100.0 / CHIP_N, 0, 12, 45, 2, 0, ch_name, ch_made, ch_start, ch_skip, ch_off, ch_fwd, ch_let_go, ch_diff,
    ch_stages, ch_check, ch_change};

static void chip_tests(void)
{
    engine_t E = CHIP;
    int nb, nd, i, base, kinds[5] = {0, 0, 0, 0, 0};
    nb = ch_carts(0xF0, 0xFF);
    nd = ch_carts(0x00, 0xEF);
    base = chn;
    for (i = 0; i < 200; i++) {
        chip_patch_t m;
        int32_t bend;
        char name[16];
        ch_random(&m, &bend, i % 5);
        snprintf(name, sizeof name, "%s %d", i % 5 ? "random" : "past", i);
        ch_add(name, &m, bend, i % 5 ? 1 : 2);
        kinds[i % 5]++;
    }
    if (!nb)
        failed("CHIP: no built-in CHIP instrument found");
    printf("CHIP: %d instruments: %d built in, %d the disk's, %d made at random (%d every field at random, %d every "
           "table, %d sweeps, %d PSG envelopes, %d step envelopes and vibratos); a step a stretch, or one for each 32 "
           "ticks of its sweep where they are more; the note off none\n", chn, nb, nd, chn - base, kinds[0], kinds[1],
           kinds[2], kinds[3], kinds[4]);
    E.npatch = chn;
    drive(&E);
    printf("  the next blocks rendered as its steps' voice's, sample for sample, its parts not kept made the same: %ld "
           "blocks\n", ch_rendered);
    printf("  chip_heard against the next blocks' peaks: %ld blocks, each at most %.3f of the most chip_heard of it and "
           "the blocks before since its return, %ld over its own (the high-pass's memory of a louder level); the "
           "instruments' audible blocks peak / heard %.1f to %+.1f dB, %.1f dB on average\n", chh_blocks, chh_ring,
           chh_over, chh_min, chh_max, chh_n ? chh_sum / chh_n : 0.0);
    ch_release_points(&E);
    ch_walks();
    ch_pace_turns();
    ch_ends(&E);
}

int main(int argc, char **argv)
{
    const char *only = argc > 2 ? argv[2] : "";   /* (one engine's section alone: dx7, juno, samp, chip) */
    if (argc < 2 || disk_load(argv[1])) {
        printf("forward_test DISK [dx7 | juno | samp | chip]\n");
        return 2;
    }
    if (!*only || !strcmp(only, "dx7"))
        dx7_tests();
    if (!*only || !strcmp(only, "juno"))
        juno_tests();
    if (!*only || !strcmp(only, "samp"))
        samp_tests();
    if (!*only || !strcmp(only, "chip"))
        chip_tests();
    if (fails)
        printf("forward: FAILED (%d)\n", fails);
    else
        printf("forward: every engine's forward work (DX7, Juno-106, sampler, CHIP) the same as its own steps, whole, "
               "in pieces and with the time going on; its steps within their ceilings; the sampler's place as its own "
               "steps; the CHIP's end where its steps end it\n");
    return fails != 0;
}
