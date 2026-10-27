/* SPDX-License-Identifier: GPL-3.0-only
 * Copyright (C) 2026 Vasily Kazantsev */
/* SONGS (specs/ui/screens/songs.md): the list of songs and, below, the current song's order of patterns,
 * 8 a line; ALGORITHM picks where PLAY starts (boxed); SEQ: the play mode; GLO the song's properties; EDIT its
 * order; SEL the menus. Every change goes into the edit memory (ui_edit_*) as whole song records. */
#include <string.h>
#include "ui_core.h"
#include "ui_priv.h"
#include "ui_data.h"
#include "fxdef.h"
#include "ed_pat.h"
#include "ui_ed.h"
#include "../play/player.h"
#include "../store/store.h"

#define LIST 7
#define REC_MAX 1024                    /* a song's record: ~450 B at most (specs/files/songs.md) */
/* the record being changed, the order's tail while it moves, a song afloat's: the UI's scratch, borrowed */
#define rec (ui_scratch)
#define TAIL (ui_scratch + REC_MAX)
#define KEEP (ui_scratch + 2 * REC_MAX)
static int pos;                         /* the boxed position (in the order: the cursor) */
static uint8_t picked;                  /* ALGORITHM picked it (else the box follows where PLAY would go on) */
static uint8_t order_mode;              /* EDIT: the cursor is in the order */
static int clip = -1;                   /* COPY's pattern */
static uint8_t nums[256];
static int nsongs;
static int mv_from = -1, mv_at;         /* MOVE: the song afloat (list indexes) */

static int index_of(uint8_t song) { for (int i = 0; i < nsongs; i++) if (nums[i] == song) return i; return -1; }
static void refresh(void) { nsongs = ui_songs(nums, 256); }
static int playing_here(const player_state_t *st) { return st->what == PL_SONG && st->song == ui_cur.song; }
static int start_pos(void)
{
    ui_song_t s;
    if (!ui_song(ui_cur.song, &s) || !s.npos) return 0;
    if (picked) return ui_clamp(pos, 0, s.npos - 1);
    player_state_t st;
    ui_player_state(&st);
    if (playing_here(&st)) return ui_clamp(st.pos, 0, s.npos - 1);
    return 0;
}

/* ---- a song's record, copied to change it ---- */
static uint32_t rlen;
static uint8_t rver;
static int load(uint8_t n)
{
    const uint8_t *p = store_get(REC_SONG, n, 0, &rlen, &rver);
    if (!p || rlen > REC_MAX || rlen < 21) return 0;
    memcpy(rec, p, rlen);
    return 1;
}
static int put(uint8_t n) { return ui_edit_put(REC_SONG, n, 0, rver, rec, rlen); }
static void song_changed(uint8_t n) { player_song_changed(n); ui_dirty(); }
/* the order's positions replaced: the record's tail (the mixer, the FX setup) moves with it */
static void set_order(const uint8_t *order, int n)
{
    uint8_t *tail = TAIL;
    int old = rec[20] + 1;
    uint32_t tl = rlen - (uint32_t)(21 + old);
    memcpy(tail, rec + 21 + old, tl);
    if (n < 1) n = 1;                                   /* (a song's record holds 1-256 positions) */
    memmove(rec + 21, order, (size_t)n);
    memcpy(rec + 21 + n, tail, tl);
    rec[20] = (uint8_t)(n - 1);
    rlen = (uint32_t)(21 + n) + tl;
    if (rec[18] >= n) rec[18] = (uint8_t)(n - 1);       /* LOOP FROM stays inside the order */
}
static void not_yet(void) { ui_toast(T("THIS: NOT IN THIS FIRMWARE YET")); }

