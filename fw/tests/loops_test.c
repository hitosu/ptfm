/* SPDX-License-Identifier: GPL-3.0-only
 * Copyright (C) 2026 Vasily Kazantsev */
/* LOOPS' loops (fw/core/play/loops.c, specs/files/loops.md) on the PC, against the store on a fake flash: the setup with
 * no record and its changes; new loops (colours, the next loops' mode, the next loop's mix, the first one's length);
 * cells at (row, lane) written, replaced and cleared, each stream's bytes as the editor page packs them (lane l of row r
 * at stream row 4r + l, gaps past 255 rows bridged with ED0), its 3,800 B refused past; peel (the streams after it
 * re-keyed, from the edit memory and from the flash), the selection; clear; mutes, solos, mixes; LENGTH shorter and
 * longer (bases and repeats cut, the question's answer), DOUBLE; the edit memory full (nothing changed); SAVE and a
 * power cycle (the loops back equal, in parts past 4 KB); REVERT; a record of version 3 read as none; the player's
 * source (its song, its clock, its tracks' instruments). */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "store_plat.h"
#include "../core/plat.h"
#include "../core/store/store.h"
#include "../core/play/rec.h"
#include "../core/play/loops.h"
#include "../core/play/player.h"

static int fails, checks;
#define CHECK(c, ...) do { checks++; if (!(c)) { fails++; printf("FAIL %s:%d: ", __FILE__, __LINE__); printf(__VA_ARGS__); printf("\n"); } } while (0)

/* the player, as loops.c tells it (seeding's two as well): what it was given */
static const pl_source_t *src_given;
static uint32_t mutes_given;
static int changed_n;
static uint8_t what_now = PL_NONE;
void player_all_sound_off(void) {}
void player_store_changed(void) {}
void player_play_source(uint8_t what, const pl_source_t *src, uint8_t pos, uint8_t row)
{
    (void)pos; (void)row;
    src_given = src;
    what_now = what;
}
void player_play_source_silent(const pl_source_t *src, uint8_t place) { (void)place; player_play_source(PL_LOOPS, src, 0, 0); }
void player_cue_loops(const pl_source_t *src) { src_given = src; }
void player_source_changed(int rows_changed) { (void)rows_changed; changed_n++; }
void player_lp_mutes(uint32_t muted, uint32_t soloed) { mutes_given = soloed & 0xFFFFu ? ~soloed & 0xFFFFu : muted; }
void player_state(player_state_t *s, int reset_max) { (void)reset_max; memset(s, 0, sizeof *s); s->what = what_now; }

/* a new song's FX setup, as the UI's: a chain of GATE, FILTER, DRIVE; BEATS A BAR 4 */
static void fx_new(uint8_t *s)
{
    memset(s, 0, 64);
    s[1] = 0x10;
    s[2] = 0xF2;
    s[11] = 4;
    s[20] = 0x5A;
}
static int edits_saved(void)
{
    int r = store_edit_save(), st;
    if (r) return r;
    while ((st = store_save_status(0)) == STORE_BUSY) store_poll();
    return st;
}
static rec_cell_t note(int n, int vol)
{
    rec_cell_t c;
    memset(&c, 0, sizeof c);
    c.has = RC_NOTE | (vol >= 0 ? RC_VOL : 0);
    c.note = (uint8_t)n;
    c.vol = (uint8_t)(vol < 0 ? 0 : vol);
    return c;
}
static rec_cell_t cmds(int n, int k)    /* a note with k commands */
{
    rec_cell_t c = note(n, 0x40);
    c.has |= RC_CMD;
    c.n = (uint8_t)k;
    for (int i = 0; i < k; i++) { c.cmd[i][0] = (uint8_t)(i ? 0x14 : 0x0E); c.cmd[i][1] = (uint8_t)(i ? 3 + i : 0xD2); }
    return c;
}
static uint32_t slen(int i, const uint8_t **s)          /* loop i's stream: its bytes with its 0x00 (1 empty) */
{
    const uint8_t *end;
    *s = loops_stream(i, &end);
    if (!*s) return 1;
    uint32_t n = 0;
    rec_cur_t cu;
    rec_cell_t c;
    rec_cur_set_end(&cu, *s, end);
    while (cu.row != 0x7FFF) rec_cur_at(&cu, cu.row, &c);
    n = (uint32_t)(cu.p - *s) + 1u;
    return n;
}
static int stream_is(int i, const uint8_t *want, uint32_t n)
{
    const uint8_t *s;
    uint32_t l = slen(i, &s);
    if (l != n || (s && memcmp(s, want, n))) {
        printf("  loop %d's stream (%u B):", i, l);
        for (uint32_t k = 0; s && k < l; k++) printf(" %02X", s[k]);
        printf("\n");
        return 0;
    }
    return 1;
}
/* everything LOOPS keeps, to compare */
typedef struct {
    uint8_t setup[80], n, sel, rows;
    struct { uint16_t inst; uint8_t colour, mono, muted, soloed, lanes, base, repeats, mix[6]; uint32_t len; uint8_t s[3800]; } l[16];
} snap_t;
static snap_t sa, sb;
static void snap(snap_t *p)
{
    memset(p, 0, sizeof *p);
    p->n = (uint8_t)loops_n(); p->sel = (uint8_t)loops_sel(); p->rows = (uint8_t)loops_rows();
    p->setup[0] = (uint8_t)loops_tempo(); p->setup[1] = (uint8_t)loops_ticks(); p->setup[2] = (uint8_t)loops_swing();
    p->setup[3] = (uint8_t)loops_grid(); p->setup[4] = (uint8_t)loops_quant(); p->setup[5] = (uint8_t)loops_legato();
    p->setup[6] = (uint8_t)loops_countin(); p->setup[7] = (uint8_t)loops_next_mono();
    memcpy(p->setup + 8, loops_fx(), 64);
    memcpy(p->setup + 72, loops_mix(p->n), 6);
    for (int i = 0; i < p->n; i++) {
        const uint8_t *s;
        p->l[i].inst = loops_inst(i); p->l[i].colour = (uint8_t)loops_colour(i); p->l[i].mono = (uint8_t)loops_mono(i);
        p->l[i].muted = (uint8_t)loops_muted(i); p->l[i].soloed = (uint8_t)loops_soloed(i); p->l[i].lanes = (uint8_t)loops_lanes(i);
        p->l[i].base = (uint8_t)loops_base(i); p->l[i].repeats = (uint8_t)loops_repeats(i);
        memcpy(p->l[i].mix, loops_mix(i), 6);
        p->l[i].len = slen(i, &s);
        if (s) memcpy(p->l[i].s, s, p->l[i].len);
    }
}
static void fresh(void)                 /* an empty flash, no edits */
{
    fake_reset();
    store_edit_revert();
    store_init();
    loops_revert();
}

