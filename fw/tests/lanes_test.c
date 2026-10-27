/* SPDX-License-Identifier: GPL-3.0-only
 * Copyright (C) 2026 Vasily Kazantsev */
/* LOOPS' lanes (fw/core/play/player.c, specs/files/loops.md) on the PC: loops written through loops.c into the store on
 * a fake flash, the firmware's built-in instruments, played by the player half a block at a time; the notes the lanes
 * start (lane, note, row, tick) and their voices checked: lanes of one loop each their own note, a note off on its lane
 * only; repeats, the rest after them, a cut repeat's end, a note ringing across the wrap; EDx and Kxx to the tick, an
 * EDx past the row never; a TB-303 lane's gate and its SLD slide; a 3xx glide's pitch; FUSION lanes (their layers, the
 * songs' FUSION notes for tick commands); mutes and solos now and at the bar line; notes asked for at once and at a row
 * ahead; a take's pass leaving its stream unread (armed, from silence, ended early); the pattern recorder's preview
 * (with the loops, A STEP LONG, alone with no loops and with loops, PLAY pausing, kept into a loop); a peel and a
 * re-record stopping what a loop holds; the store's edits heard from the next row. */
#include <stdio.h>
#include <string.h>
#include "store_plat.h"
#include "../core/store/store.h"
#include "../core/play/loops.h"

typedef struct { uint8_t lane, note, vol, glide, row, tick; uint16_t pass; } ev_t;
static ev_t evs[1024];
static int nev;
static void lane_ev(uint32_t i, uint32_t note, int32_t vol, uint32_t glide);
#define PL_TEST_LANE(i, note, vol, glide) lane_ev(i, note, vol, glide)
#include "../core/play/player.c"

/* the second core: none here (every job at once on this one) */
int plat_c2_half(void) { return 0; }
void plat_c2_post(void (*job)(uint32_t), uint32_t j) { job(j); }
void plat_c2_wait(void) {}

static int fails, checks;
#define CHECK(c, ...) do { checks++; if (!(c)) { fails++; printf("FAIL %s:%d: ", __FILE__, __LINE__); printf(__VA_ARGS__); printf("\n"); } } while (0)

static void now(int *row, int *tick)     /* the loops' replay: its row and the tick in it */
{
    struct ibxm_fm1_state st;
    if (!pl_ctxs[1].rp) {
        *row = *tick = -1;
        return;
    }
    replay_fm1_state(pl_ctxs[1].rp, &st);
    *row = st.row;
    *tick = (int)pl_ctxs[1].speed - st.tick;
}
static void lane_ev(uint32_t i, uint32_t note, int32_t vol, uint32_t glide)
{
    ev_t *e = &evs[nev < 1023 ? nev++ : 1023];
    int r, t;
    now(&r, &t);
    e->lane = (uint8_t)i;
    e->note = (uint8_t)note;
    e->vol = (uint8_t)vol;
    e->glide = (uint8_t)glide;
    e->row = (uint8_t)r;
    e->tick = (uint8_t)t;
    e->pass = pl_lp_pass;
}
static const ev_t *find(int lane, int from)   /* lane's first event from index from */
{
    for (int k = from; k < nev; k++)
        if (evs[k].lane == lane) return &evs[k];
    return 0;
}
static int count(int lane)
{
    int n = 0;
    for (int k = 0; k < nev; k++) n += evs[k].lane == lane;
    return n;
}

static int16_t buf[2 * 64];
static void blk(int n) { while (n-- > 0) player_audio(buf, 64); }
static int to(int row, int tick)         /* on, a block at a time, to (row, tick) of the loops: 1 there */
{
    for (int k = 0; k < 40000; k++) {
        int r, t;
        now(&r, &t);
        if (r == row && t >= tick) return 1;
        blk(1);
    }
    return 0;
}
static player_state_t state(void)
{
    player_state_t s;
    player_state(&s, 0);
    return s;
}
static pl_voice_t *lane_voice(int i)     /* the voice of lane i's latest note (the first found), or none */
{
    for (int k = 0; k < (int)PL_NV; k++)
        if (pl_ln_mine(&pl_v[k], pl_ln_who((uint32_t)i), pl_ln[i].gen)) return &pl_v[k];
    return 0;
}
static int held_of(int i)                /* lane i's latest note held */
{
    pl_voice_t *v = lane_voice(i);
    return v && v->held;
}