/* ---- the properties (GLO) ---- */
static char pv[32];
static ui_song_t ps;
static int props_get(void) { return ui_song(ui_cur.song, &ps); }
static const char *p_name(void) { props_get(); return ps.name; }
static const char *p_tempo(void) { props_get(); ui_fmt(pv, sizeof pv, "%02X (%d BPM)", ps.tempo, ui_song_bpm(&ps)); return pv; }
static const char *p_ticks(void) { props_get(); ui_fmt(pv, sizeof pv, "%d", ps.ticks); return pv; }
static const char *p_rpb(void) { props_get(); ui_fmt(pv, sizeof pv, "%d", ps.rpb); return pv; }
static const char *p_swing(void) { props_get(); ui_fmt(pv, sizeof pv, "%d %%", ps.swing); return pv; }
static const char *p_end(void) { props_get(); return ps.flags & 1 ? T("STOP") : T("LOOP"); }
static const char *p_from(void) { props_get(); ui_fmt(pv, sizeof pv, T("POSITION %02X"), ps.loop_from); return pv; }
static void change_byte(int at, int d, int lo, int hi)
{
    if (!load(ui_cur.song)) return;
    int v = ui_clamp(rec[at] + d, lo, hi);
    if (v == rec[at]) return;
    rec[at] = (uint8_t)v;
    if (!put(ui_cur.song)) song_changed(ui_cur.song);
}
static void c_tempo(int d, int c)
{
    if (!load(ui_cur.song)) return;
    int t = ui_clamp((rec[12] | rec[13] << 8) + d * (c ? 10 : 1), (int)REC_TEMPO_MIN, (int)REC_TEMPO_MAX);
    rec[12] = (uint8_t)t; rec[13] = (uint8_t)(t >> 8);
    if (!put(ui_cur.song)) song_changed(ui_cur.song);
}
static void c_ticks(int d, int c) { (void)c; change_byte(14, d, 1, 31); }
static void c_rpb(int d, int c) { (void)c; change_byte(15, d, 1, 16); }
static void c_swing(int d, int c) { change_byte(16, d * (c ? 5 : 1), 50, 75); }
static void c_end(int d, int c)
{
    (void)d; (void)c;
    if (!load(ui_cur.song)) return;
    rec[17] ^= 1;
    if (!put(ui_cur.song)) song_changed(ui_cur.song);
}
static void c_from(int d, int c) { (void)c; if (load(ui_cur.song)) change_byte(18, d, 0, rec[20]); }
static void renamed(const char *name)
{
    if (!name || !load(ui_cur.song)) return;
    ui_put_name6(rec, 0, 16, name[0] ? name : T("NO NAME"));
    if (!put(ui_cur.song)) song_changed(ui_cur.song);
}
static void rename(void)
{
    ui_song_t s;
    if (ui_song(ui_cur.song, &s)) ui_text_entry(T("SONG NAME"), s.name, 16, B_EDIT, renamed);
}
static void props(void)
{
    const ui_prop_t items[7] = {
        {TX("NAME"), p_name, 0, rename, 0, 0}, {TX("TEMPO"), p_tempo, c_tempo, 0, 0, 0},
        {TX("TICKS A ROW"), p_ticks, c_ticks, 0, 0, 0}, {TX("ROWS A BEAT"), p_rpb, c_rpb, 0, 0, 0},
        {TX("SWING"), p_swing, c_swing, 0, 0, 0}, {TX("AT THE END"), p_end, c_end, 0, 0, 0},
        {TX("LOOP FROM"), p_from, c_from, 0, 0, 0},
    };
    char t[32];
    ui_fmt(t, sizeof t, "%s", ui_txt_v(T("SONG # PROPERTIES"), ui_hex2(ui_cur.song)));
    ui_props(t, items, 7, B_GLO);
}

/* ---- the list's menu: NEW, CLONE, MOVE, DELETE, RENAME, REVERT ---- */
/* the song at list index i takes the record of list index j's (j's read before i is written) */
static void copy_song(uint8_t to, uint8_t from)
{
    if (load(from)) put(to);
}
enum { M_NEW = 1, M_CLONE, M_MOVE, M_DELETE, M_RENAME, M_UNLOAD, M_REVERT };
static void song_revert_ask(void);
/* DELETE's question (specs/ui/screens/songs.md): ITS PATTERNS TOO, ticked at first, the song's own ones */
static char del_row[20], del_note[20], del_name[17], del_foot[33];  /* (kept: the question draws them later) */
static const char *del_title(const ui_check_t *r, int n) { (void)r; (void)n; return ui_txt_v(T("DELETE SONG #?"), ui_hex2(ui_cur.song)); }
static void deleted(int a, ui_check_t *rows)
{
    if (a != 1) return;
    uint8_t num = ui_cur.song, own[32];
    int k = index_of(num), sh;
    if (k < 0) return;
    ui_song_own(num, own, del_note, &sh);               /* (as asked: nothing changed meanwhile) */
    player_state_t st;
    ui_player_state(&st);
    if (st.what == PL_SONG && st.playing) player_pause();
    for (int i = k; i < nsongs - 1; i++) copy_song(nums[i], nums[i + 1]);      /* the ones after move up */
    ui_edit_del(REC_SONG, nums[nsongs - 1], 0);
    int np = 0;
    if (rows[0].on) for (int p = 0; p < 0xF0; p++)
        if (own[p >> 3] >> (p & 7) & 1) { ui_edit_del(REC_PATTERN, (uint8_t)p, 0); np++; }
    refresh();
    ui_cur.song = nsongs ? nums[ui_clamp(k, 0, nsongs - 1)] : 0;
    picked = 0;
    for (int i = 0; i < nsongs; i++) player_song_changed(nums[i]);
    if (np == 1) ui_toast(ui_txt_v(T("SONG # DELETED, 1 PATTERN TOO"), ui_hex2(num)));
    else if (np) ui_toast(ui_txt_v(ui_txt_v(T("SONG # DELETED, # PATTERNS TOO"), ui_hex2(num)), ui_dec(np)));
}
/* NEW's pattern list picked (specs/ui/screens/songs.md): the song made at the end of the list, its one position the
 * pattern picked, or a new empty one (8 tracks x 64 rows at the first free number) */
