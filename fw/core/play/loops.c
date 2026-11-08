/* SPDX-License-Identifier: GPL-3.0-only
 * Copyright (C) 2026 Vasily Kazantsev */
/* LOOPS' loops, kept as the store's pieces (loops.h; specs/files/loops.md, patterns.md for a stream's bytes). */
#include <string.h>
#include "loops.h"
#include "../plat.h"
#include "player.h"
#include "rec.h"
#include "../store/store.h"

#define TABLE LP_HEAD
#define ENTRY LP_ENTRY
#define FX_LEN 64u
#define NEXT_MIX 72                     /* the next loop's mix in the head */
#define JOBS 78                         /* K1-K4's jobs and VIEW in the head (bytes 78-79) */
static const uint8_t MIX_DEF[RM_N] = {0x7F, 0x80, 0, 0, 0, 0};
static const uint8_t GRID_RPB[3] = {4, 6, 8};
static const uint8_t JOB_DEF[4] = {LJ_LEVEL, LJ_PAN, LJ_REVERB, LJ_TEMPO};   /* K1-K4's jobs not stored */
static const uint8_t ED0[2] = {0x0E, 0xD0};             /* a note delay with no note: a gap's bridge */
static const uint8_t ZERO1[1] = {0};                    /* an empty stream */

static int clamp(int v, int lo, int hi) { return v < lo ? lo : v > hi ? hi : v; }
static uint32_t a4(uint32_t n) { return (n + 3u) & ~3u; }
static int all_zero(const uint8_t *p, uint32_t n)
{
    while (n--) if (*p++) return 0;
    return 1;
}

void (*loops_fx_new)(uint8_t *s64);
const uint8_t *loops_keys_mix;
/* the next loop's mix as it is heard and as REC writes it into a new loop: its LEVEL, PAN and DUCK the head's (stored:
 * its 6 bytes; NULL or all 0 a new song's track's), its CHORUS, DELAY and REVERB SENDS the KEYS column's (the head's sends
 * kept as written, not read); with no KEYS column known (loops_keys_mix NULL) the head's own */
static void next_mix(const uint8_t *stored, uint8_t *m)
{
    memcpy(m, !stored || all_zero(stored, RM_N) ? MIX_DEF : stored, RM_N);
    if (loops_keys_mix) memcpy(m + RM_CHORUS, loops_keys_mix + RM_CHORUS, RM_N - RM_CHORUS);
}
static volatile uint8_t def_rows = 64;  /* the length of the last head made new (4 bars at 1/16): the loops' clock with
                                         * no record (src_pattern, the audio context: no hook called there) */
uint32_t loops_record_default(uint8_t *p)
{
    memset(p, 0, TABLE);
    p[0] = 120;                         /* TEMPO 120 */
    p[2] = 6 | 0x80;                    /* TICKS A ROW 6, COUNT-IN OFF, MONO LEGATO JUMP (bit 7) */
    p[3] = 50;                          /* SWING 50 % */
    /* (byte 4 0: the grid 1/16, QUANTIZE ROW, the keys POLY; 5-6 0: no loops, the placeholder; 78-79 0: VIEW 2 BARS,
     * K1-K4's jobs not stored) */
    if (loops_fx_new) loops_fx_new(p + 8);   /* the FX setup: a new song's (an all-0 one has no chain: the FX
                                              * screen could switch nothing on) */
    memcpy(p + NEXT_MIX, MIX_DEF, RM_N);     /* the next loop's mix: plain, as a new song's track */
    p[7] = (uint8_t)rec_loops_rows_default(p);   /* the length: 4 bars */
    def_rows = p[7];
    return TABLE;
}

/* an FX setup's chain names no effect twice (specs/files/songs.md#the-fx-setup: 4 bits an item, 15 after the last) */
static int chain_ok(const uint8_t *s)
{
    uint32_t seen = 0;
    for (int i = 0; i < 12; i++) {
        int v = s[1 + i / 2] >> (4 * (i % 2)) & 15;
        if (v == 15) break;
        if (seen >> v & 1u) return 0;
        seen |= 1u << v;
    }
    return 1;
}
/* the FX setup as LOOPS has it (specs/files/loops.md, bytes 8-71): all 0 a new song's (def); a chain naming an effect
 * twice (read as twelve GATEs) a new song's chain and routing, the rest kept. def NULL: none known, left */
static void fx_mend(uint8_t *s, const uint8_t *def)
{
    if (!def) return;
    if (all_zero(s, FX_LEN)) memcpy(s, def, FX_LEN);
    else if (!chain_ok(s)) memcpy(s, def, 7);
}

/* ------------------------------------------------------------------- the head --- */
/* The main loop's copy of the head (mended), taken again whenever the store changed; the selection in RAM. The audio
 * context never reads it: the source reads the store (rec_loops), its FX setup into fx_au, a buffer of its own. */
static uint8_t hd[LP_HEAD_MAX];
static uint32_t hd_gen;
static uint8_t hd_ok, sel_ram, sel_ok;
static uint16_t solo_ram;               /* the solos, RAM's alone: never saved (an entry's bit 6 written 0, never read) */
static uint8_t fx_def[FX_LEN], def_ok;  /* a new song's FX setup (the first look takes it) */
static uint8_t fx_au[FX_LEN];           /* the source's FX setup: the audio context's alone */

static const uint8_t *head(void)
{
    rec_loops_t l;
    if (hd_ok && hd_gen == store_gen()) return hd;
    if (!def_ok && loops_fx_new) { loops_fx_new(fx_def); def_ok = 1; }
    memset(hd, 0, sizeof hd);
    if (rec_loops(&l, 0)) loops_record_default(hd);
    else {
        memcpy(hd, l.head, TABLE + ENTRY * (uint32_t)l.n);
        hd[5] = l.n;
        hd[7] = l.rows;
        if (all_zero(hd + NEXT_MIX, RM_N)) memcpy(hd + NEXT_MIX, MIX_DEF, RM_N);
        fx_mend(hd + 8, def_ok ? fx_def : 0);
    }
    if (!sel_ok) { sel_ram = hd[6]; sel_ok = 1; }
    solo_ram &= (uint16_t)((1u << hd[5]) - 1u);         /* (none past the loops) */
    hd_gen = store_gen();
    hd_ok = 1;
    return hd;
}
static int copy_head(uint8_t *h)        /* the head into h (LP_HEAD_MAX), to change: the loops */
{
    const uint8_t *p = head();
    memcpy(h, p, TABLE + ENTRY * (uint32_t)p[5]);
    return p[5];
}
static const uint8_t *entry(int i) { return head() + TABLE + ENTRY * i; }
static int result(int r) { return r == STORE_OK ? LP_OK : r == STORE_ERR_FULL || r == STORE_ERR_BIG ? LP_FULL : LP_BUSY; }
/* the edit memory a piece of len takes more than it does now (at most: as the store would lay it) */
static uint32_t need_for(uint16_t id, uint32_t len)
{
    uint32_t had;
    if (!store_piece(REC_LOOPS, 0, id, &had)) return a4(len) + 8u;
    return a4(len) > a4(had) ? a4(len) : 0u;
}
static int sel_in(int s, int n) { return clamp(s, 0, n < LP_MAX ? n : LP_MAX - 1); }
/* the head h written (its selection the RAM's), the streams' head with it when the count or the length changed (none
 * without loops; the length kept in byte 7 with loops or none) */
static int put_head(uint8_t *h)
{
    const uint8_t *was = head();
    int n = h[5], ph = n != was[5] || (n && h[7] != was[7]);
    uint32_t hl = TABLE + ENTRY * (uint32_t)n;
    h[6] = (uint8_t)sel_in(sel_ram, n);
    if (store_edit_room() < need_for(STORE_PC_HEAD, hl) + (ph && n ? need_for(STORE_PC_PHEAD, 3) : 0u))
        return LP_FULL;
    int r = store_edit_piece(REC_LOOPS, 0, STORE_PC_HEAD, LOOPS_REC_VER, h, hl);
    if (r == STORE_OK && ph) {
        uint8_t p[3] = {(uint8_t)n, (uint8_t)(h[7] - 1), 0};
        r = n ? store_edit_piece(REC_LOOPS, 0, STORE_PC_PHEAD, LOOPS_REC_VER, p, 3)
              : store_edit_forget_piece(REC_LOOPS, 0, STORE_PC_PHEAD);
    }
    if (r == STORE_OK) sel_ram = h[6];
    return result(r);
}
static int set_bits(int at, int mask, int v)
{
    uint8_t h[LP_HEAD_MAX];
    copy_head(h);
    int nv = (h[at] & ~mask) | (v & mask);
    if (nv == h[at]) return LP_OK;
    h[at] = (uint8_t)nv;
    return put_head(h);
}

