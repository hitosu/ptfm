/* SPDX-License-Identifier: GPL-3.0-only
 * Copyright (C) 2026 Vasily Kazantsev */
/* RECORD LIVE's song (live.h; its records: specs/files/songs.md, patterns.md, commands.md). */
#include <string.h>
#include "live.h"
#include "../plat.h"
#include "loops.h"
#include "player.h"
#include "rec.h"
#include "../store/store.h"

/* The changes waiting for their pattern, in the order they came, 4 B each: the row (| 0x80: the pattern after
 * live_done()'s), what, two values. What: 0-95 a loop's mix byte (loop * RM_N + RM_*; a its new value, b the one before),
 * EV_SIL the loops not heard from there (a, b the mask), EV_CMD a global command (a its code, b its value), EV_PUNCH a
 * punch-in's command still running (b set to the rows it ran as it ends), EV_LOOPS the loops heard from there at their
 * row a (0xFF silent), EV_KEY up a keys' cell: EV_KEY | joined to the note before << 6 | its lane << 4 | its note delay
 * (EDx), a its note (0 none, 1 a note off) | its SLD << 7, b its instrument (kin's index) << 5 | its key off's tick (Kxx;
 * K_NONE none). Joined: a 320, or with a's bit 7 a LEG (a TB-303's note is never joined, so its bit 7 is its SLD) */
#define LOG_N 255                       /* (a full log drops the change, counted as left out: the song goes on) */
enum { EV_SIL = 0x60, EV_CMD, EV_PUNCH, EV_LOOPS, EV_KEY = 0x80 };
#define EV_MIXES (LP_MAX * RM_N)
#define K_NONE 31
#define SILENT 0xFF
typedef struct { uint8_t at, kind, a, b; } ev_t;
#define TRACK_MAX (STORE_MAX_REC - STORE_HDR - 5u)     /* a pattern track's stream at most: a part's payload, its head */
static const uint8_t MIX_DEF[RM_N] = {0x7F, 0x80, 0, 0, 0, 0};
#define DUCK_SRC (16 + 0xAA - 0x90)     /* the FX setup's DUCK SOURCE and FILTER's envelope's SOURCE (songs.md) */
#define ENV_SRC (16 + 0xB1 - 0x90)

static struct {
    uint8_t on, song, len, nl, made, over, kvol;
    uint8_t lr0;                        /* the loops' row heard at the next pattern's first row (SILENT) */
    uint16_t done, left;
    uint32_t t0, rows0;
    uint8_t name[12];
    uint16_t tempo;                     /* the loops' setup at the start: the song's */
    uint8_t ticks, rpb, swing, fx[64];
    int8_t plan[LP_MAX][LP_LANES];      /* each loop's lanes' tracks (-1 none free) */
    int8_t ktrk[4];                     /* the keys' lanes' tracks */
    int8_t owner[16];                   /* the loop whose note a track played last (-1 none) */
    uint8_t have[16][RM_N];             /* each track's mix as written so far (at the start: the song's mixer) */
    uint16_t sil;                       /* at the pattern's start: the loops left out (not heard) ... */
    uint8_t snd[LP_MAX], ksnd;          /* ... each lane (bit) with a note sounding, the keys' lanes' */
    int8_t pk;                          /* the punch-in running (-1 none) ... */
    uint16_t pabs;                      /* ... since that row of the song ... */
    uint8_t pw, pwrow, pwtrk;           /* ... its command written there (pw the pattern, 0xFF none) */
    uint8_t nkin;
    uint16_t kin[8];                    /* the keys' instruments (EV_KEY's index) */
    uint8_t nev;
    ev_t ev[LOG_N];
} lv;                                   /* (set by live_begin: nothing of it read before) */

