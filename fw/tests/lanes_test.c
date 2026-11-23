/* SPDX-License-Identifier: GPL-3.0-only
 * Copyright (C) 2026 Vasily Kazantsev */
/* LOOPS' lanes (fw/core/play/player.c, specs/files/loops.md) on the PC: loops written through loops.c into the store on
 * a fake flash, the firmware's built-in instruments, played by the player half a block at a time; the notes the lanes
 * start (lane, note, row, tick) and their voices checked: lanes of one loop each their own note, a note off on its lane
 * only; repeats, the rest after them, a cut repeat's end, a note ringing across the wrap; SHIFT (its repeats, rest and
 * cut turned with it); the length going round with no loops; EDx and Kxx to the tick, an EDx past the row never; a
 * TB-303 lane's gate and its SLD slide; a 3xx glide's pitch; FUSION lanes (their layers, the songs' FUSION notes for tick
 * commands); mutes and solos now and at the bar line, and the ghosts they make (a muted loop's notes ghosts within a
 * block, its engines going to other notes, its new notes ghosts, back once heard; a TB-303's and DUCK's source's keep
 * their engines; a muted note taken, no fade; a mute and a take at one bar line; a stage's FUSION runner on a muted
 * loop; a note REC hands over into a muted loop; BYPASS as a change of DUCK's source; a solo's ghosts past the list's
 * room); the lanes' ghosts (LEG and a glide on a ghost, a ghost let go whose note count came round passed by, a held
 * ghost going on as a loop's at REC's hand-over); notes asked for at once and at a row ahead; the stage
 * (specs/ui/screens/loops.md#the-stage): its keys' notes heard once as played, then from the stage; the lanes held from
 * their row; jumps (back, forward past the end, landing in a loop's rest, the notes asked for); a POLY stage played
 * MONO; its column (no mute or solo), the keys while it is off (16 lanes of their own), the selected loop silent, its
 * rest while a song is armed, REC's hand-over; the pattern recorder's preview (with the loops, NOTE LENGTH, ties, its
 * cut, alone, PLAY pausing, kept onto the stage); a DELETE LOOP and REC into a loop stopping what a loop holds; the
 * store's edits heard from the next row. The pool's links and lists are checked after every block
 * (sound/pool_check.h). */
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
#define POOL_CHECK 1                     /* (the pool's links and lists checked after every block: sound/pool_check.h) */
#include "../core/play/player.c"
#include "sound/pool_check.h"

/* the returns' rules cases written before the FM-1's weights (a mute's, a solo's, DUCK's source's and the keys' ghosts
 * back, the waits they count): run with the budget out of their way, as voices_test.c's; the rest on the FM-1's own */
#define LT_RULES_BUDGET 65536
#define RULES(call) (pl_budget = LT_RULES_BUDGET, call, pl_budget = PL_BUDGET)

/* the second core: none here (every job at once on this one) */
int plat_c2_half(void) { return 0; }
void plat_c2_post(void (*job)(uint32_t), uint32_t j) { job(j); }
int plat_c2_wait(void) { return 0; }

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
static void blk(int n) { while (n-- > 0) { player_audio(buf, 64); pool_check("lanes_test"); } }
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
    CHECK(v && v - pl_v == vi && v->held && PL_E(v)->u.tb.inc != PL_E(v)->u.tb.inc_to && v->note == 53,
          "row 5: the same voice slides to 53 (its pitch gliding, nothing restarted)");
    to(5, 4);
    CHECK(!held_of(0), "... no SLD on it: its gate closes half a row on");
    to(8, 0);
    blk(1);
    v = lane_voice(0);
    CHECK(v && v->held && PL_E(v)->u.tb.inc == PL_E(v)->u.tb.inc_to, "row 8: a note started again (its pitch at once)");
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
    CHECK(pl_ln[0].bend == 128 && PL_E(v)->u.dx.bend == 128 * 21845 + 128 / 3, "tick 1: 2 semitones on (its bend %d)",
          pl_ln[0].bend);
    to(1, 2);
    CHECK(pl_ln[0].bend == 256 && !pl_ln[0].glide && pl_ln[0].note == 64, "tick 2: there");
    player_stop();
    blk(1);
}

/* a volume alone on a lane (specs/ui/screens/loops.md#the-stage: the keys' pressure as the stage writes it): its sounding
 * note glides there over the row, a share a block, from where it stood (its note's 80, then the last volume), there as
 * the row ends; the next volume alone glides from there; nothing between */
static void t_vol_glide(void)
{
    fresh();
    loop_of(0xF0, 0);
    put(0, 0, 0, cell(60, 80));
    rec_cell_t c;
    memset(&c, 0, sizeof c);
    c.has = RC_VOL;
    c.vol = 127;
    put(0, 2, 0, c);
    c.vol = 80;
    put(0, 6, 0, c);
    loops_play(0);
    to(1, 0);
    pl_voice_t *v = lane_voice(0);
    int32_t g80 = v ? v->gain : 0, g[400], n = 0, most = 0, back = 0, flat = 1;
    while (n < 400) {                    /* (row 2's blocks, then row 3's) */
        int r, t;
        blk(1);
        now(&r, &t);
        if (r >= 4) break;
        if (r >= 2) g[n++] = v->gain;
    }
    int32_t g127 = n ? g[n - 1] : 0, share = n > 2 ? (g127 - g80) / (n / 2) : 0;
    for (int k = 1; k < n; k++) {
        int32_t d = g[k] - g[k - 1];
        if (d < 0) back = 1;
        if (d > most) most = d;
    }
    to(5, 0);
    flat = v->gain == g127;
    to(7, 0);
    CHECK(v && lane_voice(0) == v && n > 20 && g127 > g80 + g80 / 4 && !back && most <= 2 * share + 2 && flat &&
          v->gain == g80, "a volume alone: the note glides 80 -> 127 over its row, a share a block (most %d, the row's share "
          "%d over %d blocks), held there (%d), back to 80 over row 6 (%d of %d)", most, share, n / 2, flat, v ? v->gain : -1, g80);
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
          PL_E(v)->u.dx.bend == 256 * 21845 + 256 / 3, "... on 64 at once (its bend %d)", pl_ln[0].bend);
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
    CHECK(v && lane_voice(0) == v && v->held && PL_E(v)->u.ju.pitch88 == 64 << 8,
          "JUNO-106: LEG moves its key follow to 64");
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

