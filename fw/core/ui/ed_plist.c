/* SPDX-License-Identifier: GPL-3.0-only
 * Copyright (C) 2026 Vasily Kazantsev */
/* The pattern list (specs/ui/screens/editor.md#env-the-pattern-list; the playground's scr-grid.js) and references
 * (specs/ui/saving.md): what the EDITOR and INSTRUMENTS share (ui_ed.h). */
#include <string.h>
#include "ui_core.h"
#include "ui_priv.h"
#include "ui_data.h"
#include "ui_ed.h"
#include "ed_pat.h"
#include "ed_inst.h"
#include "../play/player.h"
#include "../play/rec.h"
#include "../store/store.h"

/* -------------------------------------------------------------------------------- references --- */
static int song_uses(uint8_t pat, int songs)            /* the song positions playing it (songs: the songs) */
{
    uint8_t sn[256];
    int n = ui_songs(sn, 256), k = 0;
    for (int i = 0; i < n; i++) {
        ui_song_t s;
        if (!ui_song(sn[i], &s)) continue;
        for (int p = 0; p < s.npos; p++) if (s.order[p] == pat) { k++; if (songs) break; }
    }
    return k;
}
static int fusion_uses(uint8_t pat)
{
    uint8_t carts[256];
    int n = ui_carts(carts, 256), k = 0;
    for (int i = 0; i < n; i++) {
        rec_cart_t c;
        if (rec_cart(carts[i], &c) || c.kind != REC_FUSION) continue;
        for (int s = 0; s < c.nslots; s++) { const uint8_t *sl = rec_slot(&c, (uint8_t)s); if (sl && rec_fusion_pattern(sl) == pat) k++; }
    }
    return k;
}
int ed_pattern_fusion(uint8_t pat) { return fusion_uses(pat) > 0; }
int ed_pattern_uses(uint8_t pat) { return song_uses(pat, 0) + fusion_uses(pat); }
int ed_inst_uses(uint16_t ref)
{
    uint8_t nums[256];
    int n = store_list(REC_PATTERN, nums, 256), k = 0;
    for (int i = 0; i < n; i++) {
        rec_pat_t p;
        if (rec_pattern(nums[i], &p)) continue;
        for (int t = 0; t < p.ntrk; t++) {
            if (!p.trk[t]) continue;
            rec_cur_t cur;
            rec_cell_t c;
            rec_cur_set(&cur, p.trk[t]);
            for (int r = 0; r < p.rows; r++) if (rec_cur_at(&cur, r, &c) && (c.has & RC_INS) && (c.cart << 8 | c.slot) == ref) k++;
        }
    }
    return k;
}

static struct { int from, to, swap, cart; } rm;
static int rm_hit(const rec_cell_t *c)
{
    if (!(c->has & RC_INS)) return 0;
    int ref = rm.cart ? c->cart : c->cart << 8 | c->slot;
    return ref == rm.from || (rm.swap && ref == rm.to);
}
static void rm_fn(int t, int r, rec_cell_t *c, void *a)
{
    (void)t; (void)r; (void)a;
    if (!rm_hit(c)) return;
    int ref = rm.cart ? c->cart : c->cart << 8 | c->slot, nw = ref == rm.from ? rm.to : rm.from;
    if (nw < 0) { c->has &= (uint8_t)~RC_INS; return; }
    if (rm.cart) c->cart = (uint8_t)nw;
    else { c->cart = (uint8_t)(nw >> 8); c->slot = (uint8_t)nw; }
}
void ed_remap(int from, int to, int swap, int whole_cart)
{
    uint8_t nums[256];
    int n = store_list(REC_PATTERN, nums, 256);
    rm.from = from; rm.to = to; rm.swap = swap; rm.cart = whole_cart;
    for (int i = 0; i < n; i++) {                       /* only the patterns that name it */
        rec_pat_t p;
        int hit = 0;
        if (rec_pattern(nums[i], &p)) continue;         /* (the built-in ones too: rewritten, they are edits) */
        for (int t = 0; t < p.ntrk && !hit; t++) {
            if (!p.trk[t]) continue;
            rec_cur_t cur;
            rec_cell_t c;
            rec_cur_set(&cur, p.trk[t]);
            for (int r = 0; r < p.rows && !hit; r++) if (rec_cur_at(&cur, r, &c) && rm_hit(&c)) hit = 1;
        }
        if (hit) ed_rewrite(nums[i], 0, 15, rm_fn, 0, -1, -1, -1);
    }
}

