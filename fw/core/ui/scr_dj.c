/* SPDX-License-Identifier: GPL-3.0-only
 * Copyright (C) 2026 Vasily Kazantsev */
/* DJ (specs/ui/screens/dj.md): two songs on two decks, the left one the main, a crossfader, the song list.
 * The stand (play/dj.h: DJ's record) is what the decks are; the player plays both (play/dj.h's dj_deck_*).
 * Here: the decks' view, LOAD (on the bar line while a deck plays), PLAY / pause, a synced start on the other
 * deck's bar line, tempo and SYNC, the DJ FILTERs (sliding while their key is held) and VOLUMEs, the
 * crossfader, SWAP, the properties, the pads (PLAY: SET CUE, CUE, JUMP, MOVE | the stops; LOOPS:
 * PATTERN, REPEAT bars, free loops, held a ROLL; the LFO pads and KEY), HOME's mutes, the stand into the edit
 * memory. */
#include <string.h>
#include "ui_core.h"
#include "ui_priv.h"
#include "ui_data.h"
#include "fxdef.h"
#include "../play/player.h"
#include "../play/dj.h"
#include "../play/rec.h"
#include "../store/store.h"
#include "../midi/midi.h"

enum { M_PLAY, M_LOOPS, M_LFO };
static const txt_t MODES[3] = {TX("PLAY"), TX("LOOPS"), TX("LFO")};
/* the black keys (note numbers) */
enum { K_LPLAY = 1, K_LHOLD = 3, K_LFILTER = 5, K_LLOAD = 8, K_RLOAD = 10, K_RPLAY = 13, K_RHOLD = 15, K_RFILTER = 17,
       K_SYNC = 20, K_MODE = 25 };         /* (POLY, 22: none since PREVIEW went, no audio of its own) */
enum { P_SET, P_CUE, P_JUMP, P_MOVE };
#define PAD_TAP_MS 250
/* the LFOs (the LFO pads): their words */
static const txt_t LFO_SPEED[9] = {TX("32 BARS"), TX("16 BARS"), TX("8 BARS"), TX("4 BARS"), TX("2 BARS"), TX("1 BAR"),
                                   TX("1/2"), TX("1/4"), TX("1/8")};
static const txt_t LFO_SHAPE[6] = {TX("SINE"), TX("SQUARE"), TX("TRIANGLE"), TX("SAW UP"), TX("SAW DOWN"), TX("RANDOM")};
static const txt_t RANGE_FADER[2][4] = {{TX("L TO HERE"), TX("L TO R"), TX("SWING"), TX("MIRROR")},
                                        {TX("HERE TO R"), TX("R TO L"), TX("SWING"), TX("MIRROR")}};
static const txt_t RANGE_FILTER[4] = {TX("OFF TO HERE"), TX("OFF TO FULL"), TX("HERE TO FULL"), TX("SWING")};
static const txt_t RANGE_FX[2] = {TX("HERE TO 100 %"), TX("0 TO 100 %")};
static const txt_t FX_NAME[3] = {TX("REVERB"), TX("DELAY"), TX("CHORUS")};
static const char *lfo_label(int i)     /* FADER, FILTER L, FILTER R, the FX LFO's effect */
{
    static const txt_t L[3] = {TX("FADER"), TX("FILTER L"), TX("FILTER R")};
    return i < 3 ? T(L[i]) : T(FX_NAME[DJ_REC[4] > 2 ? 0 : DJ_REC[4]]);
}
static const struct { uint8_t song, tempo, place, over, beats, left, state, filt, set, xf, rule, cols, list; } Y =
    {19, 31, 48, 61, 79, 77, 91, 104, 117, 131, 144, 146, 158};
#define LIST_ROWS 5

/* what a deck's song is (read at LOAD): its bars, the overview's columns, its own tempo */
typedef struct {
    uint16_t bars;                      /* the song's bars */
    uint8_t ov_h[114], ov_pos[114];     /* the overview's columns: height (2-14), the position of the column's bar */
    uint16_t own10;                     /* its own tempo, BPM x 10 */
    int16_t lo10, hi10;                 /* the BPM x 10 its tempo knob turns between (ui_tempo_range) */
    uint8_t rpb, beats, npos, stop_end, ntrk;   /* (ntrk: its most tracks, for HOME's mutes) */
    uint8_t load_to, load_own;          /* a LOAD waiting for the bar line (0xFF none) */
    uint8_t start_at;                   /* a synced start waiting for the main's bar line (2: seen armed); HOLD: playing */
    uint8_t hold, cue_held, move;       /* HOLD down; CUE held playing from the cue; MOVE held */
    uint8_t hand_moved;                 /* HOLD's hand has moved the deck (its place: the stand's) */
    uint16_t hand_t;                    /* ... its last click, ms (the next row's ticks spread over the time since) */
    uint8_t cue_down, cue_keep;         /* CUE down; its PLAY key meanwhile: let go, it plays on */
    uint8_t jump_down, filt_down;       /* JUMP down; its FILTER key down (the state line says what each does) */
    uint8_t move_pos, move_row, move_turned;
    uint8_t jump_beat;                  /* a jump waiting for the beat */
    uint8_t move_wait;                  /* MOVE let go: the move waiting for the beat */
    uint8_t set_down, set_beat, set_pos, set_row;   /* SET CUE held: the place its press found; held past 250 ms: its beat */
    uint32_t key_t[8];                  /* when its pads went down (JUMP's hold, SET CUE's beat) */
    uint8_t stop, stay;                 /* a stop's pad held (PL_STOP_*); its PLAY key turned it: let go, it stays */
    uint8_t loop_pad, roll, loop_off;   /* LOOPS: the pad of the loop set (1-7), held a ROLL; a tap switched it off */
    uint8_t lp_pos, lp_row;             /* the loop's start, its rows */
    uint8_t hold_lpos;                  /* HOLD down: its loop's start then (the head back there: the loop as it was) */
    uint16_t lp_rows;
    uint8_t hold_lrow, hold_pos, hold_row;   /* (... and where the deck stood) */
    uint32_t loop_t;                    /* when its pad went down */
    uint32_t load_t;                    /* when a LOAD on a playing deck was pressed, till it is let go (its hold) */
} dk_t;
static struct {
    uint8_t mode, mode_before, k23vol;  /* the pads' mode; ENV: K2 / K3 turn the volumes */
    int16_t list;                       /* the list's highlight */
    uint8_t last_k, last_d;             /* the last knob turned (SEL resets it): 1 xf, 2 tempo, 3 filter, 4 vol, 5 KEY,
                                         * 6 an LFO's speed (last_d: which) */
    uint8_t lfo_hold, lfo_was_on, lfo_turned;   /* an LFO's on / off pad held (its white key + 1), ALGORITHM its speed */
    uint8_t key_hold;                   /* a KEY pad held (its white key + 1): the deck's tempo knob turns KEY */
    uint8_t shown;                      /* DJ is the screen (else ui_dj_poll runs the waits) */
    uint32_t pad_mode;                  /* each white key's mode at its press, 2 bits a key: its release goes there,
                                         * the mode switched meanwhile or not */
    uint32_t keys_down, keys_flip;      /* the keys down (a bit a note), and those held across a SWAP: their release
                                         * goes to their deck, on the other side now */
    uint8_t mute_page;                  /* HOME's mutes: tracks 00-07 or 08-0F */
    uint8_t dirty;
    uint8_t loads;                      /* dj_loads() as last seen (its low byte): another, the stand reloaded
                                         * (REVERT, a seed); in the padding before dirty_at, so no RAM */
    uint32_t dirty_at, flash[27], play_at[2];   /* (play_at: a deck's PLAY key down, for its hold) */
    uint8_t play_fired[2];              /* held past the hold delay: back at the start */
    int16_t last_row[2];
    dk_t dk[2];
} dj = {.list = 0};

/* ------------------------------------------------------------------- the songs --- */
static int song_info(uint8_t n, ui_song_t *s) { return n != DJ_NO_SONG && ui_song(n, s); }
static int song_beats(uint8_t n)
{
    rec_song_t r;
    if (rec_song(n, &r) || !r.fx) return 4;
    int b = r.fx[11];
    return b < 1 ? 4 : b > 16 ? 16 : b;
}
static int bar_rows(int d) { return dj.dk[d].rpb * dj.dk[d].beats; }
/* the bar a place is in, counted from the song's start */
static int bar_of(int d, int pos, int row)
{
    ui_song_t s;
    if (!song_info(dj_deck(d)[DK_SONG], &s)) return 0;
    int b = 0, br = bar_rows(d);
    for (int p = 0; p < pos && p < s.npos; p++) b += (ui_pattern_rows(s.order[p]) + br - 1) / br;
    return b + row / br;
}
static void read_song(int d)            /* at LOAD: the bars, the overview, the own tempo */
{
    dk_t *k = &dj.dk[d];
    uint8_t n = dj_deck(d)[DK_SONG];
    ui_song_t s;
    memset(k->ov_h, 0, sizeof k->ov_h);
    k->bars = 0;
    k->ntrk = 0;
    if (!song_info(n, &s)) return;
    for (int p = 0; p < s.npos; p++) {
        int tr = ui_pattern_tracks(s.order[p]);
        if (tr > k->ntrk) k->ntrk = (uint8_t)tr;
    }
    k->rpb = s.rpb ? s.rpb : 4;
    k->beats = (uint8_t)song_beats(n);
    k->npos = (uint8_t)s.npos;
    k->stop_end = (s.flags & RS_STOP) != 0;
    k->own10 = (uint16_t)((s.tempo * 2400 / (s.ticks * k->rpb) + 5) / 10);
    int lo, hi;
    ui_tempo_range(&s, &lo, &hi);
    k->lo10 = (int16_t)lo;
    k->hi10 = (int16_t)hi;
    int br = bar_rows(d), bars = 0;
    for (int p = 0; p < s.npos; p++) bars += (ui_pattern_rows(s.order[p]) + br - 1) / br;
    k->bars = (uint16_t)(bars ? bars : 1);
    /* each bar's notes started, every track: the busiest bar 14 px. Bar b covers the columns from b * 114 / bars
     * to the next bar's, each of them as tall as the bar; with more bars than columns, a column holds the bars
     * that start in it, as tall as their mean */
    uint16_t cnt[114];                                  /* (on the main loop's stack: only while loading) */
    memset(cnt, 0, sizeof cnt);
    uint16_t most = 1;
    int b = 0, nbars = k->bars;
    for (int p = 0; p < s.npos; p++) {
        rec_pat_t pt;
        int rows = ui_pattern_rows(s.order[p]), nb = (rows + br - 1) / br;
        for (int c = b * 114 / nbars; c < (b + nb) * 114 / nbars; c++) k->ov_pos[c] = (uint8_t)p;
        if (!rec_pattern(s.order[p], &pt)) {
            for (int t = 0; t < pt.ntrk; t++) {
                if (!pt.trk[t]) continue;
                rec_cur_t cu;
                rec_cell_t c;
                rec_cur_set(&cu, pt.trk[t]);
                for (int r = 0; r < pt.rows; r++)
                    if (rec_cur_at(&cu, r, &c) && (c.has & RC_NOTE) && c.note != REC_NOTE_OFF) {
                        int bb = b + r / br, c0 = bb * 114 / nbars, c1 = (bb + 1) * 114 / nbars;
                        if (c1 <= c0) c1 = c0 + 1;
                        for (int col = c0; col < c1 && col < 114; col++) if (cnt[col] < 60000) cnt[col]++;
                    }
            }
        }
        b += nb;
    }
    for (int c = 0; c < 114; c++) {
        if (nbars > 114) {                              /* the bars starting in this column: their mean */
            int n = ((c + 1) * nbars + 113) / 114 - (c * nbars + 113) / 114;
            cnt[c] = (uint16_t)(cnt[c] / (n > 1 ? n : 1));
        }
        if (cnt[c] > most) most = cnt[c];
    }
    for (int c = 0; c < 114; c++) k->ov_h[c] = (uint8_t)(2 + (cnt[c] * 12 + most / 2) / most);
}
/* a place in the song as the overview's x (0-114): its row among the song's (each pattern whole bars), to the row */
static int ov_x(int d, int pos, int row)
{
    dk_t *k = &dj.dk[d];
    int br = bar_rows(d) ? bar_rows(d) : 1, x = (bar_of(d, pos, row) * br + row % br) * 114 / (k->bars * br);
    return x < 0 ? 0 : x > 114 ? 114 : x;
}
static int ov_col(int d, int pos, int row) { int x = ov_x(d, pos, row); return x > 113 ? 113 : x; }

/* ------------------------------------------------------------- the stand changed --- */
static void changed(void) { dj.dirty = 1; dj.dirty_at = ui_now(); ui_dirty(); }
static void put_stand(void)
{
    if (ui_edit_put(REC_DJ, 0, 0, 1, DJ_REC, DJ_REC_LEN)) ui_toast(T("MEMORY FOR EDITS IS FULL"));
    dj.dirty = 0;
}
static const char *side(int d) { return d ? T("RIGHT") : T("LEFT"); }
static int has_song(int d) { return dj_deck(d)[DK_SONG] != DJ_NO_SONG; }
static void refuse_key(int n) { if (n >= 0 && n < 27) dj.flash[n] = ui_now() + 600; }
static void no_song(int n) { refuse_key(n); ui_toast(T("NO SONG ON THE DECK")); }
static int playing(int d) { dj_deck_state_t s; dj_deck_state(d, &s); return s.playing; }