/* a new song's FX setup, as the UI's: BEATS A BAR 4 */
static void fx_new(uint8_t *s)
{
    memset(s, 0, 64);
    s[1] = 0x10;
    s[2] = 0xF2;
    s[11] = 4;
    s[20] = 0x5A;
}
static void fresh(void)                  /* an empty flash, no edits, the player at rest */
{
    fake_reset();
    store_edit_revert();
    store_init();
    loops_revert();
    player_init();
    pl_what = PL_NONE;                   /* (as at power-on: nothing played yet) */
    pl_lcue = 0;
    nev = 0;
}
static rec_cell_t cell(int note, int vol)
{
    rec_cell_t c;
    memset(&c, 0, sizeof c);
    c.has = RC_NOTE | (vol >= 0 ? RC_VOL : 0);
    c.note = (uint8_t)note;
    c.vol = (uint8_t)(vol < 0 ? 0 : vol);
    return c;
}
static void cmd(rec_cell_t *c, int code, int val)
{
    c->has |= RC_CMD;
    c->cmd[c->n][0] = (uint8_t)code;
    c->cmd[c->n++][1] = (uint8_t)val;
}
static void put(int i, int row, int lane, rec_cell_t c) { CHECK(loops_cell_put(i, row, lane, &c) == LP_OK, "cell %d %d %d", i, row, lane); }
static void note_at(int i, int row, int lane, int note) { put(i, row, lane, cell(note, 0x7F)); }
static void set_base(int i, int base, int reps)   /* loop i's base and repeat count, written into the head */
{
    rec_loops_t l;
    uint8_t h[LP_HEAD_MAX];
    CHECK(!rec_loops(&l, 0) && l.head_len <= sizeof h, "the head");
    memcpy(h, l.head, l.head_len);
    h[LP_HEAD + LP_ENTRY * i + 4] = (uint8_t)(base - 1);
    h[LP_HEAD + LP_ENTRY * i + 5] = (uint8_t)reps;
    CHECK(store_edit_piece(REC_LOOPS, 0, STORE_PC_HEAD, REC_LOOPS_VER, h, l.head_len) == STORE_OK, "the head written");
}
static int loop_of(uint8_t cart, uint8_t slot)   /* a new loop (the first: a bar long) */
{
    int first = !loops_n(), i = loops_new(cart, slot);
    if (first) loops_set_rows(16);
    return i;
}

/* lanes of one loop each their own note; a note off ends its lane's alone; a cell's volume, an instrument's own */
static void t_lanes(void)
{
    fresh();
    loop_of(0xF0, 0);
    note_at(0, 0, 0, 60);
    put(0, 2, 1, cell(64, 0x40));
    put(0, 4, 0, cell(62, -1));
    put(0, 6, 1, cell(REC_NOTE_OFF, -1));
    loops_play(0);
    blk(1);
    const ev_t *e = find(0, 0);
    CHECK(e && e->note == 60 && e->row == 0 && e->tick == 0 && e->vol == 64, "row 0: lane 0's note at once, its volume 7F");
    pl_voice_t *v = lane_voice(0);
    CHECK(v && v->held && v->who == 16 && (v->gen & 7) == 0 && v->note == 60, "... a voice of track 16, lane 0's");
    CHECK(state().what == PL_LOOPS && state().playing && state().lp_pass == 0, "the loops play, pass 0");
    to(2, 0);
    e = find(1, 0);
    CHECK(e && e->note == 64 && e->row == 2 && e->vol == 32, "row 2: lane 1's note, volume 40 as the replay takes it");
    CHECK(held_of(0) && held_of(1), "... lane 0's still held (POLY)");
    pl_voice_t *was = lane_voice(0);
    to(4, 0);
    e = find(0, 1);
    CHECK(e && e->note == 62 && e->row == 4 && e->vol == 64, "row 4: lane 0's next note (no volume: the instrument's own)");
    CHECK(!was->held || was->note != 60, "... its note before let go");
    CHECK(held_of(0) && held_of(1), "... lane 1's untouched");
    to(6, 1);
    CHECK(held_of(0) && !held_of(1), "row 6: a note off ends lane 1's note alone");
    to(0, 1);
    CHECK(state().lp_pass == 1 && count(0) == 3, "the wrap: pass 1, lane 0's note again");
    player_stop();
    blk(1);
}