static int clamp(int v, int lo, int hi) { return v < lo ? lo : v > hi ? hi : v; }
static uint32_t a4(uint32_t n) { return (n + 3u) & ~3u; }
static int is_note(const rec_cell_t *c) { return (c->has & RC_NOTE) && c->note != REC_NOTE_OFF; }
static int cmd_val(const rec_cell_t *c, int code)
{
    for (int k = 0; k < c->n; k++) if (c->cmd[k][0] == code) return c->cmd[k][1];
    return -1;
}
static void cmd_add(rec_cell_t *c, int code, int v)
{
    if (c->n >= 4) return;
    c->cmd[c->n][0] = (uint8_t)code;
    c->cmd[c->n][1] = (uint8_t)v;
    c->n++;
    c->has |= RC_CMD;
}
static void note_off(rec_cell_t *c)
{
    memset(c, 0, sizeof *c);
    c->has = RC_NOTE;
    c->note = REC_NOTE_OFF;
}
static int bridge(const rec_cell_t *c)  /* a loop's stream's gap bridged: an ED0 alone */
{
    return !(c->has & (RC_NOTE | RC_VOL)) && c->n == 1 && c->cmd[0][0] == 0x0E && c->cmd[0][1] == 0xD0;
}
/* a cell ends its lane's note: a note off, a key off; a note's own key off past its delay ends that note in its row */
static int cell_ends(const rec_cell_t *c)
{
    int k = cmd_val(c, 0x14), d = cmd_val(c, 0x0E);
    if (!is_note(c)) return (c->has & RC_NOTE) || k >= 0;
    return k >= 0 && k > (d >= 0 && d >> 4 == 0xD ? d & 15 : 0);
}
/* the track's mix brought up to the loop's, as far as the cell has room (mixer commands in LP_MIX_KEY's order) */
static void catch_up(rec_cell_t *c, uint8_t *have, const uint8_t *mix)
{
    for (int k = 0; k < RM_N; k++) {
        int key = LP_MIX_KEY[k];
        if (have[key] == mix[key] || c->n >= 4) continue;
        cmd_add(c, LP_MIX_CODE[k], mix[key]);
        have[key] = mix[key];
    }
}

/* ---------------------------------------------------------------- the loops' lanes --- */
/* loop i's lanes as they play over the length: its stream's cursor (read at the loops' rows as they come; an earlier
 * row reads it again from its start), its base and repeats, and whether the length cuts its last repeat (its notes end
 * there, at row 0) */
typedef struct { rec_cur_t cu; const uint8_t *s; int base, reps, cut; } lane_t;
static void lane_start(lane_t *o, int i)
{
    const uint8_t *end;
    o->s = loops_stream(i, &end);
    o->base = loops_base(i);
    o->reps = loops_repeats(i);
    o->cut = lv.len % o->base && (lv.len - 1) / o->base < o->reps;
    rec_cur_set_end(&o->cu, o->s, end);
}
/* lane l at the loops' row r: its cell (1, in c), and *ends: its note ends there (a cut repeat's end, its rest's start) */
static int lane_at(lane_t *o, int r, int l, rec_cell_t *c, int *ends)
{
    int k = r / o->base;
    if (k >= o->reps) { *ends = k == o->reps && r % o->base == 0; return 0; }
    *ends = !r && o->cut;
    return o->s && rec_cur_at(&o->cu, 4 * (r % o->base) + l, c) && !bridge(c);
}
/* the loops' rows a lane takes on its track: from its first cell to its last; one still sounding at the length's end
 * (it rings on into their next pass) its track whole. 0: no notes */
static int lane_span(int i, int l, int *from, int *to)
{
    lane_t o;
    rec_cell_t c;
    int first = -1, last = 0, snd = 0, notes = 0, ends;
    lane_start(&o, i);
    for (int r = 0; r < lv.len; r++) {
        int has = lane_at(&o, r, l, &c, &ends);
        if (!has && !(ends && snd)) continue;
        if (first < 0) first = r;
        last = r;
        notes += has && is_note(&c);
        snd = has && !cell_ends(&c) && (is_note(&c) || snd);
    }
    if (!notes) return 0;
    *from = snd ? 0 : first;
    *to = snd ? lv.len : last + 1;
    return 1;
}
/* the song's tracks, kept from its start: each loop's lanes in order on the first track free over their rows (the loop
 * whose rows come first there its owner, its mix the track's), then up to 4 tracks of their own for the keys' lanes
 * (their mix keys_mix) */
