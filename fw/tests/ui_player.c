/* SPDX-License-Identifier: GPL-3.0-only
 * Copyright (C) 2026 Vasily Kazantsev */
/* The UI test's player: play/player.h's calls, with no sound, on the songs' records: rows go by at each
 * song's tempo (a row: TICKS A ROW * 2.5 / TEMPO s), positions in its order, a stop at the end when the
 * song says so. Enough for the screens' tests to be deterministic and apart from the sound's code. */
#include <math.h>
#include <string.h>
#include "play/player.h"
#include "play/rec.h"
#include "plat.h"

static player_state_t st;
static uint8_t stepping;                        /* player_step: the row held */
static uint32_t row_frames, frames;            /* a row's length in frames, the frames into it */
static uint8_t song_len, stop_at_end;

static void load(uint8_t song)
{
    rec_song_t s;
    if (rec_song(song, &s)) { st.what = PL_NONE; return; }
    song_len = (uint8_t)(s.len - 1);
    stop_at_end = s.flags & RS_STOP;
    row_frames = (uint32_t)s.ticks * 44100u * 5u / (2u * s.tempo);
    st.speed = s.ticks;
    st.bpm10 = (uint16_t)(s.tempo * 240u / ((uint32_t)s.ticks * s.rpb));
}
static void at_pos(void)
{
    rec_song_t s;
    rec_pat_t p;
    if (rec_song(st.song, &s)) return;
    st.pat = s.order[st.pos];
    if (!rec_pattern(st.pat, &p)) { st.rows = (uint8_t)(p.rows - 1); st.ntrk = p.ntrk; }
}
void player_init(void) { memset(&st, 0, sizeof st); st.voice_count = 12; }
void (*player_out)(const int32_t *lr, int at, int n);
void player_audio(int16_t *lr, int n)
{
    if (lr)
        memset(lr, 0, (size_t)n * 4);
    if (!st.playing || st.what != PL_SONG) return;
    frames += (uint32_t)n;
    while (row_frames && frames >= row_frames) {
        frames -= row_frames;
        st.rows_played++;
        if (st.row < st.rows) { st.row++; continue; }
        st.row = 0;
        if (st.pos < song_len) { st.pos++; at_pos(); continue; }
        if (stop_at_end) { st.playing = 0; st.ended = 1; st.row = st.rows; return; }
        st.pos = 0;
        at_pos();
    }
}
void player_play_song(uint8_t song, uint8_t pos, uint8_t row)
{
    st.what = PL_SONG; st.song = song; st.pos = pos; st.row = row; st.playing = 1; st.ended = 0; frames = 0;
    load(song);
    at_pos();
}
void player_play_pattern(uint8_t pat, uint8_t row) { st.what = PL_LOOP; st.pat = pat; st.row = row; st.playing = 1; }
void player_play_pattern_at(uint8_t pat, uint8_t row, uint16_t tempo, uint8_t ticks, uint8_t rpb, uint8_t swing)
{
    (void)tempo; (void)ticks; (void)rpb; (void)swing;
    player_play_pattern(pat, row);
}
void player_pause(void) { st.playing = 0; }
void player_continue(void) { if (st.what != PL_NONE) { st.playing = 1; st.ended = 0; stepping = 0; } }
void player_to_start(void) { st.pos = 0; st.row = 0; frames = 0; at_pos(); }
void player_stop(void) { st.playing = 0; st.pos = 0; st.row = 0; at_pos(); }
void player_skip(int d) { int p = st.pos + d; if (p >= 0 && p <= song_len) { st.pos = (uint8_t)p; st.row = 0; at_pos(); } }
void player_all_sound_off(void) {}
void player_hush(int on) { (void)on; }
void player_mutes(uint32_t m) { st.muted = m; }
void player_voice_count(int n) { st.voice_count = (uint8_t)n; }
void player_song_changed(uint8_t song) { (void)song; }
void player_store_changed(void) {}
/* LOOPS' loops as a source: they play as what = PL_LOOPS, no rows going by here */
void player_play_source(uint8_t what, const pl_source_t *src, uint8_t pos, uint8_t row)
{
    (void)src;
    st.what = what; st.pos = pos; st.row = row; st.playing = 1; st.ended = 0;
}
void player_play_source_silent(const pl_source_t *src, uint8_t place) { (void)place; player_play_source(PL_LOOPS, src, 0, 0); }
void player_lp_rest(int how) { (void)how; }
void player_source_changed(int rows_changed) { (void)rows_changed; }
void player_source_end(uint8_t what) { st.what = what; st.playing = 0; }
/* LOOPS' lanes: their mutes kept as the muted tracks, the rest nothing here */
void player_lp_mutes(uint32_t muted, uint32_t soloed) { st.muted = soloed & 0xFFFFu ? ~soloed & 0xFFFFu : muted; }
void player_lp_mutes_at_bar(uint32_t muted, uint32_t soloed, int bar_rows) { (void)bar_rows; player_lp_mutes(muted, soloed); }
void player_lp_note(int loop, int lane, uint8_t note, uint8_t vol, int row, int delay, unsigned glide)
{
    (void)loop; (void)lane; (void)note; (void)vol; (void)row; (void)delay; (void)glide;
}
void player_lp_off(int loop, int lane, int row) { (void)loop; (void)lane; (void)row; }
void player_lp_keys(int lane, pl_inst_t in, uint8_t note, uint8_t vol, int row, unsigned glide)
{
    (void)lane; (void)in; (void)note; (void)vol; (void)row; (void)glide;
}
void player_lp_take(int loop) { (void)loop; }
void player_lp_preview(const pl_lp_grid_t *g) { (void)g; }
void player_lp_preview_alone(int on) { (void)on; }
void player_lp_preview_keep(int loop) { (void)loop; }
/* AUTO LEVEL's measurement: every instrument as loud (-30 dB, its peak -12 dB) */
int player_measure(pl_inst_t in, void *mem, uint32_t size, uint32_t (*lock)(void), void (*unlock)(uint32_t),
                   uint32_t *ms, int32_t *peak)
{
    (void)in; (void)mem; (void)size; (void)lock; (void)unlock;
    *ms = 1u << 20;
    *peak = 8192;
    return 1;
}
/* a count-in: none here (what waits for it goes at once) */
void player_count_in(int bars, int bpm10) { (void)bars; (void)bpm10; }
void player_count_stop(void) {}
void (*player_usb)(const int32_t *lr, const int16_t *click, int n);
/* (the test's: the row moved by d, as the audio would between a frame's two halves) */
void ui_player_bump_row(int d) { st.row = (uint8_t)(st.row + d); }
/* the rest of player.h, doing nothing here */
void player_tempo_share(uint32_t q16) { (void)q16; }
void player_song_volume(int db) { (void)db; }
void player_end_mode(uint8_t mode, uint8_t next) { (void)mode; (void)next; }
void player_fx_mine(uint8_t e, uint64_t codes) { (void)e; (void)codes; }
void player_editing(uint8_t what, uint32_t key) { (void)what; (void)key; }
void player_cue(uint8_t song) { (void)song; }
void player_unload(void) {}
void player_cue_loops(const pl_source_t *src) { (void)src; }
void player_keys_gain(int db) { (void)db; }
void player_keys_dry(int on) { (void)on; }
void player_keys_mix(const uint8_t *mix) { (void)mix; }
int32_t player_duck_env(void) { return 0; }
void player_click(int accent) { (void)accent; }
void player_costs(player_costs_t *c, int reset) { (void)reset; memset(c, 0, sizeof *c); }
/* the arpeggiator (nothing sounds here) */
void player_arp(int on, uint8_t rhythm, uint8_t pattern, uint8_t latch) { (void)on; (void)rhythm; (void)pattern; (void)latch; }
void player_arp_clear(void) {}
void player_arp_start(uint8_t note, pl_inst_t inst, uint8_t vel) { (void)note; (void)inst; (void)vel; }
void player_arp_inst(pl_inst_t inst) { (void)inst; }
/* stepping: the first step pauses and plays the current row again, then a row each way */
void player_step(int rows)
{
    if (st.what != PL_SONG) return;
    if (st.playing || !stepping) { st.playing = 0; stepping = 1; return; }
    int r = st.row + rows;
    if (r < 0) { if (st.pos) { st.pos--; at_pos(); r = st.rows; } else r = 0; }
    if (r > st.rows) { if (st.pos < song_len) { st.pos++; at_pos(); r = 0; } else r = st.rows; }
    st.row = (uint8_t)r;
}
static int punch = -1;
void player_punch(int k, int down) { punch = down ? k : -1; }
void player_take_cell(int track, int row, uint32_t at, int off) { (void)track; (void)row; (void)at; (void)off; }
void player_fx_cmd(uint8_t code, uint8_t value) { (void)code; (void)value; }
void player_fx_reload(void) {}
void player_fx_bypass(int on) { (void)on; }
int player_master(int16_t *lr, int n) { memset(lr, 0, (size_t)n * 4); return n; }
void player_levels(uint16_t *lv) { memset(lv, 0, 16 * sizeof *lv); }
/* the effects' side the screens call (fx/fx.h) */
void fx_gate_pattern(int i, const uint8_t *steps) { (void)i; (void)steps; }
/* (FX, VISUAL: the setup as it plays, the GATE patterns' steps, what works) */
void fx_now(uint8_t *setup) { memset(setup, 0, 64); }
void fx_gate_steps(int i, uint8_t *steps) { memset(steps, i == 16 ? 1 : 0, 16); }
uint32_t fx_active(void) { return 0; }
void fx_watch(int on) { (void)on; }
int32_t fx_meter_q15(int e) { (void)e; return 0; }
uint8_t fx_gate_step(void) { return 0; }
int fx_met_beat(void) { return -1; }
/* (FILTER's envelope: at rest; its times by step as fw/tools/gen_fx.py makes them) */
uint32_t fx_filter_open(void) { return 0; }
uint32_t fx_filter_hits(void) { return 0; }
uint32_t fx_env_t10(int decay, int step)
{
    step = step < 0 ? 0 : step > 127 ? 127 : step;
    return (uint32_t)lround((decay ? 50.0 : 20.0) * pow(decay ? 1000.0 : 1500.0, step / 127.0));
}
void player_key_on(uint8_t note, pl_inst_t inst, uint8_t vel) { (void)note; (void)inst; (void)vel; }
void player_key_off(uint8_t note) { (void)note; }
void player_keys_off(void) {}
void player_ext_tempo(uint16_t bpm10) { (void)bpm10; }
void player_ext_clock(uint32_t n, uint32_t at_ms, uint32_t per_us) { (void)n; (void)at_ms; (void)per_us; }
uint32_t player_block_frame(void) { return 0; }
void player_sync_pulse(int side) { (void)side; }
void player_keys_bend(int16_t bend64) { (void)bend64; }
void (*volatile player_note_hook)(uint8_t track, uint8_t note, uint8_t vel);
void player_state(player_state_t *s, int reset_max) { (void)reset_max; *s = st; }
int player_voices_now(pl_voice_info_t *v, int max) { (void)v; (void)max; return 0; }
int player_scope(int voice, int16_t *dst, int n) { (void)voice; memset(dst, 0, (size_t)n * 2); return n; }
/* DJ's decks (the stand-in plays none) */
void player_dj(int on, const uint8_t *fx_setup) { (void)on; (void)fx_setup; }
uint32_t player_dj_mem_size(void) { return 16; }
/* (STATUS' RAM MAP: the sound's buffers as the FM-1's release build has them) */
uint32_t player_ram(void) { return 22144u; }
unsigned int ibxm_arena_size(void) { return 6400u; }
uint32_t fx_ram(void) { return 150050u; }
uint32_t dx7_ram(void) { return 28676u; }
void player_dj_mem(void *mem) { (void)mem; }
void player_dj_set(const player_dj_t *m) { (void)m; }
void player_deck_load(int d, uint8_t song) { (void)d; (void)song; }
void player_deck_play(int d, int on, uint8_t pos, uint8_t row) { (void)d; (void)on; (void)pos; (void)row; }
void player_deck_arm(int d, uint8_t pos, uint8_t row) { (void)d; (void)pos; (void)row; }
void player_deck_place(int d, uint8_t pos, uint8_t row) { (void)d; (void)pos; (void)row; }
void player_deck_tempo(int d, uint32_t share_q16, uint16_t fixed_bpm10) { (void)d; (void)share_q16; (void)fixed_bpm10; }
void player_deck_mutes(int d, uint32_t muted) { (void)d; (void)muted; }
void player_deck_stop(int d, int kind, int down, int stay) { (void)d; (void)kind; (void)down; (void)stay; }
void player_deck_loop(int d, int kind, uint8_t pos, uint8_t row, uint16_t rows, int roll)
{
    (void)d; (void)kind; (void)pos; (void)row; (void)rows; (void)roll;
}
void player_deck_roll_end(int d) { (void)d; }
void player_dj_swap(uint8_t swaps) { (void)swaps; }
void player_deck_hand(int d, uint8_t pos, uint8_t row, uint32_t ms) { (void)d; (void)pos; (void)row; (void)ms; }
