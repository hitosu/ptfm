/* SPDX-License-Identifier: GPL-3.0-only
 * Copyright (C) 2026 Vasily Kazantsev */
/* The EDITOR (specs/ui/screens/editor.md; the playground's scr-editor.js): the pattern in 8, 4, 2 or 1
 * tracks; REC switches writing (step by step, the loop playing on or not; PLAY: a realtime take that goes round
 * the pattern until PLAY or REC stops it); every change heard at once, whatever plays the pattern; a key
 * writes its note with the current instrument; K1 picks the cell's slot (its volume, or one of its up to
 * four commands), K2 the slot's command, K3 and K4 its values (in the parameter, mixer and FX groups K3
 * picks which, K4 sets it); LFO clears; EDIT the note editor; GLO the pattern's properties; ENV the
 * pattern list; SEL blocks and their menu; ARP the row preview. */
#include <string.h>
#include "ui_core.h"
#include "ui_priv.h"
#include "ui_grid.h"
#include "ui_data.h"
#include "ui_cmds.h"
#include "fxdef.h"
#include "ui_ed.h"
#include "ed_pat.h"
#include "../play/player.h"
#include "../play/rec.h"
#include "../store/store.h"

extern const ui_screen_t SCR_EDITOR;

enum { L_NOTE, L_INST, L_VOL, L_CMD, LINES = 7 };
static struct {
    int view, row, track, left, slot;
    uint8_t rec, note_ed, nf_cells, nf_changed;
    int nf_line, nf_want, nf_part;
    /* blocks */
    uint8_t marking, block;
    int ar, at;                         /* the mark's anchor */
    int b[4];                           /* r0, r1, t0, t1 */
    uint8_t floating, fl_swap;
    int fl_row, fl_track, fl_src[4];
    uint8_t tmode;
    int transpose, tb[4];
    /* the keys */
    int8_t held_track[27];
    uint8_t held_delay[27];             /* (a take's: REAL TIME's note delay) */
    int32_t held_at[27];                /* a take's: its note's row, counted as the player counts the rows it plays (-1:
                                         * written step by step) */
    int chord_row;
    /* the row preview */
    uint8_t arp, arp_on[16];
    int arp_step;
    uint32_t arp_t;
    /* takes */
    uint8_t take, armed, take_seen;     /* take_seen: the loop has started (the request is applied a half later) */
    uint8_t take_now;                   /* the take started with no count-in: keys before take_seen land on its row */
    uint8_t play_panel;                 /* PLAY held past the hold delay, REC on: its panel (both ways to a take) */
    int take_row, last_row;
    /* opened from a FUSION slot */
    int fusion;
    int want_row, want_track, want_fusion;
    uint8_t want;
} ed = {.view = 8, .slot = 1, .fusion = -1, .chord_row = -1};

/* ------------------------------------------------------------------------------- the pattern --- */
/* the pattern edited: the one picked, or the EDITOR's own when that is gone (it is always there) */
static uint8_t pat(void)
{
    if (!ui_pattern_rows(ui_cur.pattern)) ui_cur.pattern = UI_EDITOR_PATTERN;
    return ui_cur.pattern;
}
static int own(void) { return pat() == UI_EDITOR_PATTERN; }
static int rows(void) { return ui_pattern_rows(pat()); }
static int tracks(void) { return ui_pattern_tracks(pat()); }
static const ui_view_t *V(void) { return ui_view(ed.view); }
static int sign(int s) { return s > 0 ? 1 : s < 0 ? -1 : 0; }
static void cell_at(int r, int t, rec_cell_t *c) { ed_cell(pat(), r, t, c); }
static int empty(const rec_cell_t *c) { return !(c->has & (RC_NOTE | RC_INS | RC_VOL)) && !((c->has & RC_CMD) && c->n); }
static int engine_at(int r, int t)
{
    rec_cell_t c;
    cell_at(r, t, &c);
    int ref = (c.has & RC_INS) ? c.cart << 8 | c.slot : ed_inst_at(pat(), r, t);
    if (ref < 0) ref = ui_cur.inst;
    return ui_cart_kind((uint8_t)(ref >> 8));
}

static void scroll_x(void)
{
    int n = V()->n, nt = tracks();
    if (ed.view == 1) ed.left = ed.track;
    else if (ed.view == 2) ed.left = ed.track / 2 * 2;
    else {
        if (ed.track >= ed.left + n - 1 && ed.left + n < nt) ed.left = ed.left + 2 < nt - n ? ed.left + 2 : nt - n;
        if (ed.track <= ed.left && ed.left > 0) ed.left = ed.left - 2 > 0 ? ed.left - 2 : 0;
        while (ed.track >= ed.left + n) ed.left++;
        while (ed.track < ed.left) ed.left--;
    }
    ed.left = ui_clamp(ed.left, 0, nt - n > 0 ? nt - n : 0);
}
static void clamp_cursor(void)
{
    ed.row = ui_clamp(ed.row, 0, rows() - 1 > 0 ? rows() - 1 : 0);
    ed.track = ui_clamp(ed.track, 0, tracks() - 1 > 0 ? tracks() - 1 : 0);
    scroll_x();
}
static int slots(const rec_cell_t *c) { int n = (c->has & RC_CMD) ? c->n : 0; return n + 1 < 4 ? n + 1 : 4; }
static void clamp_slot(void) { rec_cell_t c; cell_at(ed.row, ed.track, &c); ed.slot = ui_clamp(ed.slot, 0, slots(&c)); }

/* where the loop plays this pattern: its row, else -1 */
static int play_row(void)
{
    player_state_t st;
    ui_player_state(&st);
    return st.playing && st.what == PL_LOOP && st.pat == pat() ? st.row : -1;
}
/* the row nearest where the loop is (a take: the cursor follows it) */
static int nearest_row(void)
{
    player_state_t st;
    ui_player_state(&st);
    if (st.what != PL_LOOP || st.pat != pat()) return -1;
    int r = st.row + (st.speed && st.tick * 2 >= st.speed ? 1 : 0);
    return r > rows() - 1 ? rows() - 1 : r;
}
static int recording(void) { return ed.take; }

/* ------------------------------------------------------------------------------- writing --- */
static void stop_recording(void);
static int put(int r, int t, const rec_cell_t *c)
{
    if (r < 0 || r >= rows() || t < 0 || t >= tracks()) return 0;
    if (ed_put_cell(pat(), r, t, c && !empty(c) ? c : 0)) { stop_recording(); return 0; }   /* (a take ends there) */
    if (ed.note_ed) ed.nf_changed = 1;
    return 1;
}
static void advance(void)
{
    rec_pat_t p;
    if (!rec_pattern(pat(), &p) && p.step) ed.row = ui_clamp(ed.row + p.step, 0, rows() - 1);
}
/* a key's note at a cell: the current instrument and KEYS VOLUME; the cell's commands stay */
static void note_into(rec_cell_t *c, int m)
{
    c->has |= RC_NOTE | RC_INS | RC_VOL;
    c->note = (uint8_t)m;
    c->cart = (uint8_t)(ui_cur.inst >> 8);
    c->slot = (uint8_t)ui_cur.inst;
    c->vol = (uint8_t)ui_set[S_KEYS_VOLUME];
}
static void write_note(int r, int t, int m, rec_cell_t *c) { cell_at(r, t, c); note_into(c, m); }
static void sound(int n, int down)
{
    int m = ui_midi(n);
    if (m < 12 || m > 127) return;
    if (!down) { player_key_off((uint8_t)m); return; }
    pl_inst_t in = {(uint8_t)(ui_cur.inst >> 8), (uint8_t)ui_cur.inst};
    player_key_on((uint8_t)m, in, (uint8_t)ui_set[S_KEYS_VOLUME]);
}

static void start_take(int now)
{
    if (!now && ui_set[S_COUNT_IN] == 3) { ed.armed = 1; return; }         /* COUNT-IN KEY: the first key starts it */
    ed.take_row = ed.row;
    ed.last_row = ed.row;
    ed.take = 1;
    ed.take_seen = 0;
    ed.take_now = now || !ui_set[S_COUNT_IN];
    if (!now && (ui_set[S_COUNT_IN] == 1 || ui_set[S_COUNT_IN] == 2)) {   /* 1 or 2 bars counted first, at the song's
                                                                         * tempo: the take starts on the beat after */
        ui_song_t s;
        int d = ui_song(ui_cur.song, &s) ? s.ticks * s.rpb : 0;
        player_count_in(ui_set[S_COUNT_IN], d ? s.tempo * 240 / d : 1200);
    }
    player_play_pattern(pat(), (uint8_t)ed.row);
}
static int armed(void) { return ed.armed; }             /* (COUNT-IN KEY: the count-in's box says TAKE ARMED) */
static int counting(void)                               /* the take's count-in under way: keys heard, not written */
{
    player_state_t st;
    ui_player_state(&st);
    return st.count_beats != 0;
}
static void stop_recording(void)
{
    if (ed.take) {
        player_count_stop();                            /* (a count-in under way: dropped, the take with it) */
        player_pause();
        ed.take = 0;
        ed.row = ed.take_row;                           /* (back where it started: to hear it, or record again) */
        ui_toast(T("TAKE STOPPED"));
    }
}

/* a take heard (its count-in over, the loop playing): where the loop is, as the player last said */
static int taking(player_state_t *st)
{
    ui_player_state(st);
    return recording() && st->playing && st->what == PL_LOOP;
}
static const uint8_t QUANT_ROWS[5] = {2, 3, 4, 1, 0};   /* SETTINGS' QUANTIZE: 2 ROWS, 3 ROWS, 4 ROWS, ROW; REAL TIME 0 */
/* where a take's key lands by QUANTIZE (specs/ui/screens/editor.md#realtime-takes), from where the loop is (st: its row
 * and tick): the nearest line of 2, 3 or 4 rows from the pattern's start (halves up), ROW the nearest row; REAL TIME the
 * row it falls in, its tick a note delay EDx (at most 15). *row, *delay; returns how many rows on from the one playing
 * it is: > 0 it waits for its line (past the pattern's end: row 0, the take going round), else it sounds at once */
static int landing(const player_state_t *st, int *row, int *delay)
{
    int q = QUANT_ROWS[ui_set[S_QUANTIZE]], sp = st->speed ? st->speed : 1, tk = st->tick < sp ? st->tick : 0;
    int at = st->row * sp + tk;                         /* (the place in ticks) */
    int l = q ? (2 * at + q * sp) / (2 * q * sp) * q : st->row;   /* (its line: round(at / (q x speed)) x q) */
    *delay = q ? 0 : tk < 15 ? tk : 15;
    if (l >= rows()) { *row = 0; return rows() - st->row; }
    *row = l;
    return l - st->row;
}
/* a take's command in a cell (REAL TIME's EDx, Kxx): those of its code there before taken out (hi >= 0: those of that
 * high nibble only), then x if there is room (x 0: none); the cell's other commands stay. 0: no room for x */