int loops_n(void) { return head()[5]; }
int loops_tempo(void) { const uint8_t *h = head(); return clamp(h[0] | h[1] << 8, (int)REC_TEMPO_MIN, (int)REC_TEMPO_MAX); }
int loops_set_tempo(int t)
{
    uint8_t h[LP_HEAD_MAX];
    copy_head(h);
    t = clamp(t, (int)REC_TEMPO_MIN, (int)REC_TEMPO_MAX);
    if ((h[0] | h[1] << 8) == t) return LP_OK;
    h[0] = (uint8_t)t;
    h[1] = (uint8_t)(t >> 8);
    return put_head(h);
}
int loops_ticks(void) { int t = head()[2] & 31; return t ? t : 6; }
int loops_set_ticks(int t) { return set_bits(2, 31, clamp(t, 1, 31)); }
int loops_countin(void) { int c = head()[2] >> 5 & 3; return c == 3 ? 0 : c; }
int loops_set_countin(int c) { return set_bits(2, 0x60, clamp(c, 0, 2) << 5); }
int loops_legato(void) { const uint8_t *h = head(); return h[4] & 0x40 ? LG_LEGATO : h[2] >> 7; }
int loops_set_legato(int j)             /* (LEGATO: byte 4's bit 6, byte 2's bit 7 then 0; both in one write) */
{
    uint8_t h[LP_HEAD_MAX];
    copy_head(h);
    int b2 = (h[2] & 0x7F) | (j == LG_JUMP ? 0x80 : 0), b4 = (h[4] & 0xBF) | (j == LG_LEGATO ? 0x40 : 0);
    if (b2 == h[2] && b4 == h[4]) return LP_OK;
    h[2] = (uint8_t)b2;
    h[4] = (uint8_t)b4;
    return put_head(h);
}
int loops_swing(void) { return clamp(head()[3], 50, 75); }
int loops_set_swing(int s) { return set_bits(3, 0xFF, clamp(s, 50, 75)); }
int loops_grid(void) { int g = head()[4] & 3; return g > 2 ? 0 : g; }
int loops_set_grid(int g)               /* (only with no loops; the length its whole bars, as many as fit) */
{
    uint8_t h[LP_HEAD_MAX];
    if (loops_n()) return LP_NO;
    g = clamp(g, 0, 2);
    if (g == loops_grid()) return LP_OK;
    copy_head(h);
    int bars = h[7] / loops_bar_rows(), nb = GRID_RPB[g] * loops_beats(), most = clamp((int)PAT_ROWS_MAX / nb, 1, 16);
    h[4] = (uint8_t)((h[4] & ~3) | g);
    h[7] = (uint8_t)(clamp(bars, 1, most) * nb);
    return put_head(h);
}
int loops_quant(void) { int q = head()[4] >> 2 & 7; return q > LQ_REAL ? LQ_ROW : q; }
int loops_set_quant(int q) { return set_bits(4, 0x1C, clamp(q, 0, LQ_REAL) << 2); }
int loops_keys_mono(void) { return head()[4] >> 5 & 1; }
int loops_set_keys_mono(int m) { return set_bits(4, 0x20, m ? 0x20 : 0); }
int loops_view(void) { int v = head()[JOBS] >> 6; return v == 1 ? 1 : v == 2 ? 4 : 2; }
int loops_set_view(int bars)
{
    if (bars != 1 && bars != 2 && bars != 4) return LP_NO;
    return set_bits(JOBS, 0xC0, (bars == 1 ? 1 : bars == 4 ? 2 : 0) << 6);
}
int loops_job(int k)                    /* (K1 and K2 in byte 78's bits 2-0 and 5-3, K3 and K4 in byte 79's) */
{
    const uint8_t *h = head();
    if (k < 0 || k > 3) return LJ_LEVEL;
    return h[JOBS + 1] & 0x40 ? h[JOBS + k / 2] >> (3 * (k & 1)) & 7 : JOB_DEF[k];
}
int loops_set_job(int k, int job)
{
    uint8_t h[LP_HEAD_MAX];
    int j[4];
    if (k < 0 || k > 3 || job < 0 || job > LJ_VIEW) return LP_NO;
    for (int x = 0; x < 4; x++) j[x] = x == k ? job : loops_job(x);
    copy_head(h);
    int b78 = (h[JOBS] & 0xC0) | j[0] | j[1] << 3, b79 = (h[JOBS + 1] & 0x80) | j[2] | j[3] << 3 | 0x40;
    if (b78 == h[JOBS] && b79 == h[JOBS + 1]) return LP_OK;
    h[JOBS] = (uint8_t)b78;
    h[JOBS + 1] = (uint8_t)b79;
    return put_head(h);
}
int loops_sel(void) { head(); return sel_in(sel_ram, loops_n()); }
void loops_set_sel(int s) { head(); sel_ram = (uint8_t)sel_in(s, loops_n()); }
int loops_keep_sel(void)               /* (only when LOOPS has changes: alone it is none) */
{
    uint8_t h[LP_HEAD_MAX];
    copy_head(h);
    return h[6] == loops_sel() || !store_edit_obj(REC_LOOPS, 0) ? LP_OK : put_head(h);
}
const uint8_t *loops_fx(void) { return head() + 8; }
int loops_set_fx(const uint8_t *s64)
{
    uint8_t h[LP_HEAD_MAX];
    copy_head(h);
    if (!memcmp(h + 8, s64, FX_LEN)) return LP_OK;
    memcpy(h + 8, s64, FX_LEN);
    return put_head(h);
}
int loops_rpb(void) { return GRID_RPB[loops_grid()]; }
int loops_beats(void) { int b = head()[8 + 11]; return b < 1 ? 4 : b > 16 ? 16 : b; }   /* the METRONOME's BEATS A BAR */
int loops_bar_rows(void) { return loops_rpb() * loops_beats(); }
int loops_max_bars(void) { return clamp((int)PAT_ROWS_MAX / loops_bar_rows(), 1, 16); }
int loops_max_rows(void) { return loops_max_bars() * loops_bar_rows(); }
int loops_rows(void) { return head()[7]; }
int loops_rows_step(int rows, int d)
{
    int br = loops_bar_rows();
    return clamp(d > 0 ? rows / br + 1 : (rows + br - 1) / br - 1, 1, loops_max_bars()) * br;
}
int loops_bpm(void) { return (loops_tempo() * 24 * 2 / (loops_ticks() * loops_rpb()) + 1) / 2; }

/* ------------------------------------------------------------------ the loops --- */
static int real(int i) { return i >= 0 && i < loops_n(); }
uint16_t loops_inst(int i) { return real(i) ? (uint16_t)(entry(i)[0] << 8 | entry(i)[1]) : 0; }
int loops_colour(int i) { int c = real(i) ? entry(i)[2] & 15 : 0; return c > 11 ? 0 : c; }
int loops_mono(int i) { return real(i) ? entry(i)[2] >> 4 & 1 : 0; }
int loops_muted(int i) { return real(i) ? entry(i)[2] >> 5 & 1 : 0; }
int loops_soloed(int i) { return real(i) ? solo_ram >> i & 1 : 0; }
int loops_lanes(int i) { return real(i) ? (entry(i)[3] & 3) + 1 : 1; }
int loops_base(int i) { return real(i) ? clamp(entry(i)[4] + 1, 1, (int)PAT_ROWS_MAX) : 0; }
int loops_repeats(int i) { return real(i) ? (entry(i)[5] ? entry(i)[5] : 1) : 0; }
int loops_shift(int i) { return real(i) ? REC_LOOP_SHIFT(entry(i)) % loops_rows() : 0; }
static void shift_put(uint8_t *e, int s)        /* an entry's SHIFT (0-127): byte 2's bit 7 its bit 6, byte 3's bits 7-2 */
{
    e[2] = (uint8_t)((e[2] & 0x7F) | (s >> 6 & 1) << 7);
    e[3] = (uint8_t)((e[3] & 3) | (s & 63) << 2);
}
int loops_set_shift(int i, int s)
{
    uint8_t h[LP_HEAD_MAX];
    int n = copy_head(h), len = h[7];
    if (i < 0 || i >= n) return LP_NO;
    uint8_t *e = h + TABLE + ENTRY * i, was2 = e[2], was3 = e[3];
    shift_put(e, (s % len + len) % len);
    return e[2] == was2 && e[3] == was3 ? LP_OK : put_head(h);
}
const uint8_t *loops_mix(int i)
{
    static uint8_t nx[RM_N];            /* (the next loop's, as heard: next_mix) */
    if (real(i)) return entry(i) + 6;
    next_mix(head() + NEXT_MIX, nx);
    return nx;
}
int loops_set_mix(int i, int k, int v)
{
    uint8_t h[LP_HEAD_MAX];
    int n = copy_head(h);
    if (i < 0 || i > n || k < 0 || k >= RM_N) return LP_NO;
    uint8_t *m = i < n ? h + TABLE + ENTRY * i + 6 : h + NEXT_MIX;
    if (m[k] == (uint8_t)v) return LP_OK;
    m[k] = (uint8_t)v;
    return put_head(h);
}
static int set_entry(int i, int at, int mask, int v)
{
    uint8_t h[LP_HEAD_MAX];
    int n = copy_head(h);
    if (i < 0 || i >= n) return LP_NO;
    uint8_t *e = h + TABLE + ENTRY * i + at;
    int nv = (*e & ~mask) | (v & mask);
    if (nv == *e) return LP_OK;
    *e = (uint8_t)nv;
    return put_head(h);
}
int loops_set_inst(int i, uint8_t cart, uint8_t slot)
{
    int r = set_entry(i, 0, 0xFF, cart);
    return r ? r : set_entry(i, 1, 0xFF, slot);
}
int loops_set_sound(int i, uint8_t cart, uint8_t slot, int mono)   /* (one head written) */
{
    uint8_t h[LP_HEAD_MAX];
    int n = copy_head(h);
    if (i < 0 || i >= n) return LP_NO;
    uint8_t *e = h + TABLE + ENTRY * i, m = (uint8_t)((e[2] & ~0x10) | (mono ? 0x10 : 0));
    if (e[0] == cart && e[1] == slot && e[2] == m) return LP_OK;
    e[0] = cart;
    e[1] = slot;
    e[2] = m;
    return put_head(h);
}
int loops_set_muted(int i, int m) { return set_entry(i, 2, 0x20, m ? 0x20 : 0); }
int loops_set_soloed(int i, int s)
{
    if (!real(i)) return LP_NO;
    solo_ram = (uint16_t)(s ? solo_ram | 1u << i : solo_ram & ~(1u << i));
    return LP_OK;
}
int loops_set_mutes(uint32_t muted, uint32_t soloed)
{
    uint8_t h[LP_HEAD_MAX];
    int n = copy_head(h), any = 0;
    for (int i = 0; i < n; i++) {
        uint8_t *e = h + TABLE + ENTRY * i + 2, v = (uint8_t)((*e & ~0x60) | (muted >> i & 1u ? 0x20 : 0));
        any |= v != *e;
        *e = v;
    }
    int r = any ? put_head(h) : LP_OK;
    if (r == LP_OK) solo_ram = (uint16_t)(soloed & ((1u << n) - 1u));
    return r;
}
uint32_t loops_muted_mask(void)
{
    uint32_t m = 0;
    for (int i = 0; i < loops_n(); i++) if (loops_muted(i)) m |= 1u << i;
    return m;
}
uint32_t loops_soloed_mask(void)
{
    uint32_t m = 0;
    for (int i = 0; i < loops_n(); i++) if (loops_soloed(i)) m |= 1u << i;
    return m;
}
uint32_t loops_silent_mask(void)
{
    uint32_t s = loops_soloed_mask(), all = (1u << loops_n()) - 1u;
    return s ? all & ~s : loops_muted_mask();
}

