/* SPDX-License-Identifier: GPL-3.0-only
 * Copyright (C) 2026 Vasily Kazantsev */
/* LOOPS' pattern recorder (specs/ui/screens/loops.md#the-pattern-recorder-arp), ARP's box over the loops; the
 * playground's is scr-loops.js' RECORDER. 4 rows of up to 16 steps, a note a row (a row never set: the note of the row
 * above, the first C at the keyboard's octave when it first opens), STEP SIZE (the step lengths GRID makes whole rows of), LENGTH 4-16
 * steps, TIMES (the first loop's), the notes' length; kept until power-off, never saved. Its preview is the loop it
 * would write (player_lp_preview: a loop more of the player's lanes): in the loops' time while they play (ARP switches
 * its sound), else alone from its first step (player_lp_preview_alone). REC writes it (detail 7) and closes; HOME
 * closes, nothing written. */
#include <string.h>
#include "ui_core.h"
#include "ui_data.h"
#include "ui_lp.h"
#include "../play/player.h"
#include "../play/loops.h"
#include "../play/rec.h"
#include "../store/store.h"

/* what is set, kept until power-off: each row's steps and note (0: never set), the row selected, LENGTH, the step's
 * length (ui_rp_lens' index: 1/16), TIMES, A STEP LONG, the keys setting the note (SEL tapped) */
static struct { uint16_t steps[4]; uint8_t note[4], sel, len, size, times, step_long, keys; } pr = {{0}, {0}, 0, 16, 2, 1, 0, 0};
/* the box open: the loop it writes into (-1 a new one), its preview on (pv), the note heard and till when */
static struct { int8_t target; uint8_t open, pv, heard; uint32_t heard_off; } rs;

static int rpb(void) { return loops_rpb(); }
static int step_rows(void) { return ui_rp_len_rows(pr.size, rpb()); }
static int base(void) { return pr.len * step_rows(); }
static int first(void) { return !loops_n(); }
static int note_of(int r)
{
    for (int k = r; k >= 0; k--) if (pr.note[k]) return pr.note[k];
    return ui_clamp(60 + 12 * ui_cur.octave, 12, 127);
}
static uint16_t steps_of(int r) { return (uint16_t)(pr.steps[r] & ((1u << pr.len) - 1u)); }
static int lanes(void) { int n = 0; for (int r = 0; r < 4; r++) n += steps_of(r) != 0; return n; }
static int times_most(void) { int t = loops_max_rows() / base(); return t < 1 ? 1 : t > 8 ? 8 : t; }
static int times(void) { return pr.times < times_most() ? pr.times : times_most(); }
static int first_rows(void)                             /* the first loop's length: TIMES patterns in whole bars */
{
    int br = loops_bar_rows(), b = (times() * base() + br - 1) / br;
    return (b > loops_max_bars() ? loops_max_bars() : b) * br;
}
static int target(void) { return rs.target >= 0 && rs.target < loops_n() ? rs.target : -1; }
uint16_t lp_rec_inst(void) { return target() >= 0 ? loops_inst(target()) : ui_cur.inst; }
static int colour(void) { return LP_COL[target() >= 0 ? loops_colour(target()) : lp_next_colour()]; }
int lp_rec_on(void) { return rs.open; }