static void t_setup(void)
{
    fresh();
    uint8_t def[64];
    fx_new(def);
    CHECK(loops_n() == 0 && loops_rows() == 0 && loops_sel() == 0 && loops_tempo() == 120 && loops_ticks() == 6 &&
          loops_swing() == 50 && loops_grid() == 0 && loops_quant() == LQ_ROW && loops_countin() == 0 && !loops_legato() &&
          !loops_next_mono(), "no record: TEMPO 120, 6 ticks, SWING 50, 1/16, ROW, COUNT-IN OFF, GLIDE, POLY, no loops");
    CHECK(!memcmp(loops_fx(), def, 64), "no record: the FX setup a new song's");
    CHECK(loops_mix(0)[0] == 0x7F && loops_mix(0)[1] == 0x80 && !loops_mix(0)[2] && !loops_mix(0)[5], "no record: the next loop's mix plain");
    CHECK(loops_bar_rows() == 16 && loops_max_bars() == 8 && loops_max_rows() == 128 && loops_bpm() == 120,
          "1/16 at 4 beats: 16 rows a bar, 8 bars, 128 rows, 120 BPM");
    CHECK(!store_edit_count(), "reading it is no edit");
    CHECK(loops_set_tempo(130) == LP_OK && loops_tempo() == 130 && loops_set_swing(80) == LP_OK && loops_swing() == 75 &&
          loops_set_quant(LQ_REAL) == LP_OK && loops_quant() == LQ_REAL && loops_set_legato(1) == LP_OK && loops_legato() &&
          loops_set_countin(2) == LP_OK && loops_countin() == 2 && loops_set_next_mono(1) == LP_OK && loops_next_mono() &&
          loops_set_grid(1) == LP_OK && loops_grid() == 1 && loops_bar_rows() == 24 && loops_max_bars() == 5 && loops_max_rows() == 120,
          "the setup changed (SWING held to 75; 1/16T: 24 rows a bar, 5 bars)");
    CHECK(store_edit_count() == 1 && store_piece(REC_LOOPS, 0, STORE_PC_HEAD, 0) && !store_piece(REC_LOOPS, 0, STORE_PC_PHEAD, 0),
          "the head alone an edit (no loops: no streams' head)");
    uint32_t len;
    const uint8_t *h = store_piece(REC_LOOPS, 0, STORE_PC_HEAD, &len);
    CHECK(len == 80 && h[0] == 130 && h[2] == (6 | 2 << 5 | 0x80) && h[3] == 75 && h[4] == (1 | 4 << 2 | 0x20) && !h[5] && !h[7],
          "its bytes: TEMPO, TICKS | COUNT-IN | JUMP, SWING, the grid | QUANTIZE | MONO");
    CHECK(loops_set_legato(LG_LEGATO) == LP_OK && loops_legato() == LG_LEGATO &&
          (h = store_piece(REC_LOOPS, 0, STORE_PC_HEAD, &len)) && h[2] == (6 | 2 << 5) && h[4] == (1 | 4 << 2 | 0x20 | 0x40),
          "MONO LEGATO LEGATO: byte 4's bit 6, byte 2's bit 7 0");
    CHECK(loops_set_legato(LG_JUMP) == LP_OK && loops_legato() == LG_JUMP &&
          (h = store_piece(REC_LOOPS, 0, STORE_PC_HEAD, &len)) && h[2] == (6 | 2 << 5 | 0x80) && h[4] == (1 | 4 << 2 | 0x20),
          "... back to JUMP: byte 4's bit 6 0 again");
    loops_set_grid(0);
    loops_set_next_mono(0);
    CHECK(loops_set_quant(9) == LP_OK && loops_quant() == LQ_REAL, "QUANTIZE held to REAL TIME");
}

