/* SPDX-License-Identifier: GPL-3.0-only
 * Copyright (C) 2026 Vasily Kazantsev */
/* MIDI (specs/connections.md, specs/files/midi.md): see midi.h. What comes in is read in the main loop
 * (midi_poll); notes and the learned controls reach the UI as the FM-1's own input (midi_merge), so they act
 * on every screen as the FM-1's keys, knobs and buttons do. What goes out: the keys from the main loop, the
 * tracks' notes and the clock from the audio context (plat_midi_tx takes both). */
#include <string.h>
#include "midi.h"
#include "../play/player.h"
#include "../play/rec.h"
#include "../store/store.h"
#include "../store/seed.h"
#include "../ui/ui_core.h"
#include "../ui/ui_data.h"

/* (a platform without MIDI, the host tests' fakes, links plat_weak.c's no-ops, never defined here) */

#define VER_MIDIMAP 1
#define LEARN_MS 1000u                  /* what comes within a second of the first message tells the kind */
#define CLOCK_LOST_MS 500u
#define CO_FULL (600u * PLAT_RATE)      /* CLOCK OUT: a clock every CO_FULL / (BPM x 10 x 24) frames */

static midi_info_t info;

/* ------------------------------------------------------------ the MIDI map record --- */
static uint8_t map[4 + 4 * MIDI_MAP_MAX];
static int nmap;

static void map_load(void)
{
    uint32_t len = 0;
    uint8_t ver = 0;
    const uint8_t *p = store_get(REC_MIDIMAP, 0, 0, &len, &ver);
    nmap = 0;
    if (!p || ver != VER_MIDIMAP || len < 4u)
        return;
    nmap = p[0] > MIDI_MAP_MAX ? MIDI_MAP_MAX : p[0];
    if (4u + 4u * (uint32_t)nmap > len)
        nmap = (int)((len - 4u) / 4u);
    memcpy(map + 4, p + 4, 4u * (uint32_t)nmap);
}

static int map_save(void)
{
    map[0] = (uint8_t)nmap;
    map[1] = map[2] = map[3] = 0;
    return ui_edit_put(REC_MIDIMAP, 0, 0, VER_MIDIMAP, map, 4u + 4u * (uint32_t)nmap);
}

static void map_get(int i, midi_map_t *m)
{
    const uint8_t *e = map + 4 + 4 * i;
    m->ch = e[0] & 15u;
    m->note = (e[0] >> 4) & 3u;
    m->num = e[1] & 127u;
    m->ctl = e[2];
    m->kind = e[3];
}

static int map_find(int note, int ch, int num)
{
    int i;
    for (i = 0; i < nmap; i++) {
        const uint8_t *e = map + 4 + 4 * i;
        if ((e[0] & 15) == ch && ((e[0] >> 4) & 3) == note && e[1] == num)
            return i;
    }
    return -1;
}

int midi_map_count(void) { map_load(); return nmap; }
int midi_map_at(int i, midi_map_t *m)
{
    map_load();
    if (i < 0 || i >= nmap)
        return -1;
    map_get(i, m);
    return 0;
}

static uint8_t abs_last[MIDI_MAP_MAX];  /* an absolute knob's last value (0xFF: none yet) */

int midi_map_delete(int i)
{
    map_load();
    if (i < 0 || i >= nmap)
        return -1;
    memmove(map + 4 + 4 * i, map + 4 + 4 * (i + 1), 4u * (uint32_t)(nmap - i - 1));
    nmap--;
    memset(abs_last, 0xFF, sizeof abs_last);
    return map_save();
}

/* ---------------------------------------------------------------------- names --- */
static const txt_t KNOB_NAME[7] = {TX("SELECT"), TX("ALGORITHM"), TX("K1"), TX("K2"), TX("K3"), TX("K4"), TX("PRESETS")};
static const txt_t KIND_NAME[6] = {TX("ABSOLUTE"), TX("RELATIVE"), TX("RELATIVE"), TX("RELATIVE"), TX("MOMENTARY"), TX("TOGGLE")};

void midi_ctl_name(char *dst, int ctl)
{
    if (ctl < MC_MASTER) { strcpy(dst, T(KNOB_NAME[ctl])); return; }
    if (ctl == MC_MASTER) { strcpy(dst, T("MASTER")); return; }
    if (ctl < MC_KEY) { strcpy(dst, T(UI_BNAME[ctl - MC_BUTTON])); return; }
    if (ctl < MC_COUNT) {
        char n[8];
        ui_note_name(n, 53 + ctl - MC_KEY);             /* key 0 is F3 */
        ui_fmt(dst, 16, T("KEY %s"), n);
        return;
    }
    strcpy(dst, "?");
}