/* a song's order with a pattern taken out (DELETE) or renumbered (MOVE), as an edit; a song every
 * position of which plays it keeps it */
static void song_refs(uint8_t pat, int to)
{
    uint8_t *b = ui_scratch;                            /* (a song's record copied to change it) */
    uint8_t sn[256];
    int n = ui_songs(sn, 256);
    for (int i = 0; i < n; i++) {
        uint32_t len;
        uint8_t ver;
        const uint8_t *p = store_get(REC_SONG, sn[i], 0, &len, &ver);
        if (!p || len < 22 || len > UI_SCRATCH) continue;
        int npos = p[20] + 1, hit = 0, keep = 0;
        for (int k = 0; k < npos; k++) { if (p[21 + k] == pat) hit++; else keep++; }
        if (!hit || (to < 0 && !keep)) continue;
        memcpy(b, p, 21);
        int at = 21;
        for (int k = 0; k < npos; k++) {
            if (p[21 + k] != pat) b[at++] = p[21 + k];
            else if (to >= 0) b[at++] = (uint8_t)to;
        }
        int out = at - 21;
        b[20] = (uint8_t)(out - 1);
        if (b[18] >= out) b[18] = 0;                    /* LOOP FROM past the end: the start */
        memcpy(b + at, p + 21 + npos, len - 21 - (uint32_t)npos);
        ui_edit_put(REC_SONG, sn[i], 0, ver, b, (uint32_t)at + len - 21 - (uint32_t)npos);
    }
}
static void fusion_refs(uint8_t pat, int to)
{
    uint8_t *b = ui_scratch;                            /* (a FUSION cartridge copied to change it) */
    uint8_t carts[256];
    int n = ui_carts(carts, 256);
    for (int i = 0; i < n; i++) {
        rec_cart_t c;
        uint32_t len;
        if (ui_cart_kind(carts[i]) != ENG_FUSION || ins_cart_copy(carts[i], &c, b, &len, 0)) continue;
        int hit = 0;
        for (int s = 0; s < c.nslots; s++) {
            const uint8_t *sl = rec_slot(&c, (uint8_t)s);
            if (sl && rec_fusion_pattern(sl) == pat) {  /* (deleted, to < 0: FF, no pattern) */
                b[rec_slot_at(&c, (uint8_t)s)] = (uint8_t)(to >= 0 ? to : 0xFF);
                hit = 1;
            }
        }
        if (hit) ui_edit_put(REC_FUSION, carts[i], 0, 1, b, len);
    }
}

/* ------------------------------------------------------------------- the EDITOR's own pattern --- */
int ed_pattern_holds(uint8_t pat)
{
    rec_pat_t p;
    if (rec_pattern(pat, &p)) return 0;
    for (int t = 0; t < p.ntrk; t++) {
        if (!p.trk[t]) continue;
        rec_cur_t cur;
        rec_cell_t c;
        rec_cur_set(&cur, p.trk[t]);
        for (int r = 0; r < p.rows; r++)
            if (rec_cur_at(&cur, r, &c) && ((c.has & (RC_NOTE | RC_INS | RC_VOL)) || ((c.has & RC_CMD) && c.n))) return 1;
    }
    return 0;
}
int ed_own_holds(void) { return ed_pattern_holds(UI_EDITOR_PATTERN); }
/* YES empties it as BACK TO BUILT-IN does (its change deleted: the firmware's empty one again) */
void ed_own_empty(void) { if (!ed_builtin(UI_EDITOR_PATTERN)) ed_delete_pattern(UI_EDITOR_PATTERN); }
static void (*own_go)(void);
static void own_answer(int a)
{
    if (a != 1) return;
    ed_own_empty();
    own_go();
}
void ed_own_ask(void (*go)(void))
{
    if (!ed_own_holds()) { go(); return; }
    const char *lines[2] = {T("EDITOR CHANGES WILL BE LOST"), T("DO YOU WANT TO CONTINUE?")};
    own_go = go;
    ui_ask(lines, 2, T("YES"), T("NO"), own_answer);
}

