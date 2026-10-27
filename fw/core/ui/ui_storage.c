/* SPDX-License-Identifier: GPL-3.0-only
 * Copyright (C) 2026 Vasily Kazantsev */
/* CHECK STORAGE (ui_storage.h): a progress bar while the store reads and checks every record, then what failed
 * and mattered, as a list. */
#include <string.h>
#include "ui_core.h"
#include "ui_data.h"
#include "ui_storage.h"
#include "txt.h"
#include "gfx.h"
#include "../store/store.h"

#define PROB_MAX 24
static store_problem_t prob[PROB_MAX];
static int nprob, ptop;

static void prob_text(const store_problem_t *p, char *s, int max)
{
    char what[20];
    switch (p->kind) {
    case REC_SONG: ui_fmt(what, sizeof what, T("SONG %02X"), p->num); break;
    case REC_PATTERN: ui_fmt(what, sizeof what, T("PATTERN %02X"), p->num); break;
    case REC_AUDIO: ui_fmt(what, sizeof what, T("SAMPLE %02X:%02X"), p->num, p->sub); break;
    case REC_LOOPS: ui_fmt(what, sizeof what, "%s", T("LOOPS")); break;
    case REC_DJ: ui_fmt(what, sizeof what, "%s", T("DJ")); break;
    case REC_SETTINGS: ui_fmt(what, sizeof what, "%s", T("SETTINGS")); break;
    case REC_MIDIMAP: ui_fmt(what, sizeof what, "%s", T("MIDI MAPPINGS")); break;
    default: ui_fmt(what, sizeof what, T("CARTRIDGE %02X"), p->num); break;
    }
    ui_fmt(s, max, "%s%s", what,
           p->what == STORE_FELL_BACK ? T(": AN OLDER VERSION") : p->what == STORE_MISSING ? T(": MISSING") : T(": DAMAGED"));
}

static void pl_knob(ui_overlay_t *o, int e, int s, int c)
{
    (void)o; (void)c;
    int shown = nprob < PROB_MAX ? nprob : PROB_MAX;
    if (e == K_SELECT) ptop = ui_clamp(ptop + s, 0, shown - 14 > 0 ? shown - 14 : 0);
}
static int pl_tap(ui_overlay_t *o, int b) { (void)o; (void)b; return 0; }
static void pl_draw(ui_overlay_t *o)
{
    (void)o;
    char t[40];
    ui_fmt(t, sizeof t, "%s", ui_txt_v(T("STORAGE PROBLEMS: #"), ui_dec(nprob)));
    int y = BODY_Y + 2;
    ui_panel(2, y, 236, 206, t, C_CYAN);
    for (int i = 0; i < 14 && ptop + i < nprob && ptop + i < PROB_MAX; i++) {
        char s[48];
        prob_text(&prob[ptop + i], s, sizeof s);
        g_textn(8, y + 18 + i * 13, &FONT_T, s, 37, C_LGREY);
    }
}

static void checked(void)
{
    nprob = store_check(prob, PROB_MAX);
    if (!nprob) { ui_toast(T("STORAGE OK")); return; }
    ptop = 0;
    ui_overlay_t o = {.kind = OV_LIST, .opener = -1, .knob = pl_knob, .tap = pl_tap, .draw = pl_draw};
    ui_open(&o);
}

void ui_check_storage(void)
{
    ui_progress(T("CHECKING STORAGE"), 500, checked);
}
