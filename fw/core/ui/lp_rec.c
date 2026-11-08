/* SPDX-License-Identifier: GPL-3.0-only
 * Copyright (C) 2026 Vasily Kazantsev */
/* LOOPS' pattern recorder (specs/ui/screens/loops.md#the-pattern-recorder-arp), a box over the loops opened by ARP held +
 * REC (or SEL's RECORD PATTERN); the playground's is scr-loops.js' RECORDER. 4 rows of up to 16 steps, a note a row (a row never set: the note of the row
 * above, the first C at the keyboard's octave as it opens), each step a grid row; a white key tapped switches
 * its step, held past the hold delay ties it to the step before (no tie ever left alone); NOTE LENGTH, LENGTH 4-16
 * steps; all clean at every open, never saved. Its preview is the loop it would write (player_lp_preview, on the stage's
 * column), repeated over the length: in the loops' time while they play (ARP switches its sound), else alone from its
 * first step (player_lp_preview_alone). REC puts it onto the stage (lp_stage_from), its sounding notes going on as the
 * stage's (player_lp_preview_keep), and closes; HOME closes, nothing written. */
#include <string.h>
#include "ui_core.h"
#include "ui_data.h"
#include "ui_lp.h"
#include "../play/player.h"
#include "../play/loops.h"
#include "../play/rec.h"
#include "../store/store.h"

lp_pr_t lp_pr;                                          /* (set anew at every open: lp_rec_open) */
/* the box open: the loop it was opened on (-1 + NEW LOOP), its preview on (pv), the note heard on its own and its track
 * (heard_t) till when; the row each step key pressed picked (2 bits a white key: its tap or hold acts on that row only) */
static struct { int8_t target; uint8_t open, pv, heard, heard_t; uint32_t heard_off, prow; } rs;

/* K1 NOTE LENGTH (LPN_*): a beat's parts (m / d of a beat: whole rows only, at the loops' rows a beat), 1 BAR, TO THE
 * NEXT (no end: it rings until its row's next note) */
static const struct { txt_t name; uint8_t m, d; } NL[9] = {
    {TX("1/32"), 1, 8}, {TX("1/16T"), 1, 6}, {TX("1/16"), 1, 4}, {TX("1/8T"), 1, 3}, {TX("1/8"), 1, 2}, {TX("1/4"), 1, 1},
    {TX("1/2"), 2, 1}, {TX("1 BAR"), 0, 0}, {TX("TO THE NEXT"), 0, 0}};
static int nl_ok(int i) { return i >= LPN_BAR || loops_rpb() * NL[i].m % NL[i].d == 0; }   /* (whole rows: 1 at least) */
static int nl_rows(void)                                /* NOTE LENGTH in rows (0: TO THE NEXT) */
{
    int i = lp_pr.nlen;
    return i == LPN_NEXT ? 0 : i == LPN_BAR ? loops_bar_rows() : loops_rpb() * NL[i].m / NL[i].d;
}
static uint16_t len_mask(void) { return (uint16_t)((1u << lp_pr.len) - 1u); }
static int note_of(int r)
{
    for (int k = r; k >= 0; k--) if (lp_pr.note[k]) return lp_pr.note[k];
    return ui_clamp(60 + 12 * ui_cur.octave, 12, 127);
}
static uint16_t on_of(int r) { return (uint16_t)(lp_pr.on[r] & len_mask()); }   /* (a row of ties alone has no note) */
static int lanes(void) { int n = 0; for (int r = 0; r < 4; r++) n += on_of(r) != 0; return n; }
static int target(void) { return rs.target >= 0 && rs.target < loops_n() ? rs.target : -1; }
static int track(void) { return target() >= 0 ? target() : loops_n(); }   /* (the stage's mix: the loop it would go in) */
uint16_t lp_rec_inst(void) { return target() >= 0 ? loops_inst(target()) : ui_cur.inst; }
static int colour(void) { return LP_COL[target() >= 0 ? loops_colour(target()) : lp_next_colour()]; }
int lp_rec_on(void) { return rs.open; }