static int take_cmd(rec_cell_t *c, int code, int hi, const uint8_t *x)
{
    int j = 0;
    if (!(c->has & RC_CMD)) c->n = 0;
    for (int k = 0; k < c->n; k++)
        if (c->cmd[k][0] != code || (hi >= 0 && c->cmd[k][1] >> 4 != hi)) { c->cmd[j][0] = c->cmd[k][0]; c->cmd[j++][1] = c->cmd[k][1]; }
    c->n = (uint8_t)j;
    if (x && j < 4) { c->cmd[j][0] = x[0]; c->cmd[j][1] = x[1]; c->n++; }
    if (c->n) c->has |= RC_CMD;
    else c->has &= (uint8_t)~RC_CMD;
    return !x || j < 4;
}
static void keep_held(int n, int t, int32_t at, int delay)    /* key n's note: its track, its row's count (a take's;
                                                                 * -1 step by step), its note delay */
{
    ed.held_track[n] = (int8_t)t;
    ed.held_at[n] = at;
    ed.held_delay[n] = (uint8_t)delay;
}

static void key_down(int n)
{
    int m = ui_midi(n);
    if (m < 12 || m > 127) return;
    rec_cell_t c;
    player_state_t st;
    /* PLAY held + a key, or the take armed (COUNT-IN KEY): a take that starts with this note, on the cursor's row:
     * written first, the loop playing it as it starts (the player counts the rows from the pattern's start, its row 0
     * the first) */
    if ((ui_held(B_PLAY) || ed.armed) && ed.rec && !recording()) {
        ui_consume(B_PLAY);
        ed.armed = 0;
        ed.play_panel = 0;
        write_note(ed.row, ed.track, m, &c);
        take_cmd(&c, 0x0E, 0xD, 0);
        if (!put(ed.row, ed.track, &c)) return;         /* (the edit memory full: no take, as one it ends) */
        start_take(1);
        keep_held(n, ed.track, ed.row + 1, 0);
        return;
    }
    /* the next track from the cursor's no held key's note is on (a chord spreading) */
    int t = ed.track;
    for (int busy = 1; busy && t < tracks() - 1;) {
        busy = 0;
        for (int k = 0; k < 27; k++) if (ed.held_track[k] == t) busy = 1;
        if (busy) t++;
    }
    /* a take started at once, its loop not seen playing yet (the player takes the request a half or two later): the key
     * lands on the take's first row too, with the first key's chord, heard there (its row's count: row + 1) */
    if (recording() && ed.take_now && !ed.take_seen) {
        write_note(ed.take_row, t, m, &c);
        take_cmd(&c, 0x0E, 0xD, 0);
        if (put(ed.take_row, t, &c)) {
            keep_held(n, t, ed.take_row + 1, 0);
            player_take_cell(t, ed.take_row, (uint32_t)ed.take_row + 1, 0);
        }
        return;
    }
    /* a take: the note lands as QUANTIZE says and is heard as written, by its track (at its line, or at once), not as
     * the keys; a chord spreading */
    if (taking(&st)) {
        if (st.pat != pat()) return;
        int row, d, ahead = landing(&st, &row, &d);
        uint8_t x[2] = {0x0E, (uint8_t)(0xD0 | d)};     /* (EDx) */
        write_note(row, t, m, &c);
        take_cmd(&c, 0x0E, 0xD, d ? x : 0);
        if (put(row, t, &c)) {
            keep_held(n, t, (int32_t)st.rows_played + ahead, d);
            player_take_cell(t, row, ahead > 0 ? st.rows_played + (uint32_t)ahead : 0, 0);
        }
        return;
    }
    sound(n, 1);
    if (recording()) return;                            /* (the count-in: heard, not written) */
    /* step by step: a chord spreads to the next tracks on the same row */
    int row = ed.row, held = 0, maxt = -1;
    t = ed.track;
    for (int k = 0; k < 27; k++) if (ed.held_track[k] >= 0) { held = 1; if (ed.held_track[k] > maxt) maxt = ed.held_track[k]; }
    if (held && ed.chord_row >= 0) { row = ed.chord_row; t = maxt + 1; }
    else { ed.chord_row = row; advance(); }
    if (t >= tracks()) return;
    write_note(row, t, m, &c);
    if (put(row, t, &c)) keep_held(n, t, -1, 0);
}
static void key_up(int n)
{
    sound(n, 0);
    int t = ed.held_track[n], d = ed.held_delay[n];
    int32_t from = ed.held_at[n];
    ed.held_track[n] = -1;
    int any = 0;
    for (int k = 0; k < 27; k++) if (ed.held_track[k] >= 0) any = 1;
    if (!any) ed.chord_row = -1;
    player_state_t st;
    if (t < 0 || from < 0 || !taking(&st) || st.pat != pat()) return;
    /* a take's: its note off on the nearest row (REAL TIME: a key off Kxx at its tick, on the row it falls in), a row
     * after its note at least (past the pattern's end: the next pass's rows, the take going round), heard there as
     * written; a note there ends it already. REAL TIME let go in its note's own row after its delay: Kxx in its cell */
    int sp = st.speed ? st.speed : 1, tk = st.tick < sp ? st.tick : 0, real = !QUANT_ROWS[ui_set[S_QUANTIZE]];
    int32_t now = (int32_t)st.rows_played, at = now + (!real && 2 * tk >= sp);
    if (at <= from && !(real && at == from && tk > d)) at = from + 1;
    int row = (st.row + (int)(at - now)) % rows(), own = at == from;
    rec_cell_t c;
    cell_at(row, t, &c);
    if ((c.has & RC_NOTE) && !own) return;
    uint8_t x[2] = {0x14, (uint8_t)tk};                 /* (Kxx) */
    if (real && !take_cmd(&c, 0x14, -1, x)) return;
    if (!real) { c.has |= RC_NOTE; c.note = REC_NOTE_OFF; }  /* (the cell's commands stay) */
    if (put(row, t, &c)) player_take_cell(t, row, at > now ? (uint32_t)at : 0, 1);
}

/* ------------------------------------------------------------------------ K1-K4 on a cell --- */
static void cmd_text3(const uint8_t *x, char *o) { ui_cmd_text(x[0], x[1], o); }
static void show_cmd(const char *knob, const uint8_t *x, int eng)
{
    char a[4], w[40];
    cmd_text3(x, a);
    ui_cmd_words(x[0], x[1], eng, w, sizeof w);
    ui_show_knob(knob ? knob : a, w);
}
/* a command K2 gives on landing on an entry; 0 none */
static int fresh(int i, int eng, const uint8_t *taken, int nt, uint8_t *x)
{
    const ui_entry_t *e = &UI_ENTRIES[i];
    if (e->code < 0) return 0;
    if (e->code >= CE_GROUP) {
        uint8_t codes[64];
        int n = ui_cmd_codes(e->code - CE_GROUP, eng, codes, 64);
        for (int k = 0; k < n; k++) {
            int used = 0;
            for (int j = 0; j < nt; j++) if (taken[j] == codes[k]) used = 1;
            if (used) continue;
            x[0] = codes[k];
            int mx = ui_cmd_max(codes[k], eng);
            x[1] = (uint8_t)(e->code - CE_GROUP == CG_PARAM ? (mx < 64 ? mx : 64) : e->code - CE_GROUP == CG_MIXER ? 0x7F : 0);
            return 1;
        }
        return 0;
    }
    x[0] = (uint8_t)e->code;
    x[1] = (uint8_t)(e->sub >= 0 ? e->sub << 4 : e->min);
    return 1;
}
static void remove_cmd(rec_cell_t *c, int k)
{
    for (int j = k; j < c->n - 1; j++) { c->cmd[j][0] = c->cmd[j + 1][0]; c->cmd[j][1] = c->cmd[j + 1][1]; }
    c->n--;
    if (!c->n) c->has &= (uint8_t)~RC_CMD;
}
static void set_cmd(rec_cell_t *c, int k, const uint8_t *x)
{
    if (k >= c->n) k = c->n++;
    c->cmd[k][0] = x[0];
    c->cmd[k][1] = x[1];
    c->has |= RC_CMD;
}
static void cell_knob(int e, int s, int coarse)
{
    int r = ed.row, t = ed.track, eng = engine_at(r, t);
    rec_cell_t c;
    cell_at(r, t, &c);
    if (!(c.has & RC_CMD)) c.n = 0;
    char v[40];
    if (e == K_K1) {
        ed.slot = ui_clamp(ed.slot + sign(s), 0, slots(&c));
        if (!ed.slot) { ui_show_knob(T("K1"), T("VOLUME")); return; }
        if (ed.slot - 1 < c.n) {
            char w[32];
            ui_cmd_words(c.cmd[ed.slot - 1][0], c.cmd[ed.slot - 1][1], eng, w, sizeof w);
            ui_fmt(v, sizeof v, T("CMD %d %s"), ed.slot, w);
        } else ui_fmt(v, sizeof v, T("CMD %d EMPTY"), ed.slot);
        ui_show_knob(T("K1"), v);
        return;
    }
    if (ed.slot == 0) {                                 /* the volume: K3 by 16, K4 by 1; past 7F: its default */
        if (e == K_K2) { ui_show_knob(T("K3, K4"), T("THE VOLUME")); return; }
        int step = e == K_K3 || coarse ? 16 : 1, cv = (c.has & RC_VOL) ? c.vol : 0x80, nv = cv + s * step;
        if (nv > 0x7F) c.has &= (uint8_t)~RC_VOL;
        else { c.has |= RC_VOL; c.vol = (uint8_t)ui_clamp(nv, 0, 0x7F); }
        ui_show_knob(T("VOLUME"), (c.has & RC_VOL) ? ui_hex2(c.vol) : T(".. AS THE INSTRUMENT"));
    } else {
        int k = ed.slot - 1;
        uint8_t *x = k < c.n ? c.cmd[k] : 0, nx[2];
        int en = x ? ui_cmd_entry(x[0], x[1]) : 0;
        if (en < 0) en = 0;
        const ui_entry_t *ent = &UI_ENTRIES[en];
        if (e == K_K2) {
            uint8_t others[4];
            int no = 0;
            for (int j = 0; j < c.n; j++) if (j != k) others[no++] = c.cmd[j][0];
            /* each code at most once in a cell: entries another slot holds are skipped */
            int ok[96], nok = 0, at = 0;
            for (int i = 0; i < UI_NENTRIES; i++) {
                const ui_entry_t *q = &UI_ENTRIES[i];
                int skip = 0;
                if (i != en && q->code >= 0 && q->code < CE_GROUP)
                    for (int j = 0; j < no; j++) if (others[j] == q->code) skip = 1;
                if (skip) continue;
                if (i == en) at = nok;
                ok[nok++] = i;
            }
            int i = ok[ui_clamp(at + s, 0, nok - 1)];
            if (i == en && x) { nx[0] = x[0]; nx[1] = x[1]; set_cmd(&c, k, nx); }
            else if (fresh(i, eng, others, no, nx)) set_cmd(&c, k, nx);
            else if (x) remove_cmd(&c, k);
            if (k < c.n) {
                char a[4];
                cmd_text3(c.cmd[k], a);
                int ne = ui_cmd_entry(c.cmd[k][0], c.cmd[k][1]);
                ui_fmt(v, sizeof v, "%s %s", a, ne > 0 ? T(UI_ENTRIES[ne].name) : "");
                ui_show_knob(T("K2"), v);
            } else ui_show_knob(T("K2"), T("NO COMMAND"));
        } else if (!x) { ui_show_knob(e == K_K3 ? T("K3") : T("K4"), T("K2 PICKS A COMMAND")); return; }
        else if (ent->digits == 2 || ent->digits == 1) {
            if (e == K_K3 && ent->digits == 1) { ui_show_knob(T("K3"), T("K4 SETS IT")); return; }
            int d = sign(s);
            if (e == K_K3) x[1] = (uint8_t)(ui_clamp((x[1] >> 4) + d, 0, 15) << 4 | (x[1] & 15));
            else x[1] = (uint8_t)((x[1] & 0xF0) | ui_clamp((x[1] & 15) + d, 0, 15));
            if (x[1] < ent->min) x[1] = ent->min;       /* a punch-in: never 00 */
            show_cmd(0, x, eng);
        } else if (ent->digits == -1) {
            int g = ent->code - CE_GROUP;
            if (e == K_K3) {                            /* which parameter */
                uint8_t codes[64], nc = 0, all[64];
                int n = ui_cmd_codes(g, eng, all, 64);
                for (int i = 0; i < n; i++) {
                    int taken = 0;
                    for (int j = 0; j < c.n; j++) if (j != k && c.cmd[j][0] == all[i]) taken = 1;
                    if (!taken) codes[nc++] = all[i];
                }
                int at = 0;
                for (int i = 0; i < nc; i++) if (codes[i] == x[0]) at = i;
                if (nc) {
                    x[0] = codes[ui_clamp(at + sign(s), 0, nc - 1)];
                    int mx = ui_cmd_max(x[0], eng);
                    if (x[1] > mx) x[1] = (uint8_t)mx;
                }
                char nm[32];
                ui_cmd_code_name(x[0], eng, nm, sizeof nm);
                ui_show_knob(T("K3"), nm);
            } else {
                x[1] = (uint8_t)ui_clamp(x[1] + s * (coarse ? 16 : 1), 0, ui_cmd_max(x[0], eng));
                show_cmd(0, x, eng);
            }
        } else { char a[4]; cmd_text3(x, a); ui_show_knob(a, T("NO VALUE")); return; }
    }
    put(r, t, &c);
    clamp_slot();
}

