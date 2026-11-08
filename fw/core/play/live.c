/* SPDX-License-Identifier: GPL-3.0-only
 * Copyright (C) 2026 Vasily Kazantsev */
/* RECORD LIVE's song (live.h; its records: specs/files/songs.md, patterns.md, commands.md). */
#include <string.h>
#include "live.h"
#include "../plat.h"
#include "../spare.h"
#include "loops.h"
#include "player.h"
#include "rec.h"
#include "../store/store.h"

/* The log, the pattern being written, in rows' order (row: within it, past 127 the next's), their order kept within a row,
 * 8 B an entry: k its kind << 4 | its track. LG_NOTE: a the note, b its volume (0xFF none), c d the instrument, e its 3xx's
 * xx, f its tick (EDx) | PTF_ACC, SLD, LEG << 5. LG_OFF: a its tick. LG_MIX: a mixer command's code, b its value. LG_CMD
 * a global command (a its code, b its value, c placed in a cell so far), LG_PUNCH the same for a punch-in's still running
 * (b set to the rows it ran as it ends). A full log drops what comes: the song goes on, a note dropped counted as left out
 * (a note off, a mix or a command dropped counts nothing). Lent from the spare zone with the FX setup as last seen */
enum { LG_NOTE, LG_OFF, LG_MIX, LG_CMD, LG_PUNCH };
typedef struct { uint8_t row, k, a, b, c, d, e, f; } __attribute__((aligned(4))) lg_t;   /* (the borrow is aligned: copied by words) */
#ifndef LIVE_LOG_BYTES
#define LIVE_LOG_BYTES 16000u           /* (a test may build it smaller: -DLIVE_LOG_BYTES=) */
#endif
#ifndef LIVE_RING_N
#define LIVE_RING_N 64u                 /* (the same: -DLIVE_RING_N=, a power of 2) */
#endif
#define LOG_BYTES LIVE_LOG_BYTES
#define LOG_N (LOG_BYTES / sizeof(lg_t))
#define LENT (LOG_BYTES + 64u)
#define RING_N LIVE_RING_N              /* the tap's ring: what a slow main loop leaves waiting */
#define K_OF(g) ((g)->k >> 4)
#define T_OF(g) ((g)->k & 15)
#define TRACK_MAX (STORE_MAX_REC - STORE_HDR - 5u)     /* a pattern track's stream at most: a part's payload, its head */
#define NO_OWNER 0xFFu
static const uint8_t MIX_DEF[RM_N] = {0x7F, 0x80, 0, 0, 0, 0};
#define DUCK_SRC (16 + 0xAA - 0x90)     /* the FX setup's DUCK SOURCE and FILTER's envelope's SOURCE (songs.md) */
#define ENV_SRC (16 + 0xB1 - 0x90)

/* a track of the song: on, holding a note; lane: the lane it played last, key: a key's own number on the keys' lanes; owner:
 * the source of its mix (a replay's channel, PT_KEYCH the keys'; NO_OWNER none yet); its last note's row and tick; have: its
 * mix as written so far */
typedef struct {
    uint8_t on, lane, key, owner, ntick;
    uint16_t nrow;
    uint8_t have[RM_N];
} tr_t;
static pl_tap_t ring[RING_N];
static struct {
    uint8_t on, known, song, made, ticks, rpb, swing, landed;
    uint16_t done, left, tempo, tnow, n;
    uint32_t h0, t0, land[4];
    uint8_t name[12], km[RM_N];
    uint8_t mix0[16][RM_N];             /* each track's mix at the song's start */
    uint8_t fx0[64];                    /* the loops' setup at the start: the song's */
    tr_t tr[16];
    int8_t pk;                          /* the punch-in running (-1 none) ... */
    uint16_t pabs;                      /* ... since that row of the song ... */
    uint8_t pw, pwrow, pwtrk;           /* ... its command written there (pw the pattern, 0xFF none) */
    lg_t *log;                          /* (lent; with the FX setup as last seen after it) */
} lv;                                   /* (set by live_arm and live_begin: nothing of it read before) */
#define FX_SEEN ((uint8_t *)lv.log + LOG_BYTES)