static void t_new_and_cells(void)
{
    fresh();
    loops_set_mix(0, RM_PAN, 0x40);                     /* (the next loop's) */
    int a = loops_new(0xF1, 3);
    CHECK(a == 0 && loops_n() == 1 && loops_rows() == 128 && loops_base(0) == 128 && loops_repeats(0) == 1 &&
          loops_lanes(0) == 1 && loops_colour(0) == 0 && !loops_mono(0) && loops_inst(0) == 0xF103 && loops_mix(0)[1] == 0x40,
          "the first loop: the length the most rows, its base too, once, 1 lane, light red, POLY, the next loop's mix");
    CHECK(store_piece(REC_LOOPS, 0, STORE_PC_PHEAD, 0) && store_piece(REC_LOOPS, 0, STORE_PC_PHEAD, 0)[0] == 1 &&
          store_piece(REC_LOOPS, 0, STORE_PC_PHEAD, 0)[1] == 127, "the streams' head: 1 loop, 128 rows");
    loops_set_rows(64);
    loops_set_next_mono(1);
    int b = loops_new(0xF2, 0);
    CHECK(b == 1 && loops_base(1) == 64 && loops_colour(1) == 1 && loops_mono(1), "the next: its base the length, the next colour, MONO");
    loops_set_next_mono(0);
    /* cells: (row 0, lane 0), (0, 2), (5, 1) */
    rec_cell_t c = note(60, 0x7F), d;
    CHECK(loops_cell_put(0, 0, 0, &c) == LP_OK && (c = note(64, 0x50), loops_cell_put(0, 0, 2, &c)) == LP_OK &&
          (c = note(67, -1), loops_cell_put(0, 5, 1, &c)) == LP_OK, "three cells written");
    static const uint8_t S1[] = {0x90, 60, 0x7F, 0x91, 64, 0x50, 0x87, 67, 18, 0};   /* rows 0, 2, 21 */
    CHECK(stream_is(0, S1, sizeof S1), "the stream: lane 2 of row 0 at stream row 2, lane 1 of row 5 at 21 (a long skip)");
    CHECK(loops_lanes(0) == 3, "its lanes: the most used (3)");
    CHECK(loops_cell(0, 0, 2, &d) && d.note == 64 && d.vol == 0x50 && loops_cell(0, 5, 1, &d) && d.note == 67 && !(d.has & RC_VOL) &&
          !loops_cell(0, 5, 0, &d) && !loops_cell(0, 0, 1, &d), "read back at their rows and lanes");
    c = note(62, 0x7F);
    CHECK(loops_cell_put(0, 0, 2, &c) == LP_OK && loops_cell(0, 0, 2, &d) && d.note == 62, "a cell replaced");
    CHECK(loops_cell_put(0, 0, 2, 0) == LP_OK && !loops_cell(0, 0, 2, &d) && loops_lanes(0) == 2, "a cell cleared: 2 lanes left");
    CHECK(loops_note_count(0) == 2 && loops_has_notes(0) && !loops_has_notes(1), "its notes counted");
    c = note(REC_NOTE_OFF, -1);
    CHECK(loops_cell_put(0, 9, 0, &c) == LP_OK && loops_note_count(0) == 2, "a note off is no note");
    CHECK(loops_cell_put(1, 64, 0, &c) == LP_NO && loops_cell_put(0, 3, 4, &c) == LP_NO && loops_cell_put(5, 0, 0, &c) == LP_NO,
          "past its base, its lanes, the loops: refused");
    /* gaps past 255 stream rows: (0, 0) and (127, 3) are stream rows 0 and 511 (a first loop of 128 rows) */
    fresh();
    loops_new(0xF1, 3);
    c = note(48, 0x7F);
    loops_cell_put(0, 0, 0, &c);
    c = note(50, 0x7F);
    loops_cell_put(0, 127, 3, &c);
    static const uint8_t S2[] = {0x90, 48, 0x7F, 0x0F, 0x0E, 0xD0, 0xFF, 0x97, 50, 0x7F, 0xFE, 0};
    CHECK(stream_is(0, S2, sizeof S2), "a gap of 510 rows: an ED0 alone 256 rows on (a skip of 255), then a skip of 254");
    CHECK(loops_note_count(0) == 2 && !loops_cell(0, 64, 0, &d), "the bridge is no cell");
    c = note(52, 0x7F);
    loops_cell_put(0, 50, 0, &c);                       /* (stream row 200: the gap after it 310 rows) */
    static const uint8_t S4[] = {0x90, 48, 0x7F, 0x97, 52, 0x7F, 199, 0x0F, 0x0E, 0xD0, 0xFF, 0x97, 50, 0x7F, 54, 0};
    CHECK(stream_is(0, S4, sizeof S4), "a cell at stream row 200: the bridge 256 rows after it, as the packer lays it");
    loops_stream_clear(0);
    c = note(55, 0x7F);
    loops_cell_put(0, 75, 0, &c);                       /* (the first cell at stream row 300: bridged from the start) */
    static const uint8_t S5[] = {0x0F, 0x0E, 0xD0, 0xFF, 0x97, 55, 0x7F, 44, 0};
    CHECK(stream_is(0, S5, sizeof S5), "a first cell at stream row 300: a bridge at 255, then a skip of 44");
    /* the 3,800 B cap: notes with 4 commands (13 B) until the stream is full */
    loops_stream_clear(0);
    int r = LP_OK, k;
    for (k = 0; k < 512 && r == LP_OK; k++) { c = cmds(40 + k % 60, 4); r = loops_cell_put(0, k / 4, k % 4, &c); }
    const uint8_t *s;
    uint32_t l = slen(0, &s);
    CHECK(r == LP_FULL && l <= STORE_LOOP_STREAM_MAX && l + 13 > STORE_LOOP_STREAM_MAX && loops_note_count(0) == k - 1,
          "a stream full at %u B (%d cells): the next cell refused, the stream as it was", l, k - 1);
    rec_cell_t e;
    CHECK(loops_cell(0, 3, 1, &e) && e.n == 4 && e.cmd[0][0] == 0x0E && e.cmd[3][0] == 0x14 && e.cmd[3][1] == 6, "4 commands read back");
}

static void t_peel(void)
{
    fresh();
    rec_cell_t c;
    for (int i = 0; i < 4; i++) {
        loops_new((uint8_t)(0xF0 + i), (uint8_t)i);
        if (!i) loops_set_rows(32);
        c = note(40 + i, 0x7F);
        loops_cell_put(i, i, i % 4, &c);
        loops_set_mix(i, RM_LEVEL, 0x10 + i);
    }
    CHECK(loops_n() == 4 && loops_colour(3) == 3, "four loops");
    CHECK(edits_saved() == STORE_OK, "saved (their streams in the flash now)");
    snap(&sa);
    loops_set_sel(1);
    CHECK(loops_peel(1) == LP_OK && loops_n() == 3, "loop 01 peeled (from the flash)");
    snap(&sb);
    int ok = 1;
    for (int i = 1; i < 3; i++)
        ok &= sb.l[i].inst == sa.l[i + 1].inst && sb.l[i].colour == sa.l[i + 1].colour && sb.l[i].mix[0] == sa.l[i + 1].mix[0] &&
              sb.l[i].len == sa.l[i + 1].len && !memcmp(sb.l[i].s, sa.l[i + 1].s, sb.l[i].len);
    CHECK(ok && sb.l[0].inst == sa.l[0].inst && !memcmp(sb.l[0].s, sa.l[0].s, sa.l[0].len),
          "the loops after it moved up, each with its instrument, colour, mix and stream");
    CHECK(loops_sel() == 1 && loops_inst(1) == 0xF202, "the selection where it was: the loop that moved into its place");
    CHECK(edits_saved() == STORE_OK && (store_init(), snap(&sa), !memcmp(&sa, &sb, sizeof sa)), "saved, a power cycle: the same");
    loops_set_sel(3);                                   /* (+ NEW LOOP) */
    CHECK(loops_peel(2) == LP_OK && loops_n() == 2 && loops_sel() == 2, "the last peeled from + NEW LOOP: + NEW LOOP still");
    c = note(70, 0x7F);
    loops_cell_put(1, 7, 0, &c);
    CHECK(loops_peel(0) == LP_OK && loops_n() == 1 && loops_cell(0, 7, 0, &c) && c.note == 70 && loops_sel() == 1,
          "peeled from the edit memory: its edited stream moved too");
    CHECK(loops_peel(0) == LP_OK && !loops_n() && !loops_rows() && loops_sel() == 0, "the only loop peeled: the length with it");
    CHECK(loops_peel(0) == LP_NO, "nothing to peel");
    CHECK(loops_new(0xF0, 0) == 0 && loops_rows() == 128 && !loops_stream(0, 0) && !loops_has_notes(0),
          "a new first loop: empty (the flash's stream at its number left out)");
}