/* the tempo: a deck's own (the song's share); a synced right deck the main's, its half or its double. With SYNC
 * EXTERNAL and a clock the main deck plays at the clock's, its half or double nearest its song's own (the player
 * picks the same: player.c pl_fix10) */
static int clock10(void)                /* the clock's BPM x 10 (SYNC EXTERNAL), else 0 */
{
    midi_info_t i;
    if (ui_midi_set[S_SYNC] != 1) return 0;
    midi_info(&i);
    return i.bpm10;
}
static int main10(void)
{
    int e = clock10(), own = dj.dk[0].own10, best = e, bd = 1 << 30;
    if (!e || !has_song(0)) return dj_deck_bpm10(0);
    for (int m = 0; m < 3; m++) {
        int v = m == 1 ? e / 2 : m == 2 ? e * 2 : e, dd = v > own ? v - own : own - v;
        if (dd < bd) { bd = dd; best = v; }
    }
    return best;
}
static int sync_mult_for(int d)         /* the multiple nearest the right song's own */
{
    int m10 = main10(), own = dj.dk[d].own10, best = 0, bd = 1 << 30;
    static const int M[3][2] = {{1, 1}, {1, 2}, {2, 1}};       /* x1, x1/2, x2 */
    for (int m = 0; m < 3; m++) {
        int v = m10 * M[m][0] / M[m][1], dd = v > own ? v - own : own - v;
        if (dd < bd) { bd = dd; best = m; }
    }
    return best;
}
/* SYNC on: the multiple picked again for the main's tempo as it is now (a LOAD, SWAP, SYNC on, the own tempo) */
static void sync_pick(void) { if (dj_sync() && has_song(1)) DJ_REC[6] = (uint8_t)((DJ_REC[6] & ~12) | sync_mult_for(1) << 2); }
static int bpm10(int d)
{
    if (d == 1 && dj_sync() && has_song(0)) {
        static const int M[3][2] = {{1, 1}, {1, 2}, {2, 1}};
        int m = dj_sync_mult();
        return main10() * M[m][0] / M[m][1];
    }
    return d ? dj_deck_bpm10(d) : main10();
}
static void send_tempo(void) { for (int d = 0; d < 2; d++) if (has_song(d)) dj_deck_tempo(d, bpm10(d)); }
static void fmt_bpm(char *b, int v10) { ui_fmt(b, 12, "%d.%d", v10 / 10, v10 % 10); }
static void fmt_change(char *b, int d)
{
    int own = dj.dk[d].own10, now = bpm10(d);
    if (!own) { b[0] = 0; return; }
    int c = (now - own) * 1000 / own;                   /* x 0.1 % */
    ui_fmt(b, 12, "%c%d.%d%%", c < 0 ? '-' : '+', (c < 0 ? -c : c) / 10, (c < 0 ? -c : c) % 10);
}

/* ------------------------------------------------------------------- the decks --- */
/* was_playing: the LOAD waited on a playing deck (its bar line, or the end of a song that stops there): the new song
 * plays on */
static void load_now(int d, uint8_t song, int own, int was_playing)
{
    uint8_t *k = dj_deck(d);
    k[DK_SONG] = song;
    k[DK_POS] = k[DK_ROW] = k[DK_CUE_POS] = k[DK_CUE_ROW] = 0;
    k[DK_KEY] = 0;
    k[DK_FLAGS] &= (uint8_t)~(DF_REPEAT_PATTERN | DF_AT_END);
    k[DK_LOOP] = 0;
    k[DK_MUTES] = k[DK_MUTES + 1] = 0;
    k[DK_SOLO] = 0xFF;
    if (!was_playing) { k[DK_FILTER] = 0; k[DK_FLAGS] |= DF_FILTER_ON; k[DK_LFO] = 0; }   /* (on at OFF) */
    read_song(d);
    if (!was_playing || own) dj_deck_set_bpm10(d, dj.dk[d].own10);  /* tapped on a playing deck: the BPM it played */
    sync_pick();                                        /* (either deck: the main's tempo or the right song's own) */
    dj_deck_load(d, song);
    send_tempo();
    dj_deck_key(d, 0);
    dj_deck_mutes(d, 0);
    if (was_playing) dj_deck_play(d, 1);
    changed();
}
static void load_key(int d, int n)
{
    ui_song_t s;
    dk_t *k = &dj.dk[d];
    uint8_t nums[256];
    int ns = ui_songs(nums, 256);
    if (dj.list < 0 || dj.list >= ns) { refuse_key(n); return; }
    uint8_t song = nums[dj.list];
    if (!ui_song(song, &s) || !s.npos) { refuse_key(n); ui_toast(ui_txt_v(T("SONG # IS EMPTY"), ui_hex2(song))); return; }
    char b[48];
    if (k->load_to != DJ_NO_SONG) { k->load_to = DJ_NO_SONG; ui_fmt(b, sizeof b, T("%s: LOAD CANCELLED"), side(d)); ui_toast(b); return; }
    if (playing(d)) {                                   /* on the deck's next bar line */
        k->load_to = song;
        k->load_own = 0;
        k->load_t = ui_now() | 1;
        ui_fmt(b, sizeof b, T("%s: %s AT THE BAR"), side(d), s.name);
        ui_toast(b);
        return;
    }
    load_now(d, song, 1, 0);
    ui_fmt(b, sizeof b, "%s: %s", side(d), s.name);
    ui_toast(b);
}
/* a jump with a loop running takes the loop along, as HOLD does (specs/ui/screens/dj.md#the-pads): REPEAT PATTERN
 * to the position it lands in, REPEAT bars to the block of bars around the place, a free LOOP from it; a ROLL's
 * stays. outside: only from outside the loop (a start: played from outside it, the deck takes it along) */
static void loop_along(int d, int pos, int row, int outside)
{
    const uint8_t *r = dj_deck(d);
    ui_song_t s;
    if (dj.dk[d].roll || !song_info(r[DK_SONG], &s)) return;
    if (r[DK_FLAGS] & DF_REPEAT_PATTERN) { dj_loop(d, 1, pos, 0, 0, 0); return; }
    int pad = r[DK_LOOP];
    if (pad < 2) return;
    int rpb = dj.dk[d].rpb ? dj.dk[d].rpb : 4, rows = r[DK_LOOP_LEN] * rpb / 2, p = r[DK_LOOP_POS], w = r[DK_LOOP_ROW];
    if (rows < 1) rows = 1;
    if (outside)
        for (int n = 0; n < rows; n++) {                /* (in it, through the song's flow: across a pattern's end too) */
            if (p == pos && w == row) return;
            if (++w >= ui_pattern_rows(s.order[p])) { if (p + 1 >= s.npos) break; p++; w = 0; }
        }
    int start = r[DK_REPEAT] ? row / rows * rows : row;
    dj_loop(d, pad, pos, start, rows, 0);
    dj.dk[d].lp_pos = (uint8_t)pos;
    dj.dk[d].lp_row = (uint8_t)start;
    dj.dk[d].lp_rows = (uint16_t)rows;
    changed();
}
/* a paused deck plays: synced, on the other deck's next bar line (the deck playing never moves for the one
 * starting), else at once */
static void start_deck(int d)
{
    if (dj_sync() && has_song(0) && has_song(1) && playing(1 - d)) { dj.dk[d].start_at = 1; dj_deck_arm(d); return; }
    dj_deck_play(d, 1);
}
/* 1: the press turned what letting a held pad go does (no hold of its own then) */
static int play_key(int d, int n)
{
    dk_t *k = &dj.dk[d];
    if (!has_song(d)) { no_song(n); return 0; }
    if (k->start_at) { k->start_at = 0; dj_deck_play(d, 0); return 0; }   /* a second tap: the synced start cancelled */
    if (k->stop) { k->stay = !k->stay; return 1; }      /* a stop held: what letting it go does (the footer says) */
    if (k->cue_held) { k->cue_keep = !k->cue_keep; return 1; }   /* CUE held playing: let go, plays on (or back to the cue) */
    if (playing(d)) {
        dj_deck_play(d, 0);
        changed();
        return 0;
    }
    if (dj_deck(d)[DK_FLAGS] & DF_AT_END) { refuse_key(n); ui_toast(T("THE END: CUE GOES BACK")); return 0; }
    dj_on(1);
    dj_mix();
    send_tempo();
    loop_along(d, dj_deck(d)[DK_POS], dj_deck(d)[DK_ROW], 1);
    start_deck(d);
    return 0;
}
static void cue_set(int d, int pos, int row)
{
    uint8_t *k = dj_deck(d);
    int br = bar_rows(d);
    k[DK_CUE_POS] = (uint8_t)pos;
    k[DK_CUE_ROW] = (uint8_t)(row / br * br);           /* on the bar line at or before it */
    char b[32];
    ui_fmt(b, sizeof b, T("CUE: POS %s BAR %X"), ui_hex2(k[DK_CUE_POS]), k[DK_CUE_ROW] / br);
    ui_toast(b);
    changed();
}
/* SWAP: the decks change sides as they play (the player's follow: dj_swap), nothing reloaded or cut; what is kept
 * here by side goes with its deck, a key held meanwhile lets go on its deck */
static void swap(void)
{
    if (!has_song(0) && !has_song(1)) { ui_toast(T("NO SONGS TO SWAP")); return; }
    uint8_t t[24];
    int right10 = bpm10(1);                             /* (the right's BPM as heard: synced, the main's share) */
    memcpy(t, dj_deck(0), 24);
    memcpy(dj_deck(0), dj_deck(1), 24);
    memcpy(dj_deck(1), t, 24);
    dk_t tk = dj.dk[0];
    dj.dk[0] = dj.dk[1];
    dj.dk[1] = tk;
    uint32_t pa = dj.play_at[0];
    dj.play_at[0] = dj.play_at[1];
    dj.play_at[1] = pa;
    uint8_t pf = dj.play_fired[0];
    dj.play_fired[0] = dj.play_fired[1];
    dj.play_fired[1] = pf;
    int16_t lr = dj.last_row[0];
    dj.last_row[0] = dj.last_row[1];
    dj.last_row[1] = lr;
    dj.keys_flip ^= dj.keys_down;
    if (has_song(0)) dj_deck_set_bpm10(0, right10);   /* the new main keeps the BPM it played at */
    sync_pick();                                        /* (SYNC stays with the side: the multiple for the new right) */
    dj_set_xf(32 - dj_xf());
    dj_lfo_bytes(DJ_LFO_FADER)[0] ^= 2;                 /* the FADER LFO's swing mirrored with it */
    dj_swap();                                          /* (before the tempos: they go by the new sides) */
    send_tempo();
    dj_mix();
    ui_toast(T("DECKS SWAPPED"));
    changed();
}

/* ------------------------------------------------------------------- drawing --- */
static void filter_word(char *b, int v) { fxd_text(FE_FILTER, 0, v, b); }
static int side_col(int v) { return v < 0 ? C_LBLUE : v > 0 ? C_BROWN : C_LGREY; }
static void vol_bar(int d, int x0)
{
    int v = (int8_t)dj_deck(d)[DK_VOL], h = v <= DJ_VOL_OFF ? 0 : v <= 0 ? 1 + ((v + 24) * 9 * 2 + 24) / 48 : 10 + (v * 2 + 3) / 6;
    g_rect(x0 + 110, Y.song, 4, 12, C_DEEP + C_DGREY);
    if (h) g_rect(x0 + 110, Y.song + 12 - h, 4, h, v > 0 ? C_YELLOW : C_GREEN);
}
/* LOOPS' pads 2-8 (k 1-7): REPEAT the bars around, free LOOPs */
static const txt_t LOOP_W[8] = {TX("PATTERN"), TX("4 BARS"), TX("2 BARS"), TX("1 BAR"), TX("2 BARS"), TX("1 BAR"), TX("1/2 BAR"),
                                TX("1/4")};
static const char *loop_word(int k) { return T(LOOP_W[k & 7]); }
/* the state line (specs/ui/screens/dj.md#the-state-line): the first that applies; every key or pad held its word
 * (a text: drawn at once) */