static int clamp(int v, int lo, int hi) { return v < lo ? lo : v > hi ? hi : v; }
static uint32_t a4(uint32_t n) { return (n + 3u) & ~3u; }
static int cmd_val(const rec_cell_t *c, int code)
{
    for (int k = 0; k < c->n; k++) if (c->cmd[k][0] == code) return c->cmd[k][1];
    return -1;
}
/* a source's mix: channel c's (a loop's, the next loop's past them), PT_KEYCH the keys' on another screen */
static const uint8_t *mix_of(int c) { return c == PT_KEYCH ? lv.km : loops_mix(c); }
/* DUCK's and FILTER's envelope's SOURCE name a loop in LOOPS (its track there): in the song, the first track its notes
 * took */
static int src_trk(int i)
{
    for (int t = 0; t < 16; t++) if (lv.tr[t].owner == i) return t;
    return i;
}
static int src_of(int code, int v)
{
    if (code == 0xAA) return v < 16 ? src_trk(v) : v;
    if (code == 0xB1) return v >= 1 && v <= 16 ? 1 + src_trk(v - 1) : v;
    return v;
}

/* ------------------------------------------------------------------------ the log --- */
static void lost(void) { if (lv.left < 0xFFFFu) lv.left++; }     /* a note left out (the count stops at its top) */
/* a place in the log for an entry at the row, 0 when there is none (full, or the row past a pattern's 255 a note off at
 * 255 would step over): nothing counted here, the caller counts a note */
static lg_t *log_put(int row, int kind, int t)
{
    if (lv.n >= LOG_N || !lv.log || row > 255) return 0;
    int k = lv.n++;
    while (k && lv.log[k - 1].row > row) { lv.log[k] = lv.log[k - 1]; k--; }
    lg_t *g = &lv.log[k];
    memset(g, 0, sizeof *g);
    g->row = (uint8_t)row;
    g->k = (uint8_t)(kind << 4 | t);
    return g;
}
/* a command's key: the last at a row wins (an effect's ON and OFF one; any punch-in one) */
static int cmd_key(int code, int v) { return code == 0xAE || code == 0xAF ? 0x100 | v : code >= 0xC3 && code <= 0xD2 ? 0x200 : code; }
static void log_cmd(int row, int kind, int code, int v)
{
    lg_t *g = 0;
    for (int x = lv.n - 1; x >= 0 && lv.log[x].row >= row && !g; x--)
        if (lv.log[x].row == row && lv.log[x].k >= LG_CMD << 4 && cmd_key(lv.log[x].a, lv.log[x].b) == cmd_key(code, v))
            g = &lv.log[x];
    if (!g && !(g = log_put(row, kind, 0))) return;
    g->k = (uint8_t)(kind << 4);
    g->a = (uint8_t)code;
    g->b = (uint8_t)v;
}
/* the commands track t's cell at the row still has room for: a key off kept room for after a note */
static int cell_room(int t, int row)
{
    int n = 0, note = 0, off = 0;
    for (int x = lv.n - 1; x >= 0 && lv.log[x].row >= row; x--) {
        const lg_t *g = &lv.log[x];
        if (g->row != row || K_OF(g) > LG_MIX || T_OF(g) != t) continue;
        if (K_OF(g) == LG_NOTE) { note = 1; n += ((g->f & 31) != 0) + (g->e || (g->f >> 5 & PTF_LEG)) + ((g->f >> 5 & 3) != 0); }
        else { off |= K_OF(g) == LG_OFF; n++; }
    }
    return 4 - n - (note && !off);
}
/* track t's mix brought up to its source's at the row, as far as the cell has room (mixer commands in LP_MIX_KEY's order);
 * the rest at a row after (changes) */
