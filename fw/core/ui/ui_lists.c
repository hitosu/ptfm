/* SPDX-License-Identifier: GPL-3.0-only
 * Copyright (C) 2026 Vasily Kazantsev */
/* The lists screens share: the song list (specs/ui/screens/status.md, playback.md), the cartridge list's panel. */
#include <string.h>
#include "ui_core.h"
#include "ui_data.h"
#include "ed_inst.h"
#include "../play/player.h"
#include "../store/store.h"

/* ---- the cartridge list's panel (specs/ui/dialogs.md#the-cartridge-list), one for every list of cartridges ---- */
static uint8_t cart_bits[32];
int ui_cart_nums_fill(void)
{
    uint8_t nums[256];
    int n = ui_carts(nums, 256);
    memset(cart_bits, 0, sizeof cart_bits);
    for (int i = 0; i < n; i++) cart_bits[nums[i] >> 3] |= (uint8_t)(1u << (nums[i] & 7));
    return n;
}
int ui_cart_nums(uint8_t *nums)
{
    int n = 0;
    for (int c = 0; c < 256; c++) if (cart_bits[c >> 3] >> (c & 7) & 1) nums[n++] = (uint8_t)c;
    return ui_cart_order(nums, n);                      /* (the lists' order) */
}
/* the lines shown's slots used (two lookups of the cartridge a slot), kept while the store stays as it was */
#define CL_LINES 13
static struct { uint32_t gen; uint16_t ok; uint8_t who[CL_LINES], used[CL_LINES]; } clc;
static int cart_used(int line, uint8_t c)
{
    if (clc.gen != store_gen()) { clc.gen = store_gen(); clc.ok = 0; }
    if (!(clc.ok >> line & 1u) || clc.who[line] != c) {
        int used = 0, slots = ui_cart_slots(c);
        for (int s = 0; s < slots; s++) used += ui_slot_has((uint16_t)(c << 8 | s), 0);   /* (favourites, missing too) */
        clc.who[line] = c;
        clc.used[line] = (uint8_t)(used > 255 ? 255 : used);
        clc.ok |= (uint16_t)(1u << line);
    }
    return clc.used[line];
}
void ui_cart_lines(const char *title, const uint8_t *ids, const uint8_t *who, int n, int sel, int afloat)
{
    ui_cart_lines_more(title, ids, who, n, 0, sel, afloat);
}
int ui_cart_lines_more(const char *title, const uint8_t *ids, const uint8_t *who, int n, int more, int sel, int afloat)
{
    int y = BODY_Y + 2, all = n + more;
    ui_panel(2, y, 236, 206, title, C_CYAN);
    g_text(8, y + 16, &FONT_T, T("ID NAME         TYPE      SLOTS"), C_DGREY);
    int top = ui_clamp(sel - 6, 0, all - CL_LINES > 0 ? all - CL_LINES : 0);
    for (int i = top; i < n && i < top + CL_LINES; i++) {
        uint8_t c = who[i];
        int yy = y + 30 + (i - top) * 13, on = i == sel, k = ui_cart_kind(c);
        char nm[16], cnt[12];
        if (!k) continue;                               /* (gone meanwhile) */
        if (on) g_box(4, yy - 1, 232, 13, afloat ? C_YELLOW : C_WHITE);
        g_text(8, yy, &FONT_T, ui_hex2(ids[i]), ins_builtin(c) ? C_BROWN : on ? C_WHITE : C_LGREY);   /* (built in: brown) */
        if (!ui_cart_name(c, nm)) nm[0] = 0;
        nm[12] = 0;
        g_text(26, yy, &FONT_T, nm, on ? C_WHITE : C_LGREY);
        g_text(110, yy, &FONT_T, T(ENG_TYPE[k]), ENG_COL[k]);
        ui_fmt(cnt, sizeof cnt, "%03d/%03d", cart_used(i - top, c), ENG_SLOTS[k]);
        g_text(170, yy, &FONT_T, cnt, C_LGREY);
    }
    return more && n >= top && n < top + CL_LINES ? y + 30 + (n - top) * 13 : -1;
}
/* PRESETS' lists, big: the ID at 6 and the name at 36 in FONT_B (twice FONT_T: 7 lines of 26 px where FONT_T's list has
 * 13 of 13), the type in FONT_T right-aligned in its engine's colour, the name cut short only where it would reach it */
#define CL_BIG 7
void ui_cart_big(uint8_t c, int y, int on)
{
    int k = ui_cart_kind(c);
    const char *ty = T(ENG_TYPE[k]);
    int tx = 233 - g_tw(&FONT_T, ty), fit = (tx - 40) / 12;
    char nm[16];
    g_text(6, y, &FONT_B, ui_hex2(c), ins_builtin(c) ? C_BROWN : on ? C_WHITE : C_LGREY);   /* (built in: brown) */
    if (!ui_cart_name(c, nm)) nm[0] = 0;
    nm[fit < 12 ? fit : 12] = 0;
    g_text(36, y, &FONT_B, nm, on ? C_WHITE : C_LGREY);
    g_text(tx, y + 6, &FONT_T, ty, ENG_COL[k]);
}
void ui_cart_lines_big(const char *title, const uint8_t *who, int n, int sel)
{
    int y = BODY_Y + 2;
    ui_panel(2, y, 236, 206, title, C_CYAN);
    int top = ui_clamp(sel - 3, 0, n - CL_BIG > 0 ? n - CL_BIG : 0);
    for (int i = top; i < n && i < top + CL_BIG; i++) {
        int yy = y + 16 + (i - top) * 26;
        if (!ui_cart_kind(who[i])) continue;            /* (gone meanwhile) */
        if (i == sel) g_box(4, yy - 1, 232, 26, C_WHITE);
        ui_cart_big(who[i], yy, i == sel);
    }
}

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