/* ---------------------------------------------------------------- the streams --- */
int loops_bridge(const rec_cell_t *c) { return rec_bridge(c); }   /* (the player's reading: rec.c's) */
void loops_enc_start(lp_enc_t *e, uint8_t *buf, uint32_t max)
{
    e->b = buf;
    e->n = 0;
    e->max = max;
    e->prev = -1;
    e->lanes = 1;
    e->over = 0;
    e->last = -2;                       /* (a loop's stream: no instruments) */
}
void loops_enc_track(lp_enc_t *e, uint8_t *buf, uint32_t max)
{
    loops_enc_start(e, buf, max);
    e->last = -1;                       /* (a pattern track's: none yet) */
}
#define ENC_STAGE (-3)                  /* e->last: the stage's stream; enc_put's ins: a played cell's mark */
void loops_enc_stage(lp_enc_t *e, uint8_t *buf, uint32_t max)
{
    loops_enc_start(e, buf, max);
    e->last = ENC_STAGE;
}
/* a cell's bytes (ins: its instrument, cart << 8 | slot; ENC_STAGE the stage's mark: the instrument bits 01, no
 * bytes; -1 none) */
static void enc_put(lp_enc_t *e, int srow, const uint8_t *note, int ins, const uint8_t *vol, const uint8_t *cmd, int ncmd)
{
    int skip = srow - e->prev - 1, im = 0;
    if (ins == ENC_STAGE) im = 1;
    else if (ins >= 0 && e->last >= -1) im = ins == e->last ? 1 : e->last >= 0 && ins >> 8 == e->last >> 8 ? 2 : 3;
    uint32_t k = 1u + (note ? 1u : 0u) + (im == 2 ? 1u : im == 3 ? 2u : 0u) + (vol ? 1u : 0u) + (ncmd > 1 ? 2u : 0u) +
                 2u * (uint32_t)ncmd + (skip >= 7 ? 1u : 0u);
    if (im && ins >= 0) e->last = ins;
    if (e->b && !e->over && e->n + k + 1u > e->max) e->over = 1;   /* (its 0x00 kept room for) */
    if (e->b && !e->over) {
        uint8_t *o = e->b + e->n;
        *o++ = (uint8_t)((note ? 0x80 : 0) | im << 5 | (vol ? 0x10 : 0) | (ncmd ? 0x08 : 0) | (skip >= 7 ? 7 : skip));
        if (note) *o++ = *note;
        if (im == 2) *o++ = (uint8_t)ins;
        if (im == 3) { *o++ = (uint8_t)(ins >> 8); *o++ = (uint8_t)ins; }
        if (vol) *o++ = *vol;
        if (ncmd > 1) { *o++ = 0xFF; *o++ = (uint8_t)ncmd; }   /* (2-4 commands: 0xFF, their count, each) */
        for (int c = 0; c < ncmd; c++) { *o++ = cmd[2 * c]; *o++ = cmd[2 * c + 1]; }
        if (skip >= 7) *o = (uint8_t)skip;      /* (the long skip's byte last: after the commands) */
    }
    e->n += k;
    e->prev = srow;
}
void loops_enc_cell(lp_enc_t *e, int srow, const rec_cell_t *c)
{
    int note = (c->has & RC_NOTE) != 0, vol = (c->has & RC_VOL) != 0, n = c->has & RC_CMD ? c->n : 0;
    int ins = !(c->has & RC_INS) ? -1 : e->last >= -1 ? c->cart << 8 | c->slot : e->last == ENC_STAGE ? ENC_STAGE : -1;
    uint8_t v = (uint8_t)(c->vol > 0x7F ? 0x7F : c->vol);
    if ((!note && !vol && !n && ins < 0) || srow <= e->prev || srow >= (int)STORE_LOOP_ROWS) return;   /* (nothing, or
                                                                                              * out of order: not stored) */
    while (srow - e->prev - 1 > 255) enc_put(e, e->prev + 256, 0, -1, 0, ED0, 1);
    enc_put(e, srow, note ? &c->note : 0, ins, vol ? &v : 0, &c->cmd[0][0], n > 4 ? 4 : n);
    if ((srow & 3) + 1 > e->lanes) e->lanes = (srow & 3) + 1;
}
uint32_t loops_enc_end(lp_enc_t *e)
{
    if (e->b && !e->over) e->b[e->n] = 0;
    e->n++;
    return e->over ? 0u : e->n;
}

static rec_loops_t sl;                  /* the streams found, while the store's generation is sl_gen (main loop) */
static uint32_t sl_gen;
static uint8_t sl_ok;
const uint8_t *loops_stream(int i, const uint8_t **end)
{
    if (end) *end = 0;
    if (!real(i)) return 0;
    if (!sl_ok || sl_gen != store_gen()) {
        sl_ok = !rec_loops(&sl, 1);
        sl_gen = store_gen();
    }
    if (!sl_ok || i >= sl.n) return 0;
    if (end) *end = sl.trk_end[i];
    return sl.trk[i];
}
static uint32_t stream_len(const uint8_t *s, const uint8_t *end)   /* its bytes with its 0x00 */
{
    rec_cur_t cu;
    rec_cell_t c;
    if (!s) return 1;
    rec_cur_set_end(&cu, s, end);
    while (cu.row != 0x7FFF) rec_cur_at(&cu, cu.row, &c);
    return (uint32_t)(cu.p - s) + 1u;
}
static const rec_cell_t NOTE_OFF = {RC_NOTE, REC_NOTE_OFF, 0, 0, 0, 0, {{0, 0}}};
int loops_is_note(const rec_cell_t *c) { return (c->has & RC_NOTE) && c->note != REC_NOTE_OFF; }
static void stop_in(rec_cell_t *c)      /* a stop (a note off) into a cell with no note there: it takes one, unmarked */
{
    if (c->has & RC_NOTE) return;
    c->has = (uint8_t)((c->has | RC_NOTE) & ~RC_INS);
    c->note = REC_NOTE_OFF;
}
/* 1: a lane's note still sounds after cell c (was: before it): a note starts one, a note off or a key off ends it */
static int sounding(int was, const rec_cell_t *c)
{
    if (c->has & RC_NOTE) was = c->note != REC_NOTE_OFF;
    for (int k = 0; k < c->n; k++) if (c->cmd[k][0] == 0x14) was = 0;
    return was;
}
/* the lanes (bit l) of stream s whose note still sounds where its cells end, written out as encode writes them */
static int held_at_end(const uint8_t *s, const uint8_t *end, int base4, int reps, int lim4)
{
    int held = 0;
    for (int j = 0; s && j < reps; j++) {
        rec_cur_t cu;
        rec_cell_t c;
        rec_cur_set_end(&cu, s, end);
        while (cu.row < base4 && j * base4 + cu.row < lim4) {
            int l = cu.row & 3;
            rec_cur_at(&cu, cu.row, &c);
            held = (held & ~(1 << l)) | sounding(held >> l & 1, &c) << l;
        }
    }
    return held;
}
/* the stops at row 0 (w: their lanes) due before stream row srow is written, the cell c there (row 0's) taking its
 * lane's note off when it has no note; the stops left */
static int wrap_stops(lp_enc_t *e, int w, int srow, rec_cell_t *c)
{
    for (int l = 0; w && l < LP_LANES && l <= srow; l++) {
        if (!(w >> l & 1)) continue;
        if (l < srow) loops_enc_cell(e, l, &NOTE_OFF);
        else stop_in(c);
        w &= ~(1 << l);
    }
    return w;
}
/* loop i's cells encoded again (into b; NULL: counted): those of its first base4 stream rows, the cell at stream row
 * `at` replaced by put (put NULL: none replaced; a cell with nothing in it: cleared), all of it reps times, each base4
 * stream rows on, cut at lim4 (DOUBLE's writing out, LENGTH's cut). Where the player stops the loop's lanes, a note
 * still sounding there gets a note off: where its repeats end short of lim4 (its rest), or at row 0 when lim4 cuts the
 * last repeat (the stop at the wrap). Its length (0: past STORE_LOOP_STREAM_MAX), *lanes those used */
static PLAT_COLD uint32_t encode(int i, uint8_t *b, int base4, int at, const rec_cell_t *put, int reps, int lim4, int *lanes)
{
    lp_enc_t e;
    const uint8_t *end, *s = loops_stream(i, &end);
    int held = reps * base4 != lim4 ? held_at_end(s, end, base4, reps, lim4) : 0, w = reps * base4 > lim4 ? held : 0;
    loops_enc_start(&e, b, STORE_LOOP_STREAM_MAX);
    for (int j = 0; j < reps; j++) {
        rec_cur_t cu;
        rec_cell_t c;
        int placed = !put, off = j * base4;
        rec_cur_set_end(&cu, s, end);
        while (s && cu.row < base4 && off + cu.row < lim4) {
            int r = cu.row;
            rec_cur_at(&cu, r, &c);
            if (!placed && r >= at) { loops_enc_cell(&e, off + at, put); placed = 1; }
            if (r == at || loops_bridge(&c)) continue;
            w = wrap_stops(&e, w, off + r, &c);
            loops_enc_cell(&e, off + r, &c);
        }
        if (!placed && at < base4 && off + at < lim4) loops_enc_cell(&e, off + at, put);
    }
    for (int l = 0; reps * base4 < lim4 && l < LP_LANES; l++)   /* (its rest) */
        if (held >> l & 1) loops_enc_cell(&e, reps * base4 + l, &NOTE_OFF);
    if (lanes) *lanes = e.lanes;
    uint32_t n = loops_enc_end(&e);
    return b || n <= STORE_LOOP_STREAM_MAX ? n : 0u;
}
/* loop i's stream (s, len) written, its lanes into its entry with it */
static int put_stream(int i, const uint8_t *s, uint32_t len, int lanes)
{
    uint8_t h[LP_HEAD_MAX];
    copy_head(h);
    uint8_t *e = h + TABLE + ENTRY * i + 3;
    int hl = (*e & 3) + 1 != lanes;
    uint32_t need = need_for((uint16_t)i, len);
    if (hl) *e = (uint8_t)((*e & ~3) | (lanes - 1));
    if (store_edit_room() < need + (hl ? need_for(STORE_PC_HEAD, TABLE + ENTRY * (uint32_t)h[5]) : 0u)) return LP_FULL;
    int r = store_edit_piece(REC_LOOPS, 0, (uint16_t)i, LOOPS_REC_VER, s, len);
    return r != STORE_OK ? result(r) : hl ? put_head(h) : LP_OK;
}
/* loop i's stream built again (encode's) in the store's scratch and written */
static int restream(int i, int at, const rec_cell_t *put)
{
    uint8_t *b = store_scratch_claim(1);
    int lanes, r;
    if (!b) return LP_BUSY;
    uint32_t len = encode(i, b, 4 * loops_base(i), at, put, 1, 4 * loops_base(i), &lanes);
    r = len ? put_stream(i, b, len, lanes) : LP_FULL;
    store_scratch_claim(0);
    return r;
}