static void mix_sync(int t, int row)
{
    tr_t *x = &lv.tr[t];
    const uint8_t *mix = mix_of(x->owner);
    for (int k = 0; k < RM_N; k++) {
        int key = LP_MIX_KEY[k], y;
        if (x->have[key] == mix[key]) continue;
        for (y = lv.n - 1; y >= 0 && lv.log[y].row >= row; y--)
            if (lv.log[y].row == row && K_OF(&lv.log[y]) == LG_MIX && T_OF(&lv.log[y]) == t && lv.log[y].a == LP_MIX_CODE[k]) break;
        if (y >= 0 && lv.log[y].row == row) lv.log[y].b = mix[key];
        else {
            lg_t *g = cell_room(t, row) > 0 ? log_put(row, LG_MIX, t) : 0;
            if (!g) continue;
            g->a = LP_MIX_CODE[k];
            g->b = mix[key];
        }
        x->have[key] = mix[key];
    }
}

/* ------------------------------------------------------- the tap's events on the rows --- */
static int held(const pl_tap_t *e)      /* the track holding the event's lane's note (a key's, by its key), -1 none */
{
    for (int t = 0; t < 16; t++)
        if (lv.tr[t].on && lv.tr[t].lane == e->lane && (e->lane < PT_KEY || lv.tr[t].key == e->glide)) return t;
    return -1;
}
/* the track's note ends at the row, tick: a tick after it starts at least */
static void note_end(int t, int row, int tick)
{
    tr_t *x = &lv.tr[t];
    lg_t *g;
    x->on = 0;
    if (x->nrow == lv.done * LIVE_ROWS + row && tick <= x->ntick) {
        tick = x->ntick + 1;
        if (tick >= lv.ticks) { row++; tick = 0; }
    }
    if ((g = log_put(row, LG_OFF, t)) != 0) g->a = (uint8_t)tick;
}
/* a track for the note at the song's row abs (a track takes one note a row): free, the lane's first, then any with its
 * source's mix, then one unused, then the one whose mix differs least; -1 none */
static int track_for(const pl_tap_t *e, int abs)
{
    const uint8_t *mix = mix_of(e->ch);
    int best = -1, bn = 99;
    for (int t = 0; t < 16; t++) {
        const tr_t *x = &lv.tr[t];
        int n = 0;
        if (x->on || x->nrow == abs) continue;
        if (x->owner == e->ch) n = x->lane == e->lane ? -3 : -2;
        else if (x->owner == NO_OWNER) n = -1;
        else for (int k = 0; k < RM_N; k++) n += x->have[k] != mix[k];
        if (n < bn) { bn = n; best = t; }   /* (the first of the best) */
    }
    return best;
}
/* a note as it sounds: on the track holding its lane's note (ending that one there) when its mix is the note's and the
 * track has no note at the row, else on a track of its own (a glide, a legato or a slide kept only on the same track);
 * none free: left out, counted */
