/* SPDX-License-Identifier: GPL-3.0-only
 * Copyright (C) 2026 Vasily Kazantsev */
/* DJ's record and stand, and the decks in the player (dj.h; specs/files/dj.md, specs/ui/screens/dj.md).
 *
 * The player plays both decks (player.h's DJ part): the main on the song's replay, tracks 0-15, the other on
 * the second replay, tracks 16-31, each with its song's own effects and a DJ FILTER, VOLUME and the
 * crossfader on its sound and sends, the shared CHORUS, DELAY, REVERB and LIMITER with DJ's FX setup. Here:
 * the stand (DJ's record) handed to the player when DJ goes live, the main loop's side of every deck request,
 * DJ's mix (player_dj_set) from the stand, and each deck's state as the screen reads it. */
#include <string.h>
#include "../plat.h"
#include "dj.h"
#include "player.h"
#include "rec.h"
#include "../store/store.h"
#include "../spare.h"

uint8_t DJ_REC[DJ_REC_LEN];

/* DJ's FX setup with no record yet (specs/files/dj.md): a new song's, ROUTING SENDS, DELAY on at 1/8.,
 * PING-PONG ON, FEEDBACK 40 %, MIX 30 %; REVERB on at SIZE 70 %, MIX 35 % (specs/files/songs.md's bytes) */
static const uint8_t DJ_SETUP[64] = {
    1,                                   /* ROUTING: SENDS */
    0x10, 0xA3, 0x54, 0x26, 0xF7, 0xFF,  /* GATE FILTER LO-FI SENDS CHORUS DELAY REVERB DRIVE LIMITER */
    0xE0, 0x00,                          /* DELAY, REVERB, LIMITER on */
    0, 0, 4, 1, 60, 0, 0,                /* METRONOME: FOLLOW, 4 beats, BEEP, 60 % */
    0, 1, 100, 20,                       /* 0x90 GATE */
    64, 30,                              /* 0x94 FILTER */
    30, 50,                              /* 0x96 DRIVE */
    6, 21, 20,                           /* 0x98 LO-FI */
    0, 30, 50, 50,                       /* 0x9B CHORUS */
    7, 40, 60, 1, 30,                    /* 0x9F DELAY: 1/8., FEEDBACK 40, TONE 60, PING-PONG ON, MIX 30 */
    70, 40, 10, 35,                      /* 0xA4 REVERB: SIZE 70, DAMP 40, PRE-DELAY 10, MIX 35 */
    11, 9,                               /* 0xA8 LIMITER */
    0, 60, 13};                          /* 0xAA DUCK */