/* the preview: the loop it would write, its filled rows its lanes in order (as REC writes them), heard while pv */
static void tell(void)
{
    pl_lp_grid_t g;
    int k = 0;
    memset(&g, 0, sizeof g);
    for (int r = 0; r < 4; r++)
        if (on_of(r)) { g.steps[k] = on_of(r); g.ties[k] = (uint16_t)(lp_pr.tie[r] & len_mask()); g.note[k++] = (uint8_t)note_of(r); }
    g.length = lp_pr.len;
    g.note_rows = (uint8_t)nl_rows();
    g.mute = !rs.pv;
    g.vol = (uint8_t)ui_set[S_KEYS_VOLUME];
    g.track = (uint8_t)track();
    g.inst.cart = (uint8_t)(lp_rec_inst() >> 8);
    g.inst.slot = (uint8_t)lp_rec_inst();
    player_lp_preview(&g);
}
static int alone(void) { player_state_t st; ui_player_state(&st); return st.lp_alone; }
static int playing(void) { return rs.pv && (lp_playing_here() || alone()); }
static int step_now(void)                               /* the step playing: where the loops are (or the preview alone); -1 */
{
    player_state_t st;
    if (!playing()) return -1;
    ui_player_state(&st);
    return st.row % lp_pr.len;
}
static void hush(void)
{
    if (!rs.heard) return;
    pl_inst_t in = {0, 0};
    player_hear(0, in, 0, rs.heard_t);
    rs.heard = 0;
}
/* a row's note heard on its own, as the preview plays it (its instrument, on the stage's column through the loop's mix),
 * NOTE LENGTH long (TO THE NEXT: a row), 0.15 s at least */
static void hear(int note)
{
    int rows = nl_rows(), ms = (rows ? rows : 1) * loops_ticks() * 2500 / loops_tempo();
    pl_inst_t in = {(uint8_t)(lp_rec_inst() >> 8), (uint8_t)lp_rec_inst()};
    hush();                                             /* (the one before, on its track) */
    rs.heard_t = (uint8_t)track();
    player_hear((uint8_t)note, in, (uint8_t)ui_set[S_KEYS_VOLUME], rs.heard_t);
    rs.heard = (uint8_t)note;
    rs.heard_off = ui_now() + (uint32_t)(ms < 150 ? 150 : ms);
}
static void set_note(int n)
{
    lp_pr.note[lp_pr.sel] = (uint8_t)ui_clamp(n, 12, 127);
    hear(lp_pr.note[lp_pr.sel]);
    tell();
}
/* step k of the selected row: 0 off, 1 on, 2 a tie; no tie left alone (one after a step off, or the first, becomes a
 * plain note: a held run starts with one); a step on heard while the preview does not play */
static void set_step(int k, int v)
{
    int r = lp_pr.sel;
    uint16_t b = (uint16_t)(1u << k), *on = &lp_pr.on[r], *tie = &lp_pr.tie[r];
    *on = (uint16_t)(*on & ~b);
    *tie = (uint16_t)(*tie & ~b);
    if (v == 1) *on |= b;
    if (v == 2) *tie |= b;
    for (int j = 0; j < 16; j++)
        if (*tie >> j & 1u && (!j || !((*on | *tie) >> (j - 1) & 1u))) { *tie = (uint16_t)(*tie & ~(1u << j)); *on |= (uint16_t)(1u << j); }
    if (*on >> k & 1u && !playing()) hear(note_of(r));
    tell();
    ui_dirty();
}
static int pressed_row(int k) { return (int)(rs.prow >> (2 * k) & 3u); }
void lp_rec_poll(void)
{
    int k;
    if (rs.heard && (int32_t)(ui_now() - rs.heard_off) >= 0) hush();
    if (!rs.open) return;
    while ((k = lp_hold_due()) >= 0)                    /* (held past the hold delay: a tie; after a step off, or as the
                                                         * first, the rule against a tie alone makes it a plain note) */
        if (pressed_row(k) == lp_pr.sel && k < lp_pr.len) set_step(k, 2);
}