static void t_clear_mutes(void)
{
    fresh();
    rec_cell_t c = note(60, 0x7F);
    for (int i = 0; i < 3; i++) { loops_new(0xF0, (uint8_t)i); if (!i) loops_set_rows(16); loops_cell_put(i, 0, 0, &c); }
    loops_set_tempo(140);
    CHECK(loops_set_muted(1, 1) == LP_OK && loops_muted(1) && loops_muted_mask() == 2 && loops_silent_mask() == 2, "loop 01 muted");
    CHECK(loops_set_soloed(2, 1) == LP_OK && loops_soloed_mask() == 4 && loops_silent_mask() == 3 && loops_muted(1),
          "loop 02 soloed: the others silent, the mute kept beneath");
    CHECK(loops_set_mutes(0, 0) == LP_OK && !loops_silent_mask(), "all back on");
    CHECK(loops_set_inst(1, 0x0A, 0x05) == LP_OK && loops_inst(1) == 0x0A05, "APPLY INSTRUMENT");
    CHECK(loops_set_mix(2, RM_REVERB, 0x20) == LP_OK && loops_mix(2)[RM_REVERB] == 0x20 && loops_mix(1)[RM_REVERB] == 0, "a loop's mix");
    loops_play(0);
    CHECK(src_given && what_now == PL_LOOPS && mutes_given == 0, "played: the source given, no loop silent");
    loops_set_muted(0, 1);
    int ch = changed_n;
    loops_changed(0);
    CHECK(changed_n == ch + 1 && mutes_given == 1, "a change told while they play (the mutes with it)");
    CHECK(loops_clear() == LP_OK && !loops_n() && !loops_rows() && loops_tempo() == 140 && !loops_stream(0, 0),
          "CLEAR LOOPS: the loops and the length go, the setup stays");
    CHECK(!store_piece(REC_LOOPS, 0, STORE_PC_PHEAD, 0) && !store_piece(REC_LOOPS, 0, 0, 0), "no streams' head, no streams kept");
    what_now = PL_NONE;
}

static void base_reps(int i, int base, int reps)       /* loop i's base and repeats, written into the head as is */
{
    uint32_t len;
    uint8_t h[LP_HEAD_MAX];
    const uint8_t *p = store_piece(REC_LOOPS, 0, STORE_PC_HEAD, &len);
    memcpy(h, p, len);
    h[80 + 12 * i + 4] = (uint8_t)(base - 1);
    h[80 + 12 * i + 5] = (uint8_t)reps;
    store_edit_piece(REC_LOOPS, 0, STORE_PC_HEAD, 4, h, len);
}
static void t_length_double(void)
{
    fresh();
    rec_cell_t c;
    loops_new(0xF0, 0);                                 /* loop 00: a base of 64, once */
    loops_set_rows(64);
    c = note(36, 0x7F); loops_cell_put(0, 0, 0, &c);
    c = note(38, 0x7F); loops_cell_put(0, 40, 1, &c);
    loops_new(0xF0, 1);                                 /* loop 01: a base of 16, 4 times */
    c = note(42, 0x7F); loops_cell_put(1, 4, 0, &c);
    CHECK(loops_set_rows(32) == LP_OK, "(32 rows first)");
    CHECK(loops_base(0) == 32 && loops_repeats(0) == 1 && !loops_cell(0, 40, 1, &c) && loops_lanes(0) == 1 && loops_note_count(0) == 1,
          "LENGTH shorter: a base past it cut (its cells past it dropped, 1 lane left)");
    CHECK(loops_set_rows(64) == LP_OK && loops_base(0) == 32 && loops_repeats(0) == 1, "longer: silence at the end");
    /* loop 01: base 16 x 4 over 64 */
    loops_stream_clear(1);
    c = note(42, 0x7F); loops_cell_put(1, 4, 0, &c);
    base_reps(1, 16, 4);
    CHECK(loops_base(1) == 16 && loops_repeats(1) == 4, "loop 01: 16 rows, 4 times");
    CHECK(loops_cut_drops(48) == 1 && loops_cut_drops(53) == 0, "cut to 48: loop 01's note at 52 would go; to 53 none");
    CHECK(loops_set_rows(48) == LP_OK && loops_repeats(1) == 3 && loops_base(1) == 16, "cut to 48: 3 repeats left");
    CHECK(loops_set_rows(40) == LP_OK && loops_repeats(1) == 3, "cut to 40: the third repeat cut there, still 3");
    CHECK(loops_set_rows(48) == LP_OK && loops_repeats(1) == 3, "longer: the repeats as they were");
    /* DOUBLE: loop 01 fills 48 (16 x 3): x 6; loop 00 (32 of 48) written out to 48, then twice */
    CHECK(loops_double() == LP_OK && loops_rows() == 96, "DOUBLE: 96 rows");
    CHECK(loops_base(1) == 16 && loops_repeats(1) == 6, "a loop that fills the length: its repeats doubled");
    CHECK(loops_base(0) == 48 && loops_repeats(0) == 2 && loops_note_count(0) == 1 && loops_cell(0, 0, 0, &c) && c.note == 36,
          "one that rests: written out to the old length, then twice");
    loops_set_rows(96);
    loops_set_rows(48);                                 /* loop 00 as 16 x 4 over 48 (cut): written out with the cut */
    loops_stream_clear(0);
    base_reps(0, 16, 4);
    c = note(36, 0x7F); loops_cell_put(0, 1, 0, &c);
    CHECK(loops_base(0) == 16 && loops_repeats(0) == 4 && loops_double() == LP_OK && loops_base(0) == 48 && loops_repeats(0) == 2 &&
          loops_note_count(0) == 3, "16 x 4 cut at 48: written out as 3 repeats (48 rows), then twice");
    CHECK(loops_double() == LP_NO && loops_rows() == 96, "past 128 rows: refused");
    CHECK(loops_set_rows(129) == LP_NO && loops_set_rows(0) == LP_NO && loops_rows() == 96, "a length past 1-128: refused");
    {                                                   /* a head of 200 rows (written so elsewhere): read as 128 */
        uint32_t len;
        uint8_t h[LP_HEAD_MAX];
        const uint8_t *p = store_piece(REC_LOOPS, 0, STORE_PC_HEAD, &len);
        memcpy(h, p, len);
        h[7] = 200;
        store_edit_piece(REC_LOOPS, 0, STORE_PC_HEAD, 4, h, len);
    }
    CHECK(loops_rows() == 128, "a length of 200 rows read as 128");
}