/* repeats, the rest after them, a cut repeat's end at the wrap, a note ringing across the wrap */
static void t_repeats(void)
{
    fresh();
    loop_of(0xF0, 0);
    loop_of(0xF0, 1);
    loop_of(0xF0, 2);
    note_at(0, 1, 0, 60);                /* base 4 x 3: rows 1, 5, 9, then it rests from 12 */
    note_at(1, 3, 0, 62);                /* base 6 x 3: rows 3, 9, 15, the third repeat cut at 16 */
    note_at(2, 12, 0, 64);               /* base 16: it fills the length, its note rings across the wrap */
    set_base(0, 4, 3);
    set_base(1, 6, 3);
    loops_play(0);
    to(11, 5);
    int r0[3] = {-1, -1, -1}, k = 0, ok = 1;
    for (int j = 0; j < nev; j++)
        if (evs[j].lane == 0) { if (k < 3) r0[k] = evs[j].row; k++; }
    CHECK(k == 3 && r0[0] == 1 && r0[1] == 5 && r0[2] == 9, "base 4 x 3: its note at rows 1, 5, 9 (%d %d %d)", r0[0], r0[1], r0[2]);
    CHECK(held_of(0), "... held across the repeats (no note off)");
    to(12, 0);
    CHECK(!held_of(0), "row 12: its repeats over, it rests (its note let go)");
    to(15, 0);
    k = 0;
    for (int j = 0; j < nev; j++)
        if (evs[j].lane == 4) { ok &= evs[j].row == (k == 0 ? 3 : k == 1 ? 9 : 15); k++; }
    CHECK(k == 3 && ok, "base 6 x 3: rows 3, 9, 15");
    to(15, 5);
    CHECK(held_of(4) && held_of(8), "row 15: loop 1's cut repeat and loop 2's note held");
    to(0, 0);
    CHECK(!held_of(4), "the wrap: the cut repeat's note stops there");
    CHECK(held_of(8), "... loop 2's (it fills the length) rings on across it");
    to(2, 0);
    CHECK(held_of(8) && count(8) == 1 && count(0) == 4, "row 2 of pass 1: still the one note; loop 0 at its row 1 again");
    to(5, 0);
    CHECK(count(0) == 5, "loop 0 again from the start: rows 1 and 5");
    player_stop();
    blk(1);
}

/* EDx and Kxx to the tick; an EDx past the row's ticks never sounds */
static void t_ticks(void)
{
    fresh();
    loop_of(0xF0, 0);
    rec_cell_t c = cell(60, 0x7F);
    cmd(&c, 0x0E, 0xD3);
    put(0, 2, 0, c);
    c = cell(62, 0x7F);
    cmd(&c, 0x14, 0x02);
    put(0, 4, 0, c);
    c = cell(64, 0x7F);
    cmd(&c, 0x0E, 0xD7);                 /* (7 of 6 ticks: never) */
    put(0, 8, 1, c);
    c = cell(65, 0x7F);
    cmd(&c, 0x0E, 0xD1);
    cmd(&c, 0x14, 0x04);                 /* (a REAL TIME key off in its own note's row) */
    put(0, 10, 2, c);
    loops_play(0);
    to(2, 2);
    CHECK(!find(0, 0), "ED3 at row 2: not yet at tick 2");
    to(2, 3);
    const ev_t *e = find(0, 0);
    CHECK(e && e->row == 2 && e->tick == 3, "... at tick 3 (row %d tick %d)", e ? e->row : -1, e ? e->tick : -1);
    to(4, 1);
    CHECK(held_of(0) && lane_voice(0)->note == 62, "K02 at row 4: held at tick 1");
    to(4, 2);
    CHECK(!held_of(0), "... let go at tick 2");
    to(9, 0);
    CHECK(!find(1, 0), "ED7 with 6 ticks a row: never");
    to(10, 1);
    e = find(2, 0);
    CHECK(e && e->tick == 1 && held_of(2), "ED1 + K04: the note at tick 1, held");
    to(10, 4);
    CHECK(!held_of(2), "... its key off at tick 4");
    player_stop();
    blk(1);
}

/* a TB-303 lane: its gate half a row; SLD: the next note slides in its voice, nothing starting again */
static void t_tb303(void)
{
    fresh();
    loops_set_next_mono(1);
    loop_of(0xFA, 0);
    note_at(0, 0, 0, 48);
    rec_cell_t c = cell(50, 0x7F);
    cmd(&c, 0xC1, 0);
    put(0, 4, 0, c);
    note_at(0, 5, 0, 53);
    note_at(0, 8, 0, 55);
    loops_play(0);
    to(0, 2);
    pl_voice_t *v = lane_voice(0);
    CHECK(v && v->engine == EN_TB && v->held, "row 0: a TB-303 voice, held at tick 2");
    to(0, 4);
    CHECK(!held_of(0), "... its gate closed half a row on (tick 3)");
    to(4, 5);
    int vi = pl_ln[0].voice;
    CHECK(held_of(0) && vi >= 0 && pl_ln[0].gate < 0, "row 4 with SLD: held through the row, its gate open");
    to(5, 0);
    blk(1);
    v = lane_voice(0);
    CHECK(v && v - pl_v == vi && v->held && v->u.tb.inc != v->u.tb.inc_to && v->note == 53,
          "row 5: the same voice slides to 53 (its pitch gliding, nothing restarted)");
    to(5, 4);
    CHECK(!held_of(0), "... no SLD on it: its gate closes half a row on");
    to(8, 0);
    blk(1);
    v = lane_voice(0);
    CHECK(v && v->held && v->u.tb.inc == v->u.tb.inc_to, "row 8: a note started again (its pitch at once)");
    player_stop();
    blk(1);
}