static void note_at(const pl_tap_t *e, int row, int tick)
{
    int abs = lv.done * LIVE_ROWS + row, t = held(e), joined = 0;
    if (t >= 0) {
        if (lv.tr[t].owner == e->ch && lv.tr[t].nrow != abs) joined = 1;
        else { note_end(t, row, tick); t = -1; }
    }
    if (t < 0 && (t = track_for(e, abs)) < 0) { lost(); return; }
    tr_t *x = &lv.tr[t];
    if (x->owner == NO_OWNER && !lv.done) {             /* (its first pattern: the song's mixer has it from the start) */
        memcpy(x->have, mix_of(e->ch), RM_N);
        memcpy(lv.mix0[t], x->have, RM_N);
    }
    if ((e->fl & PTF_SLID) && joined)                   /* (a TB-303's slide: SLD on the note it comes from, if not written) */
        for (int y = lv.n - 1; y >= 0; y--)
            if (lv.log[y].k == (LG_NOTE << 4 | t)) { lv.log[y].f |= PTF_SLD << 5; break; }
    x->on = 1;
    x->lane = e->lane;
    x->key = e->lane < PT_KEY ? 0 : e->glide;
    x->owner = e->ch;
    x->nrow = (uint16_t)abs;
    x->ntick = (uint8_t)(tick > 15 ? 15 : tick);
    lg_t *g = log_put(row, LG_NOTE, t);
    if (!g) { lost(); return; }
    g->a = e->note;
    g->b = e->vol;
    g->c = e->cart;
    g->d = e->slot;
    g->e = joined && e->lane < PT_KEY ? e->glide : 0;
    g->f = (uint8_t)(x->ntick | (e->fl & (PTF_ACC | PTF_SLD | (joined ? PTF_LEG : 0))) << 5);
    mix_sync(t, row);
}
static void tap_event(const pl_tap_t *e, uint32_t now)
{
    uint32_t tk = now + (uint32_t)(int32_t)(int16_t)(e->t - (uint16_t)now);   /* (the ring's 16 bits, around the tick now) */
    if (e->note == PT_MOVE) {                         /* (lanes going on as others: the notes on them with them) */
        for (int t = 0; t < 16; t++)
            if (lv.tr[t].on && lv.tr[t].lane == e->lane) { lv.tr[t].lane = e->vol; lv.tr[t].owner = e->ch; }
        return;
    }
    if (e->lane >= PT_SKEYS && e->lane < PT_SKEYS + 4u && (lv.landed >> (e->lane - PT_SKEYS) & 1)) {
        uint32_t at = lv.land[e->lane - PT_SKEYS];      /* (as it landed; one left over from a note gone is stale) */
        lv.landed &= (uint8_t)~(1u << (e->lane - PT_SKEYS));
        if (tk - at <= 3u * lv.ticks) tk = at;
    }
    if ((int32_t)(tk - lv.t0) < 0) return;              /* (before its row 0) */
    uint32_t d = tk - lv.t0;
    int row = (int)(d / lv.ticks) - lv.done * LIVE_ROWS, tick = (int)(d % lv.ticks);
    if (row < 0) { row = 0; tick = 0; }                 /* (a pattern written: its next row) */
    if (row > 255) { if (e->note >= 12) lost(); return; }
    if (e->note == PT_MUTE) {                           /* (the loops silent now: their notes end there) */
        uint32_t mask = (uint32_t)e->cart | (uint32_t)e->slot << 8;
        for (int t = 0; t < 16; t++)
            if (lv.tr[t].on && lv.tr[t].lane < 4u * LP_MAX && (mask >> (lv.tr[t].lane >> 2) & 1u)) note_end(t, row, tick);
        return;
    }
    if (e->note == PT_OFF || e->note == PT_LOST) {
        int t = held(e);
        if (t >= 0) note_end(t, row, tick);
        return;
    }
    note_at(e, row, tick);
}

/* -------------------------------------------------------------------- the changes --- */
/* the song's row heard now, to the nearest row (a change made now is written there), never in a pattern written; 0: not
 * heard (the loops' replay not playing) */
static int heard(int *at)
{
    player_state_t st;
    player_state(&st, 0);
    if (!(st.what == PL_LOOPS && st.playing) || !lv.known) return 0;
    uint32_t d = (int32_t)(st.lp_ticks - lv.t0) < 0 ? 0u : st.lp_ticks - lv.t0;
    int s = (int)(d / lv.ticks) + (2u * (d % lv.ticks) >= lv.ticks);
    *at = s < lv.done * LIVE_ROWS ? lv.done * LIVE_ROWS : s;
    return 1;
}
/* what changed since the last look, logged at the row heard: TEMPO as F00 back to the song's own, else Fxx held within
 * 20-FF; an effect switched as FX ON / OFF; an FX knob with a command as its command (SOURCE as the song has it); the
 * chain, ROUTING and the METRONOME's knobs not; the tracks' mixes brought up to their sources' */
