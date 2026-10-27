/* SPDX-License-Identifier: GPL-3.0-only
 * Copyright (C) 2026 Vasily Kazantsev */
/* PT/FM's records read: see rec.h; the bytes are specs/files/'s. */
#include "rec.h"
#include "../plat.h"
#include "../store/store.h"

/* (a byte at a time, only the bytes the field touches: a bit at a time cost a taken jump a bit, ~800 for a DX7
 * voice at every note) */
uint32_t rec_bits(const uint8_t *p, uint32_t pos, int n)
{
    const uint8_t *b = p + (pos >> 3);
    uint32_t sh = pos & 7u, k;
    uint64_t v;
    if (n <= 0)
        return 0;
    v = b[0];
    for (k = 1; 8u * k < sh + (uint32_t)n; k++)
        v |= (uint64_t)b[k] << (8u * k);
    v >>= sh;
    return (uint32_t)(n >= 32 ? v : v & ((1u << n) - 1u));
}

/* fields one after another from bit 0 of p, a byte read as the fields reach it (rec_dx7_vced's) */
typedef struct {
    const uint8_t *p;
    uint32_t acc, n;                     /* bits waiting, how many */
} rec_br_t;
static inline uint32_t rec_br(rec_br_t *b, uint32_t w)   /* w <= 24 */
{
    uint32_t v;
    while (b->n < w) {
        b->acc |= (uint32_t)*b->p++ << b->n;
        b->n += 8u;
    }
    v = b->acc & ((1u << w) - 1u);
    b->acc >>= w;
    b->n -= w;
    return v;
}

void rec_name(const uint8_t *p, uint32_t pos, int chars, char *out)
{
    static const char SET[] = " ABCDEFGHIJKLMNOPQRSTUVWXYZ0123456789:.-?|/\\*#$@%";
    int i;
    for (i = 0; i < chars; i++) {
        uint32_t c = rec_bits(p, pos + 6u * (uint32_t)i, 6);
        out[i] = c < sizeof SET - 1u ? SET[c] : ' ';
    }
    out[chars] = 0;
    for (i = chars - 1; i >= 0 && out[i] == ' '; i--)   /* (the padding) */
        out[i] = 0;
}

/* ---- songs ---- */
PLAT_COLD int rec_song(uint8_t num, rec_song_t *s)
{
    uint32_t len, at, t, k, nmix;
    uint8_t ver;
    const uint8_t *p = store_get(REC_SONG, num, 0, &len, &ver);
    if (!p || ver != 1 || len < 22u)
        return -1;
    rec_name(p, 0, 16, s->name);
    s->tempo = (uint16_t)(p[12] | p[13] << 8);
    s->ticks = p[14] ? p[14] : 6u;
    s->rpb = p[15] ? p[15] : 4u;
    s->swing = p[16] < 50u ? 50u : p[16] > 75u ? 75u : p[16];
    s->flags = p[17];
    s->loop_from = p[18];
    s->gvol = p[19] > 64u ? 64u : p[19];
    s->len = (uint16_t)(p[20] + 1u);
    if (s->tempo < REC_TEMPO_MIN)
        s->tempo = REC_TEMPO_MIN;
    if (s->tempo > REC_TEMPO_MAX)
        s->tempo = REC_TEMPO_MAX;
    s->order = p + 21;
    at = 21u + s->len;
    if (at >= len)
        return -1;
    nmix = p[at++];
    if (nmix > 16u || at + nmix * RM_N + 64u > len)
        return -1;
    for (t = 0; t < 16u; t++)
        for (k = 0; k < RM_N; k++)
            s->mix[t][k] = t < nmix ? p[at + t * RM_N + k] : (uint8_t)(k == RM_LEVEL ? 0x7Fu : k == RM_PAN ? 0x80u : 0u);
    s->fx = p + at + nmix * RM_N;
    if (s->loop_from >= s->len)
        s->loop_from = 0;
    return 0;
}