/* ------------------------------------------------------- a loop as it is heard --- */
/* Loop i read as it is heard over n rows: its notes written out (its repeats one after another), a note off where the
 * player stops its lanes with a note sounding (stop: its rest's first row, or row 0 where n cuts its last repeat and
 * wrap is set), then turned sh rows round n. Rows asked for in rising order cost a step each (one cursor: the turn and
 * each repeat start it again) */
typedef struct { rec_cur_t cu; const uint8_t *s; int base, reps, end, n, sh, held, stop; } lp_heard_t;
static void heard_start(lp_heard_t *o, int i, int n, int sh, int wrap)
{
    const uint8_t *e;
    o->s = loops_stream(i, &e);
    o->base = loops_base(i);
    o->reps = loops_repeats(i);
    o->end = o->base * o->reps;
    o->n = n;
    o->sh = (sh % n + n) % n;
    o->stop = o->end < n ? o->end : wrap && n % o->base && o->end > n ? 0 : -1;
    o->held = o->stop >= 0 ? held_at_end(o->s, e, 4 * o->base, o->reps, 4 * n) : 0;
    rec_cur_set_end(&o->cu, o->s, e);
}
static int heard_cell(lp_heard_t *o, int r, int l, rec_cell_t *c)    /* 1: a cell at row r, lane l (0 <= r < n) */
{
    int f = r - o->sh, got = 0;
    if (f < 0) f += o->n;
    if (o->s && f < o->end) got = rec_cur_at(&o->cu, 4 * (f % o->base) + l, c) && !loops_bridge(c);
    if (f != o->stop || !(o->held >> l & 1)) return got;
    if (!got) *c = NOTE_OFF;
    else stop_in(c);
    return 1;
}
/* loop i as heard over n rows (sh, wrap as heard_start), its rows below lim, with a note off at row 0 on each lane still
 * sounding at lim when lim < n (a cut: the stop at the wrap), into b (NULL: counted); its length (0: past
 * STORE_LOOP_STREAM_MAX), *lanes those used */
static PLAT_COLD uint32_t write_heard(int i, uint8_t *b, int n, int sh, int wrap, int lim, int *lanes)
{
    lp_heard_t o;
    lp_enc_t e;
    rec_cell_t c;
    int cut = 0;
    for (int l = 0; lim < n && l < LP_LANES; l++) {    /* (the lanes sounding at the cut) */
        int on = 0;
        heard_start(&o, i, n, sh, wrap);
        for (int r = 0; r < lim; r++) if (heard_cell(&o, r, l, &c)) on = sounding(on, &c);
        cut |= on << l;
    }
    heard_start(&o, i, n, sh, wrap);
    loops_enc_start(&e, b, STORE_LOOP_STREAM_MAX);
    for (int r = 0; r < lim; r++)
        for (int l = 0; l < LP_LANES; l++) {
            int got = heard_cell(&o, r, l, &c);
            if (!r && cut >> l & 1) { if (!got) c = NOTE_OFF; else stop_in(&c); got = 1; }
            if (got) loops_enc_cell(&e, 4 * r + l, &c);
        }
    if (lanes) *lanes = e.lanes;
    uint32_t len = loops_enc_end(&e);
    return b || len <= STORE_LOOP_STREAM_MAX ? len : 0u;
}
uint32_t loops_heard(int i, uint8_t *buf, uint32_t max)
{
    lp_heard_t o;
    lp_enc_t e;
    rec_cell_t c;
    int n = loops_rows();
    if (!real(i)) return 0;
    heard_start(&o, i, n, loops_shift(i), 1);
    loops_enc_start(&e, buf, max);
    for (int r = 0; r < n; r++)
        for (int l = 0; l < LP_LANES; l++) if (heard_cell(&o, r, l, &c)) loops_enc_cell(&e, 4 * r + l, &c);
    uint32_t len = loops_enc_end(&e);
    return buf || len <= max ? len : 0u;
}

/* ------------------------------------------------------------------ the stage --- */
/* the stage made `to` rows long, or played twice (twice: to 2 x rows), as loops_stage_length and _double */
static PLAT_COLD uint32_t stage_fit(const uint8_t *s, uint32_t len, int rows, int to, int twice, uint8_t *buf, uint32_t max)
{
    lp_enc_t e;
    rec_cur_t cu;
    rec_cell_t c;
    int held = to < rows ? held_at_end(s, s + len, 4 * to, 1, 4 * to) : 0;   /* (the lanes sounding at the cut) */
    int lim = to < rows ? to : rows;
    loops_enc_stage(&e, buf, max);
    for (int k = 0; k < (twice ? 2 : 1); k++) {
        int w = k ? 0 : held;
        rec_cur_set_end(&cu, s, s + len);
        while (cu.row < 4 * lim) {
            int r = cu.row;
            rec_cur_at(&cu, r, &c);
            if (loops_bridge(&c)) continue;
            if (k) c.has &= (uint8_t)~LP_PLAYED;        /* (the second time unmarked) */
            w = wrap_stops(&e, w, r, &c);           /* (a lane held at the cut has a cell at or after its row 0: w ends
                                                     * here) */
            loops_enc_cell(&e, 4 * rows * k + r, &c);
        }
    }
    uint32_t n = loops_enc_end(&e);
    return buf || n <= max ? n : 0u;
}
uint32_t loops_stage_length(const uint8_t *s, uint32_t len, int rows, int to, uint8_t *buf, uint32_t max)
{
    return stage_fit(s, len, rows, to, 0, buf, max);
}
uint32_t loops_stage_double(const uint8_t *s, uint32_t len, int rows, uint8_t *buf, uint32_t max)
{
    return stage_fit(s, len, rows, 2 * rows, 1, buf, max);
}
int loops_stage_row(lp_st_rd_t *d, int r, rec_cell_t *c4)   /* (read and merged as the player does: rec.c's) */
{
    int has = rec_stage_cells(&d->cu, r, c4), w;
    int8_t from = (int8_t)d->from;                      /* (-1 to 3) */
    if (!d->mono) return has;
    w = rec_stage_merge(has, c4, &from);
    d->from = from;
    if (w < 0) return 0;
    c4[0] = c4[w];
    return 1;
}
PLAT_COLD int loops_stage_write(int i, const uint8_t *s, uint32_t len, uint8_t cart, uint8_t slot, int mono)
{
    uint8_t h[LP_HEAD_MAX], *b;
    lp_st_rd_t d;
    lp_enc_t e;
    rec_cell_t c4[LP_LANES];
    int n = copy_head(h), rows = h[7], on[LP_LANES] = {0}, mark = 0, notes = 0, held = 0, wrap, r, k;
    int stage = s && s == store_stage(0);              /* (the stage itself: it goes with the write) */
    if (i < 0 || i > n || i >= LP_MAX || !s || !len) return LP_NO;
    if (stage && store_stage_check(1) == STORE_ERR_STATE) return LP_BUSY;
    rec_cur_set_end(&d.cu, s, s + len);                 /* the lanes sounding at the length's end, and how their notes came */
    d.mono = mono;
    d.from = -1;
    for (r = 0; r < rows; r++) {
        int has = loops_stage_row(&d, r, c4);
        for (int l = 0; l < LP_LANES; l++) {
            if (!(has >> l & 1)) continue;
            notes += loops_is_note(&c4[l]);
            if (!sounding(on[l], &c4[l])) on[l] = 0;
            else if (loops_is_note(&c4[l])) { on[l] = 1; mark = (mark & ~(1 << l)) | ((c4[l].has & LP_PLAYED) != 0) << l; }
        }
    }
    if (!notes) return LP_NO;
    for (int l = 0; l < LP_LANES; l++) held |= on[l] << l;
    wrap = held & mark;                                 /* (played onto the stage: no wrap, it ends at the line) */
    if (!(b = store_scratch_claim(1))) return LP_BUSY;
    loops_enc_start(&e, b, STORE_LOOP_STREAM_MAX);      /* (a loop's: no mark written) */
    rec_cur_set_end(&d.cu, s, s + len);
    d.from = -1;
    for (int l = 0; l < LP_LANES; l++) on[l] = held >> l & 1;
    for (r = 0; r < rows; r++) {
        int has = loops_stage_row(&d, r, c4);
        for (int l = 0; l < LP_LANES; l++) {
            if (!r && wrap >> l & 1) {                  /* (a note off at row 0, where its cell has no note) */
                if (!(has >> l & 1)) c4[l] = NOTE_OFF;
                else stop_in(&c4[l]);
                has |= 1 << l;
            }
            if (!(has >> l & 1) || (!loops_is_note(&c4[l]) && !on[l])) continue;   /* (an end that ends nothing: left out) */
            on[l] = sounding(on[l], &c4[l]);
            loops_enc_cell(&e, 4 * r + l, &c4[l]);
        }
    }
    uint32_t sl = loops_enc_end(&e);
    int lanes = e.lanes;
    uint8_t *en = h + TABLE + ENTRY * i;
    if (i == n) {                                       /* (a new loop: the next colour, the next loop's mix, its sends the
                                                         * KEYS column's) */
        en[2] = (uint8_t)(n ? (loops_colour(n - 1) + 1) % 12 : 0);
        next_mix(h + NEXT_MIX, en + 6);
        h[5] = (uint8_t)(n + 1);
    }
    en[0] = cart;
    en[1] = slot;
    en[2] = (uint8_t)((en[2] & 0x2F) | (mono ? 0x10 : 0));   /* (its colour and mute kept, SHIFT 0) */
    en[3] = (uint8_t)(lanes - 1);
    en[4] = (uint8_t)(rows - 1);
    en[5] = 1;
    uint32_t need = need_for((uint16_t)i, sl) + need_for(STORE_PC_HEAD, TABLE + ENTRY * (uint32_t)h[5]) +
                    (i == n ? need_for(STORE_PC_PHEAD, 3) : 0u), room = store_edit_room();
    k = !sl ? LP_BIG : room + (stage ? store_stage_room() : 0u) < need ? LP_FULL : LP_OK;   /* (the stage's room the
                                                         * loop's: not counted twice) */
    if (k == LP_OK && stage && room < need) {           /* (its room wanted: the stage goes first, its sound told) */
        player_lp_stage_end(i);
        store_stage_drop();
        stage = 0;
    }
    if (k == LP_OK) k = result(store_edit_piece(REC_LOOPS, 0, (uint16_t)i, LOOPS_REC_VER, b, sl));
    store_scratch_claim(0);
    if (k == LP_OK) k = put_head(h);
    if (k == LP_OK && stage) {                          /* (the loop written: what of the stage sounds goes on as its,
                                                         * then the stage goes) */
        player_lp_stage_end(i);
        store_stage_drop();
    }
    return k ? k : i;
}

