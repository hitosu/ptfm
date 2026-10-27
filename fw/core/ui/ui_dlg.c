/* SPDX-License-Identifier: GPL-3.0-only
 * Copyright (C) 2026 Vasily Kazantsev */
/* The dialogs (specs/ui/dialogs.md): menus with levels, questions, properties, text entry, a progress bar;
 * the help (help.md); HOME's combination's panel and the held buttons' layers. Every text a dialog shows
 * is copied in when it opens: a packed text's buffer does not last. */
#include <string.h>
#include "ui_core.h"
#include "ui_priv.h"
#include "../store/seed.h"
#include "../play/dj.h"
#include "ui_punch.h"

void ui_panel(int x, int y, int w, int h, const char *title, int col)
{
    g_rect(x, y, w, h, C_BLACK);
    g_box(x, y, w, h, C_LGREY);
    if (title) g_text(x + 4, y + 3, &FONT_T, title, col);
}
/* a panel's texts, kept while its lines wait to be drawn (a text T() unpacked lives in a buffer the next T()s take
 * again): a ring of bytes, a panel's lines ~600 at most */
static char keep_pool[768];
static int keep_at;
static const char *keep(const char *t)
{
    int n = (int)strlen(t);
    if (n > 47) n = 47;
    if (keep_at + n + 1 > (int)sizeof keep_pool) keep_at = 0;
    char *p = keep_pool + keep_at;
    memcpy(p, t, (size_t)n);
    p[n] = 0;
    keep_at += n + 1;
    return p;
}
void ui_key_line(g_run_t *runs, int *n, const char *line)
{
    static char keys[16][11];                           /* (a panel's keys: 15 lines at most; ALGORITHM the longest) */
    static int nk;
    line = keep(line);
    const char *k = strchr(line, ':');
    if (!k) { nk++; runs[0].s = line; runs[0].c = C_LGREY; *n = 1; return; }
    char *b = keys[nk++ % 16];
    int len = (int)(k - line) < 10 ? (int)(k - line) : 10;
    memcpy(b, line, (size_t)len);
    b[len] = 0;
    runs[0].s = b; runs[0].c = C_YELLOW;
    runs[1].s = k; runs[1].c = C_LGREY;
    *n = 2;
}