static void changes(void)
{
    int at, now = loops_tempo();
    if (!lv.log || !heard(&at)) return;
    int row = at - lv.done * LIVE_ROWS;
    const uint8_t *f = loops_fx();
    uint8_t *seen = FX_SEEN;
    if (now != lv.tnow) {
        lv.tnow = (uint16_t)now;
        log_cmd(row, LG_CMD, 0x0F, now == lv.tempo ? 0 : clamp(now, 0x20, 0xFF));
    }
    if (memcmp(f, seen, 64)) {
        for (int i = 7; i < 64; i++) {
            if (f[i] == seen[i]) continue;
            if (i == 7 || i == 8) {
                for (int b = 0; b < 8; b++) {
                    int n = (i - 7) * 8 + b;
                    if (n <= 9 && ((f[i] ^ seen[i]) >> b & 1)) log_cmd(row, LG_CMD, f[i] >> b & 1 ? 0xAE : 0xAF, n);
                }
            } else if (i >= 16 && i != 16 + 0xAE - 0x90 && i != 16 + 0xAF - 0x90)
                log_cmd(row, LG_CMD, 0x90 + i - 16, src_of(0x90 + i - 16, f[i]));
        }
        memcpy(seen, f, 64);
    }
    if (lv.n < LOG_N)                   /* (a full log takes no mix: no walk each pass) */
        for (int t = 0; t < 16; t++) if (lv.tr[t].owner != NO_OWNER) mix_sync(t, row);
}

/* ---- the punch-ins ---- */
/* pattern num's track t: its command code at row r given the value v (the rows a punch-in ran) */
static void patch(int num, int t, int r, int code, int v)
{
    rec_pat_t p;
    rec_cur_t cu;
    rec_cell_t c;
    lp_enc_t e;
    uint8_t *b;
    if (rec_pattern((uint8_t)num, &p) || t >= p.ntrk || !(b = store_scratch_claim(1))) return;
    loops_enc_track(&e, b, TRACK_MAX);
    rec_cur_set_end(&cu, p.trk[t], p.trk_end[t]);
    for (int row = 0; row < p.rows; row++) {
        if (!rec_cur_at(&cu, row, &c)) continue;
        for (int k = 0; row == r && k < c.n; k++) if (c.cmd[k][0] == code) c.cmd[k][1] = (uint8_t)v;
        loops_enc_cell(&e, row, &c);
    }
    uint32_t len = loops_enc_end(&e);
    if (len) store_edit_piece(REC_PATTERN, (uint8_t)num, (uint16_t)t, 1, b, len);
    store_scratch_claim(0);
}
PLAT_COLD void live_punch_end(int at)
{
    int v = clamp(at - lv.pabs, 1, 255), x;
    if (lv.pk < 0) return;
    for (x = 0; x < lv.n && lv.log[x].k != LG_PUNCH << 4; x++)
        ;
    if (x < lv.n) {                     /* (its pattern not written yet: its value there) */
        lv.log[x].k = LG_CMD << 4;
        lv.log[x].b = (uint8_t)v;
    } else if (lv.pw != 0xFF) {
        patch(lv.pw, lv.pwtrk, lv.pwrow, 0xC3 + lv.pk, v);
    }
    lv.pk = -1;
    lv.pw = 0xFF;
}
void live_punch(int k)
{
    int at;
    if (!lv.on || !lv.log || k == lv.pk || !heard(&at)) return;
    live_punch_end(at);
    if (k < 0) return;
    log_cmd(at - lv.done * LIVE_ROWS, LG_PUNCH, 0xC3 + k, 1);
    lv.pk = (int8_t)k;
    lv.pabs = (uint16_t)at;
}

