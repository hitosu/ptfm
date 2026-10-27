/* SPDX-License-Identifier: GPL-3.0-only
 * Copyright (C) 2026 Vasily Kazantsev */
/* Editing a pattern (ed_pat.h). A track's stream (specs/files/patterns.md): a head byte a cell, then its
 * fields in the head's order: the note, the instrument, the volume, the commands, and last the byte of
 * rows skipped when the head's bits 2-0 are 7; 0x00 ends it. The instrument is written in full at its
 * first appearance on the track in a record, after that as the same (0 B) or the same cartridge (1 B).
 * An edit puts each track it changes as a piece of the edit memory (specs/files/ram.md#editing-and-saving), and
 * the pattern's head when its size changes; SAVE lays the tracks into parts, as many to a part as fit 4,080 B. */
#include <string.h>
#include "ui_core.h"
#include "ed_pat.h"
#include "../store/store.h"

#define PART_MAX ((int)(STORE_MAX_REC - STORE_HDR))     /* a record's payload: 4,080 B */

/* ------------------------------------------------------------------------- encoding a stream --- */
typedef struct { int prev, last; } enc_t;
static int empty(const rec_cell_t *c) { return !(c->has & (RC_NOTE | RC_INS | RC_VOL)) && !((c->has & RC_CMD) && c->n); }
/* the cell on row r after the stream's cells so far into out at `at`: the new end, or -1 past max */
static int enc_cell(enc_t *e, int r, const rec_cell_t *c, uint8_t *out, int at, int max)
{
    if (empty(c)) return at;
    int skip = r - e->prev - 1, nb = 0, has_cmd = (c->has & RC_CMD) && c->n;
    uint8_t head = (uint8_t)(skip < 7 ? skip : 7), body[16];
    e->prev = r;
    if (c->has & RC_NOTE) { head |= 0x80; body[nb++] = c->note; }
    if (c->has & RC_INS) {
        int ins = c->cart << 8 | c->slot;
        if (e->last == ins) head |= 0x20;
        else if (e->last >= 0 && e->last >> 8 == ins >> 8) { head |= 0x40; body[nb++] = c->slot; }
        else { head |= 0x60; body[nb++] = c->cart; body[nb++] = c->slot; }
        e->last = ins;
    }
    if (c->has & RC_VOL) { head |= 0x10; body[nb++] = c->vol > 0x7F ? 0x7F : c->vol; }
    if (has_cmd) {
        int n = c->n > 4 ? 4 : c->n;
        head |= 0x08;
        if (n == 1) { body[nb++] = c->cmd[0][0]; body[nb++] = c->cmd[0][1]; }
        else {
            body[nb++] = 0xFF;
            body[nb++] = (uint8_t)n;
            for (int k = 0; k < n; k++) { body[nb++] = c->cmd[k][0]; body[nb++] = c->cmd[k][1]; }
        }
    }
    if (skip >= 7) body[nb++] = (uint8_t)skip;
    if (at + 1 + nb > max) return -1;
    out[at++] = head;
    memcpy(out + at, body, (size_t)nb);
    return at + nb;
}
/* a stream's length with its 0x00, read cell by cell */
static int stream_len(const uint8_t *s)
{
    int at = 0;
    for (;;) {
        uint8_t h = s[at++];
        if (!h) return at;
        if (h & 0x80) at++;
        at += (h >> 5 & 3) == 2 ? 1 : (h >> 5 & 3) == 3 ? 2 : 0;
        if (h & 0x10) at++;
        if (h & 0x08) at += s[at] == 0xFF ? 2 + 2 * s[at + 1] : 2;
        if ((h & 7) == 7) at++;
        if (at > PART_MAX) return -1;
    }
}