/* ---- patterns ---- */
/* a cell's head and fields at p: its row skip, its length (decoded into c when c is not NULL). Nothing is read at
 * or past e: a cell that would run past it is cut there, with a skip that ends the stream's rows */
#define RD(q) ((q) < e ? *(q) : 0u)
static const uint8_t *cell_read(const uint8_t *p, const uint8_t *e, int *skip, rec_cur_t *cur, rec_cell_t *c)
{
    uint32_t h, k, n;
    if (p >= e) {
        *skip = 0x7FFF;
        return e;
    }
    h = *p++;
    if (c) {
        c->has = 0;
        c->n = 0;
    }
    {                                    /* (its length first: past e, it is cut) */
        const uint8_t *q = p + (h & 0x80u ? 1 : 0) + ((h >> 5 & 3u) == 2u ? 1 : (h >> 5 & 3u) == 3u ? 2 : 0) +
                           (h & 0x10u ? 1 : 0);
        if (h & 0x08u)
            q += RD(q) == 0xFFu ? 2u + 2u * RD(q + 1) : 2u;
        if ((h & 7u) == 7u)
            q++;
        if (q > e) {
            *skip = 0x7FFF;
            return e;
        }
    }
    if (h & 0x80u) {                     /* (then its fields, as they are: all before e) */
        if (c) {
            c->has |= RC_NOTE;
            c->note = *p;
        }
        p++;
    }
    switch (h >> 5 & 3u) {
    case 1:                              /* the same instrument as this track's last one here */
        break;
    case 2:                              /* its cartridge, another slot */
        cur->slot = *p++;
        break;
    case 3:
        cur->cart = p[0];
        cur->slot = p[1];
        p += 2;
        break;
    default:
        break;
    }
    if (h & 0x60u) {
        cur->ins = 1;
        if (c) {
            c->has |= RC_INS;
            c->cart = cur->cart;
            c->slot = cur->slot;
        }
    }
    if (h & 0x10u) {
        if (c) {
            c->has |= RC_VOL;
            c->vol = (uint8_t)(*p > 0x7Fu ? 0x7Fu : *p);
        }
        p++;
    }
    if (h & 0x08u) {
        if (p[0] == 0xFFu) {             /* 2-4 commands: 0xFF, the count, each code and value */
            n = p[1];
            p += 2;
        } else {
            n = 1;
        }
        for (k = 0; k < n; k++, p += 2)
            if (c && k < 4u) {
                c->cmd[k][0] = p[0];
                c->cmd[k][1] = p[1];
                c->n = (uint8_t)(k + 1u);
                c->has |= RC_CMD;
            }
    }
    if ((h & 7u) == 7u)
        *skip = *p++;
    else
        *skip = (int)(h & 7u);
    return p;
}
/* Cells passed over without cell_read's whole decoding (a call a cell: walking to each track's stream at a
 * pattern's start, and a cursor's walk, cost ~3x more). The same as cell_read gives, from the cell's head byte and
 * the commands' count alone. cell_end: where the cell at p (p < e) ends, past e too */
static inline const uint8_t *cell_end(const uint8_t *p, const uint8_t *e)
{
    uint32_t h = *p, a = h >> 5 & 3u;
    const uint8_t *q = p + 1u + (h >> 7) + (a == 3u ? 2u : a == 2u ? 1u : 0u) + (h >> 4 & 1u);
    if (h & 0x08u)
        q += RD(q) == 0xFFu ? 2u + 2u * RD(q + 1) : 2u;
    return q + ((h & 7u) == 7u);
}
static inline const uint8_t *cell_skip(const uint8_t *p, const uint8_t *e)   /* cell_read's return (p < e) */
{
    const uint8_t *q = cell_end(p, e);
    return q > e ? e : q;
}
static inline int cell_peek(const uint8_t *p, const uint8_t *e)   /* its *skip (p < e), nothing else */
{
    const uint8_t *q = cell_end(p, e);
    if (q > e)
        return 0x7FFF;
    return (*p & 7u) == 7u ? (int)q[-1] : (int)(*p & 7u);
}
static inline const uint8_t *cell_pass(const uint8_t *p, const uint8_t *e, rec_cur_t *cur)   /* cell_read(c 0):
                                                                                             * its instrument taken */
{
    const uint8_t *q;
    uint32_t h, a;
    if (p >= e)
        return e;
    q = cell_end(p, e);
    if (q > e)
        return e;
    h = *p;
    a = h >> 5 & 3u;
    if (a == 2u) {
        cur->slot = p[1u + (h >> 7)];
    } else if (a == 3u) {
        cur->cart = p[1u + (h >> 7)];
        cur->slot = p[2u + (h >> 7)];
    }
    if (a)
        cur->ins = 1;
    return q;
}