/* a 3xx glide on a MONO lane: its voice goes on, xx / 16 semitone a tick */
static void t_glide(void)
{
    fresh();
    loops_set_next_mono(1);
    loop_of(0xF0, 0);
    note_at(0, 0, 0, 60);
    rec_cell_t c = cell(64, 0x7F);
    cmd(&c, 0x03, 0x20);
    put(0, 1, 0, c);
    loops_play(0);
    to(0, 5);
    pl_voice_t *v = lane_voice(0);
    to(1, 0);
    const ev_t *e = find(0, 1);
    CHECK(e && e->note == 64 && e->glide == 0x20 && lane_voice(0) == v && v->held && v->note == 60,
          "row 1, 320: the same voice, no new one");
    CHECK(pl_ln[0].bend == 0 && pl_ln[0].bend_to == 256, "... at tick 0 still on 60, going to 64");
    to(1, 1);
    CHECK(pl_ln[0].bend == 128 && v->u.dx.bend == 128 * 21845 + 128 / 3, "tick 1: 2 semitones on (its bend %d)", pl_ln[0].bend);
    to(1, 2);
    CHECK(pl_ln[0].bend == 256 && !pl_ln[0].glide && pl_ln[0].note == 64, "tick 2: there");
    player_stop();
    blk(1);
}

/* LEG on a MONO lane (commands.md#legato): its voice goes on, its pitch there at once; a JUNO-106's filter follows the
 * key; the heard path's PL_LP_LEG the same; onto no held note a plain one */
static void t_legato(void)
{
    fresh();
    loops_set_next_mono(1);
    loop_of(0xF0, 0);
    note_at(0, 0, 0, 60);
    rec_cell_t c = cell(64, 0x7F);
    cmd(&c, 0xD3, 0);
    put(0, 1, 0, c);
    loops_play(0);
    to(0, 5);
    pl_voice_t *v = lane_voice(0);
    uint32_t age = v ? v->age : 0;
    to(1, 0);
    CHECK(v && lane_voice(0) == v && v->held && v->note == 60 && v->age == age, "row 1, LEG: the same voice, nothing "
          "started again");
    CHECK(pl_ln[0].bend == 256 && pl_ln[0].bend_to == 256 && !pl_ln[0].glide && pl_ln[0].note == 64 && v &&
          v->u.dx.bend == 256 * 21845 + 256 / 3, "... on 64 at once (its bend %d)", pl_ln[0].bend);
    player_lp_note(0, 0, 67, 0x7F, -1, 0, PL_LP_LEG);   /* (a take's, heard at once) */
    blk(1);
    CHECK(lane_voice(0) == v && v->age == age && pl_ln[0].bend == 7 * 64 && pl_ln[0].note == 67,
          "PL_LP_LEG: the same voice on 67 (its bend %d)", pl_ln[0].bend);
    player_stop();
    blk(1);
    fresh();                             /* a JUNO-106: the filter's key follow moves */
    loops_set_next_mono(1);
    loop_of(0xF8, 0);
    note_at(0, 0, 0, 60);
    put(0, 1, 0, c);
    loops_play(0);
    to(0, 5);
    v = lane_voice(0);
    to(1, 0);
    CHECK(v && lane_voice(0) == v && v->held && v->u.ju.pitch88 == 64 << 8, "JUNO-106: LEG moves its key follow to 64");
    player_stop();
    blk(1);
    fresh();                             /* onto no held note: a plain note */
    loops_set_next_mono(1);
    loop_of(0xF0, 0);
    rec_cell_t off = {0};
    off.has = RC_NOTE;
    off.note = REC_NOTE_OFF;
    note_at(0, 0, 0, 60);
    put(0, 1, 0, off);
    put(0, 2, 0, c);
    loops_play(0);
    to(1, 5);
    v = lane_voice(0);
    to(2, 0);
    const ev_t *e = find(0, 1);          /* (the lane's second note) */
    CHECK(e && e->note == 64 && lane_voice(0) && lane_voice(0)->held && pl_ln[0].bend == 0 && pl_ln[0].vnote == 64,
          "LEG after a note off: a plain note on 64");
    player_stop();
    blk(1);
}

/* the demo disk's FUSION (its cartridge 06 and its patterns E0-EF) into the store's edit memory, as an upload would
 * have it: 0 done */
