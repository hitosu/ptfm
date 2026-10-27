/* SPDX-License-Identifier: GPL-3.0-only
 * Copyright (C) 2026 Vasily Kazantsev */
/* RECORD PATTERN (specs/ui/dialogs.md#record-pattern): the dialog the EDITOR's SEL menu opens (ui_core.h). Up to 16
 * steps on the white keys, one note (K1), a step's length (K2), the size (K3), written round or once (K4), the
 * notes' length (SELECT); SEQ: the keys pick the note; OCT: the octave. PLAY previews the steps on their own: a
 * pattern of its own the player plays as a source (PL_PREVIEW, timed by the audio) at the screen's tempo, rebuilt as
 * anything changes; REC writes it (the screen's own write), HOME cancels. What is set (ui_rp) stays for the next
 * time until power-off. */
#include <string.h>
#include "ui_core.h"
#include "ui_priv.h"
#include "ui_data.h"
#include "../play/player.h"
#include "../play/rec.h"

ui_rp_t ui_rp = {0, 16, 2, 1, 0, -1};    /* no steps, 16, 1/16, round, ring on, the note from the opener */

/* a step's lengths: a beat's parts; A ROW where none is a row long (specs: with none a row long, A ROW first) */
static const struct { txt_t name; uint8_t d; } LENS[6] = {{TX("1/32"), 8}, {TX("1/16T"), 6}, {TX("1/16"), 4}, {TX("1/8T"), 3},
                                                          {TX("1/8"), 2}, {TX("1/4"), 1}};
#define A_ROW 6
int ui_rp_lens(int rpb, uint8_t *l)     /* the lengths that are whole rows at rpb, in order: how many */
{
    int n = 0, one = 0;
    for (int i = 0; i < 6; i++) if (rpb % LENS[i].d == 0 && rpb / LENS[i].d == 1) one = 1;
    if (!one) l[n++] = A_ROW;
    for (int i = 0; i < 6; i++) if (rpb % LENS[i].d == 0) l[n++] = (uint8_t)i;
    return n;
}
const char *ui_rp_len_name(int i) { return i == A_ROW ? T("A ROW") : T(LENS[i].name); }
int ui_rp_len_rows(int i, int rpb) { return i == A_ROW || rpb % LENS[i].d ? 1 : rpb / LENS[i].d; }
int ui_rp_step_rows(int rpb) { return ui_rp_len_rows(ui_rp.len, rpb); }
static int step_on(int k) { return ui_rp.steps >> k & 1; }
int ui_rp_step_at(int q, int sr, int limit, int once)
{
    int lim = ui_rp.round && !once ? limit : (ui_rp.size * sr < limit ? ui_rp.size * sr : limit);
    return q < lim && q % sr == 0 ? (q / sr) % ui_rp.size : -1;
}
int ui_rp_plan(int n, int start, int wrap, int sr, int once, uint8_t *on, uint8_t *off)
{
    int limit = wrap ? n : n - start, notes = 0;
    memset(on, 0, (size_t)n);
    memset(off, 0, (size_t)n);
    for (int q = 0; q < limit; q += sr) {
        int k = ui_rp_step_at(q, sr, limit, once);
        if (k >= 0 && step_on(k)) on[(start + q) % n] = 1;
    }
    if (ui_rp.step_long)
        for (int q = 0; q < limit; q += sr) {
            if (!on[(start + q) % n] || (!wrap && q + sr >= limit)) continue;
            int e = (start + q + sr) % n;
            if (!on[e]) off[e] = 1;
        }
    for (int r = 0; r < n; r++) notes += on[r];
    return notes;
}

