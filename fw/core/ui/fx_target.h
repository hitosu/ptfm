/* SPDX-License-Identifier: GPL-3.0-only
 * Copyright (C) 2026 Vasily Kazantsev */
/* Whose FX setup the FX screen edits (specs/ui/screens/fx.md#the-fx-setup): in LOOPS, or wherever the
 * loops play, the loops' (their record's bytes 8-71); on DJ, or wherever the decks play, DJ's (its record's
 * bytes 8-71); elsewhere the playing song's, else the current song's; with no song at all, the keys' song's
 * (UI_KEYS_SONG: the firmware's own, never listed). Its record copied, changed, put into the edit memory; the
 * sound told. Also the mixer the MIXER page shows: a song's 16 tracks, or the loops'. */
#ifndef UI_FX_TARGET_H
#define UI_FX_TARGET_H
#include <stdint.h>

enum { FT_NONE, FT_SONG, FT_LOOPS, FT_DJ };
typedef struct {
    uint8_t what;                       /* FT_* */
    uint8_t kind, num, sub;             /* its record */
    uint8_t plays;                      /* it is what the player plays (or paused) now */
    char name[17];                      /* the song's name, `THE LOOPS`, `DJ` */
} fxt_t;

void fxt_from(const char *screen);      /* the screen FX was opened from (LOOPS, DJ keep theirs) */
int fxt_get(fxt_t *t);                  /* which setup; 0 none (no song) */
int fxt_setup(const fxt_t *t, uint8_t *s64);    /* its setup as stored (edits included); 0: a new song's */
int fxt_now(const fxt_t *t, uint8_t *s64);      /* what plays: the setup with the commands' changes (1), or as stored (0) */
/* changing it: the setup as stored -> fn changes it -> the record into the edit memory. 0 done, -1 refused
 * (the button's blink), -2 the edit memory full */
int fxt_edit(const fxt_t *t, void (*fn)(uint8_t *s64, void *arg), void *arg, int button);
/* a song's TEMPO and SWING (FX's GLO for the keys' song, which SONGS never shows): read (1 there), and set into the
 * edit memory, the player told (0 done, -1 refused, -2 the edit memory full) */
int fxt_time(const fxt_t *t, int *tempo, int *swing);
int fxt_set_time(const fxt_t *t, int tempo, int swing, int button);

/* the MIXER: columns (a song's tracks, the loops' loops), each its 6 bytes (LEVEL, PAN, DUCK, the sends) */
int fxt_unsaved(const fxt_t *t);        /* its setup differs from what is saved (REVERT listed) */
int fxt_revert(const fxt_t *t);         /* REVERT: the setup as saved (fxt_edit's result) */
int fxt_mix_cols(const fxt_t *t);
int fxt_mix_get(const fxt_t *t, int col, uint8_t *six);
int fxt_mix_set(const fxt_t *t, int col, int k, int v, int button);
int fxt_mix_track(const fxt_t *t, int col);     /* the column's track */
int fxt_mix_muted(const fxt_t *t, int col);
int fxt_tracks_used(const fxt_t *t);    /* the song's most tracks over its patterns (16 for the loops) */

#endif