/* ------------------------------------------------------------------------ menus --- */
#define M_ITEMS 28                      /* the top's items and every group's, together */
#define M_GROUPS 4
/* an item: an action, or a group's head (group: its number); parent: the group it is in, -1 the top */
typedef struct { char name[20]; int16_t id; int8_t group, parent; } mi_t;
typedef struct {
    char key[12], title[28];
    mi_t it[M_ITEMS]; int n;
    int level;                          /* the group shown, or -1 */
    int sel;
    void (*pick)(int id);
} menu_t;
static menu_t menu_s;
/* each menu's last action, and each group's last used item, by the menu's key */
typedef struct { char key[12]; int8_t group; int16_t item; int16_t used[M_GROUPS]; } menu_mem_t;
static menu_mem_t mem[12];
static menu_mem_t *memory(const char *key)
{
    for (int i = 0; i < 12; i++) if (!strcmp(mem[i].key, key)) return &mem[i];
    for (int i = 0; i < 12; i++) if (!mem[i].key[0]) {
        strncpy(mem[i].key, key, sizeof mem[i].key - 1);
        mem[i].group = -1; mem[i].item = -1;
        for (int g = 0; g < M_GROUPS; g++) mem[i].used[g] = -1;
        return &mem[i];
    }
    return &mem[0];
}
static int m_count(const menu_t *m)
{
    int c = m->level >= 0;                              /* (BACK first in a group) */
    for (int i = 0; i < m->n; i++) if (m->it[i].parent == m->level) c++;
    return c;
}
/* the list shown: item i (BACK first in a group) */
static const mi_t *m_item(const menu_t *m, int i)
{
    static mi_t back = {"", -1, -1, -1};
    ui_fmt(back.name, sizeof back.name, "%s", T("BACK"));
    if (m->level >= 0) { if (!i) return &back; i--; }
    for (int k = 0; k < m->n; k++) if (m->it[k].parent == m->level && !i--) return &m->it[k];
    return &back;
}
static int m_head(const menu_t *m, int g)               /* a group's head, as the top lists it: its index there */
{
    int c = 0;
    for (int k = 0; k < m->n; k++) if (m->it[k].parent < 0) { if (m->it[k].group == g) return c; c++; }
    return 0;
}
static void menu_knob(ui_overlay_t *o, int e, int s, int c)
{
    (void)o; (void)c;
    if (e == K_SELECT) menu_s.sel = ui_clamp(menu_s.sel + s, 0, m_count(&menu_s) - 1);
}
static int menu_tap(ui_overlay_t *o, int b)
{
    menu_t *m = &menu_s;
    if (b != o->opener) return 0;
    menu_mem_t *mm = memory(m->key);
    const mi_t *it = m_item(m, m->sel);
    if (m->level >= 0 && m->sel == 0) {                 /* BACK: the level above, the group highlighted */
        m->sel = m_head(m, m->level);
        mm->group = -1; mm->item = (int16_t)(-100 - m->level);
        m->level = -1;
        return 1;
    }
    if (it->group >= 0 && m->level < 0) {
        int g = it->group, rem = mm->used[g];
        m->level = g;
        m->sel = 1;
        for (int i = 1; i < m_count(m); i++) if (m_item(m, i)->id == rem) m->sel = i;
        return 1;
    }
    int id = it->id;
    if (m->level >= 0) mm->used[m->level] = (int16_t)id;
    mm->group = (int8_t)m->level;
    mm->item = (int16_t)id;
    void (*pick)(int) = m->pick;
    ui_close(o);
    pick(id);
    return 1;
}
static void menu_draw(ui_overlay_t *o)
{
    (void)o;
    menu_t *m = &menu_s;
    int n = m_count(m), w = 150, x = 240 - w - 6, y = BODY_Y + 4, h = (n < 15 ? n : 15) * 12 + 22;
    char t[48];
    if (m->level >= 0) {
        const char *g = 0;
        for (int i = 0; i < m->n; i++) if (m->it[i].parent < 0 && m->it[i].group == m->level) g = m->it[i].name;
        ui_fmt(t, sizeof t, "%s > %s", m->title, g ? g : "");
        if (strlen(t) > 23) ui_fmt(t, sizeof t, "%s", g ? g : "");
    } else ui_fmt(t, sizeof t, "%s", m->title);
    ui_panel(x, y, w, h, t, C_CYAN);
    int top = m->sel - 7 < n - 15 ? m->sel - 7 : n - 15;
    if (top < 0) top = 0;
    for (int i = top; i < n && i < top + 15; i++) {
        int yy = y + 18 + (i - top) * 12, on = i == m->sel;
        const mi_t *it = m_item(m, i);
        if (on) { g_rect(x + 2, yy - 1, w - 4, 12, C_DEEP + C_BLUE); g_box(x + 2, yy - 1, w - 4, 12, C_WHITE); }
        if (m->level >= 0 && i == 0) {
            if (!on) g_hline(x + 4, yy + 10, w - 8, C_DGREY);
            g_text(x + 6, yy, &FONT_T, "<", on ? C_WHITE : C_CYAN);
            g_text(x + 18, yy, &FONT_T, T("BACK"), on ? C_WHITE : C_CYAN);
            continue;
        }
        g_text(x + 6, yy, &FONT_T, it->name, on ? C_WHITE : C_LGREY);
        if (it->group >= 0 && m->level < 0) g_text(x + w - 12, yy, &FONT_T, ">", on ? C_WHITE : C_LGREY);
    }
}
void ui_menu(const char *key, const char *title, const ui_mitem_t *items, int n, void (*pick)(int id), int opener)
{
    menu_t *m = &menu_s;
    memset(m, 0, sizeof *m);
    strncpy(m->key, key, sizeof m->key - 1);
    strncpy(m->title, title, sizeof m->title - 1);
    m->pick = pick;
    m->level = -1;
    int ng = 0;
    for (int i = 0; i < n && m->n < M_ITEMS; i++) {
        const ui_mitem_t *x = &items[i];
        if (!x->name) continue;
        if (x->items && x->n > 1 && ng < M_GROUPS) {    /* a group: one level; a group of one shows it in its place */
            mi_t *t = &m->it[m->n++];
            strncpy(t->name, T(x->name), sizeof t->name - 1);   /* (each name copied as it comes: T()'s buffers) */
            t->id = -1; t->group = (int8_t)ng; t->parent = -1;
            for (int j = 0; j < x->n && m->n < M_ITEMS; j++) {
                mi_t *g = &m->it[m->n++];
                strncpy(g->name, T(x->items[j].name), sizeof g->name - 1);
                g->id = (int16_t)x->items[j].id; g->group = -1; g->parent = (int8_t)ng;
            }
            ng++;
            continue;
        }
        const ui_mitem_t *a = x->items && x->n == 1 ? &x->items[0] : x;
        if (x->items && !x->n) continue;
        mi_t *t = &m->it[m->n++];
        strncpy(t->name, T(a->name), sizeof t->name - 1);
        t->id = (int16_t)a->id; t->group = -1; t->parent = -1;
    }
    if (!m->n) return;
    /* the last action highlighted: in its group (opened), else at the top (a group left by BACK: on it) */
    menu_mem_t *mm = memory(key);
    if (mm->group >= 0 && mm->group < ng) {
        m->level = mm->group;
        int found = 0;
        for (int i = 1; i < m_count(m); i++) if (m_item(m, i)->id == mm->item) { m->sel = i; found = 1; }
        if (!found) { m->level = -1; m->sel = 0; }
    } else if (mm->item <= -100) m->sel = m_head(m, -100 - mm->item);
    else for (int i = 0; i < m_count(m); i++) { const mi_t *t = m_item(m, i); if (t->group < 0 && t->id == mm->item) m->sel = i; }
    ui_overlay_t o = {.kind = OV_MENU, .opener = (int8_t)opener, .knob = menu_knob, .tap = menu_tap, .draw = menu_draw, .data = m};
    ui_open(&o);
}

/* ---------------------------------------------------------------- questions --- */
static struct { char lines[6][40]; int n; char yes[12], no[12]; void (*done)(int); } ask_s;   /* (6: FILL ROM's) */
/* a question's foot: no on the left under OCT-, yes on the right under OCT+, as the buttons sit; then its
 * keys' line, centred, 16 px above the panel's bottom */