const uint8_t *dj_fx_default(void) { return DJ_SETUP; }
static void deck_default(uint8_t *d)     /* an empty deck, its FILTER on at OFF, its filter's LFO at its first setup */
{
    memset(d, 0, 24);
    d[DK_SONG] = DJ_NO_SONG;
    d[DK_FLAGS] = DF_FILTER_ON;         /* (at OFF: its knob filters as soon as it turns) */
    d[DK_SOLO] = 0xFF;                  /* no track soloed */
    d[DK_LFO + 1] = 2;                  /* LFO: TRIANGLE, the first range, 1/2 */
    d[DK_LFO + 3] = 6;
}
uint32_t dj_record_default(uint8_t *p)
{
    memset(p, 0, DJ_REC_LEN);
    p[2] = 30;                          /* FILTER RESONANCE 30 % */
    p[6] = 1;                           /* SYNC on; the crossfader all the way to the left (byte 5: 0) */
    memcpy(p + 8, DJ_SETUP, 64);
    p[81] = 2; p[82] = 3; p[83] = 6;    /* the crossfader's LFO: TRIANGLE, MIRROR, 1/2 */
    p[85] = 2; p[87] = 6;               /* the FX LFO */
    deck_default(p + 88);
    deck_default(p + 112);
    return DJ_REC_LEN;
}
static uint8_t dj_loaded;
static uint32_t dj_nloads;
void dj_ensure(void) { if (!dj_loaded) dj_load(); }
uint32_t dj_loads(void) { return dj_nloads; }
void dj_load(void)
{
    uint32_t len;
    uint8_t ver;
    dj_loaded = 1;
    dj_nloads++;
    const uint8_t *p = store_get(REC_DJ, 0, 0, &len, &ver);
    dj_record_default(DJ_REC);
    if (p && ver == 1 && len >= DJ_REC_LEN) memcpy(DJ_REC, p, DJ_REC_LEN);
    DJ_REC[0] = 0;                      /* (DJ's VOICES and PREVIEW are gone: their bytes kept clear, unused) */
    DJ_REC[6] &= (uint8_t)~2u;
}
uint8_t *dj_deck(int d) { return DJ_REC + 88 + 24 * (d & 1); }
int dj_deck_bpm10(int d) { uint8_t *k = dj_deck(d); return k[DK_BPM] | k[DK_BPM + 1] << 8; }
void dj_deck_set_bpm10(int d, int b) { uint8_t *k = dj_deck(d); k[DK_BPM] = (uint8_t)b; k[DK_BPM + 1] = (uint8_t)(b >> 8); }
int dj_xf(void) { return DJ_REC[5] > 32 ? 32 : DJ_REC[5]; }
void dj_set_xf(int v) { DJ_REC[5] = (uint8_t)(v < 0 ? 0 : v > 32 ? 32 : v); }
int dj_sync(void) { return DJ_REC[6] & 1; }
int dj_sync_mult(void) { int m = DJ_REC[6] >> 2 & 3; return m > 2 ? 0 : m; }
int dj_has_player(void) { return 1; }
uint8_t *dj_lfo_bytes(int i) { return i == DJ_LFO_FADER ? DJ_REC + 80 : i == DJ_LFO_FX ? DJ_REC + 84 : dj_deck(i - 1) + DK_LFO; }

/* ------------------------------------------------------------ the decks in the player --- */
void (*dj_read_state)(player_state_t *s);       /* the screen's way to read the player (its frame's), or none */
static uint8_t live;                    /* the player has the decks: DJ is what plays */
static uint32_t live_at;                /* the player's halves when they went live */
static uint8_t slide[2];                /* a FILTER key held: its filter sliding there */
static uint8_t lfo_gen[DJ_NLFO];        /* each LFO's starts (another sets off afresh) */
static uint8_t swaps;                   /* the SWAPs since the decks went live (the player counts them too) */
static void *mem;                       /* the second deck's memory, borrowed from the spare zone (spare.h) */
static uint32_t mem_at;                 /* the player's halves when it was handed over */

/* the second deck's memory: borrowed when the decks go live (the zone always has it: a borrow that fits never
 * refuses), given back once the player has let it go (dj_poll) */
static void mem_get(void)
{
    player_state_t st;
    if (!mem)
        mem = spare_borrow(player_dj_mem_size());
    if (!mem)
        return;
    player_state(&st, 0);
    mem_at = st.halves;
    player_dj_mem(mem);
}
int dj_second_ok(void) { return mem != 0; }
void dj_poll(void)
{
    player_state_t st;
    if (!mem)
        return;
    player_state(&st, 0);
    if (st.dj_mem || st.halves - mem_at <= 2u || (live && st.what == PL_DJ))
        return;                         /* (still the player's, or about to be) */
    spare_return();
    mem = 0;
}

static void read_ps(player_state_t *s)
{
    if (dj_read_state)
        dj_read_state(s);
    else
        player_state(s, 0);
}
static int has(int d) { return dj_deck(d)[DK_SONG] != DJ_NO_SONG; }
uint16_t dj_song_own10(uint8_t song)     /* a song's own tempo, BPM x 10 */
{
    rec_song_t s;
    if (song == DJ_NO_SONG || rec_song(song, &s) || !s.ticks) return 1200;
    return (uint16_t)(((uint32_t)s.tempo * 2400u / (s.ticks * (s.rpb ? s.rpb : 4u)) + 5u) / 10u);
}
static uint32_t mute_mask(int d)
{
    const uint8_t *k = dj_deck(d);
    return k[DK_SOLO] != 0xFF ? ~(1u << (k[DK_SOLO] & 15)) & 0xFFFFu : (unsigned)(k[DK_MUTES] | k[DK_MUTES + 1] << 8);
}