/* ------------------------------------------------------------------- writing --- */
/* each lane's note offs (bit k: at step k): a step on held NOTE LENGTH's rows and its ties' steps, ended where that comes
 * before the lane's next note (round the steps: the next repeat's first) and no note of it is there (rec_note_offs, the
 * preview's too) */
static uint16_t offs(uint16_t on, uint16_t tie, int nr)
{
    return (uint16_t)rec_note_offs(on, tie, (uint32_t)lp_pr.len, (uint32_t)nr);
}
/* the stream REC puts onto the stage: the pattern repeated over the length (the last repeat cut there), the filled rows
 * its lanes in order, a note at KEYS VOLUME at each step on and its offs; a lane still sounding at the length's end a
 * note off at row 0 where its row 0 has none (no wrap); unmarked */
static uint32_t make(uint8_t *buf, uint32_t max)
{
    lp_enc_t e;
    rec_cell_t c;
    int rows = loops_rows(), n = lp_pr.len, nr = nl_rows(), nl = 0, held = 0;
    uint16_t on[4], off[4];
    uint8_t nt[4];
    for (int r = 0; r < 4; r++) if (on_of(r)) { on[nl] = on_of(r); off[nl] = offs(on[nl], lp_pr.tie[r], nr); nt[nl++] = (uint8_t)note_of(r); }
    for (int l = 0; l < nl; l++) {
        int s = 0;
        for (int r = 0; r < rows; r++) { int k = r % n; if (on[l] >> k & 1u) s = 1; else if (off[l] >> k & 1u) s = 0; }
        held |= s << l;
    }
    loops_enc_start(&e, buf, max);
    for (int r = 0; r < rows; r++)
        for (int l = 0; l < nl; l++) {
            int k = r % n;
            memset(&c, 0, sizeof c);
            if (on[l] >> k & 1u) { c.has = RC_NOTE | RC_VOL; c.note = nt[l]; c.vol = (uint8_t)ui_set[S_KEYS_VOLUME]; }
            else if (off[l] >> k & 1u || (!r && held >> l & 1)) { c.has = RC_NOTE; c.note = REC_NOTE_OFF; }
            else continue;
            loops_enc_cell(&e, 4 * r + l, &c);
        }
    return loops_enc_end(&e);
}
/* REC: the pattern onto the stage in place of what it held (PATTERN ON THE STAGE), in the sound the preview had, POLY when
 * it fills more than one row; the preview's sounding notes go on as the stage's (alone: the loops start from where it
 * was; nothing playing: nothing starts). No step on, or the stage refusing it: refused, nothing changed */
static int write(void)
{
    if (!lanes()) { ui_refuse(B_REC); ui_toast(T("NO STEPS ON")); return 0; }
    int heard = lp_playing_here() || alone();
    if (lp_stage_from(make, lp_rec_inst(), lanes() > 1)) return 0;
    if (heard) player_lp_preview_keep();
    ui_toast(T("PATTERN ON THE STAGE"));
    return 1;
}

/* ---------------------------------------------------------------------- input --- */
static void rec_close(ui_overlay_t *o)
{
    player_lp_preview(0);                               /* (alone: the replay stops, the loops' place kept) */
    hush();
    lp_hold_clear();
    rs.open = 0;
    if (o) ui_close(o);
}
void lp_rec_close(void)                                 /* (leaving: the change of screen closes the box itself) */
{
    ui_overlay_t *o = ui_top();
    if (rs.open) rec_close(o && o->data == (void *)&rs ? o : 0);
}
/* PLAY: playing, the loops pause (a preview heard goes on alone from where it was); stopped, they play from the play
 * position as on the screen, the preview with them */
