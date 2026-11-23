/* SPDX-License-Identifier: GPL-3.0-only
 * Copyright (C) 2026 Vasily Kazantsev */
/* SETTINGS (specs/ui/screens/settings.md): the list, and its MIDI page (SEQ or ALGORITHM turn them). K1
 * changes the setting picked, LFO puts back its default, EDIT runs a tool or shows ABOUT. */
#include <string.h>
#include "ui_core.h"
#include "ui_priv.h"
#include "ui_storage.h"
#include "../midi/midi.h"
#include "../store/store.h"

#ifndef PTFM_VERSION
#define PTFM_VERSION "DEV"
#endif

static struct { int sel, page, msel; ui_overlay_t *learn; } set;

/* ---- the MIDI page's lines: its items, MIDI CONTROLLER, LEARN, then the mappings ---- */
#define M_CTL S_MIDI_COUNT
#define M_LEARN (S_MIDI_COUNT + 1)
static int m_lines(void) { return M_LEARN + 1 + midi_map_count(); }

/* LEARN's box (settings.md#learns-box): a question's panel without answers; midi.c takes the FM-1's input
 * meanwhile, HOME cancels, and the box closes once learning is over (tick) */
static void learn_draw(ui_overlay_t *o)
{
    (void)o;
    int step = midi_learning();
    char c[16];
    ui_panel(8, 78, 224, 88, 0, C_CYAN);
    g_text_c(8, 224, 86, &FONT_T, T("MIDI LEARN"), C_WHITE);
    if (step == 2) {
        midi_ctl_name(c, midi_learn_ctl());
        g_text_c(8, 224, 98, &FONT_T, c, C_YELLOW);
        g_text_c(8, 224, 110, &FONT_T, T("MOVE THE MIDI CONTROL"), C_LGREY);
        g_text_c(8, 224, 122, &FONT_T, T("A KNOB: TURN IT. A PAD: PRESS IT"), C_LGREY);
    } else {
        g_text_c(8, 224, 110, &FONT_T, T("TOUCH AN FM-1 CONTROL"), C_LGREY);
        g_text_c(8, 224, 122, &FONT_T, T("A KNOB, A BUTTON OR A KEY"), C_LGREY);
    }
    g_run_t k[2] = {{T("HOME"), C_YELLOW}, {T(": CANCEL"), C_LGREY}};
    g_runs(8 + (224 - g_runs_w(&FONT_T, k, 2)) / 2, 150, &FONT_T, k, 2);
}
static int learn_tap(ui_overlay_t *o, int b) { (void)o; (void)b; return 1; }
static void learn_knob(ui_overlay_t *o, int e, int s, int c) { (void)o; (void)e; (void)s; (void)c; }
static int learn_key(ui_overlay_t *o, int n, int down) { (void)o; (void)n; (void)down; return 1; }
static void learn_cancel(ui_overlay_t *o) { (void)o; midi_learn_cancel(); set.learn = 0; }
static void learn_open(void)
{
    if (midi_learn_start()) { ui_refuse_t(B_EDIT, TX("64 MAPPINGS: DELETE ONE FIRST")); return; }
    ui_overlay_t o = {.kind = OV_OTHER, .opener = B_EDIT, .knob = learn_knob, .tap = learn_tap, .key = learn_key,
                      .cancel = learn_cancel, .draw = learn_draw};
    set.learn = ui_open(&o);
}
static void tick(void)
{
    if (set.learn && !midi_learning()) {                /* learned (its toast says what), or HOME */
        if (ui_top() == set.learn) ui_close(set.learn);
        set.learn = 0;
        ui_dirty();
    }
}