static PLAT_COLD int rec_pattern_walk(uint8_t num, rec_pat_t *pt)
{
    uint32_t sub, len, t, got = 0;
    uint8_t ver;
    const uint8_t *h;
    for (t = 0; t < 16u; t++)
        pt->trk[t] = 0;
    pt->ntrk = 0;
    pt->rows = 1;
    pt->step = 0;
    for (sub = 0; sub < 16u; sub++) {
        const uint8_t *p = store_get(REC_PATTERN, num, (uint8_t)sub, &len, &ver), *q, *end;
        uint32_t bits;
        if (!p || ver != 1 || len < 5u)
            continue;
        if (!got) {
            pt->ntrk = (uint8_t)(p[0] < 1u ? 1u : p[0] > 16u ? 16u : p[0]);
            pt->rows = (uint16_t)(p[1] + 1u > PAT_ROWS_MAX ? PAT_ROWS_MAX : p[1] + 1u);   /* (128 at most) */
            pt->step = p[2];
        }
        got = 1;
        bits = p[3] | (uint32_t)p[4] << 8;
        q = p + 5;
        end = p + len;
        for (t = 0; t < 16u && q < end; t++) {
            const uint8_t *s = q;
            if (!(bits >> t & 1u))
                continue;
            while (q < end && *q)        /* to the stream's end (0x00) */
                q = cell_skip(q, end);
            pt->trk[t] = q < end ? s : 0;   /* (a stream not ending in its part: none) */
            pt->trk_end[t] = end;
            q++;
        }
    }
    if (!store_edit_obj(REC_PATTERN, num))
        return got ? 0 : -1;
    if ((h = store_piece(REC_PATTERN, num, STORE_PC_HEAD, &len)) && len >= 3u) {   /* the edits over it */
        pt->ntrk = (uint8_t)(h[0] < 1u ? 1u : h[0] > 16u ? 16u : h[0]);
        pt->rows = (uint16_t)(h[1] + 1u > PAT_ROWS_MAX ? PAT_ROWS_MAX : h[1] + 1u);
        pt->step = h[2];
        got = 1;
    }
    for (t = 0; t < 16u; t++) {
        const uint8_t *s = store_piece(REC_PATTERN, num, (uint16_t)t, &len);
        if (s) {
            pt->trk[t] = len > 1u ? s : 0;   /* (a lone 0x00: an empty track) */
            pt->trk_end[t] = s + len;
        }
    }
    return got ? 0 : -1;
}

/* the patterns found ahead (rec.h): each entry rec_pattern_walk's answer for its pattern, at the store's generation
 * it was found at. The main loop writes an entry with valid 0 and sets it last; the audio context, which the main
 * loop never interrupts, only reads whole ones */
