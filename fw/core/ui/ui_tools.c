/* SPDX-License-Identifier: GPL-3.0-only
 * Copyright (C) 2026 Vasily Kazantsev */
/* SETTINGS' TOOLS (specs/ui/screens/settings.md): CHECK REFERENCES (what is missing, a line each) and
 * CLEAN UP (a checklist: the unused patterns and samples ticked, the unused instruments not); and the
 * checklist question they ask (specs/ui/dialogs.md). */
#include <string.h>
#include "ui_core.h"
#include "ui_priv.h"
#include "ui_data.h"
#include "ed_inst.h"
#include "../play/rec.h"
#include "../store/store.h"

/* ------------------------------------------------------------- a checklist --- */
static struct {
    char title[40];
    ui_check_t rows[4];
    int n, sel, top;
    char yes[16], no[16];
    const char *sub, *foot;             /* a second title line, a line under the rows, or 0 */
    int bare;                           /* yes stands with none ticked */
    void (*done)(int answer, ui_check_t *rows);
    const char *(*title_of)(const ui_check_t *rows, int n);
} ck;
static void ck_knob(ui_overlay_t *o, int e, int s, int c)
{
    (void)o; (void)c;
    if (e == K_SELECT) ck.sel = ui_clamp(ck.sel + s, 0, ck.n - 1);
    if (e == K_K1 && ck.rows[ck.sel].count) ck.rows[ck.sel].on = s > 0;
}
static int ck_tap(ui_overlay_t *o, int b)
{
    int any = 0;
    for (int i = 0; i < ck.n; i++) any |= ck.rows[i].on;
    if (b == B_OCTP && !any && !ck.bare) { ui_refuse(B_OCTP); return 1; }
    if (b == B_OCTP || b == B_OCTM) {
        void (*done)(int, ui_check_t *) = ck.done;
        ui_close(o);
        done(b == B_OCTP, ck.rows);
    }
    return 1;
}
static void ck_cancel(ui_overlay_t *o) { void (*done)(int, ui_check_t *) = ck.done; ui_close(o); done(-1, ck.rows); }
static void ck_draw(ui_overlay_t *o)
{
    (void)o;
    int n = ck.n, sy = ck.sub ? 12 : 0, ex = sy + (ck.foot ? 13 : 0), h = n * 13 + 94 + ex, y = BODY_Y + (BODY_H - h) / 2;
    ui_panel(8, y, 224, h, 0, C_CYAN);
    const char *title = ck.title_of ? ck.title_of(ck.rows, ck.n) : ck.title;
    g_text(g_centre(&FONT_T, title, 8, 224), y + 8, &FONT_T, title, C_WHITE);
    if (ck.sub) g_text(g_centre(&FONT_T, ck.sub, 8, 224), y + 20, &FONT_T, ck.sub, C_LGREY);
    if (ck.foot) g_text(g_centre(&FONT_T, ck.foot, 8, 224), y + 24 + sy + n * 13, &FONT_T, ck.foot, C_DGREY);
    for (int i = 0; i < n; i++) {
        const ui_check_t *r = &ck.rows[i];
        int ry = y + 24 + sy + i * 13, on = i == ck.sel, col = r->count ? C_LGREY : C_DGREY;
        if (on) g_box(12, ry - 1, 216, 13, C_WHITE);
        g_box(16, ry + 1, 9, 9, col);
        if (r->on) g_rect(18, ry + 3, 5, 5, C_WHITE);
        g_text(32, ry, &FONT_T, r->name, r->count ? (on ? C_WHITE : C_LGREY) : C_DGREY);
        g_text(220 - g_tw(&FONT_T, r->note), ry, &FONT_T, r->note, col);
    }
    int ay = y + 30 + n * 13 + ex;
    const char *nm[2] = {T("OCT-"), T("OCT+")}, *an[2] = {ck.no, ck.yes};
    for (int i = 0; i < 2; i++) {
        int x = i ? 120 : 8;
        g_text(g_centre(&FONT_T, nm[i], x, 112), ay, &FONT_T, nm[i], C_YELLOW);
        g_text_crop(g_centre(&FONT_S, an[i], x, 112), ay + 13, &FONT_S, an[i], C_WHITE, 0, 16);
    }
    g_run_t k[6] = {{T("SELECT"), C_YELLOW}, {T(": PICK  "), C_LGREY}, {T("K1"), C_YELLOW}, {T(": TICK  "), C_LGREY},
                    {T("HOME"), C_YELLOW}, {T(": CANCEL"), C_LGREY}};
    int one = n == 1 ? 2 : 0, kw = g_runs_w(&FONT_T, k + one, 6 - one);    /* (one line: nothing to pick) */
    g_runs(8 + (224 - kw) / 2, y + h - 16, &FONT_T, k + one, 6 - one);
}
void ui_checklist(const char *(*title)(const ui_check_t *rows, int n), const ui_check_t *rows, int n, const char *yes,
                  const char *no, void (*done)(int answer, ui_check_t *rows), const char *sub, const char *foot, int bare)
{
    ck.sub = sub; ck.foot = foot; ck.bare = bare;
    ck.n = n > 4 ? 4 : n;
    memcpy(ck.rows, rows, (size_t)ck.n * sizeof *rows);
    ck.sel = 0;
    for (int i = 0; i < ck.n; i++) if (rows[i].count) { ck.sel = i; break; }
    ck.title_of = title;
    strncpy(ck.yes, yes, 15); strncpy(ck.no, no, 15);
    ck.done = done;
    ui_overlay_t o = {.kind = OV_QUESTION, .opener = -1, .knob = ck_knob, .tap = ck_tap, .cancel = ck_cancel, .draw = ck_draw};
    ui_open(&o);
}