static void t_full(void)
{
    fresh();
    rec_cell_t c = note(60, 0x7F);
    loops_new(0xF0, 0);
    loops_set_rows(64);
    static uint8_t junk[4000];
    static const uint32_t SIZES[4] = {3000, 300, 30, 4};
    int k = 0;
    for (int z = 0; z < 4; z++)                         /* (songs as big as fit, then smaller, to the last bytes) */
        while (k < 250 && store_edit_put(REC_SONG, (uint8_t)k, 0, 1, junk, SIZES[z]) == STORE_OK) memset(junk, ++k, sizeof junk);
    CHECK(store_edit_room() < 12, "the edit memory full (%u B left)", store_edit_room());
    snap(&sa);
    CHECK(loops_new(0xF0, 1) == LP_FULL, "the edit memory full: a new loop refused");
    snap(&sb);
    CHECK(!memcmp(&sa, &sb, sizeof sa), "... nothing changed");
    CHECK(loops_cell_put(0, 0, 0, &c) == LP_FULL, "a cell refused");
    snap(&sb);
    CHECK(!memcmp(&sa, &sb, sizeof sa), "... nothing changed");
}

static void t_save_parts(void)
{
    fresh();
    rec_cell_t c;
    for (int i = 0; i < 16; i++) {                      /* 16 loops of 600 B or so: past 4 KB, saved in parts */
        CHECK(loops_new((uint8_t)i, (uint8_t)(i * 3)) == i, "loop %d made", i);
        if (!i) loops_set_rows(128);
        for (int r = 0; r < 100; r++) { c = cmds(30 + (r * 7 + i) % 80, 1 + (r + i) % 2); loops_cell_put(i, r, (r + i) % 4, &c); }
        loops_set_mix(i, RM_PAN, i * 16);
    }
    loops_set_muted(3, 1);
    loops_set_soloed(5, 1);
    loops_set_sel(7);
    loops_keep_sel();
    snap(&sa);
    CHECK(edits_saved() == STORE_OK && !store_edit_count(), "16 loops saved");
    CHECK(store_get(REC_LOOPS, 0, 1, 0, 0) != 0, "in parts (part 1 there)");
    store_init();
    loops_revert();
    snap(&sb);
    CHECK(!memcmp(&sa, &sb, sizeof sa), "a power cycle: every loop, its stream, the selection, the setup back equal");
    CHECK(loops_new(0, 0) == LP_NO, "16 loops: no more");
    /* a stream changed in a part past the first: saved there */
    c = note(99, 0x11);
    loops_cell_put(15, 127, 3, &c);
    snap(&sa);
    CHECK(edits_saved() == STORE_OK && (store_init(), snap(&sb), !memcmp(&sa, &sb, sizeof sa)), "loop 0F changed: saved, back equal");
    /* the source over it */
    rec_song_t s;
    rec_pat_t pt;
    pl_inst_t in;
    loops_cue();
    CHECK(src_given && !src_given->song(&s) && s.tempo == 120 && s.rpb == 4 && s.mix[2][RM_PAN] == 32 && s.mix[15][RM_PAN] == 240 &&
          s.fx && s.fx[11] == 4 && s.len == 1 && !s.order[0], "the source's song: the tempo, each loop's mix as its track's, the FX setup");
    CHECK(!src_given->pattern(0, &pt) && pt.rows == 128 && pt.ntrk == 16 && !pt.trk[0] && !pt.trk[15] && src_given->pattern(1, &pt),
          "its pattern: the clock, the length's rows, no streams");
    CHECK(src_given->track_inst(4, &in) == 1 && in.cart == 4 && in.slot == 12 && src_given->track_inst(16, &in) == 0,
          "each track's instrument its loop's");
    rec_loops_t l;
    rec_loop_t e;
    CHECK(!rec_loops(&l, 1) && l.n == 16 && l.rows == 128 && l.trk[9] && l.trk_end[9] > l.trk[9], "rec_loops: the 16 streams");
    rec_loop(&l, 5, &e);
    CHECK(loops_soloed(5) && !(l.head[REC_LOOPS_TABLE + REC_LOOPS_ENTRY * 5 + 2] & 0x40) && !e.muted && e.base == 128 &&
          e.repeats == 1 && e.colour == 5 && e.mix[RM_PAN] == 80, "rec_loop: loop 05's entry (its solo RAM's alone: bit 6 0)");
}

static void t_revert_v3(void)
{
    fresh();
    rec_cell_t c = note(60, 0x7F);
    loops_new(0xF0, 0);
    loops_set_rows(16);
    loops_cell_put(0, 0, 0, &c);
    edits_saved();
    loops_set_sel(1);
    CHECK(loops_sel() == 1 && !store_edit_count(), "the selection moved: no edit");
    CHECK(loops_keep_sel() == LP_OK && !store_edit_count(), "... nor at SAVE while LOOPS has no change");
    loops_new(0xF0, 1);
    loops_cell_put(1, 3, 0, &c);
    loops_set_tempo(90);
    CHECK(loops_n() == 2 && store_edit_count() == 1, "a loop more, unsaved");
    CHECK(loops_revert() == LP_OK && loops_n() == 1 && loops_tempo() == 120 && !store_edit_count() && loops_sel() == 0,
          "REVERT: as saved (the selection too)");
    /* version 3 in the flash: none here, kept till LOOPS is saved */
    fake_reset();
    store_init();
    static uint8_t v3[200];
    memset(v3, 0, sizeof v3);
    v3[0] = 99; v3[2] = 6; v3[5] = 1; v3[72] = 0xF0;
    v3[82] = 16; v3[83] = 15; v3[84] = 1; v3[85] = 1; v3[86] = 0; v3[87] = 0x80; v3[88] = 60; v3[89] = 0;
    store_save_begin();
    store_save_put(REC_LOOPS, 0, 0, 3, v3, 90);
    store_save_run();
    loops_revert();
    uint8_t ver;
    CHECK(loops_n() == 0 && loops_tempo() == 120 && store_get(REC_LOOPS, 0, 0, 0, &ver) && ver == 3, "version 3: read as none, kept");
    CHECK(store_edit_put(REC_LOOPS, 0, 0, 3, v3, 90) != STORE_OK, "an edit of version 3 refused");
    loops_new(0xF3, 0);
    CHECK(edits_saved() == STORE_OK && store_get(REC_LOOPS, 0, 0, 0, &ver) && ver == 4 && loops_n() == 1 && loops_inst(0) == 0xF300,
          "a loop made and saved: version 4 in its place");
}