#define REC_BARRIER() __asm__ __volatile__("" ::: "memory")
volatile uint8_t rec_audio_ctx;
static struct {
    volatile uint8_t valid;
    uint8_t num;
    int8_t ret;
    uint32_t gen;
    rec_pat_t pt;
} rec_ah[REC_AHEAD];
#ifdef REC_AHEAD_CHECK                   /* (host tests: every answer copied checked against the walk) */
#include <stdio.h>
#include <stdlib.h>
static unsigned long rec_ah_hits, rec_ah_audio_hits, rec_ah_audio_walks;
static void rec_ah_report(void)
{
    fprintf(stderr, "rec_ahead: %lu patterns copied, each the walk's; in the audio context %lu copied, %lu walked\n",
            rec_ah_hits, rec_ah_audio_hits, rec_ah_audio_walks);
}
static void rec_ah_reg(void)
{
    static int reg;
    if (!reg++) atexit(rec_ah_report);
}
static void rec_ah_check(uint8_t num, const rec_pat_t *a, int ra)
{
    rec_pat_t b;
    int rb = rec_pattern_walk(num, &b), t, same = ra == rb && a->ntrk == b.ntrk && a->rows == b.rows && a->step == b.step;
    for (t = 0; t < 16 && same; t++)     /* (an empty track's end is never read: the walk leaves it as it was) */
        same = a->trk[t] == b.trk[t] && (!b.trk[t] || a->trk_end[t] == b.trk_end[t]);
    if (!same) {
        fprintf(stderr, "rec_ahead: pattern %02X copied unlike the walk (ret %d/%d, ntrk %d/%d, rows %d/%d, step %d/%d)\n",
                num, ra, rb, a->ntrk, b.ntrk, a->rows, b.rows, a->step, b.step);
        for (t = 0; t < 16; t++)
            if (a->trk[t] != b.trk[t] || (b.trk[t] && a->trk_end[t] != b.trk_end[t]))
                fprintf(stderr, "  track %d: %p..%p, the walk's %p..%p\n", t, (const void *)a->trk[t],
                        (const void *)a->trk_end[t], (const void *)b.trk[t], (const void *)b.trk_end[t]);
        abort();
    }
    rec_ah_hits++;
    rec_ah_reg();
}
#endif
int rec_pattern(uint8_t num, rec_pat_t *pt)
{
    uint32_t g = store_gen(), i;
    for (i = 0; i < REC_AHEAD; i++)
        if (rec_ah[i].valid && rec_ah[i].num == num && rec_ah[i].gen == g) {
            *pt = rec_ah[i].pt;
#ifdef REC_AHEAD_CHECK
            rec_ah_audio_hits += rec_audio_ctx;
            rec_ah_check(num, pt, rec_ah[i].ret);
#endif
            return rec_ah[i].ret;
        }
#ifdef REC_AHEAD_CHECK
    if (rec_audio_ctx) {
        rec_ah_audio_walks++;
        rec_ah_reg();
    }
#endif
    return rec_pattern_walk(num, pt);
}
void rec_ahead(const uint8_t *nums, int n)
{
    uint32_t i;
    int j;
#ifdef REC_AHEAD_OFF                     /* (host A/Bs: never filled, every pattern walked as before) */
    return;
#endif
    for (i = 0; i < REC_AHEAD; i++) {    /* (not wanted any more, or found before a change: gone) */
        int keep = 0;
        if (!rec_ah[i].valid)
            continue;
        for (j = 0; j < n; j++)
            keep |= nums[j] == rec_ah[i].num;
        if (!keep || rec_ah[i].gen != store_gen())
            rec_ah[i].valid = 0;
    }
    for (j = 0; j < n; j++) {
        int have = 0, fr = -1;
        for (i = 0; i < REC_AHEAD; i++) {
            if (rec_ah[i].valid && rec_ah[i].num == nums[j])
                have = 1;
            if (!rec_ah[i].valid && fr < 0)
                fr = (int)i;
        }
        if (have || fr < 0)
            continue;
        rec_ah[fr].num = nums[j];
        rec_ah[fr].gen = store_gen();
        rec_ah[fr].ret = (int8_t)rec_pattern_walk(nums[j], &rec_ah[fr].pt);
        if (rec_ah[fr].gen != store_gen())
            continue;                    /* (the store changed meanwhile: found next time) */
        REC_BARRIER();
        rec_ah[fr].valid = 1;
    }
}

/* the cursor onto the cell at n, the one after a cell on row `after` (-1: the stream's first): its row is
 * its skip after that (the skip is the cell's own, read without taking its instrument) */