/* ----------------------------------------------------------- what is used --- */
static uint8_t pat_used[32];            /* a bit a pattern */
static int bit(const uint8_t *m, int i) { return m[i >> 3] >> (i & 7) & 1; }
static void set_bit(uint8_t *m, int i) { m[i >> 3] |= (uint8_t)(1u << (i & 7)); }
/* the slots of cartridge cart that some pattern's cell names: a bit a slot (a pass over every pattern) */
static void inst_used(uint8_t cart, uint8_t *used)
{
    uint8_t nums[256];
    memset(used, 0, 32);
    int n = store_list(REC_PATTERN, nums, 256);
    for (int i = 0; i < n; i++) {
        rec_pat_t p;
        if (rec_pattern(nums[i], &p)) continue;
        for (int t = 0; t < p.ntrk; t++) {
            rec_cur_t cur;
            rec_cell_t cell;
            rec_cur_set(&cur, p.trk[t]);
            for (int r = 0; r < p.rows; r++)
                if (rec_cur_at(&cur, r, &cell) && (cell.has & RC_INS) && cell.cart == cart) set_bit(used, cell.slot);
        }
    }
    n = ui_carts(nums, 256);                            /* the favourites naming them: uses too */
    for (int i = 0; i < n && ui_cart_rank(nums[i]) < 2; i++) {
        rec_cart_t c;
        if (rec_cart(nums[i], &c)) continue;
        for (int s = 0; s < c.nslots; s++) { const uint8_t *f = rec_fav(&c, (uint8_t)s); if (f && f[0] == cart) set_bit(used, f[1]); }
    }
}
static void scan_uses(int skip)                         /* (skip: a song left out, or -1) */
{
    memset(pat_used, 0, sizeof pat_used);
    uint8_t nums[256];
    int n = ui_songs(nums, 256);
    for (int i = 0; i < n; i++) {
        ui_song_t s;
        if (nums[i] != skip && ui_song(nums[i], &s)) for (int p = 0; p < s.npos; p++) set_bit(pat_used, s.order[p]);
    }
    n = ui_carts(nums, 256);                            /* FUSION slots' patterns */
    for (int i = 0; i < n; i++) {
        rec_cart_t c;
        if (ui_cart_kind(nums[i]) != ENG_FUSION || rec_cart(nums[i], &c)) continue;
        for (int sl = 0; sl < c.nslots; sl++) { const uint8_t *s = rec_slot(&c, (uint8_t)sl); if (s) set_bit(pat_used, rec_fusion_pattern(s)); }
    }
}