/* ------------------------------------------------------------------------------ the list --- */
static struct {
    uint8_t nums[256];
    int n, sel, opener, fusion;
    int plus;                           /* `+ NEW PATTERN` first (line 0): SONGS' NEW */
    txt_t plus_text;                    /* ... or this text there (LOOPS' `NEW`), 0 none */
    int except;                         /* a pattern left out (STORE IN A PATTERN: the EDITOR's), -1 none */
    ed_timing_fn timing;                /* PLAY's preview at its own timing (LOOPS' LOAD), 0 the current song's */
    char title[40];
    void (*pick)(int);
    uint8_t mv;                         /* MOVE: the pattern afloat */
    int mv_from, mv_at;
    int heard;                          /* the pattern PLAY previews, -1 none */
    int act_pat;                        /* the menu's pattern */
} pl = {.heard = -1};
static ui_overlay_t *pl_ov;
/* the lines' figures, worked out once a pattern while the list stands (its draw runs every frame: on the FM-1 decoding
 * each line's cells and every song's order each frame made the list crawl); cleared by refill, whatever changed */
#define PL_LINES 13                     /* (the lines shown at once) */
enum { PLF_SET = 1, PLF_NONE = 2, PLF_FUSION = 4, PLF_BUILTIN = 8 };
static struct {
    uint8_t num, flags;                 /* the pattern; PLF_*: figures there, no such pattern, in a FUSION, built in */
    uint8_t ntrk, rows, uses;           /* its tracks, rows, the songs playing it (255 at most) */
    uint8_t dots[4];                    /* each track's notes, 2 bits: none, few, more, many */
} pl_lines[PL_LINES];

static void refill(void)
{
    uint8_t all[256];
    int n = ui_patterns(all, 256);                      /* (never the EDITOR's own) */
    memset(pl_lines, 0, sizeof pl_lines);
    pl.n = 0;
    for (int i = 0; i < n; i++) if (all[i] != pl.except) pl.nums[pl.n++] = all[i];
    pl.sel = ui_clamp(pl.sel, 0, pl.n + pl.plus - 1 > 0 ? pl.n + pl.plus - 1 : 0);
}
/* the highlighted line's pattern; -1 none (+ NEW PATTERN) */
static int sel_pat(void) { int i = pl.sel - pl.plus; return i >= 0 && i < pl.n ? pl.nums[i] : -1; }
static void select_num(int pat) { for (int i = 0; i < pl.n; i++) if (pl.nums[i] == pat) pl.sel = i + pl.plus; }
static void hear_end(void) { if (pl.heard >= 0) { player_pause(); pl.heard = -1; } }
static void hear(int pat)
{
    uint16_t tempo;
    uint8_t ticks, rpb, swing;
    pl.heard = pat;
    if (pl.timing && pl.timing(pat, &tempo, &ticks, &rpb, &swing))
        player_play_pattern_at((uint8_t)pat, 0, tempo, ticks, rpb, swing);
    else player_play_pattern((uint8_t)pat, 0);
}

