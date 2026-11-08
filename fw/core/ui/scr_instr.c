/* SPDX-License-Identifier: GPL-3.0-only
 * Copyright (C) 2026 Vasily Kazantsev */
/* INSTRUMENTS (specs/ui/screens/instruments.md; the playground's scr-instr.js): a cartridge's slots in two
 * columns of 8, the current instrument in detail below (DX7 operators, Juno-106 sliders, TB-303 knobs, a
 * sample's waveform, a FUSION pattern's map); EDIT: edit mode, the parameters and a picture; SEL the slot
 * menu; ENV the cartridges (in edit mode: the samples, or the patterns of a FUSION slot); GLO the
 * cartridge. The keys play the current instrument (the core's). */
#include <string.h>
#include "ui_core.h"
#include "ui_priv.h"
#include "ui_data.h"
#include "ui_ed.h"
#include "ed_inst.h"
#include "ed_pat.h"
#include "../play/player.h"
#include "../play/rec.h"
#include "../store/store.h"
#include "../eng/samp/samp.h"
#include "../eng/chip/chip.h"

#define MAXSLOT 112
static struct {
    uint8_t edit, marking, mv, mv_swap, changed;
    uint8_t fe;                         /* edit mode is a favourite's (which instrument it names) */
    uint16_t fe_was;                    /* the current instrument at its EDIT (HOME: back to it) */
    int page, group, last, mark, mv_from, mv_at;
    int clip_n, clip_kind;
} st = {.last = -1, .mark = -1};
#define CLIP_SLOTS 4
static uint8_t clip[CLIP_SLOTS][MAXSLOT], clip_used[CLIP_SLOTS];    /* COPY's slots (4 at most) */

/* the cursor is the current place (a favourite's in a FAVORITES cartridge); while the details below show a favourite's
 * source, that instrument stands in its place (det) */
static uint16_t det = 0xFFFFu;
static uint16_t at(void) { return det != 0xFFFFu ? det : ui_cur.place; }
static uint8_t cart(void) { return (uint8_t)(at() >> 8); }
static int slot(void) { return at() & 255; }
int scr_instr_edit(void) { return st.fe ? 2 : st.edit; }
#ifdef PTFM_EDITOR
/* the editor page's pictures (the end of the file): while one is drawn, the current slot holds the bytes the page gave,
 * of the kind it said, not the store's */
static struct { const uint8_t *slot; int kind; } pic;
static int kind(void) { return pic.slot ? pic.kind : ins_kind(cart()); }
static int slot_bytes(uint8_t c, int s, uint8_t *b)    /* (an empty slot's name as rec_slot reads it: empty) */
{
    if (!pic.slot) return ins_slot_bytes(c, s, b);
    memcpy(b, pic.slot, (size_t)rec_slot_size((uint8_t)pic.kind));
    return rec_bits(b, (uint32_t)ins_name_pos(pic.kind), 6) != REC_NAME_EMPTY;
}
#else
static int kind(void) { return ins_kind(cart()); }
#define slot_bytes ins_slot_bytes
#endif
static int size(void) { int k = kind(); return k ? ENG_SLOTS[k] : 0; }
static int sign(int s) { return s > 0 ? 1 : s < 0 ? -1 : 0; }
static int pages(const ins_page_t **p)                  /* (a CHIP slot's TONE page: its oscillator's knobs) */
{
    uint8_t s[MAXSLOT];
    int k = kind();
    return ins_pages_of(k, k == REC_CHIP && slot_bytes(cart(), slot(), s) ? s : 0, p);
}
static const ins_page_t *groups(int *n)                /* the page shown, and its groups' count */
{
    const ins_page_t *p;
    int np = pages(&p);
    if (!np) { *n = 0; return 0; }
    st.page = ui_clamp(st.page, 0, np - 1);
    *n = p[st.page].ng;
    return &p[st.page];
}
static const ins_grp_t *group(void) { int n; const ins_page_t *p = groups(&n); return n ? ins_grp(p, ui_clamp(st.group, 0, n - 1)) : 0; }
static int used(uint8_t *b) { return slot_bytes(cart(), slot(), b); }
static void set_inst(int c, int s) { ui_set_inst((uint16_t)(c << 8 | s)); }
static int put_slot(int s, const uint8_t *b)
{
    if (ins_put_slot(cart(), s, b)) return -1;
    if (st.edit) st.changed = 1;
    return 0;
}

/* -------------------------------------------------------------------------------- the samples --- */
/* a sample, by its number (specs/files/instruments.md#samples: a record of its own, any sampler's instruments naming
 * it); 0 when it is not there (or read as missing) */
static int sample_info(int num, rec_sample_t *sm, char *name)
{
    if (num < 0 || num > 255 || rec_sample((uint8_t)num, sm)) return 0;
    if (name) rec_name(sm->entry, 42, 10, name);
    return 1;
}
/* THE SAMPLES list's rows (specs/ui/screens/instruments.md#the-samples), kept while the store stays as it was
 * (store_gen): every sample on the FM-1 by number, a bit each, those read (K1 on SAMPLE's steps, what ENV picks; a
 * sample whose entry and audio differ is none) and those whose deletion waits for SAVE (a `(DELETED)` row: rec_sample
 * reads it no more); the rows, those that can be picked, and their bytes (the cartridge list's ALL SAMPLES line) */
static struct { uint32_t gen, bytes; uint16_t n, picks; uint8_t ok, has[32]; } sr;
static int bit(const uint8_t *b, int k) { return b[k >> 3] >> (k & 7) & 1; }
static void rows(void)
{
    if (sr.ok && sr.gen == store_gen()) return;
    uint8_t nums[256];
    rec_sample_t s;
    uint32_t len;
    uint8_t ver;
    int n = store_list(REC_SAMPLE, nums, 256);
    memset(&sr, 0, sizeof sr);
    for (int i = 0; i < n; i++)
        if (!rec_sample(nums[i], &s)) { sr.has[nums[i] >> 3] |= (uint8_t)(1u << (nums[i] & 7)); sr.picks++; sr.bytes += 16u + s.audio_len; }
    sr.n = sr.picks;
    for (int k = 0; k < 256; k++) {                     /* (a deletion waiting: the record as saved, whole) */
        const uint8_t *p = store_edit_has(REC_SAMPLE, (uint8_t)k, 0) == 2 ? store_get_saved(REC_SAMPLE, (uint8_t)k, 0, &len, &ver) : 0;
        if (p && ver == 1 && store_sample_ok(p, len) && rec_bits(p, 42, 6) != REC_NAME_EMPTY) {
            sr.has[k >> 3] |= (uint8_t)(1u << (k & 7));
            sr.n++;
            sr.bytes += len;
        }
    }
    sr.gen = store_gen();
    sr.ok = 1;
}
static int row_num(int row)             /* the row's sample, -1 none */
{
    rows();
    for (int k = 0; k < 256; k++) if (bit(sr.has, k) && !row--) return k;
    return -1;
}
static int row_of(int num)              /* the sample's row, -1 none */
{
    rows();
    int r = 0;
    for (int k = 0; k < 256; k++) if (bit(sr.has, k)) { if (k == num) return r; r++; }
    return -1;
}
static int pickable(int num)            /* a row read (not one whose deletion waits) */
{
    rec_sample_t s;
    rows();
    return num >= 0 && num < 256 && bit(sr.has, num) && !rec_sample((uint8_t)num, &s);
}
/* THE SAMPLES list (below): opened as ENV on SAMPLE, REPLACE SAMPLE, CREATE, or the cartridge list's ALL SAMPLES line */
enum { SM_INST, SM_REPLACE, SM_CREATE, SM_ALL };
static void sm_open(int mode, int num, int edit);
/* a sample's waveform, its peaks a column, kept for the one shown: its number, width and audio (where it lies, its
 * length, 32 of its bytes), so another sample under the same number is drawn anew; nothing kept for a missing one */
static int8_t wv_lo[240], wv_hi[240];
static struct { int16_t num, w; const uint8_t *audio; uint32_t fp; } wv = {-1, 0, 0, 0};
static uint32_t audio_fp(const uint8_t *p, uint32_t n)
{
    uint32_t f = n;
    for (uint32_t i = 0; i < 32u; i++) f = f * 31u + p[(n / 32u) * i];
    return f;
}
static void wave_scan(int num, int w, const rec_sample_t *sm)
{
    uint32_t fp = audio_fp(sm->audio, sm->audio_len);  /* (its length among them) */
    if (wv.num == num && wv.w == w && wv.audio == sm->audio && wv.fp == fp) return;
    wv.num = (int16_t)num; wv.w = (int16_t)w; wv.audio = sm->audio; wv.fp = fp;
    memset(wv_lo, 0, sizeof wv_lo);
    memset(wv_hi, 0, sizeof wv_hi);
    const uint8_t *rec = sm->audio;
    uint32_t len = sm->audio_len;
    static ptr_dec_t d;
    memset(&d, 0, sizeof d);
    /* each column's peaks from up to 16 samples spread over it (the playground's way) */
    for (int i = 0; i < w; i++) {
        int32_t a = (int32_t)(sm->length * (uint32_t)i / (uint32_t)w), b = (int32_t)(sm->length * (uint32_t)(i + 1) / (uint32_t)w), step;
        if (b < a + 1) b = a + 1;
        step = (b - a) / 16 > 1 ? (b - a) / 16 : 1;
        for (int32_t k = a; k < b && k < (int32_t)sm->length; k += step) {
            int16_t x;
            ptr_read(&d, rec, len, k, 1, &x);
            int v = x >> 8;
            if (v < wv_lo[i]) wv_lo[i] = (int8_t)v;
            if (v > wv_hi[i]) wv_hi[i] = (int8_t)v;
        }
    }
}
static void wave(int idx, int x, int y, int w, int h, int col, const uint32_t *marks, const uint8_t *mcol, int nm)
{
    rec_sample_t sm;
    if (!sample_info(idx, &sm, 0) || !sm.length) { if (wv.num == idx) wv.num = -1; return; }
    wave_scan(idx, w, &sm);
    for (int i = 0; i < w; i++) {                       /* (rounded as the playground draws it) */
        int top = y + (128 * h - wv_hi[i] * h + 128) / 256, len = ((wv_hi[i] - wv_lo[i]) * h + 128) / 256;
        g_vline(x + i, top, len > 1 ? len : 1, col);
    }
    for (int k = 0; k < nm; k++)
        if (marks[k] <= sm.length) g_vline(x + (int)((marks[k] * (uint32_t)(w - 1) * 2 + sm.length) / (2 * sm.length)), y, h, mcol[k]);
}

/* ---------------------------------------------- a sample instrument's envelope over its sample --- */
/* the instrument being edited as the sampler plays it: its sample's audio; 0 if not there */
static int samp_of(const uint8_t *slot, samp_t *s)
{
    rec_sinst_t si;
    rec_sample_t sa;
    rec_sampler_inst(slot, &si);
    if (rec_sample(si.sample, &sa) || !sa.length)
        return 0;
    s->rec = sa.audio;
    s->rec_len = sa.audio_len;
    s->length = (int32_t)sa.length;
    s->rate = sa.rate ? sa.rate : 44100u;
    s->start = (int32_t)si.start;
    s->end = (int32_t)(si.end ? si.end : sa.length);
    s->loop_start = (int32_t)si.loop_start;
    s->loop_end = (int32_t)si.loop_end;
    s->loop = si.loop;
    s->base = si.base;
    s->tune = (int16_t)((int32_t)si.tune - 128);
    memcpy(s->env, si.env, 8);
    return 1;
}
#define TRACE_MOST 41344                 /* blocks: 60 s at most */
#define TRACE_W 240                      /* the trace's picture, kept: 2 bits a pixel */
#define TRACE_H 34
enum { TR_HEARD = 1, TR_PASS, TR_GHOST }; /* a block heard; a loop pass while the attack still rises; past END */
typedef struct { int len, yb, hh, px, py; } trace_t;
/* the last trace, kept as its pixels (0 none, else TR_*), and what it was traced from: traced again only when that
 * changes (a long sample's trace takes up to TRACE_MOST blocks, and the picture redraws ~30 times a second while
 * its voices play) */
static uint8_t tr_map[TRACE_W * TRACE_H / 4];
static int32_t tr_key[12], tr_have;
static void trace_px(int x, int y, int kind)
{
    if (x < 0 || x >= TRACE_W || y < 0 || y >= TRACE_H) return;
    int i = y * TRACE_W + x, sh = (i & 3) * 2;
    tr_map[i >> 2] = (uint8_t)((tr_map[i >> 2] & ~(3 << sh)) | kind << sh);
}
static void trace_line(int x0, int y0, int x1, int y1, int kind)
{
    int dx = x1 > x0 ? x1 - x0 : x0 - x1, dy = y1 > y0 ? y0 - y1 : y1 - y0, sx = x0 < x1 ? 1 : -1, sy = y0 < y1 ? 1 : -1;
    int e = dx + dy, shallow = dx >= -dy;                 /* (dotted by place: the blocks' short pieces overlap) */
    for (;;) {
        if (kind != TR_GHOST || !((shallow ? x0 : y0) & 1)) trace_px(x0, y0, kind);
        if (x0 == x1 && y0 == y1) break;
        int e2 = 2 * e;
        if (e2 >= dy) { e += dy; x0 += sx; }
        if (e2 <= dx) { e += dx; y0 += sy; }
    }
}
static void trace_pt(trace_t *d, int32_t pos, int32_t g, int kind, int brk)
{
    int x = (int)(((int64_t)pos * (TRACE_W - 1) * 2 + d->len) / (2 * (int64_t)d->len));
    int y = d->yb - (int)(((int64_t)g * d->hh + 16384) >> 15);
    if (!brk) trace_line(d->px, d->py, x, y, kind);
    d->px = x;
    d->py = y;
}
/* a note at the BASE NOTE on the sampler's own voice (samp_note_on, then samp_skip a block at a time: its envelope
 * and its place in the sample as it plays), drawn by place (x, as the markers) and gain (y, linear). With a loop
 * the key is held while the loop comes round, until a pass enters it with the envelope holding its level (R1-R3
 * done), and let go just before that pass reaches LOOP END: the release runs from LOOP END to END. Without a loop it
 * is held to END. Past END the same note goes on as if END were the sample's end: the ghost, dotted. Traced into
 * tr_map (its bottom row gain 0), which env_trace draws */
static void trace_run(samp_t s)
{
    static samp_voice_t v;
    memset(tr_map, 0, sizeof tr_map);
    if (s.end <= 0 || s.end > s.length) s.end = s.length;
    if (s.loop_end > s.end) s.loop_end = s.end;
    int32_t end = s.end;
    s.end = s.length;                    /* (the ghost's: on to the sample's end) */
    samp_note_on(&v, &s, s.base, 0);
    if (!v.on)
        return;
    trace_t d = {(int)s.length, TRACE_H - 1, TRACE_H - 1, 0, 0};
    int pass = 0, last = 0, brk = 1;
    trace_pt(&d, v.pos, 0, TR_HEARD, 1);
    for (int b = 0; b < TRACE_MOST && v.on; b++) {
        if (v.looping && v.down && last && v.dir > 0 &&
            v.pos + (int32_t)((v.frac + SAMP_N * v.step) >> 16) >= v.s.loop_end)
            samp_note_off(&v);
        int32_t was = v.pos;
        int wdir = v.dir, wloop = v.looping;
        samp_skip(&v);
        int32_t g = samp_env_q15(&v);
        if (!pass && wloop && wdir > 0 && v.dir > 0 && was < v.s.loop_start && v.pos >= v.s.loop_start)
            last = v.stage >= 3;         /* (into the loop the first time: already holding?) */
        if (wloop && ((v.s.loop == SAMP_LOOP_FORWARD && v.pos < was) || (v.s.loop == SAMP_LOOP_PINGPONG && wdir < 0 && v.dir > 0))) {
            pass++;                      /* (round again: a forward loop's jump breaks the line) */
            last = v.stage >= 3;
            brk = v.s.loop == SAMP_LOOP_FORWARD;
        }
        if (!v.on)
            break;
        trace_pt(&d, v.pos, g, !v.looping && v.pos >= end - 1 ? TR_GHOST : pass == 0 || last ? TR_HEARD : TR_PASS, brk);
        brk = 0;
        if (!v.down && !g)
            break;
    }
}
/* the instrument's envelope over its sample, TRACE_W x TRACE_H from (x, y): traced when what it is traced from
 * changed, else as kept */
static void env_trace(const uint8_t *slot, int x, int y, int col)
{
    samp_t s;
    if (!samp_of(slot, &s))
        return;
    int32_t key[12] = {s.length, (int32_t)s.rate, s.start, s.end, s.loop_start, s.loop_end, s.loop, s.base, s.tune,
                       s.env[0] | s.env[1] << 8 | s.env[2] << 16, s.env[3] | s.env[4] << 8 | s.env[5] << 16,
                       s.env[6] | s.env[7] << 8};
    if (!tr_have || memcmp(key, tr_key, sizeof key)) {
        trace_run(s);
        memcpy(tr_key, key, sizeof key);
        tr_have = 1;
    }
    for (int b = 0; b < (int)sizeof tr_map; b++)
        for (int j = 0; tr_map[b] && j < 4; j++) {
            int k = tr_map[b] >> j * 2 & 3, i = b * 4 + j;
            if (k) g_px(x + i % TRACE_W, y + i / TRACE_W, k == TR_PASS ? C_DGREY : col);
        }
}
/* where the voices playing the instrument's sample are in it (this cartridge's slots on that sample): a mark each */
static int play_marks(int sample, uint32_t *mk, uint8_t *mc, int nm)
{
    pl_voice_info_t v[PL_VOICES];
    rec_cart_t c;
    rec_sinst_t si;
    int n = ui_player_voices(v, PL_VOICES);
    if (rec_cart(cart(), &c)) return nm;
    for (int i = 0; i < n; i++) {
        const uint8_t *s = v[i].on && v[i].engine == PL_ENG_SAMPLER && v[i].pos >= 0 && v[i].inst.cart == cart()
                         ? rec_slot(&c, v[i].inst.slot) : 0;
        if (!s) continue;
        rec_sampler_inst(s, &si);
        if (si.sample != sample) continue;
        mk[nm] = (uint32_t)v[i].pos;
        mc[nm++] = C_YELLOW;
    }
    return nm;
}
/* a sample instrument's picture, the same on both edit pages: its waveform with the markers and the voices playing
 * it, and under it on the same x its envelope over the region the instrument plays, its ghost past END
 * (specs/ui/screens/instruments.md) */
static void samp_picture(const uint8_t *slot, int y, int col)
{
    rec_sinst_t si;
    rec_sample_t sm;
    rec_sampler_inst(slot, &si);
    uint32_t mk[4 + PL_VOICES] = {si.start, si.end, si.loop_start, si.loop_end};
    uint8_t mc[4 + PL_VOICES] = {C_WHITE, C_WHITE, C_GREEN, C_GREEN};
    int nm = si.loop ? 4 : 2, legend = 213, he = TRACE_H, hw = legend - 2 - y - he - 4, ye;
    hw -= hw & 1;
    ye = y + hw + 4;
    wave(si.sample, 0, y, 240, hw, C_BROWN, mk, mc, play_marks(si.sample, mk, mc, nm));
    if (!sample_info(si.sample, &sm, 0))                /* (where the waveform would be: grey, a reference to nothing) */
        g_text(0, y + 30, &FONT_T, T("THE SAMPLE IS MISSING"), C_DGREY);
    else if (sm.length) {
        g_hline(0, ye + he - 1, 240, C_DGREY);           /* (gain 0) */
        for (int k = 0; k < nm; k++)
            if (mk[k] <= sm.length) g_vline((int)((mk[k] * (uint32_t)239 * 2 + sm.length) / (2 * sm.length)), ye, he, mc[k]);
        env_trace(slot, 0, ye, col);
    }
    g_text(0, legend, &FONT_T, T("WHITE START/END  GREEN LOOP  YELLOW PLAY"), C_DGREY);
}