/* the preview: the loop it would write, its filled rows its lanes (as REC writes them), heard while pv */
static void send(void)
{
    pl_lp_grid_t g;
    int k = 0;
    memset(&g, 0, sizeof g);
    for (int r = 0; r < 4; r++)
        if (steps_of(r)) { g.steps[k] = steps_of(r); g.note[k++] = (uint8_t)note_of(r); }
    g.step_rows = (uint8_t)step_rows();
    g.length = pr.len;
    g.ring = !pr.step_long;
    g.mute = !rs.pv;
    g.vol = (uint8_t)ui_set[S_KEYS_VOLUME];
    g.track = (uint8_t)(target() >= 0 ? target() : loops_n());
    g.times = (uint8_t)times();
    g.rows = (uint8_t)first_rows();
    g.inst.cart = (uint8_t)(lp_rec_inst() >> 8);
    g.inst.slot = (uint8_t)lp_rec_inst();
    player_lp_preview(&g);
}
static int alone(void) { player_state_t st; ui_player_state(&st); return st.lp_alone; }
static int playing(void) { return rs.pv && (lp_playing_here() || alone()); }
/* the step playing: where the loops are (or the preview alone), in its steps; -1 none */
static int step_now(void)
{
    player_state_t st;
    if (!playing()) return -1;
    ui_player_state(&st);
    int b = base(), r = st.row;
    if (first() && r / b >= times()) return -1;         /* (the first loop's silent end) */
    return r % b / step_rows();
}
static void hush(void) { if (rs.heard) { player_key_off(rs.heard); rs.heard = 0; } }
static void hear(int note)                              /* a row's note heard, a step long (0.15 s at least) */
{
    int ms = step_rows() * loops_ticks() * 2500 / loops_tempo();
    hush();
    pl_inst_t in = {(uint8_t)(lp_rec_inst() >> 8), (uint8_t)lp_rec_inst()};
    player_key_on((uint8_t)note, in, (uint8_t)ui_set[S_KEYS_VOLUME]);
    rs.heard = (uint8_t)note;
    rs.heard_off = ui_now() + (uint32_t)(ms < 150 ? 150 : ms);
}
void lp_rec_poll(void) { if (rs.heard && (int32_t)(ui_now() - rs.heard_off) >= 0) hush(); }
static void set_note(int n)
{
    pr.note[pr.sel] = (uint8_t)ui_clamp(n, 12, 127);
    hear(pr.note[pr.sel]);
    send();
}

/* ------------------------------------------------------------------- writing --- */
/* REC (detail 7): the filled rows in order the lanes (MONO with one), a note at KEYS VOLUME at each step on, with A
 * STEP LONG a note off a step after each note whose next step is off (round to the base's start); its base the steps;
 * the first loop TIMES repeats in its length, a later one as many as cover the length, the last cut. Into the loop it
 * opened on (its place, colour, instrument and mix kept), else a new loop of the current instrument; going on from
 * where the preview was */
static int write(void)
{
    int nl = lanes(), sr = step_rows(), b = base(), t = target(), at = t, was_first = first(), r = LP_OK;
    char nm[8], msg[32];
    if (!nl) { ui_refuse(B_REC); ui_toast(T("NO STEPS ON")); return 0; }
    lp_enc_t e;
    rec_cell_t c;
    uint16_t st[4];
    uint8_t nt[4];
    int k = 0;
    for (int row = 0; row < 4; row++) if (steps_of(row)) { st[k] = steps_of(row); nt[k++] = (uint8_t)note_of(row); }
    loops_enc_start(&e, ui_scratch, STORE_LOOP_STREAM_MAX);
    for (int j = 0; j < pr.len; j++)
        for (int l = 0; l < nl; l++) {
            memset(&c, 0, sizeof c);
            if (st[l] >> j & 1u) { c.has = RC_NOTE | RC_VOL; c.note = nt[l]; c.vol = (uint8_t)ui_set[S_KEYS_VOLUME]; }
            else if (pr.step_long && st[l] >> ((j + pr.len - 1) % pr.len) & 1u) { c.has = RC_NOTE; c.note = REC_NOTE_OFF; }
            else continue;
            loops_enc_cell(&e, 4 * j * sr + l, &c);
        }
    uint32_t len = loops_enc_end(&e), used = ui_edit_used();
    int heard = playing();
    if (t < 0) {                                        /* a new loop (the first: the length TIMES its pattern) */
        at = loops_new((uint8_t)(ui_cur.inst >> 8), (uint8_t)ui_cur.inst);
        if (at < 0) r = at;
        else if (was_first) r = loops_set_rows(first_rows());
    }
    if (!r) r = len ? loops_put(at, b, was_first ? times() : (loops_rows() + b - 1) / b, nl == 1, ui_scratch, len, nl) : LP_FULL;
    if (r && t < 0 && at >= 0) { if (was_first) loops_clear(); else loops_peel(at); }   /* (nothing written) */
    if (ui_edit_loops(r, used)) return 0;
    if (heard) player_lp_preview_keep(at);              /* (its notes go on as the loop's, without a break) */
    else player_lp_preview(0);
    loops_changed(1);
    rs.pv = 0;
    lp_loop_name(nm, at);
    if (t >= 0) ui_toast(ui_txt_v(T("PATTERN INTO #"), nm));
    else {
        int bars = loops_rows() / loops_bar_rows();
        ui_fmt(msg, sizeof msg, T("PATTERN: %s, %s"), nm, bars == 1 ? T("1 BAR") : ui_txt_v(T("# BARS"), ui_dec(bars)));
        ui_toast(msg);
        loops_set_sel(at + 1);
    }
    return 1;
}