static uint32_t le32_(const uint8_t *p) { return p[0] | p[1] << 8 | p[2] << 16 | (uint32_t)p[3] << 24; }
static int fusion_from_disk(void)
{
    static uint8_t d[1u << 19];
    FILE *f = fopen("../../dist/firmware/demo.ptfmdsk", "rb");
    size_t n = f ? fread(d, 1, sizeof d, f) : 0;
    int put = 0;
    if (f) fclose(f);
    if (n < 16 || memcmp(d, "PTFMDISK", 8)) return -1;
    for (uint32_t i = 0, files = le32_(d + 12), at = 16; i < files && at + 12u <= n; i++) {
        uint32_t len = le32_(d + at + 4);
        if ((d[at] == REC_FUSION && d[at + 1] == 0x06) || (d[at] == REC_PATTERN && d[at + 1] >= 0xE0 && d[at + 1] < 0xF0))
            put += store_edit_put(d[at], d[at + 1], d[at + 2], d[at + 3], d + at + 12, len) == STORE_OK;
        at = (at + 12u + len + 3u) & ~3u;
    }
    return put ? 0 : -1;
}
/* FUSION lanes: a row's layers, each a voice of the lane; a row with tick commands through the songs' FUSION notes,
 * the lane's own; a note off lets go of them all */
static void t_fusion(void)
{
    int runs = 0, direct = 0;
    for (int s = 0; s < 5; s++) {        /* (each FUSION instrument of the demo disk's cartridge on a loop alone) */
        fresh();
        CHECK(!fusion_from_disk(), "FUSION: the demo disk's cartridge 06 and its patterns into the store");
        loop_of(0x06, (uint8_t)s);
        note_at(0, 0, 0, 60);
        note_at(0, 0, 1, 67);
        put(0, 8, 0, cell(REC_NOTE_OFF, -1));
        loops_play(0);
        to(0, 2);
        int layers = 0, run[2] = {0, 0};
        for (int k = 0; k < (int)PL_NV; k++)
            layers += pl_ln_mine(&pl_v[k], 16u, pl_ln[0].gen);
        for (int f = 0; f < (int)PL_SF; f++)
            for (int l = 0; l < 2; l++)
                run[l] |= pl_sf[f].on && pl_sf[f].lane == l && pl_sf[f].who == 16 && pl_sf[f].gen == pl_ln[l].gen;
        CHECK(run[0] == run[1], "06:%02X: both lanes the same way, each its own", s);
        CHECK(run[0] || layers >= 1, "06:%02X: lane 0's layers are its voices", s);
        runs += run[0];
        direct += !run[0];
        to(8, 1);
        int held = 0, on = 0;
        for (int k = 0; k < (int)PL_NV; k++) held += pl_v[k].held && pl_ln_mine(&pl_v[k], 16u, pl_ln[0].gen);
        for (int f = 0; f < (int)PL_SF; f++) on += pl_sf[f].on && pl_sf[f].lane == 0;
        CHECK(!held && !on, "06:%02X, row 8: the note off lets go of lane 0's layers, its ticks stop", s);
        player_stop();
        blk(1);
    }
    CHECK(runs >= 1 && direct >= 1, "FUSION lanes: %d by their layers, %d as songs' FUSION notes (tick commands)", direct, runs);
    printf("lanes: FUSION 06:00-04, %d by their layers, %d as songs' FUSION notes\n", direct, runs);
}

/* mutes and solos, now and at the bar line */
static void t_mutes(void)
{
    fresh();
    for (int i = 0; i < 3; i++) { loop_of(0xF0, (uint8_t)i); note_at(i, 0, 0, 60 + i); }
    loops_play(0);
    blk(2);
    CHECK(state().muted == 0, "none muted");
    player_lp_mutes(2, 0);
    blk(1);
    CHECK(pl_ctxs[1].mute == 2 && state().muted == 2, "loop 01 muted at once");
    player_lp_mutes(2, 4);
    blk(1);
    CHECK(state().muted == 0xFFFB, "loop 02 soloed: every other silent, the mute beneath");
    player_lp_mutes(0, 0);
    to(5, 0);
    player_lp_mutes_at_bar(1, 0, 16);
    blk(1);
    CHECK(state().muted == 0 && state().lp_bar_wait, "at the bar: waiting");
    to(15, 5);
    CHECK(state().muted == 0, "... still at row 15");
    to(0, 0);
    blk(1);
    CHECK(state().muted == 1 && !state().lp_bar_wait, "... applied at the bar line (row 0)");
    pl_voice_t *v = lane_voice(0);
    blk(2);
    CHECK(v && v->m_last == 0 && lane_voice(4)->m_last == 4096, "loop 00's new note silent, loop 01's heard");
    player_lp_mutes_at_bar(3, 0, 8);
    to(7, 5);
    CHECK(state().muted == 1, "a bar of 8 rows: not before row 8");
    to(8, 0);
    blk(1);
    CHECK(state().muted == 3, "... at row 8");
    player_lp_mutes_at_bar(0, 0, 16);
    blk(1);
    player_lp_mutes(4, 0);
    to(0, 1);
    CHECK(state().muted == 4 && !state().lp_bar_wait, "a change now drops one waiting for the bar");
    player_stop();
    blk(1);
}