/* --------------------------------------------------------------------------------- blocks --- */
static void block_of_anchor(void)
{
    ed.b[0] = ed.ar < ed.row ? ed.ar : ed.row; ed.b[1] = ed.ar > ed.row ? ed.ar : ed.row;
    ed.b[2] = ed.at < ed.track ? ed.at : ed.track; ed.b[3] = ed.at > ed.track ? ed.at : ed.track;
    ed.block = 1;
}
static void block_rect(int *b)
{
    if (ed.block) { memcpy(b, ed.b, sizeof ed.b); return; }
    b[0] = b[1] = ed.row;
    b[2] = b[3] = ed.track;
}
/* a block changed cell by cell, as one edit: f makes each cell of tracks t0..t1 (ed_old reads the old ones) */
static int each_track(int t0, int t1, ed_fn f, void *a)
{
    return ed_rewrite(pat(), t0, t1 < tracks() - 1 ? t1 : tracks() - 1, f, a, -1, -1, -1) == 0;
}
static int bl[4];
static int in_rows(int r) { return r >= bl[0] && r <= bl[1]; }
static void f_clear(int t, int r, rec_cell_t *c, void *a) { (void)t; (void)a; if (in_rows(r)) memset(c, 0, sizeof *c); }
static int paste_r0, paste_t0, paste_mix;
static void f_paste(int t, int r, rec_cell_t *c, void *a)
{
    (void)a;
    int rr = r - paste_r0;
    if (rr < 0 || rr >= ed_clip_rows()) return;
    rec_cell_t x;
    int has = ed_clip_cell(rr, t - paste_t0, &x);
    if (paste_mix && (!empty(c) || !has)) return;
    *c = x;
}
static void f_rows(int t, int r, rec_cell_t *c, void *a)
{
    int ins = *(int *)a;
    if (r < ed.row) return;
    if (ins) { if (r == ed.row) memset(c, 0, sizeof *c); else ed_old(t, r - 1, c); }
    else ed_old(t, r + 1, c);                           /* (past the last row: none) */
}
static void f_transpose(int t, int r, rec_cell_t *c, void *a)
{
    (void)t;
    if (in_rows(r) && (c->has & RC_NOTE) && c->note != REC_NOTE_OFF) c->note = (uint8_t)ui_clamp(c->note + *(int *)a, 12, 127);
}
/* INTERPOLATE: a command the block's first and last rows both hold goes on every row between, its value
 * stepping from one to the other; else the volumes of the notes step. At the block's first row each
 * track's sweep is found from its first and last cells */
static struct { int code, v0, v1; } ip;
static int ip_step(int v0, int v1, int r)
{
    int n = bl[1] - bl[0] > 1 ? bl[1] - bl[0] : 1;
    return v0 + ((v1 - v0) * (r - bl[0]) * 2 + (v1 >= v0 ? n : -n)) / (2 * n);
}
static void f_interp(int t, int r, rec_cell_t *c, void *a)
{
    (void)a;
    if (r == bl[0]) {
        rec_cell_t a0, a1;
        ed_old(t, bl[0], &a0);
        ed_old(t, bl[1], &a1);
        ip.code = -1;
        for (int i = 0; i < ((a0.has & RC_CMD) ? a0.n : 0) && ip.code < 0; i++)
            for (int j = 0; j < ((a1.has & RC_CMD) ? a1.n : 0); j++)
                if (a0.cmd[i][0] == a1.cmd[j][0] && ui_cmd_entry(a0.cmd[i][0], a0.cmd[i][1]) == ui_cmd_entry(a1.cmd[j][0], a1.cmd[j][1])) {
                    ip.code = a0.cmd[i][0]; ip.v0 = a0.cmd[i][1]; ip.v1 = a1.cmd[j][1];
                    break;
                }
        if (ip.code < 0) { ip.v0 = (a0.has & RC_VOL) ? a0.vol : 0x7F; ip.v1 = (a1.has & RC_VOL) ? a1.vol : 0x7F; }
    }
    if (!in_rows(r)) return;
    if (ip.code >= 0) {
        if (r == bl[0] || r == bl[1]) return;
        uint8_t nx[2] = {(uint8_t)ip.code, (uint8_t)ip_step(ip.v0, ip.v1, r)};
        int k = -1;
        for (int q = 0; q < c->n; q++) if (c->cmd[q][0] == ip.code) k = q;
        if (k >= 0) set_cmd(c, k, nx); else if (c->n < 4) set_cmd(c, c->n, nx);
    } else if ((c->has & RC_NOTE) && c->note != REC_NOTE_OFF) { c->has |= RC_VOL; c->vol = (uint8_t)ip_step(ip.v0, ip.v1, r); }
}
/* MOVE / SWAP: the block floats over the grid (its cells read where it was); SEL puts it down: its cells
 * at the new place; MOVE empties the old one, SWAP fills it with what was at the new place */
static rec_pat_t fl_p;
static rec_cur_t fl_cur[16];
static int fl_over(int r, int t, rec_cell_t *c)
{
    int rr = r - ed.fl_row, tt = t - ed.fl_track, rn = ed.fl_src[1] - ed.fl_src[0] + 1, tn = ed.fl_src[3] - ed.fl_src[2] + 1;
    if (!ed.floating || rr < 0 || rr >= rn || tt < 0 || tt >= tn || tt >= 16) return 0;
    if (!rec_cur_at(&fl_cur[tt], ed.fl_src[0] + rr, c)) memset(c, 0, sizeof *c);
    return 1;
}
static void f_drop(int t, int r, rec_cell_t *c, void *a)
{
    (void)a;
    int dr = ed.fl_row - ed.fl_src[0], dt = ed.fl_track - ed.fl_src[2];
    int in_dest = r >= ed.fl_row && r <= ed.fl_row + ed.fl_src[1] - ed.fl_src[0] && t >= ed.fl_track && t <= ed.fl_track + ed.fl_src[3] - ed.fl_src[2];
    int in_src = r >= ed.fl_src[0] && r <= ed.fl_src[1] && t >= ed.fl_src[2] && t <= ed.fl_src[3];
    if (in_dest) ed_old(t - dt, r - dr, c);
    else if (in_src) { if (ed.fl_swap) ed_old(t + dt, r + dr, c); else memset(c, 0, sizeof *c); }
}
static void drop_float(void)
{
    int tn = ed.fl_src[3] - ed.fl_src[2];
    int t0 = ed.fl_src[2] < ed.fl_track ? ed.fl_src[2] : ed.fl_track;
    int t1 = ed.fl_src[3] > ed.fl_track + tn ? ed.fl_src[3] : ed.fl_track + tn;
    each_track(t0, t1, f_drop, 0);
    ed.floating = 0;
}
static void take_inst(int from)
{
    rec_cell_t c;
    cell_at(ed.row, ed.track, &c);
    int ref = (c.has & RC_INS) ? c.cart << 8 | c.slot : ed_inst_at(pat(), ed.row, ed.track);
    char nm[16], rt[8], v[48];
    if (ref < 0 || !ui_inst_name((uint16_t)ref, nm)) { ui_refuse(from); ui_toast(T("NO INSTRUMENT HERE")); return; }
    ui_set_inst((uint16_t)ref);
    ui_ref_text(rt, (uint16_t)ref);
    int eng = ui_cart_kind((uint8_t)(ref >> 8));
    ui_fmt(v, sizeof v, "%s %s %s", rt, nm, T(ENG_SHORT[eng]));
    ui_toast(v);
}

enum { M_MARK = 1, M_TAKE, M_COPY, M_CUT, M_PASTE, M_PASTE_MIX, M_CLEAR, M_MOVE, M_SWAP, M_INSERT, M_DELETE, M_TRANSPOSE,
       M_INTERP, M_REVERT, M_RECORD, M_NEW_PAT, M_LOAD_PAT, M_STORE_PAT };
/* REVERT (specs/ui/saving.md#revert): the pattern shown as saved, every cell, its tracks, rows and edit step; what
 * plays it hears it so from its next row */
static void revert_go(void)
{
    ui_edit_revert(REC_PATTERN, pat(), 0);
    clamp_cursor();
    clamp_slot();
}