/* ------------------------------------------------------------- writing a pattern --- */
/* the commands placed as they go: a sweep over the tracks from 0 */
static void sweep(void) { for (int x = 0; x < lv.n; x++) if (lv.log[x].k >= LG_CMD << 4) lv.log[x].c = 0; }
/* track t of the pattern, rows up to end: the cell of its entries at each row: its note in its instrument, note delay,
 * glide, legato, ACC, SLD; a note's own end (it came after it) a key off at its tick; an end before a note a key off before
 * its delay (the note ends what sounds, at its tick or sooner); alone a note off on the row's first tick, else a key off;
 * its mixer commands; the row's commands where they find room first. Into e; its stream's length; *prow, *ptrk where the
 * punch-in still running went */
static PLAT_COLD uint32_t build(int t, lp_enc_t *e, int end, int *prow, int *ptrk)
{
    int x = 0;
    while (x < lv.n && lv.log[x].row < end) {
        rec_cell_t c;
        int r = lv.log[x].row, q = x, n = -1, delay = 0;
        while (q < lv.n && lv.log[q].row == r) q++;
        memset(&c, 0, sizeof c);
        for (int j = x; j < q && n < 0; j++) {
            const lg_t *g = &lv.log[j];
            if (g->k != (LG_NOTE << 4 | t)) continue;
            int fl = g->f >> 5;
            n = j;
            delay = g->f & 31;
            c.has = RC_NOTE | RC_INS;
            c.note = g->a;
            c.cart = g->c;
            c.slot = g->d;
            if (g->b != 0xFF) { c.has |= RC_VOL; c.vol = g->b; }
            if (delay) loops_cmd_add(&c, 0x0E, 0xD0 | delay);
            if (fl & PTF_LEG) loops_cmd_add(&c, 0xD3, 0);
            else if (g->e) loops_cmd_add(&c, 0x03, g->e);
            if (fl & (PTF_ACC | PTF_SLD)) loops_cmd_add(&c, fl & PTF_ACC ? (fl & PTF_SLD ? 0xC2 : 0xC0) : 0xC1, 0);
        }
        for (int j = x; j < q; j++) {
            const lg_t *g = &lv.log[j];
            if (g->k != (LG_OFF << 4 | t)) continue;
            if (n >= 0 && j > n) loops_cmd_add(&c, 0x14, g->a);
            else if (n >= 0) { if (g->a < delay) loops_cmd_add(&c, 0x14, g->a); }
            else if (g->a) loops_cmd_add(&c, 0x14, g->a);
            else { c.has |= RC_NOTE; c.note = REC_NOTE_OFF; }
        }
        for (int j = x; j < q; j++)
            if (lv.log[j].k == (LG_MIX << 4 | t)) loops_cmd_add(&c, lv.log[j].a, lv.log[j].b);
        for (int j = x; j < q; j++) {
            lg_t *g = &lv.log[j];
            if (g->k < LG_CMD << 4 || g->c) continue;
            if (c.n >= 4 || cmd_val(&c, g->a) >= 0) continue;
            loops_cmd_add(&c, g->a, g->b);
            g->c = 1;
            if (g->k == LG_PUNCH << 4) { *prow = r; *ptrk = t; }
        }
        if (c.has) loops_enc_cell(e, r, &c);
        x = q;
    }
    return loops_enc_end(e);
}
/* the song's record with its pattern num last: made with the first (its name, the loops' setup at the start, its
 * tracks' mixes, plays once and stops), then a position more each pattern */