/* notes asked for (a take's): at once, at a row ahead, a note off; a row past is at once */
static void t_requests(void)
{
    fresh();
    loop_of(0xF0, 0);
    loops_play(0);
    to(3, 2);
    player_lp_note(0, 2, 72, 0x7F, 5, 0, 0);
    blk(1);
    CHECK(!find(2, 0), "a note for row 5 at row 3: waiting");
    to(5, 0);
    const ev_t *e = find(2, 0);
    CHECK(e && e->row == 5 && e->tick == 0 && e->note == 72, "... it sounds as row 5 starts");
    player_lp_note(0, 3, 74, 0x7F, -1, 2, 0);
    blk(1);
    int r, t;
    now(&r, &t);
    CHECK(!find(3, 0), "at once with EDx 2: not yet");
    to(r, t + 2);
    e = find(3, 0);
    CHECK(e && e->tick == t + 2, "... two ticks on");
    player_lp_off(0, 2, 7);
    to(6, 5);
    CHECK(held_of(2), "an off for row 7: held at row 6");
    to(7, 0);
    blk(1);
    CHECK(!held_of(2) && held_of(3), "... let go at row 7, lane 3's held");
    to(9, 0);
    player_lp_note(0, 1, 76, 0x7F, 8, 0, 0);
    blk(1);
    e = find(1, 0);
    CHECK(e && e->row == 9, "a row already past: at once");
    player_lp_note(0, 0, 77, 0x7F, 2, 0, 0);   /* (row 2 of the next pass: 9 rows on, past half the length) */
    blk(1);
    CHECK(find(0, 0) && find(0, 0)->row == 9, "... one more than half the length away: past, at once");
    to(15, 3);
    player_lp_note(0, 0, 79, 0x7F, 0, 0, 0);   /* (row 0 of the next pass, from the last row) */
    blk(1);
    CHECK(count(0) == 1, "row 0 asked at row 15: waiting for the wrap");
    to(0, 0);
    blk(1);
    CHECK(count(0) == 2 && evs[nev - 1].note == 79 && evs[nev - 1].row == 0 && evs[nev - 1].pass == 1, "... heard at the wrap");
    player_stop();
    blk(1);
}

/* a take: armed, its pass leaves its loop's stream unread (its notes asked for heard), the stream read again after;
 * from silence at once; ended early */
static void t_take(void)
{
    fresh();
    loop_of(0xF0, 0);
    loop_of(0xF0, 1);
    note_at(0, 0, 0, 60);
    note_at(1, 0, 0, 62);
    note_at(1, 8, 0, 64);
    loops_play(0);
    to(4, 0);
    player_lp_take(1);
    blk(1);
    CHECK(state().lp_take == PL_TAKE_ARMED && state().lp_take_loop == 1, "armed");
    to(8, 1);
    CHECK(count(4) == 2, "armed: loop 01's stream still read (row 8)");
    to(0, 1);
    CHECK(state().lp_take == PL_TAKE_RUNNING && count(4) == 2 && count(0) == 2, "the wrap: running, loop 01 unread, loop 00 on");
    player_lp_note(1, 1, 67, 0x7F, -1, 0, 0);
    to(2, 0);
    CHECK(count(5) == 1 && held_of(5), "its notes asked for: heard, on its track");
    to(8, 1);
    CHECK(count(4) == 2, "row 8: still unread");
    to(0, 0);
    blk(1);
    CHECK(state().lp_take == PL_TAKE_DONE && count(4) == 3 && !held_of(5), "the next wrap: done, its stream read again, "
          "the note it held stopped at the length's end");
    player_lp_take(1);
    to(0, 1);                            /* (a pass on: running) */
    while (state().lp_take != PL_TAKE_RUNNING) blk(1);
    int n4 = count(4);
    to(6, 0);
    player_lp_note(1, 2, 69, 0x7F, -1, 0, 0);
    blk(1);
    player_lp_take(-1);
    blk(1);
    CHECK(state().lp_take == PL_TAKE_DONE && !held_of(6), "ended early: done, what it held stops");
    to(8, 1);
    CHECK(count(4) == n4, "... its stream unread till the loops' next start");
    to(0, 1);
    CHECK(count(4) == n4 + 1, "... read again from there");
    player_stop();
    blk(1);
    player_lp_take(1);
    int from = nev;
    loops_play(0);
    blk(1);
    CHECK(state().lp_take == PL_TAKE_RUNNING && find(0, from) && !find(4, from), "from silence: running at once (row 0: "
          "loop 00 heard, loop 01's stream unread)");
    player_stop();
    blk(1);
    CHECK(state().lp_take == PL_TAKE_DONE, "the loops stopped: the take over");
}