/* ---------------------------------------------------------------------- input --- */
static void rec_close(ui_overlay_t *o)
{
    player_lp_preview(0);                               /* (alone: the replay stops, the loops' place kept) */
    hush();
    rs.open = 0;
    if (o) ui_close(o);
}
void lp_rec_close(void)                                 /* (leaving: the change of screen closes the box itself) */
{
    ui_overlay_t *o = ui_top();
    if (rs.open) rec_close(o && o->data == (void *)&rs ? o : 0);
}
/* PLAY: the loops played or paused, as on the screen; pausing them, a preview heard goes on alone from where it was;
 * starting them, the preview jumps to their place */
static void rec_play(ui_overlay_t *o)
{
    (void)o;
    if (lp_playing_here()) {
        if (rs.pv) player_lp_preview_alone(1);          /* (the loops pause, the preview's replay runs on) */
        else player_pause();
        return;
    }
    if (!loops_n()) { lp_play(); return; }
    if (alone()) player_continue();                     /* (the loops from their place, the preview with them) */
    else lp_play();
}
/* ARP: the loops playing, the preview's sound off and on; else it starts and stops alone, from its first step */
static void arp(void)
{
    if (lp_playing_here()) { rs.pv = !rs.pv; send(); return; }
    if (alone()) { player_lp_preview_alone(0); rs.pv = 0; send(); return; }
    rs.pv = 1;
    send();
    player_lp_preview_alone(1);
}
static void rec_knob(ui_overlay_t *o, int e, int s, int coarse)
{
    (void)o;
    int d = s > 0 ? 1 : -1;
    if (e == K_SELECT) pr.sel = (uint8_t)ui_clamp(pr.sel + d, 0, 3);
    else if (e == K_ALG) { lp_tempo_turn(s, coarse); return; }
    else if (e == K_K1) {
        uint8_t l[7];
        int n = ui_rp_lens(rpb(), l), i = 0;
        for (int k = 0; k < n; k++) if (l[k] == pr.size) i = k;
        pr.size = l[ui_clamp(i + d, 0, n - 1)];
        pr.times = (uint8_t)times();
    } else if (e == K_K2) { pr.len = (uint8_t)ui_clamp(pr.len + 4 * d, 4, 16); pr.times = (uint8_t)times(); }
    else if (e == K_K3) { set_note(note_of(pr.sel) + s); return; }
    else if (e == K_K4 && first()) pr.times = (uint8_t)ui_clamp(pr.times + d, 1, times_most());
    else return;
    send();
}
static int rec_tap(ui_overlay_t *o, int b)
{
    (void)o;
    if (b == B_ARP) arp();
    else if (b == B_SEL) { pr.keys = !pr.keys; ui_toast(pr.keys ? T("KEYS: THE NOTE") : T("KEYS: THE STEPS")); }
    else if (b == B_GLO) { pr.step_long = !pr.step_long; ui_toast(pr.step_long ? T("NOTES A STEP LONG") : T("NOTES RING TO THE NEXT")); send(); }
    else if (b == B_OCTM || b == B_OCTP) {             /* the keyboard's octave, the row's note with it */
        int d = b == B_OCTP ? 1 : -1, n = note_of(pr.sel) + 12 * d;
        char nm[8];
        if (ui_cur.octave + d < -4 || ui_cur.octave + d > 4 || n < 12 || n > 127) { ui_refuse(b); return 1; }
        ui_cur.octave = (int8_t)(ui_cur.octave + d);
        set_note(n);
        ui_note_name(nm, n);
        ui_toast(ui_txt_v(T("NOTE #"), nm));
    } else if (b == B_SAVE) return 0;                   /* (SAVE as everywhere) */
    return 1;
}
/* a white key: its step on or off (heard while the preview is stopped); after SEL, or SEL held, a key sets the row's
 * note at the keyboard's octave */
