/* SPDX-License-Identifier: GPL-3.0-only
 * Copyright (C) 2026 Vasily Kazantsev */
/* LOOPS' lanes (fw/core/play/player.c, specs/files/loops.md) on the PC: loops written through loops.c into the store on
 * a fake flash, the firmware's built-in instruments, played by the player half a block at a time; the notes the lanes
 * start (lane, note, row, tick) and their voices checked: lanes of one loop each their own note, a note off on its lane
 * only; repeats, the rest after them, a cut repeat's end, a note ringing across the wrap; SHIFT (its repeats, rest and
 * cut turned with it); the length going round with no loops; EDx and Kxx to the tick, an EDx past the row never; a
 * TB-303 lane's gate and its SLD slide; a 3xx glide's pitch; FUSION lanes (their layers, the songs' FUSION notes for tick
 * commands); mutes and solos now and at the bar line; notes asked for at once and at a row ahead; the stage
 * (specs/ui/screens/loops.md#the-stage): its keys' notes heard once as played, then from the stage; the lanes held from
 * their row; jumps (back, forward past the end, landing in a loop's rest, the notes asked for); a POLY stage played MONO;
 * its column (no mute or solo), the keys while it is off (16 lanes of their own), the selected loop silent, its rest
 * while a song is armed, REC's hand-over; the pattern
 * recorder's preview (with the loops, NOTE LENGTH, ties, its cut, alone, PLAY pausing, kept onto the stage); a DELETE
 * LOOP and REC into a loop stopping what a loop holds; the store's edits heard from the next row. */
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
        if (pl_ln_mine(&pl_v[k], (uint32_t)i, pl_ln[i].gen)) return &pl_v[k];
    return 0;
}
static int held_n(int from, int to)      /* the voices held of lanes from..to's latest notes */
{
    int n = 0;
    for (int k = 0; k < (int)PL_NV; k++)
        for (int i = from; i <= to; i++) n += pl_v[k].held && pl_ln_mine(&pl_v[k], (uint32_t)i, pl_ln[i].gen);
    return n;
}
static int to_pass(int pass, int row, int tick)   /* on to (row, tick) of the loops' pass `pass`: 1 there */
{
    for (int k = 0; k < 80000; k++) {
        int r, t;
        now(&r, &t);
        if (pl_lp_pass == pass && r == row && t >= tick) return 1;
        if (pl_lp_pass > pass) return 0;
        blk(1);
    }
    return 0;
}
static int ev_at(int lane, int pass, int row)   /* lane's note started at (pass, row): its note, else 0 */
{
    for (int k = 0; k < nev; k++)
        if (evs[k].lane == lane && evs[k].pass == pass && evs[k].row == row) return evs[k].note;
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
static void fresh(void)                  /* an empty flash, no edits, no stage, the player at rest */
{
    fake_reset();
    store_stage_drop();                  /* (the stage stays through REVERT ALL and a mount: dropped first) */
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

/* the stage (store_stage_put, as the screen writes it): its cells in order (row, lane), the played ones marked */
static uint8_t stg_buf[4096];
static lp_enc_t stg_e;
static void stage_begin(void) { loops_enc_stage(&stg_e, stg_buf, sizeof stg_buf); }
static void stage_cell(int r, int l, rec_cell_t c, int played)
{
    if (played) c.has |= LP_PLAYED;
    loops_enc_cell(&stg_e, 4 * r + l, &c);
}
static void stage_put(void)
{
    uint32_t n = loops_enc_end(&stg_e);
    CHECK(n && store_stage_put(stg_buf, n) == STORE_OK, "the stage written (%u B)", (unsigned)n);
}
static const pl_inst_t KEYS_IN = {0xF0, 3};
static void stage_set(int mono, int track, int silent, int rest)   /* what the screen tells of it (the keys' F0:03) */
{
    pl_lp_stage_t s = {KEYS_IN, (uint8_t)mono, (uint8_t)track, (uint8_t)silent, (uint8_t)rest};
    player_lp_stage(&s);
}
static rec_cell_t off_cell(void) { rec_cell_t c = cell(REC_NOTE_OFF, -1); return c; }

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
    loops_set_keys_mono(1);
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
    loops_set_keys_mono(1);
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
    loops_set_keys_mono(1);
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
    loops_set_keys_mono(1);
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
    loops_set_keys_mono(1);
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

/* the demo disk's FUSION (its cartridge A5 and its patterns A0-A5) into the store's edit memory, as an upload would
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
        if ((d[at] == REC_FUSION && d[at + 1] == 0xA5) || (d[at] == REC_PATTERN && d[at + 1] >= 0xA0 && d[at + 1] <= 0xA5))
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
        CHECK(!fusion_from_disk(), "FUSION: the demo disk's cartridge A5 and its patterns into the store");
        loop_of(0xA5, (uint8_t)s);
        note_at(0, 0, 0, 60);
        note_at(0, 0, 1, 67);
        put(0, 8, 0, cell(REC_NOTE_OFF, -1));
        loops_play(0);
        to(0, 2);
        int layers = 0, run[2] = {0, 0};
        for (int k = 0; k < (int)PL_NV; k++)
            layers += pl_ln_mine(&pl_v[k], 0u, pl_ln[0].gen);
        for (int f = 0; f < (int)PL_SF; f++)
            for (int l = 0; l < 2; l++)
                run[l] |= pl_sf[f].on && pl_sf[f].lane == l && pl_sf[f].who == 16 && pl_sf[f].gen == pl_ln[l].gen;
        CHECK(run[0] == run[1], "06:%02X: both lanes the same way, each its own", s);
        CHECK(run[0] || layers >= 1, "06:%02X: lane 0's layers are its voices", s);
        runs += run[0];
        direct += !run[0];
        to(8, 1);
        int held = 0, on = 0;
        for (int k = 0; k < (int)PL_NV; k++) held += pl_v[k].held && pl_ln_mine(&pl_v[k], 0u, pl_ln[0].gen);
        for (int f = 0; f < (int)PL_SF; f++) on += pl_sf[f].on && pl_sf[f].lane == 0;
        CHECK(!held && !on, "06:%02X, row 8: the note off lets go of lane 0's layers, its ticks stop", s);
        player_stop();
        blk(1);
    }
    CHECK(runs >= 1 && direct >= 1, "FUSION lanes: %d by their layers, %d as songs' FUSION notes (tick commands)", direct, runs);
    printf("lanes: FUSION A5:00-04, %d by their layers, %d as songs' FUSION notes\n", direct, runs);
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

/* SHIFT (specs/files/loops.md, what is kept): a loop plays its row (row - SHIFT) round the length, its repeats and rest
 * turned with it, its last repeat the length cuts stopped at its own row 0 (the playground's: "a SHIFT of 3: the loop plays
 * 3 rows later", "3:67") */
static void t_shift(void)
{
    fresh();
    for (int i = 0; i < 3; i++) loop_of(0xF0, (uint8_t)i);
    note_at(0, 0, 0, 67);                /* base 16, SHIFT 3: heard at row 3 */
    note_at(1, 0, 0, 60);                /* base 4 x 3, SHIFT 2: rows 2, 6, 10, its rest from 14 */
    note_at(2, 2, 0, 62);                /* base 6 x 3, SHIFT 4: its rows 2, 8, 14 at 6, 12, 2; its row 0 at 4 */
    set_base(1, 4, 3);
    set_base(2, 6, 3);
    CHECK(loops_set_shift(0, 3) == LP_OK && loops_set_shift(1, 2) == LP_OK && loops_set_shift(2, 4) == LP_OK, "SHIFTs set");
    loops_play(0);
    to(15, 5);
    CHECK(ev_at(0, 0, 3) == 67 && count(0) == 1, "SHIFT 3: its row 0's note at row 3");
    CHECK(ev_at(4, 0, 2) == 60 && ev_at(4, 0, 6) == 60 && ev_at(4, 0, 10) == 60 && count(4) == 3,
          "base 4 x 3, SHIFT 2: rows 2, 6, 10");
    CHECK(ev_at(8, 0, 2) == 62 && ev_at(8, 0, 6) == 62 && ev_at(8, 0, 12) == 62 && count(8) == 3,
          "base 6 x 3, SHIFT 4: rows 2 (its cut repeat), 6, 12");
    player_stop();
    blk(1);
    nev = 0;
    loops_play(0);
    to(3, 4);
    CHECK(held_of(8), "row 3: the cut repeat's note (its row 14) held");
    to(4, 1);
    CHECK(!held_of(8), "... stopped at its own row 0 (row 4), where its first repeat begins");
    to(13, 0);
    CHECK(held_of(4), "row 13: base 4 x 3's third repeat still held");
    to(14, 1);
    CHECK(!held_of(4), "... row 14: at rest (its row 12, the repeats' end)");
    to_pass(1, 3, 1);
    CHECK(ev_at(0, 1, 3) == 67 && ev_at(4, 1, 2) == 60 && ev_at(8, 1, 2) == 62, "the next pass the same");
    player_stop();
    blk(1);
}

/* the length goes round with no loops (specs/ui/screens/loops.md#the-length: PLAY plays the loops with none) */
static void t_clock(void)
{
    fresh();
    loops_play(0);
    blk(2);
    CHECK(state().what == PL_LOOPS && state().playing && state().rows == 63, "no loops, no record: PLAY plays 4 bars (64 rows)");
    CHECK(to_pass(1, 0, 1), "... round the length: pass 1");
    player_stop();
    blk(1);
    CHECK(loops_set_rows(32) == LP_OK && !loops_n(), "the length 2 bars, no loops");
    loops_play(0);
    blk(2);
    CHECK(state().playing && state().rows == 31, "... PLAY plays its 32 rows");
    CHECK(to_pass(1, 0, 1), "... round them");
    player_stop();
    blk(1);
}

/* the stage's keys (specs/ui/screens/loops.md#the-stage: a note is heard once as it is played, then from the stage): a
 * note asked for at a row ahead stands in for the stage's cell there that once; one at once (its row begun) after its
 * cell was read; both from the stage the next pass; through the stage's column, on the track REC would put it in */
static void t_once(void)
{
    fresh();
    loop_of(0xF0, 0);                    /* (16 rows) */
    stage_set(0, 1, 0xFF, 0);            /* (+ NEW LOOP: track 1, the next loop's mix) */
    stage_begin();
    stage_cell(2, 0, cell(60, 0x7F), 0);
    stage_put();
    loops_play(0);
    to(3, 1);
    CHECK(ev_at(PL_LPS, 0, 2) == 60, "the stage's note at row 2");
    pl_voice_t *v = lane_voice(PL_LPS);
    CHECK(v && v->who == 17 && v->lc == PL_LC_STAGE && v->use == PV_SONG, "... on track 17 (+ NEW LOOP's), the stage's column");
    stage_begin();                       /* a key at row 3 landing ahead at row 5, lane 1, played onto the stage */
    stage_cell(2, 0, cell(60, 0x7F), 0);
    stage_cell(5, 1, cell(64, 0x7F), 1);
    stage_put();
    player_lp_note(PL_LP_STAGE_KEYS, 1, 64, 0x7F, 5, 0, 0);
    to(7, 2);
    CHECK(ev_at(PL_LPT + 1, 0, 5) == 64 && !ev_at(PL_LPS + 1, 0, 5), "row 5: heard once from the keys' lane, the stage's cell "
          "stood in for");
    v = lane_voice(PL_LPT + 1);
    CHECK(v && v->who == 17 && v->lc == PL_LC_SKEYS && v->held, "... the keys' voice on the stage's column");
    stage_begin();                       /* a key at once at row 7, lane 2: its row read already */
    stage_cell(2, 0, cell(60, 0x7F), 0);
    stage_cell(5, 1, cell(64, 0x7F), 1);
    stage_cell(7, 2, cell(67, 0x7F), 1);
    stage_put();
    player_lp_note(PL_LP_STAGE_KEYS, 2, 67, 0x7F, -1, 0, 0);
    blk(1);
    CHECK(ev_at(PL_LPT + 2, 0, 7) == 67 && !ev_at(PL_LPS + 2, 0, 7), "row 7, at once: heard once, from the keys' lane");
    player_lp_off(PL_LP_STAGE_KEYS, 1, -1);
    player_lp_off(PL_LP_STAGE_KEYS, 2, -1);
    to_pass(1, 8, 0);
    CHECK(ev_at(PL_LPS, 1, 2) == 60 && ev_at(PL_LPS + 1, 1, 5) == 64 && ev_at(PL_LPS + 2, 1, 7) == 67 &&
          !ev_at(PL_LPT + 1, 1, 5) && !ev_at(PL_LPT + 2, 1, 7), "the next pass: all three from the stage");
    player_stop();
    blk(1);
}

/* the lanes held (player_lp_hold, specs/ui/screens/loops.md#the-stage): from their row only those lanes are not read and
 * their notes end; the rows before and the other lanes play on (the playground's: "a key landing ahead (4 ROWS): the
 * stage's row before its landing still heard, its lanes silenced from the landing row on, the key's note there") */
static void t_held(void)
{
    fresh();
    CHECK(loops_set_rows(16) == LP_OK, "16 rows, no loops");
    stage_set(0, 0, 0xFF, 0);
    stage_begin();
    for (int r = 0; r < 16; r++) {
        stage_cell(r, 0, cell(60, 100), 0);
        stage_cell(r, 1, cell(64, 100), 0);
    }
    stage_put();
    loops_play(0);
    to_pass(1, 2, 1);                    /* a key at row 2 lands at row 4: every lane held from there, its note there */
    player_lp_hold(15, 1, 4);
    player_lp_note(PL_LP_STAGE_KEYS, 0, 69, 0x7F, 4, 0, 0);
    to(3, 5);
    CHECK(ev_at(PL_LPS, 1, 3) == 60 && ev_at(PL_LPS + 1, 1, 3) == 64 && held_of(PL_LPS) && held_of(PL_LPS + 1),
          "row 3, before the landing: both lanes heard");
    to(6, 0);
    CHECK(!ev_at(PL_LPS, 1, 4) && !ev_at(PL_LPS + 1, 1, 4) && !ev_at(PL_LPS, 1, 5) && !ev_at(PL_LPS + 1, 1, 5) &&
          !held_of(PL_LPS) && !held_of(PL_LPS + 1) && ev_at(PL_LPT, 1, 4) == 69,
          "rows 4-5: the stage's lanes silent, their notes ended at row 4, the key's note there");
    player_lp_hold(15, 0, -1);           /* (let go: free) */
    player_lp_off(PL_LP_STAGE_KEYS, 0, -1);
    to(8, 1);
    CHECK(ev_at(PL_LPS, 1, 7) == 60 && ev_at(PL_LPS + 1, 1, 7) == 64, "free again: read from the next row");
    player_lp_hold(1, 1, 9);             /* MONO's key: lane 0 alone */
    to(10, 1);
    CHECK(!ev_at(PL_LPS, 1, 9) && ev_at(PL_LPS + 1, 1, 9) == 64 && !held_of(PL_LPS) && held_of(PL_LPS + 1),
          "lane 0 held: only it silent");
    player_lp_hold(1, 0, -1);
    to(12, 0);
    player_lp_hold(15, 1, 12);           /* OVERWRITE from the line (its row begun): at once */
    blk(1);
    CHECK(!held_of(PL_LPS) && !held_of(PL_LPS + 1), "a row begun: held at once, the notes there ended");
    to(13, 1);
    CHECK(!ev_at(PL_LPS, 1, 13) && !ev_at(PL_LPS + 1, 1, 13), "... and after");
    to_pass(2, 1, 1);
    CHECK(!ev_at(PL_LPS, 2, 0) && !ev_at(PL_LPS + 1, 2, 1), "held from an earlier pass: still");
    player_lp_hold(15, 0, -1);
    player_stop();
    blk(1);
}

/* a jump (player_lp_jump, specs/ui/screens/loops.md#the-play-position-play-and-jumps): from the next row, round the
 * length; back the same pass, forward past the end the next; a loop landing in its rest falls silent there; the stage
 * plays what it holds from there; the held lanes go on from where the loops land; the notes asked for at the rows passed
 * over dropped */
static void t_jump(void)
{
    fresh();
    loop_of(0xF0, 0);                    /* (16 rows) */
    loops_play(0);
    to(5, 1);
    uint8_t sw0 = state().lp_sw;
    player_lp_jump(-3);                  /* (the next row 6 - 3) */
    to(3, 1);
    CHECK(pl_lp_pass == 0 && state().lp_pass == 0 && state().lp_jumps == 1 && state().lp_land_row == 3,
          "a jump back from row 5: on from row 3, the same pass");
    CHECK(state().lp_sw == (uint8_t)(sw0 + 1) && state().lp_sw_row == 3 && state().lp_sw_rows == pl_ctxs[1].rows_seen,
          "... a switch of the rest's (RECORD LIVE's song follows the loops from row 3)");
    to(10, 1);
    player_lp_jump(8);                   /* (11 + 8: row 3 of the next pass) */
    blk(1);
    player_lp_jump(1);                   /* (asked again before it lands: on from where it was going) */
    to(4, 1);
    CHECK(pl_lp_pass == 1 && state().lp_pass == 1 && state().lp_jumps == 2 && state().lp_land_row == 4,
          "forward past the end: the next pass, at row 4");
    player_stop();
    blk(1);
    /* a jump queued, then PLAY pausing the loops before it lands: it goes; PLAY again on from where they paused */
    fresh();
    loop_of(0xF0, 0);
    loops_play(0);
    to(5, 1);
    player_lp_jump(6);                   /* (the next row 6 + 6) */
    player_pause();
    blk(1);
    CHECK(!pl_lpj.on && !state().playing && state().row == 5, "a jump queued, the loops paused at row 5: the jump gone");
    player_continue();
    int jr = -1, jt = 0;
    for (int k = 0; k < 200 && jr != 6; k++) { blk(1); now(&jr, &jt); }
    CHECK(jr == 6 && state().lp_jumps == 0 && pl_lp_pass == 0, "PLAY again: on from row 5, the next row 6, no landing");
    player_stop();
    blk(1);
    /* landing in a loop's rest (the playground's: base 8 once over 32 rows, a bar on from row 1) */
    fresh();
    loop_of(0xF0, 0);
    loops_set_rows(32);
    set_base(0, 8, 1);
    note_at(0, 0, 0, 60);
    loops_play(0);
    to(1, 1);
    CHECK(held_of(0), "base 8 once: its note held at row 1");
    player_lp_jump(16);
    to(18, 0);
    blk(1);
    CHECK(!held_of(0) && state().lp_land_row == 18, "a jump landing in its rest (row 18): its note ended there");
    player_stop();
    blk(1);
    /* the stage plays what it holds from there: a note just played onto it, heard again after a jump back */
    fresh();
    loops_set_rows(32);
    stage_set(0, 0, 0xFF, 0);
    loops_play(0);
    to(12, 1);
    stage_begin();
    stage_cell(12, 0, cell(69, 0x7F), 1);
    stage_cell(14, 0, off_cell(), 1);
    stage_put();
    player_lp_note(PL_LP_STAGE_KEYS, 0, 69, 0x7F, -1, 0, 0);
    to(14, 1);
    player_lp_off(PL_LP_STAGE_KEYS, 0, -1);
    to(22, 1);
    player_lp_hold(15, 0, 20);           /* (OVERWRITE from row 20 ...) */
    player_lp_jump(-16);                 /* (23 - 16: row 7) */
    to(7, 1);
    CHECK(pl_lph.row[0] == 7 && pl_lph.pass[0] == 0, "... OVERWRITE's lanes held on from where the loops land (row 7)");
    player_lp_hold(15, 0, -1);
    to(12, 1);
    CHECK(ev_at(PL_LPS, 0, 12) == 69 && count(PL_LPS) == 1 && pl_lp_pass == 0, "a jump back in the same pass: the note just "
          "played onto the stage heard from it at row 12");
    to(14, 0);
    /* the notes asked for: those at the rows passed over dropped */
    player_lp_note(PL_LP_STAGE_KEYS, 1, 72, 0x7F, 16, 0, 0);
    player_lp_jump(4);                   /* (15 + 4: row 19) */
    to(20, 1);
    CHECK(!find(PL_LPT + 1, 0), "a jump forward: the note for row 16 passed over dropped");
    player_stop();
    blk(1);
}

/* a length change while the loops play (specs/ui/screens/loops.md#the-length: the play position taken round the new
 * length); RECORD LIVE's rest keeping them silent: a jump moves their place; a start frees the lanes held */
static void t_length(void)
{
    fresh();
    loop_of(0xF0, 0);
    CHECK(loops_set_rows(32) == LP_OK, "32 rows");
    note_at(0, 0, 0, 60);
    loops_play(0);
    to(20, 1);
    CHECK(loops_set_rows(16) == LP_OK, "LENGTH 16 at row 20");
    loops_changed(1);
    blk(1);
    int r, t;
    now(&r, &t);
    CHECK(r == 4 && pl_lp_pass == 0, "the play position round the new length: row 20 on as row 4 (%d)", r);
    to(5, 1);
    CHECK(pl_lp_pass == 1 && state().rows == 15, "... the next row 5, the next pass, 16 rows");
    player_lp_rest(1);                   /* (RECORD LIVE: the loops silent from the next row, their place kept) */
    to(8, 1);
    CHECK(state().lp_rest == 1 && state().lp_rest_row == 6, "the rest: silent from row 6, their place 6");
    uint8_t j0 = state().lp_jumps;
    player_lp_jump(3);
    blk(2);
    CHECK(state().lp_rest_row == 9 && state().lp_jumps == j0, "a jump while silent: their place 9, the replay as it was");
    player_lp_rest(0);
    to_pass(2, 0, 1);
    player_lp_hold(15, 2, 0);
    player_stop();
    blk(1);
    loops_play(0);
    blk(1);
    CHECK(pl_lph.row[0] == 0xFF && pl_lph.row[3] == 0xFF, "a start: nothing held");
    player_stop();
    blk(1);
}

/* a POLY stage played MONO (specs/ui/screens/loops.md#the-stage): one voice, a note on any lane takes over (the later
 * lane's of a row), an end counts only on the lane whose note sounds, as REC writes it; POLY again: its chords */
static void t_mono(void)
{
    fresh();
    CHECK(loops_set_rows(16) == LP_OK, "16 rows");
    stage_begin();
    stage_cell(0, 0, cell(60, 0x7F), 0);
    stage_cell(0, 2, cell(67, 0x7F), 0);
    stage_cell(4, 1, cell(64, 0x7F), 0);
    stage_cell(8, 1, off_cell(), 0);
    stage_cell(12, 0, off_cell(), 0);
    stage_put();
    stage_set(1, 0, 0xFF, 0);
    loops_play(0);
    to(2, 1);
    CHECK(ev_at(PL_LPS, 0, 0) == 67 && !ev_at(PL_LPS + 2, 0, 0) && held_n(PL_LPS, PL_LPS + 3) == 1,
          "row 0's chord: the later lane's note (67) alone, on one voice");
    to(5, 1);
    CHECK(ev_at(PL_LPS, 0, 4) == 64 && held_n(PL_LPS, PL_LPS + 3) == 1, "row 4: lane 1's note takes over");
    to(8, 1);
    CHECK(!held_n(PL_LPS, PL_LPS + 3), "row 8: lane 1's end ends it (its note sounds)");
    to(12, 1);
    CHECK(!held_n(PL_LPS, PL_LPS + 3) && count(PL_LPS) == 2, "row 12: lane 0's end ends nothing");
    to_pass(1, 1, 1);                    /* (row 0's 67 sounding) */
    player_lp_jump(4);                   /* (2 + 4: row 6, lane 1's note at row 4 passed over) */
    to(7, 1);
    CHECK(held_of(PL_LPS) && pl_ln[PL_LPS].note == 67 && pl_lps.from == 1 && !ev_at(PL_LPS, 1, 4),
          "a jump to row 6: the 67 rings on, the merge as counted from row 0 (lane 1's)");
    to(8, 1);
    CHECK(!held_n(PL_LPS, PL_LPS + 3), "... so row 8's end ends it, as REC writes it");
    stage_set(0, 0, 0xFF, 0);            /* POLY again */
    to_pass(2, 5, 1);
    CHECK(ev_at(PL_LPS, 2, 0) == 60 && ev_at(PL_LPS + 2, 2, 0) == 67 && ev_at(PL_LPS + 1, 2, 4) == 64 &&
          held_n(PL_LPS, PL_LPS + 3) == 3, "POLY again: its chord and lane 1's note, three voices");
    stage_set(1, 0, 0xFF, 0);            /* MONO while three sound: lanes 1-3 end at the next row */
    blk(1);
    CHECK(held_n(PL_LPS, PL_LPS + 3) == 3, "MONO: still three until the next row");
    to(6, 1);
    CHECK(held_n(PL_LPS, PL_LPS + 3) == 1 && held_of(PL_LPS), "... there lanes 1-3's notes end, lane 0's rings on");
    to(9, 0);
    CHECK(!held_n(PL_LPS, PL_LPS + 3), "... row 8's end (lane 1's note sounding as counted) ends it");
    player_stop();
    blk(1);
}

/* the stage's column (specs/ui/screens/loops.md#the-stage: mutes and solos never silence it): heard with its loop muted,
 * with another loop soloed; a new track takes its sounding notes there at once; the preview's and a heard note likewise */
static void t_column(void)
{
    fresh();
    loop_of(0xF0, 0);
    loop_of(0xF0, 1);
    note_at(0, 0, 0, 48);
    note_at(1, 0, 0, 50);
    stage_begin();
    stage_cell(1, 0, cell(60, 0x7F), 0);
    stage_put();
    stage_set(0, 0, 0, 0);               /* (loop 00 selected: silent, the stage in its place) */
    player_lp_mutes(1, 0);               /* (loop 00 muted) */
    loops_play(0);
    to(2, 2);
    pl_voice_t *v = lane_voice(PL_LPS);
    CHECK(v && v->who == 16 && v->m_last == 4096, "loop 00 muted: the stage on its track heard");
    player_lp_mutes(0, 2);               /* (loop 01 soloed) */
    blk(2);
    CHECK(v->m_last == 4096 && lane_voice(4) && lane_voice(4)->m_last == 4096, "loop 01 soloed: the stage still heard");
    stage_set(0, 2, 0xFF, 0);            /* (+ NEW LOOP: track 18, silenced by the solo) */
    blk(2);
    CHECK(v->who == 18 && v->held && v->m_last == 4096 && v->pan == pl_ctxs[1].ch[2].pan,
          "a new track: its sounding note there at once (track 18), heard");
    pl_lp_grid_t g;
    memset(&g, 0, sizeof g);
    g.steps[0] = 0xFFFF;
    g.note[0] = 72;
    g.length = 16;
    g.vol = 0x7F;
    g.inst = KEYS_IN;
    g.track = 0;
    player_lp_mutes(1, 0);
    player_lp_preview(&g);
    to(4, 2);
    pl_voice_t *p = lane_voice(4 * PL_LPV);
    CHECK(p && p->who == 16 && p->lc == PL_LC_PREVIEW && p->m_last == 4096, "the preview on loop 00's muted track: heard");
    player_lp_preview(0);
    player_hear(74, KEYS_IN, 0x7F, 0);
    blk(2);
    int heard = 0;
    for (int k = 0; k < (int)PL_NV; k++)
        heard |= pl_v[k].held && pl_v[k].lc == PL_LC_HEAR && pl_v[k].who == 16 && pl_v[k].m_last == 4096;
    CHECK(heard, "a note heard on its own (player_hear) on the muted track: heard");
    player_hear(0, KEYS_IN, 0, 0);
    player_stop();
    blk(1);
}

/* the keys while the stage is off (specs/ui/screens/loops.md#the-stage): PL_LP_STAGE_KEYS' lanes 4-19, 16 of their own
 * (POLY a key a lane, MONO one): each note at once in the keys' instrument, on the stage's column (its track's mix, no
 * mute), heard while the stage rests; a note off ends its lane's alone; a glide and a legato on one; the stage-on keys'
 * lanes 0-3 their own; a new track takes their notes there; the loops' stop lets them go */
static void t_off_keys(void)
{
    fresh();
    loop_of(0xF0, 0);
    note_at(0, 0, 0, 48);
    stage_begin();
    stage_cell(1, 0, cell(60, 0x7F), 0);
    stage_put();
    stage_set(0, 0, 0xFF, 1);            /* (the stage off: at rest, the selected loop 00 heard) */
    player_lp_mutes(1, 0);               /* (loop 00 muted: the stage's column passes it by) */
    loops_play(0);
    to(1, 2);
    CHECK(!ev_at(PL_LPS, 0, 1) && !held_n(PL_LPS, PL_LPS + 3) && ev_at(0, 0, 0) == 48, "the stage at rest: its note at row 1 not "
          "heard, loop 00's at row 0");
    player_lp_off(0, 0, -1);             /* (loop 00's note let go and quiet: the pool's 16 voices are the 16 keys' alone) */
    blk(400);
    for (int k = 0; k < 16; k++) player_lp_note(PL_LP_STAGE_KEYS, 4 + k, (uint8_t)(62 + k), 0x7F, -1, 0, 0);
    blk(1);
    int n = 0, ok = 1;
    for (int k = 0; k < 16; k++) {
        pl_voice_t *v = lane_voice((int)PL_LPO + k);
        n += v && v->held;
        ok &= v && v->who == 16 && v->use == PV_SONG && v->lc >= PL_LC_OKEYS && v->note == 62 + k;
    }
    CHECK(n == 16 && ok, "16 keys at once: each its own lane's voice (%d held), on track 16, the stage's column", n);
    blk(2);
    CHECK(lane_voice(PL_LPO) && lane_voice(PL_LPO)->m_last == 4096, "... loop 00 muted: heard");
    player_lp_off(PL_LP_STAGE_KEYS, 4 + 3, -1);
    blk(1);
    CHECK(!held_of(PL_LPO + 3) && held_of(PL_LPO + 2) && held_of(PL_LPO + 4), "a note off ends its lane's alone");
    pl_voice_t *g = lane_voice(PL_LPO);  /* a glide on one lane (MONO's GLIDE): the voice goes on, its pitch moving */
    player_lp_note(PL_LP_STAGE_KEYS, 4, 67, 0x7F, -1, 0, 0x20);
    blk(1);
    CHECK(g && lane_voice(PL_LPO) == g && g->held && pl_ln[PL_LPO].glide == 0x20, "a glide on one: the same voice, gliding");
    pl_voice_t *l = lane_voice(PL_LPO + 1);   /* a legato on another (MONO's LEGATO) */
    player_lp_note(PL_LP_STAGE_KEYS, 5, 75, 0x7F, -1, 0, PL_LP_LEG);
    blk(1);
    CHECK(l && lane_voice(PL_LPO + 1) == l && l->held && pl_ln[PL_LPO + 1].note == 75, "a legato on one: onto its voice");
    player_lp_note(PL_LP_STAGE_KEYS, 0, 80, 0x7F, -1, 0, 0);   /* the stage-on keys' lane 0: its own */
    blk(1);
    CHECK(held_of(PL_LPT) && lane_voice(PL_LPT)->lc == PL_LC_SKEYS && held_of(PL_LPO) && lane_voice(PL_LPO)->note == 62,
          "the stage-on keys' lane 0 its own (the off keys' notes sound on)");
    stage_set(0, 1, 0xFF, 1);            /* (+ NEW LOOP: track 17) */
    blk(2);
    CHECK(lane_voice(PL_LPO + 2) && lane_voice(PL_LPO + 2)->who == 17, "a new track: the off keys' sounding notes there at once (17)");
    {   /* 16 notes again (the stage-on key's let go and quiet, lane 3's back), then the 17th key (the screen asks lane 4
         * again): its note starts, the first's note is let go, the 15 others sound on */
        player_lp_off(PL_LP_STAGE_KEYS, 0, -1);
        blk(400);
        player_lp_note(PL_LP_STAGE_KEYS, 4 + 3, 65, 0x7F, -1, 0, 0);
        blk(1);
        int before = held_n((int)PL_LPO, (int)PL_LPO + 15), old0 = 0, first0 = 0;
        player_lp_note(PL_LP_STAGE_KEYS, 4, 90, 0x7F, -1, 0, 0);
        blk(1);
        for (int k = 0; k < (int)PL_NV; k++) {
            old0 += pl_v[k].held && pl_v[k].lc == PL_LC_OKEYS && pl_v[k].note != 90;
            first0 += pl_v[k].held && pl_v[k].lc == PL_LC_OKEYS && pl_v[k].note == 90;
        }
        CHECK(before == 16 && lane_voice(PL_LPO) && lane_voice(PL_LPO)->note == 90 && first0 == 1 && old0 == 0 &&
              held_n((int)PL_LPO + 1, (int)PL_LPO + 15) == 15,
              "a 17th note on lane 4: 16 sounded before; its voice sounds, the first's note let go, the 15 others sound on (%d)", before);
    }
    player_lp_off(PL_LP_STAGE_KEYS, 0, -1);
    player_stop();
    blk(2);
    CHECK(!held_n(PL_LPO, PL_LPO + 15), "the loops stopped: the off keys' notes let go");
    player_lp_mutes(0, 0);
    blk(1);
}

/* a song's note taking the voice of a stage's note still sounding (VOICES 8, the keys holding the other 7): the
 * song's own, the stage's mark gone, so its muted track keeps it silent */
static uint8_t st_pat[16], st_song[95];
static void t_steal(void)
{
    fresh();
    memset(st_pat, 0, sizeof st_pat);    /* pattern 40: one track of 16 rows, C-4 on DX7 F0:02 at row 0, held */
    st_pat[0] = 1;
    st_pat[1] = 15;
    st_pat[3] = 1;
    st_pat[5] = 0xF0;                    /* (a note, an instrument, a volume; no rows skipped) */
    st_pat[6] = 60;
    st_pat[7] = 0xF0;
    st_pat[8] = 0x02;
    st_pat[9] = 64;
    st_pat[10] = 0;
    memset(st_song, 0, sizeof st_song);  /* song 40: pattern 40 once, 125 BPM */
    st_song[12] = 125;
    st_song[14] = 6;
    st_song[15] = 4;
    st_song[16] = 50;
    st_song[17] = 2 | 1;
    st_song[19] = 64;
    st_song[21] = 0x40;
    st_song[24] = 0xF7;
    memset(st_song + 25, 0xFF, 5);
    st_song[30] = 0x80;
    CHECK(store_edit_put(REC_PATTERN, 0x40, 0, 1, st_pat, 11) == STORE_OK &&
          store_edit_put(REC_SONG, 0x40, 0, 1, st_song, sizeof st_song) == STORE_OK, "a song of one note (40)");
    loop_of(0xF0, 0);
    stage_begin();
    stage_cell(1, 0, cell(60, 0x7F), 0);
    stage_put();
    stage_set(0, 0, 0xFF, 0);
    player_voice_count(8);
    loops_play(0);
    to(2, 2);
    pl_voice_t *v = lane_voice(PL_LPS);
    int k = v ? (int)(v - pl_v) : -1, n = 0;
    for (int i = 0; i < 7; i++) player_key_on((uint8_t)(72 + i), KEYS_IN, 100);
    blk(1);
    for (int i = 0; i < 8; i++) n += pl_v[i].use != PV_FREE;
    CHECK(v && v->lc == PL_LC_STAGE && k < 8 && n == 8, "VOICES 8: the stage's note (voice %d) and 7 keys", k);
    player_mutes(1);                     /* (the song's track 0 muted) */
    player_play_song(0x40, 0, 0);
    blk(1);
    CHECK(k >= 0 && pl_v[k].use == PV_SONG && pl_v[k].who == 0 && pl_v[k].held && pl_v[k].lc == PL_LC_NONE,
          "the song's note in the stage's voice: the song's own, no stage mark");
    blk(2);
    CHECK(k >= 0 && pl_v[k].m_last == 0, "... its muted track silent");
    for (int i = 0; i < 7; i++) player_key_off((uint8_t)(72 + i));
    player_stop();
    player_mutes(0);
    player_voice_count(12);
    blk(1);
}

/* the selected loop told silent; the stage's rest while a song is armed (specs/ui/screens/loops.md#record-live) */
static void t_silent(void)
{
    fresh();
    loop_of(0xF0, 0);
    loop_of(0xF0, 1);
    for (int r = 0; r < 16; r += 4) { note_at(0, r, 0, 60); note_at(1, r + 2, 0, 62); }
    stage_begin();
    stage_cell(1, 0, cell(72, 0x7F), 0);
    stage_put();
    stage_set(0, 0, 0xFF, 0);            /* (selected loop 00, told silent below) */
    loops_play(0);
    to(1, 1);
    CHECK(held_of(0) && held_of(PL_LPS), "loop 00 and the stage heard");
    stage_set(0, 0, 0, 0);
    blk(1);
    CHECK(!held_of(0) && held_of(PL_LPS), "loop 00 told silent: its note let go at once");
    to(9, 0);
    CHECK(!ev_at(0, 0, 4) && !ev_at(0, 0, 8) && ev_at(4, 0, 6) == 62, "... its lanes unread, loop 01 on");
    stage_set(0, 1, 1, 0);               /* (the selection moved: loop 01 silent, loop 00 heard again) */
    to(13, 0);
    CHECK(ev_at(0, 0, 12) == 60 && !ev_at(4, 0, 10) && !held_of(4), "moved: loop 00 from the next row, loop 01 silent");
    stage_set(0, 1, 1, 1);               /* a song armed: the stage rests, no loop silent */
    blk(1);
    CHECK(!held_of(PL_LPS), "the rest: the stage's note let go at once");
    to_pass(1, 3, 0);
    CHECK(!ev_at(PL_LPS, 1, 1) && ev_at(4, 1, 2) == 62, "... the stage unread, the selected loop heard");
    stage_set(0, 1, 1, 0);               /* the song over: the stage heard again, the loop silent */
    blk(1);
    CHECK(!held_of(4), "... over: loop 01 silent again at once");
    to_pass(2, 2, 0);
    CHECK(ev_at(PL_LPS, 2, 1) == 72 && !ev_at(4, 2, 2), "... the stage from its next row");
    player_stop();
    blk(1);
}

/* REC's hand-over (specs/ui/screens/loops.md#rec-edit-and-the-arm: what of the stage sounds goes on as the loop's,
 * without a break): the stage's sounding note the loop's lane's, the same voice; a stage dropped otherwise stops it */
static void t_hand(void)
{
    fresh();
    loop_of(0xF0, 0);
    note_at(0, 0, 0, 48);
    stage_begin();
    stage_cell(2, 1, cell(64, 0x7F), 1);
    stage_put();
    stage_set(0, 1, 0xFF, 0);            /* (+ NEW LOOP) */
    loops_play(0);
    to(5, 0);
    pl_voice_t *v = lane_voice(PL_LPS + 1);
    CHECK(v && v->held && v->who == 17, "the stage's note held at row 5, on track 17");
    uint32_t len;
    const uint8_t *s = store_stage(&len);
    CHECK(loops_stage_write(1, s, len, KEYS_IN.cart, KEYS_IN.slot, 0) == 1, "REC: the stage as loop 01");
    CHECK(!store_stage(0), "the stage dropped by the write (the hand-over asked first)");
    loops_changed(0);
    stage_set(0, 2, 0xFF, 0);
    blk(1);
    CHECK(v->held && v->who == 17 && v->lc == PL_LC_NONE && v->gen == pl_ln[5].gen && pl_ln[5].note == 64 &&
          !pl_ln_busy(PL_LPS + 1), "... its note goes on as loop 01's lane 1's, the same voice, without a break");
    to_pass(1, 3, 0);
    CHECK(ev_at(5, 1, 2) == 64, "the next pass: from loop 01");
    stage_begin();                       /* a stage dropped without the hand-over (EDIT, a clear): what it holds stops */
    stage_cell(4, 0, cell(67, 0x7F), 0);
    stage_put();
    to(5, 0);
    CHECK(held_of(PL_LPS), "a new stage's note held");
    CHECK(store_stage_drop() == STORE_OK, "dropped");
    CHECK(!held_of(PL_LPS) && !pl_lps.cur.s, "... its note stops at once, the stage unread");
    player_stop();
    blk(1);
}

/* the pattern recorder's preview (specs/ui/screens/loops.md#the-pattern-recorder-arp): with the loops, its steps a row
 * each from their start, on the stage's column; NOTE LENGTH's note offs, ties adding their steps, the row's next note
 * ending the one before, TO THE NEXT none; the last repeat cut at the length's end; muted; alone (PLAY pausing the loops,
 * ARP; no loops: the length round); REC keeping its notes onto the stage */
static pl_lp_grid_t grid(void)
{
    pl_lp_grid_t g;
    memset(&g, 0, sizeof g);
    g.steps[0] = 0x1111;
    g.note[0] = 60;
    g.length = 16;
    g.note_rows = 1;
    g.vol = 0x7F;
    g.inst = KEYS_IN;
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
    to(12, 5);
    CHECK(lane_rows(64, rows, 8) == 3 && rows[0] == 4 && rows[1] == 8 && rows[2] == 12, "with the loops: its steps at rows 4, 8, 12");
    pl_voice_t *v = lane_voice(64);
    CHECK(v && v->who == 17 && (v->gen & 7) == 4 && v->lc == PL_LC_PREVIEW, "... on track 17 (the next loop's place), the "
          "stage's column");
    CHECK(held_of(64), "NOTE LENGTH a row (1/16 at 1/16): held through its row");
    to(13, 1);
    CHECK(!held_of(64), "... its note off on the next");
    g.note_rows = 2;                     /* (1/8) */
    player_lp_preview(&g);
    to_pass(1, 1, 5);
    CHECK(held_of(64), "1/8: held through row 1");
    to(2, 1);
    CHECK(!held_of(64), "... its note off at row 2");
    g.note_rows = 0;                     /* (TO THE NEXT) */
    player_lp_preview(&g);
    to(7, 5);
    CHECK(held_of(64) && ev_at(64, 1, 4) == 60, "TO THE NEXT: the note at row 4 held to the next");
    g.steps[0] = 0x0001;                 /* a note, two ties after it, a row long: held three rows */
    g.ties[0] = 0x0006;
    g.note_rows = 1;
    player_lp_preview(&g);
    to_pass(2, 2, 5);
    CHECK(held_of(64), "a note and two ties: held through row 2");
    to(3, 1);
    CHECK(!held_of(64), "... its note off at row 3");
    g.steps[0] = 0x0005;                 /* 1/4 with a note two rows on: the row's next note ends the one before */
    g.ties[0] = 0;
    g.note_rows = 4;
    player_lp_preview(&g);
    to_pass(3, 5, 5);
    CHECK(held_of(64) && ev_at(64, 3, 2) == 60, "1/4, its next note at row 2: that one on to row 6 (no off at row 4)");
    to(6, 1);
    CHECK(!held_of(64), "... its own off at row 6");
    g.steps[0] = 0x0008;                 /* 12 steps over 16 rows: 1 x 12 + 4, the second repeat cut */
    g.length = 12;
    g.note_rows = 0;
    player_lp_preview(&g);
    to_pass(4, 15, 5);
    CHECK(ev_at(64, 4, 3) == 60 && ev_at(64, 4, 15) == 60 && held_of(64), "LENGTH 12: rows 3 and 15, held (TO THE NEXT)");
    to_pass(5, 0, 1);
    CHECK(!held_of(64), "... cut at the length's end");
    g.mute = 1;
    g.steps[0] = 0x0001;
    player_lp_preview(&g);
    blk(1);
    int m64 = count(64);
    to_pass(6, 0, 1);
    CHECK(!held_of(64) && count(64) == m64, "muted (ARP): nothing starts");
    /* PLAY pausing the loops: the preview alone goes on from where it is, the loops' place kept */
    g.mute = 0;
    g.note_rows = 0;
    g.steps[0] = 0xFFFF;
    g.length = 16;
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
    /* REC: the pattern onto the stage, the preview's sounding notes going on as the stage's */
    player_continue();
    to(3, 0);
    pl_voice_t *pv = lane_voice(64);
    CHECK(pv && pv->held, "previewing, row 3: its note held");
    stage_set(0, 1, 0xFF, 0);
    stage_begin();
    for (int k = 0; k < 16; k++) stage_cell(k, 0, cell(60, 0x7F), 0);
    stage_put();
    player_lp_preview_keep();
    blk(1);
    CHECK(!pl_lpv.on && pv->who == 17 && pv->lc == PL_LC_STAGE && pv->gen == pl_ln[PL_LPS].gen && pv->held &&
          pl_ln[PL_LPS].note == 60, "REC onto the stage: its note held on, the stage's lane 0's now");
    to(5, 1);
    CHECK(ev_at(PL_LPS, pl_lp_pass, 4) == 60 && held_of(PL_LPS), "... the stage's notes from the next row");
    player_stop();
    blk(1);
    /* REC with the preview playing alone: the loops start from where it was, its sounding note the stage's */
    player_lp_preview(&g);
    player_lp_preview_alone(1);
    to(3, 0);
    pv = lane_voice(64);
    CHECK(state().lp_alone && pv && pv->held, "alone, row 3: the preview's note held");
    player_lp_preview_keep();
    blk(1);
    now(&r, &t);
    CHECK(!state().lp_alone && state().playing && r == 3 && pv->held && pv->lc == PL_LC_STAGE,
          "REC with the preview alone: the loops start from where it was (row 3), its note the stage's");
    player_stop();
    blk(1);
    /* the same with no stage (the pattern written into a loop): its notes let go, the loops on from where it was */
    store_stage_drop();
    player_lp_preview(&g);
    player_lp_preview_alone(1);
    to(5, 0);
    pv = lane_voice(64);
    CHECK(state().lp_alone && pv && pv->held, "alone, row 5: the preview's note held");
    player_lp_preview_keep();
    blk(1);
    now(&r, &t);
    CHECK(!state().lp_alone && state().playing && r == 5 && !pv->held && !pl_lpv.on,
          "no stage: its note let go, the loops on from row 5");
    player_stop();
    blk(1);
    /* alone with no loops: round the length (64 rows); off, it stops */
    fresh();
    loops_cue();
    g = grid();
    g.steps[0] = 0x0001;
    g.length = 16;
    player_lp_preview(&g);
    player_lp_preview_alone(1);
    blk(1);
    CHECK(state().lp_alone && state().rows == 63, "no loops: alone, round the length (64 rows)");
    to(63, 0);
    CHECK(lane_rows(64, rows, 8) == 4 && rows[0] == 0 && rows[1] == 16 && rows[2] == 32 && rows[3] == 48,
          "16 steps over 64 rows: rows 0, 16, 32, 48");
    player_lp_preview(0);
    blk(1);
    CHECK(!state().lp_alone && !state().playing, "off: it stops");
}

/* a DELETE LOOP and REC into a loop stop what a loop holds; an edit is heard from the next row */
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

/* RECORD LIVE's tap (player.h): the events of what sounds, at their ticks. */
static pl_tap_t tq[64], tgot[64];
static int tap_read(void)                /* the ring's events into tgot: how many */
{
    int n = 0;
    while (n < 64 && player_tap_peek(&tgot[n])) { player_tap_next(); n++; }
    return n;
}
static int tap_count(int n, int lane, int note)   /* events of tgot's n on that lane (-1 any) of that kind or note (-1 any) */
{
    int c = 0;
    for (int k = 0; k < n; k++) c += (lane < 0 || tgot[k].lane == lane) && (note < 0 || tgot[k].note == note);
    return c;
}
static void t_tap(void)
{
    int n;
    /* a loop's notes and their ends, at the replay's ticks; a muted loop's not told, its going silent told */
    fresh();
    loop_of(0xF0, 0);
    note_at(0, 0, 0, 60);
    put(0, 2, 0, off_cell());
    put(0, 4, 0, cell(62, 0x40));
    put(0, 8, 0, off_cell());
    player_tap(tq, 64);
    loops_play(0);
    blk(1);
    uint32_t t0 = state().lp_t0;
    to(5, 1);
    n = tap_read();
    CHECK(n == 4 && tgot[0].note == PT_MUTE, "the tap: the start tells the loops silent (%d events)", n);
    CHECK(tgot[1].lane == 0 && tgot[1].note == 60 && tgot[1].vol == 0x7F && tgot[1].cart == 0xF0 && tgot[1].ch == 0 &&
          tgot[1].t == (uint16_t)t0 && tgot[2].note == PT_OFF && tgot[2].t == (uint16_t)(t0 + 2 * 6) && tgot[3].note == 62 &&
          tgot[3].vol == 0x40 && tgot[3].t == (uint16_t)(t0 + 4 * 6), "... a loop's note, its note off and the next at their ticks");
    player_lp_mutes(1, 0);
    blk(2);
    n = tap_read();
    CHECK(n == 1 && tgot[0].note == PT_MUTE && tgot[0].cart == 1 && tgot[0].slot == 0, "... a mute taking effect told (the loops silent now)");
    to(9, 0);
    to(0, 1);
    n = tap_read();
    CHECK(tap_count(n, 0, 60) == 0, "... a muted loop's notes not told");
    player_lp_mutes(0, 0);
    blk(2);
    tap_read();
    /* the keys' stage-off lanes, the stage's keys */
    stage_set(0, 1, 0xFF, 0);
    player_lp_note(PL_LP_STAGE_KEYS, 1, 64, 0x60, -1, 0, 0);
    player_lp_note(PL_LP_STAGE_KEYS, 4 + 2, 65, 0x61, -1, 0, 0);
    blk(1);
    n = tap_read();
    CHECK(n == 2 && tgot[0].lane == PT_SKEYS + 1 && tgot[0].note == 64 && tgot[0].ch == 1 && tgot[0].cart == KEYS_IN.cart &&
          tgot[0].slot == KEYS_IN.slot && tgot[1].lane == PL_LPO + 2 && tgot[1].note == 65, "... the keys on the stage and those while it is off, "
          "in the stage's instrument on its track (%d)", n);
    player_lp_off(PL_LP_STAGE_KEYS, 1, -1);
    player_lp_off(PL_LP_STAGE_KEYS, 4 + 2, -1);
    blk(1);
    n = tap_read();
    CHECK(n == 2 && tgot[0].note == PT_OFF && tgot[1].note == PT_OFF, "... their ends");
    /* a key on any screen (a key's own lane), a FUSION-free note here; VOICES 8: 8 notes told, a 9th takes a voice: told lost,
     * then the note; the note let go: the parked one back, nothing told; a parked one's own end ends nothing heard */
    player_stop();
    blk(2);
    fresh();
    player_tap(tq, 64);
    stage_set(0, 0, 0xFF, 1);
    player_voice_count(8);
    loop_of(0xF0, 3);                    /* (a sustaining sound: no loop notes) */
    loops_play(0);
    to(1, 0);
    for (int k = 0; k < 8; k++) player_lp_note(PL_LP_STAGE_KEYS, 4 + k, (uint8_t)(60 + k), 0x7F, -1, 0, 0);
    blk(2);
    n = tap_read();
    CHECK(tap_count(n, -1, PT_MUTE) == n - 8 && tap_count(n, -1, PT_LOST) == 0, "... VOICES 8: 8 notes, each told (%d events)", n);
    player_lp_note(PL_LP_STAGE_KEYS, 4 + 8, 70, 0x7F, -1, 0, 0);
    blk(2);
    n = tap_read();
    int lost = n == 2 && tgot[0].note == PT_LOST ? tgot[0].lane - (int)PL_LPO : -1;
    CHECK(lost >= 0 && lost < 8 && tgot[1].note == 70 && tgot[1].lane == PL_LPO + 8, "... a ninth takes a voice: the note taken told lost "
          "(lane %d), then the new one (%d events)", lost, n);
    blk(8);
    CHECK(pl_parks && state().parked == 1, "... the note taken parked");
    player_lp_off(PL_LP_STAGE_KEYS, 4 + 8, -1);
    n = 0;
    for (int k = 0; k < 600 && state().parked; k++) { blk(1); n += tap_read(); }
    CHECK(n == 1 && !state().parked && held_of((int)PL_LPO + lost), "... a voice free: the parked note back, nothing told but the ninth's end (%d events)", n);
    player_lp_off(PL_LP_STAGE_KEYS, 4 + lost, -1);
    blk(1);
    n = tap_read();
    CHECK(n == 1 && tgot[0].note == PT_OFF && tgot[0].lane == PL_LPO + lost, "... its own end told (the writer finds no note of it)");
    for (int k = 0; k < 8; k++) player_lp_off(PL_LP_STAGE_KEYS, 4 + k, -1);
    blk(400);
    tap_read();
    /* a smaller count parks a note: told lost */
    for (int k = 0; k < 8; k++) player_lp_note(PL_LP_STAGE_KEYS, 4 + k, (uint8_t)(60 + k), 0x7F, -1, 0, 0);
    blk(2);
    tap_read();
    player_voice_count(4);
    blk(2);
    n = tap_read();
    CHECK(tap_count(n, -1, PT_LOST) == 4, "... VOICES falling to 4: the 4 notes past it told lost (%d)", n);
    player_voice_count(8);
    for (int k = 0; k < 8; k++) player_lp_off(PL_LP_STAGE_KEYS, 4 + k, -1);
    blk(400);
    tap_read();
    /* the keys hold every voice: a lane's note starts parked, given no voice: not told; a key's on and off told */
    for (int k = 0; k < 8; k++) player_key_on((uint8_t)(60 + k), KEYS_IN, 100);
    blk(2);
    n = tap_read();
    CHECK(n == 8 && tap_count(n, PT_KEY, -1) == 8 && tgot[0].ch == PT_KEYCH && tgot[0].glide == 60 && tgot[0].note == 60, "... a key's note told, "
          "on the keys' lane, its key its number (%d)", n);
    player_lp_note(PL_LP_STAGE_KEYS, 4, 72, 0x7F, -1, 0, 0);
    blk(2);
    n = tap_read();
    CHECK(n == 0, "... every voice a key's: a lane's note given no voice, not told (%d)", n);
    player_key_off(60);
    blk(1);
    n = tap_read();
    CHECK(n >= 1 && tgot[0].lane == PT_KEY && tgot[0].note == PT_OFF && tgot[0].glide == 60, "... a key's end told, by its key");
    player_stop();
    blk(2);
    player_tap(0, 0);
    /* a stage's notes going on as a loop's (REC): told moved */
    fresh();
    loop_of(0xF0, 0);
    stage_begin();
    stage_cell(1, 0, cell(60, 0x7F), 0);
    stage_put();
    stage_set(0, 1, 0xFF, 0);
    player_tap(tq, 64);
    loops_play(0);
    to(2, 0);
    tap_read();
    player_lp_stage_end(0);
    blk(1);
    n = tap_read();
    CHECK(n >= 1 && tap_count(n, PL_LPS, PT_MOVE) == 1, "... REC's hand-over: the note told moved to the loop's lane (%d)", n);
    player_stop();
    blk(1);
    player_tap(0, 0);
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
    t_shift();
    t_clock();
    t_once();
    t_held();
    t_jump();
    t_length();
    t_mono();
    t_column();
    t_off_keys();
    t_steal();
    t_silent();
    t_hand();
    t_preview();
    t_edits();
    t_tap();
    printf("lanes: %d checks, %d failed\n", checks, fails);
    return fails ? 1 : 0;
}
