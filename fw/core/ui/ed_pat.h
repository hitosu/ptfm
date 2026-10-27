/* SPDX-License-Identifier: GPL-3.0-only
 * Copyright (C) 2026 Vasily Kazantsev */
/* Editing a pattern (specs/files/patterns.md, ram.md#editing-and-saving), in its packed form: an edit
 * rewrites the tracks it changes, cell by cell from their old streams into new ones, each put into the edit
 * memory as its track's piece (the others untouched), the pattern's head when its size changes. No unpacked
 * copy of a pattern is kept; a track is built in ui_scratch. The encoding is the packer's, byte for byte:
 * equal cells give equal records. */
#ifndef ED_PAT_H
#define ED_PAT_H
#include <stdint.h>
#include "../play/rec.h"

/* a rewrite: for each row of each track t0..t1, fn gets the cell there (c->has 0: none) and leaves the
 * cell to write. ed_old() reads any old cell meanwhile. New size: ntrk / rows / step, -1 for unchanged;
 * what lies outside goes. 0 done, -1 refused (too big, or the edit memory full) */
typedef void (*ed_fn)(int t, int r, rec_cell_t *c, void *a);
int ed_rewrite(uint8_t pat, int t0, int t1, ed_fn fn, void *a, int ntrk, int rows, int step);
int ed_old(int t, int r, rec_cell_t *c);                /* during a rewrite: 1 a cell there (c filled), 0 none */

int ed_put_cell(uint8_t pat, int row, int track, const rec_cell_t *c);  /* one cell (NULL or has 0: cleared) */
int ed_cell(uint8_t pat, int row, int track, rec_cell_t *c);    /* 1 a cell there (c filled), 0 none */
int ed_inst_at(uint8_t pat, int row, int track);        /* the instrument the track plays at the row, -1 none */
int ed_drops(uint8_t pat, int ntrk, int rows);          /* cells a smaller size would drop */
int ed_builtin(uint8_t pat);            /* 1 built in (F0-FF) as the firmware has it; 0 the user area's or edited (all
                                         * editable) */
int ed_new_pattern(uint8_t pat, int ntrk, int rows, int step);  /* an empty pattern: 0 done */
int ed_copy_pattern(uint8_t from, uint8_t to);          /* its head and every track, as they are: 0 done */
int ed_delete_pattern(uint8_t pat);
int ed_first_free(void);                /* the first unused pattern number of the user's (00-EF), -1 none */

/* the clipboard (the EDITOR's COPY, CUT, PASTE): a block of cells kept packed, until power-off. Cells are
 * read in rising rows for each column (a cursor a column) */
#define ED_CLIP 2048
int ed_clip_copy(uint8_t pat, int r0, int r1, int t0, int t1);  /* 0 done, -1 too big */
int ed_clip_rows(void);                 /* 0: nothing copied */
int ed_clip_tracks(void);
int ed_clip_cell(int r, int t, rec_cell_t *c);          /* the block's cell (relative), 1 one there */

#endif
