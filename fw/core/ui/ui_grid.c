/* SPDX-License-Identifier: GPL-3.0-only
 * Copyright (C) 2026 Vasily Kazantsev */
/* The pattern grid (ui_grid.h). Cells are read through the record's track streams (play/rec.h), a row
 * at a time in order. */
#include <string.h>
#include "ui_core.h"
#include "ui_grid.h"
#include "ui_data.h"
#include "../play/rec.h"

static const ui_view_t VIEWS[4] = {{8, 12, 16, 12, 16}, {4, 16, 20, 12, 12}, {2, 24, 24, 14, 8}, {1, 24, 24, 14, 8}};
const ui_view_t *ui_view(int n) { return n == 8 ? &VIEWS[0] : n == 4 ? &VIEWS[1] : n == 2 ? &VIEWS[2] : &VIEWS[3]; }

void ui_grid_init(ui_grid_t *o)
{
    memset(o, 0, sizeof *o);
    o->view = 8;
    o->cur_row = o->cur_track = o->play = o->col = o->slot = -1;
    o->block[0] = -1;
    o->rpb = 4;
}

int ui_cmd_group(uint8_t c) { return c >= 0x40 && c <= 0x7F ? 1 : c >= 0x80 && c <= 0x8F ? 2 : c >= 0x90 && c <= 0xBF ? 3 : 0; }
void ui_cmd_text(uint8_t c, uint8_t v, char *out)
{
    static const char VCOL[10] = {'-', '+', 'D', 'U', 'S', 'V', 'P', '<', '>', 'M'};
    static const char B36[] = "0123456789ABCDEFGHIJKLMNOPQRSTUVWXYZ";
    int g = ui_cmd_group(c);
    if (g) { ui_fmt(out, 4, "%c%02X", g == 1 ? '*' : g == 2 ? '=' : '#', v); return; }
    if (c >= 0x24 && c <= 0x2D) { ui_fmt(out, 4, "~%c%X", VCOL[c - 0x24], v & 15); return; }
    if (c == 0xC0) { strcpy(out, T("ACC")); return; }
    if (c == 0xC1) { strcpy(out, T("SLD")); return; }
    if (c == 0xC2) { strcpy(out, T("A+S")); return; }
    if (c == 0xD3) { strcpy(out, T("LEG")); return; }
    if (c >= 0xC3 && c <= 0xD2) { ui_fmt(out, 4, "!%02X", v); return; }
    ui_fmt(out, 4, "%c%02X", c < 36 ? B36[c] : '?', v);
}
/* ui_cmd_short: ui_cmds.c */

/* the effects' colours (specs/ui/colours.md), by the FX commands' code ranges (commands.md) */
static int fx_col(uint8_t c)
{
    if (c >= 0xB0 && c <= 0xB5) return C_CYAN;          /* FILTER's envelope */
    if (c >= 0xAD) return C_LBLUE;                      /* FX ON / OFF: the commands' own colour */
    if (c < 0x94) return C_LBLUE;                       /* GATE */
    if (c < 0x96) return C_CYAN;                        /* FILTER */
    if (c < 0x98) return C_LRED;                        /* DRIVE */
    if (c < 0x9B) return C_YELLOW;                      /* LO-FI */
    if (c < 0x9F) return C_LCYAN;                       /* CHORUS */
    if (c < 0xA4) return C_LGREEN;                      /* DELAY */
    if (c < 0xA8) return C_LMAGENTA;                    /* REVERB */
    if (c < 0xAA) return C_WHITE;                       /* LIMITER */
    return C_BROWN;                                     /* DUCK */
}
static int is_global(uint8_t c, uint8_t v)
{
    return c == 0x0B || c == 0x0D || c == 0x0F || c == 0x10 || c == 0x11 || (c == 0x0E && ((v >> 4) == 6 || (v >> 4) == 0xE))
        || ui_cmd_group(c) == 3 || (c >= 0xC3 && c <= 0xD2);
}
static void draw_cmd(int x, int y, const uint8_t *cmd, uint16_t inst, int col, int ignored)
{
    char s[4];
    if (!cmd) { g_text(x, y, &FONT_T, "...", C_DGREY); return; }
    ui_cmd_text(cmd[0], cmd[1], s);
    if (ignored) { g_text(x, y, &FONT_T, s, C_DGREY); return; }
    int g = ui_cmd_group(cmd[0]), sc = col;
    if (g == 1) { char nm[12]; if (ui_inst_name(inst, nm)) sc = ENG_COL[ui_cart_kind((uint8_t)(inst >> 8))]; }
    if (g == 3) sc = fx_col(cmd[0]);
    char a[2] = {s[0], 0};
    g_text(x, y, &FONT_T, a, sc);
    g_text(x + 6, y, &FONT_T, s + 1, col);
}

