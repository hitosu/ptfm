/* SPDX-License-Identifier: GPL-3.0-only
 * Copyright (C) 2026 Vasily Kazantsev */
/* LOOPS' loops (specs/files/loops.md version 4, specs/ui/screens/loops.md): one length for all, up to 16 loops in the
 * order they were made, each an instrument, its lanes interleaved into one stream (lane l of row r at the stream's
 * row 4r + l), a base played from the loops' start as many times as its repeat count says.
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
enum { LP_OK = 0, LP_FULL = -1, LP_NO = -2, LP_BUSY = -3 };   /* a change's result: done; MEMORY FOR EDITS IS FULL
                                                 * (or a stream past its 3,800 B); refused (no such loop, 16 loops, past
                                                 * 128 rows); not now (seeding, a save being written) */
enum { LQ_ROW, LQ_2_ROWS, LQ_3_ROWS, LQ_4_ROWS, LQ_REAL };     /* QUANTIZE */

uint32_t loops_record_default(uint8_t *p);      /* the head with no loops (80 B): TEMPO 120, TICKS A ROW 6, SWING 50,
                                                 * 1/16, ROW, COUNT-IN OFF, GLIDE, POLY, the FX setup a new song's
                                                 * (loops_fx_new), the next loop's mix plain */
extern void (*loops_fx_new)(uint8_t *s64);      /* a new song's FX setup (the UI's fxd_default): LOOPS with no record,
                                                 * or a record whose FX setup is all 0 (none), starts from it */
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
int loops_set_grid(int g);
int loops_quant(void);                          /* LQ_* */
int loops_set_quant(int q);
enum { LG_GLIDE, LG_JUMP, LG_LEGATO };          /* MONO LEGATO */
int loops_legato(void);                         /* LG_* */
int loops_set_legato(int j);
int loops_countin(void);                        /* 0 OFF, 1 1 BAR, 2 2 BARS */
int loops_set_countin(int c);
int loops_next_mono(void);                      /* how the next loops record: 1 MONO, 0 POLY */
int loops_set_next_mono(int m);
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
int loops_max_rows(void);                       /* the most rows in whole bars (the first loop's clock) */
int loops_rows(void);                           /* the length in rows (0 with no loops) */
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
const uint8_t *loops_mix(int i);                /* its mixer bytes (RM_*: LEVEL, PAN, DUCK, the sends) */
int loops_set_mix(int i, int k, int v);         /* one of them */
int loops_set_inst(int i, uint8_t cart, uint8_t slot);         /* APPLY INSTRUMENT */
int loops_set_muted(int i, int m);
int loops_set_soloed(int i, int s);
int loops_set_mutes(uint32_t muted, uint32_t soloed);          /* every loop's at once (bit i: loop i) */
uint32_t loops_muted_mask(void);
uint32_t loops_soloed_mask(void);
uint32_t loops_silent_mask(void);               /* the loops not heard: with any solo every loop not soloed, else the
                                                 * muted ones */

/* told after each change of what RECORD LIVE writes while it records (play/live.c sets it): LH_MIX a loop's mix byte
 * (i: loop * RM_N + RM_*), LH_TEMPO, LH_FX the FX setup's byte i, LH_SILENT the loops not heard (loops_silent_mask);
 * the value before and after */
enum { LH_MIX, LH_TEMPO, LH_FX, LH_SILENT };
extern void (*loops_hook)(int what, int i, int was, int now);

/* ---- changes ---- */
int loops_new(uint8_t cart, uint8_t slot);      /* a new loop at the end (its number, else LP_*): the next colour, the
                                                 * next loops' mode, the next loop's mix, 1 lane, its base the length,
                                                 * once; the first sets the length to the most rows (its base too: the
                                                 * UI closes it with loops_set_rows) */
int loops_peel(int i);                          /* loop i goes, the ones after it move up; the only one: as clear */
int loops_clear(void);                          /* every loop, and the length, go (the setup, FX, next mix stay) */
int loops_set_rows(int rows);                   /* the length (whole bars, 1-128): shorter cuts each loop's base and
                                                 * repeats at the new end (cells past a base dropped); longer adds
                                                 * silence */
int loops_cut_drops(int rows);                  /* 1: a length of rows would drop a note that plays */
int loops_double(void);                         /* the length x 2, every loop playing twice what it played */
int loops_revert(void);                         /* REVERT: LOOPS as saved (its pieces dropped) */

