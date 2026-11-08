/* SPDX-License-Identifier: GPL-3.0-only
 * Copyright (C) 2026 Vasily Kazantsev */
/* LOOPS' loops (specs/files/loops.md version 4, specs/ui/screens/loops.md): one length for all, always set (4 bars at
 * first), up to 16 loops in the order they were made, each an instrument, its lanes interleaved into one stream (lane l
 * of row r at the stream's row 4r + l), a base played from the loops' start as many times as its repeat count says,
 * turned its SHIFT rows round the length. Whatever reads a loop reads it as it is heard (loops_heard).
 * The state is the store's (no image of its own): the record's head (the setup and the loop table) and each loop's
 * stream are pieces in the edit memory over the flash's record (rec.h's rec_loops reads them); every change below is
 * one edit, its pieces written at once (MEMORY FOR EDITS IS FULL: nothing changed). The main loop reads the head
 * through a copy taken again whenever the store changed; the selection lives in RAM till a head is written (moving
 * it is no change: loops_keep_sel at SAVE). The player plays them through a source (player.h's player_play_source):
 * the replay keeps the time (the length's rows, no streams), each track's instrument a loop's, and the player's lanes
 * play each loop's stream (a lanes source: player.h's player_lp_*). Main loop only, but for the source's functions (the
 * audio context). */
#ifndef PLAY_LOOPS_H
#define PLAY_LOOPS_H
#include <stdint.h>
#include "rec.h"

#define LOOPS_REC_VER REC_LOOPS_VER
#define LP_MAX 16                               /* loops at most */
#define LP_LANES 4                              /* a loop's lanes at most: notes at once */
#define LP_HEAD REC_LOOPS_TABLE                 /* the head's setup bytes; the table after them */
#define LP_ENTRY REC_LOOPS_ENTRY
#define LP_HEAD_MAX (LP_HEAD + LP_ENTRY * LP_MAX)
enum { LP_OK = 0, LP_FULL = -1, LP_NO = -2, LP_BUSY = -3, LP_BIG = -4 };   /* a change's result: done; MEMORY FOR EDITS IS
                                                 * FULL (or a stream past its 3,800 B); refused (no such loop, 16 loops,
                                                 * past 128 rows); not now (seeding, a save being written); REC's write
                                                 * past a loop's 3,800 B (THE STAGE IS FULL: loops_stage_write alone) */
enum { LQ_ROW, LQ_2_ROWS, LQ_3_ROWS, LQ_4_ROWS, LQ_REAL };     /* QUANTIZE */

uint32_t loops_record_default(uint8_t *p);      /* the head with no loops (80 B): TEMPO 120, TICKS A ROW 6, SWING 50,
                                                 * 1/16, ROW, COUNT-IN OFF, MONO LEGATO JUMP, POLY, the FX setup a new
                                                 * song's (loops_fx_new), the next loop's mix plain, the length 4 bars,
                                                 * VIEW 2 BARS, K1-K4's jobs not stored (the defaults) */
extern void (*loops_fx_new)(uint8_t *s64);      /* a new song's FX setup (the UI's fxd_default): LOOPS with no record,
                                                 * or a record whose FX setup is all 0 (none), starts from it */
extern const uint8_t *loops_keys_mix;           /* FX MIXER's KEYS column (RM_*: the UI's ui_keys_mix, every setup's):
                                                 * the next loop's CHORUS, DELAY and REVERB SENDS, heard on the stage on
                                                 * + NEW LOOP and written into the loop REC makes there; NULL (no UI):
                                                 * the head's own */
uint32_t loops_ram(void);                       /* its RAM (STATUS' RAM MAP: LOOPS): the head's copy and the source's */

/* ---- the setup ---- */
int loops_n(void);                              /* loops, 0-16 */
int loops_tempo(void);
int loops_set_tempo(int t);
int loops_ticks(void);
int loops_set_ticks(int t);
int loops_swing(void);
int loops_set_swing(int s);
int loops_grid(void);                           /* 0 1/16, 1 1/16T, 2 1/32 */
int loops_set_grid(int g);                      /* only with no loops (else LP_NO; the stage's emptiness is the screen's):
                                                 * the length keeps its whole bars (a part bar's rounded down, 1 at least,
                                                 * as many as the new grid allows) */