static void rec_play(ui_overlay_t *o)
{
    (void)o;
    if (!lp_playing_here()) { lp_play(); return; }
    if (rs.pv) player_lp_preview_alone(1);              /* (the loops pause, the preview's replay runs on) */
    else player_pause();
}
/* ARP: the loops playing, the preview's sound off and on; else it starts and stops alone, from its first step */
static void arp(void)
{
    if (lp_playing_here()) { rs.pv = !rs.pv; tell(); return; }
    if (alone()) { player_lp_preview_alone(0); rs.pv = 0; tell(); return; }
    rs.pv = 1;
    tell();
    player_lp_preview_alone(1);
}
static void rec_knob(ui_overlay_t *o, int e, int s, int coarse)
{
    (void)o;
    int d = s > 0 ? 1 : -1, i = lp_pr.nlen + d;
    if (e == K_SELECT) lp_pr.sel = (uint8_t)ui_clamp(lp_pr.sel + d, 0, 3);
    else if (e == K_ALG) lp_tempo_turn(s, coarse);
    else if (e == K_K1) {                               /* (the next length GRID makes whole rows of, that way) */
        while (i >= 0 && i <= LPN_NEXT && !nl_ok(i)) i += d;
        if (i >= 0 && i <= LPN_NEXT) { lp_pr.nlen = (uint8_t)i; tell(); }
    } else if (e == K_K2) { lp_pr.len = (uint8_t)ui_clamp(lp_pr.len + 4 * d, 4, 16); tell(); }
    else if (e == K_K3) set_note(note_of(lp_pr.sel) + s);
}
static int rec_tap(ui_overlay_t *o, int b)
{
    (void)o;
    if (b == B_ARP) arp();
    else if (b == B_SEL) { lp_pr.keys = !lp_pr.keys; ui_toast(lp_pr.keys ? T("KEYS: THE NOTE") : T("KEYS: THE STEPS")); }
    else if (b == B_OCTM || b == B_OCTP) {             /* the keyboard's octave, the row's note with it */
        int d = b == B_OCTP ? 1 : -1, n = note_of(lp_pr.sel) + 12 * d;
        char nm[8];
        if (ui_cur.octave + d < -4 || ui_cur.octave + d > 4 || n < 12 || n > 127) { ui_refuse(b); return 1; }
        ui_cur.octave = (int8_t)(ui_cur.octave + d);
        set_note(n);
        ui_note_name(nm, n);
        ui_toast(ui_txt_v(T("NOTE #"), nm));
    } else if (b == B_SAVE) return 0;                   /* (SAVE as everywhere) */
    return 1;
}
/* a white key: tapped, its step on or off, at its release; held past the hold delay (lp_rec_poll), a tie or a plain
 * note. After SEL, or SEL held, a key sets the row's note at the keyboard's octave (and the keys go back to the steps) */
static int rec_key(ui_overlay_t *o, int n, int down)
{
    (void)o;
    int k = ui_white_index(n);
    if (!down) {
        if (k >= 0 && lp_hold_up(k) && k < lp_pr.len && pressed_row(k) == lp_pr.sel)   /* (past a LENGTH made shorter meanwhile:
                                                                                         * nothing) */
            set_step(k, (lp_pr.on[lp_pr.sel] | lp_pr.tie[lp_pr.sel]) >> k & 1u ? 0 : 1);
        return 1;
    }
    if (lp_pr.keys || ui_held(B_SEL)) {
        int m = ui_midi(n);
        if (ui_held(B_SEL)) ui_consume(B_SEL);
        if (m >= 12 && m <= 127) set_note(m);
        lp_pr.keys = 0;
        return 1;
    }
    if (k < 0 || k >= lp_pr.len) return 1;
    lp_hold_down(k);
    rs.prow = (rs.prow & ~(3u << (2 * k))) | (uint32_t)lp_pr.sel << (2 * k);
    return 1;
}
static void rec_rec(ui_overlay_t *o)    /* (REC's job: then 500 ms of nothing, lp_rec_done) */
{
    if (lp_rec_quiet()) return;
    if (write()) { rec_close(o); lp_rec_done(); }
}
static void rec_cancel(ui_overlay_t *o) { rec_close(o); }
/* a step on full, a tie blinking, off dark (steps 1, 5, 9, 13 dim), past LENGTH dark, the one playing turned over;
 * setting the note: its key */