void midi_src_name(char *dst, const midi_map_t *m)
{
    const char *t = m->note ? T("NOTE # CH #") : T("CC # CH #");
    ui_fmt(dst, 16, "%s", ui_txt_v(ui_txt_v(t, ui_dec(m->num)), ui_dec(m->ch + 1)));
}

const char *midi_kind_name(int kind) { return kind >= 0 && kind < 6 ? T(KIND_NAME[kind]) : "?"; }

/* ------------------------------------------------- what reaches the FM-1's input --- */
/* Held, fresh (pressed since the last merge) and released-while-fresh: a press and its release that come
 * between two passes are still seen as a press, then a release. */
static uint32_t btn_held, btn_fresh, btn_rel;
static uint32_t key_held, key_fresh, key_rel;
static uint32_t key_shown;              /* the keys the last merge showed held by MIDI (their velocity counts) */
static int16_t knob_add[K_COUNT];
static int16_t knob_pre[K_COUNT];       /* the clicks before the first press since the last merge (knob_pre_on) */
static uint8_t knob_pre_on;
static uint8_t key_vel[27];             /* a key held by a MIDI note: its velocity */
static uint8_t note_key[128];           /* a MIDI note on: 1 + the key it holds; 0xFF: played past the keys */
static uint8_t sustained[16];           /* notes whose note off waits for the sustain pedal */
static uint8_t pedal;

static void press(uint32_t *held, uint32_t *fresh, uint32_t *rel, int i, int down)
{
    uint32_t b = 1u << i;
    if (down) {
        if (!knob_pre_on) {                     /* the clicks so far came before this press */
            memcpy(knob_pre, knob_add, sizeof knob_pre);
            knob_pre_on = 1;
        }
        *held |= b;
        *fresh |= b;
        *rel &= ~b;
    } else if (*held & b) {
        if (*fresh & b)
            *rel |= b;                  /* (seen once more as held, then let go) */
        else
            *held &= ~b;
    }
}

void midi_merge(plat_input_t *in)
{
    int k;
    in->buttons |= btn_held;
    in->notes |= key_held;
    key_shown = key_held;
    for (k = 0; k < K_COUNT; k++) {             /* (no press since the last merge: every click came before one) */
        in->knob[k] = (int16_t)(in->knob[k] + knob_add[k]);
        in->knob_pre[k] = (int16_t)(in->knob_pre[k] + (knob_pre_on ? knob_pre[k] : knob_add[k]));
        knob_add[k] = 0;
    }
    knob_pre_on = 0;
    btn_held &= ~btn_rel;
    key_held &= ~key_rel;
    btn_fresh = btn_rel = key_fresh = key_rel = 0;
}

int midi_key_vel(int n) { return n >= 0 && n < 27 && ((key_held | key_shown) >> n & 1u) ? key_vel[n] : 0; }

/* ------------------------------------------------- MIRROR: the panel from a page ---
 * (specs/connections.md#mirror-the-panel-from-a-page) The PT/FM controller's mirror messages on MIRROR's
 * channel, from USB, are the FM-1's own panel; the controller's hello switches it on (mirror mode) or off
 * (general mode), as does a USB bus reset. What MIRROR holds is let go at every hello. */
static struct {
    uint8_t on, ch;
    uint32_t btn, key;                  /* the buttons and keys MIRROR holds */
    uint32_t resets;                    /* plat_usb_resets as last seen */
} mir;

static void mirror_let_go(void)
{
    for (int i = 0; i < B_COUNT; i++)
        if (mir.btn >> i & 1u) press(&btn_held, &btn_fresh, &btn_rel, i, 0);
    for (int i = 0; i < 27; i++)
        if (mir.key >> i & 1u) press(&key_held, &key_fresh, &key_rel, i, 0);
    mir.btn = mir.key = 0;
}

static void mirror_set(int on, int ch)
{
    int was = mir.on;
    mirror_let_go();
    mir.on = (uint8_t)on;
    mir.ch = (uint8_t)(ch & 15);
    if (on != was) ui_toast(on ? T("MIRROR ON") : T("MIRROR OFF"));
}

int midi_mirror(int *ch)
{
    if (ch) *ch = mir.ch;
    return mir.on;
}

/* the controller's hello, F0 7D 50 54 43 02 <mode> <version> <channel> F7 (version 1's has no channel: the
 * modes' own, mirror 16): 1 taken. Its question (43 01) is not ours to answer. */