void ui_ask_foot(int ay, int bottom, const char *yes, const char *no, const g_run_t *keys, int nk, int lb, int rb)
{
    const char *nm[2] = {T(UI_BNAME[lb]), T(UI_BNAME[rb])}, *an[2] = {no, yes};
    for (int i = 0; i < 2; i++) {
        int x = i ? 120 : 8;
        g_text(g_centre(&FONT_T, nm[i], x, 112), ay, &FONT_T, nm[i], C_YELLOW);
        g_text_crop(g_centre(&FONT_S, an[i], x, 112), ay + 13, &FONT_S, an[i], C_WHITE, 0, 16);
    }
    int kw = g_runs_w(&FONT_T, keys, nk);
    g_runs(8 + (224 - kw) / 2, bottom - 16, &FONT_T, keys, nk);
}
static int ask_tap(ui_overlay_t *o, int b)
{
    if (b == B_OCTP || b == B_OCTM) {
        void (*done)(int) = ask_s.done;
        ui_close(o);
        if (done) done(b == B_OCTP);
    }
    return 1;
}
static void ask_cancel(ui_overlay_t *o) { void (*done)(int) = ask_s.done; ui_close(o); if (done) done(-1); }
static void ask_knob(ui_overlay_t *o, int e, int s, int c) { (void)o; (void)e; (void)s; (void)c; }
static void ask_draw(ui_overlay_t *o)
{
    (void)o;
    int h = ask_s.n * 12 + 62, y = BODY_Y + (BODY_H - h) / 2;
    ui_panel(8, y, 224, h, 0, C_CYAN);
    for (int i = 0; i < ask_s.n; i++)
        g_text(g_centre(&FONT_T, ask_s.lines[i], 8, 224), y + 8 + i * 12, &FONT_T, ask_s.lines[i], i ? C_LGREY : C_WHITE);
    g_run_t k[2] = {{T("HOME"), C_YELLOW}, {T(": CANCEL"), C_LGREY}};
    ui_ask_foot(y + 12 + ask_s.n * 12, y + h, ask_s.yes, ask_s.no, k, 2, B_OCTM, B_OCTP);
}
void ui_ask(const char *const *lines, int nlines, const char *yes, const char *no, void (*done)(int answer))
{
    ask_s.n = nlines > 6 ? 6 : nlines;
    for (int i = 0; i < ask_s.n; i++) { strncpy(ask_s.lines[i], lines[i], 39); ask_s.lines[i][39] = 0; }
    strncpy(ask_s.yes, yes, 11); ask_s.yes[11] = 0;     /* (an answer is a word or two in S: 14 at most) */
    strncpy(ask_s.no, no, 11); ask_s.no[11] = 0;
    ask_s.done = done;
    ui_overlay_t o = {.kind = OV_QUESTION, .opener = -1, .knob = ask_knob, .tap = ask_tap, .cancel = ask_cancel, .draw = ask_draw};
    ui_open(&o);
}

const char *ui_overlay_words(const ui_overlay_t *o)    /* (the state a page asks for) */
{
    if (!o) return 0;
    if (o->draw == ask_draw) return ask_s.lines[0];
    if (o->draw == menu_draw) return m_item(&menu_s, menu_s.sel)->name;   /* the highlighted item (BACK in a group) */
    return 0;
}

/* --------------------------------------------------------------- properties --- */
#define P_ITEMS 10                      /* (a screen's properties: 7 at most so far) */
static struct { char title[32], sub[20]; ui_prop_t items[P_ITEMS]; char names[P_ITEMS][20]; int n, sel; } props_s;
static void props_knob(ui_overlay_t *o, int e, int s, int c)
{
    (void)o;
    if (e == K_SELECT) props_s.sel = ui_clamp(props_s.sel + s, 0, props_s.n - 1);
    else if (e == K_K1 && props_s.items[props_s.sel].change) props_s.items[props_s.sel].change(s, c);
}
static int props_tap(ui_overlay_t *o, int b)
{
    if (b == o->opener) { ui_close(o); return 1; }
    const ui_prop_t *it = &props_s.items[props_s.sel];
    if (b == B_EDIT && it->edit) { it->edit(); return 1; }
    if (b == B_LFO && it->reset) { it->reset(); return 1; }
    return 0;
}
static void props_draw(ui_overlay_t *o)
{
    (void)o;
    int n = props_s.n, sub = props_s.sub[0] ? 12 : 0, h = n * 14 + 24 + sub, y = BODY_Y + 4;
    ui_panel(6, y, 228, h, props_s.title, C_CYAN);
    if (sub) g_text(10, y + 15, &FONT_T, props_s.sub, C_WHITE);   /* (whose they are, under a title ending in FOR) */
    for (int i = 0; i < n; i++) {
        const ui_prop_t *it = &props_s.items[i];
        int yy = y + 19 + sub + i * 14, on = i == props_s.sel;
        if (on) g_box(8, yy - 2, 224, 14, C_WHITE);
        g_text(12, yy, &FONT_T, props_s.names[i], on ? C_WHITE : C_CYAN);
        const char *v = it->get ? it->get() : "";
        int col = it->col ? it->col(on) : on ? C_WHITE : C_LGREY;
        if (!it->change && !it->edit) col = C_DGREY;
        g_text(228 - g_tw(&FONT_T, v), yy, &FONT_T, v, col);
    }
}
void ui_props(const char *title, const ui_prop_t *items, int n, int opener)
{
    strncpy(props_s.title, title, sizeof props_s.title - 1);
    props_s.sub[0] = 0;
    props_s.n = n > P_ITEMS ? P_ITEMS : n;
    for (int i = 0; i < props_s.n; i++) {
        props_s.items[i] = items[i];
        strncpy(props_s.names[i], T(items[i].name), sizeof props_s.names[0] - 1);
        props_s.names[i][sizeof props_s.names[0] - 1] = 0;
    }
    props_s.sel = 0;
    ui_overlay_t o = {.kind = OV_PROPS, .opener = (int8_t)opener, .knob = props_knob, .tap = props_tap, .draw = props_draw};
    ui_open(&o);
}
void ui_props_for(const char *name)
{
    strncpy(props_s.sub, name, sizeof props_s.sub - 1);
    props_s.sub[sizeof props_s.sub - 1] = 0;
}

