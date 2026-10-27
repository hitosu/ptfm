/* SPDX-License-Identifier: GPL-3.0-only
 * Copyright (C) 2026 Vasily Kazantsev */
/* The pattern grid the EDITOR and PLAYBACK share (specs/ui/screens/editor.md, playback.md): the views of 8,
 * 4, 2 and 1 tracks, the focus row, the playing row's band, a marked block, beat rows; and commands in the
 * grid's 3 characters (specs/files/commands.md). */
#ifndef UI_GRID_H
#define UI_GRID_H
#include <stdint.h>
#include "../play/rec.h"

typedef struct { uint8_t n, row_h, focus_h, head, rows; } ui_view_t;
const ui_view_t *ui_view(int tracks);   /* 8, 4, 2 or 1 */

typedef struct {
    int view;                           /* 8, 4, 2, 1 */
    int top, left;                      /* the first row and track shown */
    int cur_row, cur_track;             /* the cursor (the focus row), -1 none */
    int play;                           /* the playing row (a blue band), -1 none */
    int col;                            /* a cursor track without a cursor (PLAYBACK's), -1 none */
    int slot;                           /* the EDITOR's K1: 0 the volume, 1-4 a command; -1 none */
    int block[4];                       /* a marked block: r0, r1, t0, t1 (r0 -1: none) */
    int y0;                             /* the grid's top; 0: the body's */
    int rpb;                            /* rows a beat (the current song's) */
    int fusion;                         /* a FUSION pattern: its rows are notes */
    /* a carried block (the EDITOR's MOVE / SWAP): 1 when (row, track) lies under it, c its cell there
     * (c->has 0: empty); drawn on a deep magenta. NULL: none */
    int (*over)(int row, int track, rec_cell_t *c);
    const uint8_t *hot;                 /* rows lit deep green (a FUSION's rows the keys play), a bit a row; NULL none */
} ui_grid_t;
void ui_grid_init(ui_grid_t *o);        /* every field as none */
void ui_grid(uint8_t pat, const ui_grid_t *o);

/* a command (code, value) in the grid's 3 characters (`A04`, `*3F`, `=7F`, `#40`, `~+3`, `ACC`, `!08`) */
void ui_cmd_text(uint8_t code, uint8_t value, char *out);
int ui_cmd_group(uint8_t code);         /* 0 a tracker command, 1 PARAM, 2 MIXER, 3 FX */
/* a command's short name, 9 characters at most, for the instrument's engine (ENG_*); "" where it has none
 * yet (the EDITOR's full table fills it in) */
const char *ui_cmd_short(uint8_t code, uint8_t value, int engine);

#endif