/* -------------------------------------------------------- CHECK REFERENCES --- */
/* each missing reference kept as its numbers, worded as it is drawn */
enum { MS_CELL, MS_POS, MS_FUSION, MS_FAV, MS_SAMPLER };
typedef struct { uint8_t kind, a, b, c, d, e; } miss_t;
#define MISS_MAX 64
static miss_t miss[MISS_MAX];
static int nmiss, miss_top;
static void add_miss(int kind, int a, int b, int c, int d, int e)
{
    if (nmiss < MISS_MAX) {
        miss_t m = {(uint8_t)kind, (uint8_t)a, (uint8_t)b, (uint8_t)c, (uint8_t)d, (uint8_t)e};
        miss[nmiss] = m;
    }
    nmiss++;
}
static void miss_text(const miss_t *m, char *s)
{
    if (m->kind == MS_CELL) ui_fmt(s, 48, T("PATTERN %02X ROW %02X TRACK %02X: %02X:%02X"), m->a, m->b, m->c, m->d, m->e);
    else if (m->kind == MS_POS) ui_fmt(s, 48, T("SONG %02X POSITION %02X: PATTERN %02X"), m->a, m->b, m->c);
    else if (m->kind == MS_FAV) ui_fmt(s, 48, T("FAVORITE %02X:%02X: %02X:%02X"), m->a, m->b, m->c, m->d);
    else if (m->kind == MS_SAMPLER) ui_fmt(s, 48, T("SAMPLER %02X:%02X: SAMPLE %02X"), m->a, m->b, m->c);
    else ui_fmt(s, 48, T("FUSION %02X:%02X: PATTERN %02X"), m->a, m->b, m->c);
}
/* the old-form samplers (a sampler record in a version this firmware does not read, specs/files/instruments.md#the-old-form):
 * one at num; their numbers, ascending. They, and what names them (a cell, a favourite), are no reference to nothing
 * (specs/files/references.md): the list ends with each once and does not count them, so they are found as it is drawn
 * and not kept */