/* the EDITOR's own pattern (specs/ui/screens/editor.md#the-editors-own-pattern): SEL's NEW PATTERN, LOAD A PATTERN
 * (a copy into it), STORE IN A PATTERN (a copy of what the EDITOR shows into a numbered one, the EDITOR following);
 * every way to another pattern asks first while the EDITOR's own holds anything (ed_own_ask) */
static void go_to(int n)
{
    stop_recording();
    ui_cur.pattern = (uint8_t)n;
    ed.row = 0; ed.left = 0;
    clamp_cursor();
    clamp_slot();
}
static void own_new(void)
{
    ed_own_empty();                                     /* (8 x 64 again, whatever its size was) */
    go_to(UI_EDITOR_PATTERN);
}
static int load_from, store_to;
static void load_go(void)
{
    if (ed_copy_pattern((uint8_t)load_from, UI_EDITOR_PATTERN)) return;
    go_to(UI_EDITOR_PATTERN);
    ui_toast(ui_txt_v(T("PATTERN # LOADED"), ui_hex2(load_from)));
}
static void load_pick(int n) { load_from = n; ed_own_ask(load_go); }
static void store_go(void)
{
    int from = pat();
    if (ed_copy_pattern((uint8_t)from, (uint8_t)store_to)) return;
    if (from == UI_EDITOR_PATTERN) ed_own_empty();      /* (its notes moved into the numbered one) */
    go_to(store_to);
    ui_toast(ui_txt_v(T("STORED IN PATTERN #"), ui_hex2(store_to)));
}
static void store_own(void) { if (own()) store_go(); else ed_own_ask(store_go); }   /* (from a numbered one: as any move) */
static void store_answer(int a) { if (a == 1) store_own(); }
static void store_pick(int n)
{
    if (n == ED_PICK_NEW && (n = ed_first_free()) < 0) { ui_toast(T("NO FREE PATTERN")); return; }
    store_to = n;
    if (!ed_pattern_holds((uint8_t)n)) { store_own(); return; }
    static char l0[32];                                 /* (the question copies its lines) */
    ui_fmt(l0, sizeof l0, T("PATTERN %02X WILL BE REPLACED"), n);
    const char *lines[2] = {l0, T("DO YOU WANT TO CONTINUE?")};
    ui_ask(lines, 2, T("YES"), T("NO"), store_answer);
}

/* SEL -> RECORD PATTERN (ui_recpat.c): REC puts its notes into the cursor's track from the cursor's
 * row on to the pattern's end, or once, at the song's ROWS A BEAT; each note the current instrument's, a step off
 * leaves its cell as it is; with A STEP LONG note offs on empty cells. One edit. */
static int song_rpb(void) { ui_song_t s; return ui_song(ui_cur.song, &s) && s.rpb ? s.rpb : 4; }
static uint16_t rp_sound(void) { return ui_cur.inst; }
static int rp_song(rec_song_t *s)                       /* the preview: the song's tempo, the track's mixer */
{
    if (rec_song(ui_cur.song, s)) return -1;
    memcpy(s->mix[0], s->mix[ed.track & 15], RM_N);
    return 0;
}
static void rp_pause(void) { player_state_t st; ui_player_state(&st); if (st.playing) player_pause(); }
static int rp_where(g_run_t *r, char *buf, int n)
{
    int k = ui_fmt(buf, n, T("ROW %s"), ui_hex2(ed.row)) + 1;
    ui_fmt(buf + k, n - k, T(" OF %s"), ui_hex2(rows()));
    r[0] = (g_run_t){T("FROM "), C_LGREY};
    r[1] = (g_run_t){buf, C_WHITE};
    r[2] = (g_run_t){buf + k, C_LGREY};
    return 3;
}
/* its notes and (A STEP LONG) note offs, the latter on empty cells only: one edit, all or nothing */
static void f_rp(int t, int r, rec_cell_t *c, void *a)
{
    const uint8_t *const *plan = a;                     /* (on, off: a row each) */
    (void)t;
    if (plan[0][r]) note_into(c, ui_rp.note);
    else if (plan[1][r] && empty(c)) { memset(c, 0, sizeof *c); c->has = RC_NOTE; c->note = REC_NOTE_OFF; }
}
static int rp_write(void)
{
    int notes;
    uint8_t on[PAT_ROWS_MAX], off[PAT_ROWS_MAX];
    const uint8_t *plan[2] = {on, off};
    notes = ui_rp_plan(rows(), ed.row, 0, ui_rp_step_rows(song_rpb()), 0, on, off);
    if (!each_track(ed.track, ed.track, f_rp, plan)) return 0;
    if (ed.note_ed) ed.nf_changed = 1;
    ui_toast(ui_txt_v(notes == 1 ? T("PATTERN: # NOTE") : T("PATTERN: # NOTES"), ui_dec(notes)));
    return 1;
}
static void record_pattern(void)
{
    rec_cell_t c;
    char title[36];
    cell_at(ed.row, ed.track, &c);
    ui_fmt(title, sizeof title, "%s", ui_txt_v(T("RECORD PATTERN INTO TRACK #"), ui_hex2(ed.track)));
    ui_rp_open_t o = {B_SEL, title, song_rpb(), (c.has & RC_NOTE) && c.note != REC_NOTE_OFF ? c.note : 60, TX("TO THE END"),
                      rp_sound, rp_song, rp_pause, rp_where, rp_write};
    ui_rp_open(&o);
}
static void block_pick(int a)
{
    int b[4];
    block_rect(b);
    memcpy(bl, b, sizeof bl);
    ed.marking = 0;
    char v[40];
    switch (a) {
    case M_RECORD: record_pattern(); return;
    case M_TAKE: take_inst(B_SEL); return;
    case M_MARK:
        ed.marking = 1; ed.ar = ed.row; ed.at = ed.track; block_of_anchor();
        ui_toast(T("SELECT, ALG: MARK  SEL: ITS MENU"));
        return;
    case M_COPY:
        if (ed_clip_copy(pat(), b[0], b[1], b[2], b[3])) { ui_toast(T("BLOCK TOO BIG TO COPY")); return; }
        ui_fmt(v, sizeof v, T("COPIED %d X %d"), b[1] - b[0] + 1, b[3] - b[2] + 1);
        ui_toast(v);
        return;
    case M_CUT:
        if (ed_clip_copy(pat(), b[0], b[1], b[2], b[3])) { ui_toast(T("BLOCK TOO BIG TO COPY")); return; }
        each_track(b[2], b[3], f_clear, 0);
        ed.block = 0;
        return;
    case M_PASTE: case M_PASTE_MIX:
        if (!ed_clip_rows()) { ui_toast(T("NOTHING COPIED")); return; }
        paste_r0 = ed.row; paste_t0 = ed.track; paste_mix = a == M_PASTE_MIX;
        each_track(ed.track, ed.track + ed_clip_tracks() - 1, f_paste, 0);
        return;
    case M_CLEAR: each_track(b[2], b[3], f_clear, 0); ed.block = 0; return;
    case M_MOVE: case M_SWAP:
        ed.floating = 1; ed.fl_swap = a == M_SWAP;
        memcpy(ed.fl_src, b, sizeof ed.fl_src);
        ed.fl_row = ed.row = b[0]; ed.fl_track = ed.track = b[2]; ed.block = 0;
        ui_toast(a == M_MOVE ? T("MOVE: SEL PUTS IT DOWN") : T("SWAP: SEL PUTS IT DOWN"));
        return;
    case M_INSERT: case M_DELETE: {
        int ins = a == M_INSERT;
        each_track(b[2], ed.block ? b[3] : tracks() - 1, f_rows, &ins);
        return;
    }
    case M_TRANSPOSE: ed.tmode = 1; ed.transpose = 0; memcpy(ed.tb, b, sizeof ed.tb); ui_toast(T("K1: TRANSPOSE, SEL: DONE")); return;
    case M_INTERP: each_track(b[2], b[3], f_interp, 0); return;
    case M_REVERT: {
        static char q[36];
        if (own()) ui_fmt(q, sizeof q, "%s", T("REVERT THE PATTERN TO SAVED?"));
        else ui_fmt(q, sizeof q, T("REVERT PATTERN %02X TO SAVED?"), pat());
        ui_ask_revert(q, 0, revert_go);
        return;
    }
    case M_NEW_PAT: ed_own_ask(own_new); return;
    case M_LOAD_PAT: ed_pattern_list(T("LOAD INTO THE EDITOR"), pat(), load_pick, B_SEL, -1); return;
    case M_STORE_PAT: ed_pattern_list_new_but(T("STORE THE PATTERN IN"), pat(), store_pick, B_SEL); return;
    default: return;
    }
}
static void transpose(int d)
{
    memcpy(bl, ed.tb, sizeof bl);
    if (!each_track(ed.tb[2], ed.tb[3], f_transpose, &d)) return;
    ed.transpose += d;
    char v[8];
    ui_fmt(v, sizeof v, "%s%02X", ed.transpose > 0 ? "+" : ed.transpose < 0 ? "-" : "", ed.transpose < 0 ? -ed.transpose : ed.transpose);
    ui_show_knob(T("TRANSPOSE"), v);
}
static void block_menu(void)
{
    if (ed.floating) { drop_float(); return; }
    if (ed.tmode) {
        char v[24];
        ed.tmode = 0;
        ui_fmt(v, sizeof v, T("TRANSPOSED %s%02X"), ed.transpose > 0 ? "+" : ed.transpose < 0 ? "-" : "", ed.transpose < 0 ? -ed.transpose : ed.transpose);
        ui_toast(v);
        return;
    }
    int b[4];
    block_rect(b);
    ui_mitem_t items[16], rows_g[2], tr_g[2];           /* (the menu copies its items) */
    char title[32];
    int n = 0;
    /* (a menu's items are a table: TX texts. The CELL menu fills all 16: one more would be left out, never
     * written past the table) */
#define ADD(nm, x, g, gn) do { if (n < (int)(sizeof items / sizeof items[0])) items[n++] = (ui_mitem_t){nm, x, g, gn}; } while (0)
    if (!ed.block) ADD(TX("MARK"), M_MARK, 0, 0);
    if (!ed.block) ADD(TX("TAKE ITS INSTRUMENT"), M_TAKE, 0, 0);
    ADD(TX("COPY"), M_COPY, 0, 0);
    {                                                   /* (a built-in pattern is edited as any) */
        ADD(TX("CUT"), M_CUT, 0, 0);
        ADD(TX("PASTE"), M_PASTE, 0, 0);
        ADD(TX("PASTE MIX"), M_PASTE_MIX, 0, 0);
        ADD(TX("CLEAR"), M_CLEAR, 0, 0);
        ADD(TX("MOVE"), M_MOVE, 0, 0);
        ADD(TX("SWAP"), M_SWAP, 0, 0);
        rows_g[0] = (ui_mitem_t){TX("INSERT ROW"), M_INSERT, 0, 0};
        rows_g[1] = (ui_mitem_t){TX("DELETE ROW"), M_DELETE, 0, 0};
        ADD(TX("ROWS"), 0, rows_g, 2);
        /* INTERPOLATE needs two rows: on one, TRANSPOSE shows in the group's place */
        if (b[1] > b[0]) {
            tr_g[0] = (ui_mitem_t){TX("TRANSPOSE"), M_TRANSPOSE, 0, 0};
            tr_g[1] = (ui_mitem_t){TX("INTERPOLATE"), M_INTERP, 0, 0};
            ADD(TX("TRANSFORM"), 0, tr_g, 2);
        } else {
            ADD(TX("TRANSPOSE"), M_TRANSPOSE, 0, 0);
        }
        if (!ed.block) {                                /* (the EDITOR's own pattern's, specs/ui/screens/editor.md) */
            ADD(TX("NEW PATTERN"), M_NEW_PAT, 0, 0);
            ADD(TX("LOAD A PATTERN"), M_LOAD_PAT, 0, 0);
            ADD(TX("STORE IN A PATTERN"), M_STORE_PAT, 0, 0);
        }
        if (!ed.block) ADD(TX("RECORD PATTERN"), M_RECORD, 0, 0);
        if (!ed.block && ui_edit_unsaved(REC_PATTERN, pat(), 0)) ADD(TX("REVERT"), M_REVERT, 0, 0);
    }
#undef ADD
    if (ed.block) ui_fmt(title, sizeof title, T("BLOCK %d X %d"), b[1] - b[0] + 1, b[3] - b[2] + 1);
    else ui_fmt(title, sizeof title, "%s", T("CELL"));
    ui_menu("block", title, items, n, block_pick, B_SEL);
}