/* ---------------------------------------------------------------- the dialog's state --- */
#define PV_BUF 52                        /* the preview's track: a note and a note off a step at most, 3 B a cell */
static struct {
    ui_rp_open_t o;
    char title[36];                      /* (the opener's, kept) */
    uint8_t keys_note;                   /* SEQ: the keys pick the note */
    uint8_t pv_on, buf, rows, dirty, back;   /* the preview: playing; its buffer; its rows; to build again; what
                                              * played before it */
    uint8_t heard;                       /* a note heard (K1, a step on), 0 none */
    uint32_t heard_off, built;           /* when the heard note ends; the halves at the last build */
    uint16_t snd;
    uint16_t tempo;                      /* the preview's song: the screen's tempo, swing, effects, the mixer */
    uint8_t ticks, rpb, swing, mix0[RM_N];
    const uint8_t *fx;
} rs;
static uint8_t pv_stream[2][PV_BUF];

static pl_inst_t inst_of(uint16_t s) { pl_inst_t in = {(uint8_t)(s >> 8), (uint8_t)s}; return in; }
static void hush(void) { if (rs.heard) { player_key_off(rs.heard); rs.heard = 0; } }
static void hear(int note, uint32_t ms)  /* a note heard on the keys' voices, ms long (0: till hushed) */
{
    hush();
    rs.snd = rs.o.sound ? rs.o.sound() : ui_cur.inst;
    player_key_on((uint8_t)note, inst_of(rs.snd), (uint8_t)ui_set[S_KEYS_VOLUME]);
    rs.heard = (uint8_t)note;
    rs.heard_off = ms ? ui_now() + ms : 0;
}

/* ---------------------------------------------------------------- the preview --- */
static const uint8_t ORDER0 = 0;
static int pv_song(rec_song_t *s)
{
    memset(s, 0, sizeof *s);
    memcpy(s->name, "PREVIEW", 8);
    s->tempo = rs.tempo;
    s->ticks = rs.ticks;
    s->rpb = rs.rpb;
    s->swing = rs.swing;
    s->gvol = 64;
    s->len = 1;
    s->order = &ORDER0;
    for (int t = 0; t < 16; t++) memcpy(s->mix[t], rs.mix0, RM_N);
    s->fx = rs.fx;
    return 0;
}
static int pv_pattern(uint8_t num, rec_pat_t *pt)
{
    if (num) return -1;
    memset(pt, 0, sizeof *pt);
    pt->ntrk = 1;
    pt->rows = rs.rows;
    pt->step = 1;
    pt->trk[0] = pv_stream[rs.buf];
    return 0;
}
static int pv_inst(int track, pl_inst_t *in) { if (track) return 0; *in = inst_of(rs.snd); return 1; }
static const pl_source_t PV_SRC = {pv_song, pv_pattern, pv_inst, 0};

/* the steps as a pattern of their own, round the size, into the buffer the player is not reading */
static void pv_build(void)
{
    uint8_t on[128], off[128];
    int sr = ui_rp_step_rows(rs.rpb), rows = ui_rp.size * sr, n = 0, last = -1, old = rs.rows;
    uint8_t *s = pv_stream[rs.buf ^ 1];
    if (rows > 128) rows = 128;
    ui_rp_plan(rows, 0, 1, sr, 1, on, off);
    for (int r = 0; r < rows && n + 4 <= PV_BUF; r++) {
        if (!on[r] && !off[r]) continue;
        int skip = r - last - 1;
        s[n++] = (uint8_t)(0x80 | (skip > 6 ? 7 : skip));
        s[n++] = (uint8_t)(on[r] ? ui_rp.note : REC_NOTE_OFF);
        if (skip > 6) s[n++] = (uint8_t)skip;   /* (the long skip's byte after the cell's) */
        last = r;
    }
    s[n] = 0;
    rs.buf ^= 1;
    rs.rows = (uint8_t)rows;
    rs.dirty = 0;
    player_state_t st;
    ui_player_state(&st);
    rs.built = st.halves;
    if (rs.pv_on) player_source_changed(rows != old);
}
static void pv_stop(void)
{
    if (!rs.pv_on) return;
    rs.pv_on = 0;
    player_source_end(rs.back);
}
static void pv_start(void)
{
    rec_song_t s;
    player_state_t st;
    ui_player_state(&st);
    if (!rs.o.song || rs.o.song(&s) || !s.fx) { ui_refuse(B_PLAY); return; }
    rs.back = st.what == PL_LOOPS || st.what == PL_PREVIEW ? PL_NONE : st.what;   /* (the loops start from LOOPS) */
    if (rs.o.pause) rs.o.pause();
    hush();
    rs.tempo = s.tempo;
    rs.ticks = s.ticks ? s.ticks : 6;
    rs.rpb = s.rpb ? s.rpb : 4;
    rs.swing = s.swing;
    memcpy(rs.mix0, s.mix[0], RM_N);
    rs.fx = s.fx;
    rs.snd = rs.o.sound ? rs.o.sound() : ui_cur.inst;
    pv_build();
    rs.pv_on = 1;
    player_play_source(PL_PREVIEW, &PV_SRC, 0, 0);
}
static void changed(void) { if (rs.pv_on) rs.dirty = 1; ui_dirty(); }
/* the step the preview plays, or -1 */
static int step_now(void)
{
    if (!rs.pv_on) return -1;
    player_state_t st;
    ui_player_state(&st);
    int sr = ui_rp_step_rows(rs.rpb);
    return st.what == PL_PREVIEW && st.playing && sr ? st.row / sr : -1;
}