/* --------------------------------------------------------------- text entry --- */
static const char *const CHARSETS[3] = {"ABCDEFGHIJKLMNOPQRSTUVWXYZ", "0123456789", " :.-?|/\\*#$@%"};
static const char CHARS[] = "ABCDEFGHIJKLMNOPQRSTUVWXYZ0123456789 :.-?|/\\*#$@%";
static struct { char title[24]; char chars[17]; int max, pos; void (*done)(const char *); } te;
static void te_knob(ui_overlay_t *o, int e, int s, int coarse)
{
    (void)o;
    if (e == K_ALG) te.pos = ui_clamp(te.pos + s, 0, te.max - 1);
    else if (e == K_SELECT) {
        char c = te.chars[te.pos];
        if (coarse) {
            int set = 0;
            for (int k = 0; k < 3; k++) if (strchr(CHARSETS[k], c)) set = k;
            te.chars[te.pos] = CHARSETS[ui_wrap(set + (s > 0 ? 1 : -1), 3)][0];
        } else {
            const char *p = strchr(CHARS, c);
            int i = p ? (int)(p - CHARS) : 0, n = (int)sizeof CHARS - 1;
            te.chars[te.pos] = CHARS[ui_wrap(i + s, n)];
        }
    }
}
static void te_mark(ui_overlay_t *o, int e, int s) { te_knob(o, e, s, 1); }
static void te_finish(ui_overlay_t *o, int ok)
{
    char out[17];
    memcpy(out, te.chars, sizeof out);
    int n = te.max;
    while (n && out[n - 1] == ' ') n--;
    out[n] = 0;
    void (*done)(const char *) = te.done;
    ui_close(o);
    done(ok ? out : 0);
}
static int te_tap(ui_overlay_t *o, int b)
{
    if (b == o->opener) { te_finish(o, 1); return 1; }
    if (b == B_LFO) { memmove(te.chars + te.pos, te.chars + te.pos + 1, (size_t)(te.max - te.pos - 1)); te.chars[te.max - 1] = ' '; return 1; }
    if (b == B_OCTP) { memmove(te.chars + te.pos + 1, te.chars + te.pos, (size_t)(te.max - te.pos - 1)); te.chars[te.pos] = ' '; return 1; }
    return 1;
}
static void te_cancel(ui_overlay_t *o) { te_finish(o, 0); }
static void te_draw(ui_overlay_t *o)
{
    int y = BODY_Y + 44, x0 = g_centre(&FONT_B, &"WWWWWWWWWWWWWWWW"[16 - te.max], 10, 220);
    ui_panel(10, y, 220, 104, te.title, C_CYAN);
    for (int i = 0; i < te.max; i++) {
        int x = x0 + i * 12;
        char c[2] = {te.chars[i] == ' ' ? '_' : te.chars[i], 0};
        if (i == te.pos) g_rect(x, y + 22, 12, 26, C_DEEP + C_BLUE);
        g_text(x, y + 23, &FONT_B, c, i == te.pos ? C_WHITE : te.chars[i] == ' ' ? C_DGREY : C_LGREY);
    }
    char done_b[16];
    ui_fmt(done_b, sizeof done_b, "%s", T(UI_BNAME[o->opener]));
    g_run_t a[4] = {{T("ALGORITHM"), C_YELLOW}, {T(": MOVE  "), C_LGREY}, {T("SELECT"), C_YELLOW}, {T(": LETTER"), C_LGREY}};
    g_run_t b[4] = {{done_b, C_YELLOW}, {T(": DONE  "), C_LGREY}, {T("HOME"), C_YELLOW}, {T(": CANCEL"), C_LGREY}};
    g_runs(16, y + 56, &FONT_T, a, 4);                  /* (each line drawn as it is made: T()'s buffers turn) */
    g_runs(16, y + 70, &FONT_T, b, 4);
    g_run_t c[4] = {{T("LFO"), C_YELLOW}, {T(": DELETE  "), C_LGREY}, {T("OCT+"), C_YELLOW}, {T(": SPACE"), C_LGREY}};
    g_runs(16, y + 84, &FONT_T, c, 4);
}
void ui_text_entry(const char *title, const char *name, int max, int opener, void (*done)(const char *name))
{
    if (max > 16) max = 16;
    strncpy(te.title, title, sizeof te.title - 1);
    memset(te.chars, ' ', sizeof te.chars);
    for (int i = 0; i < max && name[i]; i++) te.chars[i] = name[i];
    te.chars[max] = 0;
    te.max = max;
    te.pos = 0;
    te.done = done;
    ui_overlay_t o = {.kind = OV_TEXT, .opener = (int8_t)opener, .knob = te_knob, .mark = te_mark, .tap = te_tap,
                      .cancel = te_cancel, .draw = te_draw};
    ui_open(&o);
}