int midi_sysex(const uint8_t *msg, uint32_t n)
{
    if (n < 7 || msg[0] != 0xF0 || msg[1] != 0x7D || msg[2] != 0x50 || msg[3] != 0x54 || msg[4] != 0x43 ||
        msg[n - 1] != 0xF7)
        return 0;
    if (msg[5] != 0x02 || n < 9 || msg[6] > 1)
        return 1;                       /* (the question, or not a hello we know: taken, nothing done) */
    mirror_set(msg[6] == 0, n >= 10 ? msg[8] : msg[6] == 0 ? 15 : 0);
    return 1;
}

/* a message on MIRROR's channel from USB: the panel */
static void mirror_msg(uint8_t type, uint8_t d1, uint8_t d2)
{
    if (type == 9u || type == 8u) {     /* a key: note 53 + k */
        int k = (int)d1 - 53, down = type == 9u && d2;
        if (k < 0 || k >= 27) return;
        key_vel[k] = 0;                 /* (it plays as the FM-1's key: KEYS VOLUME) */
        press(&key_held, &key_fresh, &key_rel, k, down);
        if (down) mir.key |= 1u << k; else mir.key &= ~(1u << k);
        return;
    }
    if (type != 11u) return;
    if (d1 >= 102u && d1 < 102u + B_COUNT) {    /* a button: CC 102 + id */
        int b = d1 - 102, down = d2 > 0;
        press(&btn_held, &btn_fresh, &btn_rel, b, down);
        if (down) mir.btn |= 1u << b; else mir.btn &= ~(1u << b);
    } else if (d1 >= 20u && d1 < 20u + K_COUNT) {   /* a knob: CC 20 + id, relative */
        int clicks = d2 < 64u ? d2 : d2 > 64u ? (int)d2 - 128 : 0;
        knob_add[d1 - 20] = (int16_t)(knob_add[d1 - 20] + clicks);
    } else if (d1 == 7u) {              /* MASTER */
        plat_master_set((uint16_t)(d2 * 1023u / 127u));
    }
}

/* ----------------------------------------------------------------- MIDI OUT --- */
static uint8_t keys_sent[128];          /* a key's note sent on: 1 + its channel, for its end */

static void tx3(uint8_t a, uint8_t b, uint8_t c)
{
    uint8_t m[3] = {a, b, c};
    if (plat_midi_tx(m) == 0)
        info.out++;
}
static void tx_at(uint8_t a, uint8_t b, uint8_t c, uint32_t frame)   /* (the audio context: when its frame is heard) */
{
    uint8_t m[3] = {a, b, c};
    if (plat_midi_tx_at(m, (int)frame) == 0)
        info.out++;
}

void midi_out_key(uint8_t note, uint8_t vel, int on)
{
    int mode = ui_midi_set[S_MIDI_OUT];                 /* 0 OFF, 1 KEYS, 2 ALL */
    note &= 127u;
    if (on) {
        uint8_t ch = (uint8_t)((ui_midi_set[S_MIDI_OUT_CH] - 1) & 15);
        if (!mode)
            return;
        tx3((uint8_t)(0x90u | ch), note, vel ? vel : 1u);
        keys_sent[note] = (uint8_t)(ch + 1u);
    } else if (keys_sent[note]) {
        tx3((uint8_t)(0x80u | (keys_sent[note] - 1u)), note, 0);   /* (on the channel it went on) */
        keys_sent[note] = 0;
    }
}

/* The tracks' notes (ALL): the player tells, in the audio context, each note a track starts and ends. */
static volatile uint8_t out_all;        /* MIDI OUT is ALL (the main loop sets it) */
static volatile uint8_t tracks_flush;   /* the main loop asks: every track's note sent ends */
static uint8_t track_sent[16];          /* audio context: each track's note sent, + 1 */

static void track_note(uint8_t t, uint8_t note, uint8_t vel)
{
    uint32_t f = player_block_frame();  /* (sent as the note's block is heard) */
    if (t >= 16u)
        return;
    if (track_sent[t]) {
        tx_at((uint8_t)(0x80u | t), (uint8_t)(track_sent[t] - 1u), 0, f);
        track_sent[t] = 0;
    }
    if (vel && out_all && note < 128u) {
        tx_at((uint8_t)(0x90u | t), note, vel > 127u ? 127u : vel, f);
        track_sent[t] = (uint8_t)(note + 1u);
    }
}

/* -------------------------------------------------------------- the clock out --- */
static volatile uint16_t co_bpm10;      /* the tempo of what plays or played last (the main loop's) */
static volatile uint8_t co_on;          /* CLOCK OUT on and SYNC INTERNAL */
static uint8_t co_q[8][3];              /* START / STOP / CONTINUE / the song position, for the audio context */
static volatile uint8_t co_w, co_r;
static uint32_t co_phase;               /* audio context */