static int rec_key(ui_overlay_t *o, int n, int down)
{
    (void)o;
    if (!down) return 1;
    if (pr.keys || ui_held(B_SEL)) {
        int m = ui_midi(n);
        if (ui_held(B_SEL)) ui_consume(B_SEL);
        if (m >= 12 && m <= 127) set_note(m);
        pr.keys = 0;
        return 1;
    }
    int k = ui_white_index(n);
    if (k < 0 || k >= pr.len) return 1;
    pr.steps[pr.sel] ^= (uint16_t)(1u << k);
    if (pr.steps[pr.sel] >> k & 1u && !playing()) hear(note_of(pr.sel));
    send();
    return 1;
}
static void rec_rec(ui_overlay_t *o) { if (write()) rec_close(o); }
static void rec_cancel(ui_overlay_t *o) { rec_close(o); }
/* a step on full, off dark (steps 1, 5, 9, 13 dim), past LENGTH dark, the one playing turned over; setting the note:
 * its key */
static int rec_leds(ui_overlay_t *o, uint8_t *keys)
{
    (void)o;
    memset(keys, 0, 27);
    if (pr.keys || ui_held(B_SEL)) {
        int k = note_of(pr.sel) - ui_midi(0);
        if (k >= 0 && k < 27) keys[k] = LV_FULL;
        return 1;
    }
    int pl = step_now(), st = pr.steps[pr.sel];
    for (int k = 0; k < 16; k++) {
        int on = st >> k & 1;
        keys[UI_WHITE[k]] = (uint8_t)(k >= pr.len ? 0 : k == pl ? (on ? 0 : LV_FULL) : on ? LV_FULL : k % 4 == 0 ? LV_DIM : 0);
    }
    return 1;
}
static int rec_lit(ui_overlay_t *o, int b)
{
    (void)o;
    if (b == B_ARP) return LV_FULL;
    if (b == B_REC || b == B_GLO) return LV_DIM;
    if (b == B_SEL) return pr.keys ? LV_FULL : LV_DIM;
    if (b == B_SEQ || b == B_EDIT || b == B_LFO || b == B_ENV) return 0;
    return LV_NONE;
}