/* ------------------------------------------------------------------ progress --- */
static struct { char text[40]; uint32_t t0; int ms; void (*then)(void); uint8_t on; int (*pct)(void); } pr;
static void pr_draw(ui_overlay_t *o)
{
    (void)o;
    int k = pr.pct ? pr.pct() * 176 / 100 : pr.ms ? (int)((ui_now() - pr.t0) * 176 / (uint32_t)pr.ms) : 176, y = BODY_Y + 80;
    if (k > 176) k = 176;
    ui_panel(20, y, 200, 44, 0, C_CYAN);
    g_text(g_centre(&FONT_T, pr.text, 20, 200), y + 6, &FONT_T, pr.text, C_WHITE);
    g_box(30, y + 24, 180, 10, C_LGREY);
    g_rect(32, y + 26, k, 6, C_GREEN);
}
static int pr_tap(ui_overlay_t *o, int b) { (void)o; (void)b; return 1; }
void ui_progress(const char *text, int ms, void (*then)(void))
{
    strncpy(pr.text, text, sizeof pr.text - 1);
    pr.t0 = ui_now(); pr.ms = ms; pr.then = then; pr.on = 1; pr.pct = 0;
    ui_overlay_t o = {.kind = OV_PROGRESS, .opener = -1, .tap = pr_tap, .draw = pr_draw};
    ui_open(&o);
}
void ui_progress_pct(const char *text, int (*pct)(void))
{
    strncpy(pr.text, text, sizeof pr.text - 1);
    pr.on = 0; pr.pct = pct; pr.then = 0;
    ui_overlay_t o = {.kind = OV_PROGRESS, .opener = -1, .tap = pr_tap, .draw = pr_draw};
    ui_open(&o);
}
void ui_progress_close(void)
{
    for (int i = 0; i < ui_overlay_count(); i++) if (ui_overlay_at(i)->kind == OV_PROGRESS) { ui_close(ui_overlay_at(i)); break; }
    pr.pct = 0;
}
int ui_blocking(void)
{
    for (int i = 0; i < ui_overlay_count(); i++) if (ui_overlay_at(i)->kind == OV_PROGRESS) return 1;
    return 0;
}
void ui_dlg_poll(void)
{
    if (!pr.on || ui_now() - pr.t0 < (uint32_t)pr.ms) return;
    pr.on = 0;
    for (int i = 0; i < ui_overlay_count(); i++) if (ui_overlay_at(i)->kind == OV_PROGRESS) { ui_close(ui_overlay_at(i)); break; }
    if (pr.then) pr.then();
}

/* --------------------------------------------------------------- the help --- */
/* readings of the keys whose names say little (help.md) */
static const char *const READ_KEY[12] = {"LFO", "ENV", "SEQ", "ARP", "OP1", "OP2", "OP3", "OP4", "OP5", "PIT", "MONO", "POLY"};
static const txt_t READ_TXT[12] = {
    TX("LEAVE FIELD OPEN"), TX("EXPLORE NAMED VAULTS"), TX("SHOW ELSE, QUICK"), TX("AGAIN, REPEAT, PLEASE"), TX("OPEN PAGE 1"),
    TX("OPEN PAGE 2"), TX("OPEN PAGE 3"), TX("OPEN PAGE 4"), TX("OPEN PAGE 5"), TX("PICTURES IN TIME"), TX("MY OWN NERDY OPTIONS"),
    TX("PARTY ON, LOOP YOURSELF")};