/* ------------------------------------------------------------------------------- the rewrite --- */
static rec_pat_t old;
static rec_cur_t old_cur[16];
int ed_old(int t, int r, rec_cell_t *c)
{
    memset(c, 0, sizeof *c);
    if (t < 0 || t >= 16 || t >= old.ntrk || !old.trk[t] || r < 0 || r >= old.rows) return 0;
    return rec_cur_at(&old_cur[t], r, c);
}
static struct { int t0, t1, nt, nr; ed_fn fn; void *a; } rw;
/* track t's new stream into out at `at` (its end), -1 past max */
static int track_out(int t, uint8_t *out, int at, int max)
{
    int keep = !(t >= rw.t0 && t <= rw.t1 && rw.fn) && rw.nr >= old.rows;
    if (keep) {                                         /* as it is */
        if (t >= old.ntrk || !old.trk[t]) { if (at + 1 > max) return -1; out[at] = 0; return at + 1; }
        int n = stream_len(old.trk[t]);
        if (n < 0 || at + n > max) return -1;
        memcpy(out + at, old.trk[t], (size_t)n);
        return at + n;
    }
    enc_t e = {-1, -1};
    rec_cur_t rc;
    rec_cur_set(&rc, t < old.ntrk ? old.trk[t] : 0);
    for (int r = 0; r < rw.nr; r++) {
        rec_cell_t c;
        if (!(r < old.rows && rec_cur_at(&rc, r, &c))) memset(&c, 0, sizeof c);
        if (!(c.has & RC_CMD)) c.n = 0;
        if (t >= rw.t0 && t <= rw.t1 && rw.fn) rw.fn(t, r, &c, rw.a);
        at = enc_cell(&e, r, &c, out, at, max - 1);
        if (at < 0) return -1;
    }
    out[at++] = 0;
    return at;
}
static void old_read(uint8_t pat)       /* the pattern as it stands, read for the rewrite (ed_old's too) */
{
    if (rec_pattern(pat, &old)) old.ntrk = 0;
    for (int t = 0; t < 16; t++) rec_cur_set_end(&old_cur[t], t < old.ntrk ? old.trk[t] : 0, t < old.ntrk ? old.trk_end[t] : 0);
}
int ed_rewrite(uint8_t pat, int t0, int t1, ed_fn fn, void *a, int ntrk, int rows, int step)
{
    if (rec_pattern(pat, &old)) return -1;
    old_read(pat);
    rw.t0 = t0; rw.t1 = t1; rw.fn = fn; rw.a = a;
    rw.nt = ntrk < 0 ? old.ntrk : ui_clamp(ntrk, 1, 16);
    rw.nr = rows < 0 ? old.rows : ui_clamp(rows, 1, PAT_ROWS_MAX);
    int ns = step < 0 ? old.step : ui_clamp(step, 0, 16), nt_old = old.ntrk;
    /* the tracks that change: those rewritten, all of them when rows go, those the size brings in (empty) */
    uint32_t todo = 0, need = 12, n_todo = 0;
    for (int t = 0; t < 16; t++) {
        int mine = t >= rw.t0 && t <= rw.t1 && rw.fn, cut = rw.nr < old.rows && t < old.ntrk && old.trk[t];
        if (t < rw.nt && (mine || cut || t >= nt_old)) todo |= 1u << t;
    }
    for (int t = 0; t < 16; t++) {                      /* each one's new length first (built, then dropped) */
        if (!(todo >> t & 1u)) continue;
        int n = track_out(t, ui_scratch, 0, PART_MAX - 5);
        if (n < 0) { ui_toast(T("TOO BIG FOR ONE RECORD")); return -1; }
        need += (((uint32_t)n + 3u) & ~3u) + 8u;
        n_todo++;
    }
    if (ui_edit_reserve(need)) return -1;               /* all of it or nothing */
    old_read(pat);                                      /* (made room: pieces may have moved) */
    /* then each, built in the scratch and put as its track's piece; with several, the old ones stay where they lay
     * while the others are made from them (a block moved between tracks) */
    int ok = 0;
    if (n_todo > 1) store_edit_hold(1);
    for (int t = 0; t < 16 && !ok; t++) {
        if (!(todo >> t & 1u)) continue;
        int n = track_out(t, ui_scratch, 0, PART_MAX - 5);
        if (n < 0 || ui_edit_piece(REC_PATTERN, pat, (uint16_t)t, 1, ui_scratch, (uint32_t)n)) ok = -1;
    }
    store_edit_hold(0);
    if (!ok && (rw.nt != old.ntrk || rw.nr != old.rows || ns != old.step)) {   /* the size: its head */
        uint8_t h[3] = {(uint8_t)rw.nt, (uint8_t)(rw.nr - 1), (uint8_t)ns};
        if (ui_edit_piece(REC_PATTERN, pat, STORE_PC_HEAD, 1, h, 3)) ok = -1;
    }
    return ok;
}

/* ------------------------------------------------------------------------------ one cell --- */
typedef struct { int row; const rec_cell_t *c; } put_t;
static void put_fn(int t, int r, rec_cell_t *c, void *a)
{
    (void)t;
    const put_t *p = a;
    if (r != p->row) return;
    if (p->c) *c = *p->c; else memset(c, 0, sizeof *c);
}
int ed_put_cell(uint8_t pat, int row, int track, const rec_cell_t *c)
{
    put_t p = {row, c};
    return ed_rewrite(pat, track, track, put_fn, &p, -1, -1, -1);
}
int ed_cell(uint8_t pat, int row, int track, rec_cell_t *c)
{
    rec_pat_t p;
    rec_cur_t cur;
    memset(c, 0, sizeof *c);
    if (rec_pattern(pat, &p) || track < 0 || track >= 16 || track >= p.ntrk) return 0;
    rec_cur_set(&cur, p.trk[track]);
    return rec_cur_at(&cur, row, c);
}
int ed_inst_at(uint8_t pat, int row, int track)
{
    rec_pat_t p;
    rec_cur_t cur;
    rec_cell_t c;
    if (rec_pattern(pat, &p) || track < 0 || track >= 16) return -1;
    rec_cur_set(&cur, p.trk[track]);
    if (rec_cur_at(&cur, row, &c) && (c.has & RC_INS)) return c.cart << 8 | c.slot;
    return cur.ins ? cur.cart << 8 | cur.slot : -1;
}
int ed_drops(uint8_t pat, int ntrk, int rows)
{
    rec_pat_t p;
    int k = 0;
    if (rec_pattern(pat, &p)) return 0;
    for (int t = 0; t < p.ntrk; t++) {
        if (!p.trk[t]) continue;
        rec_cur_t cur;
        rec_cell_t c;
        rec_cur_set(&cur, p.trk[t]);
        for (int r = 0; r < p.rows; r++) if (rec_cur_at(&cur, r, &c) && (r >= rows || t >= ntrk)) k++;
    }
    return k;
}
int ed_builtin(uint8_t pat)             /* 1 a built-in one as the firmware has it (brown), 0 the user area's or edited */
{
    store_rec_t r;
    if (!store_info(REC_PATTERN, pat, 0, &r)) return pat >= 0xF0;
    return r.area == STORE_AREA_BUILTIN;
}

