/* SPDX-License-Identifier: GPL-3.0-only
 * Copyright (C) 2026 Vasily Kazantsev */
/* PLAYBACK (specs/ui/screens/playback.md): what plays, followed, not editable: the playing pattern in the
 * grid of 8, 4, 2 or 1 tracks (SEQ, K2), the cursor track (ALGORITHM), positions (K1). */
#include <string.h>
#include "ui_core.h"
#include "ui_priv.h"
#include "ui_grid.h"
#include "ui_data.h"
#include "ui_ed.h"
#include "../play/player.h"
#include "../store/store.h"

static struct { int view, left, track; } pb = {8, 0, 0};
typedef struct { int pat, row, pos, npos; uint8_t song; } where_t;
/* what PLAYBACK shows: what plays (or paused), else the current song's first position */
static int where(where_t *w)
{
    player_state_t st;
    ui_player_state(&st);
    ui_song_t s;
    if (st.what == PL_SONG || st.what == PL_LOOP) {
        w->pat = st.pat; w->row = st.row; w->song = st.song;
        w->pos = st.what == PL_SONG ? st.pos : -1;
        w->npos = st.what == PL_SONG && ui_song(st.song, &s) ? s.npos : 0;
        return 1;
    }
    if (!ui_song(ui_cur.song, &s) || !s.npos) return 0;
    w->pat = s.order[0]; w->row = 0; w->pos = 0; w->npos = s.npos; w->song = ui_cur.song;
    return 1;
}
static const char *view_word(void)
{
    static char b[12];
    if (pb.view == 1) return T("1 TRACK");
    ui_fmt(b, sizeof b, T("%d TRACKS"), pb.view);
    return b;
}
static const char *seq(void) { return view_word(); }
static const char *page(void)
{
    static char b[16];
    where_t w;
    if (!where(&w)) return 0;
    if (w.pos < 0 && w.pat == UI_EDITOR_PATTERN) return T("EDITOR");          /* (its own: no number) */
    if (w.pos < 0) ui_fmt(b, sizeof b, T("PAT %02X"), w.pat);
    else ui_fmt(b, sizeof b, "%02X/%02X", w.pos, w.npos - 1 > 0 ? w.npos - 1 : 0);
    return b;
}
/* K4 the playback speed, K3 the songs' volume (specs/ui/screens/playback.md): the speed a share of each song's own
 * tempo, a DJ deck's way, turned in BPM, 3 to 600 (ui_tempo_range; player_tempo_share); the volume dB on each song's own level,
 * its voices alone, DJ's VOLUME's steps (player_song_volume); both heard at once, kept through other songs, 100 % and
 * 0 dB again when anything else plays, not stored. LFO resets the one turned last. What was set last is what to go on
 * from while the player may not have it yet (its snapshot is a half behind) */