static int screen_col(const char *n)
{
    static const char *const S[10] = {"SONGS", "EDITOR", "INSTRUMENTS", "PLAYBACK", "FX", "LOOPS", "DJ", "VISUAL", "STATUS", "SETTINGS"};
    static const uint8_t C[10] = {C_LBLUE, C_LBLUE, C_LBLUE, C_GREEN, C_GREEN, C_GREEN, C_GREEN, C_BROWN, C_BROWN, C_MAGENTA};
    for (int i = 0; i < 10; i++) if (!strcmp(S[i], n)) return C[i];
    return C_LGREY;
}
typedef struct { uint16_t pages[24]; int n; uint16_t named; int page; } help_t;
static help_t help_s;
static int help_title(const char *s, int len, int first)
{
    if (first) return 1;
    if (!len || s[0] == ' ') return 0;
    int depth = 0;
    for (int i = 0; i < len; i++) {
        if (s[i] == '{' || s[i] == '[') depth++;
        else if (s[i] == '}' || s[i] == ']') depth--;
        else if (s[i] == ':' && !depth) return 0;
    }
    return 1;
}
static int isup(char c) { return c >= 'A' && c <= 'Z'; }
/* one line of the help as runs, drawn at y (draw 0: only the readings it names, into *named) */
static void help_line(const char *s, int len, int first, int y, uint16_t *named, int draw)
{
    int x = 0, title = help_title(s, len, first), base = title ? C_CYAN : C_LGREY, i = 0;
    char piece[48];
    int np = 0;
#define FLUSH() do { if (np) { piece[np] = 0; if (draw) x = g_text(x, y, &FONT_T, piece, base); else x += np * 6; np = 0; } } while (0)
#define PUT(str, col) do { if (draw) x = g_text(x, y, &FONT_T, str, col); else x += (int)strlen(str) * 6; } while (0)
    while (i < len) {
        char ch = s[i];
        if (ch == '{' || ch == '[') {
            const char *e = memchr(s + i, ch == '{' ? '}' : ']', (size_t)(len - i));
            if (!e) break;
            char name[24];
            int nl = (int)(e - (s + i + 1));
            if (nl > 23) nl = 23;
            memcpy(name, s + i + 1, (size_t)nl);
            name[nl] = 0;
            FLUSH();
            i = (int)(e - s) + 1;
            if (ch == '[') { PUT(name, screen_col(name)); continue; }
            int autor = nl && name[nl - 1] == '*';
            if (autor) name[--nl] = 0;
            PUT(name, C_YELLOW);
            int r = -1;
            for (int k = 0; k < 12; k++) if (!strcmp(READ_KEY[k], name)) r = k;
            if (r >= 0) {
                char rd[32];
                ui_fmt(rd, sizeof rd, " (%s)", T(READ_TXT[r]));
                int rl = (int)strlen(rd);
                if (autor && !(*named >> r & 1)) { PUT(rd, C_DGREY); *named |= (uint16_t)(1u << r); continue; }
                if (len - i >= rl && !memcmp(s + i, rd, (size_t)rl)) { PUT(rd, C_DGREY); *named |= (uint16_t)(1u << r); i += rl; }
            }
            continue;
        }
        if (len - i >= 3 && !memcmp(s + i, "RED", 3) && !(i + 3 < len && isup(s[i + 3])) && !(i > 0 && isup(s[i - 1]))) {
            FLUSH();
            PUT("RED", C_RED);
            i += 3;
            continue;
        }
        if (np < 46) piece[np++] = ch;
        i++;
    }
    FLUSH();
#undef FLUSH
#undef PUT
}
/* a page's lines; the first page padded to 16 lines, then PRESS HOME TO GET BACK */
static void help_page(int k, uint16_t *named, int draw)
{
    char *text = (char *)ui_scratch;                    /* (the page unpacked: 17 lines of 40 at most) */
    txt_unpack(help_s.pages[k], text, UI_SCRATCH);
    int line = 0;
    const char *p = text;
    while (*p) {
        const char *e = strchr(p, '\n');
        int len = e ? (int)(e - p) : (int)strlen(p);
        help_line(p, len, line == 0, BODY_Y + 2 + line * 12, named, draw);
        line++;
        p += len + (e ? 1 : 0);
        if (!e) break;
    }
    if (k == 0 && draw) {
        g_run_t r[3] = {{T("PRESS "), C_LGREY}, {T("HOME"), C_YELLOW}, {T(" TO GET BACK"), C_LGREY}};
        g_runs(0, BODY_Y + 2 + 16 * 12, &FONT_T, r, 3);
    }
}
static void help_knob(ui_overlay_t *o, int e, int s, int c)
{
    (void)o; (void)c;
    if (e == K_SELECT) help_s.page = ui_clamp(help_s.page + s, 0, help_s.n - 1);
}
static int help_tap(ui_overlay_t *o, int b) { (void)o; (void)b; return 1; }
static void help_draw(ui_overlay_t *o)
{
    (void)o;
    g_rect(0, BODY_Y, 240, BODY_H, C_BLACK);
    uint16_t named = 0;
    for (int k = 0; k < help_s.page; k++) help_page(k, &named, 0);  /* readings already named on pages before */
    help_page(help_s.page, &named, 1);
}
const char *ui_help_page_text(ui_overlay_t *help, char *buf)
{
    (void)help;
    ui_page_of(buf, help_s.page, help_s.n);
    return buf;
}
void ui_open_help(void)
{
    const ui_screen_t *s = ui_screen_now();
    help_s.n = 0;
    help_s.page = 0;
    const txt_help_t *own = 0;
    for (int i = 0; i < TXT_HELP_N; i++) if (!strcmp(TXT_HELP[i].screen, T(s->name))) own = &TXT_HELP[i];
    int status = !strcmp(T(s->name), "STATUS");
    if (status) for (int k = 0; k < TXT_HELP_SHARED_N && help_s.n < 24; k++) help_s.pages[help_s.n++] = TXT_HELP_PAGES[TXT_HELP_SHARED + k];
    if (own) for (int k = 0; k < own->n && help_s.n < 24; k++) help_s.pages[help_s.n++] = TXT_HELP_PAGES[own->first + k];
    if (!status) for (int k = 0; k < TXT_HELP_SHARED_N && help_s.n < 24; k++) help_s.pages[help_s.n++] = TXT_HELP_PAGES[TXT_HELP_SHARED + k];
    if (!help_s.n) return;
    ui_overlay_t o = {.kind = OV_HELP, .opener = -1, .knob = help_knob, .tap = help_tap, .draw = help_draw};
    ui_open(&o);
}