static int missing(const rec_cell_t *c)
{
    char nm[12];
    return c && (c->has & RC_INS) && !ui_inst_name((uint16_t)(c->cart << 8 | c->slot), nm);
}
static int note_col(const rec_cell_t *c, int inst)
{
    char nm[12];
    if (c->note == REC_NOTE_OFF) return C_LGREY;
    if (missing(c)) return C_RED;
    if (inst >= 0 && ui_inst_name((uint16_t)inst, nm)) return ENG_COL[ui_cart_kind((uint8_t)(inst >> 8))];
    return C_LGREY;
}
static void note_name(const rec_cell_t *c, char *s)
{
    if (!c || !(c->has & RC_NOTE)) { strcpy(s, "---"); return; }
    ui_note_name(s, c->note == REC_NOTE_OFF ? 1 : c->note);
}
static int empty_cell(const rec_cell_t *c) { return !c || !c->has; }

static void draw_cell(const rec_cell_t *c, int inst, int x, int y, int h, int view, int is_f, int muted, int fusion, int slot,
                      int sel)
{
    char s[8];
    int has_note = c && (c->has & RC_NOTE);
#define DIM(col) (muted ? C_DGREY : (col))
    if (view == 8) {
        int r0 = is_f ? 0 : 2, r1 = is_f ? 16 : 14;
        if (!has_note) { g_text_crop(x, y, &FONT_S, !empty_cell(c) ? "..." : "---", C_DGREY, r0, r1); return; }
        if (missing(c)) g_rect(x, y, 24, h, C_DEEP + C_RED);
        note_name(c, s);
        g_text_crop(x, y, &FONT_S, s, DIM(note_col(c, inst)), r0, r1);
        return;
    }
    if (view == 4) {
        int ty = y + (h - 12) / 2;
        if (empty_cell(c)) { g_text(x, ty, &FONT_T, "--- ..:..", C_DGREY); return; }
        int col = DIM(note_col(c, inst));
        if (missing(c)) g_rect(x + 24, ty, 30, 12, C_DEEP + C_RED);
        note_name(c, s);
        g_text(x, ty, &FONT_T, s, !has_note ? C_LGREY : col);
        if (c->has & RC_INS) { ui_ref_text(s, (uint16_t)(c->cart << 8 | c->slot)); g_text(x + 24, ty, &FONT_T, s, missing(c) ? C_RED : col); }
        else g_text(x + 24, ty, &FONT_T, "..:..", C_DGREY);
        return;
    }
    int col = c ? DIM(note_col(c, inst)) : C_DGREY;
    if (view == 1) {
        int eng = inst >= 0 ? ui_cart_kind((uint8_t)(inst >> 8)) : 0;
        note_name(c, s);
        g_text(x, y + 4, &FONT_S, s, has_note ? col : C_DGREY);
        int ix = x + 28;
        if (missing(c)) g_rect(ix, y, 30, 12, C_DEEP + C_RED);
        if (c && (c->has & RC_INS)) { ui_ref_text(s, (uint16_t)(c->cart << 8 | c->slot)); g_text(ix, y, &FONT_T, s, missing(c) ? C_RED : col); }
        else g_text(ix, y, &FONT_T, "..:..", C_DGREY);
        if (c && (c->has & RC_VOL)) g_text(ix, y + 12, &FONT_T, ui_hex2(c->vol), sel == 0 ? C_WHITE : DIM(C_BROWN));
        else g_text(ix, y + 12, &FONT_T, "..", sel == 0 ? C_WHITE : C_DGREY);
        for (int k = 0; k < 4; k++) {
            int cx = x + 64 + (k % 2) * 84, cy = y + (k >> 1) * 12, on = sel == k + 1;
            if (!c || k >= c->n) { g_text(cx, cy, &FONT_T, "...", on ? C_WHITE : C_DGREY); continue; }
            const uint8_t *cmd = c->cmd[k];
            int ignored = fusion && is_global(cmd[0], cmd[1]);
            const char *name = ui_cmd_short(cmd[0], cmd[1], eng);
            if (on) { char t[4]; ui_cmd_text(cmd[0], cmd[1], t); g_text(cx, cy, &FONT_T, t, C_WHITE); g_text(cx + 24, cy, &FONT_T, name, C_WHITE); continue; }
            draw_cmd(cx, cy, cmd, (uint16_t)inst, DIM(C_LBLUE), ignored);
            g_text(cx + 24, cy, &FONT_T, name, ignored ? C_DGREY : DIM(C_LBLUE));
        }
        return;
    }
    /* 2 tracks: the note in S (B in the focus row), the rest in T, one command, the pips */
    const ui_font_t *nf = is_f ? &FONT_B : &FONT_S;
    int nw = is_f ? 36 : 24, ny = is_f ? y : y + 4, ty = y + 6;
    note_name(c, s);
    g_text(x, ny, nf, s, has_note ? col : C_DGREY);
    int tx = x + nw + 4;
    if (missing(c)) g_rect(tx, ty, 30, 12, C_DEEP + C_RED);
    if (c && (c->has & RC_INS)) { ui_ref_text(s, (uint16_t)(c->cart << 8 | c->slot)); g_text(tx, ty, &FONT_T, s, missing(c) ? C_RED : col); }
    else g_text(tx, ty, &FONT_T, "..:..", C_DGREY);
    tx += 36;
    if (c && (c->has & RC_VOL)) g_text(tx, ty, &FONT_T, ui_hex2(c->vol), DIM(C_BROWN));
    else g_text(tx, ty, &FONT_T, "..", C_DGREY);
    if (sel == 0) g_hline(tx, ty + 12, 12, C_WHITE);
    tx += 18;
    int k = slot > 0 ? slot - 1 : 0, n = c ? c->n : 0;
    const uint8_t *cmd = k < n ? c->cmd[k] : 0;
    draw_cmd(tx, ty, cmd, (uint16_t)inst, DIM(C_LBLUE), fusion && cmd && is_global(cmd[0], cmd[1]));
    if (sel > 0) g_hline(tx, ty + 12, 18, C_WHITE);
    for (int i = 0; i < n; i++) g_rect(tx + i * 5, ty + 14, 3, 2, i == k ? C_WHITE : DIM(C_LBLUE));
#undef DIM
}