static void float_step(int d)
{
    int k = pl.mv_at;
    uint8_t all[256];
    int n = store_list(REC_PATTERN, all, 256);
    for (;;) {
        k += d;
        if (k < 0 || k > 0xEF) return;
        int used = 0;
        for (int i = 0; i < n; i++) if (all[i] == k && k != pl.mv_from) used = 1;
        if (!used) break;
    }
    pl.mv_at = k;
}
static void pl_knob(ui_overlay_t *o, int e, int s, int c)
{
    (void)o; (void)c;
    if (e != K_SELECT) return;
    if (pl.mv) { float_step(s > 0 ? 1 : -1); return; }
    if (pl.n + pl.plus) pl.sel = ui_clamp(pl.sel + s, 0, pl.n + pl.plus - 1);
    if (pl.heard >= 0 && pl.heard != sel_pat()) {       /* the preview follows (+ NEW PATTERN: it ends) */
        if (sel_pat() >= 0) hear(sel_pat());
        else hear_end();
    }
}
static void pl_play(ui_overlay_t *o)
{
    (void)o;
    if (pl.heard >= 0 && pl.heard == sel_pat()) hear_end();
    else if (sel_pat() >= 0) hear(sel_pat());
}

/* the menu: NEW, CLONE, MOVE, DELETE; a built-in pattern (F0-FD, none yet) NEW and CLONE, and once changed BACK TO
 * BUILT-IN (its change deleted) */
enum { PM_NEW = 1, PM_CLONE, PM_MOVE, PM_DELETE, PM_BUILTIN };
static int ref_pat, ref_to, ref_uses;
static void do_delete(int upd)
{
    if (upd) { song_refs((uint8_t)ref_pat, -1); fusion_refs((uint8_t)ref_pat, -1); }   /* (a song whose whole order is
                                                         * the pattern keeps it: never left with no positions) */
    ed_delete_pattern((uint8_t)ref_pat);
    refill();
    char v[32];
    ui_fmt(v, sizeof v, T("PATTERN %02X DELETED"), ref_pat);
    ui_toast(v);
    if (upd) ui_save_refs(ref_uses);
}
static void do_move(int upd)
{
    if (ed_copy_pattern((uint8_t)ref_pat, (uint8_t)ref_to)) return;    /* (the edit memory full: it asked; not moved) */
    ed_delete_pattern((uint8_t)ref_pat);
    if (upd) { song_refs((uint8_t)ref_pat, ref_to); fusion_refs((uint8_t)ref_pat, ref_to); }
    if (ui_cur.pattern == ref_pat) ui_cur.pattern = (uint8_t)ref_to;
    refill();
    select_num(ref_to);
    char v[40];
    ui_fmt(v, sizeof v, T("PATTERN %02X MOVED TO %02X"), ref_pat, ref_to);
    ui_toast(v);
    if (upd) ui_save_refs(ref_uses);
}
static void (*ref_change)(int);
static void ref_answer(int a) { if (a >= 0) ref_change(a == 1); }
/* a change of something used asks first (specs/ui/saving.md#references): YES makes it with every
 * reference it touches and saves; NO makes it and leaves the references; HOME does nothing */