static const char *state_word(int d, const dj_deck_state_t *st)
{
    static char b[24];
    dk_t *k = &dj.dk[d];
    static const txt_t STOPS[5] = {TX(""), TX("BRAKE"), TX("BACKSPIN"), TX("ECHO OUT"), TX("REVERB OUT")};
    if (k->load_to != DJ_NO_SONG) return k->load_own ? T("LOADS AT ITS TEMPO") : T("LOADS AT THE BAR");
    if (k->hold) return T("HELD");
    if (k->move) { ui_fmt(b, sizeof b, T("MOVE POS %s BAR %X"), ui_hex2(k->move_pos), k->move_row / bar_rows(d)); return b; }
    if (k->cue_down) return k->cue_held ? T("CUE PLAY") : T("CUE");
    if (k->set_down) return k->set_beat ? T("SET CUE ON THE BEAT") : T("SET CUE");
    if (k->jump_down) return ui_now() - k->key_t[P_JUMP] < PAD_TAP_MS ? T("JUMP") : T("JUMP ON THE BEAT");
    if (dj.play_at[d] && (dj.play_fired[d] || (int32_t)(ui_now() - dj.play_at[d]) >= PAD_TAP_MS))
        return dj.play_fired[d] ? T("BACK TO THE START") : T("HOLD: TO THE START");
    if (k->filt_down) {
        char w[12];
        filter_word(w, (int8_t)dj_deck(d)[DK_FILTER]);
        if (dj_deck(d)[DK_FLAGS] & DF_FILTER_ON) ui_fmt(b, sizeof b, T("FILTER ON %s"), w);
        else strcpy(b, T("FILTER OFF"));
        return b;
    }
    if (dj.key_hold && ((dj.key_hold - 1) >> 2) == d + 1) return T("KEY");   /* (its KEY pad: 8 the left's, 12 the right's) */
    if (dj.lfo_hold && ((dj.lfo_hold - 1) >> 2) == d + 1) {                  /* its filter's LFO on / off pad: its speed */
        uint8_t sp = dj_lfo_bytes(d + 1)[3];
        ui_fmt(b, sizeof b, T("LFO %s"), T(LFO_SPEED[sp > 8 ? 6 : sp]));
        return b;
    }
    if (k->roll && k->loop_pad) { ui_fmt(b, sizeof b, T("ROLL %s"), loop_word(k->loop_pad)); return b; }
    if (k->stop && k->stop < 5) return T(STOPS[k->stop]);
    if (k->start_at) return T("STARTS AT THE BAR");
    if (k->jump_beat) return T("JUMP ON THE BEAT");
    if (k->move_wait) return T("MOVE ON THE BEAT");
    if (st->ended || (dj_deck(d)[DK_FLAGS] & DF_AT_END)) return T("AT THE END");
    if (!st->playing && st->pos == dj_deck(d)[DK_CUE_POS] && st->row == dj_deck(d)[DK_CUE_ROW]) return T("AT THE CUE");
    return 0;
}
static int slider_x(int cx, int v) { return cx + (v < 0 ? (v * 23 * 2 - 64) / 128 : (v * 23 * 2 + 63) / 126); }
static void filter_row(int d, int x0)
{
    const uint8_t *k = dj_deck(d);
    player_state_t ps;
    int v = (int8_t)k[DK_FILTER], heard = k[DK_FLAGS] & DF_FILTER_ON, col = heard ? side_col(v) : C_DGREY;
    int y = Y.filt + 5, cx = x0 + 47 + 23, lfo = dj_lfo_bytes(DJ_LFO_FILT_L + d)[0] & 1;
    char b[16];
    ui_player_state(&ps);
    int p = d ^ dj_crossed(&ps);                        /* (the player's side of it: a SWAP on its way) */
    int swing = lfo && dj_live() && (ps.lfo_live >> (PL_LFO_FILT_L + p) & 1);
    if (swing) v = (heard ? ps.lfo_filt8[p] : ps.lfo_prev8[p]) / 256;   /* (its LFO: as heard, or the preview) */
    filter_word(b, v);
    g_text(x0, Y.filt, &FONT_T, b, v ? (swing && !heard ? C_DGREY : heard ? side_col(v) : C_DGREY) : C_DGREY);
    g_hline(x0 + 47, y, 47, C_DGREY);
    int at = slider_x(cx, v);
    if (v) g_rect(at < cx ? at : cx, y - 2, (at > cx ? at - cx : cx - at) + 1, 5, swing && !heard ? C_DGREY : col);
    if (swing) {                                        /* the home tick: where the knob has it */
        int h = slider_x(cx, (int8_t)k[DK_FILTER]);
        g_vline(h, y - 3, 7, C_WHITE);
    }
    g_vline(cx, y - 3, 7, C_LGREY);
    if (lfo) g_text(x0 + 97, Y.filt, &FONT_T, T("LFO"), swing && heard ? C_WHITE : C_DGREY);
}
static void set_line(int d, int x0)
{
    const uint8_t *k = dj_deck(d);
    char key[10] = "", mute[12] = "";
    int kv = (int8_t)k[DK_KEY];
    if (kv) ui_fmt(key, sizeof key, T("KEY %s%d"), kv > 0 ? "+" : "", kv);
    int muted = 0;
    unsigned m = (unsigned)(k[DK_MUTES] | k[DK_MUTES + 1] << 8);
    for (int t = 0; t < 16; t++) muted += m >> t & 1;
    const char *mw = 0, *mn = 0;
    char mwb[8];                                        /* (the texts kept: more are unpacked before they are drawn) */
    if (k[DK_SOLO] != 0xFF) { mw = strcpy(mwb, T("SOLO")); mn = ui_hex2(k[DK_SOLO]); }
    else if (muted) { mw = strcpy(mwb, T("MUTE")); mn = ui_dec(muted); }
    char lw[20] = "", sw[12] = "";
    const char *loop = lw, *shortw = sw;
    if (k[DK_FLAGS] & DF_REPEAT_PATTERN) { strcpy(lw, T("REPEAT PATTERN")); strcpy(sw, T("PATTERN")); }
    else if (k[DK_LOOP] >= 2 && k[DK_LOOP] <= 8) {
        ui_fmt(sw, sizeof sw, "%s", loop_word(k[DK_LOOP] - 1));
        ui_fmt(lw, sizeof lw, "%s %s", k[DK_LOOP] <= 4 ? T("REPEAT") : T("LOOP"), shortw);
    }
    int x = x0;
    if (loop[0] && (int)strlen(loop) + (mw ? 1 + 4 + 1 + (int)strlen(mn) : 0) + (key[0] ? 1 + (int)strlen(key) : 0) > 19) loop = shortw;
    if (loop[0]) x = g_text(x0, Y.set, &FONT_T, loop, C_GREEN) + 6;
    if (mw) {
        ui_fmt(mute, sizeof mute, " %s", mn);
        g_run_t r[2] = {{mw, C_CYAN}, {mute, C_LGREY}};
        g_runs(x, Y.set, &FONT_T, r, 2);
    }
    if (key[0]) g_text(x0 + 114 - g_tw(&FONT_T, key), Y.set, &FONT_T, key, C_WHITE);
}
static void overview(int d, const dj_deck_state_t *st, int x0, int warn)
{
    dk_t *k = &dj.dk[d];
    if (!k->bars) return;
    for (int c = 0; c < 114; c++) {
        int h = k->ov_h[c], last8 = warn && c >= (k->bars - 8) * 114 / k->bars;
        g_rect(x0 + c, Y.over + 14 - h, 1, h, last8 ? C_RED : k->ov_pos[c] == st->pos ? C_LGREY : C_DGREY);
    }
    const uint8_t *rec = dj_deck(d);
    if (rec[DK_FLAGS] & DF_REPEAT_PATTERN)              /* what goes round: the pattern repeated */
        for (int c = 0; c < 114; c++) if (k->ov_pos[c] == st->pos) {
            g_rect(x0 + c, Y.over + 14 - k->ov_h[c], 1, k->ov_h[c], C_GREEN);
            g_rect(x0 + c, Y.over + 14, 1, 2, C_GREEN);
        }
    if (rec[DK_LOOP] >= 2 && rec[DK_LOOP] <= 8) {       /* a REPEAT or a LOOP: its rows green, to the row */
        int rows = rec[DK_LOOP_LEN] * (k->rpb ? k->rpb : 4) / 2;
        int c0 = ov_col(d, rec[DK_LOOP_POS], rec[DK_LOOP_ROW]), c1 = ov_x(d, rec[DK_LOOP_POS], rec[DK_LOOP_ROW] + rows);
        if (c1 <= c0) c1 = c0 + 1;
        for (int c = c0; c < c1 && c < 114; c++) {
            g_rect(x0 + c, Y.over + 14 - k->ov_h[c], 1, k->ov_h[c], C_GREEN);
            g_rect(x0 + c, Y.over + 14, 1, 2, C_GREEN);
        }
    }
    for (int c = 1; c < 114; c++) if (k->ov_pos[c] != k->ov_pos[c - 1]) g_vline(x0 + c, Y.over, 16, C_BLACK);
    int cue = ov_col(d, dj_deck(d)[DK_CUE_POS], dj_deck(d)[DK_CUE_ROW]);
    g_rect(x0 + (cue > 111 ? 111 : cue), Y.over - 2, 3, 2, C_YELLOW);
    for (int m = 0; m < (k->move ? 2 : 1); m++) {      /* the playhead, MOVE's cursor: each between black lines, so
                                                         * they show on any column in any theme (colours.md) */
        int cx = x0 + (m ? ov_col(d, k->move_pos, k->move_row) : ov_col(d, st->pos, st->row));
        if (cx > x0) g_vline(cx - 1, Y.over - 1, 17, C_BLACK);
        if (cx < x0 + 113) g_vline(cx + 1, Y.over - 1, 17, C_BLACK);
        g_vline(cx, Y.over - 1, 17, m ? C_CYAN : C_WHITE);
    }
}
static void draw_deck(int d, int x0)
{
    ui_song_t s;
    dj_deck_state_t st;
    uint8_t n = dj_deck(d)[DK_SONG];
    if (!song_info(n, &s)) {
        g_text(x0, Y.song, &FONT_T, T("NO SONG"), C_DGREY);
        g_run_t r[2] = {{d ? T("OP5") : T("OP4"), C_YELLOW}, {T(": LOAD THE"), C_LGREY}};
        g_runs(x0, Y.place, &FONT_T, r, 2);
        g_text(x0, Y.over, &FONT_T, T("HIGHLIGHTED SONG"), C_LGREY);
        return;
    }
    dj_deck_state(d, &st);
    dk_t *k = &dj.dk[d];
    char b[24], name[16];
    g_text(x0, Y.song, &FONT_T, ui_hex2(n), C_DGREY);
    strncpy(name, s.name, 15);
    name[15] = 0;
    g_text(x0 + 18, Y.song, &FONT_T, name, st.playing ? C_GREEN : C_WHITE);
    vol_bar(d, x0);
    char bp[12];
    fmt_bpm(bp, bpm10(d));
    ui_fmt(b, sizeof b, "%5s", bp);
    g_text(x0 + 40 - g_tw(&FONT_S, b), Y.tempo, &FONT_S, b, C_WHITE);
    g_text(x0 + 44, Y.tempo + 3, &FONT_T, T("BPM"), C_LGREY);
    if (d == 1 && dj_sync() && has_song(0)) { const char *sy = T("SYNC"); g_text(x0 + 114 - g_tw(&FONT_T, sy), Y.tempo + 3, &FONT_T, sy, C_GREEN); }
    else { fmt_change(b, d); g_text(x0 + 114 - g_tw(&FONT_T, b), Y.tempo + 3, &FONT_T, b, !strcmp(b + 1, "0.0%") ? C_LGREY : C_WHITE); }
    int br = bar_rows(d), rows = ui_pattern_rows(s.order[st.pos < s.npos ? st.pos : 0]), bars_in = (rows + br - 1) / br;
    char p1[12], p2[12];
    ui_fmt(p1, sizeof p1, "%02X/%02X", st.pos, s.npos > 1 ? s.npos - 1 : 0);   /* (positions two digits: POS 02/05) */
    ui_of(p2, st.row / br, bars_in ? bars_in : 1);
    g_run_t r[4] = {{T("POS "), C_CYAN}, {p1, C_LGREY}, {T(" BAR "), C_CYAN}, {p2, C_LGREY}};
    g_runs(x0, Y.place, &FONT_T, r, 4);
    int bar_now = bar_of(d, st.pos, st.row), bars_left = k->bars - bar_now, warn = k->stop_end && st.playing && bars_left <= 8;
    overview(d, &st, x0, warn);
    int w = 60 / k->beats, beat = st.row % br / k->rpb;
    for (int i = 0; i < k->beats; i++)
        g_rect(x0 + i * w, Y.beats, w - 2, 8, st.playing && i == beat ? (i ? C_GREEN : C_YELLOW) : C_DEEP + C_GREEN);
    /* the time left at the deck's tempo: the rows to the end */
    int rows_left = (k->bars - bar_now) * br - st.row % br, b10 = bpm10(d) ? bpm10(d) : 1200;
    uint32_t secs = (uint32_t)((int64_t)rows_left * 600 / ((int64_t)b10 * k->rpb / 1) );
    char t[12];
    ui_fmt_time(t, secs);
    ui_fmt(b, sizeof b, "-%s", t);
    g_text(x0 + 114 - g_tw(&FONT_T, b), Y.left, &FONT_T, b, warn ? C_RED : C_LGREY);
    const char *what = state_word(d, &st);
    if (what) g_text(x0, Y.state, &FONT_T, what, C_YELLOW);
    filter_row(d, x0);
    set_line(d, x0);
}
static void draw_list(void)
{
    uint8_t nums[256];
    int n = ui_songs(nums, 256);
    g_hline(0, Y.rule, 240, C_DGREY);
    const char *bpm_h = T("BPM"), *len_h = T("LEN");
    g_text(18, Y.cols, &FONT_T, T("NR"), C_CYAN);
    g_text(36, Y.cols, &FONT_T, T("NAME"), C_CYAN);
    g_text(156 - g_tw(&FONT_T, bpm_h), Y.cols, &FONT_T, bpm_h, C_CYAN);
    g_text(180 - g_tw(&FONT_T, len_h), Y.cols, &FONT_T, len_h, C_CYAN);   /* (the positions: its length) */
    g_text(192, Y.cols, &FONT_T, T("TIME"), C_CYAN);
    if (!n) { g_text(2, Y.list, &FONT_T, T("NO SONGS: MAKE ONE ON SONGS"), C_DGREY); return; }
    int top = ui_clamp(dj.list - 2, 0, n - LIST_ROWS > 0 ? n - LIST_ROWS : 0);
    for (int i = top; i < n && i < top + LIST_ROWS; i++) {
        ui_song_t s;
        if (!ui_song(nums[i], &s)) continue;
        int y = Y.list + 13 * (i - top), on = i == dj.list;
        if (on) g_box(0, y - 1, 240, 13, C_WHITE);
        if (dj_deck(0)[DK_SONG] == nums[i]) g_text(2, y, &FONT_T, T("L"), playing(0) ? C_GREEN : C_LGREY);
        if (dj_deck(1)[DK_SONG] == nums[i]) g_text(8, y, &FONT_T, T("R"), playing(1) ? C_GREEN : C_LGREY);
        g_text(18, y, &FONT_T, ui_hex2(nums[i]), C_DGREY);
        char nm[17];
        strncpy(nm, s.name, 16);
        nm[16] = 0;
        g_text(36, y, &FONT_T, nm, on ? C_WHITE : C_LGREY);
        const char *bpm = ui_dec(ui_song_bpm(&s)), *pos = ui_dec(s.npos);
        g_text(156 - g_tw(&FONT_T, bpm), y, &FONT_T, bpm, C_LGREY);
        g_text(180 - g_tw(&FONT_T, pos), y, &FONT_T, pos, C_LGREY);
        char t[8];
        ui_fmt_time(t, ui_song_secs(&s));
        g_text(192, y, &FONT_T, t, C_LGREY);
    }
}
static void crossfader(void)
{
    player_state_t ps;
    g_text(14, Y.xf, &FONT_T, T("FADER"), C_CYAN);
    g_text(50, Y.xf, &FONT_T, T("L"), C_CYAN);
    g_hline(60, Y.xf + 5, 120, C_DGREY);
    ui_player_state(&ps);
    int set = 60 + (dj_xf() * 117 * 2 + 32) / 64, x = set;
    if (dj_live() && (ps.lfo_live >> PL_LFO_FADER & 1)) {   /* its LFO: the mark where the swing has it, the set place
                                                             * dark grey, filled between them */
        x = 60 + ((dj_crossed(&ps) ? (32 << 8) - ps.lfo_xf8 : ps.lfo_xf8) * 117 * 2 / 256 + 32) / 64;
        g_rect((set < x ? set : x) + 1, Y.xf + 3, (set < x ? x - set : set - x) + 1, 5, C_LGREY);
        g_rect(set, Y.xf, 3, 11, C_DGREY);
    }
    g_rect(x, Y.xf, 3, 11, C_WHITE);
    g_text(184, Y.xf, &FONT_T, T("R"), C_CYAN);
    g_text(196, Y.xf, &FONT_T, T("ALG"), C_YELLOW);
}
static void draw(void)
{
    g_vline(120, BODY_Y, 112, C_DGREY);
    draw_deck(0, 2);
    draw_deck(1, 123);
    crossfader();
    draw_list();
}