/* ---------------------------------------------------------------- input --- */
static void rp_knob(ui_overlay_t *ov, int e, int s, int coarse)
{
    (void)ov;
    int d = s > 0 ? 1 : -1;
    if (e == K_K1) { ui_rp.note = (int16_t)ui_clamp(ui_rp.note + d * (coarse ? 12 : 1), 12, 127); if (!rs.pv_on) hear(ui_rp.note, 200); }
    else if (e == K_K2) {
        uint8_t l[7];
        int n = ui_rp_lens(rs.o.rpb, l), at = 0;
        for (int i = 0; i < n; i++) if (l[i] == ui_rp.len) at = i;
        ui_rp.len = l[ui_clamp(at + d, 0, n - 1)];
    } else if (e == K_K3) ui_rp.size = (uint8_t)ui_clamp(ui_rp.size + d, 1, 16);
    else if (e == K_K4) ui_rp.round = !ui_rp.round;
    else if (e == K_SELECT) ui_rp.step_long = (uint8_t)(s > 0);   /* (clockwise a step long, back ringing on) */
    else return;
    changed();
}
static int rp_key(ui_overlay_t *ov, int n, int down)
{
    (void)ov;
    if (!down) return 1;
    if (rs.keys_note) {                  /* SEQ: any key the note, at the keyboard's octave */
        ui_rp.note = (int16_t)ui_clamp(ui_midi(n), 12, 127);
        if (!rs.pv_on) hear(ui_rp.note, 200);
        changed();
        return 1;
    }
    int k = ui_white_index(n);
    if (k < 0 || k >= ui_rp.size) return 1;
    ui_rp.steps ^= (uint16_t)(1u << k);
    if (step_on(k) && step_now() < 0 && !rs.pv_on) hear(ui_rp.note, 200);   /* (unless the steps are playing) */
    changed();
    return 1;
}
static int rp_tap(ui_overlay_t *ov, int b)
{
    (void)ov;
    if (b == B_SEQ) { rs.keys_note = !rs.keys_note; ui_toast(rs.keys_note ? T("KEYS: THE NOTE") : T("KEYS: THE STEPS")); }
    if (b == B_OCTM || b == B_OCTP) {    /* the keyboard's octave as everywhere, the note an octave with it */
        int d = b == B_OCTP ? 1 : -1;
        if (ui_cur.octave + d < -4 || ui_cur.octave + d > 4 || ui_rp.note + 12 * d < 12 || ui_rp.note + 12 * d > 127) {
            ui_refuse(b);
            return 1;
        }
        ui_cur.octave = (int8_t)(ui_cur.octave + d);
        ui_rp.note = (int16_t)(ui_rp.note + 12 * d);
        if (!rs.pv_on) hear(ui_rp.note, 200);
        char nm[8];
        ui_note_name(nm, ui_rp.note);
        ui_toast(ui_txt_v(T("NOTE #"), nm));
        changed();
    }
    return 1;                            /* (the other buttons do nothing here) */
}
static void rp_play(ui_overlay_t *ov) { (void)ov; if (rs.pv_on) pv_stop(); else pv_start(); }
static void rp_cancel(ui_overlay_t *ov) { pv_stop(); hush(); ui_close(ov); }
static void rp_rec(ui_overlay_t *ov)
{
    if (!(ui_rp.steps & ((1u << ui_rp.size) - 1u))) { ui_refuse(B_REC); ui_toast(T("NO STEPS ON")); return; }
    pv_stop();
    hush();
    if (rs.o.write && rs.o.write()) ui_close(ov);
}