/* ---------------------------------------------------------------------------- the pictures --- */
static const int16_t SIN[91] = {0, 17, 35, 52, 70, 87, 105, 122, 139, 156, 174, 191, 208, 225, 242, 259, 276, 292, 309, 326, 342,
    358, 375, 391, 407, 423, 438, 454, 469, 485, 500, 515, 530, 545, 559, 574, 588, 602, 616, 629, 643, 656, 669, 682, 695,
    707, 719, 731, 743, 755, 766, 777, 788, 799, 809, 819, 829, 839, 848, 857, 866, 875, 883, 891, 899, 906, 914, 921, 927,
    934, 940, 946, 951, 956, 961, 966, 970, 974, 978, 982, 985, 988, 990, 993, 995, 996, 998, 999, 999, 1000, 1000};
static int sin_d(int d)                                  /* sin of d degrees, x 1000 */
{
    d %= 360;
    if (d < 0) d += 360;
    return d <= 90 ? SIN[d] : d <= 180 ? SIN[180 - d] : d <= 270 ? -SIN[d - 180] : -SIN[360 - d];
}
static void knob_pic(int x, int y, int r, int v, const char *label, int col)
{
    g_circle(x, y, r, C_DGREY);
    /* the angle in tenths of a degree, the sines between the table's degrees; ends rounded half up */
    int a10 = -1350 + (v * 5400 + 127) / 254, d = (a10 + 3600) / 10 - 360, f = a10 - d * 10;
    int sn = sin_d(d) * (10 - f) + sin_d(d + 1) * f, cs = sin_d(d + 90) * (10 - f) + sin_d(d + 91) * f;  /* x 10000 */
    int sx = sn * (r - 2), cy = cs * (r - 2);
#define RND(v) ((v) >= 0 ? ((v) + 5000) / 10000 : -((-(v) - 5000 + 9999) / 10000))   /* (floor(v / 10000 + 0.5)) */
    g_line(x, y, x + RND(sx), y - RND(cy), col);
#undef RND
    g_text(g_centre(&FONT_T, label, x - 20, 40), y + r + 3, &FONT_T, label, C_CYAN);
    g_text(g_centre(&FONT_T, ui_hex2(v), x - 20, 40), y + r + 15, &FONT_T, ui_hex2(v), C_WHITE);
}
/* a DX7 algorithm: carriers along the bottom, each modulator above what it modulates (columns in halves) */
static int alg_cols(const int kids[6][6], const int *nk, int i)
{
    if (!nk[i]) return 1;
    int s = 0;
    for (int k = 0; k < nk[i]; k++) s += alg_cols(kids, nk, kids[i][k]);
    return s;
}
static int col_at, pos_c2[6], pos_d[6];
static void alg_place(const int kids[6][6], const int *nk, int i, int depth)
{
    int span = alg_cols(kids, nk, i), c0 = col_at;
    if (nk[i]) for (int k = nk[i] - 1; k >= 0; k--) alg_place(kids, nk, kids[i][k], depth + 1);
    else col_at++;
    pos_c2[i] = 2 * c0 + span - 1;                      /* (c0 + (span - 1) / 2) x 2 */
    pos_d[i] = depth;
}
static void algorithm(int alg, int x0, int y0, int w, int h, int sel_op)
{
    const uint8_t *f = DX7_ALG[ui_clamp(alg, 1, 32) - 1];
    int tgt[6], kids[6][6], nk[6] = {0};
    for (int i = 0; i < 6; i++) {
        int ob = f[i] & 3;
        tgt[i] = -1;
        if (ob) for (int j = i + 1; j < 6; j++) if (((f[j] >> 4) & 3) == ob) { tgt[i] = j; break; }
        if (tgt[i] >= 0) kids[tgt[i]][nk[tgt[i]]++] = i;
    }
    for (int i = 0; i < 6; i++) pos_d[i] = -1;
    col_at = 0;
    for (int i = 5; i >= 0; i--) if (tgt[i] < 0 && !(f[i] & 3)) alg_place(kids, nk, i, 0);
    int ncol = col_at > 1 ? col_at : 1, depth = 0;
    for (int i = 0; i < 6; i++) if (pos_d[i] + 1 > depth) depth = pos_d[i] + 1;
    int cw = w / ncol < 26 ? w / ncol : 26, rh = h / (depth ? depth : 1) < 22 ? h / (depth ? depth : 1) : 22;
#define AX(i) (x0 + ((pos_c2[i] + 1) * cw + 1) / 2)
#define AY(i) (y0 + h - 12 - pos_d[i] * rh)
    for (int i = 0; i < 6; i++) if (tgt[i] >= 0 && pos_d[i] >= 0 && pos_d[tgt[i]] >= 0) g_line(AX(i), AY(i) + 6, AX(tgt[i]), AY(tgt[i]) - 5, C_DGREY);
    for (int i = 0; i < 6; i++) {
        if (pos_d[i] < 0) continue;
        int x = AX(i), y = AY(i), op = 6 - i, carrier = !(f[i] & 3);
        g_rect(x - 7, y - 6, 15, 13, op - 1 == sel_op ? C_DEEP + C_LRED : C_BLACK);
        g_box(x - 7, y - 6, 15, 13, carrier ? C_LRED : C_LGREY);
        g_text(x - 2, y - 5, &FONT_T, ui_dec(op), op - 1 == sel_op ? C_WHITE : carrier ? C_LRED : C_LGREY);
        if ((f[i] & 0xc0) == 0xc0) { g_hline(x + 8, y - 3, 3, C_YELLOW); g_vline(x + 10, y - 9, 6, C_YELLOW); g_hline(x - 1, y - 9, 11, C_YELLOW); }
        if (carrier) g_vline(x, y + 7, 3, C_LRED);
    }
    g_hline(x0 + 4, y0 + h - 1, ncol * cw - 8, C_LRED);
#undef AX
#undef AY
}
/* a DX7-style envelope (R1-R4, L1-L4, 0-99): the stages' lengths from their rates, a held part, the release */
static void dx_env(const int *r, const int *l, int x, int y, int w, int h, int col)
{
    int dur[4], pt[6][2], t = 0;
    for (int k = 0; k < 4; k++) { dur[k] = (100 - r[k]) * 55; if (dur[k] < 200) dur[k] = 200; }   /* x 100 */
    pt[0][0] = 0; pt[0][1] = 0;
    for (int k = 0; k < 3; k++) { t += dur[k]; pt[k + 1][0] = t; pt[k + 1][1] = l[k]; }
    t += 1400; pt[4][0] = t; pt[4][1] = l[2];
    t += dur[3]; pt[5][0] = t; pt[5][1] = l[3];
#define EY(l) (y + h - ((l) * h + 98) / 99)             /* (a level's y, cut to a pixel as the playground does) */
    for (int k = 1; k < 6; k++)
        g_line(x + pt[k - 1][0] * w / t, EY(pt[k - 1][1]), x + pt[k][0] * w / t, EY(pt[k][1]), col);
#undef EY
    g_vline(x + ((t - dur[3] - 1400) * w + t / 2) / t, y, h, C_DGREY);     /* (rounded) */
}
static const int16_t E3[13] = {1000, 717, 513, 368, 264, 189, 135, 97, 69, 50, 36, 26, 18};
static const int16_t E25[13] = {1000, 670, 449, 301, 202, 135, 91, 61, 41, 27, 18, 12, 8};
/* a Juno-106 envelope (A, D, S, R 0-127), drawn in thousandths of its seconds */
static void adsr(int A, int D, int S, int R, int x, int y, int w, int h, int col)
{
    /* times in 1/127 ms: A 0.5-2.5 s, D and R 0.5-3.5 s, 2 s held; y cut to a pixel as the playground does */
    int32_t a = 63500 + A * 2000, d = 63500 + D * 3000, r = 63500 + R * 3000, hold = 254000, tot = a + d + hold + r;
#define AX(t) (x + (int)((t) * w / tot))                /* (at most 1.5 M x 240: 32 bits) */
#define AY(num, den) (y + h - (int)(((num) * h + (den) - 1) / (den)))
    g_line(x, y + h, AX(a), y, col);
    int px = AX(a), py = y;
    for (int k = 1; k <= 12; k++) {
        int nx = AX(a + d * k / 12), ny = AY(S * 1000 + (127 - S) * E3[k], 127000);
        g_line(px, py, nx, ny, col); px = nx; py = ny;
    }
    int sy = AY(S, 127);
    g_line(px, py, AX(a + d + hold), sy, col);
    px = AX(a + d + hold); py = sy;
    for (int k = 1; k <= 12; k++) {
        int nx = AX(a + d + hold + r * k / 12), ny = AY(S * E25[k], 127000);
        g_line(px, py, nx, ny, col); px = nx; py = ny;
    }
#undef AX
#undef AY
}
/* the Juno-106's filter as a picture: a two-pole low-pass's response over 20 Hz - 20 kHz, its cut-off
 * 20 Hz * 2^(FREQ / 127 * 10), its Q 0.7 + RES / 127 * 10, falling as 1 / r^2 past it (the playground's
 * curve), in fixed point: log2 and 2^x a 64th at a time */
static const uint16_t LOG2_F[65] = {0, 1466, 2909, 4331, 5732, 7112, 8473, 9814, 11136, 12440, 13727, 14996, 16248, 17484, 18704,
    19909, 21098, 22272, 23433, 24579, 25711, 26830, 27936, 29029, 30109, 31178, 32234, 33279, 34312, 35334, 36346, 37346, 38336,
    39316, 40286, 41246, 42196, 43137, 44068, 44990, 45904, 46809, 47705, 48593, 49472, 50344, 51207, 52063, 52911, 53751, 54584,
    55410, 56229, 57040, 57845, 58643, 59434, 60219, 60997, 61769, 62534, 63294, 64047, 64794, 65535};
static const uint32_t EXP2_F[65] = {65536, 66250, 66971, 67700, 68438, 69183, 69936, 70698, 71468, 72246, 73032, 73828, 74632,
    75444, 76266, 77096, 77936, 78785, 79642, 80510, 81386, 82273, 83169, 84074, 84990, 85915, 86851, 87796, 88752, 89719, 90696,
    91684, 92682, 93691, 94711, 95743, 96785, 97839, 98905, 99982, 101070, 102171, 103283, 104408, 105545, 106694, 107856, 109031,
    110218, 111418, 112631, 113858, 115098, 116351, 117618, 118899, 120194, 121502, 122825, 124163, 125515, 126882, 128263, 129660,
    131072};
static int32_t log2_q16(uint64_t v, int frac_bits)      /* log2 of v / 2^frac_bits, Q16 */
{
    if (!v) return -(32 << 16);
    int m = 63;
    while (!(v >> m)) m--;
    uint32_t f = (uint32_t)(m >= 16 ? v >> (m - 16) : v << (16 - m)) & 0xFFFF;        /* the mantissa's fraction, Q16 */
    int k = (int)(f >> 10);
    int32_t l = LOG2_F[k] + (int32_t)((uint32_t)(LOG2_F[k + 1] - LOG2_F[k]) * (f & 1023) >> 10);
    return (m - frac_bits) * 65536 + l;
}
static uint64_t exp2_q16(int32_t q)                     /* 2^(q / 65536), Q16 (q within +-31 << 16) */
{
    int32_t ip = q >> 16;
    uint32_t f = (uint32_t)q & 0xFFFF, k = f >> 10;
    uint64_t m = EXP2_F[k] + ((uint64_t)(EXP2_F[k + 1] - EXP2_F[k]) * (f & 1023) >> 10);
    return ip >= 0 ? m << ip : m >> -ip;
}
static void filter_curve(int freq, int res, int x, int y, int w, int h, int col)
{
    /* (no 64-bit divide: multiplies and shifts only where 32 bits do not hold it) */
    int py = -1;
    int32_t q = 45875 + res * 655360 / 127;             /* Q16 */
    uint32_t q2 = (uint32_t)(q >> 4) * (uint32_t)(q >> 4) >> 8;    /* q^2, Q16 */
    for (int i = 0; i < w; i++) {
        int32_t L = i * 653118 / w - freq * 655360 / 127;          /* log2(f / fc), Q16 */
        if (L < -(6 << 16)) L = -(6 << 16);             /* (flat below; past +6 octaves clamped at the bottom anyway) */
        if (L > (6 << 16)) L = 6 << 16;
        int64_t r2 = (int64_t)exp2_q16(2 * L);           /* r^2, Q16 (at most 2^28) */
        int64_t a = (1 << 16) - r2;
        uint32_t rq = (uint32_t)r2 / (q2 >> 8) << 8;    /* r^2 / q^2, Q16 */
        uint64_t den = (uint64_t)(a * a) + ((uint64_t)rq << 16);  /* (1 - r^2)^2 + r^2 / q^2, Q32 */
        int32_t lg = log2_q16(den, 32) + (L > 0 ? 4 * L : 0);   /* + 2 log2 max(1, r^2): the extra fall */
        int db10 = -(int)((int64_t)lg * 1972830 >> 32);  /* dB x 10: lg x 10 log10(2) x 10 / 2^16, 1972830 = 30.103 x 2^16 */
        if (db10 < -360) db10 = -360;
        if (db10 > 180) db10 = 180;
        int yy = y + (h * (180 - db10) * 2 + 540) / 1080;                               /* (rounded) */
        if (py >= 0) g_line(x + i - 1, py, x + i, yy, col);
        py = yy;
    }
    g_hline(x, (h * 18 * 2 + 54) / 108 + y, w, C_DGREY);
}
/* the TB-303's pictures (specs/ui/screens/instruments.md#the-tb-303s-pictures), as the playground's: the engine's law
 * (Open303's measured mapping) in log2 Hz, Q16. The knobs: WAVE, CUTOFF, RESO, ENV MOD, DECAY, ACCENT (0-127) */
#define TB_L0 500951                                    /* log2(200 Hz), Q16 */
#define TB_L1 870823                                    /* log2(10 kHz) */
/* (no 64-bit divide here, as everywhere in the firmware: 32-bit divides, 64-bit products) */
static int32_t tb_decay_ms(int d, int acc)              /* DECAY: 200 ms x 10^(d / 127), whole ms; an accented note 200 */
{
    return acc ? 200 : (int32_t)((200u * exp2_q16(d * 217706 / 127) + 32768) >> 16);   /* (217706: log2(10), Q16) */
}
#define TB_REST (-1)                                    /* (a time when the envelope is long gone) */
static int32_t tb_expm(int32_t t_ms8, int32_t tau_ms)   /* e^(-t / tau), Q16; t in ms Q8 (at most ~8 s) */
{
    if (t_ms8 < 0 || t_ms8 / 256 > tau_ms * 30) return 0;
    int32_t x = t_ms8 * 256 / tau_ms;                   /* (t / tau, Q16) */
    return (int32_t)exp2_q16(-(int32_t)(((int64_t)x * 94548) >> 16));   /* (94548: log2(e), Q16) */
}
static int32_t tb_cutoff(const int *k, int32_t t_ms8, int acc)   /* log2 Hz, Q16, t ms into a note (Q8; TB_REST) */
{
    int32_t cn = k[1] * 65536 / 127, en = k[3] * 65536 / 127;
    int64_t sc = ((int64_t)(65536 - cn) * (((int64_t)247335 * en >> 16) + 48300) + (int64_t)cn * (((int64_t)274924 * en >> 16) + 56623)) >> 16;
    int32_t off = (int32_t)((int64_t)3165 * cn >> 16) + 19294;   /* 0.0483 cn + 0.2944 */
    int32_t dec = tb_decay_ms(k[4], acc);
    int64_t l = 543593 + ((int64_t)192061 * cn >> 16) + (sc * (tb_expm(t_ms8, dec) - off) >> 16);
    if (acc) {                                          /* (the accent: the envelope through a 15 ms RC) */
        int32_t g = (dec << 16) / (dec - 15);           /* A / (A - B) = DECAY / (DECAY - 15 ms), Q16 */
        l += (int64_t)(k[5] * 65536 / 127) * ((int64_t)g * (tb_expm(t_ms8, dec) - tb_expm(t_ms8, 15)) >> 16) >> 16;
    }
    return (int32_t)(l < TB_L0 ? TB_L0 : l > TB_L1 ? TB_L1 : l);
}
static int tb_span(const int *k)                        /* the envelope's span, ms: 1.6 DECAYs, 300-2500 */
{
    int s = (int)((320u * exp2_q16(k[4] * 217706 / 127) + 32768) >> 16);
    return s < 300 ? 300 : s > 2500 ? 2500 : s;
}
static void tb_envelope(const int *k, int x, int y, int w, int h, int col)
{
    int T = tb_span(k);
    char s[12];
#define TY(l) (y + h - 1 - (((l) - TB_L0) * (h - 1) + (TB_L1 - TB_L0) / 2) / (TB_L1 - TB_L0))
    for (int i = 0; i < w; i++) {
        int yy = TY(tb_cutoff(k, i * T * 256 / (w - 1), 0));
        if (y + h - yy - 1 > 0) g_vline(x + i, yy + 1, y + h - yy - 1, C_DEEP + col);
        g_px(x + i, yy, col);
    }
    int rest = TY(tb_cutoff(k, TB_REST, 0));
    for (int i = 0; i < w; i += 3) g_px(x + i, rest, C_DGREY);     /* CUTOFF, where it comes to rest */
    for (int i = 0; i < w; i += 2) g_px(x + i, TY(tb_cutoff(k, i * T * 256 / (w - 1), 1)), C_YELLOW);
#undef TY
    if (T < 1000) ui_fmt(s, sizeof s, "%d MS", T);
    else ui_fmt(s, sizeof s, "%d.%d S", (T + 50) / 1000, (T + 50) / 100 % 10);
    g_text(x, y + h + 2, &FONT_T, "0", C_DGREY);
    g_text(x + w - g_tw(&FONT_T, s), y + h + 2, &FONT_T, s, C_DGREY);
}
static void tb_filter(const int *k, int x, int y, int w, int h, int col)
{
    int32_t q = 45875 + k[2] * 458752 / 127;            /* 0.7 + 7 RESO, Q16 */
    uint32_t q2 = (uint32_t)(q >> 4) * (uint32_t)(q >> 4) >> 8;    /* q^2, Q16 */
    int32_t fr = tb_cutoff(k, TB_REST, 0), ft = tb_cutoff(k, 2 << 8, 0);
#define TD(db10) (y + ((180 - ((db10) < -360 ? -360 : (db10) > 180 ? 180 : (db10))) * (h - 1) + 270) / 540)
#define TFX(lf) ((((lf) - 283241) * (w - 1) + 326559) / 653118)   /* (log2 f from 20 Hz over 1000x) */
    g_hline(x, TD(0), w, C_DGREY);
    for (int i = 0; i < w; i++) {
        int32_t lf = 283241 + i * 653118 / (w - 1);     /* log2(f), Q16: 20 Hz .. 20 kHz */
        for (int top = 0; top < 2; top++) {
            if (top && i % 2) continue;
            int32_t L = lf - (top ? ft : fr);
            if (L < -(6 << 16)) L = -(6 << 16);
            if (L > (6 << 16)) L = 6 << 16;
            int64_t r2 = (int64_t)exp2_q16(2 * L), a = (1 << 16) - r2;
            uint32_t rq = (uint32_t)r2 / (q2 >> 8) << 8;                 /* r^2 / q^2, Q16 */
            uint64_t den = (uint64_t)(a * a) + ((uint64_t)rq << 16);      /* (1 - r^2)^2 + r^2 / q^2, Q32 */
            int db10 = -(int)((int64_t)log2_q16(den, 32) * 1972830 >> 31);   /* -20 log10(den) in tenths: two sections */
            int yy = TD(db10);
            if (top) { g_px(x + i, yy, C_LGREY); continue; }
            if (y + h - yy - 1 > 0) g_vline(x + i, yy + 1, y + h - yy - 1, C_DEEP + col);
            g_px(x + i, yy, col);
        }
    }
    g_vline(x + TFX(fr), y, h, col);
    for (int yy = y; yy < y + h; yy += 2) g_px(x + TFX(ft), yy, C_LGREY);
#undef TD
#undef TFX
}
static void tb_wave(int v, int x, int y, int col)       /* a cycle, 14 x 8: the saw and the square mixed by WAVE */
{
    int py = -1;
    for (int i = 0; i < 14; i++) {
        int num = (127 - v) * (14 - 2 * i) + v * 14 * (i < 7 ? 1 : -1);  /* (the sample x 127 x 14) */
        int yy = ((y + 4) * 1016 - 2 * num + 508) / 1016;   /* (y + 4 - sample x 3.5, rounded: 3.5 / (127 x 14) = 2 / 1016) */
        if (py >= 0 && (yy - py > 1 || py - yy > 1)) g_vline(x + i, py < yy ? py : yy, py < yy ? yy - py : py - yy, col);
        else g_px(x + i, yy, col);
        py = yy;
    }
}
static void fusion_map(int pat, int x, int y, int w, int h)
{
    rec_pat_t p;
    if (pat == 0xFF || rec_pattern((uint8_t)pat, &p)) { g_text(x, y + 20, &FONT_T, T("NO PATTERN: EDIT MAKES ONE"), C_DGREY); return; }
    int rh = (h - 16) / (p.ntrk ? p.ntrk : 1);
    if (rh > 12) rh = 12;
    if (rh < 2) rh = 2;
    int k0 = 0x35 + 12 * ui_cur.octave, k1 = 0x4F + 12 * ui_cur.octave;
    g_rect(x + (k0 * w + 64) / 128, y, ((k1 - k0 + 1) * w + 64) / 128, p.ntrk * rh, C_DEEP + C_BLUE);     /* (rounded) */
    for (int t = 0; t < p.ntrk; t++) {
        if (!p.trk[t]) continue;
        rec_cur_t cur;
        rec_cell_t c;
        rec_cur_set(&cur, p.trk[t]);
        for (int r = 0; r < 128 && r < p.rows; r++) {
            if (!rec_cur_at(&cur, r, &c) || !(c.has & RC_NOTE) || c.note == REC_NOTE_OFF) continue;
            int ref = (c.has & RC_INS) ? c.cart << 8 | c.slot : cur.ins ? cur.cart << 8 | cur.slot : -1;
            int col = ref >= 0 ? ENG_COL[ui_cart_kind((uint8_t)(ref >> 8))] : C_LGREY;
            g_rect(x + r * w / 128, y + t * rh, (w + 127) / 128, rh - 1, col);
        }
    }
    int c4 = x + (0x3C * w + 64) / 128;
    g_vline(c4, y - 2, p.ntrk * rh + 4, C_WHITE);
    g_text(c4 - 8, y + p.ntrk * rh + 3, &FONT_T, "C-4", C_WHITE);
    g_text(x, y + p.ntrk * rh + 3, &FONT_T, "00", C_DGREY);
    g_text(x + w - 12, y + p.ntrk * rh + 3, &FONT_T, "7F", C_DGREY);
}