/* --------------------------------------- HOME's combination, the held layers --- */
/* a combination's lines at the body's bottom: the heading, then a control and what it does a line */
static void combo_panel(const g_run_t *heading, int nh, g_run_t (*lines)[UI_LINE_RUNS], const int *nr, int n)
{
    int h = n * 12 + 26, y = BODY_Y + BODY_H - h - 2;
    ui_panel(4, y, 232, h, 0, C_CYAN);
    g_runs(8, y + 5, &FONT_T, heading, nh);
    for (int i = 0; i < n; i++) g_runs(8, y + 19 + i * 12, &FONT_T, lines[i], nr[i]);
}
static void home_panel(const combo_t *c, int held)
{
    g_run_t lines[14][UI_LINE_RUNS];
    int nr[14];
    char buf[24][48];
    int n = ui_home_lines(c, lines, nr, buf, 13);
    const ui_screen_t *s = ui_screen_now();
    int ay, ah;
    /* once a white key has acted, on a screen that gives a strip (LOOPS' preview): one line there */
    if (c->lock == CK_WHITE && s->home_area && s->home_area(&ay, &ah) && n == 1) {
        ui_panel(4, ay, 232, ah, 0, C_CYAN);
        g_run_t r[UI_LINE_RUNS + 4];
        int k = 0;
        if (held) { r[k].s = T("HOLD "); r[k++].c = C_CYAN; }
        r[k].s = T("HOME"); r[k++].c = C_YELLOW;
        r[k].s = " + "; r[k++].c = C_CYAN;
        for (int i = 0; i < nr[0]; i++) r[k++] = lines[0][i];
        g_runs(8, ay + 2, &FONT_T, r, k);
        if (!held) { g_run_t cl[2] = {{T("HOME"), C_YELLOW}, {T(": CLOSE"), C_LGREY}}; g_runs(8, ay + 14, &FONT_T, cl, 2); }
        return;
    }
    if (held) {
        g_run_t hd[3] = {{T("HOLD "), C_CYAN}, {T("HOME"), C_YELLOW}, {" +", C_CYAN}};
        combo_panel(hd, 3, lines, nr, n);
    } else {
        ui_key_line(lines[n], &nr[n], T("HOME: CLOSE"));
        n++;
        g_run_t hd[2] = {{T("HOME"), C_YELLOW}, {" +", C_CYAN}};
        combo_panel(hd, 2, lines, nr, n);
    }
}
static int home_tap_ov(ui_overlay_t *o, int b)
{
    combo_t *c = ui_home_combo();
    if (b == B_LFO && c && (!c->lock || c->lock == CK_LFO)) { ui_close(o); ui_open_help(); return 1; }
    if (b == B_SEQ && c && ui_home_seq_switches(c)) { c->solo = !c->solo; return 1; }
    ui_close(o);
    return 0;
}
static void home_draw(ui_overlay_t *o) { (void)o; combo_t *c = ui_home_combo(); if (c) home_panel(c, 0); }
static void home_knob(ui_overlay_t *o, int e, int s, int k) { (void)o; (void)e; (void)s; (void)k; }
void ui_open_home(void)
{
    combo_t *c;
    ui_overlay_t o = {.kind = OV_HOME, .opener = B_HOME, .passes = 1, .keep = 1, .knob = home_knob, .tap = home_tap_ov,
                      .draw = home_draw};
    ui_open(&o);
    c = ui_home_combo();
    if (c) { c->lock = 0; c->solo = 0; c->closed = 0; c->at = ui_now(); c->is_ov = 1; }
}

static int8_t layers[4];
static int nlayers;
static const char *const KEYNUM[16] = {"1 ", "2 ", "3 ", "4 ", "5 ", "6 ", "7 ", "8 ", "9 ", "10 ", "11 ", "12 ", "13 ", "14 ", "15 ", "16 "};
/* FX held: the punch-ins as a table, two a line by their full names, the keys' numbers right-aligned in 2 characters,
 * the second column 19 characters in (37 at most) */
static const char *const KEYNUM2[16] = {" 1 ", " 2 ", " 3 ", " 4 ", " 5 ", " 6 ", " 7 ", " 8 ", " 9 ", "10 ", "11 ", "12 ",
                                        "13 ", "14 ", "15 ", "16 "};
static int punch_lines(g_run_t (*lines)[UI_LINE_RUNS], int *nr)
{
    static char left[8][17];                            /* (the first column's names, padded to the second) */
    for (int l = 0; l < 8; l++) {
        int r = 0;
        for (int c = 0; c < 2; c++) {
            int k = 2 * l + c;
            const char *name = T(UI_PUNCH_NAME[k]);
            if (!c) {
                int i = 0;
                for (; name[i] && i < 16; i++) left[l][i] = name[i];
                for (; i < 16; i++) left[l][i] = ' ';
                left[l][16] = 0;
                name = left[l];
            } else name = keep(name);                   /* (kept: drawn after) */
            lines[l][r].s = KEYNUM2[k]; lines[l][r++].c = C_YELLOW;
            lines[l][r].s = name; lines[l][r++].c = ui_punch_now() == k ? C_WHITE : C_LGREY;
        }
        nr[l] = r;
    }
    return 8;
}
/* DJ's LFO pads by key (screens/dj.md#the-lfo-pads), a line a group of four as FX's punch-ins, what is on white: an
 * LFO running, the reverse, a deck's KEY not 0; then how the on / off and KEY pads take knobs. 6 lines */
static const txt_t LFO_KEYS[4][4] = {{TX("FADER"), TX("SHAPE"), TX("RANGE"), TX("REVERSE")},
                                     {TX("FILTER L"), TX("SHAPE"), TX("RANGE"), TX("KEY")},
                                     {TX("FILTER R"), TX("SHAPE"), TX("RANGE"), TX("KEY")},
                                     {TX("FX"), TX("SHAPE"), TX("RANGE"), TX("ALL OFF")}};