/* the pattern recorder's preview: with the loops, A STEP LONG, muted, alone (no loops; with loops), PLAY pausing the
 * loops, kept into a loop */
static pl_lp_grid_t grid(void)
{
    pl_lp_grid_t g;
    memset(&g, 0, sizeof g);
    g.steps[0] = 0x1111;
    g.note[0] = 60;
    g.step_rows = 1;
    g.length = 16;
    g.ring = 1;
    g.vol = 0x7F;
    g.inst.cart = 0xF0;
    g.inst.slot = 3;
    return g;
}
static int lane_rows(int lane, int *rows, int max)
{
    int n = 0;
    for (int k = 0; k < nev; k++)
        if (evs[k].lane == lane) { if (n < max) rows[n] = evs[k].row; n++; }
    return n;
}
static void t_preview(void)
{
    int rows[8];
    fresh();
    loop_of(0xF0, 0);
    note_at(0, 0, 0, 48);
    loops_play(0);
    to(1, 0);
    pl_lp_grid_t g = grid();
    g.track = 1;
    player_lp_preview(&g);
    to(13, 0);
    CHECK(lane_rows(64, rows, 8) == 3 && rows[0] == 4 && rows[1] == 8 && rows[2] == 12, "with the loops: its steps at rows 4, 8, 12");
    pl_voice_t *v = lane_voice(64);
    CHECK(v && v->who == 17 && (v->gen & 7) == 4, "... on track 17 (the next loop's place), the preview's lane");
    g.ring = 0;
    g.steps[0] = 0x0001;
    g.length = 4;
    g.step_rows = 2;                     /* (8 rows: twice over the bar) */
    player_lp_preview(&g);
    nev = 0;
    to(0, 1);
    CHECK(held_of(64), "A STEP LONG: its note at row 0");
    to(2, 1);
    CHECK(!held_of(64), "... a note off a step on");
    to(8, 1);
    CHECK(held_of(64) && lane_rows(64, rows, 8) == 2, "... again at row 8");
    g.mute = 1;
    player_lp_preview(&g);
    blk(1);
    CHECK(!held_of(64), "muted (ARP): what it holds stops");
    to(0, 1);
    CHECK(lane_rows(64, rows, 8) == 2, "... nothing starts");
    /* PLAY pausing the loops: the preview alone goes on from where it is, the loops' place kept */
    g.mute = 0;
    g.ring = 1;
    g.steps[0] = 0xFFFF;
    g.length = 16;
    g.step_rows = 1;
    player_lp_preview(&g);
    to(6, 0);
    player_lp_preview_alone(1);
    blk(2);
    int r, t;
    now(&r, &t);
    CHECK(state().lp_alone && !state().playing && r == 6 && !held_of(0), "alone: the replay on from row 6, the loops silent");
    int n0 = count(0), n64 = count(64);
    to(1, 0);
    CHECK(count(0) == n0 && count(64) > n64, "... the loops unread at the wrap, the preview on");
    player_continue();
    blk(1);
    now(&r, &t);
    CHECK(!state().lp_alone && state().playing && r == 6, "PLAY: the loops from their place (row 6), the preview with them");
    player_pause();
    blk(1);
    CHECK(!state().playing && !state().lp_alone, "PLAY again: paused");
    player_lp_preview_alone(1);
    blk(1);
    now(&r, &t);
    CHECK(state().lp_alone && r == 0, "ARP with the loops paused: the preview alone from its first step");
    player_lp_preview_alone(0);
    blk(1);
    CHECK(!state().lp_alone && !state().playing && state().row == 6, "ARP again: it stops, the loops' place kept (row 6)");
    /* kept into a loop: its notes go on as the loop's */
    player_continue();
    to(3, 0);
    pl_voice_t *pv = lane_voice(64);
    CHECK(pv && pv->held, "previewing, row 3: its note held");
    int lo = loops_new(0xF0, 3);
    for (int k = 0; k < 16; k++) note_at(lo, k, 0, 60);
    player_lp_preview_keep(lo);
    loops_changed(0);
    blk(1);
    CHECK(!pl_lpv.on && pv->who == 17 && (pv->gen & 7) == 0 && pv->gen == pl_ln[4].gen && pv->held && pl_ln[4].note == 60,
          "kept into loop 01: its note held on, the loop's lane 0's now");
    to(5, 1);
    CHECK(find(4, 0) && held_of(4), "... the loop's notes from the next row");
    player_stop();
    blk(1);
    /* alone with no loops: its own clock, TIMES repeats, the rest of the bars silent */
    fresh();
    loops_cue();
    g = grid();
    g.steps[0] = 0x0001;
    g.length = 4;
    g.step_rows = 4;                     /* (16 rows) */
    g.times = 2;
    g.rows = 48;                         /* (3 bars: the third silent) */
    player_lp_preview(&g);
    player_lp_preview_alone(1);
    blk(1);
    CHECK(state().lp_alone && state().rows == 47, "no loops: alone in a clock of its rows (48)");
    to(47, 0);
    CHECK(lane_rows(64, rows, 8) == 2 && rows[0] == 0 && rows[1] == 16 && !held_of(64), "TIMES 2: rows 0 and 16, then at rest");
    to(0, 1);
    CHECK(lane_rows(64, rows, 8) == 3, "... again from its start");
    g.rows = 32;
    g.times = 1;
    player_lp_preview(&g);
    blk(2);
    CHECK(state().rows == 31, "its clock's rows changed: the replay built again");
    player_lp_preview(0);
    blk(1);
    CHECK(!state().lp_alone && !state().playing, "off: it stops");
}