/* ------------------------------------------------------------------- the knobs --- */
static void tempo_knob(int d, int s, int coarse)
{
    if (!has_song(d)) return;
    if (d == 1 && dj_sync() && has_song(0)) { ui_toast(T("SYNC: TEMPO ON THE MAIN DECK")); return; }
    if (d == 0 && clock10()) { ui_toast(T("SYNC EXTERNAL: THE CLOCK'S TEMPO")); return; }
    int v = ui_clamp(dj_deck_bpm10(d) + s * (coarse ? 10 : 1), dj.dk[d].lo10, dj.dk[d].hi10);
    dj_deck_set_bpm10(d, v);
    send_tempo();
    dj.last_k = 2; dj.last_d = (uint8_t)d;
    char nm[12], val[24], bp[12], ch[12];
    fmt_bpm(bp, bpm10(d));
    fmt_change(ch, d);
    ui_fmt(nm, sizeof nm, T("TEMPO %c"), d ? 'R' : 'L');
    ui_fmt(val, sizeof val, "%s BPM %s", bp, ch);
    ui_show_knob(nm, val);
    changed();
}
static void filter_knob(int d, int s, int coarse)
{
    uint8_t *k = dj_deck(d);
    if (!has_song(d)) return;
    int v = ui_clamp((int8_t)k[DK_FILTER] + s * (coarse ? 10 : 1), -64, 63);
    k[DK_FILTER] = (uint8_t)(int8_t)v;
    dj_deck_filter(d, v, k[DK_FLAGS] & DF_FILTER_ON);
    dj.last_k = 3; dj.last_d = (uint8_t)d;
    char nm[12], val[20], w[12];
    filter_word(w, v);
    ui_fmt(nm, sizeof nm, T("FILTER %c"), d ? 'R' : 'L');
    ui_fmt(val, sizeof val, "%s%s", w, k[DK_FLAGS] & DF_FILTER_ON ? "" : T(" (OFF)"));
    ui_show_knob(nm, val);
    changed();
}
static void vol_word(char *b, int v) { if (v <= DJ_VOL_OFF) strcpy(b, T("OFF")); else ui_fmt(b, 12, "%s%d DB", v > 0 ? "+" : "", v); }
static void volume_knob(int d, int s, int coarse)
{
    uint8_t *k = dj_deck(d);
    if (!has_song(d)) return;
    int v = ui_clamp((int8_t)k[DK_VOL] + s * (coarse ? 6 : 1), DJ_VOL_OFF, 6);
    k[DK_VOL] = (uint8_t)(int8_t)v;
    dj_deck_volume(d, v);
    dj.last_k = 4; dj.last_d = (uint8_t)d;
    char nm[12], w[12];
    vol_word(w, v);
    ui_fmt(nm, sizeof nm, T("VOLUME %c"), d ? 'R' : 'L');
    ui_show_knob(nm, w);
    changed();
}
static void xf_word(char *b)
{
    int x = dj_xf();
    if (x == 16) strcpy(b, T("MIDDLE"));
    else ui_fmt(b, 16, "%c %d%%", x < 16 ? 'L' : 'R', ((x < 16 ? 16 - x : x - 16) * 100 * 2 + 16) / 32);
}
/* HOLD down: the deck by hand, from where it stopped or the hand left it: K1 / K4 a row a click, heard (each row as if
 * it came next, its ticks over the time since the last click, then held ringing), K2 / K3 rows silently (what rang
 * let go); MOVE held: its cursor (bars, patterns) */
static void hand(int d, int rows, int heard)
{
    dk_t *k = &dj.dk[d];
    dj_deck_state_t st;
    ui_song_t s;
    dj_deck_state(d, &st);
    if (!song_info(dj_deck(d)[DK_SONG], &s)) return;
    int pos = k->hand_moved ? dj_deck(d)[DK_POS] : st.pos, row = k->hand_moved ? dj_deck(d)[DK_ROW] : st.row;
    if (!heard) dj_deck_play(d, 0);
    for (int i = 0; i < (heard ? (rows < 0 ? -rows : rows) : 1); i++) {   /* (heard: each row of a fast turn) */
        int p = pos, r = row + (heard ? (rows < 0 ? -1 : 1) : rows);
        while (r < 0 && p > 0) { p--; r += ui_pattern_rows(s.order[p]); }
        while (r >= ui_pattern_rows(s.order[p]) && p < s.npos - 1) { r -= ui_pattern_rows(s.order[p]); p++; }
        r = ui_clamp(r, 0, ui_pattern_rows(s.order[p]) - 1);
        if (p == pos && r == row) break;                /* (the song's start or end: no further) */
        pos = p;
        row = r;
        if (heard) {
            uint16_t now = (uint16_t)ui_now();
            dj_deck_hand(d, pos, row, i ? 20 : (uint16_t)(now - k->hand_t));
            k->hand_t = now;
        }
    }
    k->hand_moved = 1;
    dj_deck(d)[DK_POS] = (uint8_t)pos;
    dj_deck(d)[DK_ROW] = (uint8_t)row;
    if (!heard) dj_deck_place(d, pos, row);
    {                                                   /* a loop moves with the head; back where the deck stood, it
                                                         * is as it was */
        const uint8_t *r = dj_deck(d);
        if (pos == k->hold_pos && row == k->hold_row && r[DK_LOOP] >= 2 && !k->roll && !(r[DK_FLAGS] & DF_REPEAT_PATTERN)) {
            int rows = r[DK_LOOP_LEN] * (k->rpb ? k->rpb : 4) / 2;
            rows = rows < 1 ? 1 : rows;
            dj_loop(d, r[DK_LOOP], k->hold_lpos, k->hold_lrow, rows, 0);
            k->lp_pos = k->hold_lpos;
            k->lp_row = k->hold_lrow;
            k->lp_rows = (uint16_t)rows;
        } else
            loop_along(d, pos, row, 0);
    }
    char b[24];
    ui_fmt(b, sizeof b, T("%s ROW %s"), ui_hex2(pos), ui_hex2(row));
    ui_show_knob(T("POS"), b);
    changed();
}
static void move_turn(int d, int pats, int bars)
{
    dk_t *k = &dj.dk[d];
    ui_song_t s;
    if (!song_info(dj_deck(d)[DK_SONG], &s)) return;
    int br = bar_rows(d), pos = k->move_pos, row = k->move_row;
    if (pats) { pos = ui_clamp(pos + pats, 0, s.npos - 1); row = ui_clamp(row, 0, (ui_pattern_rows(s.order[pos]) - 1) / br * br); }
    if (bars) {
        row += bars * br;
        while (row < 0 && pos > 0) { pos--; row += (ui_pattern_rows(s.order[pos]) + br - 1) / br * br; }
        while (row >= ui_pattern_rows(s.order[pos]) && pos < s.npos - 1) { row -= (ui_pattern_rows(s.order[pos]) + br - 1) / br * br; pos++; }
        row = ui_clamp(row, 0, (ui_pattern_rows(s.order[pos]) - 1) / br * br);
    }
    k->move_pos = (uint8_t)pos;
    k->move_row = (uint8_t)row;
    k->move_turned = 1;
    char nm[12], b[24];
    ui_fmt(nm, sizeof nm, T("MOVE %c"), d ? 'R' : 'L');
    ui_fmt(b, sizeof b, T("POS %s BAR %X"), ui_hex2(pos), row / br);
    ui_show_knob(nm, b);
}
/* an LFO switched on: its shape and speed (FADER LFO: TRIANGLE 4 BARS); with its filter or effect off, that it does
 * nothing (its speed turned shows as a knob: FADER LFO 4 BARS) */
static void lfo_toast(int g)
{
    const uint8_t *bb = dj_lfo_bytes(g);
    const char *sp = T(LFO_SPEED[bb[3] > 8 ? 6 : bb[3]]);
    char b[40];
    if (g == 1 || g == 2) {
        if (!(dj_deck(g - 1)[DK_FLAGS] & DF_FILTER_ON)) ui_fmt(b, sizeof b, T("%s LFO: ITS FILTER IS OFF"), lfo_label(g));
        else ui_fmt(b, sizeof b, T("%s LFO: %s %s"), lfo_label(g), T(LFO_SHAPE[bb[1] % 6]), sp);
    } else if (g == 3) {
        static const uint8_t BIT[3] = {6, 5, 4};        /* REVERB, DELAY, CHORUS: their FX ON bits */
        int fx = DJ_REC[4] > 2 ? 0 : DJ_REC[4];
        if (!((DJ_FX[7] | DJ_FX[8] << 8) >> BIT[fx] & 1)) ui_fmt(b, sizeof b, T("%s LFO: %s IS OFF"), T(FX_NAME[fx]), T(FX_NAME[fx]));
        else ui_fmt(b, sizeof b, T("%s LFO: %s %s"), T(FX_NAME[fx]), T(LFO_SHAPE[bb[1] % 6]), sp);
    } else ui_fmt(b, sizeof b, T("FADER LFO: %s %s"), T(LFO_SHAPE[bb[1] % 6]), sp);
    ui_toast(b);
}
static void knob(int e, int s, int coarse)
{
    if (e == K_SELECT) {
        uint8_t nums[256];
        int n = ui_songs(nums, 256);
        dj.list = (int16_t)ui_clamp(dj.list + (s > 0 ? 1 : -1), 0, n - 1 > 0 ? n - 1 : 0);
        return;
    }
    if (e == K_ALG && dj.lfo_hold) {                    /* an LFO's on / off pad held: its speed */
        int i = (dj.lfo_hold - 1) >> 2;
        uint8_t *b = dj_lfo_bytes(i);
        b[3] = (uint8_t)ui_clamp((b[3] > 8 ? 6 : b[3]) + (s > 0 ? 1 : -1), 0, 8);
        dj.lfo_turned = 1;
        dj.last_k = 6; dj.last_d = (uint8_t)i;
        dj_mix();
        char nm[20];                                    /* (the header follows the turn, as any knob: `FADER LFO 1/4`) */
        ui_fmt(nm, sizeof nm, T("%s LFO"), lfo_label(i));
        ui_show_knob(nm, T(LFO_SPEED[b[3]]));
        changed();
        return;
    }
    if (dj.key_hold && (e == K_K1 || e == K_K4) && (e == K_K1) == (((dj.key_hold - 1) >> 2) == 1)) {
        int d = ((dj.key_hold - 1) >> 2) == 1 ? 0 : 1;  /* a KEY pad held: its deck's tempo knob turns KEY (the other's
                                                         * its tempo) */
        dj_deck_key(d, (int8_t)dj_deck(d)[DK_KEY] + (s > 0 ? 1 : -1));
        dj.last_k = 5; dj.last_d = (uint8_t)d;
        char nm[8], v[8];
        ui_fmt(nm, sizeof nm, T("KEY %c"), d ? 'R' : 'L');
        int kv = (int8_t)dj_deck(d)[DK_KEY];
        ui_fmt(v, sizeof v, "%s%d", kv > 0 ? "+" : "", kv);
        ui_show_knob(nm, v);
        changed();
        return;
    }
    if (e == K_ALG) {                                   /* the crossfader */
        dj_set_xf(dj_xf() + s);
        dj_mix();
        dj.last_k = 1;
        char b[16];
        xf_word(b);
        ui_show_knob(T("X-FADER"), b);
        changed();
        return;
    }
    int d = e == K_K1 || e == K_K2 ? 0 : 1, outer = e == K_K1 || e == K_K4;
    if (e < K_K1 || e > K_K4) return;
    if (dj.dk[d].move) { move_turn(d, outer ? 0 : s, outer ? s : 0); return; }
    if (dj.dk[d].hold) { hand(d, outer ? s : s * dj.dk[d].rpb, outer); return; }
    if (outer) tempo_knob(d, s, coarse);
    else if (dj.k23vol) volume_knob(d, s, coarse);
    else filter_knob(d, s, coarse);
}