static uint32_t song_len(void)
{
    uint32_t len;
    if (!lv.made) return 21u + 1u + 1u + RM_N * 16u + 64u + 8u;
    return store_get(REC_SONG, lv.song, 0, &len, 0) ? len + 1u : 0u;
}
static int song_put(int num)
{
    uint8_t *b = store_scratch_claim(1), ver = 1;
    uint32_t len;
    int r;
    if (!b) return STORE_ERR_STATE;
    if (!lv.made) {
        int nt = 16;
        while (nt > 0 && !memcmp(lv.mix0[nt - 1], MIX_DEF, RM_N)) nt--;   /* (the mixer: up to the last that differs) */
        memset(b, 0, 23);
        memcpy(b, lv.name, 12);
        b[12] = (uint8_t)lv.tempo;
        b[13] = (uint8_t)(lv.tempo >> 8);
        b[14] = lv.ticks;
        b[15] = lv.rpb;
        b[16] = lv.swing;
        b[17] = RS_STOP | RS_LINEAR;    /* (AT THE END = STOP; our own songs slide linearly) */
        b[19] = 64;
        b[21] = (uint8_t)num;           /* (one position) */
        b[22] = (uint8_t)nt;
        for (int t = 0; t < nt; t++) memcpy(b + 23 + RM_N * t, lv.mix0[t], RM_N);
        len = 23u + RM_N * (uint32_t)nt;
        memcpy(b + len, lv.fx0, 64);
        b[len + DUCK_SRC] = (uint8_t)src_of(0xAA, b[len + DUCK_SRC]);
        b[len + ENV_SRC] = (uint8_t)src_of(0xB1, b[len + ENV_SRC]);
        memset(b + len + 64, 0, 8);
        len += 72u;
    } else {
        const uint8_t *s = store_get(REC_SONG, lv.song, 0, &len, &ver);
        uint32_t npos = s && len >= 22u ? s[20] + 1u : 0u;
        if (!npos || 21u + npos > len || len + 1u > STORE_SCRATCH_BYTES) { store_scratch_claim(0); return STORE_ERR_STATE; }
        memcpy(b, s, 21u + npos);       /* (copied out first: the edit memory's own bytes) */
        b[21 + npos] = (uint8_t)num;
        memcpy(b + 22 + npos, s + 21 + npos, len - 21u - npos);
        b[20] = (uint8_t)npos;
        len++;
    }
    r = store_edit_put(REC_SONG, lv.song, 0, ver, b, len);
    store_scratch_claim(0);
    return r;
}
PLAT_COLD int live_chunk(int end)
{
    lp_enc_t e;
    uint32_t len[16], need;
    int hi = 0, num, t, r, prow = -1, ptrk = -1;
    uint8_t *b;
    end = clamp(end, 1, LIVE_ROWS);
    sweep();
    for (t = 0; t < 16; t++) {          /* the room first: each track's stream, the commands placed as they will be */
        loops_enc_track(&e, 0, TRACK_MAX);
        len[t] = build(t, &e, end, &prow, &ptrk);   /* (128 cells of 16 B at most: never near TRACK_MAX) */
        if (len[t] > 1u) hi = t + 1;
    }
    if (!hi) hi = 1;
    if ((num = rec_free_pattern()) < 0) return LIVE_PATTERNS;
    need = a4(song_len()) + 8u + 12u;
    for (t = 0; t < hi; t++) need += a4(len[t]) + 8u;
    if (!store_edit_reserve(need) || song_put(num) != STORE_OK) return LIVE_FULL;
    sweep();                            /* (then its tracks written) */
    {
        uint8_t h[3] = {(uint8_t)hi, (uint8_t)(end - 1), 1};
        if (store_edit_piece(REC_PATTERN, (uint8_t)num, STORE_PC_HEAD, 1, h, 3) != STORE_OK) return LIVE_FULL;
    }
    for (t = 0; t < hi; t++) {
        if (!(b = store_scratch_claim(1))) return LIVE_FULL;
        loops_enc_track(&e, b, TRACK_MAX);
        len[t] = build(t, &e, end, &prow, &ptrk);
        r = store_edit_piece(REC_PATTERN, (uint8_t)num, (uint16_t)t, 1, b, len[t]);
        store_scratch_claim(0);
        if (r != STORE_OK) return LIVE_FULL;
    }
    if (prow >= 0) { lv.pw = (uint8_t)num; lv.pwrow = (uint8_t)prow; lv.pwtrk = (uint8_t)ptrk; }
    {                                   /* (the next one's entries become the current's) */
        int k = 0;
        for (int x = 0; x < lv.n; x++)
            if (lv.log[x].row >= end) {
                lv.log[k] = lv.log[x];
                lv.log[k++].row = (uint8_t)(lv.log[x].row - end);
            }
        lv.n = (uint16_t)k;
    }
    lv.done++;
    lv.made = 1;
    return LIVE_OK;
}