/* ------------------------------------------------------------------- GLO: the properties --- */
static int new_ntrk, new_rows, new_step;
static void resize_go(void)
{
    ed_rewrite(pat(), 1, 0, 0, 0, new_ntrk, new_rows, new_step);      /* (no track rewritten: only the size) */
    clamp_cursor();
}
static void resize_answer(int a) { if (a == 1) resize_go(); }
static void resize(int ntrk, int nrows, int step)
{
    new_ntrk = ntrk; new_rows = nrows; new_step = step;
    int k = ed_drops(pat(), ntrk, nrows);
    if (!k) { resize_go(); return; }
    const char *lines[2];                               /* (the question copies its lines) */
    char l0[32];
    ui_fmt(l0, sizeof l0, k == 1 ? T("THIS DROPS %d CELL") : T("THIS DROPS %d CELLS"), k);
    lines[0] = l0;
    lines[1] = T("MAKE THE PATTERN SMALLER?");
    ui_ask(lines, 2, T("YES"), T("NO"), resize_answer);
}
static int cur_step(void) { rec_pat_t p; return rec_pattern(pat(), &p) ? 1 : p.step; }
static const char *p_tracks(void) { return ui_dec(tracks()); }
static const char *p_rows(void) { return ui_dec(rows()); }
static const char *p_step(void) { return ui_dec(cur_step()); }
static const char *p_used(void)
{
    static char b[24];
    int n = ed_pattern_uses(pat()) - ed_pattern_fusion(pat());
    ui_fmt(b, sizeof b, n == 1 ? T("%d SONG%s") : T("%d SONGS%s"), n, ed_pattern_fusion(pat()) ? T(", FUSION") : "");
    return b;
}
static void c_tracks(int d, int c) { (void)c; resize(ui_clamp(tracks() + d, 1, 16), rows(), cur_step()); }
static void c_rows(int d, int c) { resize(tracks(), ui_clamp(rows() + d * (c ? 16 : 1), 1, PAT_ROWS_MAX), cur_step()); }
static void c_step(int d, int c) { (void)c; resize(tracks(), rows(), ui_clamp(cur_step() + d, 0, 16)); }
static void props(void)
{
    char title[32];
    ui_prop_t items[4] = {{TX("TRACKS"), p_tracks, c_tracks, 0, 0, 0}, {TX("ROWS"), p_rows, c_rows, 0, 0, 0},
                          {TX("EDIT STEP"), p_step, c_step, 0, 0, 0}, {TX("USED BY"), p_used, 0, 0, 0, 0}};
    if (own()) { ui_props(T("PATTERN PROPERTIES"), items, 3, B_GLO); return; }     /* (its own: no number, no USED BY) */
    ui_fmt(title, sizeof title, T("PATTERN %02X PROPERTIES"), pat());
    ui_props(title, items, 4, B_GLO);
}

/* ------------------------------------------------------------------------ the note editor --- */
static void nf_cell(rec_cell_t *c) { cell_at(ed.row, ed.track, c); if (!(c->has & RC_CMD)) c->n = 0; }
static const ui_entry_t *entry_of(const uint8_t *x) { int e = ui_cmd_entry(x[0], x[1]); return &UI_ENTRIES[e > 0 ? e : 0]; }
static int parts_of(const uint8_t *x) { return !x ? 1 : entry_of(x)->digits == 0 ? 1 : 2; }
static void nf_clamp(void)
{
    rec_cell_t c;
    nf_cell(&c);
    ed.nf_line = ui_clamp(ed.nf_line, 0, L_CMD + c.n < LINES - 1 ? L_CMD + c.n : LINES - 1);
    const uint8_t *x = ed.nf_line >= L_CMD && ed.nf_line - L_CMD < c.n ? c.cmd[ed.nf_line - L_CMD] : 0;
    if (ed.nf_part >= parts_of(x) || ed.nf_line < L_CMD) ed.nf_part = 0;
}
static void nf_open(void)
{
    ed.nf_line = ed.slot == 0 ? L_VOL : L_CMD + ed.slot - 1;
    ed.nf_want = ed.nf_line;
    ed.nf_part = 0;
    ed.nf_cells = 0;
    ed.nf_changed = 0;
    ed.note_ed = 1;
    ui_edit_session(1);                 /* the mode's copy: HOME puts back what it changed */
    nf_clamp();
}
static void nf_close(int keep)
{
    if (!keep && ed.nf_changed) ui_edit_session_drop(); /* HOME: every cell changed since EDIT as it was */
    else ui_edit_session(0);
    if (ed.nf_line == L_VOL) ed.slot = 0;
    else if (ed.nf_line >= L_CMD) ed.slot = ed.nf_line - L_CMD + 1;
    ed.note_ed = 0;
    clamp_slot();
}
/* the commands K1 turns through, K2's groups opened up: each tracker effect, each parameter of the engine,
 * each mixer value, each FX code; g the group K2 jumps by */
typedef struct { int16_t c; int8_t sub; uint8_t g; } kind_t;
/* the list's q-th entry (want >= 0) into *out; returns how many there are (computed as it goes) */
static int kinds(int eng, int want, kind_t *out)
{
    int n = 0;
    if (want == n++ && out) *out = (kind_t){-1, -1, 0};
    for (int i = 1; i < UI_NENTRIES; i++) {
        const ui_entry_t *e = &UI_ENTRIES[i];
        if (e->code >= CE_GROUP) {
            uint8_t codes[64];
            int nc = ui_cmd_codes(e->code - CE_GROUP, eng, codes, 64), g = 4 + e->code - CE_GROUP;
            for (int j = 0; j < nc; j++) if (want == n++ && out) *out = (kind_t){codes[j], -1, (uint8_t)g};
        } else if (want == n++ && out) {   /* (LEG in ACC / SLD / A+S's group) */
            *out = (kind_t){e->code, e->sub, (uint8_t)(e->code < 0x24 ? 1 : e->code < 0x30 ? 2 : e->code < 0xC3 || e->code == 0xD3 ? 3 : 4)};
        }
    }
    return n;
}
static void kind_get(int eng, int q, kind_t *k) { kinds(eng, q, k); }
static int group_at(int eng, int q) { kind_t k; kinds(eng, q, &k); return k.g; }
static int same_kind(const kind_t *k, const uint8_t *x) { return x ? k->c == x[0] && (k->sub < 0 || k->sub == x[1] >> 4) : k->c < 0; }
/* an entry may go in slot `self` of the cell: none, the one there, or a code no other slot holds */
static int kind_ok(int eng, int q, const rec_cell_t *c, int self, const uint8_t *x)
{
    kind_t k;
    kinds(eng, q, &k);
    if (k.c < 0 || same_kind(&k, x)) return 1;
    for (int j = 0; j < c->n; j++) if (j != self && c->cmd[j][0] == k.c) return 0;
    return 1;
}
/* the instruments as the note editor's INSTR turns them: -1 (as the track), then every used slot,
 * cartridge by cartridge. K1: d steps; K2: d cartridges, each at its first */
