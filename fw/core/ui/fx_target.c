/* SPDX-License-Identifier: GPL-3.0-only
 * Copyright (C) 2026 Vasily Kazantsev */
/* Whose FX setup the FX screen edits, and its mixer (fx_target.h). The loops' setup and mixer are changed
 * where LOOPS keeps them (play/loops.h), a song's and DJ's in a copy of their record. */
#include <string.h>
#include "ui_core.h"
#include "ui_data.h"
#include "fx_target.h"
#include "fxdef.h"
#include "../store/store.h"
#include "../play/player.h"
#include "../play/rec.h"
#include "../play/loops.h"
#include "../play/dj.h"
#include "../fx/fx.h"

#define REC_MAX 640                     /* a song's record (at most ~450 B, its mixer grown to 16 tracks) */
static uint8_t buf[REC_MAX];            /* a record being changed */
static char from_name[12];

void fxt_from(const char *screen)
{
    strncpy(from_name, screen ? screen : "", sizeof from_name - 1);
}
int fxt_get(fxt_t *t)
{
    player_state_t st;
    ui_player_state(&st);
    memset(t, 0, sizeof *t);
    /* (the loops playing or played last, stopped: what the keys hear; held for the keys on LOOPS: from there) */
    if ((st.what == PL_LOOPS && !st.lcue) || !strcmp(from_name, "LOOPS")) {
        t->what = FT_LOOPS; t->kind = REC_LOOPS;
        strcpy(t->name, T("THE LOOPS"));
        t->plays = st.what == PL_LOOPS;
    } else if ((st.what == PL_DJ && st.playing) || !strcmp(from_name, "DJ")) {
        t->what = FT_DJ; t->kind = REC_DJ;
        strcpy(t->name, T("DJ"));
        t->plays = st.what == PL_DJ;
    } else {
        /* (the song played last, stopped or not; nothing played since power-on or UNLOAD SONG: the keys' song) */
        uint8_t song = st.what == PL_SONG || st.what == PL_LOOP ? st.song : st.what == PL_NONE ? UI_KEYS_SONG : ui_cur.song;
        ui_song_t s;
        if (!ui_song(song, &s)) {                       /* (no song at all, or the song gone: the keys' song) */
            song = UI_KEYS_SONG;
            if (!ui_song(song, &s)) return 0;
        }
        t->what = FT_SONG; t->kind = REC_SONG; t->num = song;
        strcpy(t->name, s.name);
        /* (nothing played yet: the keys' song held all the same, its setup what the keys play through: player_cue) */
        t->plays = (st.what == PL_SONG || st.what == PL_LOOP || st.what == PL_NONE) && st.song == song;
    }
    return 1;
}

/* a song's record as stored, and its FX setup's offset in it */
static const uint8_t *record(const fxt_t *t, uint32_t *len, uint8_t *ver, int *off)
{
    const uint8_t *p = store_get(t->kind, t->num, t->sub, len, ver);
    rec_song_t s;
    if (t->what != FT_SONG || !p || rec_song(t->num, &s)) return 0;
    *off = (int)(s.fx - p);
    return p;
}
int fxt_setup(const fxt_t *t, uint8_t *s64)
{
    uint32_t len;
    uint8_t ver;
    int off;
    if (t->what == FT_LOOPS) { memcpy(s64, loops_fx(), FX_SETUP_LEN); return 1; }
    if (t->what == FT_DJ) { dj_ensure(); memcpy(s64, DJ_FX, FX_SETUP_LEN); return 1; }
    const uint8_t *p = t->what ? record(t, &len, &ver, &off) : 0;
    if (!p) { fxd_default(s64); return 0; }
    memcpy(s64, p + off, FX_SETUP_LEN);
    return 1;
}
int fxt_now(const fxt_t *t, uint8_t *s64)
{
    if (t->plays) { fx_now(s64); return 1; }
    fxt_setup(t, s64);
    return 0;
}
/* a change of the loops' (an edit: r its LP_* result, was the edit memory used before it), to the player when they
 * play */