static int dj_lfo_lines(g_run_t (*lines)[UI_LINE_RUNS], int *nr)
{
    int n = 0;                                          /* (the texts kept: the lines are drawn after) */
    for (int g = 0; g < 4; g++) {
        const uint8_t *b = dj_lfo_bytes(g);
        int r = 0;
        for (int i = 0; i < 4; i++) {
            int on = i == 0 ? b[0] & 1 : i == 3 && g == 0 ? b[0] & 2 : i == 3 && g < 3 ? dj_deck(g - 1)[DK_KEY] != 0 : 0;
            if (i) { lines[n][r].s = "|"; lines[n][r++].c = C_DGREY; }
            lines[n][r].s = KEYNUM[g * 4 + i]; lines[n][r++].c = C_YELLOW;
            lines[n][r].s = keep(T(LFO_KEYS[g][i])); lines[n][r++].c = on ? C_WHITE : C_LGREY;
        }
        nr[n++] = r;
    }
    {
        g_run_t on[4] = {{"1 5 9 13", C_YELLOW}, {keep(T(": ON / OFF; + ")), C_LGREY}, {keep(T("ALG")), C_YELLOW},
                         {keep(T(": SPEED")), C_LGREY}};
        g_run_t key[4] = {{"8 12", C_YELLOW}, {keep(T(" HELD + ")), C_LGREY}, {keep(T("K1 / K4")), C_YELLOW},
                          {keep(T(": THE KEY")), C_LGREY}};
        memcpy(lines[n], on, sizeof on);
        nr[n++] = 4;
        memcpy(lines[n], key, sizeof key);
        nr[n++] = 4;
    }
    return n;
}
/* a held button's lines (dialogs.md#hold-layers); 0 none */
static int layer_lines(int b, g_run_t (*lines)[UI_LINE_RUNS], int *nr, int max)
{
    int n = 0;
    ui_overlay_t *o = ui_top();
    const ui_screen_t *s = ui_screen_now();
    switch (b) {
    case B_FX:
        return max >= 8 ? punch_lines(lines, nr) : 0;
    case B_SEL:
        if (o && o->kind == OV_TEXT) { ui_key_line(lines[n], &nr[n], T("SELECT IN A NAME: NEXT GROUP")); return 1; }
        if (s->sel_lines) {
            const char *l[8];
            int k = s->sel_lines(l, 8);
            for (int i = 0; i < k && n < max; i++) { ui_key_line(lines[n], &nr[n], l[i]); n++; }
        }
        if (!s->no_keyboard && n < max) { ui_key_line(lines[n], &nr[n], T("PRESETS: OTHER CARTRIDGE")); n++; }
        return n;
    case B_ARP:                                         /* the arpeggiator's; the EDITOR's row preview its rhythm */
        if (s->own_arp == UI_ARP_PREVIEW) { ui_key_line(lines[0], &nr[0], T("SELECT: THE RHYTHM, # A ROW, _ A REST")); return 1; }
        if (s->own_arp) return 0;
        ui_key_line(lines[0], &nr[0], T("SELECT: THE RHYTHM, # A NOTE, _ A REST"));
        ui_key_line(lines[1], &nr[1], T("ALGORITHM: THE PATTERN"));
        ui_key_line(lines[2], &nr[2], T("K1: HOW MANY NOTES LATCH"));
        ui_key_line(lines[3], &nr[3], T("LFO: LET THE LATCHED NOTES GO"));
        return 4;
    case B_SAVE:
        ui_key_line(lines[0], &nr[0], T("OCT+: SAVE"));
        ui_key_line(lines[1], &nr[1], T("OCT-: REVERT ALL"));
        return 2;
    case B_LFO:
        ui_key_line(lines[n], &nr[n], T("HOME: HELP FOR THIS SCREEN"));
        n++;
        if (!strcmp(T(s->name), "DJ") && max >= 7) n += dj_lfo_lines(lines + n, nr + n);
        return n;
    case B_HOME: return 1;
    default: return 0;
    }
}
int ui_layer_open(int b)
{
    g_run_t l[8][UI_LINE_RUNS];
    int nr[8];
    if (!layer_lines(b, l, nr, 8) || nlayers == 4) return 0;
    layers[nlayers++] = (int8_t)b;
    ui_dirty();
    return 1;
}
void ui_layer_close(int b)
{
    for (int i = 0; i < nlayers; i++) if (layers[i] == b) {
        memmove(&layers[i], &layers[i + 1], (size_t)(nlayers - i - 1));
        nlayers--;
        ui_dirty();
        return;
    }
}
void ui_layers_draw(void)
{
    for (int i = 0; i < nlayers; i++) {
        int b = layers[i];
        if (b == B_HOME) {
            combo_t *c = ui_home_combo();
            combo_t none = {0, 0, 0, 0, 0};
            home_panel(c ? c : &none, 1);
            continue;
        }
        g_run_t lines[8][UI_LINE_RUNS];
        int nr[8], n = layer_lines(b, lines, nr, 8);
        if (!n) continue;
        g_run_t hd[4] = {{T("HOLD "), C_CYAN}, {T(UI_BNAME[b]), C_YELLOW}, {b == B_FX ? " + " : " +", C_CYAN}, {T("KEYS"), C_YELLOW}};
        combo_panel(hd, b == B_FX ? 4 : 3, lines, nr, n);
    }
}

/* ------------------------------------------------------------- seeding --- */
/* the body while the flash is being written over USB (whatever sends it): nothing else acts */
void ui_seed_draw(void)
{
    seed_info_t si;
    seed_info(&si);
    g_rect(0, BODY_Y, 240, BODY_H, C_BLACK);
    const char *a = T("RECEIVING DATA"), *c = T("SOUND AND KEYS WAIT");
    g_text(g_centre(&FONT_S, a, 0, 240), BODY_Y + 66, &FONT_S, a, C_WHITE);
    char t[32];
    ui_fmt(t, sizeof t, "%s", ui_txt_v(ui_txt_v(T("BLOCK # OF #"), ui_dec(si.done)), ui_dec(si.total)));
    g_text(g_centre(&FONT_T, t, 0, 240), BODY_Y + 92, &FONT_T, t, C_LGREY);
    g_box(30, BODY_Y + 108, 180, 10, C_LGREY);
    if (si.total) g_rect(32, BODY_Y + 110, (int)(176u * si.done / si.total), 6, C_GREEN);
    g_text(g_centre(&FONT_T, c, 0, 240), BODY_Y + 134, &FONT_T, c, C_DGREY);
}