static int rec_leds(ui_overlay_t *o, uint8_t *keys)
{
    (void)o;
    memset(keys, 0, 27);
    if (lp_pr.keys || ui_held(B_SEL)) {
        int k = note_of(lp_pr.sel) - ui_midi(0);
        if (k >= 0 && k < 27) keys[k] = LV_FULL;
        return 1;
    }
    int pl = step_now(), on = lp_pr.on[lp_pr.sel], tie = lp_pr.tie[lp_pr.sel];
    for (int k = 0; k < 16; k++) {
        int s = (on | tie) >> k & 1;
        keys[UI_WHITE[k]] = (uint8_t)(k >= lp_pr.len ? 0 : k == pl ? (s ? 0 : LV_FULL) : tie >> k & 1 ? (ui_blink(250) ? LV_FULL : LV_DIM)
                                      : s ? LV_FULL : k % 4 == 0 ? LV_DIM : 0);
    }
    return 1;
}
static int rec_lit(ui_overlay_t *o, int b)
{
    (void)o;
    if (b == B_ARP) return LV_FULL;
    if (b == B_REC) return LV_DIM;
    if (b == B_SEL) return lp_pr.keys ? LV_FULL : LV_DIM;
    if (b == B_GLO || b == B_SEQ || b == B_EDIT || b == B_LFO || b == B_ENV) return 0;
    return LV_NONE;
}

/* ---------------------------------------------------------------------- the box --- */
static int sx(int k) { return 30 + 12 * k + 2 * (k / 4); }   /* step k's x */
/* what it makes: its whole repeats x its steps (+ the cut one's) = the length, and its lanes */
void lp_rec_makes(char *dst, int max)
{
    char ln[12];
    int b = lp_pr.len, k = lanes(), len = loops_rows(), whole = len / b, rest = len % b;
    ui_fmt(ln, sizeof ln, "%s", k == 1 ? T("1 LANE") : ui_txt_v(T("# LANES"), ui_dec(k)));
    if (rest) ui_fmt(dst, max, T("%d X %d + %d = %d ROWS, %s"), whole, b, rest, len, ln);
    else ui_fmt(dst, max, T("%d X %d = %d ROWS, %s"), whole, b, len, ln);
}
static void rec_draw(ui_overlay_t *o)
{
    (void)o;
    static const txt_t NAMES[4] = {TX("K1 NOTE LENGTH"), TX("K2 LENGTH"), TX("K3 NOTE"), TX("REPEATS")};
    int col = colour(), pl = step_now();
    char b[40];
    g_rect(4, 36, 232, 186, C_BLACK);
    g_box(4, 36, 232, 186, C_LGREY);
    g_text(10, 40, &FONT_T, T("RECORD PATTERN: TO STAGE"), C_CYAN);
    g_text(188, 40, &FONT_T, T("PREVIEW"), playing() ? C_GREEN : C_DGREY);
    for (int k = 0; k < lp_pr.len; k += 4) { ui_fmt(b, sizeof b, "%d", k / 4 + 1); g_text(sx(k) + 3, 56, &FONT_T, b, C_DGREY); }
    for (int r = 0; r < 4; r++) {
        int y = 70 + 17 * r, on = lp_pr.on[r], tie = lp_pr.tie[r];
        ui_note_name(b, note_of(r));
        g_text(8, y + 1, &FONT_T, b, r == lp_pr.sel ? C_WHITE : on_of(r) ? col : C_DGREY);
        for (int k = 0; k < lp_pr.len; k++) {
            int t = tie >> k & 1;
            g_rect(sx(k), y, 11, 13, t ? (k == pl ? C_LGREY : C_DEEP + col) : on >> k & 1 ? (k == pl ? C_WHITE : col)
                                   : k == pl ? C_DEEP + C_BLUE : C_DEEP + C_DGREY);
            if (t && k) g_rect(sx(k - 1) + 6, y + 5, sx(k) - sx(k - 1) + 1, 3, col);   /* (a tie: a bar from the step before) */
        }
        if (r == lp_pr.sel) g_box(6, y - 2, 228, 17, C_WHITE);
    }
    for (int i = 0; i < 4; i++) {
        int y = 140 + 12 * i;
        if (i == 0) ui_fmt(b, sizeof b, "%s", T(NL[lp_pr.nlen].name));
        else if (i == 1) ui_fmt(b, sizeof b, "%s", ui_txt_v(T("# STEPS"), ui_dec(lp_pr.len)));
        else if (i == 2) ui_note_name(b, note_of(lp_pr.sel));
        else ui_fmt(b, sizeof b, "%s", ui_txt_v(T("OVER #"), lp_len_text(loops_rows())));
        g_text(12, y, &FONT_T, T(NAMES[i]), i == 3 ? C_DGREY : C_CYAN);   /* (REPEATS: no knob, told dim) */
        g_text(228 - g_tw(&FONT_T, b), y, &FONT_T, b, i == 3 ? C_DGREY : i == 2 ? C_WHITE : C_LGREY);
    }
    lp_rec_makes(b, sizeof b);
    g_text(12, 192, &FONT_T, b, C_LGREY);
    g_text(12, 206, &FONT_T, T("KEY: A STEP  HELD: A TIE  SEL: NOTE"), C_DGREY);
}