static void new_song_with(int pat)
{
    int n = nsongs ? nums[nsongs - 1] + 1 : 0;          /* at the end: nothing shifts */
    if (n > UI_SONG_LAST) { ui_toast(T("NO FREE SONG NUMBER")); return; }   /* (FE: the keys' song) */
    if (pat == ED_PICK_NEW) {
        int f = ui_free_pattern();
        uint8_t p[5] = {8, 63, 1, 0, 0};
        if (f < 0) { ui_toast(T("NO FREE PATTERN")); return; }
        if (ui_edit_put(REC_PATTERN, (uint8_t)f, 0, 1, p, sizeof p)) return;
        pat = f;
    }
    memset(rec, 0, 21 + 1 + 1 + 64 + 8);
    ui_put_name6(rec, 0, 16, T("NEW SONG"));
    rec[12] = 120; rec[13] = 0;                         /* TEMPO 120 */
    rec[14] = 6; rec[15] = 4; rec[16] = 50; rec[19] = 64;
    rec[20] = 0; rec[21] = (uint8_t)pat;                /* one position: the pattern picked */
    rec[22] = 0;                                        /* the mixer: no tracks stored */
    fxd_default(rec + 23);                              /* then the FX setup, a new song's (only LIMITER on) */
    rlen = 23 + 64 + 8;                                 /* (and the spare bytes) */
    rver = 1;
    if (!put((uint8_t)n)) { refresh(); ui_cur.song = (uint8_t)n; pos = 0; picked = 0; }
}
static void list_pick(int id)
{
    int k = index_of(ui_cur.song);
    switch (id) {
    case M_NEW:                                         /* (first its pattern: NEW SONG STARTS WITH) */
        if ((nsongs ? nums[nsongs - 1] + 1 : 0) > UI_SONG_LAST) { ui_toast(T("NO FREE SONG NUMBER")); return; }
        ed_pattern_list_new(T("NEW SONG STARTS WITH"), ui_cur.pattern, new_song_with, B_SEL);
        return;
    case M_CLONE: {
        if (k < 0) return;
        int last = nums[nsongs - 1];
        if (last >= UI_SONG_LAST) { ui_toast(T("NO FREE SONG NUMBER")); return; }
        copy_song((uint8_t)(last + 1), (uint8_t)last);              /* the ones after the song move down a number */
        for (int i = nsongs - 1; i > k + 1; i--) copy_song(nums[i], nums[i - 1]);
        if (load(nums[k])) {
            ui_song_t s;
            char nm[20];
            ui_song(nums[k], &s);
            ui_fmt(nm, sizeof nm, "%s", s.name);
            nm[12] = 0;
            int e = (int)strlen(nm);
            while (e && nm[e - 1] == ' ') nm[--e] = 0;
            ui_fmt(nm + e, (int)sizeof nm - e, " 2");
            ui_put_name6(rec, 0, 16, nm);
            put(k + 1 < nsongs ? nums[k + 1] : (uint8_t)(last + 1));
        }
        refresh();
        if (k + 1 < nsongs) ui_cur.song = nums[k + 1];
        pos = 0; picked = 0;
        return;
    }
    case M_MOVE:
        if (k < 0) return;
        mv_from = mv_at = k;
        ui_toast(T("SELECT MOVES IT, SEL PUTS IT DOWN"));
        return;
    case M_DELETE: {
        ui_song_t s;
        if (!ui_song(ui_cur.song, &s)) return;
        uint8_t own[32];
        int sh, n = ui_song_own(ui_cur.song, own, del_note, &sh);
        ui_fmt(del_name, sizeof del_name, "%s", s.name);
        ui_fmt(del_row, sizeof del_row, "%s", T("ITS PATTERNS TOO"));
        ui_fmt(del_foot, sizeof del_foot, "%s", ui_txt_v(T("# SHARED WITH OTHER SONGS STAY"), ui_dec(sh)));
        ui_check_t r = {del_row, del_note, n, n > 0};
        ui_checklist(del_title, &r, 1, T("DELETE"), T("KEEP"), deleted, del_name, sh ? del_foot : 0, 1);
        return;
    }
    case M_RENAME: rename(); return;
    case M_UNLOAD: ui_unload_song(); return;
    case M_REVERT: song_revert_ask(); return;
    }
}
static void list_menu(void)
{
    const ui_mitem_t items[7] = {{TX("NEW"), M_NEW, 0, 0}, {TX("CLONE"), M_CLONE, 0, 0}, {TX("MOVE"), M_MOVE, 0, 0},
                                 {TX("DELETE"), M_DELETE, 0, 0}, {TX("RENAME"), M_RENAME, 0, 0},
                                 {ui_song_held() ? TX("UNLOAD SONG") : 0, M_UNLOAD, 0, 0}, {TX("REVERT"), M_REVERT, 0, 0}};
    char t[24];
    int rv = ui_edit_unsaved(REC_SONG, ui_cur.song, 0);
    ui_fmt(t, sizeof t, T("SONG %02X"), ui_cur.song);
    ui_menu("songs", t, items, 6 + rv, list_pick, B_SEL);   /* (UNLOAD SONG while a song is held; REVERT last, while
                                                             * unsaved) */
}
/* MOVE: the song afloat put down at its new place; the ones between shift a number */
static void put_down(void)
{
    int from = mv_from, at = mv_at;
    mv_from = -1;
    if (at == from) return;
    uint8_t *keep = KEEP;
    uint32_t klen;
    uint8_t kver;
    const uint8_t *p = store_get(REC_SONG, nums[from], 0, &klen, &kver);
    if (!p || klen > REC_MAX) return;
    memcpy(keep, p, klen);
    if (from < at) for (int i = from; i < at; i++) copy_song(nums[i], nums[i + 1]);
    else for (int i = from; i > at; i--) copy_song(nums[i], nums[i - 1]);
    ui_edit_put(REC_SONG, nums[at], 0, kver, keep, klen);
    ui_cur.song = nums[at];
    for (int i = 0; i < nsongs; i++) player_song_changed(nums[i]);
    ui_toast(ui_txt_v(T("SONG MOVED TO #"), ui_hex2(nums[at])));
}