int loops_quant(void);                          /* LQ_* */
int loops_set_quant(int q);
enum { LG_GLIDE, LG_JUMP, LG_LEGATO };          /* MONO LEGATO */
int loops_legato(void);                         /* LG_* */
int loops_set_legato(int j);
int loops_countin(void);                        /* 0 OFF, 1 1 BAR, 2 2 BARS */
int loops_set_countin(int c);
int loops_keys_mono(void);                      /* the keys' mode (byte 4's bit 5): 1 MONO, 0 POLY: how the keys play on
                                                 * LOOPS and how REC keeps the stage */
int loops_set_keys_mono(int m);
int loops_view(void);                           /* VIEW: the bars a screen, 1, 2 or 4 (byte 78's bits 7-6; 2 at first) */
int loops_set_view(int bars);                   /* (1, 2 or 4; else LP_NO) */
enum { LJ_LEVEL, LJ_PAN, LJ_DUCK, LJ_CHORUS, LJ_DELAY, LJ_REVERB, LJ_TEMPO, LJ_VIEW };   /* K1-K4's jobs, by number (a mix
                                                 * value's job its RM_*) */
int loops_job(int k);                           /* knob k's (0-3: K1-K4) job, LJ_*: at first (not stored) LEVEL, PAN,
                                                 * REVERB SEND, TEMPO */
int loops_set_job(int k, int job);              /* (the four written, stored: byte 79's bit 6) */
int loops_sel(void);                            /* the loop selected; loops_n(): the placeholder (+ NEW LOOP) */
void loops_set_sel(int s);                      /* (RAM: no change by itself; written with the next head, or at SAVE) */
int loops_keep_sel(void);                       /* SAVE: the selection into the head when it moved and LOOPS has
                                                 * changes */
const uint8_t *loops_fx(void);                  /* LOOPS' FX setup (64 B, METRONOME included) */
int loops_set_fx(const uint8_t *s64);
int loops_rpb(void);                            /* rows a beat, by GRID */
int loops_beats(void);                          /* the METRONOME's BEATS A BAR */
int loops_bar_rows(void);
int loops_max_bars(void);                       /* 16, or fewer when 16 bars pass a pattern's 128 rows */
int loops_max_rows(void);                       /* the most rows in whole bars */
int loops_rows(void);                           /* the length in rows, 1-128, always (with loops or none) */
int loops_rows_step(int rows, int d);           /* LENGTH a click (d > 0 longer) from rows: a bar more or less, from a part
                                                 * bar the next whole bar each way, 1 bar to the most */
int loops_bpm(void);

/* ---- the loops (i < loops_n(); i = loops_n(): the next loop's mix) ---- */
uint16_t loops_inst(int i);                     /* its instrument, cartridge << 8 | slot */
int loops_colour(int i);                        /* 0-11 (specs/ui/screens/loops.md#colours) */
int loops_mono(int i);
int loops_muted(int i);
int loops_soloed(int i);
int loops_lanes(int i);                         /* 1-4: the most lanes its notes use */
int loops_base(int i);                          /* the rows its notes cover, 1-128 */
int loops_repeats(int i);                       /* 1-255 */
int loops_shift(int i);                         /* its SHIFT: the rows it plays later, round the length, 0 to the length - 1
                                                 * (stored 0-127, read modulo the length) */
int loops_set_shift(int i, int s);              /* ADJUST kept: s taken round the length (negative: earlier) */
const uint8_t *loops_mix(int i);                /* its mixer bytes (RM_*: LEVEL, PAN, DUCK, the sends); loops_n() the next
                                                 * loop's as heard, its sends the KEYS column's (loops_keys_mix; a copy,
                                                 * good till the next call) */
int loops_set_mix(int i, int k, int v);         /* one of them (loops_n(): the head's next loop's mix, its sends kept
                                                 * there as written, not heard) */