static void plan(const uint8_t *keys_mix)
{
    uint8_t lf[64], lt[64], lk[64];
    int nlane = 0, from0[16], k = 0;
    for (int t = 0; t < 16; t++) { lv.owner[t] = -1; from0[t] = 0x100; memcpy(lv.have[t], MIX_DEF, RM_N); }
    for (int i = 0; i < lv.nl; i++)
        for (int l = 0; l < LP_LANES; l++) {
            int f, e, t;
            lv.plan[i][l] = -1;
            if (!lane_span(i, l, &f, &e)) continue;
            for (t = 0; t < 16; t++) {
                int ov = 0;
                for (int j = 0; j < nlane && !ov; j++) ov = lk[j] == t && f < lt[j] && lf[j] < e;
                if (!ov) break;
            }
            if (t >= 16) continue;
            lf[nlane] = (uint8_t)f;
            lt[nlane] = (uint8_t)e;
            lk[nlane++] = (uint8_t)t;
            lv.plan[i][l] = (int8_t)t;
            if (f < from0[t]) { from0[t] = f; lv.owner[t] = (int8_t)i; }
        }
    for (int t = 0; t < 16; t++) {
        if (lv.owner[t] >= 0) memcpy(lv.have[t], loops_mix(lv.owner[t]), RM_N);
        else if (k < 4 && from0[t] == 0x100) { lv.ktrk[k++] = (int8_t)t; memcpy(lv.have[t], keys_mix, RM_N); }
    }
    while (k < 4) lv.ktrk[k++] = -1;
}
/* DUCK's and FILTER's envelope's SOURCE name a loop in LOOPS (its track there): in the song, its first lane's track */
static int src_trk(int i)
{
    for (int l = 0; i < lv.nl && l < LP_LANES; l++) if (lv.plan[i][l] >= 0) return lv.plan[i][l];
    return i;
}
static int src_of(int code, int v)
{
    if (code == 0xAA) return v < 16 ? src_trk(v) : v;
    if (code == 0xB1) return v >= 1 && v <= 16 ? 1 + src_trk(v - 1) : v;
    return v;
}

/* -------------------------------------------------------------------- the changes --- */
/* the song's row heard now, to the nearest row (a change made now is written there), never in a pattern written; 0:
 * not heard (the loops' replay not playing) */
static int heard(int *at)
{
    player_state_t st;
    player_state(&st, 0);
    if (!(st.what == PL_LOOPS && st.playing) || !lv.rows0) return 0;
    int sp = st.speed ? st.speed : 6, s = (int)(st.rows_played - lv.rows0) + (2 * st.tick >= sp);
    if (s < lv.done * LIVE_ROWS) s = lv.done * LIVE_ROWS;
    *at = s;
    return 1;
}
static uint8_t at_of(int at) { return (uint8_t)((at / LIVE_ROWS > lv.done ? 0x80 : 0) | (at % LIVE_ROWS)); }
static ev_t *ev_new(uint8_t at)
{
    if (lv.nev >= LOG_N) { lv.left++; return 0; }
    ev_t *e = &lv.ev[lv.nev++];
    e->at = at;
    return e;
}
/* a command's key: the last at a row wins (an effect's ON and OFF one; any punch-in one) */
static int cmd_key(int code, int v) { return code == 0xAE || code == 0xAF ? 0x100 | v : code >= 0xC3 && code <= 0xD2 ? 0x200 : code; }
static void log_cmd(int at_, int kind, int code, int v)
{
    uint8_t at = at_of(at_);
    ev_t *e = 0;
    for (int x = 0; x < lv.nev && !e; x++)
        if (lv.ev[x].at == at && (lv.ev[x].kind == EV_CMD || lv.ev[x].kind == EV_PUNCH) &&
            cmd_key(lv.ev[x].a, lv.ev[x].b) == cmd_key(code, v))
            e = &lv.ev[x];
    if (!e && !(e = ev_new(at))) return;
    e->kind = (uint8_t)kind;
    e->a = (uint8_t)code;
    e->b = (uint8_t)v;
}
static void log_mix(int at_, int what, int was, int now)
{
    uint8_t at = at_of(at_);
    ev_t *e;
    for (int x = 0; x < lv.nev; x++)
        if (lv.ev[x].at == at && lv.ev[x].kind == what) { lv.ev[x].a = (uint8_t)now; return; }   /* (its first's before kept) */
    if (!(e = ev_new(at))) return;
    e->kind = (uint8_t)what;
    e->a = (uint8_t)now;
    e->b = (uint8_t)was;
}
static void log_one(int at_, int kind, int a, int b)   /* a row's mask or the loops' row: the last at a row wins */
{
    uint8_t at = at_of(at_);
    ev_t *e = 0;
    for (int x = 0; x < lv.nev && !e; x++) if (lv.ev[x].at == at && lv.ev[x].kind == kind) e = &lv.ev[x];
    if (!e && !(e = ev_new(at))) return;
    e->kind = (uint8_t)kind;
    e->a = (uint8_t)a;
    e->b = (uint8_t)b;
}
static void log_sil(int at, int mask) { log_one(at, EV_SIL, mask & 0xFF, mask >> 8); }
void live_loops(int at, int row)
{
    if (!lv.on) return;
    if (at < lv.done * LIVE_ROWS) at = lv.done * LIVE_ROWS;   /* (a pattern written: from the next's first row) */
    log_one(at, EV_LOOPS, row < 0 || row >= lv.len ? SILENT : row, 0);
}
/* loops.c's hook: a change of the loops heard now, logged at its row; TEMPO as F00 back to the song's own, else Fxx held
 * within 20-FF; an effect switched as FX ON / OFF; an FX knob with a command as its command (SOURCE as the song has it);
 * the chain, ROUTING and the METRONOME's knobs not */