void dj_mix(void)                       /* DJ's mix to the player, as the stand has it */
{
    player_dj_t m;
    memset(&m, 0, sizeof m);
    m.xf = (uint8_t)dj_xf();
    m.sharp = DJ_REC[1] & 1;
    m.reso = DJ_REC[2] > 100 ? 100 : DJ_REC[2];
    m.sends_filtered = !DJ_REC[3];
    m.sync = (uint8_t)(dj_sync() && has(0) && has(1));
    m.sync_mult = (uint8_t)dj_sync_mult();
    for (int d = 0; d < 2; d++) {
        const uint8_t *k = dj_deck(d);
        m.vol[d] = (int8_t)k[DK_VOL];
        m.filt[d] = (int8_t)k[DK_FILTER];
        m.filt_on[d] = (k[DK_FLAGS] & DF_FILTER_ON) != 0;
        m.filt_slide[d] = slide[d];
        m.key[d] = (int8_t)k[DK_KEY];
    }
    m.lfo_fx = DJ_REC[4] > 2 ? 0 : DJ_REC[4];
    m.mix_set[0] = DJ_FX[16 + 0x0E];    /* CHORUS' MIX */
    m.mix_set[1] = DJ_FX[16 + 0x13];    /* DELAY's */
    m.mix_set[2] = DJ_FX[16 + 0x17];    /* REVERB's */
    for (int i = 0; i < DJ_NLFO; i++) {
        const uint8_t *b = dj_lfo_bytes(i);
        m.lfo[i].on = b[0] & 1;
        m.lfo[i].rev = b[0] >> 1 & 1;
        m.lfo[i].shape = b[1] > 5 ? 2 : b[1];
        m.lfo[i].range = b[2] & 3;
        m.lfo[i].len = b[3] > 8 ? 6 : b[3];
        m.lfo[i].gen = lfo_gen[i];
    }
    m.swaps = swaps;
    player_dj_set(&m);
}

/* the stand's loop or REPEAT on a deck into the player (files/dj.md: bytes 11-15, the flags' REPEAT PATTERN) */
static void loop_to_player(int d)
{
    const uint8_t *k = dj_deck(d);
    if (k[DK_FLAGS] & DF_REPEAT_PATTERN) {
        player_deck_loop(d, PL_DLOOP_PATTERN, k[DK_POS], 0, 0, 0);
        return;
    }
    if (!k[DK_LOOP]) {
        player_deck_loop(d, PL_DLOOP_NONE, 0, 0, 0, 0);
        return;
    }
    rec_song_t s;
    uint32_t rpb = !rec_song(k[DK_SONG], &s) && s.rpb ? s.rpb : 4u, rows = (uint32_t)k[DK_LOOP_LEN] * rpb / 2u;
    player_deck_loop(d, PL_DLOOP_ROWS, k[DK_LOOP_POS], k[DK_LOOP_ROW], (uint16_t)(rows ? rows : 1u), 0);
}

static void tempo_to_player(int d, int bpm10)
{
    uint8_t song = dj_deck(d)[DK_SONG];
    uint32_t own = dj_song_own10(song), share = (uint32_t)bpm10 * 65536u / (own ? own : 1200u);
    int fixed = d == 1 && dj_sync() && has(0);   /* (synced: the main's BPM, its song's tempo commands overridden) */
    player_deck_tempo(d, share, (uint16_t)(fixed ? bpm10 : 0));
}

/* the decks go live in the player, as the stand has them, paused: DJ's first start. What played (a song, the
 * pattern loop, the loops) stops there, its sound cut (the player's side: every voice out, the effects' tails
 * emptied), and DJ's FX setup takes the shared effects. 0: DJ's memory could not be had (the edits hold it: SAVE
 * first) */