/* ------------------------------------------------------------------------- whole patterns --- */
int ed_new_pattern(uint8_t pat, int ntrk, int rows, int step)
{
    uint8_t p[5] = {(uint8_t)ntrk, (uint8_t)(rows - 1), (uint8_t)step, 0, 0};
    return ui_edit_put(REC_PATTERN, pat, 0, 1, p, 5);
}
static int kept_len(const rec_pat_t *p, int t)  /* track t's stream as a copy keeps it (its first 128 rows: an older
                                                 * record may have more), with its 0x00; -1 it does not read */
{
    if (t >= p->ntrk || !p->trk[t]) return 1;
    int n = stream_len(p->trk[t]);
    return n < 1 ? -1 : (int)store_track_kept(p->trk[t], (uint32_t)n, PAT_ROWS_MAX);
}
int ed_copy_pattern(uint8_t from, uint8_t to)
{
    rec_pat_t p;
    uint32_t need = 12;
    int ok = 0;
    if (rec_pattern(from, &p)) return -1;
    for (int t = 0; t < 16; t++) need += (uint32_t)((kept_len(&p, t) + 3) & ~3) + 8u;
    if (ui_edit_reserve(need) || rec_pattern(from, &p)) return -1;    /* (made room: its pieces may have moved) */
    uint8_t h[3] = {p.ntrk, (uint8_t)(p.rows - 1), p.step};
    if (ui_edit_piece(REC_PATTERN, to, STORE_PC_HEAD, 1, h, 3)) return -1;
    for (int t = 0; t < 16 && !ok; t++) {               /* every track, an empty one too (what was there goes) */
        int n = kept_len(&p, t);
        if (n < 1) return -1;
        if (n > 1) memcpy(ui_scratch, p.trk[t], (size_t)n - 1);
        ui_scratch[n - 1] = 0;
        if (ui_edit_piece(REC_PATTERN, to, (uint16_t)t, 1, ui_scratch, (uint32_t)n)) ok = -1;
    }
    return ok;
}
int ed_delete_pattern(uint8_t pat) { return ui_edit_del(REC_PATTERN, pat, 0); }
int ed_first_free(void)
{
    uint8_t nums[256];
    int n = store_list(REC_PATTERN, nums, 256), k = 0;
    for (int i = 0; i <= 0xEF; i++) {
        while (k < n && nums[k] < i) k++;
        if (k >= n || nums[k] != i) return i;
    }
    return -1;
}

/* ---------------------------------------------------------------------------- the clipboard --- */
static uint8_t clip[ED_CLIP];
static rec_cur_t clip_cur[16];
static int clip_rows, clip_tracks;
int ed_clip_copy(uint8_t pat, int r0, int r1, int t0, int t1)
{
    rec_pat_t p;
    if (rec_pattern(pat, &p)) return -1;
    int rows = r1 - r0 + 1, tracks = t1 - t0 + 1, at = 0;
    const uint8_t *start[16];
    clip_rows = 0;
    for (int k = 0; k < tracks && k < 16; k++) {
        int t = t0 + k;
        enc_t e = {-1, -1};
        rec_cur_t rc;
        rec_cur_set(&rc, t < p.ntrk ? p.trk[t] : 0);
        start[k] = clip + at;
        for (int r = 0; r < rows; r++) {
            rec_cell_t c;
            if (!rec_cur_at(&rc, r0 + r, &c)) continue;
            if (!(c.has & RC_CMD)) c.n = 0;
            at = enc_cell(&e, r, &c, clip, at, ED_CLIP - 1);
            if (at < 0) return -1;
        }
        if (at >= ED_CLIP) return -1;                   /* (an empty track after the clipboard filled: no room for its 0x00) */
        clip[at++] = 0;
    }
    for (int k = 0; k < tracks && k < 16; k++) rec_cur_set(&clip_cur[k], start[k]);
    clip_rows = rows;
    clip_tracks = tracks;
    return 0;
}
int ed_clip_rows(void) { return clip_rows; }
int ed_clip_tracks(void) { return clip_tracks; }
int ed_clip_cell(int r, int t, rec_cell_t *c)
{
    memset(c, 0, sizeof *c);
    if (!clip_rows || r < 0 || r >= clip_rows || t < 0 || t >= clip_tracks) return 0;
    return rec_cur_at(&clip_cur[t], r, c);
}