static void hook(int what, int i, int was, int now)
{
    int at;
    if (!lv.on || !heard(&at)) return;
    if (what == LH_MIX) log_mix(at, i, was, now);
    else if (what == LH_SILENT) log_sil(at, now);
    else if (what == LH_TEMPO) log_cmd(at, EV_CMD, 0x0F, now == lv.tempo ? 0 : clamp(now, 0x20, 0xFF));
    else if (i == 7 || i == 8) {
        for (int b = 0; b < 8; b++) {
            int n = (i - 7) * 8 + b;
            if (n <= 9 && ((was ^ now) >> b & 1)) log_cmd(at, EV_CMD, now >> b & 1 ? 0xAE : 0xAF, n);
        }
    } else if (i >= 16 && i < 64 && i != 16 + 0xAE - 0x90 && i != 16 + 0xAF - 0x90)
        log_cmd(at, EV_CMD, 0x90 + i - 16, src_of(0x90 + i - 16, now));
}

/* ---- the keys' cells ---- */
static int kin_of(uint16_t inst)        /* the instrument's index, one not waiting in a cell given again if need be */
{
    for (int k = 0; k < lv.nkin; k++) if (lv.kin[k] == inst) return k;
    if (lv.nkin < 8) { lv.kin[lv.nkin] = inst; return lv.nkin++; }
    for (int k = 0; k < 8; k++) {
        int used = 0;
        for (int x = 0; x < lv.nev && !used; x++) used = lv.ev[x].kind >= EV_KEY && lv.ev[x].b >> 5 == k;
        if (!used) { lv.kin[k] = inst; return k; }
    }
    return 7;
}
static ev_t *key_at(uint8_t at, int lane)
{
    for (int x = 0; x < lv.nev; x++)
        if (lv.ev[x].at == at && lv.ev[x].kind >= EV_KEY && (lv.ev[x].kind >> 4 & 3) == lane) return &lv.ev[x];
    return 0;
}
int live_key(int at_, int lane, int note, int delay, int glide, uint16_t inst)
{
    uint8_t at = at_of(at_);
    ev_t *e = key_at(at, lane);
    if (!e && !(e = ev_new(at))) return -1;
    e->kind = (uint8_t)(EV_KEY | (glide ? 0x40 : 0) | lane << 4 | (delay & 15));
    e->a = (uint8_t)((note & 0x7F) | (glide == 2 ? 0x80 : 0));   /* (LEG: joined, bit 7) */
    e->b = (uint8_t)(kin_of(inst) << 5 | K_NONE);
    return 0;
}
void live_key_slide(int at, int lane)
{
    ev_t *e = at / LIVE_ROWS < lv.done ? 0 : key_at(at_of(at), lane);
    if (e && (e->a & 0x7F) >= 12 && !(e->kind & 0x40)) e->a |= 0x80;   /* (a joined note is never a TB-303's) */
}
int live_key_off(int at_, int lane, int kxx, int own)
{
    uint8_t at = at_of(at_);
    ev_t *e = key_at(at, lane);
    if (e && kxx >= 0 && own && (e->b & 31) == K_NONE) {   /* (let go in its note's row: that cell's key off) */
        e->b = (uint8_t)((e->b & 0xE0) | kxx);
        return 1;
    }
    if (e && (e->a & 0x7F) >= 12) return 0;                 /* (a note there ends it) */
    if (e && kxx >= 0) {
        if ((e->b & 31) == K_NONE) e->b = (uint8_t)((e->b & 0xE0) | kxx);
        return 2;
    }
    if (!e && !(e = ev_new(at))) return 0;
    e->kind = (uint8_t)(EV_KEY | lane << 4);
    e->a = (uint8_t)(kxx >= 0 ? 0 : REC_NOTE_OFF);
    e->b = (uint8_t)(kxx >= 0 ? kxx : K_NONE);
    return 2;
}
static void key_cell(const ev_t *e, rec_cell_t *c)
{
    int note = e->a & 0x7F, k = e->b & 31;
    memset(c, 0, sizeof *c);
    if (note) {
        c->has = RC_NOTE;
        c->note = (uint8_t)note;
    }
    if (note >= 12) {                   /* (a note: the instrument, the keys' volume, its note delay, its glide, LEG or SLD) */
        uint16_t in = lv.kin[e->b >> 5];
        c->has |= RC_INS | RC_VOL;
        c->cart = (uint8_t)(in >> 8);
        c->slot = (uint8_t)in;
        c->vol = lv.kvol;
        if (e->kind & 15) cmd_add(c, 0x0E, 0xD0 | (e->kind & 15));
        if (e->kind & 0x40) cmd_add(c, e->a & 0x80 ? 0xD3 : 0x03, e->a & 0x80 ? 0 : 0x20);
        else if (e->a & 0x80) cmd_add(c, 0xC1, 0);
    }
    if (k != K_NONE) cmd_add(c, 0x14, k);
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
    for (x = 0; x < lv.nev && lv.ev[x].kind != EV_PUNCH; x++)
        ;
    if (x < lv.nev) {                   /* (its pattern not written yet: its value there) */
        lv.ev[x].kind = EV_CMD;
        lv.ev[x].b = (uint8_t)v;
    } else if (lv.pw != 0xFF) {
        patch(lv.pw, lv.pwtrk, lv.pwrow, 0xC3 + lv.pk, v);
    }
    lv.pk = -1;
    lv.pw = 0xFF;
}
void live_punch(int k)
{
    int at;
    if (!lv.on || k == lv.pk || !heard(&at)) return;
    live_punch_end(at);
    if (k < 0) return;
    log_cmd(at, EV_PUNCH, 0xC3 + k, 1);
    lv.pk = (int8_t)k;
    lv.pabs = (uint16_t)at;
}