/* ---- SAVE TO PATTERN, LOAD FROM PATTERN ---- */
typedef struct { uint8_t row, lane, note, vol; uint8_t c0, v0; } tc_t;   /* a cell (note 0, vol 0: none; c0: a command) */
static rec_cell_t cell_of(const tc_t *t)
{
    rec_cell_t c = note(t->note, t->vol ? t->vol : -1);
    if (t->note == REC_NOTE_OFF) c.has = RC_NOTE;
    if (!t->note) c.has &= (uint8_t)~RC_NOTE;
    if (t->c0) { c.has |= RC_CMD; c.n = 1; c.cmd[0][0] = t->c0; c.cmd[0][1] = t->v0; }
    return c;
}
/* a loop more of instrument in: base rows played reps times (the first: the length len), its cells in stream order */
static int mk_loop(uint16_t in, int len, int base, int reps, const tc_t *t, int n)
{
    static uint8_t b[3800];
    lp_enc_t e;
    int lanes = 1, i = loops_new((uint8_t)(in >> 8), (uint8_t)in);
    if (i < 0) return -1;
    if (!i) loops_set_rows(len);
    loops_enc_start(&e, b, sizeof b);
    for (int k = 0; k < n; k++) {
        rec_cell_t c = cell_of(&t[k]);
        loops_enc_cell(&e, 4 * t[k].row + t[k].lane, &c);
        if (t[k].lane + 1 > lanes) lanes = t[k].lane + 1;
    }
    return loops_put(i, base, reps, 0, b, loops_enc_end(&e), lanes) == LP_OK ? i : -1;
}
/* pattern num made whole (an edit): ntrk x rows, its cells (lane: the track) in each track's rows' order, a note naming
 * its instrument (vol's place: the instrument's slot; c0 0xFE: the instrument left to the track) */
static void mk_pat(uint8_t num, int ntrk, int rows, const tc_t *t, int n, uint8_t cart)
{
    static uint8_t b[4096];
    uint32_t at = 5;
    uint16_t bits = 0;
    for (int k = 0; k < ntrk; k++) {
        lp_enc_t e;
        int any = 0;
        loops_enc_track(&e, b + at, sizeof b - at);
        for (int j = 0; j < n; j++) {
            if (t[j].lane != k) continue;
            tc_t x = t[j];
            rec_cell_t c;
            x.vol = 0;
            if (x.c0 == 0xFE) x.c0 = 0;
            c = cell_of(&x);
            if (t[j].note && t[j].note != REC_NOTE_OFF && t[j].c0 != 0xFE) { c.has |= RC_INS; c.cart = cart; c.slot = t[j].vol; }
            loops_enc_cell(&e, t[j].row, &c);
            any = 1;
        }
        if (!any) continue;
        at += loops_enc_end(&e);
        bits |= (uint16_t)(1u << k);
    }
    b[0] = (uint8_t)ntrk; b[1] = (uint8_t)(rows - 1); b[2] = 1; b[3] = (uint8_t)bits; b[4] = (uint8_t)(bits >> 8);
    store_edit_put(REC_PATTERN, num, 0, 1, b, at);
}
static int pat_cell(uint8_t num, int row, int trk, rec_cell_t *c)   /* 1 a cell there */
{
    rec_pat_t p;
    rec_cur_t cu;
    if (rec_pattern(num, &p) || trk >= p.ntrk) return 0;
    rec_cur_set_end(&cu, p.trk[trk], p.trk_end[trk]);
    return rec_cur_at(&cu, row, c);
}
static int pat_cmd(const rec_cell_t *c, int code)
{
    for (int k = 0; k < c->n; k++) if (c->cmd[k][0] == code) return c->cmd[k][1];
    return -1;
}
/* loop i's notes as `row:lane:note` joined by commas (offs as `row:lane:off`) */
static void notes_of(int i, char *out, int max)
{
    rec_cell_t c;
    out[0] = 0;
    for (int r = 0; r < loops_base(i); r++)
        for (int l = 0; l < 4; l++)
            if (loops_cell(i, r, l, &c) && (c.has & RC_NOTE)) {
                int n = (int)strlen(out);
                if (c.note == REC_NOTE_OFF) snprintf(out + n, (size_t)(max - n), "%s%d:%d:off", n ? "," : "", r, l);
                else snprintf(out + n, (size_t)(max - n), "%s%d:%d:%d", n ? "," : "", r, l, c.note);
            }
}
static void t_store_load(void)
{
    char got[400];
    int left;
    rec_cell_t c;
    rec_pat_t p;
    fresh();
    /* loop 0: a note and its off over 4 rows, x4; loop 1: two lanes over 16 rows, its LEVEL 40 */
    static const tc_t L0[2] = {{0, 0, 60, 100, 0, 0}, {2, 0, REC_NOTE_OFF, 0, 0, 0}};
    static const tc_t L1[5] = {{0, 0, 64, 100, 0, 0}, {0, 1, 67, 100, 0, 0}, {8, 0, REC_NOTE_OFF, 0, 0, 0}, {8, 1, REC_NOTE_OFF, 0, 0, 0},
                               {12, 0, 65, 100, 0, 0}};
    mk_loop(0xF000, 16, 4, 4, L0, 2);
    mk_loop(0xF001, 16, 16, 1, L1, 5);
    loops_set_mix(1, RM_LEVEL, 0x40);
    CHECK(loops_store(0x10, &left) == LP_OK && !left && !rec_pattern(0x10, &p) && p.rows == 16 && p.ntrk == 3 && p.step == 1,
          "SAVE TO PATTERN: a pattern of the loops' length, a lane a track where they overlap (3 tracks)");
    int rep = 1;
    for (int r = 0; r < 16; r += 4)
        rep &= pat_cell(0x10, r, 0, &c) && c.note == 60 && (c.has & RC_INS) && c.cart == 0xF0 && !c.slot && c.vol == 100 &&
               pat_cell(0x10, r + 2, 0, &c) && c.note == REC_NOTE_OFF;
    CHECK(rep, "... the repeats written out, each note naming its instrument");
    CHECK(pat_cell(0x10, 0, 1, &c) && c.note == 64 && c.cart == 0xF0 && c.slot == 1 && pat_cmd(&c, 0x80) == 0x40 && c.n == 1 &&
          pat_cell(0x10, 0, 2, &c) && c.note == 67 && pat_cmd(&c, 0x80) == 0x40 && pat_cell(0x10, 12, 1, &c) && !c.n &&
          pat_cell(0x10, 0, 0, &c) && !c.n, "... a loop's mix as commands with its first note on a track (=80 40), none where "
          "it is plain");
    CHECK(p.trk[0][0] == 0xF0 && p.trk[0][1] == 60 && p.trk[0][2] == 0xF0 && p.trk[0][3] == 0 && (p.trk[0][7] & 0x60) == 0x20,
          "... the instrument in full at its first appearance on a track, the same after it (the EDITOR's bytes)");
    /* back: a loop an instrument (two tracks of one: its lanes), its mix from the = commands */
    lp_timing_t tm = {150, 6, 4, 50};
    lp_loaded_t res;
    loops_set_mix(1, RM_LEVEL, 0x7F);
    CHECK(loops_load(0x10, &tm, &res) == LP_OK && res.loops == 2 && !res.insts && !res.cmds && loops_n() == 2 && loops_rows() == 16 &&
          loops_tempo() == 150, "LOAD FROM PATTERN: the stored loops back, 2 loops, nothing left out, the pattern's tempo");
    notes_of(0, got, sizeof got);
    CHECK(!strcmp(got, "0:0:60,2:0:off,4:0:60,6:0:off,8:0:60,10:0:off,12:0:60,14:0:off") && loops_base(0) == 16 &&
          loops_repeats(0) == 1 && loops_inst(0) == 0xF000 && loops_colour(0) == 0 && !loops_mono(0) && loops_mix(0)[RM_LEVEL] == 0x7F,
          "... loop 00 written out over the length, once, POLY, plain mix");
    notes_of(1, got, sizeof got);
    CHECK(!strcmp(got, "0:0:64,0:1:67,8:0:off,8:1:off,12:0:65") && loops_lanes(1) == 2 && loops_inst(1) == 0xF001 &&
          loops_colour(1) == 1 && loops_mix(1)[RM_LEVEL] == 0x40 && loops_mix(1)[RM_PAN] == 0x80, "... loop 01: its two lanes "
          "from two tracks, its mix from its = command (" "%s)", got);
    /* LOAD: more than 4 at once makes another loop of the instrument; a command a loop does not hold counted; a timing
     * of 3 rows a beat kept at the loops' GRID, the tempo to the same BPM; rows past the length's bars left */
    static const tc_t P1[8] = {{0, 0, 60, 0, 0x04, 0x44}, {0, 1, 62, 0, 0, 0}, {0, 2, 64, 0, 0x0E, 0xD2}, {0, 3, 65, 0, 0, 0},
                               {0, 4, 67, 0, 0, 0}, {4, 4, REC_NOTE_OFF, 0, 0, 0}, {5, 0, REC_NOTE_OFF, 0, 0x0A, 0x01}, {6, 4, 69, 0, 0, 0}};
    mk_pat(0x11, 5, 20, P1, 8, 0xF0);
    tm = (lp_timing_t){90, 6, 3, 50};
    CHECK(loops_load(0x11, &tm, &res) == LP_OK && res.loops == 2 && res.cmds == 2 && loops_rows() == 32 && loops_tempo() == 120 &&
          loops_grid() == 0, "LOAD: 5 notes at once of an instrument: 2 loops; the vibrato and the slide left out (2 commands); 3 "
          "rows a beat: 1/16, TEMPO 120 (the same BPM); 20 rows: 2 bars");
    notes_of(0, got, sizeof got);
    CHECK(!strcmp(got, "0:0:60,0:1:62,0:2:64,0:3:65,5:0:off,6:0:69") && loops_cell(0, 0, 2, &c) && pat_cmd(&c, 0x0E) == 0xD2 &&
          loops_cell(0, 0, 0, &c) && !c.n, "... the first 4 its lanes (the note delay kept), the note after an off on the lane free "
          "(%s)", got);
    notes_of(1, got, sizeof got);
    CHECK(!strcmp(got, "0:0:67,4:0:off") && loops_inst(1) == 0xF000, "... the fifth another loop of its instrument (%s)", got);
    static const tc_t PL[2] = {{0, 0, 60, 0, 0, 0}, {4, 0, 64, 0, 0xD3, 0}};   /* LEG kept, as a glide is */
    mk_pat(0x12, 1, 16, PL, 2, 0xF0);
    CHECK(loops_load(0x12, &tm, &res) == LP_OK && res.loops == 1 && !res.cmds && loops_cell(0, 4, 0, &c) && c.note == 64 &&
          pat_cmd(&c, 0xD3) == 0, "LOAD: LEG kept with its note");
    /* 17 instruments: the 17th left out */
    tc_t many[17];
    for (int k = 0; k < 17; k++) many[k] = (tc_t){(uint8_t)k, 0, (uint8_t)(48 + k), (uint8_t)k, 0, 0};
    mk_pat(0x12, 1, 32, many, 17, 0x05);
    tm = (lp_timing_t){120, 6, 4, 50};
    CHECK(loops_load(0x12, &tm, &res) == LP_OK && res.loops == 16 && res.insts == 1 && loops_n() == 16 && loops_colour(13) == 1,
          "LOAD: 16 loops at most, the 17th instrument left out; the colours round again");
    /* SAVE TO PATTERN: 5 loops of 4 lanes over all the rows: the fifth's notes fit no track, counted */
    fresh();
    static const tc_t F4[4] = {{0, 0, 60, 100, 0, 0}, {0, 1, 64, 100, 0, 0}, {0, 2, 67, 100, 0, 0}, {0, 3, 71, 100, 0, 0}};
    for (int k = 0; k < 5; k++) mk_loop(0xF000, 16, 16, 1, F4, 4);
    CHECK(loops_store(0x20, &left) == LP_OK && left == 4 && !rec_pattern(0x20, &p) && p.ntrk == 16, "SAVE TO PATTERN: what fits no "
          "track left out and counted (4 notes), 16 tracks");
    loops_clear();
    CHECK(loops_store(0x20, &left) == LP_NO, "... no loops: refused");
}