static const char *sub(void) { return set.page ? T("MIDI") : 0; }
static const char *seq(void) { static char b[8]; ui_page_of(b, set.page, 2); return b; }
/* VOICES 16 picked while core 2 does not run: it counts as 12 till core 2 runs (specs/ui/screens/settings.md#voices) */
static void voices_told(int k)
{
    plat_cpu_t c;
    if (k != S_VOICES || ui_set[S_VOICES] != 2) return;
    plat_cpu(&c, 0);
    if (c.c2_state != PLAT_C2_ON) ui_toast_t(TX("16 VOICES: 12 WITHOUT CPU 2"));
}
static void change(const ui_setting_t *it, int16_t *v, int s, int k, int midi)
{
    int nv = it->list ? ui_clamp(*v + (s > 0 ? 1 : -1), 0, it->n - 1) : ui_clamp(*v + (s > 0 ? 1 : -1) * it->step, it->min, it->max);
    if (nv == *v) return;                               /* at an end: nothing changed, nothing unsaved */
    *v = (int16_t)nv;
    if (!midi && k == S_TOOLS) return;                  /* (picking a tool is no change) */
    ui_settings_changed();
    if (!midi) ui_settings_apply(k);
    if (!midi) voices_told(k);
    if (!midi && k == S_SUPPLIES && ui_set[S_SUPPLIES]) ui_toast_t(TX("LOWER FROM THE NEXT POWER-ON"));   /* (raised supplies
                                                         * are never lowered: a power-off does it) */
}
static void knob(int e, int s, int coarse)
{
    (void)coarse;
    if (e == K_ALG) { set.page = ui_clamp(set.page + (s > 0 ? 1 : -1), 0, 1); return; }
    if (set.page) {
        if (e == K_SELECT) set.msel = ui_clamp(set.msel + (s > 0 ? 1 : -1), 0, m_lines() - 1);
        else if (e == K_K1 && set.msel < S_MIDI_COUNT) change(&UI_MIDI_SETTINGS[set.msel], &ui_midi_set[set.msel], s, set.msel, 1);
        return;
    }
    if (e == K_SELECT) { set.sel = ui_clamp(set.sel + (s > 0 ? 1 : -1), 0, S_COUNT - 1); return; }
    if (e != K_K1 || set.sel == S_ABOUT) return;
    change(&UI_SETTINGS[set.sel], &ui_set[set.sel], s, set.sel, 0);
}
static const txt_t ABOUT[9] = {
    TX("PT/FM "), TX("POCKET TRACKER / FUSION MACHINE"), TX(""), TX("GPL-3.0-ONLY, (C) 2026 HITOSU"), TX(""),
    TX("FONTS: TERMINUS, SIL OFL 1.1"), TX("DX7 AFTER MSFA (APACHE-2.0)"), TX("JUNO-106 AFTER KR-106 (GPL-3.0)"),
    TX("TB-303 AFTER OPEN303 (MIT)")};
static void about_draw(ui_overlay_t *o)
{
    (void)o;
    char v[48];
    g_rect(0, BODY_Y, 240, BODY_H, C_BLACK);
    for (int i = 0; i < 9; i++) {
        const char *t = T(ABOUT[i]);
        if (i == 0) { ui_fmt(v, sizeof v, "%s%s", t, PTFM_VERSION); t = v; }   /* (the first line: and the version) */
        g_text(0, BODY_Y + 2 + i * 12, &FONT_T, t, i == 0 ? C_WHITE : !strncmp(t, "FONTS", 5) ? C_CYAN : C_LGREY);
    }
}
static int about_tap(ui_overlay_t *o, int b) { if (b == B_EDIT) ui_close(o); return 1; }
static void about_knob(ui_overlay_t *o, int e, int s, int c) { (void)o; (void)e; (void)s; (void)c; }
/* SEL's REVERT (settings.md#the-sel-menu): the page's own as saved, the settings record on the list, the MIDI map on
 * the MIDI page (midi.c reads it again as it is next used) */