/* ---- the order (EDIT): HOME drops what changed in it, and the patterns made meanwhile ---- */
/* the song's record as it was at EDIT: an edit mode's copy of it in the edit memory (of the song alone: the EDITOR
 * opened from here edits patterns, which the order's HOME leaves as they are) */
static uint8_t sess_made[16], sess_nmade;
static void order_begin(void)
{
    ui_edit_session_of(REC_SONG, ui_cur.song);
    sess_nmade = 0;
    order_mode = 1;
    pos = start_pos();
}
static void order_keep(void)
{
    order_mode = 0;
    ui_edit_session(0);
}
static void order_drop(void)
{
    int made = sess_nmade;
    order_mode = 0;
    for (int i = 0; i < sess_nmade; i++) ui_edit_del(REC_PATTERN, sess_made[i], 0);    /* (made here: they go) */
    sess_nmade = 0;
    if (!ui_edit_session_drop() && made) ui_toast(T("CHANGES DROPPED"));
    song_changed(ui_cur.song);
}
static void order_edit(void (*fn)(uint8_t *order, int *n))
{
    if (!load(ui_cur.song)) return;
    uint8_t order[256];
    int n = rec[20] + 1;
    memcpy(order, rec + 21, (size_t)n);
    fn(order, &n);
    set_order(order, n);
    if (!put(ui_cur.song)) song_changed(ui_cur.song);
}
static int arg_pat;
static void o_set(uint8_t *o, int *n) { (void)n; o[pos] = (uint8_t)arg_pat; }
static void o_remove(uint8_t *o, int *n)
{
    if (*n <= 1) return;
    memmove(o + pos, o + pos + 1, (size_t)(*n - pos - 1));
    (*n)--;
    pos = ui_clamp(pos, 0, *n - 1);
}
static void o_insert(uint8_t *o, int *n)
{
    if (*n >= 256) return;
    memmove(o + pos + 1, o + pos, (size_t)(*n - pos));
    (*n)++;
}
static void o_after(uint8_t *o, int *n)                 /* arg_pat in after the position, the cursor onto it */
{
    if (*n >= 256) return;
    memmove(o + pos + 2, o + pos + 1, (size_t)(*n - pos - 1));
    o[pos + 1] = (uint8_t)arg_pat;
    (*n)++;
    pos++;
}
static int new_pattern(int like)
{
    int f = ui_free_pattern();
    if (f < 0) { ui_toast(T("NO FREE PATTERN")); return -1; }
    int tr = like >= 0 ? ui_pattern_tracks((uint8_t)like) : 0, rows = like >= 0 ? ui_pattern_rows((uint8_t)like) : 0;
    uint8_t p[5] = {(uint8_t)(tr ? tr : 8), (uint8_t)((rows ? rows : 64) - 1), 1, 0, 0};
    if (ui_edit_put(REC_PATTERN, (uint8_t)f, 0, 1, p, sizeof p)) return -1;
    if (sess_nmade < 16) sess_made[sess_nmade++] = (uint8_t)f;
    return f;
}
static int clone_pattern(int src)
{
    int f = ui_free_pattern();
    if (f < 0) { ui_toast(T("NO FREE PATTERN")); return -1; }
    if (ed_copy_pattern((uint8_t)src, (uint8_t)f)) return -1;
    if (sess_nmade < 16) sess_made[sess_nmade++] = (uint8_t)f;
    return f;
}
enum { O_EDIT = 1, O_NEW, O_INSERT, O_DELETE, O_COPY, O_PASTE, O_CLONE, O_REVERT };
/* REVERT (specs/ui/saving.md#revert): the song as saved, its record whole (its order, name, properties, mixer and FX
 * setup; not its patterns: each is the EDITOR's to revert); what plays hears it at once */
