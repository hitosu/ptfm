/* SPDX-License-Identifier: GPL-3.0-only
 * Copyright (C) 2026 Vasily Kazantsev */
/* What the screens show of the records (specs/files/): names, a song's header and order, a pattern's
 * size, a cartridge's kind and slots. Read from the store each time (records are read in place). */
#ifndef UI_DATA_H
#define UI_DATA_H
#include <stdint.h>
#include "txt.h"

/* the engines, as the footer names them (specs/ui/frame.md) */
enum { ENG_NONE, ENG_DX7, ENG_JUNO, ENG_TB303, ENG_SAMPLER, ENG_FUSION, ENG_CHIP, ENG_COUNT };   /* (= the kinds) */
extern const txt_t ENG_SHORT[ENG_COUNT];   /* "", "DX7", "JUNO", "TB-303", "SAMPLE", "FUSION", "CHIP" (TX texts: T() them) */
extern const txt_t ENG_TYPE[ENG_COUNT];    /* "", "DX7", "JUNO-106", "TB-303", "SAMPLER", "FUSION", "CHIP" */
extern const uint8_t ENG_COL[ENG_COUNT];   /* their colours: light red, light cyan, light green, yellow, light magenta,
                                            * light blue */
extern const uint8_t ENG_SLOTS[ENG_COUNT]; /* slots a cartridge: 32, 128, 128, 64, 64, 128 */

/* a name of n characters, 6 bits each from bit `bit` of p (specs/files/README.md): into dst, trailing
 * spaces dropped; returns 0 if the first character is 63 (empty) */
int ui_name6(const uint8_t *p, uint32_t bit, int n, char *dst);
uint32_t ui_bits(const uint8_t *p, uint32_t bit, int n);    /* n bits from bit, LSB first */
void ui_put_bits(uint8_t *p, uint32_t bit, int n, uint32_t v);
void ui_put_name6(uint8_t *p, uint32_t bit, int n, const char *name);  /* padded with spaces; other characters: spaces */
int ui_free_pattern(void);              /* the first pattern number 00-EF without a pattern, or -1 */

/* songs */
typedef struct {
    char name[17];
    uint16_t tempo;                     /* TEMPO 3-600 */
    uint8_t ticks, rpb, swing, flags, loop_from, gvol;
    uint16_t npos;                      /* positions, 1-256 */
    const uint8_t *order;               /* npos pattern numbers */
} ui_song_t;
int ui_song(uint8_t n, ui_song_t *s);   /* 1 the song is there */
int ui_songs(uint8_t *nums, int max);   /* the songs there, in numbers' order (never the keys' song) */
/* the keys' song (FE): the firmware's own, never listed. With no song at all it is what the player holds and the FX
 * page edits, so the FM-1 plays as a synth from its keys alone: TEMPO 120, 6 ticks of 4 rows a beat, a new song's
 * FX setup; changed, kept as a built-in's change is (SAVE: a copy in the user area). The songs' numbers end at FD */
#define UI_KEYS_SONG 0xFE
#define UI_SONG_LAST 0xFD
void ui_keys_song_init(void);           /* made and given to the store (store_builtin_extra), at start */
int ui_song_bpm(const ui_song_t *s);    /* TEMPO * 24 / (TICKS A ROW * ROWS A BEAT), rounded */
uint32_t ui_song_secs(const ui_song_t *s);      /* its length played once */
/* the BPM x 10 DJ and PLAYBACK can play it at (specs/ui/screens/dj.md#tempo-and-sync): 3 to 600 BPM, where its ticks
 * stay within TEMPO 3 to 600 (PL_TEMPO_MIN, _MAX) at its TICKS A ROW and ROWS A BEAT */
void ui_tempo_range(const ui_song_t *s, int *lo10, int *hi10);

/* patterns */
/* the EDITOR's own pattern (FE; specs/ui/screens/editor.md#the-editors-own-pattern): the firmware's own, empty (8
 * tracks x 64 rows, EDIT STEP 1), never listed; changed, kept as a built-in's change is (SAVE: a copy in the user
 * area). FF stays "no pattern" in a song's order and a FUSION slot */
#define UI_EDITOR_PATTERN 0xFE
void ui_editor_pattern_init(void);      /* given to the store (store_builtin_extra), at start */
int ui_patterns(uint8_t *nums, int max);        /* the patterns there, in numbers' order (never the EDITOR's own) */
int ui_pattern_rows(uint8_t n);         /* its rows (1-128), 0 if it is not there */
int ui_pattern_tracks(uint8_t n);

/* cartridges and instruments */
int ui_cart_kind(uint8_t cart);         /* ENG_*, ENG_NONE if there is none */
int ui_cart_name(uint8_t cart, char *dst);
int ui_cart_slots(uint8_t cart);        /* slots stored */
int ui_carts(uint8_t *nums, int max);   /* every cartridge, in numbers' order */
int ui_inst_name(uint16_t ref, char *dst);      /* 1 the slot is used (dst its name), else 0 */
int ui_inst_level(uint16_t ref);        /* LEVEL in 0.5 dB, signed */

#endif
