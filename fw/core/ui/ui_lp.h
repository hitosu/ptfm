/* SPDX-License-Identifier: GPL-3.0-only
 * Copyright (C) 2026 Vasily Kazantsev */
/* LOOPS' screen (scr_loops.c) and its pattern recorder (lp_rec.c): what they share. */
#ifndef UI_LP_H
#define UI_LP_H
#include <stdint.h>

extern const uint8_t LP_COL[12];        /* each loop's colour by its number (specs/ui/screens/loops.md#colours) */

/* scr_loops.c */
int lp_playing_here(void);              /* the loops play */
int lp_busy(int b);                     /* a change of the loops refused now (a take armed or recording: b blinks; a
                                         * song armed or recorded: RECORDING A SONG too): 1 refused */
void lp_tempo_turn(int steps, int coarse);     /* ALGORITHM: TEMPO, shown in the header */
void lp_play(void);                     /* PLAY as the screen's (the loops played or paused) */
int lp_next_colour(void);               /* a new loop's colour */
void lp_loop_name(char *dst, int i);    /* `LOOP 03` */
void lp_live_poll(void);                /* the main loop, on every screen: RECORD LIVE's song (its start, each pass
                                         * written as the next is heard, the loops pausing ending it) */

/* lp_rec.c: the pattern recorder (ARP), an overlay over the loops */
void lp_rec_open(void);                 /* on the selected loop (into it), or + NEW LOOP */
int lp_rec_on(void);                    /* open */
uint16_t lp_rec_inst(void);             /* the instrument it plays and writes (open only) */
void lp_rec_close(void);                /* closed, nothing written (leaving LOOPS) */
void lp_rec_poll(void);                 /* the main loop, on LOOPS: a note heard let go after its time */

#endif