static int used_slot(int c, int s) { char nm[16]; return ui_inst_name((uint16_t)(c << 8 | s), nm); }
static int inst_step(int ref, int d)
{
    uint8_t carts[256];
    int nc = ui_carts(carts, 256);
    for (int m = 0; m < (d < 0 ? -d : d); m++) {
        int found = -2, ci = -1;
        for (int i = 0; i < nc; i++) if (ref >= 0 && carts[i] == ref >> 8) ci = i;
        if (d > 0) {
            for (int i = ref < 0 ? 0 : ci; i < nc && found < -1; i++) {
                int from = i == ci && ref >= 0 ? (ref & 255) + 1 : 0, ns = ui_cart_slots(carts[i]);
                for (int s = from; s < ns; s++) if (used_slot(carts[i], s)) { found = carts[i] << 8 | s; break; }
            }
        } else if (ref >= 0) {
            found = -1;
            for (int i = ci; i >= 0 && found == -1; i--) {
                int from = i == ci ? (ref & 255) - 1 : ui_cart_slots(carts[i]) - 1;
                for (int s = from; s >= 0; s--) if (used_slot(carts[i], s)) { found = carts[i] << 8 | s; break; }
            }
        }
        if (found == -2) break;
        ref = found;
    }
    return ref;
}
static int cart_step(int ref, int d)
{
    uint8_t carts[256];
    int nc = ui_carts(carts, 256), ci = -1;
    for (int i = 0; i < nc; i++) if (ref >= 0 && carts[i] == ref >> 8) ci = i;
    for (int want = ci + d; want >= -1 && want < nc; want += d > 0 ? 1 : -1) {
        if (want < 0) return -1;
        int ns = ui_cart_slots(carts[want]);
        for (int s = 0; s < ns; s++) if (used_slot(carts[want], s)) return carts[want] << 8 | s;
        if (!d) break;
    }
    return ref;
}
static void fresh_of(const kind_t *k, int eng, uint8_t *x)
{
    x[0] = (uint8_t)k->c;
    x[1] = 0;
    int g = ui_cmd_group(x[0]);
    if (k->sub >= 0) x[1] = (uint8_t)(k->sub << 4);
    else if (ui_cmd_punch(x[0]) >= 0) x[1] = 1;
    else if (g == CG_PARAM) { int mx = ui_cmd_max(x[0], eng); x[1] = (uint8_t)(mx < 64 ? mx : 64); }
    else if (g == CG_MIXER) x[1] = 0x7F;
}
static void nf_knob(int s, int big)
{
    int r = ed.row, t = ed.track, eng = engine_at(r, t), d = s, l = ed.nf_line;
    rec_cell_t c;
    nf_cell(&c);
    char v[48], nm[16], rt[8];
    const char *kn = big ? T("K2") : T("K1");
    if (l == L_NOTE) {                                  /* none, a note off, then C-0 ... G-9 */
        int idx = !(c.has & RC_NOTE) ? 0 : c.note == REC_NOTE_OFF ? 1 : c.note - 10;
        int ni = big && idx >= 2 ? ui_clamp(idx + 12 * d, 2, 117) : ui_clamp(idx + d, 0, 117);
        if (!ni) c.has &= (uint8_t)~RC_NOTE;
        else { c.has |= RC_NOTE; c.note = (uint8_t)(ni == 1 ? REC_NOTE_OFF : ni + 10); }
        if (ni >= 2 && !(c.has & RC_INS) && ed_inst_at(pat(), r, t) < 0) {
            c.has |= RC_INS; c.cart = (uint8_t)(ui_cur.inst >> 8); c.slot = (uint8_t)ui_cur.inst;
        }
        char nn[8];
        ui_note_name(nn, !(c.has & RC_NOTE) ? -1 : c.note);
        ui_fmt(v, sizeof v, T("NOTE %s"), nn);
        ui_show_knob(kn, v);
    } else if (l == L_INST) {                           /* as the track, then each instrument, cartridge by cartridge */
        int ref = (c.has & RC_INS) ? c.cart << 8 | c.slot : -1;
        ref = big ? cart_step(ref, d) : inst_step(ref, d);
        if (ref < 0) c.has &= (uint8_t)~RC_INS;
        else { c.has |= RC_INS; c.cart = (uint8_t)(ref >> 8); c.slot = (uint8_t)ref; }
        if (!(c.has & RC_INS)) ui_show_knob(kn, T("AS THE TRACK"));
        else {
            ui_ref_text(rt, (uint16_t)(c.cart << 8 | c.slot));
            ui_inst_name((uint16_t)(c.cart << 8 | c.slot), nm);
            ui_fmt(v, sizeof v, "%s %s", rt, nm);
            ui_show_knob(kn, v);
        }
    } else if (l == L_VOL) {                            /* past 7F: the instrument's own */
        int cv = (c.has & RC_VOL) ? c.vol : 0x80, nv = cv + d * (big ? 16 : 1);
        if (nv > 0x7F) c.has &= (uint8_t)~RC_VOL;
        else { c.has |= RC_VOL; c.vol = (uint8_t)ui_clamp(nv, 0, 0x7F); }
        ui_show_knob(T("VOLUME"), (c.has & RC_VOL) ? ui_hex2(c.vol) : T("AS THE INSTRUMENT"));
    } else {
        int k = l - L_CMD;
        uint8_t *x = k < c.n ? c.cmd[k] : 0;
        if (ed.nf_part == 0) {                          /* what it is: K1 one, K2 the next group's first */
            int n = kinds(eng, 0, 0), i = 0;
            kind_t q;
            for (int j = 0; j < n; j++) { kind_get(eng, j, &q); if (same_kind(&q, x)) { i = j; break; } }
            if (big) {
                for (int m = 0; m < (d < 0 ? -d : d); m++) {
                    int g = group_at(eng, i), j;
                    if (d > 0) {                        /* the next group's first that may go here */
                        for (j = i + 1; j < n && (group_at(eng, j) == g || !kind_ok(eng, j, &c, k, x)); j++) {}
                        if (j < n) i = j;
                    } else {                            /* this group's first, else the one before's */
                        int f = i;
                        while (f > 0 && group_at(eng, f - 1) == g) f--;
                        if (f == i && f > 0) { f--; int g2 = group_at(eng, f); while (f > 0 && group_at(eng, f - 1) == g2) f--; }
                        while (f < i && !kind_ok(eng, f, &c, k, x)) f++;
                        i = f;
                    }
                }
            } else {
                for (int m = 0; m < (d < 0 ? -d : d); m++) {
                    int j = i + (d > 0 ? 1 : -1);
                    while (j >= 0 && j < n && !kind_ok(eng, j, &c, k, x)) j += d > 0 ? 1 : -1;
                    if (j < 0 || j >= n) break;
                    i = j;
                }
            }
            kind_get(eng, i, &q);
            if (q.c < 0) { if (x) remove_cmd(&c, k); }
            else if (!same_kind(&q, x)) {
                uint8_t nx[2];
                fresh_of(&q, eng, nx);
                if (x) set_cmd(&c, k, nx);
                else { set_cmd(&c, c.n, nx); ed.nf_line = ed.nf_want = L_CMD + c.n - 1; }
            }
            int kk = ed.nf_line - L_CMD;
            if (kk >= 0 && kk < c.n) {
                char a[4];
                cmd_text3(c.cmd[kk], a);
                ui_fmt(v, sizeof v, "%s %s", a, T(entry_of(c.cmd[kk])->name));
                ui_show_knob(kn, v);
            } else ui_show_knob(kn, T("NO COMMAND"));
        } else if (x) {                                 /* its value */
            const ui_entry_t *e = entry_of(x);
            int step = big ? 16 : 1;
            if (e->digits == 1) x[1] = (uint8_t)((x[1] & 0xF0) | ui_clamp((x[1] & 15) + d * step, 0, 15));
            else if (e->digits == 2) x[1] = (uint8_t)ui_clamp(x[1] + d * step, e->min, 255);
            else x[1] = (uint8_t)ui_clamp(x[1] + d * step, 0, ui_cmd_max(x[0], eng));
            show_cmd(0, x, eng);
        }
    }
    put(r, t, &c);
    nf_clamp();
}
static void nf_move(int e, int s)
{
    if (ed.nf_cells) {                                  /* SEL tapped: the rows and tracks */
        if (e == K_SELECT) ed.row = ui_clamp(ed.row + s, 0, rows() - 1);
        else ed.track = ui_clamp(ed.track + sign(s), 0, tracks() - 1);
        scroll_x();
        ed.nf_line = ed.nf_want;
        nf_clamp();
    } else if (e == K_SELECT) { ed.nf_line += s; nf_clamp(); ed.nf_want = ed.nf_line; }
    else {
        rec_cell_t c;
        nf_cell(&c);
        const uint8_t *x = ed.nf_line >= L_CMD && ed.nf_line - L_CMD < c.n ? c.cmd[ed.nf_line - L_CMD] : 0;
        ed.nf_part = ui_clamp(ed.nf_part + sign(s), 0, parts_of(x) - 1);
        nf_clamp();
    }
}
/* a command line's two parts as text: what it is, and its value ("" none) */
static void cmd_parts(const uint8_t *x, int eng, char *a, char *b)
{
    char w[48];
    b[0] = 0;
    if (!x) { strcpy(a, T("NO COMMAND")); return; }
    int g = ui_cmd_group(x[0]);
    const ui_entry_t *e = entry_of(x);
    ui_cmd_words(x[0], x[1], eng, w, sizeof w);
    if (g == CG_PARAM || g == CG_FX) {
        char n[32];
        ui_cmd_code_name(x[0], eng, n, sizeof n);
        if (g == CG_FX && (x[0] == 0xAE || x[0] == 0xAF)) {
            ui_fmt(a, 32, "%s", n);
            if (x[1] < FE_N) ui_fmt(b, 24, "%s", T(FX_EFF[x[1]].name)); else ui_fmt(b, 24, "%02X", x[1]);
            return;
        }
        ui_fmt(a, 32, "%s", n);
        size_t ln = strlen(n);
        if (!strncmp(w, n, ln) && w[ln] == ' ') ui_fmt(b, 24, "%s", w + ln + 1); else ui_fmt(b, 24, "%02X", x[1]);
        return;
    }
    if (g == CG_MIXER) { ui_cmd_code_name(x[0], eng, a, 32); ui_fmt(b, 24, "%02X", x[1]); return; }
    if (ui_cmd_punch(x[0]) >= 0) { ui_fmt(a, 32, "%s", T(e->name)); ui_fmt(b, 24, x[1] == 1 ? T("%d ROW") : T("%d ROWS"), x[1]); return; }
    ui_fmt(a, 32, "%s", T(e->name));
    if (e->digits == 1) ui_fmt(b, 24, "%X", x[1] & 15);
    else if (e->digits == 2) ui_fmt(b, 24, "%02X", x[1]);
}
static void draw_note_editor(void)
{
    rec_cell_t c;
    nf_cell(&c);
    int eng = engine_at(ed.row, ed.track), y0 = BODY_Y;
    int ref = (c.has & RC_INS) ? c.cart << 8 | c.slot : ed_inst_at(pat(), ed.row, ed.track);
    char nm[16], rt[8], line[48], a[32], b[24];
    int col = ref >= 0 && ui_inst_name((uint16_t)ref, nm) ? ENG_COL[ui_cart_kind((uint8_t)(ref >> 8))] : C_LGREY;
    g_rect(0, y0, 240, BODY_H, C_BLACK);
    int fus = ed_pattern_fusion(pat()) && ed.row >= 12 && ed.row < 128;
    char nn[8];
    ui_note_name(nn, ed.row);
    if (own()) ui_fmt(line, sizeof line, T("ROW %02X  TRACK %02X"), ed.row, ed.track);   /* (its own: no number) */
    else ui_fmt(line, sizeof line, T("ROW %02X%s%s  TRACK %02X  PATTERN %02X"), ed.row, fus ? " " : "", fus ? nn : "", ed.track, pat());
    g_text(4, y0 + 1, &FONT_T, line, ed.nf_cells ? C_WHITE : C_CYAN);
    for (int i = 0; i < LINES; i++) {
        int y = y0 + 16 + i * 24, on = ed.nf_line == i, k = C_DGREY;
        const char *lab = "";
        a[0] = b[0] = 0;
        if (i == L_NOTE) {
            lab = T("NOTE");
            ui_note_name(a, !(c.has & RC_NOTE) ? -1 : c.note);
            k = !(c.has & RC_NOTE) ? C_DGREY : c.note == REC_NOTE_OFF ? C_LGREY : col;
        } else if (i == L_INST) {
            lab = T("INSTR");
            if (c.has & RC_INS) {
                ui_ref_text(rt, (uint16_t)(c.cart << 8 | c.slot));
                if (!ui_inst_name((uint16_t)(c.cart << 8 | c.slot), nm)) nm[0] = 0;
                ui_fmt(a, sizeof a, "%s %s", rt, nm);
                k = col;
            } else strcpy(a, T("AS THE TRACK"));
        } else if (i == L_VOL) {
            lab = T("VOLUME");
            if (c.has & RC_VOL) { strcpy(a, ui_hex2(c.vol)); ui_fmt(b, sizeof b, "%d %%", (c.vol * 100 + 63) / 127); k = C_BROWN; }
            else strcpy(a, T("AS THE INSTRUMENT"));
        } else {
            static const txt_t CMDL[4] = {TX("CMD 1"), TX("CMD 2"), TX("CMD 3"), TX("CMD 4")};
            int q = i - L_CMD;
            lab = T(CMDL[q]);
            if (q < c.n) { cmd_parts(c.cmd[q], eng, a, b); k = C_LBLUE; }
            else if (q == c.n) strcpy(a, T("+ COMMAND"));
        }
        if (on) { g_hline(0, y, 240, C_DGREY); g_hline(0, y + 23, 240, C_DGREY); }
        g_text(4, y + 6, &FONT_T, lab, on ? C_WHITE : C_CYAN);
        g_textn(52, y + 4, &FONT_S, a, 23, on && ed.nf_part == 0 ? C_WHITE : k);
        if (b[0]) g_text(236 - g_tw(&FONT_S, b), y + 4, &FONT_S, b, on && ed.nf_part == 1 ? C_WHITE : k);
        if (on && !a[0]) g_text(52, y + 4, &FONT_S, "...", C_WHITE);
    }
    const char *big = ed.nf_line == L_NOTE ? T("OCTAVE") : ed.nf_line == L_INST ? T("CARTRIDGE") : ed.nf_line == L_VOL ? T("BY 16")
                    : ed.nf_part == 0 ? T("NEXT GROUP") : T("BY 16");
    g_run_t r[6];
    if (ed.nf_cells) {
        r[0] = (g_run_t){T("SELECT"), C_YELLOW}; r[1] = (g_run_t){T(" ROWS  "), C_LGREY};
        r[2] = (g_run_t){T("ALG"), C_YELLOW}; r[3] = (g_run_t){T(" TRACKS  "), C_LGREY};
        r[4] = (g_run_t){T("SEL"), C_YELLOW}; r[5] = (g_run_t){T(" FIELDS"), C_LGREY};
    } else {
        static char bb[24];
        ui_fmt(bb, sizeof bb, " %s  ", big);
        r[0] = (g_run_t){T("K1"), C_YELLOW}; r[1] = (g_run_t){T(" STEP  "), C_LGREY};
        r[2] = (g_run_t){T("K2"), C_YELLOW}; r[3] = (g_run_t){bb, C_LGREY};
        r[4] = (g_run_t){T("SEL"), C_YELLOW}; r[5] = (g_run_t){T(" CELLS"), C_LGREY};
    }
    g_runs(0, y0 + 16 + LINES * 24 + 2, &FONT_T, r, 6);
}