static int old_sampler(int num)
{
    uint32_t len;
    uint8_t ver;
    return store_get(REC_SAMPLER, (uint8_t)num, 0, &len, &ver) && ver != rec_cart_ver(REC_SAMPLER);
}
static int old_samplers(uint8_t *nums)
{
    uint8_t all[256];
    int n = store_list(REC_SAMPLER, all, 256), k = 0;
    for (int i = 0; i < n; i++) if (old_sampler(all[i])) nums[k++] = all[i];
    return k;
}
static void ml_knob(ui_overlay_t *o, int e, int s, int c)
{
    (void)o; (void)c;
    uint8_t old[256];
    int shown = (nmiss < MISS_MAX ? nmiss : MISS_MAX) + old_samplers(old);
    if (e == K_SELECT) miss_top = ui_clamp(miss_top + s, 0, shown - 14 > 0 ? shown - 14 : 0);
}
static int ml_tap(ui_overlay_t *o, int b) { (void)o; (void)b; return 0; }
static void ml_draw(ui_overlay_t *o)
{
    (void)o;
    char t[40];
    ui_fmt(t, sizeof t, "%s", ui_txt_v(T("MISSING REFERENCES: #"), ui_dec(nmiss)));
    int y = BODY_Y + 2;
    ui_panel(2, y, 236, 206, t, C_CYAN);
    uint8_t old[256];
    int kept = nmiss < MISS_MAX ? nmiss : MISS_MAX, nold = old_samplers(old);
    for (int i = 0; i < 14 && miss_top + i < kept + nold; i++) {
        char s[48];
        int at = miss_top + i;
        if (at < kept) miss_text(&miss[at], s);
        else ui_fmt(s, sizeof s, T("CARTRIDGE %02X: OLDER FORM"), old[at - kept]);   /* (after the references, not counted) */
        g_textn(8, y + 18 + i * 13, &FONT_T, s, 37, C_LGREY);
    }
}
static void checked(void)
{
    uint8_t old[256];
    if (!nmiss && !old_samplers(old)) { ui_toast(T("NO MISSING REFERENCES")); return; }
    miss_top = 0;
    ui_overlay_t o = {.kind = OV_LIST, .opener = -1, .knob = ml_knob, .tap = ml_tap, .draw = ml_draw};
    ui_open(&o);
}
void ui_check_references(void)
{
    char nm[12];
    uint8_t nums[256];
    nmiss = 0;
    int n = ui_patterns(nums, 256);                     /* (the EDITOR's own is never named by number) */
    for (int i = 0; i < n; i++) {
        rec_pat_t p;
        if (rec_pattern(nums[i], &p)) continue;
        for (int t = 0; t < p.ntrk; t++) {
            rec_cur_t cur;
            rec_cell_t cell;
            rec_cur_set(&cur, p.trk[t]);
            for (int r = 0; r < p.rows; r++)            /* (an old sampler's instrument: none) */
                if (rec_cur_at(&cur, r, &cell) && (cell.has & RC_INS) && !ui_inst_name((uint16_t)(cell.cart << 8 | cell.slot), nm) &&
                    !old_sampler(cell.cart))
                    add_miss(MS_CELL, nums[i], r, t, cell.cart, cell.slot);
        }
    }
    n = ui_songs(nums, 256);
    for (int i = 0; i < n; i++) {
        ui_song_t sg;
        if (!ui_song(nums[i], &sg)) continue;
        for (int p = 0; p < sg.npos; p++) if (!ui_pattern_rows(sg.order[p])) add_miss(MS_POS, nums[i], p, sg.order[p], 0, 0);
    }
    n = ui_carts(nums, 256);
    for (int i = 0; i < n; i++) {
        rec_cart_t c;
        uint16_t src;
        if (rec_cart(nums[i], &c)) continue;
        for (int sl = 0; sl < c.nslots; sl++) {         /* a FUSION's pattern; a favourite's source (FAVORITE FB:07: 0B:05) */
            const uint8_t *x = rec_slot(&c, (uint8_t)sl);
            if (c.kind == REC_FUSION && x && !ui_pattern_rows(rec_fusion_pattern(x))) add_miss(MS_FUSION, nums[i], sl, rec_fusion_pattern(x), 0, 0);
            if (c.kind == REC_FAV && ui_fav((uint16_t)(nums[i] << 8 | sl), &src) == UI_FAV_MISSING && !old_sampler(src >> 8))
                add_miss(MS_FAV, nums[i], sl, src >> 8, src & 255, 0);
            if (c.kind == REC_SAMPLER && x) {           /* a sample instrument naming a sample that is not there (or read as missing) */
                rec_sinst_t si;
                rec_sample_t sm;
                rec_sampler_inst(x, &si);
                if (rec_sample(si.sample, &sm)) add_miss(MS_SAMPLER, nums[i], sl, si.sample, 0, 0);
            }
        }
    }
    ui_progress(T("CHECKING"), 600, checked);
}