/* ---- a loop's notes: a cell at (row, lane) of its base ---- */
const uint8_t *loops_stream(int i, const uint8_t **end);       /* its stream (rec_cur reads it) and its part's end;
                                                                * NULL empty */
int loops_cell(int i, int row, int lane, rec_cell_t *c);        /* 1 there, 0 none */
int loops_cell_put(int i, int row, int lane, const rec_cell_t *c);   /* a cell in place of what is there (c NULL or
                                                                * c->has 0: cleared); its instrument left out */
int loops_stream_clear(int i);                  /* its notes gone (a re-record), 1 lane */
int loops_set_stream(int i, const uint8_t *s, uint32_t len);   /* its stream whole (len with its 0x00; 1: empty) */
/* its notes replaced whole (the pattern recorder's): its base, repeats and MONO, and its stream (s, len with its 0x00,
 * encoded as loops_enc_* make it: its cells in its base's rows), the lanes it uses */
int loops_put(int i, int base, int repeats, int mono, const uint8_t *s, uint32_t len, int lanes);
int loops_note_count(int i);                    /* the notes its base starts */
int loops_has_notes(int i);
/* a stream built cell by cell, in stream rows' order (4 row + lane): gaps past 255 rows bridged (an ED0 alone, a skip
 * of 255 after the cell before), a cell with nothing in it left out */
typedef struct { uint8_t *b; uint32_t n, max; int prev, lanes, over, last; } lp_enc_t;
void loops_enc_start(lp_enc_t *e, uint8_t *buf, uint32_t max);   /* buf NULL: only counted */
/* ... a pattern track's instead (specs/files/patterns.md): its rows the pattern's, a cell's instrument (RC_INS) written
 * as a track's (in full at its first, then as the same or the same cartridge's), byte for byte as the EDITOR writes */
void loops_enc_track(lp_enc_t *e, uint8_t *buf, uint32_t max);
void loops_enc_cell(lp_enc_t *e, int srow, const rec_cell_t *c);
uint32_t loops_enc_end(lp_enc_t *e);            /* the stream's bytes with its 0x00 (0: past max) */
/* a track's mix as mixer commands (specs/files/commands.md#mixer-and-fx-0x80-0xbf), in the order they go into a cell:
 * LEVEL `=80`, PAN `8xx`, DELAY SEND `=82`, REVERB SEND `=83`, CHORUS SEND `=81`, DUCK `=84` (RM_* and their codes) */
extern const uint8_t LP_MIX_KEY[RM_N], LP_MIX_CODE[RM_N];

/* ---- storing in a pattern and loading one (specs/files/loops.md#storing-and-loading) ---- */
/* SAVE TO PATTERN: the loops written out into pattern num (an edit, its head and all 16 tracks), as long as they are:
 * every loop's notes, repeats written out, each naming its instrument; each lane onto the first track free over all its
 * rows (*left: the notes of those that fit none); a loop's mix as mixer commands with its first note on a track, where
 * the track's differs. LP_OK, LP_NO (no loops), LP_FULL, LP_BUSY */
int loops_store(uint8_t num, int *left);
/* LOAD FROM PATTERN's timing: a song's TEMPO, TICKS A ROW, ROWS A BEAT, SWING; loops_timing_fit makes it LOOPS': ROWS A
 * BEAT that GRID lacks keeps the loops' GRID, the TEMPO scaled to the same BPM */
typedef struct { uint16_t tempo; uint8_t ticks, rpb, swing; } lp_timing_t;
void loops_timing_fit(lp_timing_t *tm);
/* LOAD FROM PATTERN: pattern num in place of the loops, at timing tm (fitted): its rows in whole bars (128 at most); a
 * loop an instrument, in the order they first play, its notes from every track that plays it, up to 4 at once as its
 * lanes (more at once: another loop of it), each POLY, once, its mix from the mixer commands with its notes; glides,
 * note delays and key offs kept, the other commands left out and counted; 16 loops at most (the instruments past them
 * counted). With loops, the timing becomes theirs; the selection the first. LP_OK, LP_NO (no such pattern), LP_FULL,
 * LP_BUSY */
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
                                                 * loops_n() the next loop's mix): 0 read */

#endif