#define VOL_OFF (-25)                                   /* (DJ's: under -24 dB) */
static struct { uint32_t share, share_at, db_at; int db; uint8_t last; } spd = {65536u, 0, 0, 0, K_K4};
static int own10(const ui_song_t *s)                    /* the song's own tempo, BPM x 10 */
{
    int d = (s->ticks ? s->ticks : 6) * (s->rpb ? s->rpb : 4);
    return (s->tempo * 2400 / d + 5) / 10;
}
static int recent(uint32_t at) { return at && (int32_t)(ui_now() - at) < 100; }   /* (| 1: maybe 1 ms ahead) */
static uint32_t share_now(void)
{
    player_state_t st;
    ui_player_state(&st);
    return recent(spd.share_at) ? spd.share : st.tempo_share ? st.tempo_share : 65536u;
}
static int db_now(void)
{
    player_state_t st;
    ui_player_state(&st);
    return recent(spd.db_at) ? spd.db : st.vol_db;
}
static void set_share(uint32_t share, const ui_song_t *sg)
{
    player_tempo_share(share);
    spd.share = share;
    spd.share_at = ui_now() | 1;
    spd.last = K_K4;
    int own = own10(sg), now = (int)(((uint32_t)own * share + 32768u) >> 16), c = own ? (now - own) * 1000 / own : 0;
    char v[28];                                         /* `124.0 BPM -3.2%` (DJ's words: the change x 0.1 %) */
    ui_fmt(v, sizeof v, "%d.%d BPM %c%d.%d%%", now / 10, now % 10, c < 0 ? '-' : '+', (c < 0 ? -c : c) / 10, (c < 0 ? -c : c) % 10);
    ui_show_knob(T("K4 TEMPO"), v);
}
static void set_db(int db)
{
    player_song_volume(db);
    spd.db = db;
    spd.db_at = ui_now() | 1;
    spd.last = K_K3;
    char v[12];                                         /* `-3 DB`, `0 DB`, `+2 DB`, `OFF` (DJ's VOLUME's words) */
    if (db <= VOL_OFF) ui_fmt(v, sizeof v, "%s", T("OFF"));
    else ui_fmt(v, sizeof v, "%s%d DB", db > 0 ? "+" : "", db);
    ui_show_knob(T("K3 VOLUME"), v);
}
static void knob(int e, int s, int coarse)
{
    where_t w;
    int has = where(&w);
    if (e == K_ALG) {
        int nt = has ? ui_pattern_tracks((uint8_t)w.pat) : 0;
        if (nt) pb.track = ui_clamp(pb.track + (s > 0 ? 1 : -1), 0, nt - 1);
        return;
    }
    if (e == K_K2) {                                    /* the view: 1, 2, 4, 8 tracks, clockwise more */
        static const int V[4] = {1, 2, 4, 8};
        int i = 0;
        for (int k = 0; k < 4; k++) if (V[k] == pb.view) i = k;
        pb.view = V[ui_clamp(i + (s > 0 ? 1 : -1), 0, 3)];
        ui_show_knob(T("K2 VIEW"), view_word());
        return;
    }
    if (e == K_K4 && has && w.pos >= 0) {               /* the playback speed: 1 BPM a click (SEL held: 10) */
        ui_song_t sg;
        int own;
        if (!ui_song(w.song, &sg) || (own = own10(&sg)) <= 0) return;
        int now = (int)(((uint32_t)own * share_now() + 32768u) >> 16), lo, hi;
        ui_tempo_range(&sg, &lo, &hi);
        now = ui_clamp(now + s * (coarse ? 100 : 10), lo, hi);
        set_share(((uint32_t)now << 16) / (uint32_t)own, &sg);
        return;
    }
    if (e == K_K3 && has && w.pos >= 0) {               /* the songs' volume: a dB a click (SEL held: 6) */
        set_db(ui_clamp(db_now() + s * (coarse ? 6 : 1), VOL_OFF, 6));
        return;
    }
    if (e == K_SELECT) { player_step(s > 0 ? 1 : -1); return; }   /* a row at a time, the row holding meanwhile */
    if (e == K_K1 && has && w.pos >= 0) {               /* another position: playing, when the row ends */
        player_skip(s > 0 ? 1 : -1);
        int to = ui_clamp(w.pos + (s > 0 ? 1 : -1), 0, w.npos - 1);
        ui_song_t sg;
        char v[32];
        if (ui_song(w.song, &sg)) {
            ui_fmt(v, sizeof v, T("%02X/%02X PATTERN %02X"), to, w.npos - 1, sg.order[to]);
            ui_show_knob(T("K1 POSITION"), v);
        }
    }
}
static const char *p_scroll(void) { return T(UI_SETTINGS[S_PLAYBACK_SCROLL].list[ui_set[S_PLAYBACK_SCROLL]]); }
static void p_scroll_change(int d, int c)
{
    (void)d; (void)c;
    ui_set[S_PLAYBACK_SCROLL] = !ui_set[S_PLAYBACK_SCROLL];
    ui_settings_changed();
}
static const char *p_unload(void)                       /* (what is held: the song's name, THE LOOPS, DJ) */
{
    static ui_song_t s;
    player_state_t st;
    ui_player_state(&st);
    if (st.what == PL_LOOPS) return T("THE LOOPS");
    if (st.what == PL_DJ) return T("DJ");
    return ui_song(st.song, &s) ? s.name : "";
}
static void p_unload_edit(void) { ui_close(0); ui_unload_song(); }   /* (EDIT on it: done, the panel closed) */
static void list_play(int song) { ui_cur.song = (uint8_t)song; ui_play_song((uint8_t)song, 0); }
static int edit_pat;                                    /* EDIT: the EDITOR on the playing pattern (after the EDITOR's own
                                                         * pattern's warning, ed_own_ask) */