int loops_cell(int i, int row, int lane, rec_cell_t *c)
{
    const uint8_t *end, *s;
    rec_cur_t cu;
    if (!real(i) || row < 0 || row >= loops_base(i) || lane < 0 || lane >= LP_LANES || !(s = loops_stream(i, &end))) return 0;
    rec_cur_set_end(&cu, s, end);
    return rec_cur_at(&cu, 4 * row + lane, c) && !loops_bridge(c);
}
int loops_cell_put(int i, int row, int lane, const rec_cell_t *c)
{
    rec_cell_t none;
    if (!real(i) || row < 0 || row >= loops_base(i) || lane < 0 || lane >= LP_LANES) return LP_NO;
    memset(&none, 0, sizeof none);
    return restream(i, 4 * row + lane, c ? c : &none);
}
int loops_stream_clear(int i) { return real(i) ? put_stream(i, ZERO1, 1, 1) : LP_NO; }
int loops_set_stream(int i, const uint8_t *s, uint32_t len)
{
    uint8_t *b;
    lp_enc_t e;
    rec_cur_t cu;
    rec_cell_t c;
    int r, base4 = 4 * loops_base(i);
    if (!real(i) || !s || !len || s[len - 1]) return LP_NO;
    if (!(b = store_scratch_claim(1))) return LP_BUSY;
    loops_enc_start(&e, b, STORE_LOOP_STREAM_MAX);     /* (as encoded here: no instrument, its base's rows) */
    rec_cur_set_end(&cu, s, s + len);
    while (cu.row < base4) {
        int row = cu.row;
        rec_cur_at(&cu, row, &c);
        if (!loops_bridge(&c)) loops_enc_cell(&e, row, &c);
    }
    uint32_t n = loops_enc_end(&e);
    r = n ? put_stream(i, b, n, e.lanes) : LP_FULL;
    store_scratch_claim(0);
    return r;
}
PLAT_COLD int loops_put(int i, int base, int repeats, int mono, const uint8_t *s, uint32_t len, int lanes)
{
    uint8_t h[LP_HEAD_MAX];
    int n = copy_head(h), r;
    if (i < 0 || i >= n || base < 1 || base > (int)PAT_ROWS_MAX || repeats < 1 || !s || !len || s[len - 1]) return LP_NO;
    if (len > STORE_LOOP_STREAM_MAX) return LP_FULL;
    uint8_t *e = h + TABLE + ENTRY * i;
    e[2] = (uint8_t)((e[2] & ~0x10) | (mono ? 0x10 : 0));
    e[3] = (uint8_t)((e[3] & ~3) | (clamp(lanes, 1, LP_LANES) - 1));
    e[4] = (uint8_t)(base - 1);
    e[5] = (uint8_t)clamp(repeats, 1, 255);
    if (store_edit_room() < need_for((uint16_t)i, len) + need_for(STORE_PC_HEAD, TABLE + ENTRY * (uint32_t)n)) return LP_FULL;
    if ((r = store_edit_piece(REC_LOOPS, 0, (uint16_t)i, LOOPS_REC_VER, s, len)) != STORE_OK) return result(r);
    return put_head(h);
}
int loops_note_count(int i)
{
    const uint8_t *end, *s = loops_stream(i, &end);
    rec_cur_t cu;
    rec_cell_t c;
    int n = 0, base4 = 4 * loops_base(i);
    rec_cur_set_end(&cu, s, end);
    while (s && cu.row < base4) {
        rec_cur_at(&cu, cu.row, &c);
        n += (c.has & RC_NOTE) && c.note != REC_NOTE_OFF;
    }
    return n;
}
int loops_has_notes(int i) { return loops_note_count(i) > 0; }

/* -------------------------------------------------------------------- changes --- */
PLAT_COLD int loops_new(uint8_t cart, uint8_t slot)
{
    uint8_t h[LP_HEAD_MAX];
    int n = copy_head(h), r;
    if (n >= LP_MAX) return LP_NO;
    uint8_t *e = h + TABLE + ENTRY * n;
    e[0] = cart;
    e[1] = slot;
    e[2] = (uint8_t)((n ? (loops_colour(n - 1) + 1) % 12 : 0) | (loops_keys_mono() ? 0x10 : 0));   /* (SHIFT 0) */
    e[3] = 0;                           /* 1 lane */
    e[4] = (uint8_t)(h[7] - 1);         /* its base: the length */
    e[5] = 1;                           /* once */
    memcpy(e + 6, h + NEXT_MIX, RM_N);  /* (the next loop's mix as stored: the tests' constructor; the UI's new loops are
                                         * REC's, loops_stage_write) */
    h[5] = (uint8_t)(n + 1);
    if (store_edit_room() < need_for((uint16_t)n, 1) + need_for(STORE_PC_HEAD, TABLE + ENTRY * (uint32_t)(n + 1)) +
                            need_for(STORE_PC_PHEAD, 3))
        return LP_FULL;
    /* its stream empty (a stream the flash has at its number left out) */
    if ((r = store_edit_piece(REC_LOOPS, 0, (uint16_t)n, LOOPS_REC_VER, ZERO1, 1)) != STORE_OK) return result(r);
    r = put_head(h);
    return r ? r : n;
}
int loops_clear(void)
{
    uint8_t h[LP_HEAD_MAX];
    copy_head(h);
    h[5] = 0;
    int r = put_head(h);
    if (r) return r;
    solo_ram = 0;
    for (int t = 0; t < LP_MAX; t++) store_edit_forget_piece(REC_LOOPS, 0, (uint16_t)t);   /* (past the loops: never read) */
    return LP_OK;
}
PLAT_COLD int loops_peel(int i)
{
    uint8_t h[LP_HEAD_MAX];
    int n = copy_head(h), r;
    uint32_t need = 0, most = 0;
    if (i < 0 || i >= n) return LP_NO;
    if (n == 1) return loops_clear();
    for (int k = i + 1; k < n; k++) {   /* the room first: each stream after it at the number before */
        const uint8_t *end, *s = loops_stream(k, &end);
        uint32_t l = stream_len(s, end);
        need += need_for((uint16_t)(k - 1), l);
        if (a4(l) > most) most = a4(l);
    }
    if (store_edit_room() < need + most + need_for(STORE_PC_HEAD, TABLE + ENTRY * (uint32_t)n) + need_for(STORE_PC_PHEAD, 3))
        return LP_FULL;
    for (int k = i + 1; k < n; k++) {   /* (each read again: a write may move the pieces) */
        const uint8_t *end, *s = loops_stream(k, &end);
        if ((r = store_edit_piece(REC_LOOPS, 0, (uint16_t)(k - 1), LOOPS_REC_VER, s ? s : ZERO1, stream_len(s, end))) != STORE_OK)
            return result(r);
    }
    store_edit_forget_piece(REC_LOOPS, 0, (uint16_t)(n - 1));   /* (its number past the loops now) */
    memmove(h + TABLE + ENTRY * i, h + TABLE + ENTRY * (i + 1), ENTRY * (uint32_t)(n - 1 - i));
    h[5] = (uint8_t)(n - 1);
    uint16_t below = (uint16_t)((1u << i) - 1u), s = (uint16_t)((solo_ram & below) | (solo_ram >> 1 & ~below));
    if ((r = put_head(h)) == LP_OK) solo_ram = s;       /* (the solos after it move up with their loops) */
    return r;
}
int loops_cut_drops(int rows)
{
    int len = loops_rows();
    for (int i = 0; i < loops_n(); i++) {
        lp_heard_t o;
        rec_cell_t c;
        heard_start(&o, i, len, loops_shift(i), 1);
        for (int r = rows; r < len; r++)
            for (int l = 0; l < LP_LANES; l++) if (heard_cell(&o, r, l, &c) && loops_is_note(&c)) return 1;
    }
    return 0;
}
/* LENGTH's way with loop i (entry e, the old length old, the new rows): its stream written again, as write_heard does
 * (LW_SHIFT: as heard over old, SHIFT 0, cut at rows when shorter; LW_OVER: longer, over old with no stop at the wrap,
 * its last repeat's cut written out; LW_CUT: shorter, its base past rows cut there), or LW_KEEP (its entry alone: the
 * repeats past the new end, or never reached, dropped); e's base and repeats set, its SHIFT 0 */