/* -------------------------------------------- CHIP's pictures (scr-instr.js's own, in the same whole numbers) --- */
static int rdiv(int a, int b) { return (2 * a + b) / (2 * b); }                  /* a / b rounded (a >= 0, b > 0) */
static const int16_t PSG_KHZ[7] = {1773, 1773, 1773, 1000, 1790, 2000, 1773};   /* the PSG envelope's clock by CLOCK */
static const int32_t PSG_HZ[7] = {1773400, 1773400, 1773400, 1000000, 1789772, 2000000, 1773400};
static const txt_t CKIND[5] = {TX("VOL"), TX("ARP"), TX("PITCH"), TX("DUTY"), TX("NOISE")};
static const int8_t ROWK[6] = {0, 1, 2, -1, 3, 4};      /* TABLES' rows: the table kinds, VIBRATO (-1) between */
static int env_lvl(int shape, int steps, int r, int pos)    /* the PSG envelope's level (chip.c's own) */
{
    int att = shape >> 2 & 1, alt = shape >> 1 & 1, hold = shape & 1, cont = shape >> 3 & 1, mx = steps - 1;
    if (r == 0) return att ? pos : mx - pos;
    if (!cont) return 0;
    if (hold) return (att ^ alt) ? mx : 0;
    return (alt ? (att ^ (r & 1)) : att) ? pos : mx - pos;
}
typedef struct { const int8_t *v; int len, loop, speed, num; } ctab_t;
static int chip_tab(const chip_patch_t *p, int k, ctab_t *L)    /* the instrument's table of kind k */
{
    L->v = chip_table(k, p->tab[k], &L->len, &L->loop);
    L->speed = p->spd[k];
    L->num = p->tab[k];
    return L->v != 0;
}
static int chip_steps(const chip_patch_t *p, uint8_t *o, int *mx)  /* a period or a few of the steps: how many */
{
    int n = 0;
    if (p->osc == CHIP_PULSE) {
        uint32_t d = chip_duty_bits(p->clock, p->duty);
        for (; n < 24; n++) o[n] = (uint8_t)(d >> (n & 7) & 1u);
        *mx = 1;
    } else if (p->osc == CHIP_TRIANGLE || p->osc == CHIP_WAVE) {
        const uint8_t *w = p->osc == CHIP_WAVE ? chip_wave(p->wave) : chip_tri();
        for (; n < 64; n++) o[n] = w[n & 31];
        *mx = 15;
    } else if (p->osc == CHIP_BUZZ) {
        int st = p->bz_steps ? 32 : 16, shape = p->bz_shape == 1 ? 12 : p->bz_shape == 2 ? 10 : 8;
        for (int r = 0; r < 2; r++)
            for (int i = 0; i < st; i++) o[n++] = (uint8_t)(p->bz_gate && i >= st / 2 ? 0 : env_lvl(shape, st, r, i));
        *mx = st - 1;
    } else {                                            /* (the noise: its long register from 1) */
        uint32_t l = 1;
        for (; n < 64; n++) { l = (l >> 1) | ((l ^ (l >> 1)) & 1u) << 14; o[n] = (uint8_t)(l & 1u); }
        *mx = 1;
    }
    return n;
}
static void steps_pic(int x, int y, int w, int h, const uint8_t *v, int n, int mx, int col)    /* steps as a line */
{
    int py = -1;
    g_rect(x, y + h - 1, w, 1, C_DGREY);
    for (int i = 0; i < n; i++) {
        int x0 = x + rdiv(i * w, n), x1 = x + rdiv((i + 1) * w, n) - 1, yy = y + h - 1 - rdiv(v[i] * (h - 1), mx);
        if (py >= 0 && py != yy) g_line(x0, py, x0, yy, col);
        g_line(x0, yy, x1, yy, col);
        py = yy;
    }
}
static int chip_env_frame(const chip_patch_t *p, int f, int fps)  /* the volume or the envelope's level at frame f */
{
    if (p->env == CHIP_ENV_PSG) {
        int es = p->curve == 3 ? 32 : 16, pos = f * PSG_HZ[p->clock] / (fps * (es == 32 ? 8 : 16) * p->psg_period);
        return rdiv(env_lvl(p->psg_shape, es, pos / es, pos % es) * 15, es - 1);
    }
    if (p->env == CHIP_ENV_DOWN || p->env == CHIP_ENV_UP) {     /* (a step every STEP / 64 s; 8BIT's (STEP + 1) / 240 s) */
        int k = p->clock == CHIP_8BIT ? f * 240 / (fps * (p->env_step + 1)) : p->env_step ? f * 64 / (fps * p->env_step) : 0;
        return p->env == CHIP_ENV_DOWN ? (p->vol > k ? p->vol - k : 0) : (p->vol + k < 15 ? p->vol + k : 15);
    }
    return p->vol;
}
static void chip_vol_frames(const chip_patch_t *p, uint8_t *out, int n, int off)   /* the level a frame, let go at off */
{
    int fps = p->fps50 ? 50 : 60, li = 0, lc = 0, rel = 15, ra = 0;
    ctab_t L;
    int has = chip_tab(p, 0, &L);
    for (int f = 0; f < n; f++) {
        int lv = 15;
        if (has) {
            lv = L.v[li < L.len - 1 ? li : L.len - 1];
            if (++lc >= L.speed) { lc = 0; if (++li >= L.len) li = L.loop >= 0 ? L.loop : L.len; }
        }
        if (f >= off) { if (!p->release) rel = 0; else if (++ra >= p->release) { ra = 0; rel = rel > 0 ? rel - 1 : 0; } }
        out[f] = (uint8_t)((chip_env_frame(p, f, fps) * (lv > 0 ? lv : 0) * rel * 2 + 225) / 450);
    }
}
static void vol_pic(int x, int y, int w, int h, const uint8_t *fr, int n, int off, int col)
{
    g_rect(x, y + h - 1, w, 1, C_DGREY);
    for (int i = 0; i < n; i++) {
        int x0 = x + i * w / n, ww = x + (i + 1) * w / n - x0, top = y + h - 1 - rdiv(fr[i] * (h - 2), 15);
        if (ww < 1) ww = 1;
        g_rect(x0, top, ww, y + h - 1 - top, C_DEEP + col);
        g_rect(x0, top, ww, 1, col);
    }
    for (int yy = y; yy < y + h - 1; yy += 2) g_px(x + off * w / n, yy, C_DGREY);
}
/* a table drawn for its kind (VOL, DUTY, NOISE up from the floor, ARP and PITCH up and down) over `frames`, past its
 * end darker, or a bar a step (frames 0); a yellow line where it loops */
static void table_pic(int x, int y, int w, int h, int k, const ctab_t *L, int frames)
{
    static const uint8_t KMAX[5] = {15, 24, 127, 3, 15};
    int sg = k == 1 || k == 2, mx = KMAX[k], n = frames ? frames : L->len > 8 ? L->len : 8, gap = w >= 3 * n, mid = y + (h >> 1);
    if (k == 1) {
        mx = 12;
        for (int i = 0; i < L->len; i++) { int a = L->v[i] < 0 ? -L->v[i] : L->v[i]; if (a > mx) mx = a; }
    }
    if (sg) for (int xx = x; xx < x + w; xx += 2) g_px(xx, mid, C_DGREY);
    else g_rect(x, y + h - 1, w, 1, C_DGREY);
    for (int f = 0; f < n; f++) {
        int i = f;
        if (frames) {
            i = f / L->speed;
            if (i >= L->len) i = L->loop >= 0 ? L->loop + (i - L->len) % (L->len - L->loop) : L->len - 1;
        } else if (f >= L->len) continue;
        int v = L->v[i], x0 = x + f * w / n, ww = x + (f + 1) * w / n - x0 - gap;
        int col = frames && f / L->speed >= L->len ? C_DEEP + C_LBLUE : C_LBLUE;
        if (ww < 1) ww = 1;
        if (sg) {
            int hh = rdiv((v < 0 ? -v : v) * ((h >> 1) - 1), mx);
            g_rect(x0, v >= 0 ? mid - hh : mid, ww, hh + 1, col);
        } else {
            int hh = rdiv((k == 4 ? 15 - v : v) * (h - 2), KMAX[k]);
            if (hh < 1) hh = 1;
            g_rect(x0, y + h - 1 - hh, ww, hh, col);
        }
    }
    if (L->loop >= 0) {
        int lx = x + L->loop * (frames ? L->speed : 1) * w / n - 1;
        g_rect(lx > x ? lx : x, y, 1, h, C_YELLOW);
    }
}
static void vib_pic(int x, int y, int w, int h, const chip_patch_t *p, int frames)   /* flat through its delay, then
                                                                                       * its cycle */
{
    int mid = y + (h >> 1), d = p->vib_depth, hf = d >> 1, cy[8] = {0, hf, d, hf, 0, -hf, -d, -hf};
    for (int xx = x; xx < x + w; xx += 2) g_px(xx, mid, C_DGREY);
    if (!d) return;
    for (int f = 0; f < frames; f++) {
        int ff = f - p->vib_delay, v = ff < 0 ? 0 : cy[ff / p->vib_speed & 7], hh = rdiv((v < 0 ? -v : v) * ((h >> 1) - 1), 62);
        int x0 = x + f * w / frames, ww = x + (f + 1) * w / frames - x0;
        g_rect(x0, v >= 0 ? mid - hh : mid, ww < 1 ? 1 : ww, hh + 1, C_LBLUE);
    }
    int dx = x + p->vib_delay * w / frames;
    g_rect(dx < x + w - 1 ? dx : x + w - 1, y, 1, h, C_YELLOW);
}
static void text_r(int xr, int y, const char *s, int col) { g_text(xr - g_tw(&FONT_T, s), y, &FONT_T, s, col); }
static void chip_picture(const chip_patch_t *p, int y, int col)
{
    int fps = p->fps50 ? 50 : 60;
    uint8_t v[128];
    char t[48];
    if (st.page <= 1) {                                 /* (TONE, VOLUME: the level over 1.5 s at the right) */
        g_text(124, y + 2, &FONT_T, T("VOLUME"), C_CYAN);
        text_r(240, y + 2, T("1.5 S"), C_DGREY);
        chip_vol_frames(p, v, fps * 3 >> 1, fps);
        vol_pic(124, y + 18, 114, 70, v, fps * 3 >> 1, fps, col);
    }
    if (st.page == 0) {
        int mx, n = chip_steps(p, v, &mx);
        if (p->osc == CHIP_NOISE) ui_fmt(t, sizeof t, T("RATE %X"), p->n_rate);
        else if (p->clock == CHIP_EXACT) ui_fmt(t, sizeof t, "%s", T("EXACT"));
        else {
            uint32_t reg;
            int32_t c10, a;
            chip_tuning(p, 60, &reg, &c10);
            a = c10 < 0 ? -c10 : c10;
            ui_fmt(t, sizeof t, reg > 0xFFFu ? "$%04X %c%d.%d" : "$%03X %c%d.%d", reg, c10 < 0 ? '-' : '+', a / 10, a % 10);
        }
        g_text(0, y + 2, &FONT_T, T("WAVE"), C_CYAN);
        text_r(114, y + 2, t, C_LGREY);
        steps_pic(0, y + 18, 114, 70, v, n, mx, col);
    } else if (st.page == 1) {
        if (p->env == CHIP_ENV_PSG) {
            int es = p->curve == 3 ? 32 : 16, n = 0;
            for (int r = 0; r < 4; r++)
                for (int i = 0; i < es; i++) v[n++] = (uint8_t)env_lvl(p->psg_shape, es, r, i);
            ui_fmt(t, sizeof t, "%d MS", rdiv(256 * p->psg_period, PSG_KHZ[p->clock]));
            g_text(0, y + 2, &FONT_T, T("PSG ENVELOPE"), C_CYAN);
            text_r(114, y + 2, t, C_DGREY);
            steps_pic(0, y + 18, 114, 70, v, n, es - 1, col);
        } else {
            static const txt_t KN[3] = {TX("NONE"), TX("DOWN"), TX("UP")};
            for (int f = 0; f < fps; f++) v[f] = (uint8_t)chip_env_frame(p, f, fps);
            g_text(0, y + 2, &FONT_T, T("ENVELOPE"), C_CYAN);
            text_r(114, y + 2, T(KN[p->env]), C_DGREY);
            steps_pic(0, y + 18, 114, 70, v, fps, 15, col);
        }
    } else if (st.page == 2) {
        int k = st.group >= 0 && st.group < 6 ? ROWK[st.group] : 0;
        if (k < 0) {
            if (p->vib_depth) ui_fmt(t, sizeof t, T("DELAY %d, %d CENTS"), p->vib_delay, p->vib_depth);
            else ui_fmt(t, sizeof t, "%s", T("OFF"));
            g_text(0, y + 2, &FONT_T, T("VIBRATO"), C_CYAN);
            text_r(240, y + 2, t, C_LGREY);
            vib_pic(0, y + 16, 240, 72, p, 48);
        } else {
            ctab_t L;
            g_text(0, y + 2, &FONT_T, T(CKIND[k]), C_CYAN);
            if (chip_tab(p, k, &L)) {
                ui_fmt(t, sizeof t, T("%02X %s, %d STEPS"), L.num, T(CHIP_TAB_NAMES[k][L.num - 1]), L.len);
                if (L.loop >= 0) ui_fmt(t + strlen(t), (int)(sizeof t - strlen(t)), T(", LOOP %d"), L.loop + 1);
                text_r(240, y + 2, t, C_LGREY);
                table_pic(0, y + 16, 240, 72, k, &L, 48);
            } else {
                text_r(240, y + 2, T("NONE"), C_DGREY);
                g_rect(0, y + 87, 240, 1, C_DGREY);
            }
        }
        g_text(0, y + 89, &FONT_T, "0", C_DGREY);
        text_r(240, y + 89, T("48 FRAMES"), C_DGREY);
    } else {
        int on = p->osc == CHIP_WAVE;
        const uint8_t *w = chip_wave(p->wave);
        ui_fmt(t, sizeof t, "%02X %s", p->wave, T(CHIP_WAVE_NAMES[p->wave & 15]));
        g_text(0, y + 2, &FONT_T, T("WAVE TABLE"), C_CYAN);
        text_r(240, y + 2, t, on ? C_LGREY : C_DGREY);
        for (int i = 0; i < 32; i++) {
            int hh = rdiv(w[i] * 48, 15);
            if (hh < 1) hh = 1;
            g_rect(8 + i * 7, y + 66 - hh, 5, hh, on ? col : C_DGREY);
        }
        for (int j = 0; j < 16; j++) {
            int x = (j & 7) * 30, yy = y + 71 + (j >> 3) * 14;
            const uint8_t *wj = chip_wave(j);
            for (int i = 0; i < 32; i++) {
                int hh = rdiv(wj[i] * 10, 15);
                if (hh < 1) hh = 1;
                g_rect(x + 3 + (i * 3 >> 2), yy + 11 - hh, 1, hh, j == p->wave ? C_WHITE : col);
            }
            if (j == p->wave) g_box(x, yy - 1, 30, 14, C_WHITE);
        }
    }
}
static void chip_row_pic(int row, const chip_patch_t *p, int yy)    /* a TABLES row's table (row: kind + 1, 6 the
                                                                     * vibrato), small */
{
    ctab_t L;
    if (row == 6) { if (p->vib_depth) vib_pic(172, yy, 66, 10, p, 48); return; }
    if (chip_tab(p, row - 1, &L)) table_pic(172, yy, 66, 10, row - 1, &L, 0);
}

/* the details under the list (browsing) */
static const int16_t P10[100] = {1000, 1023, 1047, 1072, 1096, 1122, 1148, 1175, 1202, 1230, 1259, 1288, 1318, 1349, 1380, 1413,
    1445, 1479, 1514, 1549, 1585, 1622, 1660, 1698, 1738, 1778, 1820, 1862, 1905, 1950, 1995, 2042, 2089, 2138, 2188, 2239, 2291,
    2344, 2399, 2455, 2512, 2570, 2630, 2691, 2754, 2818, 2884, 2951, 3020, 3090, 3162, 3236, 3311, 3388, 3467, 3548, 3631, 3715,
    3802, 3890, 3981, 4074, 4169, 4266, 4365, 4467, 4571, 4677, 4786, 4898, 5012, 5128, 5248, 5370, 5495, 5623, 5754, 5888, 6025,
    6166, 6309, 6456, 6607, 6761, 6918, 7079, 7244, 7413, 7586, 7762, 7943, 8128, 8317, 8511, 8709, 8912, 9120, 9332, 9550, 9772};