/* a FUSION note's layers on a lane at the lane's own volume: another lane of the loop (its track) playing louder changes
 * nothing of them (each layer started later, each layer's Axy) */
static void t_fusion_vol(void)
{
    int bad = 0, tried = 0;
    for (int s = 0; s < 5; s++) {
        int32_t g[2][40];
        for (int run2 = 0; run2 < 2; run2++) {
            fresh();
            CHECK(!fusion_from_disk(), "FUSION: the demo disk's cartridge A5 and its patterns into the store");
            loop_of(0xA5, (uint8_t)s);
            put(0, 0, 0, cell(60, 30));
            if (run2) put(0, 1, 1, cell(67, 127));
            put(0, 15, 0, cell(REC_NOTE_OFF, -1));
            loops_play(0);
            to(0, 1);
            for (int b = 0; b < 40; b++) {
                int32_t sum = 0;
                blk(8);
                for (int k = 0; k < (int)PL_NV; k++) if (pl_v[k].held && pl_ln_mine(&pl_v[k], 0u, pl_ln[0].gen)) sum += pl_v[k].gain;
                g[run2][b] = sum;
            }
            player_stop();
            blk(1);
        }
        for (int b = 0; b < 40; b++) if (g[0][b] != g[1][b]) { bad++; printf("  06:%02X block %d: lane 0's layers %d alone, %d beside a louder lane\n", s, b * 8, g[0][b], g[1][b]); break; }
        tried++;
    }
    CHECK(!bad, "FUSION lanes: a lane's layers at its own volume, whatever another lane of the loop plays (%d of %d differ)", bad, tried);
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

/* mutes make ghosts (specs/proposals/202611121830-ghost-notes.md, section 3) */
static uint32_t eng_blocks(void) { return pl_eng_blocks[0] + pl_eng_blocks[1] + pl_eng_blocks[2] + pl_eng_blocks[3]; }
static int lanes_n(int from, int to, int ghosts)   /* lanes from..to's latest notes in an engine, or (ghosts) held
                                                     * ghosts */
{
    int n = 0;
    for (int i = from; i <= to; i++) {
        pl_voice_t *v = lane_voice(i);
        n += v && (ghosts ? v->held && v->place == PL_NO_ENG && v->gh == GH_GHOST : v->place != PL_NO_ENG);
    }
    return n;
}
static int let_go_ghosts(uint32_t who)   /* ghosts let go of track who */
{
    int n = 0;
    for (uint32_t k = 0; k < pl_gn; k++) n += pl_v[pl_gl[k]].who == who && !pl_v[pl_gl[k]].held;
    return n;
}
static pl_inst_t pad_in;                 /* a built-in JUNO-106 pad: ATTACK 30 at most, SUSTAIN 100 or more, its VCA on
                                          * the envelope, RELEASE 20 or more (heard a block after its let-go) */
static int pad_found(void)
{
    rec_cart_t rc;
    if (rec_cart(0xF8, &rc)) return 0;
    for (int s = 0; s < rc.nslots && s < 128; s++) {
        const uint8_t *p;
        if (!rec_slot(&rc, (uint8_t)s)) continue;
        pad_in.cart = 0xF8;
        pad_in.slot = (uint8_t)s;
        if (pl_engine(pad_in, &p) != EN_JUNO) continue;
        uint8_t jp[JUNO_PATCH_BYTES];
        rec_juno_patch(p, jp);
        if (jp[11] <= 30 && jp[13] >= 100 && jp[14] >= 20 && !(jp[17] & 4u)) return 1;
    }
    return 0;
}
/* VOICES 8, loops 00-02 a chord of four held each (12 notes for 8 engines: loop 00's, started first, taken as ghosts),
 * loop 01's let go at row 12. Loop 01 muted: within a block its notes are ghosts, and the engines it gave up go to loop
 * 00's ghosts a block on; at VOICES 12, with engines free, its ghosts stay ghosts while muted and nothing renders them;
 * unmuted, they come back at once (4 a block); let go and muted, its tails are ghosts let go; the next bar's notes
 * start as ghosts, taking no engine; loop 02 soloed: every other loop's notes ghosts */
static void t_mute_ghosts(void)
{
    fresh();
    CHECK(pad_found(), "a JUNO-106 pad among the built-in ones (%02X:%02X)", pad_in.cart, pad_in.slot);
    for (int i = 0; i < 3; i++) {
        loop_of(pad_in.cart, pad_in.slot);
        for (int l = 0; l < 4; l++) note_at(i, 0, l, 48 + 12 * i + 3 * l);
    }
    for (int l = 0; l < 4; l++) put(1, 12, l, off_cell());
    player_voice_count(8);
    blk(1);
    loops_play(0);
    blk(2);
    CHECK(lanes_n(0, 3, 1) == 4 && lanes_n(4, 11, 0) == 8 && state().ghosts == 4,
          "VOICES 8, 12 notes: loop 00's ghosts, 01's and 02's in engines");
    uint8_t had[4];
    for (int l = 0; l < 4; l++) had[l] = lane_voice(4 + l) ? lane_voice(4 + l)->place : PL_NO_ENG;
    player_lp_mutes(2, 0);
    blk(1);
    CHECK(lanes_n(4, 7, 1) == 4 && lanes_n(8, 11, 0) == 4 && state().ghosts == 8,
          "loop 01 muted: its notes ghosts within a block (%d)", lanes_n(4, 7, 1));
    blk(1);
    int reused = 1;
    for (int l = 0; l < 4; l++) {
        uint32_t k = had[l] < PL_NE ? pl_e[had[l]].voice : PL_NO_VOICE;
        reused &= k != PL_NO_VOICE && pl_v[k].who == 16 && pl_v[k].back > 0;
    }
    CHECK(reused && lanes_n(0, 3, 0) == 4 && lanes_n(4, 7, 1) == 4 && state().ghosts == 4,
          "... a block on, the engines it gave up loop 00's ghosts coming back");
    player_voice_count(12);
    blk(1);
    uint32_t b0 = eng_blocks();
    blk(8);
    CHECK(lanes_n(4, 7, 1) == 4 && lanes_n(0, 3, 0) == 4 && lanes_n(8, 11, 0) == 4 && eng_blocks() - b0 == 8u * 8u,
          "VOICES 12, 4 engines free: its ghosts never back while muted, unrendered (%u engine blocks in 8: the other "
          "loops' 8 notes)", eng_blocks() - b0);
    player_lp_mutes(0, 0);
    blk(1);
    CHECK(lanes_n(4, 7, 0) == 4 && lane_voice(4)->back > 0 && state().ghosts == 0,
          "unmuted: its 4 ghosts back at once, fading in");
    to(12, 1);
    CHECK(!held_n(4, 7) && lanes_n(4, 7, 0) == 4, "row 12: loop 01's notes let go, ringing in their engines");
    player_lp_mutes(2, 0);
    blk(1);
    CHECK(let_go_ghosts(17) == 4 && lanes_n(4, 7, 0) == 0, "... muted: its tails ghosts let go (%d)",
          let_go_ghosts(17));
    uint32_t a0 = pl_age, s0 = pl_steals;
    to(0, 1);
    int born = 1;
    for (int l = 0; l < 4; l++) {
        pl_voice_t *v = lane_voice(4 + l);
        born &= v && v->age > a0 && v->held && v->place == PL_NO_ENG && v->gh == GH_GHOST && v->m_last == 0;
    }
    CHECK(born && lanes_n(0, 3, 0) == 4 && lanes_n(8, 11, 0) == 4 && pl_steals - s0 == 4u, "the next bar: its notes "
          "start as ghosts, no engine (loops 00's and 02's 8 into 4 free engines and 4 taken: %u)", pl_steals - s0);
    player_lp_mutes(0, 4);
    blk(1);
    CHECK(lanes_n(0, 7, 1) == 8 && lanes_n(8, 11, 0) == 4 && state().ghosts == 8,
          "loop 02 soloed: loop 00's and 01's notes ghosts, 02's in engines");
    player_lp_mutes(0, 0);
    blk(2);
    CHECK(lanes_n(0, 11, 0) == 12 && state().ghosts == 0, "the solo ended: all 12 back");
    player_stop();
    blk(1);
}
/* a TB-303's note keeps its engine muted, and a muted TB-303 loop's notes still take engines: a TB-303 loop (a note a
 * row) and a pad loop, both muted */
static void t_mute_tb(void)
{
    fresh();
    pad_found();
    loop_of(0xFA, 0);
    for (int r = 0; r < 16; r++) note_at(0, r, 0, 36 + (r & 3));
    loop_of(pad_in.cart, pad_in.slot);
    for (int l = 0; l < 4; l++) note_at(1, 0, l, 60 + 3 * l);
    loops_play(0);
    blk(2);
    player_lp_mutes(3, 0);
    blk(1);
    int tb = 1, n = 0;
    for (int r = 2; r < 10; r++) {       /* (each row's TB-303 note, held: in an engine, a ghost never) */
        to(r, 1);
        pl_voice_t *v = lane_voice(0);
        tb &= v && v->engine == EN_TB && v->held && v->place != PL_NO_ENG && v->gh == GH_NO && v->m_last == 0;
        n += lanes_n(4, 7, 1);
    }
    CHECK(tb && n == 8 * 4, "muted: each row's TB-303 note in an engine, silent; the pad's notes ghosts (%d)", n);
    player_voice_count(8);               /* (VOICES 8: 7 keys and the TB-303; an 8th key takes the TB-303's note, the
                                          * quietest held, silent: cut at once, no fade rendered) */
    for (int k = 0; k < 7; k++) player_key_on((uint8_t)(72 + k), KEYS_IN, 100);
    to(11, 1);
    pl_voice_t *v = lane_voice(0);
    int tb_was = v && v->engine == EN_TB && v->place != PL_NO_ENG;
    uint32_t b0 = eng_blocks();
    player_key_on(80, KEYS_IN, 100);
    blk(1);
    n = 0;
    for (int k = 0; k < (int)PL_NV; k++) n += pl_v[k].use != PV_FREE && pl_v[k].engine == EN_TB;
    CHECK(tb_was && !n && eng_blocks() - b0 == 8u,
          "... its note taken by a key: gone, no fade rendered (%u engine blocks)", eng_blocks() - b0);
    player_keys_off();
    player_voice_count(12);
    player_stop();
    blk(1);
}
/* DUCK's source (track 1, loop 01) muted keeps its engines and DUCK follows it; a change of the source acts as a mute
 * on the old source's muted notes and an unmute on the new one's muted ghosts; DUCK off, a mute again */
static void t_mute_duck(void)
{
    fresh();
    pad_found();
    for (int i = 0; i < 3; i++) {
        loop_of(pad_in.cart, pad_in.slot);
        for (int l = 0; l < 4; l++) note_at(i, 0, l, 48 + 12 * i + 3 * l);
    }
    loops_play(0);
    blk(2);
    fx_cmd(0xAA, 1);                     /* (DUCK's SOURCE track 01, DEPTH 100, on) */
    fx_cmd(0xAB, 100);
    fx_cmd(0xAE, FX_DUCK);
    blk(8);
    player_lp_mutes(2, 0);
    blk(8);
    pl_voice_t *v = lane_voice(4);
    CHECK(lanes_n(4, 7, 0) == 4 && v && v->m_last == 0 && player_duck_env() > 0,
          "DUCK's source muted: its notes keep their engines, silent, DUCK following them (%d)",
          (int)player_duck_env());
    fx_cmd(0xAA, 2);
    blk(1);
    CHECK(lanes_n(4, 7, 1) == 4 && lanes_n(8, 11, 0) == 4, "the source now loop 02: loop 01's muted notes ghosts");
    player_lp_mutes(6, 0);
    blk(2);
    CHECK(lanes_n(4, 7, 1) == 4 && lanes_n(8, 11, 0) == 4, "loop 02 muted too: the source's notes keep their engines");
    fx_cmd(0xAA, 1);
    blk(2);
    CHECK(lanes_n(4, 7, 0) == 4 && lanes_n(8, 11, 1) == 4,
          "the source loop 01 again: its ghosts back, loop 02's ghosts");
    player_fx_bypass(1);                 /* (BYPASS: no DUCK in force, no source) */
    blk(2);
    CHECK(lanes_n(4, 7, 1) == 4 && lanes_n(8, 11, 1) == 4, "BYPASS: no source, loop 01's muted notes ghosts too");
    player_fx_bypass(0);
    blk(2);
    CHECK(lanes_n(4, 7, 0) == 4 && lanes_n(8, 11, 1) == 4, "BYPASS off: the source's ghosts back");
    fx_cmd(0xAF, FX_DUCK);
    blk(1);
    CHECK(lanes_n(4, 11, 1) == 8 && lanes_n(0, 3, 0) == 4, "DUCK off: the muted loops' notes all ghosts");
    player_stop();
    blk(1);
}
/* a mute and a take at the same bar line: VOICES 8, loop 00 a quiet chord of 4 from row 4, loop 01 a loud one from row
 * 8, both held across the bar line (the 8 engines full); loop 01 muted at the bar line, and a note asked for on loop 02
 * at row 0. The note takes one of loop 01's (muted now: silent, though its last block was heard), never loop 00's heard
 * ones (which would fade out, and back in a block on) */
static void t_mute_bar_take(void)
{
    fresh();
    pad_found();
    for (int i = 0; i < 3; i++) loop_of(pad_in.cart, pad_in.slot);
    for (int l = 0; l < 4; l++) put(0, 4, l, cell(48 + 3 * l, 0x10));
    for (int l = 0; l < 4; l++) note_at(1, 8, l, 60 + 3 * l);
    player_voice_count(8);
    blk(1);
    loops_play(0);
    to(12, 1);
    int p0 = lane_voice(0) ? lane_voice(0)->peak : -1, p1 = lane_voice(4) ? lane_voice(4)->peak : -1;
    CHECK(lanes_n(0, 7, 0) == 8 && p0 > 0 && p0 < p1, "8 notes in the 8 engines, loop 00's quieter (peaks %d, %d)", p0,
          p1);
    player_lp_mutes_at_bar(2, 0, 16);
    player_lp_note(2, 0, 72, 0x7F, 0, 0, 0);
    to(0, 0);
    int taken = lanes_n(0, 3, 1);
    blk(1);
    int back = 0;
    for (int l = 0; l < 4; l++) back += lane_voice(l) && lane_voice(l)->back > 0;
    CHECK(!taken && !back && lanes_n(0, 3, 0) == 4 && lanes_n(8, 8, 0) == 1 && lanes_n(4, 7, 1) == 4,
          "the bar line: loop 02's note took a muted note's engine; loop 00's untouched (%d taken, %d back)", taken,
          back);
    player_stop();
    blk(1);
}
/* a FUSION note with tick commands (a songs' FUSION note's runner, pl_sf: its layers started by pl_kl_start) on the
 * stage's column of a muted loop: its layers in engines from their first block, no fade in (never ghosts) */
static void t_mute_fusion_column(void)
{
    int found = -1;
    for (int s = 0; s < 5 && found < 0; s++) {   /* (the demo disk's FUSION instrument whose row runs so) */
        fresh();
        fusion_from_disk();
        loop_of(0xA5, (uint8_t)s);
        note_at(0, 0, 0, 60);
        loops_play(0);
        to(0, 2);
        for (int f = 0; f < (int)PL_SF; f++) found = pl_sf[f].on && pl_sf[f].lane == 0 ? s : found;
        player_stop();
        blk(1);
    }
    CHECK(found >= 0, "a FUSION instrument of A5 whose row runs with tick commands (A5:%02X)", found);
    fresh();
    fusion_from_disk();
    loop_of(0xF0, 0);
    note_at(0, 0, 0, 48);
    stage_begin();
    stage_cell(1, 0, cell(60, 0x7F), 0);
    stage_put();
    pl_lp_stage_t st = {{0xA5, (uint8_t)found}, 0, 0, 0, 0};   /* (the stage on loop 00's track, that FUSION) */
    player_lp_stage(&st);
    loops_play(0);
    player_lp_mutes(1, 0);
    to(0, 3);
    int seen = 0, ok = 0, runner = 0;
    for (int b = 0; b < 400 && !seen; b++) {
        blk(1);
        for (int k = 0; k < (int)PL_NV; k++)
            if (pl_v[k].use == PV_SONG && pl_v[k].lc == PL_LC_STAGE) {
                seen++;
                ok += pl_v[k].place != PL_NO_ENG && !pl_v[k].back;
            }
    }
    for (int f = 0; f < (int)PL_SF; f++) runner |= pl_sf[f].on && pl_sf[f].lane == PL_LPS;
    CHECK(runner && seen > 0 && ok == seen,
          "loop 00 muted: the stage's FUSION runner's layers in engines from their first block, no fade in (%d of %d)",
          ok, seen);
    player_stop();
    blk(1);
}

/* section 2's rule on the lanes' ghosts (specs/proposals/202611121830-ghost-notes.md): the keys holding every engine
 * (VOICES 8), MONO loops: loop 00 a JUNO-106 note and LEG at row 1, loop 01 a DX7 note and a 3xx glide at row 1, both
 * ghosts from their start; LEG moves loop 00's ghost there at once (its pitch, its JUNO-106 key), the glide loop 01's a
 * tick at a time, as on sounding lanes; the keys let go, both back at their pitch and key. A ghost let go whose note
 * count is the lane's again (a lane's wraps every 32 notes) is no lane's note now: LEG and the glide pass it by. A
 * stage's held note taken by the keys (a held ghost) goes on as the loop's at REC's hand-over (MOVE), back as the
 * loop's */
static void t_lane_ghosts(void)
{
    fresh();
    loops_set_keys_mono(1);
    loop_of(0xF8, 0);
    note_at(0, 0, 0, 60);
    rec_cell_t c = cell(64, 0x7F);
    cmd(&c, 0xD3, 0);
    put(0, 1, 0, c);
    loop_of(0xF0, 0);
    note_at(1, 0, 0, 60);
    rec_cell_t g3 = cell(64, 0x7F);
    cmd(&g3, 0x03, 0x20);
    put(1, 1, 0, g3);
    player_voice_count(8);
    for (int k = 0; k < 8; k++) player_key_on((uint8_t)(72 + k), KEYS_IN, 100);
    blk(2);
    loops_play(0);
    to(0, 5);
    pl_voice_t *a = lane_voice(0), *b = lane_voice(4);
    CHECK(a && b && a->place == PL_NO_ENG && b->place == PL_NO_ENG && a->gh == GH_GHOST && b->gh == GH_GHOST,
          "the keys holding every engine: loops 00's and 01's notes ghosts");
    if (!a || !b) return;
    to(1, 0);
    CHECK(lane_voice(0) == a && a->held && a->note == 60 && a->bend == 256 && a->key == 64 && a->place == PL_NO_ENG,
          "LEG on a lane's ghost: its pitch and key there at once (bend %d, key %d)", a->bend, a->key);
    CHECK(lane_voice(4) == b && pl_ln[4].bend_to == 256 && b->bend == 0, "a 3xx glide on a lane's ghost: begun");
    to(1, 1);
    CHECK(b->bend == 128 && b->place == PL_NO_ENG, "... a tick on: 2 semitones (its bend %d)", b->bend);
    to(1, 2);
    CHECK(b->bend == 256 && b->place == PL_NO_ENG, "... there, a ghost still");
    player_keys_off();
    blk(4);
    CHECK(a->place != PL_NO_ENG && PL_E(a)->u.ju.pitch88 == 64 << 8 && PL_E(a)->u.ju.bend == 256 * 21845 + 256 / 3 &&
          b->place != PL_NO_ENG && PL_E(b)->u.dx.bend == 256 * 21845 + 256 / 3,
          "the keys let go: both back at their pitch and key");
    pl_voice_t *h = pl_v_spare();        /* (a ghost let go of lane 0, its note count the lane's now: as 32 notes on) */
    *h = *a;
    h->age = ++pl_age;                   /* (an age of its own, as every voice's) */
    h->place = PL_NO_ENG;
    h->held = 0;
    h->back = 0;
    h->bend = 0;
    h->key = 60;
    h->t_off = pl_blk;
    h->t_end = pl_blk + PL_GH_KEEP;
    pl_gh_add(h);
    pl_use(1);
    int leg = pl_ln_leg(0, *pl_ln_in(0), 67, -1);
    pl_ln[0].bend_to = 0;                /* (and a glide back from there) */
    pl_ln[0].glide = 0x40;
    pl_ln_glide(0);
    pl_use(0);
    CHECK(leg && a->bend == 7 * 64 - 0x40 * 4 && h->bend == 0 && h->key == 60, "LEG and a glide pass by a ghost let go "
          "whose note count came round (the lane's note %d, the ghost's %d)", a->bend, h->bend);
    pl_v_free(h);
    player_stop();
    blk(1);
    fresh();                             /* MOVE: the stage's held note a ghost, handed over by REC */
    loop_of(0xF0, 3);
    stage_begin();
    stage_cell(1, 0, cell(60, 0x7F), 0);
    stage_put();
    stage_set(0, 1, 0xFF, 0);
    player_voice_count(8);
    loops_play(0);
    to(1, 1);
    for (int k = 0; k < 8; k++) player_key_on((uint8_t)(48 + k), KEYS_IN, 100);
    blk(2);
    pl_voice_t *s = 0;
    for (int k = 0; k < (int)PL_NV; k++)
        if (pl_v[k].use == PV_SONG && pl_v[k].gh == GH_GHOST && pl_v[k].held && pl_v[k].who == pl_ln_who(PL_LPS))
            s = &pl_v[k];
    CHECK(s != 0, "a stage's held note taken by the keys: a held ghost");
    if (!s) return;
    player_lp_stage_end(0);
    blk(1);
    CHECK(s->gh == GH_GHOST && s->held && s->who == pl_ln_who(0) && s->lc == PL_LC_NONE && pl_ln_mine(s, 0, pl_ln[0].gen),
          "MOVE (REC's hand-over): the held ghost goes on as loop 00's lane 0's");
    player_keys_off();
    blk(4);
    CHECK(s->place != PL_NO_ENG && s->held && s->who == pl_ln_who(0), "... the keys let go: back as the loop's");
    player_stop();
    player_voice_count(12);
    blk(1);
}
/* the list full of a solo's ghosts when it ends (section 3): VOICES 8, eight POLY loops of a JUNO-106 pad chord of four
 * held (32 notes: 8 sound, 24 ghosts, the list full); loop 07 soloed: every other loop's notes ghosts, the 4 past the 24
 * lost (counted; which go is the list full's rule, tested in voices_test), loop 07's in engines; the solo ended: none
 * lost then, 4 back in the block (the engines free), the rest waiting, held ghosts */
static void t_solo_full(void)
{
    fresh();
    CHECK(pad_found(), "a JUNO-106 pad among the built-in ones");
    for (int i = 0; i < 8; i++) {
        loop_of(pad_in.cart, pad_in.slot);
        for (int l = 0; l < 4; l++) note_at(i, 0, l, 36 + 5 * i + l);
    }
    player_voice_count(8);
    blk(1);
    loops_play(0);
    blk(4);
    int held0 = held_n(0, 31);
    CHECK(held0 == 32 && state().voices == 8 && pl_gn == 24, "32 notes at VOICES 8: 8 heard, 24 ghosts (%d %d %d)", held0,
          state().voices, (int)pl_gn);
    player_lp_mutes(0, 0x80);
    blk(2);
    int held1 = held_n(0, 31);
    CHECK(pl_gn == 24 && lanes_n(28, 31, 0) == 4 && held1 == 28, "loop 07 soloed: its notes heard, the others' ghosts, "
          "the list full, 4 past it lost (%d held, %d ghosts)", held1, (int)pl_gn);
    player_lp_mutes(0, 0);
    blk(1);
    CHECK(held_n(0, 31) == 28 && pl_gn == 20 && state().voices == 8, "the solo ended: none lost then, 4 back in the "
          "engines free (%d held, %d ghosts)", held_n(0, 31), (int)pl_gn);
    player_stop();
    player_voice_count(12);
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

/* CUT (specs/ui/screens/loops.md#cut): the loops' rows from their start, across the length's end, go round a cut loop's
 * cut, so it drifts against the length; at each it plays what it plays at that row uncut (SHIFT turning its notes under
 * the window); a note sounding over its wrap ends there; its notes past the mark unheard; PLAY from the start plays the
 * same again; the stage over it (its track's) wraps there too */
static void t_cut(void)
{
    fresh();
    loop_of(0xF0, 0);
    loop_of(0xF0, 1);
    note_at(0, 0, 0, 67);
    note_at(0, 2, 0, 62);
    note_at(0, 4, 0, 60);                /* (past the mark at 3: unheard) */
    note_at(1, 1, 0, 64);                /* (cut at 3: its note held over rows 1-2, ended at its wrap) */
    CHECK(loops_set_cut(0, 3) == LP_OK && loops_set_cut(1, 3) == LP_OK, "cuts at 3 (a bar of 16 rows)");
    loops_play(0);
    to(2, 3);
    CHECK(held_of(4), "row 2: loop 1's note from row 1 held");
    to(3, 1);
    CHECK(!held_of(4), "... ended at its wrap (row 3)");
    to(15, 5);
    CHECK(ev_at(0, 0, 0) == 67 && ev_at(0, 0, 3) == 67 && ev_at(0, 0, 15) == 67 && ev_at(0, 0, 2) == 62 && ev_at(0, 0, 14) == 62 &&
          !ev_at(0, 0, 4) && count(0) == 11, "pass 0: its rows 0 and 2 round the cut (0, 3 ... 15 and 2, 5 ... 14), its note at 4 unheard");
    to_pass(1, 15, 5);
    CHECK(ev_at(0, 1, 2) == 67 && ev_at(0, 1, 1) == 62 && !ev_at(0, 1, 0), "pass 1: it runs on across the length's end (its row 0 at "
          "row 2, its row 2 at 1): drifting");
    player_stop();
    blk(1);
    nev = 0;
    loops_play(0);
    to(3, 1);
    CHECK(ev_at(0, 0, 0) == 67 && ev_at(0, 0, 3) == 67, "PLAY from the start: the same again");
    player_stop();
    blk(1);
    /* SHIFT on a cut loop: the window stays, its notes turn under it */
    nev = 0;
    CHECK(loops_set_shift(0, 1) == LP_OK && loops_cut(0) == 3, "(SHIFT 1: the cut kept)");
    loops_play(0);
    blk(2);
    to(7, 1);
    CHECK(ev_at(0, 0, 1) == 67 && ev_at(0, 0, 4) == 67 && ev_at(0, 0, 7) == 67 && count(0) == 3,
          "SHIFT 1: the window's rows play the loop's 15, 0, 1: its note at 0 at rows 1, 4, 7; its note at 2 out of the window");
    player_stop();
    blk(1);
    /* the stage over a cut loop wraps at its mark */
    nev = 0;
    stage_begin();
    stage_cell(0, 0, cell(72, 0x7F), 1);
    stage_cell(5, 0, cell(74, 0x7F), 1);                 /* (past the mark: unheard) */
    stage_put();
    stage_set(0, 0, 0, 0);
    loops_play(0);
    blk(2);
    to(7, 1);
    CHECK(ev_at(PL_LPS, 0, 0) == 72 && ev_at(PL_LPS, 0, 3) == 72 && ev_at(PL_LPS, 0, 6) == 72 && !ev_at(PL_LPS, 0, 5),
          "the stage over loop 0 (cut 3): its row 0 at 0, 3, 6; its row 5 unheard");
    player_stop();
    blk(1);
    stage_set(0, 2, 0xFF, 0);
    nev = 0;
    loops_play(0);
    blk(2);
    to(7, 1);
    CHECK(ev_at(PL_LPS, 0, 0) == 72 && ev_at(PL_LPS, 0, 5) == 74 && !ev_at(PL_LPS, 0, 3), "... over + NEW LOOP: the length's rows, as ever");
    player_stop();
    blk(1);
    store_stage_drop();
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

/* the stage's keys heard at once (PL_LP_NOW, specs/ui/screens/loops.md#the-stage: each key sounds at its press): a note
 * landing on a row ahead heard now, standing in for its cell there that once (neither the stage's lane nor the keys'
 * starts it there), then from the stage the next pass; a quick tap let go before its row heard short, nothing at its row,
 * the next pass from the stage; a jump forward passing over its row drops it silently, a jump back keeps it; MONO's merge
 * stood in for alike, LEG at once on the note sounding */
static void t_now(void)
{
    fresh();
    loop_of(0xF0, 0);                    /* (16 rows) */
    stage_set(0, 1, 0xFF, 0);
    stage_begin();
    stage_cell(5, 1, cell(64, 0x7F), 1); /* (as the screen writes it: the key at row 3 lands on row 5) */
    stage_cell(12, 2, cell(67, 0x7F), 1);
    stage_cell(13, 2, off_cell(), 1);
    stage_put();
    loops_play(0);
    to(3, 2);
    nev = 0;
    player_lp_note(PL_LP_STAGE_KEYS, 1, 64, 0x7F, 5, 0, PL_LP_NOW);
    blk(1);
    CHECK(ev_at(PL_LPT + 1, 0, 3) == 64 && held_of(PL_LPT + 1), "a key at row 3 landing on row 5: heard at once, at row 3");
    to(6, 1);
    CHECK(count(PL_LPT + 1) == 1 && !ev_at(PL_LPS + 1, 0, 5) && held_of(PL_LPT + 1) && !pl_lpq_n,
          "... not started again at row 5 (the stage's cell stood in for, the keys' lane's note going on)");
    player_lp_off(PL_LP_STAGE_KEYS, 1, -1);
    blk(1);
    CHECK(!held_of(PL_LPT + 1), "... its release: ended at once");
    to(10, 2);                           /* a quick tap at row 10 landing on row 12, let go at once */
    player_lp_note(PL_LP_STAGE_KEYS, 2, 67, 0x7F, 12, 0, PL_LP_NOW);
    blk(2);
    CHECK(ev_at(PL_LPT + 2, 0, 10) == 67, "a quick tap at row 10 landing on row 12: heard at once");
    player_lp_off(PL_LP_STAGE_KEYS, 2, -1);
    blk(1);
    CHECK(!held_of(PL_LPT + 2), "... let go before its row: ended at once, heard short");
    to(14, 0);
    CHECK(!ev_at(PL_LPS + 2, 0, 12) && count(PL_LPT + 2) == 1, "... nothing at row 12 that pass (stood in for)");
    to_pass(1, 14, 0);
    CHECK(ev_at(PL_LPS + 1, 1, 5) == 64 && ev_at(PL_LPS + 2, 1, 12) == 67 && count(PL_LPT + 1) == 1 && count(PL_LPT + 2) == 1,
          "the next pass: both from the stage, at their rows (5, 12)");
    player_stop();
    blk(1);
    /* jumps: forward over its row, dropped without a sound; back, kept for its row */
    fresh();
    loop_of(0xF0, 0);
    stage_set(0, 1, 0xFF, 0);
    stage_begin();
    stage_cell(3, 0, cell(60, 0x7F), 1);
    stage_cell(8, 1, cell(62, 0x7F), 1);
    stage_put();
    loops_play(0);
    to(1, 2);
    nev = 0;
    player_lp_note(PL_LP_STAGE_KEYS, 0, 60, 0x7F, 3, 0, PL_LP_NOW);
    player_lp_jump(4);                   /* (the next row 2 + 4: row 6, row 3 passed over) */
    to(7, 0);
    CHECK(ev_at(PL_LPT, 0, 1) == 60 && count(PL_LPT) == 1 && !count(PL_LPS) && !pl_lpq_n,
          "a jump forward over its row (3): heard at once only, its stand-in dropped without a sound");
    player_lp_off(PL_LP_STAGE_KEYS, 0, -1);
    nev = 0;
    player_lp_note(PL_LP_STAGE_KEYS, 1, 62, 0x7F, 8, 0, PL_LP_NOW);
    player_lp_jump(-4);                  /* (the next row 8 - 4: row 4, the same pass) */
    to(9, 1);
    CHECK(ev_at(PL_LPT + 1, 0, 7) == 62 && count(PL_LPT + 1) == 1 && !ev_at(PL_LPS + 1, 0, 8) && !pl_lpq_n && pl_lp_pass == 0,
          "a jump back before its row (8): kept, the stage's cell stood in for there");
    player_lp_off(PL_LP_STAGE_KEYS, 1, -1);
    player_stop();
    blk(1);
    /* MONO: the merge's cell stood in for alike; LEG at once on the note the keys' lane holds */
    fresh();
    loops_set_rows(16);
    stage_set(1, 0, 0xFF, 0);
    stage_begin();
    stage_cell(2, 0, cell(60, 0x7F), 1);
    stage_cell(4, 0, cell(64, 0x7F), 1);
    stage_put();
    loops_play(0);
    to(1, 2);
    nev = 0;
    player_lp_note(PL_LP_STAGE_KEYS, 0, 60, 0x7F, 2, 0, PL_LP_NOW);
    to(3, 1);
    player_lp_note(PL_LP_STAGE_KEYS, 0, 64, 0x7F, 4, 0, PL_LP_LEG | PL_LP_NOW);
    blk(1);
    pl_voice_t *v = lane_voice(PL_LPT);
    CHECK(ev_at(PL_LPT, 0, 1) == 60 && ev_at(PL_LPT, 0, 3) == 64 && pl_ln[PL_LPT].note == 64 && held_n(PL_LPT, PL_LPT) == 1 &&
          v && v->held, "MONO: a key at once, LEG over it at once: the one voice held moves to its note");
    to(6, 0);
    CHECK(!count(PL_LPS) && count(PL_LPT) == 2 && !pl_lpq_n, "... neither started again by the stage (rows 2, 4: stood in for)");
    player_lp_off(PL_LP_STAGE_KEYS, 0, -1);
    to_pass(1, 5, 0);
    CHECK(ev_at(PL_LPS, 1, 2) == 60 && ev_at(PL_LPS, 1, 4) == 64, "... the next pass: from the stage");
    player_stop();
    blk(1);
    store_stage_drop();
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
    loops_play(0);
    player_lp_mutes(1, 0);               /* (loop 00 muted: after the start, which sends the loops' own mutes) */
    to(1, 0);
    pl_voice_t *v = lane_voice(PL_LPS);
    CHECK(v && v->place != PL_NO_ENG && !v->back,
          "loop 00 muted: the stage's note in an engine from its first block, no fade in (never a ghost)");
    to(2, 2);
    v = lane_voice(PL_LPS);
    CHECK(v && v->who == 16 && v->m_last == 4096 && v->place != PL_NO_ENG,
          "loop 00 muted: the stage on its track heard (no ghost)");
    player_lp_mutes(0, 2);               /* (loop 01 soloed) */
    blk(2);
    CHECK(v->m_last == 4096 && v->place != PL_NO_ENG && lane_voice(4) && lane_voice(4)->m_last == 4096,
          "loop 01 soloed: the stage still heard");
    stage_set(0, 2, 0xFF, 0);            /* (+ NEW LOOP: track 18, silenced by the solo) */
    blk(2);
    CHECK(v->who == 18 && v->held && v->m_last == 4096 && v->place != PL_NO_ENG && v->pan == pl_ctxs[1].ch[2].pan,
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
    CHECK(p && p->who == 16 && p->lc == PL_LC_PREVIEW && p->m_last == 4096 && p->place != PL_NO_ENG,
          "the preview on loop 00's muted track: heard");
    player_lp_preview(0);
    player_hear(74, KEYS_IN, 0x7F, 0);
    blk(1);
    int heard = 0;
    for (int k = 0; k < (int)PL_NV; k++)
        heard |= pl_v[k].held && pl_v[k].lc == PL_LC_HEAR && pl_v[k].who == 16 && pl_v[k].m_last == 4096 &&
                 pl_v[k].place != PL_NO_ENG && !pl_v[k].back;
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
 * song's own, the stage's mark gone, so its track's mute makes it a ghost */
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
    player_play_song(0x40, 0, 0);
    blk(1);
    CHECK(k >= 0 && pl_v[k].use == PV_SONG && pl_v[k].who == 0 && pl_v[k].held && pl_v[k].lc == PL_LC_NONE,
          "the song's note in the stage's voice: the song's own, no stage mark");
    player_mutes(1);                     /* (the song's track 0 muted) */
    blk(2);
    CHECK(k >= 0 && pl_v[k].m_last == 0 && pl_v[k].held && pl_v[k].place == PL_NO_ENG && pl_v[k].gh == GH_GHOST,
          "... its track muted: a ghost, silent");
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

/* a note moving into a muted loop: REC hands the stage's held note (heard: the stage's column) over to loop 01, muted;
 * a block on it goes on as that loop's note, a ghost, its engine free */
static void t_hand_muted(void)
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
    CHECK(v && v->held && v->who == 17 && v->place != PL_NO_ENG, "the stage's note held at row 5, on track 17, heard");
    uint32_t len;
    const uint8_t *s = store_stage(&len);
    CHECK(loops_stage_write(1, s, len, KEYS_IN.cart, KEYS_IN.slot, 0) == 1 && loops_set_muted(1, 1) == LP_OK,
          "REC: the stage as loop 01, muted");
    loops_changed(0);
    stage_set(0, 2, 0xFF, 0);
    blk(2);
    CHECK(v->held && v->who == 17 && v->lc == PL_LC_NONE && v->place == PL_NO_ENG && v->gh == GH_GHOST &&
          state().muted == 2, "... its note goes on as muted loop 01's: a ghost");
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
    /* a key on any screen (a key's own lane), a FUSION-free note here; VOICES 8: 8 notes told, a 9th takes an engine: told
     * lost, then the note; the note let go: the ghost back at once into its tail's engine (none free), nothing told; a
     * ghost's own end ends nothing heard */
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
    CHECK(pl_steals && state().ghosts == 1, "... the note taken a ghost");
    int c9 = -1, gv = -1, nb = 0;
    for (int k = 0; k < (int)PL_NV; k++) {
        if (pl_v[k].use == PV_SONG && pl_v[k].lc == PL_LC_OKEYS + 8 && pl_v[k].place != PL_NO_ENG) c9 = pl_v[k].place;
        if (pl_v[k].use == PV_SONG && pl_v[k].lc == PL_LC_OKEYS + lost && pl_v[k].gh == GH_GHOST) gv = k;
    }
    player_lp_off(PL_LP_STAGE_KEYS, 4 + 8, -1);
    n = 0;
    for (int k = 0; k < 600 && state().ghosts; k++) { blk(1); n += tap_read(); nb++; }
    CHECK(n == 1 && !state().ghosts && held_of((int)PL_LPO + lost) && gv >= 0 && c9 >= 0 && pl_v[gv].place == c9 && nb <= 2,
          "... the ninth let go: the ghost back in its tail's engine at once, nothing told but the ninth's end (%d events, %d "
          "blocks)", n, nb);
    player_lp_off(PL_LP_STAGE_KEYS, 4 + lost, -1);
    blk(1);
    n = tap_read();
    CHECK(n == 1 && tgot[0].note == PT_OFF && tgot[0].lane == PL_LPO + lost, "... its own end told (the writer finds no note of it)");
    for (int k = 0; k < 8; k++) player_lp_off(PL_LP_STAGE_KEYS, 4 + k, -1);
    blk(400);
    tap_read();
    /* a smaller count makes a note a ghost: told lost */
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
    /* the keys hold every engine: a lane's note starts as a ghost, given no engine: not told; a key's on and off told */
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
    /* a stage's note taken by the keys (VOICES 8), let go while a ghost, goes on as the loop's at REC's hand-over as its
     * tail in an engine would: its return on the loop's track */
    fresh();
    loop_of(0xF0, 3);                    /* (a sustaining sound) */
    stage_begin();
    stage_cell(1, 0, cell(60, 0x7F), 0);
    stage_cell(3, 0, off_cell(), 0);
    stage_put();
    stage_set(0, 1, 0xFF, 0);
    player_voice_count(8);
    loops_play(0);
    to(1, 1);
    for (int k = 0; k < 8; k++) player_key_on((uint8_t)(48 + k), KEYS_IN, 100);
    to(3, 1);
    pl_voice_t *g = 0;
    for (int k = 0; k < (int)PL_NV; k++)
        if (pl_v[k].use == PV_SONG && pl_v[k].gh == GH_GHOST && !pl_v[k].held && pl_v[k].who == pl_ln_who(PL_LPS))
            g = &pl_v[k];
    CHECK(g != 0, "... a stage's note taken by the keys, let go while a ghost");
    player_lp_stage_end(0);
    blk(1);
    CHECK(g && g->gh == GH_GHOST && g->who == pl_ln_who(0) && pl_ln_of(g, 0, pl_ln[0].gen), "... REC's hand-over: the ghost "
          "moved to the loop's lane with it");
    player_keys_off();
    player_stop();
    blk(1);
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
    t_vol_glide();
    t_fusion_vol();
    t_legato();
    t_fusion();
    t_mutes();
    RULES(t_mute_ghosts());
    t_mute_tb();
    RULES(t_mute_duck());
    t_mute_bar_take();
    t_mute_fusion_column();
    RULES(t_lane_ghosts());
    RULES(t_solo_full());
    t_requests();
    t_shift();
    t_cut();
    t_clock();
    t_once();
    t_now();
    t_held();
    t_jump();
    t_length();
    t_mono();
    t_column();
    t_off_keys();
    t_steal();
    t_silent();
    t_hand();
    t_hand_muted();
    t_preview();
    t_edits();
    RULES(t_tap());
    printf("lanes: %d checks, %d failed\n", checks, fails);
    return fails ? 1 : 0;
}