/* ------------------------------------------------------------------- the keys --- */
/* the LFO pads (specs/ui/screens/dj.md#the-lfo-pads), in groups of four across both decks' keys: the crossfader's,
 * the left filter's (and the left deck's KEY), the right's, the FX LFO's (and ALL OFF) */
static void lfo_pad(int wi, int down)
{
    int g = wi >> 2, k = wi & 3, n = UI_WHITE[wi], d = g == 1 ? 0 : g == 2 ? 1 : -1;
    char b[40];
    if (!down) {
        if (dj.lfo_hold == wi + 1) {                    /* a running one let go off, unless its speed turned */
            uint8_t *bb = dj_lfo_bytes(g);
            if (dj.lfo_was_on && !dj.lfo_turned) {
                bb[0] &= (uint8_t)~1u;
                dj_mix();
                ui_fmt(b, sizeof b, T("%s LFO OFF"), lfo_label(g));
                ui_toast(b);
                changed();
            }
            dj.lfo_hold = 0;
        }
        if (dj.key_hold == wi + 1) dj.key_hold = 0;
        return;
    }
    if (g == 3 && k == 3) {                             /* ALL OFF */
        for (int i = 0; i < DJ_NLFO; i++) dj_lfo_bytes(i)[0] &= (uint8_t)~1u;
        dj_mix();
        ui_toast(T("ALL LFOS OFF"));
        changed();
        return;
    }
    if (d >= 0 && !has_song(d)) { no_song(n); return; }
    if (d >= 0 && k == 3) {                             /* KEY: held, the deck's tempo knob turns it */
        int kv = (int8_t)dj_deck(d)[DK_KEY];
        char nm[8], v[8];
        dj.key_hold = (uint8_t)(wi + 1);
        ui_fmt(nm, sizeof nm, T("KEY %c"), d ? 'R' : 'L');
        ui_fmt(v, sizeof v, "%s%d", kv > 0 ? "+" : "", kv);
        ui_show_knob(nm, v);
        return;
    }
    uint8_t *bb = dj_lfo_bytes(g);
    if (k == 0) {                                       /* on at the press; a running one off at the release */
        dj.lfo_hold = (uint8_t)(wi + 1);
        dj.lfo_turned = 0;
        dj.lfo_was_on = bb[0] & 1;
        if (!(bb[0] & 1)) {
            bb[0] |= 1;
            dj_lfo_restart(g);
            lfo_toast(g);
            changed();
        }
        return;
    }
    if (k == 1) bb[1] = (uint8_t)((bb[1] + 1) % 6);
    else if (k == 2) bb[2] = (uint8_t)((bb[2] + 1) % (g == 3 ? 2 : 4));
    else bb[0] ^= 2;                                    /* (pad 4: the crossfader's reverse) */
    dj_mix();
    const char *w = T(k == 1 ? LFO_SHAPE[bb[1] % 6] : g == 0 ? RANGE_FADER[bb[0] >> 1 & 1][bb[2] & 3]
                      : g == 3 ? RANGE_FX[bb[2] & 1] : RANGE_FILTER[bb[2] & 3]);
    ui_fmt(b, sizeof b, "%s: %s", lfo_label(g), w);
    ui_toast(b);
    changed();
}
/* LOOPS' pads (specs/ui/screens/dj.md#the-pads): 1 PATTERN | 2-4 REPEAT 4, 2, 1 BAR (the bars around, on the
 * pattern's bar grid) | 5-8 a free LOOP of 2 BARS, 1 BAR, 1/2 BAR, 1/4 from the last beat; held, a ROLL */
static void loops_pad(int d, int k, int n, int down, const dj_deck_state_t *st)
{
    dk_t *dk = &dj.dk[d];
    uint8_t *rec = dj_deck(d);
    char b[40];
    if (k == 0) {
        if (!down) return;
        if (st->ended) { refuse_key(n); return; }
        int on = !(rec[DK_FLAGS] & DF_REPEAT_PATTERN);
        dj_loop(d, on ? 1 : 0, st->pos, 0, 0, 0);
        dk->loop_pad = dk->roll = 0;                    /* (a ROLL held: gone, its release finds no loop) */
        ui_fmt(b, sizeof b, T("%s: REPEAT PATTERN %s"), side(d), on ? T("ON") : T("OFF"));
        ui_toast(b);
        changed();
        return;
    }
    if (down) {
        if (st->ended) { refuse_key(n); return; }
        int running = rec[DK_LOOP] >= 2;                /* (a REPEAT or a LOOP going round, as the stand has it) */
        if (rec[DK_LOOP] == k + 1 && !dk->roll) {       /* a second tap: off, playing on through its end */
            dj_loop(d, 0, 0, 0, 0, 0);
            dk->loop_pad = 0;
            dk->loop_off = 1;
            changed();
            return;
        }
        int rpb = dk->rpb ? dk->rpb : 4, br = bar_rows(d) ? bar_rows(d) : 16, rows, start, pos = st->pos;
        static const uint8_t BARS4[4] = {0, 4, 2, 1};
        if (k <= 3) {                                   /* the bars around (another of them: re-aligned) */
            rows = br * BARS4[k];
            start = st->row / rows * rows;
        } else {                                        /* free: from the last beat (never under 0.15 s) */
            rows = k == 4 ? 2 * br : k == 5 ? br : k == 6 ? br / 2 : rpb;
            int bpm = bpm10(d) ? bpm10(d) : 1200;
            while (rows * 4000 < rpb * bpm) rows *= 2;
            int unit = rows >= rpb ? rpb : rows;
            start = st->row / unit * unit;
            if (running) { pos = rec[DK_LOOP_POS]; start = rec[DK_LOOP_ROW]; }   /* (another: its length, the same start) */
        }
        dk->loop_pad = (uint8_t)k;
        if (!running) dk->roll = (uint8_t)st->playing;  /* (with a loop going round, another pad is no ROLL; a ROLL
                                                         * held stays one) */
        dk->loop_off = 0;
        dk->loop_t = ui_now();
        dk->lp_pos = (uint8_t)pos;
        dk->lp_row = (uint8_t)start;
        dk->lp_rows = (uint16_t)rows;
        dj_loop(d, k + 1, pos, start, rows, dk->roll);
        changed();
        return;
    }
    if (dk->loop_off || dk->loop_pad != k) { dk->loop_off = 0; return; }
    if (rec[DK_LOOP] != k + 1) { dk->loop_pad = dk->roll = 0; return; }   /* (gone meanwhile: a LOAD, RESET, PATTERN) */
    if (dk->roll) {
        if (ui_now() - dk->loop_t < PAD_TAP_MS) {      /* a tap: it loops on */
            dj_loop(d, k + 1, dk->lp_pos, dk->lp_row, dk->lp_rows, 0);
            ui_fmt(b, sizeof b, "%s: %s %s", side(d), k <= 3 ? T("REPEAT") : T("LOOP"), loop_word(k));
            ui_toast(b);
        } else {                                        /* a ROLL let go: on where it would have been */
            dj_roll_end(d);
            dk->loop_pad = 0;
        }
        dk->roll = 0;
        changed();
    }
}
static void pad(int wi, int down, int flip)    /* (flip: let go after a SWAP, its deck on the other side) */
{
    int d = (wi < 8 ? 0 : 1) ^ flip, k = wi % 8, n = UI_WHITE[wi], mode = dj.mode;
    dk_t *dk = &dj.dk[d];
    if (down) dj.pad_mode = (dj.pad_mode & ~(3u << 2 * wi)) | (uint32_t)mode << 2 * wi;
    else mode = (int)(dj.pad_mode >> 2 * wi & 3u);     /* (let go as it was pressed: a stop, CUE, a ROLL end) */
    if (mode == M_LFO) { lfo_pad(wi, down); return; }
    if (!has_song(d)) return;
    dj_deck_state_t st;
    dj_deck_state(d, &st);
    if (mode == M_LOOPS) { loops_pad(d, k, n, down, &st); return; }
    uint8_t *rec = dj_deck(d);
    if (down) dk->key_t[k] = ui_now();
    switch (k) {
    case P_SET:                                         /* SET CUE: the bar line nearest, playing on */
        if (!down) { dk->set_down = 0; return; }
        if (dk->move) { cue_set(d, dk->move_pos, dk->move_row); return; }
        if (st.ended) { refuse_key(n); ui_toast(T("THE END: CUE GOES BACK")); return; }
        if (st.playing) {                               /* (the nearest bar line past the pattern: the next position's
                                                         * start; on the last, the bar line at or before) */
            int br = bar_rows(d), pos = st.pos, row = (st.row + br / 2) / br * br;
            if (row >= st.rows) { if (pos + 1 < dk->npos) { pos++; row = 0; } else row = st.row; }
            cue_set(d, pos, row);
        } else if (dk->hold) {                          /* HOLD down: the cue on the bar line at or before the hand,
                                                         * the deck left where the hand is */
            cue_set(d, st.pos, st.row);
        } else {                                        /* paused: as CUE's tap, the deck there too */
            cue_set(d, st.pos, st.row);
            rec[DK_POS] = rec[DK_CUE_POS]; rec[DK_ROW] = rec[DK_CUE_ROW];
            dj_deck_place(d, rec[DK_POS], rec[DK_ROW]);
        }
        dk->set_down = 1; dk->set_beat = 0; dk->set_pos = st.pos; dk->set_row = st.row;     /* (held: its beat) */
        return;
    case P_CUE:                                         /* CUE: paused, set; playing, back to it while held */
        dk->cue_down = (uint8_t)down;
        if (down) {
            dk->cue_keep = 0;
            if (!st.playing && !st.ended) {
                if (st.pos == rec[DK_CUE_POS] && st.row == rec[DK_CUE_ROW]) {
                    dk->cue_held = 1;
                    dj_on(1);
                    loop_along(d, st.pos, st.row, 1);
                    dj_deck_play(d, 1);
                } else { cue_set(d, st.pos, st.row); rec[DK_POS] = rec[DK_CUE_POS]; rec[DK_ROW] = rec[DK_CUE_ROW]; dj_deck_place(d, rec[DK_POS], rec[DK_ROW]); }
            } else {                                    /* (playing, or at the song's end: back to it, playing while held;
                                                         * a LOAD waiting dropped, as by a pause) */
                dk->cue_held = 1;
                dk->load_to = DJ_NO_SONG;
                dk->load_own = 0;
                dk->load_t = 0;
                if (!st.playing) { rec[DK_FLAGS] &= (uint8_t)~DF_AT_END; dj_on(1); }
                loop_along(d, rec[DK_CUE_POS], rec[DK_CUE_ROW], 1);
                rec[DK_POS] = rec[DK_CUE_POS]; rec[DK_ROW] = rec[DK_CUE_ROW];   /* (the end's place is 0:0: a start is
                                                         * from the stand's place, as JUMP) */
                dj_deck_place(d, rec[DK_CUE_POS], rec[DK_CUE_ROW]);
                if (!st.playing) dj_deck_play(d, 1);
            }
        } else if (dk->cue_held) {
            dk->cue_held = 0;
            if (dk->cue_keep) { dk->cue_keep = 0; return; }  /* (its PLAY key meanwhile: it plays on) */
            dj_deck_play(d, 0);
            dj_deck_place(d, rec[DK_CUE_POS], rec[DK_CUE_ROW]);
            rec[DK_POS] = rec[DK_CUE_POS]; rec[DK_ROW] = rec[DK_CUE_ROW];
            changed();
        }
        return;
    case P_JUMP:                                        /* JUMP: let go, to the cue and on; held 250 ms, on the beat */
        dk->jump_down = (uint8_t)down;
        if (down) return;
        if (st.playing && ui_now() - dk->key_t[P_JUMP] >= PAD_TAP_MS) { dk->jump_beat = 1; return; }
        dj_on(1);
        loop_along(d, rec[DK_CUE_POS], rec[DK_CUE_ROW], 0);
        rec[DK_POS] = rec[DK_CUE_POS]; rec[DK_ROW] = rec[DK_CUE_ROW];   /* (paused, it plays from the stand's place) */
        dj_deck_place(d, rec[DK_CUE_POS], rec[DK_CUE_ROW]);
        rec[DK_FLAGS] &= (uint8_t)~DF_AT_END;
        if (!st.playing) start_deck(d);                 /* (synced: from the other's bar line, as its PLAY key) */
        return;
    case P_MOVE:                                        /* MOVE: held, the knobs move a cursor; let go, there */
        if (down) { dk->move = 1; dk->move_turned = 0; dk->move_pos = st.pos; dk->move_row = (uint8_t)(st.row / bar_rows(d) * bar_rows(d)); return; }
        dk->move = 0;
        if (dk->move_turned && st.playing) { dk->move_wait = 1; return; }  /* on the next beat (tick) */
        if (dk->move_turned) {
            int into = st.row % bar_rows(d);            /* on the beat of the bar it was playing */
            int row = st.playing ? dk->move_row + into : dk->move_row;
            loop_along(d, dk->move_pos, row, 0);
            rec[DK_POS] = dk->move_pos;
            rec[DK_ROW] = (uint8_t)row;
            dj_deck_place(d, dk->move_pos, row);
            changed();
        }
        return;
    default: {                                          /* the stops, one at a time, each while held */
        static const uint8_t KIND[4] = {PL_STOP_BRAKE, PL_STOP_BACKSPIN, PL_STOP_ECHO, PL_STOP_REVERB};
        if (down) {
            if (!st.playing || st.stop || dk->start_at || dk->stop) { refuse_key(n); return; }
            dk->stop = KIND[k - 4];
            dk->stay = 0;
            dj_stop(d, dk->stop, 1, 0);
            return;
        }
        if (dk->stop) {                                 /* let go: on in time, or (its PLAY key) stays stopped */
            dj_stop(d, dk->stop, 0, dk->stay);
            dk->stop = 0;
            changed();
        }
        return;
    }
    }
}
static int key(int n, int down)
{
    uint32_t bit = n >= 0 && n < 32 ? 1u << n : 0u;
    int flip = !down && (dj.keys_flip & bit);           /* (held across a SWAP: let go on its deck, the other side now) */
    if (down) dj.keys_down |= bit;
    else { dj.keys_down &= ~bit; dj.keys_flip &= ~bit; }
    int wi = ui_white_index(n);
    if (wi >= 0) { pad(wi, down, flip); return 1; }
    int d = (n == K_RPLAY || n == K_RHOLD || n == K_RFILTER || n == K_RLOAD) ^ flip;
    switch (n) {
    case K_LPLAY: case K_RPLAY:
        if (down) { if (!play_key(d, n) && has_song(d)) dj.play_at[d] = ui_now() | 1; }
        else { dj.play_at[d] = 0; dj.play_fired[d] = 0; }
        return 1;
    case K_LHOLD: case K_RHOLD:
        if (!has_song(d)) { if (down) no_song(n); return 1; }
        if (down) {
            dj.dk[d].hold = 1;
            dj.dk[d].start_at = (uint8_t)playing(d);    /* (to play on when let go) */
            dj.dk[d].hand_moved = 0;
            dj.dk[d].hand_t = (uint16_t)(ui_now() - 200);   /* (the hand's first row: its ticks over 0.2 s at least) */
            dj_deck_state_t hs;
            dj_deck_state(d, &hs);
            dj.dk[d].hold_pos = hs.pos;                 /* (where it stands, its loop's start: the head back there,
                                                         * the loop as it was) */
            dj.dk[d].hold_row = hs.row;
            dj.dk[d].hold_lpos = dj_deck(d)[DK_LOOP_POS];
            dj.dk[d].hold_lrow = dj_deck(d)[DK_LOOP_ROW];
            dj_deck_play(d, 0);
        } else {
            dj.dk[d].hold = 0;
            if (dj.dk[d].start_at) { dj.dk[d].start_at = 0; dj_deck_play(d, 1); }   /* (on from the hand's place) */
            else dj_deck_play(d, 0);                    /* (the hand's row let go: paused there) */
        }
        return 1;
    case K_LFILTER: case K_RFILTER: {
        if (!down) { dj.dk[d].filt_down = 0; dj_filter_slide(d, 0); return 1; }   /* (let go: the filter there; a quick
                                                         * press switches at once) */
        uint8_t *k = dj_deck(d);
        if (!has_song(d)) { no_song(n); return 1; }
        dj.dk[d].filt_down = 1;
        k[DK_FLAGS] ^= DF_FILTER_ON;
        dj_filter_slide(d, 1);                          /* (sliding while held, over 0.5 s at most) */
        dj_deck_filter(d, (int8_t)k[DK_FILTER], k[DK_FLAGS] & DF_FILTER_ON);
        char b[24], w[12];
        filter_word(w, (int8_t)k[DK_FILTER]);
        if (k[DK_FLAGS] & DF_FILTER_ON) ui_fmt(b, sizeof b, T("FILTER %c ON %s"), d ? 'R' : 'L', w);
        else ui_fmt(b, sizeof b, T("FILTER %c OFF"), d ? 'R' : 'L');
        ui_toast(b);
        changed();
        return 1;
    }
    case K_LLOAD: case K_RLOAD:
        if (down) load_key(d, n);
        else dj.dk[d].load_t = 0;                       /* (let go: no hold to count) */
        return 1;
    case K_SYNC:
        if (!down) return 1;
        if (!has_song(1)) { no_song(n); return 1; }
        if (dj_sync() && has_song(0)) dj_deck_set_bpm10(1, bpm10(1));   /* (off: the right deck keeps the tempo it plays at) */
        DJ_REC[6] ^= 1;
        sync_pick();
        send_tempo();
        ui_toast(dj_sync() ? T("SYNC ON") : T("SYNC OFF"));
        changed();
        return 1;
    case K_MODE:
        if (!down) return 1;
        dj.mode = dj.mode == M_LFO ? dj.mode_before : dj.mode == M_PLAY ? M_LOOPS : M_PLAY;
        ui_toast(ui_txt_v(T("PADS: #"), T(MODES[dj.mode])));
        return 1;
    default: return 1;
    }
}

