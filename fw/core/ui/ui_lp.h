/* SPDX-License-Identifier: GPL-3.0-only
 * Copyright (C) 2026 Vasily Kazantsev */
/* LOOPS' screen (scr_loops.c) and its pattern recorder (lp_rec.c): what they share. */
#ifndef UI_LP_H
#define UI_LP_H
#include <stdint.h>

extern const uint8_t LP_COL[12];        /* each loop's colour by its number (specs/ui/screens/loops.md#colours) */

/* scr_loops.c */
void lp_init(void);                     /* the screen's state as at power-on (ui_init) */
int lp_playing_here(void);              /* the loops play */
int lp_busy(int b);                     /* a change of the loops refused now (counting in: b blinks): 1 refused */
int lp_song_on(void);                   /* a song records (the loops' replay never stops: no pattern list's preview) */
void lp_song_stop(void);                /* a save or REVERT ALL is coming: a song recording ends now, while the edit memory
                                         * takes its last pattern */
void lp_rec_with(void);                 /* ARP held + REC, at REC's release: the pattern recorder */
void lp_tempo_turn(int steps, int coarse);     /* ALGORITHM: TEMPO, shown in the header */
void lp_play(void);                     /* PLAY as the screen's (the loops played or stopped) */
int lp_next_colour(void);               /* a new loop's colour */
void lp_loop_name(char *dst, int i);    /* `LOOP 03` */
void lp_live_poll(void);                /* the main loop, on every screen: the play position, the stage fitted to the
                                         * length, RECORD LIVE's song (its start, each pass written as the next is
                                         * heard, the loops pausing ending it) */
/* REC's wait (specs/ui/screens/loops.md#rec-edit-and-the-arm): for 500 ms after REC does a job on LOOPS it does
 * nothing (no blink, no toast). lp_rec_done: a job done, the wait starts (the pattern recorder's REC putting its
 * pattern onto the stage calls it too); lp_rec_quiet: 1 while it waits (REC then does nothing) */
void lp_rec_done(void);
int lp_rec_quiet(void);
const char *lp_len_text(int rows);      /* the length as shown: `4 BARS`, `1 BAR`, a part bar in its beats `21 BEATS` */
/* the pattern recorder's REC (specs/ui/screens/loops.md#the-pattern-recorder-arp): its stream onto the stage in place of
 * what the stage held, made by make (buf NULL: counted; its bytes with the 0x00, 0 past max) over the length's rows,
 * unmarked; checked first (refused: REC blinks, the header says why, nothing changed), then the keys down stop writing,
 * the stream put, the keys' instrument inst, POLY when poly; the player told of the stage. STORE_OK, or the refusal */
int lp_stage_from(uint32_t (*make)(uint8_t *buf, uint32_t max), uint16_t inst, int poly);
/* the white keys held past the hold delay (CONTROL mode's, the pattern recorder's: never at once, the recorder takes the
 * keys while it is open): white key i down; let go (1: a tap, its hold not come); the first whose hold came (-1 none;
 * then it is done); all let go */
void lp_hold_down(int i);
int lp_hold_up(int i);
int lp_hold_due(void);
void lp_hold_clear(void);
/* the state's LOOPS bytes (specs/connections.md#the-state, 331-334), on every screen: u16 the notes the stage starts (0
 * with it empty); the selected loop's SHIFT (0 on + NEW LOOP); bit 0 ADJUST open, bit 1 OVERWRITE on where it acts (on
 * LOOPS), bit 2 OVERWRITE on */
void lp_state(uint8_t *p);

/* lp_rec.c: the pattern recorder (ARP), an overlay over the loops */
enum { LPN_1_32, LPN_1_16T, LPN_1_16, LPN_1_8T, LPN_1_8, LPN_1_4, LPN_1_2, LPN_BAR, LPN_NEXT };   /* NOTE LENGTH */
/* what it has set, kept until power-off, never saved (the tests read and set it, as the playground's do): each row's
 * steps on and its ties (a step held from the one before; never on a step on), bit k step k; each row's note (0 never
 * set: the row above's, the first's C at the keyboard's octave); the row SELECT picked, LENGTH (4-16 steps), NOTE LENGTH
 * (LPN_*), the keys setting the selected row's note (SEL tapped) */
typedef struct { uint16_t on[4], tie[4]; uint8_t note[4], sel, len, nlen, keys; } lp_pr_t;
extern lp_pr_t lp_pr;
void lp_rec_open(void);                 /* on the selected loop (its sound), or + NEW LOOP */
int lp_rec_on(void);                    /* open */
uint16_t lp_rec_inst(void);             /* the instrument it plays and REC gives the stage (open only) */
void lp_rec_close(void);                /* closed, nothing written (leaving LOOPS) */
void lp_rec_poll(void);                 /* the main loop, on LOOPS: a step key's hold, a note heard let go after its time */
void lp_rec_makes(char *dst, int max);  /* what it makes: `4 X 16 = 64 ROWS, 2 LANES` */

#endif