static void cur_to(rec_cur_t *c, const uint8_t *n, int after)
{
    int skip = 0;
    c->p = n;
    if (n >= c->e || !*n) {              /* (the stream's end, or its part's) */
        c->row = 0x7FFF;
        return;
    }
    skip = cell_peek(n, c->e);
    c->row = (int16_t)(skip >= 0x7FFF || after + 1 + skip >= 0x7FFF ? 0x7FFF : after + 1 + skip);
}

int rec_pattern_size(uint8_t num, uint8_t *ntrk, uint16_t *rows)
{
    uint32_t len, sub;
    uint8_t ver;
    const uint8_t *h = store_piece(REC_PATTERN, num, STORE_PC_HEAD, &len);
    if (h && len >= 3u) {                /* (its head edited) */
        *ntrk = (uint8_t)(h[0] < 1u ? 1u : h[0] > 16u ? 16u : h[0]);
        *rows = (uint16_t)(h[1] + 1u > PAT_ROWS_MAX ? PAT_ROWS_MAX : h[1] + 1u);
        return 0;
    }
    for (sub = 0; sub < 16u; sub++) {    /* (any part has bytes 0-2) */
        const uint8_t *p = store_get(REC_PATTERN, num, (uint8_t)sub, &len, &ver);
        if (!p || ver != 1 || len < 5u)
            continue;
        *ntrk = (uint8_t)(p[0] < 1u ? 1u : p[0] > 16u ? 16u : p[0]);
        *rows = (uint16_t)(p[1] + 1u > PAT_ROWS_MAX ? PAT_ROWS_MAX : p[1] + 1u);   /* (128 at most) */
        return 0;
    }
    return -1;
}

void rec_cur_set_end(rec_cur_t *c, const uint8_t *stream, const uint8_t *end)
{
    c->s = stream;
    c->e = end;
    c->asked = -1;
    c->cart = c->slot = c->ins = 0;
    c->p = stream;
    c->row = 0x7FFF;
    if (stream)
        cur_to(c, stream, -1);
}
void rec_cur_set(rec_cur_t *c, const uint8_t *stream)   /* (its part's end not known: a part's most, 4 KB) */
{
    rec_cur_set_end(c, stream, stream ? stream + 4096 : 0);
}

void rec_cur_reseat(rec_cur_t *c, const uint8_t *stream, const uint8_t *end)
{
    int asked = c->asked;
    rec_cur_set_end(c, stream, end);
    if (!stream || asked < 0)
        return;
    while (c->row <= asked) {            /* (the cells up to the last row asked for: their instruments taken) */
        int r = c->row;
        cur_to(c, cell_pass(c->p, c->e, c), r);
    }
    c->asked = (int16_t)asked;
}

int rec_cur_at(rec_cur_t *c, int row, rec_cell_t *out)
{
    int skip;
    if (!c->s)
        return 0;
    if (row <= c->asked)                 /* back (or the same row again): from the start */
        rec_cur_set_end(c, c->s, c->e);
    c->asked = (int16_t)row;
    while (c->row < row) {               /* the cells before it (their instruments taken) */
        int r = c->row;
        cur_to(c, cell_pass(c->p, c->e, c), r);
    }
    if (c->row != row)
        return 0;
    cur_to(c, cell_read(c->p, c->e, &skip, c, out), row);
    return 1;
}