static void co_post(uint8_t a, uint8_t b, uint8_t c)
{
    uint8_t w = co_w;
    if ((uint8_t)(w - co_r) >= 8u)
        return;
    co_q[w & 7u][0] = a;
    co_q[w & 7u][1] = b;
    co_q[w & 7u][2] = c;
    __asm__ volatile("" ::: "memory");
    co_w = (uint8_t)(w + 1u);
}

void midi_audio(int frames)
{
    int t;
    if (tracks_flush) {
        for (t = 0; t < 16; t++)
            track_note((uint8_t)t, 0, 0);
        tracks_flush = 0;
    }
    if (!co_on) {
        co_r = co_w;
        return;
    }
    while (co_r != co_w) {
        uint8_t *m = co_q[co_r & 7u];
        if (plat_midi_tx_at(m, 0) == 0)
            info.out++;
        if (m[0] == 0xFAu || m[0] == 0xFBu)
            co_phase = CO_FULL;         /* START, CONTINUE: the next clock now */
        __asm__ volatile("" ::: "memory");
        co_r = (uint8_t)(co_r + 1u);
    }
    {                                   /* each clock at the frame it falls on (sent as that frame is heard) */
        uint32_t inc = (uint32_t)co_bpm10 * 24u, j = 0, need;
        if (!inc)
            return;
        for (;;) {
            need = co_phase >= CO_FULL ? 0u : (CO_FULL - co_phase + inc - 1u) / inc;
            if (j + need > (uint32_t)frames) {
                co_phase += ((uint32_t)frames - j) * inc;
                break;
            }
            j += need;
            co_phase += need * inc - CO_FULL;
            tx_at(0xF8u, 0, 0, j);
        }
    }
}

/* the song position in sixteenths (MIDI beats) of (pos, row), and back */
static int spp_of(uint8_t song, int pos, int row)
{
    rec_song_t s;
    uint32_t rows = 0;
    int i;
    if (rec_song(song, &s))
        return 0;
    for (i = 0; i < pos && i < s.len; i++) {
        uint8_t nt;
        uint16_t r;
        if (!rec_pattern_size(s.order[i], &nt, &r))
            rows += r;
    }
    rows += (uint32_t)row;
    rows = rows * 4u / (s.rpb ? s.rpb : 4u);
    return rows > 16383u ? 16383 : (int)rows;
}

static int place_of(uint8_t song, int spp, uint8_t *pos, uint8_t *row)
{
    rec_song_t s;
    uint32_t rows;
    int i;
    if (rec_song(song, &s))
        return -1;
    rows = (uint32_t)spp * (s.rpb ? s.rpb : 4u) / 4u;
    for (i = 0; i < s.len; i++) {
        uint8_t nt;
        uint16_t r = 64;
        if (rec_pattern_size(s.order[i], &nt, &r))
            r = 64;
        if (rows < r) {
            *pos = (uint8_t)i;
            *row = (uint8_t)rows;
            return 0;
        }
        rows -= r;
    }
    return -1;                          /* past the song's end */
}

/* --------------------------------------------------------------- the clock in --- */
static uint32_t ck_t[25];               /* the last clocks' times */
static int ck_n, ck_i;
static uint32_t ck_last;
static uint16_t ck_bpm10;
static uint8_t ck_lost;
static uint32_t ck_count;               /* clocks since the master's START or CONTINUE (what follows waits for 1) */
static uint32_t ck_per_us;              /* the time between two, measured over the last ones */
static int spp_pending = -1;            /* a song position come in while stopped: CONTINUE goes there */

/* each clock: its count and arrival to the player, which keeps what follows the clock in place on them
 * (specs/connections.md#clock); the tempo from the last 25 (the BPM shown, and the pace between clocks) */
static void clock_in(uint32_t ms)
{
    int first;
    ck_t[ck_i] = ms;
    ck_i = (ck_i + 1) % 25;
    if (ck_n < 25)
        ck_n++;
    ck_last = ms | 1u;
    ck_lost = 0;
    ck_count++;
    if (ck_n >= 7) {
        uint32_t span, n, bpm10;
        first = (ck_i + 25 - ck_n) % 25;    /* the oldest kept */
        span = ms - ck_t[first];
        n = (uint32_t)ck_n - 1u;
        bpm10 = span ? (25000u * n + span / 2u) / span : 0u;   /* 24 clocks a beat: 600000 / 24 = 25000 */
        if (bpm10 >= 200u && bpm10 <= 3000u) {
            ck_per_us = span * 1000u / n;
            if (bpm10 + 1u < ck_bpm10 || bpm10 > ck_bpm10 + 1u) {
                ck_bpm10 = (uint16_t)bpm10;
                player_ext_tempo(ck_bpm10);
            }
        }
    }
    player_ext_clock(ck_count, ms, ck_bpm10 ? ck_per_us : 0u);
}