/* ------------------------------------------------------------------------- the row preview --- */
/* ARP: the cursor's row repeats at the song's tempo in SETTINGS' ARP RHYTHM (`#` played, `_` a rest) */
static void arp_off(void)
{
    for (int t = 0; t < 16; t++) if (ed.arp_on[t]) { player_key_off(ed.arp_on[t]); ed.arp_on[t] = 0; }
}
static uint32_t row_ms(void)
{
    ui_song_t s;
    if (!ui_song(ui_cur.song, &s) || !s.tempo) return 125;
    return (uint32_t)s.ticks * 2500u / s.tempo;
}
static void arp_tick(void)
{
    if (!ed.arp) return;
    uint32_t now = ui_now();
    if ((int32_t)(now - ed.arp_t) < 0) return;
    ed.arp_t = now + row_ms();
    const char *rh = T(UI_SETTINGS[S_ARP_RHYTHM].list[ui_set[S_ARP_RHYTHM]]);
    int n = (int)strlen(rh), k = ed.arp_step++ % (n ? n : 1);
    if (rh[k] != '#') return;
    arp_off();
    for (int t = 0; t < tracks() && t < 16; t++) {
        rec_cell_t c;
        cell_at(ed.row, t, &c);
        if (!(c.has & RC_NOTE) || c.note == REC_NOTE_OFF || ui_muted(t)) continue;
        int ref = (c.has & RC_INS) ? c.cart << 8 | c.slot : ed_inst_at(pat(), ed.row, t);
        if (ref < 0) continue;
        pl_inst_t in = {(uint8_t)(ref >> 8), (uint8_t)ref};
        player_key_on(c.note, in, (c.has & RC_VOL) ? c.vol : 0x7F);
        ed.arp_on[t] = c.note;
    }
}

/* ------------------------------------------------------------------------------- the screen --- */
static const char *sub(void) { return own() ? 0 : ui_hex2(pat()); }    /* (`EDITOR` alone on its own pattern) */
static const char *seq(void)
{
    static char b[12];
    if (ed.view == 1) return T("1 TRACK");
    ui_fmt(b, sizeof b, T("%d TRACKS"), ed.view);
    return b;
}
static const char *page(void)
{
    static char b[12];
    int nr = V()->rows, n = (rows() + nr - 1) / nr, pr = play_row();
    int top = recording() && pr >= 0 ? pr : ed.row;
    if (n <= 1) return 0;
    ui_page_of(b, top / nr, n);
    return b;
}
static void enter(const ui_screen_t *from)
{
    (void)from;
    for (int n = 0; n < 27; n++) ed.held_track[n] = -1;
    ed.chord_row = -1;
    ed.fusion = -1;
    if (ed.want) {
        ed.want = 0;
        ed.row = ed.want_row;
        ed.track = ed.want_track;
        ed.fusion = ed.want_fusion;
        ed.left = 0;
    }
    pat();
    clamp_cursor();
    clamp_slot();
}
static void leave(const ui_screen_t *to)
{
    (void)to;
    stop_recording();
    if (ed.arp) { ed.arp = 0; arp_off(); }
    if (ed.note_ed) nf_close(1);
    ed.marking = 0; ed.armed = 0;
}
static int lit(int b)
{
    if (b == B_REC) return ed.rec ? (recording() || ed.armed ? (ui_blink(200) ? LV_FULL : 0) : LV_FULL) : LV_DIM;
    if (b == B_EDIT && ed.note_ed) return LV_FULL;
    if (b == B_ARP && ed.arp) return LV_FULL;
    if (b == B_SEL && ed.note_ed) return ed.nf_cells ? LV_FULL : LV_DIM;
    if (b == B_SEL && (ed.block || ed.floating || ed.tmode)) return LV_FULL;
    return LV_NONE;
}
static void mark(int e, int s)
{
    if (ed.note_ed) return;                             /* no blocks in the note editor */
    if (!ed.block) { ed.ar = ed.row; ed.at = ed.track; }
    if (e == K_SELECT) ed.row = ui_clamp(ed.row + s, 0, rows() - 1);
    else ed.track = ui_clamp(ed.track + sign(s), 0, tracks() - 1);
    scroll_x();
    block_of_anchor();
}
static void knob(int e, int s, int coarse)
{
    if (ed.note_ed) {                                   /* K3, K4: nothing here */
        if (e == K_SELECT || e == K_ALG) nf_move(e, s);
        else if (e == K_K1 || e == K_K2) nf_knob(s, e == K_K2);
        return;
    }
    if (ed.tmode && e == K_K1) { transpose(s * (coarse ? 12 : 1)); return; }
    if (ed.marking && (e == K_SELECT || e == K_ALG)) { mark(e, s); return; }     /* after SEL, MARK */
    if (e == K_SELECT || e == K_ALG) {
        ed.block = 0;
        if (e == K_SELECT) ed.row = ui_clamp(ed.row + s * (coarse ? 16 : 1), 0, rows() - 1);
        else ed.track = ui_clamp(ed.track + sign(s), 0, tracks() - 1);
        scroll_x();
        if (ed.floating) { ed.fl_row = ed.row; ed.fl_track = ed.track; }
        clamp_slot();
        return;
    }
    cell_knob(e, s, coarse);
}
/* the state's cursor (specs/connections.md): the pattern (FE its own), the cursor's row and track, bit 0 its own
 * pattern holds anything */