/* ---- the loops ---- */
/* the flash's part sub of the loops, in version 4 (another version is as none) */
static const uint8_t *loops_part(uint32_t sub, uint32_t *len)
{
    uint8_t ver;
    const uint8_t *p = store_get(REC_LOOPS, 0, (uint8_t)sub, len, &ver);
    return p && ver == REC_LOOPS_VER ? p : 0;
}
PLAT_COLD int rec_loops(rec_loops_t *l, int streams)
{
    uint32_t len, hl, sub, t, n;
    const uint8_t *h = store_piece(REC_LOOPS, 0, STORE_PC_HEAD, &hl), *f = loops_part(0, &len);
    for (t = 0; t < 16u; t++)
        l->trk[t] = l->trk_end[t] = 0;
    if (!h && f) {                       /* (no edit of it: the flash's, before its streams) */
        h = f;
        hl = len > 5u ? REC_LOOPS_TABLE + REC_LOOPS_ENTRY * (uint32_t)f[5] : len;
        if (hl > len)
            hl = len;
    }
    if (!h || hl < REC_LOOPS_TABLE) {
        l->head = 0;
        l->head_len = 0;
        l->n = l->rows = 0;
        return -1;
    }
    n = h[5] > REC_LOOPS_MAX ? REC_LOOPS_MAX : h[5];
    if (REC_LOOPS_TABLE + REC_LOOPS_ENTRY * n > hl)
        n = (hl - REC_LOOPS_TABLE) / REC_LOOPS_ENTRY;
    l->head = h;
    l->head_len = hl;
    l->n = (uint8_t)n;
    l->rows = (uint8_t)(!n ? 0u : h[7] > PAT_ROWS_MAX ? PAT_ROWS_MAX : h[7] ? h[7] : 1u);
    if (!streams || !n)
        return 0;
    for (sub = 0; f && sub < 16u; sub++) {   /* the flash's streams, in whichever part holds each */
        const uint8_t *p = sub ? loops_part(sub, &len) : f, *q, *end;
        uint32_t at = sub ? 0u : REC_LOOPS_TABLE + REC_LOOPS_ENTRY * (uint32_t)(f[5] > 16u ? 16u : f[5]), bits;
        if (!p || (!sub && !f[5]) || at + 5u > len)
            continue;
        bits = p[at + 3u] | (uint32_t)p[at + 4u] << 8;
        q = p + at + 5u;
        end = p + len;
        for (t = 0; t < 16u && q < end; t++) {
            const uint8_t *s = q;
            if (!(bits >> t & 1u))
                continue;
            while (q < end && *q)
                q = cell_skip(q, end);
            if (q < end) {               /* (a stream not ending in its part: none) */
                l->trk[t] = s;
                l->trk_end[t] = end;
            }
            q++;
        }
    }
    for (t = 0; t < n; t++) {            /* the edits over them */
        const uint8_t *s = store_piece(REC_LOOPS, 0, (uint16_t)t, &len);
        if (s) {
            l->trk[t] = len > 1u ? s : 0;   /* (a lone 0x00: an empty stream) */
            l->trk_end[t] = len > 1u ? s + len : 0;
        }
    }
    for (t = n; t < 16u; t++)            /* (past the loops: none) */
        l->trk[t] = l->trk_end[t] = 0;
    return 0;
}
void rec_loop(const rec_loops_t *l, int i, rec_loop_t *o)
{
    const uint8_t *e = l->head + REC_LOOPS_TABLE + REC_LOOPS_ENTRY * (uint32_t)i;
    o->cart = e[0];
    o->slot = e[1];
    o->colour = (uint8_t)((e[2] & 15u) > 11u ? 0u : e[2] & 15u);
    o->mono = (uint8_t)(e[2] >> 4 & 1u);
    o->muted = (uint8_t)(e[2] >> 5 & 1u);             /* (bit 6, a solo, never read: solos are not saved) */
    o->lanes = (uint8_t)((e[3] & 3u) + 1u);
    o->base = (uint8_t)(e[4] + 1u > PAT_ROWS_MAX ? PAT_ROWS_MAX : e[4] + 1u);
    o->repeats = e[5] ? e[5] : 1u;
    o->mix = e + 6;
}

/* ---- cartridges ---- */
static const uint8_t SLOT_SIZE[REC_CART_LAST + 1] = {0, 112, 28, 24, 40, 16, 28};

int rec_slot_size(uint8_t kind) { return kind >= REC_DX7 && kind <= REC_CART_LAST ? SLOT_SIZE[kind] : 0; }