static void transport_in(uint8_t st, int spp)
{
    player_state_t s;
    const ui_screen_t *scr = ui_screen_now();
    player_state(&s, 0);
    if (st == 0xFAu || (st == 0xFBu && !s.playing)) {   /* (what starts waits for the next clock: its first) */
        ck_count = 0;
        player_ext_clock(0, plat_ms(), ck_bpm10 ? ck_per_us : 0u);
    }
    if (scr && scr->transport) {        /* LOOPS: the loops; DJ: the main deck (specs/connections.md#clock) */
        if (st == 0xFAu)
            ck_n = 0;
        scr->transport(st, spp);
        return;
    }
    if (st == 0xFAu) {                  /* START: the current song from its start */
        spp_pending = -1;
        ck_n = 0;
        ui_play_song(ui_cur.song, 0);
    } else if (st == 0xFCu) {           /* STOP */
        if (s.playing)
            player_pause();
    } else if (st == 0xFBu) {           /* CONTINUE: from the song position that came, else on */
        uint8_t pos, row;
        if (spp_pending >= 0 && !place_of(ui_cur.song, spp_pending, &pos, &row))
            player_play_song(ui_cur.song, pos, row);
        else if (!s.playing)
            player_continue();
        spp_pending = -1;
    } else if (st == 0xF2u) {           /* the song position: there, now if playing, else at CONTINUE */
        uint8_t pos, row;
        if (s.playing && s.what == PL_SONG) {
            if (!place_of(ui_cur.song, spp, &pos, &row))
                player_play_song(ui_cur.song, pos, row);
        } else {
            spp_pending = spp;
        }
    }
}

/* -------------------------------------------------------------- MIDI learn --- */
static struct {
    uint8_t step, ctl;
    uint8_t have, note, ch, num;        /* the MIDI control named by its first message */
    uint8_t vals[8], nv, released;
    uint16_t master0;
    uint32_t t0;
} ln;

int midi_learning(void) { return ln.step; }
int midi_learn_ctl(void) { return ln.ctl; }

int midi_learn_start(void)
{
    plat_input_t in;
    if (midi_map_count() >= MIDI_MAP_MAX)
        return -1;
    memset(&ln, 0, sizeof ln);
    plat_input(&in);                    /* (the knobs' clicks so far are no touch) */
    ln.master0 = in.master;
    ln.step = 1;
    return 0;
}

void midi_learn_cancel(void)
{
    ln.step = 0;
    ui_dirty();
}

int midi_learn_take(const plat_input_t *in, const plat_input_t *last)
{
    uint32_t bd = in->buttons & ~last->buttons, nd = in->notes & ~last->notes;
    int i, d;
    if (!ln.step)
        return 0;
    if (bd >> B_HOME & 1u) {            /* HOME cancels, at either step */
        midi_learn_cancel();
        return 1;
    }
    if (ln.step != 1)
        return 1;
    for (i = 0; i < B_COUNT; i++)
        if (bd >> i & 1u) {
            ln.ctl = (uint8_t)(MC_BUTTON + i);
            goto picked;
        }
    for (i = 0; i < 27; i++)
        if (nd >> i & 1u) {
            ln.ctl = (uint8_t)(MC_KEY + i);
            goto picked;
        }
    for (i = 0; i < K_COUNT; i++)
        if (in->knob[i]) {
            ln.ctl = (uint8_t)(MC_KNOB + i);
            goto picked;
        }
    d = (int)in->master - (int)ln.master0;
    if (d > 40 || d < -40) {
        ln.ctl = MC_MASTER;
        goto picked;
    }
    return 1;
picked:
    ln.step = 2;
    ui_dirty();
    return 1;
}

static int knob_kind(void)
{
    int i, same = 0, all64 = 1, small = 1, top = 0, mid = 0;
    if (ln.ctl == MC_MASTER)
        return MK_ABS;
    for (i = 0; i < ln.nv; i++) {
        int v = ln.vals[i];
        if (v < 57 || v > 71)
            all64 = 0;
        if (!(v >= 1 && v <= 15) && !(v >= 113 && v <= 127) && !(v >= 65 && v <= 79))
            small = 0;
        if (v >= 113)
            top = 1;
        if (v >= 65 && v <= 79)
            mid = 1;
        if (i && v == ln.vals[i - 1])
            same++;
    }
    if (ln.nv == 1) {                   /* one message: an endless encoder's codes, else a knob's value */
        int v = ln.vals[0];
        return v == 1 || v == 127 ? MK_REL_2C : v == 63 || v == 65 ? MK_REL_64 : MK_ABS;
    }
    if (!same)                         /* never the same value twice running: an absolute knob (it sends a change) */
        return MK_ABS;
    if (all64)                          /* the same codes again and again: an endless encoder's */
        return MK_REL_64;
    if (small)
        return top || !mid ? MK_REL_2C : MK_REL_SM;
    return MK_ABS;
}