/* ------------------------------------------------------------- writing a pattern --- */
/* the pattern being written: its changes in rows' order (their order kept within a row), the loops' mixes at its start
 * (the value before each one's first change waiting), the commands put in a cell so far (a sweep over the tracks from
 * 0), where the punch-in still running went (-1 nowhere), the loops' row heard after its last row (SILENT) */
typedef struct {
    uint8_t idx[LOG_N], n, end, lr_end;
    uint8_t mix0[LP_MAX][RM_N];
    uint8_t placed[LOG_N / 8];
    int8_t prow, ptrk;
} pass_t;
static int ev_row(int x) { return lv.ev[x].at & 0x7F; }
static void pass_start(pass_t *p, int end)
{
    p->n = 0;
    p->end = (uint8_t)end;
    for (int x = 0; x < lv.nev; x++) {
        int k;
        if (lv.ev[x].at & 0x80) continue;
        for (k = p->n++; k > 0 && ev_row(p->idx[k - 1]) > ev_row(x); k--) p->idx[k] = p->idx[k - 1];
        p->idx[k] = (uint8_t)x;
    }
    for (int i = 0; i < lv.nl; i++) memcpy(p->mix0[i], loops_mix(i), RM_N);
    for (int x = lv.nev - 1; x >= 0; x--)
        if (lv.ev[x].kind < EV_MIXES) p->mix0[lv.ev[x].kind / RM_N][lv.ev[x].kind % RM_N] = lv.ev[x].b;
}
static void sweep(pass_t *p) { memset(p->placed, 0, sizeof p->placed); p->prow = p->ptrk = -1; }
/* track t of the pattern (t < 0: what fits no track, counted): row by row the cell of the lanes on it as they played at
 * the loops' row heard there (none while they were silent, their notes ending where they fell silent; a loop silenced
 * left out, its notes ending there; a note bringing the track's mix to its loop's), a loop's mix turned on the track it
 * holds, the keys' cells, the row's commands where they find room first. Into e (NULL: not encoded); keep: the track's
 * state at the end kept for the next pattern. Its stream's length (t < 0: the notes left out) */