/* open (ARP held + REC, SEL's RECORD PATTERN: scr_loops.c refuses it first where it must), on the selected loop (its
 * sound) or + NEW LOOP, clean every time: no steps, the first row, 16 steps, NOTE LENGTH 1/16 (a GRID without it: a
 * row's); the preview plays from the moment it opens (the loops stopped: alone, from its first step; what played on
 * into LOOPS paused for it) */
void lp_rec_open(void)
{
    player_state_t st;
    memset(&lp_pr, 0, sizeof lp_pr);
    lp_pr.len = 16;
    lp_pr.nlen = LPN_1_16;
    if (!nl_ok(LPN_1_16)) {                             /* (another GRID: a row's length, else the first) */
        int i = 0;
        while (!nl_ok(i)) i++;
        for (int k = LPN_1_32; k <= LPN_1_4; k++) if (nl_ok(k) && loops_rpb() * NL[k].m == NL[k].d) i = k;
        lp_pr.nlen = (uint8_t)i;
    }
    lp_pr.note[0] = (uint8_t)note_of(0);                /* (the first row's C kept from here: OCT moves the selected row's) */
    rs.target = (int8_t)(loops_sel() < loops_n() ? loops_sel() : -1);
    rs.pv = 1;
    rs.open = 1;
    rs.heard = 0;
    lp_hold_clear();
    ui_overlay_t o = {.kind = OV_OTHER, .opener = B_ARP, .knob = rec_knob, .tap = rec_tap, .key = rec_key,
                      .cancel = rec_cancel, .draw = rec_draw, .key_leds = rec_leds, .play = rec_play, .rec = rec_rec,
                      .lit = rec_lit, .data = (void *)&rs};
    ui_open(&o);
    ui_player_state(&st);
    if (!(st.what == PL_LOOPS && st.playing)) {
        if (st.playing) player_pause();
        loops_cue();
    }
    tell();
    if (!(st.what == PL_LOOPS && st.playing)) player_lp_preview_alone(1);
}