static void learn_done(int kind)
{
    char c[16], s[16], b[40];
    midi_map_t m = {ln.ch, ln.note, ln.num, ln.ctl, (uint8_t)kind};
    int i;
    map_load();
    i = map_find(ln.note, ln.ch, ln.num);   /* learned again: its old mapping goes */
    if (i >= 0) {
        memmove(map + 4 + 4 * i, map + 4 + 4 * (i + 1), 4u * (uint32_t)(nmap - i - 1));
        nmap--;
    }
    if (nmap < MIDI_MAP_MAX) {
        uint8_t *e = map + 4 + 4 * nmap++;
        e[0] = (uint8_t)(m.ch | m.note << 4);
        e[1] = m.num;
        e[2] = m.ctl;
        e[3] = m.kind;
    }
    memset(abs_last, 0xFF, sizeof abs_last);
    ln.step = 0;
    if (map_save()) {
        ui_toast(T("MEMORY FOR EDITS IS FULL"));
        return;
    }
    midi_ctl_name(c, m.ctl);
    ui_fmt(s, sizeof s, "%s", ui_txt_v(ui_txt_v(m.note ? T("NOTE #, CH #") : T("CC #, CH #"), ui_dec(m.num)), ui_dec(m.ch + 1)));
    ui_fmt(b, sizeof b, "%s: %s", c, s);
    ui_toast(b);
}

/* step 2: a channel message; 1 taken by learning */
static int learn_msg(uint8_t st, uint8_t d1, uint8_t d2, uint32_t now)
{
    uint8_t type = st >> 4, ch = st & 15u;
    int note = type == 8u || type == 9u, cc = type == 11u, val;
    if (ln.step != 2)
        return 0;
    if (!note && !cc)
        return 1;
    if (ln.ctl <= MC_MASTER && !cc)     /* a knob and MASTER learn a CC */
        return 1;
    val = type == 8u ? 0 : d2;
    if (!ln.have) {
        ln.have = 1;
        ln.note = (uint8_t)note;
        ln.ch = ch;
        ln.num = d1;
        ln.t0 = now | 1u;
        ln.vals[0] = (uint8_t)val;
        ln.nv = 1;
        if (ln.ctl == MC_MASTER)
            learn_done(MK_ABS);
        return 1;
    }
    if (ln.note != note || ln.ch != ch || ln.num != d1)
        return 1;                       /* another control: not the one named */
    if (ln.nv < 8u)
        ln.vals[ln.nv++] = (uint8_t)val;
    if (ln.ctl >= MC_BUTTON) {          /* a pad: its release now says momentary */
        if (!val)
            learn_done(MK_MOMENTARY);
    } else if (ln.nv >= 6u) {
        learn_done(knob_kind());
    }
    return 1;
}

static void learn_time(uint32_t now)
{
    if (ln.step != 2 || !ln.have || now - ln.t0 < LEARN_MS)
        return;
    learn_done(ln.ctl >= MC_BUTTON ? MK_TOGGLE : knob_kind());
}

/* ---------------------------------------------------- a learned control acts --- */
static void control(int i, int val)
{
    midi_map_t m;
    int clicks = 0;
    map_get(i, &m);
    if (m.ctl < MC_MASTER) {
        switch (m.kind) {
        case MK_ABS:
            if (abs_last[i] != 0xFFu)
                clicks = val - abs_last[i];     /* a click a step of its value; its first value sets where it is */
            abs_last[i] = (uint8_t)val;
            break;
        case MK_REL_2C: clicks = val < 64 ? val : val - 128; break;
        case MK_REL_64: clicks = val - 64; break;
        case MK_REL_SM: clicks = val & 64 ? -(val & 63) : val & 63; break;
        default: break;
        }
        knob_add[m.ctl] = (int16_t)(knob_add[m.ctl] + clicks);
        return;
    }
    if (m.ctl == MC_MASTER) {
        plat_master_set((uint16_t)(val * 1023 / 127));
        return;
    }
    if (m.ctl < MC_COUNT) {
        int down = m.kind == MK_TOGGLE ? 1 : val > 0, b = m.ctl < MC_KEY ? m.ctl - MC_BUTTON : m.ctl - MC_KEY;
        uint32_t *h = m.ctl < MC_KEY ? &btn_held : &key_held, *f = m.ctl < MC_KEY ? &btn_fresh : &key_fresh,
                 *r = m.ctl < MC_KEY ? &btn_rel : &key_rel;
        if (m.ctl >= MC_KEY)
            key_vel[b] = 0;             /* (a pad on a key plays at KEYS VOLUME) */
        press(h, f, r, b, down);
        if (m.kind == MK_TOGGLE)
            press(h, f, r, b, 0);       /* a tap on each press */
    }
}