PLAT_COLD int dj_on(int on)
{
    player_state_t st;
    if (!on) {
        if (live) player_dj(0, 0);     /* (the player gives DJ's memory up: dj_poll returns it) */
        live = 0;
        return 1;
    }
    if (dj_live()) return 1;
    player_state(&st, 0);
    dj_ensure();
    live_at = st.halves;
    mem_get();
    if (!mem) return 0;
    swaps = 0;                          /* (the player's count starts again too) */
    player_dj(1, DJ_FX);
    live = 1;
    for (int d = 0; d < 2; d++) {
        const uint8_t *k = dj_deck(d);
        player_deck_load(d, k[DK_SONG]);
        if (!has(d)) continue;
        player_deck_place(d, k[DK_POS], k[DK_ROW]);
    }
    for (int d = 0; d < 2; d++) {
        if (!has(d)) continue;
        tempo_to_player(d, d == 1 && dj_sync() && has(0) ? dj_deck_bpm10(0) * (dj_sync_mult() == 2 ? 2 : 1) /
                        (dj_sync_mult() == 1 ? 2 : 1) : dj_deck_bpm10(d));
        player_deck_mutes(d, mute_mask(d));
        loop_to_player(d);
    }
    for (int i = 0; i < DJ_NLFO; i++)   /* (an LFO that was on sets off afresh as the decks play) */
        if (dj_lfo_bytes(i)[0] & 1) lfo_gen[i]++;
    dj_mix();
    return 1;
}
int dj_live(void)
{
    player_state_t st;
    if (!live) return 0;
    player_state(&st, 0);
    if (st.what != PL_DJ && st.halves - live_at > 2u) live = 0;   /* (a song, a pattern or the loops started;
                                                                   * the request takes a half to arrive) */
    return live;
}
/* opening DJ changes nothing of what plays: the decks are live only if they are what plays (or played last);
 * else DJ stays apart (no sound, the effects untouched) till its first start (dj_on) */
void dj_enter(void)
{
    player_state_t st;
    player_state(&st, 0);
    if (st.what == PL_DJ) { live = 1; live_at = st.halves; return; }
    live = 0;
}