void ui_grid(uint8_t pat, const ui_grid_t *o)
{
    rec_pat_t p;
    if (rec_pattern(pat, &p)) return;
    const ui_view_t *v = ui_view(o->view);
    int lab_w = 14, n = v->n < p.ntrk - o->left ? v->n : p.ntrk - o->left, col_w = (240 - lab_w) / v->n;
    int y = o->y0 ? o->y0 : BODY_Y, rpb = o->rpb > 0 ? o->rpb : 4;
    rec_cur_t cur[16];
    int inst_at[16];                                    /* each shown track's instrument at the row reached */
    for (int k = 0; k < n; k++) {
        rec_cur_set(&cur[k], p.trk[o->left + k]);
        inst_at[k] = -1;
    }
    /* the track numbers; in the 2-track view, what K1 picked above its column; in the 1-track view the
     * track's instrument */
    for (int k = 0; k < n; k++) {
        int t = o->left + k, x = lab_w + k * col_w;
        const char *s = ui_hex2(t);
        if (o->view == 2) {
            g_text(x, y + (v->head - 12), &FONT_T, s, ui_muted(t) ? C_DGREY : t == o->cur_track ? C_WHITE : C_CYAN);
            if (o->slot >= 0) {
                char sl[8];
                if (o->slot) ui_fmt(sl, sizeof sl, T("CMD%d"), o->slot); else strcpy(sl, T("VOL"));
                g_text(o->slot ? x + 79 : x + 61, y + (v->head - 12), &FONT_T, sl, C_DGREY);
            }
            continue;
        }
        if (o->view == 1) {
            g_text(x, y + (v->head - 12), &FONT_T, s, ui_muted(t) ? C_DGREY : o->cur_row >= 0 ? C_WHITE : C_CYAN);
            int at = o->cur_row >= 0 ? o->cur_row : o->play >= 0 ? o->play : o->top;
            rec_cur_t c1;
            rec_cell_t cell;
            rec_cur_set(&c1, p.trk[t]);
            int got = rec_cur_at(&c1, at, &cell), ti = got && (cell.has & RC_INS) ? cell.cart << 8 | cell.slot
                                                                              : c1.ins ? c1.cart << 8 | c1.slot : -1;
            char nm[12];
            if (ti >= 0 && ui_inst_name((uint16_t)ti, nm))
                g_text(x + 18, y + (v->head - 12), &FONT_T, nm, ui_muted(t) ? C_DGREY : ENG_COL[ui_cart_kind((uint8_t)(ti >> 8))]);
            continue;
        }
        g_text(x + (col_w - 12) / 2, y + (v->head - 12), &FONT_T, s,
               ui_muted(t) ? C_DGREY : t == (o->cur_row >= 0 ? o->cur_track : o->col) ? C_WHITE : C_CYAN);
    }
    if (o->left > 0) g_text(0, y, &FONT_T, "<", C_DGREY);
    if (o->left + v->n < p.ntrk) g_text(234, y, &FONT_T, ">", C_DGREY);
    y += v->head;
    for (int r = o->top; r < p.rows && r < o->top + v->rows; r++) {
        int is_f = r == o->cur_row, h = is_f ? v->focus_h : v->row_h;
        if (o->col >= o->left && o->col < o->left + n) g_rect(lab_w + (o->col - o->left) * col_w, y, col_w - 1, h, C_DEEP + C_DGREY);
        if (o->play == r) g_rect(0, y, 240, h, C_DEEP + C_BLUE);
        if (o->hot && (o->hot[r >> 3] >> (r & 7) & 1)) g_rect(0, y, 240, h, C_DEEP + C_GREEN);   /* a FUSION's row a key plays */
        if (is_f) { g_hline(0, y, 240, C_DGREY); g_hline(0, y + h - 1, 240, C_DGREY); }
        int ty = y + (h - 12) / 2, beat = o->fusion ? r % 12 == 0 : r % rpb == 0;
        g_text(0, ty, &FONT_T, ui_hex2(r), r % (rpb * 4) == 0 && !o->fusion ? C_LCYAN : beat ? C_CYAN : C_DGREY);
        for (int k = 0; k < n; k++) {
            int t = o->left + k, x = lab_w + k * col_w;
            if (o->block[0] >= 0 && r >= o->block[0] && r <= o->block[1] && t >= o->block[2] && t <= o->block[3])
                g_rect(x, y + (is_f ? 1 : 0), col_w - 1, h - (is_f ? 2 : 0), C_DEEP + C_MAGENTA);
            rec_cell_t cell;
            int got = rec_cur_at(&cur[k], r, &cell);
            if (got && (cell.has & RC_INS)) inst_at[k] = cell.cart << 8 | cell.slot;
            else if (cur[k].ins) inst_at[k] = cur[k].cart << 8 | cur[k].slot;
            if (o->over && o->over(r, t, &cell)) {                      /* under a carried block: its cell */
                got = cell.has != 0;
                g_rect(x, y, col_w - 1, h, C_DEEP + C_MAGENTA);
                if (got && (cell.has & RC_INS)) inst_at[k] = cell.cart << 8 | cell.slot;
            }
            /* the instrument a cell sounds: its own, else the track's last before it */
            int inst = inst_at[k];
            int sel = o->cur_row >= 0 && is_f && t == o->cur_track ? o->slot : -2;
            draw_cell(got ? &cell : 0, inst, x, y, h, o->view, is_f, ui_muted(t), o->fusion, o->slot, sel);
            if (o->cur_row >= 0 && is_f && t == o->cur_track) g_box(x - 1, y, col_w, h, C_WHITE);
        }
        y += h;
    }
}