static int loops_done(const fxt_t *t, int r, uint32_t was)
{
    if (ui_edit_loops(r, was)) return -2;
    if (t->plays) loops_changed(0);
    return 0;
}
int fxt_edit(const fxt_t *t, void (*fn)(uint8_t *s64, void *arg), void *arg, int button)
{
    uint32_t len;
    uint8_t ver;
    int off;
    if (t->what == FT_LOOPS) {                          /* LOOPS' head, where its FX setup is */
        uint8_t s[FX_SETUP_LEN];
        uint32_t was = ui_edit_used();
        memcpy(s, loops_fx(), FX_SETUP_LEN);
        fn(s, arg);
        return loops_done(t, loops_set_fx(s), was);
    }
    if (t->what == FT_DJ) {                             /* DJ's stand, where its FX setup is */
        dj_ensure();
        fn(DJ_FX, arg);
        return ui_edit_put(REC_DJ, 0, 0, 1, DJ_REC, DJ_REC_LEN) ? -2 : 0;
    }
    const uint8_t *p = t->what ? record(t, &len, &ver, &off) : 0;
    if (!p || len > REC_MAX) { ui_refuse(button); return -1; }
    if (p != buf) memcpy(buf, p, len);
    fn(buf + off, arg);
    return ui_edit_put(t->kind, t->num, t->sub, ver, buf, len) ? -2 : 0;
}

int fxt_time(const fxt_t *t, int *tempo, int *swing)
{
    uint32_t len;
    uint8_t ver;
    int off;
    const uint8_t *p = t->what == FT_SONG ? record(t, &len, &ver, &off) : 0;
    if (!p) return 0;
    *tempo = p[12] | p[13] << 8;
    *swing = p[16];
    return 1;
}
int fxt_set_time(const fxt_t *t, int tempo, int swing, int button)
{
    uint32_t len;
    uint8_t ver;
    int off;
    const uint8_t *p = t->what == FT_SONG ? record(t, &len, &ver, &off) : 0;
    if (!p || len > REC_MAX) { ui_refuse(button); return -1; }
    if (p != buf) memcpy(buf, p, len);
    buf[12] = (uint8_t)tempo;
    buf[13] = (uint8_t)(tempo >> 8);
    buf[16] = (uint8_t)swing;
    if (ui_edit_put(t->kind, t->num, t->sub, ver, buf, len)) return -2;
    player_song_changed(t->num);
    return 0;
}

/* ---------------------------------------------------------------- REVERT --- */
/* the setup as saved (the flash's record; with none, a new song's / the loops' / DJ's first): what REVERT brings back */
static void copy_fn(uint8_t *s64, void *arg) { memcpy(s64, arg, FX_SETUP_LEN); }
static void saved_setup(const fxt_t *t, uint8_t *s64)
{
    uint32_t len;
    uint8_t ver, items[12];
    const uint8_t *p = store_get_saved(t->kind, t->num, t->sub, &len, &ver);
    fxd_default(s64);
    if (t->what == FT_LOOPS) {                          /* (as LOOPS reads it: an all-0 or chainless one, mended) */
        int none = 1, n;
        if (!p || ver != LOOPS_REC_VER || len < 72u) return;
        for (int i = 8; i < 72 && none; i++) none = !p[i];
        if (none) return;
        uint8_t d[FX_SETUP_LEN];
        memcpy(d, s64, FX_SETUP_LEN);
        memcpy(s64, p + 8, FX_SETUP_LEN);
        n = fxd_chain(s64, items);
        for (int i = 0; i < n; i++) for (int j = 0; j < i; j++) if (items[i] == items[j]) { memcpy(s64, d, 7); return; }
        return;
    }
    if (t->what == FT_DJ) {
        uint8_t r[DJ_REC_LEN];
        if (p && len >= DJ_REC_LEN) memcpy(s64, p + 8, FX_SETUP_LEN);
        else { dj_record_default(r); memcpy(s64, r + 8, FX_SETUP_LEN); }
        return;
    }
    if (t->what == FT_SONG && p && len > 21u) {         /* (a song's: after its order and mixer) */
        uint32_t at = 21u + p[20] + 1u, n = at < len ? p[at] : 0u;
        if (at + 1u + 6u * n + FX_SETUP_LEN <= len) memcpy(s64, p + at + 1u + 6u * n, FX_SETUP_LEN);
    }
}
int fxt_unsaved(const fxt_t *t)
{
    uint8_t a[FX_SETUP_LEN], b[FX_SETUP_LEN];
    if (!t->what) return 0;
    fxt_setup(t, a);
    saved_setup(t, b);
    return memcmp(a, b, FX_SETUP_LEN) != 0;
}
int fxt_revert(const fxt_t *t)
{
    uint8_t s[FX_SETUP_LEN];
    saved_setup(t, s);
    return fxt_edit(t, copy_fn, s, B_SEL);
}