int loops_set_inst(int i, uint8_t cart, uint8_t slot);
int loops_set_sound(int i, uint8_t cart, uint8_t slot, int mono);   /* APPLY INSTRUMENT: its instrument and MONO / POLY */
int loops_set_muted(int i, int m);
int loops_set_soloed(int i, int s);
int loops_set_mutes(uint32_t muted, uint32_t soloed);          /* every loop's at once (bit i: loop i) */
uint32_t loops_muted_mask(void);
uint32_t loops_soloed_mask(void);
uint32_t loops_silent_mask(void);               /* the loops not heard: with any solo every loop not soloed, else the
                                                 * muted ones */

/* ---- changes ---- */
int loops_new(uint8_t cart, uint8_t slot);      /* a new loop at the end, empty (its number, else LP_*): the next colour,
                                                 * the keys' mode, the head's next loop's mix as stored, 1 lane, its base
                                                 * the length, once, SHIFT 0; the length as it is (the tests' constructor:
                                                 * the UI's new loops are REC's, loops_stage_write) */
int loops_peel(int i);                          /* DELETE LOOP: loop i goes, the ones after it move up (the length
                                                 * stays) */
int loops_clear(void);                          /* CLEAR LOOPS: every loop goes (the length, setup, FX, next mix stay) */
/* LENGTH (1-128 rows; with loops or none): a shifted loop first written out as it is heard over the old length (SHIFT
 * 0); shorter then cuts each loop's base and repeats at the new end (a base past it cut there, a note sounding over the
 * cut stopped at the wrap: a note off at row 0); longer first writes out over the old length a loop whose last repeat
 * the old length cut (the repeats it never reached dropped), so the rows added are silence */
int loops_set_rows(int rows);
int loops_cut_drops(int rows);                  /* 1: a length of rows would drop a note heard (the loops as heard) */
int loops_double(void);                         /* the length x 2, every loop playing twice what it played (its SHIFT
                                                 * kept) */
int loops_revert(void);                         /* REVERT: LOOPS as saved (its pieces dropped) */
int loops_quantize(int i, int q);               /* SEL's QUANTIZE (LQ_ROW, LQ_2_ROWS ... LQ_4_ROWS): loop i's notes to the
                                                 * nearest line of that size, the lines counted as the loop is heard (from
                                                 * the loops' start, its SHIFT counted, its first repeat), back in its base;
                                                 * note delays and key offs go; each note off moves with its note (its
                                                 * length kept, a row at least, a half row up); two notes landing on one
                                                 * lane's row: the later wins; one rounded to the base's end to its start */

/* ---- a loop's notes: a cell at (row, lane) of its base ---- */
const uint8_t *loops_stream(int i, const uint8_t **end);       /* its stream (rec_cur reads it) and its part's end;
                                                                * NULL empty */
int loops_cell(int i, int row, int lane, rec_cell_t *c);        /* 1 there, 0 none */
int loops_cell_put(int i, int row, int lane, const rec_cell_t *c);   /* a cell in place of what is there (c NULL or
                                                                * c->has 0: cleared); its instrument left out */
int loops_stream_clear(int i);                  /* its notes gone (the tests', clearing a loop's stream), 1 lane */
int loops_set_stream(int i, const uint8_t *s, uint32_t len);   /* its stream whole (len with its 0x00; 1: empty) */
/* its notes replaced whole (the pattern recorder's): its base, repeats and MONO, and its stream (s, len with its 0x00,
 * encoded as loops_enc_* make it: its cells in its base's rows), the lanes it uses */
int loops_put(int i, int base, int repeats, int mono, const uint8_t *s, uint32_t len, int lanes);
int loops_note_count(int i);                    /* the notes its base starts */
int loops_has_notes(int i);
/* a stream built cell by cell, in stream rows' order (4 row + lane): gaps past 255 rows bridged (an ED0 alone, a skip
 * of 255 after the cell before), a cell with nothing in it left out; a loop's has no instrument (a cell's RC_INS left
 * out) */
typedef struct { uint8_t *b; uint32_t n, max; int prev, lanes, over, last; } lp_enc_t;
void loops_enc_start(lp_enc_t *e, uint8_t *buf, uint32_t max);   /* buf NULL: only counted */
/* ... a pattern track's instead (specs/files/patterns.md): its rows the pattern's, a cell's instrument (RC_INS) written
 * as a track's (in full at its first, then as the same or the same cartridge's), byte for byte as the EDITOR writes */