/* ------------------------------------------------------------------- the buttons --- */
static char pv[24];
static const char *p_xf(void) { return DJ_REC[1] ? T("SHARP") : T("SMOOTH"); }
static void p_xf_ch(int d, int c) { (void)c; DJ_REC[1] = d > 0; dj_mix(); changed(); }
static const char *p_reso(void) { ui_fmt(pv, sizeof pv, "%d %%", DJ_REC[2]); return pv; }
static void p_reso_ch(int d, int c) { DJ_REC[2] = (uint8_t)ui_clamp(DJ_REC[2] + (d > 0 ? 1 : -1) * (c ? 10 : 1), 0, 100); dj_mix(); changed(); }
static const char *p_sends(void) { return DJ_REC[3] ? T("OFF") : T("ON"); }
static void p_sends_ch(int d, int c) { (void)c; DJ_REC[3] = d < 0; dj_mix(); changed(); }
static const char *p_lfofx(void) { static const txt_t W[3] = {TX("REVERB MIX"), TX("DELAY MIX"), TX("CHORUS MIX")}; return T(W[DJ_REC[4] > 2 ? 0 : DJ_REC[4]]); }
static void p_lfofx_ch(int d, int c) { (void)c; DJ_REC[4] = (uint8_t)ui_clamp(DJ_REC[4] + (d > 0 ? 1 : -1), 0, 2); dj_mix(); changed(); }
static const char *p_reset(void) { return T("EDIT"); }
static int p_reset_col(int on) { (void)on; return C_YELLOW; }
static void reset_answer(int a)
{
    if (a != 1) return;
    uint8_t keep[8], fx[64];
    memcpy(keep, DJ_REC, 5);
    memcpy(fx, DJ_FX, 64);
    for (int d = 0; d < 2; d++) dj_deck_play(d, 0);
    dj_record_default(DJ_REC);
    memcpy(DJ_REC, keep, 5);                            /* the properties and DJ's FX setup stay */
    memcpy(DJ_FX, fx, 64);
    for (int d = 0; d < 2; d++) { read_song(d); dj.dk[d].load_to = DJ_NO_SONG; }
    dj_mix();
    changed();
    ui_toast(T("DJ RESET"));
}
static void p_reset_edit(void)
{
    const char *l[2] = {T("RESET DJ?"), T("BOTH DECKS EMPTIED, ALL AS AT FIRST")};
    ui_ask(l, 2, T("RESET"), T("KEEP"), reset_answer);
}
/* LFO held showed the LFO pads' keys (its layer): let go, the pads are the LFOs' (already: they stay) */
static void layer_up(int b)
{
    if (b != B_LFO || dj.mode == M_LFO) return;
    dj.mode_before = dj.mode;
    dj.mode = M_LFO;
    ui_toast(ui_txt_v(T("PADS: #"), T(MODES[dj.mode])));
}
static int tap(int b)
{
    char w[16];
    switch (b) {
    case B_SEQ: swap(); return 1;
    case B_LFO:
        if (dj.mode == M_LFO) dj.mode = dj.mode_before; else { dj.mode_before = dj.mode; dj.mode = M_LFO; }
        ui_toast(ui_txt_v(T("PADS: #"), T(MODES[dj.mode])));
        return 1;
    case B_GLO: {
        const ui_prop_t items[5] = {{TX("CROSSFADER"), p_xf, p_xf_ch, 0, 0, 0},
                                    {TX("FILTER RESONANCE"), p_reso, p_reso_ch, 0, 0, 0},
                                    {TX("FILTER THE SENDS"), p_sends, p_sends_ch, 0, 0, 0},
                                    {TX("FX LFO"), p_lfofx, p_lfofx_ch, 0, 0, 0},
                                    {TX("RESET DJ"), p_reset, 0, p_reset_edit, 0, p_reset_col}};
        ui_props(T("DJ PROPERTIES"), items, 5, B_GLO);
        return 1;
    }
    case B_ENV:
        dj.k23vol = !dj.k23vol;
        ui_toast(dj.k23vol ? T("K2 K3: VOLUME") : T("K2 K3: FILTER"));
        return 1;
    case B_SEL:                                         /* the last knob turned back to its default */
        if (dj.last_k == 1) { dj_set_xf(16); dj_mix(); xf_word(w); ui_show_knob(T("X-FADER"), w); changed(); }
        else if (dj.last_k == 2 && has_song(dj.last_d)) {
            dj_deck_set_bpm10(dj.last_d, dj.dk[dj.last_d].own10);
            send_tempo();
            char bp[12];
            fmt_bpm(bp, bpm10(dj.last_d));
            ui_toast(ui_txt_v(T("TEMPO # BPM: THE SONG'S"), bp));
            changed();
        } else if (dj.last_k == 3) { filter_knob(dj.last_d, -(int8_t)dj_deck(dj.last_d)[DK_FILTER], 0); }
        else if (dj.last_k == 4) { dj_deck(dj.last_d)[DK_VOL] = 0; dj_deck_volume(dj.last_d, 0); vol_word(w, 0); ui_show_knob(T("VOLUME"), w); changed(); }
        else if (dj.last_k == 5) { dj_deck_key(dj.last_d, 0); ui_show_knob(dj.last_d ? T("KEY R") : T("KEY L"), "0"); changed(); }
        else if (dj.last_k == 6) {                      /* an LFO's speed back to 1/2 */
            dj_lfo_bytes(dj.last_d)[3] = 6;
            dj_mix();
            ui_fmt(w, sizeof w, T("%s LFO"), lfo_label(dj.last_d));
            ui_show_knob(w, T(LFO_SPEED[6]));
            changed();
        }
        return 1;
    case B_OCTP: case B_OCTM: return 1;                 /* nothing alone (in HOME's combination: home_oct) */
    case B_EDIT: case B_ARP: return 1;
    default: return 0;
    }
}
/* the PLAY button: both decks at once (1: its hold is DJ's, back to the cues). Something else playing (DJ apart
 * till its first start): PLAY pauses that, and the next PLAY plays the decks */
static int play_down(void)
{
    player_state_t st;
    if (playing(0) || playing(1)) { for (int d = 0; d < 2; d++) dj_deck_play(d, 0); changed(); return 1; }
    ui_player_state(&st);
    if (!dj_live() && st.playing) { player_pause(); ui_toast(T("PAUSED: PLAY AGAIN FOR DJ")); return 1; }
    int songs = 0, ready = 0;                           /* (no deck to start: refused, DJ not made live) */
    for (int d = 0; d < 2; d++) if (has_song(d)) { songs = 1; ready |= !(dj_deck(d)[DK_FLAGS] & DF_AT_END); }
    if (!ready) { ui_refuse(B_PLAY); ui_toast(songs ? T("THE END: CUE GOES BACK") : T("NO SONG ON THE DECK")); return 1; }
    dj_on(1);
    dj_mix();
    send_tempo();
    for (int d = 0; d < 2; d++)
        if (has_song(d) && !(dj_deck(d)[DK_FLAGS] & DF_AT_END)) {
            loop_along(d, dj_deck(d)[DK_POS], dj_deck(d)[DK_ROW], 1);
            dj_deck_play(d, 1);
        }
    return 1;
}
static int can_play(void) { return has_song(0) || has_song(1); }   /* (PLAY's LED: a deck with a song) */
/* PLAY held, HOME + PLAY: both decks back to their cue points, paused (a start waiting given up) */
static void to_cues(void)
{
    for (int d = 0; d < 2; d++) {
        uint8_t *r = dj_deck(d);
        if (!has_song(d)) continue;
        if (dj.dk[d].start_at || playing(d)) { dj.dk[d].start_at = 0; dj_deck_play(d, 0); }
        r[DK_POS] = r[DK_CUE_POS];
        r[DK_ROW] = r[DK_CUE_ROW];
        r[DK_FLAGS] &= (uint8_t)~DF_AT_END;
        dj_deck_place(d, r[DK_POS], r[DK_ROW]);
    }
    ui_toast(T("BACK TO THE CUES"));
    changed();
}
static int hold(int b)
{
    if (b != B_PLAY) return 0;
    to_cues();
    return 1;
}
/* the mutes' pages: 2 where a deck's song has more than 8 tracks */
static int mute_pages(void) { return dj.dk[0].ntrk > 8 || dj.dk[1].ntrk > 8 ? 2 : 1; }
/* HOME + OCT- / OCT+, with the white keys: the tracks they reach, 00-07 or 08-0F; nowhere to go: nothing */
static int home_oct(int b, int go)
{
    int to = dj.mute_page + (b == B_OCTP ? 1 : -1);
    if (to < 0 || to >= mute_pages()) return 0;
    if (go) {
        dj.mute_page = (uint8_t)to;
        ui_toast(to ? T("MUTES: TRACKS 08-0F") : T("MUTES: TRACKS 00-07"));
    }
    return 1;
}
/* HOME's white keys: a deck's tracks, dim while they play, dark muted (or past its song's tracks), a solo bright */
static void mute_leds(uint8_t *keys)
{
    for (int wi = 0; wi < 16; wi++) {
        int d = wi >= 8, t = wi % 8 + 8 * dj.mute_page;
        const uint8_t *k = dj_deck(d);
        unsigned m = (unsigned)(k[DK_MUTES] | k[DK_MUTES + 1] << 8);
        keys[UI_WHITE[wi]] = !has_song(d) || t >= dj.dk[d].ntrk ? 0 : k[DK_SOLO] == t ? LV_FULL
                           : k[DK_SOLO] != 0xFF || (m >> t & 1) ? 0 : LV_DIM;
    }
}
static void mute_key(int t)                             /* HOME + white key: keys 1-8 the left deck's tracks, 9-16 the right's */
{
    int d = t >= 8, tr = t % 8 + 8 * dj.mute_page;
    uint8_t *k = dj_deck(d);
    if (!has_song(d)) return;
    unsigned m = (unsigned)(k[DK_MUTES] | k[DK_MUTES + 1] << 8) ^ 1u << tr;
    k[DK_MUTES] = (uint8_t)m;
    k[DK_MUTES + 1] = (uint8_t)(m >> 8);
    dj_deck_mutes(d, m);
    char b[32];
    ui_fmt(b, sizeof b, T("%s TRACK %s %s"), side(d), ui_hex2(tr), m >> tr & 1 ? T("MUTED") : T("ON"));
    ui_toast(b);
    changed();
}
static void solo_key(int t)
{
    int d = t >= 8, tr = t % 8 + 8 * dj.mute_page;
    uint8_t *k = dj_deck(d);
    if (!has_song(d)) return;
    k[DK_SOLO] = (uint8_t)(k[DK_SOLO] == tr ? 0xFF : tr);
    char b[32];
    if (k[DK_SOLO] == 0xFF) strcpy(b, T("SOLO OFF")); else ui_fmt(b, sizeof b, T("%s TRACK %s SOLO"), side(d), ui_hex2(tr));
    ui_toast(b);
    dj_deck_mutes(d, k[DK_SOLO] == 0xFF ? (unsigned)(k[DK_MUTES] | k[DK_MUTES + 1] << 8) : ~(1u << tr) & 0xFFFFu);
    changed();
}