static void ref_ask(const char *label, int uses, void (*change)(int))
{
    if (!uses) { change(0); return; }
    const char *lines[2];                               /* (the question copies its lines) */
    char l0[40];
    ui_fmt(l0, sizeof l0, uses == 1 ? T("%s IS USED %d TIME") : T("%s IS USED %d TIMES"), label, uses);
    lines[0] = l0;
    lines[1] = T("UPDATE REFERENCES?");
    ref_change = change;
    ref_uses = uses;
    ui_ask(lines, 2, T("YES"), T("NO"), ref_answer);
}
static void put_down(void)
{
    int from = pl.mv_from, to = pl.mv_at;
    pl.mv = 0;
    if (to == from) return;
    char label[16];
    ui_fmt(label, sizeof label, T("PATTERN %02X"), from);
    ref_pat = from; ref_to = to;
    ref_ask(label, ed_pattern_uses((uint8_t)from), do_move);
}
static void menu_pick(int a)
{
    int n = pl.act_pat, free = ed_first_free();
    char v[40], label[16];
    ui_fmt(label, sizeof label, T("PATTERN %02X"), n);
    if ((a == PM_NEW || a == PM_CLONE) && free < 0) { ui_toast(T("ALL 240 PATTERNS USED")); return; }
    if (a == PM_NEW) {
        if (ed_new_pattern((uint8_t)free, 8, 64, 1)) return;   /* (not made: the edit memory full) */
        refill(); select_num(free);
        ui_fmt(v, sizeof v, T("PATTERN %02X MADE"), free);
        ui_toast(v);
    } else if (a == PM_CLONE) {                         /* a copy in the user area, editable */
        if (ed_copy_pattern((uint8_t)n, (uint8_t)free)) return;
        refill(); select_num(free);
        ui_fmt(v, sizeof v, T("%02X CLONED TO %02X"), n, free);
        ui_toast(v);
    } else if (a == PM_DELETE) {
        ref_pat = n;
        ref_ask(label, ed_pattern_uses((uint8_t)n), do_delete);   /* (songs' positions and FUSION slots) */
    } else if (a == PM_MOVE) {
        if (free < 0) { ui_toast(T("NO FREE NUMBER")); return; }
        pl.mv = 1; pl.mv_from = pl.mv_at = n;
        ui_toast(T("SELECT MOVES IT, SEL PUTS IT DOWN"));
    } else if (a == PM_BUILTIN) {                       /* its change deleted: the firmware's own again */
        ed_delete_pattern((uint8_t)n);
        refill(); select_num(n);
        ui_toast(ui_txt_v(T("# BUILT-IN AGAIN"), label));
    }
}
static void menu(void)
{
    int n = sel_pat();
    if (n < 0) return;
    pl.act_pat = n;
    ui_mitem_t m[4];                                    /* (the menu copies its items) */
    char title[32];
    int k = 0;
    m[k++] = (ui_mitem_t){TX("NEW"), PM_NEW, 0, 0};
    m[k++] = (ui_mitem_t){TX("CLONE"), PM_CLONE, 0, 0};
    if (n >= 0xF0) {                                    /* (built in: changed, its change can go) */
        if (!ed_builtin((uint8_t)n)) m[k++] = (ui_mitem_t){TX("BACK TO BUILT-IN"), PM_BUILTIN, 0, 0};
    } else {
        m[k++] = (ui_mitem_t){TX("MOVE"), PM_MOVE, 0, 0};
        m[k++] = (ui_mitem_t){TX("DELETE"), PM_DELETE, 0, 0};
    }
    ui_fmt(title, sizeof title, T("PATTERN %02X"), n);
    ui_menu("plist", title, m, k, menu_pick, B_SEL);
}
static int pl_tap(ui_overlay_t *o, int b)
{
    if (pl.mv) { if (b == B_SEL) put_down(); return 1; }
    if (b == o->opener) {
        void (*pick)(int) = pl.pick;
        int n = pl.plus && pl.sel == 0 ? ED_PICK_NEW : sel_pat();
        hear_end();
        ui_close(o);
        pl_ov = 0;
        if (pick && n >= 0) pick(n);
        return 1;
    }
    if (b == B_SEL) { menu(); return 1; }
    if (b == B_ARP) { pl_play(o); return 1; }          /* (ARP does what PLAY does: a shortcut the hint leaves out) */
    return 0;
}
static int pl_lit(ui_overlay_t *o, int b)               /* ARP lit while the preview plays */
{
    (void)o;
    return b == B_ARP && pl.heard >= 0 ? LV_FULL : LV_NONE;
}
static void pl_cancel(ui_overlay_t *o)
{
    if (pl.mv) { pl.mv = 0; ui_toast(T("NOT MOVED")); return; }
    hear_end();
    ui_close(o);
    pl_ov = 0;
}
/* a FUSION slot's list: the keys play its FUSION instrument */
static int pl_key(ui_overlay_t *o, int n, int down)
{
    (void)o;
    char nm[16];
    if (pl.fusion < 0 || !ui_inst_name((uint16_t)pl.fusion, nm)) return 0;
    int m = ui_midi(n);
    if (m < 12 || m > 127) return 1;
    if (down) { pl_inst_t in = {(uint8_t)(pl.fusion >> 8), (uint8_t)pl.fusion}; player_key_on((uint8_t)m, in, (uint8_t)ui_set[S_KEYS_VOLUME]); }
    else player_key_off((uint8_t)m);
    return 1;
}
/* pattern num's figures (shown: the patterns the lines show this frame, num among them): kept, else worked out into
 * a free place or one of a pattern no line shows */