static void song_revert_go(void)
{
    ui_edit_revert(REC_SONG, ui_cur.song, 0);
    if (order_mode) ui_edit_session_of(REC_SONG, ui_cur.song);     /* (its copy now the saved one) */
    refresh();
    if (!ui_song(ui_cur.song, &ps)) { ui_cur.song = nsongs ? nums[0] : 0; order_mode = 0; ui_edit_session(0); }
    pos = ui_clamp(pos, 0, ps.npos > 0 ? ps.npos - 1 : 0);
    song_changed(ui_cur.song);
}
static void song_revert_ask(void)
{
    static char q[32];
    ui_fmt(q, sizeof q, T("REVERT SONG %02X TO SAVED?"), ui_cur.song);
    ui_ask_revert(q, 0, song_revert_go);
}
static void editor_back(void) {}
static int edit_pat;                    /* EDIT PATTERN's, after the EDITOR's own pattern's warning (ed_own_ask) */
static void edit_go(void)
{
    ui_cur.pattern = (uint8_t)edit_pat;
    ui_go_deep(ui_screen_named("EDITOR"), editor_back, 0);
}
static void order_pick(int id)
{
    ui_song_t s;
    if (!ui_song(ui_cur.song, &s)) return;
    int here = s.npos ? s.order[pos] : -1;
    switch (id) {
    case O_EDIT: {
        if (!ui_screen_named("EDITOR")) { ui_toast(T("EDITOR: NOT IN THIS FIRMWARE YET")); return; }
        edit_pat = here;
        ed_own_ask(edit_go);
        return;
    }
    case O_NEW: {
        int f = new_pattern(here);
        if (f < 0) return;
        arg_pat = f;
        order_edit(o_after);
        ui_toast(ui_txt_v(T("PATTERN # MADE"), ui_hex2(f)));
        return;
    }
    case O_INSERT: order_edit(o_insert); return;
    case O_DELETE: order_edit(o_remove); return;
    case O_COPY: clip = here; ui_toast(ui_txt_v(T("PATTERN # COPIED"), ui_hex2(here))); return;
    case O_PASTE: arg_pat = clip; order_edit(o_after); return;
    case O_CLONE: {
        int f = clone_pattern(here);
        if (f < 0) return;
        arg_pat = f;
        order_edit(o_set);
        ui_toast(ui_txt_v(ui_txt_v(T("# CLONED TO #"), ui_hex2(here)), ui_hex2(f)));
        return;
    }
    case O_REVERT: song_revert_ask(); return;
    }
}
static void order_menu(void)
{
    ui_mitem_t items[8];
    int n = 0;
    ui_mitem_t it = {0, 0, 0, 0};
    ui_song_t s;
    int has = ui_song(ui_cur.song, &s) && s.npos > 0;
#define ADD(nm, x) do { it.name = (nm); it.id = (x); items[n++] = it; } while (0)
    if (has) ADD(TX("EDIT PATTERN"), O_EDIT);
    ADD(TX("NEW PATTERN"), O_NEW);
    ADD(TX("INSERT"), O_INSERT);
    if (has) { ADD(TX("DELETE"), O_DELETE); ADD(TX("COPY"), O_COPY); }
    if (clip >= 0) ADD(TX("PASTE"), O_PASTE);
    if (has) ADD(TX("CLONE PATTERN"), O_CLONE);
    if (ui_edit_unsaved(REC_SONG, ui_cur.song, 0)) ADD(TX("REVERT"), O_REVERT);
#undef ADD
    char t[24];
    ui_fmt(t, sizeof t, T("POSITION %02X"), pos);
    ui_menu("order", t, items, n, order_pick, B_SEL);
}
static int env_to;
static void env_go(void)
{
    ui_cur.pattern = (uint8_t)env_to;
    ui_toast(ui_txt_v(T("PATTERN # FOR THE EDITOR"), ui_hex2(env_to)));
}
static void env_pick(int pat)
{
    if (order_mode) { arg_pat = pat; order_edit(o_set); return; }
    env_to = pat;
    ed_own_ask(env_go);                 /* (the EDITOR's own pattern's warning first) */
}