static void edit_go(void)
{
    ui_cur.pattern = (uint8_t)edit_pat;
    ui_go_deep(ui_screen_named("EDITOR"), 0, 0);
}
static int tap(int b)
{
    if (b == B_SEQ) {                                   /* 8, 4, 2, 1 tracks, round */
        pb.view = pb.view == 8 ? 4 : pb.view == 4 ? 2 : pb.view == 2 ? 1 : 8;
        ui_toast(view_word());
        return 1;
    }
    if (b == B_GLO) {
        const ui_prop_t items[2] = {{TX("SCROLL"), p_scroll, p_scroll_change, 0, 0, 0},
                                    {TX("UNLOAD SONG"), p_unload, 0, p_unload_edit, 0, 0}};
        ui_props(T("PLAYBACK VIEW"), items, 1 + ui_song_held(), B_GLO);   /* (UNLOAD SONG while a song is held) */
        return 1;
    }
    if (b == B_EDIT) {
        where_t w;
        if (!where(&w)) return 1;
        if (!ui_screen_named("EDITOR")) { ui_toast(T("EDITOR: NOT IN THIS FIRMWARE YET")); return 1; }
        edit_pat = w.pat;
        if (w.pat == UI_EDITOR_PATTERN) edit_go();     /* (its own playing: nothing replaced) */
        else ed_own_ask(edit_go);
        return 1;
    }
    if (b == B_ENV) {
        player_state_t st;
        ui_player_state(&st);
        ui_song_list(T("SONGS"), st.what == PL_SONG ? st.song : ui_cur.song, 0, list_play);
        return 1;
    }
    if (b == B_LFO) {                                   /* the knob turned last back: the speed 100 %, the volume 0 dB */
        where_t w;
        ui_song_t sg;
        if (!where(&w) || w.pos < 0 || !ui_song(w.song, &sg)) return 1;
        if (spd.last == K_K3) set_db(0);
        else set_share(65536u, &sg);
        return 1;
    }
    return 0;
}
static const char *footer(uint8_t *col)
{
    static char b[24];
    where_t w;
    if (!where(&w)) return 0;
    if (w.pos < 0 && w.pat == UI_EDITOR_PATTERN) ui_fmt(b, sizeof b, T("ROW %02X"), w.row);
    else if (w.pos < 0) ui_fmt(b, sizeof b, T("PAT %02X ROW %02X"), w.pat, w.row);
    else ui_fmt(b, sizeof b, T("POS %02X ROW %02X"), w.pos, w.row);
    *col = C_LGREY;
    return b;
}
static void draw(void)
{
    where_t w;
    int rows_n;
    if (!where(&w) || !(rows_n = ui_pattern_rows((uint8_t)w.pat))) {
        g_text(0, BODY_Y + 40, &FONT_T, T("NOTHING HAS PLAYED: PLAY STARTS THE SONG"), C_DGREY);
        return;
    }
    const ui_view_t *v = ui_view(pb.view);
    int rows = v->rows, by_rows = ui_set[S_PLAYBACK_SCROLL] == 0;
    int top = by_rows ? w.row - (rows >> 1) : w.row / rows * rows;
    top = ui_clamp(top, by_rows ? -(rows >> 1) : 0, rows_n - 1 > 0 ? rows_n - 1 : 0);
    if (top < 0) top = 0;
    int nt = ui_pattern_tracks((uint8_t)w.pat);
    pb.track = ui_clamp(pb.track, 0, nt - 1 > 0 ? nt - 1 : 0);
    if (pb.track < pb.left) pb.left = pb.track;
    if (pb.track >= pb.left + pb.view) pb.left = pb.track - pb.view + 1;
    pb.left = ui_clamp(pb.left, 0, nt - pb.view > 0 ? nt - pb.view : 0);
    ui_grid_t g;
    ui_grid_init(&g);
    ui_song_t s;
    g.view = pb.view; g.top = top; g.left = pb.left; g.play = w.row; g.col = pb.track;
    g.rpb = ui_song(w.song, &s) ? s.rpb : 4;
    ui_grid((uint8_t)w.pat, &g);
}

const ui_screen_t SCR_PLAYBACK = {
    .name = TX("PLAYBACK"), .group = G_PLAY,
    .does = BIT(B_EDIT) | BIT(B_GLO) | BIT(B_SEQ) | BIT(B_ENV) | BIT(B_LFO),
    .draw = draw, .seq = seq, .page = page, .footer = footer, .knob = knob, .tap = tap,
};