void loops_enc_track(lp_enc_t *e, uint8_t *buf, uint32_t max);
/* ... the stage's (specs/files/ram.md#the-stage-in-the-edit-memory): a loop's stream whose cells played onto the stage
 * carry a mark, LP_PLAYED in a cell's has: its head's instrument bits 01 (the same instrument as before, no bytes),
 * which a loop's stream never has; rec_cur_at reads it back as RC_INS. The stage's stream is never saved, and no
 * mark reaches a loop: a loop's encoder leaves it out (loops_stage_write). EDIT's copy (loops_heard) is unmarked */
#define LP_PLAYED RC_INS
void loops_enc_stage(lp_enc_t *e, uint8_t *buf, uint32_t max);
void loops_enc_cell(lp_enc_t *e, int srow, const rec_cell_t *c);
uint32_t loops_enc_end(lp_enc_t *e);            /* the stream's bytes with its 0x00 (0: past max) */
/* loop i as it is heard over the length, into buf (max; NULL: counted): its notes written out (its repeats one after
 * another, a note off where it goes to rest with a note sounding, a note off at row 0 where the length cuts its last
 * repeat), then turned its SHIFT rows; a stream of the length's rows, unmarked (EDIT's copy onto the stage). Its bytes
 * with its 0x00; 0 past max (EDIT past the stage's 3,800 B: THE STAGE IS FULL), or no such loop */
uint32_t loops_heard(int i, uint8_t *buf, uint32_t max);
/* the stage's stream (s, len with its 0x00) of `rows` rows, into buf (max; NULL: counted), its marks kept. Its bytes
 * with its 0x00; 0 past max (DOUBLE past the stage's 3,800 B: THE STAGE IS FULL) */
/* ... made `to` rows long (a length change): cut there, a lane still sounding at the cut a note off at row 0
 * (unmarked) where its row 0 holds no note; longer, the rows added empty */
uint32_t loops_stage_length(const uint8_t *s, uint32_t len, int rows, int to, uint8_t *buf, uint32_t max);
/* ... played twice (DOUBLE: 2 x rows): its cells again after them, the second time unmarked */
uint32_t loops_stage_double(const uint8_t *s, uint32_t len, int rows, uint8_t *buf, uint32_t max);
int loops_is_note(const rec_cell_t *c);         /* a note (not a note off) */
void loops_cmd_add(rec_cell_t *c, int code, int v);   /* a command added to cell c (its 4 kept: none more) */
int loops_bridge(const rec_cell_t *c);          /* a gap's bridge, nothing in it: an ED0 alone (no cell to a reader) */
/* the stage's row r as it plays (*c4: its lanes' cells, a bridge none; the bits returned those there), read in rows'
 * order through cu (set on the stream); a MONO stage (mono) merged onto lane 0 (from: the lane whose note sounds, -1
 * none, carried from row to row): a note on any lane takes over (the later lane's of one row), an end counts only on
 * the lane whose note sounds. REC writes it so, the screen draws and changes it so */
typedef struct { rec_cur_t cu; int mono, from; } lp_st_rd_t;
int loops_stage_row(lp_st_rd_t *d, int r, rec_cell_t *c4);
/* REC: the stage's stream (s, len with its 0x00, the length's rows) written into loop i, or a new loop at the end (i ==
 * loops_n(): the next colour, the next loop's mix as heard (loops_mix's: its sends the KEYS column's), unmuted): its
 * instrument (cart, slot) and mode (mono) the keys', its
 * base the length, once, SHIFT 0; a loop's place, colour, mix and mute kept. What is written is what was heard: MONO one
 * lane (a POLY stage merged as it played: the later of overlapping notes, an end counting on the lane whose note
 * sounds); no wrap for the notes played onto the stage (marked: one still sounding at the length's end ends at the
 * line, a note off at row 0); the notes EDIT brought as they are (one across the line keeps its end past it); a note
 * off or key off that ends nothing (its lane silent there, counted round from the length's end) left out; no mark
 * written. The loop's number; LP_NO (no such loop, 16 loops, no note on the stage), LP_BIG (the loop past 3,800 B: the
 * ends at the line added to a full stage), LP_FULL, LP_BUSY. s must not lie in the store's scratch (the stage's piece
 * does not). s the stage's piece itself (store_stage): its room counted free (the loop takes its place), and once the
 * loop is written the stage goes, what of it sounds told to go on as the loop's first (player_lp_stage_end(i), then
 * store_stage_drop); where the loop needs the stage's room, the stage goes (so told) before the loop's pieces are put
 * (there alone a put failing after it, which the room counted and BUSY checked first should rule out, would leave
 * neither the stage nor the loop: nothing can be put back); refused (the store busy included), nothing changed */