/* ---------------------------------------------------------------- the lights --- */
static int rp_leds(ui_overlay_t *ov, uint8_t *keys)
{
    (void)ov;
    memset(keys, 0, 27);
    if (rs.keys_note) {                  /* the note's key, where the keyboard's octave has it */
        int n = ui_rp.note - ui_midi(0);
        if (n >= 0 && n < 27) keys[n] = LV_FULL;
        return 1;
    }
    int pl = step_now();
    for (int k = 0; k < 16; k++) {
        int on = step_on(k);
        keys[UI_WHITE[k]] = (uint8_t)(k >= ui_rp.size ? 0 : k == pl ? (on ? 0 : LV_FULL) : on ? LV_FULL : k % 4 == 0 ? LV_DIM : 0);
    }
    return 1;
}
static int rp_lit(ui_overlay_t *ov, int b)
{
    (void)ov;
    if (b == B_REC) return LV_DIM;
    if (b == B_SEQ) return rs.keys_note ? LV_FULL : LV_DIM;
    if (b == B_PLAY) return 0;           /* (PLAY green: dim, full while the preview plays) */
    if (b == B_COUNT) return rs.pv_on ? LV_FULL : LV_DIM;
    return LV_NONE;
}

/* ---------------------------------------------------------------- the panel --- */
static void line(int y, const char *k, int kcol, const char *name, const char *v, const char *more, int mcol)
{
    g_run_t r[4] = {{k, (uint8_t)kcol}, {name, C_LGREY}, {v, C_WHITE}, {more ? more : "", (uint8_t)mcol}};
    g_runs(14, y, &FONT_T, r, more ? 4 : 3);
}
static void rp_draw(ui_overlay_t *ov)
{
    (void)ov;
    int h = 180, y = BODY_Y + (BODY_H - h) / 2, pl = step_now(), eng = 0;
    uint16_t snd = rs.o.sound ? rs.o.sound() : ui_cur.inst;
    char nm[12], b[24], ins[16];
    if (rs.heard && rs.heard_off && (int32_t)(ui_now() - rs.heard_off) >= 0) hush();
    if (rs.pv_on && rs.dirty) {          /* (the player has had two halves with the buffer it reads now) */
        player_state_t st;
        ui_player_state(&st);
        if (st.halves - rs.built >= 2) pv_build();
    }
    eng = ui_cart_kind((uint8_t)(snd >> 8));
    int col = eng > 0 && eng < ENG_COUNT ? ENG_COL[eng] : C_LGREY;
    ui_panel(8, y, 224, h, 0, C_LGREY);
    g_text(g_centre(&FONT_T, rs.o.title, 8, 224), y + 6, &FONT_T, rs.o.title, C_WHITE);
    for (int k = 0; k < 16; k++) {       /* the steps, in fours; the one playing boxed */
        int x = 13 + k * 13 + (k / 4) * 2;
        if (k >= ui_rp.size) { g_box(x, y + 22, 11, 14, C_DEEP + C_DGREY); continue; }
        g_rect(x, y + 22, 11, 14, step_on(k) ? col : C_DEEP + C_DGREY);
        if (k == pl) g_box(x - 1, y + 21, 13, 16, C_WHITE);
    }
    ui_note_name(nm, ui_rp.note);
    if (!ui_inst_name(snd, ins)) ins[0] = 0;
    char sp[24];
    ui_fmt(sp, sizeof sp, "  %s", ins);
    line(y + 44, T("K1"), C_YELLOW, T(" NOTE   "), nm, sp, col);
    line(y + 56, T("K2"), C_YELLOW, T(" STEP   "), ui_rp_len_name(ui_rp.len), 0, 0);
    ui_fmt(b, sizeof b, "%d %s", ui_rp.size, ui_rp.size > 1 ? T("STEPS") : T("STEP"));
    line(y + 68, T("K3"), C_YELLOW, T(" SIZE   "), b, 0, 0);
    line(y + 80, T("K4"), C_YELLOW, T(" WRITE  "), ui_rp.round ? T(rs.o.round) : T("ONCE"), 0, 0);
    {
        g_run_t r[3] = {{T("SELECT"), C_YELLOW}, {T(" NOTES "), C_LGREY},
                        {ui_rp.step_long ? T("A STEP LONG") : T("RING TO THE NEXT"), C_WHITE}};
        g_runs(14, y + 92, &FONT_T, r, 3);
    }
    if (rs.o.where) {
        g_run_t r[6];
        char wb[48];
        int n = rs.o.where(r, wb, sizeof wb);
        g_runs(14, y + 104, &FONT_T, r, n);
    }
    {
        g_run_t r[2] = {{T("PLAY"), C_YELLOW}, {rs.pv_on ? T(": STOP HEARING THEM") : T(": HEAR THE STEPS ON THEIR OWN"), C_LGREY}};
        g_runs(14, y + 116, &FONT_T, r, 2);
    }
    g_run_t k[6] = {{T("KEYS"), C_YELLOW}, {rs.keys_note ? T(": NOTE  ") : T(": STEPS  "), C_LGREY},
                    {T("SEQ"), C_YELLOW}, {rs.keys_note ? T(": STEPS  ") : T(": NOTE  "), C_LGREY},
                    {T("OCT"), C_YELLOW}, {T(": OCTAVE"), C_LGREY}};
    ui_ask_foot(y + 132, y + h, T("RECORD"), T("CANCEL"), k, 6, B_HOME, B_REC);
}

