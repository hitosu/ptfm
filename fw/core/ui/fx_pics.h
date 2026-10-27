/* SPDX-License-Identifier: GPL-3.0-only
 * Copyright (C) 2026 Vasily Kazantsev */
/* FX EDIT's pictures, one an effect (specs/ui/screens/fx.md, The pictures): drawn from the knobs only,
 * never from what plays (GATE's playhead and the METRONOME's beat follow the clock, FILTER's cutoff its envelope);
 * none writes a value a knob already shows. */
#ifndef UI_FX_PICS_H
#define UI_FX_PICS_H
#include <stdint.h>

typedef struct {
    int x, y, w, h;
    int col;                            /* the effect's colour, dark grey while it is off */
    int k[8];                           /* its knobs' values, as they play */
    uint32_t open;                      /* FILTER: how far its envelope has it open now, a share of the way from CUT to
                                         * fully open, Q16 */
    int hit;                            /* FILTER: its envelope's source hit within the last 0.1 s */
    int editing;                        /* GATE's steps being edited */
    int step, beat;                     /* GATE's playing step, the METRONOME's beat; -1 none */
    int bpm10;                          /* the tempo, BPM x 10 */
    uint8_t dck[16];                    /* DUCK: each track's DCK amount */
    int ntrk;                           /* DUCK: the song's tracks */
    const char *src_name;               /* DUCK: the source track's instrument, or "" */
    int dj;                             /* the SENDS item on DJ's setup */
} fxpic_t;
void fx_picture(int e, const fxpic_t *c);       /* e: FE_* or FE_SENDS */

#endif