/* a peel and a re-record stop what a loop holds; an edit is heard from the next row */
static void t_edits(void)
{
    fresh();
    for (int i = 0; i < 3; i++) loop_of(0xF0, (uint8_t)i);
    note_at(0, 0, 0, 60);
    note_at(1, 0, 0, 62);
    note_at(2, 0, 0, 64);
    loops_play(0);
    to(2, 0);
    pl_voice_t *a = lane_voice(4), *b = lane_voice(8);
    CHECK(a && a->held && b && b->held, "loops 01 and 02 hold their notes");
    CHECK(loops_peel(1) == LP_OK, "loop 01 peeled");
    loops_changed(0);
    blk(1);
    CHECK(!a->held && !b->held && pl_lpn == 2, "... loop 01's notes stop, and loop 02's, in its place now");
    to(4, 0);
    int from = nev;
    note_at(1, 6, 0, 66);                /* (written while playing: from the next row) */
    loops_changed(0);
    to(6, 1);
    CHECK(find(4, from) && find(4, from)->note == 66 && find(4, from)->row == 6, "a cell written at row 4: heard at row 6");
    pl_voice_t *c = lane_voice(4);
    CHECK(loops_stream_clear(1) == LP_OK, "loop 01 re-recorded");
    blk(1);
    CHECK(!c->held, "... what it held stops at once");
    player_stop();
    blk(1);
}

/* RECORD LIVE's keys: a key's note on a keys' lane, at once or at its row, in its own instrument on the keys' voices; its
 * end; the loops stopping lets them go */
static void t_keys(void)
{
    fresh();
    loop_of(0xF0, 0);
    loops_play(0);
    to(2, 0);
    pl_inst_t in = {0xF1, 2}, in2 = {0xF0, 3};
    player_lp_keys(0, in, 72, 100, -1, 0);
    blk(1);
    pl_voice_t *v = lane_voice((int)PL_LPK);
    const ev_t *e = find((int)PL_LPK, 0);
    CHECK(e && e->note == 72 && e->row == 2 && v && v->held && v->use == PV_KEY && v->who == PL_LNK_WHO && v->inst.cart == 0xF1 &&
          v->inst.slot == 2, "a keys' lane's note at once: a key's voice (the keys' strip) in its own instrument");
    player_lp_keys(1, in2, 74, 100, 5, 0);
    blk(1);
    CHECK(!find((int)PL_LPK + 1, 0), "... another for row 5 at row 2: waiting");
    to(5, 0);
    blk(1);
    e = find((int)PL_LPK + 1, 0);
    pl_voice_t *w = lane_voice((int)PL_LPK + 1);
    CHECK(e && e->row == 5 && e->note == 74 && w && w->held && w->inst.slot == 3 && w->who == PL_LNK_WHO + 1u,
          "... it sounds as row 5 starts, in its instrument");
    player_lp_off(PL_LP_KEYS, 0, -1);
    blk(1);
    CHECK(!v->held && w->held, "its end: lane 0's let go, lane 1's held");
    player_stop();
    blk(1);
    CHECK(!w->held, "the loops stopped: the keys' lanes' notes let go");
}

int main(void)
{
    setvbuf(stdout, 0, _IONBF, 0);
    loops_fx_new = fx_new;
    t_lanes();
    t_repeats();
    t_ticks();
    t_tb303();
    t_glide();
    t_legato();
    t_fusion();
    t_mutes();
    t_requests();
    t_take();
    t_preview();
    t_edits();
    t_keys();
    printf("lanes: %d checks, %d failed\n", checks, fails);
    return fails ? 1 : 0;
}