void ui_rp_open(const ui_rp_open_t *o)
{
    uint8_t l[7];
    int n = ui_rp_lens(o->rpb, l), ok = 0;
    rs.o = *o;
    strncpy(rs.title, o->title ? o->title : "", sizeof rs.title - 1);
    rs.title[sizeof rs.title - 1] = 0;
    rs.o.title = rs.title;
    if (ui_rp.note < 0) ui_rp.note = (int16_t)ui_clamp(o->note, 12, 127);
    for (int i = 0; i < n; i++) if (l[i] == ui_rp.len) ok = 1;
    if (!ok) {                           /* (another screen's grid: the length a row long, else the first) */
        ui_rp.len = l[0];
        for (int i = 0; i < n; i++) if (l[i] != A_ROW && o->rpb / LENS[l[i]].d == 1) ui_rp.len = l[i];
    }
    rs.keys_note = 0;
    rs.pv_on = 0;
    rs.dirty = 0;
    rs.heard = 0;
    rs.rpb = (uint8_t)o->rpb;
    ui_overlay_t ov = {.kind = OV_OTHER, .opener = (int8_t)o->opener, .knob = rp_knob, .tap = rp_tap, .key = rp_key,
                       .cancel = rp_cancel, .draw = rp_draw, .key_leds = rp_leds, .play = rp_play, .rec = rp_rec,
                       .lit = rp_lit};
    ui_open(&ov);
}