static int bits(const uint8_t *s, int pos, int n) { return (int)rec_bits(s, (uint32_t)pos, n); }
static void level_text(int v, char *o)                 /* (the details' LEVEL: a sign only below 0) */
{
    ui_fmt(o, 16, "%s%d%s DB", v < 0 ? "-" : "", (v < 0 ? -v : v) / 2, (v & 1) ? ".5" : "");
}
static void details(int y)
{
    uint8_t s[MAXSLOT];
    int k = kind(), h = 226 - y, col = ENG_COL[k];
    char b[48], lv[16];
    if (!used(s)) {                                     /* (a sampler's: CREATE, an instrument needing a sample) */
        g_text(4, y + 30, &FONT_T, k == REC_SAMPLER ? T("EMPTY SLOT: SEL MENU, CREATE OR PASTE") : T("EMPTY SLOT: SEL MENU, INIT OR PASTE"), C_DGREY);
        return;
    }
    level_text((int8_t)bits(s, ins_level_pos(k), 8), lv);
    if (k == REC_DX7) {
        int alg = bits(s, 678 + 56, 5) + 1;
        g_run_t r[8] = {{T("ALG "), C_CYAN}, {ui_hex2(alg - 1), C_WHITE}, {T("  FB "), C_CYAN}, {0, C_WHITE}, {T("  LFO "), C_CYAN},
                        {0, C_WHITE}, {T("  LEVEL "), C_CYAN}, {lv, C_WHITE}};
        char fb[4], lfo[4];
        ui_fmt(fb, sizeof fb, "%X", bits(s, 678 + 61, 3));
        ui_fmt(lfo, sizeof lfo, "%02X", bits(s, 678 + 65, 7));
        r[3].s = fb; r[5].s = lfo;
        g_runs(0, y, &FONT_T, r, 8);
        g_text(0, y + 13, &FONT_T, T("OP RATIO LV R1 R2 R3 R4 L1 L2 L3 L4"), C_DGREY);
        const uint8_t *f = DX7_ALG[alg - 1];
        for (int i = 0; i < 6; i++) {
            int ob = (5 - i) * 113, carrier = !(f[5 - i] & 3), yy = y + 25 + i * 11;
            int lvl = bits(s, ob + 89, 7), mode = bits(s, ob + 96, 1), co = bits(s, ob + 97, 5), fi = bits(s, ob + 102, 7);
            char ratio[12];
            if (!mode) {
                int hund = co ? co * (100 + fi) : (100 + fi + 1) / 2;
                ui_fmt(ratio, sizeof ratio, "%2d.%02d", hund / 100, hund % 100);
            } else {
                int hz = (co & 3) == 0 ? 1 : (co & 3) == 1 ? 10 : (co & 3) == 2 ? 100 : 1000;
                ui_fmt(ratio, sizeof ratio, "%4dH", (hz * P10[fi > 99 ? 99 : fi] + 500) / 1000);
            }
            g_text(0, yy, &FONT_T, ui_txt_v(" #", ui_dec(i + 1)), carrier ? col : C_LGREY);
            g_text(18, yy, &FONT_T, ratio, lvl ? C_WHITE : C_DGREY);
            g_text(54, yy, &FONT_T, ui_hex2(lvl), lvl ? C_BROWN : C_DGREY);
            ui_fmt(b, sizeof b, "%02X %02X %02X %02X", bits(s, ob, 7) > 99 ? 99 : bits(s, ob, 7), bits(s, ob + 7, 7) > 99 ? 99 : bits(s, ob + 7, 7),
                   bits(s, ob + 14, 7) > 99 ? 99 : bits(s, ob + 14, 7), bits(s, ob + 21, 7) > 99 ? 99 : bits(s, ob + 21, 7));
            g_text(72, yy, &FONT_T, b, C_LGREY);
            ui_fmt(b, sizeof b, "%02X %02X %02X %02X", bits(s, ob + 28, 7) > 99 ? 99 : bits(s, ob + 28, 7), bits(s, ob + 35, 7) > 99 ? 99 : bits(s, ob + 35, 7),
                   bits(s, ob + 42, 7) > 99 ? 99 : bits(s, ob + 42, 7), bits(s, ob + 49, 7) > 99 ? 99 : bits(s, ob + 49, 7));
            g_text(144, yy, &FONT_T, b, C_LGREY);
        }
    } else if (k == REC_JUNO) {
        static const txt_t NM[17] = {TX("RT"), TX("DL"), TX("LF"), TX("PW"), TX("NS"), TX("SB"), TX("HP"), TX("FQ"), TX("RS"), TX("EN"),
                                     TX("LF"), TX("KB"), TX("LV"), TX("A"), TX("D"), TX("S"), TX("R")};
        static const uint8_t POS[17] = {0, 7, 14, 21, 28, 105, 255, 35, 42, 49, 56, 63, 70, 77, 84, 91, 98};
        static const uint8_t CL[17] = {9, 9, 11, 11, 11, 11, 3, 12, 12, 12, 12, 12, 6, 14, 14, 14, 14};
        int sh = h - 30;
        for (int i = 0; i < 17; i++) {
            int x = 2 + i * 14, v = POS[i] == 255 ? bits(s, 121, 2) * 42 : bits(s, POS[i], 7);
            g_rect(x + 4, y + 2, 2, sh, C_DGREY);
            g_rect(x + 1, y + 2 + ((127 - v) * (sh - 4) * 2 + 127) / 254, 8, 4, CL[i]);     /* (rounded) */
            const char *nm = T(NM[i]);
            g_text(x + (nm[1] ? 0 : 2), y + sh + 5, &FONT_T, nm, C_CYAN);
        }
        static const txt_t RNG[3] = {TX("16'"), TX("8'"), TX("4'")}, CHO[3] = {TX("OFF"), TX("I"), TX("II")};
        ui_fmt(b, sizeof b, T("%s %s%sCHORUS %s %s"), T(RNG[bits(s, 112, 2) % 3]), bits(s, 115, 1) ? T("SAW ") : "",
               bits(s, 114, 1) ? T("PULSE ") : "", T(CHO[bits(s, 116, 2) % 3]), bits(s, 120, 1) ? T("GATE") : T("ENV"));
        g_textn(0, y + h - 12, &FONT_T, b, 40, C_LGREY);
    } else if (k == REC_TB303) {
        static const txt_t NM[6] = {TX("WAVE"), TX("CUTOFF"), TX("RESO"), TX("ENVMOD"), TX("DECAY"), TX("ACCENT")};
        for (int i = 0; i < 6; i++) knob_pic(22 + i * 39, y + 26, 14, bits(s, 7 * i, 7), T(NM[i]), col);
    } else if (k == REC_SAMPLER) {
        rec_sinst_t si;
        rec_sample_t sm;
        char nm[16];
        rec_sampler_inst(s, &si);
        if (!sample_info(si.sample, &sm, nm)) {        /* (a number with no sample: grey, as every reference to nothing) */
            g_text(0, y + 20, &FONT_T, T("THE SAMPLE IS MISSING"), C_DGREY);
            return;
        }
        uint32_t cs = sm.rate ? (sm.length * 100 + sm.rate / 2) / sm.rate : 0;      /* (hundredths, rounded) */
        char hd[16];
        ui_fmt(hd, sizeof hd - 1, T("SAMPLE %02X"), si.sample);   /* (its number, then its name: `SAMPLE 23 KICK`) */
        strcat(hd, " ");
        ui_fmt(b, sizeof b, "  %u.%02u S  %u HZ", cs / 100, cs % 100, sm.rate);
        g_run_t r[3] = {{hd, C_LGREY}, {nm, (uint8_t)col}, {b, C_LGREY}};
        g_runs(0, y, &FONT_T, r, 3);
        uint32_t mk[4 + PL_VOICES] = {si.start, si.end, si.loop_start, si.loop_end};
        uint8_t mc[4 + PL_VOICES] = {C_WHITE, C_WHITE, C_GREEN, C_GREEN};
        wave(si.sample, 0, y + 16, 240, h - 34, C_BROWN, mk, mc, play_marks(si.sample, mk, mc, si.loop ? 4 : 2));   /* (and
                                                         * where its voices play: yellow) */
        static const txt_t LP[3] = {TX("OFF"), TX("FORWARD"), TX("PING-PONG")};
        char nn[8], tn[8];
        ui_note_name(nn, si.base);
        int tu = si.tune - 128;
        ui_fmt(tn, sizeof tn, "%s%02X", tu > 0 ? "+" : tu < 0 ? "-" : "", tu < 0 ? -tu : tu);
        ui_fmt(b, sizeof b, T("LOOP %s  BASE %s  TUNE %s"), T(LP[si.loop % 3]), nn, tn);
        g_text(0, y + h - 14, &FONT_T, b, C_LGREY);
    } else if (k == REC_FUSION) {
        int pat = s[0];
        rec_pat_t p;
        char sz[32] = "";
        if (pat != 0xFF && !rec_pattern((uint8_t)pat, &p)) ui_fmt(sz, sizeof sz, T("  %d TRACKS, %d ROWS"), p.ntrk, p.rows);
        g_run_t r[3] = {{T("PATTERN "), C_CYAN}, {pat == 0xFF ? "--" : ui_hex2(pat), C_WHITE}, {sz, C_LGREY}};
        g_runs(0, y, &FONT_T, r, 3);
        fusion_map(pat, 0, y + 16, 240, h - 30);
    } else if (k == REC_CHIP) {
        static const txt_t OSCN[5] = {TX("PULSE "), TX("TRIANGLE "), TX("WAVE "), TX("BUZZ "), TX("NOISE ")};
        static const txt_t DUTYN[4] = {TX("12.5%"), TX("25%"), TX("50%"), TX("75%")}, SH[3] = {TX("SAW DN"), TX("SAW UP"), TX("TRI")};
        static const txt_t CLK[7] = {TX("BRK"), TX("8BIT"), TX("PSG 1.77"), TX("PSG 1.00"), TX("PSG 1.79"), TX("16BIT"), TX("EXACT")};
        static const txt_t ROWN[6] = {TX("VOL"), TX("ARP"), TX("PIT"), TX("VIB"), TX("DTY"), TX("NSE")};
        static const txt_t SWN[3] = {TX("OFF"), TX("DOWN"), TX("UP")};
        chip_patch_t p;
        uint8_t v[64];
        char hd[24], rl[8], hz[12];
        int mx, n;
        chip_unpack(s, &p);
        if (p.osc == CHIP_PULSE) ui_fmt(hd, sizeof hd, "%s  ", T(DUTYN[p.duty]));
        else if (p.osc == CHIP_WAVE) ui_fmt(hd, sizeof hd, "%s  ", T(CHIP_WAVE_NAMES[p.wave & 15]));
        else if (p.osc == CHIP_BUZZ) ui_fmt(hd, sizeof hd, "%s %d  ", T(SH[p.bz_shape]), p.bz_steps ? 32 : 16);
        else ui_fmt(hd, sizeof hd, "  ");
        g_run_t r[4] = {{T(OSCN[p.osc]), C_CYAN}, {hd, C_WHITE}, {T("CLOCK "), C_CYAN}, {T(CLK[p.clock]), C_WHITE}};
        g_runs(0, y, &FONT_T, r, 4);
        n = chip_steps(&p, v, &mx);
        steps_pic(0, y + 14, 114, 62, v, n, mx, col);
        for (int i = 0; i < 6; i++) {
            int yy = y + 12 + 11 * i, kk = ROWK[i];
            ctab_t L;
            g_text(124, yy, &FONT_T, T(ROWN[i]), C_CYAN);
            if (kk < 0 && p.vib_depth) vib_pic(146, yy, 94, 10, &p, 48);
            else if (kk >= 0 && chip_tab(&p, kk, &L)) table_pic(146, yy, 94, 10, kk, &L, 0);
            else g_text(146, yy, &FONT_T, "--", C_DGREY);
        }
        ui_fmt(rl, sizeof rl, "%X  ", p.release);
        ui_fmt(hz, sizeof hz, "%d HZ  ", p.fps50 ? 50 : 60);
        g_run_t b2[7] = {{T("SWEEP "), C_CYAN}, {ui_txt_v("#  ", T(SWN[p.sweep])), C_WHITE}, {T("REL "), C_CYAN}, {rl, C_WHITE},
                         {hz, C_WHITE}, {T("LEVEL "), C_CYAN}, {lv, C_WHITE}};
        g_runs(0, y + 81, &FONT_T, b2, 7);
    }
}

/* a favourite's details (specs/ui/screens/instruments.md#favorites): its source as its own cartridge shows it; nothing
 * for a missing one; an empty one says how to fill it */
static void fav_details(int y)
{
    uint16_t src;
    int f = ui_fav(ui_cur.place, &src);
    if (f == UI_FAV_EMPTY) g_text(4, y + 30, &FONT_T, T("EMPTY SLOT: EDIT PICKS AN INSTRUMENT"), C_DGREY);
    if (f != UI_FAV_OK) return;
    det = src;
    details(y);
    det = 0xFFFFu;
}
/* a favourite's edit mode: which instrument it names, the picked one's details below */
static void draw_fav_edit(void)
{
    char rt[8];
    g_rect(0, BODY_Y, 240, BODY_H, C_BLACK);
    ui_ref_text(rt, ui_cur.place);
    g_text(4, 20, &FONT_T, ui_txt_v(T("FAVORITE #"), rt), C_CYAN);
    g_text(4, 33, &FONT_T, T("SOURCE"), C_DGREY);
    ui_fav_draw(46, 33, ui_cur.place, 0);
    g_text(4, 46, &FONT_T, T("K1: CARTRIDGE  PRESETS: INSTRUMENT"), C_DGREY);
    fav_details(100);
}

/* a value in its 7-character cell: past 7, the space before its unit goes (`-12.5DB`) */
static void cell7(char *v)
{
    int n = (int)strlen(v);
    if (n > 7) {
        static const char *const U[4] = {" DB", " MS", " S", " HZ"};
        for (int k = 0; k < 4; k++) {
            int ul = (int)strlen(U[k]);
            if (n > ul && !strcmp(v + n - ul, U[k])) { memmove(v + n - ul, v + n - ul + 1, (size_t)ul); break; }
        }
    }
    v[7] = 0;
}
/* edit mode: the parameters and a picture */
static void draw_edit(void)
{
    uint8_t s[MAXSLOT];
    int k = kind(), col = ENG_COL[k], sampler = k == REC_SAMPLER;
    g_rect(0, BODY_Y, 240, BODY_H, C_BLACK);
    if (!used(s)) { g_text(4, BODY_Y + 40, &FONT_T, T("EMPTY SLOT: NOTHING TO EDIT"), C_DGREY); return; }
    int pic_y = sampler ? BODY_Y + 83 : BODY_Y, par_y = sampler ? BODY_Y : BODY_Y + 100;   /* (a sampler's table: 4 rows at
                                                         * most, to y 95) */
    const ins_grp_t *g = group();
    int ng;
    const ins_page_t *gs = groups(&ng);
    const ins_page_t *pg;
    int np = pages(&pg);
    char b[48];
    if (k == REC_DX7) {
        int op = g && g->op >= 0 ? g->op : 0, ob = (5 - op) * 113, r[4], l[4];
        algorithm(bits(s, 678 + 56, 5) + 1, 0, pic_y + 2, 120, 92, g ? g->op : -1);
        ui_fmt(b, sizeof b, T("OP%d ENVELOPE"), op + 1);
        g_text(126, pic_y + 2, &FONT_T, b, C_CYAN);
        for (int i = 0; i < 4; i++) { r[i] = bits(s, ob + 7 * i, 7); l[i] = bits(s, ob + 28 + 7 * i, 7); }
        dx_env(r, l, 126, pic_y + 18, 110, 70, col);
    } else if (k == REC_JUNO) {
        g_text(0, pic_y + 2, &FONT_T, T("ENVELOPE"), C_CYAN);
        adsr(bits(s, 77, 7), bits(s, 84, 7), bits(s, 91, 7), bits(s, 98, 7), 0, pic_y + 18, 114, 70, col);
        g_text(124, pic_y + 2, &FONT_T, T("FILTER"), C_CYAN);
        filter_curve(bits(s, 35, 7), bits(s, 42, 7), 124, pic_y + 18, 114, 70, col);
    } else if (k == REC_TB303) {                       /* its envelope and filter, as the Juno-106's */
        int kn[6];
        for (int i = 0; i < 6; i++) kn[i] = bits(s, 7 * i, 7);
        g_text(0, pic_y + 2, &FONT_T, T("ENVELOPE"), C_CYAN);
        const char *acc = T("ACC");
        g_text(114 - g_tw(&FONT_T, acc), pic_y + 2, &FONT_T, acc, C_YELLOW);
        tb_envelope(kn, 0, pic_y + 18, 114, 66, col);
        g_text(124, pic_y + 2, &FONT_T, T("FILTER"), C_CYAN);
        const char *wn = kn[0] < 16 ? T("SAW") : kn[0] > 111 ? T("SQUARE") : T("SAW+SQ");
        g_text(220 - g_tw(&FONT_T, wn), pic_y + 2, &FONT_T, wn, C_LGREY);
        tb_wave(kn[0], 224, pic_y + 4, col);
        tb_filter(kn, 124, pic_y + 18, 114, 66, col);
    } else if (sampler) {
        samp_picture(s, pic_y, col);                    /* (both pages: ALGORITHM turns only the table) */
    } else if (k == REC_FUSION) {
        int pat = s[0];
        rec_pat_t p;
        char sz[32] = "";
        if (pat != 0xFF && !rec_pattern((uint8_t)pat, &p)) ui_fmt(sz, sizeof sz, T("  %d TRACKS, %d ROWS"), p.ntrk, p.rows);
        g_run_t r[3] = {{T("PATTERN "), C_CYAN}, {pat == 0xFF ? "--" : ui_hex2(pat), C_WHITE}, {sz, C_LGREY}};
        g_runs(0, pic_y + 2, &FONT_T, r, 3);
        fusion_map(pat, 0, pic_y + 18, 240, 78);
    }
    chip_patch_t cp = {0};
    if (k == REC_CHIP) {
        chip_unpack(s, &cp);
        chip_picture(&cp, pic_y, col);
    }
    /* the parameters: the selected group's knobs named above, every group's values */
    g_hline(0, par_y, 240, C_DGREY);
    char pn[12];
    ui_page_of(pn, st.page, np);
    g_run_t hr[3] = {{np ? T(pg[st.page].name) : "", C_CYAN}, {pn, C_DGREY}, {np > 1 ? T("  ALGORITHM: PAGES") : "", C_DGREY}};
    char pn2[16];
    ui_fmt(pn2, sizeof pn2, "  %s", pn);
    hr[1].s = pn2;
    g_runs(0, par_y + 2, &FONT_T, hr, 3);
    if (g && g->pattern_row) {
        g_run_t r[6] = {{T("SEL"), C_YELLOW}, {T(": EDIT IT  "), C_LGREY}, {T("ENV"), C_YELLOW}, {T(": PICK  "), C_LGREY},
                        {T("K1"), C_YELLOW}, {T(": OTHER"), C_LGREY}};
        g_runs(0, par_y + 15, &FONT_T, r, 6);
    } else if (g && !g->name_row) {
        ui_fmt(b, sizeof b, g->np > 1 ? T("K1-K%d") : T("K1"), g->np);
        g_text(2, par_y + 15, &FONT_T, b, C_YELLOW);
        for (int q = 0; q < g->np; q++) g_textn(60 + q * 45, par_y + 15, &FONT_T, T(g->p[q].n), 7, q == st.last ? C_WHITE : C_LGREY);
    } else if (g) {
        g_run_t r[2] = {{T("SEL"), C_YELLOW}, {T(": TYPE A NAME"), C_LGREY}};
        g_runs(0, par_y + 15, &FONT_T, r, 2);
    }
    int top = st.group - 3 < ng - 6 ? st.group - 3 : ng - 6;
    if (top < 0) top = 0;
    char nm[16];
    for (int i = top; i < ng && i < top + 6; i++) {
        const ins_grp_t *gg = ins_grp(gs, i);
        int yy = par_y + 30 + (i - top) * 12, on = i == st.group;
        if (on) g_box(0, yy - 1, 240, 12, C_WHITE);
        g_textn(2, yy, &FONT_T, T(gg->name), 9, on ? C_WHITE : C_CYAN);
        if (gg->name_row) { rec_slot_name((uint8_t)k, s, nm); g_text(60, yy, &FONT_T, nm, col); continue; }
        ins_lay_t lay;
        int own = ins_layout(gg, &lay);                 /* (CHIP's wider values, TABLES' rows: columns of their own) */
        for (int q = 0; q < gg->np; q++) {
            ins_text(s, &gg->p[q], b, sizeof b);
            if (own) b[lay.cw[q] < (int)sizeof b ? lay.cw[q] : (int)sizeof b - 1] = 0;
            else cell7(b);
            g_text(own ? lay.cx[q] : 60 + q * 45, yy, &FONT_T, b, on && q == st.last ? C_WHITE : C_LGREY);
        }
        if (own && lay.row) chip_row_pic(lay.row, &cp, yy);
    }
    if (k == REC_CHIP && st.page == 3 && cp.osc != CHIP_WAVE)
        g_text(2, par_y + 42, &FONT_T, T("ONLY THE WAVE OSCILLATOR READS IT"), C_DGREY);
}

/* ------------------------------------------------------------------------------- editing --- */
/* the tail: what plays after a release at most, START (or LOOP START) to END at the base note; a longer
 * fade is never heard, so RELEASE stops there */