/* ---------------------------------------------------------------- the mixer --- */
int fxt_mix_cols(const fxt_t *t) { return t->what == FT_SONG ? 16 : t->what == FT_LOOPS ? loops_n() : 0; }
int fxt_mix_get(const fxt_t *t, int col, uint8_t *six)
{
    static const uint8_t DEF[6] = {0x7F, 0x80, 0, 0, 0, 0};
    memcpy(six, DEF, 6);
    if (t->what == FT_SONG) {
        rec_song_t s;
        if (rec_song(t->num, &s) || col < 0 || col > 15) return 0;
        memcpy(six, s.mix[col], 6);
        return 1;
    }
    if (t->what == FT_LOOPS && col >= 0 && col < loops_n()) { memcpy(six, loops_mix(col), 6); return 1; }
    return 0;
}
int fxt_mix_set(const fxt_t *t, int col, int k, int v, int button)
{
    uint32_t len;
    uint8_t ver;
    if (t->what == FT_LOOPS) {
        uint32_t was = ui_edit_used();
        if (col < 0 || col >= loops_n()) return -1;
        return loops_done(t, loops_set_mix(col, k, v), was);
    }
    const uint8_t *p = store_get(t->kind, t->num, t->sub, &len, &ver);
    if (t->what != FT_SONG || !p || col < 0 || col > 15) return -1;
    /* the song's mixer, all 16 tracks stored from now on (specs/files/songs.md): the record grows */
    uint32_t npos = (uint32_t)p[20] + 1u, mo = 21u + npos, n = p[mo], o = 0;
    if (mo + 1u + n * 6u > len || len - n * 6u + 16u * 6u > REC_MAX) return -1;
    memcpy(buf, p, mo);
    o = mo;
    buf[o++] = 16;
    for (uint32_t tr = 0; tr < 16; tr++) {
        static const uint8_t DEF[6] = {0x7F, 0x80, 0, 0, 0, 0};
        memcpy(buf + o, tr < n ? p + mo + 1u + tr * 6u : DEF, 6);
        o += 6;
    }
    buf[mo + 1u + (uint32_t)col * 6u + (uint32_t)k] = (uint8_t)v;
    uint32_t rest = len - (mo + 1u + n * 6u);
    memcpy(buf + o, p + mo + 1u + n * 6u, rest);
    o += rest;
    if (ui_edit_put(REC_SONG, t->num, 0, ver, buf, o)) return -2;
    player_song_changed(t->num);
    return 0;
}
int fxt_mix_track(const fxt_t *t, int col) { (void)t; return col; }   /* (loop i plays on track i) */
int fxt_mix_muted(const fxt_t *t, int col)
{
    if (t->what != FT_LOOPS) return ui_muted(col);
    return col < loops_n() ? (int)(loops_silent_mask() >> col & 1u) : 0;   /* (muted, or silenced by a solo) */
}
int fxt_tracks_used(const fxt_t *t)
{
    if (t->what != FT_SONG) return 16;
    ui_song_t s;
    int most = 0;
    if (!ui_song(t->num, &s)) return 0;
    for (int i = 0; i < s.npos; i++) {
        int n = ui_pattern_tracks(s.order[i]);
        if (n > most) most = n;
    }
    return most;
}