static void revert_go(void)
{
    if (set.page) {
        ui_edit_revert(REC_MIDIMAP, 0, 0);
        set.msel = ui_clamp(set.msel, 0, m_lines() - 1);
    } else ui_edit_revert(REC_SETTINGS, 0, 0);      /* (applied again at once: ui_settings_reload) */
}
static void menu_pick(int id)
{
    (void)id;
    ui_ask_revert(set.page ? T("REVERT THE MIDI MAP TO SAVED?") : T("REVERT THE SETTINGS TO SAVED?"), 0, revert_go);
}
static int tap(int b)
{
    if (b == B_SEQ) { set.page = (set.page + 1) % 2; return 1; }
    if (b == B_SEL) {
        if (!ui_edit_unsaved(set.page ? REC_MIDIMAP : REC_SETTINGS, 0, 0)) { ui_refuse_t(B_SEL, TX("NOTHING TO REVERT")); return 1; }
        static const ui_mitem_t m[1] = {{TX("REVERT"), 1, 0, 0}};
        ui_menu("settings", set.page ? T("MIDI") : T("SETTINGS"), m, 1, menu_pick, B_SEL);
        return 1;
    }
    if (set.page) {
        if (b == B_LFO && set.msel < S_MIDI_COUNT) {
            if (ui_midi_set[set.msel] != UI_MIDI_SETTINGS[set.msel].def) { ui_midi_set[set.msel] = UI_MIDI_SETTINGS[set.msel].def; ui_settings_changed(); }
            return 1;
        }
        if (b == B_LFO && set.msel > M_LEARN) {         /* the mapping picked goes (SEL's REVERT brings it back) */
            midi_map_t m;
            char c[16], s[16], t[40];
            if (midi_map_at(set.msel - M_LEARN - 1, &m)) return 1;
            midi_ctl_name(c, m.ctl);
            midi_src_name(s, &m);
            if (midi_map_delete(set.msel - M_LEARN - 1)) { ui_toast_t(TX("MEMORY FOR EDITS IS FULL")); return 1; }
            ui_fmt(t, sizeof t, "%s: %s", c, ui_txt_t(TX("# DELETED"), s));
            ui_toast(t);
            set.msel = ui_clamp(set.msel, 0, m_lines() - 1);
            return 1;
        }
        if (b == B_EDIT && set.msel == M_LEARN) { learn_open(); return 1; }
        if (b == B_EDIT && set.msel == M_CTL) { ui_controller_ask(); return 1; }
        return b == B_EDIT;
    }
    if (b == B_LFO) {
        if (set.sel != S_TOOLS && set.sel != S_ABOUT && ui_set[set.sel] != UI_SETTINGS[set.sel].def) {
            ui_set[set.sel] = UI_SETTINGS[set.sel].def;
            ui_settings_changed();
            ui_settings_apply(set.sel);
            voices_told(set.sel);
        }
        return 1;
    }
    if (b == B_EDIT) {
        if (set.sel == S_TOOLS) {
            /* (TOOLS: CHECK REFERENCES, CHECK STORAGE, CLEAN UP, AUTO LEVEL ALL, REVERT ALL, FACTORY RESET) */
            if (ui_set[S_TOOLS] == 0) { ui_check_references(); return 1; }
            if (ui_set[S_TOOLS] == 1) { ui_check_storage(); return 1; }
            if (ui_set[S_TOOLS] == 2) { ui_clean_up(0); return 1; }
            if (ui_set[S_TOOLS] == 3) { ui_auto_level_all(-1, 0); return 1; }
            if (ui_set[S_TOOLS] == 4) ui_revert_all_ask();     /* REVERT ALL: SAVE + OCT-'s question */
            if (ui_set[S_TOOLS] == 5) ui_factory_reset_ask();
            return 1;
        }
        if (set.sel == S_ABOUT) {
            ui_overlay_t o = {.kind = OV_OTHER, .opener = B_EDIT, .knob = about_knob, .tap = about_tap, .draw = about_draw};
            ui_open(&o);
            return 1;
        }
    }
    return 0;
}
static const char *footer(uint8_t *col)
{
    static char b[40];
    char d[24];
    if (set.page) {
        if (set.msel == M_LEARN) { *col = C_LGREY; return T("EDIT: LEARN"); }
        if (set.msel == M_CTL) { *col = C_LGREY; return T("EDIT: SWITCH"); }
        if (set.msel > M_LEARN) { *col = C_LGREY; return T("LFO: DELETE"); }
        const ui_setting_t *it = &UI_MIDI_SETTINGS[set.msel];
        ui_setting_text(it, it->def, d);
        ui_fmt(b, sizeof b, T("DEFAULT %s"), d);
        *col = C_DGREY;
        return b;
    }
    if (set.sel == S_TOOLS || set.sel == S_ABOUT) { *col = C_LGREY; return T("EDIT: RUN"); }
    const ui_setting_t *it = &UI_SETTINGS[set.sel];
    ui_setting_text(it, it->def, d);
    ui_fmt(b, sizeof b, T("DEFAULT %s"), d);
    *col = C_DGREY;
    return b;
}
/* both pages in FONT_S (settings.md#layout): rows of 16 px from y 19, 12 lines in view, the selected row whole and kept 5
 * lines down while it can; names at 4, values right-aligned to 236, on the next line when the row passes 29
 * characters; a mapping two lines, its FM-1 control, then its MIDI control and kind */