static uint32_t tail_ms(const uint8_t *s)
{
    rec_sinst_t si;
    rec_sample_t sm;
    rec_sampler_inst(s, &si);
    if (!sample_info(si.sample, &sm, 0) || !sm.rate) return 0;
    uint32_t end = si.end && si.end < sm.length ? si.end : sm.length, from = si.loop ? si.loop_start : si.start;
    return end > from ? (end - from) * 1000u / sm.rate : 0;
}
static int release_step(const uint8_t *s, int r, int d)
{
    uint32_t tail = tail_ms(s), lim = tail > 4 ? tail : 4;
    int longest = -1;
    for (int i = 7; i < 100; i++) if (RELEASE_MS[i] <= lim) { longest = i; break; }
    for (int n = d < 0 ? -d : d; n > 0; n--) {
        uint32_t now = RELEASE_MS[r];
        int k = r;
        do k -= sign(d); while (k >= 0 && k <= 99 && RELEASE_MS[k] == now);
        if (k > 99 || k < 0 || (d > 0 && k < (longest > 0 ? longest : 0))) break;
        r = k;
    }
    return r;
}
static void whole(uint8_t *s, int idx)                 /* another sample: the instrument plays it whole */
{
    rec_sample_t sm;
    uint32_t len = sample_info(idx, &sm, 0) ? sm.length : 0;
    const ins_page_t *pg;
    ins_pages(REC_SAMPLER, &pg);
    const ins_grp_t *g = pg[0].g;
    ins_set(s, &g[0].p[0], idx);                        /* SAMPLE */
    ins_set(s, &g[0].p[1], 0);                          /* START */
    ins_set(s, &g[0].p[2], (int)len);                   /* END */
    ins_set(s, &g[1].p[0], 0);                          /* LOOP OFF */
    ins_set(s, &g[1].p[1], 0);
    ins_set(s, &g[1].p[2], (int)len);
}
/* a new instrument on sample num (specs/files/instruments.md#sample-instruments): whole, BASE NOTE C-4 at its rate, TUNE
 * none, VOLUME 64, LEVEL 0 dB, a new instrument's envelope; named after its sample (INIT, a missing one's) */
static void new_inst(uint8_t *s, int num)
{
    rec_sample_t sm;
    char nm[16];
    ins_init(REC_SAMPLER, s);
    whole(s, num);
    if (sample_info(num, &sm, nm)) ins_name_set(s, ins_name_pos(REC_SAMPLER), 10, nm);
}
static void turn(const ins_prm_t *p, int sv, int coarse)
{
    uint8_t s[MAXSLOT];
    if (!p || !used(s)) return;
    int v = ins_get(s, p), k = kind(), nv = v;
    if (p->kind == PK_LEVEL) nv = ui_clamp(v + sv * (coarse ? 12 : 1), -128, 127);
    else if (p->kind == PK_RELEASE) nv = release_step(s, v, coarse ? sv * 4 : sv);
    else if (p->kind == PK_PATTERN) {
        uint8_t nums[256];
        int n = ui_patterns(nums, 256), at = -1;        /* (never the EDITOR's own) */
        for (int i = 0; i < n; i++) if (nums[i] == v) at = i;
        if (n) nv = nums[ui_clamp(at + sign(sv), 0, n - 1)];
    } else if (p->kind == PK_LIST) nv = ui_clamp(v + sign(sv), 0, p->max);
    else if (p->kind == PK_CHTAB) nv = ui_clamp(v + sv * (coarse ? 16 : 1), 0, CHIP_TAB_N[p->min]);   /* (its kind's) */
    else if (p->kind == PK_SAMPLE) {                    /* every sample on the FM-1 by number, a click a sample, passing
                                                         * over deletions waiting for SAVE (from a missing one: the next
                                                         * there either way) */
        for (int c = sv > 0 ? sv : -sv; c > 0; c--) {
            int f = nv + sign(sv);
            while (f >= 0 && f < 256 && !pickable(f)) f += sign(sv);
            if (f < 0 || f > 255) break;
            nv = f;
        }
        if (nv == v) return;                            /* (none further: nothing changes) */
    } else {
        int mx = p->max, step = coarse ? 16 : 1;
        if (k == REC_SAMPLER) {
            rec_sinst_t si;
            rec_sample_t sm;
            rec_sampler_inst(s, &si);
            if (p->kind == PK_POS && sample_info(si.sample, &sm, 0)) {
                mx = (int)sm.length;
                step = ((int)sm.length + 100) / 200;
                if (step < 1) step = 1;
                if (coarse) step *= 10;
            }
        }
        if (p->max == 7 || p->max == 3 || !strcmp(T(p->n), "COARSE")) step = 1;
        nv = ui_clamp(v + sv * step, p->min, mx);
    }
    ins_set(s, p, nv);
    if (p->kind == PK_SAMPLE) whole(s, nv);
    if (put_slot(slot(), s)) return;
    char t[24];
    ins_text(s, p, t, sizeof t);
    ui_show_knob(T(p->lng ? p->lng : p->n), t);
}
static void reset(const ins_prm_t *p)
{
    uint8_t s[MAXSLOT], def[MAXSLOT];
    if (!p || p->kind == PK_PATTERN || !used(s)) return;
    ins_init(kind(), def);
    ins_set(s, p, p->kind == PK_LEVEL ? 0 : ins_get(def, p));
    if (put_slot(slot(), s)) return;
    char t[24], v[40];
    ins_text(s, p, t, sizeof t);
    ui_fmt(v, sizeof v, T("%s (DEFAULT)"), t);
    ui_show_knob(T(p->lng ? p->lng : p->n), v);
}
static void edit_close(int keep)
{
    if (!keep && st.changed) ui_edit_session_drop();    /* HOME: the instrument as it was at EDIT */
    else ui_edit_session(0);
    if (st.fe && !keep) { ui_cur.inst = st.fe_was; ui_set_inst(ui_cur.place); }    /* (a favourite's: its source again) */
    st.edit = st.fe = 0;
    st.changed = 0;
}

/* ---------------------------------------------------------------------- a favourite's edit mode --- */
/* (specs/ui/screens/instruments.md#favorites) K1 the source's cartridge over every cartridge with an instrument, the
 * cartridge list showing; PRESETS and SELECT that cartridge's instruments; ENV the same list, ENV again picks. Each
 * change written into the slot at once (the mode's copy kept: HOME drops it), the instrument following it */
static int first_inst(uint8_t c)        /* its first instrument's slot, -1 none (a FAVORITES cartridge has none) */
{
    char nm[12];
    for (int s = 0, n = ui_cart_slots(c); s < n; s++) if (ui_inst_name((uint16_t)(c << 8 | s), nm)) return s;
    return -1;
}
static int reach(uint8_t *nums)         /* K1's cartridges, in the lists' order */
{
    int n = ui_carts(nums, 256), k = 0;
    for (int i = 0; i < n; i++) if (first_inst(nums[i]) >= 0) nums[k++] = nums[i];
    return k;
}
static void fav_put(uint16_t src)       /* the favourite at the cursor names src: the instrument follows */
{
    uint8_t s[3] = {(uint8_t)(src >> 8), (uint8_t)src, 0};
    if (!put_slot(slot(), s)) ui_set_inst(ui_cur.place);
}
static void fav_step(int d)             /* PRESETS, SELECT: the next instrument of the source's cartridge (from its first
                                         * when the source is missing; with no cartridge there, K1's first) */
{
    uint16_t src = 0;
    uint8_t nums[256];
    char nm[12];
    int ok = ui_fav(ui_cur.place, &src) == UI_FAV_OK, c = src >> 8;
    if (first_inst((uint8_t)c) < 0) { if (!reach(nums)) return; c = nums[0]; ok = 0; }
    for (int j = 0, n = ui_cart_slots((uint8_t)c); j < n; j++) {
        int i = ok ? ui_wrap((src & 255) + d * (j + 1), n) : j;
        if (ui_inst_name((uint16_t)(c << 8 | i), nm)) { fav_put((uint16_t)(c << 8 | i)); break; }
    }
    ui_il_show();
}
/* the cartridge list over the screen (dialogs.md#the-cartridge-list), the cartridges K1 reaches: K1's (closing a second
 * after its last click), or ENV's (its own highlight, ENV picks) */
static struct { uint8_t env; int sel; uint32_t at; } fl;
static int fl_sel(const uint8_t *nums, int n)
{
    uint16_t src = 0;
    ui_fav(ui_cur.place, &src);
    for (int k = 0; k < n; k++) if (nums[k] == src >> 8) return k;
    return 0;
}
static void fl_draw(ui_overlay_t *o)
{
    (void)o;
    uint8_t nums[256];
    int n = reach(nums);
    ui_cart_lines(T("CARTRIDGES"), nums, nums, n, fl.env ? fl.sel : fl_sel(nums, n), 0);
}
static void knob(int e, int s, int coarse);
static void fav_cart(int d);
static void fl_knob(ui_overlay_t *o, int e, int s, int c)
{
    uint8_t nums[256];
    if (e == K_K1) { fav_cart(sign(s)); return; }
    if (e == K_SELECT && fl.env) { fl.sel = ui_clamp(fl.sel + s, 0, reach(nums) - 1); return; }
    ui_close(o);                                        /* (any other control: its own job) */
    knob(e, s, c);
}
static int fl_tap(ui_overlay_t *o, int b)
{
    uint8_t nums[256];
    if (b != B_ENV) return 0;                           /* (passes: the list closes, the tap does its job) */
    int n = reach(nums);
    if (fl.env && fl.sel < n) { ui_close(o); fav_put((uint16_t)(nums[fl.sel] << 8 | first_inst(nums[fl.sel]))); return 1; }
    fl.env = 1;                                         /* (open from K1: ENV's now, kept open) */
    fl.sel = fl_sel(nums, n);
    return 1;
}
static ui_overlay_t *fl_find(void) { ui_overlay_t *o = ui_top(); return o && o->draw == fl_draw ? o : 0; }
static void fl_open(int env)
{
    uint8_t nums[256];
    fl.at = ui_now() | 1u;
    fl.env = (uint8_t)env;
    if (env) fl.sel = fl_sel(nums, reach(nums));
    if (fl_find()) return;
    ui_overlay_t o = {.kind = OV_LIST, .opener = (int8_t)(env ? B_ENV : -1), .passes = 1, .knob = fl_knob, .tap = fl_tap,
                      .draw = fl_draw};
    ui_open(&o);
}
static void fav_cart(int d)             /* K1: the source's cartridge; a missing source starts at its own cartridge */
{
    uint8_t nums[256];
    uint16_t src = 0;
    int ok = ui_fav(ui_cur.place, &src) == UI_FAV_OK, n = reach(nums), i = -1;
    if (!n) return;
    for (int k = 0; k < n; k++) if (nums[k] == src >> 8) i = k;
    i = i < 0 ? 0 : ok ? ui_wrap(i + d, n) : i;
    fav_put((uint16_t)(nums[i] << 8 | first_inst(nums[i])));
    fl_open(0);
}
static void fav_edit(void)              /* EDIT on a favourite: an empty one starts at the current instrument */
{
    uint8_t s[3];
    st.edit = st.fe = 1; st.changed = 0; st.fe_was = ui_cur.inst;
    ui_edit_session(1);
    if (!ins_slot_bytes(cart(), slot(), s)) fav_put(ui_cur.inst);
}

/* ------------------------------------------------------------------------------- the menus --- */
static void open_fusion_go(void)
{
    uint8_t s[MAXSLOT];
    if (!used(s) || s[0] == 0xFF) return;
    ed_editor_open(s[0], 0x3C + 12 * ui_cur.octave, 0, ui_cur.inst);   /* (edit mode waits: leave() / enter()) */
}
static void open_fusion(void) { ed_own_ask(open_fusion_go); }    /* (the EDITOR's own pattern's warning first) */
static void rename_done(const char *name)
{
    uint8_t s[MAXSLOT];
    if (!name || !used(s)) return;
    ins_name_set(s, ins_name_pos(kind()), 10, name[0] ? name : T("NO NAME"));
    put_slot(slot(), s);
}
static void rename(void)
{
    uint8_t s[MAXSLOT];
    char nm[16], title[24];
    if (!used(s)) return;
    rec_slot_name((uint8_t)kind(), s, nm);
    ui_ref_text(title + 11, ui_cur.place);
    ui_fmt(title, sizeof title, T("INSTRUMENT %s"), title + 11);
    ui_text_entry(title, nm, 10, B_EDIT, rename_done);
}
/* references (specs/ui/saving.md#references): every cell naming one instrument made to name another
 * (to -1: no instrument), the two swapped when swap */
static void remap(int from, int to, int swap) { ed_remap(from, to, swap, 0); }
static int q_mode, q_r0, q_r1, q_from, q_at, q_uses;    /* the question's change (q_uses: either question's uses) */
enum { Q_DELETE, Q_MOVE, Q_SWAP };
static void change(int upd)
{
    uint8_t a[MAXSLOT], b[MAXSLOT];
    if (q_mode == Q_DELETE) {
        for (int k = q_r0; k <= q_r1; k++) {
            if (upd) remap(cart() << 8 | k, -1, 0);
            put_slot(k, 0);
        }
    } else {
        int ha = ins_slot_bytes(cart(), q_from, a), hb = ins_slot_bytes(cart(), q_at, b);
        put_slot(q_at, ha ? a : 0);
        put_slot(q_from, q_mode == Q_SWAP && hb ? b : 0);
        if (upd) remap(cart() << 8 | q_from, cart() << 8 | q_at, q_mode == Q_SWAP);
        set_inst(cart(), q_at);
    }
    if (upd) ui_save_refs(q_uses);
}
static void q_answer(int a) { if (a >= 0) change(a == 1); }
static void ask_refs(const char *label, int uses)
{
    if (!uses) { change(0); return; }
    const char *lines[2];                               /* (the question copies its lines) */
    char l0[40];
    ui_fmt(l0, sizeof l0, uses == 1 ? T("%s IS USED %d TIME") : T("%s IS USED %d TIMES"), label, uses);
    lines[0] = l0;
    lines[1] = T("UPDATE REFERENCES?");
    q_uses = uses;
    ui_ask(lines, 2, T("YES"), T("NO"), q_answer);
}
static void put_down(void)
{
    int from = st.mv_from, at = st.mv_at;
    st.mv = 0;
    if (at == from) return;
    q_mode = st.mv_swap ? Q_SWAP : Q_MOVE; q_from = from; q_at = at;
    char label[8];
    ui_ref_text(label, (uint16_t)(cart() << 8 | from));
    ask_refs(label, kind() == REC_FAV ? 0 : ed_inst_uses((uint16_t)(cart() << 8 | from)) + (st.mv_swap ? ed_inst_uses((uint16_t)(cart() << 8 | at)) : 0));   /* (nothing refers to a favourite's place) */
}
static void float_step(int d)
{
    uint8_t b[MAXSLOT];
    int k = st.mv_at;
    for (int n = d < 0 ? -d : d; n > 0; n--) {
        int j = k + sign(d);
        if (!st.mv_swap) while (j >= 0 && j < size() && ins_slot_bytes(cart(), j, b) && j != st.mv_from) j += sign(d);
        if (j < 0 || j >= size()) break;
        k = j;
    }
    st.mv_at = k;
}
enum { S_MARK = 1, S_PATTERN, S_COPY, S_PASTE, S_MOVE, S_SWAP, S_DELETE, S_INIT, S_RENAME, S_AUTO, S_REVERT, S_REMOVE,
       S_REPLACE, S_CREATE,             /* (a sampler's: REPLACE SAMPLE; CREATE in INIT's place on an empty slot) */
       S_NEWC = 0x100,                  /* NEW CARTRIDGE's types: + the kind */
       S_ADDTO = 0x200 };               /* ADD TO FAVORITES: + the FAVORITES cartridge */
/* NEW's types, in the menus' order, and their kinds; a new one made at the number after the last of the user's */
static const txt_t NEW_TN[7] = {TX("DX7"), TX("JUNO-106"), TX("TB-303"), TX("CHIP"), TX("SAMPLER"), TX("FUSION"), TX("FAVORITES")};
static const uint8_t NEW_TK[7] = {REC_DX7, REC_JUNO, REC_TB303, REC_CHIP, REC_SAMPLER, REC_FUSION, REC_FAV};
static void new_items(ui_mitem_t *t, int base) { for (int k = 0; k < 7; k++) t[k] = (ui_mitem_t){NEW_TN[k], base + NEW_TK[k], 0, 0}; }
static int new_cart(int kind)
{
    static const txt_t NEWN[7] = {TX("NEW DX7"), TX("NEW JUNO-106"), TX("NEW TB-303"), TX("NEW SAMPLER"), TX("NEW FUSION"),
                                  TX("NEW CHIP"), TX("MY FAVORITES")};      /* (by kind) */
    int id = ins_free_cart();
    char v[24];
    if (id < 0) { ui_toast(T("ALL CARTRIDGE NUMBERS USED")); return -1; }
    ins_new_cart((uint8_t)id, kind, T(NEWN[kind - 1]));
    ui_fmt(v, sizeof v, T("CARTRIDGE %02X MADE"), id);
    ui_toast(v);
    return id;
}
/* the FAVORITES cartridges, in the lists' order (at most max) */
static int favs(uint8_t *f, int max)
{
    uint8_t nums[256];
    int n = ui_carts(nums, 256), k = 0;
    for (int i = 0; i < n && k < max; i++) if (ui_cart_rank(nums[i]) < 2) f[k++] = nums[i];
    return k;
}
/* ADD TO FAVORITES (specs/ui/screens/instruments.md#favorites): the slots' instruments (r0-r1), each into fc's first
 * empty slot, those there already and the empty slots skipped; nothing added unless they all fit */