static int stand_state(int d, dj_deck_state_t *s)
{
    const uint8_t *k = dj_deck(d);
    memset(s, 0, sizeof *s);
    s->song = k[DK_SONG];
    s->speed = 6;
    s->bpm10 = (uint16_t)dj_deck_bpm10(d);
    s->pos = k[DK_POS];
    s->row = k[DK_ROW];
    s->ended = (k[DK_FLAGS] & DF_AT_END) != 0;
    return 0;
}
int dj_crossed(const player_state_t *ps) { return live && ((ps->dj_swaps ^ swaps) & 1u); }
int dj_deck_state(int d, dj_deck_state_t *s)
{
    player_state_t ps;
    if (!live) return stand_state(d, s);
    read_ps(&ps);
    if (ps.what != PL_DJ) return stand_state(d, s);
    memset(s, 0, sizeof *s);
    s->song = dj_deck(d)[DK_SONG];
    d ^= dj_crossed(&ps);               /* (a SWAP the player has not made yet: its decks the other way round) */
    s->playing = ps.deck[d].playing;
    s->armed = ps.deck[d].armed;
    s->ended = ps.deck[d].ended;
    s->pos = ps.deck[d].pos;
    s->pat = ps.deck[d].pat;
    s->row = ps.deck[d].row;
    s->rows = ps.deck[d].rows;
    s->tick = ps.deck[d].tick;
    s->speed = ps.deck[d].speed;
    s->bpm10 = ps.deck[d].bpm10;
    s->rows_played = ps.deck[d].rows_played;
    s->stop = ps.deck[d].stop;
    s->stopped = ps.deck[d].stopped;
    s->loop = ps.deck[d].loop;
    s->loop_pos = ps.deck[d].loop_pos;
    s->loop_row = ps.deck[d].loop_row;
    s->loop_rows = ps.deck[d].loop_rows;
    s->roll = ps.deck[d].roll;
    s->halves = ps.halves;
    return 1;
}
void dj_deck_load(int d, uint8_t song)
{
    (void)song;
    if (!live) return;
    player_deck_load(d, dj_deck(d)[DK_SONG]);
    loop_to_player(d);
}
void dj_deck_play(int d, int on)
{
    const uint8_t *k = dj_deck(d);
    if (on && !live) dj_on(1);
    if (!live) return;
    player_deck_play(d, on, k[DK_POS], k[DK_ROW]);
}
void dj_deck_arm(int d)                 /* the deck starts on the other's next bar line (it waits armed) */
{
    const uint8_t *k = dj_deck(d);
    if (!live) dj_on(1);
    player_deck_arm(d, k[DK_POS], k[DK_ROW]);
}
void dj_deck_place(int d, int pos, int row) { if (live) player_deck_place(d, (uint8_t)pos, (uint8_t)row); }
void dj_deck_hand(int d, int pos, int row, int ms)
{
    if (!live) dj_on(1);                /* (the decks live: the hand is heard) */
    if (live) player_deck_hand(d, (uint8_t)pos, (uint8_t)row, (uint32_t)(ms < 0 ? 0 : ms));
}
void dj_deck_tempo(int d, int bpm10) { if (live && has(d)) tempo_to_player(d, bpm10); }
void dj_deck_volume(int d, int db) { (void)d; (void)db; if (live) dj_mix(); }
void dj_deck_filter(int d, int cut, int on) { (void)d; (void)cut; (void)on; if (live) dj_mix(); }
void dj_filter_slide(int d, int on) { slide[d & 1] = (uint8_t)(on != 0); if (live) dj_mix(); }
void dj_deck_key(int d, int semis) { dj_deck(d)[DK_KEY] = (uint8_t)(int8_t)(semis < -6 ? -6 : semis > 6 ? 6 : semis); if (live) dj_mix(); }
void dj_deck_mutes(int d, uint32_t mask) { (void)mask; if (live) player_deck_mutes(d, mute_mask(d)); }
void dj_lfo_restart(int i) { lfo_gen[i]++; if (live) dj_mix(); }
void dj_swap(void)
{
    uint8_t t = slide[0];               /* (what is kept here by side goes with its deck) */
    slide[0] = slide[1];
    slide[1] = t;
    t = lfo_gen[DJ_LFO_FILT_L];
    lfo_gen[DJ_LFO_FILT_L] = lfo_gen[DJ_LFO_FILT_R];
    lfo_gen[DJ_LFO_FILT_R] = t;
    swaps++;
    if (live) player_dj_swap(swaps);
}
void dj_stop(int d, int kind, int down, int stay) { if (live) player_deck_stop(d, kind, down, stay); }
/* a deck's loop: pad 0 none, 1 REPEAT PATTERN, 2-4 REPEAT the bars around (rows long from pos / row), 5-8 a free
 * loop; into the stand and the player; roll: held (let go: dj_roll_end) */
void dj_loop(int d, int pad, int pos, int row, int rows, int roll)
{
    uint8_t *k = dj_deck(d);
    rec_song_t s;
    uint32_t rpb = !rec_song(k[DK_SONG], &s) && s.rpb ? s.rpb : 4u;
    k[DK_FLAGS] &= (uint8_t)~DF_REPEAT_PATTERN;
    k[DK_LOOP] = 0;
    if (pad == 1) {
        k[DK_FLAGS] |= DF_REPEAT_PATTERN;
        if (live) player_deck_loop(d, PL_DLOOP_PATTERN, (uint8_t)pos, 0, 0, roll);
        return;
    }
    if (pad >= 2) {
        k[DK_LOOP] = (uint8_t)pad;
        k[DK_LOOP_POS] = (uint8_t)pos;
        k[DK_LOOP_ROW] = (uint8_t)row;
        k[DK_LOOP_LEN] = (uint8_t)((uint32_t)rows * 2u / rpb > 255u ? 255u : (uint32_t)rows * 2u / rpb);
        k[DK_REPEAT] = pad <= 4;
        if (live) player_deck_loop(d, PL_DLOOP_ROWS, (uint8_t)pos, (uint8_t)row, (uint16_t)rows, roll);
        return;
    }
    if (live) player_deck_loop(d, PL_DLOOP_NONE, 0, 0, 0, 0);
}
void dj_roll_end(int d)
{
    uint8_t *k = dj_deck(d);
    k[DK_LOOP] = 0;
    k[DK_FLAGS] &= (uint8_t)~DF_REPEAT_PATTERN;
    if (live) player_deck_roll_end(d);
}
