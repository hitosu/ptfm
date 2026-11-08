/* SPDX-License-Identifier: GPL-3.0-only
 * Copyright (C) 2026 Vasily Kazantsev */
/* RECORD LIVE (specs/ui/screens/loops.md#record-live): everything that sounds, written as a new song, a pattern every
 * LIVE_ROWS rows of the song's time from its start, a position each, into the edit memory pattern by pattern. The player's
 * tap (player.h) gives each note where it enters the voices; here each goes on a track (one whose mix is its source's)
 * and into a log of the pattern being written, kept in rows' order in a borrow from the spare zone; what changes (TEMPO,
 * the FX setup, the mixes) is looked for at each poll and logged as commands at their rows. The song's rows are its own:
 * its time is the loops' replay's ticks (player_state().lp_ticks) from its row 0 over its ticks a row, fixed at its start,
 * so nothing LOOPS does changes how a note is written. What finds no track is heard, left out and counted. Main loop only;
 * its state its own, never stored. */
#ifndef PLAY_LIVE_H
#define PLAY_LIVE_H
#include <stdint.h>

#define LIVE_ROWS 128                   /* a pattern's rows: a new one every 128 rows of the song */
enum { LIVE_OK, LIVE_FULL, LIVE_PATTERNS };   /* a pattern written; not: the edit memory full, no pattern number free
                                         * (nothing of it written) */
void live_arm(void);                    /* armed: the tap on, the log's memory asked for (and again at each poll) */
void live_disarm(void);                 /* given up, or over: the tap off, the memory given back */
/* the song `song` begins at its row 0, the replay's tick t0 (known 0: a replay started for it, told by live_known_set):
 * its name (16 6-bit characters, 12 B), the keys' mix (RM_*), when (player_state().halves), the punch-in running then
 * (-1 none: one running is not written). The loops' setup at the start is its own */
void live_begin(uint8_t song, const uint8_t *name, const uint8_t *keys_mix, uint32_t halves, uint32_t t0, int known, int punch);
uint8_t live_song(void);
int live_known(void);                   /* its row 0 known */
void live_known_set(uint32_t t0);       /* ... now */
uint32_t live_t0(void);                 /* the replay's tick at its row 0 */
int live_ticks(void);                   /* its ticks a row */
int live_done(void);                    /* the patterns written (its positions) */
int live_left(void);                    /* the notes left out so far */
uint32_t live_h0(void);                 /* its start (halves) */
/* on every pass of the main loop: armed, what the tap heard before the row playing dropped; recording, the tap's events
 * on the song's rows, what changed logged */
void live_poll(void);
/* the pattern live_done() written, its rows up to end (LIVE_ROWS whole; less: the song ended there, its last pattern
 * that long): LIVE_* */
int live_chunk(int end);
/* what is heard next on the stage's keys' lane (0-3) is written at the replay's tick `tick`: a key on the stage heard at
 * once, written where QUANTIZE landed it */
void live_land(int lane, uint32_t tick);
void live_punch(int k);                 /* the punch-in running now (0-15, -1 none), as heard */
void live_punch_end(int at);            /* the punch-in running ends at the song's row `at` (the recording's end) */

#endif