static int line_of(int num, const uint8_t *shown, int nshown)
{
    int at = -1;
    for (int i = 0; i < PL_LINES; i++) if ((pl_lines[i].flags & PLF_SET) && pl_lines[i].num == num) return i;
    for (int i = 0; i < PL_LINES && at < 0; i++) {
        int on = 0;
        for (int k = 0; k < nshown && (pl_lines[i].flags & PLF_SET); k++) on |= shown[k] == pl_lines[i].num;
        if (!on) at = i;
    }
    if (at < 0) at = 0;                                 /* (never: more places than lines) */
    memset(&pl_lines[at], 0, sizeof pl_lines[0]);
    pl_lines[at].num = (uint8_t)num;
    pl_lines[at].flags = PLF_SET;
    rec_pat_t p;
    if (rec_pattern((uint8_t)num, &p)) { pl_lines[at].flags |= PLF_NONE; return at; }
    int uses = song_uses((uint8_t)num, 1);
    pl_lines[at].ntrk = (uint8_t)p.ntrk;
    pl_lines[at].rows = (uint8_t)p.rows;
    pl_lines[at].uses = (uint8_t)(uses > 255 ? 255 : uses);
    if (fusion_uses((uint8_t)num) > 0) pl_lines[at].flags |= PLF_FUSION;
    if (ed_builtin((uint8_t)num)) pl_lines[at].flags |= PLF_BUILTIN;
    for (int t = 0; t < p.ntrk; t++) {
        int k = 0;
        if (p.trk[t]) {
            rec_cur_t cur;
            rec_cell_t c;
            rec_cur_set(&cur, p.trk[t]);
            for (int r = 0; r < p.rows; r++)
                if (rec_cur_at(&cur, r, &c) && (c.has & RC_NOTE) && c.note != REC_NOTE_OFF) k++;
        }
        int d = !k ? 0 : k * 8 < p.rows ? 1 : k * 3 < p.rows ? 2 : 3;
        pl_lines[at].dots[t >> 2] |= (uint8_t)(d << (t & 3) * 2);
    }
    return at;
}
static void pl_draw(ui_overlay_t *o)
{
    (void)o;
    int y = BODY_Y + 2, rows = 14;                      /* (13 lines, then the hint) */
    /* a pattern afloat (MOVE) is listed under the number it would take, boxed yellow */
    uint8_t nums[256];
    int n = 0, sel = pl.sel;
    if (pl.mv) {
        for (int i = 0; i < pl.n; i++) if (pl.nums[i] != pl.mv_from) nums[n++] = pl.nums[i];
        int k = n;
        while (k > 0 && nums[k - 1] > pl.mv_at) { nums[k] = nums[k - 1]; k--; }
        nums[k] = (uint8_t)pl.mv_at;
        n++;
        sel = k;
    } else { memcpy(nums, pl.nums, (size_t)pl.n); n = pl.n; sel -= pl.plus; }
    ui_panel(2, y, 236, 206, pl.title, C_CYAN);
    g_text(8, y + 16, &FONT_T, T("NR  SIZE   NOTES BY TRACK     SONGS"), C_DGREY);
    /* (lines: + NEW PATTERN at -1 when shown, then the patterns 0 .. n - 1) */
    int top = sel - 6 < n - rows + 1 ? sel - 6 : n - rows + 1;
    if (top < -pl.plus) top = -pl.plus;
    uint8_t shown[PL_LINES];                            /* (the patterns the lines show: their figures kept) */
    int nshown = 0;
    for (int i = top < 0 ? 0 : top; i < n && i < top + rows - 1; i++)
        shown[nshown++] = (uint8_t)(pl.mv && nums[i] == pl.mv_at ? pl.mv_from : nums[i]);
    for (int i = top; i < n && i < top + rows - 1; i++) {
        int yy = y + 30 + (i - top) * 12, on = i == sel;
        if (i < 0) {                                    /* + NEW PATTERN: an empty one, 8 x 64 (LOOPS: NEW) */
            if (on) g_box(4, yy - 1, 232, 12, C_WHITE);
            g_text(8, yy, &FONT_T, pl.plus_text ? T(pl.plus_text) : T("+ NEW PATTERN"), C_WHITE);
            continue;
        }
        int num = pl.mv && nums[i] == pl.mv_at ? pl.mv_from : nums[i];
        int f = line_of(num, shown, nshown), uses = pl_lines[f].uses, fus = pl_lines[f].flags & PLF_FUSION;
        if (pl_lines[f].flags & PLF_NONE) continue;
        int dim = !uses && !fus;
        if (on) g_box(4, yy - 1, 232, 12, pl.mv ? C_YELLOW : C_WHITE);
        int nc = pl_lines[f].flags & PLF_BUILTIN ? C_BROWN : on ? C_WHITE : dim ? C_DGREY : C_LGREY;
        g_text(8, yy, &FONT_T, ui_hex2(nums[i]), nc);
        char size[12];
        ui_fmt(size, sizeof size, "%02dX%-3d", pl_lines[f].ntrk, pl_lines[f].rows);
        g_text(32, yy, &FONT_T, size, dim ? C_DGREY : C_CYAN);
        for (int t = 0; t < pl_lines[f].ntrk; t++) {
            static const uint8_t COL[4] = {C_DGREY, C_LGREY, C_WHITE, C_YELLOW};   /* (none, few, more, many notes) */
            int d = pl_lines[f].dots[t >> 2] >> (t & 3) * 2 & 3;
            g_rect(80 + t * 6, yy + 4, 4, 4, dim && d ? C_DGREY : COL[d]);
        }
        const char *u = fus ? T("FUSION") : uses ? ui_dec(uses) : "-";
        g_text(186, yy, &FONT_T, u, fus ? C_LMAGENTA : dim ? C_DGREY : C_LGREY);
    }
    g_run_t h[2] = {{T("PLAY"), C_YELLOW}, {pl.heard >= 0 ? T(": STOP HEARING IT") : T(": HEAR IT"), C_LGREY}};
    g_runs(8, y + 30 + (rows - 1) * 12, &FONT_T, h, 2);
}
static void pattern_list(const char *title, int current, void (*pick)(int pat), int opener, int fusion_inst, int plus,
                         ed_timing_fn timing, int except)
{
    pl.sel = 0;
    pl.plus = plus;
    pl.plus_text = 0;
    pl.except = except;
    pl.timing = timing;
    pl.mv = 0;
    pl.heard = -1;
    refill();
    select_num(current);
    ui_fmt(pl.title, sizeof pl.title, "%s", title);
    pl.pick = pick;
    pl.opener = opener;
    pl.fusion = fusion_inst;
    ui_overlay_t o = {.kind = OV_LIST, .opener = (int8_t)opener, .knob = pl_knob, .tap = pl_tap, .cancel = pl_cancel,
                      .draw = pl_draw, .key = pl_key, .play = pl_play, .lit = pl_lit};
    pl_ov = ui_open(&o);
}
void ed_pattern_list(const char *title, int current, void (*pick)(int pat), int opener, int fusion_inst)
{
    pattern_list(title, current, pick, opener, fusion_inst, 0, 0, -1);
}
void ed_pattern_list_new(const char *title, int current, void (*pick)(int pat), int opener)
{
    pattern_list(title, current, pick, opener, -1, 1, 0, -1);
}
void ed_pattern_list_new_but(const char *title, int except, void (*pick)(int pat), int opener)
{
    pattern_list(title, -1, pick, opener, -1, 1, 0, except);
}
void ed_pattern_list_timed(const char *title, int current, void (*pick)(int pat), int opener, ed_timing_fn timing)
{
    pattern_list(title, current, pick, opener, -1, 0, timing, -1);
}
void ed_pattern_list_plus(const char *title, txt_t plus, void (*pick)(int pat), int opener)
{
    pattern_list(title, -1, pick, opener, -1, 1, 0, -1);
    pl.plus_text = plus;
}