/* ---------------------------------------------------------------------- the box --- */
static int sx(int k) { return 30 + 12 * k + 2 * (k / 4); }   /* step k's x */
static const char *bars_text(int b) { return b == 1 ? T("1 BAR") : ui_txt_v(T("# BARS"), ui_dec(b)); }
/* what it makes: the first loop's rows and length, a later loop's repeats over the length; its lanes */
static void makes(char *dst, int max)
{
    char ln[12];
    int b = base(), k = lanes(), br = loops_bar_rows();
    ui_fmt(ln, sizeof ln, "%s", k == 1 ? T("1 LANE") : ui_txt_v(T("# LANES"), ui_dec(k)));
    if (first()) {
        int x = times(), rows = b * x, len = first_rows();
        ui_fmt(dst, max, rows == len ? T("%d X %d = %d ROWS = %s, %s") : T("%d X %d = %d ROWS IN %s, %s"), b, x, rows,
               bars_text(len / br), ln);
        return;
    }
    int len = loops_rows(), whole = len / b, rest = len % b;
    if (rest) ui_fmt(dst, max, T("%d X %d + %d = %d ROWS, %s"), whole, b, rest, len, ln);
    else ui_fmt(dst, max, T("%d X %d = %d ROWS, %s"), whole, b, len, ln);
}
static void rec_draw(ui_overlay_t *o)
{
    (void)o;
    int t = target(), col = colour(), pl = step_now(), fst = first();
    char b[40], nm[8];
    g_rect(4, 36, 232, 186, C_BLACK);
    g_box(4, 36, 232, 186, C_LGREY);
    if (t >= 0) { lp_loop_name(nm, t); g_text(10, 40, &FONT_T, ui_txt_v(T("RECORD PATTERN: INTO #"), nm), C_CYAN); }
    else g_text(10, 40, &FONT_T, T("RECORD PATTERN: NEW LOOP"), C_CYAN);
    g_text(188, 40, &FONT_T, T("PREVIEW"), playing() ? C_GREEN : C_DGREY);
    for (int k = 0; k < pr.len; k += 4) { ui_fmt(b, sizeof b, "%d", k / 4 + 1); g_text(sx(k) + 3, 56, &FONT_T, b, C_DGREY); }
    for (int r = 0; r < 4; r++) {
        int on = r == pr.sel, st = pr.steps[r];
        ui_note_name(b, note_of(r));
        g_text(8, 71 + 17 * r, &FONT_T, b, on ? C_WHITE : steps_of(r) ? col : C_DGREY);
        for (int k = 0; k < pr.len; k++)
            g_rect(sx(k), 70 + 17 * r, 11, 13, st >> k & 1 ? (k == pl ? C_WHITE : col) : k == pl ? C_DEEP + C_BLUE : C_DEEP + C_DGREY);
        if (on) g_box(6, 68 + 17 * r, 228, 17, C_WHITE);
    }
    for (int i = 0; i < 4; i++) {
        static const txt_t NAMES[4] = {TX("K1 STEP SIZE"), TX("K2 LENGTH"), TX("K3 NOTE"), TX("K4 TIMES")};
        int y = 140 + 12 * i, dim = i == 3 && !fst;
        if (i == 0) ui_fmt(b, sizeof b, "%s", ui_rp_len_name(pr.size));
        else if (i == 1) ui_fmt(b, sizeof b, "%s", ui_txt_v(T("# STEPS"), ui_dec(pr.len)));
        else if (i == 2) ui_note_name(b, note_of(pr.sel));
        else if (fst) ui_fmt(b, sizeof b, "X%d", times());
        else ui_fmt(b, sizeof b, "%s", ui_txt_v(T("FILLS #"), bars_text(loops_rows() / loops_bar_rows())));
        g_text(12, y, &FONT_T, T(NAMES[i]), dim ? C_DGREY : C_CYAN);
        g_text(228 - g_tw(&FONT_T, b), y, &FONT_T, b, dim ? C_DGREY : i == 2 ? C_WHITE : C_LGREY);
    }
    makes(b, sizeof b);
    g_text(12, 192, &FONT_T, b, C_LGREY);
    g_text(12, 206, &FONT_T, T("SELECT: ROW  SEL: NOTE  KEYS: STEPS"), C_DGREY);
}

/* ARP: open, on the selected loop (into it) or + NEW LOOP; the preview plays from the moment it opens (the loops
 * stopped: alone, from its first step; what played on into LOOPS paused for it) */
void lp_rec_open(void)
{
    uint8_t l[7];
    int n = ui_rp_lens(rpb(), l), ok = 0;
    player_state_t st;
    for (int i = 0; i < n; i++) ok |= l[i] == pr.size;
    if (!ok) {                                          /* (another GRID: the length a row long, else the first) */
        pr.size = l[0];
        for (int i = 0; i < n; i++) if (ui_rp_len_rows(l[i], rpb()) == 1) pr.size = l[i];
    }
    pr.times = (uint8_t)times();
    pr.keys = 0;
    if (!pr.note[0]) pr.note[0] = (uint8_t)note_of(0); /* (the first row's C kept from here: OCT moves the selected row's) */
    rs.target = (int8_t)(loops_sel() < loops_n() ? loops_sel() : -1);
    rs.pv = 1;
    rs.open = 1;
    rs.heard = 0;
    ui_overlay_t o = {.kind = OV_OTHER, .opener = B_ARP, .knob = rec_knob, .tap = rec_tap, .key = rec_key,
                      .cancel = rec_cancel, .draw = rec_draw, .key_leds = rec_leds, .play = rec_play, .rec = rec_rec,
                      .lit = rec_lit, .data = (void *)&rs};
    ui_open(&o);
    ui_player_state(&st);
    if (!(st.what == PL_LOOPS && st.playing)) {
        if (st.playing) player_pause();
        loops_cue();
    }
    send();
    if (!(st.what == PL_LOOPS && st.playing)) player_lp_preview_alone(1);
}