/* ---------------------------------------------------------------- CLEAN UP --- */
static uint32_t bytes[3], counts[3];
static void kb(char *d, uint32_t b)                     /* `512 B`, `1.3K`, `14K` (dialogs.md) */
{
    uint32_t t = (b * 10 + 512) / 1024;
    if (b < 1024) ui_fmt(d, 12, "%u B", b);
    else if (b < 10240) ui_fmt(d, 12, "%u.%uK", t / 10, t % 10);
    else ui_fmt(d, 12, "%uK", (b + 512) / 1024);
}
/* the samples some sampler's instrument names, in any sampler cartridge (a bit a sample's number) */
static void samples_named(uint8_t *named)
{
    uint8_t nums[256];
    memset(named, 0, 32);
    int n = ui_carts(nums, 256);
    for (int i = 0; i < n; i++) {
        rec_cart_t c;
        if (ui_cart_kind(nums[i]) != ENG_SAMPLER || rec_cart(nums[i], &c)) continue;
        for (int sl = 0; sl < c.nslots; sl++) {
            const uint8_t *x = rec_slot(&c, (uint8_t)sl);
            if (x) { rec_sinst_t si; rec_sampler_inst(x, &si); set_bit(named, si.sample); }
        }
    }
}
static void measure(void)
{
    uint8_t nums[256], named[32];
    uint32_t len;
    uint8_t ver;
    memset(bytes, 0, sizeof bytes);
    memset(counts, 0, sizeof counts);
    scan_uses(-1);
    int n = store_list(REC_PATTERN, nums, 256);
    for (int i = 0; i < n; i++) {
        if (nums[i] >= 0xF0 || bit(pat_used, nums[i])) continue;
        counts[0]++;
        for (int sub = 0; sub < 16; sub++) if (store_get(REC_PATTERN, nums[i], (uint8_t)sub, &len, &ver)) bytes[0] += len + 16;
    }
    samples_named(named);                               /* a sample no instrument names, in any sampler */
    n = store_list(REC_SAMPLE, nums, 256);
    for (int i = 0; i < n; i++)
        if (!bit(named, nums[i]) && store_get(REC_SAMPLE, nums[i], 0, &len, &ver)) { counts[1]++; bytes[1] += len + 16; }
    n = ui_carts(nums, 256);
    for (int i = 0; i < n; i++) {
        rec_cart_t c;
        if (nums[i] >= 0xF0 || rec_cart(nums[i], &c)) continue;
        uint8_t used[32];
        inst_used(nums[i], used);
        for (int sl = 0; sl < c.nslots; sl++)
            if (rec_slot(&c, (uint8_t)sl) && !bit(used, sl)) { counts[2]++; bytes[2] += (uint32_t)rec_slot_size(c.kind); }
    }
}
static char ck_title[40];
static const char *clean_title(const ui_check_t *rows, int n)
{
    uint32_t b = 0;
    char k[12];
    for (int i = 0; i < n; i++) if (rows[i].on) b += bytes[i];
    kb(k, b);
    ui_fmt(ck_title, sizeof ck_title, "%s", ui_txt_v(T("CLEAN UP: FREES #"), k));
    return ck_title;
}
static void (*clean_after)(void);
static void cleaned(int a, ui_check_t *rows)
{
    if (a != 1) return;
    uint8_t nums[256];
    int deleted = 0;
    if (rows[0].on) {
        int n = store_list(REC_PATTERN, nums, 256);
        for (int i = 0; i < n; i++) {
            if (nums[i] >= 0xF0 || bit(pat_used, nums[i])) continue;
            ui_edit_del(REC_PATTERN, nums[i], 0);       /* (the pattern whole) */
            deleted++;
        }
    }
    if (rows[1].on) {                                   /* the samples no instrument names, in any sampler (named
                                                         * before any instrument below goes, as measure() counts) */
        uint8_t named[32];
        samples_named(named);
        int n = store_list(REC_SAMPLE, nums, 256);
        for (int i = 0; i < n; i++)
            if (!bit(named, nums[i])) { ui_edit_del(REC_SAMPLE, nums[i], 0); deleted++; }
    }
    int n = ui_carts(nums, 256);
    for (int i = 0; i < n; i++) {
        rec_cart_t c;
        uint8_t *rec = ui_scratch;                      /* (the cartridge's record, changed, then put) */
        uint32_t clen;
        uint8_t cver;
        if (nums[i] >= 0xF0 || ins_cart_copy(nums[i], &c, rec, &clen, &cver)) continue;
        uint8_t used[32];
        if (rows[2].on) inst_used(nums[i], used);
        int changed = 0, kind = ui_cart_kind(nums[i]);
        if (rows[2].on) for (int sl = 0; sl < c.nslots; sl++) {
            const uint8_t *x = rec_slot(&c, (uint8_t)sl);
            if (!x || bit(used, sl)) continue;
            uint32_t at = rec_slot_at(&c, (uint8_t)sl);
            int size = rec_slot_size(c.kind);
            memset(rec + at, 0, (size_t)size);
            /* empty: its name's first character 63 (specs/files/README.md; a sampler's slot in version 2) */
            static const uint16_t NAME_BIT[ENG_COUNT] = {0, 784, 123, 42, 176, 8, 121};
            ui_put_bits(rec + at, NAME_BIT[kind], 6, 63);
            changed = 1;
            deleted++;
        }
        if (changed) ui_edit_put(c.kind, nums[i], 0, cver, rec, clen);
    }
    ui_toast(ui_txt_v(T("CLEANED UP: # DELETED"), ui_dec(deleted)));
    if (clean_after) clean_after();
}
uint32_t ui_clean_up_frees(void) { measure(); return bytes[0] + bytes[1]; }   /* (the rows it starts with ticked) */
void ui_clean_up(void (*after)(void))
{
    measure();
    if (!counts[0] && !counts[1] && !counts[2]) { ui_toast(T("NOTHING TO CLEAN UP")); return; }
    ui_check_t rows[3];
    static const txt_t NAMES[3] = {TX("UNUSED PATTERNS"), TX("UNUSED SAMPLES"), TX("UNUSED INSTRUMENTS")};
    static char names[3][20], notes[3][20];              /* (kept: the list draws them later) */
    for (int i = 0; i < 3; i++) {
        char k[12];
        kb(k, bytes[i]);
        if (counts[i]) ui_fmt(notes[i], sizeof notes[i], "%u  %s", counts[i], k);
        else ui_fmt(notes[i], sizeof notes[i], "%s", T("NONE"));
        rows[i].name = strcpy(names[i], T(NAMES[i]));
        rows[i].note = notes[i];
        rows[i].count = (int)counts[i];
        rows[i].on = i < 2 && counts[i] > 0;            /* the unused instruments not: they are most of a bank */
    }
    clean_after = after;
    ui_checklist(clean_title, rows, 3, T("CLEAN UP"), T("KEEP"), cleaned, 0, 0, 0);
}

/* ------------------------------------------------- a song's own patterns --- */
int ui_song_own(uint8_t song, uint8_t own[32], char *note, int *shared)
{
    ui_song_t s;
    uint8_t seen[32] = {0};
    uint32_t b = 0;
    int n = 0;
    memset(own, 0, 32);
    *shared = 0;
    scan_uses(song);
    if (ui_song(song, &s)) for (int p = 0; p < s.npos; p++) {
        uint8_t q = s.order[p];
        if (q >= 0xF0 || bit(seen, q)) continue;        /* (the built-in ones never) */
        set_bit(seen, q);
        if (bit(pat_used, q)) { ++*shared; continue; }
        uint32_t len, sz = 0;
        uint8_t ver;
        for (int sub = 0; sub < 16; sub++) if (store_get(REC_PATTERN, q, (uint8_t)sub, &len, &ver)) sz += len + 16;
        if (!sz) continue;                              /* (not there: nothing to delete) */
        b += sz;
        set_bit(own, q);
        n++;
    }
    char k[12];
    kb(k, b);
    if (n) ui_fmt(note, 20, "%u  %s", (unsigned)n, k);
    else ui_fmt(note, 20, "%s", T("NONE"));
    return n;
}