/* ---- the screen ---- */
static void enter(const ui_screen_t *from) { (void)from; refresh(); }
static const char *page(void)
{
    static char b[12];
    int i = mv_from >= 0 ? mv_at : index_of(ui_cur.song);
    if (!nsongs || i < 0) return 0;
    ui_page_of(b, i, nsongs);
    return b;
}
static const char *sub(void) { return ui_cur.play_mode == 1 ? T("REPEAT") : ui_cur.play_mode == 2 ? T("NEXT") : 0; }
static int lit(int b)
{
    if (b == B_EDIT && order_mode) return LV_FULL;
    if (b == B_SEL && mv_from >= 0) return LV_FULL;
    if (b == B_SEQ) return ui_cur.play_mode ? LV_FULL : LV_DIM;
    return LV_NONE;
}
/* the play modes (specs/ui/screens/songs.md#the-play-modes): SEQ's ONE PLAY, REPEAT SONG, PLAY NEXT, the player's
 * PL_END_ONCE / _REPEAT / _NEXT, in hold from PLAY here until something else plays. PLAY NEXT's next song: the one
 * after `song` in the list with positions (0xFF after the last) */
static uint8_t song_after(uint8_t song)
{
    uint8_t ns[256];
    int n = ui_songs(ns, 256), i = 0;
    while (i < n && ns[i] != song) i++;
    for (int j = i + 1; j < n; j++) { ui_song_t s; if (ui_song(ns[j], &s) && s.npos) return ns[j]; }
    return 0xFF;
}
static void mode_to_player(uint8_t song) { player_end_mode((uint8_t)(PL_END_ONCE + ui_cur.play_mode), song_after(song)); }
/* the main loop, on every screen: PLAY NEXT's next song kept up for the player as each song starts (and every second,
 * the list may change); the cursor follows the song that plays, on SONGS' list (not in an order being edited, nor
 * with a song afloat) */