static void state_cursor(uint8_t *c)
{
    c[0] = pat();
    c[1] = (uint8_t)ed.row;
    c[2] = (uint8_t)ed.track;
    c[3] = (uint8_t)(ed_own_holds() ? 1 : 0);
}
static int env_to;
static void env_go(void) { go_to(env_to); }
static void env_pick(int n) { env_to = n; ed_own_ask(env_go); }
static int tap(int b)
{
    if (b == B_SEQ) {                                   /* 8, 4, 2, 1 tracks, round */
        ed.view = ed.view == 8 ? 4 : ed.view == 4 ? 2 : ed.view == 2 ? 1 : 8;
        clamp_cursor();
        ui_toast(seq());
        return 1;
    }
    if (b == B_EDIT) {
        if (ed.note_ed) { nf_close(1); return 1; }      /* keeping the changes */
        nf_open();
        return 1;
    }
    if (b == B_GLO) { props(); return 1; }
    if (b == B_ENV) { ed_pattern_list(T("PATTERNS"), pat(), env_pick, B_ENV, -1); return 1; }
    if (b == B_SEL && ed.note_ed) {                     /* SELECT, ALG: the fields, or the rows and tracks */
        ed.nf_cells = !ed.nf_cells;
        ui_toast(ed.nf_cells ? T("SELECT, ALG: CELLS") : T("SELECT, ALG: FIELDS"));
        return 1;
    }
    if (b == B_SEL) { block_menu(); return 1; }
    if (b == B_LFO) {
        rec_cell_t c;
        cell_at(ed.row, ed.track, &c);
        if (!empty(&c)) put(ed.row, ed.track, 0);
        else { memset(&c, 0, sizeof c); c.has = RC_NOTE; c.note = REC_NOTE_OFF; put(ed.row, ed.track, &c); }
        if (!ed.note_ed) advance(); else nf_clamp();
        clamp_slot();
        return 1;
    }
    if (b == B_ARP) {
        ed.arp = !ed.arp;
        if (ed.arp) {                                   /* (whatever plays stops first) */
            player_state_t st;
            ui_player_state(&st);
            if (ed.take) stop_recording();
            else if (st.playing) player_pause();
            ed.arp_t = ui_now(); ed.arp_step = 0;
        }
        else arp_off();
        const char *rh = T(UI_SETTINGS[S_ARP_RHYTHM].list[ui_set[S_ARP_RHYTHM]]);
        ui_toast(ed.arp ? ui_txt_v(T("ROW PREVIEW #"), rh) : T("ROW PREVIEW OFF"));
        return 1;
    }
    return 0;
}
static int hold(int b)
{
    if (b == B_PLAY) {                                  /* (PLAY's own hold here, REC on: what letting go and a key do) */
        if (ed.rec && !recording()) { ed.play_panel = 1; ui_dirty(); }
        return 1;
    }
    if (b != B_ENV) return 0;
    if (!ui_top()) take_inst(B_ENV);                    /* the pipette: a shortcut, ENV's tap stays the list */
    return 1;
}
static int sel_lines(const char **lines, int max)
{
    if (ed.note_ed || max < 2) return 0;
    lines[0] = T("SELECT, ALGORITHM: MARK A BLOCK");
    lines[1] = ed.tmode ? T("K1: AN OCTAVE") : T("K4: BY 16");
    return 2;
}
static void rec(void)
{
    if (ed.armed) { ed.armed = 0; ui_toast(T("TAKE CANCELLED")); return; }
    if (ed.take) { stop_recording(); return; }
    ed.rec = !ed.rec;
    player_state_t st;
    ui_player_state(&st);
    if (ed.rec) {                                       /* (the EDITOR's loop plays on: the keys write step by step) */
        if (st.playing && st.what == PL_SONG) { player_pause(); ui_toast(T("SONG PAUSED: WRITING")); }
        else ui_toast(T("REC ON: KEYS WRITE"));
    } else ui_toast(T("REC OFF: KEYS PLAY"));
}
static int play_down(void)
{
    player_state_t st;
    ui_player_state(&st);
    if (!ed.rec) {                                      /* the shown pattern loops from the cursor's row */
        if (st.playing && st.what == PL_LOOP && st.pat == pat()) player_pause();
        else if (st.playing && st.what == PL_SONG && st.pat == pat())
            player_play_pattern(pat(), (uint8_t)st.row);   /* (a song playing it hands over on the same row) */
        else player_play_pattern(pat(), (uint8_t)ed.row);
        return 2;
    }
    if (ed.armed) { ed.armed = 0; ui_toast(T("TAKE CANCELLED")); return 2; }
    if (ed.take) { stop_recording(); return 2; }
    if (st.playing && st.what == PL_LOOP) {
        player_pause();
        ui_toast(T("LOOP STOPPED: PLAY FOR A TAKE"));
        return 2;
    }
    if (st.playing) player_pause();
    return 1;
}
static void play_up(int used)
{
    ed.play_panel = 0;
    if (!used && ed.rec && !recording()) start_take(0);
}
static int key(int n, int down)
{
    if (ed.note_ed) {
        if (down) {                                     /* a key: the note, with the current instrument; the rest stays */
            int m = ui_midi(n);
            rec_cell_t c;
            nf_cell(&c);
            if (m >= 12 && m <= 127) {
                c.has |= RC_NOTE | RC_INS;
                c.note = (uint8_t)m;
                c.cart = (uint8_t)(ui_cur.inst >> 8);
                c.slot = (uint8_t)ui_cur.inst;
                put(ed.row, ed.track, &c);
            }
        }
        sound(n, down);
        return 1;
    }
    if (!ed.rec) {
        char nm[16];
        if (ed.fusion < 0 || !ui_inst_name((uint16_t)ed.fusion, nm)) return 0;
        int m = ui_midi(n);                             /* the FUSION as it is being written */
        if (m < 12 || m > 127) return 1;
        if (down) { pl_inst_t in = {(uint8_t)(ed.fusion >> 8), (uint8_t)ed.fusion}; player_key_on((uint8_t)m, in, (uint8_t)ui_set[S_KEYS_VOLUME]); }
        else player_key_off((uint8_t)m);
        return 1;
    }
    if (down) key_down(n); else key_up(n);
    return 1;
}
static int back(void)
{
    if (ed.armed) { ed.armed = 0; ui_toast(T("TAKE CANCELLED")); return 1; }
    if (ed.floating) { ed.floating = 0; ui_toast(T("CANCELLED")); return 1; }
    if (ed.tmode) { ed.tmode = 0; return 1; }
    if (ed.note_ed) { nf_close(0); return 1; }          /* HOME: dropping them */
    if (ed.block) { ed.block = 0; ed.marking = 0; return 1; }
    return 0;
}
static const char *footer(uint8_t *col)
{
    static char b[48];
    rec_cell_t c;
    int eng = engine_at(ed.row, ed.track);
    cell_at(ed.row, ed.track, &c);
    if (!(c.has & RC_CMD)) c.n = 0;
    *col = C_LGREY;
    if (ed.note_ed) {
        char nn[8], nm[16], rt[8];
        if (ed.nf_line == L_NOTE) { ui_note_name(nn, (c.has & RC_NOTE) ? c.note : -1); ui_fmt(b, sizeof b, T("NOTE %s"), nn); return b; }
        if (ed.nf_line == L_INST) {
            if (!(c.has & RC_INS)) return T("AS THE TRACK");
            ui_ref_text(rt, (uint16_t)(c.cart << 8 | c.slot));
            if (!ui_inst_name((uint16_t)(c.cart << 8 | c.slot), nm)) nm[0] = 0;
            ui_fmt(b, sizeof b, "%s %s", rt, nm);
            return b;
        }
        if (ed.nf_line == L_VOL) { ui_fmt(b, sizeof b, T("VOLUME %s"), (c.has & RC_VOL) ? ui_hex2(c.vol) : ".."); return b; }
        int k = ed.nf_line - L_CMD;
        if (k < c.n) {
            char a[4], w[40];
            cmd_text3(c.cmd[k], a);
            ui_cmd_words(c.cmd[k][0], c.cmd[k][1], eng, w, sizeof w);
            ui_fmt(b, sizeof b, "%s %s", a, w);
            return b;
        }
        *col = C_DGREY;
        ui_fmt(b, sizeof b, T("CMD %d EMPTY"), k + 1);
        return b;
    }
    if (ed.slot == 0) {
        ui_fmt(b, sizeof b, T("VOLUME %s"), (c.has & RC_VOL) ? ui_hex2(c.vol) : "..");
        if (!(c.has & RC_VOL)) *col = C_DGREY;
        return b;
    }
    if (ed.slot - 1 < c.n) {
        char a[4], w[40];
        cmd_text3(c.cmd[ed.slot - 1], a);
        ui_cmd_words(c.cmd[ed.slot - 1][0], c.cmd[ed.slot - 1][1], eng, w, sizeof w);
        ui_fmt(b, sizeof b, "%s %s", a, w);
        return b;
    }
    *col = C_DGREY;
    ui_fmt(b, sizeof b, T("CMD %d EMPTY"), ed.slot);
    return b;
}
static void tick(void)
{
    arp_tick();
    if (ed.take) {                                      /* the take goes round the pattern until PLAY or REC stops it */
        player_state_t st;
        ui_player_state(&st);
        int on = st.playing && st.what == PL_LOOP && st.pat == pat();
        if (!ed.take_seen) { if (on) ed.take_seen = 1; else return; }
        if (!on) {                                      /* (something else plays now: the take is over) */
            ed.take = 0;
            ed.row = ed.take_row;
            ui_toast(T("TAKE STOPPED"));
        } else ed.last_row = st.row;
        ui_dirty();
    }
}
static void draw(void)
{
    if (ed.note_ed) { draw_note_editor(); return; }
    int pr = play_row();
    if (recording() && !counting()) { int at = nearest_row(); if (at >= 0) ed.row = at; }
    int nr = V()->rows, follow = recording() && pr >= 0;
    ui_grid_t g;
    ui_grid_init(&g);
    ui_song_t s;
    g.view = ed.view;
    g.top = (follow ? pr : ed.row) / nr * nr;
    g.left = ed.left;
    g.cur_row = ed.row; g.cur_track = ed.track;
    g.play = pr;
    g.slot = ed.slot;
    if (ed.block) memcpy(g.block, ed.b, sizeof g.block);
    g.rpb = ui_song(ui_cur.song, &s) ? s.rpb : 4;
    g.fusion = ed_pattern_fusion(pat());
    g.over = 0;
    if (ed.floating && !rec_pattern(pat(), &fl_p)) {    /* the carried block's cells, read where it was */
        for (int k = 0; k < 16; k++) {
            int t = ed.fl_src[2] + k;
            rec_cur_set(&fl_cur[k], t <= ed.fl_src[3] && t < fl_p.ntrk ? fl_p.trk[t] : 0);
        }
        g.over = fl_over;
    }
    static uint8_t hot[32];
    if (ed.fusion >= 0) {                               /* the FUSION's rows the keys play */
        memset(hot, 0, sizeof hot);
        uint32_t keys = plat_notes();
        for (int n = 0; n < 27; n++) if (keys >> n & 1) { int m = ui_midi(n); if (m >= 0 && m < 256) hot[m >> 3] |= (uint8_t)(1u << (m & 7)); }
        g.hot = hot;
    }
    ui_grid(pat(), &g);
    if (ed.play_panel) {                                /* (specs/ui/screens/editor.md#recording) */
        ui_panel(4, BODY_Y + 150, 232, 40, 0, C_LGREY);
        g_run_t a[2] = {{T("LET GO"), C_YELLOW}, {T(": START THE TAKE"), C_LGREY}};
        g_run_t k[2] = {{T("PRESS A KEY"), C_YELLOW}, {T(": START ON THAT NOTE"), C_LGREY}};
        g_runs(10, BODY_Y + 156, &FONT_T, a, 2);
        g_runs(10, BODY_Y + 172, &FONT_T, k, 2);
    }
}

const ui_screen_t SCR_EDITOR = {
    .name = TX("EDITOR"), .group = G_MAKE,
    .does = BIT(B_EDIT) | BIT(B_SEL) | BIT(B_ENV) | BIT(B_GLO) | BIT(B_SEQ) | BIT(B_ARP) | BIT(B_LFO) | BIT(B_REC),
    .combines = BIT(B_ARP),
    .enter = enter, .leave = leave, .draw = draw, .sub = sub, .page = page, .seq = seq, .footer = footer,
    .knob = knob, .mark = mark, .tap = tap, .key = key, .back = back, .lit = lit, .sel_lines = sel_lines,
    .play_down = play_down, .play_up = play_up, .rec = rec, .hold = hold, .tick = tick, .armed = armed,
    .recording = recording, .own_arp = UI_ARP_PREVIEW, .cursor = state_cursor,
};

void ed_editor_goto(uint8_t p, int row, int track)
{
    ui_cur.pattern = p;
    if (ui_screen_now() == &SCR_EDITOR) { ed.row = row; ed.track = track; ed.left = 0; clamp_cursor(); clamp_slot(); ui_dirty(); return; }
    ed.want = 1;
    ed.want_row = row;
    ed.want_track = track;
    ed.want_fusion = -1;
}
void ed_editor_open(uint8_t p, int row, int track, int fusion_inst)
{
    ui_cur.pattern = p;
    ed.want = 1;
    ed.want_row = row;
    ed.want_track = track;
    ed.want_fusion = fusion_inst;
    ui_go_deep(&SCR_EDITOR, 0, 1);
}