/* --------------------------------------------------------------------- LEDs --- */
static int key_leds(uint8_t *keys)
{
    uint32_t now = ui_now();
    int fast = (now / 150) % 2 == 0;
    for (int d = 0; d < 2; d++) {
        dj_deck_state_t st;
        dj_deck_state(d, &st);
        int has = has_song(d), b0 = d ? 8 : 0, beat = st.playing && st.row % (dj.dk[d].rpb ? dj.dk[d].rpb : 4) == 0;
        keys[d ? K_RPLAY : K_LPLAY] = (uint8_t)(!has ? 0 : dj.dk[d].start_at ? (fast ? LV_FULL : 0) : st.playing ? (beat ? LV_FULL : LV_DIM) : LV_DIM);
        keys[d ? K_RHOLD : K_LHOLD] = (uint8_t)(!has ? 0 : dj.dk[d].hold ? LV_FULL : LV_DIM);
        keys[d ? K_RFILTER : K_LFILTER] = (uint8_t)(!has ? 0 : dj_deck(d)[DK_FLAGS] & DF_FILTER_ON ? LV_FULL : LV_DIM);
        keys[d ? K_RLOAD : K_LLOAD] = (uint8_t)(dj.dk[d].load_to != DJ_NO_SONG ? (fast ? LV_FULL : 0) : LV_DIM);
        for (int k = 0; k < 8; k++) {                   /* the pads: 1 and 5 the finding light */
            int n = UI_WHITE[b0 + k], v = (k == 0 || k == 4) ? LV_DIM : 0;
            if (dj.mode == M_PLAY && has) {
                if (k == P_CUE && !st.playing && st.pos == dj_deck(d)[DK_CUE_POS] && st.row == dj_deck(d)[DK_CUE_ROW]) v = LV_FULL;
                if (k == P_CUE && dj.dk[d].cue_held) v = LV_FULL;
                if (k == P_MOVE && dj.dk[d].move) v = LV_FULL;
            }
            if (dj.mode == M_PLAY && k >= 4 && dj.dk[d].stop && k == (int)dj.dk[d].stop + 3) v = LV_FULL;
            if (dj.mode == M_LOOPS && k == 0 && dj_deck(d)[DK_FLAGS] & DF_REPEAT_PATTERN) v = LV_FULL;
            if (dj.mode == M_LOOPS && k && dj_deck(d)[DK_LOOP] == k + 1) v = LV_FULL;
            if (dj.mode == M_LFO) {                     /* each four's first dim (on / off), the rest dark; full what is
                                                         * on: an LFO running, the reverse, a deck's KEY not 0 */
                int g = (b0 + k) >> 2, kk = k & 3, dd = g == 1 ? 0 : g == 2 ? 1 : -1;
                const uint8_t *bb = dj_lfo_bytes(g);
                v = dd >= 0 && !has_song(dd) ? (kk == 0 ? LV_DIM : 0)   /* (a deck with no song: the finding light) */
                  : kk == 0 ? (bb[0] & 1 ? LV_FULL : LV_DIM)
                  : kk == 3 && g == 0 ? (bb[0] & 2 ? LV_FULL : 0)
                  : kk == 3 && dd >= 0 ? ((int8_t)dj_deck(dd)[DK_KEY] || dj.key_hold == b0 + k + 1 ? LV_FULL : 0) : 0;
            }
            keys[n] = (uint8_t)v;
        }
    }
    keys[K_SYNC] = (uint8_t)(!has_song(1) ? 0 : dj_sync() ? LV_FULL : LV_DIM);
    keys[K_MODE] = LV_DIM;
    for (int n = 0; n < 27; n++) if (dj.flash[n] > now) keys[n] = (uint8_t)(fast ? LV_FULL : 0);
    return 1;
}
static int lit(int b)
{
    if (b == B_LFO) return dj.mode == M_LFO ? LV_FULL : LV_DIM;
    if (b == B_ENV) return dj.k23vol ? LV_FULL : LV_DIM;
    if (b == B_SEQ) return has_song(0) || has_song(1) ? LV_DIM : 0;
    if (b == B_EDIT || b == B_ARP) return 0;
    if (b == B_OCTM || b == B_OCTP) return 0;           /* (lit in HOME's combination only: home_oct) */
    return LV_NONE;
}

/* --------------------------------------------------------------- the frame --- */
static const char *sub(void)
{
    return T(MODES[dj.mode]);
}
/* SEL held's layer: the knobs' big steps */
static int sel_lines(const char **lines, int max)
{
    if (max < 2) return 0;
    lines[0] = T("K1, K4: THE TEMPO BY 1 BPM");
    lines[1] = dj.k23vol ? T("K2, K3: THE VOLUMES BY 6 DB") : T("K2, K3: THE FILTERS BY 10");
    return 2;
}
/* the footer's left (specs/ui/screens/dj.md#texts): the pads' keys by their mode; a key or pad held, what it does
 * (the first that applies) */
static int foot_hints(g_run_t *r, int max)
{
    static char w[32], fx[12];
    int n = 0, d;
#define RUN(s_, c_) do { if (n < max) { r[n].s = (s_); r[n].c = (uint8_t)(c_); n++; } } while (0)
    for (d = 0; d < 2; d++) if (dj.dk[d].hold) {        /* a HOLD key: the deck by hand */
        RUN(d ? T("K4") : T("K1"), C_YELLOW); RUN(T(" ROWS HEARD  "), C_LGREY); RUN(d ? T("K3") : T("K2"), C_YELLOW);
        RUN(T(" BEATS"), C_LGREY);
        return n;
    }
    for (d = 0; d < 2; d++) if (dj.dk[d].stop) {        /* a stop held: what letting it go does; its PLAY key turns it */
        int stay = dj.dk[d].stay;
        RUN(T("LET GO"), C_YELLOW); RUN(stay ? T(" STAYS  ") : T(" PLAYS ON  "), C_LGREY);
        RUN(d ? T("OP6") : T("OP1"), C_YELLOW); RUN(stay ? T(" PLAYS ON") : T(" STAYS"), C_LGREY);
        return n;
    }
    for (d = 0; d < 2; d++) if (dj.dk[d].cue_held) {    /* CUE held playing from the cue: the same */
        int keep = dj.dk[d].cue_keep;
        RUN(T("LET GO"), C_YELLOW); RUN(keep ? T(" PLAYS ON  ") : T(" CUE  "), C_LGREY);
        RUN(d ? T("OP6") : T("OP1"), C_YELLOW); RUN(keep ? T(" CUE") : T(" PLAYS ON"), C_LGREY);
        return n;
    }
    for (d = 0; d < 2; d++) if (dj.dk[d].jump_down) {
        RUN(T("LET GO"), C_YELLOW);
        RUN(ui_now() - dj.dk[d].key_t[P_JUMP] < PAD_TAP_MS ? T(" JUMPS TO THE CUE") : T(" CUE ON THE BEAT"), C_LGREY);
        return n;
    }
    for (d = 0; d < 2; d++) if ((dj.dk[d].cue_down || dj.dk[d].set_down) && has_song(d)) {   /* the cue point */
        int br = bar_rows(d) ? bar_rows(d) : 16, rpb = dj.dk[d].rpb ? dj.dk[d].rpb : 4, row = dj_deck(d)[DK_CUE_ROW];
        ui_fmt(w, sizeof w, T(" POS %s BAR %X"), ui_hex2(dj_deck(d)[DK_CUE_POS]), row / br);
        if (row % br / rpb) ui_fmt(w + strlen(w), (int)(sizeof w - strlen(w)), T(" BEAT %d"), row % br / rpb);
        RUN(T("CUE"), C_YELLOW); RUN(w, C_LGREY);
        return n;
    }
    for (d = 0; d < 2; d++) if (dj.dk[d].move) {
        RUN(d ? T("K4") : T("K1"), C_YELLOW); RUN(T(" BARS  "), C_LGREY); RUN(d ? T("K3") : T("K2"), C_YELLOW);
        RUN(T(" PATTERNS"), C_LGREY);
        return n;
    }
    if (dj.lfo_hold) {                                  /* an LFO's on / off pad: ALG its speed; what letting go does */
        RUN(T("ALG"), C_YELLOW); RUN(T(" SPEED  "), C_LGREY);
        RUN(T("LET GO"), C_YELLOW); RUN(dj.lfo_was_on && !dj.lfo_turned ? T(" OFF") : T(" STAYS ON"), C_LGREY);
        return n;
    }
    if (dj.key_hold) {
        RUN(((dj.key_hold - 1) >> 2) == 1 ? T("K1") : T("K4"), C_YELLOW); RUN(T(" KEY, A SEMITONE A CLICK"), C_LGREY);
        return n;
    }
    if (dj.mode == M_PLAY) { RUN(T("SET CUE JUMP MOVE"), C_LGREY); RUN("|", C_DGREY); RUN(T("STOPS"), C_LGREY); }
    else if (dj.mode == M_LOOPS) {
        RUN("1", C_YELLOW); RUN(T(" PATTERN "), C_LGREY); RUN("2-4", C_YELLOW); RUN(T(" BARS "), C_LGREY);
        RUN("5-8", C_YELLOW); RUN(T(" FREE"), C_LGREY);
    } else {                                            /* (the FX LFO's effect, as GLO picks it) */
        ui_fmt(fx, sizeof fx, " %s", lfo_label(3));
        RUN("1", C_YELLOW); RUN(T(" FADER "), C_LGREY); RUN("5", C_YELLOW); RUN(T(" FILTER "), C_LGREY); RUN("13", C_YELLOW);
        RUN(fx, C_LGREY);
    }
#undef RUN
    return n;
}
/* the footer's right (specs/ui/screens/dj.md#texts): the FX LFO's MIX as it swings, white, while the LFO is on
 * (`REV 62%`; `REV OFF` while DJ's FX has the effect off); the voices are the header's (frame.md#the-header) */
static const char *footer(uint8_t *col)
{
    static char b[16];
    static const txt_t SHORT[3] = {TX("REV"), TX("DLY"), TX("CHO")};
    static const uint8_t BIT[3] = {6, 5, 4};            /* REVERB, DELAY, CHORUS: their FX ON bits */
    if (!(dj_lfo_bytes(DJ_LFO_FX)[0] & 1)) return 0;
    player_state_t st;
    ui_player_state(&st);
    int fx = DJ_REC[4] > 2 ? 0 : DJ_REC[4];
    if (!((DJ_FX[7] | DJ_FX[8] << 8) >> BIT[fx] & 1)) ui_fmt(b, sizeof b, T("%s OFF"), T(SHORT[fx]));
    else if (dj_live() && st.lfo_mix >= 0) ui_fmt(b, sizeof b, "%s %d%%", T(SHORT[fx]), st.lfo_mix);
    else ui_fmt(b, sizeof b, "%s", T(SHORT[fx]));
    *col = C_WHITE;
    return b;
}
/* a deck paused: the stand keeps where it is */
static void keep_place(int d, const dj_deck_state_t *st)
{
    uint8_t *r = dj_deck(d), end = st->ended ? DF_AT_END : 0;
    if (r[DK_POS] != st->pos || r[DK_ROW] != st->row) { r[DK_POS] = st->pos; r[DK_ROW] = st->row; changed(); }
    if ((r[DK_FLAGS] & DF_AT_END) != end) { r[DK_FLAGS] = (uint8_t)((r[DK_FLAGS] & ~DF_AT_END) | end); changed(); }
}
/* the decks standing otherwise than written (specs/files/dj.md#the-stand) and not an edit yet: a change put once both
 * decks pause, or a deck playing (its place moves on). One unsaved change (ui_unsaved_refresh), whatever the screen */