/* ------------------------------------------------------------- its start and end --- */
static void lend(void)                  /* the log's memory, when the zone has it free (DJ's may be going back still) */
{
    if (lv.log) return;
    lv.log = spare_borrow(LENT);
    if (lv.log) memcpy(FX_SEEN, lv.on ? lv.fx0 : loops_fx(), 64);
}
void live_arm(void)
{
    memset(&lv, 0, sizeof lv);
    lend();
    player_tap(ring, RING_N);
}
void live_disarm(void)
{
    player_tap(0, 0);
    if (lv.log) spare_return();
    lv.log = 0;
    lv.on = 0;
}
PLAT_COLD void live_begin(uint8_t song, const uint8_t *name, const uint8_t *keys_mix, uint32_t halves, uint32_t t0, int known, int punch)
{
    lv.song = song;
    lv.h0 = halves;
    lv.t0 = t0;
    lv.known = (uint8_t)(known != 0);
    memcpy(lv.km, keys_mix, RM_N);
    memcpy(lv.name, name, 12);
    lv.tempo = lv.tnow = (uint16_t)loops_tempo();
    lv.ticks = (uint8_t)loops_ticks();
    lv.rpb = (uint8_t)loops_rpb();
    lv.swing = (uint8_t)loops_swing();
    memcpy(lv.fx0, loops_fx(), 64);
    if (lv.log) memcpy(FX_SEEN, lv.fx0, 64);
    for (int t = 0; t < 16; t++) {
        lv.tr[t].owner = NO_OWNER;
        lv.tr[t].lane = 0xFF;
        lv.tr[t].nrow = 0xFFFF;
        memcpy(lv.tr[t].have, MIX_DEF, RM_N);
        memcpy(lv.mix0[t], MIX_DEF, RM_N);
    }
    lv.pk = (int8_t)punch;              /* (a punch-in running from before: not written) */
    lv.pw = 0xFF;
    lv.on = 1;
}
void live_poll(void)
{
    pl_tap_t e;
    player_state_t st;
    player_state(&st, 0);
    if (!lv.log) lend();
    if (!lv.on) {                       /* (armed: only what the row playing has started kept, and never more than half the
                                         * ring, the oldest going: keys played with the loops stopped fill no ring) */
        uint16_t from = (uint16_t)(st.lp_ticks - st.tick);
        while (player_tap_peek(&e) && ((int16_t)(e.t - from) < 0 || player_tap_held() > RING_N / 2u)) player_tap_next();
        return;
    }
    if (!lv.log || !lv.known) return;
    while (player_tap_peek(&e)) {
        player_tap_next();
        tap_event(&e, st.lp_ticks);
    }
    changes();
}
void live_land(int lane, uint32_t tick)
{
    lv.land[lane & 3] = tick;
    lv.landed |= (uint8_t)(1u << (lane & 3));
}
uint8_t live_song(void) { return lv.song; }
int live_known(void) { return lv.known; }
void live_known_set(uint32_t t0) { lv.t0 = t0; lv.known = 1; }
uint32_t live_t0(void) { return lv.t0; }
int live_ticks(void) { return lv.ticks; }
int live_done(void) { return lv.done; }
int live_left(void) { return lv.left + (int)player_tap_lost(); }
uint32_t live_h0(void) { return lv.h0; }