void ui_play_mode_poll(void)
{
    static uint8_t seen = 0xFF;
    static uint32_t at;
    player_state_t st;
    ui_player_state(&st);
    if (st.what != PL_SONG || st.end_mode != PL_END_NEXT) { seen = 0xFF; return; }
    if (st.song != seen || (int32_t)(ui_now() - at) >= 1000) {
        uint8_t nx = song_after(st.song);
        if (nx != st.end_next) player_end_mode(PL_END_NEXT, nx);
        if (seen != 0xFF && st.song != seen && !order_mode && mv_from < 0) { ui_cur.song = st.song; pos = 0; picked = 0; }
        seen = st.song;
        at = ui_now();
    }
}
static int sel_lines(const char **l, int max) { if (order_mode && max) { l[0] = T("K1: 8 PATTERNS AT A TIME"); return 1; } return 0; }
static void knob(int e, int s, int coarse)
{
    ui_song_t sg;
    int has = ui_song(ui_cur.song, &sg);
    if (!order_mode) {
        if (e == K_SELECT && mv_from >= 0) { mv_at = ui_clamp(mv_at + (s > 0 ? 1 : -1), 0, nsongs - 1); return; }
        if (e == K_SELECT && nsongs) {
            int i = index_of(ui_cur.song);
            int j = ui_clamp((i < 0 ? 0 : i) + s, 0, nsongs - 1);
            if (nums[j] != ui_cur.song) { ui_cur.song = nums[j]; pos = 0; picked = 0; }
        } else if (e == K_ALG && has && sg.npos) {
            pos = ui_clamp(start_pos() + s, 0, sg.npos - 1);
            picked = 1;
            ui_show_knob(T("PLAY FROM"), ui_txt_v(T("POS #"), ui_hex2(pos)));
        }
        return;
    }
    if (!has) return;
    if (e == K_SELECT) pos = ui_clamp(pos + s * 8, 0, sg.npos - 1);
    else if (e == K_ALG) pos = ui_clamp(pos + s, 0, sg.npos - 1);
    else if (e == K_K1 && sg.npos) {
        uint8_t pn[256];
        int n = store_list(REC_PATTERN, pn, 256), i = 0;
        while (n && pn[n - 1] >= 0xF0) n--;             /* (the built-in patterns are no song's) */
        for (int k = 0; k < n; k++) if (pn[k] == sg.order[pos]) i = k;
        if (!n) return;
        i = ui_clamp(i + s * (coarse ? 8 : 1), 0, n - 1);
        arg_pat = pn[i];
        order_edit(o_set);
        ui_show_knob(T("K1 PATTERN"), ui_hex2(pn[i]));
    }
}
static int play_down(void)
{
    player_state_t st;
    ui_player_state(&st);
    if (st.playing && playing_here(&st) && (!picked || order_mode)) { player_pause(); return 2; }
    int from = picked && !order_mode ? start_pos() : -1;
    picked = 0;
    mode_to_player(ui_cur.song);                        /* (the play mode takes hold: PLAY starts the song here) */
    ui_play_song(ui_cur.song, from);
    return 2;
}
static int can_play(void) { ui_song_t s; return ui_song(ui_cur.song, &s) && s.npos > 0; }
static int tap(int b)
{
    if (mv_from >= 0) { if (b == B_SEL) put_down(); return 1; }     /* a song afloat: only SEL (and HOME) */
    if (b == B_EDIT) {
        if (order_mode) { order_keep(); return 1; }     /* keeping the changes */
        if (ui_song(ui_cur.song, &ps)) order_begin();
        return 1;
    }
    if (b == B_GLO) { if (ui_song(ui_cur.song, &ps)) props(); return 1; }
    if (b == B_SEQ) {                                   /* the play mode, round; at once on the songs playing */
        ui_cur.play_mode = (uint8_t)((ui_cur.play_mode + 1) % 3);
        ui_toast(ui_cur.play_mode == 1 ? T("REPEAT SONG") : ui_cur.play_mode == 2 ? T("PLAY NEXT") : T("ONE PLAY"));
        player_state_t st;
        ui_player_state(&st);
        if (st.what == PL_SONG) mode_to_player(st.song);
        return 1;
    }
    if (b == B_SEL) { if (order_mode) order_menu(); else list_menu(); return 1; }
    if (b == B_ENV) {
        ui_song_t s;
        int cur_pat = ui_song(ui_cur.song, &s) && s.npos ? s.order[order_mode ? pos : 0] : 0;
        ed_pattern_list(T("PATTERNS"), cur_pat, env_pick, B_ENV, -1);
        return 1;
    }
    if (b == B_LFO && order_mode) { order_edit(o_remove); return 1; }
    (void)not_yet;
    return 0;
}
static int back(void)
{
    if (mv_from >= 0) { mv_from = -1; ui_toast(T("NOT MOVED")); return 1; }
    if (order_mode) { order_drop(); return 1; }
    return 0;
}
static const char *footer(uint8_t *col)
{
    static char b[24];
    ui_song_t s;
    if (!ui_song(ui_cur.song, &s)) return 0;
    char t[12];
    ui_fmt_time(t, ui_song_secs(&s));
    ui_fmt(b, sizeof b, "%d BPM %s", ui_song_bpm(&s), t);
    *col = C_LGREY;
    return b;
}
/* the list with the song afloat shown at mv_at */
static uint8_t listed(int i)
{
    if (mv_from < 0) return nums[i];
    if (i == mv_at) return nums[mv_from];
    int k = i;
    if (mv_from < mv_at && i >= mv_from && i < mv_at) k = i + 1;
    if (mv_from > mv_at && i > mv_at && i <= mv_from) k = i - 1;
    return nums[k];
}
static void draw(void)
{
    int y0 = BODY_Y;
    player_state_t st;
    ui_player_state(&st);
    int ci = mv_from >= 0 ? mv_at : index_of(ui_cur.song);
    if (ci < 0) ci = 0;
    int top = ci - (LIST >> 1) < nsongs - LIST ? ci - (LIST >> 1) : nsongs - LIST;
    if (top < 0) top = 0;
    g_text(8, y0 + 2, &FONT_T, T("NR"), C_CYAN);       /* (the column names, each over its column) */
    g_text(26, y0 + 2, &FONT_T, T("NAME"), C_CYAN);
    g_text(132, y0 + 2, &FONT_T, T("BPM"), C_CYAN);
    g_text(156, y0 + 2, &FONT_T, T("LEN"), C_CYAN);     /* (the positions: its length) */
    g_text(186, y0 + 2, &FONT_T, T("TIME"), C_CYAN);
    for (int i = top; i < nsongs && i < top + LIST; i++) {
        ui_song_t s;
        uint8_t n = listed(i);
        if (!ui_song(n, &s)) continue;
        int y = y0 + 16 + (i - top) * 13, on = i == ci, is_playing = st.what == PL_SONG && st.song == n;
        if (on && !order_mode) g_box(0, y - 1, 240, 13, mv_from >= 0 ? C_YELLOW : C_WHITE);
        if (is_playing && st.playing) g_play(1, y + 1, C_GREEN);
        int col = on ? C_WHITE : C_LGREY;
        g_text(8, y, &FONT_T, ui_hex2(mv_from >= 0 ? nums[i] : n), on ? C_WHITE : C_DGREY);
        g_text(26, y, &FONT_T, s.name, is_playing && st.playing ? C_GREEN : col);   /* (green while it plays) */
        char v[12];
        ui_fmt(v, sizeof v, "%3d", ui_song_bpm(&s));
        g_text(132, y, &FONT_T, v, col);
        ui_fmt(v, sizeof v, "%3d", s.npos);
        g_text(156, y, &FONT_T, v, col);
        ui_fmt_time(v, ui_song_secs(&s));
        g_text(186, y, &FONT_T, v, col);
    }
    ui_song_t s;
    int y1 = y0 + 16 + LIST * 13 + 4;
    g_hline(0, y1 - 3, 240, C_DGREY);
    if (!ui_song(ui_cur.song, &s)) {
        if (!nsongs) g_text(0, y1, &FONT_T, T("NO SONGS: SEL MAKES ONE OR UPLOAD A DISK"), C_DGREY);
        return;
    }
    g_run_t r[3] = {{T("ORDER "), C_CYAN}, {s.name, C_WHITE}, {order_mode ? T("  (EDIT)") : "", C_LBLUE}};
    g_runs(0, y1, &FONT_T, r, 3);
    if (!order_mode) {
        g_run_t h[2] = {{T("SEQ"), C_YELLOW}, {ui_cur.play_mode == 1 ? T(" REPEAT SONG") : ui_cur.play_mode == 2 ? T(" PLAY NEXT") : T(" ONE PLAY"),
                                             ui_cur.play_mode ? C_GREEN : C_LGREY}};
        g_runs(240 - g_runs_w(&FONT_T, h, 2), y1, &FONT_T, h, 2);
    }
    int play_pos = st.playing && playing_here(&st) ? st.pos : -1;
    if (!order_mode && mv_from < 0) pos = start_pos();
    int first = pos / 8 - 3 > 0 ? pos / 8 - 3 : 0;
    for (int l = first; l < first + 5; l++) {
        int y = y1 + 14 + (l - first) * 13;
        if (l * 8 >= s.npos && l > 0) break;
        g_text(0, y, &FONT_T, ui_hex2(l * 8), C_DGREY);
        for (int k = 0; k < 8; k++) {
            int i = l * 8 + k;
            if (i >= s.npos) break;
            int x = 22 + k * 27, n = s.order[i], there = ui_pattern_rows((uint8_t)n) > 0;
            if (i == play_pos) g_rect(x - 3, y - 1, 24, 13, C_DEEP + C_BLUE);
            if (i == pos && mv_from < 0) g_box(x - 3, y - 1, 24, 13, C_WHITE);
            g_text(x, y, &FONT_T, ui_hex2(n), !there ? C_RED : order_mode && i == pos ? C_WHITE : i == play_pos ? C_LGREEN : C_LGREY);
        }
    }
    if (!s.npos) g_text(22, y1 + 14, &FONT_T, T("EMPTY: ENV ADDS A PATTERN"), C_DGREY);
}

/* the state's cursor (specs/connections.md#the-state): the current song, the position PLAY starts from */
static void cursor(uint8_t *c) { c[0] = ui_cur.song; c[1] = (uint8_t)start_pos(); }
/* what it edits (specs/ui/screens/editor.md): the song's properties open, it plays at them, its Fxx skipped */
static void editing(uint8_t *what, uint32_t *key)
{
    const ui_overlay_t *o = ui_top();
    if (!o || o->kind != OV_PROPS) return;
    *what = PL_ED_TIME;
    *key = ui_cur.song;
}

const ui_screen_t SCR_SONGS = {
    .name = TX("SONGS"), .group = G_MAKE,
    .does = BIT(B_EDIT) | BIT(B_SEL) | BIT(B_ENV) | BIT(B_GLO) | BIT(B_LFO) | BIT(B_SEQ),
    .enter = enter, .draw = draw, .sub = sub, .page = page, .footer = footer, .knob = knob, .tap = tap,
    .back = back, .lit = lit, .sel_lines = sel_lines, .play_down = play_down, .can_play = can_play,
    .cursor = cursor, .editing = editing,
};