int ui_dj_stand_moved(void)
{
    if (dj.loads != (uint8_t)dj_loads()) return 0;     /* (the stand reloaded since: its change gone) */
    return dj.dirty || playing(0) || playing(1);
}
/* a save with the decks paused: where they stand into the stand, put as the edit it saves */
void ui_dj_stand_put(void)
{
    dj_deck_state_t st;
    if (dj.loads != (uint8_t)dj_loads() || playing(0) || playing(1)) return;
    for (int d = 0; d < 2; d++) if (dj_deck_state(d, &st)) keep_place(d, &st);
    if (dj.dirty) put_stand();
}
/* what waits for a deck's bar line or beat (a LOAD, JUMP and MOVE on the beat, SET CUE's beat, a synced start), the
 * LOAD and PLAY keys' holds, and where a paused deck stands: DJ's tick, and while DJ is live on every other screen
 * too (ui_dj_poll), so what waits lands in time wherever the screen is. shown: DJ is the screen (a row redraws it) */
/* SET CUE held 250 ms: the cue at the start of the beat its press fell in; paused, the deck there too (HOLD down:
 * where the hand is). st: the deck as read (the stand's while DJ is not live), moved with it */
static void set_cue_beat(int d, dj_deck_state_t *st)
{
    dk_t *k = &dj.dk[d];
    if (!k->set_down || k->set_beat || ui_now() - k->key_t[P_SET] < PAD_TAP_MS) return;
    int br = bar_rows(d) ? bar_rows(d) : 16, rpb = k->rpb ? k->rpb : 4, row = k->set_row / rpb * rpb;
    k->set_beat = 1;
    dj_deck(d)[DK_CUE_POS] = k->set_pos;
    dj_deck(d)[DK_CUE_ROW] = (uint8_t)row;
    if (!st->playing && !k->hold) {
        dj_deck(d)[DK_POS] = st->pos = k->set_pos;
        dj_deck(d)[DK_ROW] = st->row = (uint8_t)row;    /* (st too: keep_place copies it next) */
        dj_deck_place(d, k->set_pos, row);
    }
    char b[40];
    ui_fmt(b, sizeof b, T("CUE: POS %s BAR %X BEAT %d"), ui_hex2(k->set_pos), row / br, row % br / rpb);
    ui_toast(b);
    changed();
}
static void waits(int shown)
{
    if (dj.loads != (uint8_t)dj_loads()) {              /* the stand reloaded: its change not yet put is gone too,
                                                         * the overviews are the new songs' */
        dj.loads = (uint8_t)dj_loads();
        dj.dirty = 0;
        for (int d = 0; d < 2; d++) {                   /* (what waited, the decks before's: dropped) */
            dk_t *k = &dj.dk[d];
            read_song(d);
            k->load_to = DJ_NO_SONG;
            k->load_t = 0;
            k->jump_beat = k->move_wait = k->start_at = 0;
        }
    }
    for (int d = 0; d < 2; d++) {
        dj_deck_state_t st;
        dk_t *k = &dj.dk[d];
        if (!dj_deck_state(d, &st)) {                   /* DJ not live (another song started, or none yet): no LOAD
                                                         * waits (only a playing deck's does); SET CUE's beat on the
                                                         * stand */
            k->load_to = DJ_NO_SONG;
            k->load_own = 0;
            k->load_t = 0;
            set_cue_beat(d, &st);
            continue;
        }
        int br = bar_rows(d) ? bar_rows(d) : 16;
        if (k->start_at == 1 && st.armed && !k->hold) k->start_at = 2;            /* (a synced start: the arm taken) */
        if (k->start_at == 2 && !st.armed && !st.playing) k->start_at = 0;       /* (... dropped by the player: PLAY
                                                         * pausing both, a LOAD, RESET) */
        if (k->start_at && st.playing && !st.armed && !k->hold) k->start_at = 0;   /* (a synced start: it started) */
        if (k->load_to != DJ_NO_SONG && !st.playing) {
            if (st.ended) {                             /* a song that stops: its end is its last bar line, the new
                                                         * song plays on (st as the player will have it: keep_place
                                                         * leaves the stand) */
                load_now(d, k->load_to, k->load_own, 1);
                st.ended = 0;
                st.playing = 1;
            } else k->load_t = 0;                        /* the deck paused (its PLAY, HOLD, the PLAY key's hold, the
                                                         * PLAY button): dropped, its hold with it */
            k->load_to = DJ_NO_SONG;
            k->load_own = 0;
        }
        if (st.row != dj.last_row[d]) {
            dj.last_row[d] = (int16_t)st.row;
            if (st.row % br == 0 && st.playing && k->load_to != DJ_NO_SONG) {   /* a LOAD waiting: the bar line */
                load_now(d, k->load_to, k->load_own, 1);
                k->load_to = DJ_NO_SONG;
            }
            int rpb = k->rpb ? k->rpb : 4;
            if (st.row % rpb == 0 && k->jump_beat) {    /* JUMP held: to the cue on the beat */
                k->jump_beat = 0;
                loop_along(d, dj_deck(d)[DK_CUE_POS], dj_deck(d)[DK_CUE_ROW], 0);
                dj_deck_place(d, dj_deck(d)[DK_CUE_POS], dj_deck(d)[DK_CUE_ROW]);
            }
            if (st.row % rpb == 0 && k->move_wait) {    /* MOVE let go: into the cursor's bar on the beat, in phase */
                k->move_wait = 0;
                int row = k->move_row + st.row % br;
                loop_along(d, k->move_pos, row, 0);
                dj_deck(d)[DK_POS] = k->move_pos;
                dj_deck(d)[DK_ROW] = (uint8_t)row;
                dj_deck_place(d, k->move_pos, row);
                changed();
            }
            if (shown) ui_dirty();
        }
        set_cue_beat(d, &st);
        if (!st.playing && shown) keep_place(d, &st);   /* (the stand follows a paused deck while DJ shows it; a save
                                                         * elsewhere puts it: ui_dj_stand_put) */
        /* LOAD held past the hold delay on a playing deck (load_t: its press, till it is let go): the song's own
         * tempo, at the bar line, or at once if the bar line came first (a synced right deck the main's either way) */
        if (k->load_t && (int32_t)(ui_now() - k->load_t) >= ui_set[S_HOLD_DELAY]) {   /* (load_t may be 1 ms
                                                         * ahead: signed) */
            ui_song_t s;
            char b[48];
            uint8_t song = k->load_to != DJ_NO_SONG ? k->load_to : dj_deck(d)[DK_SONG];
            k->load_t = 0;
            if (k->load_to != DJ_NO_SONG) k->load_own = 1;
            else if (!(d == 1 && dj_sync() && has_song(0))) {
                dj_deck_set_bpm10(d, k->own10);
                if (d == 0) sync_pick();                /* (as at the bar line: load_now) */
                send_tempo();
                changed();
            }
            if (ui_song(song, &s)) { ui_fmt(b, sizeof b, T("%s: %s AT ITS TEMPO"), side(d), s.name); ui_toast(b); }
        }
        /* a deck's PLAY key held past the hold delay: back to its start, paused. (play_at is ui_now() | 1, up to 1 ms
         * ahead of now: compared signed, or a press in the same pass would be a hold at once) */
        if (dj.play_at[d] && !dj.play_fired[d] && (int32_t)(ui_now() - dj.play_at[d]) >= ui_set[S_HOLD_DELAY]) {
            dj.play_fired[d] = 1;                       /* (the state line says so till it is let go) */
            dj_deck_play(d, 0);
            dj_deck_place(d, 0, 0);
            dj_deck(d)[DK_POS] = dj_deck(d)[DK_ROW] = 0;
            dj_deck(d)[DK_FLAGS] &= (uint8_t)~DF_AT_END;
            char b[32];
            ui_fmt(b, sizeof b, T("%s: BACK TO THE START"), side(d));
            ui_toast(b);
            changed();
        }
    }
}
/* (live only after DJ was shown: its waits set; the stand reloaded meanwhile (REVERT, a seed): nothing waits, DJ's tick
 * reads it again) */
void ui_dj_poll(void) { if (!dj.shown && dj.loads == (uint8_t)dj_loads() && dj_live()) waits(0); }
static void tick(void)
{
    waits(1);
    if (dj.dirty && !playing(0) && !playing(1) && ui_now() - dj.dirty_at > 600) put_stand();
}
static void enter(const ui_screen_t *from)
{
    static uint8_t once;
    uint8_t nums[256];
    (void)from;
    dj_read_state = ui_player_state;                    /* (the decks as the frame shows them) */
    dj_ensure();
    dj_enter();                                         /* (what plays plays on: DJ apart till its first start) */
    for (int d = 0; d < 2; d++) {
        read_song(d);
        if (!once) { dj.dk[d].load_to = DJ_NO_SONG; dj.dk[d].start_at = 0; }   /* (later what waits waited on) */
    }
    if (!once) dj.list = -1;                            /* (the first entry: the current song highlighted) */
    once = 1;
    dj.shown = 1;
    int ns = ui_songs(nums, 256);                       /* (songs deleted meanwhile: the current song highlighted, else
                                                         * the last; fewer tracks after a LOAD: the first mutes) */
    if (dj.list < 0 || dj.list >= ns) {
        dj.list = (int16_t)(ns > 0 ? ns - 1 : 0);
        for (int i = 0; i < ns; i++) if (nums[i] == ui_cur.song) dj.list = (int16_t)i;
    }
    if (dj.mute_page >= mute_pages()) dj.mute_page = 0;
}
static void leave(const ui_screen_t *to)
{
    (void)to;
    dj.shown = 0;
    for (int d = 0; d < 2; d++) {
        dk_t *k = &dj.dk[d];
        k->hold = k->move = k->cue_down = k->jump_down = k->filt_down = k->set_down = 0;   /* (CUE held playing: its
                                                         * release reaches DJ on any screen, back to the cue then) */
        dj.play_at[d] = 0;
        dj.play_fired[d] = 0;
        if (k->stop) { dj_stop(d, k->stop, 0, k->stay); k->stop = 0; }   /* (a hold let go with the screen) */
        if (k->roll && k->loop_pad) { dj_roll_end(d); k->loop_pad = 0; }
        k->roll = 0;
    }
    dj.lfo_hold = dj.key_hold = 0;
    if (dj.dirty && !playing(0) && !playing(1)) put_stand();
}

/* the state's cursor (specs/connections.md#the-state): the song the list highlights, the pads' mode, what K2 and
 * K3 turn */
static void cursor(uint8_t *c)
{
    uint8_t nums[256];
    int ns = ui_songs(nums, 256);
    c[0] = dj.list >= 0 && dj.list < ns ? nums[dj.list] : DJ_NO_SONG;
    c[1] = dj.mode;
    c[2] = dj.k23vol;
}

/* SYNC EXTERNAL: the master's START plays the main deck from its cue point, STOP pauses it, CONTINUE plays it on
 * (as its PLAY key); a song position does nothing here: the decks are moved by hand (specs/ui/screens/dj.md) */
static void transport(int st, int spp)
{
    uint8_t *rec = dj_deck(0);
    (void)spp;
    if (!has_song(0)) return;
    if (st == 0xFA) {
        if (!dj_on(1)) return;
        dj_mix();
        send_tempo();
        loop_along(0, rec[DK_CUE_POS], rec[DK_CUE_ROW], 0);
        rec[DK_POS] = rec[DK_CUE_POS];
        rec[DK_ROW] = rec[DK_CUE_ROW];
        rec[DK_FLAGS] &= (uint8_t)~DF_AT_END;
        if (playing(0)) dj_deck_place(0, rec[DK_POS], rec[DK_ROW]);
        else dj_deck_play(0, 1);
        changed();
    } else if (st == 0xFC) {
        if (playing(0)) { dj_deck_play(0, 0); changed(); }
    } else if (st == 0xFB) {
        if (playing(0) || (rec[DK_FLAGS] & DF_AT_END) || !dj_on(1)) return;
        dj_mix();
        send_tempo();
        loop_along(0, rec[DK_POS], rec[DK_ROW], 1);
        dj_deck_play(0, 1);
        changed();
    }
}

const ui_screen_t SCR_DJ = {
    .name = TX("DJ"), .group = G_PLAY, .no_keyboard = 1,
    .does = BIT(B_SEQ) | BIT(B_LFO) | BIT(B_GLO) | BIT(B_ENV) | BIT(B_SEL),
    .enter = enter, .leave = leave, .draw = draw, .sub = sub, .footer = footer, .knob = knob, .tap = tap, .key = key,
    .lit = lit, .key_leds = key_leds, .play_down = play_down, .can_play = can_play, .tick = tick, .mute_key = mute_key,
    .solo_key = solo_key, .mute_leds = mute_leds, .home_oct = home_oct, .foot_hints = foot_hints, .layer_up = layer_up,
    .sel_lines = sel_lines, .own_arp = UI_ARP_NONE, .cursor = cursor, .transport = transport, .hold = hold,
    .to_start = to_cues,
};
