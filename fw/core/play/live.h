/* SPDX-License-Identifier: GPL-3.0-only
 * Copyright (C) 2026 Vasily Kazantsev */
/* RECORD LIVE (specs/ui/screens/loops.md#record-live): a performance over the loops written as a new song, a pattern
 * every LIVE_ROWS rows of the song's time from its start, a position each, into the edit memory pattern by pattern. The
 * song's time is the loops' replay's rows (player_state().rows_played): the replay runs on while the loops are silent
 * (player_lp_rest), so the screen tells where the loops were (live_loops: their row from a song row, or silent, as the
 * player's switches say), starts it (live_begin), lands the keys' notes (live_key_*), tells the punch-ins and asks for
 * each pattern once its rows are heard (live_chunk); the loops' own changes (their mixes, TEMPO, the FX setup, mutes
 * and solos) come through loops.c's hook. A change waits, with the song row it was heard at, until its pattern is
 * written: the loops' notes as their lanes played them at the loops' rows heard (repeats written out, silent rows
 * empty, a loop left out from the row its mute took effect, a note off for a note sounding where they fall silent), the
 * keys on tracks of their own, the changes as commands at their rows. The tracks are planned at the start and kept: each
 * lane on the first track free over the loops' rows, then up to 4 for the keys; what fits none is heard, left out and
 * counted. Main loop only; its state its own, never stored. */
#ifndef PLAY_LIVE_H
#define PLAY_LIVE_H
#include <stdint.h>

#define LIVE_ROWS 128                   /* a pattern's rows: a new one every 128 rows of the song */
enum { LIVE_OK, LIVE_FULL, LIVE_PATTERNS };   /* a pattern written; not: the edit memory full (or too many changes
                                         * waiting), no pattern number free (nothing of it written) */
/* the song `song` begins at its row 0, the replay's rows_played rows0: its name (16 6-bit characters, 12 B), the keys'
 * tracks' mix (RM_*), their notes' volume, when (player_state().halves), the punch-in running then (-1 none: one
 * running is not written), the loops' row heard at row 0 (0xFF silent). The loops' setup at the start is its own */
void live_begin(uint8_t song, const uint8_t *name, const uint8_t *keys_mix, uint8_t keys_vol, uint32_t halves,
                uint32_t rows0, int punch, int loops_row);
void live_stop(void);                   /* over: nothing more written, nothing more heard of it */
int live_on(void);
uint8_t live_song(void);
uint32_t live_rows0(void);              /* the rows_played at its row 0 (0: not known yet, nothing heard) */
void live_rows0_set(uint32_t rows0);    /* ... now known (a replay started for it: its first switch's) */
int live_done(void);                    /* the patterns written (its positions) */
int live_left(void);                    /* the notes left out so far */
uint32_t live_t0(void);                 /* its start (halves) */
int live_over(void);                    /* 1: a change found no room (it stops: LIVE_FULL) */
/* from the song's row `at` the loops are heard at their row `row` (0xFF: silent from there): a switch of the player's
 * (player_state().lp_sw ...), at the song row its rows_played gives */
void live_loops(int at, int row);
/* the pattern live_done() written, its rows up to end (LIVE_ROWS whole; less: the song ended there, its last pattern
 * that long): LIVE_* */
int live_chunk(int end);
/* the keys' cells (at: the song's row, in live_done()'s pattern or the next; lane 0-3): a note in instrument inst
 * (delay: REAL TIME's EDx; glide: 1 a 320 with it, 2 a LEG; never on a TB-303's note), replacing what that lane has
 * at the row: 0, -1 no room */
int live_key(int at, int lane, int note, int delay, int glide, uint16_t inst);
void live_key_slide(int at, int lane);  /* the note there slides into the next (a TB-303's SLD) */
/* a key let go: its note's end at the row (kxx >= 0: REAL TIME's key off at that tick, else a note off; own: in its
 * note's own row): 0 nothing written (a note there ends it), 1 into its note's cell (heard now), 2 written there */
int live_key_off(int at, int lane, int kxx, int own);
void live_punch(int k);                 /* the punch-in running now (0-15, -1 none), as heard */
void live_punch_end(int at);            /* the punch-in running ends at the song's row `at` (the recording's end) */

#endif