static void fav_add(uint8_t fc, int r0, int r1)
{
    uint16_t have[128], add[128];
    uint8_t s[MAXSLOT];
    char rt[8];
    int n = 0, there = -1, empty = 0, first = -1;
    for (int k = 0; k < 128; k++) { have[k] = 0xFFFFu; if (ins_slot_bytes(fc, k, s)) have[k] = (uint16_t)(s[0] << 8 | s[1]); else empty++; }
    for (int k = r0; k <= r1; k++) {
        uint16_t src = (uint16_t)(cart() << 8 | k);
        int j = 0;
        if (!ins_slot_bytes(cart(), k, s)) continue;
        while (j < 128 && have[j] != src) j++;
        if (j < 128) { if (there < 0) there = j; continue; }
        add[n++] = src;
    }
    if (!n) { if (there >= 0) { ui_ref_text(rt, (uint16_t)(fc << 8 | there)); ui_toast(ui_txt_v(T("ALREADY IN #"), rt)); } return; }
    if (empty < n) { ui_toast(T("FAVORITES FULL")); return; }
    for (int k = 0, i = 0; k < 128 && i < n; k++) {
        if (have[k] != 0xFFFFu) continue;
        uint8_t b[3] = {(uint8_t)(add[i] >> 8), (uint8_t)add[i], 0};
        if (ins_put_slot(fc, k, b)) return;             /* (MEMORY FOR EDITS IS FULL: said) */
        if (!i++) first = k;
    }
    ui_ref_text(rt, (uint16_t)(fc << 8 | first));
    ui_toast(ui_txt_v(T("ADDED TO #"), rt));
}
/* REVERT (specs/ui/saving.md#revert): the slot as saved, its sound so from its next note */
static void slot_revert_go(void) { ui_edit_revert_slot(cart(), (uint8_t)slot()); }
static int slot_unsaved(void)
{
    for (uint8_t k = REC_DX7; k <= REC_CART_LAST; k++) if (store_piece(k, cart(), (uint16_t)slot(), 0)) return 1;
    return 0;
}
static void auto_done(int set) { if (set && st.edit) st.changed = 1; ui_dirty(); }  /* (edit mode: HOME takes it back too) */
static void slot_pick(int a)
{
    int i = slot(), r0 = st.mark >= 0 ? (st.mark < i ? st.mark : i) : i, r1 = st.mark >= 0 ? (st.mark > i ? st.mark : i) : i;
    uint8_t s[MAXSLOT];
    char v[40];
    st.marking = 0;
    if (a >= S_ADDTO) { fav_add((uint8_t)(a - S_ADDTO), r0, r1); st.mark = -1; return; }
    if (a > S_NEWC) {                                   /* NEW CARTRIDGE: made and opened */
        int id = new_cart(a - S_NEWC);
        if (id >= 0) set_inst(id, 0);
        st.mark = -1;
        return;
    }
    switch (a) {
    case S_MARK: st.marking = 1; st.mark = i; ui_toast(T("SELECT, ALG: MARK  SEL: ITS MENU")); return;
    case S_REMOVE:                                      /* the favourites go, their sources stay; FB emptied is the
                                                         * firmware's own again: its record goes */
        for (int k = r0; k <= r1; k++) put_slot(k, 0);
        if (cart() == 0xFB && !ui_cart_slots(0xFB) && !ins_builtin(0xFB) && ui_cart_name(0xFB, v) && !strcmp(v, T("FAVORITES")))
            ui_edit_del(REC_FAV, 0xFB, 0);
        break;
    case S_PATTERN: open_fusion(); break;
    case S_COPY:
        st.clip_n = 0;
        st.clip_kind = kind();
        for (int k = r0; k <= r1 && st.clip_n < CLIP_SLOTS; k++, st.clip_n++) clip_used[st.clip_n] = (uint8_t)ins_slot_bytes(cart(), k, clip[st.clip_n]);
        ui_fmt(v, sizeof v, T("COPIED %d"), st.clip_n);
        ui_toast(v);
        break;
    case S_PASTE:
        if (!st.clip_n) { ui_toast(T("NOTHING COPIED")); return; }
        if (st.clip_kind != kind()) {                   /* an instrument is its engine's */
            ui_refuse(B_SEL);
            ui_toast(ui_txt_v(T("COPIED FROM #: NOT HERE"), T(ENG_TYPE[st.clip_kind])));
            return;
        }
        for (int k = 0; k < st.clip_n && i + k < size(); k++) put_slot(i + k, clip_used[k] ? clip[k] : 0);
        break;
    case S_INIT:                                        /* (a sampler's: its sample kept, a new instrument's values; an
                                                         * empty slot left empty: an instrument needs a sample) */
        for (int k = r0; k <= r1; k++) {
            if (kind() != REC_SAMPLER) ins_init(kind(), s);
            else if (ins_slot_bytes(cart(), k, s)) new_inst(s, s[0]);
            else continue;
            put_slot(k, s);
        }
        break;
    case S_REPLACE: if (used(s)) sm_open(SM_REPLACE, s[0], 0); break;
    case S_CREATE: sm_open(SM_CREATE, -1, 0); break;
    case S_RENAME: rename(); break;
    case S_AUTO: if (used(s)) ui_auto_level(cart(), i, auto_done); break;
    case S_DELETE: {                                    /* (marked slots: their uses together; in edit mode, a sample
                                                         * instrument's: edit mode left first, its changes kept) */
        if (st.edit) edit_close(1);
        int uses = 0;
        for (int k = r0; k <= r1; k++) if (ins_slot_bytes(cart(), k, s)) uses += ed_inst_uses((uint16_t)(cart() << 8 | k));
        q_mode = Q_DELETE; q_r0 = r0; q_r1 = r1;
        char label[16];
        if (r0 == r1) ui_ref_text(label, ui_cur.place); else ui_fmt(label, sizeof label, T("SLOTS %02X-%02X"), r0, r1);
        ask_refs(label, uses);
        break;
    }
    case S_MOVE: case S_SWAP: {
        if (!used(s)) break;
        int any_empty = 0;
        for (int k = 0; k < size(); k++) if (!ins_slot_bytes(cart(), k, s)) any_empty = 1;
        if (a == S_MOVE && !any_empty) { ui_toast(T("NO EMPTY SLOT: SWAP INSTEAD")); return; }
        st.mv = 1; st.mv_swap = a == S_SWAP; st.mv_from = st.mv_at = i;
        ui_toast(T("SELECT MOVES IT, SEL PUTS IT DOWN"));
        break;
    }
    case S_REVERT: {                                    /* the slot as saved, after a question */
        static char q[32];
        char rt[8];
        ui_ref_text(rt, ui_cur.place);
        ui_fmt(q, sizeof q, T("REVERT SLOT %s TO SAVED?"), rt);
        ui_ask_revert(q, 0, slot_revert_go);
        break;
    }
    default: break;
    }
    st.mark = -1;
}
static void slot_menu(void)
{
    uint8_t s[MAXSLOT], fv[6];
    ui_mitem_t m[14], nc[7], fa[6];                     /* (the menu copies its items) */
    char title[32];
    int n = 0, i = slot(), has = used(s), fav = kind() == REC_FAV, nf = favs(fv, 6), sampler = kind() == REC_SAMPLER;
    int fusion = st.edit && kind() == REC_FUSION && has && s[0] != 0xFF && ui_pattern_rows(s[0]);
    /* a sampler's: REPLACE SAMPLE on one instrument; CREATE in INIT's place on one empty slot (an instrument needs a
     * sample: specs/ui/screens/instruments.md, Sampler cartridges) */
    ui_mitem_t init = sampler && !has && st.mark < 0 ? (ui_mitem_t){TX("CREATE"), S_CREATE, 0, 0} : (ui_mitem_t){TX("INIT"), S_INIT, 0, 0};
    int replace = sampler && has && st.mark < 0;
    /* ADD TO FAVORITES on an instrument, while a FAVORITES cartridge is there: one, an action; several, a group of them
     * (named as listed, below) */
    for (int k = 0; k < nf; k++) fa[k] = (ui_mitem_t){TX("FAVORITES"), S_ADDTO + fv[k], 0, 0};
    ui_mitem_t add = nf == 1 ? fa[0] : (ui_mitem_t){0, 0, fa, (uint8_t)nf};
    add.name = TX("ADD TO FAVORITES");
    int can_add = nf && !fav && (has || st.mark >= 0);
    new_items(nc, S_NEWC);
    if (st.edit) {                                      /* (built in: edited as any) */
        if (fusion) m[n++] = (ui_mitem_t){TX("EDIT PATTERN"), S_PATTERN, 0, 0};
        if (can_add) m[n++] = add;
        m[n++] = (ui_mitem_t){TX("COPY"), S_COPY, 0, 0};
        m[n++] = (ui_mitem_t){TX("PASTE"), S_PASTE, 0, 0};
        if (replace) m[n++] = (ui_mitem_t){TX("REPLACE SAMPLE"), S_REPLACE, 0, 0};
        m[n++] = init;
        m[n++] = (ui_mitem_t){TX("RENAME"), S_RENAME, 0, 0};
        m[n++] = (ui_mitem_t){TX("AUTO LEVEL"), S_AUTO, 0, 0};
        if (sampler && has) m[n++] = (ui_mitem_t){TX("DELETE"), S_DELETE, 0, 0};   /* (edit mode left first) */
        if (slot_unsaved()) m[n++] = (ui_mitem_t){TX("REVERT"), S_REVERT, 0, 0};
    } else {
        if (st.mark < 0) m[n++] = (ui_mitem_t){TX("MARK"), S_MARK, 0, 0};
        if (can_add) m[n++] = add;
        m[n++] = (ui_mitem_t){TX("COPY"), S_COPY, 0, 0};
        m[n++] = (ui_mitem_t){TX("PASTE"), S_PASTE, 0, 0};
        if (replace) m[n++] = (ui_mitem_t){TX("REPLACE SAMPLE"), S_REPLACE, 0, 0};
        m[n++] = (ui_mitem_t){TX("MOVE"), S_MOVE, 0, 0};
        m[n++] = (ui_mitem_t){TX("SWAP"), S_SWAP, 0, 0};
        if (fav) m[n++] = (ui_mitem_t){TX("REMOVE"), S_REMOVE, 0, 0};    /* (a favourite: no sound or name of its own) */
        else {
            m[n++] = (ui_mitem_t){TX("DELETE"), S_DELETE, 0, 0};
            m[n++] = init;
            m[n++] = (ui_mitem_t){TX("RENAME"), S_RENAME, 0, 0};
            m[n++] = (ui_mitem_t){TX("AUTO LEVEL"), S_AUTO, 0, 0};
        }
        m[n++] = (ui_mitem_t){TX("NEW CARTRIDGE"), 0, nc, 7};
        if (st.mark < 0 && slot_unsaved()) m[n++] = (ui_mitem_t){TX("REVERT"), S_REVERT, 0, 0};
    }
    char rt[8];
    ui_ref_text(rt, ui_cur.place);
    int a0 = st.mark < i ? st.mark : i, a1 = st.mark > i ? st.mark : i;
    if (st.mark < 0) ui_fmt(title, sizeof title, T("SLOT %s"), rt);
    else if (fav) ui_fmt(title, sizeof title, T("SLOTS %02X:%02X-%02X"), cart(), a0, a1);
    else ui_fmt(title, sizeof title, T("SLOTS %02X-%02X"), a0, a1);
    ui_menu(st.edit ? "slot-edit" : "slots", title, m, n, slot_pick, B_SEL);
    for (int k = 0; k < nf && nf > 1; k++) {            /* the group's names: `FB FAVORITES`, `07 MY FAVORITES` */
        char nm[16], b[20];
        ui_cart_name(fv[k], nm);
        ui_fmt(b, sizeof b, "%02X %s", fv[k], nm);
        ui_menu_name(S_ADDTO + fv[k], b);
    }
}

/* ---------------------------------------------------------------------- the cartridge list --- */
static struct { uint8_t nums[256]; int n, sel, mv, mv_from, mv_at, act; } cl;
/* its lines: the cartridges, then ALL SAMPLES while the FM-1 holds a sample (the line after the last: cl.n) */
static int cl_lines(void) { rows(); return cl.n + (sr.n > 0); }
static void cl_refill(void) { cl.n = ui_carts(cl.nums, 256); cl.sel = ui_clamp(cl.sel, 0, cl_lines() - 1 > 0 ? cl_lines() - 1 : 0); }
static void cl_select(int id) { for (int i = 0; i < cl.n; i++) if (cl.nums[i] == id) cl.sel = i; }
static void cl_knob(ui_overlay_t *o, int e, int s, int c)
{
    (void)o; (void)c;
    if (e != K_SELECT) return;
    if (cl.mv) {                                         /* within the user's numbers, 00-EF, to free ones (an old
                                                         * sampler's is taken: rec_cart_taken) */
        int k = cl.mv_at;
        do k += sign(s); while (k >= 0 && k <= 0xEF && rec_cart_taken((uint8_t)k) && k != cl.mv_from);
        if (k >= 0 && k <= 0xEF) cl.mv_at = k;
        return;
    }
    if (cl_lines()) cl.sel = ui_clamp(cl.sel + s, 0, cl_lines() - 1);
}
static int cl_uses(uint8_t c)
{
    int u = 0, ns = ui_cart_kind(c) == ENG_FAV ? 0 : ui_cart_slots(c);    /* (nothing refers to a FAVORITES one) */
    uint8_t s[MAXSLOT];
    for (int k = 0; k < ns; k++) if (ins_slot_bytes(c, k, s)) u += ed_inst_uses((uint16_t)(c << 8 | k));
    return u;
}
static void cl_remap_cart(int from, int to) { ed_remap(from, to, 0, 1); }
/* a cartridge moved: the place and the current instrument go with it, each on its own (the place a favourite naming
 * one of its instruments: the instrument moves, the favourite as its references were answered) */
static void cart_follow(int from, int to)
{
    if (cart() == from) set_inst(to, slot());
    else if (ui_cur.inst >> 8 == from) ui_cur.inst = (uint16_t)(to << 8 | (ui_cur.inst & 255));
}
static int cl_q_from, cl_q_to;
static void cl_delete(int upd)
{
    if (upd) cl_remap_cart(cl_q_from, -1);
    ins_delete_cart((uint8_t)cl_q_from);
    cl_refill();
    if (!ins_kind(cart()) && cl.n) set_inst(cl.nums[0], 0);
    if (upd) ui_save_refs(q_uses);
}
/* MOVE of a cartridge, a sampler's as any (its instruments only: the samples are things of their own) */
static void cl_move(int upd)
{
    rec_cart_t c;
    if (!rec_cart((uint8_t)cl_q_from, &c)) {
        char nm[16];
        rec_cart_name(&c, nm);
        ins_copy_cart((uint8_t)cl_q_from, (uint8_t)cl_q_to, nm);
        ui_edit_del(c.kind, (uint8_t)cl_q_from, 0);
    }
    if (upd) cl_remap_cart(cl_q_from, cl_q_to);
    cart_follow(cl_q_from, cl_q_to);
    cl_refill();
    cl_select(cl_q_to);
    if (upd) ui_save_refs(q_uses);
}
static void (*cl_change)(int);
static void cl_answer(int a) { if (a >= 0) cl_change(a == 1); }
static void cl_ask(int c, int uses, void (*change)(int))
{
    if (!uses) { change(0); return; }
    const char *lines[2];
    char l0[40];
    char label[16];
    ui_fmt(label, sizeof label, T("CARTRIDGE %02X"), c);
    ui_fmt(l0, sizeof l0, uses == 1 ? T("%s IS USED %d TIME") : T("%s IS USED %d TIMES"), label, uses);
    lines[0] = l0;
    lines[1] = T("UPDATE REFERENCES?");
    cl_change = change;
    q_uses = uses;
    ui_ask(lines, 2, T("YES"), T("NO"), cl_answer);
}
static void cl_rename_done(const char *name)
{
    if (name) ins_rename_cart((uint8_t)cl.act, name[0] ? name : T("NO NAME"));
}
enum { CM_DX7 = 1, CM_JUNO, CM_TB, CM_SAMPLER, CM_FUSION, CM_CHIP, CM_FAV, CM_CLONE, CM_MOVE, CM_DELETE, CM_RENAME, CM_AUTO,
       CM_BUILTIN, CM_REVERT };                        /* (NEW's types: their kinds, REC_*) */
/* REVERT (specs/ui/saving.md#revert): the whole cartridge as saved, every slot and its name (a cartridge made anew
 * goes; one of another kind at its number comes back) */
static void cl_revert_go(void)
{
    ui_edit_revert(REC_DX7, (uint8_t)cl.act, 0);
    cl_refill();
    cl_select(cl.act);
    if (!ins_kind(cart()) && cl.n) set_inst(cl.nums[0], 0);
}
static void cl_pick(int a)
{
    int c = cl.act, id = ins_free_cart();
    char v[40], nm[16];
    if (a >= CM_DX7 && a <= CM_FAV) {
        if ((id = new_cart(a)) >= 0) { cl_refill(); cl_select(id); }
    } else if (a == CM_CLONE) {                          /* an editable copy in the user area */
        rec_cart_t rc;
        if (id < 0) { ui_toast(T("ALL CARTRIDGE NUMBERS USED")); return; }
        if (rec_cart((uint8_t)c, &rc)) return;
        rec_cart_name(&rc, nm);
        nm[9] = 0;
        ui_fmt(v, sizeof v, "%s 2", nm);
        ins_copy_cart((uint8_t)c, (uint8_t)id, v);
        cl_refill(); cl_select(id);
        ui_fmt(v, sizeof v, T("%02X CLONED TO %02X"), c, id);
        ui_toast(v);
    } else if (a == CM_RENAME) {
        rec_cart_t rc;
        char title[16];
        if (rec_cart((uint8_t)c, &rc)) return;
        rec_cart_name(&rc, nm);
        ui_fmt(title, sizeof title, T("CARTRIDGE %02X"), c);
        ui_text_entry(title, nm, 12, B_EDIT, cl_rename_done);
    } else if (a == CM_DELETE) {
        cl_q_from = c; cl_ask(c, cl_uses((uint8_t)c), cl_delete);
    } else if (a == CM_MOVE) {
        cl.mv = 1; cl.mv_from = cl.mv_at = c; ui_toast(T("SELECT MOVES IT, SEL PUTS IT DOWN"));
    } else if (a == CM_AUTO) ui_auto_level_all(c, 0);
    else if (a == CM_BUILTIN) {                         /* a built-in cartridge's change deleted: the firmware's own */
        char label[20];
        uint8_t kind = (uint8_t)ui_cart_kind((uint8_t)c);  /* (ENG_* are the kinds) */
        ui_edit_del(kind, (uint8_t)c, 0);
        cl_refill(); cl_select(c);
        ui_fmt(label, sizeof label, T("CARTRIDGE %02X"), c);
        ui_toast(ui_txt_v(T("# BUILT-IN AGAIN"), label));
    } else if (a == CM_REVERT) {
        static char q[32];
        ui_fmt(q, sizeof q, T("REVERT CARTRIDGE %02X TO SAVED?"), c);
        ui_ask_revert(q, 0, cl_revert_go);
    }
}
static void cl_menu(void)
{
    if (cl.sel >= cl.n) return;                         /* (ALL SAMPLES: not a cartridge, no menu) */
    int c = cl.nums[cl.sel];
    cl.act = c;
    ui_mitem_t m[8], types[7];                          /* (the menu copies its items) */
    char title[32];
    int n = 0, fav = ui_cart_kind((uint8_t)c) == ENG_FAV;  /* (FAVORITES: no AUTO LEVEL ALL, no sounds of its own) */
    new_items(types, 0);                                /* (CM_DX7 .. CM_FAV are the kinds) */
    m[n++] = (ui_mitem_t){TX("NEW"), 0, types, 7};
    m[n++] = (ui_mitem_t){TX("CLONE"), CM_CLONE, 0, 0};
    if (c >= 0xF0) {                                    /* built in: edited as any; changed, the change can go */
        m[n++] = (ui_mitem_t){TX("RENAME"), CM_RENAME, 0, 0};
        if (!fav) m[n++] = (ui_mitem_t){TX("AUTO LEVEL ALL"), CM_AUTO, 0, 0};
        if (!ins_builtin((uint8_t)c)) m[n++] = (ui_mitem_t){TX("BACK TO BUILT-IN"), CM_BUILTIN, 0, 0};
    } else {
        m[n++] = (ui_mitem_t){TX("MOVE"), CM_MOVE, 0, 0};
        m[n++] = (ui_mitem_t){TX("DELETE"), CM_DELETE, 0, 0};
        m[n++] = (ui_mitem_t){TX("RENAME"), CM_RENAME, 0, 0};
        if (!fav) m[n++] = (ui_mitem_t){TX("AUTO LEVEL ALL"), CM_AUTO, 0, 0};
    }
    if (ui_edit_unsaved(REC_DX7, (uint8_t)c, 0)) m[n++] = (ui_mitem_t){TX("REVERT"), CM_REVERT, 0, 0};
    ui_fmt(title, sizeof title, T("CARTRIDGE %02X"), c);
    ui_menu("carts", title, m, n, cl_pick, B_SEL);
}
static int cl_tap(ui_overlay_t *o, int b)
{
    if (cl.mv) {
        if (b == B_SEL) {
            cl.mv = 0;
            if (cl.mv_at != cl.mv_from) {
                cl_q_from = cl.mv_from; cl_q_to = cl.mv_at; cl_ask(cl.mv_from, cl_uses((uint8_t)cl.mv_from), cl_move);
            }
        }
        return 1;
    }
    if (b == o->opener && cl.sel >= cl.n) { sm_open(SM_ALL, -1, 0); return 1; }   /* ALL SAMPLES: THE SAMPLES list,
                                                         * the cartridge list kept under it */
    if (b == o->opener) {
        int c = cl.n ? cl.nums[cl.sel] : -1;
        ui_close(o);
        if (c >= 0) {
            uint8_t s[MAXSLOT];
            int first = 0, ns = ui_cart_slots((uint8_t)c);
            for (int k = 0; k < ns; k++) if (ins_slot_bytes((uint8_t)c, k, s)) { first = k; break; }
            set_inst(c, first);
        }
        return 1;
    }
    if (b == B_SEL) { cl_menu(); return 1; }
    return 0;
}
static void cl_cancel(ui_overlay_t *o)
{
    if (cl.mv) { cl.mv = 0; ui_toast(T("NOT MOVED")); return; }
    ui_close(o);
}
static void cl_draw(ui_overlay_t *o)
{
    (void)o;
    /* a cartridge afloat is listed under the number it would take, boxed yellow */
    uint8_t ids[256], who[256], rk[256];
    int n = 0, sel = cl.sel;
    for (int i = 0; i < cl.n; i++) {
        int id = cl.mv && cl.nums[i] == cl.mv_from ? cl.mv_at : cl.nums[i], r = cl.mv ? ui_cart_rank(cl.nums[i]) : 0;
        int k = n;                                      /* (in the lists' order: among its rank's, by number) */
        while (cl.mv && k > 0 && (rk[k - 1] << 8 | ids[k - 1]) > (r << 8 | id)) { ids[k] = ids[k - 1]; who[k] = who[k - 1]; rk[k] = rk[k - 1]; k--; }
        ids[k] = (uint8_t)id; who[k] = cl.nums[i]; rk[k] = (uint8_t)r;
        n++;
    }
    if (cl.mv) for (int i = 0; i < n; i++) if (who[i] == cl.mv_from) sel = i;
    int more = cl_lines() - cl.n, y = ui_cart_lines_more(T("CARTRIDGES"), ids, who, n, more, sel, cl.mv);
    if (y < 0) return;
    /* the samples' line (specs/ui/screens/instruments.md#the-cartridges-env): nothing in the ID column, ALL SAMPLES
     * yellow as the samples' names (the cursor's white), their size as STATUS writes sizes, their count of 256 */
    char sz[12], cnt[12];
    int on = !cl.mv && sel == cl.n;
    if (on) g_box(4, y - 1, 232, 13, C_WHITE);
    g_text(26, y, &FONT_T, T("ALL SAMPLES"), on ? C_WHITE : C_YELLOW);
    if (sr.bytes < 1024u) ui_fmt(sz, sizeof sz, "%u B", sr.bytes);
    else if (sr.bytes < 10240u) { uint32_t t = (sr.bytes * 10u + 512u) / 1024u; ui_fmt(sz, sizeof sz, "%u.%uK", t / 10u, t % 10u); }
    else ui_fmt(sz, sizeof sz, "%uK", (sr.bytes + 512u) / 1024u);
    g_text(110, y, &FONT_T, sz, C_LGREY);
    ui_fmt(cnt, sizeof cnt, "%03d/256", sr.n);
    g_text(170, y, &FONT_T, cnt, C_LGREY);
}
static void cart_list(void)
{
    cl.mv = 0;
    cl_refill();
    cl_select(cart());
    ui_overlay_t o = {.kind = OV_LIST, .opener = B_ENV, .knob = cl_knob, .tap = cl_tap, .cancel = cl_cancel, .draw = cl_draw};
    ui_open(&o);
}