PLAT_COLD int rec_cart(uint8_t num, rec_cart_t *c)
{
    uint32_t len, hl;
    uint8_t ver;
    int k;
    c->kind = 0;
    for (k = REC_DX7; k <= REC_CART_LAST; k++) {
        const uint8_t *p = store_get((uint8_t)k, num, 0, &len, &ver), *h = 0;
        int ed = store_edit_obj((uint8_t)k, num) != 0;
        if (p && (ver != 1 || len < 10u))
            p = 0;
        if (ed && (h = store_piece((uint8_t)k, num, STORE_PC_HEAD, &hl)) && hl < 10u)
            h = 0;
        if (!p && !h)
            continue;
        c->kind = (uint8_t)k;
        c->num = num;
        c->edited = (uint8_t)ed;
        c->base = p;
        c->base_len = p ? len : 0u;
        c->base_n = p ? p[9] : 0u;
        c->base_ns = p && k == REC_SAMPLER && len > 10u ? p[10] : 0u;
        if (p && k == REC_SAMPLER && 11u + 40u * c->base_n + 16u * c->base_ns > len)
            return c->kind = 0, -1;
        if (p && k != REC_SAMPLER && 10u + (uint32_t)SLOT_SIZE[k] * c->base_n > len)
            return c->kind = 0, -1;
        c->head = h ? h : p;
        c->nslots = c->head[9];
        c->nsamples = k == REC_SAMPLER && (h ? hl : len) > 10u ? c->head[10] : 0u;
        return 0;
    }
    return -1;
}

void rec_cart_name(const rec_cart_t *c, char *out)
{
    if (c->kind)
        rec_name(c->head, 0, 12, out);
    else
        out[0] = 0;
}

/* each kind's name and LEVEL: where they start in the slot (bits) */
static uint32_t name_pos(uint8_t kind)
{
    return kind == REC_DX7 ? 784u : kind == REC_JUNO ? 123u : kind == REC_TB303 ? 42u : kind == REC_SAMPLER ? 174u
         : kind == REC_CHIP ? 121u : 8u;
}

static uint32_t level_pos(uint8_t kind)
{
    return kind == REC_DX7 ? 844u : kind == REC_JUNO ? 183u : kind == REC_TB303 ? 102u : kind == REC_SAMPLER ? 103u
         : kind == REC_CHIP ? 181u : 68u;
}

const uint8_t *rec_slot(const rec_cart_t *c, uint8_t slot)
{
    const uint8_t *s = 0;
    uint32_t len;
    if (!c->kind || slot >= c->nslots)
        return 0;
    if (c->edited && (s = store_piece(c->kind, c->num, slot, &len)) && len != (uint32_t)SLOT_SIZE[c->kind])
        return 0;
    if (!s) {                            /* (as the flash has it) */
        if (!c->base || slot >= c->base_n)
            return 0;
        s = c->base + (c->kind == REC_SAMPLER ? 11u : 10u) + (uint32_t)SLOT_SIZE[c->kind] * slot;
    }
    if (rec_bits(s, name_pos(c->kind), 6) == REC_NAME_EMPTY)
        return 0;
    return s;
}

int8_t rec_level(uint8_t kind, const uint8_t *slot) { return (int8_t)rec_bits(slot, level_pos(kind), 8); }

void rec_slot_name(uint8_t kind, const uint8_t *slot, char *out) { rec_name(slot, name_pos(kind), 10, out); }

/* the DX7's fields, in the slot's order (specs/files/instruments.md#dx7): each operator 6 to 1, then the
 * voice; MSFA's layout keeps the same order, a byte each */
void rec_dx7_vced(const uint8_t *slot, uint8_t *vced)
{
    static const uint8_t OPW[21] = {7, 7, 7, 7, 7, 7, 7, 7, 7, 7, 7, 2, 2, 3, 2, 3, 7, 1, 5, 7, 4};
    static const uint8_t VW[19] = {7, 7, 7, 7, 7, 7, 7, 7, 5, 3, 1, 7, 7, 7, 7, 1, 3, 3, 6};
    rec_br_t b = {slot, 0, 0};
    uint32_t i, op;
    for (op = 0; op < 6u; op++)
        for (i = 0; i < 21u; i++)
            vced[op * 21u + i] = (uint8_t)rec_br(&b, OPW[i]);
    for (i = 0; i < 19u; i++)
        vced[126u + i] = (uint8_t)rec_br(&b, VW[i]);
    for (i = 145; i < 156u; i++)         /* (the name and the rest: not read by the engine) */
        vced[i] = 0;
}