/* the player's stops kept where notes are written out (DOUBLE, LENGTH shorter, SAVE TO PATTERN: a note off where a loop
 * goes to rest or a cut repeat ends); SAVE TO PATTERN's mix commands in time order on a shared track; LOAD keeping a
 * key off's tick and a note delay's */
static void t_stops_ticks(void)
{
    char got[400];
    int left;
    rec_cell_t c;
    rec_pat_t p;
    lp_timing_t tm = {120, 6, 4, 50};
    lp_loaded_t res;
    /* DOUBLE: loop 00 (16 rows once over 32) holds a note into its rest; loop 01 (12 x 3, cut at 32) one over the cut */
    fresh();
    static const tc_t H0[1] = {{4, 0, 60, 100, 0, 0}};
    static const tc_t H1[3] = {{2, 1, 62, 100, 0, 0}, {9, 0, 64, 100, 0, 0}, {10, 0, REC_NOTE_OFF, 0, 0, 0}};
    mk_loop(0xF000, 32, 16, 1, H0, 1);
    mk_loop(0xF001, 32, 12, 3, H1, 3);
    CHECK(loops_double() == LP_OK && loops_base(0) == 32 && loops_repeats(0) == 2 && loops_base(1) == 32 && loops_repeats(1) == 2,
          "(DOUBLE: both written out to 32 rows, twice)");
    notes_of(0, got, sizeof got);
    CHECK(!strcmp(got, "4:0:60,16:0:off"), "DOUBLE: a note held into the loop's rest stops there (%s)", got);
    notes_of(1, got, sizeof got);
    CHECK(!strcmp(got, "0:1:off,2:1:62,9:0:64,10:0:off,14:1:62,21:0:64,22:0:off,26:1:62") && loops_lanes(1) == 2,
          "... one held over a cut repeat's end stops at the wrap: a note off at row 0 (%s)", got);
    /* LENGTH shorter: a base cut, its note held over the new end stops at the wrap */
    fresh();
    mk_loop(0xF000, 32, 32, 1, H0, 1);
    CHECK(loops_set_rows(16) == LP_OK && loops_base(0) == 16, "(LENGTH: 16 rows)");
    notes_of(0, got, sizeof got);
    CHECK(!strcmp(got, "0:0:off,4:0:60") && loops_lanes(0) == 1, "LENGTH shorter: a note held over the cut stops at the wrap (%s)",
          got);
    /* SAVE TO PATTERN: loop 00 (4 x 2 over 16) holds a note into its rest: its note off there, the track free after it */
    fresh();
    static const tc_t S0[1] = {{1, 0, 60, 100, 0, 0}};
    static const tc_t S1[2] = {{12, 0, 64, 100, 0, 0}, {14, 0, REC_NOTE_OFF, 0, 0, 0}};
    mk_loop(0xF000, 16, 4, 2, S0, 1);
    mk_loop(0xF001, 16, 16, 1, S1, 2);
    CHECK(loops_store(0x30, &left) == LP_OK && !left && !rec_pattern(0x30, &p) && p.ntrk == 1 && pat_cell(0x30, 5, 0, &c) &&
          c.note == 60 && pat_cell(0x30, 8, 0, &c) && c.note == REC_NOTE_OFF && pat_cell(0x30, 12, 0, &c) && c.note == 64 &&
          c.slot == 1, "SAVE TO PATTERN: a note held into a loop's rest stops there; the track free after it (1 track)");
    /* SAVE TO PATTERN: two loops on one track, the one made second playing first: each one's mix against the one before
     * it in time */
    fresh();
    static const tc_t M0[2] = {{8, 0, 60, 100, 0, 0}, {14, 0, REC_NOTE_OFF, 0, 0, 0}};
    static const tc_t M1[2] = {{0, 0, 64, 100, 0, 0}, {4, 0, REC_NOTE_OFF, 0, 0, 0}};
    mk_loop(0xF000, 16, 16, 1, M0, 2);
    mk_loop(0xF001, 16, 16, 1, M1, 2);
    loops_set_mix(1, RM_PAN, 0x30);
    CHECK(loops_store(0x31, &left) == LP_OK && !rec_pattern(0x31, &p) && p.ntrk == 1 && pat_cell(0x31, 0, 0, &c) && c.note == 64 &&
          pat_cmd(&c, 0x08) == 0x30 && c.n == 1 && pat_cell(0x31, 8, 0, &c) && c.note == 60 && pat_cmd(&c, 0x08) == 0x80 && c.n == 1,
          "SAVE TO PATTERN: a shared track's mix commands in time order (row 0 PAN 30, row 8 back to PAN 80)");
    loops_set_mix(0, RM_PAN, 0x30);
    CHECK(loops_store(0x32, &left) == LP_OK && pat_cell(0x32, 0, 0, &c) && pat_cmd(&c, 0x08) == 0x30 && c.n == 1 &&
          pat_cell(0x32, 8, 0, &c) && !c.n, "... both PAN 30: the first in time brings it, the second nothing");
    /* LOAD: a key off alone (a REAL TIME note's end) through SAVE TO PATTERN and back: the key off kept at its tick */
    fresh();
    static const tc_t K0[2] = {{0, 0, 60, 100, 0, 0}, {2, 0, 0, 0, 0x14, 0x03}};
    mk_loop(0xF000, 16, 16, 1, K0, 2);
    CHECK(loops_store(0x33, &left) == LP_OK && pat_cell(0x33, 2, 0, &c) && !(c.has & RC_NOTE) && pat_cmd(&c, 0x14) == 3,
          "(SAVE TO PATTERN: the key off alone, K03)");
    CHECK(loops_load(0x33, &tm, &res) == LP_OK && res.loops == 1 && !res.cmds && loops_cell(0, 2, 0, &c) && !(c.has & RC_NOTE) &&
          c.n == 1 && pat_cmd(&c, 0x14) == 3, "LOAD: a key off alone kept as itself, at its tick (not a note off at tick 0)");
    /* LOAD: a note delayed 2 ticks on another loop: the old note's off as late; on the same loop, another track's note at
     * that row takes another lane, the track's own its lane */
    static const tc_t D0[2] = {{0, 0, 60, 0, 0, 0}, {4, 0, 62, 1, 0x0E, 0xD2}};
    mk_pat(0x34, 1, 16, D0, 2, 0xF0);
    CHECK(loops_load(0x34, &tm, &res) == LP_OK && res.loops == 2 && !res.cmds && loops_cell(0, 4, 0, &c) && c.note == REC_NOTE_OFF &&
          pat_cmd(&c, 0x0E) == 0xD2 && loops_cell(1, 4, 0, &c) && c.note == 62 && pat_cmd(&c, 0x0E) == 0xD2,
          "LOAD: the next note delayed (ED2) on another loop: the old note's off as late (=== ED2)");
    static const tc_t D1[3] = {{0, 1, 60, 0, 0, 0}, {4, 1, 62, 0, 0x0E, 0xD2}, {4, 0, 64, 0, 0, 0}};
    mk_pat(0x35, 2, 16, D1, 3, 0xF0);
    CHECK(loops_load(0x35, &tm, &res) == LP_OK && res.loops == 1, "(LOAD: one loop)");
    notes_of(0, got, sizeof got);
    CHECK(!strcmp(got, "0:0:60,4:0:62,4:1:64") && loops_cell(0, 4, 0, &c) && pat_cmd(&c, 0x0E) == 0xD2, "... on the same loop: "
          "another track's note at that row on another lane, the delayed one on its track's lane (%s)", got);
}

int main(void)
{
    setvbuf(stdout, 0, _IONBF, 0);                      /* (each line out before a sanitizer's report) */
    loops_fx_new = fx_new;
    t_setup();
    t_new_and_cells();
    t_peel();
    t_clear_mutes();
    t_length_double();
    t_full();
    t_save_parts();
    t_revert_v3();
    t_store_load();
    t_stops_ticks();
    printf("loops: %d checks, %d failed\n", checks, fails);
    return fails ? 1 : 0;
}
