/* SPDX-License-Identifier: GPL-3.0-only
 * Copyright (C) 2026 Vasily Kazantsev */
/* DJ's record and stand (specs/files/dj.md): the setup, DJ's FX setup, the decks as they stand, kept as the
 * record's 136 bytes; and the decks in the player (dj.c: the main loop's side; player.h plays them). */
#ifndef PLAY_DJ_H
#define PLAY_DJ_H
#include <stdint.h>
#include "player.h"

#define DJ_REC_LEN 136
uint32_t dj_record_default(uint8_t *p);         /* a new store's record (files/dj.md, With no record yet): 136 */
const uint8_t *dj_fx_default(void);             /* its FX setup (64 B, in flash) */

extern uint8_t DJ_REC[DJ_REC_LEN];              /* the record as the stand is now */
#define DJ_FX (DJ_REC + 8)                      /* DJ's FX setup */
void dj_load(void);                             /* from the store (the edit memory first), else the default */
void dj_ensure(void);                           /* dj_load once */
uint32_t dj_loads(void);                        /* dj_load's runs so far: the stand reloaded (REVERT, a seed) when it
                                                 * changes, so what a screen kept of the old one goes */
uint8_t *dj_deck(int d);                        /* a deck's 24 bytes: 0 left (the main), 1 right */
enum {                                          /* a deck's bytes (files/dj.md, A deck) */
    DK_SONG = 0, DK_POS = 1, DK_ROW = 2, DK_CUE_POS = 3, DK_CUE_ROW = 4, DK_BPM = 5, DK_KEY = 7, DK_VOL = 8,
    DK_FILTER = 9, DK_FLAGS = 10, DK_LOOP = 11, DK_LOOP_POS = 12, DK_LOOP_ROW = 13, DK_LOOP_LEN = 14, DK_REPEAT = 15,
    DK_MUTES = 16, DK_SOLO = 18, DK_LFO = 19
};
enum { DF_FILTER_ON = 1, DF_REPEAT_PATTERN = 2, DF_AT_END = 4 };
#define DJ_NO_SONG 0xFF
#define DJ_VOL_OFF (-25)
int dj_deck_bpm10(int d);
void dj_deck_set_bpm10(int d, int bpm10);
int dj_xf(void);                                /* the crossfader, 0 (left) - 32 */
void dj_set_xf(int v);
int dj_sync(void);
int dj_sync_mult(void);                         /* 0 the main's BPM, 1 its half, 2 its double */
uint16_t dj_song_own10(uint8_t song);           /* a song's own tempo, BPM x 10 */
/* the LFOs (the LFO pads): their setups in the record (files/dj.md, An LFO's setup: byte 0 bit 0 on, bit 1
 * reversed; 1 shape; 2 range; 3 speed) */
enum { DJ_LFO_FADER, DJ_LFO_FILT_L, DJ_LFO_FILT_R, DJ_LFO_FX, DJ_NLFO };
uint8_t *dj_lfo_bytes(int i);
void dj_lfo_restart(int i);                     /* switched on: it sets off afresh (on the lead deck's next row) */

/* the decks in the player */
typedef struct {
    uint8_t song, playing, armed, ended, pos, pat, row, rows, tick, speed;
    uint16_t bpm10;
    uint32_t rows_played;
    uint8_t stop, stopped;                      /* a stop held (PL_STOP_*), it stands */
    uint8_t loop, loop_pos, loop_row, roll;     /* a loop (PL_DLOOP_*), where it starts, a ROLL */
    uint16_t loop_rows;
    uint32_t halves;                            /* the player's halves at the reading (0 from the stand) */
} dj_deck_state_t;
extern void (*dj_read_state)(player_state_t *s);   /* how the decks' state is read (the screen's frame), or none */
int dj_deck_state(int d, dj_deck_state_t *s);   /* 1 the player has the deck, else 0 (s from the stand) */
void dj_enter(void);                            /* DJ opens: what plays plays on (DJ apart till its first start) */
int dj_on(int on);                              /* the decks live in the player as the stand has them (or off): DJ's
                                                 * first start stops what plays, its sound cut; 0: the spare zone did
                                                 * not lend DJ's memory (lent already: never while DJ is its one
                                                 * borrower; the build keeps it big enough) */
int dj_live(void);
int dj_second_ok(void);                         /* the second deck has its memory (the spare zone's, borrowed as the
                                                 * decks go live, given back once DJ has ended) */
void dj_poll(void);                             /* the main loop, every pass: the second deck's memory given back
                                                 * once DJ has ended */
void dj_deck_load(int d, uint8_t song);
void dj_deck_play(int d, int on);               /* from where the stand has it */
void dj_deck_arm(int d);                        /* starts on the other deck's next bar line */
void dj_deck_place(int d, int pos, int row);
void dj_deck_hand(int d, int pos, int row, int ms);   /* HOLD's hand: that row heard (player_deck_hand) */
void dj_deck_tempo(int d, int bpm10);
void dj_deck_volume(int d, int db);
void dj_deck_filter(int d, int cut, int on);
void dj_filter_slide(int d, int on);            /* its FILTER key held: sliding (let go: there) */
void dj_deck_key(int d, int semis);
void dj_deck_mutes(int d, uint32_t mask);
void dj_stop(int d, int kind, int down, int stay);   /* a stop's pad (PL_STOP_*) down / up */
void dj_loop(int d, int pad, int pos, int row, int rows, int roll);   /* pad 0 off, 1 PATTERN, 2-4 bars, 5-8 free */
void dj_roll_end(int d);
void dj_swap(void);                             /* SWAP: the stand's two decks were exchanged (the caller swapped
                                                 * their records): the player's follow as they play, nothing reloaded */
int dj_crossed(const player_state_t *ps);       /* 1: the player's state has the decks the other way round (a SWAP
                                                 * not made there yet): its deck d is the stand's 1 - d */
void dj_mix(void);                              /* DJ's mix to the player: the crossfader, VOLUMEs, filters, KEY,
                                                 * the LFOs */
int dj_has_player(void);

#endif