/* ------------------------------------------------------- notes as the keys --- */
static void note_end(uint8_t note);

static void note_on(uint8_t note, uint8_t vel)
{
    int n = note - ui_midi(0);
    if (note_key[note])                 /* (on again without an off: the old one ends first) */
        note_end(note);
    sustained[note >> 3] &= (uint8_t)~(1u << (note & 7u));
    if (n >= 0 && n < 27) {
        key_vel[n] = vel;
        press(&key_held, &key_fresh, &key_rel, n, 1);
        note_key[note] = (uint8_t)(n + 1);
        return;
    }
    {                                   /* past the keys' range: it plays the current instrument */
        pl_inst_t in = {(uint8_t)(ui_cur.inst >> 8), (uint8_t)ui_cur.inst};
        player_key_on(note, in, vel);
        note_key[note] = 0xFFu;
    }
}

static void note_end(uint8_t note)
{
    uint8_t k = note_key[note];
    note_key[note] = 0;
    if (k == 0xFFu)
        player_key_off(note);
    else if (k)
        press(&key_held, &key_fresh, &key_rel, k - 1, 0);
}

static void note_off(uint8_t note)
{
    if (!note_key[note])
        return;
    if (pedal) {                        /* held by the sustain pedal until it is let go */
        sustained[note >> 3] |= (uint8_t)(1u << (note & 7u));
        return;
    }
    note_end(note);
}

static void pedal_set(int down)
{
    int i;
    pedal = (uint8_t)down;
    if (down)
        return;
    for (i = 0; i < 128; i++)
        if (sustained[i >> 3] >> (i & 7) & 1u) {
            sustained[i >> 3] &= (uint8_t)~(1u << (i & 7));
            note_end((uint8_t)i);
        }
}

static void program(int p)              /* the cartridge's p-th used slot, as PRESETS turns to it */
{
    uint8_t c = (uint8_t)(ui_cur.inst >> 8);
    int s, n = ui_cart_slots(c), k = 0;
    char nm[12];
    for (s = 0; s < n; s++)
        if (ui_inst_name((uint16_t)(c << 8 | s), nm) && k++ == p) {
            ui_set_inst((uint16_t)(c << 8 | s));
            return;
        }
}

static void bank(int b)                 /* the b-th cartridge, at its first used slot */
{
    uint8_t nums[256];
    int n = ui_carts(nums, 256), s, slots;
    char nm[12];
    if (b >= n)
        return;
    slots = ui_cart_slots(nums[b]);
    for (s = 0; s < slots; s++)
        if (ui_inst_name((uint16_t)(nums[b] << 8 | s), nm)) {
            ui_set_inst((uint16_t)(nums[b] << 8 | s));
            return;
        }
}

/* ------------------------------------------------------------------ the pass --- */
static void message(const plat_midi_t *e)
{
    uint8_t st = e->m[0], d1 = e->m[1] & 127u, d2 = e->m[2] & 127u, type = st >> 4, ch = st & 15u;
    int in_ch = ui_midi_set[S_MIDI_IN_CH], ext = ui_midi_set[S_SYNC] == 1, i;
    info.in++;
    if (st >= 0xF8u) {                  /* realtime: as SYNC says */
        if (!ext)
            return;
        if (st == 0xF8u) {
            clock_in(e->ms);
            info.clock_on = 1;
        } else {
            transport_in(st, 0);
        }
        return;
    }
    if (st == 0xF2u) {
        if (ext)
            transport_in(st, d1 | d2 << 7);
        return;
    }
    if (st < 0x80u || st >= 0xF0u)
        return;
    if (mir.on && e->src == PLAT_MIDI_USB && ch == mir.ch && (type == 8u || type == 9u || type == 11u)) {
        mirror_msg(type, d1, d2);       /* MIRROR first: the panel, before learning and any mapping */
        return;
    }
    if (learn_msg(st, d1, d2, e->ms))
        return;
    if (type == 9u || type == 8u || type == 11u) {      /* a learned control: itself, whatever IN CHANNEL */
        map_load();
        i = map_find(type != 11u, ch, d1);
        if (i >= 0) {
            control(i, type == 8u ? 0 : d2);
            return;
        }
    }
    if (in_ch && ch != in_ch - 1)
        return;
    switch (type) {
    case 8u:
    case 9u:
        if (type == 9u && d2)
            note_on(d1, d2);
        else
            note_off(d1);               /* (a note on at velocity 0 is a note off) */
        break;
    case 11u:
        if (d1 == 64u)
            pedal_set(d2 >= 64u);
        else if (d1 == 0u)
            bank(d2);
        break;
    case 12u:
        program(d1);
        break;
    case 14u:
        player_keys_bend((int16_t)(((int)(d1 | d2 << 7) - 8192) / 64));    /* ±2 semitones, in 1/64 */
        break;
    default:
        break;
    }
}