int loops_stage_write(int i, const uint8_t *s, uint32_t len, uint8_t cart, uint8_t slot, int mono);
/* a track's mix as mixer commands (specs/files/commands.md#mixer-and-fx-0x80-0xbf), in the order they go into a cell:
 * LEVEL `=80`, PAN `8xx`, DELAY SEND `=82`, REVERB SEND `=83`, CHORUS SEND `=81`, DUCK `=84` (RM_* and their codes) */
extern const uint8_t LP_MIX_KEY[RM_N], LP_MIX_CODE[RM_N];

/* ---- storing in a pattern and loading one (specs/files/loops.md#storing-and-loading) ---- */
/* SAVE TO PATTERN: the loops written out into pattern num (an edit, its head and all 16 tracks), as long as they are:
 * every loop as it is heard (loops_heard: its repeats, a note off where it goes to rest, the cut at the wrap, turned
 * SHIFT rows), each note naming its instrument; each lane onto the first track free over all its rows (a lane sounding
 * at the length's end rings into its start: the track whole; *left: the notes of those that fit none); a loop's mix as
 * mixer commands with its first note on a track, where the track's differs. LP_OK, LP_NO (no loops), LP_FULL, LP_BUSY */
int loops_store(uint8_t num, int *left);
/* LOAD FROM PATTERN's timing: a song's TEMPO, TICKS A ROW, ROWS A BEAT, SWING; loops_timing_fit makes it LOOPS': ROWS A
 * BEAT that GRID lacks keeps the loops' GRID, the TEMPO scaled to the same BPM */
typedef struct { uint16_t tempo; uint8_t ticks, rpb, swing; } lp_timing_t;
void loops_timing_fit(lp_timing_t *tm);
/* LOAD FROM PATTERN: pattern num in place of the loops, at timing tm (fitted): its rows in whole bars (128 at most); a
 * loop an instrument, in the order they first play, its notes from every track that plays it, up to 4 at once as its
 * lanes (more at once: another loop of it), each POLY, once, SHIFT 0, its mix from the mixer commands with its notes;
 * glides, note delays and key offs kept, the other commands left out and counted; 16 loops at most (the instruments past
 * them counted). The timing and the length become the pattern's, with notes or none; the selection the first. LP_OK,
 * LP_NO (no such pattern), LP_FULL, LP_BUSY */
typedef struct { uint8_t loops, insts; uint16_t cmds; } lp_loaded_t;   /* loops made; instruments, commands left out */
int loops_load(uint8_t num, const lp_timing_t *tm, lp_loaded_t *res);

/* ---- the player ---- */
void loops_play(uint8_t row);                   /* the loops from a row (their mutes and solos with them) */
void loops_play_silent(uint8_t place);          /* ... their replay from row 0, silent, their place `place` (RECORD
                                                 * LIVE: player_play_source_silent) */
void loops_cue(void);                           /* LOOPS shown, nothing playing: the loops held for the keys
                                                 * (player_cue_loops) */
void loops_changed(int rows);                   /* a change made: the player told when the loops are what plays or is
                                                 * held (rows: the length changed); their mutes and solos only when they
                                                 * differ from what it was given last (player_lp_mutes_at_bar's change
                                                 * waiting there kept) */
int loops_song(rec_song_t *s);                  /* the loops' "song" as the player has it (tempo, mixer, FX; track
                                                 * loops_n() the next loop's mix as heard, loops_mix's): 0 read */

#endif