void rec_juno_patch(const uint8_t *slot, uint8_t *p)
{
    uint32_t i, range, chorus, hpf;
    for (i = 0; i < 16u; i++)
        p[i] = (uint8_t)rec_bits(slot, 7u * i, 7);
    range = rec_bits(slot, 112, 2);
    chorus = rec_bits(slot, 116, 2);
    p[16] = (uint8_t)((range == 0u ? 1u : range == 1u ? 2u : 4u) | rec_bits(slot, 114, 1) << 3 |
                      rec_bits(slot, 115, 1) << 4 | (chorus == 0u ? 0x20u : chorus == 1u ? 0x40u : 0u));
    hpf = rec_bits(slot, 121, 2);
    p[17] = (uint8_t)(rec_bits(slot, 118, 1) | rec_bits(slot, 119, 1) << 1 | rec_bits(slot, 120, 1) << 2 |
                      (3u - hpf) << 3);
}

void rec_tb_knobs(const uint8_t *slot, uint8_t *k)
{
    uint32_t i;
    for (i = 0; i < 6u; i++)
        k[i] = (uint8_t)rec_bits(slot, 7u * i, 7);
}

uint8_t rec_fusion_pattern(const uint8_t *slot) { return slot[0]; }

void rec_sampler_inst(const uint8_t *slot, rec_sinst_t *si)
{
    uint32_t i;
    si->sample = (uint8_t)rec_bits(slot, 0, 6);
    si->start = rec_bits(slot, 6, 20);
    si->end = rec_bits(slot, 26, 20);
    si->loop_start = rec_bits(slot, 46, 20);
    si->loop_end = rec_bits(slot, 66, 20);
    si->loop = (uint8_t)rec_bits(slot, 86, 2);
    si->base = (uint8_t)rec_bits(slot, 88, 7);
    si->tune = (uint8_t)rec_bits(slot, 95, 8);
    si->level = (int8_t)rec_bits(slot, 103, 8);
    si->volume = rec_sampler_volume(slot);
    for (i = 0; i < 8u; i++)
        si->env[i] = (uint8_t)rec_bits(slot, 118u + 7u * i, 7);
}

uint8_t rec_sampler_volume(const uint8_t *slot) { return (uint8_t)rec_bits(slot, 111, 7); }

uint32_t rec_slot_at(const rec_cart_t *c, uint8_t slot)
{
    return (c->kind == REC_SAMPLER ? 11u : 10u) + (uint32_t)SLOT_SIZE[c->kind] * slot;
}

const uint8_t *rec_sample_entry(const rec_cart_t *c, uint8_t idx)
{
    const uint8_t *e;
    uint32_t len;
    if (c->kind != REC_SAMPLER || idx >= c->nsamples)
        return 0;
    if (c->edited && (e = store_piece(REC_SAMPLER, c->num, STORE_PC_TABLE, &len)))    /* (its table edited) */
        return 16u * (idx + 1u) <= len ? e + 16u * idx : 0;
    if (!c->base || idx >= c->base_ns)
        return 0;
    return c->base + 11u + 40u * c->base_n + 16u * idx;
}

int rec_sample(const rec_cart_t *c, uint8_t idx, rec_sample_t *s)
{
    const uint8_t *e = rec_sample_entry(c, idx);
    if (!e)
        return -1;
    if (rec_bits(e, 42, 6) == REC_NAME_EMPTY)
        return -1;
    s->length = rec_bits(e, 0, 20);
    s->rate = rec_bits(e, 20, 18);
    s->coding = (uint8_t)rec_bits(e, 38, 4);
    return 0;
}