/* MIDI OUT's state and the clock out's, from what the player does */
static struct {
    uint8_t playing, song, pos, row, rows, what;
} was;

static void out_state(void)
{
    player_state_t s;
    int playing, mode = ui_midi_set[S_MIDI_OUT];
    player_state(&s, 0);
    playing = s.playing && s.what != PL_NONE;
    if (out_all && mode != 2)
        tracks_flush = 1;               /* ALL no more: the tracks' notes sent end */
    out_all = (uint8_t)(mode == 2);
    if (was.playing && (!playing || s.what != was.what))
        tracks_flush = 1;               /* STOP, a change of what plays: every note sent ends */
    co_bpm10 = s.bpm10;
    co_on = (uint8_t)(ui_midi_set[S_CLOCK_OUT] == 1 && ui_midi_set[S_SYNC] == 0);
    {                                   /* SYNC PULSE: its side to the sound, the analog out stereo while it is on */
        static int8_t pulse = -1;
        int p = ui_sync_pulse();
        if (p != pulse) {
            pulse = (int8_t)p;
            player_sync_pulse(p);
            ui_settings_apply(S_OUTPUT);
        }
    }
    if (co_on && s.what == PL_SONG) {
        if (playing && !was.playing) {
            if (s.pos == 0 && s.rows_played < 4u) {
                co_post(0xFAu, 0, 0);   /* START */
            } else {
                int p = spp_of(s.song, s.pos, s.row);
                co_post(0xF2u, (uint8_t)(p & 127), (uint8_t)(p >> 7));
                co_post(0xFBu, 0, 0);   /* CONTINUE */
            }
        } else if (!playing && was.playing) {
            co_post(0xFCu, 0, 0);       /* STOP */
        } else if (playing && (s.song != was.song ||
                               (s.pos != was.pos && !(s.pos == was.pos + 1 && was.row + 2 >= was.rows) && s.pos != 0))) {
            int p = spp_of(s.song, s.pos, s.row);          /* a jump: another song, a position skipped */
            co_post(0xFCu, 0, 0);
            co_post(0xF2u, (uint8_t)(p & 127), (uint8_t)(p >> 7));
            co_post(0xFBu, 0, 0);
        }
    }
    was.playing = (uint8_t)playing;
    was.song = s.song;
    was.pos = s.pos;
    was.row = s.row;
    was.rows = s.rows;
    was.what = s.what;
}

void midi_poll(void)
{
    plat_midi_t e;
    uint32_t now = plat_ms(), resets = plat_usb_resets();
    int n = 0;
    if (resets != mir.resets) {         /* a USB bus reset: MIRROR ends */
        mir.resets = resets;
        if (mir.on) mirror_set(0, mir.ch);
    }
    while (n < 64 && plat_midi_rx(&e)) {    /* (always read: a full queue would hold USB's SysEx back too) */
        n++;
        if (!seed_active())             /* (while seeding, nothing plays) */
            message(&e);
    }
    learn_time(now);
    if (ui_midi_set[S_SYNC] != 1) {
        if (ck_bpm10) {
            ck_bpm10 = 0;
            ck_n = 0;
            player_ext_tempo(0);
        }
        info.clock_on = 0;
    } else if (ck_last && now - ck_last > CLOCK_LOST_MS) {
        ck_last = 0;                    /* plays on at the last tempo */
        ck_n = 0;
        info.clock_on = 0;
        if (!ck_lost) {
            ck_lost = 1;
            ui_toast(T("CLOCK LOST"));
        }
    }
    out_state();
}

void midi_settings_changed(void) { out_state(); }

void midi_info(midi_info_t *i)
{
    *i = info;
    i->bpm10 = ck_bpm10;
}

void midi_init(void)
{
    memset(abs_last, 0xFF, sizeof abs_last);
    mir.resets = plat_usb_resets();
    map_load();
    player_note_hook = track_note;
}