enum { LW_KEEP, LW_SHIFT, LW_OVER, LW_CUT };
static int length_way(uint8_t *e, int sh, int old, int rows)
{
    int base = e[4] + 1, reps = e[5] ? e[5] : 1, way = LW_KEEP;
    if (sh) { way = LW_SHIFT; base = old; reps = 1; }
    else if (rows > old && base * reps > old) {
        if (old % base) { way = LW_OVER; base = old; reps = 1; }
        else reps = old / base;
    }
    if (rows < old) {
        if (base > rows) { if (way == LW_KEEP) way = LW_CUT; base = rows; reps = 1; }
        else if (reps > (rows + base - 1) / base) reps = (rows + base - 1) / base;
    }
    e[4] = (uint8_t)(base - 1);
    e[5] = (uint8_t)reps;
    shift_put(e, 0);
    return way;
}
static uint32_t length_write(int i, int way, uint8_t *b, int old, int rows, int *lanes)
{
    return write_heard(i, b, old, way == LW_SHIFT ? loops_shift(i) : 0, way == LW_SHIFT, rows < old ? rows : old, lanes);
}
PLAT_COLD int loops_set_rows(int rows)
{
    uint8_t h[LP_HEAD_MAX], way[LP_MAX];
    int n = copy_head(h), old = h[7], r;
    uint32_t need = 0;
    if (rows < 1 || rows > (int)PAT_ROWS_MAX) return LP_NO;
    if (rows == old) return LP_OK;
    for (int i = 0; i < n; i++) {       /* the room first: the streams written again */
        way[i] = (uint8_t)length_way(h + TABLE + ENTRY * i, loops_shift(i), old, rows);
        if (way[i] == LW_KEEP) continue;
        uint32_t l = length_write(i, way[i], 0, old, rows, 0);
        if (!l) return LP_FULL;
        need += need_for((uint16_t)i, l);
    }
    h[7] = (uint8_t)rows;
    if (store_edit_room() < need + need_for(STORE_PC_HEAD, TABLE + ENTRY * (uint32_t)n) + need_for(STORE_PC_PHEAD, 3))
        return LP_FULL;
    for (int i = 0; i < n; i++) {       /* (each built in the store's scratch from the loop as it was: the head written last) */
        uint8_t *e = h + TABLE + ENTRY * i, *b;
        int lanes;
        if (way[i] == LW_KEEP) continue;
        if (!(b = store_scratch_claim(1))) return LP_BUSY;
        uint32_t l = length_write(i, way[i], b, old, rows, &lanes);
        r = l ? store_edit_piece(REC_LOOPS, 0, (uint16_t)i, LOOPS_REC_VER, b, l) : STORE_ERR_FULL;
        store_scratch_claim(0);
        if (r != STORE_OK) return result(r);
        e[3] = (uint8_t)((e[3] & ~3) | (lanes - 1));
    }
    return put_head(h);
}
PLAT_COLD int loops_double(void)
{
    uint8_t h[LP_HEAD_MAX];
    int n = copy_head(h), len = h[7], r;
    uint32_t need = 0;
    if (!n || 2 * len > loops_max_rows()) return LP_NO;
    for (int i = 0; i < n; i++) {       /* the room first: the loops written out */
        int base = loops_base(i), reps = loops_repeats(i);
        if (base * reps == len) continue;
        uint32_t l = encode(i, 0, 4 * base, -1, 0, (len + base - 1) / base < reps ? (len + base - 1) / base : reps, 4 * len, 0);
        if (!l) return LP_FULL;
        need += need_for((uint16_t)i, l);
    }
    if (store_edit_room() < need + need_for(STORE_PC_HEAD, TABLE + ENTRY * (uint32_t)n) + need_for(STORE_PC_PHEAD, 3))
        return LP_FULL;
    for (int i = 0; i < n; i++) {
        uint8_t *e = h + TABLE + ENTRY * i;
        int base = loops_base(i), reps = loops_repeats(i);
        if (base * reps == len) { e[5] = (uint8_t)(2 * reps); continue; }   /* (it fills the length: twice as often) */
        uint8_t *b = store_scratch_claim(1);   /* its notes written out to the length (the cut applied), then twice */
        if (!b) return LP_BUSY;
        uint32_t l = encode(i, b, 4 * base, -1, 0, (len + base - 1) / base < reps ? (len + base - 1) / base : reps, 4 * len, 0);
        r = l ? store_edit_piece(REC_LOOPS, 0, (uint16_t)i, LOOPS_REC_VER, b, l) : STORE_ERR_FULL;
        store_scratch_claim(0);
        if (r != STORE_OK) return result(r);
        e[4] = (uint8_t)(len - 1);
        e[5] = 2;
    }
    h[7] = (uint8_t)(2 * len);
    return put_head(h);
}
/* SEL's QUANTIZE, in ticks (sp a row; exact, a half rounded up): each lane's notes, in order, each with its start (its
 * row and note delay) and its end (a note off, a key off: its own after its start, or a later cell's; the next note
 * leaves it none), placed at the nearest line of q rows where it is heard (start + SHIFT round the length), back in the
 * base (modulo it), bare of note delays and key offs; its end a note off its length on (a row at least), where nothing
 * lies yet. src: the base row each row's note comes from (0xFF none), off: a note off there (bit r) */
#define Q_NONE 0xFFu
static PLAT_COLD void q_place(uint8_t *src, uint32_t *off, int r, int st, int en, int Q, int sp, int sh, int rows, int base)
{
    int ht = (st + sh * sp) % (rows * sp), at = (2 * ht + Q * sp) / (2 * Q * sp) * Q;
    int ns = ((st + at * sp - ht) / sp % base + base) % base;   /* (st - ht is whole rows: exact) */
    src[ns] = (uint8_t)r;
    off[ns >> 5] &= ~(1u << (ns & 31));
    if (en < 0) return;
    int len = (2 * (en - st) + sp) / (2 * sp), ne = ns + (len < 1 ? 1 : len);
    if (ne < base && src[ne] == Q_NONE) off[ne >> 5] |= 1u << (ne & 31);
}
PLAT_COLD int loops_quantize(int i, int q)
{
    static const uint8_t QR[4] = {1, 2, 3, 4};      /* (LQ_ROW, LQ_2_ROWS, LQ_3_ROWS, LQ_4_ROWS) */
    uint8_t src[LP_LANES][PAT_ROWS_MAX], *b;
    uint32_t off[LP_LANES][PAT_ROWS_MAX / 32];
    rec_cur_t cu[LP_LANES];
    const uint8_t *end, *s;
    lp_enc_t e;
    rec_cell_t c;
    int Q, sp = loops_ticks(), base = loops_base(i), rows = loops_rows(), sh = loops_shift(i), lanes, r;
    if (!real(i) || q < LQ_ROW || q > LQ_4_ROWS) return LP_NO;
    Q = QR[q];
    s = loops_stream(i, &end);
    memset(src, Q_NONE, sizeof src);
    memset(off, 0, sizeof off);
    for (int l = 0; l < LP_LANES; l++) {    /* each lane's notes placed in order (the later wins a row) */
        int cr = -1, cs = 0, ce = -1;       /* (the note pending: its row, start, end) */
        rec_cur_set_end(&cu[l], s, end);
        for (r = 0; s && r < base; r++) {
            int d = -1, k = -1, note;
            if (!rec_cur_at(&cu[l], 4 * r + l, &c) || loops_bridge(&c)) continue;
            note = loops_is_note(&c);
            for (int j = 0; j < c.n; j++) {     /* (each command's first) */
                if (c.cmd[j][0] == 0x14 && k < 0) k = c.cmd[j][1];
                else if (note && c.cmd[j][0] == 0x0E && c.cmd[j][1] >> 4 == 0xD && d < 0) d = c.cmd[j][1] & 15;
            }
            if (d < 0) d = 0;
            if (k >= 0 && (!note || k < d) && cr >= 0) { ce = r * sp + k; q_place(src[l], off[l], cr, cs, ce, Q, sp, sh, rows, base); cr = -1; }
            if ((c.has & RC_NOTE) && !note && cr >= 0) { q_place(src[l], off[l], cr, cs, r * sp, Q, sp, sh, rows, base); cr = -1; }
            if (!note) continue;
            if (cr >= 0) q_place(src[l], off[l], cr, cs, -1, Q, sp, sh, rows, base);
            cr = r;
            cs = r * sp + d;
            ce = -1;
            if (k >= 0 && k > d) { q_place(src[l], off[l], cr, cs, r * sp + k, Q, sp, sh, rows, base); cr = -1; }
        }
        if (cr >= 0) q_place(src[l], off[l], cr, cs, ce, Q, sp, sh, rows, base);
        rec_cur_set_end(&cu[l], s, end);
    }
    if (!(b = store_scratch_claim(1))) return LP_BUSY;
    loops_enc_start(&e, b, STORE_LOOP_STREAM_MAX);
    for (r = 0; r < base; r++)
        for (int l = 0; l < LP_LANES; l++) {
            if (src[l][r] != Q_NONE && rec_cur_at(&cu[l], 4 * src[l][r] + l, &c)) {
                int n = 0;                  /* (bare: its note delay and key off gone) */
                for (int j = 0; j < c.n; j++)
                    if (c.cmd[j][0] != 0x14 && !(c.cmd[j][0] == 0x0E && c.cmd[j][1] >> 4 == 0xD)) {
                        c.cmd[n][0] = c.cmd[j][0];
                        c.cmd[n++][1] = c.cmd[j][1];
                    }
                c.n = (uint8_t)n;
                if (!n) c.has &= (uint8_t)~RC_CMD;
                loops_enc_cell(&e, 4 * r + l, &c);
            } else if (off[l][r >> 5] >> (r & 31) & 1u) loops_enc_cell(&e, 4 * r + l, &NOTE_OFF);
        }
    lanes = e.lanes;
    uint32_t len = loops_enc_end(&e);
    r = len ? put_stream(i, b, len, lanes) : LP_FULL;
    store_scratch_claim(0);
    return r;
}
int loops_revert(void)
{
    int r = store_edit_forget(REC_LOOPS, 0, 0);
    sel_ok = 0;                         /* (the selection as saved too) */
    return result(r);
}

/* ------------------------------------------------- storing in a pattern, loading one --- */
const uint8_t LP_MIX_KEY[RM_N] = {RM_LEVEL, RM_PAN, RM_DELAY, RM_REVERB, RM_CHORUS, RM_DUCK};
const uint8_t LP_MIX_CODE[RM_N] = {0x80, 0x08, 0x82, 0x83, 0x81, 0x84};
#define TRACK_MAX (STORE_MAX_REC - STORE_HDR - 5u)     /* a pattern track's stream at most: a part's payload, its head */
void loops_cmd_add(rec_cell_t *c, int code, int v)
{
    if (c->n >= 4) return;
    c->cmd[c->n][0] = (uint8_t)code;
    c->cmd[c->n][1] = (uint8_t)v;
    c->n++;
    c->has |= RC_CMD;
}
/* SAVE TO PATTERN's plan, over each loop as it is heard (heard_cell): each lane's rows (from its first cell to its last;
 * a note still sounding at the length's end rings into the start: all of them) and its track (-1: none free over them) */