/* ---------------------------------------------------------------------------- THE SAMPLES list --- */
/* every sample on the FM-1, a row a sample in number order (rows(): those whose deletion waits for SAVE among them,
 * `(DELETED)`), titled SAMPLES (23) however it opens (specs/ui/screens/instruments.md#the-samples):
 *   SM_INST     ENV on a sample instrument's SAMPLE (edit mode): the keys play the highlighted sample through the
 *               instrument, ENV picks it (played whole), HOME leaves the instrument as it was;
 *   SM_REPLACE  REPLACE SAMPLE: the keys play it as a new instrument would, ENV makes the instrument play it as a new one
 *               would (its name following when it was the old sample's), HOME leaves it as it was;
 *   SM_CREATE   CREATE (and EDIT on an empty sampler slot: edit then opens on the new instrument): ENV makes one there,
 *               named after its sample, HOME makes nothing;
 *   SM_ALL      the cartridge list's ALL SAMPLES line: to look, listen and delete; ENV or HOME back to the line.
 * A `(DELETED)` row is not picked (ENV refused) and SEL there opens nothing. sel the row highlighted */
static struct {
    int16_t sel;
    uint8_t mode, edit, keys;
    uint8_t del;                                /* DELETE's sample (its question's) */
    uint16_t del_n;                             /* ... and the instruments that use it */
} sm;
static int sm_num(void) { return row_num(sm.sel); }     /* the highlighted row's sample, -1 none */
static void sm_close(ui_overlay_t *o)
{
    if (sm.keys) player_keys_off();                     /* (its keys' notes: their let-go would find no list) */
    sm.keys = 0;
    player_keys_sample(0);
    ui_close(o);
}

/* Its SEL menu (specs/ui/screens/instruments.md): `SAMPLE 03`: DELETE. Deleting a sample deletes the instruments that
 * use it in every sampler (specs/files/instruments.md#deleting), counted in its warning */
enum { SMM_DELETE = 1 };
static int sm_users(int num)                    /* every sampler's instruments that play the sample */
{
    uint8_t carts[256];
    int nc = store_list(REC_SAMPLER, carts, 256), n = 0;
    for (int i = 0; i < nc; i++) {
        rec_cart_t c;
        if (rec_cart(carts[i], &c) || c.kind != REC_SAMPLER) continue;    /* (an old sampler: never touched) */
        for (int s = 0; s < c.nslots; s++) {
            const uint8_t *x = rec_slot(&c, (uint8_t)s);
            rec_sinst_t si;
            if (!x) continue;
            rec_sampler_inst(x, &si);
            n += si.sample == num;
        }
    }
    return n;
}
/* the sample deleted (its record: a sample's number stays its own) and every instrument that played it emptied, the
 * room for it all there first (MEMORY FOR EDITS IS FULL: nothing changed). Saved together, or, storage full, its
 * deletion first (the store's deletions first) */
static void sm_delete_now(int users)
{
    uint8_t carts[256], s[MAXSLOT];
    char v[24];
    int num = sm.del, nc = store_list(REC_SAMPLER, carts, 256);
    if (ui_edit_reserve((uint32_t)users * 80u + 32u) || ui_edit_del(REC_SAMPLE, (uint8_t)num, 0)) return;
    for (int i = 0; i < nc; i++) {
        if (ins_kind(carts[i]) != REC_SAMPLER) continue;
        for (int k = 0; k < 64; k++)                    /* (each read anew: the one before changed the record) */
            if (ins_slot_bytes(carts[i], k, s) && s[0] == num) ins_put_slot(carts[i], k, 0);
    }
    if (users) return;
    ui_fmt(v, sizeof v, T("SAMPLE %02X DELETED"), num);
    ui_toast(v);
}
static void sm_deleted(int a) { if (a == 1) sm_delete_now(sm.del_n); }
static void sm_pick(int a)
{
    if (a != SMM_DELETE) return;
    if (st.edit) {                                      /* (in edit mode: left first, its changes kept, so HOME never
                                                         * takes a confirmed deletion back with them; ENV on SAMPLE's list,
                                                         * an edit mode's, then only looks: ENV and HOME close it) */
        edit_close(1);
        if (sm.mode == SM_INST) sm.mode = SM_ALL;
    }
    int n = sm_users(sm.del);
    sm.del_n = (uint16_t)n;
    if (!n) { sm_delete_now(0); return; }
    const char *l[3] = {T("DELETE SAMPLE?"), 0, T("DELETED TOO")};
    char two[40];
    if (n == 1) ui_fmt(two, sizeof two, "%s", T("1 INSTRUMENT USES IT AND IS"));
    else ui_fmt(two, sizeof two, "%s", ui_txt_v(T("# INSTRUMENTS USE IT AND ARE"), ui_dec(n)));
    l[1] = two;
    ui_ask(l, 3, T("DELETE"), T("KEEP"), sm_deleted);
}
static void sm_menu(void)
{
    ui_mitem_t m[1];                                    /* (the menu copies its items) */
    char title[24];
    int num = sm_num();
    if (!pickable(num)) return;                         /* (a deletion waiting: nothing) */
    sm.del = (uint8_t)num;
    m[0] = (ui_mitem_t){TX("DELETE"), SMM_DELETE, 0, 0};
    ui_fmt(title, sizeof title, T("SAMPLE %02X"), num);
    ui_menu("samples", title, m, 1, sm_pick, B_SEL);
}
static void sm_knob(ui_overlay_t *o, int e, int s, int c)
{
    (void)o; (void)c;
    rows();
    if (e == K_SELECT && sr.n) sm.sel = (int16_t)ui_clamp(sm.sel + s, 0, sr.n - 1);
}
/* what ENV would make of sample num: the instrument playing it whole (SM_INST), else a new instrument on it */
static void sm_bytes(uint8_t *s, int num)
{
    if (sm.mode == SM_INST && used(s)) whole(s, num);
    else new_inst(s, num);
}
static int sm_key(ui_overlay_t *o, int n, int down)     /* the keys: the highlighted sample, before it is picked */
{
    (void)o;
    uint8_t s[MAXSLOT];
    int m = ui_midi(n), num = sm_num();
    if (m < 12 || m > 127) return 1;
    if (!down) { player_key_off((uint8_t)m); return 1; }
    if (num < 0) return 1;
    sm_bytes(s, num);                                   /* (a deletion waiting: no sample read, silent) */
    player_keys_sample(s);
    pl_inst_t in = {PL_KEYS_SAMPLE_CART, PL_KEYS_SAMPLE_SLOT};    /* (FE:FF, apart from the UI's "none" 0xFFFF:
                                                                 * player.h) */
    player_key_on((uint8_t)m, in, (uint8_t)ui_set[S_KEYS_VOLUME]);
    sm.keys = 1;
    return 1;
}
static void sm_put(int num)                     /* ENV's pick */
{
    uint8_t s[MAXSLOT], was[MAXSLOT];
    char inm[16], snm[16];
    rec_sample_t sa;
    if (sm.mode == SM_INST) { if (used(s)) { whole(s, num); put_slot(slot(), s); } return; }
    if (sm.mode == SM_REPLACE) {                /* every value a new instrument's; its name following when it was the
                                                 * old sample's */
        if (!used(was)) return;
        new_inst(s, num);
        rec_slot_name(REC_SAMPLER, was, inm);
        if (!sample_info(was[0], &sa, snm) || strcmp(inm, snm)) ins_name_set(s, ins_name_pos(REC_SAMPLER), 10, inm);
        put_slot(slot(), s);
        return;
    }
    new_inst(s, num);                           /* CREATE: a new instrument there */
    if (sm.edit) {                              /* (EDIT's: edit mode on it, as it was at EDIT should HOME leave it) */
        st.edit = 1; st.page = 0; st.group = 0; st.last = -1; st.changed = 0;
        ui_edit_session(1);
    }
    put_slot(slot(), s);
}
static int sm_tap(ui_overlay_t *o, int b)
{
    if (b == B_ENV) {
        int num = sm_num();
        if (sm.mode == SM_ALL) { sm_close(o); return 1; }   /* (back to the line) */
        if (!pickable(num)) { ui_refuse(B_ENV); return 1; }
        sm_close(o);
        sm_put(num);
        return 1;
    }
    if (b == B_SEL) { sm_menu(); return 1; }
    return 0;
}
static void sm_draw(ui_overlay_t *o)
{
    (void)o;
    int y = BODY_Y + 2;
    char title[40], nm[16], t[16];
    rows();                                             /* (as they are now: a deletion meanwhile) */
    sm.sel = (int16_t)ui_clamp(sm.sel, 0, sr.n - 1 > 0 ? sr.n - 1 : 0);
    ui_fmt(title, sizeof title, "%s", ui_txt_v(T("SAMPLES (#)"), ui_dec(sr.n)));
    ui_panel(2, y, 236, 206, title, C_CYAN);
    int top = sm.sel - 6 < sr.n - 14 ? sm.sel - 6 : sr.n - 14;
    if (top < 0) top = 0;
    for (int k = 0, i = -1; k < 256 && i < top + 13; k++) {
        if (!bit(sr.has, k) || ++i < top) continue;
        rec_sample_t s;
        uint32_t length, rate, len;
        int yy = y + 18 + (i - top) * 13, on = i == sm.sel, gone = !sample_info(k, &s, nm);
        if (gone) {                                     /* (its deletion waits for SAVE: all grey, (DELETED)) */
            const uint8_t *e = store_get_saved(REC_SAMPLE, (uint8_t)k, 0, &len, 0);
            if (!e) continue;
            length = rec_bits(e, 0, 20);
            rate = rec_bits(e, 20, 18);
        } else {
            length = s.length;
            rate = s.rate;
        }
        if (on) g_box(4, yy - 1, 232, 13, C_WHITE);
        g_text(8, yy, &FONT_T, ui_hex2(k), C_DGREY);
        g_textn(24, yy, &FONT_T, gone ? T("(DELETED)") : nm, 12, gone ? C_DGREY : on ? C_WHITE : C_YELLOW);
        uint32_t cs = rate ? (length * 100 + rate / 2) / rate : 0;
        ui_fmt(t, sizeof t, "%u.%02uS", cs / 100, cs % 100);
        g_text(100, yy, &FONT_T, t, gone ? C_DGREY : C_LGREY);
        g_text(140, yy, &FONT_T, ui_dec((int)rate), C_DGREY);
        if (on && !gone) wave(k, 176, yy, 56, 10, C_BROWN, 0, 0, 0);   /* (the highlighted one's: each is decoded to draw) */
    }
}
/* the list's rows, sample num highlighted (else the first that can be picked; SM_ALL's the first) */
static void sm_fill(int num)
{
    int r = pickable(num) || (sm.mode == SM_ALL && row_of(num) >= 0) ? row_of(num) : -1;
    for (int k = 0; k < 256 && r < 0; k++) if (pickable(k) || (sm.mode == SM_ALL && bit(sr.has, k))) r = row_of(k);
    sm.sel = (int16_t)(r < 0 ? 0 : r);
}
/* THE SAMPLES list opened (mode SM_*; num the sample to highlight; edit: CREATE's from EDIT): with no sample on the FM-1
 * to pick, `NO SAMPLES ON THE FM-1` and nothing opens (ALL SAMPLES' line is there only while a sample is) */
static void sm_open(int mode, int num, int edit)
{
    rows();
    if (mode != SM_ALL && !sr.picks) { ui_toast(T("NO SAMPLES ON THE FM-1")); return; }
    sm.mode = (uint8_t)mode;
    sm.edit = (uint8_t)edit;
    sm.keys = 0;
    sm_fill(num);
    ui_overlay_t o = {.kind = OV_LIST, .opener = B_ENV, .knob = sm_knob, .tap = sm_tap, .cancel = sm_close, .draw = sm_draw,
                      .key = sm_key};
    ui_open(&o);
}
static void sample_list(void)                   /* ENV on SAMPLE: the instrument's sample highlighted */
{
    uint8_t s[MAXSLOT];
    if (used(s)) sm_open(SM_INST, s[0], 0);
}

/* -------------------------------------------------------------------- the FUSION fill question --- */
/* EDIT on an empty FUSION slot: a new pattern of 128 rows, EDIT STEP 0, offered filled with the last
 * instrument picked, its note on each row; K1 another cartridge, SELECT another sound; the keys play it */
static int fill_src, fill_pat;
/* the sounds a FUSION can be filled with: every used slot of the cartridges that are not FUSION, in
 * order; the one after (d 1) or before (d -1) ref, or ref itself at an end. cart: a cartridge at a time
 * (each at its first) */
static int sound_step(int ref, int d, int by_cart)
{
    uint8_t carts[256];
    int nc = ui_carts(carts, 256), ci = -1;
    char nm[16];
    for (int i = 0; i < nc; i++) if (ref >= 0 && carts[i] == ref >> 8) ci = i;
    for (int i = ci < 0 ? (d > 0 ? 0 : nc - 1) : ci; i >= 0 && i < nc; i += d) {
        if (ui_cart_kind(carts[i]) == ENG_FUSION) continue;
        int ns = ui_cart_slots(carts[i]);
        if (by_cart) {
            if (i == ci) continue;
            for (int s = 0; s < ns; s++) if (ui_inst_name((uint16_t)(carts[i] << 8 | s), nm)) return carts[i] << 8 | s;
            continue;
        }
        int from = i == ci ? (ref & 255) + d : d > 0 ? 0 : ns - 1;
        for (int s = from; s >= 0 && s < ns; s += d) if (ui_inst_name((uint16_t)(carts[i] << 8 | s), nm)) return carts[i] << 8 | s;
    }
    return ref;
}
static void fill_fn(int t, int r, rec_cell_t *c, void *a)
{
    (void)t; (void)a;
    if (r < 12) return;                                 /* (rows under C-0 never play) */
    c->has = RC_NOTE | RC_INS;
    c->note = (uint8_t)r;
    c->cart = (uint8_t)(fill_src >> 8);
    c->slot = (uint8_t)fill_src;
}
static void fill_make(int fill)
{
    uint8_t s[MAXSLOT];
    char nm[16];
    ed_new_pattern((uint8_t)fill_pat, 4, 128, 0);
    if (fill) ed_rewrite((uint8_t)fill_pat, 0, 0, fill_fn, 0, -1, -1, -1);
    if (!used(s)) {
        ins_init(REC_FUSION, s);
        ui_fmt(nm, sizeof nm, T("FUSION %02X"), fill_pat);
        ins_name_set(s, ins_name_pos(REC_FUSION), 10, nm);
    }
    s[0] = (uint8_t)fill_pat;
    put_slot(slot(), s);
    ed_editor_open((uint8_t)fill_pat, 0x3C + 12 * ui_cur.octave, 0, ui_cur.inst);
}
static void fill_ask(void);
static void fill_answer(int a)
{
    player_keys_off();
    if (a >= 0) fill_make(a == 1);
}
static void fill_knob(ui_overlay_t *o, int e, int s, int c)
{
    (void)c;
    int d = sign(s);
    if (e == K_SELECT) fill_src = sound_step(fill_src, d, 0);
    else if (e == K_K1) fill_src = sound_step(fill_src, d, 1);  /* another cartridge, at its first */
    else return;
    ui_close(o);
    fill_ask();
}
static int fill_key(ui_overlay_t *o, int n, int down)
{
    (void)o;
    int m = ui_midi(n);
    if (m < 12 || m > 127) return 1;
    if (down) { pl_inst_t in = {(uint8_t)(fill_src >> 8), (uint8_t)fill_src}; player_key_on((uint8_t)m, in, (uint8_t)ui_set[S_KEYS_VOLUME]); }
    else player_key_off((uint8_t)m);
    return 1;
}
static void fill_ask(void)
{
    const char *lines[4];                               /* (the question copies its lines) */
    char l1[40];
    char rt[8], nm[16];
    ui_ref_text(rt, (uint16_t)fill_src);
    if (!ui_inst_name((uint16_t)fill_src, nm)) nm[0] = 0;
    ui_fmt(l1, sizeof l1, T("FILL IT WITH %s %s?"), rt, nm);
    lines[0] = T("A NEW PATTERN FOR THIS SLOT");
    lines[1] = l1;
    lines[2] = T("ITS NOTE ON EACH ROW: THE SAME SOUND");
    lines[3] = T("K1, SELECT: ANOTHER  KEYS: HEAR IT");
    ui_ask(lines, 4, T("FILL"), T("EMPTY"), fill_answer);
    ui_overlay_t *q = ui_top();
    if (q) { q->knob = fill_knob; q->key = fill_key; }
}
static void fusion_new_go(void)
{
    fill_pat = rec_free_pattern();
    if (fill_pat < 0) { ui_toast(T("ALL 240 PATTERNS USED")); return; }
    char nm[12];                                        /* the last sound picked, else the first there is */
    int last = ui_last_inst != 0xFFFFu && ui_cart_kind((uint8_t)(ui_last_inst >> 8)) != ENG_FUSION && ui_inst_name(ui_last_inst, nm);
    fill_src = last ? ui_last_inst : sound_step(-1, 1, 0);
    if (fill_src < 0) { fill_make(0); return; }
    fill_ask();
}
static void fusion_new(void) { ed_own_ask(fusion_new_go); }      /* (it ends in the EDITOR: its own pattern's warning first) */