#define SET_LINES 12
#define SET_LINE 29
/* an item, its name at 4 and its value right-aligned to 236 (on its row's second line when it has two) */
static void item_row(const ui_setting_t *it, int16_t val, int y, int k, int whole, int on, int col)
{
    char v[24];
    if (on) g_box(0, y - 1, 240, UI_ROW * k, C_WHITE);
    g_text(4, y, &FONT_S, T(it->name), on ? C_WHITE : C_CYAN);
    ui_setting_text(it, val, v);
    if (whole) g_text_r(236, y + (k - 1) * UI_ROW, &FONT_S, v, on ? C_WHITE : col);
}
static int item_nl(const ui_setting_t *it, int16_t val)
{
    char v[24];
    ui_setting_text(it, val, v);
    return ui_row_lines(T(it->name), v, SET_LINE);
}
static void list_row(int i, int y, int k, int whole, int on)
{
    const ui_setting_t *it = &UI_SETTINGS[i];
    item_row(it, ui_set[i], y, k, whole, on, i == S_THEME ? C_LMAGENTA : ui_set[i] == it->def ? C_LGREY : C_YELLOW);
}
static void midi_row(int i, int y, int k, int whole, int on)
{
    if (i < S_MIDI_COUNT) {
        const ui_setting_t *it = &UI_MIDI_SETTINGS[i];
        item_row(it, ui_midi_set[i], y, k, whole, on, ui_midi_set[i] == it->def ? C_LGREY : C_YELLOW);
        return;
    }
    if (on) g_box(0, y - 1, 240, UI_ROW * k, C_WHITE);
    if (i == M_CTL) {                                   /* (EDIT: the switch, after its questions) */
        g_text(4, y, &FONT_S, T("MIDI CONTROLLER"), on ? C_WHITE : C_CYAN);
        return;
    }
    if (i == M_LEARN) {
        g_text(4, y, &FONT_S, T("LEARN"), on ? C_WHITE : C_CYAN);
        g_text_r(236, y, &FONT_S, ui_txt_d(TX("# OF 64"), midi_map_count()), on ? C_WHITE : C_LGREY);
        return;
    }
    midi_map_t m;                                       /* a mapping: the FM-1's control; under it the MIDI one, its kind */
    char c[16], s[16];
    if (midi_map_at(i - M_LEARN - 1, &m)) return;
    midi_ctl_name(c, m.ctl);
    midi_src_name(s, &m);
    g_text(4, y, &FONT_S, c, on ? C_WHITE : C_YELLOW);
    if (!whole) return;                                 /* (its first line alone at the bottom) */
    g_run_t r[3] = {{s, on ? C_WHITE : C_LGREY}, {"  ", C_LGREY}, {midi_kind_name(m.kind), on ? C_WHITE : C_DGREY}};
    g_runs(236 - g_runs_w(&FONT_S, r, 3), y + UI_ROW, &FONT_S, r, 3);
}
typedef char settings_nl_fits[S_COUNT <= M_LEARN + 1 + MIDI_MAP_MAX ? 1 : -1];   /* (both pages' rows in draw's nl) */
static void draw(void)
{
    uint8_t nl[M_LEARN + 1 + MIDI_MAP_MAX];             /* (each row's lines, counted once a draw) */
    if (set.page) {
        /* the items (measured), MIDI CONTROLLER and LEARN (a line each), the mappings (two lines each) */
        int n = m_lines(), line;
        for (int i = 0; i < n; i++) nl[i] = (uint8_t)(i < S_MIDI_COUNT ? item_nl(&UI_MIDI_SETTINGS[i], ui_midi_set[i]) : i > M_LEARN ? 2 : 1);
        line = ui_rows_draw(n, set.msel, SET_LINES, 5, nl, BODY_Y + 2, midi_row);
        if (n == M_LEARN + 1) g_text(4, BODY_Y + 2 + line * UI_ROW, &FONT_S, T("NO MAPPINGS: EDIT ON LEARN"), C_DGREY);
        return;
    }
    for (int i = 0; i < S_COUNT; i++) nl[i] = (uint8_t)item_nl(&UI_SETTINGS[i], ui_set[i]);
    ui_rows_draw(S_COUNT, set.sel, SET_LINES, 5, nl, BODY_Y + 2, list_row);
}

/* the state's cursor (specs/connections.md#the-state): the page (the list, MIDI), the line, the tool TOOLS picks */
static void cursor(uint8_t *c)
{
    c[0] = (uint8_t)set.page;
    c[1] = (uint8_t)(set.page ? set.msel : set.sel);
    c[2] = (uint8_t)ui_set[S_TOOLS];
}

const ui_screen_t SCR_SETTINGS = {
    .name = TX("SETTINGS"), .group = G_SYSTEM,
    .does = BIT(B_LFO) | BIT(B_EDIT) | BIT(B_SEQ) | BIT(B_SEL),
    .draw = draw, .sub = sub, .seq = seq, .footer = footer, .knob = knob, .tap = tap, .tick = tick,
    .cursor = cursor,
};