static PLAT_COLD uint32_t build(pass_t *p, int t, lp_enc_t *e, int keep)
{
    lane_t ln[LP_MAX];
    uint8_t mix[LP_MAX][RM_N], have[RM_N], snd[LP_MAX], on[LP_MAX], ksnd = lv.ksnd, kon = 0;
    int owner = t >= 0 ? lv.owner[t] : -1, left = 0, x = 0, lr = lv.lr0;
    uint16_t sil = lv.sil;
    rec_cell_t c;
    memcpy(mix, p->mix0, sizeof mix);
    memcpy(snd, lv.snd, sizeof snd);
    memcpy(have, t >= 0 ? lv.have[t] : MIX_DEF, RM_N);
    for (int i = 0; i < lv.nl; i++) {
        on[i] = 0;
        for (int l = 0; l < LP_LANES; l++) if (lv.plan[i][l] == t) on[i] |= (uint8_t)(1u << l);
        if (on[i]) lane_start(&ln[i], i);
    }
    for (int l = 0; l < 4; l++) if (lv.ktrk[l] == t) kon |= (uint8_t)(1u << l);
    for (int r = 0; r < p->end; r++) {
        int has = 0, q = x, nsil = -1, nlr = -1;
        uint16_t turned = 0;
        while (q < p->n && ev_row(p->idx[q]) == r) q++;
        for (int j = x; j < q; j++) {   /* the loops' mixes turned, the loops silenced, where the loops are heard */
            const ev_t *v = &lv.ev[p->idx[j]];
            if (v->kind < EV_MIXES) { mix[v->kind / RM_N][v->kind % RM_N] = v->a; turned |= (uint16_t)(1u << (v->kind / RM_N)); }
            else if (v->kind == EV_SIL) nsil = v->a | v->b << 8;
            else if (v->kind == EV_LOOPS) nlr = v->a;
        }
        if (nlr >= 0) {                 /* (silent from here: their notes end; on again: from that row, nothing sounding) */
            if (nlr == SILENT && lr != SILENT)
                for (int i = 0; i < lv.nl; i++) {
                    if ((snd[i] & on[i]) && !has) { note_off(&c); has = 1; }
                    snd[i] = 0;
                }
            lr = nlr;
        }
        if (nsil >= 0) {
            for (int i = 0; i < lv.nl; i++)
                if ((nsil & ~sil) >> i & 1) {
                    if ((snd[i] & on[i]) && !has) { note_off(&c); has = 1; }
                    snd[i] = 0;
                }
            sil = (uint16_t)nsil;
        }
        for (int i = 0; lr != SILENT && i < lv.nl; i++) {
            if ((sil >> i & 1) || !on[i]) continue;
            for (int l = 0; l < LP_LANES; l++) {
                rec_cell_t d;
                int ends, bit = 1 << l;
                if (!(on[i] & bit)) continue;
                if (!lane_at(&ln[i], lr, l, &d, &ends)) {
                    if (ends && (snd[i] & bit)) { if (!has) { note_off(&c); has = 1; } snd[i] &= (uint8_t)~bit; }
                    continue;
                }
                if (!cell_ends(&d) && (is_note(&d) || (snd[i] & bit))) snd[i] |= (uint8_t)bit; else snd[i] &= (uint8_t)~bit;
                if (t < 0) { left += is_note(&d); continue; }
                if (is_note(&d)) {
                    uint16_t in = loops_inst(i);
                    d.has |= RC_INS;
                    d.cart = (uint8_t)(in >> 8);
                    d.slot = (uint8_t)in;
                    catch_up(&d, have, mix[i]);
                    owner = i;
                }
                c = d;
                has = 1;
            }
        }
        for (int i = 0; t >= 0 && i < lv.nl; i++) {
            rec_cell_t y;
            if (!(turned >> i & 1) || owner != i || !on[i]) continue;
            if (has) y = c; else memset(&y, 0, sizeof y);
            catch_up(&y, have, mix[i]);
            if (y.has) { c = y; has = 1; }
        }
        for (int j = x; j < q; j++) {   /* the keys' cells */
            const ev_t *v = &lv.ev[p->idx[j]];
            rec_cell_t k;
            int bit = 1 << (v->kind >> 4 & 3);
            if (v->kind < EV_KEY || !(kon & bit)) continue;
            key_cell(v, &k);
            if (!cell_ends(&k) && (is_note(&k) || (ksnd & bit))) ksnd |= (uint8_t)bit; else ksnd &= (uint8_t)~bit;
            if (t < 0) { left += is_note(&k); continue; }
            c = k;
            has = 1;
        }
        for (int j = x; t >= 0 && j < q; j++) {   /* the row's commands: each in the first cell with room, from track 0 */
            int k = p->idx[j];
            const ev_t *v = &lv.ev[k];
            if ((v->kind != EV_CMD && v->kind != EV_PUNCH) || (p->placed[k >> 3] >> (k & 7) & 1)) continue;
            if (has && (c.n >= 4 || cmd_val(&c, v->a) >= 0)) continue;
            if (!has) { memset(&c, 0, sizeof c); has = 1; }
            cmd_add(&c, v->a, v->b);
            p->placed[k >> 3] |= (uint8_t)(1u << (k & 7));
            if (v->kind == EV_PUNCH) { p->prow = (int8_t)r; p->ptrk = (int8_t)t; }
        }
        if (has && e) loops_enc_cell(e, r, &c);
        if (lr != SILENT) lr = (lr + 1) % lv.len;   /* (the loops' next row: their start again past the length) */
        x = q;
    }
    p->lr_end = (uint8_t)lr;
    if (keep && t >= 0) {
        memcpy(lv.have[t], have, RM_N);
        lv.owner[t] = (int8_t)owner;
        for (int i = 0; i < lv.nl; i++) lv.snd[i] = (uint8_t)((lv.snd[i] & ~on[i]) | (snd[i] & on[i]));
        lv.ksnd = (uint8_t)((lv.ksnd & ~kon) | (ksnd & kon));
    }
    return t < 0 ? (uint32_t)left : e ? loops_enc_end(e) : 0u;
}
static int free_pattern(void)           /* the first pattern number of the user's free (00-EF), -1 none */
{
    uint8_t nums[256];
    int n = store_list(REC_PATTERN, nums, 256), k = 0;
    for (int i = 0; i <= 0xEF; i++) {
        while (k < n && nums[k] < i) k++;
        if (k >= n || nums[k] != i) return i;
    }
    return -1;
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
        while (nt > 0 && !memcmp(lv.have[nt - 1], MIX_DEF, RM_N)) nt--;   /* (the mixer: up to the last that differs) */
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
        for (int t = 0; t < nt; t++) memcpy(b + 23 + RM_N * t, lv.have[t], RM_N);
        len = 23u + RM_N * (uint32_t)nt;
        memcpy(b + len, lv.fx, 64);
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
static void log_next(void)              /* the pattern written: its changes go, the next one's become the current's */
{
    int k = 0;
    for (int x = 0; x < lv.nev; x++)
        if (lv.ev[x].at & 0x80) {
            lv.ev[k] = lv.ev[x];
            lv.ev[k++].at &= 0x7F;
        }
    lv.nev = (uint8_t)k;
}
PLAT_COLD int live_chunk(int end)
{
    pass_t p;
    lp_enc_t e;
    uint32_t len[16], need;
    int hi = 0, num, t, r;
    uint8_t *b;
    if (lv.over) return LIVE_FULL;
    end = clamp(end, 1, LIVE_ROWS);
    pass_start(&p, end);
    sweep(&p);
    for (t = 0; t < 16; t++) {          /* the room first: each track's stream, the commands placed as they will be */
        loops_enc_track(&e, 0, TRACK_MAX);
        if ((len[t] = build(&p, t, &e, 0)) > TRACK_MAX) return LIVE_FULL;
        if (len[t] > 1u) hi = t + 1;
    }
    if (!hi) hi = 1;
    lv.left = (uint16_t)(lv.left + build(&p, -1, 0, 0));
    if ((num = free_pattern()) < 0) return LIVE_PATTERNS;
    need = a4(song_len()) + 8u + 12u;
    for (t = 0; t < 16; t++) need += a4(len[t]) + 8u;
    if (!store_edit_reserve(need) || song_put(num) != STORE_OK) return LIVE_FULL;
    sweep(&p);                          /* (then its tracks written, their state kept) */
    {
        uint8_t h[3] = {(uint8_t)hi, (uint8_t)(end - 1), 1};
        if (store_edit_piece(REC_PATTERN, (uint8_t)num, STORE_PC_HEAD, 1, h, 3) != STORE_OK) return LIVE_FULL;
    }
    for (t = 0; t < 16; t++) {
        if (!(b = store_scratch_claim(1))) return LIVE_FULL;
        loops_enc_track(&e, b, TRACK_MAX);
        len[t] = build(&p, t, &e, 1);
        r = store_edit_piece(REC_PATTERN, (uint8_t)num, (uint16_t)t, 1, b, len[t]);
        store_scratch_claim(0);
        if (r != STORE_OK) return LIVE_FULL;
    }
    for (int j = 0; j < p.n; j++) if (lv.ev[p.idx[j]].kind == EV_SIL) lv.sil = (uint16_t)(lv.ev[p.idx[j]].a | lv.ev[p.idx[j]].b << 8);
    lv.lr0 = p.lr_end;
    if (p.prow >= 0) { lv.pw = (uint8_t)num; lv.pwrow = (uint8_t)p.prow; lv.pwtrk = (uint8_t)p.ptrk; }
    log_next();
    lv.done++;
    lv.made = 1;
    return LIVE_OK;
}

/* --------------------------------------------------------------------- its start --- */
PLAT_COLD void live_begin(uint8_t song, const uint8_t *name, const uint8_t *keys_mix, uint8_t keys_vol, uint32_t halves,
                          uint32_t rows0, int punch, int loops_row)
{
    memset(&lv, 0, sizeof lv);
    lv.song = song;
    lv.t0 = halves;
    lv.rows0 = rows0;
    lv.len = (uint8_t)(loops_rows() ? loops_rows() : 1);
    lv.lr0 = (uint8_t)(loops_row < 0 || loops_row >= lv.len ? SILENT : loops_row);
    lv.nl = (uint8_t)loops_n();
    lv.kvol = keys_vol;
    memcpy(lv.name, name, 12);
    lv.tempo = (uint16_t)loops_tempo();
    lv.ticks = (uint8_t)loops_ticks();
    lv.rpb = (uint8_t)loops_rpb();
    lv.swing = (uint8_t)loops_swing();
    plan(keys_mix);
    memcpy(lv.fx, loops_fx(), 64);
    lv.fx[DUCK_SRC] = (uint8_t)src_of(0xAA, lv.fx[DUCK_SRC]);
    lv.fx[ENV_SRC] = (uint8_t)src_of(0xB1, lv.fx[ENV_SRC]);
    lv.sil = (uint16_t)loops_silent_mask();
    lv.pk = (int8_t)punch;              /* (a punch-in running from before: not written) */
    lv.pw = 0xFF;
    lv.on = 1;
    loops_hook = hook;
}
void live_stop(void)
{
    lv.on = 0;
    loops_hook = 0;
}
int live_on(void) { return lv.on; }
uint8_t live_song(void) { return lv.song; }
uint32_t live_rows0(void) { return lv.rows0; }
void live_rows0_set(uint32_t rows0) { lv.rows0 = rows0; }
int live_done(void) { return lv.done; }
int live_left(void) { return lv.left; }
uint32_t live_t0(void) { return lv.t0; }
int live_over(void) { return lv.over; }