typedef struct { uint8_t from[LP_MAX][LP_LANES], to[LP_MAX][LP_LANES]; int8_t trk[LP_MAX][LP_LANES]; } st_plan_t;
static int store_plan(st_plan_t *pl, int n, int rows, int *left)
{
    int used = 0;
    for (int i = 0; i < n; i++) {
        lp_heard_t o;
        for (int l = 0; l < LP_LANES; l++) {
            rec_cell_t c;
            int first = -1, last = 0, held = 0, notes = 0, t;
            pl->trk[i][l] = -1;
            heard_start(&o, i, rows, loops_shift(i), 1);
            for (int r = 0; r < rows; r++) {
                if (!heard_cell(&o, r, l, &c)) continue;
                if (first < 0) first = r;
                last = r;
                held = sounding(held, &c);
                notes += loops_is_note(&c);
            }
            if (!notes) continue;
            pl->from[i][l] = (uint8_t)(held ? 0 : first);
            pl->to[i][l] = (uint8_t)(held ? rows : last + 1);
            for (t = 0; t < 16; t++) {          /* (the first track the lanes on it so far leave free over these rows) */
                int ov = 0;
                for (int k = 0; k <= i && !ov; k++)
                    for (int j = 0; j < LP_LANES && !ov; j++)
                        ov = (k < i || j < l) && pl->trk[k][j] == t && pl->from[i][l] < pl->to[k][j] && pl->from[k][j] < pl->to[i][l];
                if (!ov) break;
            }
            if (t >= 16) { *left += notes; continue; }
            pl->trk[i][l] = (int8_t)t;
            if (t + 1 > used) used = t + 1;
        }
    }
    return used > 0 ? used : 1;
}
/* track t of the stored pattern, encoded into b (NULL: counted): row by row the cell of the lane on it there, as heard
 * (heard_cell: a note off where the lane's loop goes to rest, the stop at the wrap, its SHIFT), a note naming its loop's
 * instrument; the lane's first note its loop's mix as commands where the track's mix there differs (as the lanes before
 * it in time left it), as far as it has room. Its length (0: past a part) */
static PLAT_COLD uint32_t store_track(const st_plan_t *pl, int n, int rows, int t, uint8_t *b)
{
    lp_heard_t o;
    lp_enc_t e;
    uint8_t have[RM_N];
    int ci = -1, cl = 0, first = 0;
    memcpy(have, MIX_DEF, RM_N);
    loops_enc_track(&e, b, TRACK_MAX);
    for (int r = 0; r < rows; r++) {
        rec_cell_t c;
        int ai = -1, al = 0;
        for (int i = 0; i < n; i++)
            for (int l = 0; l < LP_LANES; l++)
                if (pl->trk[i][l] == t && pl->from[i][l] <= r && r < pl->to[i][l]) { ai = i; al = l; }
        if (ai < 0) continue;
        if (ai != ci || al != cl) { ci = ai; cl = al; first = 1; heard_start(&o, ai, rows, loops_shift(ai), 1); }
        if (!heard_cell(&o, r, al, &c)) continue;
        if (loops_is_note(&c)) {
            uint16_t in = loops_inst(ai);
            const uint8_t *mix = loops_mix(ai);
            c.has |= RC_INS;
            c.cart = (uint8_t)(in >> 8);
            c.slot = (uint8_t)in;
            for (int k = 0; k < RM_N && first; k++) {
                int key = LP_MIX_KEY[k];
                if (have[key] == mix[key] || c.n >= 4) continue;
                loops_cmd_add(&c, LP_MIX_CODE[k], mix[key]);
                have[key] = mix[key];
            }
            first = 0;
        }
        loops_enc_cell(&e, r, &c);
    }
    return loops_enc_end(&e);
}
PLAT_COLD int loops_store(uint8_t num, int *left)
{
    st_plan_t pl;
    int n = loops_n(), rows = loops_rows(), ntrk, t, r;
    uint32_t need = 12, len;
    uint8_t *b;
    *left = 0;
    if (!n || !rows) return LP_NO;
    ntrk = store_plan(&pl, n, rows, left);
    for (t = 0; t < 16; t++) {          /* the room first: every track's piece (an empty one a lone 0x00) */
        if (!(len = store_track(&pl, n, rows, t, 0)) || len > TRACK_MAX) return LP_FULL;
        need += a4(len) + 8u;
    }
    if (!store_edit_reserve(need)) return LP_FULL;
    uint8_t h[3] = {(uint8_t)ntrk, (uint8_t)(rows - 1), 1};
    if ((r = store_edit_piece(REC_PATTERN, num, STORE_PC_HEAD, 1, h, 3)) != STORE_OK) return result(r);
    for (t = 0; t < 16; t++) {          /* (each built in the store's scratch, then put) */
        if (!(b = store_scratch_claim(1))) return LP_BUSY;
        len = store_track(&pl, n, rows, t, b);
        r = len ? store_edit_piece(REC_PATTERN, num, (uint16_t)t, 1, b, len) : STORE_ERR_FULL;
        store_scratch_claim(0);
        if (r != STORE_OK) return result(r);
    }
    return LP_OK;
}

void loops_timing_fit(lp_timing_t *tm)
{
    int grid = -1, tempo = tm->tempo, rpb = tm->rpb ? tm->rpb : 4;
    for (int g = 0; g < 3; g++) if (GRID_RPB[g] == rpb) grid = g;
    if (grid < 0) { grid = loops_grid(); tempo = (tempo * GRID_RPB[grid] * 2 + rpb) / (2 * rpb); }
    tm->tempo = (uint16_t)clamp(tempo, (int)REC_TEMPO_MIN, (int)REC_TEMPO_MAX);
    tm->rpb = GRID_RPB[grid];
    tm->ticks = (uint8_t)clamp(tm->ticks, 1, 31);
    tm->swing = (uint8_t)clamp(tm->swing, 50, 75);
}
/* LOAD's assignment, the same in every run: the pattern row by row, in each row first what ends each track's note (a
 * note, a note off, a key off alone: its loop's lane free from there, a note off on it at the same tick: EDx with a
 * note delay, a key off as itself; ticks into the row, the lane free only for the track's own next note in that row),
 * then each note in track order onto its instrument's loops, the first lane free, else a loop more. A run gives loop
 * k's cells to e in its stream's order (k < 0: none), and what it found */
