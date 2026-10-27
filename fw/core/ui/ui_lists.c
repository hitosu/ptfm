/* SPDX-License-Identifier: GPL-3.0-only
 * Copyright (C) 2026 Vasily Kazantsev */
/* The lists screens share: the song list (specs/ui/screens/status.md, playback.md). */
#include <string.h>
#include "ui_core.h"
#include "ui_data.h"
#include "../play/player.h"
#include "../store/store.h"

/* (the pattern list is ed_plist.c's, one for every screen) */
typedef struct { uint8_t nums[256]; int n, sel; char title[24]; void (*pick)(int); void (*play)(int); } list_t;
static list_t sl;
static void sl_knob(ui_overlay_t *o, int e, int s, int c)
{
    (void)o; (void)c;
    if (e == K_SELECT && sl.n) sl.sel = ui_clamp(sl.sel + s, 0, sl.n - 1);
}
static int sl_tap(ui_overlay_t *o, int b)
{
    if (b != o->opener) return 0;
    void (*pick)(int) = sl.pick;
    int song = sl.n ? sl.nums[sl.sel] : -1;
    ui_close(o);
    if (pick && song >= 0) pick(song);
    return 1;
}
static void sl_play(ui_overlay_t *o)
{
    void (*play)(int) = sl.play;
    int song = sl.n ? sl.nums[sl.sel] : -1;
    ui_close(o);
    if (play && song >= 0) play(song);
}
static void sl_draw(ui_overlay_t *o)
{
    (void)o;
    int y = BODY_Y + 2, rows = 13;
    player_state_t st;
    ui_player_state(&st);
    int playing = st.playing && st.what == PL_SONG ? st.song : -1;
    ui_panel(2, y, 236, 206, sl.title, C_CYAN);
    g_text(8, y + 16, &FONT_T, T("NR"), C_DGREY);      /* (the column heads, each over its column) */
    g_text(26, y + 16, &FONT_T, T("NAME"), C_DGREY);
    g_text(152 - g_tw(&FONT_T, T("BPM")), y + 16, &FONT_T, T("BPM"), C_DGREY);
    g_text(164, y + 16, &FONT_T, T("TIME"), C_DGREY);
    int top = sl.sel - 6 < sl.n - rows ? sl.sel - 6 : sl.n - rows;
    if (top < 0) top = 0;
    for (int i = top; i < sl.n && i < top + rows; i++) {
        ui_song_t s;
        if (!ui_song(sl.nums[i], &s)) continue;
        int yy = y + 30 + (i - top) * 13, on = i == sl.sel, col = on ? C_WHITE : sl.nums[i] == playing ? C_GREEN : C_LGREY;
        if (on) g_box(4, yy - 1, 232, 13, C_WHITE);
        g_text(8, yy, &FONT_T, ui_hex2(sl.nums[i]), col);
        g_text(26, yy, &FONT_T, s.name, col);
        const char *bpm = ui_dec(ui_song_bpm(&s));
        g_text(152 - g_tw(&FONT_T, bpm), yy, &FONT_T, bpm, on ? C_WHITE : C_CYAN);
        char t[12];
        ui_fmt_time(t, ui_song_secs(&s));
        g_text(164, yy, &FONT_T, t, col);
    }
}
void ui_song_list(const char *title, int current, void (*pick)(int song), void (*play)(int song))
{
    sl.n = ui_songs(sl.nums, 256);
    sl.sel = 0;
    for (int i = 0; i < sl.n; i++) if (sl.nums[i] == current) sl.sel = i;
    strncpy(sl.title, title, sizeof sl.title - 1);
    sl.pick = pick;
    sl.play = play;
    ui_overlay_t o = {.kind = OV_LIST, .opener = B_ENV, .knob = sl_knob, .tap = sl_tap, .draw = sl_draw,
                      .play = play ? sl_play : 0};
    ui_open(&o);
}