/* ----------------------------------------------------------------------------- the screen --- */
static void cart_name_done(const char *name) { if (name) ins_rename_cart(cart(), name[0] ? name : T("NO NAME")); }
static const char *p_name(void) { static char nm[16]; rec_cart_t c; if (rec_cart(cart(), &c)) return ""; rec_cart_name(&c, nm); return nm; }
static void p_name_edit(void)
{
    char title[16];
    ui_fmt(title, sizeof title, T("CARTRIDGE %02X"), cart());
    ui_text_entry(title, p_name(), 12, B_EDIT, cart_name_done);
}
static const char *p_type(void) { return T(ENG_TYPE[kind()]); }
static const char *p_used(void)
{
    static char b[16];
    uint8_t s[MAXSLOT];
    int u = 0;
    for (int k = 0; k < size(); k++) if (ins_slot_bytes(cart(), k, s)) u++;
    ui_fmt(b, sizeof b, T("%d OF %d"), u, size());
    return b;
}
static const char *p_with(void) { return T("EDIT"); }
static const char *sub(void) { return st.edit ? T("EDIT") : 0; }
static const char *seq(void)
{
    static char b[12];
    const ins_page_t *p;
    int n = pages(&p);
    if (!st.edit || n < 2) return 0;
    ui_page_of(b, st.page, n);
    return b;
}
static const char *page(void)
{
    static char b[12];
    int n = size();
    if (st.edit || n <= 16) return 0;
    ui_page_of(b, (st.mv ? st.mv_at : slot()) / 16, (n + 15) / 16);
    return b;
}
static int env_list(void)                               /* in edit mode: 1 the samples, 2 the patterns, 0 none */
{
    const ins_grp_t *g = st.edit ? group() : 0;
    if (!g) return 0;
    if (kind() == REC_SAMPLER && st.page == 0 && !strcmp(T(g->name), "SAMPLE")) return 1;
    if (g->pattern_row) return 2;
    return 0;
}
static int lit(int b)
{
    const ins_page_t *p;
    if (b == B_EDIT && st.edit) return LV_FULL;
    if (b == B_SEQ) return st.edit && pages(&p) > 1 ? LV_DIM : 0;
    if (b == B_SEL && (st.mark >= 0 || st.mv)) return LV_FULL;
    if (b == B_ENV && st.edit && !st.fe && !env_list()) return 0;
    if (b == B_LFO && st.fe) return 0;                  /* (a favourite's edit mode: nothing to reset) */
    return LV_NONE;
}
static int sel_lines(const char **lines, int max)
{
    if (max < 1) return 0;
    lines[0] = st.edit ? T("K1-K4: BIG STEPS") : T("SELECT, ALGORITHM: MARK SLOTS");
    return 1;
}
static void step_cart(int d)
{
    uint8_t nums[256], s[MAXSLOT];
    int n = ui_carts(nums, 256), i = 0;
    if (!n) return;
    for (int k = 0; k < n; k++) if (nums[k] == cart()) i = k;
    int c = nums[ui_clamp(i + d, 0, n - 1)], first = 0, ns = ui_cart_slots((uint8_t)c);
    for (int k = 0; k < ns; k++) if (ins_slot_bytes((uint8_t)c, k, s)) { first = k; break; }
    set_inst(c, first);
}
static void mark(int e, int s)
{
    if (st.edit) return;
    if (st.mark < 0) st.mark = slot();
    set_inst(cart(), ui_clamp(slot() + s * (e == K_ALG ? 8 : 1), 0, size() - 1));
}
static void knob(int e, int s, int coarse)
{
    if (st.fe) {                                        /* a favourite's: K1 the cartridge, SELECT its instruments */
        if (e == K_K1) fav_cart(sign(s));
        else if (e == K_SELECT) fav_step(sign(s));
        return;
    }
    if (st.edit) {
        int ng;
        const ins_page_t *p;
        groups(&ng);
        if (e == K_SELECT) { st.group = ui_clamp(st.group + sign(s), 0, ng - 1); st.last = -1; return; }
        if (e == K_ALG) { st.page = ui_clamp(st.page + sign(s), 0, pages(&p) - 1); st.group = 0; st.last = -1; return; }
        if (e >= K_K1 && e <= K_K4) {
            const ins_grp_t *g = group();
            if (!g || g->name_row) return;
            int q = e - K_K1;
            if (q >= g->np) return;
            st.last = q;
            turn(&g->p[q], s, coarse);
        }
        return;
    }
    if (st.mv) { if (e == K_SELECT) float_step(sign(s)); else if (e == K_ALG) float_step(sign(s) * 8); return; }
    if (st.marking && (e == K_SELECT || e == K_ALG)) { mark(e, s); return; }
    if (e == K_SELECT) { st.mark = -1; set_inst(cart(), ui_clamp(slot() + s, 0, size() - 1)); }
    else if (e == K_ALG) { st.mark = -1; set_inst(cart(), ui_clamp(slot() + sign(s) * 8, 0, size() - 1)); }
    else if (e == K_K1) step_cart(sign(s));
}
static void pattern_pick(int n)
{
    uint8_t s[MAXSLOT];
    if (!used(s)) return;
    s[0] = (uint8_t)n;
    put_slot(slot(), s);
}
static int tap(int b)
{
    uint8_t s[MAXSLOT];
    if (st.mv) { if (b == B_SEL) put_down(); return 1; }         /* afloat: only SEL (and HOME) */
    if (st.fe && b != B_EDIT && b != B_GLO) {           /* a favourite's edit mode: ENV the cartridges; SEL, ALGORITHM,
                                                         * LFO nothing */
        if (b == B_ENV) fl_open(1);
        return b == B_ENV || b == B_SEL || b == B_LFO || b == B_SEQ;
    }
    if (b == B_EDIT) {
        if (st.edit) { edit_close(1); return 1; }       /* keeping the changes */
        if (kind() == REC_FAV) { fav_edit(); return 1; }    /* (which instrument the favourite names) */
        int has = used(s);                              /* (built in: edited as any) */
        if (kind() == REC_FUSION && (!has || s[0] == 0xFF || !ui_pattern_rows(s[0]))) { fusion_new(); return 1; }
        if (kind() == REC_SAMPLER && !has) { sm_open(SM_CREATE, -1, 1); return 1; }   /* (CREATE's pick, then edit mode) */
        if (!has) { ui_toast(T("EMPTY SLOT: SEL, INIT")); return 1; }
        st.edit = 1; st.page = 0; st.group = 0; st.last = -1; st.changed = 0;
        ui_edit_session(1);                             /* the mode's copy: HOME puts back what it changed */
        return 1;
    }
    if (b == B_SEQ && st.edit) {
        const ins_page_t *p;
        int n = pages(&p);
        st.page = n ? (st.page + 1) % n : 0; st.group = 0; st.last = -1;
        return 1;
    }
    if (b == B_LFO && st.edit) {
        const ins_grp_t *g = group();
        if (g && !g->name_row && g->np) reset(&g->p[st.last >= 0 ? st.last : 0]);
        return 1;
    }
    if (b == B_SEL) {                                   /* on the edit page's NAME or PATTERN: straight there */
        const ins_grp_t *g = st.edit ? group() : 0;
        if (g && g->name_row) rename();
        else if (g && g->pattern_row && used(s) && s[0] != 0xFF && ui_pattern_rows(s[0])) open_fusion();
        else slot_menu();
        return 1;
    }
    if (b == B_GLO) {
        static char title[16];
        ui_prop_t items[4] = {{TX("NAME"), p_name, 0, p_name_edit, 0, 0}, {TX("TYPE"), p_type, 0, 0, 0, 0},
                              {TX("SLOTS USED"), p_used, 0, 0, 0, 0}, {TX("RENAME WITH"), p_with, 0, 0, 0, 0}};
        ui_fmt(title, sizeof title, T("CARTRIDGE %02X"), cart());
        ui_props(title, items, 4, B_GLO);
        return 1;
    }
    if (b == B_ENV) {
        int l = env_list();
        if (l == 1) { sample_list(); return 1; }
        if (l == 2) {                                   /* on a FUSION slot's PATTERN: the pattern list */
            char title[24], rt[8];
            ui_ref_text(rt, ui_cur.inst);
            ui_fmt(title, sizeof title, T("PATTERN FOR %s"), rt);
            ed_pattern_list(title, used(s) ? s[0] : -1, pattern_pick, B_ENV, ui_cur.inst);
            return 1;
        }
        if (!st.edit) cart_list();                      /* the cartridges while browsing; edit mode keeps its instrument */
        return 1;
    }
    return 0;                                           /* (ARP: the arpeggiator, the core's) */
}
static int back(void)
{
    if (st.mv) { st.mv = 0; ui_toast(T("NOT MOVED")); return 1; }
    if (st.edit) { edit_close(0); return 1; }           /* HOME: dropping them */
    if (st.mark >= 0) { st.mark = -1; st.marking = 0; return 1; }
    return 0;
}
/* edit mode stays across a trip to another screen (a FUSION's pattern in the EDITOR): its session ends
 * there, keeping its changes, and a new one starts on the way back */
static void leave(const ui_screen_t *to) { (void)to; st.marking = 0; if (st.edit) { ui_edit_session(0); st.changed = 0; } }
static void enter(const ui_screen_t *from) { (void)from; if (st.edit) { ui_edit_session(1); st.changed = 0; } }
static const char *footer(uint8_t *col)
{
    static char b[24];
    rec_cart_t c;
    char nm[16];
    if (rec_cart(cart(), &c)) return 0;
    rec_cart_name(&c, nm);
    ui_fmt(b, sizeof b, "%02X %s", cart(), nm);
    *col = ENG_COL[c.kind];
    return b;
}
static void tick(void)
{
    /* another engine may have fewer pages and groups (PRESETS into another cartridge) */
    const ins_page_t *p;
    int np = pages(&p), ng;
    st.page = ui_clamp(st.page, 0, np - 1 > 0 ? np - 1 : 0);
    groups(&ng);
    st.group = ui_clamp(st.group, 0, ng - 1 > 0 ? ng - 1 : 0);
    ui_overlay_t *o = fl_find();                        /* K1's cartridge list: a second after its last click */
    if (o && !fl.env && (int32_t)(ui_now() - fl.at) >= 1000) ui_close(o);
}
static void draw(void)
{
    if (st.edit) { if (st.fe) draw_fav_edit(); else draw_edit(); return; }
    rec_cart_t c;
    if (rec_cart(cart(), &c)) {
        uint8_t nums[256];
        if (ui_carts(nums, 256)) set_inst(nums[0], 0);
        return;
    }
    int k = c.kind, col = ENG_COL[k], us = 0;
    uint8_t s[MAXSLOT];
    char nm[16], cnt[12];
    for (int q = 0; q < c.nslots; q++) if (ins_slot_bytes(cart(), q, s)) us++;
    rec_cart_name(&c, nm);
    ui_fmt(cnt, sizeof cnt, "%d/%d", us, ENG_SLOTS[k]);
    char id[4], nm2[16], ty[16];
    ui_fmt(id, sizeof id, "%02X ", cart());
    ui_fmt(nm2, sizeof nm2, "%s ", nm);
    ui_fmt(ty, sizeof ty, "%s ", T(ENG_TYPE[k]));
    g_run_t r[4] = {{id, C_WHITE}, {nm2, C_LGREY}, {ty, (uint8_t)col}, {cnt, C_DGREY}};
    if (strlen(id) + strlen(nm2) + strlen(ty) + strlen(cnt) > 30) g_runs(0, BODY_Y, &FONT_T, r, 4);    /* (past its 30
                                                         * characters: in T) */
    else g_runs_crop(0, BODY_Y, &FONT_S, r, 4, 0, 16);
    int cur = st.mv ? st.mv_at : slot(), pg = cur / 16 * 16;
    int m0 = st.mark >= 0 ? (st.mark < slot() ? st.mark : slot()) : -1, m1 = st.mark >= 0 ? (st.mark > slot() ? st.mark : slot()) : -1;
    for (int q = 0; q < 16; q++) {
        int i = pg + q;
        if (i >= ENG_SLOTS[k]) break;
        int x = q < 8 ? 0 : 120, y = BODY_Y + 17 + (q % 8) * 12, on = i == cur;
        /* what slot i shows while one floats */
        int shown = i;
        if (st.mv && st.mv_at != st.mv_from) {
            if (i == st.mv_at) shown = st.mv_from;
            else if (i == st.mv_from) shown = st.mv_swap ? st.mv_at : -1;
        }
        int has = shown >= 0 && ins_slot_bytes(cart(), shown, s);
        if (i >= m0 && i <= m1) g_rect(x, y - 1, 119, 12, C_DEEP + C_MAGENTA);
        if (on) g_box(x, y - 1, 119, 12, st.mv ? C_YELLOW : C_WHITE);
        g_text(x + 2, y, &FONT_T, ui_hex2(i), on ? C_WHITE : C_DGREY);
        if (has && k == REC_FAV) { ui_fav_draw(x + 20, y, (uint16_t)(cart() << 8 | shown), on); continue; }   /* (its source) */
        if (has) rec_slot_name((uint8_t)k, s, nm);
        g_text(x + 20, y, &FONT_T, has ? nm : "----------", has ? (on ? C_WHITE : col) : C_DGREY);
    }
    g_hline(0, BODY_Y + 113, 240, C_DGREY);
    if (k == REC_FAV) fav_details(BODY_Y + 116);
    else details(BODY_Y + 116);
}

/* the state's cursor (specs/connections.md#the-state): the cartridge, the slot */
static void cursor(uint8_t *c) { c[0] = cart(); c[1] = (uint8_t)slot(); }
/* what it edits (specs/ui/screens/editor.md): in edit mode the instrument, played as edited (no `*` commands) */
static void editing(uint8_t *what, uint32_t *key)
{
    if (!st.edit || st.fe) return;
    *what = PL_ED_INST;
    *key = (uint32_t)cart() << 8 | (uint32_t)slot();
}
/* an instrument afloat: PRESETS does nothing (SEL puts it down, HOME takes it back); a favourite's edit mode: the source's
 * cartridge's instruments; else PRESETS as everywhere */
static int presets(int step)
{
    if (st.fe) fav_step(sign(step));
    return st.mv || st.fe;
}
/* SEL + PRESETS in edit mode: the cartridges of the edit's own sort only (a favourite's the FAVORITES cartridges, an
 * instrument's the others), so an edit never goes on in a slot of the other sort */
static int presets_cart(int step)
{
    uint8_t nums[256];
    int n = ui_carts(nums, 256), i = 0, k = 0;
    if (!st.edit) return 0;
    for (int j = 0; j < n; j++)
        if ((ui_cart_kind(nums[j]) == ENG_FAV) == (st.fe != 0)) { if (nums[j] == cart()) i = k; nums[k++] = nums[j]; }
    if (k) { uint8_t c = nums[ui_wrap(i + sign(step), k)]; set_inst(c, ui_cart_first(c)); }
    return 1;
}

const ui_screen_t SCR_INSTRUMENTS = {
    .name = TX("INSTRUMENTS"), .group = G_MAKE,
    .does = BIT(B_EDIT) | BIT(B_SEL) | BIT(B_ENV) | BIT(B_GLO) | BIT(B_ARP) | BIT(B_LFO),
    .combines = BIT(B_ARP),
    .enter = enter, .leave = leave, .draw = draw, .sub = sub, .page = page, .seq = seq, .footer = footer,
    .knob = knob, .mark = mark, .tap = tap, .back = back, .lit = lit, .sel_lines = sel_lines, .tick = tick,
    .cursor = cursor, .presets = presets, .presets_cart = presets_cart, .editing = editing,
};

#ifdef PTFM_EDITOR
/* ---------------------------------------------------------------- the editor page's pictures --- */
/* The editor page's WebAssembly draws this screen's pictures of an instrument from its bytes (editor/wasm/ed_pics.c
 * keeps the interface): this screen's own drawing at its own places, clipped to the picture's rectangle, so the pixels
 * there are the FM-1's. Called once a band (g_band 0, then 1), fresh on the first: what the screen keeps of a sample's
 * waveform and envelope is made again (the page's flash image may hold other audio at the same numbers). What it sets
 * to draw (the current instrument and octave, edit mode's page and group, the samples' list) is put back after, so the
 * screen goes on as it was.
 *   what: SP_DETAILS the details under the list (browsing); SP_EDIT edit mode's picture at (page, group), the
 *   ALGORITHM page and the SELECT group (a DX7's operator, a CHIP page's, its TABLES row); SP_CHIP_ROW a CHIP TABLES
 *   row's table at the row's right (group: the row); SP_WAVE the waveform part of a sample instrument's edit picture;
 *   SP_SAMPLES the samples' list's waveform of the instrument's sample (ENV on SAMPLE).
 *   k its kind (REC_*), bytes its slot (rec_slot_size(k) bytes); inst the cartridge << 8 | slot they stand for: a
 *   sample instrument's samples and a FUSION's pattern are the store's (the page's flash image, mounted), as are the
 *   voices playing (a sample's yellow marks). rect: x, y, w, h. 0 drawn, -1 no such picture. */
enum { SP_DETAILS, SP_EDIT, SP_CHIP_ROW, SP_WAVE, SP_SAMPLES };
int scr_instr_picture(int what, int k, const uint8_t *bytes, int inst, int pg, int grp, int octave, int fresh,
                      int32_t *rect)
{
    if (k < REC_DX7 || k >= REC_FAV || what < SP_DETAILS || what > SP_SAMPLES || !bytes) return -1;   /* (FAVORITES: no
                                                         * picture of its own) */
    if (what == SP_CHIP_ROW && k != REC_CHIP) return -1;
    if ((what == SP_WAVE || what == SP_SAMPLES) && k != REC_SAMPLER) return -1;
    uint8_t st0[sizeof st], sm0[sizeof sm];
    uint16_t inst0 = ui_cur.inst, place0 = ui_cur.place;
    int8_t oct0 = ui_cur.octave;
    memcpy(st0, &st, sizeof st);
    memcpy(sm0, &sm, sizeof sm);
    pic.slot = bytes;
    pic.kind = k;
    ui_cur.inst = ui_cur.place = (uint16_t)inst;
    ui_cur.octave = (int8_t)ui_clamp(octave, -4, 4);
    memset(&st, 0, sizeof st);
    st.last = st.mark = -1;
    st.edit = what != SP_DETAILS;
    st.page = what == SP_CHIP_ROW ? 2 : what == SP_WAVE || what == SP_SAMPLES ? 0 : pg;     /* (TABLES; SAMPLE) */
    st.group = what == SP_WAVE || what == SP_SAMPLES ? 0 : grp;
    tick();                                             /* (page and group within the instrument's, as the screen keeps them) */
    if (fresh) { wv.num = -1; tr_have = 0; }
    int32_t r[4] = {0, 0, 0, 0};
    int ok = 1;
    if (what == SP_DETAILS) {
        r[1] = BODY_Y + 116; r[2] = 240; r[3] = 226 - r[1];     /* (as draw() puts them) */
    } else if (what == SP_EDIT) {
        r[1] = k == REC_SAMPLER ? BODY_Y + 83 : BODY_Y;          /* (as draw_edit() puts it: above the parameters, or a
                                                                  * sample instrument's below them, to the body's end) */
        r[2] = 240; r[3] = k == REC_SAMPLER ? BODY_Y + BODY_H - r[1] : 100;
    } else if (what == SP_CHIP_ROW) {
        ins_lay_t lay;
        int ng;
        groups(&ng);
        int top = st.group - 3 < ng - 6 ? st.group - 3 : ng - 6;   /* (draw_edit()'s rows) */
        if (top < 0) top = 0;
        ok = st.group == grp && ins_layout(group(), &lay) && lay.row;
        r[0] = 172; r[1] = BODY_Y + 100 + 30 + (st.group - top) * 12; r[2] = 66; r[3] = 10;
    } else if (what == SP_WAVE) {
        int hw = 213 - 2 - (BODY_Y + 83) - TRACE_H - 4;          /* (samp_picture()'s waveform) */
        r[1] = BODY_Y + 83; r[2] = 240; r[3] = hw - (hw & 1);
    } else {
        rec_cart_t c;
        rec_sinst_t si;
        ok = !rec_cart(cart(), &c) && c.kind == REC_SAMPLER;
        if (ok) {                                       /* (sample_list()'s cursor, sm_draw()'s rows) */
            rec_sampler_inst(bytes, &si);
            sm.mode = SM_INST;
            sm_fill(si.sample);
            int top = sm.sel - 6 < sr.n - 14 ? sm.sel - 6 : sr.n - 14;
            if (top < 0) top = 0;
            r[0] = 176; r[1] = BODY_Y + 2 + 18 + (sm.sel - top) * 13; r[2] = 56; r[3] = 10;
        }
    }
    if (ok) {
        g_clip(r[0], r[1], r[2], r[3]);
        if (what == SP_DETAILS) details(r[1]);
        else if (what == SP_SAMPLES) sm_draw(0);
        else draw_edit();
        g_noclip();
    }
    memcpy(&st, st0, sizeof st);
    memcpy(&sm, sm0, sizeof sm);
    ui_cur.inst = inst0;
    ui_cur.place = place0;
    ui_cur.octave = oct0;
    pic.slot = 0;
    if (!ok) return -1;
    memcpy(rect, r, sizeof r);
    return 0;
}
#endif