typedef struct {
    uint16_t inst[LP_MAX];              /* each loop's instrument, in the order they first play */
    uint8_t lanes[LP_MAX], mixed[LP_MAX], mix[LP_MAX][RM_N];   /* its lanes; its mix (mixed: one found with its notes) */
    uint8_t n, nout;                    /* the loops; the instruments left out ... */
    uint16_t out[32];                   /* ... which (the first 32) */
    uint16_t cmds;                      /* the commands left out */
} ld_t;
static int mix_key(int code) { for (int k = 0; k < RM_N; k++) if (LP_MIX_CODE[k] == code) return LP_MIX_KEY[k]; return -1; }
static int ld_keep(int code, int v)     /* (key offs, note delays, glides, ACC / SLD / A+S, LEG) */
{
    return code == 0x14 || (code == 0x0E && v >> 4 == 0xD) || code == 0x03 || (code >= 0xC0 && code <= 0xC2) || code == 0xD3;
}
static PLAT_COLD void ld_run(const rec_pat_t *p, int lim, int k, lp_enc_t *e, ld_t *d)
{
    rec_cur_t cu[16];
    rec_cell_t cl[16], put[LP_LANES];
    uint8_t has[16], busy[LP_MAX][LP_LANES], off[LP_LANES], ofc[LP_LANES][2];   /* busy: a lane's note's end (0xFF: not
                                                 * yet found); off: loop k's lane's at this row, ofc its command */
    int ins[16], on[16];                /* each track's instrument (-1 none yet); its note's loop << 2 | lane (-1 none) */
    int8_t back[16];                    /* the lane its note left ticks into this row (-1 none) */
    memset(d, 0, sizeof *d);
    memset(busy, 0, sizeof busy);
    memset(off, 0xFF, sizeof off);
    for (int t = 0; t < 16; t++) {
        rec_cur_set_end(&cu[t], t < p->ntrk ? p->trk[t] : 0, t < p->ntrk ? p->trk_end[t] : 0);
        ins[t] = on[t] = -1;
    }
    for (int r = 0; r < lim; r++) {
        int placed = 0;
        for (int t = 0; t < p->ntrk; t++) {     /* what ends each track's note */
            const rec_cell_t *c = &cl[t];
            int note, kx = -1, dl = 0;
            back[t] = -1;
            if (!(has[t] = (uint8_t)rec_cur_at(&cu[t], r, &cl[t]))) continue;
            note = loops_is_note(c);
            if (c->has & RC_INS) ins[t] = c->cart << 8 | c->slot;
            for (int j = 0; j < c->n; j++) {
                int code = c->cmd[j][0], v = c->cmd[j][1];
                if (code == 0x14) kx = v;
                if ((c->has & RC_NOTE) && code == 0x0E && v >> 4 == 0xD) dl = v & 15;   /* (a note off's too) */
                else if (!(note && (ld_keep(code, v) || mix_key(code) >= 0)) && code != 0x14) d->cmds++;
            }
            if (((c->has & RC_NOTE) || kx >= 0) && on[t] >= 0) {
                int L = on[t] >> 2, l = on[t] & 3, tick = c->has & RC_NOTE ? dl : kx;
                busy[L][l] = (uint8_t)(tick ? r + 1 : r);
                if (tick) back[t] = (int8_t)on[t];
                if (L == k) {
                    off[l] = (uint8_t)r;
                    ofc[l][0] = (uint8_t)(c->has & RC_NOTE ? (dl ? 0x0E : 0) : 0x14);
                    ofc[l][1] = (uint8_t)(c->has & RC_NOTE ? 0xD0 | dl : kx);
                }
                on[t] = -1;
            }
        }
        for (int t = 0; t < p->ntrk; t++) {     /* each note onto its instrument's loops */
            const rec_cell_t *c = &cl[t];
            int L, l = 0;
            if (!has[t] || !loops_is_note(c) || ins[t] < 0) continue;
            for (L = 0; L < d->n; L++) {
                if (d->inst[L] != ins[t]) continue;
                for (l = 0; l < LP_LANES && busy[L][l] > r && back[t] != (L << 2 | l); l++)
                    ;
                if (l < LP_LANES) break;
            }
            if (L == d->n) {
                if (d->n >= LP_MAX) {           /* (16 loops: its instrument left out) */
                    int o = 0;
                    while (o < d->nout && o < 32 && d->out[o] != ins[t]) o++;
                    if (o == d->nout) { if (o < 32) d->out[o] = (uint16_t)ins[t]; d->nout++; }
                    continue;
                }
                d->inst[d->n++] = (uint16_t)ins[t];
                l = 0;
            }
            busy[L][l] = 0xFF;
            on[t] = L << 2 | l;
            if (l + 1 > d->lanes[L]) d->lanes[L] = (uint8_t)(l + 1);
            if (!d->mixed[L]) {                 /* (its mix: from the first of its notes with mixer commands) */
                int any = 0;
                for (int j = 0; j < c->n; j++) any |= mix_key(c->cmd[j][0]) >= 0;
                if (any) {
                    memcpy(d->mix[L], MIX_DEF, RM_N);
                    for (int j = 0; j < c->n; j++) { int key = mix_key(c->cmd[j][0]); if (key >= 0) d->mix[L][key] = c->cmd[j][1]; }
                    d->mixed[L] = 1;
                }
            }
            if (L != k) continue;
            memset(&put[l], 0, sizeof put[l]);  /* (its note, volume and the commands a loop holds; no instrument) */
            put[l].has = (uint8_t)(c->has & (RC_NOTE | RC_VOL));
            put[l].note = c->note;
            put[l].vol = c->vol;
            for (int j = 0; j < c->n; j++) if (ld_keep(c->cmd[j][0], c->cmd[j][1])) loops_cmd_add(&put[l], c->cmd[j][0], c->cmd[j][1]);
            placed |= 1 << l;
        }
        for (int l = 0; k >= 0 && l < LP_LANES; l++) {
            rec_cell_t x;
            if (placed >> l & 1) loops_enc_cell(e, 4 * r + l, &put[l]);
            else if (off[l] == r) {             /* (a note off, EDx: ticks late; or the key off alone) */
                x = NOTE_OFF;
                if (ofc[l][0] == 0x14) x.has = 0;
                if (ofc[l][0]) loops_cmd_add(&x, ofc[l][0], ofc[l][1]);
                loops_enc_cell(e, 4 * r + l, &x);
            }
            if (off[l] == r) off[l] = 0xFF;
        }
    }
}
PLAT_COLD int loops_load(uint8_t num, const lp_timing_t *tm0, lp_loaded_t *res)
{
    ld_t d, x;                          /* (what the runs find: the first's kept) */
    uint8_t h[LP_HEAD_MAX], *b;
    lp_timing_t tm = *tm0;
    rec_pat_t p;
    lp_enc_t e;
    uint32_t need = 0, len;
    int k, r, n;
    memset(res, 0, sizeof *res);
    if (rec_pattern(num, &p)) return LP_NO;
    loops_timing_fit(&tm);
    int br = tm.rpb * loops_beats(), most = clamp((int)PAT_ROWS_MAX / br, 1, 16);
    int rows = (p.rows + br - 1) / br, lim;
    rows = (rows < most ? rows : most) * br;
    lim = rows < p.rows ? rows : p.rows;
    ld_run(&p, lim, -1, 0, &d);
    n = d.n;
    res->loops = (uint8_t)n;
    res->insts = d.nout;
    res->cmds = d.cmds;
    for (k = 0; k < n; k++) {           /* the room first: each loop's stream, counted */
        loops_enc_start(&e, 0, STORE_LOOP_STREAM_MAX);
        ld_run(&p, lim, k, &e, &x);
        if ((len = loops_enc_end(&e)) > STORE_LOOP_STREAM_MAX) return LP_FULL;
        need += need_for((uint16_t)k, len);
    }
    if (store_edit_room() < need + need_for(STORE_PC_HEAD, TABLE + ENTRY * (uint32_t)n) + need_for(STORE_PC_PHEAD, 3))
        return LP_FULL;
    for (k = 0; k < n; k++) {           /* (each built in the store's scratch from the pattern read again: a write may
                                         * move its pieces) */
        if (rec_pattern(num, &p)) return LP_NO;
        if (!(b = store_scratch_claim(1))) return LP_BUSY;
        loops_enc_start(&e, b, STORE_LOOP_STREAM_MAX);
        ld_run(&p, lim, k, &e, &x);
        len = loops_enc_end(&e);
        r = len ? store_edit_piece(REC_LOOPS, 0, (uint16_t)k, LOOPS_REC_VER, b, len) : STORE_ERR_FULL;
        store_scratch_claim(0);
        if (r != STORE_OK) return result(r);
    }
    copy_head(h);
    for (k = 0; k < n; k++) {           /* each POLY, once over the length, the colours in turn */
        uint8_t *en = h + TABLE + ENTRY * k;
        en[0] = (uint8_t)(d.inst[k] >> 8);
        en[1] = (uint8_t)d.inst[k];
        en[2] = (uint8_t)(k % 12);
        en[3] = (uint8_t)(d.lanes[k] - 1);
        en[4] = (uint8_t)(rows - 1);
        en[5] = 1;
        memcpy(en + 6, d.mixed[k] ? d.mix[k] : MIX_DEF, RM_N);
    }
    h[0] = (uint8_t)tm.tempo;           /* (the pattern's timing and length, with notes or none) */
    h[1] = (uint8_t)(tm.tempo >> 8);
    h[2] = (uint8_t)((h[2] & ~31) | tm.ticks);
    h[3] = tm.swing;
    h[4] = (uint8_t)((h[4] & ~3) | (tm.rpb == 6 ? 1 : tm.rpb == 8 ? 2 : 0));
    h[7] = (uint8_t)rows;
    h[5] = (uint8_t)n;
    sel_ram = 0;
    if ((r = put_head(h)) != LP_OK) return r;
    solo_ram = 0;
    for (k = n; k < LP_MAX; k++) store_edit_forget_piece(REC_LOOPS, 0, (uint16_t)k);   /* (past the loops: never read) */
    return LP_OK;
}

/* ----------------------------------------------------------------- the player ---
 * The audio context calls the source's functions: they read the store (rec_loops), never the main loop's copy */
static const uint8_t ORDER0 = 0;
static const uint8_t DEF_HEAD[8] = {120, 0, 6 | 0x80, 50, 0, 0, 0, 0};    /* (no loops record: its setup's first bytes) */
/* the loops' song: each loop's mix its track's, the next loop's as heard (next: next_mix's) its place's (track n: the
 * pattern recorder's preview and the stage on `+ NEW LOOP` play there) */
static void song_of(const uint8_t *h, int n, const uint8_t *next, const uint8_t *fx, rec_song_t *s)
{
    int g = h[4] & 3;
    memset(s, 0, sizeof *s);
    memcpy(s->name, "LOOPS", 6);
    s->tempo = (uint16_t)clamp(h[0] | h[1] << 8, (int)REC_TEMPO_MIN, (int)REC_TEMPO_MAX);
    s->ticks = (uint8_t)(h[2] & 31 ? h[2] & 31 : 6);
    s->rpb = GRID_RPB[g > 2 ? 0 : g];
    s->swing = (uint8_t)clamp(h[3], 50, 75);
    s->gvol = 64;
    s->len = 1;
    s->order = &ORDER0;
    for (int t = 0; t < 16; t++)
        memcpy(s->mix[t], t < n ? h + TABLE + ENTRY * t + 6 : t == n ? next : MIX_DEF, RM_N);
    s->fx = fx;
}
static int src_song(rec_song_t *s)
{
    rec_loops_t l;
    uint8_t nx[RM_N];
    if (rec_loops(&l, 0)) {
        next_mix(0, nx);
        song_of(DEF_HEAD, 0, nx, def_ok ? fx_def : 0, s);
        return 0;
    }
    memcpy(fx_au, l.head + 8, FX_LEN);
    fx_mend(fx_au, def_ok ? fx_def : 0);
    next_mix(l.head + NEXT_MIX, nx);
    song_of(l.head, l.n, nx, all_zero(fx_au, FX_LEN) ? 0 : fx_au, s);
    return 0;
}
/* the clock: the length's rows, no streams; with loops or none (PLAY plays the loops with none: the length goes round),
 * no record yet the length a new head has (def_rows) */
static int src_pattern(uint8_t num, rec_pat_t *pt)
{
    rec_loops_t l;
    if (num) return -1;
    memset(pt, 0, sizeof *pt);
    pt->ntrk = 16;
    pt->rows = rec_loops(&l, 0) ? def_rows : l.rows;
    pt->step = 1;
    return 0;
}
static int src_inst(int track, pl_inst_t *in)
{
    rec_loops_t l;
    rec_loop_t e;
    if (rec_loops(&l, 0) || track < 0 || track >= l.n) return 0;
    rec_loop(&l, track, &e);
    in->cart = e.cart;
    in->slot = e.slot;
    return 1;
}
static const pl_source_t SRC = {src_song, src_pattern, src_inst, 1};
int loops_song(rec_song_t *s)
{
    const uint8_t *h = head();
    uint8_t nx[RM_N];
    next_mix(h + NEXT_MIX, nx);
    song_of(h, h[5], nx, h + 8, s);
    return 0;
}
/* the loops' mutes and solos to the player, now (always; else only when they differ from what it was given last: a
 * change waiting there for the bar line, player_lp_mutes_at_bar, is not dropped by another edit) */
static uint32_t sent_m = 0xFFFFFFFFu, sent_s;
static void tell_mutes(int always)
{
    uint32_t m = loops_muted_mask(), s = loops_soloed_mask();
    if (!always && m == sent_m && s == sent_s) return;
    sent_m = m;
    sent_s = s;
    player_lp_mutes(m, s);
}
void loops_play(uint8_t row)
{
    tell_mutes(1);
    player_play_source(PL_LOOPS, &SRC, 0, row);
}
void loops_play_silent(uint8_t place)
{
    tell_mutes(1);
    player_play_source_silent(&SRC, place);
}
void loops_cue(void)
{
    head();                             /* (a new song's FX setup known first) */
    player_cue_loops(&SRC);
}
void loops_changed(int rows)
{
    player_state_t st;
    player_state(&st, 0);
    if (st.what != PL_LOOPS) return;
    tell_mutes(0);
    player_source_changed(rows);
}
